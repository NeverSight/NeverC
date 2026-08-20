/*
 * QUIC Frame Layer Tests
 * Tests frame codec: parse + write roundtrip for all major frame types.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Pull in the QUIC source directly for testing */
#include "../../../std/src/net/quic/quic_varint.c"
#include "../../../std/src/net/quic/quic_frame.c"
#include "../../../std/src/net/quic/quic_transport_params.c"

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

/* ======================================================================
 * CRYPTO Frame Tests
 * ====================================================================== */

static void test_crypto_frame_roundtrip(void) {
    uint8_t data[] = "ClientHello TLS data here";
    uint8_t buf[256];
    size_t written;

    ASSERT_EQ(neverc_quic_write_crypto_frame(buf, sizeof(buf), 0,
               data, sizeof(data) - 1, &written), 0);
    ASSERT_TRUE(written > 0);

    quic_frame_crypto_t out;
    size_t consumed;
    ASSERT_EQ(neverc_quic_parse_crypto_frame(buf, written, &out, &consumed), 0);
    ASSERT_EQ(consumed, written);
    ASSERT_EQ(out.offset, 0);
    ASSERT_EQ(out.data_len, sizeof(data) - 1);
    ASSERT_TRUE(memcmp(out.data, data, out.data_len) == 0);
}

static void test_crypto_frame_with_offset(void) {
    uint8_t data[] = "continuation data";
    uint8_t buf[256];
    size_t written;

    ASSERT_EQ(neverc_quic_write_crypto_frame(buf, sizeof(buf), 12345,
               data, sizeof(data) - 1, &written), 0);

    quic_frame_crypto_t out;
    size_t consumed;
    ASSERT_EQ(neverc_quic_parse_crypto_frame(buf, written, &out, &consumed), 0);
    ASSERT_EQ(out.offset, 12345);
    ASSERT_EQ(out.data_len, sizeof(data) - 1);
    ASSERT_TRUE(memcmp(out.data, data, out.data_len) == 0);
}

/* ======================================================================
 * STREAM Frame Tests
 * ====================================================================== */

static void test_stream_frame_basic(void) {
    uint8_t data[] = "Hello QUIC stream!";
    quic_frame_stream_t frame = {
        .stream_id = 4,
        .offset = 0,
        .data = data,
        .data_len = sizeof(data) - 1,
        .fin = 0,
    };

    uint8_t buf[256];
    size_t written;
    ASSERT_EQ(neverc_quic_write_stream_frame(buf, sizeof(buf), &frame, &written), 0);

    quic_frame_stream_t out;
    size_t consumed;
    ASSERT_EQ(neverc_quic_parse_stream_frame(buf, written, &out, &consumed), 0);
    ASSERT_EQ(consumed, written);
    ASSERT_EQ(out.stream_id, 4);
    ASSERT_EQ(out.offset, 0);
    ASSERT_EQ(out.data_len, sizeof(data) - 1);
    ASSERT_EQ(out.fin, 0);
    ASSERT_TRUE(memcmp(out.data, data, out.data_len) == 0);
}

static void test_stream_frame_with_offset_and_fin(void) {
    uint8_t data[] = "final bytes";
    quic_frame_stream_t frame = {
        .stream_id = 17,
        .offset = 99999,
        .data = data,
        .data_len = sizeof(data) - 1,
        .fin = 1,
    };

    uint8_t buf[256];
    size_t written;
    ASSERT_EQ(neverc_quic_write_stream_frame(buf, sizeof(buf), &frame, &written), 0);

    quic_frame_stream_t out;
    size_t consumed;
    ASSERT_EQ(neverc_quic_parse_stream_frame(buf, written, &out, &consumed), 0);
    ASSERT_EQ(out.stream_id, 17);
    ASSERT_EQ(out.offset, 99999);
    ASSERT_EQ(out.data_len, sizeof(data) - 1);
    ASSERT_EQ(out.fin, 1);
}

static void test_crypto_frame_rejects_offset_length_overflow(void) {
    /* RFC 9000 §19.6: offset + length MUST NOT exceed 2^62-1. */
    uint8_t overflow[] = {
        0x06,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, /* offset = 2^62-1 */
        0x01, 'A',
    };
    quic_frame_crypto_t out;
    size_t consumed;
    ASSERT_EQ(neverc_quic_parse_crypto_frame(overflow, sizeof(overflow),
                                             &out, &consumed), -1);

    uint8_t at_limit[] = {
        0x06,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, /* offset = 2^62-1 */
        0x00,
    };
    ASSERT_EQ(neverc_quic_parse_crypto_frame(at_limit, sizeof(at_limit),
                                             &out, &consumed), 0);
    ASSERT_EQ(out.offset, QUIC_VARINT_MAX);
    ASSERT_EQ(out.data_len, 0);

    uint8_t data[] = { 'A' };
    uint8_t buf[32];
    size_t written;
    ASSERT_EQ(neverc_quic_write_crypto_frame(buf, sizeof(buf), QUIC_VARINT_MAX,
                                             data, 1, &written), -1);
    ASSERT_EQ(neverc_quic_write_crypto_frame(buf, sizeof(buf), QUIC_VARINT_MAX,
                                             data, 0, &written), 0);
}

static void test_crypto_frame_rejects_undersized_buffer(void) {
    uint8_t data[] = { 'A', 'B', 'C', 'D' };
    uint8_t buf[32];
    size_t written = 0;
    ASSERT_EQ(neverc_quic_write_crypto_frame(buf, sizeof(buf), 0,
                                             data, sizeof(data), &written), 0);
    ASSERT_TRUE(written > sizeof(data));
    written = 0;
    ASSERT_EQ(neverc_quic_write_crypto_frame(buf, 1, 0, data, sizeof(data),
                                             &written), -1);
    ASSERT_EQ(written, 0);
}

