/* RFC 9113 section 6.5.2 measures the field list uncompressed. Budgeting the
 * compressed block instead lets a run of one-byte indexed references expand
 * into hundreds of megabytes of copying from a 64 KB request. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t copied_bytes;

static char *counting_strdup(const char *s) {
    size_t length = strlen(s);
    char *copy = (char *)malloc(length + 1);
    if (copy) {
        memcpy(copy, s, length + 1);
        copied_bytes += length + 1;
    }
    return copy;
}

#define strdup counting_strdup
#include "../../../std/src/net/http/http2/http2.c"
#undef strdup

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n",                 \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define VALUE_LEN 4031U
#define REFERENCES 20000U
#define LIST_BUDGET 65536U

/* Literal with incremental indexing, new name "a", then VALUE_LEN bytes of
 * 'v'. The entry costs 1 + VALUE_LEN + 32 = 4064 <= 4096, so it lands in the
 * dynamic table at absolute index 62 and every later 0xbe references it. */
static size_t build_block(uint8_t *out) {
    size_t pos = 0;
    out[pos++] = 0x40;
    out[pos++] = 0x01;
    out[pos++] = 'a';
    out[pos++] = 0x7f;                            /* 7-bit prefix, all ones */
    size_t remainder = VALUE_LEN - 127U;          /* 3904 */
    while (remainder >= 128U) {
        out[pos++] = (uint8_t)(0x80U | (remainder & 0x7fU));
        remainder >>= 7;
    }
    out[pos++] = (uint8_t)remainder;
    memset(out + pos, 'v', VALUE_LEN);
    pos += VALUE_LEN;
    for (size_t i = 0; i < REFERENCES; i++) out[pos++] = 0xbe;
    return pos;
}

static void free_headers(neverc_hpack_header_t *headers, int count) {
    for (int i = 0; i < count; i++) {
        free(headers[i].name);
        free(headers[i].value);
    }
}

int main(void) {
    uint8_t *block = (uint8_t *)malloc(VALUE_LEN + REFERENCES + 64U);
    CHECK(block != NULL);
    size_t block_len = build_block(block);

    neverc_hpack_header_t headers[64];
    int nheaders = 0;

    neverc_hpack_decoder_t *dec =
        neverc_hpack_decoder_create(NEVERC_HPACK_MAX_DYNAMIC_TABLE_SIZE);
    CHECK(dec != NULL);
    neverc_hpack_decoder_set_max_list_size(dec, LIST_BUDGET);

    copied_bytes = 0;
    int rc = neverc_hpack_decode(dec, block, block_len, headers,
                                 (int)(sizeof(headers) / sizeof(headers[0])),
                                 &nheaders);
    /* Truncation is reported as a stream error, not a connection error. */
    CHECK(rc == 1);
    /* Without the budget this copies REFERENCES * (VALUE_LEN + 33) bytes,
     * roughly 80 MB. The budget caps it near LIST_BUDGET. */
    CHECK(copied_bytes < 4U * 1024U * 1024U);
    free_headers(headers, nheaders);

    /* The dynamic table must still be in sync: a later block holding only
     * the indexed reference has to resolve to the entry inserted above. */
    uint8_t reference = 0xbe;
    nheaders = 0;
    copied_bytes = 0;
    CHECK(neverc_hpack_decode(dec, &reference, 1U, headers,
                              (int)(sizeof(headers) / sizeof(headers[0])),
                              &nheaders) == 0);
    CHECK(nheaders == 1);
    CHECK(strcmp(headers[0].name, "a") == 0);
    CHECK(strlen(headers[0].value) == VALUE_LEN);
    free_headers(headers, nheaders);
    neverc_hpack_decoder_destroy(dec);

    /* An unbudgeted decoder keeps the previous behaviour. */
    dec = neverc_hpack_decoder_create(NEVERC_HPACK_MAX_DYNAMIC_TABLE_SIZE);
    CHECK(dec != NULL);
    nheaders = 0;
    rc = neverc_hpack_decode(dec, block, block_len, headers,
                             (int)(sizeof(headers) / sizeof(headers[0])), &nheaders);
    CHECK(rc == 1); /* still overflows the 64-entry array */
    CHECK(nheaders == (int)(sizeof(headers) / sizeof(headers[0])));
    free_headers(headers, nheaders);
    neverc_hpack_decoder_destroy(dec);

    /* A block that fits the budget must decode cleanly. */
    dec = neverc_hpack_decoder_create(NEVERC_HPACK_MAX_DYNAMIC_TABLE_SIZE);
    CHECK(dec != NULL);
    neverc_hpack_decoder_set_max_list_size(dec, LIST_BUDGET);
    nheaders = 0;
    CHECK(neverc_hpack_decode(dec, block, VALUE_LEN + 6U, headers,
                              (int)(sizeof(headers) / sizeof(headers[0])),
                              &nheaders) == 0);
    CHECK(nheaders == 1);
    CHECK(strcmp(headers[0].name, "a") == 0);
    free_headers(headers, nheaders);
    neverc_hpack_decoder_destroy(dec);

    free(block);
    puts("passed");
    return 0;
}
