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
#include "../../../std/src/encoding/xml/xml.c"
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
    static const char document[] =
        "<root a=\"1\" b=\"2\" c=\"3\" d=\"4\" e=\"5\">"
        "first<x/>second<y/>third<z/>fourth<u/>fifth<v/>sixth"
        "</root>";

    reset_allocator(0);
    neverc_xml_node_t *tree =
        neverc_xml_parse(document, sizeof(document) - 1);
    CHECK(tree != NULL);
    size_t parse_allocations = allocation_count;
    neverc_xml_node_free(tree);

    for (size_t failure = 1; failure <= parse_allocations; failure++) {
        reset_allocator(failure);
        tree = neverc_xml_parse(document, sizeof(document) - 1);
        CHECK(tree == NULL);
    }

    reset_allocator(1);
    neverc_xml_decoder_t decoder;
    neverc_xml_token_t token;
    neverc_xml_decoder_init(&decoder, "text", 4);
    CHECK(neverc_xml_decode_token(&decoder, &token) == -1);
    neverc_xml_token_free(&token);

    reset_allocator(1);
    size_t outlen = 99;
    CHECK(neverc_xml_escape("value", &outlen) == NULL);
    CHECK(outlen == 0);

    reset_allocator(1);
    outlen = 99;
    CHECK(neverc_xml_unescape("&amp;", 5, &outlen) == NULL);
    CHECK(outlen == 0);
    puts("passed");
    return 0;
}
