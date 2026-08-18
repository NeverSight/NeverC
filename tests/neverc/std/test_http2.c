/*
 * NeverC HTTP/2 test suite.
 *
 * Tests HPACK header compression, frame parsing, and settings.
 * Uses RFC 7541 test vectors for HPACK validation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "neverc/std/net/http/http2.h"
#include "neverc/std/net/http.h"
#include "neverc/std/net/tcp.h"

#ifndef _WIN32
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#endif

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    static void test_##name(void); \
    static void run_test_##name(void) { \
        printf("  test %-50s ", #name); \
        test_##name(); \
        printf("PASS\n"); \
        tests_passed++; \
    } \
    static void test_##name(void)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("FAIL\n    %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        tests_failed++; return; \
    } \
} while(0)

#define ASSERT_EQ(a, b) do { \
    long long _a = (long long)(a), _b = (long long)(b); \
    if (_a != _b) { \
        printf("FAIL\n    %s:%d: %s == %lld, expected %lld\n", \
               __FILE__, __LINE__, #a, _a, _b); \
        tests_failed++; return; \
    } \
} while(0)

#define ASSERT_STREQ(a, b) do { \
    if (strcmp((a), (b)) != 0) { \
        printf("FAIL\n    %s:%d: \"%s\" != \"%s\"\n", \
               __FILE__, __LINE__, (a), (b)); \
        tests_failed++; return; \
    } \
} while(0)

/* ===== Test 1: Frame header read/write roundtrip ===== */
TEST(frame_header_roundtrip) {
    neverc_h2_frame_header_t hdr = {
        .length = 16384,
        .type = NC_H2_FRAME_HEADERS,
        .flags = NC_H2_FLAG_END_HEADERS | NC_H2_FLAG_END_STREAM,
        .stream_id = 1
    };
    uint8_t buf[9];
    ASSERT_EQ(neverc_h2_frame_header_write(&hdr, buf), 0);

    neverc_h2_frame_header_t parsed;
    ASSERT_EQ(neverc_h2_frame_header_read(buf, 9, &parsed), 0);
    ASSERT_EQ(parsed.length, 16384);
    ASSERT_EQ(parsed.type, NC_H2_FRAME_HEADERS);
    ASSERT_EQ(parsed.flags, NC_H2_FLAG_END_HEADERS | NC_H2_FLAG_END_STREAM);
    ASSERT_EQ(parsed.stream_id, 1);
}

/* ===== Test 2: Frame header with max values ===== */
TEST(frame_header_max_values) {
    neverc_h2_frame_header_t hdr = {
        .length = 0xFFFFFF, /* 24-bit max */
        .type = 0xFF,
        .flags = 0xFF,
        .stream_id = 0x7FFFFFFF /* 31-bit max */
    };
    uint8_t buf[9];
    neverc_h2_frame_header_write(&hdr, buf);

    neverc_h2_frame_header_t parsed;
    neverc_h2_frame_header_read(buf, 9, &parsed);
    ASSERT_EQ(parsed.length, 0xFFFFFF);
    ASSERT_EQ(parsed.type, 0xFF);
    ASSERT_EQ(parsed.flags, 0xFF);
    ASSERT_EQ(parsed.stream_id, 0x7FFFFFFF);
}

TEST(frame_header_rejects_truncated_and_null) {
    neverc_h2_frame_header_t parsed;
    uint8_t short_buf[8] = {0};
    memset(&parsed, 0xa5, sizeof(parsed));
    ASSERT_EQ(neverc_h2_frame_header_read(short_buf, sizeof(short_buf),
                                          &parsed), -1);
    ASSERT_EQ(neverc_h2_frame_header_read(NULL, 9, &parsed), -1);
    ASSERT_EQ(neverc_h2_frame_header_read(short_buf, 9, NULL), -1);

    uint8_t out[9];
    memset(out, 0xa5, sizeof(out));
    ASSERT_EQ(neverc_h2_frame_header_write(NULL, out), -1);
    ASSERT_EQ(out[0], 0xa5);

    neverc_h2_frame_header_t oversized = {
        .length = 0x1000000u, .type = 0, .flags = 0, .stream_id = 1
    };
    ASSERT_EQ(neverc_h2_frame_header_write(&oversized, out), -1);
    ASSERT_EQ(out[0], 0xa5);

    neverc_h2_frame_header_t reserved_bit = {
        .length = 1, .type = 0, .flags = 0, .stream_id = 0x80000001u
    };
    ASSERT_EQ(neverc_h2_frame_header_write(&reserved_bit, out), -1);
}

/* ===== Test 3: Settings initialization ===== */
TEST(settings_defaults) {
    neverc_h2_settings_t s;
    neverc_h2_settings_init(&s);
    ASSERT_EQ(s.header_table_size, 4096);
    ASSERT_EQ(s.enable_push, 1);
    ASSERT_EQ(s.max_concurrent_streams, 100);
    ASSERT_EQ(s.initial_window_size, 65535);
    ASSERT_EQ(s.max_frame_size, 16384);
}

/* ===== Test 4: HPACK decoder — indexed header field ===== */
TEST(hpack_decode_indexed) {
    neverc_hpack_decoder_t *dec = neverc_hpack_decoder_create(4096);
    ASSERT_TRUE(dec != NULL);

    /* Indexed header: :method = GET (index 2) → 0x82 */
    uint8_t data[] = { 0x82 };
    neverc_hpack_header_t headers[16];
    int nheaders;
    ASSERT_EQ(neverc_hpack_decode(dec, data, sizeof(data), headers, 16, &nheaders), 0);
    ASSERT_EQ(nheaders, 1);
    ASSERT_STREQ(headers[0].name, ":method");
    ASSERT_STREQ(headers[0].value, "GET");

    free(headers[0].name);
    free(headers[0].value);
    neverc_hpack_decoder_destroy(dec);
}

/* ===== Test 5: HPACK decoder — multiple indexed ===== */
TEST(hpack_decode_multi_indexed) {
    neverc_hpack_decoder_t *dec = neverc_hpack_decoder_create(4096);

    /* :method GET (2), :path / (4), :scheme https (7) */
    uint8_t data[] = { 0x82, 0x84, 0x87 };
    neverc_hpack_header_t headers[16];
    int nheaders;
    ASSERT_EQ(neverc_hpack_decode(dec, data, sizeof(data), headers, 16, &nheaders), 0);
    ASSERT_EQ(nheaders, 3);
    ASSERT_STREQ(headers[0].name, ":method");
    ASSERT_STREQ(headers[0].value, "GET");
    ASSERT_STREQ(headers[1].name, ":path");
    ASSERT_STREQ(headers[1].value, "/");
    ASSERT_STREQ(headers[2].name, ":scheme");
    ASSERT_STREQ(headers[2].value, "https");

    for (int i = 0; i < nheaders; i++) {
        free(headers[i].name);
        free(headers[i].value);
    }
    neverc_hpack_decoder_destroy(dec);
}

/* ===== Test 6: HPACK decoder — literal with indexing (new name) ===== */
TEST(hpack_decode_literal_new_name) {
    neverc_hpack_decoder_t *dec = neverc_hpack_decoder_create(4096);

    /* 0x40: literal incremental indexing, new name
     * name = "custom-key" (10 bytes, no huffman)
     * value = "custom-val" (10 bytes, no huffman) */
    uint8_t data[] = {
        0x40,                                          /* literal incremental, new name */
        0x0a,                                          /* name length = 10 */
        'c','u','s','t','o','m','-','k','e','y',       /* name */
        0x0a,                                          /* value length = 10 */
        'c','u','s','t','o','m','-','v','a','l'        /* value */
    };
    neverc_hpack_header_t headers[16];
    int nheaders;
    ASSERT_EQ(neverc_hpack_decode(dec, data, sizeof(data), headers, 16, &nheaders), 0);
    ASSERT_EQ(nheaders, 1);
    ASSERT_STREQ(headers[0].name, "custom-key");
    ASSERT_STREQ(headers[0].value, "custom-val");

    free(headers[0].name);
    free(headers[0].value);
    neverc_hpack_decoder_destroy(dec);
}

/* ===== Test 7: HPACK decoder — literal with indexed name ===== */
TEST(hpack_decode_literal_indexed_name) {
    neverc_hpack_decoder_t *dec = neverc_hpack_decoder_create(4096);

    /* 0x44: literal incremental indexing, name index 4 (:path)
     * value = "/sample" (7 bytes) */
    uint8_t data[] = {
        0x44,                              /* 0x40 | 4 → name index 4 */
        0x07,                              /* value length = 7 */
        '/','s','a','m','p','l','e'        /* value */
    };
    neverc_hpack_header_t headers[16];
    int nheaders;
    ASSERT_EQ(neverc_hpack_decode(dec, data, sizeof(data), headers, 16, &nheaders), 0);
    ASSERT_EQ(nheaders, 1);
    ASSERT_STREQ(headers[0].name, ":path");
    ASSERT_STREQ(headers[0].value, "/sample");

    free(headers[0].name);
    free(headers[0].value);
    neverc_hpack_decoder_destroy(dec);
}

/* ===== Test 8: HPACK encoder — basic encoding ===== */
TEST(hpack_encode_basic) {
    neverc_hpack_encoder_t *enc = neverc_hpack_encoder_create(4096);
    ASSERT_TRUE(enc != NULL);

    neverc_hpack_header_t headers[] = {
        { .name = ":method", .value = "GET", .sensitive = 0 },
        { .name = ":path", .value = "/", .sensitive = 0 },
    };
    uint8_t buf[256];
    size_t len;
    ASSERT_EQ(neverc_hpack_encode(enc, headers, 2, buf, sizeof(buf), &len), 0);
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(len <= 10);

    /* Verify by decoding */
    neverc_hpack_decoder_t *dec = neverc_hpack_decoder_create(4096);
    neverc_hpack_header_t decoded[16];
    int nheaders;
    ASSERT_EQ(neverc_hpack_decode(dec, buf, len, decoded, 16, &nheaders), 0);
    ASSERT_EQ(nheaders, 2);
    ASSERT_STREQ(decoded[0].name, ":method");
    ASSERT_STREQ(decoded[0].value, "GET");
    ASSERT_STREQ(decoded[1].name, ":path");
    ASSERT_STREQ(decoded[1].value, "/");

    for (int i = 0; i < nheaders; i++) {
        free(decoded[i].name);
        free(decoded[i].value);
    }
    neverc_hpack_encoder_destroy(enc);
    neverc_hpack_decoder_destroy(dec);
}

/* ===== Test 9: HPACK encode/decode roundtrip (custom headers) ===== */
TEST(hpack_roundtrip_custom) {
    neverc_hpack_encoder_t *enc = neverc_hpack_encoder_create(4096);
    neverc_hpack_decoder_t *dec = neverc_hpack_decoder_create(4096);

    neverc_hpack_header_t headers[] = {
        { .name = ":method", .value = "POST", .sensitive = 0 },
        { .name = ":path", .value = "/api/v1/users", .sensitive = 0 },
        { .name = ":scheme", .value = "https", .sensitive = 0 },
        { .name = "content-type", .value = "application/json", .sensitive = 0 },
        { .name = "authorization", .value = "Bearer token123", .sensitive = 1 },
    };

    uint8_t buf[1024];
    size_t len;
    ASSERT_EQ(neverc_hpack_encode(enc, headers, 5, buf, sizeof(buf), &len), 0);

    neverc_hpack_header_t decoded[16];
    int nheaders;
    ASSERT_EQ(neverc_hpack_decode(dec, buf, len, decoded, 16, &nheaders), 0);
    ASSERT_EQ(nheaders, 5);

    ASSERT_STREQ(decoded[0].name, ":method");
    ASSERT_STREQ(decoded[0].value, "POST");
    ASSERT_STREQ(decoded[1].name, ":path");
    ASSERT_STREQ(decoded[1].value, "/api/v1/users");
    ASSERT_STREQ(decoded[2].name, ":scheme");
    ASSERT_STREQ(decoded[2].value, "https");
    ASSERT_STREQ(decoded[3].name, "content-type");
    ASSERT_STREQ(decoded[3].value, "application/json");
    ASSERT_STREQ(decoded[4].name, "authorization");
    ASSERT_STREQ(decoded[4].value, "Bearer token123");

    for (int i = 0; i < nheaders; i++) {
        free(decoded[i].name);
        free(decoded[i].value);
    }
    neverc_hpack_encoder_destroy(enc);
    neverc_hpack_decoder_destroy(dec);
}

/* ===== Test 10: Huffman encode/decode roundtrip ===== */
TEST(huffman_roundtrip) {
    const char *test_strings[] = {
        "www.example.com",
        "no-cache",
        "custom-key",
        "custom-value",
        "Mon, 21 Oct 2013 20:13:21 GMT",
        "https://www.example.com",
        NULL
    };

    for (int i = 0; test_strings[i] != NULL; i++) {
        const char *s = test_strings[i];
        size_t slen = strlen(s);

        uint8_t encoded[256];
        size_t elen;
        ASSERT_EQ(neverc_hpack_huffman_encode(
            (const uint8_t *)s, slen, encoded, sizeof(encoded), &elen), 0);

        /* Huffman encoding should be smaller or equal */
        ASSERT_TRUE(elen <= slen || slen == 0);

        uint8_t decoded[256];
        size_t dlen;
        ASSERT_EQ(neverc_hpack_huffman_decode(
            encoded, elen, decoded, sizeof(decoded), &dlen), 0);
        ASSERT_EQ(dlen, slen);
        ASSERT_TRUE(memcmp(decoded, s, slen) == 0);
    }
}

/* ===== Test 11: Server create/destroy ===== */
TEST(h2_server_lifecycle) {
    neverc_h2_server_t *srv = neverc_h2_server_create(NULL);
    ASSERT_TRUE(srv != NULL);

    neverc_h2_server_set_max_streams(srv, 200);
    neverc_h2_server_set_max_frame_size(srv, 32768);
    neverc_h2_server_set_initial_window_size(srv, 1048576);

    neverc_h2_server_destroy(srv);
}

/* ===== Test 12: Client preface constant ===== */
TEST(client_preface) {
    ASSERT_EQ(NC_H2_CLIENT_PREFACE_LEN, 24);
    ASSERT_EQ(strlen(NC_H2_CLIENT_PREFACE), 24);
    ASSERT_TRUE(memcmp(NC_H2_CLIENT_PREFACE,
                        "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n", 24) == 0);
}

/* ===== Test 13: HPACK dynamic table eviction ===== */
TEST(hpack_dynamic_table_eviction) {
    neverc_hpack_decoder_t *dec = neverc_hpack_decoder_create(128);

    /* Add several entries to force eviction.
     * Each entry overhead is name_len + value_len + 32.
     * With max_size=128, adding large entries will evict old ones. */
    for (int i = 0; i < 5; i++) {
        char name_buf[32], val_buf[32];
        snprintf(name_buf, sizeof(name_buf), "key%d", i);
        snprintf(val_buf, sizeof(val_buf), "val%d", i);

        /* Build literal incremental indexing packet */
        uint8_t data[128];
        size_t pos = 0;
        data[pos++] = 0x40; /* literal incremental, new name */
        size_t nlen = strlen(name_buf);
        data[pos++] = (uint8_t)nlen;
        memcpy(data + pos, name_buf, nlen); pos += nlen;
        size_t vlen = strlen(val_buf);
        data[pos++] = (uint8_t)vlen;
        memcpy(data + pos, val_buf, vlen); pos += vlen;

        neverc_hpack_header_t headers[16];
        int nheaders;
        ASSERT_EQ(neverc_hpack_decode(dec, data, pos, headers, 16, &nheaders), 0);
        ASSERT_EQ(nheaders, 1);
        free(headers[0].name);
        free(headers[0].value);
    }

    neverc_hpack_decoder_destroy(dec);
}

TEST(hpack_oversized_entries_do_not_expand_table) {
    neverc_hpack_decoder_t *dec = neverc_hpack_decoder_create(64);
    ASSERT_TRUE(dec != NULL);

    char oversized_value[41];
    memset(oversized_value, 'a', sizeof(oversized_value) - 1);
    oversized_value[sizeof(oversized_value) - 1] = '\0';
    char medium_value[34];
    memset(medium_value, 'b', sizeof(medium_value) - 1);
    medium_value[sizeof(medium_value) - 1] = '\0';

    const char *values[] = {oversized_value, medium_value};
    for (int i = 0; i < 2; i++) {
        uint8_t data[128];
        size_t value_len = strlen(values[i]);
        size_t pos = 0;
        data[pos++] = 0x40;
        data[pos++] = 1;
        data[pos++] = 'x';
        data[pos++] = (uint8_t)value_len;
        memcpy(data + pos, values[i], value_len);
        pos += value_len;

        neverc_hpack_header_t headers[1];
        int nheaders = 0;
        ASSERT_EQ(neverc_hpack_decode(
                      dec, data, pos, headers, 1, &nheaders), 0);
        ASSERT_EQ(nheaders, 1);
        free(headers[0].name);
        free(headers[0].value);
    }

    uint8_t newest_dynamic_entry[] = {0xbe};
    neverc_hpack_header_t headers[1];
    int nheaders = 0;
    ASSERT_EQ(neverc_hpack_decode(dec, newest_dynamic_entry,
                                  sizeof(newest_dynamic_entry), headers, 1,
                                  &nheaders), -1);
    neverc_hpack_decoder_destroy(dec);
}

