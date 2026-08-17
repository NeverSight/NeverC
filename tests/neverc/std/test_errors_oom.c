#include <stdint.h>
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
    reset_allocator(1, 0);
    CHECK(neverc_errors_wrap("context", &cause) == NULL);
    CHECK(cause.msg != NULL);

    reset_allocator(2, 1);
    CHECK(neverc_errors_wrap("context", &cause) == NULL);
    CHECK(cause.msg != NULL);

    reset_allocator(2, 0);
    CHECK(neverc_errors_wrap("context", &cause) == NULL);
    CHECK(cause.msg != NULL);

    reset_allocator(2, 0);
    CHECK(neverc_errors_wrap("context", NULL) == NULL);

    CHECK(dup_string(NULL) == NULL);
    size_t sum = 0;
    CHECK(!errors_size_add(SIZE_MAX, 1, &sum));
    CHECK(!errors_size_add(SIZE_MAX - 2, 3, &sum));

    reset_allocator(100000, 0);
    neverc_error_t *j1 = neverc_errors_new("a");
    neverc_error_t *j2 = neverc_errors_new("b");
    CHECK(j1 != NULL && j2 != NULL);
    neverc_error_t *pair[] = {j1, j2};
    reset_allocator(1, 1);
    CHECK(neverc_errors_join(pair, 2) == NULL);
    neverc_errors_free(j1);
    neverc_errors_free(j2);

    puts("passed");
    return 0;
}
