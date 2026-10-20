/* The documented SDK ISP statistics and region detector, in the uClibc process. */
#include "ak_mrd.h"
static int motion_enabled = 1, motion_sensitivity = 50, motion_ready;
static int motion_detected, motion_suspended = 1, motion_regions, motion_peak;
static unsigned int motion_events, motion_positive;
static unsigned long motion_last_event, motion_until, motion_quiet_until;
static void *motion_library, *motion_handle;
static int motion_rows, motion_night, motion_ptz_lock = -1;
static int motion_zone[4] = {0, 0, 100, 100}; /* x, y, width, height, percent */
static MRD_RECTANGLE motion_boxes[MAX_CAP];
static int motion_box_count;
static void tracking_tick(MRD_RECTANGLE *, int);
static void sound_tick(void);
static int motion_cell_private(int, int);
static const char *motion_error = "";
static void *(*mrd_init)(void);
static int (*mrd_destroy)(void *);
static int (*mrd_filters)(void *, unsigned short, unsigned short);
static int (*mrd_size)(void *, int, int);
static int (*mrd_mode)(void *, unsigned char);
static int (*mrd_floating)(void *, int, int, int, int, int, int);
static int (*mrd_detect)(void *, unsigned short [][MD_SIZE], int, MRD_RECTANGLE *);
static int privacy_active(void) { return access("/tmp/anyka-privacy-mode", F_OK) == 0; }
/* Same sensitivity-to-region-size curve as platform/sample/md/ak_md.c. */
static const unsigned char motion_min_size[101] = {
    50,49,48,47,46,45,44,43,42,41,40,39,38,37,36,35,34,33,32,31,
    30,29,28,27,27,26,25,24,24,23,22,21,21,20,19,18,18,17,16,15,
    14,14,13,13,12,12,12,12,12,12,12,12,11,11,11,11,11,11,11,11,
    10,10,10,10,9,9,9,9,9,9,8,8,8,8,8,8,8,8,7,7,7,7,6,6,6,6,
    6,6,6,6,5,5,5,5,4,4,4,4,4,4,4
};

static void motion_suppress(unsigned int seconds)
{
    unsigned long until = monotonic_seconds() + seconds;
    if (until > motion_quiet_until) motion_quiet_until = until;
    motion_positive = 0; motion_until = 0; motion_detected = 0;
    motion_box_count = 0;
}

static int motion_scene(int night)
{
    motion_night = night;
    motion_suppress(5);
    if (!motion_handle) return 0;
    return mrd_mode(motion_handle, night ? 0 : 1) ||
        (night ? mrd_floating(motion_handle, 4, 2, 4, 9, 2, 3) :
            mrd_floating(motion_handle, 3, 2, 4, 36, 2, 3));
}

static int motion_load(void)
{
    /* GP5 validated sensor mode is 2304x1296: 24 rows, less than 3.5 MP.
     * The SDK sensor-crop/main EX getters block this stock encoder; never call them here. */
    motion_rows = VPSS_MD_DIMENSION_V_MAX;
    fprintf(stderr, "motion: validated GP5 sensor grid rows=%d\n", motion_rows);
    motion_library = dlopen("libak_mrd.so", RTLD_NOW | RTLD_LOCAL);
    if (!motion_library) return -1;
#define LOAD_MRD(variable, name) do { variable = dlsym(motion_library, name); if (!variable) return -1; } while (0)
    LOAD_MRD(mrd_init, "ak_mrd_init");
    LOAD_MRD(mrd_destroy, "ak_mrd_destroy");
    LOAD_MRD(mrd_filters, "ak_mrd_set_filters");
    LOAD_MRD(mrd_size, "ak_mrd_set_motion_region_size");
    LOAD_MRD(mrd_mode, "ak_mrd_set_mode");
    LOAD_MRD(mrd_floating, "ak_mrd_set_floating_wadding_params");
    LOAD_MRD(mrd_detect, "ak_mrd");
#undef LOAD_MRD
    motion_handle = mrd_init();
    if (!motion_handle) return -1;
    if (mrd_filters(motion_handle, 10000, 5000) ||
        mrd_size(motion_handle, motion_min_size[motion_sensitivity], 300) || motion_scene(motion_night)) return -1;
    fprintf(stderr, "motion: SDK region detector initialized\n");
    return 0;
}

static void motion_observe(int regions, unsigned long now)
{
    motion_regions = regions;
    if (regions > 0) {
        if (motion_positive < 2) motion_positive++;
        if (motion_positive >= 2) {
            if (!motion_detected) motion_events++;
            motion_until = now + 5;
            struct { long seconds; long nanoseconds; } wall;
            if (!syscall(SYS_clock_gettime32, CLOCK_REALTIME, &wall) && wall.seconds >= 1704067200)
                motion_last_event = (unsigned long)wall.seconds;
        }
    } else motion_positive = 0;
    motion_detected = motion_until > now;
}