TEST(hpack_dynamic_table_honors_supported_capacity) {
    ASSERT_TRUE(neverc_hpack_decoder_create(UINT32_MAX) == NULL);
    ASSERT_TRUE(neverc_hpack_encoder_create(UINT32_MAX) == NULL);

    neverc_hpack_encoder_t *enc = neverc_hpack_encoder_create(
        NEVERC_HPACK_MAX_DYNAMIC_TABLE_SIZE);
    ASSERT_TRUE(enc != NULL);
    ASSERT_EQ(neverc_hpack_encoder_set_max_table_size(enc, UINT32_MAX), -1);
    neverc_hpack_encoder_destroy(enc);

    neverc_hpack_decoder_t *dec = neverc_hpack_decoder_create(
        NEVERC_HPACK_MAX_DYNAMIC_TABLE_SIZE);
    ASSERT_TRUE(dec != NULL);

    uint8_t literal[] = {0x40, 0x00, 0x00};
    for (int i = 0; i < 257; i++) {
        neverc_hpack_header_t headers[1];
        int nheaders = 0;
        ASSERT_EQ(neverc_hpack_decode(dec, literal, sizeof(literal), headers,
                                      1, &nheaders), 0);
        ASSERT_EQ(nheaders, 1);
        free(headers[0].name);
        free(headers[0].value);
    }

    uint8_t oldest_retained[] = {0xff, 0xbe, 0x01};
    neverc_hpack_header_t headers[1];
    int nheaders = 0;
    ASSERT_EQ(neverc_hpack_decode(dec, oldest_retained,
                                  sizeof(oldest_retained), headers, 1,
                                  &nheaders), 0);
    ASSERT_EQ(nheaders, 1);
    free(headers[0].name);
    free(headers[0].value);

    uint8_t beyond_capacity[] = {0xff, 0xbf, 0x01};
    nheaders = 0;
    ASSERT_EQ(neverc_hpack_decode(dec, beyond_capacity,
                                  sizeof(beyond_capacity), headers, 1,
                                  &nheaders), -1);
    neverc_hpack_decoder_destroy(dec);
}

TEST(hpack_zero_capacity_output_is_not_written) {
    neverc_hpack_encoder_t *enc = neverc_hpack_encoder_create(4096);
    ASSERT_TRUE(enc != NULL);
    neverc_hpack_header_t header = {
        .name = "custom", .value = "value", .sensitive = 0,
    };
    uint8_t output[128];
    memset(output, 0xa5, sizeof(output));
    size_t output_length = 123;

    ASSERT_EQ(neverc_hpack_encode(
                  enc, &header, 1, output, 0, &output_length), -1);
    ASSERT_EQ(output[0], 0xa5);
    neverc_hpack_encoder_destroy(enc);
}

TEST(hpack_sensitive_headers_are_never_indexed) {
    neverc_hpack_encoder_t *enc = neverc_hpack_encoder_create(4096);
    neverc_hpack_decoder_t *dec = neverc_hpack_decoder_create(4096);
    ASSERT_TRUE(enc != NULL && dec != NULL);
    neverc_hpack_header_t sensitive = {
        .name = "authorization", .value = "secret", .sensitive = 1,
    };
    uint8_t encoded[256];
    size_t encoded_length = 0;
    ASSERT_EQ(neverc_hpack_encode(enc, &sensitive, 1, encoded,
                                  sizeof(encoded), &encoded_length), 0);
    ASSERT_TRUE(encoded_length > 0);
    ASSERT_EQ(encoded[0] & 0xf0, 0x10);

    neverc_hpack_header_t decoded[1];
    int nheaders = 0;
    ASSERT_EQ(neverc_hpack_decode(dec, encoded, encoded_length, decoded, 1,
                                  &nheaders), 0);
    ASSERT_EQ(nheaders, 1);
    free(decoded[0].name);
    free(decoded[0].value);

    uint8_t dynamic_index[] = {0xbe};
    nheaders = 0;
    ASSERT_EQ(neverc_hpack_decode(dec, dynamic_index, sizeof(dynamic_index),
                                  decoded, 1, &nheaders), -1);
    neverc_hpack_encoder_destroy(enc);
    neverc_hpack_decoder_destroy(dec);
}

TEST(hpack_rejects_overflowing_integer) {
    neverc_hpack_decoder_t *dec = neverc_hpack_decoder_create(4096);
    ASSERT_TRUE(dec != NULL);
    uint8_t overflowing_size_update[] = {
        0x3f, 0x80, 0x80, 0x80, 0x80, 0x80,
        0x80, 0x80, 0x80, 0x80, 0x02,
    };
    neverc_hpack_header_t headers[1];
    int nheaders = 0;
    ASSERT_EQ(neverc_hpack_decode(dec, overflowing_size_update,
                                  sizeof(overflowing_size_update), headers, 1,
                                  &nheaders), -1);
    neverc_hpack_decoder_destroy(dec);
}

TEST(hpack_encoder_emits_dynamic_table_size_update) {
    neverc_hpack_encoder_t *enc = neverc_hpack_encoder_create(4096);
    neverc_hpack_decoder_t *dec = neverc_hpack_decoder_create(4096);
    ASSERT_TRUE(enc != NULL && dec != NULL);
    ASSERT_EQ(neverc_hpack_encoder_set_max_table_size(enc, 0), 0);

    neverc_hpack_header_t header = {
        .name = "custom-key", .value = "custom-val", .sensitive = 0,
    };
    uint8_t encoded[128];
    size_t encoded_length = 0;
    ASSERT_EQ(neverc_hpack_encode(enc, &header, 1, encoded,
                                  sizeof(encoded), &encoded_length), 0);
    ASSERT_TRUE(encoded_length > 1);
    ASSERT_EQ(encoded[0] & 0xe0, 0x20);

    neverc_hpack_header_t decoded[1];
    int nheaders = 0;
    ASSERT_EQ(neverc_hpack_decode(dec, encoded, encoded_length, decoded, 1,
                                  &nheaders), 0);
    ASSERT_EQ(nheaders, 1);
    ASSERT_STREQ(decoded[0].name, "custom-key");
    ASSERT_STREQ(decoded[0].value, "custom-val");
    free(decoded[0].name);
    free(decoded[0].value);

    uint8_t dynamic_index[] = {0xbe};
    nheaders = 0;
    ASSERT_EQ(neverc_hpack_decode(dec, dynamic_index, sizeof(dynamic_index),
                                  decoded, 1, &nheaders), -1);
    neverc_hpack_encoder_destroy(enc);
    neverc_hpack_decoder_destroy(dec);
}

TEST(hpack_encoder_emits_min_then_current_after_shrink_grow) {
    neverc_hpack_encoder_t *enc = neverc_hpack_encoder_create(4096);
    neverc_hpack_decoder_t *dec = neverc_hpack_decoder_create(4096);
    ASSERT_TRUE(enc != NULL && dec != NULL);
    ASSERT_EQ(neverc_hpack_encoder_set_max_table_size(enc, 0), 0);
    ASSERT_EQ(neverc_hpack_encoder_set_max_table_size(enc, 4096), 0);

    neverc_hpack_header_t header = {
        .name = ":method", .value = "GET", .sensitive = 0,
    };
    uint8_t encoded[32];
    size_t encoded_length = 0;
    ASSERT_EQ(neverc_hpack_encode(enc, &header, 1, encoded,
                                  sizeof(encoded), &encoded_length), 0);
    ASSERT_TRUE(encoded_length > 2);
    ASSERT_EQ(encoded[0], 0x20); /* dynamic table size update 0 */
    ASSERT_EQ(encoded[1] & 0xe0, 0x20); /* then the restored size */

    neverc_hpack_header_t decoded[1];
    int nheaders = 0;
    ASSERT_EQ(neverc_hpack_decode(dec, encoded, encoded_length, decoded, 1,
                                  &nheaders), 0);
    ASSERT_EQ(nheaders, 1);
    ASSERT_STREQ(decoded[0].name, ":method");
    ASSERT_STREQ(decoded[0].value, "GET");
    free(decoded[0].name);
    free(decoded[0].value);
    neverc_hpack_encoder_destroy(enc);
    neverc_hpack_decoder_destroy(dec);
}

TEST(hpack_rejects_table_size_update_above_max) {
    neverc_hpack_decoder_t *dec = neverc_hpack_decoder_create(4096);
    ASSERT_TRUE(dec != NULL);
    /* Dynamic table size update 8192 with a 5-bit prefix. */
    uint8_t oversized[] = {0x3f, 0xe1, 0x3f};
    neverc_hpack_header_t headers[1];
    int nheaders = 0;
    ASSERT_EQ(neverc_hpack_decode(dec, oversized, sizeof(oversized),
                                  headers, 1, &nheaders), -1);
    neverc_hpack_decoder_destroy(dec);
}

TEST(hpack_rejects_table_size_update_after_fields) {
    neverc_hpack_decoder_t *dec = neverc_hpack_decoder_create(4096);
    ASSERT_TRUE(dec != NULL);
    uint8_t mixed[] = {0x82, 0x20};
    neverc_hpack_header_t headers[2];
    int nheaders = 0;
    ASSERT_EQ(neverc_hpack_decode(dec, mixed, sizeof(mixed), headers, 2,
                                  &nheaders), -1);
    for (int i = 0; i < nheaders; i++) {
        free(headers[i].name);
        free(headers[i].value);
    }
    neverc_hpack_decoder_destroy(dec);
}

TEST(hpack_unchanged_table_size_does_not_emit_update) {
    neverc_hpack_encoder_t *enc = neverc_hpack_encoder_create(4096);
    ASSERT_TRUE(enc != NULL);
    ASSERT_EQ(neverc_hpack_encoder_set_max_table_size(enc, 4096), 0);
    neverc_hpack_header_t header = {
        .name = ":method", .value = "GET", .sensitive = 0,
    };
    uint8_t encoded[16];
    size_t encoded_length = 0;
    ASSERT_EQ(neverc_hpack_encode(enc, &header, 1, encoded,
                                  sizeof(encoded), &encoded_length), 0);
    ASSERT_EQ(encoded_length, 1);
    ASSERT_EQ(encoded[0], 0x82);
    neverc_hpack_encoder_destroy(enc);
}

TEST(hpack_rejects_crlf_in_field_octets) {
    neverc_hpack_encoder_t *enc = neverc_hpack_encoder_create(4096);
    neverc_hpack_decoder_t *dec = neverc_hpack_decoder_create(4096);
    neverc_hpack_header_t injected = {
        .name = "x-foo\r\nx-injected", .value = "1"
    };
    uint8_t encoded[64];
    size_t encoded_length = 0;
    ASSERT_EQ(neverc_hpack_encode(enc, &injected, 1, encoded,
                                  sizeof(encoded), &encoded_length), -1);

    neverc_hpack_header_t crlf_value = {
        .name = "x-foo", .value = "1\r\nHost: evil"
    };
    ASSERT_EQ(neverc_hpack_encode(enc, &crlf_value, 1, encoded,
                                  sizeof(encoded), &encoded_length), -1);

    /* Literal name "x\r\ny" (3 octets) + value "1". */
    uint8_t crlf_name[] = { 0x00, 0x03, 'x', '\r', '\n', 0x01, '1' };
    neverc_hpack_header_t decoded[1];
    int nheaders = 0;
    ASSERT_EQ(neverc_hpack_decode(dec, crlf_name, sizeof(crlf_name),
                                  decoded, 1, &nheaders), -1);

    uint8_t crlf_value_block[] = {
        0x00, 0x05, 'x', '-', 'f', 'o', 'o', 0x03, 'a', '\r', '\n'
    };
    ASSERT_EQ(neverc_hpack_decode(dec, crlf_value_block,
                                  sizeof(crlf_value_block), decoded, 1,
                                  &nheaders), -1);

    /* HTAB is legal in field values, not in names. Incremental indexing
     * of a name containing HTAB would poison the dynamic table. */
    neverc_hpack_header_t htab_name = {
        .name = "x\tfoo", .value = "1"
    };
    ASSERT_EQ(neverc_hpack_encode(enc, &htab_name, 1, encoded,
                                  sizeof(encoded), &encoded_length), -1);
    uint8_t htab_name_block[] = {
        0x00, 0x03, 'x', '\t', 'y', 0x01, '1'
    };
    ASSERT_EQ(neverc_hpack_decode(dec, htab_name_block,
                                  sizeof(htab_name_block), decoded, 1,
                                  &nheaders), -1);

    uint8_t htab_value_block[] = {
        0x00, 0x05, 'x', '-', 'f', 'o', 'o', 0x03, 'a', '\t', 'b'
    };
    ASSERT_EQ(neverc_hpack_decode(dec, htab_value_block,
                                  sizeof(htab_value_block), decoded, 1,
                                  &nheaders), 0);
    ASSERT_EQ(nheaders, 1);
    ASSERT_STREQ(decoded[0].name, "x-foo");
    ASSERT_STREQ(decoded[0].value, "a\tb");
    free(decoded[0].name);
    free(decoded[0].value);
    neverc_hpack_encoder_destroy(enc);
    neverc_hpack_decoder_destroy(dec);
}

TEST(hpack_encode_falls_back_when_huffman_does_not_fit) {
    neverc_hpack_encoder_t *enc = neverc_hpack_encoder_create(4096);
    neverc_hpack_decoder_t *dec = neverc_hpack_decoder_create(4096);
    ASSERT_TRUE(enc != NULL && dec != NULL);

    char value[3001];
    /* obs-text (0x80) is a valid field octet that Huffman-expands, so the
     * encoder must fall back to a raw literal. CTL 0x01 is no longer legal. */
    memset(value, (char)0x80, 3000);
    value[3000] = '\0';
    neverc_hpack_header_t header = {
        .name = "x-bin", .value = value, .sensitive = 0,
    };
    uint8_t encoded[4096];
    size_t encoded_length = 0;
    ASSERT_EQ(neverc_hpack_encode(enc, &header, 1, encoded,
                                  sizeof(encoded), &encoded_length), 0);
    ASSERT_TRUE(encoded_length > 3000);

    neverc_hpack_header_t decoded[1];
    int nheaders = 0;
    ASSERT_EQ(neverc_hpack_decode(dec, encoded, encoded_length, decoded, 1,
                                  &nheaders), 0);
    ASSERT_EQ(nheaders, 1);
    ASSERT_STREQ(decoded[0].name, "x-bin");
    ASSERT_EQ(strlen(decoded[0].value), 3000);
    ASSERT_TRUE(memcmp(decoded[0].value, value, 3000) == 0);
    free(decoded[0].name);
    free(decoded[0].value);
    neverc_hpack_encoder_destroy(enc);
    neverc_hpack_decoder_destroy(dec);
}

/* ===== Test 14: Frame types and flags ===== */
TEST(frame_types_and_flags) {
    uint8_t types[] = {
        NC_H2_FRAME_DATA, NC_H2_FRAME_HEADERS, NC_H2_FRAME_PRIORITY,
        NC_H2_FRAME_RST_STREAM, NC_H2_FRAME_SETTINGS, NC_H2_FRAME_PUSH_PROMISE,
        NC_H2_FRAME_PING, NC_H2_FRAME_GOAWAY, NC_H2_FRAME_WINDOW_UPDATE,
        NC_H2_FRAME_CONTINUATION
    };
    for (int i = 0; i < 10; i++) {
        neverc_h2_frame_header_t hdr = {
            .length = (uint32_t)(100 * (i + 1)),
            .type = types[i],
            .flags = (uint8_t)(i * 3),
            .stream_id = (uint32_t)(i + 1)
        };
        uint8_t buf[9];
        neverc_h2_frame_header_write(&hdr, buf);

        neverc_h2_frame_header_t parsed;
        neverc_h2_frame_header_read(buf, 9, &parsed);
        ASSERT_EQ(parsed.type, types[i]);
        ASSERT_EQ(parsed.stream_id, (uint32_t)(i + 1));
    }
}

static const char *buf_contains(const char *buf, size_t len, const char *needle) {
    size_t nlen = strlen(needle);
    if (!buf || nlen == 0 || len < nlen)
        return NULL;
    for (size_t i = 0; i + nlen <= len; i++) {
        if (memcmp(buf + i, needle, nlen) == 0)
            return buf + i;
    }
    return NULL;
}

TEST(h2_client_rejects_zero_timeout) {
    neverc_h2_client_config_t config = neverc_h2_client_config_default();
    config.timeout_ms = 0;
    const char *error = NULL;
    neverc_h2_client_t *client = neverc_h2_client_dial(
        "127.0.0.1:1", "localhost", 0, &config, &error);
    neverc_h2_client_free(client);
    ASSERT_TRUE(client == NULL);
    ASSERT_TRUE(error != NULL);
    ASSERT_STREQ(error, "invalid HTTP/2 client configuration");
}

#ifndef _WIN32
#define H2_TEST_IO_TIMEOUT_MS 5000

static void sock_set_timeout(int fd, int ms) {
    if (fd < 0 || ms < 0) return;
    struct timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (long)(ms % 1000) * 1000;
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

static int h2_reap_child(pid_t child, int *status) {
    int local = 0;
    if (!status) status = &local;
    int elapsed = 0;
    while (elapsed < H2_TEST_IO_TIMEOUT_MS) {
        pid_t r = waitpid(child, status, WNOHANG);
        if (r == child) return 0;
        if (r < 0) return -1;
        usleep(20000);
        elapsed += 20;
    }
    (void)kill(child, SIGKILL);
    (void)waitpid(child, status, 0);
    return -1;
}

static int sock_write_all(int fd, const void *buf, size_t len) {
    const char *p = (const char *)buf;
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = write(fd, p + sent, len - sent);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return -1;
        sent += (size_t)n;
    }
    return 0;
}

static int sock_read_all(int fd, void *buf, size_t len) {
    char *p = (char *)buf;
    size_t got = 0;
    while (got < len) {
        ssize_t n = read(fd, p + got, len - got);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return -1;
        got += (size_t)n;
    }
    return 0;
}

static int h2_drain_frame_payload(int fd, uint32_t len) {
    uint8_t buf[512];
    while (len > 0) {
        size_t chunk = len > sizeof(buf) ? sizeof(buf) : len;
        if (sock_read_all(fd, buf, chunk) != 0)
            return -1;
        len -= (uint32_t)chunk;
    }
    return 0;
}

static int h2_client_handshake(int fd) {
    int sent_ack = 0;
    int got_ack = 0;

    while (!got_ack) {
        uint8_t hdr[9];
        if (sock_read_all(fd, hdr, 9) != 0)
            return -1;
        uint32_t len = ((uint32_t)hdr[0] << 16) |
                       ((uint32_t)hdr[1] << 8) |
                       (uint32_t)hdr[2];
        if (h2_drain_frame_payload(fd, len) != 0)
            return -1;

        if (hdr[3] == NC_H2_FRAME_SETTINGS) {
            if (hdr[4] & NC_H2_FLAG_ACK) {
                got_ack = 1;
            } else if (!sent_ack) {
                uint8_t ack[9] = { 0, 0, 0, NC_H2_FRAME_SETTINGS,
                                   NC_H2_FLAG_ACK, 0, 0, 0, 0 };
                if (sock_write_all(fd, ack, sizeof(ack)) != 0)
                    return -1;
                sent_ack = 1;
            }
        }
    }
    return 0;
}