static void test_stream_frame_rejects_offset_length_overflow(void) {
    /* RFC 9000 §19.8: offset + length MUST NOT exceed 2^62-1. */
    uint8_t overflow_len[] = {
        0x0E, /* STREAM + OFF + LEN */
        0x00,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, /* offset = 2^62-1 */
        0x01, 'A',
    };
    quic_frame_stream_t out;
    size_t consumed;
    ASSERT_EQ(neverc_quic_parse_stream_frame(overflow_len, sizeof(overflow_len),
                                             &out, &consumed), -1);

    uint8_t overflow_to_end[] = {
        0x0C, /* STREAM + OFF, no LEN: data extends to packet end */
        0x00,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        'A',
    };
    ASSERT_EQ(neverc_quic_parse_stream_frame(overflow_to_end,
                                             sizeof(overflow_to_end),
                                             &out, &consumed), -1);

    uint8_t at_limit[] = {
        0x0E,
        0x00,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0x00,
    };
    ASSERT_EQ(neverc_quic_parse_stream_frame(at_limit, sizeof(at_limit),
                                             &out, &consumed), 0);
    ASSERT_EQ(out.offset, QUIC_VARINT_MAX);
    ASSERT_EQ(out.data_len, 0);

    uint8_t data[] = { 'A' };
    quic_frame_stream_t frame = {
        .stream_id = 0,
        .offset = QUIC_VARINT_MAX,
        .data = data,
        .data_len = 1,
        .fin = 0,
    };
    uint8_t buf[32];
    size_t written;
    ASSERT_EQ(neverc_quic_write_stream_frame(buf, sizeof(buf), &frame,
                                             &written), -1);
    frame.data_len = 0;
    ASSERT_EQ(neverc_quic_write_stream_frame(buf, sizeof(buf), &frame,
                                             &written), 0);
}

static void test_stream_frame_nonminimal_type(void) {
    /* RFC 9000 §16: type 0x0a (STREAM + LEN) encoded as a 2-byte varint. */
    uint8_t buf[] = { 0x40, 0x0A, 0x04, 0x05, 'h', 'e', 'l', 'l', 'o' };
    quic_frame_stream_t out;
    size_t consumed;
    ASSERT_EQ(neverc_quic_parse_stream_frame(buf, sizeof(buf), &out, &consumed),
              0);
    ASSERT_EQ(consumed, sizeof(buf));
    ASSERT_EQ(out.stream_id, 4);
    ASSERT_EQ(out.offset, 0);
    ASSERT_EQ(out.data_len, 5);
    ASSERT_EQ(out.fin, 0);
    ASSERT_TRUE(memcmp(out.data, "hello", 5) == 0);
}

static void test_stream_frame_large_id(void) {
    uint8_t data[] = "x";
    quic_frame_stream_t frame = {
        .stream_id = 1000000004ULL,
        .offset = 0xFFFFFFFULL,
        .data = data,
        .data_len = 1,
        .fin = 0,
    };

    uint8_t buf[256];
    size_t written;
    ASSERT_EQ(neverc_quic_write_stream_frame(buf, sizeof(buf), &frame, &written), 0);

    quic_frame_stream_t out;
    size_t consumed;
    ASSERT_EQ(neverc_quic_parse_stream_frame(buf, written, &out, &consumed), 0);
    ASSERT_EQ(out.stream_id, 1000000004ULL);
    ASSERT_EQ(out.offset, 0xFFFFFFFULL);
}

/* ======================================================================
 * ACK Frame Tests
 * ====================================================================== */

static void test_ack_frame_single_range(void) {
    quic_ack_range_t ranges[1] = {{ .start = 5, .end = 11 }};
    quic_frame_ack_t ack = {
        .largest_acked = 10,
        .ack_delay = 100,
        .ranges = ranges,
        .nranges = 1,
    };

    uint8_t buf[256];
    size_t written;
    ASSERT_EQ(neverc_quic_write_ack_frame(buf, sizeof(buf), &ack, &written), 0);

    quic_frame_ack_t out;
    size_t consumed;
    ASSERT_EQ(neverc_quic_parse_ack_frame(buf, written, &out, &consumed), 0);
    ASSERT_EQ(consumed, written);
    ASSERT_EQ(out.largest_acked, 10);
    ASSERT_EQ(out.ack_delay, 100);
    ASSERT_EQ(out.nranges, 1);
    ASSERT_EQ(out.ranges[0].start, 5);
    ASSERT_EQ(out.ranges[0].end, 11);
    free(out.ranges);
}

static void test_ack_rejects_first_range_past_largest(void) {
    /* First ACK Range larger than Largest Acknowledged is invalid. */
    uint8_t buf[] = {
        0x02, /* ACK */
        0x05, /* largest = 5 */
        0x00, /* delay */
        0x00, /* range count */
        0x06, /* first range = 6 > 5 */
    };
    quic_frame_ack_t out;
    size_t consumed;
    ASSERT_EQ(neverc_quic_parse_ack_frame(buf, sizeof(buf), &out, &consumed),
              -1);
}

static void test_retire_conn_id_roundtrip(void) {
    uint8_t buf[16];
    size_t written;
    ASSERT_EQ(neverc_quic_write_retire_conn_id(buf, sizeof(buf), 7,
                                               &written), 0);
    quic_frame_retire_conn_id_t out;
    size_t consumed;
    ASSERT_EQ(neverc_quic_parse_retire_conn_id(buf, written, &out,
                                               &consumed), 0);
    ASSERT_EQ(consumed, written);
    ASSERT_EQ(out.sequence, 7);
}

