/*
 * QUIC Connection State Machine Tests
 * Tests connection lifecycle, stream management, flow control, and close.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#ifndef _WIN32
#include <errno.h>
#endif
#include "neverc/std/net/quic.h"

/* Pull in source directly for unit testing */
#include "../../../std/src/net/quic/quic_varint.c"
#include "../../../std/src/net/quic/quic_packet.c"
#include "../../../std/src/net/quic/quic_transport_params.c"
#include "../../../std/src/net/quic/quic_loss.c"

/*
 * This unit owns neither a UDP endpoint nor a TLS/transport engine.  Keep
 * those boundaries inert while exercising the real connection, flow-control,
 * transport-parameter, and loss-detection state.
 */
static char g_stub_tls_storage[1];

quic_tls_t *neverc_quic_tls_create(int is_server) {
    (void)is_server;
    return (quic_tls_t *)&g_stub_tls_storage;
}

void neverc_quic_tls_destroy(quic_tls_t *tls) { (void)tls; }

int neverc_quic_tls_set_initial_dcid(quic_tls_t *tls, const uint8_t *dcid,
                                     size_t dcid_len, uint32_t version) {
    (void)tls;
    (void)dcid;
    (void)dcid_len;
    (void)version;
    return 0;
}

int neverc_quic_tls_configure(quic_tls_t *tls,
                              const neverc_quic_config_t *config,
                              const char *server_name,
                              const quic_transport_params_t *local_params,
                              quic_transport_params_t *peer_params) {
    (void)tls;
    (void)config;
    (void)server_name;
    (void)local_params;
    if (peer_params)
        neverc_quic_transport_params_default(peer_params);
    return 0;
}

const char *neverc_quic_tls_error(const quic_tls_t *tls) {
    (void)tls;
    return "stub tls error";
}

int neverc_quic_tls_start(quic_tls_t *tls) {
    (void)tls;
    return 0;
}

void neverc_quic_tls_crypto_data_acked(quic_tls_t *tls, quic_enc_level_t level,
                                       uint64_t offset, size_t length) {
    (void)tls;
    (void)level;
    (void)offset;
    (void)length;
}

void neverc_quic_tls_crypto_data_lost(quic_tls_t *tls, quic_enc_level_t level,
                                    uint64_t offset) {
    (void)tls;
    (void)level;
    (void)offset;
}

void neverc_udp_close(neverc_udp_conn_t *conn) { (void)conn; }
static int g_flush_result;
int neverc_quic_conn_flush(struct neverc_quic_conn *conn) {
    (void)conn;
    return g_flush_result;
}

#include "../../../std/src/net/quic/quic_conn.c"

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