static void motion_tick(void)
{
	if (privacy_active()) {
		motion_suppress(5); motion_suspended = 1; motion_regions = 0; motion_peak = 0;
		return;
	}
    if (!motion_enabled) { motion_detected = 0; motion_positive = 0; motion_until = 0; return; }
    if (*motion_error) return; /* Fail closed without repeatedly allocating or logging. */
    if (!motion_handle && motion_load()) { motion_error = "SDK motion initialization failed"; return; }
    unsigned long now = monotonic_seconds();
    int moving = motion_ptz_lock < 0 || access("/tmp/anyka-ptz-homed", F_OK) ||
        flock(motion_ptz_lock, LOCK_SH | LOCK_NB);
    if (!moving) flock(motion_ptz_lock, LOCK_UN);
    if (moving) motion_suppress(5);
    motion_suspended = moving || now < motion_quiet_until;
    if (motion_suspended) return;
    struct vpss_md_info md = {{{0}}};
    MRD_RECTANGLE boxes[MAX_CAP] = {{0}};
    if (!motion_ready) fprintf(stderr, "motion: reading first ISP statistics\n");
    if (ak_vpss_md_get_stat(0, &md)) {
        motion_ready = 0; motion_detected = 0; motion_error = "ISP motion statistics unavailable"; return;
    }
    motion_peak = 0;
    for (int y = 0; y < motion_rows; y++) for (int x = 0; x < MD_SIZE; x++) {
        /* Gate the native ISP grid before MRD, as the SDK MD area filter does.
         * A cell is included when its center is inside the selected rectangle. */
        int px = (2*x+1)*100/(2*MD_SIZE), py = (2*y+1)*100/(2*motion_rows);
        if (px < motion_zone[0] || px >= motion_zone[0]+motion_zone[2] ||
            py < motion_zone[1] || py >= motion_zone[1]+motion_zone[3] || motion_cell_private(px, py)) md.stat[y][x] = 0;
        if (md.stat[y][x] > motion_peak) motion_peak = md.stat[y][x];
    }
    int regions = mrd_detect(motion_handle, md.stat, motion_rows, boxes);
    if (regions < 0 || regions > MAX_CAP) {
        motion_ready = 0; motion_detected = 0; motion_error = "SDK motion analysis failed"; return;
    }
    if (!motion_ready) fprintf(stderr, "motion: ISP statistics ready, peak=%d regions=%d\n", motion_peak, regions);
    motion_ready = 1;
    memcpy(motion_boxes, boxes, sizeof(boxes)); motion_box_count = regions;
    motion_observe(regions, now);
    tracking_tick(boxes, regions);
}

static int motion_set(const char *operation, int value)
{
    if (!strcmp(operation, "motion_enabled") && (value == 0 || value == 1)) {
        motion_enabled = value;
        motion_suppress(5);
        return 1;
    }
    if (!strcmp(operation, "motion_sensitivity") && value >= 1 && value <= 100) {
        if (motion_handle && mrd_size(motion_handle, motion_min_size[value], 300)) return 0;
        motion_sensitivity = value;
        motion_suppress(5);
        return 1;
    }
    return 0;
}

static void motion_start(void)
{
    const char *directory = getenv("ANYKA_MOTION_DIR");
    if (directory) {
        const char *names[] = {"motion-enabled.conf", "motion-sensitivity.conf"};
        for (int i = 0; i < 2; i++) {
            char path[512];
            if (snprintf(path, sizeof(path), "%s/%s", directory, names[i]) >= (int)sizeof(path)) continue;
            FILE *input = fopen(path, "r");
            int value;
            if (input) {
                if (fscanf(input, "%d", &value) == 1) motion_set(i ? "motion_sensitivity" : "motion_enabled", value);
                fclose(input);
            }
        }
    }
    motion_ptz_lock = open("/tmp/anyka-ptz.lock", O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    motion_suppress(10);
}

static int motion_publish(FILE *output)
{
	int private = privacy_active();
	int failed = fprintf(output, ",\"privacy_mode\":%s,\"motion_enabled\":%s,\"motion_sensitivity\":%d,\"motion_detected\":%s,\"motion_ready\":%s,\"motion_suspended\":%s,\"motion_regions\":%d,\"motion_peak\":%d,\"motion_events\":%u,\"motion_last_event\":%lu,\"motion_error\":\"%s\"",
		private ? "true" : "false", motion_enabled ? "true" : "false", motion_sensitivity, !private && motion_detected ? "true" : "false",
        motion_ready ? "true" : "false", motion_suspended ? "true" : "false", motion_regions,
        motion_peak, motion_events, motion_last_event, motion_error) < 0;
    failed |= fprintf(output, ",\"motion_zone\":[%d,%d,%d,%d],\"motion_boxes\":[",
        motion_zone[0], motion_zone[1], motion_zone[2], motion_zone[3]) < 0;
    for (int i = 0; i < motion_box_count; i++) {
        MRD_RECTANGLE *b = &motion_boxes[i];
        failed |= fprintf(output, "%s[%d,%d,%d,%d]", i ? "," : "",
            b->left*100/32, b->top*100/24, b->right*100/32, b->bottom*100/24) < 0;
    }
    failed |= fputs("]", output) == EOF;
    return failed;
}

static void motion_wait(void)
{
    for (int i = 0; i < 5 && monitor_running; i++) { motion_tick(); sound_tick(); ak_sleep_ms(200); }
}

static void motion_stop(void)
{
    if (motion_handle) mrd_destroy(motion_handle);
    if (motion_library) dlclose(motion_library);
    if (motion_ptz_lock >= 0) close(motion_ptz_lock);
    motion_handle = motion_library = NULL; motion_ptz_lock = -1;
}
