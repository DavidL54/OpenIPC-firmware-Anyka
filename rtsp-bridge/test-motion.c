#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/syscall.h>
#include <unistd.h>
#include "ak_common.h"
#include "ak_thread.h"
#include "ak_vi.h"
#include "ak_vpss.h"
#ifndef SYS_clock_gettime32
#define SYS_clock_gettime32 SYS_clock_gettime
#endif
static int monitor_running;
static unsigned long fake_now;
static unsigned long monotonic_seconds(void) { return fake_now; }
#include "anyka-motion.h"

static int reject_size;
static int set_size(void *handle, int minimum, int maximum)
{
    assert(handle && minimum >= 4 && minimum <= 50 && maximum == 300);
    return reject_size;
}

int main(void)
{
    motion_observe(1, 100);
    assert(!motion_detected && motion_events == 0);
    motion_observe(0, 100);
    motion_observe(1, 101);
    assert(!motion_detected); /* An isolated positive must not trigger. */
    motion_observe(1, 102);
    assert(motion_detected && motion_events == 1);
    motion_observe(0, 106);
    assert(motion_detected);
    motion_observe(0, 107);
    assert(!motion_detected); /* Exact hold expiry. */
    motion_observe(2, 108); motion_observe(2, 109);
    assert(motion_detected && motion_events == 2);
    fake_now = 110; motion_suppress(5);
    assert(!motion_detected && !motion_positive && motion_quiet_until == 115);
    motion_suppress(10);
    fake_now = 111; motion_suppress(5);
    assert(motion_quiet_until == 120); /* Scene changes must not shorten warmup. */
    assert(!motion_set("motion_enabled", 2));
    assert(!motion_set("motion_sensitivity", 0));
    assert(!motion_set("motion_sensitivity", 101));
    assert(!motion_set("invalid", 50));
    assert(motion_min_size[50] == 12 && motion_min_size[100] == 4);
    motion_handle = (void *)1; mrd_size = set_size;
    for (int i = 1; i <= 100; i++) assert(motion_set("motion_sensitivity", i));
    reject_size = 1;
    assert(!motion_set("motion_sensitivity", 50) && motion_sensitivity == 100);
    assert(motion_set("motion_enabled", 0) && !motion_enabled);
    puts("Motion debounce, hold expiry, PTZ suppression and sensitivity boundaries passed");
}
