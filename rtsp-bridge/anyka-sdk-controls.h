/* Small controls sharing the running ISP and ADC. No extra capture process. */
#include "ak_ai.h"
#include "ak_mt.h"

static int advanced_publish(FILE *);
static int advanced_set(const char *, const char *);
static void advanced_start(void);
static void advanced_restore(void);
static int native_settings[8], native_changed[8];
static const char *native_names[] = {"wdr", "power_hz", "anti_flicker", "exposure", "white_balance_auto", "white_balance_red", "white_balance_green", "white_balance_blue"};
static int masks[4][4], mask_style;
static struct vpss_isp_ae_attr exposure_baseline;
static int exposure_baseline_ready;

/* VI callbacks may precede the ISP output mask. Never analyse masked content
 * in the person classifier, regardless of whether the ISP already hid it. */
static void person_redact(unsigned char *pixels)
{
    for (int i = 0; i < 4; i++) if (masks[i][2] && masks[i][3]) {
        int left = (masks[i][0]*320/100)&~1;
        int right = ((masks[i][0]+masks[i][2])*320+99)/100;
        int top = (masks[i][1]*180/100)&~1;
        int bottom = ((masks[i][1]+masks[i][3])*180+99)/100;
        right = (right+1)&~1; bottom = (bottom+1)&~1;
        for (int y = top; y < bottom; y++) memset(pixels+y*320+left, 16, right-left);
        for (int y = top/2; y < bottom/2; y++) memset(pixels+320*180+y*320+left, 128, right-left);
    }
}

static int motion_cell_private(int x, int y)
{
    for (int i = 0; i < 4; i++) if (masks[i][2] && masks[i][3] && x >= masks[i][0] && x < masks[i][0]+masks[i][2] && y >= masks[i][1] && y < masks[i][1]+masks[i][3]) return 1;
    return 0;
}

static int parse_values(const char *text, int *values, int count)
{
    const char *p = text;
    for (int i = 0; i < count; i++) {
        const char *start = p;
        if (*p == '-') p++;
        const char *digits = p;
        while (*p >= '0' && *p <= '9') p++;
        if (p == digits || p-digits > 4 || (*digits == '0' && p-digits != 1) ||
            (*start == '-' && *digits == '0')) return 0;
        char *end;
        long value = strtol(start, &end, 10);
        if (end != p || value < -1000 || value > 1000) return 0;
        values[i] = (int)value;
        if (i+1 < count) { if (*p++ != ',') return 0; }
        else if (*p) return 0;
    }
    return 1;
}

static int valid_rectangle(const int *r, int allow_empty)
{
    if (allow_empty && !r[0] && !r[1] && !r[2] && !r[3]) return 1;
    return r[0] >= 0 && r[1] >= 0 && r[2] >= 1 && r[3] >= 1 &&
        r[0]+r[2] <= 100 && r[1]+r[3] <= 100;
}

static int apply_masks(void)
{
    struct vpss_mask_area_info area = {{{0}}};
    struct vpss_mask_color_info color = {0};
    color.color_type = mask_style ? VPSS_MASK_MOSAIC_VIDEO : VPSS_MASK_ORIGINAL_COLOR;
    color.mk_alpha = 255; color.masic_attr.mosai_size_hor = 4; color.masic_attr.mosai_size_vec = 4;
    for (int i = 0; i < 4; i++) {
        struct vpss_mask_area *a = &area.main_mask[i];
        a->enable = masks[i][2] > 0 && masks[i][3] > 0;
        /* Native mask coordinates are main-channel pixels. The SDK scales sub. */
        a->start_xpos = masks[i][0]*2304/100;
        a->end_xpos = (masks[i][0]+masks[i][2])*2304/100;
        a->start_ypos = masks[i][1]*1296/100;
        a->end_ypos = (masks[i][1]+masks[i][3])*1296/100;
    }
    return !ak_vpss_mask_set_color(0, &color) && !ak_vpss_mask_set_area(0, &area);
}

