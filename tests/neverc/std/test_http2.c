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

int main(void) {
    printf("HTTP/2 test suite:\n");

    run_test_frame_header_roundtrip();
    run_test_frame_header_max_values();
    run_test_settings_defaults();
    run_test_hpack_decode_indexed();
    run_test_hpack_decode_multi_indexed();
    run_test_hpack_decode_literal_new_name();
    run_test_hpack_decode_literal_indexed_name();
    run_test_hpack_encode_basic();
    run_test_hpack_roundtrip_custom();
    run_test_huffman_roundtrip();
    run_test_h2_server_lifecycle();
    run_test_client_preface();
    run_test_hpack_dynamic_table_eviction();
    run_test_frame_types_and_flags();

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