static void test_ack_ecn_counts(void) {
    uint8_t buf[] = {
        0x03, /* ACK_ECN */
        0x05, /* largest = 5 */
        0x00, /* delay */
        0x00, /* range count */
        0x05, /* first range = 5 */
        0x01, 0x02, 0x03, /* ect0, ect1, ce */
    };
    quic_frame_ack_t out;
    size_t consumed;
    ASSERT_EQ(neverc_quic_parse_ack_frame(buf, sizeof(buf), &out, &consumed),
              0);
    ASSERT_EQ(consumed, sizeof(buf));
    ASSERT_EQ(out.largest_acked, 5);
    ASSERT_EQ(out.ect0, 1);
    ASSERT_EQ(out.ect1, 2);
    ASSERT_EQ(out.ecn_ce, 3);
    free(out.ranges);
}

static void test_connection_close_rejects_oversize_reason(void) {
    quic_frame_connection_close_t cc;
    memset(&cc, 0, sizeof(cc));
    cc.error_code = 0x0a;
    cc.reason = "x";
    cc.reason_len = (size_t)QUIC_VARINT_MAX + 1U;
    uint8_t buf[32];
    size_t written;
    ASSERT_EQ(neverc_quic_write_connection_close(buf, sizeof(buf), &cc,
                                                 &written), -1);
}

static void test_ack_frame_multiple_ranges(void) {
    /* Ack ranges: [95,101), [80,86), [50,61) */
    quic_ack_range_t ranges[3] = {
        { .start = 95, .end = 101 },
        { .start = 80, .end = 86 },
        { .start = 50, .end = 61 },
    };
    quic_frame_ack_t ack = {
        .largest_acked = 100,
        .ack_delay = 250,
        .ranges = ranges,
        .nranges = 3,
    };

    uint8_t buf[256];
    size_t written;
    ASSERT_EQ(neverc_quic_write_ack_frame(buf, sizeof(buf), &ack, &written), 0);

    quic_frame_ack_t out;
    size_t consumed;
    ASSERT_EQ(neverc_quic_parse_ack_frame(buf, written, &out, &consumed), 0);
    ASSERT_EQ(out.nranges, 3);
    ASSERT_EQ(out.ranges[0].start, 95);
    ASSERT_EQ(out.ranges[0].end, 101);
    ASSERT_EQ(out.ranges[1].start, 80);
    ASSERT_EQ(out.ranges[1].end, 86);
    ASSERT_EQ(out.ranges[2].start, 50);
    ASSERT_EQ(out.ranges[2].end, 61);
    free(out.ranges);
}

/* ======================================================================
 * CONNECTION_CLOSE Frame Tests
 * ====================================================================== */

static void test_connection_close_transport(void) {
    quic_frame_connection_close_t cc = {
        .error_code = 0x0a, /* PROTOCOL_VIOLATION */
        .frame_type = 0x06, /* CRYPTO frame triggered it */
        .reason = "bad handshake",
        .reason_len = 13,
        .is_app = 0,
    };

    uint8_t buf[256];
    size_t written;
    ASSERT_EQ(neverc_quic_write_connection_close(buf, sizeof(buf), &cc, &written), 0);

    quic_frame_connection_close_t out;
    size_t consumed;
    ASSERT_EQ(neverc_quic_parse_connection_close(buf, written, &out, &consumed), 0);
    ASSERT_EQ(consumed, written);
    ASSERT_EQ(out.error_code, 0x0a);
    ASSERT_EQ(out.frame_type, 0x06);
    ASSERT_EQ(out.reason_len, 13);
    ASSERT_EQ(out.is_app, 0);
    ASSERT_TRUE(memcmp(out.reason, "bad handshake", 13) == 0);
}

static void test_connection_close_app(void) {
    quic_frame_connection_close_t cc = {
        .error_code = 0x0100, /* H3_NO_ERROR */
        .reason = "",
        .reason_len = 0,
        .is_app = 1,
    };

    uint8_t buf[256];
    size_t written;
    ASSERT_EQ(neverc_quic_write_connection_close(buf, sizeof(buf), &cc, &written), 0);

    quic_frame_connection_close_t out;
    size_t consumed;
    ASSERT_EQ(neverc_quic_parse_connection_close(buf, written, &out, &consumed), 0);
    ASSERT_EQ(out.error_code, 0x0100);
    ASSERT_EQ(out.is_app, 1);
    ASSERT_EQ(out.reason_len, 0);
}

/* ======================================================================
 * RESET_STREAM / STOP_SENDING Tests
 * ====================================================================== */

static void test_reset_stream_roundtrip(void) {
    uint8_t buf[64];
    size_t written;
    ASSERT_EQ(neverc_quic_write_reset_stream(buf, sizeof(buf),
               /*stream_id=*/8, /*error=*/0x0101, /*final_size=*/55000, &written), 0);

    quic_frame_reset_stream_t out;
    size_t consumed;
    ASSERT_EQ(neverc_quic_parse_reset_stream(buf, written, &out, &consumed), 0);
    ASSERT_EQ(out.stream_id, 8);
    ASSERT_EQ(out.error_code, 0x0101);
    ASSERT_EQ(out.final_size, 55000);
}

/* ======================================================================
 * Transport Parameters Tests
 * ====================================================================== */

static void test_transport_params_roundtrip(void) {
    quic_transport_params_t tp;
    neverc_quic_transport_params_default(&tp);
    tp.max_idle_timeout = 60000;
    tp.initial_max_data = 5 * 1024 * 1024;
    tp.initial_max_streams_bidi = 200;
    tp.initial_max_streams_uni = 50;
    tp.disable_active_migration = 1;
    tp.max_datagram_frame_size = 1200;

    /* Set a connection ID */
    memcpy(tp.initial_scid, "\x01\x02\x03\x04", 4);
    tp.initial_scid_len = 4;

    uint8_t buf[512];
    size_t written;
    ASSERT_EQ(neverc_quic_transport_params_encode(&tp, buf, sizeof(buf), &written), 0);
    ASSERT_TRUE(written > 0);

    quic_transport_params_t decoded;
    ASSERT_EQ(neverc_quic_transport_params_decode(buf, written, &decoded), 0);
    ASSERT_EQ(decoded.max_idle_timeout, 60000);
    ASSERT_EQ(decoded.initial_max_data, 5 * 1024 * 1024);
    ASSERT_EQ(decoded.initial_max_streams_bidi, 200);
    ASSERT_EQ(decoded.initial_max_streams_uni, 50);
    ASSERT_EQ(decoded.disable_active_migration, 1);
    ASSERT_EQ(decoded.max_datagram_frame_size, 1200);
    ASSERT_EQ(decoded.initial_scid_len, 4);
    ASSERT_TRUE(memcmp(decoded.initial_scid, "\x01\x02\x03\x04", 4) == 0);
}

