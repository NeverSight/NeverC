/*
 * HTTP/3 Server Integration Tests
 * Tests control stream setup, SETTINGS, request parsing, and response encoding.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Pull in source directly for unit testing */
#include "../../../std/src/net/quic/quic_varint.c"
#include "../../../std/src/net/http3/http3_frame.c"
#include "../../../std/src/net/http3/http3_server.c"

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

/* Stub neverc_http_listen_and_serve_tls for linking */
int neverc_http_listen_and_serve_tls(const char *addr,
                                      neverc_http_mux_t *mux,
                                      const char *cert_file,
                                      const char *key_file) {
    (void)addr; (void)mux; (void)cert_file; (void)key_file;
    return 0;
}

/* ======================================================================
 * Server lifecycle tests
 * ====================================================================== */

static void test_server_create_destroy(void) {
    neverc_http3_server_t *srv = neverc_http3_server_create(NULL);
    ASSERT_NOT_NULL(srv);
    ASSERT_EQ(neverc_http3_server_max_streams(srv), 100);
    ASSERT_EQ(neverc_http3_server_is_running(srv), 0);
    neverc_http3_server_destroy(srv);
}

static void test_server_set_max_streams(void) {
    neverc_http3_server_t *srv = neverc_http3_server_create(NULL);
    neverc_http3_server_set_max_streams(srv, 200);
    ASSERT_EQ(neverc_http3_server_max_streams(srv), 200);
    neverc_http3_server_destroy(srv);
}

static void test_server_stop(void) {
    neverc_http3_server_t *srv = neverc_http3_server_create(NULL);
    srv->running = 1;
    ASSERT_EQ(neverc_http3_server_is_running(srv), 1);
    neverc_http3_server_stop(srv);
    ASSERT_EQ(neverc_http3_server_is_running(srv), 0);
    neverc_http3_server_destroy(srv);
}

/* ======================================================================
 * Connection state tests
 * ====================================================================== */

static void test_conn_init(void) {
    h3_conn_t conn;
    int rc = h3_conn_init(&conn, NULL);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(conn.encoder);
    ASSERT_NOT_NULL(conn.decoder);
    ASSERT_EQ(conn.local_settings.qpack_max_table_capacity, 4096);
    ASSERT_EQ(conn.peer_settings_received, 0);
    ASSERT_EQ(conn.goaway_sent, 0);
    h3_conn_cleanup(&conn);
}

/* ======================================================================
 * Control stream data tests
 * ====================================================================== */

static void test_control_stream_data(void) {
    h3_conn_t conn;
    h3_conn_init(&conn, NULL);

    uint8_t buf[512];
    size_t written;
    int rc = h3_build_control_stream_data(&conn, buf, sizeof(buf), &written);
    ASSERT_EQ(rc, 0);
    ASSERT_TRUE(written > 0);

    /* First byte should be stream type (varint 0x00 = control) */
    ASSERT_EQ(buf[0], H3_STREAM_TYPE_CONTROL);

    /* Then a SETTINGS frame */
    h3_frame_header_t hdr;
    rc = neverc_h3_parse_frame_header(buf + 1, written - 1, &hdr);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(hdr.type, (uint64_t)NC_H3_FRAME_SETTINGS);

    h3_conn_cleanup(&conn);
}

static void test_control_stream_settings_decode(void) {
    h3_conn_t conn;
    h3_conn_init(&conn, NULL);

    /* Build control stream data */
    uint8_t buf[512];
    size_t written;
    h3_build_control_stream_data(&conn, buf, sizeof(buf), &written);

    /* Parse the SETTINGS payload out of it */
    h3_frame_header_t hdr;
    neverc_h3_parse_frame_header(buf + 1, written - 1, &hdr);

    /* Decode SETTINGS */
    const uint8_t *payload = buf + 1 + hdr.header_size;
    h3_settings_t decoded;
    int rc = neverc_h3_settings_decode(payload, (size_t)hdr.length, &decoded);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(decoded.qpack_max_table_capacity, 4096);
    ASSERT_EQ(decoded.qpack_blocked_streams, 100);

    h3_conn_cleanup(&conn);
}

/* ======================================================================
 * Peer SETTINGS processing
 * ====================================================================== */

static void test_process_settings(void) {
    h3_conn_t conn;
    h3_conn_init(&conn, NULL);

    /* Encode some settings */
    h3_settings_t peer = { .qpack_max_table_capacity = 8192,
                           .max_field_section_size = 32 * 1024 * 1024,
                           .qpack_blocked_streams = 50 };
    uint8_t buf[256];
    size_t written;
    neverc_h3_settings_encode(&peer, buf, sizeof(buf), &written);

    /* Extract payload (skip frame header) */
    h3_frame_header_t hdr;
    neverc_h3_parse_frame_header(buf, written, &hdr);

    int rc = h3_process_settings(&conn, buf + hdr.header_size, (size_t)hdr.length);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(conn.peer_settings.qpack_max_table_capacity, 8192);
    ASSERT_EQ(conn.peer_settings.qpack_blocked_streams, 50);
    ASSERT_EQ(conn.peer_settings_received, 1);

    /* Duplicate SETTINGS should fail */
    rc = h3_process_settings(&conn, buf + hdr.header_size, (size_t)hdr.length);
    ASSERT_EQ(rc, -1);

    h3_conn_cleanup(&conn);
}

