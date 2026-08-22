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

#include "../../../std/src/unicode/unicode.c"
#define malloc controlled_malloc
#define realloc controlled_realloc
#include "../../../std/src/bytes/bytes.c"
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

static int is_comma(uint8_t c) {
    return c == ',';
}

static uint8_t identity(uint8_t c) {
    return c;
}

int main(void) {
    static const uint8_t data[] = "alpha,beta,gamma";
    size_t outlen = 99;

    reset_allocator(1);
    CHECK(neverc_bytes_trim(data, sizeof(data) - 1, "a", &outlen) == NULL);
    CHECK(outlen == 0);

    reset_allocator(1);
    outlen = 99;
    CHECK(neverc_bytes_replace(data, sizeof(data) - 1,
                               (const uint8_t *)",", 1,
                               (const uint8_t *)"--", 2, 0, &outlen) == NULL);
    CHECK(outlen == 0);

    reset_allocator(1);
    outlen = 99;
    CHECK(neverc_bytes_map(identity, data, sizeof(data) - 1, &outlen) == NULL);
    CHECK(outlen == 0);

    reset_allocator(1);
    size_t count = 99;
    CHECK(neverc_bytes_split(data, sizeof(data) - 1,
                             (const uint8_t *)",", 1, &count) == NULL);
    CHECK(count == 0);

    char many[81];
    char many_fields[81];
    for (size_t i = 0; i < 40; i++) {
        many[i * 2] = 'x';
        many[i * 2 + 1] = ',';
        many_fields[i * 2] = 'x';
        many_fields[i * 2 + 1] = ' ';
    }
    many[80] = '\0';
    many_fields[80] = '\0';

    reset_allocator(2);
    count = 99;
    CHECK(neverc_bytes_split((const uint8_t *)many, 80,
                             (const uint8_t *)",", 1, &count) == NULL);
    CHECK(count == 0);

    reset_allocator(2);
    count = 99;
    CHECK(neverc_bytes_fields((const uint8_t *)many_fields, 80, &count) == NULL);
    CHECK(count == 0);

    reset_allocator(2);
    count = 99;
    CHECK(neverc_bytes_fields_func((const uint8_t *)many, 80,
                                   is_comma, &count) == NULL);
    CHECK(count == 0);

    reset_allocator(2);
    count = 99;
    CHECK(neverc_bytes_split_after((const uint8_t *)many, 80,
                                   (const uint8_t *)",", 1, &count) == NULL);
    CHECK(count == 0);

    static const uint8_t invalid[] = {0xff, 0xff, 0xff, 0xff};
    reset_allocator(1);
    outlen = 99;
    CHECK(neverc_bytes_to_valid_utf8(invalid, sizeof(invalid),
                                     (const uint8_t *)"replacement", 11,
                                     &outlen) == NULL);
    CHECK(outlen == 0);

    reset_allocator(2);
    outlen = 99;
    CHECK(neverc_bytes_to_valid_utf8(invalid, sizeof(invalid),
                                     (const uint8_t *)"replacement", 11,
                                     &outlen) == NULL);
    CHECK(outlen == 0);

    puts("passed");
    return 0;
}