static size_t write_tp(uint8_t *buf, size_t cap, uint64_t id,
                       const uint8_t *val, size_t vlen) {
    size_t pos = 0, w;
    if (neverc_quic_varint_encode(id, buf + pos, cap - pos, &w) != 0)
        return 0;
    pos += w;
    if (neverc_quic_varint_encode(vlen, buf + pos, cap - pos, &w) != 0)
        return 0;
    pos += w;
    if (vlen) {
        if (pos + vlen > cap) return 0;
        memcpy(buf + pos, val, vlen);
        pos += vlen;
    }
    return pos;
}

static void test_transport_params_client_forbidden(void) {
    quic_transport_params_t tp;
    uint8_t buf[64];
    uint8_t scid[4] = { 1, 2, 3, 4 };
    size_t n = write_tp(buf, sizeof(buf), 0x0f, scid, sizeof(scid));
    ASSERT_TRUE(n > 0);
    ASSERT_EQ(neverc_quic_transport_params_decode(buf, n, &tp), 0);
    ASSERT_EQ(tp.has_initial_scid, 1);
    ASSERT_EQ(neverc_quic_transport_params_require_client(&tp), 0);
    ASSERT_EQ(neverc_quic_transport_params_require_server(&tp), -1);

    /* Empty original_destination_connection_id is still present. */
    n = write_tp(buf, sizeof(buf), 0x00, NULL, 0);
    ASSERT_TRUE(n > 0);
    ASSERT_EQ(neverc_quic_transport_params_decode(buf, n, &tp), 0);
    ASSERT_EQ(tp.has_original_dcid, 1);
    ASSERT_EQ(tp.original_dcid_len, 0);
    ASSERT_EQ(neverc_quic_transport_params_client_forbidden(&tp), -1);

    uint8_t token[16];
    memset(token, 0xA5, sizeof(token));
    n = write_tp(buf, sizeof(buf), 0x02, token, sizeof(token));
    ASSERT_EQ(neverc_quic_transport_params_decode(buf, n, &tp), 0);
    ASSERT_EQ(neverc_quic_transport_params_client_forbidden(&tp), -1);

    uint8_t pref[42];
    memset(pref, 0, sizeof(pref));
    pref[24] = 1;
    pref[25] = 0xAA;
    memset(pref + 26, 0x5A, 16);
    n = write_tp(buf, sizeof(buf), 0x0d, pref, sizeof(pref));
    ASSERT_EQ(neverc_quic_transport_params_decode(buf, n, &tp), 0);
    ASSERT_EQ(tp.has_preferred_address, 1);
    ASSERT_EQ(neverc_quic_transport_params_client_forbidden(&tp), -1);

    uint8_t retry[8];
    memset(retry, 0x11, sizeof(retry));
    n = write_tp(buf, sizeof(buf), 0x10, retry, sizeof(retry));
    ASSERT_EQ(neverc_quic_transport_params_decode(buf, n, &tp), 0);
    ASSERT_EQ(tp.has_retry_scid, 1);
    ASSERT_EQ(neverc_quic_transport_params_client_forbidden(&tp), -1);
    ASSERT_EQ(neverc_quic_transport_params_require_server(&tp), -1);

    /* Server params: original_dcid + initial_scid, no retry_scid. */
    uint8_t server[64];
    size_t a = write_tp(server, sizeof(server), 0x00, scid, sizeof(scid));
    size_t b = write_tp(server + a, sizeof(server) - a, 0x0f, scid,
                        sizeof(scid));
    ASSERT_TRUE(a > 0 && b > 0);
    ASSERT_EQ(neverc_quic_transport_params_decode(server, a + b, &tp), 0);
    ASSERT_EQ(neverc_quic_transport_params_require_server(&tp), 0);
    ASSERT_EQ(neverc_quic_transport_params_require_client(&tp), -1);
}

static void test_new_token_rejects_empty(void) {
    uint8_t empty[] = { 0x07, 0x00 };
    size_t consumed = 0;
    ASSERT_EQ(neverc_quic_parse_new_token(empty, sizeof(empty), &consumed),
              -1);

    uint8_t ok[] = { 0x07, 0x01, 0xaa };
    ASSERT_EQ(neverc_quic_parse_new_token(ok, sizeof(ok), &consumed), 0);
    ASSERT_EQ(consumed, 3);
}

static void test_transport_params_defaults(void) {
    quic_transport_params_t tp;
    neverc_quic_transport_params_default(&tp);

    ASSERT_EQ(tp.max_udp_payload_size, 65527);
    ASSERT_EQ(tp.ack_delay_exponent, 3);
    ASSERT_EQ(tp.max_ack_delay, 25);
    ASSERT_EQ(tp.active_connection_id_limit, 2);
    ASSERT_EQ(tp.initial_max_data, 10 * 1024 * 1024);
    ASSERT_EQ(tp.initial_max_streams_bidi, 100);
}