/* ======================================================================
 * GOAWAY frame
 * ====================================================================== */

static void test_goaway(void) {
    h3_conn_t conn;
    h3_conn_init(&conn, NULL);
    conn.last_stream_id = 12;

    uint8_t buf[64];
    size_t written;
    int rc = h3_send_goaway(&conn, buf, sizeof(buf), &written);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(conn.goaway_sent, 1);
    ASSERT_TRUE(written > 0);

    /* Parse it */
    h3_frame_header_t hdr;
    neverc_h3_parse_frame_header(buf, written, &hdr);
    ASSERT_EQ(hdr.type, (uint64_t)NC_H3_FRAME_GOAWAY);

    /* Sending again should fail */
    rc = h3_send_goaway(&conn, buf, sizeof(buf), &written);
    ASSERT_EQ(rc, -1);

    h3_conn_cleanup(&conn);
}

/* ======================================================================
 * Request header parsing
 * ====================================================================== */

static void test_parse_request_headers(void) {
    h3_conn_t conn;
    h3_conn_init(&conn, NULL);

    /* Encode a typical HTTP/3 request */
    neverc_qpack_header_t hdrs[] = {
        { (char *)":method", (char *)"GET" },
        { (char *)":path", (char *)"/api/users" },
        { (char *)":scheme", (char *)"https" },
        { (char *)":authority", (char *)"example.com" },
        { (char *)"accept", (char *)"application/json" },
        { (char *)"x-request-id", (char *)"req-42" },
    };

    uint8_t encoded[1024];
    size_t encoded_len;
    neverc_qpack_encode(conn.encoder, hdrs, 6, encoded, sizeof(encoded), &encoded_len);

    h3_request_t req;
    int rc = h3_parse_request_headers(&conn, encoded, encoded_len, &req);
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(req.method, "GET");
    ASSERT_STR_EQ(req.path, "/api/users");
    ASSERT_STR_EQ(req.scheme, "https");
    ASSERT_STR_EQ(req.authority, "example.com");
    ASSERT_EQ(req.nheaders, 2);
    ASSERT_STR_EQ(req.header_names[0], "accept");
    ASSERT_STR_EQ(req.header_values[0], "application/json");
    ASSERT_STR_EQ(req.header_names[1], "x-request-id");
    ASSERT_STR_EQ(req.header_values[1], "req-42");

    h3_request_cleanup(&req);
    h3_conn_cleanup(&conn);
}

static void test_parse_post_request(void) {
    h3_conn_t conn;
    h3_conn_init(&conn, NULL);

    neverc_qpack_header_t hdrs[] = {
        { (char *)":method", (char *)"POST" },
        { (char *)":path", (char *)"/api/data" },
        { (char *)":scheme", (char *)"https" },
        { (char *)":authority", (char *)"api.example.com" },
        { (char *)"content-type", (char *)"application/json" },
    };

    uint8_t encoded[1024];
    size_t encoded_len;
    neverc_qpack_encode(conn.encoder, hdrs, 5, encoded, sizeof(encoded), &encoded_len);

    h3_request_t req;
    int rc = h3_parse_request_headers(&conn, encoded, encoded_len, &req);
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(req.method, "POST");
    ASSERT_STR_EQ(req.path, "/api/data");
    ASSERT_EQ(req.nheaders, 1);
    ASSERT_STR_EQ(req.header_names[0], "content-type");
    ASSERT_STR_EQ(req.header_values[0], "application/json");

    h3_request_cleanup(&req);
    h3_conn_cleanup(&conn);
}

/* ======================================================================
 * Response encoding
 * ====================================================================== */

static void test_encode_response_200(void) {
    h3_conn_t conn;
    h3_conn_init(&conn, NULL);

    h3_response_t resp = {
        .status = 200,
        .header_names = { (char *)"content-type" },
        .header_values = { (char *)"text/plain" },
        .nheaders = 1,
        .body = (uint8_t *)"Hello, HTTP/3!",
        .body_len = 14,
    };

    uint8_t out[4096];
    size_t out_len;
    int rc = h3_encode_response(&conn, &resp, out, sizeof(out), &out_len);
    ASSERT_EQ(rc, 0);
    ASSERT_TRUE(out_len > 0);

    /* First frame should be HEADERS */
    h3_frame_header_t hdr;
    neverc_h3_parse_frame_header(out, out_len, &hdr);
    ASSERT_EQ(hdr.type, (uint64_t)NC_H3_FRAME_HEADERS);

    /* After HEADERS frame comes DATA frame */
    size_t offset = hdr.header_size + (size_t)hdr.length;
    h3_frame_header_t data_hdr;
    neverc_h3_parse_frame_header(out + offset, out_len - offset, &data_hdr);
    ASSERT_EQ(data_hdr.type, (uint64_t)NC_H3_FRAME_DATA);
    ASSERT_EQ(data_hdr.length, 14);

    /* Verify body content */
    size_t body_offset = offset + data_hdr.header_size;
    ASSERT_TRUE(memcmp(out + body_offset, "Hello, HTTP/3!", 14) == 0);

    h3_conn_cleanup(&conn);
}

