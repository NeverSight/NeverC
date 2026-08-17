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
    uint8_t truncated[] = { 0x01, 0x0A };
    h3_frame_header_t hdr;
    ASSERT_EQ(neverc_h3_parse_frame_header(truncated, sizeof(truncated),
                                           &hdr), -1);

    uint8_t buf[12];
    buf[0] = 0x01;
    buf[1] = 0x0A;
    memset(buf + 2, 'h', 10);
    int rc = neverc_h3_parse_frame_header(buf, sizeof(buf), &hdr);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(hdr.type, H3_FRAME_HEADERS);
    ASSERT_EQ(hdr.length, 10);
}

static void test_frame_header_settings(void) {
    uint8_t truncated[] = { 0x04, 0x08 };
    h3_frame_header_t hdr;
    ASSERT_EQ(neverc_h3_parse_frame_header(truncated, sizeof(truncated),
                                           &hdr), -1);

    uint8_t buf[10];
    buf[0] = 0x04;
    buf[1] = 0x08;
    memset(buf + 2, 0, 8);
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

static void test_frame_header_truncated_varint(void) {
    /* 2-byte QUIC varint prefix with only the first byte present. */
    uint8_t buf[] = { 0x40 };
    h3_frame_header_t hdr;
    ASSERT_EQ(neverc_h3_parse_frame_header(buf, sizeof(buf), &hdr), -1);

    uint8_t truncated_len[] = { 0x00, 0x40 };
    ASSERT_EQ(neverc_h3_parse_frame_header(truncated_len,
                                           sizeof(truncated_len), &hdr), -1);

    uint8_t truncated_4[] = { 0x80, 0x00 };
    ASSERT_EQ(neverc_h3_parse_frame_header(truncated_4, sizeof(truncated_4),
                                           &hdr), -1);

    uint8_t truncated_8[] = { 0xC0, 0x00, 0x00 };
    ASSERT_EQ(neverc_h3_parse_frame_header(truncated_8, sizeof(truncated_8),
                                           &hdr), -1);
}

/* ======================================================================
 * SETTINGS Frame
 * ====================================================================== */

static void test_settings_default(void) {
    h3_settings_t s;
    neverc_h3_settings_default(&s);
    ASSERT_EQ(s.qpack_max_table_capacity, 0);
    ASSERT_EQ(s.max_field_section_size, 16 * 1024 * 1024);
    ASSERT_EQ(s.qpack_blocked_streams, 0);
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

static void test_settings_reserved_http2_ids_rejected(void) {
    /* RFC 9114 §11.2.2: HTTP/2 setting identifiers 0x02..0x05 MUST be
     * treated as H3_SETTINGS_ERROR. */
    static const uint8_t reserved_ids[] = { 0x00, 0x02, 0x03, 0x04, 0x05 };
    h3_settings_t decoded;
    for (size_t i = 0; i < sizeof(reserved_ids); i++) {
        uint8_t payload[] = { reserved_ids[i], 0x00 };
        ASSERT_EQ(neverc_h3_settings_decode(payload, sizeof(payload),
                                            &decoded), -1);
    }
}

static void test_settings_duplicate_ids_rejected(void) {
    /* RFC 9114 §7.2.4: a setting identifier MUST NOT occur more than once. */
    uint8_t payload[] = { 0x01, 0x00, 0x01, 0x20 };
    h3_settings_t decoded;
    ASSERT_EQ(neverc_h3_settings_decode(payload, sizeof(payload),
                                        &decoded), -1);
}

static void test_settings_grease_ignored(void) {
    /* RFC 9114 §7.2.4.1: identifiers 0x1f * N + 0x21 MUST be ignored. */
    uint8_t payload[] = { 0x21, 0x00 };
    h3_settings_t decoded;
    ASSERT_EQ(neverc_h3_settings_decode(payload, sizeof(payload),
                                        &decoded), 0);
    ASSERT_EQ(decoded.qpack_max_table_capacity, 0);
    ASSERT_EQ(decoded.qpack_blocked_streams, 0);
}

static void test_settings_truncated_pair_rejected(void) {
    h3_settings_t decoded;
    uint8_t id_only[] = { 0x01 };
    ASSERT_EQ(neverc_h3_settings_decode(id_only, sizeof(id_only),
                                        &decoded), -1);

    uint8_t truncated_value[] = { 0x01, 0x40 };
    ASSERT_EQ(neverc_h3_settings_decode(truncated_value,
                                        sizeof(truncated_value),
                                        &decoded), -1);
}

static void test_settings_zero_values(void) {
    h3_settings_t s = { 0, UINT64_MAX, 0 };
    uint8_t buf[256];
    size_t written;
    int rc = neverc_h3_settings_encode(&s, buf, sizeof(buf), &written);
    ASSERT_EQ(rc, 0);

    h3_frame_header_t hdr;
    neverc_h3_parse_frame_header(buf, written, &hdr);
    /* Omitted SETTINGS_MAX_FIELD_SECTION_SIZE still means unlimited. */
    ASSERT_EQ(hdr.length, 0);

    /* RFC 9114 §7.2.4.1: an explicit 0 is a limit of 0, not unlimited. */
    h3_settings_t zero_limit = { 0, 0, 0 };
    rc = neverc_h3_settings_encode(&zero_limit, buf, sizeof(buf), &written);
    ASSERT_EQ(rc, 0);
    neverc_h3_parse_frame_header(buf, written, &hdr);
    ASSERT_TRUE(hdr.length >= 2);

    h3_settings_t decoded;
    ASSERT_EQ(neverc_h3_settings_decode(buf + hdr.header_size,
                                        (size_t)hdr.length, &decoded), 0);
    ASSERT_EQ(decoded.max_field_section_size, 0);

    uint8_t payload[] = { 0x06, 0x00 };
    ASSERT_EQ(neverc_h3_settings_decode(payload, sizeof(payload), &decoded), 0);
    ASSERT_EQ(decoded.max_field_section_size, 0);
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

static void test_data_frame_rejects_undersized_buffer(void) {
    uint8_t buf[8];
    uint8_t data[16];
    size_t written = 0;
    memset(data, 'A', sizeof(data));
    ASSERT_EQ(neverc_h3_write_data_frame(buf, sizeof(buf), data, sizeof(data),
                                         &written), -1);
}

static void test_headers_frame_rejects_undersized_buffer(void) {
    uint8_t buf[4];
    uint8_t headers[8] = { 0x00, 0x00, 0xC0, 25, 0, 0, 0, 0 };
    size_t written = 0;
    ASSERT_EQ(neverc_h3_write_headers_frame(buf, sizeof(buf), headers, 8,
                                            &written), -1);
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
    /* RFC 9114 §7.2.6: identifier 0 means no request streams processed. */
    int rc = neverc_h3_write_goaway_frame(buf, sizeof(buf), 0, &written);
    ASSERT_EQ(rc, 0);
    rc = neverc_h3_write_goaway_frame(buf, sizeof(buf), 4, &written);
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

static void test_qpack_literal_name_ref_never_index(void) {
    /* RFC 9204 §4.5.4: N=1 (never index) is 01N1NNNN = 0x50|0x20|idx.
     * Static cookie is index 5 → 0x75. */
    neverc_qpack_decoder_t *dec = neverc_qpack_decoder_create(4096);
    static const uint8_t block[] = {
        0x00, 0x00, 0x75, 0x09,
        's', 'e', 's', 's', 'i', 'o', 'n', '=', '1'
    };
    neverc_qpack_header_t decoded[4];
    int nheaders = 0;
    ASSERT_EQ(neverc_qpack_decode(dec, block, sizeof(block),
                                  decoded, 4, &nheaders), 0);
    ASSERT_EQ(nheaders, 1);
    ASSERT_STR_EQ(decoded[0].name, "cookie");
    ASSERT_STR_EQ(decoded[0].value, "session=1");
    free(decoded[0].name);
    free(decoded[0].value);
    neverc_qpack_decoder_destroy(dec);
}

static void test_qpack_rejects_s_bit_with_zero_ric(void) {
    neverc_qpack_decoder_t *dec = neverc_qpack_decoder_create(4096);
    static const uint8_t block[] = { 0x00, 0x80, 0xC1 };
    neverc_qpack_header_t decoded[4];
    int nheaders = 0;
    ASSERT_EQ(neverc_qpack_decode(dec, block, sizeof(block),
                                  decoded, 4, &nheaders), -1);
    neverc_qpack_decoder_destroy(dec);
}

static void test_qpack_rejects_nul_and_crlf(void) {
    neverc_qpack_encoder_t *enc = neverc_qpack_encoder_create(4096);
    neverc_qpack_decoder_t *dec = neverc_qpack_decoder_create(4096);
    neverc_qpack_header_t inject[] = {
        { (char *)"x-custom", (char *)"a\r\nX-Injected: 1" },
    };
    uint8_t encoded[128];
    size_t encoded_len = 0;
    ASSERT_EQ(neverc_qpack_encode(enc, inject, 1, encoded, sizeof(encoded),
                                  &encoded_len), -1);

    /* Literal name "x" + value "a\0b" (RFC 9110 §5.5). */
    static const uint8_t nul_block[] = {
        0x00, 0x00, 0x21, 'x', 0x03, 'a', 0x00, 'b'
    };
    neverc_qpack_header_t decoded[4];
    int nheaders = 0;
    ASSERT_EQ(neverc_qpack_decode(dec, nul_block, sizeof(nul_block),
                                  decoded, 4, &nheaders), -1);

    static const uint8_t crlf_block[] = {
        0x00, 0x00, 0x21, 'x', 0x03, 'a', '\r', '\n'
    };
    ASSERT_EQ(neverc_qpack_decode(dec, crlf_block, sizeof(crlf_block),
                                  decoded, 4, &nheaders), -1);

    neverc_qpack_encoder_destroy(enc);
    neverc_qpack_decoder_destroy(dec);
}

static void test_qpack_field_section_size(void) {
    /* RFC 9113 §6.5.2: size = name + value + 32 per field. */
    neverc_qpack_header_t headers[] = {
        { (char *)":method", (char *)"GET" },
        { (char *)":scheme", (char *)"https" },
        { (char *)":authority", (char *)"example.com" },
        { (char *)":path", (char *)"/" },
    };
    uint64_t size = 0;
    ASSERT_EQ(neverc_qpack_field_section_size(headers, 4, &size), 0);
    ASSERT_EQ(size, (uint64_t)(7 + 3 + 32 + 7 + 5 + 32 + 10 + 11 + 32 +
                               5 + 1 + 32));

    neverc_qpack_encoder_t *enc = neverc_qpack_encoder_create(0);
    uint8_t encoded[64];
    size_t encoded_len = 0;
    ASSERT_EQ(neverc_qpack_encode(enc, headers, 4, encoded, sizeof(encoded),
                                  &encoded_len), 0);
    /* Static-table QPACK is far smaller than the uncompressed section.
     * Comparing encoded_len to SETTINGS_MAX_FIELD_SECTION_SIZE would
     * wrongly accept a section the peer refused. */
    ASSERT_TRUE(encoded_len < 50);
    ASSERT_TRUE(size > 50);
    neverc_qpack_encoder_destroy(enc);

    ASSERT_EQ(neverc_qpack_field_section_size(NULL, 1, &size), -1);
    ASSERT_EQ(neverc_qpack_field_section_size(headers, 0, &size), 0);
    ASSERT_EQ(size, 0);

    uint64_t alias_size = 1;
    ASSERT_EQ(neverc_http3_qpack_field_section_size(headers, 4, &alias_size),
              0);
    ASSERT_EQ(alias_size, (uint64_t)(7 + 3 + 32 + 7 + 5 + 32 + 10 + 11 + 32 +
                                     5 + 1 + 32));
    ASSERT_EQ(neverc_http3_qpack_field_section_size(NULL, 1, &alias_size), -1);
}

static void test_qpack_rejects_uppercase_and_empty_name(void) {
    neverc_qpack_encoder_t *enc = neverc_qpack_encoder_create(4096);
    neverc_qpack_decoder_t *dec = neverc_qpack_decoder_create(4096);
    uint8_t encoded[128];
    size_t encoded_len = 0;
    neverc_qpack_header_t upper[] = {
        { (char *)"Content-Type", (char *)"text/plain" },
    };
    ASSERT_EQ(neverc_qpack_encode(enc, upper, 1, encoded, sizeof(encoded),
                                  &encoded_len), -1);

    neverc_qpack_header_t empty_name[] = {
        { (char *)"", (char *)"x" },
    };
    ASSERT_EQ(neverc_qpack_encode(enc, empty_name, 1, encoded, sizeof(encoded),
                                  &encoded_len), -1);

    /* Literal name "X" (uppercase) + value "a" must not be accepted. */
    static const uint8_t upper_block[] = {
        0x00, 0x00, 0x21, 'X', 0x01, 'a'
    };
    neverc_qpack_header_t decoded[4];
    int nheaders = 0;
    ASSERT_EQ(neverc_qpack_decode(dec, upper_block, sizeof(upper_block),
                                  decoded, 4, &nheaders), -1);

    neverc_qpack_encoder_destroy(enc);
    neverc_qpack_decoder_destroy(dec);
}

static void test_authority_rejects_host_list_and_userinfo(void) {
    ASSERT_EQ(neverc_h3_authority_allowed("example.com"), 1);
    ASSERT_EQ(neverc_h3_authority_allowed("localhost:8080"), 1);
    ASSERT_EQ(neverc_h3_authority_allowed("[::1]"), 1);
    ASSERT_EQ(neverc_h3_authority_allowed("[::1]:443"), 1);
    ASSERT_EQ(neverc_h3_authority_allowed("example.com,evil.com"), 0);
    ASSERT_EQ(neverc_h3_authority_allowed("user@example.com"), 0);
    ASSERT_EQ(neverc_h3_authority_allowed("example.com/foo"), 0);
    ASSERT_EQ(neverc_h3_authority_allowed("localhost:99999"), 0);
    ASSERT_EQ(neverc_h3_authority_allowed("[::1"), 0);
    ASSERT_EQ(neverc_h3_authority_allowed("::1"), 0);
    ASSERT_EQ(neverc_h3_authority_allowed("[::1,evil.com]"), 0);
    ASSERT_EQ(neverc_h3_authority_allowed("foo bar"), 0);
    ASSERT_EQ(neverc_h3_authority_allowed(""), 0);
    ASSERT_EQ(neverc_h3_authority_allowed(NULL), 0);
}

static void test_trailer_name_rejects_framing_fields(void) {
    ASSERT_EQ(neverc_h3_trailer_name_allowed("x-ok"), 1);
    ASSERT_EQ(neverc_h3_trailer_name_allowed("content-length"), 0);
    ASSERT_EQ(neverc_h3_trailer_name_allowed("host"), 0);
    ASSERT_EQ(neverc_h3_trailer_name_allowed("transfer-encoding"), 0);
    ASSERT_EQ(neverc_h3_trailer_name_allowed("connection"), 0);
    ASSERT_EQ(neverc_h3_trailer_name_allowed("te"), 0);
    ASSERT_EQ(neverc_h3_trailer_name_allowed(":status"), 0);
    ASSERT_EQ(neverc_h3_trailer_name_allowed(""), 0);
    ASSERT_EQ(neverc_h3_trailer_name_allowed(NULL), 0);
}

static void test_response_body_forbidden_for_204_304(void) {
    ASSERT_EQ(neverc_h3_response_body_allowed(200), 1);
    ASSERT_EQ(neverc_h3_response_body_allowed(204), 0);
    ASSERT_EQ(neverc_h3_response_body_allowed(304), 0);
    ASSERT_EQ(neverc_h3_response_body_allowed(100), 0);
    ASSERT_EQ(neverc_h3_response_body_allowed(101), 0);
}

static void test_apply_response_content_length(void) {
    int present = 1;
    ASSERT_EQ(neverc_h3_apply_response_content_length(200, &present), 0);
    ASSERT_EQ(present, 1);

    present = 1;
    ASSERT_EQ(neverc_h3_apply_response_content_length(204, &present), -1);

    /* 304 may carry Content-Length of the selected representation; it must
     * not be compared to the empty message body. */
    present = 1;
    ASSERT_EQ(neverc_h3_apply_response_content_length(304, &present), 0);
    ASSERT_EQ(present, 0);

    present = 1;
    ASSERT_EQ(neverc_h3_apply_response_content_length(100, &present), 0);
    ASSERT_EQ(present, 0);

    ASSERT_EQ(neverc_h3_apply_response_content_length(200, NULL), -1);
}

static void test_request_path_allows_options_asterisk(void) {
    ASSERT_EQ(neverc_h3_request_path_allowed("GET", "/"), 1);
    ASSERT_EQ(neverc_h3_request_path_allowed("GET", "/index"), 1);
    ASSERT_EQ(neverc_h3_request_path_allowed("OPTIONS", "*"), 1);
    ASSERT_EQ(neverc_h3_request_path_allowed("OPTIONS", "/"), 1);
    ASSERT_EQ(neverc_h3_request_path_allowed("GET", "*"), 0);
    ASSERT_EQ(neverc_h3_request_path_allowed("POST", "*"), 0);
    ASSERT_EQ(neverc_h3_request_path_allowed("OPTIONS", "**"), 0);
    ASSERT_EQ(neverc_h3_request_path_allowed("GET", ""), 0);
    ASSERT_EQ(neverc_h3_request_path_allowed("GET", "/a b"), 0);
    ASSERT_EQ(neverc_h3_request_path_allowed("GET", "/a#b"), 0);
    {
        char delpath[] = {'/', 'a', 0x7f, '\0'};
        ASSERT_EQ(neverc_h3_request_path_allowed("GET", delpath), 0);
    }
}

static void test_method_rejects_non_token(void) {
    /* RFC 9114 §4.3.1 / RFC 9110 §9: :method is a token. A space would
     * fail-open as request-target smuggling on an HTTP/1 hop. */
    ASSERT_EQ(neverc_h3_method_allowed("GET"), 1);
    ASSERT_EQ(neverc_h3_method_allowed("POST"), 1);
    ASSERT_EQ(neverc_h3_method_allowed("PATCH"), 1);
    ASSERT_EQ(neverc_h3_method_allowed("OPTIONS"), 1);
    ASSERT_EQ(neverc_h3_method_allowed("CONNECT"), 0);
    ASSERT_EQ(neverc_h3_method_allowed("GET /admin"), 0);
    ASSERT_EQ(neverc_h3_method_allowed("GET\t/admin"), 0);
    ASSERT_EQ(neverc_h3_method_allowed(""), 0);
    ASSERT_EQ(neverc_h3_method_allowed(NULL), 0);
    ASSERT_EQ(neverc_h3_method_allowed("GET/1"), 0);
}

static void test_field_section_over_limit(void) {
    ASSERT_EQ(neverc_h3_field_section_over_limit(0, 0), 0);
    ASSERT_EQ(neverc_h3_field_section_over_limit(1, 0), 1);
    ASSERT_EQ(neverc_h3_field_section_over_limit(100, 100), 0);
    ASSERT_EQ(neverc_h3_field_section_over_limit(101, 100), 1);
    ASSERT_EQ(neverc_h3_field_section_over_limit(1, UINT64_MAX), 0);
}

static void test_goaway_and_stream_type_helpers(void) {
    ASSERT_EQ(neverc_h3_goaway_id_accept(0, 0, 10), 0);
    ASSERT_EQ(neverc_h3_goaway_id_accept(1, 10, 10), 0);
    ASSERT_EQ(neverc_h3_goaway_id_accept(1, 10, 9), 0);
    ASSERT_EQ(neverc_h3_goaway_id_accept(1, 10, 11), -1);

    uint64_t max_bidi = neverc_h3_graceful_goaway_id();
    ASSERT_EQ(neverc_h3_server_goaway_id_valid(0), 1);
    ASSERT_EQ(neverc_h3_server_goaway_id_valid(4), 1);
    ASSERT_EQ(neverc_h3_server_goaway_id_valid(max_bidi), 1);
    ASSERT_EQ(neverc_h3_server_goaway_id_valid(1), 0); /* server bidi */
    ASSERT_EQ(neverc_h3_server_goaway_id_valid(2), 0); /* client uni */
    ASSERT_EQ(neverc_h3_request_stream_after_goaway(4, 8), 1);
    ASSERT_EQ(neverc_h3_request_stream_after_goaway(8, 4), 0);
    ASSERT_EQ(neverc_h3_request_stream_after_goaway(max_bidi, 8), 0);

    uint8_t buf[32];
    size_t written = 0;
    ASSERT_EQ(neverc_h3_write_goaway_frame(buf, sizeof(buf), max_bidi,
                                           &written), 0);
    h3_frame_header_t hdr;
    ASSERT_EQ(neverc_h3_parse_frame_header(buf, written, &hdr), 0);
    ASSERT_EQ(hdr.type, H3_FRAME_GOAWAY);

    ASSERT_EQ(neverc_h3_uni_stream_type_class(0x00), 0); /* control */
    ASSERT_EQ(neverc_h3_uni_stream_type_class(0x02), 0); /* encoder */
    ASSERT_EQ(neverc_h3_uni_stream_type_class(0x03), 0); /* decoder */
    ASSERT_EQ(neverc_h3_uni_stream_type_class(0x01), -1); /* client push */
    ASSERT_EQ(neverc_h3_uni_stream_type_class(0x21), 1); /* GREASE */
    ASSERT_EQ(neverc_h3_uni_stream_type_class(0x40), 1);
}

static void test_varint_payload_and_max_push_id(void) {
    uint64_t value = 99;
    uint8_t one[] = { 0x05 };
    ASSERT_EQ(neverc_h3_parse_varint_payload(one, sizeof(one), &value), 0);
    ASSERT_EQ(value, 5);
    ASSERT_EQ(neverc_h3_parse_varint_payload(one, 0, &value), -1);
    uint8_t padded[] = { 0x05, 0x00 };
    ASSERT_EQ(neverc_h3_parse_varint_payload(padded, sizeof(padded), &value),
              -1);
    uint8_t truncated[] = { 0x40 };
    ASSERT_EQ(neverc_h3_parse_varint_payload(truncated, sizeof(truncated),
                                             &value), -1);
    ASSERT_EQ(neverc_h3_max_push_id_accept(0, 0, 10), 0);
    ASSERT_EQ(neverc_h3_max_push_id_accept(1, 10, 10), 0);
    ASSERT_EQ(neverc_h3_max_push_id_accept(1, 10, 11), 0);
    ASSERT_EQ(neverc_h3_max_push_id_accept(1, 10, 9), -1);
}

static void test_qpack_decoder_stream_instructions(void) {
    size_t consumed = 0;
    uint8_t cancel[] = { 0x40 }; /* Stream Cancellation, id 0 */
    ASSERT_EQ(neverc_qpack_decoder_stream_instruction(
                  cancel, sizeof(cancel), &consumed),
              1);
    ASSERT_EQ(consumed, 1);

    uint8_t ack[] = { 0x80 }; /* Section Acknowledgement */
    ASSERT_EQ(neverc_qpack_decoder_stream_instruction(
                  ack, sizeof(ack), &consumed),
              -1);

    uint8_t inc0[] = { 0x00 }; /* Insert Count Increment 0 */
    ASSERT_EQ(neverc_qpack_decoder_stream_instruction(
                  inc0, sizeof(inc0), &consumed),
              -1);

    uint8_t inc1[] = { 0x01 };
    ASSERT_EQ(neverc_qpack_decoder_stream_instruction(
                  inc1, sizeof(inc1), &consumed),
              -1);

    uint8_t truncated[] = { 0x7f }; /* increment, 6-bit prefix continues */
    ASSERT_EQ(neverc_qpack_decoder_stream_instruction(
                  truncated, sizeof(truncated), &consumed),
              0);
}

static void test_qpack_rejects_truncated_prefix_integer(void) {
    neverc_qpack_decoder_t *dec = neverc_qpack_decoder_create(4096);
    /* Indexed static, 6-bit prefix saturated, continuation truncated. */
    static const uint8_t block[] = { 0x00, 0x00, 0xFF, 0x80 };
    neverc_qpack_header_t decoded[4];
    int nheaders = 0;
    ASSERT_EQ(neverc_qpack_decode(dec, block, sizeof(block),
                                  decoded, 4, &nheaders), -1);
    ASSERT_EQ(nheaders, 0);
    neverc_qpack_decoder_destroy(dec);
}

static void test_qpack_rejects_dynamic_and_post_base(void) {
    neverc_qpack_decoder_t *dec = neverc_qpack_decoder_create(0);
    neverc_qpack_header_t decoded[4];
    int nheaders = 99;

    /* Indexed Field Line, T=0 (dynamic table) index 0. */
    static const uint8_t dynamic_indexed[] = { 0x00, 0x00, 0x80 };
    ASSERT_EQ(neverc_qpack_decode(dec, dynamic_indexed,
                                  sizeof(dynamic_indexed), decoded, 4,
                                  &nheaders), -1);
    ASSERT_EQ(nheaders, 0);

    /* Indexed Field Line With Post-Base Index. */
    static const uint8_t post_base[] = { 0x00, 0x00, 0x10 };
    ASSERT_EQ(neverc_qpack_decode(dec, post_base, sizeof(post_base),
                                  decoded, 4, &nheaders), -1);
    ASSERT_EQ(nheaders, 0);

    /* Literal with dynamic name reference (T=0). */
    static const uint8_t dyn_name[] = { 0x00, 0x00, 0x40, 0x01, 'x' };
    ASSERT_EQ(neverc_qpack_decode(dec, dyn_name, sizeof(dyn_name),
                                  decoded, 4, &nheaders), -1);
    ASSERT_EQ(nheaders, 0);
    neverc_qpack_decoder_destroy(dec);
}

static void test_qpack_rejects_truncated_header_list(void) {
    /* Two indexed static lines (:method GET, :method POST). Decoding with
     * max_headers=1 must not fail-open by silently dropping the rest. */
    neverc_qpack_decoder_t *dec = neverc_qpack_decoder_create(0);
    static const uint8_t block[] = { 0x00, 0x00, (uint8_t)(0xC0 | 17),
                                     (uint8_t)(0xC0 | 20) };
    neverc_qpack_header_t decoded[4];
    int nheaders = 0;
    ASSERT_EQ(neverc_qpack_decode(dec, block, sizeof(block), decoded, 1,
                                  &nheaders), -1);
    ASSERT_EQ(nheaders, 0);
    neverc_qpack_decoder_destroy(dec);
}

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
    test_frame_header_truncated_varint();
    test_settings_default();
    test_settings_encode_decode_roundtrip();
    test_settings_reserved_http2_ids_rejected();
    test_settings_duplicate_ids_rejected();
    test_settings_grease_ignored();
    test_settings_truncated_pair_rejected();
    test_settings_zero_values();
    test_data_frame_write();
    test_data_frame_rejects_undersized_buffer();
    test_headers_frame_rejects_undersized_buffer();
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
    test_qpack_literal_name_ref_never_index();
    test_qpack_rejects_s_bit_with_zero_ric();
    test_qpack_rejects_nul_and_crlf();
    test_qpack_field_section_size();
    test_qpack_rejects_uppercase_and_empty_name();
    test_authority_rejects_host_list_and_userinfo();
    test_trailer_name_rejects_framing_fields();
    test_response_body_forbidden_for_204_304();
    test_apply_response_content_length();
    test_request_path_allows_options_asterisk();
    test_method_rejects_non_token();
    test_field_section_over_limit();
    test_goaway_and_stream_type_helpers();
    test_varint_payload_and_max_push_id();
    test_qpack_decoder_stream_instructions();
    test_qpack_rejects_truncated_prefix_integer();
    test_qpack_rejects_dynamic_and_post_base();
    test_qpack_rejects_truncated_header_list();
    test_qpack_encoder_create_destroy();

    printf("\n%d passed, %d failed (of %d)\n", tests_passed, tests_failed, tests_run);
    if (tests_failed == 0) puts("passed");
    return tests_failed > 0 ? 1 : 0;
}