static void test_transport_params_active_cid_limit_rfc_bounds(void) {
    /* RFC 9000 §18.2: values below 2 are invalid; there is no upper bound. */
    quic_transport_params_t tp;
    uint8_t buf[16];
    uint8_t one[] = { 1 };
    size_t n = write_tp(buf, sizeof(buf), 0x0e, one, sizeof(one));
    ASSERT_TRUE(n > 0);
    ASSERT_EQ(neverc_quic_transport_params_decode(buf, n, &tp), -1);

    uint8_t sixteen[] = { 16 };
    n = write_tp(buf, sizeof(buf), 0x0e, sixteen, sizeof(sixteen));
    ASSERT_TRUE(n > 0);
    ASSERT_EQ(neverc_quic_transport_params_decode(buf, n, &tp), 0);
    ASSERT_EQ(tp.active_connection_id_limit, 16);
}

static void test_transport_params_preferred_address_layout(void) {
    /* RFC 9000 §18.2: IPv4+port + IPv6+port + CID length 1..20 + CID + token. */
    quic_transport_params_t tp;
    uint8_t valid[42];
    memset(valid, 0, sizeof(valid));
    valid[24] = 1; /* connection ID length */
    valid[25] = 0xAA;
    memset(valid + 26, 0x5A, 16);
    uint8_t buf[64];
    size_t n = write_tp(buf, sizeof(buf), 0x0d, valid, sizeof(valid));
    ASSERT_TRUE(n > 0);
    ASSERT_EQ(neverc_quic_transport_params_decode(buf, n, &tp), 0);
    ASSERT_EQ(tp.has_preferred_address, 1);

    uint8_t truncated[41];
    memset(truncated, 0, sizeof(truncated));
    truncated[24] = 1;
    n = write_tp(buf, sizeof(buf), 0x0d, truncated, sizeof(truncated));
    ASSERT_EQ(neverc_quic_transport_params_decode(buf, n, &tp), -1);

    uint8_t bad_cid_len[42];
    memset(bad_cid_len, 0, sizeof(bad_cid_len));
    bad_cid_len[24] = 0;
    n = write_tp(buf, sizeof(buf), 0x0d, bad_cid_len, sizeof(bad_cid_len));
    ASSERT_EQ(neverc_quic_transport_params_decode(buf, n, &tp), -1);

    n = write_tp(buf, sizeof(buf), 0x0d, NULL, 0);
    ASSERT_EQ(neverc_quic_transport_params_decode(buf, n, &tp), -1);
}

static void test_quic_varint_bounds_and_truncated(void) {
    uint8_t buf[8];
    size_t written = 0, consumed = 0;
    uint64_t value = 0;

    ASSERT_EQ(neverc_quic_varint_encode(63, buf, sizeof(buf), &written), 0);
    ASSERT_EQ(written, 1);
    ASSERT_EQ(neverc_quic_varint_decode(buf, written, &value, &consumed), 0);
    ASSERT_EQ(value, 63);
    ASSERT_EQ(consumed, 1);
    ASSERT_EQ(neverc_quic_varint_len(63), 1);

    ASSERT_EQ(neverc_quic_varint_encode(64, buf, sizeof(buf), &written), 0);
    ASSERT_EQ(written, 2);
    ASSERT_EQ(neverc_quic_varint_len(64), 2);

    ASSERT_EQ(neverc_quic_varint_encode(QUIC_VARINT_MAX, buf, sizeof(buf),
                                        &written), 0);
    ASSERT_EQ(written, 8);
    ASSERT_EQ(neverc_quic_varint_decode(buf, written, &value, &consumed), 0);
    ASSERT_EQ(value, QUIC_VARINT_MAX);
    ASSERT_EQ(neverc_quic_varint_len(QUIC_VARINT_MAX), 8);
    ASSERT_EQ(neverc_quic_varint_len(QUIC_VARINT_MAX + 1U), 0);
    ASSERT_EQ(neverc_quic_varint_encode(QUIC_VARINT_MAX + 1U, buf, sizeof(buf),
                                        &written), -1);

    uint8_t truncated2[] = { 0x40 };
    ASSERT_EQ(neverc_quic_varint_decode(truncated2, sizeof(truncated2),
                                        &value, &consumed), -1);
    uint8_t truncated4[] = { 0x80, 0x00 };
    ASSERT_EQ(neverc_quic_varint_decode(truncated4, sizeof(truncated4),
                                        &value, &consumed), -1);
    uint8_t truncated8[] = { 0xC0, 0x00, 0x00 };
    ASSERT_EQ(neverc_quic_varint_decode(truncated8, sizeof(truncated8),
                                        &value, &consumed), -1);
    ASSERT_EQ(neverc_quic_varint_decode(NULL, 1, &value, &consumed), -1);
}

static void test_truncated_frames_rejected(void) {
    quic_frame_crypto_t crypto;
    quic_frame_stream_t stream;
    quic_frame_ack_t ack;
    quic_frame_reset_stream_t reset;
    quic_frame_connection_close_t close_frame;
    size_t consumed = 0;

    uint8_t crypto_short[] = { 0x06, 0x00, 0x05, 'A' };
    ASSERT_EQ(neverc_quic_parse_crypto_frame(crypto_short, sizeof(crypto_short),
                                             &crypto, &consumed), -1);
    uint8_t crypto_type[] = { 0x06 };
    ASSERT_EQ(neverc_quic_parse_crypto_frame(crypto_type, sizeof(crypto_type),
                                             &crypto, &consumed), -1);

    uint8_t stream_short[] = { 0x0A, 0x00, 0x05, 'A' };
    ASSERT_EQ(neverc_quic_parse_stream_frame(stream_short, sizeof(stream_short),
                                             &stream, &consumed), -1);

    uint8_t ack_short[] = { 0x02, 0x05, 0x00, 0x01 };
    ASSERT_EQ(neverc_quic_parse_ack_frame(ack_short, sizeof(ack_short),
                                          &ack, &consumed), -1);

    uint8_t ack_missing_gap[] = { 0x02, 0x05, 0x00, 0x01, 0x05 };
    ASSERT_EQ(neverc_quic_parse_ack_frame(ack_missing_gap,
                                          sizeof(ack_missing_gap),
                                          &ack, &consumed), -1);

    uint8_t ack_ecn_short[] = {
        0x03, 0x05, 0x00, 0x00, 0x05, 0x01, 0x02
    };
    ASSERT_EQ(neverc_quic_parse_ack_frame(ack_ecn_short, sizeof(ack_ecn_short),
                                          &ack, &consumed), -1);

    uint8_t reset_short[] = { 0x04, 0x08, 0x01 };
    ASSERT_EQ(neverc_quic_parse_reset_stream(reset_short, sizeof(reset_short),
                                             &reset, &consumed), -1);

    uint8_t close_short[] = { 0x1c, 0x0a, 0x00, 0x05, 'x' };
    ASSERT_EQ(neverc_quic_parse_connection_close(close_short,
                                                 sizeof(close_short),
                                                 &close_frame, &consumed), -1);

    uint8_t token_short[] = { 0x07, 0x05, 0xaa };
    ASSERT_EQ(neverc_quic_parse_new_token(token_short, sizeof(token_short),
                                          &consumed), -1);
}