static void test_encode_response_no_body(void) {
    h3_conn_t conn;
    h3_conn_init(&conn, NULL);

    h3_response_t resp = {
        .status = 204,
        .nheaders = 0,
        .body = NULL,
        .body_len = 0,
    };

    uint8_t out[4096];
    size_t out_len;
    int rc = h3_encode_response(&conn, &resp, out, sizeof(out), &out_len);
    ASSERT_EQ(rc, 0);

    /* Only HEADERS frame, no DATA frame */
    h3_frame_header_t hdr;
    neverc_h3_parse_frame_header(out, out_len, &hdr);
    ASSERT_EQ(hdr.type, (uint64_t)NC_H3_FRAME_HEADERS);
    ASSERT_EQ(out_len, hdr.header_size + (size_t)hdr.length);

    h3_conn_cleanup(&conn);
}

/* ======================================================================
 * Response header roundtrip (encode response → decode QPACK → verify)
 * ====================================================================== */

static void test_response_headers_roundtrip(void) {
    h3_conn_t conn;
    h3_conn_init(&conn, NULL);

    h3_response_t resp = {
        .status = 301,
        .header_names = { (char *)"location", (char *)"x-custom" },
        .header_values = { (char *)"/new-path", (char *)"value123" },
        .nheaders = 2,
        .body = NULL,
        .body_len = 0,
    };

    uint8_t out[4096];
    size_t out_len;
    h3_encode_response(&conn, &resp, out, sizeof(out), &out_len);

    /* Extract HEADERS frame payload */
    h3_frame_header_t hdr;
    neverc_h3_parse_frame_header(out, out_len, &hdr);
    const uint8_t *qpack_data = out + hdr.header_size;
    size_t qpack_len = (size_t)hdr.length;

    /* Decode QPACK */
    neverc_qpack_header_t decoded[16];
    int nheaders;
    int rc = neverc_qpack_decode(conn.decoder, qpack_data, qpack_len,
                                  decoded, 16, &nheaders);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(nheaders, 3); /* :status + location + x-custom */

    ASSERT_STR_EQ(decoded[0].name, ":status");
    ASSERT_STR_EQ(decoded[0].value, "301");
    ASSERT_STR_EQ(decoded[1].name, "location");
    ASSERT_STR_EQ(decoded[1].value, "/new-path");
    ASSERT_STR_EQ(decoded[2].name, "x-custom");
    ASSERT_STR_EQ(decoded[2].value, "value123");

    for (int i = 0; i < nheaders; i++) {
        free(decoded[i].name);
        free(decoded[i].value);
    }
    h3_conn_cleanup(&conn);
}

/* ======================================================================
 * Frame type validation
 * ====================================================================== */

static void test_valid_frame_types(void) {
    ASSERT_TRUE(h3_valid_request_frame(NC_H3_FRAME_DATA));
    ASSERT_TRUE(h3_valid_request_frame(NC_H3_FRAME_HEADERS));
    ASSERT_TRUE(!h3_valid_request_frame(NC_H3_FRAME_SETTINGS));
    ASSERT_TRUE(!h3_valid_request_frame(NC_H3_FRAME_GOAWAY));

    ASSERT_TRUE(h3_valid_control_frame(NC_H3_FRAME_SETTINGS));
    ASSERT_TRUE(h3_valid_control_frame(NC_H3_FRAME_GOAWAY));
    ASSERT_TRUE(h3_valid_control_frame(NC_H3_FRAME_MAX_PUSH_ID));
    ASSERT_TRUE(!h3_valid_control_frame(NC_H3_FRAME_DATA));
    ASSERT_TRUE(!h3_valid_control_frame(NC_H3_FRAME_HEADERS));
}

/* ======================================================================
 * main
 * ====================================================================== */

int main(void) {
    printf("HTTP/3 Server Integration test suite:\n\n");

    test_server_create_destroy();
    test_server_set_max_streams();
    test_server_stop();
    test_conn_init();
    test_control_stream_data();
    test_control_stream_settings_decode();
    test_process_settings();
    test_goaway();
    test_parse_request_headers();
    test_parse_post_request();
    test_encode_response_200();
    test_encode_response_no_body();
    test_response_headers_roundtrip();
    test_valid_frame_types();

    printf("\n%d passed, %d failed (of %d)\n", tests_passed, tests_failed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
