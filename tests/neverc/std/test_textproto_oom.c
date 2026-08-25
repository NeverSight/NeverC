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

#define malloc controlled_malloc
#define calloc controlled_calloc
#include "../../../std/src/net/textproto/textproto.c"
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

static void reset_allocator(size_t failure) {
    allocation_count = 0;
    fail_at = failure;
}

int main(void) {
    neverc_mime_header_t h;
    reset_allocator(0);
    neverc_mime_header_init(&h);
    CHECK(neverc_mime_header_try_add(&h, "content-type", "text/plain") == 0);
    CHECK(neverc_mime_header_len(&h) == 1);
    size_t add_allocations = allocation_count;
    neverc_mime_header_free(&h);

    for (size_t i = 1; i <= add_allocations; i++) {
        reset_allocator(i);
        neverc_mime_header_init(&h);
        CHECK(neverc_mime_header_try_add(
                  &h, "content-type", "text/plain") == -1);
        CHECK(neverc_mime_header_len(&h) == 0);
        neverc_mime_header_free(&h);
    }

    reset_allocator(0);
    neverc_mime_header_init(&h);
    CHECK(neverc_mime_header_try_set(&h, "X-Test", "old") == 0);
    CHECK(strcmp(neverc_mime_header_get(&h, "X-Test"), "old") == 0);
    reset_allocator(1);
    CHECK(neverc_mime_header_try_set(&h, "X-Test", "new") == -1);
    CHECK(strcmp(neverc_mime_header_get(&h, "X-Test"), "old") == 0);
    neverc_mime_header_free(&h);

    reset_allocator(1);
    CHECK(neverc_textproto_canonical_mime_header_key("Key") == NULL);

    char *lines[1] = { NULL };
    size_t nlines = 99;
    reset_allocator(1);
    static const char dot_data[] = "line\r\n.\r\n";
    CHECK(neverc_textproto_read_dot_lines(
              dot_data, strlen(dot_data), lines, 1, &nlines, NULL) == -1);
    CHECK(nlines == 0);
    puts("passed");
    return 0;
}
