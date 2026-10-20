/* musl-only clock compatibility for the short-lived HTTPS helper.
 * The stock 4.4 kernel's time64 probing reports success without valid output.
 * Use its working native 32-bit clock ABI and expand into musl timespec.
 * Never preload this musl library into the proprietary uClibc SDK. */
#define _GNU_SOURCE
#include <time.h>
#include <sys/syscall.h>
#include <unistd.h>

int clock_gettime(clockid_t clock,struct timespec *result)
{
    struct {long seconds,nanoseconds;} native;
    long error=syscall(SYS_clock_gettime32,clock,&native);
    if(error) return -1;
    result->tv_sec=native.seconds;result->tv_nsec=native.nanoseconds;return 0;
}
