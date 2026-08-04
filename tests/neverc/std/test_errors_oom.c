#include <stdio.h>
#include <stdlib.h>

static size_t allocation_count;
static size_t fail_at;
static int fail_persistently;

static void *controlled_malloc(size_t size) {
    allocation_count++;
    if (allocation_count == fail_at ||
        (fail_persistently && allocation_count > fail_at))
        return NULL;
    return malloc(size);
}

#define malloc controlled_malloc
#include "../../../std/src/errors/errors.c"
#undef malloc

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n",              \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static void reset_allocator(size_t failure, int persistent) {
    allocation_count = 0;
    fail_at = failure;
    fail_persistently = persistent;
}

int main(void) {
    reset_allocator(2, 0);
    CHECK(neverc_errors_new("message") == NULL);

    neverc_error_t cause = {"cause", NULL, 0};
    reset_allocator(2, 1);
    CHECK(neverc_errors_wrap("context", &cause) == NULL);
    CHECK(cause.msg != NULL);

    puts("passed");
    return 0;
}
