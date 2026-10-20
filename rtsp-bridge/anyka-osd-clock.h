/* SDK OSD calls stay inside the uClibc video process. No additional VI reader. */
#include "ak_osd.h"
#define CLOCK_MAIN_WIDTH 580
#define CLOCK_SUB_WIDTH 236

static int clock_requested = 1, clock_ready, clock_initialized, clock_failed;
static int clock_canvas[2];
static pthread_mutex_t clock_mutex;
static int clock_mutex_ready, clock_thread_started;
static int clock_running;
static ak_pthread_t clock_thread;
static unsigned long clock_update_ms;
static unsigned long clock_milliseconds(void)
{
    struct { long seconds, nanoseconds; } t;
    if (syscall(SYS_clock_gettime32, CLOCK_MONOTONIC, &t)) return 0;
    return (unsigned long)t.seconds * 1000UL + (unsigned long)t.nanoseconds / 1000000UL;
}
static int clock_visible;
static const char *clock_error = "";
static char clock_text[32] = "";
static void *clock_library;
static int (*osd_init)(struct osd_attr *);
static int (*osd_create)(struct osd_canvas_info *);
static int (*osd_draw)(struct osd_drawing_info *);
static int (*osd_commit)(int, int);
static int (*osd_enable)(int, int, int);
static int (*osd_uninit)(void);
/* uClibc time_t is 32-bit; the cross compiler's musl time_t is 64-bit. */
struct sdk_tm { int sec, min, hour, mday, mon, year, wday, yday, isdst; long gmtoff; const char *zone; };
static struct sdk_tm *(*sdk_localtime)(const long *, struct sdk_tm *);
static unsigned char clock_bitmap[CLOCK_MAIN_WIDTH * 44 / 2];
static unsigned int clock_colors[16] = {0, 0xffffffff, 0xff000000};
static const unsigned char clock_glyphs[13][7] = {
    {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},
    {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30},
    {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8},
    {14,17,17,14,17,17,14}, {14,17,17,15,1,1,14},
    {0,4,4,0,4,4,0}, {1,2,2,4,8,8,16}, {0,0,0,0,0,0,0}
};

static void clock_pixel(int width, int height, int x, int y, int color)
{
    if (x < 0 || y < 0 || x >= width || y >= height) return;
    unsigned int index = (unsigned int)(y * width + x);
    int shift = (index & 1) * 4;
    clock_bitmap[index / 2] = (clock_bitmap[index / 2] & ~(15 << shift)) | (color << shift);
}

static void clock_render(const char *text, int scale, int width, int height)
{
    memset(clock_bitmap, 0, sizeof(clock_bitmap));
    /* Black outline first, white digits second; transparent background. */
    for (int pass = 0; pass < 2; pass++) {
        int outline = pass ? 0 : 1;
        for (int c = 0; c < 19 && text[c]; c++) {
            int glyph = text[c] == ':' ? 10 : text[c] == '/' ? 11 : text[c] == ' ' ? 12 : text[c] - '0';
            if (glyph < 0 || glyph > 12) continue;
            for (int y = 0; y < 7; y++) for (int x = 0; x < 5; x++) {
                if (!(clock_glyphs[glyph][y] & (1 << (4 - x)))) continue;
                for (int dy = -outline; dy < scale + outline; dy++)
                    for (int dx = -outline; dx < scale + outline; dx++)
                        clock_pixel(width, height, 4 + c * 6 * scale + x * scale + dx,
                            4 + y * scale + dy, pass ? 1 : 2);
            }
        }
    }
}