static int apply_native_setting(int index, int value)
{
    switch (index) {
    case 0: return value >= -50 && value <= 50 && !ak_vpss_effect_set(0, VPSS_EFFECT_WDR, value);
    case 1: return (value == 50 || value == 60) && !ak_vpss_effect_set(0, VPSS_POWER_HZ, value);
    case 2: return value >= 0 && value <= 100 && !ak_vpss_set_anti_flicker_strength(0, value);
    case 3: {
        if (value < -50 || value > 50) return 0;
        if (!exposure_baseline_ready) {
            if (ak_vpss_get_ae_attr(0, &exposure_baseline)) return 0;
            exposure_baseline_ready = 1;
        }
        struct vpss_isp_ae_attr ae = exposure_baseline;
        for (int i = 0; i < 16; i++) {
            int target = (int)ae.target_lumiance[i]+value;
            ae.target_lumiance[i] = target < 1 ? 1 : target > 255 ? 255 : target;
        }
        return !ak_vpss_set_ae_attr(0, &ae);
    }
    case 4: {
        if (value != 0 && value != 1) return 0;
        struct vpss_isp_wb_type_attr wb = {value ? VPSS_OP_TYPE_AUTO : VPSS_OP_TYPE_MANUAL};
        return !ak_vpss_set_wb_type(0, &wb);
    }
    default: {
        if (value < 64 || value > 1024) return 0;
        struct vpss_isp_mwb_attr wb;
        if (ak_vpss_get_mwb_attr(0, &wb)) return 0;
        if (index == 5) wb.r_gain = value;
        if (index == 6) wb.g_gain = value;
        if (index == 7) wb.b_gain = value;
        return !ak_vpss_set_mwb_attr(0, &wb);
    }
    }
}

static int sound_enabled, sound_sensitivity = 50, sound_ready, sound_detected;
static unsigned long sound_until;
static const char *sound_error = "";
static int sound_id = -1, sound_configured;
static int (*sound_register)(void);
static int (*sound_handle)(int, int *);
static int (*sound_attr)(int, struct ak_audio_vad_attr *);
static int (*sound_status)(int, struct ak_audio_vad_status_ex *);
static void *sound_library;

static int sound_configure(void)
{
    if (!sound_library) {
        sound_library = dlopen("libplat_audio_detect.so", RTLD_NOW | RTLD_GLOBAL);
        if (!sound_library) return 0;
        void *vad = dlopen("libakvad.so", RTLD_NOW | RTLD_GLOBAL);
        sound_register = vad ? dlsym(vad, "ak_vad_register") : NULL;
        sound_handle = dlsym(RTLD_DEFAULT, "ak_ai_get_handle_id");
        sound_attr = dlsym(RTLD_DEFAULT, "ak_ai_set_vad_attr");
        sound_status = dlsym(RTLD_DEFAULT, "ak_ai_get_vad_status_ex");
        if (!sound_register || !sound_handle || !sound_attr || !sound_status || sound_register()) return 0;
    }
    if (sound_handle(DEV_ADC, &sound_id)) return 0;
    struct ak_audio_vad_attr attr = {0};
    attr.enable = sound_enabled;
    attr.vad_thresh = (101-sound_sensitivity)*160; /* 0.005..0.49 full-scale */
    attr.vad_level = 0;
    attr.vad_type = AK_VAD_TYPE_PARAM(AK_VAD_DET_AFTER_ADC, AK_VAD_DET_SOUND_LEVEL, 0, 0);
    return !sound_attr(sound_id, &attr);
}

static void sound_tick(void)
{
    if (privacy_active()) { sound_detected = 0; sound_until = 0; return; }
    if (!sound_enabled || *sound_error) { sound_detected = 0; return; }
    if (!sound_configured) {
        if (!sound_configure()) { sound_error = "SDK sound initialization failed"; return; }
        sound_configured = 1;
    }
    struct ak_audio_vad_status_ex status = {0};
    status.vadType = AK_VAD_TYPE_PARAM(AK_VAD_DET_AFTER_ADC, AK_VAD_DET_SOUND_LEVEL, 0, 0);
    if (sound_status(sound_id, &status)) { sound_ready = 0; sound_error = "SDK sound status unavailable"; return; }
    sound_ready = 1;
    if (status.trigger) sound_until = monotonic_seconds()+3;
    sound_detected = sound_until > monotonic_seconds();
}

static int tracking_enabled, tracking_ready;
static unsigned int tracking_moves;
static const char *tracking_error = "";
static unsigned long tracking_last;
static void *tracking_handle;

