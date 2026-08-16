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
    test_stream_frame_basic();
    test_stream_frame_with_offset_and_fin();
    test_stream_frame_large_id();
    test_stream_frame_nonminimal_type();
    test_ack_frame_single_range();
    test_ack_frame_multiple_ranges();
    test_connection_close_transport();
    test_connection_close_app();
    test_reset_stream_roundtrip();
    test_transport_params_roundtrip();
    test_transport_params_defaults();
    test_ping_frame();
    test_handshake_done_frame();
    test_new_conn_id_valid();
    test_new_conn_id_rejects_zero_length();
    test_new_conn_id_rejects_retire_after_sequence();
    test_frame_allowed_encryption_levels();
    test_max_data_frame();

    printf("\n%d passed, %d failed (of %d)\n", tests_passed, tests_failed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
