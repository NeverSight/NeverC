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
#include "../../../std/src/net/quic/quic_transport_params.c"
#include "../../../std/src/net/quic/quic_loss.c"

/*
 * This unit owns neither a UDP endpoint nor a TLS/transport engine.  Keep
 * those boundaries inert while exercising the real connection, flow-control,
 * transport-parameter, and loss-detection state.
 */
static quic_tls_t g_stub_tls;

quic_tls_t *neverc_quic_tls_create(int is_server) {
    (void)is_server;
    return &g_stub_tls;
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
int neverc_quic_conn_flush(struct neverc_quic_conn *conn) {
    (void)conn;
    return 0;
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
    ASSERT_EQ(s->state, QUIC_STREAM_CLOSED);

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

    conn->state = QUIC_CONN_CLOSED;
    ASSERT_EQ(neverc_quic_conn_is_alive_check(conn), 0);

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
    test_stream_write_grow_buffer();
    test_stream_read_fin();
    test_stream_write_after_close();
    test_conn_close();
    test_conn_close_wakes_streams();
    test_conn_close_idempotent();
    test_conn_is_alive();
    test_conn_alpn();
    test_stream_get_id();
    printf("\n%d passed, %d failed (of %d)\n", tests_passed, tests_failed, tests_run);
    if (tests_failed == 0) puts("passed");
    return tests_failed > 0 ? 1 : 0;
}
