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

#include "../../../std/src/unicode/unicode.c"
#include "../../../std/src/unicode/utf8/utf8.c"
#define malloc controlled_malloc
#define calloc controlled_calloc
#define realloc controlled_realloc
#include "../../../std/src/regexp/regexp.c"
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

static int valid_find_all(char **matches, int count) {
    if (!matches || count != 24) return 0;
    for (int i = 0; i < count; i++) {
        if (matches[i][0] != (char)('a' + i) || matches[i][1] != '\0')
            return 0;
    }
    return 1;
}

static int valid_replace(const char *result, size_t outlen) {
    static const char replacement[] = "replacement";
    if (!result || outlen != 64U * (sizeof(replacement) - 1U)) return 0;
    for (size_t i = 0; i < 64U; i++) {
        if (memcmp(result + i * (sizeof(replacement) - 1U), replacement,
                   sizeof(replacement) - 1U) != 0)
            return 0;
    }
    return result[outlen] == '\0';
}

static int valid_split(char **parts, int count) {
    if (!parts || count != 21) return 0;
    for (int i = 0; i < count; i++)
        if (parts[i][0] != '\0') return 0;
    return 1;
}

int main(void) {
    static const char find_text[] =
        "a b c d e f g h i j k l m n o p q r s t u v w x";
    static const char split_text[] = ",,,,,,,,,,,,,,,,,,,,";

    reset_allocator(0);
    const char *err = NULL;
    neverc_regexp_t *re = neverc_regexp_compile("([a-z]{2,4}|[0-9]+)-value", &err);
    CHECK(re != NULL);
    size_t compile_allocations = allocation_count;
    neverc_regexp_free(re);

    for (size_t i = 1; i <= compile_allocations; i++) {
        reset_allocator(i);
        err = NULL;
        re = neverc_regexp_compile("([a-z]{2,4}|[0-9]+)-value", &err);
        CHECK(re == NULL);
        CHECK(err != NULL);
    }

    reset_allocator(0);
    re = neverc_regexp_compile("[a-z]", NULL);
    CHECK(re != NULL);
    reset_allocator(0);
    int count = 0;
    char **matches = neverc_regexp_find_all(re, find_text, -1, &count);
    CHECK(valid_find_all(matches, count));
    size_t find_allocations = allocation_count;
    neverc_regexp_free_strings(matches, count);

    for (size_t i = 1; i <= find_allocations; i++) {
        reset_allocator(i);
        count = 99;
        matches = neverc_regexp_find_all(re, find_text, -1, &count);
        CHECK((matches == NULL && count == 0) || valid_find_all(matches, count));
        neverc_regexp_free_strings(matches, count);
    }
    neverc_regexp_free(re);

    reset_allocator(0);
    re = neverc_regexp_compile("x", NULL);
    CHECK(re != NULL);
    char source[65];
    memset(source, 'x', sizeof(source) - 1U);
    source[sizeof(source) - 1U] = '\0';
    reset_allocator(0);
    size_t outlen = 0;
    char *result = neverc_regexp_replace_all(re, source, "replacement", &outlen);
    CHECK(valid_replace(result, outlen));
    size_t replace_allocations = allocation_count;
    free(result);

    for (size_t i = 1; i <= replace_allocations; i++) {
        reset_allocator(i);
        outlen = 99;
        result = neverc_regexp_replace_all(re, source, "replacement", &outlen);
        CHECK((result == NULL && outlen == 0) || valid_replace(result, outlen));
        free(result);
    }
    neverc_regexp_free(re);

    reset_allocator(0);
    re = neverc_regexp_compile(",", NULL);
    CHECK(re != NULL);
    reset_allocator(0);
    count = 0;
    char **parts = neverc_regexp_split(re, split_text, -1, &count);
    CHECK(valid_split(parts, count));
    size_t split_allocations = allocation_count;
    neverc_regexp_free_strings(parts, count);

    for (size_t i = 1; i <= split_allocations; i++) {
        reset_allocator(i);
        count = 99;
        parts = neverc_regexp_split(re, split_text, -1, &count);
        CHECK((parts == NULL && count == 0) || valid_split(parts, count));
        neverc_regexp_free_strings(parts, count);
    }
    neverc_regexp_free(re);

    reset_allocator(1);
    CHECK(neverc_regexp_quote_meta("a.b") == NULL);
    puts("passed");
    return 0;
}