static void tracking_tick(MRD_RECTANGLE *boxes, int count)
{
    if (!tracking_enabled || *tracking_error) return;
    if (access("/tmp/anyka-ptz-homed", F_OK)) { tracking_ready = 0; return; }
    if (!tracking_handle) {
        tracking_handle = ak_mt_init();
        if (!tracking_handle) { tracking_error = "SDK tracking initialization failed"; return; }
        ak_mt_set_fps(tracking_handle, 5);
        ak_mt_set_motion_region_params(tracking_handle, 24, 4, 300, 8, 6);
        ak_mt_set_max_decision_frames(tracking_handle, 3);
        ak_mt_set_field_angles(tracking_handle, 90, 60, 32, 24);
        ak_mt_set_rotation_range(tracking_handle, 2, 20, 2, 20);
        ak_mt_set_camera_rotation_factor(tracking_handle, -1);
        ak_mt_open_camera_rotation_switch_v(tracking_handle);
    }
    tracking_ready = 1;
    unsigned long now = monotonic_seconds();
    if (now-tracking_last < 6 || !count) return;
    FILE *manual = fopen("/tmp/anyka-ptz-manual", "r");
    unsigned long until = 0;
    if (manual) { if (fscanf(manual, "%lu", &until) != 1) until = now+30; fclose(manual); }
    if (now < until) return;
    int best = 0;
    for (int i = 1; i < count; i++) if (boxes[i].count > boxes[best].count) best = i;
    MRD_RECTANGLE *b = &boxes[best];
    MT_RECTANGLE rect = {{(b->left+b->right)/2, (b->top+b->bottom)/2}, b->right-b->left, b->bottom-b->top};
    MT_POINT angle = {0};
    int decision = ak_mt_tracking(tracking_handle, 24, &rect, &angle);
    /* The vendor reset-to-home decision is deliberately not applied. */
    if (decision != 1 || (!angle.px && !angle.py)) return;
    const char *helper = getenv("ANYKA_PTZ_HELPER");
    if (!helper || access("/tmp/anyka-ptz-homed", F_OK)) return;
    pid_t child = fork();
    if (child < 0) return;
    if (!child) {
        char *action = abs(angle.px) >= abs(angle.py) ? (angle.px > 0 ? "track-right" : "track-left") : (angle.py > 0 ? "track-down" : "track-up");
        char *args[] = {"/media/lib/ld-musl-arm.so.1", "--library-path", "/media/lib:/media/usr/lib", (char *)helper, action, NULL};
        char *env[] = {"PATH=/bin:/usr/bin", NULL};
        int output = open("/dev/null", O_WRONLY); if (output >= 0) { dup2(output, 1); close(output); }
        execve(args[0], args, env); _exit(127);
    }
    int outcome; pid_t waited;
    do { waited = waitpid(child, &outcome, 0); } while (waited < 0 && errno == EINTR);
    tracking_last = now;
    if (waited == child && WIFEXITED(outcome) && WEXITSTATUS(outcome) == 0) tracking_moves++;
    else { tracking_error = "PTZ tracking stopped by motor guard"; tracking_ready = 0; }
    motion_suppress(5);
    ak_mt_reset_tracking_param(tracking_handle);
}

static int advanced_set(const char *name, const char *text)
{
    int v[4];
    if (!strcmp(name, "motion_zone")) {
        if (!parse_values(text, v, 4) || !valid_rectangle(v, 0)) return 0;
        memcpy(motion_zone, v, sizeof(v)); motion_suppress(5); return 1;
    }
    if (!strncmp(name, "privacy_mask", 12) && name[12] >= '1' && name[12] <= '4' && !name[13]) {
        if (!parse_values(text, v, 4) || !valid_rectangle(v, 1)) return 0;
        int i = name[12]-'1', previous[4]; memcpy(previous, masks[i], sizeof(previous));
        memcpy(masks[i], v, sizeof(v));
        if (!apply_masks()) { memcpy(masks[i], previous, sizeof(previous)); apply_masks(); return 0; }
        motion_suppress(5); return 1;
    }
    if (!parse_values(text, v, 1)) return 0;
    if (!strcmp(name, "person_enabled")) return person_set(v[0]);
    if (!strcmp(name, "privacy_style") && (v[0] == 0 || v[0] == 1)) {
        int before = mask_style; mask_style = v[0];
        if (apply_masks()) return 1;
        mask_style = before; apply_masks(); return 0;
    }
    if (!strcmp(name, "sound_enabled") || !strcmp(name, "sound_sensitivity")) {
        int enabled = sound_enabled, sensitivity = sound_sensitivity;
        if (!strcmp(name, "sound_enabled")) { if (v[0] != 0 && v[0] != 1) return 0; sound_enabled = v[0]; }
        else { if (v[0] < 1 || v[0] > 100) return 0; sound_sensitivity = v[0]; }
        if (sound_enabled || enabled) {
            if (!sound_configure()) { sound_enabled = enabled; sound_sensitivity = sensitivity; return 0; }
            sound_configured = sound_enabled;
        }
        sound_error = ""; sound_until = 0; sound_detected = 0; return 1;
    }
    if (!strcmp(name, "tracking_enabled") && (v[0] == 0 || v[0] == 1)) {
        if (v[0] && !motion_enabled) return 0;
        tracking_enabled = v[0]; tracking_error = ""; tracking_last = monotonic_seconds();
        if (tracking_handle) ak_mt_reset_tracking_param(tracking_handle);
        return 1;
    }
    for (int i = 0; i < 8; i++) if (!strcmp(name, native_names[i])) {
        if (!apply_native_setting(i, v[0])) return 0;
        native_settings[i] = v[0]; native_changed[i] = 1; motion_suppress(5); return 1;
    }
    return 0;
}