typedef struct {
    int fd;
    int streaming;
    uint32_t max_streams;
    uint32_t initial_window;
} h2_serve_ctx_t;

static void h2_test_handler(neverc_http_request_t *req,
                            neverc_http_response_writer_t *w) {
    neverc_http_set_status(w, 200);
    neverc_http_set_header(w, "Content-Type", "text/plain; charset=utf-8");
    neverc_http_writef(w, "Hello from NeverC HTTP/2!\nMethod: %s\nPath: %s\n",
                       req->method ? req->method : "",
                       req->path ? req->path : "");
}

static void h2_cl_mismatch_handler(neverc_http_request_t *req,
                                   neverc_http_response_writer_t *w) {
    (void)req;
    (void)neverc_http_set_content_length(w, 3U);
    neverc_http_write_string(w, "abcdef");
}

static void h2_cl_short_handler(neverc_http_request_t *req,
                                neverc_http_response_writer_t *w) {
    (void)req;
    (void)neverc_http_set_content_length(w, 10U);
    neverc_http_write_string(w, "ab");
}

static void h2_stream_handler(neverc_http_request_t *req,
                              neverc_http_response_writer_t *w,
                              void *context) {
    char buf[64];
    int n = neverc_http_request_body_read(req, buf, sizeof(buf));
    (void)context;
    neverc_http_writef(w, "got=%d", n);
}

static void h2_run_server_child(h2_serve_ctx_t *ctx) {
    neverc_http_mux_t *mux = neverc_http_new_mux();
    if (!mux) _exit(1);
    if (ctx->streaming &&
        neverc_http_mux_handle_stream_context(
            mux, "POST /stream", h2_stream_handler, NULL) != 0)
        _exit(1);
    neverc_http_mux_handle(mux, "/", h2_test_handler);
    neverc_http_mux_handle(mux, "/cl-mismatch", h2_cl_mismatch_handler);
    neverc_http_mux_handle(mux, "/cl-short", h2_cl_short_handler);
    neverc_h2_server_t *server = neverc_h2_server_create(mux);
    if (!server) {
        neverc_http_mux_free(mux);
        _exit(1);
    }
    if (ctx->max_streams > 0)
        neverc_h2_server_set_max_streams(server, ctx->max_streams);
    if (ctx->initial_window > 0)
        neverc_h2_server_set_initial_window_size(server, ctx->initial_window);
    sock_set_timeout(ctx->fd, H2_TEST_IO_TIMEOUT_MS);
    int rc = neverc_h2_serve_conn(server, ctx->fd);
    neverc_h2_server_destroy(server);
    neverc_http_mux_free(mux);
    _exit(rc == 0 ? 0 : 1);
}

/* End-to-end: client sends minimal h2c request, server responds. */
TEST(h2c_serve_conn_roundtrip) {
    neverc_tcp_conn_t *client = NULL;
    neverc_tcp_conn_t *server = NULL;
    ASSERT_EQ(neverc_tcp_pipe(&client, &server), 0);

    int server_fd = neverc_tcp_conn_fd(server);
    ASSERT_TRUE(server_fd >= 0);

    h2_serve_ctx_t ctx = { .fd = server_fd };
    pid_t child = fork();
    ASSERT_TRUE(child >= 0);
    if (child == 0) {
        neverc_tcp_close(client);
        h2_run_server_child(&ctx);
    }
    neverc_tcp_close(server);

    int client_fd = neverc_tcp_conn_fd(client);
    ASSERT_TRUE(client_fd >= 0);
    sock_set_timeout(client_fd, H2_TEST_IO_TIMEOUT_MS);

    ASSERT_EQ(sock_write_all(client_fd, NC_H2_CLIENT_PREFACE,
                              NC_H2_CLIENT_PREFACE_LEN), 0);

    uint8_t settings_frame[9] = { 0, 0, 0, NC_H2_FRAME_SETTINGS, 0, 0, 0, 0, 0 };
    ASSERT_EQ(sock_write_all(client_fd, settings_frame, sizeof(settings_frame)), 0);
    ASSERT_EQ(h2_client_handshake(client_fd), 0);

    uint8_t req_hdr_frame[9] = {
        0, 0, 14, NC_H2_FRAME_HEADERS,
        (uint8_t)(NC_H2_FLAG_END_HEADERS | NC_H2_FLAG_END_STREAM),
        0, 0, 0, 1
    };
    uint8_t req_hpack[] = {
        0x82, 0x84, 0x86,
        0x01, 0x09, 'l', 'o', 'c', 'a', 'l', 'h', 'o', 's', 't'
    };
    ASSERT_EQ(sock_write_all(client_fd, req_hdr_frame, sizeof(req_hdr_frame)), 0);
    ASSERT_EQ(sock_write_all(client_fd, req_hpack, sizeof(req_hpack)), 0);

    char resp[2048];
    size_t resp_len = 0;
    while (resp_len < sizeof(resp) - 1) {
        ssize_t n = read(client_fd, resp + resp_len, sizeof(resp) - 1 - resp_len);
        if (n <= 0)
            break;
        resp_len += (size_t)n;
        if (buf_contains(resp, resp_len, "Hello from NeverC HTTP/2!") != NULL)
            break;
    }
    resp[resp_len] = '\0';
    ASSERT_TRUE(buf_contains(resp, resp_len, "Hello from NeverC HTTP/2!") != NULL);
    ASSERT_TRUE(buf_contains(resp, resp_len, "Method: GET") != NULL);
    ASSERT_TRUE(buf_contains(resp, resp_len, "Path: /") != NULL);

    neverc_tcp_close(client);

    int status = 0;
    ASSERT_EQ(h2_reap_child(child, &status), 0);
    ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0);

}

/* CONTINUATION: HEADERS without END_HEADERS + CONTINUATION with END_HEADERS */
TEST(h2c_continuation_headers) {
    neverc_tcp_conn_t *client = NULL;
    neverc_tcp_conn_t *server = NULL;
    ASSERT_EQ(neverc_tcp_pipe(&client, &server), 0);

    h2_serve_ctx_t ctx = {
        .fd = neverc_tcp_conn_fd(server)
    };
    pid_t child = fork();
    ASSERT_TRUE(child >= 0);
    if (child == 0) {
        neverc_tcp_close(client);
        h2_run_server_child(&ctx);
    }
    neverc_tcp_close(server);

    int client_fd = neverc_tcp_conn_fd(client);
    sock_set_timeout(client_fd, H2_TEST_IO_TIMEOUT_MS);
    ASSERT_EQ(sock_write_all(client_fd, NC_H2_CLIENT_PREFACE,
                              NC_H2_CLIENT_PREFACE_LEN), 0);

    uint8_t settings_frame[9] = { 0, 0, 0, NC_H2_FRAME_SETTINGS, 0, 0, 0, 0, 0 };
    ASSERT_EQ(sock_write_all(client_fd, settings_frame, sizeof(settings_frame)), 0);
    ASSERT_EQ(h2_client_handshake(client_fd), 0);

    uint8_t hdr1[9] = { 0, 0, 1, NC_H2_FRAME_HEADERS, NC_H2_FLAG_END_STREAM,
                        0, 0, 0, 1 };
    uint8_t hpack1[] = { 0x82 };
    uint8_t cont[9] = { 0, 0, 13, NC_H2_FRAME_CONTINUATION,
                        (uint8_t)NC_H2_FLAG_END_HEADERS, 0, 0, 0, 1 };
    uint8_t hpack2[] = {
        0x84, 0x86,
        0x01, 0x09, 'l', 'o', 'c', 'a', 'l', 'h', 'o', 's', 't'
    };

    ASSERT_EQ(sock_write_all(client_fd, hdr1, sizeof(hdr1)), 0);
    ASSERT_EQ(sock_write_all(client_fd, hpack1, sizeof(hpack1)), 0);
    ASSERT_EQ(sock_write_all(client_fd, cont, sizeof(cont)), 0);
    ASSERT_EQ(sock_write_all(client_fd, hpack2, sizeof(hpack2)), 0);

    char resp[2048];
    size_t resp_len = 0;
    while (resp_len < sizeof(resp) - 1) {
        ssize_t n = read(client_fd, resp + resp_len, sizeof(resp) - 1 - resp_len);
        if (n <= 0)
            break;
        resp_len += (size_t)n;
        if (buf_contains(resp, resp_len, "Path: /") != NULL)
            break;
    }
    resp[resp_len] = '\0';
    ASSERT_TRUE(buf_contains(resp, resp_len, "Path: /") != NULL);

    neverc_tcp_close(client);

    int status = 0;
    ASSERT_EQ(h2_reap_child(child, &status), 0);
    ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0);

}

static int h2_send_headers_on(int fd, uint32_t stream_id,
                              const neverc_hpack_header_t *headers,
                              int count, int end_stream) {
    neverc_hpack_encoder_t *enc = neverc_hpack_encoder_create(4096);
    if (!enc) return -1;
    uint8_t block[2048];
    size_t block_len = 0;
    int rc = neverc_hpack_encode(enc, headers, count, block, sizeof(block),
                                 &block_len);
    neverc_hpack_encoder_destroy(enc);
    if (rc != 0 || block_len == 0 || block_len > 0xffffffU) return -1;
    neverc_h2_frame_header_t frame = {
        .length = (uint32_t)block_len,
        .type = NC_H2_FRAME_HEADERS,
        .flags = (uint8_t)(NC_H2_FLAG_END_HEADERS |
                           (end_stream ? NC_H2_FLAG_END_STREAM : 0)),
        .stream_id = stream_id
    };
    uint8_t header[9];
    if (neverc_h2_frame_header_write(&frame, header) != 0) return -1;
    if (sock_write_all(fd, header, sizeof(header)) != 0) return -1;
    return sock_write_all(fd, block, block_len);
}

static int h2_send_headers(int fd, const neverc_hpack_header_t *headers,
                           int count, int end_stream) {
    return h2_send_headers_on(fd, 1, headers, count, end_stream);
}

static int h2_send_data_on(int fd, uint32_t stream_id, const void *data,
                           size_t length, int end_stream) {
    neverc_h2_frame_header_t frame = {
        .length = (uint32_t)length,
        .type = NC_H2_FRAME_DATA,
        .flags = end_stream ? (uint8_t)NC_H2_FLAG_END_STREAM : 0,
        .stream_id = stream_id
    };
    uint8_t header[9];
    if (neverc_h2_frame_header_write(&frame, header) != 0) return -1;
    if (sock_write_all(fd, header, sizeof(header)) != 0) return -1;
    return length == 0 ? 0 : sock_write_all(fd, data, length);
}

static int h2_send_data_end(int fd, const void *data, size_t length,
                            int end_stream) {
    return h2_send_data_on(fd, 1, data, length, end_stream);
}

static int h2_send_data(int fd, const void *data, size_t length) {
    return h2_send_data_end(fd, data, length, 0);
}

static int h2_send_priority_self(int fd, uint32_t stream_id) {
    uint8_t frame[14] = {
        0, 0, 5, NC_H2_FRAME_PRIORITY, 0,
        (uint8_t)((stream_id >> 24) & 0x7f),
        (uint8_t)(stream_id >> 16),
        (uint8_t)(stream_id >> 8),
        (uint8_t)stream_id,
        (uint8_t)((stream_id >> 24) & 0x7f),
        (uint8_t)(stream_id >> 16),
        (uint8_t)(stream_id >> 8),
        (uint8_t)stream_id,
        0
    };
    return sock_write_all(fd, frame, sizeof(frame));
}

static int h2_read_rst_on(int fd, uint32_t expect_stream,
                          uint32_t *error_code) {
    for (;;) {
        uint8_t header[9];
        if (sock_read_all(fd, header, sizeof(header)) != 0) return -1;
        uint32_t length = ((uint32_t)header[0] << 16) |
                          ((uint32_t)header[1] << 8) | header[2];
        uint32_t stream_id = ((uint32_t)(header[5] & 0x7f) << 24) |
                             ((uint32_t)header[6] << 16) |
                             ((uint32_t)header[7] << 8) | header[8];
        uint8_t payload[256];
        if (length > sizeof(payload)) {
            if (h2_drain_frame_payload(fd, length) != 0) return -1;
            continue;
        }
        if (length > 0 && sock_read_all(fd, payload, length) != 0)
            return -1;
        if (header[3] == NC_H2_FRAME_RST_STREAM &&
            stream_id == expect_stream && length == 4) {
            *error_code = ((uint32_t)payload[0] << 24) |
                          ((uint32_t)payload[1] << 16) |
                          ((uint32_t)payload[2] << 8) | payload[3];
            return 0;
        }
        if (header[3] == NC_H2_FRAME_GOAWAY)
            return -1;
    }
}

static int h2_read_rst(int fd, uint32_t *error_code) {
    return h2_read_rst_on(fd, 1, error_code);
}

static int h2_read_goaway(int fd, uint32_t *error_code) {
    for (;;) {
        uint8_t header[9];
        if (sock_read_all(fd, header, sizeof(header)) != 0) return -1;
        uint32_t length = ((uint32_t)header[0] << 16) |
                          ((uint32_t)header[1] << 8) | header[2];
        uint8_t payload[256];
        if (length > sizeof(payload)) {
            if (h2_drain_frame_payload(fd, length) != 0) return -1;
            continue;
        }
        if (length > 0 && sock_read_all(fd, payload, length) != 0)
            return -1;
        if (header[3] == NC_H2_FRAME_GOAWAY && length >= 8) {
            *error_code = ((uint32_t)payload[4] << 24) |
                          ((uint32_t)payload[5] << 16) |
                          ((uint32_t)payload[6] << 8) | payload[7];
            return 0;
        }
    }
}

static int h2_pipe_handshake_opts(neverc_tcp_conn_t **client, pid_t *child,
                                  int streaming, uint32_t max_streams) {
    neverc_tcp_conn_t *server = NULL;
    if (neverc_tcp_pipe(client, &server) != 0) return -1;
    h2_serve_ctx_t ctx = {
        .fd = neverc_tcp_conn_fd(server),
        .streaming = streaming,
        .max_streams = max_streams,
    };
    *child = fork();
    if (*child < 0) {
        neverc_tcp_close(*client);
        neverc_tcp_close(server);
        return -1;
    }
    if (*child == 0) {
        neverc_tcp_close(*client);
        h2_run_server_child(&ctx);
    }
    neverc_tcp_close(server);
    int fd = neverc_tcp_conn_fd(*client);
    sock_set_timeout(fd, H2_TEST_IO_TIMEOUT_MS);
    uint8_t settings[9] = { 0, 0, 0, NC_H2_FRAME_SETTINGS, 0, 0, 0, 0, 0 };
    if (sock_write_all(fd, NC_H2_CLIENT_PREFACE, NC_H2_CLIENT_PREFACE_LEN) != 0 ||
        sock_write_all(fd, settings, sizeof(settings)) != 0 ||
        h2_client_handshake(fd) != 0) {
        neverc_tcp_close(*client);
        *client = NULL;
        (void)h2_reap_child(*child, NULL);
        return -1;
    }
    return 0;
}

static int h2_pipe_handshake(neverc_tcp_conn_t **client, pid_t *child,
                             int streaming) {
    return h2_pipe_handshake_opts(client, child, streaming, 0);
}

TEST(h2c_rejects_missing_authority) {
    neverc_tcp_conn_t *client = NULL;
    pid_t child = -1;
    ASSERT_EQ(h2_pipe_handshake(&client, &child, 0), 0);
    int fd = neverc_tcp_conn_fd(client);
    neverc_hpack_header_t headers[] = {
        { .name = ":method", .value = "GET" },
        { .name = ":path", .value = "/" },
        { .name = ":scheme", .value = "http" },
    };
    ASSERT_EQ(h2_send_headers(fd, headers, 3, 1), 0);
    uint32_t error_code = 0xffffffffU;
    ASSERT_EQ(h2_read_rst(fd, &error_code), 0);
    ASSERT_EQ(error_code, NC_H2_PROTOCOL_ERROR);
    neverc_tcp_close(client);
    int status = 0;
    ASSERT_EQ(h2_reap_child(child, &status), 0);
    ASSERT_TRUE(WIFEXITED(status));
}

TEST(h2c_rejects_spaced_authority) {
    neverc_tcp_conn_t *client = NULL;
    pid_t child = -1;
    ASSERT_EQ(h2_pipe_handshake(&client, &child, 0), 0);
    int fd = neverc_tcp_conn_fd(client);
    neverc_hpack_header_t headers[] = {
        { .name = ":method", .value = "GET" },
        { .name = ":path", .value = "/" },
        { .name = ":scheme", .value = "http" },
        { .name = ":authority", .value = "foo bar" },
    };
    ASSERT_EQ(h2_send_headers(fd, headers, 4, 1), 0);
    uint32_t error_code = 0xffffffffU;
    ASSERT_EQ(h2_read_rst(fd, &error_code), 0);
    ASSERT_EQ(error_code, NC_H2_PROTOCOL_ERROR);
    neverc_tcp_close(client);
    int status = 0;
    ASSERT_EQ(h2_reap_child(child, &status), 0);
    ASSERT_TRUE(WIFEXITED(status));
}

static int h2c_expect_protocol_rst(neverc_hpack_header_t *headers, int count) {
    neverc_tcp_conn_t *client = NULL;
    pid_t child = -1;
    if (h2_pipe_handshake(&client, &child, 0) != 0) return -1;
    int fd = neverc_tcp_conn_fd(client);
    uint32_t error_code = 0xffffffffU;
    int ok = h2_send_headers(fd, headers, count, 1) == 0 &&
             h2_read_rst(fd, &error_code) == 0 &&
             error_code == NC_H2_PROTOCOL_ERROR;
    neverc_tcp_close(client);
    int status = 0;
    if (h2_reap_child(child, &status) != 0 || !WIFEXITED(status))
        return -1;
    return ok ? 0 : -1;
}

