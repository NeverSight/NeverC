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
#include "../../../std/src/mime/mime.c"
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

static int fail_every_alloc(const char *input, size_t nkeys) {
    char media_type[64];
    char *keys[4] = {NULL, NULL, NULL, NULL};
    char *values[4] = {NULL, NULL, NULL, NULL};
    int count = 0;

    reset_allocator(0);
    CHECK(neverc_mime_parse_media_type(
              input, media_type, sizeof(media_type), keys, values,
              (int)nkeys, &count) == 0);
    size_t successful_allocations = allocation_count;
    mime_free_params(keys, values, count);

    for (size_t i = 1; i <= successful_allocations; i++) {
        reset_allocator(i);
        strcpy(media_type, "unchanged");
        count = 99;
        CHECK(neverc_mime_parse_media_type(
                  input, media_type, sizeof(media_type), keys, values,
                  (int)nkeys, &count) == -1);
        CHECK(media_type[0] == '\0');
        CHECK(count == 0);
        for (size_t k = 0; k < nkeys; k++)
            CHECK(keys[k] == NULL && values[k] == NULL);
    }
    return 0;
}

int main(void) {
    CHECK(fail_every_alloc(
              "text/plain; charset=utf-8; boundary=example", 2) == 0);

    /* RFC 2231 decode mallocs the unescaped value. OOM there must fail
     * the parse, not drop the parameter and return success. */
    CHECK(fail_every_alloc(
              "application/octet-stream; filename*=utf-8''na%C3%AFve.txt",
              2) == 0);

    /* Continuation stitching calloc/mallocs scratch buffers. OOM must
     * fail closed rather than returning a truncated name. */
    CHECK(fail_every_alloc(
              "application/octet-stream; "
              "filename*0*=utf-8''foo; filename*1=.txt",
              2) == 0);

    puts("passed");
    return 0;
}
