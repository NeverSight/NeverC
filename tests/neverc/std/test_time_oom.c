#include <stdio.h>
#include <stdlib.h>

static size_t allocation_count;
static size_t fail_at;

static void *controlled_malloc(size_t size) {
    allocation_count++;
    return fail_at != 0 && allocation_count == fail_at ? NULL : malloc(size);
}

#define malloc controlled_malloc
#include "../../../std/src/time/time.c"
#undef malloc

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

    fail_at = 0;
    char *formatted = neverc_time_format_rfc3339(epoch);
    CHECK(formatted != NULL);
    free(formatted);
    puts("passed");
    return 0;
}