static void test_padding_rejects_nonminimal_type(void) {
    ASSERT_EQ(neverc_quic_frame_type_encoding_ok(0, 1), 0);
    ASSERT_EQ(neverc_quic_frame_type_encoding_ok(0, 2), -1);
    ASSERT_EQ(neverc_quic_frame_type_encoding_ok(0, 0), -1);
    ASSERT_EQ(neverc_quic_frame_type_encoding_ok(1, 2), 0);
}

static void test_stream_count_frame_bounds(void) {
    uint8_t buf[16];
    size_t pos = 0, w = 0, consumed = 0;
    uint64_t maximum = 0;
    ASSERT_EQ(neverc_quic_varint_encode(0x16, buf + pos, sizeof(buf) - pos,
                                        &w), 0);
    pos += w;
    ASSERT_EQ(neverc_quic_varint_encode(QUIC_MAX_STREAM_COUNT, buf + pos,
                                        sizeof(buf) - pos, &w), 0);
    pos += w;
    ASSERT_EQ(neverc_quic_parse_stream_count_frame(buf, pos, &maximum,
                                                   &consumed), 0);
    ASSERT_EQ(maximum, QUIC_MAX_STREAM_COUNT);
    ASSERT_EQ(consumed, pos);

    pos = 0;
    ASSERT_EQ(neverc_quic_varint_encode(0x12, buf + pos, sizeof(buf) - pos,
                                        &w), 0);
    pos += w;
    ASSERT_EQ(neverc_quic_varint_encode(QUIC_MAX_STREAM_COUNT + 1U, buf + pos,
                                        sizeof(buf) - pos, &w), 0);
    pos += w;
    ASSERT_EQ(neverc_quic_parse_stream_count_frame(buf, pos, &maximum,
                                                   &consumed), -1);

    uint8_t truncated[] = { 0x13, 0x40 };
    ASSERT_EQ(neverc_quic_parse_stream_count_frame(truncated,
                                                   sizeof(truncated),
                                                   &maximum, &consumed), -1);
}

static void test_transport_params_truncated_and_rfc_bounds(void) {
    quic_transport_params_t tp;
    uint8_t buf[32];

    uint8_t truncated_id[] = { 0x40 };
    ASSERT_EQ(neverc_quic_transport_params_decode(truncated_id,
                                                  sizeof(truncated_id),
                                                  &tp), -1);

    uint8_t truncated_len[] = { 0x01 };
    ASSERT_EQ(neverc_quic_transport_params_decode(truncated_len,
                                                  sizeof(truncated_len),
                                                  &tp), -1);

    uint8_t truncated_len_varint[] = { 0x01, 0x40 };
    ASSERT_EQ(neverc_quic_transport_params_decode(truncated_len_varint,
                                                  sizeof(truncated_len_varint),
                                                  &tp), -1);

    uint8_t short_value[] = { 0x01, 0x05, 0x00, 0x00 };
    ASSERT_EQ(neverc_quic_transport_params_decode(short_value,
                                                  sizeof(short_value),
                                                  &tp), -1);

    uint8_t padded_idle[] = { 0x01, 0x02, 0x01, 0x00 };
    ASSERT_EQ(neverc_quic_transport_params_decode(padded_idle,
                                                  sizeof(padded_idle),
                                                  &tp), -1);

    uint8_t dup[16];
    size_t a = write_tp(dup, sizeof(dup), 0x01, (const uint8_t *)"\x01", 1);
    size_t b = write_tp(dup + a, sizeof(dup) - a, 0x01,
                        (const uint8_t *)"\x02", 1);
    ASSERT_TRUE(a > 0 && b > 0);
    ASSERT_EQ(neverc_quic_transport_params_decode(dup, a + b, &tp), -1);

    uint8_t udp[] = { 0x80, 0x00, 0x04, 0xAF }; /* 1199 as 4-byte varint */
    size_t n = write_tp(buf, sizeof(buf), 0x03, udp, sizeof(udp));
    ASSERT_TRUE(n > 0);
    ASSERT_EQ(neverc_quic_transport_params_decode(buf, n, &tp), -1);

    uint8_t exp[] = { 21 };
    n = write_tp(buf, sizeof(buf), 0x0a, exp, sizeof(exp));
    ASSERT_EQ(neverc_quic_transport_params_decode(buf, n, &tp), -1);

    uint8_t delay[4];
    size_t dw = 0;
    ASSERT_EQ(neverc_quic_varint_encode(16384, delay, sizeof(delay), &dw), 0);
    n = write_tp(buf, sizeof(buf), 0x0b, delay, dw);
    ASSERT_EQ(neverc_quic_transport_params_decode(buf, n, &tp), -1);

    uint8_t streams[8];
    size_t sw = 0;
    ASSERT_EQ(neverc_quic_varint_encode(QUIC_MAX_STREAM_COUNT + 1U, streams,
                                        sizeof(streams), &sw), 0);
    n = write_tp(buf, sizeof(buf), 0x08, streams, sw);
    ASSERT_EQ(neverc_quic_transport_params_decode(buf, n, &tp), -1);

    ASSERT_EQ(neverc_quic_varint_encode(QUIC_MAX_STREAM_COUNT, streams,
                                        sizeof(streams), &sw), 0);
    n = write_tp(buf, sizeof(buf), 0x08, streams, sw);
    ASSERT_EQ(neverc_quic_transport_params_decode(buf, n, &tp), 0);
    ASSERT_EQ(tp.initial_max_streams_bidi, QUIC_MAX_STREAM_COUNT);

    quic_transport_params_t encoded;
    neverc_quic_transport_params_default(&encoded);
    encoded.initial_max_streams_bidi = QUIC_MAX_STREAM_COUNT + 1U;
    uint8_t out[256];
    size_t written = 0;
    ASSERT_EQ(neverc_quic_transport_params_encode(&encoded, out, sizeof(out),
                                                  &written), -1);

    uint8_t migration[] = { 0x0c, 0x01, 0x00 };
    ASSERT_EQ(neverc_quic_transport_params_decode(migration,
                                                  sizeof(migration),
                                                  &tp), -1);

    uint8_t token15[15];
    memset(token15, 0x11, sizeof(token15));
    n = write_tp(buf, sizeof(buf), 0x02, token15, sizeof(token15));
    ASSERT_EQ(neverc_quic_transport_params_decode(buf, n, &tp), -1);
}