#define ASSERT_NULL(ptr) do { \
    tests_run++; \
    if ((ptr) == NULL) { tests_passed++; } \
    else { tests_failed++; printf("  FAIL %s:%d: %s is not NULL\n", __func__, __LINE__, #ptr); } \
} while(0)

/* ======================================================================
 * Connection creation tests
 * ====================================================================== */

static void test_conn_create_client(void) {
    struct neverc_quic_conn *conn = neverc_quic_conn_create(QUIC_SIDE_CLIENT, -1);
    ASSERT_NOT_NULL(conn);
    ASSERT_EQ(conn->state, QUIC_CONN_IDLE);
    ASSERT_EQ(conn->side, QUIC_SIDE_CLIENT);
    ASSERT_EQ(conn->next_bidi_stream_id, 0);
    ASSERT_EQ(conn->next_uni_stream_id, 2);
    ASSERT_EQ(conn->n_local_cids, 1);
    ASSERT_TRUE(conn->local_cids[0].len == 8);
    neverc_quic_conn_destroy(conn);
}

static void test_conn_create_server(void) {
    struct neverc_quic_conn *conn = neverc_quic_conn_create(QUIC_SIDE_SERVER, -1);
    ASSERT_NOT_NULL(conn);
    ASSERT_EQ(conn->state, QUIC_CONN_IDLE);
    ASSERT_EQ(conn->side, QUIC_SIDE_SERVER);
    ASSERT_EQ(conn->next_bidi_stream_id, 1);
    ASSERT_EQ(conn->next_uni_stream_id, 3);
    neverc_quic_conn_destroy(conn);
}

static void test_conn_defaults(void) {
    struct neverc_quic_conn *conn = neverc_quic_conn_create(QUIC_SIDE_CLIENT, -1);
    ASSERT_NOT_NULL(conn);
    ASSERT_EQ(conn->idle_timeout_ms, 30000);
    ASSERT_EQ(conn->peer_max_streams_bidi, 100);
    ASSERT_EQ(conn->peer_max_streams_uni, 100);
    ASSERT_EQ(conn->flow.max_data_local, 10 * 1024 * 1024);
    ASSERT_EQ(conn->flow.max_data_peer, 10 * 1024 * 1024);
    neverc_quic_conn_destroy(conn);
}

/* ======================================================================
 * Connection ID tests
 * ====================================================================== */

static void test_conn_id_generation(void) {
    struct neverc_quic_conn *conn = neverc_quic_conn_create(QUIC_SIDE_CLIENT, -1);
    ASSERT_NOT_NULL(conn);
    ASSERT_EQ(conn->n_local_cids, 1);
    ASSERT_EQ(conn->local_cids[0].sequence, 0);
    ASSERT_EQ(conn->local_cids[0].retired, 0);

    conn_add_local_cid(conn);
    ASSERT_EQ(conn->n_local_cids, 2);
    ASSERT_EQ(conn->local_cids[1].sequence, 1);

    conn_add_local_cid(conn);
    ASSERT_EQ(conn->n_local_cids, 3);
    ASSERT_EQ(conn->local_cids[2].sequence, 2);

    neverc_quic_conn_destroy(conn);
}

static void test_conn_id_max_limit(void) {
    struct neverc_quic_conn *conn = neverc_quic_conn_create(QUIC_SIDE_CLIENT, -1);
    ASSERT_NOT_NULL(conn);

    for (int i = 1; i < QUIC_MAX_LOCAL_CONN_IDS; i++)
        conn_add_local_cid(conn);
    ASSERT_EQ(conn->n_local_cids, QUIC_MAX_LOCAL_CONN_IDS);

    int rc = conn_add_local_cid(conn);
    ASSERT_EQ(rc, -1);
    ASSERT_EQ(conn->n_local_cids, QUIC_MAX_LOCAL_CONN_IDS);

    neverc_quic_conn_destroy(conn);
}

/* ======================================================================
 * Stream creation tests
 * ====================================================================== */

static void test_stream_open_bidi_client(void) {
    struct neverc_quic_conn *conn = neverc_quic_conn_create(QUIC_SIDE_CLIENT, -1);
    conn->state = QUIC_CONN_ESTABLISHED;

    quic_stream_t *s1 = neverc_quic_conn_open_stream(conn);
    ASSERT_NOT_NULL(s1);
    ASSERT_EQ(s1->id, 0);   /* client bidi: 0, 4, 8, ... */

    quic_stream_t *s2 = neverc_quic_conn_open_stream(conn);
    ASSERT_NOT_NULL(s2);
    ASSERT_EQ(s2->id, 4);

    quic_stream_t *s3 = neverc_quic_conn_open_stream(conn);
    ASSERT_NOT_NULL(s3);
    ASSERT_EQ(s3->id, 8);

    ASSERT_EQ(conn->n_streams, 3);
    neverc_quic_conn_destroy(conn);
}

static void test_stream_open_uni_server(void) {
    struct neverc_quic_conn *conn = neverc_quic_conn_create(QUIC_SIDE_SERVER, -1);
    conn->state = QUIC_CONN_ESTABLISHED;

    quic_stream_t *s1 = neverc_quic_conn_open_uni_stream(conn);
    ASSERT_NOT_NULL(s1);
    ASSERT_EQ(s1->id, 3);   /* server uni: 3, 7, 11, ... */

    quic_stream_t *s2 = neverc_quic_conn_open_uni_stream(conn);
    ASSERT_NOT_NULL(s2);
    ASSERT_EQ(s2->id, 7);

    neverc_quic_conn_destroy(conn);
}

static void test_stream_open_requires_established(void) {
    struct neverc_quic_conn *conn = neverc_quic_conn_create(QUIC_SIDE_CLIENT, -1);
    ASSERT_EQ(conn->state, QUIC_CONN_IDLE);

    quic_stream_t *s = neverc_quic_conn_open_stream(conn);
    ASSERT_NULL(s);

    conn->state = QUIC_CONN_DRAINING;
    s = neverc_quic_conn_open_stream(conn);
    ASSERT_NULL(s);

    neverc_quic_conn_destroy(conn);
}

static void test_stream_is_local(void) {
    struct neverc_quic_conn *conn = neverc_quic_conn_create(QUIC_SIDE_CLIENT, -1);
    ASSERT_TRUE(stream_is_local(conn, 0));   /* client bidi */
    ASSERT_TRUE(stream_is_local(conn, 4));   /* client bidi */
    ASSERT_TRUE(!stream_is_local(conn, 1));  /* server bidi */
    ASSERT_TRUE(!stream_is_local(conn, 3));  /* server uni */
    ASSERT_TRUE(stream_is_local(conn, 2));   /* client uni */
    neverc_quic_conn_destroy(conn);
}

/* ======================================================================
 * Stream data tests
 * ====================================================================== */

static void test_stream_write_read(void) {
    struct neverc_quic_conn *conn = neverc_quic_conn_create(QUIC_SIDE_CLIENT, -1);
    conn->state = QUIC_CONN_ESTABLISHED;

    quic_stream_t *s = neverc_quic_conn_open_stream(conn);
    ASSERT_NOT_NULL(s);

    const char *msg = "Hello QUIC!";
    int written = neverc_quic_stream_write_data(s, msg, strlen(msg));
    ASSERT_EQ(written, (int)strlen(msg));
    ASSERT_EQ(s->send_len, strlen(msg));

    /* Feed the reply through the real receive/reassembly path. */
    const char *reply = "OK";
    quic_frame_stream_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.stream_id = s->id;
    frame.data = (const uint8_t *)reply;
    frame.data_len = 2;
    ASSERT_EQ(neverc_quic_stream_receive(conn, &frame), 0);

    char buf[64] = {0};
    int nread = neverc_quic_stream_read_data(s, buf, sizeof(buf));
    ASSERT_EQ(nread, 2);
    ASSERT_TRUE(memcmp(buf, "OK", 2) == 0);
    ASSERT_EQ(s->recv_len, 0);

    neverc_quic_conn_destroy(conn);
}

static void test_stream_write_ignores_flush_failure(void) {
    struct neverc_quic_conn *conn = neverc_quic_conn_create(QUIC_SIDE_CLIENT, -1);
    conn->state = QUIC_CONN_ESTABLISHED;

    quic_stream_t *s = neverc_quic_conn_open_stream(conn);
    ASSERT_NOT_NULL(s);

    g_flush_result = -1;
    const char *msg = "queued anyway";
    int written = neverc_quic_stream_write_data(s, msg, strlen(msg));
    g_flush_result = 0;
    ASSERT_EQ(written, (int)strlen(msg));
    ASSERT_EQ(s->send_len, strlen(msg));
    ASSERT_TRUE(memcmp(s->send_buf, msg, strlen(msg)) == 0);

    neverc_quic_conn_destroy(conn);
}

static void test_stream_write_grow_buffer(void) {
    struct neverc_quic_conn *conn = neverc_quic_conn_create(QUIC_SIDE_CLIENT, -1);
    conn->state = QUIC_CONN_ESTABLISHED;

    quic_stream_t *s = neverc_quic_conn_open_stream(conn);
    ASSERT_NOT_NULL(s);
    size_t initial_cap = s->send_buf_cap;

    /* Write more than buffer capacity */
    uint8_t *big = (uint8_t *)malloc(initial_cap + 100);
    memset(big, 'A', initial_cap + 100);
    int written = neverc_quic_stream_write_data(s, big, initial_cap + 100);
    ASSERT_EQ(written, (int)(initial_cap + 100));
    ASSERT_TRUE(s->send_buf_cap > initial_cap);
    free(big);

    neverc_quic_conn_destroy(conn);
}

static void test_stream_read_fin(void) {
    struct neverc_quic_conn *conn = neverc_quic_conn_create(QUIC_SIDE_CLIENT, -1);
    conn->state = QUIC_CONN_ESTABLISHED;

    quic_stream_t *s = neverc_quic_conn_open_stream(conn);
    s->recv_fin = 1;
    s->recv_len = 0;

    char buf[64];
    int nread = neverc_quic_stream_read_data(s, buf, sizeof(buf));
    ASSERT_EQ(nread, 0); /* EOF */

    neverc_quic_conn_destroy(conn);
}

static void test_stream_zero_flow_control_limit(void) {
    struct neverc_quic_conn *conn = neverc_quic_conn_create(QUIC_SIDE_CLIENT, -1);
    conn->state = QUIC_CONN_ESTABLISHED;
    conn->peer_params.initial_max_stream_data_bidi_remote = 0;
    conn->local_params.initial_max_stream_data_bidi_local = 0;

    quic_stream_t *stream = neverc_quic_conn_open_stream(conn);
    ASSERT_NOT_NULL(stream);
    ASSERT_EQ(stream->send_max_data, 0);
    ASSERT_EQ(stream->recv_max_data, 0);

    uint8_t byte = 'x';
    quic_frame_stream_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.stream_id = stream->id;
    frame.data = &byte;
    frame.data_len = 1;
    ASSERT_EQ(neverc_quic_stream_receive(conn, &frame), -1);
    ASSERT_EQ(conn->state, QUIC_CONN_DRAINING);
    ASSERT_EQ(conn->close_error_code, QUIC_ERR_FLOW_CONTROL_ERROR);
    neverc_quic_conn_destroy(conn);

    conn = neverc_quic_conn_create(QUIC_SIDE_CLIENT, -1);
    conn->state = QUIC_CONN_ESTABLISHED;
    conn->peer_params.initial_max_stream_data_bidi_remote = 0;
    conn->local_params.initial_max_stream_data_bidi_local = 0;
    stream = neverc_quic_conn_open_stream(conn);
    ASSERT_NOT_NULL(stream);
    frame.data_len = 0;
    frame.fin = 1;
    ASSERT_EQ(neverc_quic_stream_receive(conn, &frame), 0);

    neverc_quic_conn_destroy(conn);
}

static void test_stream_write_after_close(void) {
    struct neverc_quic_conn *conn = neverc_quic_conn_create(QUIC_SIDE_CLIENT, -1);
    conn->state = QUIC_CONN_ESTABLISHED;

    quic_stream_t *s = neverc_quic_conn_open_stream(conn);
    neverc_quic_stream_close_write_side(s);
    ASSERT_EQ(s->state, QUIC_STREAM_HALF_CLOSED_LOCAL);

    int written = neverc_quic_stream_write_data(s, "x", 1);
    ASSERT_EQ(written, -1);

    neverc_quic_conn_destroy(conn);
}

/* ======================================================================
 * Connection close tests
 * ====================================================================== */

static void test_conn_close(void) {
    struct neverc_quic_conn *conn = neverc_quic_conn_create(QUIC_SIDE_CLIENT, -1);
    conn->state = QUIC_CONN_ESTABLISHED;

    neverc_quic_conn_close_internal(conn, 0x42, "testing", 1);
    ASSERT_EQ(conn->state, QUIC_CONN_DRAINING);
    ASSERT_EQ(conn->close_error_code, 0x42);
    ASSERT_EQ(conn->close_is_app, 1);
    ASSERT_TRUE(strcmp(conn->close_reason, "testing") == 0);

    neverc_quic_conn_destroy(conn);
}

static void test_conn_close_wakes_streams(void) {
    struct neverc_quic_conn *conn = neverc_quic_conn_create(QUIC_SIDE_CLIENT, -1);
    conn->state = QUIC_CONN_ESTABLISHED;

    quic_stream_t *s = neverc_quic_conn_open_stream(conn);
    ASSERT_EQ(s->state, QUIC_STREAM_OPEN);

    neverc_quic_conn_close_internal(conn, 0, "done", 0);
    ASSERT_EQ(s->state, QUIC_STREAM_HALF_CLOSED_REMOTE);

    neverc_quic_conn_destroy(conn);
}

static void test_conn_close_idempotent(void) {
    struct neverc_quic_conn *conn = neverc_quic_conn_create(QUIC_SIDE_CLIENT, -1);
    conn->state = QUIC_CONN_ESTABLISHED;

    neverc_quic_conn_close_internal(conn, 1, "first", 0);
    ASSERT_EQ(conn->state, QUIC_CONN_DRAINING);
    ASSERT_EQ(conn->close_error_code, 1);

    neverc_quic_conn_close_internal(conn, 999, "second", 1);
    ASSERT_EQ(conn->close_error_code, 1); /* unchanged */

    neverc_quic_conn_destroy(conn);
}

/* ======================================================================
 * Connection queries
 * ====================================================================== */

static void test_conn_is_alive(void) {
    struct neverc_quic_conn *conn = neverc_quic_conn_create(QUIC_SIDE_CLIENT, -1);

    conn->state = QUIC_CONN_IDLE;
    ASSERT_EQ(neverc_quic_conn_is_alive_check(conn), 0);

    conn->state = QUIC_CONN_HANDSHAKING;
    ASSERT_EQ(neverc_quic_conn_is_alive_check(conn), 1);

    conn->state = QUIC_CONN_ESTABLISHED;
    ASSERT_EQ(neverc_quic_conn_is_alive_check(conn), 1);

    conn->state = QUIC_CONN_DRAINING;
    ASSERT_EQ(neverc_quic_conn_is_alive_check(conn), 0);
    ASSERT_EQ(quic_conn_allows_stream_read(conn), 1);

    conn->state = QUIC_CONN_CLOSED;
    ASSERT_EQ(neverc_quic_conn_is_alive_check(conn), 0);
    ASSERT_EQ(quic_conn_allows_stream_read(conn), 0);

    neverc_quic_conn_destroy(conn);
}

static void seed_idle_loss_history(struct neverc_quic_conn *conn) {
    neverc_quic_loss_on_sent(&conn->loss, QUIC_PNS_APPLICATION, 0, 1000,
                             1200, 1);
    neverc_quic_loss_mark_acked(&conn->loss, QUIC_PNS_APPLICATION, 0, 1100);
    neverc_quic_loss_on_ack(&conn->loss, QUIC_PNS_APPLICATION, 0, 0, 1100);
    neverc_quic_loss_cleanup(&conn->loss, QUIC_PNS_APPLICATION);
}

static void test_conn_loss_timeout_disarmed_after_address_validation(void) {
    struct neverc_quic_conn *conn =
        neverc_quic_conn_create(QUIC_SIDE_SERVER, -1);
    conn->peer_completed_address_validation = 1;
    conn->handshake_confirmed = 1;
    seed_idle_loss_history(conn);

    ASSERT_EQ(neverc_quic_loss_get_timeout(&conn->loss, 1), 0);
    ASSERT_EQ(neverc_quic_conn_loss_timeout(conn, 2000), 0);

    neverc_quic_conn_destroy(conn);
}

static void test_conn_loss_timeout_fresh_before_peer_validation(void) {
    struct neverc_quic_conn *conn =
        neverc_quic_conn_create(QUIC_SIDE_CLIENT, -1);
    conn->peer_completed_address_validation = 0;
    seed_idle_loss_history(conn);

    uint64_t expected = 2000 + neverc_quic_pto(&conn->loss.rtt, 0);
    ASSERT_EQ(neverc_quic_conn_loss_timeout(conn, 2000), expected);
    ASSERT_EQ(neverc_quic_conn_loss_timeout(conn, 2100), expected);

    neverc_quic_loss_on_sent(&conn->loss, QUIC_PNS_HANDSHAKE, 1, 2150,
                             1200, 1);
    ASSERT_TRUE(neverc_quic_conn_loss_timeout(conn, 2150) > 0);
    neverc_quic_loss_mark_acked(&conn->loss, QUIC_PNS_HANDSHAKE, 1, 2160);
    neverc_quic_loss_on_ack(&conn->loss, QUIC_PNS_HANDSHAKE, 1, 0, 2160);
    neverc_quic_loss_cleanup(&conn->loss, QUIC_PNS_HANDSHAKE);
    uint64_t reentered = 2200 + neverc_quic_pto(&conn->loss.rtt, 0);
    ASSERT_EQ(neverc_quic_conn_loss_timeout(conn, 2200), reentered);
    ASSERT_TRUE(reentered != expected);

    neverc_quic_conn_destroy(conn);
}

static void test_conn_loss_timeout_cancelled_at_amplification_limit(void) {
    struct neverc_quic_conn *conn =
        neverc_quic_conn_create(QUIC_SIDE_SERVER, -1);
    conn->address_validated = 0;
    conn->bytes_received_before_validation = 100;
    conn->bytes_sent_before_validation = 300;
    neverc_quic_loss_on_sent(&conn->loss, QUIC_PNS_INITIAL, 0, 1000,
                             1200, 1);

    ASSERT_EQ(neverc_quic_conn_loss_timeout(conn, 2000), 0);

    neverc_quic_conn_destroy(conn);
}

static void test_conn_alpn(void) {
    struct neverc_quic_conn *conn = neverc_quic_conn_create(QUIC_SIDE_CLIENT, -1);

    ASSERT_NULL(neverc_quic_conn_get_alpn(conn));

    strcpy(conn->alpn, "h3");
    const char *alpn = neverc_quic_conn_get_alpn(conn);
    ASSERT_NOT_NULL(alpn);
    ASSERT_TRUE(strcmp(alpn, "h3") == 0);

    neverc_quic_conn_destroy(conn);
}

static void test_stream_get_id(void) {
    struct neverc_quic_conn *conn = neverc_quic_conn_create(QUIC_SIDE_CLIENT, -1);
    conn->state = QUIC_CONN_ESTABLISHED;

    quic_stream_t *s = neverc_quic_conn_open_stream(conn);
    ASSERT_EQ(neverc_quic_stream_get_id(s), 0);
    ASSERT_EQ(neverc_quic_stream_get_id(NULL), UINT64_MAX);

    neverc_quic_conn_destroy(conn);
}

static void test_transport_params_absent_flow_control_is_zero(void) {
    quic_transport_params_t tp;
    ASSERT_EQ(neverc_quic_transport_params_decode((const uint8_t *)"", 0, &tp),
              0);
    ASSERT_EQ(tp.initial_max_data, 0);
    ASSERT_EQ(tp.initial_max_stream_data_bidi_local, 0);
    ASSERT_EQ(tp.initial_max_stream_data_bidi_remote, 0);
    ASSERT_EQ(tp.initial_max_stream_data_uni, 0);
    ASSERT_EQ(tp.initial_max_streams_bidi, 0);
    ASSERT_EQ(tp.initial_max_streams_uni, 0);
    ASSERT_EQ(tp.max_idle_timeout, 0);
    ASSERT_EQ(tp.max_udp_payload_size, 65527);
    ASSERT_EQ(tp.ack_delay_exponent, 3);
    ASSERT_EQ(tp.max_ack_delay, 25);
    ASSERT_EQ(tp.active_connection_id_limit, 2);
    ASSERT_EQ(tp.max_datagram_frame_size, 0);
}

static void test_transport_params_partial_does_not_invent_limits(void) {
    uint8_t buf[16];
    size_t pos = 0, written = 0;
    size_t value_len = neverc_quic_varint_len(100);
    ASSERT_EQ(neverc_quic_varint_encode(0x04, buf + pos, sizeof(buf) - pos,
                                        &written), 0);
    pos += written;
    ASSERT_EQ(neverc_quic_varint_encode(value_len, buf + pos,
                                        sizeof(buf) - pos, &written), 0);
    pos += written;
    ASSERT_EQ(neverc_quic_varint_encode(100, buf + pos, sizeof(buf) - pos,
                                        &written), 0);
    pos += written;

    quic_transport_params_t tp;
    ASSERT_EQ(neverc_quic_transport_params_decode(buf, pos, &tp), 0);
    ASSERT_EQ(tp.initial_max_data, 100);
    ASSERT_EQ(tp.initial_max_streams_bidi, 0);
    ASSERT_EQ(tp.initial_max_stream_data_bidi_remote, 0);
    ASSERT_EQ(tp.initial_max_stream_data_uni, 0);
}

static void test_effective_idle_timeout(void) {
    ASSERT_EQ(neverc_quic_effective_idle_timeout_ms(30000, 0), 30000);
    ASSERT_EQ(neverc_quic_effective_idle_timeout_ms(30000, 5000), 5000);
    ASSERT_EQ(neverc_quic_effective_idle_timeout_ms(30000, 60000), 30000);
    ASSERT_EQ(neverc_quic_effective_idle_timeout_ms(0, 5000), 5000);
    ASSERT_EQ(neverc_quic_effective_idle_timeout_ms(0, 0), 0);
}

static void test_apply_peer_transport_params_copies_max_ack_delay(void) {
    /* RFC 9002: PTO and ACK-delay cap use the peer's advertised max_ack_delay. */
    struct neverc_quic_conn *conn =
        neverc_quic_conn_create(QUIC_SIDE_CLIENT, -1);
    ASSERT_NOT_NULL(conn);
    ASSERT_EQ(conn->loss.rtt.max_ack_delay, 25);

    conn->peer_params.max_ack_delay = 100;
    conn->peer_params.initial_max_data = 123;
    conn->peer_params.initial_max_streams_bidi = 7;
    conn->peer_params.initial_max_streams_uni = 3;
    conn->peer_params.max_idle_timeout = 5000;
    conn->peer_params.disable_active_migration = 1;
    neverc_quic_conn_apply_peer_transport_params(conn);
    ASSERT_EQ(conn->loss.rtt.max_ack_delay, 100);
    ASSERT_EQ(conn->flow.max_data_peer, 123);
    ASSERT_EQ(conn->peer_max_streams_bidi, 7);
    ASSERT_EQ(conn->peer_max_streams_uni, 3);
    ASSERT_EQ(conn->idle_timeout_ms, 5000);
    ASSERT_EQ(conn->peer_disable_migration, 1);

    conn->peer_params.max_ack_delay = 0;
    neverc_quic_conn_apply_peer_transport_params(conn);
    ASSERT_EQ(conn->loss.rtt.max_ack_delay, 0);
    neverc_quic_conn_destroy(conn);
}

static void test_stream_receive_opens_lower_ids(void) {
    struct neverc_quic_conn *conn =
        neverc_quic_conn_create(QUIC_SIDE_SERVER, -1);
    conn->state = QUIC_CONN_ESTABLISHED;
    uint8_t byte = 'x';
    quic_frame_stream_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.stream_id = 8;
    frame.data = &byte;
    frame.data_len = 1;
    ASSERT_EQ(neverc_quic_stream_receive(conn, &frame), 0);
    ASSERT_EQ(conn->n_streams, 3);
    ASSERT_NOT_NULL(neverc_quic_conn_find_stream(conn, 0));
    ASSERT_NOT_NULL(neverc_quic_conn_find_stream(conn, 4));
    ASSERT_NOT_NULL(neverc_quic_conn_find_stream(conn, 8));
    ASSERT_EQ(conn->opened_peer_streams_bidi, 3);
    ASSERT_EQ(neverc_quic_conn_find_stream(conn, 0)->peer_initiated, 1);
    ASSERT_EQ(neverc_quic_conn_find_stream(conn, 8)->recv_len, 1);

    neverc_quic_conn_destroy(conn);
}

static void test_stream_receive_opens_lower_uni_ids(void) {
    struct neverc_quic_conn *conn =
        neverc_quic_conn_create(QUIC_SIDE_SERVER, -1);
    conn->state = QUIC_CONN_ESTABLISHED;
    uint8_t byte = 'u';
    quic_frame_stream_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.stream_id = 10;
    frame.data = &byte;
    frame.data_len = 1;
    ASSERT_EQ(neverc_quic_stream_receive(conn, &frame), 0);
    ASSERT_EQ(conn->n_streams, 3);
    ASSERT_NOT_NULL(neverc_quic_conn_find_stream(conn, 2));
    ASSERT_NOT_NULL(neverc_quic_conn_find_stream(conn, 6));
    ASSERT_NOT_NULL(neverc_quic_conn_find_stream(conn, 10));
    ASSERT_EQ(conn->opened_peer_streams_uni, 3);

    neverc_quic_conn_destroy(conn);
}

static void test_stream_receive_gap_respects_limit(void) {
    struct neverc_quic_conn *conn =
        neverc_quic_conn_create(QUIC_SIDE_SERVER, -1);
    conn->state = QUIC_CONN_ESTABLISHED;
    conn->local_params.initial_max_streams_bidi = 2;
    uint8_t byte = 'x';
    quic_frame_stream_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.stream_id = 8;
    frame.data = &byte;
    frame.data_len = 1;
    ASSERT_EQ(neverc_quic_stream_receive(conn, &frame), -1);
    ASSERT_EQ(conn->n_streams, 0);
    ASSERT_EQ(conn->state, QUIC_CONN_DRAINING);
    ASSERT_EQ(conn->close_error_code, QUIC_ERR_STREAM_LIMIT_ERROR);

    neverc_quic_conn_destroy(conn);
}

static void test_max_stream_data_creates_peer_bidi(void) {
    struct neverc_quic_conn *conn =
        neverc_quic_conn_create(QUIC_SIDE_SERVER, -1);
    conn->state = QUIC_CONN_ESTABLISHED;
    /* RFC 9000 §19.10: MAX_STREAM_DATA only raises the send window. */
    conn->peer_params.initial_max_stream_data_bidi_local = 0;
    ASSERT_EQ(neverc_quic_stream_apply_max_stream_data(conn, 8, 4096), 0);
    ASSERT_EQ(conn->n_streams, 3);
    quic_stream_t *stream = neverc_quic_conn_find_stream(conn, 8);
    ASSERT_NOT_NULL(stream);
    ASSERT_EQ(stream->send_max_data, 4096);
    ASSERT_NOT_NULL(neverc_quic_conn_find_stream(conn, 0));
    ASSERT_EQ(neverc_quic_stream_apply_max_stream_data(conn, 2, 4096), -1);
    ASSERT_EQ(conn->state, QUIC_CONN_DRAINING);
    ASSERT_EQ(conn->close_error_code, QUIC_ERR_STREAM_STATE_ERROR);
    ASSERT_EQ(neverc_quic_stream_apply_max_stream_data(conn, 1, 4096), -1);

    neverc_quic_conn_destroy(conn);
}

static void test_stop_sending_creates_peer_bidi(void) {
    struct neverc_quic_conn *conn =
        neverc_quic_conn_create(QUIC_SIDE_SERVER, -1);
    conn->state = QUIC_CONN_ESTABLISHED;
    ASSERT_EQ(neverc_quic_stream_apply_stop_sending(conn, 8, 0x01), 0);
    quic_stream_t *stream = neverc_quic_conn_find_stream(conn, 8);
    ASSERT_NOT_NULL(stream);
    ASSERT_EQ(stream->reset_pending, 1);
    ASSERT_EQ(stream->reset_error_code, 0x01);
    ASSERT_EQ(conn->n_streams, 3);
    ASSERT_EQ(neverc_quic_stream_apply_stop_sending(conn, 2, 0x01), -1);
    ASSERT_EQ(conn->state, QUIC_CONN_DRAINING);
    ASSERT_EQ(conn->close_error_code, QUIC_ERR_STREAM_STATE_ERROR);
    ASSERT_EQ(neverc_quic_stream_apply_stop_sending(conn, 1, 0x01), -1);

    neverc_quic_conn_destroy(conn);
}

static void test_decode_packet_number_wrap_and_limit(void) {
    ASSERT_EQ(neverc_quic_decode_packet_number(250, 5, 8), 261);
    ASSERT_EQ(neverc_quic_decode_packet_number(0, 1, 8), 1);
    uint64_t decoded = neverc_quic_decode_packet_number(
        (UINT64_C(1) << 62) - 1, 0, 32);
    ASSERT_TRUE(decoded > ((UINT64_C(1) << 62) - 1));
}

static int write_parseable_long_header(uint32_t version,
                                       quic_packet_type_t type,
                                       uint8_t *buf, size_t cap,
                                       size_t *packet_len) {
    quic_packet_header_t header;
    memset(&header, 0, sizeof(header));
    header.type = type;
    header.version = version;
    header.dcid.len = 8;
    memset(header.dcid.data, 0x11, 8);
    header.scid.len = 8;
    memset(header.scid.data, 0x22, 8);
    header.pkt_number = 1;
    header.pkt_number_len = 1;
    header.payload_len = 20;
    size_t header_len = 0;
    if (neverc_quic_write_long_header(buf, cap, &header, &header_len) != 0)
        return -1;
    if (header_len + 20 > cap) return -1;
    memset(buf + header_len, 0, 20);
    *packet_len = header_len + 20;
    return 0;
}

static void test_v1_long_header_type_bits(void) {
    uint8_t buf[128];
    size_t packet_len = 0;
    ASSERT_EQ(write_parseable_long_header(NEVERC_QUIC_VERSION_1,
                                          QUIC_PKT_INITIAL, buf,
                                          sizeof(buf), &packet_len), 0);
    /* RFC 9000: Initial long-header type bits are 0b00. */
    ASSERT_EQ((buf[0] >> 4) & 3, 0);

    quic_packet_header_t parsed;
    ASSERT_EQ(neverc_quic_parse_packet_header(buf, packet_len, &parsed, 8),
              0);
    ASSERT_EQ(parsed.type, QUIC_PKT_INITIAL);
    ASSERT_EQ(parsed.version, NEVERC_QUIC_VERSION_1);
}

static void test_client_drops_server_initial_token(void) {
    /* RFC 9000 §17.2.2: Token Length on a server Initial must be 0. */
    uint8_t token[4] = {0xde, 0xad, 0xbe, 0xef};
    uint8_t buf[128];
    quic_packet_header_t header;
    size_t header_len = 0;
    size_t packet_len;
    quic_packet_header_t parsed;

    memset(&header, 0, sizeof(header));
    header.type = QUIC_PKT_INITIAL;
    header.version = NEVERC_QUIC_VERSION_1;
    header.dcid.len = 8;
    memset(header.dcid.data, 0x11, 8);
    header.scid.len = 8;
    memset(header.scid.data, 0x22, 8);
    header.pkt_number = 1;
    header.pkt_number_len = 1;
    header.payload_len = 20;
    header.token = token;
    header.token_len = sizeof(token);
    ASSERT_EQ(neverc_quic_write_long_header(buf, sizeof(buf), &header,
                                            &header_len), 0);
    memset(buf + header_len, 0, 20);
    packet_len = header_len + 20;
    ASSERT_EQ(neverc_quic_parse_packet_header(buf, packet_len, &parsed, 8),
              0);
    ASSERT_EQ(parsed.type, QUIC_PKT_INITIAL);
    ASSERT_EQ(parsed.token_len, sizeof(token));
    ASSERT_EQ(neverc_quic_client_must_drop_initial_token(1, &parsed), 1);
    ASSERT_EQ(neverc_quic_client_must_drop_initial_token(0, &parsed), 0);
    ASSERT_EQ(neverc_quic_server_must_reject_unissued_token(1, &parsed), 1);
    ASSERT_EQ(neverc_quic_server_must_reject_unissued_token(0, &parsed), 0);

    ASSERT_EQ(write_parseable_long_header(NEVERC_QUIC_VERSION_1,
                                          QUIC_PKT_INITIAL, buf,
                                          sizeof(buf), &packet_len), 0);
    ASSERT_EQ(neverc_quic_parse_packet_header(buf, packet_len, &parsed, 8),
              0);
    ASSERT_EQ(parsed.token_len, 0);
    ASSERT_EQ(neverc_quic_client_must_drop_initial_token(1, &parsed), 0);
    ASSERT_EQ(neverc_quic_server_must_reject_unissued_token(1, &parsed), 0);
}

static void test_v2_long_header_type_bits(void) {
    /* RFC 9369 §3.2: v2 remaps long-header types so v1 Initial bits
     * (0b00) are Retry and v2 Initial is 0b01. */
    static const struct {
        quic_packet_type_t type;
        unsigned bits;
    } cases[] = {
        { QUIC_PKT_RETRY, 0 },
        { QUIC_PKT_INITIAL, 1 },
        { QUIC_PKT_0RTT, 2 },
        { QUIC_PKT_HANDSHAKE, 3 },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        uint8_t buf[128];
        size_t packet_len = 0;
        if (cases[i].type == QUIC_PKT_RETRY) {
            quic_packet_header_t header;
            memset(&header, 0, sizeof(header));
            header.type = QUIC_PKT_RETRY;
            header.version = NEVERC_QUIC_VERSION_2;
            header.dcid.len = 8;
            memset(header.dcid.data, 0x11, 8);
            header.scid.len = 8;
            memset(header.scid.data, 0x22, 8);
            header.pkt_number_len = 1;
            size_t written = 0;
            ASSERT_EQ(neverc_quic_write_long_header(buf, sizeof(buf),
                                                    &header, &written), 0);
            ASSERT_EQ((buf[0] >> 4) & 3, cases[i].bits);
            continue;
        }
        ASSERT_EQ(write_parseable_long_header(NEVERC_QUIC_VERSION_2,
                                              cases[i].type, buf,
                                              sizeof(buf), &packet_len),
                  0);
        ASSERT_EQ((buf[0] >> 4) & 3, cases[i].bits);

        quic_packet_header_t parsed;
        ASSERT_EQ(neverc_quic_parse_packet_header(buf, packet_len,
                                                  &parsed, 8), 0);
        ASSERT_EQ(parsed.type, cases[i].type);
        ASSERT_EQ(parsed.version, NEVERC_QUIC_VERSION_2);
    }
}

static void test_conn_defaults_to_quic_v1(void) {
    struct neverc_quic_conn *conn =
        neverc_quic_conn_create(QUIC_SIDE_SERVER, -1);
    ASSERT_NOT_NULL(conn);
    ASSERT_EQ(conn->version, NEVERC_QUIC_VERSION_1);
    neverc_quic_conn_destroy(conn);
}

static void test_copy_peer_cid_allows_empty(void) {
    struct neverc_quic_conn *conn =
        neverc_quic_conn_create(QUIC_SIDE_SERVER, -1);
    quic_conn_id_t empty;
    memset(&empty, 0, sizeof(empty));
    ASSERT_EQ(quic_copy_peer_cid(conn, &empty), 0);
    ASSERT_EQ(conn->n_peer_cids, 1);
    ASSERT_EQ(conn->peer_cids[0].len, 0);
    neverc_quic_conn_destroy(conn);
}

static void test_configure_rejects_unknown_version(void) {
    struct neverc_quic_conn *conn =
        neverc_quic_conn_create(QUIC_SIDE_SERVER, -1);
    conn->version = 0xdeadbeefU;
    neverc_quic_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    neverc_udp_addr_t peer;
    memset(&peer, 0, sizeof(peer));
    quic_conn_id_t cid;
    memset(&cid, 0, sizeof(cid));
    cid.len = 8;
    ASSERT_EQ(neverc_quic_conn_configure(
                  conn, &cfg, (neverc_udp_conn_t *)(uintptr_t)1, 0, &peer,
                  NULL, &cid, &cid, NULL),
              -1);
    neverc_quic_conn_destroy(conn);
}

static void test_configure_rejects_oversized_stream_limit(void) {
    struct neverc_quic_conn *conn =
        neverc_quic_conn_create(QUIC_SIDE_SERVER, -1);
    neverc_quic_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.max_streams_bidi = (uint64_t)QUIC_MAX_STREAMS + 1U;
    neverc_udp_addr_t peer;
    memset(&peer, 0, sizeof(peer));
    quic_conn_id_t cid;
    memset(&cid, 0, sizeof(cid));
    cid.len = 8;
    ASSERT_EQ(neverc_quic_conn_configure(
                  conn, &cfg, (neverc_udp_conn_t *)(uintptr_t)1, 0, &peer,
                  NULL, &cid, &cid, NULL),
              -1);
    neverc_quic_conn_destroy(conn);
}

static void test_stream_fin_smaller_than_highest_is_final_size_error(void) {
    struct neverc_quic_conn *conn =
        neverc_quic_conn_create(QUIC_SIDE_SERVER, -1);
    conn->state = QUIC_CONN_ESTABLISHED;
    uint8_t data[10];
    memset(data, 'A', sizeof(data));
    quic_frame_stream_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.stream_id = 0;
    frame.data = data;
    frame.data_len = sizeof(data);
    ASSERT_EQ(neverc_quic_stream_receive(conn, &frame), 0);

    frame.data_len = 0;
    frame.fin = 1;
    ASSERT_EQ(neverc_quic_stream_receive(conn, &frame), -1);
    ASSERT_EQ(conn->state, QUIC_CONN_DRAINING);
    ASSERT_EQ(conn->close_error_code, QUIC_ERR_FINAL_SIZE_ERROR);
    neverc_quic_conn_destroy(conn);
}

static void test_stream_after_reset_stays_reset(void) {
    struct neverc_quic_conn *conn =
        neverc_quic_conn_create(QUIC_SIDE_SERVER, -1);
    conn->state = QUIC_CONN_ESTABLISHED;
    uint8_t data[5] = { 'h', 'e', 'l', 'l', 'o' };
    quic_frame_stream_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.stream_id = 0;
    frame.data = data;
    frame.data_len = sizeof(data);
    ASSERT_EQ(neverc_quic_stream_receive(conn, &frame), 0);

    quic_frame_reset_stream_t reset;
    memset(&reset, 0, sizeof(reset));
    reset.stream_id = 0;
    reset.error_code = 0x01;
    reset.final_size = sizeof(data);
    ASSERT_EQ(neverc_quic_stream_receive_reset(conn, &reset), 0);

    frame.fin = 1;
    ASSERT_EQ(neverc_quic_stream_receive(conn, &frame), 0);

    quic_stream_t *stream = neverc_quic_conn_find_stream(conn, 0);
    ASSERT_NOT_NULL(stream);
    char buf[8] = {0};
    ASSERT_EQ(neverc_quic_stream_read_data(stream, buf, sizeof(buf)), 5);
    ASSERT_TRUE(memcmp(buf, "hello", 5) == 0);
    ASSERT_EQ(neverc_quic_stream_read_data(stream, buf, sizeof(buf)), -1);
    ASSERT_EQ(stream->state, QUIC_STREAM_RESET);
    ASSERT_EQ(stream->recv_fin, 0);
    neverc_quic_conn_destroy(conn);

    conn = neverc_quic_conn_create(QUIC_SIDE_SERVER, -1);
    conn->state = QUIC_CONN_ESTABLISHED;
    memset(&reset, 0, sizeof(reset));
    reset.stream_id = 0;
    reset.final_size = sizeof(data);
    ASSERT_EQ(neverc_quic_stream_receive_reset(conn, &reset), 0);
    memset(&frame, 0, sizeof(frame));
    frame.stream_id = 0;
    frame.data = data;
    frame.data_len = sizeof(data);
    frame.fin = 1;
    ASSERT_EQ(neverc_quic_stream_receive(conn, &frame), 0);
    stream = neverc_quic_conn_find_stream(conn, 0);
    ASSERT_NOT_NULL(stream);
    ASSERT_EQ(neverc_quic_stream_read_data(stream, buf, sizeof(buf)), -1);
    ASSERT_EQ(stream->recv_fin, 0);
    neverc_quic_conn_destroy(conn);
}

static void test_reset_retires_connection_window(void) {
    struct neverc_quic_conn *conn =
        neverc_quic_conn_create(QUIC_SIDE_SERVER, -1);
    conn->state = QUIC_CONN_ESTABLISHED;
    uint64_t before_consumed = conn->flow.data_consumed;
    uint64_t before_max = conn->flow.max_data_local;
    quic_frame_reset_stream_t reset;
    memset(&reset, 0, sizeof(reset));
    reset.stream_id = 0;
    reset.final_size = 1000;
    ASSERT_EQ(neverc_quic_stream_receive_reset(conn, &reset), 0);
    ASSERT_EQ(conn->flow.data_consumed, before_consumed + 1000);
    ASSERT_EQ(conn->flow.max_data_local, before_max + 1000);
    ASSERT_EQ(conn->max_data_pending, 1);
    neverc_quic_conn_destroy(conn);
}

static void test_reset_final_size_below_highest(void) {
    struct neverc_quic_conn *conn =
        neverc_quic_conn_create(QUIC_SIDE_SERVER, -1);
    conn->state = QUIC_CONN_ESTABLISHED;
    uint8_t data[10];
    memset(data, 'B', sizeof(data));
    quic_frame_stream_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.stream_id = 0;
    frame.data = data;
    frame.data_len = sizeof(data);
    ASSERT_EQ(neverc_quic_stream_receive(conn, &frame), 0);

    quic_frame_reset_stream_t reset;
    memset(&reset, 0, sizeof(reset));
    reset.stream_id = 0;
    reset.final_size = 4;
    ASSERT_EQ(neverc_quic_stream_receive_reset(conn, &reset), -1);
    ASSERT_EQ(conn->state, QUIC_CONN_DRAINING);
    ASSERT_EQ(conn->close_error_code, QUIC_ERR_FINAL_SIZE_ERROR);
    neverc_quic_conn_destroy(conn);
}

static void test_stream_overlapping_data_must_match(void) {
    struct neverc_quic_conn *conn =
        neverc_quic_conn_create(QUIC_SIDE_SERVER, -1);
    conn->state = QUIC_CONN_ESTABLISHED;
    uint8_t first[] = { 'H', 'E', 'L', 'L', 'O' };
    uint8_t overlap_ok[] = { 'L', 'L', 'O', '!', '!' };
    uint8_t overlap_bad[] = { 'X', 'X', 'X' };
    quic_frame_stream_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.stream_id = 0;
    frame.data = first;
    frame.data_len = sizeof(first);
    ASSERT_EQ(neverc_quic_stream_receive(conn, &frame), 0);

    frame.offset = 2;
    frame.data = overlap_ok;
    frame.data_len = sizeof(overlap_ok);
    ASSERT_EQ(neverc_quic_stream_receive(conn, &frame), 0);
    quic_stream_t *stream = neverc_quic_conn_find_stream(conn, 0);
    ASSERT_NOT_NULL(stream);
    ASSERT_EQ(stream->recv_len, 7);
    ASSERT_TRUE(memcmp(stream->recv_buf, "HELLO!!", 7) == 0);

    frame.offset = 0;
    frame.data = overlap_bad;
    frame.data_len = sizeof(overlap_bad);
    ASSERT_EQ(neverc_quic_stream_receive(conn, &frame), -1);
    ASSERT_EQ(conn->state, QUIC_CONN_DRAINING);
    ASSERT_EQ(conn->close_error_code, QUIC_ERR_PROTOCOL_VIOLATION);
    neverc_quic_conn_destroy(conn);
}

static void test_new_conn_id_retire_prior_to_marks_unsent(void) {
    struct neverc_quic_conn *conn =
        neverc_quic_conn_create(QUIC_SIDE_CLIENT, -1);
    quic_conn_id_t peer;
    memset(&peer, 0, sizeof(peer));
    peer.len = 8;
    memset(peer.data, 0x11, 8);
    ASSERT_EQ(quic_copy_peer_cid(conn, &peer), 0);

    quic_frame_new_conn_id_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.sequence = 1;
    frame.retire_prior_to = 1;
    frame.conn_id_len = 8;
    memset(frame.conn_id, 0x22, 8);
    memset(frame.stateless_reset_token, 0x33, 16);
    ASSERT_EQ(neverc_quic_conn_add_peer_cid(conn, &frame), 0);
    ASSERT_EQ(conn->n_peer_cids, 2);
    ASSERT_EQ(conn->peer_cids[0].retired, 1);
    ASSERT_EQ(conn->peer_cids[0].retire_unsent, 1);
    ASSERT_EQ(conn->peer_cids[1].retired, 0);
    ASSERT_EQ(conn->active_peer_cid_idx, 1);
    neverc_quic_conn_destroy(conn);
}

static void test_new_conn_id_rejects_zero_length_dcid(void) {
    struct neverc_quic_conn *conn =
        neverc_quic_conn_create(QUIC_SIDE_CLIENT, -1);
    quic_conn_id_t peer;
    memset(&peer, 0, sizeof(peer));
    ASSERT_EQ(quic_copy_peer_cid(conn, &peer), 0);
    ASSERT_EQ(conn->peer_cids[0].len, 0);

    quic_frame_new_conn_id_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.sequence = 1;
    frame.retire_prior_to = 0;
    frame.conn_id_len = 8;
    memset(frame.conn_id, 0x22, 8);
    memset(frame.stateless_reset_token, 0x33, 16);
    ASSERT_EQ(neverc_quic_conn_add_peer_cid(conn, &frame), -1);
    ASSERT_EQ(conn->state, QUIC_CONN_DRAINING);
    ASSERT_EQ(conn->close_error_code, QUIC_ERR_PROTOCOL_VIOLATION);
    neverc_quic_conn_destroy(conn);
}

static void test_new_conn_id_below_retire_prior_to_is_retired(void) {
    struct neverc_quic_conn *conn =
        neverc_quic_conn_create(QUIC_SIDE_CLIENT, -1);
    quic_conn_id_t peer;
    memset(&peer, 0, sizeof(peer));
    peer.len = 8;
    memset(peer.data, 0x11, 8);
    ASSERT_EQ(quic_copy_peer_cid(conn, &peer), 0);

    quic_frame_new_conn_id_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.sequence = 2;
    frame.retire_prior_to = 2;
    frame.conn_id_len = 8;
    memset(frame.conn_id, 0x22, 8);
    memset(frame.stateless_reset_token, 0x33, 16);
    ASSERT_EQ(neverc_quic_conn_add_peer_cid(conn, &frame), 0);
    ASSERT_EQ(conn->highest_retire_prior_to, 2);

    frame.sequence = 1;
    frame.retire_prior_to = 0;
    memset(frame.conn_id, 0x44, 8);
    memset(frame.stateless_reset_token, 0x55, 16);
    ASSERT_EQ(neverc_quic_conn_add_peer_cid(conn, &frame), 0);
    int found = 0;
    for (int i = 0; i < conn->n_peer_cids; i++) {
        if (conn->peer_cids[i].sequence != 1)
            continue;
        found = 1;
        ASSERT_EQ(conn->peer_cids[i].retired, 1);
        ASSERT_EQ(conn->peer_cids[i].retire_unsent, 1);
        ASSERT_TRUE(conn->active_peer_cid_idx != i);
    }
    ASSERT_EQ(found, 1);
    neverc_quic_conn_destroy(conn);
}

static void test_apply_max_data_raises_window(void) {
    struct neverc_quic_conn *conn =
        neverc_quic_conn_create(QUIC_SIDE_CLIENT, -1);
    conn->flow.max_data_peer = 100;
    ASSERT_EQ(neverc_quic_conn_apply_max_data_locked(conn, 50), 0);
    ASSERT_EQ(conn->flow.max_data_peer, 100);
    ASSERT_EQ(neverc_quic_conn_apply_max_data_locked(conn, 4096), 0);
    ASSERT_EQ(conn->flow.max_data_peer, 4096);
    neverc_quic_conn_destroy(conn);
}

static void test_version_negotiation_roundtrip(void) {
    quic_conn_id_t dcid;
    quic_conn_id_t scid;
    memset(&dcid, 0, sizeof(dcid));
    memset(&scid, 0, sizeof(scid));
    dcid.len = 8;
    scid.len = 4;
    memset(dcid.data, 0xaa, 8);
    memset(scid.data, 0xbb, 4);
    uint32_t versions[2] = { NEVERC_QUIC_VERSION_1, NEVERC_QUIC_VERSION_2 };
    uint8_t buf[64];
    size_t written = 0;
    ASSERT_EQ(neverc_quic_write_version_negotiation(
                  buf, sizeof(buf), 0xC0, &dcid, &scid, versions, 2,
                  &written),
              0);
    ASSERT_TRUE(written >= 5);
    ASSERT_TRUE(neverc_quic_is_version_negotiation(buf, written));
    ASSERT_TRUE(neverc_quic_version_negotiation_supports(
        buf, written, NEVERC_QUIC_VERSION_1));
    ASSERT_TRUE(neverc_quic_version_negotiation_supports(
        buf, written, NEVERC_QUIC_VERSION_2));
    ASSERT_TRUE(!neverc_quic_version_negotiation_supports(
        buf, written, 0x12345678U));
    ASSERT_EQ(buf[5], 8);
    ASSERT_EQ(buf[5 + 1 + 8], 4);
}

static void test_version_negotiation_empty_cid(void) {
    quic_conn_id_t empty;
    quic_conn_id_t scid;
    memset(&empty, 0, sizeof(empty));
    memset(&scid, 0, sizeof(scid));
    scid.len = 8;
    memset(scid.data, 0xcc, 8);
    uint32_t version = NEVERC_QUIC_VERSION_1;
    uint8_t buf[32];
    size_t written = 0;
    ASSERT_EQ(neverc_quic_write_version_negotiation(
                  buf, sizeof(buf), 0x80, &empty, &scid, &version, 1,
                  &written),
              0);
    ASSERT_TRUE(neverc_quic_is_version_negotiation(buf, written));
    ASSERT_TRUE(neverc_quic_version_negotiation_supports(
        buf, written, NEVERC_QUIC_VERSION_1));
}

static void test_version_negotiation_dcid_extract(void) {
    quic_conn_id_t dcid;
    quic_conn_id_t scid;
    memset(&dcid, 0, sizeof(dcid));
    memset(&scid, 0, sizeof(scid));
    dcid.len = 8;
    scid.len = 4;
    memset(dcid.data, 0xaa, 8);
    memset(scid.data, 0xbb, 4);
    uint32_t version = NEVERC_QUIC_VERSION_1;
    uint8_t buf[64];
    size_t written = 0;
    ASSERT_EQ(neverc_quic_write_version_negotiation(
                  buf, sizeof(buf), 0x80, &dcid, &scid, &version, 1,
                  &written),
              0);
    quic_conn_id_t got;
    memset(&got, 0, sizeof(got));
    ASSERT_EQ(neverc_quic_version_negotiation_dcid(buf, written, &got), 0);
    ASSERT_EQ(got.len, 8);
    ASSERT_TRUE(memcmp(got.data, dcid.data, 8) == 0);
    ASSERT_EQ(neverc_quic_version_negotiation_dcid(buf, 4, &got), -1);
}

static void test_unprotected_packet_length_coalesced_and_truncated(void) {
    uint8_t buf[64];
    memset(buf, 0xcc, sizeof(buf));
    buf[0] = 0xC0; /* long, fixed, Initial, 1-byte PN */
    buf[1] = 0;
    buf[2] = 0;
    buf[3] = 0;
    buf[4] = 1; /* v1 */
    buf[5] = 0; /* DCID len */
    buf[6] = 0; /* SCID len */
    buf[7] = 0; /* token len */
    buf[8] = 20; /* Length */
    size_t packet_len = 0;
    ASSERT_EQ(neverc_quic_unprotected_packet_length(buf, 40, 0, &packet_len),
              0);
    ASSERT_EQ(packet_len, 29);
    ASSERT_EQ(neverc_quic_unprotected_packet_length(buf, 14, 0, &packet_len),
              -1);

    uint8_t short_hdr[20];
    memset(short_hdr, 0x11, sizeof(short_hdr));
    short_hdr[0] = 0x40;
    ASSERT_EQ(neverc_quic_unprotected_packet_length(short_hdr,
                                                    sizeof(short_hdr), 8,
                                                    &packet_len),
              0);
    ASSERT_EQ(packet_len, sizeof(short_hdr));
    ASSERT_EQ(neverc_quic_unprotected_packet_length(short_hdr, 5, 8,
                                                    &packet_len),
              -1);
}

static void test_unprotected_is_initial_ignores_hp_pn_length(void) {
    /* RFC 9000 §14.1 / RFC 9001 §5.4.1 / Go x/net getPacketType:
     * Initial identification for the 1200-byte datagram discard must use
     * unprotected type bits, not a full header parse of HP-garbled PN
     * length. */
    uint8_t buf[128];
    size_t packet_len = 0;
    ASSERT_EQ(write_parseable_long_header(NEVERC_QUIC_VERSION_1,
                                          QUIC_PKT_INITIAL, buf,
                                          sizeof(buf), &packet_len), 0);
    ASSERT_EQ(neverc_quic_unprotected_is_initial(buf, packet_len), 1);

    buf[0] = (uint8_t)((buf[0] & 0xf0U) | 0x0fU);
    ASSERT_EQ(neverc_quic_unprotected_is_initial(buf, packet_len), 1);

    /* Length=1 plus garbled 4-byte PN length: full parse fails, type bits
     * still name the packet Initial. The old conn-level check fail-opened. */
    uint8_t tiny[16];
    memset(tiny, 0, sizeof(tiny));
    tiny[0] = 0xcf; /* long, fixed, v1 Initial, reserved+PN len garbled */
    tiny[4] = 1;
    tiny[8] = 1; /* Length */
    tiny[9] = 0xaa;
    ASSERT_EQ(neverc_quic_unprotected_is_initial(tiny, 10), 1);
    quic_packet_header_t parsed;
    ASSERT_EQ(neverc_quic_parse_packet_header(tiny, 10, &parsed, 0), -1);

    ASSERT_EQ(write_parseable_long_header(NEVERC_QUIC_VERSION_1,
                                          QUIC_PKT_HANDSHAKE, buf,
                                          sizeof(buf), &packet_len), 0);
    ASSERT_EQ(neverc_quic_unprotected_is_initial(buf, packet_len), 0);

    ASSERT_EQ(write_parseable_long_header(NEVERC_QUIC_VERSION_2,
                                          QUIC_PKT_INITIAL, buf,
                                          sizeof(buf), &packet_len), 0);
    ASSERT_EQ(neverc_quic_unprotected_is_initial(buf, packet_len), 1);

    /* RFC 9369: v2 Retry reuses v1 Initial type bits 0b00. */
    quic_packet_header_t retry;
    memset(&retry, 0, sizeof(retry));
    retry.type = QUIC_PKT_RETRY;
    retry.version = NEVERC_QUIC_VERSION_2;
    retry.dcid.len = 8;
    memset(retry.dcid.data, 0x11, 8);
    retry.scid.len = 8;
    memset(retry.scid.data, 0x22, 8);
    retry.pkt_number_len = 1;
    size_t written = 0;
    ASSERT_EQ(neverc_quic_write_long_header(buf, sizeof(buf), &retry,
                                            &written), 0);
    ASSERT_EQ((buf[0] >> 4) & 3, 0);
    ASSERT_EQ(neverc_quic_unprotected_is_initial(buf, written), 0);

    uint8_t vn[5] = { 0xc0, 0, 0, 0, 0 };
    ASSERT_EQ(neverc_quic_unprotected_is_initial(vn, sizeof(vn)), 0);
    uint8_t short_hdr = 0x40;
    ASSERT_EQ(neverc_quic_unprotected_is_initial(&short_hdr, 1), 0);
    ASSERT_EQ(neverc_quic_unprotected_is_initial(NULL, 5), 0);
}

static void test_pn_window_tracks_extra_and_reacks(void) {
    quic_pn_state_t state;
    memset(&state, 0, sizeof(state));
    ASSERT_EQ(neverc_quic_pn_already_received(&state, 0), 0);
    ASSERT_EQ(neverc_quic_pn_mark_received(&state, 0, 1), 1);
    ASSERT_EQ(neverc_quic_pn_already_received(&state, 0), 1);
    ASSERT_EQ(neverc_quic_pn_mark_received(&state, 0, 1), 0);
    ASSERT_EQ(neverc_quic_pn_was_ack_eliciting(&state, 0), 1);

    ASSERT_EQ(neverc_quic_pn_mark_received(&state, 100, 1), 1);
    ASSERT_EQ(neverc_quic_pn_already_received(&state, 0), 1);
    ASSERT_EQ(neverc_quic_pn_already_received(&state, 1), 0);
    ASSERT_EQ(neverc_quic_pn_mark_received(&state, 0, 1), 0);
    ASSERT_EQ(neverc_quic_pn_was_ack_eliciting(&state, 0), 1);

    quic_ack_range_t ranges[8];
    int nranges = 0;
    ASSERT_EQ(neverc_quic_pn_ack_ranges(&state, ranges, 8, &nranges), 0);
    ASSERT_EQ(nranges, 2);
    ASSERT_EQ(ranges[0].start, 100);
    ASSERT_EQ(ranges[0].end, 101);
    ASSERT_EQ(ranges[1].start, 0);
    ASSERT_EQ(ranges[1].end, 1);

    quic_pn_state_t window;
    memset(&window, 0, sizeof(window));
    uint64_t pn;
    for (pn = 0; pn < 64; pn++)
        ASSERT_EQ(neverc_quic_pn_mark_received(&window, pn, 1), 1);
    ASSERT_EQ(neverc_quic_pn_mark_received(&window, 64, 1), 1);
    ASSERT_EQ(neverc_quic_pn_already_received(&window, 0), 1);
    ASSERT_EQ(neverc_quic_pn_already_received(&window, 1), 1);
    nranges = 0;
    ASSERT_EQ(neverc_quic_pn_ack_ranges(&window, ranges, 8, &nranges), 0);
    ASSERT_EQ(nranges, 1);
    ASSERT_EQ(ranges[0].start, 0);
    ASSERT_EQ(ranges[0].end, 65);

    quic_pn_state_t ack_only;
    memset(&ack_only, 0, sizeof(ack_only));
    ASSERT_EQ(neverc_quic_pn_mark_received(&ack_only, 0, 0), 1);
    ASSERT_EQ(neverc_quic_pn_mark_received(&ack_only, 100, 1), 1);
    ASSERT_EQ(neverc_quic_pn_was_ack_eliciting(&ack_only, 0), 0);
    ASSERT_EQ(neverc_quic_pn_already_received(&ack_only, 0), 1);
}

static void test_retired_local_cid_still_matches(void) {
    struct neverc_quic_conn *conn =
        neverc_quic_conn_create(QUIC_SIDE_SERVER, -1);
    ASSERT_NOT_NULL(conn);
    ASSERT_EQ(conn->n_local_cids, 1);
    uint8_t cid[QUIC_MAX_CID_LEN];
    uint8_t len = conn->local_cids[0].len;
    memcpy(cid, conn->local_cids[0].id, len);
    ASSERT_TRUE(neverc_quic_conn_id_matches(conn, cid, len));
    ASSERT_EQ(neverc_quic_conn_retire_local_cid_locked(conn, 0), 0);
    ASSERT_EQ(conn->local_cids[0].retired, 1);
    ASSERT_EQ(conn->new_cid_pending, 1);
    ASSERT_TRUE(neverc_quic_conn_id_matches(conn, cid, len));
    ASSERT_EQ(neverc_quic_conn_retire_local_cid_locked(conn, 0), 0);
    ASSERT_EQ(neverc_quic_conn_retire_local_cid_locked(conn, 99), -1);
    neverc_quic_conn_destroy(conn);
}

/* ======================================================================
 * main
 * ====================================================================== */

int main(void) {
    printf("QUIC Connection State Machine test suite:\n\n");

    test_conn_create_client();
    test_conn_create_server();
    test_conn_defaults();
    test_conn_id_generation();
    test_conn_id_max_limit();
    test_stream_open_bidi_client();
    test_stream_open_uni_server();
    test_stream_open_requires_established();
    test_stream_is_local();
    test_stream_write_read();
    test_stream_write_ignores_flush_failure();
    test_stream_write_grow_buffer();
    test_stream_read_fin();
    test_stream_zero_flow_control_limit();
    test_stream_write_after_close();
    test_conn_close();
    test_conn_close_wakes_streams();
    test_conn_close_idempotent();
    test_conn_is_alive();
    test_conn_loss_timeout_disarmed_after_address_validation();
    test_conn_loss_timeout_fresh_before_peer_validation();
    test_conn_loss_timeout_cancelled_at_amplification_limit();
    test_conn_alpn();
    test_stream_get_id();
    test_transport_params_absent_flow_control_is_zero();
    test_transport_params_partial_does_not_invent_limits();
    test_effective_idle_timeout();
    test_apply_peer_transport_params_copies_max_ack_delay();
    test_stream_receive_opens_lower_ids();
    test_stream_receive_opens_lower_uni_ids();
    test_stream_receive_gap_respects_limit();
    test_max_stream_data_creates_peer_bidi();
    test_stop_sending_creates_peer_bidi();
    test_decode_packet_number_wrap_and_limit();
    test_v1_long_header_type_bits();
    test_client_drops_server_initial_token();
    test_v2_long_header_type_bits();
    test_conn_defaults_to_quic_v1();
    test_copy_peer_cid_allows_empty();
    test_configure_rejects_unknown_version();
    test_configure_rejects_oversized_stream_limit();
    test_stream_fin_smaller_than_highest_is_final_size_error();
    test_stream_after_reset_stays_reset();
    test_reset_retires_connection_window();
    test_reset_final_size_below_highest();
    test_stream_overlapping_data_must_match();
    test_new_conn_id_retire_prior_to_marks_unsent();
    test_new_conn_id_rejects_zero_length_dcid();
    test_new_conn_id_below_retire_prior_to_is_retired();
    test_apply_max_data_raises_window();
    test_version_negotiation_roundtrip();
    test_version_negotiation_empty_cid();
    test_version_negotiation_dcid_extract();
    test_unprotected_packet_length_coalesced_and_truncated();
    test_unprotected_is_initial_ignores_hp_pn_length();
    test_pn_window_tracks_extra_and_reacks();
    test_retired_local_cid_still_matches();
    printf("\n%d passed, %d failed (of %d)\n", tests_passed, tests_failed, tests_run);
    if (tests_failed == 0) puts("passed");
    return tests_failed > 0 ? 1 : 0;
}
