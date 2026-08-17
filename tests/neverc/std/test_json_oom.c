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

static void *controlled_calloc(size_t count, size_t size) {
    return allocation_fails() ? NULL : calloc(count, size);
}

static void *controlled_realloc(void *ptr, size_t size) {
    return allocation_fails() ? NULL : realloc(ptr, size);
}

#define malloc controlled_malloc
#define calloc controlled_calloc
#define realloc controlled_realloc
#include "../../../std/src/encoding/json/json.c"
#undef malloc
#undef calloc
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
    /* Array/string only: object parse can fall back to O(n^2) set if the
     * key index OOMs, which is still correct and would make a full-sweep
     * NULL check fail. */
    static const char document[] = "[1,true,\"x\",[2]]";

    reset_allocator(0);
    neverc_json_value_t *v =
        neverc_json_parse(document, sizeof(document) - 1);
    CHECK(v != NULL);
    size_t parse_allocations = allocation_count;
    neverc_json_free(v);

    for (size_t failure = 1; failure <= parse_allocations; failure++) {
        reset_allocator(failure);
        v = neverc_json_parse(document, sizeof(document) - 1);
        CHECK(v == NULL);
    }

    /* Heap spill in parse_string: a run longer than the 256-byte stack buf. */
    {
        char long_str[300];
        size_t n = 0;
        long_str[n++] = '"';
        memset(long_str + n, 'a', 280);
        n += 280;
        long_str[n++] = '"';
        reset_allocator(0);
        v = neverc_json_parse(long_str, n);
        CHECK(v != NULL);
        parse_allocations = allocation_count;
        neverc_json_free(v);
        for (size_t failure = 1; failure <= parse_allocations; failure++) {
            reset_allocator(failure);
            v = neverc_json_parse(long_str, n);
            CHECK(v == NULL);
        }
    }

    reset_allocator(1);
    CHECK(neverc_json_new_array() == NULL);
    reset_allocator(2);
    CHECK(neverc_json_new_array() == NULL);
    reset_allocator(1);
    CHECK(neverc_json_new_object() == NULL);
    reset_allocator(2);
    CHECK(neverc_json_new_object() == NULL);
    reset_allocator(1);
    CHECK(neverc_json_new_string("hi") == NULL);
    reset_allocator(2);
    CHECK(neverc_json_new_string("hi") == NULL);

    reset_allocator(0);
    neverc_json_value_t *arr = neverc_json_new_array();
    CHECK(arr != NULL);
    neverc_json_value_t *item = neverc_json_new_number(1);
    CHECK(item != NULL);
    for (int i = 0; i < 8; i++) {
        neverc_json_value_t *n = neverc_json_new_number((double)i);
        CHECK(n != NULL);
        CHECK(neverc_json_array_append(arr, n) == 0);
    }
    reset_allocator(1);
    CHECK(neverc_json_array_append(arr, item) == -1);
    neverc_json_free(item);
    neverc_json_free(arr);

    puts("passed");
    return 0;
}