/* ======================================================================
 * Ping / Handshake Done
 * ====================================================================== */

static void test_ping_frame(void) {
    uint8_t buf[16];
    size_t written;
    ASSERT_EQ(neverc_quic_write_ping(buf, sizeof(buf), &written), 0);
    ASSERT_EQ(written, 1);
    ASSERT_EQ(buf[0], 0x01);
}

static void test_handshake_done_frame(void) {
    uint8_t buf[16];
    size_t written;
    ASSERT_EQ(neverc_quic_write_handshake_done(buf, sizeof(buf), &written), 0);
    ASSERT_EQ(written, 1);
    ASSERT_EQ(buf[0], 0x1e);
}

/* ======================================================================
 * MAX_DATA / MAX_STREAM_DATA
 * ====================================================================== */

static size_t write_new_conn_id(uint8_t *buf, size_t cap, uint64_t sequence,
                                uint64_t retire_prior_to, uint8_t cid_len,
                                const uint8_t *cid, const uint8_t *token) {
    size_t pos = 0, w;
    if (neverc_quic_varint_encode(0x18, buf + pos, cap - pos, &w) != 0)
        return 0;
    pos += w;
    if (neverc_quic_varint_encode(sequence, buf + pos, cap - pos, &w) != 0)
        return 0;
    pos += w;
    if (neverc_quic_varint_encode(retire_prior_to, buf + pos, cap - pos,
                                  &w) != 0)
        return 0;
    pos += w;
    if (pos >= cap) return 0;
    buf[pos++] = cid_len;
    if (cid_len) {
        if (pos + cid_len > cap) return 0;
        memcpy(buf + pos, cid, cid_len);
        pos += cid_len;
    }
    if (pos + 16 > cap) return 0;
    memcpy(buf + pos, token, 16);
    return pos + 16;
}

