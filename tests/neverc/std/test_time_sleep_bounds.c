#include <limits.h>
#include <stdint.h>
#include <stdio.h>

#if defined(_WIN32)

int main(void) {
    puts("passed");
    return 0;
}

#else

#define nanosleep neverc_test_nanosleep
#include "../../../std/src/time/time.c"
#undef nanosleep
#include "../../../std/src/time/tzdata/tzdata.c"

static int sleep_calls;
static time_t sleep_seconds[4];
static long sleep_nanoseconds[4];

int neverc_test_nanosleep(const struct timespec *req, struct timespec *rem) {
    if (sleep_calls < 4) {
        sleep_seconds[sleep_calls] = req->tv_sec;
        sleep_nanoseconds[sleep_calls] = req->tv_nsec;
    }
    sleep_calls++;
    if (rem) {
        rem->tv_sec = 0;
        rem->tv_nsec = 0;
    }
    return 0;
}

int main(void) {
    neverc_duration_t duration =
        ((neverc_duration_t)INT_MAX + 1) * NEVERC_TIME_SECOND + 123;
    neverc_time_sleep(duration);

    if (sleep_calls != 2 || sleep_seconds[0] != (time_t)INT_MAX ||
        sleep_nanoseconds[0] != 123 || sleep_seconds[1] != (time_t)1 ||
        sleep_nanoseconds[1] != 0) {
        fputs("POSIX sleep chunking regression failed\n", stderr);
        return 1;
    }
    puts("passed");
    return 0;
}

#endif