TEST(h2c_rejects_invalid_authority) {
    neverc_hpack_header_t port[] = {
        { .name = ":method", .value = "GET" },
        { .name = ":path", .value = "/" },
        { .name = ":scheme", .value = "http" },
        { .name = ":authority", .value = "localhost:99999" },
    };
    neverc_hpack_header_t ipv6[] = {
        { .name = ":method", .value = "GET" },
        { .name = ":path", .value = "/" },
        { .name = ":scheme", .value = "http" },
        { .name = ":authority", .value = "[::1" },
    };
    neverc_hpack_header_t comma[] = {
        { .name = ":method", .value = "GET" },
        { .name = ":path", .value = "/" },
        { .name = ":scheme", .value = "http" },
        { .name = ":authority", .value = "example.com,evil.com" },
    };
    neverc_hpack_header_t bare_v6[] = {
        { .name = ":method", .value = "GET" },
        { .name = ":path", .value = "/" },
        { .name = ":scheme", .value = "http" },
        { .name = ":authority", .value = "::1" },
    };
    neverc_hpack_header_t v6_comma[] = {
        { .name = ":method", .value = "GET" },
        { .name = ":path", .value = "/" },
        { .name = ":scheme", .value = "http" },
        { .name = ":authority", .value = "[::1,evil.com]" },
    };
    ASSERT_EQ(h2c_expect_protocol_rst(port, 4), 0);
    ASSERT_EQ(h2c_expect_protocol_rst(ipv6, 4), 0);
    ASSERT_EQ(h2c_expect_protocol_rst(comma, 4), 0);
    ASSERT_EQ(h2c_expect_protocol_rst(bare_v6, 4), 0);
    ASSERT_EQ(h2c_expect_protocol_rst(v6_comma, 4), 0);
}

TEST(h2c_rejects_invalid_path) {
    neverc_hpack_header_t spaced[] = {
        { .name = ":method", .value = "GET" },
        { .name = ":path", .value = "/foo bar" },
        { .name = ":scheme", .value = "http" },
        { .name = ":authority", .value = "localhost" },
    };
    neverc_hpack_header_t star[] = {
        { .name = ":method", .value = "GET" },
        { .name = ":path", .value = "*" },
        { .name = ":scheme", .value = "http" },
        { .name = ":authority", .value = "localhost" },
    };
    ASSERT_EQ(h2c_expect_protocol_rst(spaced, 4), 0);
    ASSERT_EQ(h2c_expect_protocol_rst(star, 4), 0);
}

TEST(h2c_rejects_empty_authority) {
    neverc_tcp_conn_t *client = NULL;
    pid_t child = -1;
    ASSERT_EQ(h2_pipe_handshake(&client, &child, 0), 0);
    int fd = neverc_tcp_conn_fd(client);
    neverc_hpack_header_t headers[] = {
        { .name = ":method", .value = "GET" },
        { .name = ":path", .value = "/" },
        { .name = ":scheme", .value = "http" },
        { .name = ":authority", .value = "" },
    };
    ASSERT_EQ(h2_send_headers(fd, headers, 4, 1), 0);
    uint32_t error_code = 0xffffffffU;
    ASSERT_EQ(h2_read_rst(fd, &error_code), 0);
    ASSERT_EQ(error_code, NC_H2_PROTOCOL_ERROR);
    neverc_tcp_close(client);
    int status = 0;
    ASSERT_EQ(h2_reap_child(child, &status), 0);
    ASSERT_TRUE(WIFEXITED(status));
}

TEST(h2c_rejects_empty_host) {
    neverc_tcp_conn_t *client = NULL;
    pid_t child = -1;
    ASSERT_EQ(h2_pipe_handshake(&client, &child, 0), 0);
    int fd = neverc_tcp_conn_fd(client);
    neverc_hpack_header_t headers[] = {
        { .name = ":method", .value = "GET" },
        { .name = ":path", .value = "/" },
        { .name = ":scheme", .value = "http" },
        { .name = "host", .value = "" },
    };
    ASSERT_EQ(h2_send_headers(fd, headers, 4, 1), 0);
    uint32_t error_code = 0xffffffffU;
    ASSERT_EQ(h2_read_rst(fd, &error_code), 0);
    ASSERT_EQ(error_code, NC_H2_PROTOCOL_ERROR);
    neverc_tcp_close(client);
    int status = 0;
    ASSERT_EQ(h2_reap_child(child, &status), 0);
    ASSERT_TRUE(WIFEXITED(status));
}

TEST(h2c_rejects_path_fragment) {
    neverc_tcp_conn_t *client = NULL;
    pid_t child = -1;
    ASSERT_EQ(h2_pipe_handshake(&client, &child, 0), 0);
    int fd = neverc_tcp_conn_fd(client);
    neverc_hpack_header_t headers[] = {
        { .name = ":method", .value = "GET" },
        { .name = ":path", .value = "/foo#bar" },
        { .name = ":scheme", .value = "http" },
        { .name = ":authority", .value = "localhost" },
    };
    ASSERT_EQ(h2_send_headers(fd, headers, 4, 1), 0);
    uint32_t error_code = 0xffffffffU;
    ASSERT_EQ(h2_read_rst(fd, &error_code), 0);
    ASSERT_EQ(error_code, NC_H2_PROTOCOL_ERROR);
    neverc_tcp_close(client);
    int status = 0;
    ASSERT_EQ(h2_reap_child(child, &status), 0);
    ASSERT_TRUE(WIFEXITED(status));
}

TEST(h2c_rejects_empty_authority_with_host) {
    neverc_tcp_conn_t *client = NULL;
    pid_t child = -1;
    ASSERT_EQ(h2_pipe_handshake(&client, &child, 0), 0);
    int fd = neverc_tcp_conn_fd(client);
    neverc_hpack_header_t headers[] = {
        { .name = ":method", .value = "GET" },
        { .name = ":path", .value = "/" },
        { .name = ":scheme", .value = "http" },
        { .name = ":authority", .value = "" },
        { .name = "host", .value = "victim.example" },
    };
    ASSERT_EQ(h2_send_headers(fd, headers, 5, 1), 0);
    uint32_t error_code = 0xffffffffU;
    ASSERT_EQ(h2_read_rst(fd, &error_code), 0);
    ASSERT_EQ(error_code, NC_H2_PROTOCOL_ERROR);
    neverc_tcp_close(client);
    int status = 0;
    ASSERT_EQ(h2_reap_child(child, &status), 0);
    ASSERT_TRUE(WIFEXITED(status));
}

TEST(h2c_rejects_empty_host_with_authority) {
    neverc_tcp_conn_t *client = NULL;
    pid_t child = -1;
    ASSERT_EQ(h2_pipe_handshake(&client, &child, 0), 0);
    int fd = neverc_tcp_conn_fd(client);
    neverc_hpack_header_t headers[] = {
        { .name = ":method", .value = "GET" },
        { .name = ":path", .value = "/" },
        { .name = ":scheme", .value = "http" },
        { .name = ":authority", .value = "victim.example" },
        { .name = "host", .value = "" },
    };
    ASSERT_EQ(h2_send_headers(fd, headers, 5, 1), 0);
    uint32_t error_code = 0xffffffffU;
    ASSERT_EQ(h2_read_rst(fd, &error_code), 0);
    ASSERT_EQ(error_code, NC_H2_PROTOCOL_ERROR);
    neverc_tcp_close(client);
    int status = 0;
    ASSERT_EQ(h2_reap_child(child, &status), 0);
    ASSERT_TRUE(WIFEXITED(status));
}

TEST(h2c_rejects_host_authority_mismatch) {
    neverc_tcp_conn_t *client = NULL;
    pid_t child = -1;
    ASSERT_EQ(h2_pipe_handshake(&client, &child, 0), 0);
    int fd = neverc_tcp_conn_fd(client);
    neverc_hpack_header_t headers[] = {
        { .name = ":method", .value = "GET" },
        { .name = ":path", .value = "/" },
        { .name = ":scheme", .value = "http" },
        { .name = ":authority", .value = "example.com" },
        { .name = "host", .value = "evil.com" },
    };
    ASSERT_EQ(h2_send_headers(fd, headers, 5, 1), 0);
    uint32_t error_code = 0xffffffffU;
    ASSERT_EQ(h2_read_rst(fd, &error_code), 0);
    ASSERT_EQ(error_code, NC_H2_PROTOCOL_ERROR);
    neverc_tcp_close(client);
    int status = 0;
    ASSERT_EQ(h2_reap_child(child, &status), 0);
    ASSERT_TRUE(WIFEXITED(status));
}

TEST(h2c_rejects_streaming_content_length_overrun) {
    neverc_tcp_conn_t *client = NULL;
    pid_t child = -1;
    ASSERT_EQ(h2_pipe_handshake(&client, &child, 1), 0);
    int fd = neverc_tcp_conn_fd(client);
    neverc_hpack_header_t headers[] = {
        { .name = ":method", .value = "POST" },
        { .name = ":path", .value = "/stream" },
        { .name = ":scheme", .value = "http" },
        { .name = ":authority", .value = "localhost" },
        { .name = "content-length", .value = "1" },
    };
    ASSERT_EQ(h2_send_headers(fd, headers, 5, 0), 0);
    ASSERT_EQ(h2_send_data(fd, "abcde", 5), 0);
    uint32_t error_code = 0xffffffffU;
    ASSERT_EQ(h2_read_rst(fd, &error_code), 0);
    ASSERT_EQ(error_code, NC_H2_PROTOCOL_ERROR);
    neverc_tcp_close(client);
    int status = 0;
    ASSERT_EQ(h2_reap_child(child, &status), 0);
    ASSERT_TRUE(WIFEXITED(status));
}

TEST(h2c_rejects_buffered_content_length_overrun) {
    neverc_tcp_conn_t *client = NULL;
    pid_t child = -1;
    ASSERT_EQ(h2_pipe_handshake(&client, &child, 0), 0);
    int fd = neverc_tcp_conn_fd(client);
    neverc_hpack_header_t headers[] = {
        { .name = ":method", .value = "POST" },
        { .name = ":path", .value = "/" },
        { .name = ":scheme", .value = "http" },
        { .name = ":authority", .value = "localhost" },
        { .name = "content-length", .value = "1" },
    };
    ASSERT_EQ(h2_send_headers(fd, headers, 5, 0), 0);
    ASSERT_EQ(h2_send_data_end(fd, "abcde", 5, 1), 0);
    uint32_t error_code = 0xffffffffU;
    ASSERT_EQ(h2_read_rst(fd, &error_code), 0);
    ASSERT_EQ(error_code, NC_H2_PROTOCOL_ERROR);
    neverc_tcp_close(client);
    int status = 0;
    ASSERT_EQ(h2_reap_child(child, &status), 0);
    ASSERT_TRUE(WIFEXITED(status));
}

TEST(h2c_rejects_content_length_underrun) {
    neverc_tcp_conn_t *client = NULL;
    pid_t child = -1;
    ASSERT_EQ(h2_pipe_handshake(&client, &child, 0), 0);
    int fd = neverc_tcp_conn_fd(client);
    neverc_hpack_header_t headers[] = {
        { .name = ":method", .value = "POST" },
        { .name = ":path", .value = "/" },
        { .name = ":scheme", .value = "http" },
        { .name = ":authority", .value = "localhost" },
        { .name = "content-length", .value = "5" },
    };
    ASSERT_EQ(h2_send_headers(fd, headers, 5, 0), 0);
    ASSERT_EQ(h2_send_data_end(fd, "abc", 3, 1), 0);
    uint32_t error_code = 0xffffffffU;
    ASSERT_EQ(h2_read_rst(fd, &error_code), 0);
    ASSERT_EQ(error_code, NC_H2_PROTOCOL_ERROR);
    neverc_tcp_close(client);
    int status = 0;
    ASSERT_EQ(h2_reap_child(child, &status), 0);
    ASSERT_TRUE(WIFEXITED(status));
}

TEST(h2c_rejects_end_stream_headers_with_content_length) {
    neverc_hpack_header_t headers[] = {
        { .name = ":method", .value = "POST" },
        { .name = ":path", .value = "/" },
        { .name = ":scheme", .value = "http" },
        { .name = ":authority", .value = "localhost" },
        { .name = "content-length", .value = "5" },
    };
    ASSERT_EQ(h2c_expect_protocol_rst(headers, 5), 0);
}

TEST(h2c_rejects_status_pseudo_on_request) {
    neverc_hpack_header_t headers[] = {
        { .name = ":method", .value = "GET" },
        { .name = ":path", .value = "/" },
        { .name = ":scheme", .value = "http" },
        { .name = ":authority", .value = "localhost" },
        { .name = ":status", .value = "200" },
    };
    ASSERT_EQ(h2c_expect_protocol_rst(headers, 5), 0);
}

TEST(h2c_rejects_missing_scheme) {
    neverc_hpack_header_t headers[] = {
        { .name = ":method", .value = "GET" },
        { .name = ":path", .value = "/" },
        { .name = ":authority", .value = "localhost" },
    };
    ASSERT_EQ(h2c_expect_protocol_rst(headers, 3), 0);
}

TEST(h2c_rejects_duplicate_method) {
    neverc_hpack_header_t headers[] = {
        { .name = ":method", .value = "GET" },
        { .name = ":method", .value = "POST" },
        { .name = ":path", .value = "/" },
        { .name = ":scheme", .value = "http" },
        { .name = ":authority", .value = "localhost" },
    };
    ASSERT_EQ(h2c_expect_protocol_rst(headers, 5), 0);
}

TEST(h2c_rejects_uppercase_header_name) {
    neverc_hpack_header_t headers[] = {
        { .name = ":method", .value = "GET" },
        { .name = ":path", .value = "/" },
        { .name = ":scheme", .value = "http" },
        { .name = ":authority", .value = "localhost" },
        { .name = "X-Foo", .value = "1" },
    };
    ASSERT_EQ(h2c_expect_protocol_rst(headers, 5), 0);
}

TEST(h2c_response_content_length_mismatch_is_reset) {
    neverc_tcp_conn_t *client = NULL;
    pid_t child = -1;
    ASSERT_EQ(h2_pipe_handshake(&client, &child, 0), 0);
    int fd = neverc_tcp_conn_fd(client);
    neverc_hpack_header_t headers[] = {
        { .name = ":method", .value = "GET" },
        { .name = ":path", .value = "/cl-mismatch" },
        { .name = ":scheme", .value = "http" },
        { .name = ":authority", .value = "localhost" },
    };
    ASSERT_EQ(h2_send_headers(fd, headers, 4, 1), 0);
    uint32_t error_code = 0xffffffffU;
    ASSERT_EQ(h2_read_rst(fd, &error_code), 0);
    ASSERT_EQ(error_code, NC_H2_INTERNAL_ERROR);
    neverc_tcp_close(client);
    int status = 0;
    ASSERT_EQ(h2_reap_child(child, &status), 0);
    ASSERT_TRUE(WIFEXITED(status));
}

TEST(h2c_response_content_length_underrun_is_reset) {
    neverc_tcp_conn_t *client = NULL;
    pid_t child = -1;
    ASSERT_EQ(h2_pipe_handshake(&client, &child, 0), 0);
    int fd = neverc_tcp_conn_fd(client);
    neverc_hpack_header_t headers[] = {
        { .name = ":method", .value = "GET" },
        { .name = ":path", .value = "/cl-short" },
        { .name = ":scheme", .value = "http" },
        { .name = ":authority", .value = "localhost" },
    };
    ASSERT_EQ(h2_send_headers(fd, headers, 4, 1), 0);
    uint32_t error_code = 0xffffffffU;
    ASSERT_EQ(h2_read_rst(fd, &error_code), 0);
    ASSERT_EQ(error_code, NC_H2_INTERNAL_ERROR);
    neverc_tcp_close(client);
    int status = 0;
    ASSERT_EQ(h2_reap_child(child, &status), 0);
    ASSERT_TRUE(WIFEXITED(status));
}

TEST(h2c_rejects_header_name_crlf) {
    neverc_tcp_conn_t *client = NULL;
    pid_t child = -1;
    ASSERT_EQ(h2_pipe_handshake(&client, &child, 0), 0);
    int fd = neverc_tcp_conn_fd(client);
    /* Encoder refuses CR/LF; send a raw literal so the decoder is tested. */
    uint8_t block[] = {
        0x82, 0x84, 0x86,
        0x01, 0x09, 'l', 'o', 'c', 'a', 'l', 'h', 'o', 's', 't',
        0x00, 0x11, 'x', '-', 'f', 'o', 'o', '\r', '\n',
        'x', '-', 'i', 'n', 'j', 'e', 'c', 't', 'e', 'd',
        0x01, '1'
    };
    neverc_h2_frame_header_t frame = {
        .length = (uint32_t)sizeof(block),
        .type = NC_H2_FRAME_HEADERS,
        .flags = (uint8_t)(NC_H2_FLAG_END_HEADERS | NC_H2_FLAG_END_STREAM),
        .stream_id = 1
    };
    uint8_t header[9];
    ASSERT_EQ(neverc_h2_frame_header_write(&frame, header), 0);
    ASSERT_EQ(sock_write_all(fd, header, sizeof(header)), 0);
    ASSERT_EQ(sock_write_all(fd, block, sizeof(block)), 0);
    uint32_t error_code = 0xffffffffU;
    ASSERT_EQ(h2_read_goaway(fd, &error_code), 0);
    ASSERT_EQ(error_code, NC_H2_COMPRESSION_ERROR);
    neverc_tcp_close(client);
    int status = 0;
    ASSERT_EQ(h2_reap_child(child, &status), 0);
    ASSERT_TRUE(WIFEXITED(status));
}

