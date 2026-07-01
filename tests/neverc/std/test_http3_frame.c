/*
 * HTTP/3 Frame Layer + QPACK Tests
 * Tests frame encoding/decoding, SETTINGS, and QPACK header compression.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Pull in source directly for unit testing */
#include "../../../std/src/net/quic/quic_varint.c"
#include "../../../std/src/net/http3/http3_frame.c"

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

#define ASSERT_EQ(got, expected) do { \
    tests_run++; \
    if ((got) == (expected)) { tests_passed++; } \
    else { tests_failed++; printf("  FAIL %s:%d: %s == %lld, expected %lld\n", \
           __func__, __LINE__, #got, (long long)(got), (long long)(expected)); } \
} while(0)

#define ASSERT_TRUE(cond) do { \
    tests_run++; \
    if (cond) { tests_passed++; } \
    else { tests_failed++; printf("  FAIL %s:%d: %s\n", __func__, __LINE__, #cond); } \
} while(0)

#define ASSERT_NOT_NULL(ptr) do { \
    tests_run++; \
    if ((ptr) != NULL) { tests_passed++; } \
    else { tests_failed++; printf("  FAIL %s:%d: %s is NULL\n", __func__, __LINE__, #ptr); } \
} while(0)

#define ASSERT_STR_EQ(got, expected) do { \
    tests_run++; \
    if ((got) && (expected) && strcmp((got), (expected)) == 0) { tests_passed++; } \
    else { tests_failed++; printf("  FAIL %s:%d: \"%s\" != \"%s\"\n", \
           __func__, __LINE__, (got) ? (got) : "(null)", (expected) ? (expected) : "(null)"); } \
} while(0)

/* ======================================================================
 * Frame Header Parsing
 * ====================================================================== */

static void test_frame_header_data(void) {
    /* DATA frame: type=0x00, length=5 */
    uint8_t buf[] = { 0x00, 0x05, 'H', 'e', 'l', 'l', 'o' };
    h3_frame_header_t hdr;
    int rc = neverc_h3_parse_frame_header(buf, sizeof(buf), &hdr);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(hdr.type, H3_FRAME_DATA);
    ASSERT_EQ(hdr.length, 5);
    ASSERT_EQ(hdr.header_size, 2);
}

static void test_frame_header_headers(void) {
    uint8_t buf[] = { 0x01, 0x0A };
    h3_frame_header_t hdr;
    int rc = neverc_h3_parse_frame_header(buf, sizeof(buf), &hdr);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(hdr.type, H3_FRAME_HEADERS);
    ASSERT_EQ(hdr.length, 10);
}

static void test_frame_header_settings(void) {
    uint8_t buf[] = { 0x04, 0x08 };
    h3_frame_header_t hdr;
    int rc = neverc_h3_parse_frame_header(buf, sizeof(buf), &hdr);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(hdr.type, H3_FRAME_SETTINGS);
    ASSERT_EQ(hdr.length, 8);
}

static void test_frame_header_goaway(void) {
    uint8_t buf[] = { 0x07, 0x01, 0x00 };
    h3_frame_header_t hdr;
    int rc = neverc_h3_parse_frame_header(buf, sizeof(buf), &hdr);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(hdr.type, H3_FRAME_GOAWAY);
    ASSERT_EQ(hdr.length, 1);
}

static void test_frame_header_empty_buf(void) {
    h3_frame_header_t hdr;
    int rc = neverc_h3_parse_frame_header(NULL, 0, &hdr);
    ASSERT_EQ(rc, -1);
}

/* ======================================================================
 * SETTINGS Frame
 * ====================================================================== */

static void test_settings_default(void) {
    h3_settings_t s;
    neverc_h3_settings_default(&s);
    ASSERT_EQ(s.qpack_max_table_capacity, 4096);
    ASSERT_EQ(s.max_field_section_size, 16 * 1024 * 1024);
    ASSERT_EQ(s.qpack_blocked_streams, 100);
}

static void test_settings_encode_decode_roundtrip(void) {
    h3_settings_t orig, decoded;
    neverc_h3_settings_default(&orig);

    uint8_t buf[256];
    size_t written;
    int rc = neverc_h3_settings_encode(&orig, buf, sizeof(buf), &written);
    ASSERT_EQ(rc, 0);
    ASSERT_TRUE(written > 0);

    /* Parse frame header to get payload offset */
    h3_frame_header_t hdr;
    rc = neverc_h3_parse_frame_header(buf, written, &hdr);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(hdr.type, H3_FRAME_SETTINGS);

    /* Decode payload */
    rc = neverc_h3_settings_decode(buf + hdr.header_size, (size_t)hdr.length, &decoded);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(decoded.qpack_max_table_capacity, orig.qpack_max_table_capacity);
    ASSERT_EQ(decoded.max_field_section_size, orig.max_field_section_size);
    ASSERT_EQ(decoded.qpack_blocked_streams, orig.qpack_blocked_streams);
}

static void test_settings_zero_values(void) {
    h3_settings_t s = { 0, UINT64_MAX, 0 };
    uint8_t buf[256];
    size_t written;
    int rc = neverc_h3_settings_encode(&s, buf, sizeof(buf), &written);
    ASSERT_EQ(rc, 0);

    h3_frame_header_t hdr;
    neverc_h3_parse_frame_header(buf, written, &hdr);
    /* Empty settings payload (all defaults or zero/unlimited) */
    ASSERT_EQ(hdr.length, 0);
}

/* ======================================================================
 * DATA Frame
 * ====================================================================== */

static void test_data_frame_write(void) {
    uint8_t buf[64];
    size_t written;
    int rc = neverc_h3_write_data_frame(buf, sizeof(buf),
                                         (const uint8_t *)"Hello", 5, &written);
    ASSERT_EQ(rc, 0);

    h3_frame_header_t hdr;
    neverc_h3_parse_frame_header(buf, written, &hdr);
    ASSERT_EQ(hdr.type, H3_FRAME_DATA);
    ASSERT_EQ(hdr.length, 5);
    ASSERT_TRUE(memcmp(buf + hdr.header_size, "Hello", 5) == 0);
}

static void test_data_frame_empty(void) {
    uint8_t buf[64];
    size_t written;
    int rc = neverc_h3_write_data_frame(buf, sizeof(buf),
                                         (const uint8_t *)"", 0, &written);
    ASSERT_EQ(rc, 0);

    h3_frame_header_t hdr;
    neverc_h3_parse_frame_header(buf, written, &hdr);
    ASSERT_EQ(hdr.type, H3_FRAME_DATA);
    ASSERT_EQ(hdr.length, 0);
}

/* ======================================================================
 * HEADERS Frame
 * ====================================================================== */

static void test_headers_frame_write(void) {
    uint8_t encoded[] = { 0x00, 0x00, 0xC0 | 25 }; /* fake QPACK: indexed :status 200 */
    uint8_t buf[64];
    size_t written;
    int rc = neverc_h3_write_headers_frame(buf, sizeof(buf), encoded, 3, &written);
    ASSERT_EQ(rc, 0);

    h3_frame_header_t hdr;
    neverc_h3_parse_frame_header(buf, written, &hdr);
    ASSERT_EQ(hdr.type, H3_FRAME_HEADERS);
    ASSERT_EQ(hdr.length, 3);
}

/* ======================================================================
 * GOAWAY Frame
 * ====================================================================== */

static void test_goaway_frame_write(void) {
    uint8_t buf[64];
    size_t written;
    int rc = neverc_h3_write_goaway_frame(buf, sizeof(buf), 4, &written);
    ASSERT_EQ(rc, 0);

    h3_frame_header_t hdr;
    neverc_h3_parse_frame_header(buf, written, &hdr);
    ASSERT_EQ(hdr.type, H3_FRAME_GOAWAY);
    /* Stream ID 4 fits in 1 byte varint */
    ASSERT_TRUE(hdr.length >= 1);
}

static void test_goaway_frame_large_id(void) {
    uint8_t buf[64];
    size_t written;
    int rc = neverc_h3_write_goaway_frame(buf, sizeof(buf), 0x3FFF, &written);
    ASSERT_EQ(rc, 0);

    h3_frame_header_t hdr;
    neverc_h3_parse_frame_header(buf, written, &hdr);
    ASSERT_EQ(hdr.type, H3_FRAME_GOAWAY);
}

/* ======================================================================
 * QPACK Static Table
 * ====================================================================== */

static void test_qpack_static_table_size(void) {
    ASSERT_EQ(QPACK_STATIC_TABLE_SIZE, 99);
}

static void test_qpack_static_table_entries(void) {
    ASSERT_STR_EQ(QPACK_STATIC_TABLE[0].name, ":authority");
    ASSERT_STR_EQ(QPACK_STATIC_TABLE[0].value, "");
    ASSERT_STR_EQ(QPACK_STATIC_TABLE[17].name, ":method");
    ASSERT_STR_EQ(QPACK_STATIC_TABLE[17].value, "GET");
    ASSERT_STR_EQ(QPACK_STATIC_TABLE[25].name, ":status");
    ASSERT_STR_EQ(QPACK_STATIC_TABLE[25].value, "200");
    ASSERT_STR_EQ(QPACK_STATIC_TABLE[98].name, "x-frame-options");
    ASSERT_STR_EQ(QPACK_STATIC_TABLE[98].value, "sameorigin");
}

static void test_qpack_find_static_exact(void) {
    int idx = qpack_find_static(":method", "GET");
    ASSERT_EQ(idx, 17);

    idx = qpack_find_static(":status", "200");
    ASSERT_EQ(idx, 25);

    idx = qpack_find_static("nonexistent", "val");
    ASSERT_EQ(idx, -1);
}

static void test_qpack_find_static_name(void) {
    int idx = qpack_find_static_name(":method");
    ASSERT_EQ(idx, 15); /* first occurrence: CONNECT */

    idx = qpack_find_static_name(":status");
    ASSERT_EQ(idx, 24); /* first occurrence: 103 */

    idx = qpack_find_static_name("x-custom-header");
    ASSERT_EQ(idx, -1);
}

/* ======================================================================
 * QPACK Encode/Decode Roundtrip
 * ====================================================================== */

static void test_qpack_encode_indexed(void) {
    neverc_qpack_encoder_t *enc = neverc_qpack_encoder_create(4096);
    ASSERT_NOT_NULL(enc);

    neverc_qpack_header_t headers[] = {
        { (char *)":method", (char *)"GET" },
        { (char *)":status", (char *)"200" },
    };

    uint8_t out[256];
    size_t out_len;
    int rc = neverc_qpack_encode(enc, headers, 2, out, sizeof(out), &out_len);
    ASSERT_EQ(rc, 0);
    ASSERT_TRUE(out_len > 2); /* at least prefix + 2 indexed refs */

    neverc_qpack_encoder_destroy(enc);
}

static void test_qpack_roundtrip_indexed(void) {
    neverc_qpack_encoder_t *enc = neverc_qpack_encoder_create(4096);
    neverc_qpack_decoder_t *dec = neverc_qpack_decoder_create(4096);

    neverc_qpack_header_t headers[] = {
        { (char *)":method", (char *)"GET" },
        { (char *)":scheme", (char *)"https" },
        { (char *)":status", (char *)"200" },
    };

    uint8_t encoded[512];
    size_t encoded_len;
    int rc = neverc_qpack_encode(enc, headers, 3, encoded, sizeof(encoded), &encoded_len);
    ASSERT_EQ(rc, 0);

    neverc_qpack_header_t decoded[16];
    int nheaders;
    rc = neverc_qpack_decode(dec, encoded, encoded_len, decoded, 16, &nheaders);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(nheaders, 3);

    ASSERT_STR_EQ(decoded[0].name, ":method");
    ASSERT_STR_EQ(decoded[0].value, "GET");
    ASSERT_STR_EQ(decoded[1].name, ":scheme");
    ASSERT_STR_EQ(decoded[1].value, "https");
    ASSERT_STR_EQ(decoded[2].name, ":status");
    ASSERT_STR_EQ(decoded[2].value, "200");

    for (int i = 0; i < nheaders; i++) {
        free(decoded[i].name);
        free(decoded[i].value);
    }

    neverc_qpack_encoder_destroy(enc);
    neverc_qpack_decoder_destroy(dec);
}

static void test_qpack_roundtrip_name_ref(void) {
    neverc_qpack_encoder_t *enc = neverc_qpack_encoder_create(4096);
    neverc_qpack_decoder_t *dec = neverc_qpack_decoder_create(4096);

    /* :status name exists in table but "201" doesn't */
    neverc_qpack_header_t headers[] = {
        { (char *)":status", (char *)"201" },
        { (char *)"content-type", (char *)"text/xml" },
    };

    uint8_t encoded[512];
    size_t encoded_len;
    int rc = neverc_qpack_encode(enc, headers, 2, encoded, sizeof(encoded), &encoded_len);
    ASSERT_EQ(rc, 0);

    neverc_qpack_header_t decoded[16];
    int nheaders;
    rc = neverc_qpack_decode(dec, encoded, encoded_len, decoded, 16, &nheaders);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(nheaders, 2);

    ASSERT_STR_EQ(decoded[0].name, ":status");
    ASSERT_STR_EQ(decoded[0].value, "201");
    ASSERT_STR_EQ(decoded[1].name, "content-type");
    ASSERT_STR_EQ(decoded[1].value, "text/xml");

    for (int i = 0; i < nheaders; i++) {
        free(decoded[i].name);
        free(decoded[i].value);
    }

    neverc_qpack_encoder_destroy(enc);
    neverc_qpack_decoder_destroy(dec);
}

static void test_qpack_roundtrip_literal(void) {
    neverc_qpack_encoder_t *enc = neverc_qpack_encoder_create(4096);
    neverc_qpack_decoder_t *dec = neverc_qpack_decoder_create(4096);

    /* Custom header not in static table at all */
    neverc_qpack_header_t headers[] = {
        { (char *)"x-custom-header", (char *)"custom-value" },
        { (char *)"x-request-id", (char *)"abc-123-def" },
    };

    uint8_t encoded[512];
    size_t encoded_len;
    int rc = neverc_qpack_encode(enc, headers, 2, encoded, sizeof(encoded), &encoded_len);
    ASSERT_EQ(rc, 0);

    neverc_qpack_header_t decoded[16];
    int nheaders;
    rc = neverc_qpack_decode(dec, encoded, encoded_len, decoded, 16, &nheaders);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(nheaders, 2);

    ASSERT_STR_EQ(decoded[0].name, "x-custom-header");
    ASSERT_STR_EQ(decoded[0].value, "custom-value");
    ASSERT_STR_EQ(decoded[1].name, "x-request-id");
    ASSERT_STR_EQ(decoded[1].value, "abc-123-def");

    for (int i = 0; i < nheaders; i++) {
        free(decoded[i].name);
        free(decoded[i].value);
    }

    neverc_qpack_encoder_destroy(enc);
    neverc_qpack_decoder_destroy(dec);
}

static void test_qpack_roundtrip_mixed(void) {
    neverc_qpack_encoder_t *enc = neverc_qpack_encoder_create(4096);
    neverc_qpack_decoder_t *dec = neverc_qpack_decoder_create(4096);

    neverc_qpack_header_t headers[] = {
        { (char *)":method", (char *)"GET" },          /* indexed */
        { (char *)":path", (char *)"/api/v1/users" },  /* name ref + literal value */
        { (char *)"x-trace-id", (char *)"t-42" },      /* fully literal */
        { (char *)":scheme", (char *)"https" },        /* indexed */
    };

    uint8_t encoded[1024];
    size_t encoded_len;
    int rc = neverc_qpack_encode(enc, headers, 4, encoded, sizeof(encoded), &encoded_len);
    ASSERT_EQ(rc, 0);

    neverc_qpack_header_t decoded[16];
    int nheaders;
    rc = neverc_qpack_decode(dec, encoded, encoded_len, decoded, 16, &nheaders);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(nheaders, 4);

    ASSERT_STR_EQ(decoded[0].name, ":method");
    ASSERT_STR_EQ(decoded[0].value, "GET");
    ASSERT_STR_EQ(decoded[1].name, ":path");
    ASSERT_STR_EQ(decoded[1].value, "/api/v1/users");
    ASSERT_STR_EQ(decoded[2].name, "x-trace-id");
    ASSERT_STR_EQ(decoded[2].value, "t-42");
    ASSERT_STR_EQ(decoded[3].name, ":scheme");
    ASSERT_STR_EQ(decoded[3].value, "https");

    for (int i = 0; i < nheaders; i++) {
        free(decoded[i].name);
        free(decoded[i].value);
    }

    neverc_qpack_encoder_destroy(enc);
    neverc_qpack_decoder_destroy(dec);
}

static void test_qpack_empty_headers(void) {
    neverc_qpack_encoder_t *enc = neverc_qpack_encoder_create(4096);
    neverc_qpack_decoder_t *dec = neverc_qpack_decoder_create(4096);

    uint8_t encoded[64];
    size_t encoded_len;
    int rc = neverc_qpack_encode(enc, NULL, 0, encoded, sizeof(encoded), &encoded_len);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(encoded_len, 2); /* just the prefix */

    neverc_qpack_header_t decoded[16];
    int nheaders;
    rc = neverc_qpack_decode(dec, encoded, encoded_len, decoded, 16, &nheaders);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(nheaders, 0);

    neverc_qpack_encoder_destroy(enc);
    neverc_qpack_decoder_destroy(dec);
}

/* ======================================================================
 * QPACK Encoder/Decoder lifecycle
 * ====================================================================== */

static void test_qpack_encoder_create_destroy(void) {
    neverc_qpack_encoder_t *enc = neverc_qpack_encoder_create(8192);
    ASSERT_NOT_NULL(enc);
    neverc_qpack_encoder_destroy(enc);

    neverc_qpack_decoder_t *dec = neverc_qpack_decoder_create(8192);
    ASSERT_NOT_NULL(dec);
    neverc_qpack_decoder_destroy(dec);
}

/* ======================================================================
 * main
 * ====================================================================== */

int main(void) {
    printf("HTTP/3 Frame + QPACK test suite:\n\n");

    test_frame_header_data();
    test_frame_header_headers();
    test_frame_header_settings();
    test_frame_header_goaway();
    test_frame_header_empty_buf();
    test_settings_default();
    test_settings_encode_decode_roundtrip();
    test_settings_zero_values();
    test_data_frame_write();
    test_data_frame_empty();
    test_headers_frame_write();
    test_goaway_frame_write();
    test_goaway_frame_large_id();
    test_qpack_static_table_size();
    test_qpack_static_table_entries();
    test_qpack_find_static_exact();
    test_qpack_find_static_name();
    test_qpack_encode_indexed();
    test_qpack_roundtrip_indexed();
    test_qpack_roundtrip_name_ref();
    test_qpack_roundtrip_literal();
    test_qpack_roundtrip_mixed();
    test_qpack_empty_headers();
    test_qpack_encoder_create_destroy();

    printf("\n%d passed, %d failed (of %d)\n", tests_passed, tests_failed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