static int clock_load(void)
{
    clock_library = dlopen("libmpi_osd.so", RTLD_NOW | RTLD_LOCAL);
    if (!clock_library) return -1;
#define LOAD_OSD(variable, symbol) do { variable = dlsym(clock_library, symbol); if (!variable) return -1; } while (0)
    LOAD_OSD(osd_init, "ak_osd_init");
    LOAD_OSD(osd_create, "ak_osd_create_canvas");
    LOAD_OSD(osd_draw, "ak_osd_draw_bitmap_to_canvas");
    LOAD_OSD(osd_commit, "ak_osd_set_region_to_vi");
    LOAD_OSD(osd_enable, "ak_osd_set_canvas_enable");
    LOAD_OSD(osd_uninit, "ak_osd_uninit");
#undef LOAD_OSD
    sdk_localtime = dlsym(RTLD_DEFAULT, "localtime_r");
    if (!sdk_localtime) return -1;
    struct osd_attr attr = {AK_OSD_COLOR_FMT_RGB, clock_colors, 1, AK_OSD_TYPE_COLOR, 4};
    if (osd_init(&attr)) return -1;
    clock_initialized = 1;
    for (int ch = 0; ch < 2; ch++) {
        struct osd_canvas_info canvas = {0};
        canvas.channel_id = ch;
        canvas.pos_in_yuv.x = ch ? 8 : 16;
        canvas.pos_in_yuv.y = ch ? 8 : 16;
        canvas.region_size_width = ch ? CLOCK_SUB_WIDTH : CLOCK_MAIN_WIDTH;
        canvas.region_size_height = ch ? 24 : 44;
        if (osd_create(&canvas)) return -1;
        clock_canvas[ch] = 1;
        if (osd_enable(ch, 0, 0)) return -1;
    }
    return 0;
}

static int clock_hide(void)
{
    clock_ready = 0;
    if (!clock_visible) return 0;
    int failed = 0;
    /* This ISP keeps the last bitmap when only its enable flag is cleared. */
    memset(clock_bitmap, 0, sizeof(clock_bitmap));
    for (int ch = 0; ch < 2; ch++) if (clock_canvas[ch]) {
        struct osd_drawing_info draw = {0};
        draw.channel_id = ch;
        draw.buffer = clock_bitmap;
        draw.bitmap_size_width = ch ? CLOCK_SUB_WIDTH : CLOCK_MAIN_WIDTH;
        draw.bitmap_size_height = ch ? 24 : 44;
        draw.buffer_size = draw.bitmap_size_width * draw.bitmap_size_height / 2;
        failed |= osd_draw(&draw) != 0;
        failed |= osd_commit(ch, 0) != 0;
        failed |= osd_enable(ch, 0, 0) != 0;
    }
    if (!failed) clock_visible = 0;
    return failed ? -1 : 0;
}

static void clock_tick(void)
{
    struct { long seconds; long nanoseconds; } now;
    if (!clock_requested) {
        clock_failed = clock_hide() != 0;
        clock_error = clock_failed ? "SDK OSD clear failed" : "";
        return;
    }
    if (clock_failed) return;
    if (syscall(SYS_clock_gettime32, CLOCK_REALTIME, &now) || now.seconds < 1704067200) {
        clock_hide(); clock_error = "Waiting for clock synchronization"; return;
    }
    if (!clock_initialized && clock_load()) {
        clock_hide(); clock_failed = 1; clock_error = "SDK OSD initialization failed";
        fprintf(stderr, "osd-clock: initialization failed\n"); return;
    }
    struct sdk_tm local;
    if (!sdk_localtime(&now.seconds, &local)) { clock_hide(); clock_error = "Clock conversion failed"; return; }
    if (local.year < 124 || local.year > 200 || local.mon < 0 || local.mon > 11 ||
        local.mday < 1 || local.mday > 31 || local.hour < 0 || local.hour > 23 ||
        local.min < 0 || local.min > 59 || local.sec < 0 || local.sec > 60) {
        clock_hide(); clock_error = "Invalid clock date"; return;
    }
    snprintf(clock_text, sizeof(clock_text), "%02d/%02d/%04d %02d:%02d:%02d",
        local.mday, local.mon + 1, local.year + 1900, local.hour, local.min, local.sec);
    for (int ch = 0; ch < 2; ch++) {
        int width = ch ? CLOCK_SUB_WIDTH : CLOCK_MAIN_WIDTH, height = ch ? 24 : 44;
        clock_render(clock_text, ch ? 2 : 5, width, height);
        struct osd_drawing_info draw = {0};
        draw.channel_id = ch;
        draw.buffer = clock_bitmap;
        draw.buffer_size = width * height / 2;
        draw.bitmap_size_width = width;
        draw.bitmap_size_height = height;
        clock_visible = 1;
        if (osd_draw(&draw) || osd_commit(ch, 0) || (!clock_ready && osd_enable(ch, 0, 1))) {
            clock_hide(); clock_failed = 1; clock_error = "SDK OSD update failed";
            fprintf(stderr, "osd-clock: channel %d update failed\n", ch); return;
        }
    }
    clock_ready = 1; clock_error = "";
}

