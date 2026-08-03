#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t allocation_count;
static size_t fail_at;

static int allocation_fails(void) {
    allocation_count++;
    return fail_at != 0 && allocation_count == fail_at;
}

static void *controlled_malloc(size_t size) {
    return allocation_fails() ? NULL : malloc(size);
}

static void *controlled_realloc(void *ptr, size_t size) {
    return allocation_fails() ? NULL : realloc(ptr, size);
}

#define malloc controlled_malloc
#define realloc controlled_realloc
#include "../../../std/src/fmt/fmt.c"
#undef malloc
#undef realloc

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n",              \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static void reset_allocator(size_t failure) {
    allocation_count = 0;
    fail_at = failure;
}

int main(void) {
    reset_allocator(1);
    CHECK(neverc_fmt_sprintf("value") == NULL);

    char literal[300];
    memset(literal, 'x', sizeof(literal) - 1);
    literal[sizeof(literal) - 1] = '\0';
    reset_allocator(2);
    CHECK(neverc_fmt_sprintf(literal) == NULL);

    reset_allocator(2);
    CHECK(neverc_fmt_sprintf("%300s", "x") == NULL);
    puts("passed");
    return 0;
}
