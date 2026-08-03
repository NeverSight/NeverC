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
#include <unistd.h>
#include <sys/socket.h>
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

#ifndef _WIN32
static int sock_write_all(int fd, const void *buf, size_t len) {
    const char *p = (const char *)buf;
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = write(fd, p + sent, len - sent);
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
} h2_serve_ctx_t;

static void h2_test_handler(neverc_http_request_t *req,
                            neverc_http_response_writer_t *w) {
    neverc_http_set_status(w, 200);
    neverc_http_set_header(w, "Content-Type", "text/plain; charset=utf-8");
    neverc_http_writef(w, "Hello from NeverC HTTP/2!\nMethod: %s\nPath: %s\n",
                       req->method ? req->method : "",
                       req->path ? req->path : "");
}

static void h2_run_server_child(h2_serve_ctx_t *ctx) {
    neverc_http_mux_t *mux = neverc_http_new_mux();
    if (!mux) _exit(1);
    neverc_http_mux_handle(mux, "/", h2_test_handler);
    neverc_h2_server_t *server = neverc_h2_server_create(mux);
    if (!server) {
        neverc_http_mux_free(mux);
        _exit(1);
    }
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

    ASSERT_EQ(sock_write_all(client_fd, NC_H2_CLIENT_PREFACE,
                              NC_H2_CLIENT_PREFACE_LEN), 0);

    uint8_t settings_frame[9] = { 0, 0, 0, NC_H2_FRAME_SETTINGS, 0, 0, 0, 0, 0 };
    ASSERT_EQ(sock_write_all(client_fd, settings_frame, sizeof(settings_frame)), 0);
    ASSERT_EQ(h2_client_handshake(client_fd), 0);

    uint8_t req_hdr_frame[9] = {
        0, 0, 3, NC_H2_FRAME_HEADERS,
        (uint8_t)(NC_H2_FLAG_END_HEADERS | NC_H2_FLAG_END_STREAM),
        0, 0, 0, 1
    };
    uint8_t req_hpack[] = { 0x82, 0x84, 0x86 };
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
    ASSERT_EQ(waitpid(child, &status, 0), child);
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
    ASSERT_EQ(sock_write_all(client_fd, NC_H2_CLIENT_PREFACE,
                              NC_H2_CLIENT_PREFACE_LEN), 0);

    uint8_t settings_frame[9] = { 0, 0, 0, NC_H2_FRAME_SETTINGS, 0, 0, 0, 0, 0 };
    ASSERT_EQ(sock_write_all(client_fd, settings_frame, sizeof(settings_frame)), 0);
    ASSERT_EQ(h2_client_handshake(client_fd), 0);

    uint8_t hdr1[9] = { 0, 0, 1, NC_H2_FRAME_HEADERS, NC_H2_FLAG_END_STREAM,
                        0, 0, 0, 1 };
    uint8_t hpack1[] = { 0x82 };
    uint8_t cont[9] = { 0, 0, 2, NC_H2_FRAME_CONTINUATION,
                        (uint8_t)NC_H2_FLAG_END_HEADERS, 0, 0, 0, 1 };
    uint8_t hpack2[] = { 0x84, 0x86 };

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
    ASSERT_EQ(waitpid(child, &status, 0), child);
    ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0);

}
#endif /* !_WIN32 */

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
    run_test_hpack_oversized_entries_do_not_expand_table();
    run_test_hpack_dynamic_table_honors_supported_capacity();
    run_test_hpack_zero_capacity_output_is_not_written();
    run_test_hpack_sensitive_headers_are_never_indexed();
    run_test_hpack_rejects_overflowing_integer();
    run_test_frame_types_and_flags();
#ifndef _WIN32
    run_test_h2c_serve_conn_roundtrip();
    run_test_h2c_continuation_headers();
#endif

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