static int advanced_publish(FILE *output)
{
    int failed = 0;
    for (int i = 0; i < 8; i++) failed |= fprintf(output, ",\"%s\":%d", native_names[i], native_settings[i]) < 0;
    failed |= fprintf(output, ",\"privacy_style\":%d,\"privacy_masks\":[", mask_style) < 0;
    for (int i = 0; i < 4; i++) failed |= fprintf(output, "%s[%d,%d,%d,%d]", i ? "," : "", masks[i][0], masks[i][1], masks[i][2], masks[i][3]) < 0;
    failed |= fprintf(output, "],\"sound_enabled\":%s,\"sound_sensitivity\":%d,\"sound_ready\":%s,\"sound_detected\":%s,\"sound_error\":\"%s\",\"tracking_enabled\":%s,\"tracking_ready\":%s,\"tracking_moves\":%u,\"tracking_error\":\"%s\"",
        sound_enabled ? "true" : "false", sound_sensitivity, sound_ready ? "true" : "false", !privacy_active() && sound_detected ? "true" : "false", sound_error,
        tracking_enabled ? "true" : "false", tracking_ready ? "true" : "false", tracking_moves, tracking_error) < 0;
    return failed;
}

static void advanced_restore(void)
{
    exposure_baseline_ready = 0;
    for (int i = 0; i < 8; i++) if (native_changed[i]) apply_native_setting(i, native_settings[i]);
    apply_masks();
}

static void advanced_start(void)
{
    ak_vpss_effect_get(0, VPSS_EFFECT_WDR, &native_settings[0]);
    ak_vpss_effect_get(0, VPSS_POWER_HZ, &native_settings[1]);
    ak_vpss_get_anti_flicker_strength(0, &native_settings[2]);
    struct vpss_isp_wb_type_attr wb = {0};
    if (!ak_vpss_get_wb_type(0, &wb)) native_settings[4] = wb.wb_type == VPSS_OP_TYPE_AUTO;
    struct vpss_isp_mwb_attr mwb = {0};
    if (!ak_vpss_get_mwb_attr(0, &mwb)) { native_settings[5] = mwb.r_gain; native_settings[6] = mwb.g_gain; native_settings[7] = mwb.b_gain; }
    const char *directory = getenv("ANYKA_MOTION_DIR");
    if (!directory) return;
    const char *extras[] = {"motion_zone", "privacy_style", "privacy_mask1", "privacy_mask2", "privacy_mask3", "privacy_mask4", "sound_enabled", "sound_sensitivity", "tracking_enabled", "person_enabled"};
    for (int i = 0; i < 18; i++) {
        const char *name = i < 8 ? native_names[i] : extras[i-8];
        char path[512], value[64];
        if (snprintf(path, sizeof(path), "%s/native-%s.conf", directory, name) >= (int)sizeof(path)) continue;
        FILE *input = fopen(path, "r");
        if (input) { if (fgets(value, sizeof(value), input)) { value[strcspn(value, "\r\n")] = 0; advanced_set(name, value); } fclose(input); }
    }
    for (int i = 0; i < 7; i++) {
        const char *name = i < 5 ? image_names[i] : i == 5 ? "flip_vertical" : "flip_horizontal";
        char path[512], text[64]; int value;
        if (snprintf(path, sizeof(path), "%s/native-%s.conf", directory, name) >= (int)sizeof(path)) continue;
        FILE *input = fopen(path, "r");
        if (!input) continue;
        int valid = fgets(text, sizeof(text), input) != NULL; fclose(input);
        if (!valid) continue;
        text[strcspn(text, "\r\n")] = 0;
        if (!parse_values(text, &value, 1)) continue;
		if (i < 5 && value >= -50 && value <= 50 && set_image_effect(i, value)) {
            image_overrides[i] = value; image_changed[i] = 1;
        } else if (i >= 5 && (value == 0 || value == 1)) {
            int vertical = 0, horizontal = 0;
            if (ak_vpss_get_flip_mirror(0, &vertical, &horizontal)) continue;
            if (i == 5) vertical = value; else horizontal = value;
            if (!ak_vpss_switch_flip_mirror(0, vertical, horizontal)) {
                orientation_changed = 1; vertical_override = vertical; horizontal_override = horizontal;
            }
        }
    }
}
