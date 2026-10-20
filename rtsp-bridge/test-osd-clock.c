#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>
#include "ak_common.h"
#ifndef SYS_clock_gettime32
#define SYS_clock_gettime32 SYS_clock_gettime
#endif
#include "anyka-osd-clock.h"

static int operation, fail_commit;
static int draw_blank(struct osd_drawing_info *draw)
{
    assert(operation++ % 3 == 0);
    assert(draw->buffer_size <= (int)sizeof(clock_bitmap));
    for (int i = 0; i < draw->buffer_size; i++) assert(draw->buffer[i] == 0);
    return 0;
}
static int commit_blank(int ch, int region)
{
    (void)ch; assert(region == 0); assert(operation++ % 3 == 1);
    return fail_commit;
}
static int disable_canvas(int ch, int region, int enable)
{
    (void)ch; assert(region == 0 && enable == 0); assert(operation++ % 3 == 2);
    return 0;
}
int main(void)
{
    /* Exercise both canvas sizes with every digit, including edge clipping. */
    for (int hour = 0; hour < 24; hour++) for (int minute = 0; minute < 60; minute++) {
        char text[64]; snprintf(text, sizeof(text), "09/10/2026 %02d:%02d:59", hour, minute);
        for (int ch = 0; ch < 2; ch++) {
            int width = ch ? CLOCK_SUB_WIDTH : CLOCK_MAIN_WIDTH, height = ch ? 24 : 44;
            clock_render(text, ch ? 2 : 5, width, height);
            int white = 0, outline = 0;
            for (int i = 0; i < width * height; i++) {
                int pixel = (clock_bitmap[i / 2] >> ((i & 1) * 4)) & 15;
                assert(pixel <= 2); white += pixel == 1; outline += pixel == 2;
                if (i / width == 0 || i % width == 0) assert(pixel == 0);
            }
            assert(white > 100 && outline > 100);
        }
    }
    osd_draw = draw_blank; osd_commit = commit_blank; osd_enable = disable_canvas;
    clock_canvas[0] = clock_canvas[1] = clock_visible = clock_ready = 1;
    assert(clock_hide() == 0 && operation == 6 && !clock_ready && !clock_visible);
    assert(clock_hide() == 0 && operation == 6); /* No repeated ISP calls while hidden. */
    clock_visible = 1; fail_commit = 1;
    assert(clock_hide() == -1 && clock_visible); /* Keep retrying a failed clear. */
    puts("OSD bitmap bounds, transparent clear order and failure reporting passed");
}