TEST(h2c_rejects_method_with_space) {
    neverc_tcp_conn_t *client = NULL;
    pid_t child = -1;
    ASSERT_EQ(h2_pipe_handshake(&client, &child, 0), 0);
    int fd = neverc_tcp_conn_fd(client);
    neverc_hpack_header_t headers[] = {
        { .name = ":method", .value = "GET /admin" },
        { .name = ":path", .value = "/" },
        { .name = ":scheme", .value = "http" },
        { .name = ":authority", .value = "localhost" },
    };
    ASSERT_EQ(h2_send_headers(fd, headers, 4, 1), 0);
    uint32_t error_code = 0xffffffffU;
    ASSERT_EQ(h2_read_rst(fd, &error_code), 0);
    ASSERT_EQ(error_code, NC_H2_PROTOCOL_ERROR);
    neverc_tcp_close(client);
    int status = 0;
    ASSERT_EQ(h2_reap_child(child, &status), 0);
    ASSERT_TRUE(WIFEXITED(status));
}

TEST(h2c_rejects_reused_refused_stream_id) {
    neverc_tcp_conn_t *client = NULL;
    pid_t child = -1;
    ASSERT_EQ(h2_pipe_handshake_opts(&client, &child, 0, 1), 0);
    int fd = neverc_tcp_conn_fd(client);
    neverc_hpack_header_t headers[] = {
        { .name = ":method", .value = "GET" },
        { .name = ":path", .value = "/" },
        { .name = ":scheme", .value = "http" },
        { .name = ":authority", .value = "localhost" },
    };
    ASSERT_EQ(h2_send_headers_on(fd, 1, headers, 4, 0), 0);
    ASSERT_EQ(h2_send_headers_on(fd, 3, headers, 4, 1), 0);
    uint32_t error_code = 0xffffffffU;
    ASSERT_EQ(h2_read_rst_on(fd, 3, &error_code), 0);
    ASSERT_EQ(error_code, NC_H2_REFUSED_STREAM);
    error_code = 0xffffffffU;
    ASSERT_EQ(h2_send_headers_on(fd, 3, headers, 4, 1), 0);
    ASSERT_EQ(h2_read_rst_on(fd, 3, &error_code), 0);
    ASSERT_EQ(error_code, NC_H2_STREAM_CLOSED);
    neverc_tcp_close(client);
    int status = 0;
    ASSERT_EQ(h2_reap_child(child, &status), 0);
    ASSERT_TRUE(WIFEXITED(status));
}

TEST(h2c_rejects_overpadded_data_on_closed_stream) {
    neverc_tcp_conn_t *client = NULL;
    pid_t child = -1;
    ASSERT_EQ(h2_pipe_handshake(&client, &child, 0), 0);
    int fd = neverc_tcp_conn_fd(client);
    neverc_hpack_header_t headers[] = {
        { .name = ":method", .value = "GET" },
        { .name = ":path", .value = "/" },
        { .name = ":scheme", .value = "http" },
        { .name = ":authority", .value = "localhost" },
    };
    ASSERT_EQ(h2_send_headers(fd, headers, 4, 1), 0);
    uint8_t data_hdr[9] = {
        0, 0, 1, NC_H2_FRAME_DATA, NC_H2_FLAG_PADDED, 0, 0, 0, 1
    };
    uint8_t pad_len = 1;
    ASSERT_EQ(sock_write_all(fd, data_hdr, sizeof(data_hdr)), 0);
    ASSERT_EQ(sock_write_all(fd, &pad_len, 1), 0);
    uint32_t error_code = 0xffffffffU;
    ASSERT_EQ(h2_read_goaway(fd, &error_code), 0);
    ASSERT_EQ(error_code, NC_H2_PROTOCOL_ERROR);
    neverc_tcp_close(client);
    int status = 0;
    ASSERT_EQ(h2_reap_child(child, &status), 0);
    ASSERT_TRUE(WIFEXITED(status));
}

TEST(h2c_window_update_overflow_is_connection_error) {
    neverc_tcp_conn_t *client = NULL;
    pid_t child = -1;
    ASSERT_EQ(h2_pipe_handshake(&client, &child, 0), 0);
    int fd = neverc_tcp_conn_fd(client);
    uint8_t window_update[13] = {
        0, 0, 4, NC_H2_FRAME_WINDOW_UPDATE, 0, 0, 0, 0, 0,
        0x7f, 0xff, 0xff, 0xff};
    ASSERT_EQ(sock_write_all(fd, window_update, sizeof(window_update)), 0);
    uint32_t error_code = 0xffffffffU;
    ASSERT_EQ(h2_read_goaway(fd, &error_code), 0);
    ASSERT_EQ(error_code, NC_H2_FLOW_CONTROL_ERROR);
    neverc_tcp_close(client);
    int status = 0;
    ASSERT_EQ(h2_reap_child(child, &status), 0);
    ASSERT_TRUE(WIFEXITED(status));
}

TEST(h2c_empty_data_does_not_send_zero_window_update) {
    neverc_tcp_conn_t *client = NULL;
    neverc_tcp_conn_t *server = NULL;
    ASSERT_EQ(neverc_tcp_pipe(&client, &server), 0);
    h2_serve_ctx_t ctx = {
        .fd = neverc_tcp_conn_fd(server),
        .initial_window = 8192,
    };
    pid_t child = fork();
    ASSERT_TRUE(child >= 0);
    if (child == 0) {
        neverc_tcp_close(client);
        h2_run_server_child(&ctx);
    }
    neverc_tcp_close(server);
    int fd = neverc_tcp_conn_fd(client);
    sock_set_timeout(fd, H2_TEST_IO_TIMEOUT_MS);
    uint8_t settings[9] = { 0, 0, 0, NC_H2_FRAME_SETTINGS, 0, 0, 0, 0, 0 };
    ASSERT_EQ(sock_write_all(fd, NC_H2_CLIENT_PREFACE,
                             NC_H2_CLIENT_PREFACE_LEN), 0);
    ASSERT_EQ(sock_write_all(fd, settings, sizeof(settings)), 0);
    ASSERT_EQ(h2_client_handshake(fd), 0);
    neverc_hpack_header_t headers[] = {
        { .name = ":method", .value = "GET" },
        { .name = ":path", .value = "/" },
        { .name = ":scheme", .value = "http" },
        { .name = ":authority", .value = "localhost" },
    };
    ASSERT_EQ(h2_send_headers(fd, headers, 4, 0), 0);
    ASSERT_EQ(h2_send_data_end(fd, NULL, 0, 1), 0);
    int saw_headers = 0;
    int saw_zero_update = 0;
    for (;;) {
        uint8_t header[9];
        if (sock_read_all(fd, header, sizeof(header)) != 0)
            break;
        uint32_t length = ((uint32_t)header[0] << 16) |
                          ((uint32_t)header[1] << 8) | header[2];
        uint8_t payload[256];
        if (length > sizeof(payload)) {
            if (h2_drain_frame_payload(fd, length) != 0)
                break;
            continue;
        }
        if (length > 0 && sock_read_all(fd, payload, length) != 0)
            break;
        if (header[3] == NC_H2_FRAME_WINDOW_UPDATE && length == 4) {
            uint32_t increment = ((uint32_t)(payload[0] & 0x7f) << 24) |
                                 ((uint32_t)payload[1] << 16) |
                                 ((uint32_t)payload[2] << 8) | payload[3];
            if (increment == 0)
                saw_zero_update = 1;
        }
        if (header[3] == NC_H2_FRAME_HEADERS) {
            saw_headers = 1;
            break;
        }
        if (header[3] == NC_H2_FRAME_GOAWAY)
            break;
    }
    ASSERT_TRUE(saw_headers);
    ASSERT_TRUE(!saw_zero_update);
    neverc_tcp_close(client);
    int status = 0;
    ASSERT_EQ(h2_reap_child(child, &status), 0);
    ASSERT_TRUE(WIFEXITED(status));
}

TEST(h2c_stream_window_update_overflow_is_connection_error) {
    neverc_tcp_conn_t *client = NULL;
    pid_t child = -1;
    ASSERT_EQ(h2_pipe_handshake(&client, &child, 0), 0);
    int fd = neverc_tcp_conn_fd(client);
    neverc_hpack_header_t headers[] = {
        { .name = ":method", .value = "GET" },
        { .name = ":path", .value = "/" },
        { .name = ":scheme", .value = "http" },
        { .name = ":authority", .value = "localhost" },
    };
    ASSERT_EQ(h2_send_headers(fd, headers, 4, 0), 0);
    uint8_t window_update[13] = {
        0, 0, 4, NC_H2_FRAME_WINDOW_UPDATE, 0, 0, 0, 0, 1,
        0x7f, 0xff, 0xff, 0xff};
    ASSERT_EQ(sock_write_all(fd, window_update, sizeof(window_update)), 0);
    uint32_t error_code = 0xffffffffU;
    ASSERT_EQ(h2_read_rst(fd, &error_code), 0);
    ASSERT_EQ(error_code, NC_H2_FLOW_CONTROL_ERROR);
    ASSERT_EQ(h2_send_data_end(fd, "x", 1, 1), 0);
    error_code = 0xffffffffU;
    ASSERT_EQ(h2_read_rst(fd, &error_code), 0);
    ASSERT_EQ(error_code, NC_H2_STREAM_CLOSED);
    neverc_tcp_close(client);
    int status = 0;
    ASSERT_EQ(h2_reap_child(child, &status), 0);
    ASSERT_TRUE(WIFEXITED(status));
}

TEST(h2c_priority_self_dependency_closes_idle_stream) {
    neverc_tcp_conn_t *client = NULL;
    pid_t child = -1;
    ASSERT_EQ(h2_pipe_handshake(&client, &child, 0), 0);
    int fd = neverc_tcp_conn_fd(client);
    ASSERT_EQ(h2_send_priority_self(fd, 1), 0);
    uint32_t error_code = 0xffffffffU;
    ASSERT_EQ(h2_read_rst(fd, &error_code), 0);
    ASSERT_EQ(error_code, NC_H2_PROTOCOL_ERROR);
    neverc_hpack_header_t headers[] = {
        { .name = ":method", .value = "GET" },
        { .name = ":path", .value = "/" },
        { .name = ":scheme", .value = "http" },
        { .name = ":authority", .value = "localhost" },
    };
    ASSERT_EQ(h2_send_headers(fd, headers, 4, 1), 0);
    error_code = 0xffffffffU;
    ASSERT_EQ(h2_read_rst(fd, &error_code), 0);
    ASSERT_EQ(error_code, NC_H2_STREAM_CLOSED);
    neverc_tcp_close(client);
    int status = 0;
    ASSERT_EQ(h2_reap_child(child, &status), 0);
    ASSERT_TRUE(WIFEXITED(status));
}

TEST(h2c_priority_self_dependency_aborts_open_stream) {
    neverc_tcp_conn_t *client = NULL;
    pid_t child = -1;
    ASSERT_EQ(h2_pipe_handshake(&client, &child, 0), 0);
    int fd = neverc_tcp_conn_fd(client);
    neverc_hpack_header_t headers[] = {
        { .name = ":method", .value = "GET" },
        { .name = ":path", .value = "/" },
        { .name = ":scheme", .value = "http" },
        { .name = ":authority", .value = "localhost" },
    };
    ASSERT_EQ(h2_send_headers(fd, headers, 4, 0), 0);
    ASSERT_EQ(h2_send_priority_self(fd, 1), 0);
    uint32_t error_code = 0xffffffffU;
    ASSERT_EQ(h2_read_rst(fd, &error_code), 0);
    ASSERT_EQ(error_code, NC_H2_PROTOCOL_ERROR);
    ASSERT_EQ(h2_send_data_end(fd, "x", 1, 1), 0);
    error_code = 0xffffffffU;
    ASSERT_EQ(h2_read_rst(fd, &error_code), 0);
    ASSERT_EQ(error_code, NC_H2_STREAM_CLOSED);
    neverc_tcp_close(client);
    int status = 0;
    ASSERT_EQ(h2_reap_child(child, &status), 0);
    ASSERT_TRUE(WIFEXITED(status));
}

TEST(h2c_continuation_flood_is_connection_error) {
    neverc_tcp_conn_t *client = NULL;
    pid_t child = -1;
    ASSERT_EQ(h2_pipe_handshake(&client, &child, 0), 0);
    int fd = neverc_tcp_conn_fd(client);
    signal(SIGPIPE, SIG_IGN);
    uint8_t headers[10] = {
        0, 0, 1, NC_H2_FRAME_HEADERS, NC_H2_FLAG_END_STREAM,
        0, 0, 0, 1, 0x82};
    ASSERT_EQ(sock_write_all(fd, headers, sizeof(headers)), 0);
    uint8_t continuation[9] = {
        0, 0, 0, NC_H2_FRAME_CONTINUATION, 0, 0, 0, 0, 1};
    for (int i = 0; i < 129; i++) {
        if (sock_write_all(fd, continuation, sizeof(continuation)) != 0)
            break;
    }
    uint32_t error_code = 0xffffffffU;
    ASSERT_EQ(h2_read_goaway(fd, &error_code), 0);
    ASSERT_EQ(error_code, NC_H2_ENHANCE_YOUR_CALM);
    neverc_tcp_close(client);
    int status = 0;
    ASSERT_EQ(h2_reap_child(child, &status), 0);
    ASSERT_TRUE(WIFEXITED(status));
}

TEST(h2c_continuation_without_headers_is_connection_error) {
    neverc_tcp_conn_t *client = NULL;
    pid_t child = -1;
    ASSERT_EQ(h2_pipe_handshake(&client, &child, 0), 0);
    int fd = neverc_tcp_conn_fd(client);
    uint8_t continuation[10] = {
        0, 0, 1, NC_H2_FRAME_CONTINUATION, NC_H2_FLAG_END_HEADERS,
        0, 0, 0, 1, 0x82};
    ASSERT_EQ(sock_write_all(fd, continuation, sizeof(continuation)), 0);
    uint32_t error_code = 0xffffffffU;
    ASSERT_EQ(h2_read_goaway(fd, &error_code), 0);
    ASSERT_EQ(error_code, NC_H2_PROTOCOL_ERROR);
    neverc_tcp_close(client);
    int status = 0;
    ASSERT_EQ(h2_reap_child(child, &status), 0);
    ASSERT_TRUE(WIFEXITED(status));
}

TEST(h2c_rejects_bad_connection_preface) {
    neverc_tcp_conn_t *client = NULL;
    neverc_tcp_conn_t *server = NULL;
    ASSERT_EQ(neverc_tcp_pipe(&client, &server), 0);
    h2_serve_ctx_t ctx = { .fd = neverc_tcp_conn_fd(server) };
    pid_t child = fork();
    ASSERT_TRUE(child >= 0);
    if (child == 0) {
        neverc_tcp_close(client);
        h2_run_server_child(&ctx);
    }
    neverc_tcp_close(server);
    int fd = neverc_tcp_conn_fd(client);
    sock_set_timeout(fd, H2_TEST_IO_TIMEOUT_MS);
    char bad[NC_H2_CLIENT_PREFACE_LEN];
    memset(bad, 'X', sizeof(bad));
    ASSERT_EQ(sock_write_all(fd, bad, sizeof(bad)), 0);
    uint32_t error_code = 0xffffffffU;
    ASSERT_EQ(h2_read_goaway(fd, &error_code), 0);
    ASSERT_EQ(error_code, NC_H2_PROTOCOL_ERROR);
    neverc_tcp_close(client);
    int status = 0;
    ASSERT_EQ(h2_reap_child(child, &status), 0);
    ASSERT_TRUE(WIFEXITED(status));
}

TEST(h2c_rejects_push_promise) {
    neverc_tcp_conn_t *client = NULL;
    pid_t child = -1;
    ASSERT_EQ(h2_pipe_handshake(&client, &child, 0), 0);
    int fd = neverc_tcp_conn_fd(client);
    uint8_t push[13] = {
        0, 0, 4, NC_H2_FRAME_PUSH_PROMISE, 0, 0, 0, 0, 1,
        0, 0, 0, 2};
    ASSERT_EQ(sock_write_all(fd, push, sizeof(push)), 0);
    uint32_t error_code = 0xffffffffU;
    ASSERT_EQ(h2_read_goaway(fd, &error_code), 0);
    ASSERT_EQ(error_code, NC_H2_PROTOCOL_ERROR);
    neverc_tcp_close(client);
    int status = 0;
    ASSERT_EQ(h2_reap_child(child, &status), 0);
    ASSERT_TRUE(WIFEXITED(status));
}

TEST(h2c_rejects_settings_ack_with_payload) {
    neverc_tcp_conn_t *client = NULL;
    pid_t child = -1;
    ASSERT_EQ(h2_pipe_handshake(&client, &child, 0), 0);
    int fd = neverc_tcp_conn_fd(client);
    uint8_t ack[15] = {
        0, 0, 6, NC_H2_FRAME_SETTINGS, NC_H2_FLAG_ACK, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0};
    ASSERT_EQ(sock_write_all(fd, ack, sizeof(ack)), 0);
    uint32_t error_code = 0xffffffffU;
    ASSERT_EQ(h2_read_goaway(fd, &error_code), 0);
    ASSERT_EQ(error_code, NC_H2_FRAME_SIZE_ERROR);
    neverc_tcp_close(client);
    int status = 0;
    ASSERT_EQ(h2_reap_child(child, &status), 0);
    ASSERT_TRUE(WIFEXITED(status));
}

TEST(h2c_rejects_unsolicited_settings_ack) {
    neverc_tcp_conn_t *client = NULL;
    pid_t child = -1;
    ASSERT_EQ(h2_pipe_handshake(&client, &child, 0), 0);
    int fd = neverc_tcp_conn_fd(client);
    uint8_t ack[9] = {
        0, 0, 0, NC_H2_FRAME_SETTINGS, NC_H2_FLAG_ACK, 0, 0, 0, 0};
    ASSERT_EQ(sock_write_all(fd, ack, sizeof(ack)), 0);
    uint32_t error_code = 0xffffffffU;
    ASSERT_EQ(h2_read_goaway(fd, &error_code), 0);
    ASSERT_EQ(error_code, NC_H2_PROTOCOL_ERROR);
    neverc_tcp_close(client);
    int status = 0;
    ASSERT_EQ(h2_reap_child(child, &status), 0);
    ASSERT_TRUE(WIFEXITED(status));
}