/* All OSD buffers and SDK calls have one owner at a time. Initialize the
 * mutex at runtime with uClibc, never with a musl static initializer. */
static void clock_lock(void) { if (clock_mutex_ready) pthread_mutex_lock(&clock_mutex); }
static void clock_unlock(void) { if (clock_mutex_ready) pthread_mutex_unlock(&clock_mutex); }

static int clock_set_enabled(int enabled)
{
    clock_lock();
    clock_requested = enabled;
    if (clock_thread_started) clock_tick();
    int ok = !clock_failed;
    clock_unlock();
    return ok;
}

static int clock_publish(FILE *output)
{
    clock_lock();
    int failed = fprintf(output,
        ",\"osd_clock\":%s,\"osd_clock_ready\":%s,\"osd_clock_time\":\"%s\",\"osd_clock_error\":\"%s\",\"osd_clock_update_ms\":%lu",
        clock_requested ? "true" : "false", clock_ready ? "true" : "false",
        clock_text, clock_error, clock_update_ms) < 0;
    clock_unlock();
    return failed;
}

static void *clock_worker(void *unused)
{
    (void)unused;
    while (__atomic_load_n(&clock_running, __ATOMIC_ACQUIRE)) {
        clock_lock();
        unsigned long started = clock_milliseconds();
        clock_tick();
        clock_update_ms = clock_milliseconds() - started;
        /* Small RAM-only witness, independent of the slower ISP status poll. */
        FILE *out = fopen("/tmp/anyka-osd-clock.json.tmp", "w");
        if (out) {
            int bad = fputs("{", out) == EOF;
            /* Already holding clock_mutex. */
            bad |= fprintf(out, "\"ready\":%s,\"time\":\"%s\",\"update_ms\":%lu}\n",
                clock_ready ? "true" : "false", clock_text, clock_update_ms) < 0;
            if (fclose(out)) bad = 1;
            if (!bad) rename("/tmp/anyka-osd-clock.json.tmp", "/tmp/anyka-osd-clock.json");
        }
        clock_unlock();
        struct { long seconds, nanoseconds; } now;
        unsigned int delay = 1000;
        if (!syscall(SYS_clock_gettime32, CLOCK_REALTIME, &now))
            delay = 1000 - (unsigned int)now.nanoseconds / 1000000U;
        if (delay < 20) delay = 20;
        while (delay && __atomic_load_n(&clock_running, __ATOMIC_ACQUIRE)) {
            unsigned int part = delay > 100 ? 100 : delay;
            ak_sleep_ms(part); delay -= part;
        }
    }
    return NULL;
}

static void clock_start(void)
{
    const char *path = getenv("ANYKA_OSD_CONFIG");
    FILE *input = path ? fopen(path, "r") : NULL;
    if (input) { int value = fgetc(input); if (value == '0') clock_requested = 0; fclose(input); }
    if (pthread_mutex_init(&clock_mutex, NULL)) {
        clock_failed = 1; clock_error = "OSD mutex initialization failed"; return;
    }
    clock_mutex_ready = 1;
    __atomic_store_n(&clock_running, 1, __ATOMIC_RELEASE);
    clock_thread_started = !ak_thread_create(&clock_thread, clock_worker, NULL, 100 * 1024, -1);
    if (!clock_thread_started) {
        __atomic_store_n(&clock_running, 0, __ATOMIC_RELEASE);
        clock_failed = 1; clock_error = "OSD thread initialization failed";
    }
}

static void clock_stop(void)
{
    __atomic_store_n(&clock_running, 0, __ATOMIC_RELEASE);
    if (clock_thread_started) ak_thread_join(clock_thread);
    clock_thread_started = 0;
    clock_lock();
    clock_hide();
    if (clock_initialized) osd_uninit();
    if (clock_library) dlclose(clock_library);
    clock_library = NULL; clock_initialized = 0; clock_failed = 0;
    clock_canvas[0] = clock_canvas[1] = 0;
    clock_unlock();
    if (clock_mutex_ready) pthread_mutex_destroy(&clock_mutex);
    clock_mutex_ready = 0;
    unlink("/tmp/anyka-osd-clock.json");
}
