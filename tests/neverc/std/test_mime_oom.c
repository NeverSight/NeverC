#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t allocation_count;
static size_t fail_at;

static void *controlled_malloc(size_t size) {
    allocation_count++;
    return fail_at != 0 && allocation_count == fail_at ? NULL : malloc(size);
}

#define malloc controlled_malloc
#include "../../../std/src/mime/mime.c"
#undef malloc

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
    char media_type[32];
    char *keys[2] = {NULL, NULL};
    char *values[2] = {NULL, NULL};
    int count = 0;

    reset_allocator(0);
    CHECK(neverc_mime_parse_media_type(
              "text/plain; charset=utf-8; boundary=example",
              media_type, sizeof(media_type), keys, values, 2, &count) == 0);
    CHECK(strcmp(media_type, "text/plain") == 0);
    CHECK(count == 2);
    size_t successful_allocations = allocation_count;
    mime_free_params(keys, values, count);

    for (size_t i = 1; i <= successful_allocations; i++) {
        reset_allocator(i);
        strcpy(media_type, "unchanged");
        count = 99;
        CHECK(neverc_mime_parse_media_type(
                  "text/plain; charset=utf-8; boundary=example",
                  media_type, sizeof(media_type), keys, values, 2,
                  &count) == -1);
        CHECK(media_type[0] == '\0');
        CHECK(count == 0);
        CHECK(keys[0] == NULL && values[0] == NULL);
        CHECK(keys[1] == NULL && values[1] == NULL);
    }

    /* RFC 2231 decode mallocs the unescaped value. OOM there must fail
     * the parse, not drop the parameter and return success. */
    reset_allocator(0);
    CHECK(neverc_mime_parse_media_type(
              "application/octet-stream; filename*=utf-8''na%C3%AFve.txt",
              media_type, sizeof(media_type), keys, values, 2, &count) == 0);
    CHECK(count == 1);
    size_t rfc2231_allocations = allocation_count;
    mime_free_params(keys, values, count);

    for (size_t i = 1; i <= rfc2231_allocations; i++) {
        reset_allocator(i);
        strcpy(media_type, "unchanged");
        count = 99;
        CHECK(neverc_mime_parse_media_type(
                  "application/octet-stream; "
                  "filename*=utf-8''na%C3%AFve.txt",
                  media_type, sizeof(media_type), keys, values, 2,
                  &count) == -1);
        CHECK(media_type[0] == '\0');
        CHECK(count == 0);
        CHECK(keys[0] == NULL && values[0] == NULL);
        CHECK(keys[1] == NULL && values[1] == NULL);
    }

    puts("passed");
    return 0;
}