TEST(h2c_window_update_zero_is_connection_error) {
    neverc_tcp_conn_t *client = NULL;
    pid_t child = -1;
    ASSERT_EQ(h2_pipe_handshake(&client, &child, 0), 0);
    int fd = neverc_tcp_conn_fd(client);
    uint8_t window_update[13] = {
        0, 0, 4, NC_H2_FRAME_WINDOW_UPDATE, 0, 0, 0, 0, 0,
        0, 0, 0, 0};
    ASSERT_EQ(sock_write_all(fd, window_update, sizeof(window_update)), 0);
    uint32_t error_code = 0xffffffffU;
    ASSERT_EQ(h2_read_goaway(fd, &error_code), 0);
    ASSERT_EQ(error_code, NC_H2_PROTOCOL_ERROR);
    neverc_tcp_close(client);
    int status = 0;
    ASSERT_EQ(h2_reap_child(child, &status), 0);
    ASSERT_TRUE(WIFEXITED(status));
}

TEST(h2c_stream_window_update_zero_is_stream_error) {
    neverc_tcp_conn_t *client = NULL;
    pid_t child = -1;
    ASSERT_EQ(h2_pipe_handshake(&client, &child, 0), 0);
    int fd = neverc_tcp_conn_fd(client);
    neverc_hpack_header_t headers[] = {
        { .name = ":method", .value = "GET" },
        { .name = ":path", .value = "/" },
        { .name = ":scheme", .value = "http" },
        { .name = ":authority", .value = "localhost" },
    };
    ASSERT_EQ(h2_send_headers(fd, headers, 4, 0), 0);
    uint8_t window_update[13] = {
        0, 0, 4, NC_H2_FRAME_WINDOW_UPDATE, 0, 0, 0, 0, 1,
        0, 0, 0, 0};
    ASSERT_EQ(sock_write_all(fd, window_update, sizeof(window_update)), 0);
    uint32_t error_code = 0xffffffffU;
    ASSERT_EQ(h2_read_rst(fd, &error_code), 0);
    ASSERT_EQ(error_code, NC_H2_PROTOCOL_ERROR);
    neverc_tcp_close(client);
    int status = 0;
    ASSERT_EQ(h2_reap_child(child, &status), 0);
    ASSERT_TRUE(WIFEXITED(status));
}

TEST(h2c_rejects_even_stream_id) {
    neverc_tcp_conn_t *client = NULL;
    pid_t child = -1;
    ASSERT_EQ(h2_pipe_handshake(&client, &child, 0), 0);
    int fd = neverc_tcp_conn_fd(client);
    neverc_hpack_header_t headers[] = {
        { .name = ":method", .value = "GET" },
        { .name = ":path", .value = "/" },
        { .name = ":scheme", .value = "http" },
        { .name = ":authority", .value = "localhost" },
    };
    ASSERT_EQ(h2_send_headers_on(fd, 2, headers, 4, 1), 0);
    uint32_t error_code = 0xffffffffU;
    ASSERT_EQ(h2_read_goaway(fd, &error_code), 0);
    ASSERT_EQ(error_code, NC_H2_PROTOCOL_ERROR);
    neverc_tcp_close(client);
    int status = 0;
    ASSERT_EQ(h2_reap_child(child, &status), 0);
    ASSERT_TRUE(WIFEXITED(status));
}

TEST(h2c_rejects_idle_even_data_after_odd_stream) {
    neverc_tcp_conn_t *client = NULL;
    pid_t child = -1;
    ASSERT_EQ(h2_pipe_handshake(&client, &child, 0), 0);
    int fd = neverc_tcp_conn_fd(client);
    neverc_hpack_header_t headers[] = {
        { .name = ":method", .value = "GET" },
        { .name = ":path", .value = "/" },
        { .name = ":scheme", .value = "http" },
        { .name = ":authority", .value = "localhost" },
    };
    ASSERT_EQ(h2_send_headers_on(fd, 5, headers, 4, 1), 0);
    ASSERT_EQ(h2_send_data_on(fd, 2, "x", 1, 1), 0);
    uint32_t error_code = 0xffffffffU;
    ASSERT_EQ(h2_read_goaway(fd, &error_code), 0);
    ASSERT_EQ(error_code, NC_H2_PROTOCOL_ERROR);
    neverc_tcp_close(client);
    int status = 0;
    ASSERT_EQ(h2_reap_child(child, &status), 0);
    ASSERT_TRUE(WIFEXITED(status));
}

TEST(h2c_headers_without_end_headers_then_data_is_connection_error) {
    neverc_tcp_conn_t *client = NULL;
    pid_t child = -1;
    ASSERT_EQ(h2_pipe_handshake(&client, &child, 0), 0);
    int fd = neverc_tcp_conn_fd(client);
    uint8_t headers[10] = {
        0, 0, 1, NC_H2_FRAME_HEADERS, 0, 0, 0, 0, 1, 0x82};
    ASSERT_EQ(sock_write_all(fd, headers, sizeof(headers)), 0);
    ASSERT_EQ(h2_send_data(fd, "x", 1), 0);
    uint32_t error_code = 0xffffffffU;
    ASSERT_EQ(h2_read_goaway(fd, &error_code), 0);
    ASSERT_EQ(error_code, NC_H2_PROTOCOL_ERROR);
    neverc_tcp_close(client);
    int status = 0;
    ASSERT_EQ(h2_reap_child(child, &status), 0);
    ASSERT_TRUE(WIFEXITED(status));
}

TEST(h2c_continuation_on_refused_stream_keeps_hpack) {
    neverc_tcp_conn_t *client = NULL;
    pid_t child = -1;
    ASSERT_EQ(h2_pipe_handshake_opts(&client, &child, 0, 1), 0);
    int fd = neverc_tcp_conn_fd(client);
    neverc_hpack_header_t headers[] = {
        { .name = ":method", .value = "GET" },
        { .name = ":path", .value = "/" },
        { .name = ":scheme", .value = "http" },
        { .name = ":authority", .value = "localhost" },
    };
    ASSERT_EQ(h2_send_headers_on(fd, 1, headers, 4, 0), 0);
    uint8_t hdr1[10] = {
        0, 0, 1, NC_H2_FRAME_HEADERS, NC_H2_FLAG_END_STREAM,
        0, 0, 0, 3, 0x82};
    uint8_t cont[22] = {
        0, 0, 13, NC_H2_FRAME_CONTINUATION, NC_H2_FLAG_END_HEADERS,
        0, 0, 0, 3,
        0x84, 0x86, 0x01, 0x09, 'l', 'o', 'c', 'a', 'l', 'h', 'o', 's', 't'};
    ASSERT_EQ(sock_write_all(fd, hdr1, sizeof(hdr1)), 0);
    ASSERT_EQ(sock_write_all(fd, cont, sizeof(cont)), 0);
    uint32_t error_code = 0xffffffffU;
    ASSERT_EQ(h2_read_rst_on(fd, 3, &error_code), 0);
    ASSERT_EQ(error_code, NC_H2_REFUSED_STREAM);
    ASSERT_EQ(h2_send_data_end(fd, NULL, 0, 1), 0);
    char resp[2048];
    size_t resp_len = 0;
    while (resp_len < sizeof(resp) - 1) {
        ssize_t n = read(fd, resp + resp_len, sizeof(resp) - 1 - resp_len);
        if (n <= 0)
            break;
        resp_len += (size_t)n;
        if (buf_contains(resp, resp_len, "Hello from NeverC HTTP/2!") != NULL)
            break;
    }
    ASSERT_TRUE(buf_contains(resp, resp_len, "Hello from NeverC HTTP/2!") != NULL);
    neverc_tcp_close(client);
    int status = 0;
    ASSERT_EQ(h2_reap_child(child, &status), 0);
    ASSERT_TRUE(WIFEXITED(status));
}

TEST(h2c_headers_priority_self_dependency_is_stream_error) {
    neverc_tcp_conn_t *client = NULL;
    pid_t child = -1;
    ASSERT_EQ(h2_pipe_handshake(&client, &child, 0), 0);
    int fd = neverc_tcp_conn_fd(client);
    uint8_t frame[28] = {
        0, 0, 19, NC_H2_FRAME_HEADERS,
        (uint8_t)(NC_H2_FLAG_END_HEADERS | NC_H2_FLAG_END_STREAM |
                  NC_H2_FLAG_PRIORITY),
        0, 0, 0, 1,
        0, 0, 0, 1, 16,
        0x82, 0x84, 0x86,
        0x01, 0x09, 'l', 'o', 'c', 'a', 'l', 'h', 'o', 's', 't'};
    ASSERT_EQ(sock_write_all(fd, frame, sizeof(frame)), 0);
    uint32_t error_code = 0xffffffffU;
    ASSERT_EQ(h2_read_rst(fd, &error_code), 0);
    ASSERT_EQ(error_code, NC_H2_PROTOCOL_ERROR);
    neverc_hpack_header_t headers[] = {
        { .name = ":method", .value = "GET" },
        { .name = ":path", .value = "/" },
        { .name = ":scheme", .value = "http" },
        { .name = ":authority", .value = "localhost" },
    };
    ASSERT_EQ(h2_send_headers(fd, headers, 4, 1), 0);
    error_code = 0xffffffffU;
    ASSERT_EQ(h2_read_rst(fd, &error_code), 0);
    ASSERT_EQ(error_code, NC_H2_STREAM_CLOSED);
    neverc_tcp_close(client);
    int status = 0;
    ASSERT_EQ(h2_reap_child(child, &status), 0);
    ASSERT_TRUE(WIFEXITED(status));
}

TEST(h2c_headers_priority_self_dependency_keeps_hpack) {
    neverc_tcp_conn_t *client = NULL;
    pid_t child = -1;
    ASSERT_EQ(h2_pipe_handshake(&client, &child, 0), 0);
    int fd = neverc_tcp_conn_fd(client);
    /* Incremental-indexing literal x-a:b must still enter the decoder table
     * even though PRIORITY self-dependency resets the stream. */
    uint8_t self_headers[35] = {
        0, 0, 26, NC_H2_FRAME_HEADERS,
        (uint8_t)(NC_H2_FLAG_END_HEADERS | NC_H2_FLAG_END_STREAM |
                  NC_H2_FLAG_PRIORITY),
        0, 0, 0, 1,
        0, 0, 0, 1, 16,
        0x82, 0x84, 0x86,
        0x01, 0x09, 'l', 'o', 'c', 'a', 'l', 'h', 'o', 's', 't',
        0x40, 0x03, 'x', '-', 'a', 0x01, 'b'};
    ASSERT_EQ(sock_write_all(fd, self_headers, sizeof(self_headers)), 0);
    uint32_t error_code = 0xffffffffU;
    ASSERT_EQ(h2_read_rst(fd, &error_code), 0);
    ASSERT_EQ(error_code, NC_H2_PROTOCOL_ERROR);
    uint8_t next[24] = {
        0, 0, 15, NC_H2_FRAME_HEADERS,
        (uint8_t)(NC_H2_FLAG_END_HEADERS | NC_H2_FLAG_END_STREAM),
        0, 0, 0, 3,
        0x82, 0x84, 0x86,
        0x01, 0x09, 'l', 'o', 'c', 'a', 'l', 'h', 'o', 's', 't',
        0xbe};
    ASSERT_EQ(sock_write_all(fd, next, sizeof(next)), 0);
    char resp[2048];
    size_t resp_len = 0;
    while (resp_len < sizeof(resp) - 1) {
        ssize_t n = read(fd, resp + resp_len, sizeof(resp) - 1 - resp_len);
        if (n <= 0)
            break;
        resp_len += (size_t)n;
        if (buf_contains(resp, resp_len, "Hello from NeverC HTTP/2!") != NULL)
            break;
    }
    ASSERT_TRUE(buf_contains(resp, resp_len, "Hello from NeverC HTTP/2!") != NULL);
    neverc_tcp_close(client);
    int status = 0;
    ASSERT_EQ(h2_reap_child(child, &status), 0);
    ASSERT_TRUE(WIFEXITED(status));
}

TEST(h2c_connection_window_ignores_stream_initial_size) {
    neverc_tcp_conn_t *client = NULL;
    neverc_tcp_conn_t *server = NULL;
    ASSERT_EQ(neverc_tcp_pipe(&client, &server), 0);
    h2_serve_ctx_t ctx = {
        .fd = neverc_tcp_conn_fd(server),
        .initial_window = 8192,
    };
    pid_t child = fork();
    ASSERT_TRUE(child >= 0);
    if (child == 0) {
        neverc_tcp_close(client);
        h2_run_server_child(&ctx);
    }
    neverc_tcp_close(server);
    int fd = neverc_tcp_conn_fd(client);
    sock_set_timeout(fd, H2_TEST_IO_TIMEOUT_MS);
    uint8_t settings[9] = { 0, 0, 0, NC_H2_FRAME_SETTINGS, 0, 0, 0, 0, 0 };
    ASSERT_EQ(sock_write_all(fd, NC_H2_CLIENT_PREFACE,
                             NC_H2_CLIENT_PREFACE_LEN), 0);
    ASSERT_EQ(sock_write_all(fd, settings, sizeof(settings)), 0);
    ASSERT_EQ(h2_client_handshake(fd), 0);
    neverc_hpack_header_t headers[] = {
        { .name = ":method", .value = "POST" },
        { .name = ":path", .value = "/" },
        { .name = ":scheme", .value = "http" },
        { .name = ":authority", .value = "localhost" },
    };
    char body[5000];
    memset(body, 'a', sizeof(body));
    ASSERT_EQ(h2_send_headers_on(fd, 1, headers, 4, 0), 0);
    ASSERT_EQ(h2_send_data_on(fd, 1, body, sizeof(body), 0), 0);
    ASSERT_EQ(h2_send_headers_on(fd, 3, headers, 4, 0), 0);
    ASSERT_EQ(h2_send_data_on(fd, 3, body, sizeof(body), 1), 0);
    ASSERT_EQ(h2_send_data_on(fd, 1, NULL, 0, 1), 0);
    char resp[4096];
    size_t resp_len = 0;
    int hellos = 0;
    int saw_goaway = 0;
    while (resp_len < sizeof(resp) - 1) {
        ssize_t n = read(fd, resp + resp_len, sizeof(resp) - 1 - resp_len);
        if (n <= 0)
            break;
        size_t start = resp_len;
        resp_len += (size_t)n;
        for (size_t i = start; i + 9 <= resp_len; i++) {
            if ((unsigned char)resp[i + 3] == NC_H2_FRAME_GOAWAY)
                saw_goaway = 1;
        }
        const char *p = resp;
        hellos = 0;
        while ((p = buf_contains(p, (size_t)(resp + resp_len - p),
                                 "Hello from NeverC HTTP/2!")) != NULL) {
            hellos++;
            p += 25;
        }
        if (hellos >= 2 || saw_goaway)
            break;
    }
    ASSERT_TRUE(!saw_goaway);
    ASSERT_TRUE(hellos >= 2);
    neverc_tcp_close(client);
    int status = 0;
    ASSERT_EQ(h2_reap_child(child, &status), 0);
    ASSERT_TRUE(WIFEXITED(status));
}