static void test_new_conn_id_valid(void) {
    uint8_t cid[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    uint8_t token[16];
    memset(token, 0xA5, sizeof(token));
    uint8_t buf[64];
    size_t written = write_new_conn_id(buf, sizeof(buf), 1, 1, 8, cid, token);
    ASSERT_TRUE(written > 0);

    quic_frame_new_conn_id_t out;
    size_t consumed;
    ASSERT_EQ(neverc_quic_parse_new_conn_id(buf, written, &out, &consumed), 0);
    ASSERT_EQ(consumed, written);
    ASSERT_EQ(out.sequence, 1);
    ASSERT_EQ(out.retire_prior_to, 1);
    ASSERT_EQ(out.conn_id_len, 8);
    ASSERT_TRUE(memcmp(out.conn_id, cid, 8) == 0);
    ASSERT_TRUE(memcmp(out.stateless_reset_token, token, 16) == 0);

    ASSERT_EQ(neverc_quic_parse_new_conn_id(buf, written - 1, &out,
                                            &consumed), -1);
}

static void test_new_conn_id_rejects_zero_length(void) {
    uint8_t token[16];
    memset(token, 0x11, sizeof(token));
    uint8_t buf[64];
    size_t written = write_new_conn_id(buf, sizeof(buf), 1, 0, 0, NULL, token);
    ASSERT_TRUE(written > 0);

    quic_frame_new_conn_id_t out;
    size_t consumed;
    ASSERT_EQ(neverc_quic_parse_new_conn_id(buf, written, &out, &consumed), -1);
}

static void test_new_conn_id_rejects_retire_after_sequence(void) {
    uint8_t cid[8] = { 9, 8, 7, 6, 5, 4, 3, 2 };
    uint8_t token[16];
    memset(token, 0x22, sizeof(token));
    uint8_t buf[64];
    size_t written = write_new_conn_id(buf, sizeof(buf), 1, 2, 8, cid, token);
    ASSERT_TRUE(written > 0);

    quic_frame_new_conn_id_t out;
    size_t consumed;
    ASSERT_EQ(neverc_quic_parse_new_conn_id(buf, written, &out, &consumed), -1);
}

static void test_frame_allowed_encryption_levels(void) {
    /* RFC 9000 §12.4: STREAM / MAX_DATA / NEW_CONNECTION_ID are 0-RTT/1-RTT
     * only. CRYPTO and transport CONNECTION_CLOSE may appear in Initial. */
    ASSERT_EQ(neverc_quic_frame_allowed(0x08, QUIC_ENC_INITIAL), 0);
    ASSERT_EQ(neverc_quic_frame_allowed(0x08, QUIC_ENC_HANDSHAKE), 0);
    ASSERT_EQ(neverc_quic_frame_allowed(0x08, QUIC_ENC_APPLICATION), 1);
    ASSERT_EQ(neverc_quic_frame_allowed(0x10, QUIC_ENC_INITIAL), 0);
    ASSERT_EQ(neverc_quic_frame_allowed(0x10, QUIC_ENC_APPLICATION), 1);
    ASSERT_EQ(neverc_quic_frame_allowed(0x18, QUIC_ENC_INITIAL), 0);
    ASSERT_EQ(neverc_quic_frame_allowed(0x18, QUIC_ENC_APPLICATION), 1);
    ASSERT_EQ(neverc_quic_frame_allowed(0x19, QUIC_ENC_INITIAL), 0);
    ASSERT_EQ(neverc_quic_frame_allowed(0x19, QUIC_ENC_APPLICATION), 1);
    ASSERT_EQ(neverc_quic_frame_allowed(0x04, QUIC_ENC_INITIAL), 0);
    ASSERT_EQ(neverc_quic_frame_allowed(0x06, QUIC_ENC_INITIAL), 1);
    ASSERT_EQ(neverc_quic_frame_allowed(0x06, QUIC_ENC_EARLY_DATA), 0);
    ASSERT_EQ(neverc_quic_frame_allowed(0x06, QUIC_ENC_APPLICATION), 1);
    ASSERT_EQ(neverc_quic_frame_allowed(0x1c, QUIC_ENC_INITIAL), 1);
    ASSERT_EQ(neverc_quic_frame_allowed(0x1d, QUIC_ENC_INITIAL), 0);
    ASSERT_EQ(neverc_quic_frame_allowed(0x1d, QUIC_ENC_EARLY_DATA), 1);
    ASSERT_EQ(neverc_quic_frame_allowed(0x1d, QUIC_ENC_APPLICATION), 1);
    ASSERT_EQ(neverc_quic_frame_allowed(0x1e, QUIC_ENC_HANDSHAKE), 0);
    ASSERT_EQ(neverc_quic_frame_allowed(0x1e, QUIC_ENC_APPLICATION), 1);
    ASSERT_EQ(neverc_quic_frame_allowed(0x02, QUIC_ENC_INITIAL), 1);
    ASSERT_EQ(neverc_quic_frame_allowed(0x02, QUIC_ENC_EARLY_DATA), 0);
    /* RFC 9000 Table 3: PATH_CHALLENGE / PATH_RESPONSE are 1-RTT only. */
    ASSERT_EQ(neverc_quic_frame_allowed(0x1a, QUIC_ENC_EARLY_DATA), 0);
    ASSERT_EQ(neverc_quic_frame_allowed(0x1a, QUIC_ENC_APPLICATION), 1);
    ASSERT_EQ(neverc_quic_frame_allowed(0x1b, QUIC_ENC_EARLY_DATA), 0);
    ASSERT_EQ(neverc_quic_frame_allowed(0x1b, QUIC_ENC_APPLICATION), 1);
}

static void test_max_data_frame(void) {
    uint8_t buf[64];
    size_t written;
    ASSERT_EQ(neverc_quic_write_max_data(buf, sizeof(buf), 0x1000000, &written), 0);
    ASSERT_TRUE(written > 0);

    /* Verify first byte is MAX_DATA frame type */
    uint64_t ftype;
    size_t consumed;
    ASSERT_EQ(neverc_quic_varint_decode(buf, written, &ftype, &consumed), 0);
    ASSERT_EQ(ftype, 0x10);
}

/* ======================================================================
 * Main
 * ====================================================================== */

int main(void) {
    printf("QUIC Frame & Transport Params test suite:\n");

    test_crypto_frame_roundtrip();
    test_crypto_frame_with_offset();
    test_crypto_frame_rejects_offset_length_overflow();
    test_crypto_frame_rejects_undersized_buffer();
    test_quic_varint_bounds_and_truncated();
    test_truncated_frames_rejected();
    test_padding_rejects_nonminimal_type();
    test_stream_count_frame_bounds();
    test_stream_frame_basic();
    test_stream_frame_with_offset_and_fin();
    test_stream_frame_large_id();
    test_stream_frame_rejects_offset_length_overflow();
    test_stream_frame_nonminimal_type();
    test_ack_frame_single_range();
    test_ack_frame_multiple_ranges();
    test_ack_rejects_first_range_past_largest();
    test_retire_conn_id_roundtrip();
    test_ack_ecn_counts();
    test_connection_close_rejects_oversize_reason();
    test_connection_close_transport();
    test_connection_close_app();
    test_reset_stream_roundtrip();
    test_transport_params_roundtrip();
    test_transport_params_client_forbidden();
    test_new_token_rejects_empty();
    test_transport_params_defaults();
    test_transport_params_active_cid_limit_rfc_bounds();
    test_transport_params_preferred_address_layout();
    test_transport_params_truncated_and_rfc_bounds();
    test_ping_frame();
    test_handshake_done_frame();
    test_new_conn_id_valid();
    test_new_conn_id_rejects_zero_length();
    test_new_conn_id_rejects_retire_after_sequence();
    test_frame_allowed_encryption_levels();
    test_max_data_frame();

    printf("\n%d passed, %d failed (of %d)\n", tests_passed, tests_failed, tests_run);
    if (tests_failed == 0) puts("passed");
    return tests_failed > 0 ? 1 : 0;
}
