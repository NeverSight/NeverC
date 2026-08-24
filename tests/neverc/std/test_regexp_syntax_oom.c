#include <stdio.h>
#include <stdlib.h>

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
#include "../../../std/src/regexp/syntax/syntax.c"
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
    static const char pattern[] =
        "(?ism:(?P<name>[a-zA-Z0-9_]+).$)(one|two|three){2,4}";
    const char *err = NULL;
    reset_allocator(0);
    neverc_regexp_syntax_node_t *tree =
        neverc_regexp_syntax_parse(pattern, 0, &err);
    CHECK(tree != NULL);
    size_t parse_allocations = allocation_count;
    neverc_regexp_syntax_free(tree);

    for (size_t failure = 1; failure <= parse_allocations; failure++) {
        reset_allocator(failure);
        err = NULL;
        tree = neverc_regexp_syntax_parse(pattern, 0, &err);
        CHECK(tree == NULL);
        CHECK(err != NULL);
    }

    reset_allocator(0);
    tree = neverc_regexp_syntax_parse(pattern, 0, &err);
    CHECK(tree != NULL);
    reset_allocator(0);
    char *text = neverc_regexp_syntax_string(tree);
    CHECK(text != NULL);
    size_t string_allocations = allocation_count;
    free(text);

    for (size_t failure = 1; failure <= string_allocations; failure++) {
        reset_allocator(failure);
        text = neverc_regexp_syntax_string(tree);
        CHECK(text == NULL);
    }
    neverc_regexp_syntax_free(tree);
    puts("passed");
    return 0;
}