static void h2_run_adversarial_response_child(
    neverc_tcp_listener_t *listener, int response_kind, int notify_fd) {
    const char *error = NULL;
    neverc_tcp_conn_t *connection = neverc_tcp_accept(listener, &error);
    if (!connection) _exit(1);
    int fd = neverc_tcp_conn_fd(connection);
    sock_set_timeout(fd, H2_TEST_IO_TIMEOUT_MS);
    char preface[NC_H2_CLIENT_PREFACE_LEN];
    if (sock_read_all(fd, preface, sizeof(preface)) != 0 ||
        memcmp(preface, NC_H2_CLIENT_PREFACE, sizeof(preface)) != 0)
        _exit(1);

    uint8_t header[NC_H2_FRAME_HEADER_SIZE];
    if (sock_read_all(fd, header, sizeof(header)) != 0)
        _exit(1);
    uint32_t length = ((uint32_t)header[0] << 16) |
                      ((uint32_t)header[1] << 8) | header[2];
    if (header[3] != NC_H2_FRAME_SETTINGS ||
        h2_drain_frame_payload(fd, length) != 0)
        _exit(1);

    if (response_kind == 2) {
        uint8_t forbidden_settings[NC_H2_FRAME_HEADER_SIZE] = {
            0, 0, 6, NC_H2_FRAME_SETTINGS, 0, 0, 0, 0, 0};
        uint8_t enable_push[] = {
            0, NC_H2_SETTINGS_ENABLE_PUSH, 0, 0, 0, 1};
        if (sock_write_all(fd, forbidden_settings,
                           sizeof(forbidden_settings)) != 0 ||
            sock_write_all(fd, enable_push, sizeof(enable_push)) != 0)
            _exit(1);
        int got_frame = sock_read_all(fd, header, sizeof(header)) == 0;
        if (write(notify_fd, "x", 1U) != 1)
            _exit(1);
        if (got_frame &&
            (header[3] != NC_H2_FRAME_SETTINGS ||
             !(header[4] & NC_H2_FLAG_ACK)))
            _exit(1);
        char drain[256];
        while (got_frame && read(fd, drain, sizeof(drain)) > 0) { }
        neverc_tcp_close(connection);
        neverc_tcp_listener_close(listener);
        close(notify_fd);
        _exit(0);
    }

    if (response_kind == 3) {
        uint8_t large_table_settings[NC_H2_FRAME_HEADER_SIZE] = {
            0, 0, 6, NC_H2_FRAME_SETTINGS, 0, 0, 0, 0, 0};
        uint8_t table_size[] = {
            0, NC_H2_SETTINGS_HEADER_TABLE_SIZE, 0, 1, 0, 0};
        uint8_t settings_ack[NC_H2_FRAME_HEADER_SIZE] = {
            0, 0, 0, NC_H2_FRAME_SETTINGS, NC_H2_FLAG_ACK, 0, 0, 0, 0};
        if (sock_write_all(fd, large_table_settings,
                           sizeof(large_table_settings)) != 0 ||
            sock_write_all(fd, table_size, sizeof(table_size)) != 0 ||
            sock_write_all(fd, settings_ack, sizeof(settings_ack)) != 0)
            _exit(1);
        if (write(notify_fd, "x", 1U) != 1)
            _exit(1);
        char drain[256];
        while (read(fd, drain, sizeof(drain)) > 0) { }
        neverc_tcp_close(connection);
        neverc_tcp_listener_close(listener);
        close(notify_fd);
        _exit(0);
    }

    if (response_kind == 4) {
        uint8_t server_settings[NC_H2_FRAME_HEADER_SIZE] = {
            0, 0, 0, NC_H2_FRAME_SETTINGS, 0, 0, 0, 0, 0};
        uint8_t settings_ack[NC_H2_FRAME_HEADER_SIZE] = {
            0, 0, 0, NC_H2_FRAME_SETTINGS, NC_H2_FLAG_ACK, 0, 0, 0, 0};
        uint8_t idle_data[10] = {
            0, 0, 1, NC_H2_FRAME_DATA, 0, 0, 0, 0, 5, 'x'};
        if (sock_write_all(fd, server_settings,
                           sizeof(server_settings)) != 0 ||
            sock_write_all(fd, settings_ack, sizeof(settings_ack)) != 0 ||
            sock_write_all(fd, idle_data, sizeof(idle_data)) != 0)
            _exit(1);
        if (write(notify_fd, "x", 1U) != 1)
            _exit(1);
        char drain[256];
        while (read(fd, drain, sizeof(drain)) > 0) { }
        neverc_tcp_close(connection);
        neverc_tcp_listener_close(listener);
        close(notify_fd);
        _exit(0);
    }

    if (response_kind == 5) {
        uint8_t server_settings[NC_H2_FRAME_HEADER_SIZE] = {
            0, 0, 0, NC_H2_FRAME_SETTINGS, 0, 0, 0, 0, 0};
        uint8_t settings_ack[NC_H2_FRAME_HEADER_SIZE] = {
            0, 0, 0, NC_H2_FRAME_SETTINGS, NC_H2_FLAG_ACK, 0, 0, 0, 0};
        uint8_t self_priority[14] = {
            0, 0, 5, NC_H2_FRAME_PRIORITY, 0, 0, 0, 0, 1,
            0, 0, 0, 1, 0};
        if (sock_write_all(fd, server_settings,
                           sizeof(server_settings)) != 0 ||
            sock_write_all(fd, settings_ack, sizeof(settings_ack)) != 0 ||
            sock_write_all(fd, self_priority, sizeof(self_priority)) != 0)
            _exit(1);
        if (write(notify_fd, "x", 1U) != 1)
            _exit(1);
        char drain[256];
        while (read(fd, drain, sizeof(drain)) > 0) { }
        neverc_tcp_close(connection);
        neverc_tcp_listener_close(listener);
        close(notify_fd);
        _exit(0);
    }

    if (response_kind == 6) {
        uint8_t server_settings[NC_H2_FRAME_HEADER_SIZE] = {
            0, 0, 0, NC_H2_FRAME_SETTINGS, 0, 0, 0, 0, 0};
        uint8_t settings_ack[NC_H2_FRAME_HEADER_SIZE] = {
            0, 0, 0, NC_H2_FRAME_SETTINGS, NC_H2_FLAG_ACK, 0, 0, 0, 0};
        uint8_t push[13] = {
            0, 0, 4, NC_H2_FRAME_PUSH_PROMISE, 0, 0, 0, 0, 1,
            0, 0, 0, 2};
        if (sock_write_all(fd, server_settings,
                           sizeof(server_settings)) != 0 ||
            sock_write_all(fd, settings_ack, sizeof(settings_ack)) != 0 ||
            sock_write_all(fd, push, sizeof(push)) != 0)
            _exit(1);
        if (write(notify_fd, "x", 1U) != 1)
            _exit(1);
        char drain[256];
        while (read(fd, drain, sizeof(drain)) > 0) { }
        neverc_tcp_close(connection);
        neverc_tcp_listener_close(listener);
        close(notify_fd);
        _exit(0);
    }

    if (response_kind == 7) {
        uint8_t server_settings[NC_H2_FRAME_HEADER_SIZE] = {
            0, 0, 0, NC_H2_FRAME_SETTINGS, 0, 0, 0, 0, 0};
        uint8_t settings_ack[NC_H2_FRAME_HEADER_SIZE] = {
            0, 0, 0, NC_H2_FRAME_SETTINGS, NC_H2_FLAG_ACK, 0, 0, 0, 0};
        uint8_t idle_rst[13] = {
            0, 0, 4, NC_H2_FRAME_RST_STREAM, 0, 0, 0, 0, 1,
            0, 0, 0, NC_H2_CANCEL};
        if (sock_write_all(fd, server_settings,
                           sizeof(server_settings)) != 0 ||
            sock_write_all(fd, settings_ack, sizeof(settings_ack)) != 0 ||
            sock_write_all(fd, idle_rst, sizeof(idle_rst)) != 0)
            _exit(1);
        if (write(notify_fd, "x", 1U) != 1)
            _exit(1);
        char drain[256];
        while (read(fd, drain, sizeof(drain)) > 0) { }
        neverc_tcp_close(connection);
        neverc_tcp_listener_close(listener);
        close(notify_fd);
        _exit(0);
    }

    if (response_kind == 8) {
        uint8_t extra_ack[NC_H2_FRAME_HEADER_SIZE] = {
            0, 0, 0, NC_H2_FRAME_SETTINGS, NC_H2_FLAG_ACK, 0, 0, 0, 0};
        uint8_t server_settings[NC_H2_FRAME_HEADER_SIZE] = {
            0, 0, 0, NC_H2_FRAME_SETTINGS, 0, 0, 0, 0, 0};
        uint8_t settings_ack[NC_H2_FRAME_HEADER_SIZE] = {
            0, 0, 0, NC_H2_FRAME_SETTINGS, NC_H2_FLAG_ACK, 0, 0, 0, 0};
        if (sock_write_all(fd, server_settings,
                           sizeof(server_settings)) != 0 ||
            sock_write_all(fd, settings_ack, sizeof(settings_ack)) != 0 ||
            sock_write_all(fd, extra_ack, sizeof(extra_ack)) != 0)
            _exit(1);
        if (write(notify_fd, "x", 1U) != 1)
            _exit(1);
        char drain[256];
        while (read(fd, drain, sizeof(drain)) > 0) { }
        neverc_tcp_close(connection);
        neverc_tcp_listener_close(listener);
        close(notify_fd);
        _exit(0);
    }

    uint8_t server_settings[NC_H2_FRAME_HEADER_SIZE] = {
        0, 0, 0, NC_H2_FRAME_SETTINGS, 0, 0, 0, 0, 0};
    uint8_t settings_ack[NC_H2_FRAME_HEADER_SIZE] = {
        0, 0, 0, NC_H2_FRAME_SETTINGS, NC_H2_FLAG_ACK, 0, 0, 0, 0};
    if (sock_write_all(fd, server_settings, sizeof(server_settings)) != 0 ||
        sock_write_all(fd, settings_ack, sizeof(settings_ack)) != 0)
        _exit(1);

    int request_headers_seen = 0;
    while (!request_headers_seen) {
        if (sock_read_all(fd, header, sizeof(header)) != 0)
            _exit(1);
        length = ((uint32_t)header[0] << 16) |
                 ((uint32_t)header[1] << 8) | header[2];
        uint32_t stream_id = ((uint32_t)(header[5] & 0x7f) << 24) |
                             ((uint32_t)header[6] << 16) |
                             ((uint32_t)header[7] << 8) | header[8];
        request_headers_seen = header[3] == NC_H2_FRAME_HEADERS &&
                               stream_id == 1U;
        if (h2_drain_frame_payload(fd, length) != 0)
            _exit(1);
    }

    if (response_kind == 9) {
        uint8_t self_headers[15] = {
            0, 0, 6, NC_H2_FRAME_HEADERS,
            (uint8_t)(NC_H2_FLAG_END_HEADERS | NC_H2_FLAG_PRIORITY),
            0, 0, 0, 1,
            0, 0, 0, 1, 0, 0x88};
        if (sock_write_all(fd, self_headers, sizeof(self_headers)) != 0)
            _exit(1);
        char drain[256];
        while (read(fd, drain, sizeof(drain)) > 0) { }
        neverc_tcp_close(connection);
        neverc_tcp_listener_close(listener);
        _exit(0);
    }

    uint8_t response_header[NC_H2_FRAME_HEADER_SIZE] = {
        0, 0, 1, NC_H2_FRAME_HEADERS, NC_H2_FLAG_END_HEADERS,
        0, 0, 0, 1};
    uint8_t invalid_trailer_header[NC_H2_FRAME_HEADER_SIZE] = {
        0, 0, 1, NC_H2_FRAME_HEADERS,
        (uint8_t)(NC_H2_FLAG_END_HEADERS | NC_H2_FLAG_END_STREAM),
        0, 0, 0, 1};
    uint8_t status_200 = 0x88;
    if (sock_write_all(fd, response_header, sizeof(response_header)) != 0 ||
        sock_write_all(fd, &status_200, 1U) != 0)
        _exit(1);
    if (response_kind == 0) {
        if (sock_write_all(fd, invalid_trailer_header,
                           sizeof(invalid_trailer_header)) != 0 ||
            sock_write_all(fd, &status_200, 1U) != 0)
            _exit(1);
    } else {
        uint8_t data_header[NC_H2_FRAME_HEADER_SIZE] = {
            0, 0, 1, NC_H2_FRAME_DATA, 0, 0, 0, 0, 1};
        uint8_t body = 'a';
        for (int i = 0; i < 12; i++) {
            if (sock_write_all(fd, data_header, sizeof(data_header)) != 0 ||
                sock_write_all(fd, &body, 1U) != 0)
                _exit(1);
        }
    }
    /*
     * The client SETTINGS ACK and request HEADERS are written by different
     * threads.  If HEADERS arrives first, closing with the ACK unread resets
     * the socket and can discard the adversarial response still in flight.
     */
    char drain[256];
    while (read(fd, drain, sizeof(drain)) > 0) { }
    neverc_tcp_close(connection);
    neverc_tcp_listener_close(listener);
    _exit(0);
}

TEST(h2_client_invalid_trailer_emits_terminal_error) {
    const char *error = NULL;
    neverc_tcp_listener_t *listener =
        neverc_tcp_listen("127.0.0.1:0", &error);
    ASSERT_TRUE(listener != NULL);
    neverc_tcp_addr_t address;
    ASSERT_EQ(neverc_tcp_listener_addr(listener, &address), 0);

    pid_t child = fork();
    ASSERT_TRUE(child >= 0);
    if (child == 0)
        h2_run_adversarial_response_child(listener, 0, -1);
    neverc_tcp_listener_close(listener);

    char dial_address[64];
    snprintf(dial_address, sizeof(dial_address), "127.0.0.1:%d",
             address.port);
    neverc_h2_client_config_t config = neverc_h2_client_config_default();
    config.timeout_ms = 3000;
    neverc_h2_client_t *client = neverc_h2_client_dial(
        dial_address, "localhost", 0, &config, &error);
    neverc_h2_client_stream_t *stream = client
        ? neverc_h2_client_stream_open(client, NULL, "GET", "/",
                                       NULL, 0U, 1, &error)
        : NULL;
    neverc_context_t *background = neverc_context_background();
    neverc_context_t *context = background
        ? neverc_context_with_timeout(background, 2000, NULL) : NULL;
    neverc_h2_client_event_t *headers = NULL;
    neverc_h2_client_event_t *terminal = NULL;
    int first = stream && context
        ? neverc_h2_client_stream_receive(stream, context, &headers) : -1;
    int second = first == 1
        ? neverc_h2_client_stream_receive(stream, context, &terminal) : -1;
    int header_type = headers ? (int)headers->type : -1;
    const char *header_error = headers ? headers->error : NULL;
    uint32_t header_error_code = headers ? headers->error_code : 0;
    int terminal_type = terminal ? (int)terminal->type : -1;
    const char *terminal_error = terminal ? terminal->error : NULL;
    uint32_t terminal_error_code = terminal ? terminal->error_code : 0;

    if (!client)
        kill(child, SIGTERM);

    neverc_h2_client_event_free(headers);
    neverc_h2_client_event_free(terminal);
    neverc_context_free(context);
    neverc_context_free(background);
    neverc_h2_client_stream_free(stream);
    neverc_h2_client_free(client);
    int status = 0;
    (void)h2_reap_child(child, &status);
    ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    if (first != 1 || header_type != NEVERC_H2_CLIENT_EVENT_HEADERS ||
        second != 1 || terminal_type != NEVERC_H2_CLIENT_EVENT_ERROR ||
        terminal_error_code != NC_H2_PROTOCOL_ERROR) {
        printf("\n    first=%d header_type=%d header_code=%u header_error=%s"
               " second=%d terminal_type=%d terminal_code=%u"
               " terminal_error=%s\n    ",
               first, header_type, header_error_code,
               header_error ? header_error : "(null)", second, terminal_type,
               terminal_error_code,
               terminal_error ? terminal_error : "(null)");
    }
    ASSERT_EQ(first, 1);
    ASSERT_EQ(header_type, NEVERC_H2_CLIENT_EVENT_HEADERS);
    ASSERT_EQ(second, 1);
    ASSERT_EQ(terminal_type, NEVERC_H2_CLIENT_EVENT_ERROR);
    ASSERT_EQ(terminal_error_code, NC_H2_PROTOCOL_ERROR);
}

TEST(h2_client_queue_overflow_emits_terminal_error) {
    const char *error = NULL;
    neverc_tcp_listener_t *listener =
        neverc_tcp_listen("127.0.0.1:0", &error);
    ASSERT_TRUE(listener != NULL);
    neverc_tcp_addr_t address;
    ASSERT_EQ(neverc_tcp_listener_addr(listener, &address), 0);

    pid_t child = fork();
    ASSERT_TRUE(child >= 0);
    if (child == 0)
        h2_run_adversarial_response_child(listener, 1, -1);
    neverc_tcp_listener_close(listener);

    char dial_address[64];
    snprintf(dial_address, sizeof(dial_address), "127.0.0.1:%d",
             address.port);
    neverc_h2_client_config_t config = neverc_h2_client_config_default();
    config.timeout_ms = 3000;
    neverc_h2_client_t *client = neverc_h2_client_dial(
        dial_address, "localhost", 0, &config, &error);
    neverc_h2_client_stream_t *stream = client
        ? neverc_h2_client_stream_open(client, NULL, "GET", "/",
                                       NULL, 0U, 1, &error)
        : NULL;
    usleep(300000);

    neverc_context_t *background = neverc_context_background();
    neverc_context_t *context = background
        ? neverc_context_with_timeout(background, 2000, NULL) : NULL;
    int saw_error = 0;
    for (int i = 0; stream && context && i < 16; i++) {
        neverc_h2_client_event_t *event = NULL;
        int received = neverc_h2_client_stream_receive(
            stream, context, &event);
        if (received != 1) break;
        if (event && event->type == NEVERC_H2_CLIENT_EVENT_ERROR)
            saw_error = 1;
        neverc_h2_client_event_free(event);
        if (saw_error) break;
    }
    if (!client)
        kill(child, SIGTERM);

    neverc_context_free(context);
    neverc_context_free(background);
    neverc_h2_client_stream_free(stream);
    neverc_h2_client_free(client);
    int status = 0;
    (void)h2_reap_child(child, &status);
    ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    ASSERT_TRUE(saw_error);
}

TEST(h2_client_rejects_server_enable_push) {
    const char *error = NULL;
    neverc_tcp_listener_t *listener =
        neverc_tcp_listen("127.0.0.1:0", &error);
    ASSERT_TRUE(listener != NULL);
    neverc_tcp_addr_t address;
    ASSERT_EQ(neverc_tcp_listener_addr(listener, &address), 0);
    int notified[2];
    ASSERT_EQ(pipe(notified), 0);

    pid_t child = fork();
    ASSERT_TRUE(child >= 0);
    if (child == 0) {
        close(notified[0]);
        h2_run_adversarial_response_child(listener, 2, notified[1]);
    }
    close(notified[1]);
    neverc_tcp_listener_close(listener);

    char dial_address[64];
    snprintf(dial_address, sizeof(dial_address), "127.0.0.1:%d",
             address.port);
    neverc_h2_client_config_t config = neverc_h2_client_config_default();
    config.timeout_ms = 3000;
    neverc_h2_client_t *client = neverc_h2_client_dial(
        dial_address, "localhost", 0, &config, &error);
    struct pollfd ready = {
        .fd = notified[0],
        .events = POLLIN,
        .revents = 0,
    };
    int processed = client && poll(&ready, 1U, 3000) == 1;
    char signal = 0;
    if (processed)
        processed = read(notified[0], &signal, 1U) == 1;
    close(notified[0]);
    neverc_h2_client_stream_t *stream = processed
        ? neverc_h2_client_stream_open(client, NULL, "GET", "/",
                                       NULL, 0U, 1, &error)
        : NULL;
    int rejected = processed && stream == NULL;
    if (!processed)
        kill(child, SIGTERM);

    neverc_h2_client_stream_free(stream);
    neverc_h2_client_free(client);
    int status = 0;
    (void)h2_reap_child(child, &status);
    ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    ASSERT_TRUE(rejected);
}

