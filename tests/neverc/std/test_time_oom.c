#include <stdio.h>
#include <stdlib.h>

static size_t allocation_count;
static size_t fail_at;

static void *controlled_malloc(size_t size) {
    allocation_count++;
    return fail_at != 0 && allocation_count == fail_at ? NULL : malloc(size);
}

static void *controlled_calloc(size_t n, size_t sz) {
    allocation_count++;
    return fail_at != 0 && allocation_count == fail_at ? NULL : calloc(n, sz);
}

#define malloc controlled_malloc
#define calloc controlled_calloc
#include "../../../std/src/time/time.c"
#include "../../../std/src/time/tzdata/tzdata.c"
#undef malloc
#undef calloc

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n",              \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static void fail_first_allocation(void) {
    allocation_count = 0;
    fail_at = 1;
}

int main(void) {
    neverc_time_t epoch = neverc_time_unix(0, 0);

    fail_first_allocation();
    CHECK(neverc_time_format_rfc3339(epoch) == NULL);

    fail_first_allocation();
    CHECK(neverc_time_format_unix_date(epoch) == NULL);

    fail_first_allocation();
    CHECK(neverc_time_format_duration(NEVERC_TIME_SECOND) == NULL);

    fail_first_allocation();
    CHECK(neverc_time_format(epoch, "2006-01-02") == NULL);

    fail_first_allocation();
    CHECK(neverc_time_format(epoch, "002 __2 _2006") == NULL);

    fail_first_allocation();
    CHECK(neverc_tzdata_fixed_zone("UTC+8", 28800) == NULL);

    allocation_count = 0;
    fail_at = 2;
    CHECK(neverc_tzdata_fixed_zone("UTC+8", 28800) == NULL);

    fail_at = 0;
    char *formatted = neverc_time_format_rfc3339(epoch);
    CHECK(formatted != NULL);
    free(formatted);
    puts("passed");
    return 0;
}