TEST(h2_client_accepts_large_header_table_size) {
    const char *error = NULL;
    neverc_tcp_listener_t *listener =
        neverc_tcp_listen("127.0.0.1:0", &error);
    ASSERT_TRUE(listener != NULL);
    neverc_tcp_addr_t address;
    ASSERT_EQ(neverc_tcp_listener_addr(listener, &address), 0);
    int notified[2];
    ASSERT_EQ(pipe(notified), 0);

    pid_t child = fork();
    ASSERT_TRUE(child >= 0);
    if (child == 0) {
        close(notified[0]);
        h2_run_adversarial_response_child(listener, 3, notified[1]);
    }
    close(notified[1]);
    neverc_tcp_listener_close(listener);

    char dial_address[64];
    snprintf(dial_address, sizeof(dial_address), "127.0.0.1:%d",
             address.port);
    neverc_h2_client_config_t config = neverc_h2_client_config_default();
    config.timeout_ms = 3000;
    neverc_h2_client_t *client = neverc_h2_client_dial(
        dial_address, "localhost", 0, &config, &error);
    struct pollfd ready = {
        .fd = notified[0],
        .events = POLLIN,
        .revents = 0,
    };
    int processed = client && poll(&ready, 1U, 3000) == 1;
    char signal = 0;
    if (processed)
        processed = read(notified[0], &signal, 1U) == 1;
    close(notified[0]);
    neverc_h2_client_stream_t *stream = processed
        ? neverc_h2_client_stream_open(client, NULL, "GET", "/",
                                       NULL, 0U, 1, &error)
        : NULL;
    int accepted = processed && stream != NULL;
    if (!processed)
        kill(child, SIGTERM);

    neverc_h2_client_stream_free(stream);
    neverc_h2_client_free(client);
    int status = 0;
    (void)h2_reap_child(child, &status);
    ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    ASSERT_TRUE(accepted);
}

static int h2_client_rejection_observed(
    neverc_h2_client_t *client, neverc_h2_client_stream_t *stream) {
    if (!client || !stream)
        return 1;

    neverc_context_t *background = neverc_context_background();
    neverc_context_t *context = background
        ? neverc_context_with_timeout(background, 2000, NULL) : NULL;
    neverc_h2_client_event_t *event = NULL;
    int received = context
        ? neverc_h2_client_stream_receive(stream, context, &event) : -1;
    int rejected = received == 0 ||
        (received == 1 && event &&
         event->type == NEVERC_H2_CLIENT_EVENT_ERROR);
    neverc_h2_client_event_free(event);
    neverc_context_free(context);
    neverc_context_free(background);
    return rejected;
}

TEST(h2_client_rejects_idle_stream_data) {
    const char *error = NULL;
    neverc_tcp_listener_t *listener =
        neverc_tcp_listen("127.0.0.1:0", &error);
    ASSERT_TRUE(listener != NULL);
    neverc_tcp_addr_t address;
    ASSERT_EQ(neverc_tcp_listener_addr(listener, &address), 0);
    int notified[2];
    ASSERT_EQ(pipe(notified), 0);

    pid_t child = fork();
    ASSERT_TRUE(child >= 0);
    if (child == 0) {
        close(notified[0]);
        h2_run_adversarial_response_child(listener, 4, notified[1]);
    }
    close(notified[1]);
    neverc_tcp_listener_close(listener);

    char dial_address[64];
    snprintf(dial_address, sizeof(dial_address), "127.0.0.1:%d",
             address.port);
    neverc_h2_client_config_t config = neverc_h2_client_config_default();
    config.timeout_ms = 3000;
    neverc_h2_client_t *client = neverc_h2_client_dial(
        dial_address, "localhost", 0, &config, &error);
    struct pollfd ready = {
        .fd = notified[0],
        .events = POLLIN,
        .revents = 0,
    };
    int processed = poll(&ready, 1U, 3000) == 1;
    char signal = 0;
    if (processed)
        processed = read(notified[0], &signal, 1U) == 1;
    close(notified[0]);
    neverc_h2_client_stream_t *stream = NULL;
    if (client && processed)
        stream = neverc_h2_client_stream_open(client, NULL, "GET", "/",
                                              NULL, 0U, 1, &error);
    int rejected = h2_client_rejection_observed(client, stream);
    if (!processed)
        kill(child, SIGTERM);

    neverc_h2_client_stream_free(stream);
    neverc_h2_client_free(client);
    int status = 0;
    (void)h2_reap_child(child, &status);
    ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    ASSERT_TRUE(rejected);
}

TEST(h2_client_rejects_priority_self_dependency) {
    const char *error = NULL;
    neverc_tcp_listener_t *listener =
        neverc_tcp_listen("127.0.0.1:0", &error);
    ASSERT_TRUE(listener != NULL);
    neverc_tcp_addr_t address;
    ASSERT_EQ(neverc_tcp_listener_addr(listener, &address), 0);
    int notified[2];
    ASSERT_EQ(pipe(notified), 0);

    pid_t child = fork();
    ASSERT_TRUE(child >= 0);
    if (child == 0) {
        close(notified[0]);
        h2_run_adversarial_response_child(listener, 5, notified[1]);
    }
    close(notified[1]);
    neverc_tcp_listener_close(listener);

    char dial_address[64];
    snprintf(dial_address, sizeof(dial_address), "127.0.0.1:%d",
             address.port);
    neverc_h2_client_config_t config = neverc_h2_client_config_default();
    config.timeout_ms = 3000;
    neverc_h2_client_t *client = neverc_h2_client_dial(
        dial_address, "localhost", 0, &config, &error);
    struct pollfd ready = {
        .fd = notified[0],
        .events = POLLIN,
        .revents = 0,
    };
    int processed = poll(&ready, 1U, 3000) == 1;
    char signal = 0;
    if (processed)
        processed = read(notified[0], &signal, 1U) == 1;
    close(notified[0]);
    neverc_h2_client_stream_t *stream = NULL;
    if (client && processed)
        stream = neverc_h2_client_stream_open(client, NULL, "GET", "/",
                                              NULL, 0U, 1, &error);
    int rejected = h2_client_rejection_observed(client, stream);
    if (!processed)
        kill(child, SIGTERM);

    neverc_h2_client_stream_free(stream);
    neverc_h2_client_free(client);
    int status = 0;
    (void)h2_reap_child(child, &status);
    ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    ASSERT_TRUE(rejected);
}

TEST(h2_client_rejects_push_promise) {
    const char *error = NULL;
    neverc_tcp_listener_t *listener =
        neverc_tcp_listen("127.0.0.1:0", &error);
    ASSERT_TRUE(listener != NULL);
    neverc_tcp_addr_t address;
    ASSERT_EQ(neverc_tcp_listener_addr(listener, &address), 0);
    int notified[2];
    ASSERT_EQ(pipe(notified), 0);

    pid_t child = fork();
    ASSERT_TRUE(child >= 0);
    if (child == 0) {
        close(notified[0]);
        h2_run_adversarial_response_child(listener, 6, notified[1]);
    }
    close(notified[1]);
    neverc_tcp_listener_close(listener);

    char dial_address[64];
    snprintf(dial_address, sizeof(dial_address), "127.0.0.1:%d",
             address.port);
    neverc_h2_client_config_t config = neverc_h2_client_config_default();
    config.timeout_ms = 3000;
    neverc_h2_client_t *client = neverc_h2_client_dial(
        dial_address, "localhost", 0, &config, &error);
    struct pollfd ready = {
        .fd = notified[0],
        .events = POLLIN,
        .revents = 0,
    };
    int processed = poll(&ready, 1U, 3000) == 1;
    char signal = 0;
    if (processed)
        processed = read(notified[0], &signal, 1U) == 1;
    close(notified[0]);
    neverc_h2_client_stream_t *stream = NULL;
    if (client && processed)
        stream = neverc_h2_client_stream_open(client, NULL, "GET", "/",
                                              NULL, 0U, 1, &error);
    int rejected = h2_client_rejection_observed(client, stream);
    if (!processed)
        kill(child, SIGTERM);

    neverc_h2_client_stream_free(stream);
    neverc_h2_client_free(client);
    int status = 0;
    (void)h2_reap_child(child, &status);
    ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    ASSERT_TRUE(rejected);
}

TEST(h2_client_rejects_idle_rst_stream) {
    const char *error = NULL;
    neverc_tcp_listener_t *listener =
        neverc_tcp_listen("127.0.0.1:0", &error);
    ASSERT_TRUE(listener != NULL);
    neverc_tcp_addr_t address;
    ASSERT_EQ(neverc_tcp_listener_addr(listener, &address), 0);
    int notified[2];
    ASSERT_EQ(pipe(notified), 0);

    pid_t child = fork();
    ASSERT_TRUE(child >= 0);
    if (child == 0) {
        close(notified[0]);
        h2_run_adversarial_response_child(listener, 7, notified[1]);
    }
    close(notified[1]);
    neverc_tcp_listener_close(listener);

    char dial_address[64];
    snprintf(dial_address, sizeof(dial_address), "127.0.0.1:%d",
             address.port);
    neverc_h2_client_config_t config = neverc_h2_client_config_default();
    config.timeout_ms = 3000;
    neverc_h2_client_t *client = neverc_h2_client_dial(
        dial_address, "localhost", 0, &config, &error);
    struct pollfd ready = {
        .fd = notified[0],
        .events = POLLIN,
        .revents = 0,
    };
    int processed = poll(&ready, 1U, 3000) == 1;
    char signal = 0;
    if (processed)
        processed = read(notified[0], &signal, 1U) == 1;
    close(notified[0]);
    neverc_h2_client_stream_t *stream = NULL;
    if (client && processed)
        stream = neverc_h2_client_stream_open(client, NULL, "GET", "/",
                                              NULL, 0U, 1, &error);
    int rejected = h2_client_rejection_observed(client, stream);
    if (!processed)
        kill(child, SIGTERM);

    neverc_h2_client_stream_free(stream);
    neverc_h2_client_free(client);
    int status = 0;
    (void)h2_reap_child(child, &status);
    ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    ASSERT_TRUE(rejected);
}

TEST(h2_client_rejects_unsolicited_settings_ack) {
    const char *error = NULL;
    neverc_tcp_listener_t *listener =
        neverc_tcp_listen("127.0.0.1:0", &error);
    ASSERT_TRUE(listener != NULL);
    neverc_tcp_addr_t address;
    ASSERT_EQ(neverc_tcp_listener_addr(listener, &address), 0);
    int notified[2];
    ASSERT_EQ(pipe(notified), 0);

    pid_t child = fork();
    ASSERT_TRUE(child >= 0);
    if (child == 0) {
        close(notified[0]);
        h2_run_adversarial_response_child(listener, 8, notified[1]);
    }
    close(notified[1]);
    neverc_tcp_listener_close(listener);

    char dial_address[64];
    snprintf(dial_address, sizeof(dial_address), "127.0.0.1:%d",
             address.port);
    neverc_h2_client_config_t config = neverc_h2_client_config_default();
    config.timeout_ms = 3000;
    neverc_h2_client_t *client = neverc_h2_client_dial(
        dial_address, "localhost", 0, &config, &error);
    struct pollfd ready = {
        .fd = notified[0],
        .events = POLLIN,
        .revents = 0,
    };
    int processed = poll(&ready, 1U, 3000) == 1;
    char signal = 0;
    if (processed)
        processed = read(notified[0], &signal, 1U) == 1;
    close(notified[0]);
    neverc_h2_client_stream_t *stream = NULL;
    if (client && processed)
        stream = neverc_h2_client_stream_open(client, NULL, "GET", "/",
                                              NULL, 0U, 1, &error);
    int rejected = h2_client_rejection_observed(client, stream);
    if (!processed)
        kill(child, SIGTERM);

    neverc_h2_client_stream_free(stream);
    neverc_h2_client_free(client);
    int status = 0;
    (void)h2_reap_child(child, &status);
    ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    ASSERT_TRUE(rejected);
}

TEST(h2_client_rejects_headers_priority_self_dependency) {
    const char *error = NULL;
    neverc_tcp_listener_t *listener =
        neverc_tcp_listen("127.0.0.1:0", &error);
    ASSERT_TRUE(listener != NULL);
    neverc_tcp_addr_t address;
    ASSERT_EQ(neverc_tcp_listener_addr(listener, &address), 0);

    pid_t child = fork();
    ASSERT_TRUE(child >= 0);
    if (child == 0)
        h2_run_adversarial_response_child(listener, 9, -1);
    neverc_tcp_listener_close(listener);

    char dial_address[64];
    snprintf(dial_address, sizeof(dial_address), "127.0.0.1:%d",
             address.port);
    neverc_h2_client_config_t config = neverc_h2_client_config_default();
    config.timeout_ms = 3000;
    neverc_h2_client_t *client = neverc_h2_client_dial(
        dial_address, "localhost", 0, &config, &error);
    neverc_h2_client_stream_t *stream = client
        ? neverc_h2_client_stream_open(client, NULL, "GET", "/",
                                       NULL, 0U, 1, &error)
        : NULL;
    neverc_context_t *background = neverc_context_background();
    neverc_context_t *context = background
        ? neverc_context_with_timeout(background, 2000, NULL) : NULL;
    neverc_h2_client_event_t *event = NULL;
    int received = stream && context
        ? neverc_h2_client_stream_receive(stream, context, &event) : -1;
    int rejected = !client || stream == NULL || received != 1 ||
        (event && event->type == NEVERC_H2_CLIENT_EVENT_ERROR);

    if (!client)
        kill(child, SIGTERM);

    neverc_h2_client_event_free(event);
    neverc_context_free(context);
    neverc_context_free(background);
    neverc_h2_client_stream_free(stream);
    neverc_h2_client_free(client);
    int status = 0;
    (void)h2_reap_child(child, &status);
    ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    ASSERT_TRUE(rejected);
}
#endif /* !_WIN32 */

int main(void) {
    printf("HTTP/2 test suite:\n");

    run_test_frame_header_roundtrip();
    run_test_frame_header_max_values();
    run_test_frame_header_rejects_truncated_and_null();
    run_test_settings_defaults();
    run_test_hpack_decode_indexed();
    run_test_hpack_decode_multi_indexed();
    run_test_hpack_decode_literal_new_name();
    run_test_hpack_decode_literal_indexed_name();
    run_test_hpack_encode_basic();
    run_test_hpack_roundtrip_custom();
    run_test_huffman_roundtrip();
    run_test_client_preface();
    run_test_hpack_dynamic_table_eviction();
    run_test_hpack_oversized_entries_do_not_expand_table();
    run_test_hpack_dynamic_table_honors_supported_capacity();
    run_test_hpack_zero_capacity_output_is_not_written();
    run_test_hpack_sensitive_headers_are_never_indexed();
    run_test_hpack_rejects_overflowing_integer();
    run_test_hpack_encoder_emits_dynamic_table_size_update();
    run_test_hpack_encoder_emits_min_then_current_after_shrink_grow();
    run_test_hpack_rejects_table_size_update_above_max();
    run_test_hpack_rejects_table_size_update_after_fields();
    run_test_hpack_unchanged_table_size_does_not_emit_update();
    run_test_hpack_rejects_crlf_in_field_octets();
    run_test_hpack_encode_falls_back_when_huffman_does_not_fit();
    run_test_frame_types_and_flags();
    run_test_h2_client_rejects_zero_timeout();
#ifndef _WIN32
    run_test_h2c_serve_conn_roundtrip();
    run_test_h2c_continuation_headers();
    run_test_h2c_rejects_missing_authority();
    run_test_h2c_rejects_empty_authority();
    run_test_h2c_rejects_spaced_authority();
    run_test_h2c_rejects_invalid_authority();
    run_test_h2c_rejects_invalid_path();
    run_test_h2c_rejects_empty_host();
    run_test_h2c_rejects_path_fragment();
    run_test_h2c_rejects_empty_authority_with_host();
    run_test_h2c_rejects_empty_host_with_authority();
    run_test_h2c_rejects_host_authority_mismatch();
    run_test_h2c_rejects_streaming_content_length_overrun();
    run_test_h2c_rejects_buffered_content_length_overrun();
    run_test_h2c_rejects_content_length_underrun();
    run_test_h2c_rejects_end_stream_headers_with_content_length();
    run_test_h2c_rejects_status_pseudo_on_request();
    run_test_h2c_rejects_missing_scheme();
    run_test_h2c_rejects_duplicate_method();
    run_test_h2c_rejects_uppercase_header_name();
    run_test_h2c_response_content_length_mismatch_is_reset();
    run_test_h2c_response_content_length_underrun_is_reset();
    run_test_h2c_rejects_header_name_crlf();
    run_test_h2c_rejects_method_with_space();
    run_test_h2c_rejects_reused_refused_stream_id();
    run_test_h2c_rejects_overpadded_data_on_closed_stream();
    run_test_h2c_window_update_overflow_is_connection_error();
    run_test_h2c_empty_data_does_not_send_zero_window_update();
    run_test_h2c_stream_window_update_overflow_is_connection_error();
    run_test_h2c_priority_self_dependency_closes_idle_stream();
    run_test_h2c_priority_self_dependency_aborts_open_stream();
    run_test_h2c_continuation_flood_is_connection_error();
    run_test_h2c_continuation_without_headers_is_connection_error();
    run_test_h2c_rejects_bad_connection_preface();
    run_test_h2c_rejects_push_promise();
    run_test_h2c_rejects_settings_ack_with_payload();
    run_test_h2c_rejects_unsolicited_settings_ack();
    run_test_h2c_window_update_zero_is_connection_error();
    run_test_h2c_stream_window_update_zero_is_stream_error();
    run_test_h2c_rejects_even_stream_id();
    run_test_h2c_rejects_idle_even_data_after_odd_stream();
    run_test_h2c_headers_without_end_headers_then_data_is_connection_error();
    run_test_h2c_continuation_on_refused_stream_keeps_hpack();
    run_test_h2c_headers_priority_self_dependency_is_stream_error();
    run_test_h2c_headers_priority_self_dependency_keeps_hpack();
    run_test_h2c_connection_window_ignores_stream_initial_size();
    run_test_h2_client_invalid_trailer_emits_terminal_error();
    run_test_h2_client_queue_overflow_emits_terminal_error();
    run_test_h2_client_rejects_server_enable_push();
    run_test_h2_client_accepts_large_header_table_size();
    run_test_h2_client_rejects_idle_stream_data();
    run_test_h2_client_rejects_priority_self_dependency();
    run_test_h2_client_rejects_push_promise();
    run_test_h2_client_rejects_idle_rst_stream();
    run_test_h2_client_rejects_unsolicited_settings_ack();
    run_test_h2_client_rejects_headers_priority_self_dependency();
#endif
    /* After fork tests: creating the handler pool first can leave the
     * process multi-threaded, and fork() then deadlocks in the child. */
    run_test_h2_server_lifecycle();

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
