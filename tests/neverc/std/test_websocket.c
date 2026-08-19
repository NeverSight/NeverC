#include "neverc/std/net/websocket.h"
#include "neverc/std/net/http.h"
#include "neverc/std/net/tcp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#endif

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

static void check_int(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got %d, expected %d\n", name, got, expected); }
}

static void check_str(const char *name, const char *got, const char *expected) {
    tests_run++;
    if (got && expected && strcmp(got, expected) == 0) tests_passed++;
    else {
        tests_failed++;
        printf("  FAIL: %s: got \"%s\", expected \"%s\"\n",
               name, got ? got : "NULL", expected ? expected : "NULL");
    }
}

static void check_not_null(const char *name, const void *ptr) {
    tests_run++;
    if (ptr) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got NULL\n", name); }
}

/* ===== RFC 6455 accept key test vector ===== */

static void test_compute_accept(void) {
    printf("[compute_accept]\n");
    char accept[64];
    const char *key = "dGhlIHNhbXBsZSBub25jZQ==";
    check_int("compute ok", neverc_ws_compute_accept(key, accept, sizeof(accept)), 0);
    check_str("accept value", accept, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}

/* ===== null safety ===== */

static void test_null_safety(void) {
    printf("[null_safety]\n");
    check_int("compute null", neverc_ws_compute_accept(NULL, NULL, 0), -1);
    check_int("handshake null", neverc_ws_handshake_server(NULL, NULL, 0, NULL), -1);
    const char *err = NULL;
    check_int("dial null", neverc_ws_dial(NULL, NULL, &err) == NULL, 1);
    check_int("reject wss plaintext fallback",
              neverc_ws_dial("wss://localhost/ws", NULL, &err) == NULL, 1);
    neverc_ws_conn_free(NULL);
    check_int("write null", neverc_ws_write_message(NULL, "x"), -1);
    tests_passed++; tests_run++;
}

static void test_utf8_prefix_validation(void) {
    printf("[utf8_prefix_validation]\n");
    static const uint8_t incomplete_three[] = {0xe2, 0x82};
    static const uint8_t bad_three[] = {0xe2, 0x28};
    static const uint8_t bad_four_first[] = {0xf1, 0x20};
    static const uint8_t bad_four_second[] = {0xf1, 0x80, 0x20};
    static const uint8_t complete_four[] = {0xf0, 0x9f, 0x98, 0x80};

    check_int("incomplete three-byte prefix",
              neverc_ws_valid_utf8_prefix(
                  incomplete_three, sizeof(incomplete_three)), 1);
    check_int("reject bad first continuation",
              neverc_ws_valid_utf8_prefix(bad_three, sizeof(bad_three)), 0);
    check_int("reject bad four-byte first continuation",
              neverc_ws_valid_utf8_prefix(
                  bad_four_first, sizeof(bad_four_first)), 0);
    check_int("reject bad four-byte second continuation",
              neverc_ws_valid_utf8_prefix(
                  bad_four_second, sizeof(bad_four_second)), 0);
    check_int("accept complete four-byte character",
              neverc_ws_valid_utf8_prefix(
                  complete_four, sizeof(complete_four)), 1);
}

static void test_handshake_rejects(void) {
    printf("[handshake_rejects]\n");
    const char *err = NULL;
    neverc_tcp_listener_t *ln = neverc_tcp_listen("127.0.0.1:0", &err);
    check_not_null("reject listen", ln);
    if (!ln) return;

    neverc_tcp_addr_t laddr;
    neverc_tcp_listener_addr(ln, &laddr);
    char addr[64];
    snprintf(addr, sizeof(addr), "127.0.0.1:%u", (unsigned)laddr.port);
    neverc_tcp_conn_t *client = neverc_tcp_dial(addr, &err);
    neverc_tcp_conn_t *server = neverc_tcp_accept(ln, &err);
    check_not_null("reject client", client);
    check_not_null("reject server", server);
    if (!client || !server) {
        if (client) neverc_tcp_close(client);
        if (server) neverc_tcp_close(server);
        neverc_tcp_listener_close(ln);
        return;
    }

    size_t consumed = 0;
    const char *version_130 =
        "GET /ws HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Version: 130\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "\r\n";
    check_int("reject version 130",
              neverc_ws_handshake_server(server, version_130,
                                         strlen(version_130), &consumed),
              -1);

    const char *upgrade_suffix =
        "GET /ws HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Upgrade: websocketfoo\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "\r\n";
    check_int("reject Upgrade suffix",
              neverc_ws_handshake_server(server, upgrade_suffix,
                                         strlen(upgrade_suffix), &consumed),
              -1);

    const char *http10 =
        "GET /ws HTTP/1.0\r\n"
        "Host: localhost\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "\r\n";
    check_int("reject HTTP/1.0",
              neverc_ws_handshake_server(server, http10, strlen(http10),
                                         &consumed),
              -1);

    const char *no_host =
        "GET /ws HTTP/1.1\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "\r\n";
    check_int("reject missing Host",
              neverc_ws_handshake_server(server, no_host, strlen(no_host),
                                         &consumed),
              -1);

    const char *short_key =
        "GET /ws HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ=\r\n"
        "\r\n";
    check_int("reject short key",
              neverc_ws_handshake_server(server, short_key, strlen(short_key),
                                         &consumed),
              -1);

    const char *unpadded_key =
        "GET /ws HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Sec-WebSocket-Key: AAAAAAAAAAAAAAAAAAAAAAAA\r\n"
        "\r\n";
    check_int("reject unpadded 24-char key",
              neverc_ws_handshake_server(server, unpadded_key,
                                         strlen(unpadded_key), &consumed),
              -1);

    const char *dup_upgrade =
        "GET /ws HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Upgrade: websocket\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "\r\n";
    check_int("reject duplicate Upgrade",
              neverc_ws_handshake_server(server, dup_upgrade,
                                         strlen(dup_upgrade), &consumed),
              -1);

    const char *with_body =
        "GET /ws HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Content-Length: 4\r\n"
        "\r\n"
        "XXXX";
    check_int("reject Content-Length body",
              neverc_ws_handshake_server(server, with_body, strlen(with_body),
                                         &consumed),
              -1);

    const char *with_te =
        "GET /ws HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n";
    check_int("reject Transfer-Encoding",
              neverc_ws_handshake_server(server, with_te, strlen(with_te),
                                         &consumed),
              -1);

    const char *good =
        "GET /ws HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade , keep-alive\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "\r\n";
    check_int("accept Connection token list",
              neverc_ws_handshake_server(server, good, strlen(good),
                                         &consumed),
              0);

    const char *folded_cl =
        "GET /ws HTTP/1.1\r\n"
        "Host: localhost\r\n"
        " Content-Length: 4\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "\r\n"
        "XXXX";
    check_int("reject obs-fold Content-Length",
              neverc_ws_handshake_server(server, folded_cl, strlen(folded_cl),
                                         &consumed),
              -1);

    const char *bare_lf_cl =
        "GET /ws HTTP/1.1\r\n"
        "Host: localhost\n"
        "Content-Length: 4\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "\r\n"
        "XXXX";
    check_int("reject bare LF smuggled Content-Length",
              neverc_ws_handshake_server(server, bare_lf_cl,
                                         strlen(bare_lf_cl), &consumed),
              -1);

    neverc_tcp_close(client);
    neverc_tcp_close(server);
    neverc_tcp_listener_close(ln);
}

static void test_reject_unmasked_client_frame(void) {
    printf("[reject_unmasked_client_frame]\n");
    const char *err = NULL;
    neverc_tcp_listener_t *ln = neverc_tcp_listen("127.0.0.1:0", &err);
    check_not_null("unmasked listen", ln);
    if (!ln) return;

    neverc_tcp_addr_t laddr;
    neverc_tcp_listener_addr(ln, &laddr);
    char addr[64];
    snprintf(addr, sizeof(addr), "127.0.0.1:%u", (unsigned)laddr.port);
    neverc_tcp_conn_t *client = neverc_tcp_dial(addr, &err);
    neverc_tcp_conn_t *server = neverc_tcp_accept(ln, &err);
    check_not_null("unmasked client", client);
    check_not_null("unmasked server", server);
    if (!client || !server) {
        if (client) neverc_tcp_close(client);
        if (server) neverc_tcp_close(server);
        neverc_tcp_listener_close(ln);
        return;
    }

    const char *good =
        "GET /ws HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "\r\n";
    size_t consumed = 0;
    check_int("unmasked handshake",
              neverc_ws_handshake_server(server, good, strlen(good),
                                         &consumed),
              0);

    /* RFC 6455 §5.1: a server MUST close on an unmasked client frame. */
    uint8_t unmasked[] = { 0x81, 0x05, 'h', 'e', 'l', 'l', 'o' };
    check_int("write unmasked",
              neverc_tcp_write(client, unmasked, sizeof(unmasked)) ==
                  (int)sizeof(unmasked),
              1);

    neverc_ws_conn_t *ws = neverc_ws_conn_new(server);
    check_not_null("unmasked server ws", ws);
    if (ws) {
        int opcode = 0;
        char buf[16];
        size_t n = 0;
        check_int("reject unmasked client frame",
                  neverc_ws_read_frame(ws, &opcode, NULL, buf, sizeof(buf),
                                       &n),
                  -1);
        neverc_ws_conn_free(ws);
    } else {
        neverc_tcp_close(server);
    }
    neverc_tcp_close(client);
    neverc_tcp_listener_close(ln);
}

static int ws_write_masked_frame_ex(neverc_tcp_conn_t *conn, int opcode,
                                    int fin, const void *data, size_t len) {
    if (!conn || len > 125 || (len > 0 && !data)) return -1;
    uint8_t mask[4] = {0x37, 0xfa, 0x21, 0x3d};
    uint8_t hdr[6];
    hdr[0] = (uint8_t)((fin ? 0x80 : 0x00) | (opcode & 0x0f));
    hdr[1] = (uint8_t)(0x80 | len);
    memcpy(hdr + 2, mask, 4);
    if (neverc_tcp_write(conn, hdr, sizeof(hdr)) != (int)sizeof(hdr))
        return -1;
    if (len == 0) return 0;
    uint8_t masked[125];
    const uint8_t *bytes = (const uint8_t *)data;
    size_t i;
    for (i = 0; i < len; i++)
        masked[i] = (uint8_t)(bytes[i] ^ mask[i % 4]);
    return neverc_tcp_write(conn, masked, len) == (int)len ? 0 : -1;
}

static int ws_write_masked_frame(neverc_tcp_conn_t *conn, int opcode,
                                 const void *data, size_t len) {
    return ws_write_masked_frame_ex(conn, opcode, 1, data, len);
}

static int ws_drain_http_response(neverc_tcp_conn_t *conn) {
    char buf[1024];
    size_t total = 0;
    while (total < sizeof(buf) - 1) {
        int n = neverc_tcp_read(conn, buf + total, 1);
        if (n <= 0) return -1;
        total += (size_t)n;
        if (total >= 4 && memcmp(buf + total - 4, "\r\n\r\n", 4) == 0)
            return 0;
    }
    return -1;
}

static int ws_tcp_read_exact(neverc_tcp_conn_t *conn, void *buf, size_t len) {
    char *p = (char *)buf;
    size_t total = 0;
    while (total < len) {
        int n = neverc_tcp_read(conn, p + total, len - total);
        if (n <= 0) return -1;
        total += (size_t)n;
    }
    return 0;
}

static neverc_ws_conn_t *ws_test_server_handshake(
    neverc_tcp_conn_t *server, neverc_tcp_conn_t *client) {
    neverc_tcp_set_timeout(server, 5000);
    neverc_tcp_set_timeout(client, 5000);
    const char *good =
        "GET /ws HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "\r\n";
    size_t consumed = 0;
    if (neverc_ws_handshake_server(server, good, strlen(good),
                                   &consumed) != 0)
        return NULL;
    if (ws_drain_http_response(client) != 0)
        return NULL;
    return neverc_ws_conn_new(server);
}

static void test_close_code_message_too_big(void) {
    printf("[close_code_message_too_big]\n");
    const char *err = NULL;
    neverc_tcp_listener_t *ln = neverc_tcp_listen("127.0.0.1:0", &err);
    check_not_null("too-big listen", ln);
    if (!ln) return;

    neverc_tcp_addr_t laddr;
    neverc_tcp_listener_addr(ln, &laddr);
    char addr[64];
    snprintf(addr, sizeof(addr), "127.0.0.1:%u", (unsigned)laddr.port);
    neverc_tcp_conn_t *client = neverc_tcp_dial(addr, &err);
    neverc_tcp_conn_t *server = neverc_tcp_accept(ln, &err);
    check_not_null("too-big client", client);
    check_not_null("too-big server", server);
    if (!client || !server) {
        if (client) neverc_tcp_close(client);
        if (server) neverc_tcp_close(server);
        neverc_tcp_listener_close(ln);
        return;
    }

    neverc_ws_conn_t *ws = ws_test_server_handshake(server, client);
    check_not_null("too-big server ws", ws);
    if (ws) {
        check_int("set read limit", neverc_ws_set_read_limit(ws, 8), 0);
        char payload[20];
        memset(payload, 'x', sizeof(payload));
        check_int("write oversized frame",
                  ws_write_masked_frame(client, NC_WS_OPCODE_BINARY,
                                        payload, sizeof(payload)),
                  0);
        int opcode = 0;
        char buf[32];
        size_t n = 0;
        check_int("reject oversized frame",
                  neverc_ws_read_frame(ws, &opcode, NULL, buf, sizeof(buf),
                                       &n),
                  -1);
        uint8_t close_hdr[4];
        check_int("read close header",
                  ws_tcp_read_exact(client, close_hdr, sizeof(close_hdr)),
                  0);
        check_int("close opcode", close_hdr[0], 0x88);
        check_int("close unmasked len 2", close_hdr[1], 0x02);
        uint16_t code = (uint16_t)(((uint16_t)close_hdr[2] << 8) |
                                   close_hdr[3]);
        check_int("close code 1009", code, 1009);
        neverc_ws_conn_free(ws);
    } else {
        neverc_tcp_close(server);
    }
    neverc_tcp_close(client);
    neverc_tcp_listener_close(ln);
}

static void test_close_invalid_utf8_reason_is_1007(void) {
    printf("[close_invalid_utf8_reason]\n");
    const char *err = NULL;
    neverc_tcp_listener_t *ln = neverc_tcp_listen("127.0.0.1:0", &err);
    check_not_null("utf8-close listen", ln);
    if (!ln) return;

    neverc_tcp_addr_t laddr;
    neverc_tcp_listener_addr(ln, &laddr);
    char addr[64];
    snprintf(addr, sizeof(addr), "127.0.0.1:%u", (unsigned)laddr.port);
    neverc_tcp_conn_t *client = neverc_tcp_dial(addr, &err);
    neverc_tcp_conn_t *server = neverc_tcp_accept(ln, &err);
    check_not_null("utf8-close client", client);
    check_not_null("utf8-close server", server);
    if (!client || !server) {
        if (client) neverc_tcp_close(client);
        if (server) neverc_tcp_close(server);
        neverc_tcp_listener_close(ln);
        return;
    }

    neverc_ws_conn_t *ws = ws_test_server_handshake(server, client);
    check_not_null("utf8-close server ws", ws);
    if (ws) {
        uint8_t payload[] = { 0x03, 0xe8, 0x80 }; /* 1000 + invalid UTF-8 */
        check_int("write close with bad reason",
                  ws_write_masked_frame(client, NC_WS_OPCODE_CLOSE,
                                        payload, sizeof(payload)),
                  0);
        int opcode = 0;
        char buf[32];
        size_t n = 0;
        check_int("reject invalid close reason",
                  neverc_ws_read_frame(ws, &opcode, NULL, buf, sizeof(buf),
                                       &n),
                  -1);
        uint8_t close_hdr[4];
        check_int("read close header",
                  ws_tcp_read_exact(client, close_hdr, sizeof(close_hdr)),
                  0);
        check_int("close opcode", close_hdr[0], 0x88);
        check_int("close unmasked len 2", close_hdr[1], 0x02);
        uint16_t code = (uint16_t)(((uint16_t)close_hdr[2] << 8) |
                                   close_hdr[3]);
        check_int("close code 1007", code, 1007);
        neverc_ws_conn_free(ws);
    } else {
        neverc_tcp_close(server);
    }
    neverc_tcp_close(client);
    neverc_tcp_listener_close(ln);
}

static void test_local_buffer_too_small_keeps_stream(void) {
    printf("[local_buffer_too_small]\n");
    const char *err = NULL;
    neverc_tcp_listener_t *ln = neverc_tcp_listen("127.0.0.1:0", &err);
    check_not_null("small-buf listen", ln);
    if (!ln) return;

    neverc_tcp_addr_t laddr;
    neverc_tcp_listener_addr(ln, &laddr);
    char addr[64];
    snprintf(addr, sizeof(addr), "127.0.0.1:%u", (unsigned)laddr.port);
    neverc_tcp_conn_t *client = neverc_tcp_dial(addr, &err);
    neverc_tcp_conn_t *server = neverc_tcp_accept(ln, &err);
    check_not_null("small-buf client", client);
    check_not_null("small-buf server", server);
    if (!client || !server) {
        if (client) neverc_tcp_close(client);
        if (server) neverc_tcp_close(server);
        neverc_tcp_listener_close(ln);
        return;
    }

    neverc_ws_conn_t *ws = ws_test_server_handshake(server, client);
    check_not_null("small-buf server ws", ws);
    if (ws) {
        char payload[20];
        memset(payload, 'y', sizeof(payload));
        check_int("write large data frame",
                  ws_write_masked_frame(client, NC_WS_OPCODE_BINARY,
                                        payload, sizeof(payload)),
                  0);
        int opcode = 0;
        char tiny[4];
        size_t n = 0;
        check_int("local buffer too small",
                  neverc_ws_read_frame(ws, &opcode, NULL, tiny, sizeof(tiny),
                                       &n),
                  -1);
        check_int("write follow-up hello",
                  ws_write_masked_frame(client, NC_WS_OPCODE_TEXT,
                                        "hello", 5),
                  0);
        char buf[16];
        n = 0;
        opcode = 0;
        check_int("next frame after drain",
                  neverc_ws_read_frame(ws, &opcode, NULL, buf, sizeof(buf),
                                       &n),
                  0);
        check_int("follow-up opcode", opcode, NC_WS_OPCODE_TEXT);
        check_int("follow-up length", (int)n, 5);
        buf[n] = '\0';
        check_str("follow-up payload", buf, "hello");
        neverc_ws_conn_free(ws);
    } else {
        neverc_tcp_close(server);
    }
    neverc_tcp_close(client);
    neverc_tcp_listener_close(ln);
}

/* Discarding a non-final continuation into a too-small buffer must clear
 * fragment state so the next TEXT/BINARY is not rejected as 1002. */
static void test_small_buffer_discards_fragment_keeps_stream(void) {
    printf("[small_buffer_discards_fragment]\n");
    const char *err = NULL;
    neverc_tcp_listener_t *ln = neverc_tcp_listen("127.0.0.1:0", &err);
    check_not_null("frag-discard listen", ln);
    if (!ln) return;

    neverc_tcp_addr_t laddr;
    neverc_tcp_listener_addr(ln, &laddr);
    char addr[64];
    snprintf(addr, sizeof(addr), "127.0.0.1:%u", (unsigned)laddr.port);
    neverc_tcp_conn_t *client = neverc_tcp_dial(addr, &err);
    neverc_tcp_conn_t *server = neverc_tcp_accept(ln, &err);
    check_not_null("frag-discard client", client);
    check_not_null("frag-discard server", server);
    if (!client || !server) {
        if (client) neverc_tcp_close(client);
        if (server) neverc_tcp_close(server);
        neverc_tcp_listener_close(ln);
        return;
    }

    neverc_ws_conn_t *ws = ws_test_server_handshake(server, client);
    check_not_null("frag-discard server ws", ws);
    if (ws) {
        check_int("write first text fragment",
                  ws_write_masked_frame_ex(client, NC_WS_OPCODE_TEXT, 0,
                                           "ab", 2),
                  0);
        int opcode = 0;
        char tiny[4];
        size_t n = 0;
        check_int("read first fragment",
                  neverc_ws_read_frame(ws, &opcode, NULL, tiny, sizeof(tiny),
                                       &n),
                  0);
        check_int("first fragment opcode", opcode, NC_WS_OPCODE_TEXT);
        check_int("first fragment len", (int)n, 2);

        char oversized[20];
        memset(oversized, 'z', sizeof(oversized));
        check_int("write oversized continuation",
                  ws_write_masked_frame_ex(client, NC_WS_OPCODE_CONTINUATION, 1,
                                           oversized, sizeof(oversized)),
                  0);
        n = 0;
        check_int("discard oversized continuation",
                  neverc_ws_read_frame(ws, &opcode, NULL, tiny, sizeof(tiny),
                                       &n),
                  -1);

        check_int("write follow-up text",
                  ws_write_masked_frame(client, NC_WS_OPCODE_TEXT,
                                        "hello", 5),
                  0);
        char buf[16];
        n = 0;
        opcode = 0;
        check_int("next text after fragment discard",
                  neverc_ws_read_frame(ws, &opcode, NULL, buf, sizeof(buf),
                                       &n),
                  0);
        check_int("follow-up opcode after discard", opcode, NC_WS_OPCODE_TEXT);
        check_int("follow-up length after discard", (int)n, 5);
        buf[n] = '\0';
        check_str("follow-up payload after discard", buf, "hello");
        neverc_ws_conn_free(ws);
    } else {
        neverc_tcp_close(server);
    }
    neverc_tcp_close(client);
    neverc_tcp_listener_close(ln);
}

/* An oversized TEXT while a fragment is open is 1002, not a silent discard. */
static void test_oversized_text_during_fragment_is_1002(void) {
    printf("[oversized_text_during_fragment]\n");
    const char *err = NULL;
    neverc_tcp_listener_t *ln = neverc_tcp_listen("127.0.0.1:0", &err);
    check_not_null("ovsz-text listen", ln);
    if (!ln) return;

    neverc_tcp_addr_t laddr;
    neverc_tcp_listener_addr(ln, &laddr);
    char addr[64];
    snprintf(addr, sizeof(addr), "127.0.0.1:%u", (unsigned)laddr.port);
    neverc_tcp_conn_t *client = neverc_tcp_dial(addr, &err);
    neverc_tcp_conn_t *server = neverc_tcp_accept(ln, &err);
    check_not_null("ovsz-text client", client);
    check_not_null("ovsz-text server", server);
    if (!client || !server) {
        if (client) neverc_tcp_close(client);
        if (server) neverc_tcp_close(server);
        neverc_tcp_listener_close(ln);
        return;
    }

    neverc_ws_conn_t *ws = ws_test_server_handshake(server, client);
    check_not_null("ovsz-text server ws", ws);
    if (ws) {
        check_int("write first text fragment",
                  ws_write_masked_frame_ex(client, NC_WS_OPCODE_TEXT, 0,
                                           "ab", 2),
                  0);
        int opcode = 0;
        char tiny[4];
        size_t n = 0;
        check_int("read first fragment",
                  neverc_ws_read_frame(ws, &opcode, NULL, tiny, sizeof(tiny),
                                       &n),
                  0);

        char oversized[20];
        memset(oversized, 'z', sizeof(oversized));
        check_int("write oversized text during fragment",
                  ws_write_masked_frame_ex(client, NC_WS_OPCODE_TEXT, 1,
                                           oversized, sizeof(oversized)),
                  0);
        n = 0;
        check_int("reject oversized text during fragment",
                  neverc_ws_read_frame(ws, &opcode, NULL, tiny, sizeof(tiny),
                                       &n),
                  -1);
        uint8_t close_hdr[4];
        check_int("ovsz-text close header",
                  ws_tcp_read_exact(client, close_hdr, sizeof(close_hdr)),
                  0);
        uint16_t code = (uint16_t)(((uint16_t)close_hdr[2] << 8) |
                                   close_hdr[3]);
        check_int("ovsz-text close code 1002", code, 1002);
        neverc_ws_conn_free(ws);
    } else {
        neverc_tcp_close(server);
    }
    neverc_tcp_close(client);
    neverc_tcp_listener_close(ln);
}

/* A continuation with no open fragment is 1002 even if it does not fit. */
static void test_oversized_stray_continuation_is_1002(void) {
    printf("[oversized_stray_continuation]\n");
    const char *err = NULL;
    neverc_tcp_listener_t *ln = neverc_tcp_listen("127.0.0.1:0", &err);
    check_not_null("ovsz-cont listen", ln);
    if (!ln) return;

    neverc_tcp_addr_t laddr;
    neverc_tcp_listener_addr(ln, &laddr);
    char addr[64];
    snprintf(addr, sizeof(addr), "127.0.0.1:%u", (unsigned)laddr.port);
    neverc_tcp_conn_t *client = neverc_tcp_dial(addr, &err);
    neverc_tcp_conn_t *server = neverc_tcp_accept(ln, &err);
    check_not_null("ovsz-cont client", client);
    check_not_null("ovsz-cont server", server);
    if (!client || !server) {
        if (client) neverc_tcp_close(client);
        if (server) neverc_tcp_close(server);
        neverc_tcp_listener_close(ln);
        return;
    }

    neverc_ws_conn_t *ws = ws_test_server_handshake(server, client);
    check_not_null("ovsz-cont server ws", ws);
    if (ws) {
        char oversized[20];
        memset(oversized, 'z', sizeof(oversized));
        check_int("write stray continuation",
                  ws_write_masked_frame_ex(client, NC_WS_OPCODE_CONTINUATION, 1,
                                           oversized, sizeof(oversized)),
                  0);
        int opcode = 0;
        char tiny[4];
        size_t n = 0;
        check_int("reject stray continuation",
                  neverc_ws_read_frame(ws, &opcode, NULL, tiny, sizeof(tiny),
                                       &n),
                  -1);
        uint8_t close_hdr[4];
        check_int("ovsz-cont close header",
                  ws_tcp_read_exact(client, close_hdr, sizeof(close_hdr)),
                  0);
        uint16_t code = (uint16_t)(((uint16_t)close_hdr[2] << 8) |
                                   close_hdr[3]);
        check_int("ovsz-cont close code 1002", code, 1002);
        neverc_ws_conn_free(ws);
    } else {
        neverc_tcp_close(server);
    }
    neverc_tcp_close(client);
    neverc_tcp_listener_close(ln);
}

/* read_message must reset fragment state when a later fragment does not
 * fit the caller buffer, or the next message is rejected as 1002. */
static void test_read_message_overflow_clears_fragment(void) {
    printf("[read_message_overflow_clears_fragment]\n");
    const char *err = NULL;
    neverc_tcp_listener_t *ln = neverc_tcp_listen("127.0.0.1:0", &err);
    check_not_null("msg-overflow listen", ln);
    if (!ln) return;

    neverc_tcp_addr_t laddr;
    neverc_tcp_listener_addr(ln, &laddr);
    char addr[64];
    snprintf(addr, sizeof(addr), "127.0.0.1:%u", (unsigned)laddr.port);
    neverc_tcp_conn_t *client = neverc_tcp_dial(addr, &err);
    neverc_tcp_conn_t *server = neverc_tcp_accept(ln, &err);
    check_not_null("msg-overflow client", client);
    check_not_null("msg-overflow server", server);
    if (!client || !server) {
        if (client) neverc_tcp_close(client);
        if (server) neverc_tcp_close(server);
        neverc_tcp_listener_close(ln);
        return;
    }

    neverc_ws_conn_t *ws = ws_test_server_handshake(server, client);
    check_not_null("msg-overflow server ws", ws);
    if (ws) {
        check_int("write first message fragment",
                  ws_write_masked_frame_ex(client, NC_WS_OPCODE_TEXT, 0,
                                           "hello", 5),
                  0);
        char mid[20];
        memset(mid, 'x', sizeof(mid));
        check_int("write overflowing mid fragment",
                  ws_write_masked_frame_ex(client, NC_WS_OPCODE_CONTINUATION, 0,
                                           mid, sizeof(mid)),
                  0);
        char small[8];
        size_t n = 0;
        check_int("read_message overflow",
                  neverc_ws_read_message(ws, small, sizeof(small), &n),
                  -1);

        check_int("write follow-up message",
                  ws_write_masked_frame(client, NC_WS_OPCODE_TEXT, "ok", 2),
                  0);
        char buf[16];
        n = 0;
        check_int("next message after overflow",
                  neverc_ws_read_message(ws, buf, sizeof(buf), &n),
                  0);
        check_int("follow-up message length", (int)n, 2);
        check_str("follow-up message", buf, "ok");
        neverc_ws_conn_free(ws);
    } else {
        neverc_tcp_close(server);
    }
    neverc_tcp_close(client);
    neverc_tcp_listener_close(ln);
}

static void test_close_half_closes_write(void) {
    printf("[close_half_closes_write]\n");
    const char *err = NULL;
    neverc_tcp_listener_t *ln = neverc_tcp_listen("127.0.0.1:0", &err);
    check_not_null("close-fin listen", ln);
    if (!ln) return;

    neverc_tcp_addr_t laddr;
    neverc_tcp_listener_addr(ln, &laddr);
    char addr[64];
    snprintf(addr, sizeof(addr), "127.0.0.1:%u", (unsigned)laddr.port);
    neverc_tcp_conn_t *client = neverc_tcp_dial(addr, &err);
    neverc_tcp_conn_t *server = neverc_tcp_accept(ln, &err);
    check_not_null("close-fin client", client);
    check_not_null("close-fin server", server);
    if (!client || !server) {
        if (client) neverc_tcp_close(client);
        if (server) neverc_tcp_close(server);
        neverc_tcp_listener_close(ln);
        return;
    }

    neverc_ws_conn_t *ws = ws_test_server_handshake(server, client);
    check_not_null("close-fin server ws", ws);
    if (ws) {
        uint8_t payload[] = { 0x03, 0xe8 };
        check_int("write close 1000",
                  ws_write_masked_frame(client, NC_WS_OPCODE_CLOSE,
                                        payload, sizeof(payload)),
                  0);
        int opcode = 0;
        char buf[32];
        size_t n = 0;
        check_int("read close frame",
                  neverc_ws_read_frame(ws, &opcode, NULL, buf, sizeof(buf),
                                       &n),
                  0);
        check_int("close opcode", opcode, NC_WS_OPCODE_CLOSE);
        uint8_t close_hdr[4];
        check_int("read close reply",
                  ws_tcp_read_exact(client, close_hdr, sizeof(close_hdr)),
                  0);
        check_int("close reply opcode", close_hdr[0], 0x88);
        check_int("close reply unmasked", (close_hdr[1] & 0x80) == 0, 1);
        neverc_tcp_set_read_timeout(client, 2000);
        char extra[8];
        check_int("TCP FIN after WebSocket close",
                  neverc_tcp_read(client, extra, sizeof(extra)), 0);
        neverc_ws_conn_free(ws);
    } else {
        neverc_tcp_close(server);
    }
    neverc_tcp_close(client);
    neverc_tcp_listener_close(ln);
}

static void test_frame_length_overflow(void) {
    printf("[frame_length_overflow]\n");
    const char *err = NULL;
    neverc_tcp_listener_t *ln = neverc_tcp_listen("127.0.0.1:0", &err);
    check_not_null("len-overflow listen", ln);
    if (!ln) return;

    neverc_tcp_addr_t laddr;
    neverc_tcp_listener_addr(ln, &laddr);
    char addr[64];
    snprintf(addr, sizeof(addr), "127.0.0.1:%u", (unsigned)laddr.port);
    neverc_tcp_conn_t *client = neverc_tcp_dial(addr, &err);
    neverc_tcp_conn_t *server = neverc_tcp_accept(ln, &err);
    check_not_null("len-overflow client", client);
    check_not_null("len-overflow server", server);
    if (!client || !server) {
        if (client) neverc_tcp_close(client);
        if (server) neverc_tcp_close(server);
        neverc_tcp_listener_close(ln);
        return;
    }

    neverc_ws_conn_t *ws = ws_test_server_handshake(server, client);
    check_not_null("len-overflow server ws", ws);
    if (ws) {
        /* RFC 6455: 64-bit length most significant bit MUST be 0. */
        uint8_t msb[14] = {
            0x82, 0xff,
            0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x37, 0xfa, 0x21, 0x3d
        };
        check_int("write 64-bit MSB length",
                  neverc_tcp_write(client, msb, sizeof(msb)) ==
                      (int)sizeof(msb),
                  1);
        int opcode = 0;
        char buf[16];
        size_t n = 0;
        check_int("reject 64-bit MSB length",
                  neverc_ws_read_frame(ws, &opcode, NULL, buf, sizeof(buf),
                                       &n),
                  -1);
        uint8_t close_hdr[4];
        check_int("MSB close header",
                  ws_tcp_read_exact(client, close_hdr, sizeof(close_hdr)),
                  0);
        check_int("MSB close opcode", close_hdr[0], 0x88);
        uint16_t code = (uint16_t)(((uint16_t)close_hdr[2] << 8) |
                                   close_hdr[3]);
        check_int("MSB close code 1002", code, 1002);
        neverc_ws_conn_free(ws);
    } else {
        neverc_tcp_close(server);
    }
    neverc_tcp_close(client);
    neverc_tcp_listener_close(ln);

    ln = neverc_tcp_listen("127.0.0.1:0", &err);
    check_not_null("nonminimal listen", ln);
    if (!ln) return;
    neverc_tcp_listener_addr(ln, &laddr);
    snprintf(addr, sizeof(addr), "127.0.0.1:%u", (unsigned)laddr.port);
    client = neverc_tcp_dial(addr, &err);
    server = neverc_tcp_accept(ln, &err);
    check_not_null("nonminimal client", client);
    check_not_null("nonminimal server", server);
    if (!client || !server) {
        if (client) neverc_tcp_close(client);
        if (server) neverc_tcp_close(server);
        neverc_tcp_listener_close(ln);
        return;
    }
    ws = ws_test_server_handshake(server, client);
    check_not_null("nonminimal server ws", ws);
    if (ws) {
        /* 16-bit length encoding of 5 is non-minimal and must be rejected. */
        uint8_t nonmin[8] = {
            0x82, 0xfe, 0x00, 0x05, 0x37, 0xfa, 0x21, 0x3d
        };
        check_int("write non-minimal 16-bit length",
                  neverc_tcp_write(client, nonmin, sizeof(nonmin)) ==
                      (int)sizeof(nonmin),
                  1);
        int opcode = 0;
        char buf[16];
        size_t n = 0;
        check_int("reject non-minimal 16-bit length",
                  neverc_ws_read_frame(ws, &opcode, NULL, buf, sizeof(buf),
                                       &n),
                  -1);
        neverc_ws_conn_free(ws);
    } else {
        neverc_tcp_close(server);
    }
    neverc_tcp_close(client);
    neverc_tcp_listener_close(ln);
}

static void test_fragment_exceeds_read_limit(void) {
    printf("[fragment_exceeds_read_limit]\n");
    const char *err = NULL;
    neverc_tcp_listener_t *ln = neverc_tcp_listen("127.0.0.1:0", &err);
    check_not_null("frag-limit listen", ln);
    if (!ln) return;

    neverc_tcp_addr_t laddr;
    neverc_tcp_listener_addr(ln, &laddr);
    char addr[64];
    snprintf(addr, sizeof(addr), "127.0.0.1:%u", (unsigned)laddr.port);
    neverc_tcp_conn_t *client = neverc_tcp_dial(addr, &err);
    neverc_tcp_conn_t *server = neverc_tcp_accept(ln, &err);
    check_not_null("frag-limit client", client);
    check_not_null("frag-limit server", server);
    if (!client || !server) {
        if (client) neverc_tcp_close(client);
        if (server) neverc_tcp_close(server);
        neverc_tcp_listener_close(ln);
        return;
    }

    neverc_ws_conn_t *ws = ws_test_server_handshake(server, client);
    check_not_null("frag-limit server ws", ws);
    if (ws) {
        check_int("set fragment read limit",
                  neverc_ws_set_read_limit(ws, 8), 0);
        check_int("write first binary fragment",
                  ws_write_masked_frame_ex(client, NC_WS_OPCODE_BINARY, 0,
                                           "12345", 5),
                  0);
        int opcode = 0;
        char buf[16];
        size_t n = 0;
        check_int("read first fragment",
                  neverc_ws_read_frame(ws, &opcode, NULL, buf, sizeof(buf),
                                       &n),
                  0);
        check_int("first fragment len", (int)n, 5);
        check_int("write overflowing continuation",
                  ws_write_masked_frame_ex(client, NC_WS_OPCODE_CONTINUATION, 1,
                                           "67890", 5),
                  0);
        check_int("reject assembled binary over limit",
                  neverc_ws_read_frame(ws, &opcode, NULL, buf, sizeof(buf),
                                       &n),
                  -1);
        uint8_t close_hdr[4];
        check_int("fragment close header",
                  ws_tcp_read_exact(client, close_hdr, sizeof(close_hdr)),
                  0);
        uint16_t code = (uint16_t)(((uint16_t)close_hdr[2] << 8) |
                                   close_hdr[3]);
        check_int("fragment close code 1009", code, 1009);
        neverc_ws_conn_free(ws);
    } else {
        neverc_tcp_close(server);
    }
    neverc_tcp_close(client);
    neverc_tcp_listener_close(ln);
}

static void test_control_frame_short_copy(void) {
    printf("[control_frame_short_copy]\n");
    const char *err = NULL;
    neverc_tcp_listener_t *ln = neverc_tcp_listen("127.0.0.1:0", &err);
    check_not_null("ctrl-copy listen", ln);
    if (!ln) return;

    neverc_tcp_addr_t laddr;
    neverc_tcp_listener_addr(ln, &laddr);
    char addr[64];
    snprintf(addr, sizeof(addr), "127.0.0.1:%u", (unsigned)laddr.port);
    neverc_tcp_conn_t *client = neverc_tcp_dial(addr, &err);
    neverc_tcp_conn_t *server = neverc_tcp_accept(ln, &err);
    check_not_null("ctrl-copy client", client);
    check_not_null("ctrl-copy server", server);
    if (!client || !server) {
        if (client) neverc_tcp_close(client);
        if (server) neverc_tcp_close(server);
        neverc_tcp_listener_close(ln);
        return;
    }

    neverc_ws_conn_t *ws = ws_test_server_handshake(server, client);
    check_not_null("ctrl-copy server ws", ws);
    if (ws) {
        check_int("write 8-byte ping",
                  ws_write_masked_frame(client, NC_WS_OPCODE_PING,
                                        "pingping", 8),
                  0);
        int opcode = 0;
        char tiny[2];
        size_t n = 99;
        check_int("read ping into small buffer",
                  neverc_ws_read_frame(ws, &opcode, NULL, tiny, sizeof(tiny),
                                       &n),
                  0);
        check_int("ping opcode", opcode, NC_WS_OPCODE_PING);
        check_int("ping out_len capped to buffer", (int)n, 2);
        uint8_t pong[10];
        check_int("read auto pong header",
                  ws_tcp_read_exact(client, pong, 2), 0);
        check_int("pong opcode", pong[0] & 0x0f, NC_WS_OPCODE_PONG);
        check_int("pong unmasked full payload", pong[1], 8);
        check_int("read auto pong body",
                  ws_tcp_read_exact(client, pong + 2, 8), 0);
        check_int("pong payload", memcmp(pong + 2, "pingping", 8) == 0, 1);
        neverc_ws_conn_free(ws);
    } else {
        neverc_tcp_close(server);
    }
    neverc_tcp_close(client);
    neverc_tcp_listener_close(ln);
}

static void test_reject_rsv_bits(void) {
    printf("[reject_rsv_bits]\n");
    const char *err = NULL;
    neverc_tcp_listener_t *ln = neverc_tcp_listen("127.0.0.1:0", &err);
    check_not_null("rsv listen", ln);
    if (!ln) return;

    neverc_tcp_addr_t laddr;
    neverc_tcp_listener_addr(ln, &laddr);
    char addr[64];
    snprintf(addr, sizeof(addr), "127.0.0.1:%u", (unsigned)laddr.port);
    neverc_tcp_conn_t *client = neverc_tcp_dial(addr, &err);
    neverc_tcp_conn_t *server = neverc_tcp_accept(ln, &err);
    check_not_null("rsv client", client);
    check_not_null("rsv server", server);
    if (!client || !server) {
        if (client) neverc_tcp_close(client);
        if (server) neverc_tcp_close(server);
        neverc_tcp_listener_close(ln);
        return;
    }

    neverc_ws_conn_t *ws = ws_test_server_handshake(server, client);
    check_not_null("rsv server ws", ws);
    if (ws) {
        /* RFC 6455 §5.2: RSV1-3 must be 0 unless an extension is negotiated. */
        uint8_t rsv1[] = {
            0xc1, 0x85, 0x37, 0xfa, 0x21, 0x3d,
            (uint8_t)('h' ^ 0x37), (uint8_t)('e' ^ 0xfa),
            (uint8_t)('l' ^ 0x21), (uint8_t)('l' ^ 0x3d),
            (uint8_t)('o' ^ 0x37)
        };
        check_int("write RSV1 text",
                  neverc_tcp_write(client, rsv1, sizeof(rsv1)) ==
                      (int)sizeof(rsv1),
                  1);
        int opcode = 0;
        char buf[16];
        size_t n = 0;
        check_int("reject RSV1",
                  neverc_ws_read_frame(ws, &opcode, NULL, buf, sizeof(buf),
                                       &n),
                  -1);
        uint8_t close_hdr[4];
        check_int("RSV close header",
                  ws_tcp_read_exact(client, close_hdr, sizeof(close_hdr)),
                  0);
        check_int("RSV close opcode", close_hdr[0], 0x88);
        uint16_t code = (uint16_t)(((uint16_t)close_hdr[2] << 8) |
                                   close_hdr[3]);
        check_int("RSV close code 1002", code, 1002);
        neverc_ws_conn_free(ws);
    } else {
        neverc_tcp_close(server);
    }
    neverc_tcp_close(client);
    neverc_tcp_listener_close(ln);
}

static void test_reject_fragmented_control(void) {
    printf("[reject_fragmented_control]\n");
    const char *err = NULL;
    neverc_tcp_listener_t *ln = neverc_tcp_listen("127.0.0.1:0", &err);
    check_not_null("frag-ctrl listen", ln);
    if (!ln) return;

    neverc_tcp_addr_t laddr;
    neverc_tcp_listener_addr(ln, &laddr);
    char addr[64];
    snprintf(addr, sizeof(addr), "127.0.0.1:%u", (unsigned)laddr.port);
    neverc_tcp_conn_t *client = neverc_tcp_dial(addr, &err);
    neverc_tcp_conn_t *server = neverc_tcp_accept(ln, &err);
    check_not_null("frag-ctrl client", client);
    check_not_null("frag-ctrl server", server);
    if (!client || !server) {
        if (client) neverc_tcp_close(client);
        if (server) neverc_tcp_close(server);
        neverc_tcp_listener_close(ln);
        return;
    }

    neverc_ws_conn_t *ws = ws_test_server_handshake(server, client);
    check_not_null("frag-ctrl server ws", ws);
    if (ws) {
        check_int("write fragmented ping",
                  ws_write_masked_frame_ex(client, NC_WS_OPCODE_PING, 0,
                                           "ping", 4),
                  0);
        int opcode = 0;
        char buf[16];
        size_t n = 0;
        check_int("reject fragmented ping",
                  neverc_ws_read_frame(ws, &opcode, NULL, buf, sizeof(buf),
                                       &n),
                  -1);
        uint8_t close_hdr[4];
        check_int("frag-ctrl close header",
                  ws_tcp_read_exact(client, close_hdr, sizeof(close_hdr)),
                  0);
        uint16_t code = (uint16_t)(((uint16_t)close_hdr[2] << 8) |
                                   close_hdr[3]);
        check_int("frag-ctrl close code 1002", code, 1002);
        neverc_ws_conn_free(ws);
    } else {
        neverc_tcp_close(server);
    }
    neverc_tcp_close(client);
    neverc_tcp_listener_close(ln);
}

static void test_reject_oversize_control(void) {
    printf("[reject_oversize_control]\n");
    const char *err = NULL;
    neverc_tcp_listener_t *ln = neverc_tcp_listen("127.0.0.1:0", &err);
    check_not_null("oversize-ctrl listen", ln);
    if (!ln) return;

    neverc_tcp_addr_t laddr;
    neverc_tcp_listener_addr(ln, &laddr);
    char addr[64];
    snprintf(addr, sizeof(addr), "127.0.0.1:%u", (unsigned)laddr.port);
    neverc_tcp_conn_t *client = neverc_tcp_dial(addr, &err);
    neverc_tcp_conn_t *server = neverc_tcp_accept(ln, &err);
    check_not_null("oversize-ctrl client", client);
    check_not_null("oversize-ctrl server", server);
    if (!client || !server) {
        if (client) neverc_tcp_close(client);
        if (server) neverc_tcp_close(server);
        neverc_tcp_listener_close(ln);
        return;
    }

    neverc_ws_conn_t *ws = ws_test_server_handshake(server, client);
    check_not_null("oversize-ctrl server ws", ws);
    if (ws) {
        /* 16-bit length 126 on a ping is both non-minimal and > 125. */
        uint8_t hdr[8] = {
            0x89, 0xfe, 0x00, 0x7e, 0x37, 0xfa, 0x21, 0x3d
        };
        check_int("write ping with 16-bit length 126",
                  neverc_tcp_write(client, hdr, sizeof(hdr)) ==
                      (int)sizeof(hdr),
                  1);
        int opcode = 0;
        char buf[16];
        size_t n = 0;
        check_int("reject control payload 126",
                  neverc_ws_read_frame(ws, &opcode, NULL, buf, sizeof(buf),
                                       &n),
                  -1);
        uint8_t close_hdr[4];
        check_int("oversize-ctrl close header",
                  ws_tcp_read_exact(client, close_hdr, sizeof(close_hdr)),
                  0);
        uint16_t code = (uint16_t)(((uint16_t)close_hdr[2] << 8) |
                                   close_hdr[3]);
        check_int("oversize-ctrl close code 1002", code, 1002);
        neverc_ws_conn_free(ws);
    } else {
        neverc_tcp_close(server);
    }
    neverc_tcp_close(client);
    neverc_tcp_listener_close(ln);
}

#ifndef _WIN32

static int tcp_read_exact(neverc_tcp_conn_t *conn, void *buf, size_t len) {
    char *p = (char *)buf;
    size_t total = 0;
    while (total < len) {
        int n = neverc_tcp_read(conn, p + total, len - total);
        if (n <= 0) return -1;
        total += (size_t)n;
    }
    return 0;
}

static void test_client_dial_and_masking(void) {
    printf("[client_dial_masking]\n");
    const char *err = NULL;
    neverc_tcp_listener_t *ln = neverc_tcp_listen("127.0.0.1:0", &err);
    check_not_null("client listen", ln);
    if (!ln) return;

    neverc_tcp_addr_t laddr;
    neverc_tcp_listener_addr(ln, &laddr);
    pid_t pid = fork();
    if (pid == 0) {
        char url[128];
        snprintf(url, sizeof(url), "ws://127.0.0.1:%u/chat?mode=test",
                 (unsigned)laddr.port);
        neverc_ws_conn_t *ws = neverc_ws_dial(url, NULL, &err);
        if (!ws) _exit(1);
        if (neverc_ws_set_timeout(ws, 5000) != 0 ||
            neverc_ws_write_message(ws, "client-mask") != 0) {
            neverc_ws_conn_free(ws);
            _exit(2);
        }
        char reply[64];
        size_t reply_len = 0;
        int rc = neverc_ws_read_message(ws, reply, sizeof(reply), &reply_len);
        neverc_ws_conn_free(ws);
        _exit(rc == 0 && reply_len == 9 && strcmp(reply, "server-ok") == 0
                  ? 0
                  : 3);
    }

    neverc_tcp_conn_t *conn = neverc_tcp_accept(ln, &err);
    check_not_null("client accept", conn);
    if (conn) {
        neverc_tcp_set_timeout(conn, 5000);
        char request[4096];
        int total = 0;
        while (total < (int)sizeof(request) - 1) {
            int n = neverc_tcp_read(conn, request + total,
                                    sizeof(request) - 1 - (size_t)total);
            if (n <= 0) break;
            total += n;
            request[total] = '\0';
            if (strstr(request, "\r\n\r\n")) break;
        }
        check_int("client target",
                  strstr(request, "GET /chat?mode=test HTTP/1.1\r\n") != NULL,
                  1);
        size_t consumed = 0;
        check_int("client handshake",
                  neverc_ws_handshake_server(conn, request, (size_t)total,
                                              &consumed),
                  0);

        uint8_t header[2];
        check_int("client frame header", tcp_read_exact(conn, header, 2), 0);
        size_t payload_len = header[1] & 0x7f;
        check_int("client frame masked", (header[1] & 0x80) != 0, 1);
        check_int("client frame opcode", header[0] & 0x0f, NC_WS_OPCODE_TEXT);
        check_int("client payload length", (int)payload_len, 11);
        uint8_t mask[4];
        char payload[64];
        if (payload_len < sizeof(payload) &&
            tcp_read_exact(conn, mask, sizeof(mask)) == 0 &&
            tcp_read_exact(conn, payload, payload_len) == 0) {
            for (size_t i = 0; i < payload_len; i++)
                payload[i] = (char)((uint8_t)payload[i] ^ mask[i % 4]);
            payload[payload_len] = '\0';
            check_str("client masked payload", payload, "client-mask");
        } else {
            check_int("client masked payload read", -1, 0);
        }

        neverc_ws_conn_t *server_ws = neverc_ws_conn_new(conn);
        check_not_null("client server ws", server_ws);
        if (server_ws) {
            check_int("client server reply",
                      neverc_ws_write_message(server_ws, "server-ok"), 0);
            neverc_ws_conn_free(server_ws);
        } else {
            neverc_tcp_close(conn);
        }
    }

    int status = 0;
    waitpid(pid, &status, 0);
    check_int("dial client ok",
              WIFEXITED(status) && WEXITSTATUS(status) == 0, 1);
    neverc_tcp_listener_close(ln);
}

static void test_reject_masked_server_frame(void) {
    printf("[reject_masked_server_frame]\n");
    const char *err = NULL;
    neverc_tcp_listener_t *ln = neverc_tcp_listen("127.0.0.1:0", &err);
    check_not_null("masked-server listen", ln);
    if (!ln) return;

    neverc_tcp_addr_t laddr;
    neverc_tcp_listener_addr(ln, &laddr);
    pid_t pid = fork();
    if (pid == 0) {
        char url[128];
        snprintf(url, sizeof(url), "ws://127.0.0.1:%u/ws",
                 (unsigned)laddr.port);
        neverc_ws_conn_t *ws = neverc_ws_dial(url, NULL, &err);
        if (!ws) _exit(1);
        if (neverc_ws_set_timeout(ws, 5000) != 0) {
            neverc_ws_conn_free(ws);
            _exit(2);
        }
        char msg[64];
        size_t n = 0;
        int rc = neverc_ws_read_message(ws, msg, sizeof(msg), &n);
        neverc_ws_conn_free(ws);
        _exit(rc == 0 ? 3 : 0);
    }

    neverc_tcp_conn_t *conn = neverc_tcp_accept(ln, &err);
    check_not_null("masked-server accept", conn);
    if (conn) {
        neverc_tcp_set_timeout(conn, 5000);
        char request[4096];
        int total = 0;
        while (total < (int)sizeof(request) - 1) {
            int n = neverc_tcp_read(conn, request + total,
                                    sizeof(request) - 1 - (size_t)total);
            if (n <= 0) break;
            total += n;
            request[total] = '\0';
            if (strstr(request, "\r\n\r\n")) break;
        }
        size_t consumed = 0;
        check_int("masked-server handshake",
                  neverc_ws_handshake_server(conn, request, (size_t)total,
                                             &consumed),
                  0);
        /* RFC 6455 §5.1: a client MUST close on a masked server frame. */
        check_int("write illegal masked server frame",
                  ws_write_masked_frame(conn, NC_WS_OPCODE_TEXT, "hello", 5),
                  0);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    check_int("client rejected masked server frame",
              WIFEXITED(status) && WEXITSTATUS(status) == 0, 1);
    if (conn) neverc_tcp_close(conn);
    neverc_tcp_listener_close(ln);
}

static void test_reject_server_extensions(void) {
    printf("[reject_server_extensions]\n");
    const char *err = NULL;
    neverc_tcp_listener_t *ln = neverc_tcp_listen("127.0.0.1:0", &err);
    check_not_null("extensions listen", ln);
    if (!ln) return;

    neverc_tcp_addr_t laddr;
    neverc_tcp_listener_addr(ln, &laddr);
    pid_t pid = fork();
    if (pid == 0) {
        char url[128];
        snprintf(url, sizeof(url), "ws://127.0.0.1:%u/ws",
                 (unsigned)laddr.port);
        neverc_ws_conn_t *ws = neverc_ws_dial(url, NULL, &err);
        if (ws) {
            neverc_ws_conn_free(ws);
            _exit(1);
        }
        _exit(0);
    }

    neverc_tcp_conn_t *conn = neverc_tcp_accept(ln, &err);
    check_not_null("extensions accept", conn);
    if (conn) {
        neverc_tcp_set_timeout(conn, 5000);
        char request[4096];
        int total = 0;
        while (total < (int)sizeof(request) - 1) {
            int n = neverc_tcp_read(conn, request + total,
                                    sizeof(request) - 1 - (size_t)total);
            if (n <= 0) break;
            total += n;
            request[total] = '\0';
            if (strstr(request, "\r\n\r\n")) break;
        }
        const char *key_line = strstr(request, "Sec-WebSocket-Key: ");
        char accept[64];
        int have_accept = 0;
        if (key_line) {
            const char *key = key_line + 19;
            if ((size_t)(request + total - key) >= 24) {
                char key_buf[25];
                memcpy(key_buf, key, 24);
                key_buf[24] = '\0';
                have_accept = neverc_ws_compute_accept(
                    key_buf, accept, sizeof(accept)) == 0;
            }
        }
        check_int("extensions client key", have_accept, 1);
        if (have_accept) {
            char response[512];
            int n = snprintf(
                response, sizeof(response),
                "HTTP/1.1 101 Switching Protocols\r\n"
                "Upgrade: websocket\r\n"
                "Connection: Upgrade\r\n"
                "Sec-WebSocket-Accept: %s\r\n"
                "Sec-WebSocket-Extensions: permessage-deflate\r\n"
                "\r\n",
                accept);
            if (n > 0)
                neverc_tcp_write(conn, response, (size_t)n);
        }
        neverc_tcp_close(conn);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    check_int("client rejected server extensions",
              WIFEXITED(status) && WEXITSTATUS(status) == 0, 1);
    neverc_tcp_listener_close(ln);
}

static int ws_client_send_masked_text(neverc_tcp_conn_t *conn, const char *msg) {
    size_t len = strlen(msg);
    if (len > 125) return -1;
    uint8_t mask[4] = {0x37, 0xfa, 0x21, 0x3d};
    uint8_t hdr[6];
    hdr[0] = 0x81;
    hdr[1] = (uint8_t)(0x80 | len);
    memcpy(hdr + 2, mask, 4);
    if (neverc_tcp_write(conn, hdr, 6) != 6) return -1;
    char *masked = (char *)malloc(len);
    if (!masked) return -1;
    for (size_t i = 0; i < len; i++)
        masked[i] = (char)(msg[i] ^ mask[i % 4]);
    int rc = neverc_tcp_write(conn, masked, len);
    free(masked);
    return rc == (int)len ? 0 : -1;
}

static void test_handshake_and_echo(void) {
    printf("[handshake_echo]\n");
    const char *err = NULL;

    neverc_tcp_listener_t *ln = neverc_tcp_listen("127.0.0.1:0", &err);
    check_not_null("listen", ln);
    if (!ln) return;

    neverc_tcp_addr_t laddr;
    neverc_tcp_listener_addr(ln, &laddr);

    pid_t pid = fork();
    if (pid == 0) {
        usleep(50000);
        char addr[64];
        snprintf(addr, sizeof(addr), "127.0.0.1:%d", laddr.port);
        neverc_tcp_conn_t *c = neverc_tcp_dial(addr, &err);
        if (!c) _exit(1);
        neverc_tcp_set_timeout(c, 5000);

        const char *req =
            "GET /ws HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
            "\r\n";
        neverc_tcp_write(c, req, strlen(req));

        char resp[512];
        int total = 0;
        while (total < (int)sizeof(resp) - 1) {
            int n = neverc_tcp_read(c, resp + total, sizeof(resp) - 1 - (size_t)total);
            if (n <= 0) _exit(2);
            total += n;
            resp[total] = '\0';
            if (strstr(resp, "\r\n\r\n")) break;
        }
        if (!strstr(resp, "101") || !strstr(resp, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo="))
            _exit(3);

        if (ws_client_send_masked_text(c, "hello ws") != 0) _exit(4);

        uint8_t fh[2];
        if (ws_tcp_read_exact(c, fh, 2) != 0) _exit(4);
        size_t plen = fh[1] & 0x7F;
        char body[64];
        if (plen > 0 && ws_tcp_read_exact(c, body, plen) != 0) _exit(5);
        body[plen] = '\0';

        neverc_tcp_close(c);
        _exit(strcmp(body, "echo:hello ws") == 0 ? 0 : 6);
    }

    neverc_tcp_conn_t *conn = neverc_tcp_accept(ln, &err);
    check_not_null("accept", conn);

    if (conn) {
        neverc_tcp_set_timeout(conn, 5000);
        char buf[1024];
        int total = 0;
        while (total < (int)sizeof(buf) - 1) {
            int n = neverc_tcp_read(conn, buf + total, sizeof(buf) - 1 - (size_t)total);
            if (n <= 0) break;
            total += n;
            if (total >= 4 && strstr(buf, "\r\n\r\n")) break;
        }

        size_t consumed = 0;
        check_int("handshake", neverc_ws_handshake_server(conn, buf, (size_t)total,
                                                           &consumed), 0);

        neverc_ws_conn_t *ws = neverc_ws_conn_new(conn);
        check_not_null("ws conn", ws);

        if (ws) {
            char msg[256];
            size_t mlen = 0;
            check_int("read msg", neverc_ws_read_message(ws, msg, sizeof(msg),
                                                          &mlen), 0);
            check_str("msg body", msg, "hello ws");

            char reply[64];
            snprintf(reply, sizeof(reply), "echo:%s", msg);
            check_int("write reply", neverc_ws_write_message(ws, reply), 0);

            neverc_ws_conn_free(ws);
        } else {
            neverc_tcp_close(conn);
        }
    }

    int status;
    waitpid(pid, &status, 0);
    check_int("client ok", WIFEXITED(status) && WEXITSTATUS(status) == 0, 1);

    neverc_tcp_listener_close(ln);
}

static int ws_client_send_masked_frame(neverc_tcp_conn_t *conn, int opcode,
                                        const void *payload, size_t len) {
    if (len > 125) return -1;
    uint8_t mask[4] = {0x37, 0xfa, 0x21, 0x3d};
    uint8_t hdr[6];
    hdr[0] = (uint8_t)(0x80 | (opcode & 0x0F));
    hdr[1] = (uint8_t)(0x80 | len);
    memcpy(hdr + 2, mask, 4);
    if (neverc_tcp_write(conn, hdr, 6) != 6) return -1;
    const char *p = (const char *)payload;
    char *masked = (char *)malloc(len);
    if (!masked) return -1;
    for (size_t i = 0; i < len; i++)
        masked[i] = (char)(p[i] ^ mask[i % 4]);
    int rc = neverc_tcp_write(conn, masked, len);
    free(masked);
    return rc == (int)len ? 0 : -1;
}

static void test_ping_pong(void) {
    printf("[ping_pong]\n");
    const char *err = NULL;

    neverc_tcp_listener_t *ln = neverc_tcp_listen("127.0.0.1:0", &err);
    check_not_null("ping listen", ln);
    if (!ln) return;

    neverc_tcp_addr_t laddr;
    neverc_tcp_listener_addr(ln, &laddr);

    pid_t pid = fork();
    if (pid == 0) {
        usleep(50000);
        char addr[64];
        snprintf(addr, sizeof(addr), "127.0.0.1:%d", laddr.port);
        neverc_tcp_conn_t *c = neverc_tcp_dial(addr, &err);
        if (!c) _exit(1);
        neverc_tcp_set_timeout(c, 5000);

        const char *req =
            "GET /ws HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
            "\r\n";
        neverc_tcp_write(c, req, strlen(req));

        char resp[512];
        int total = 0;
        while (total < (int)sizeof(resp) - 1) {
            int n = neverc_tcp_read(c, resp + total, sizeof(resp) - 1 - (size_t)total);
            if (n <= 0) _exit(2);
            total += n;
            if (strstr(resp, "\r\n\r\n")) break;
        }

        /* Client sends PING, expects server PONG */
        if (ws_client_send_masked_frame(c, NC_WS_OPCODE_PING, "ping", 4) != 0)
            _exit(3);

        uint8_t fh[2];
        if (ws_tcp_read_exact(c, fh, 2) != 0) _exit(4);
        int opcode = fh[0] & 0x0F;
        size_t plen = fh[1] & 0x7F;
        if (opcode != NC_WS_OPCODE_PONG) _exit(5);

        char payload[16];
        if (plen > 0 && ws_tcp_read_exact(c, payload, plen) != 0) _exit(6);

        neverc_tcp_close(c);
        _exit(plen == 4 && memcmp(payload, "ping", 4) == 0 ? 0 : 7);
    }

    neverc_tcp_conn_t *conn = neverc_tcp_accept(ln, &err);
    if (conn) {
        neverc_tcp_set_timeout(conn, 5000);
        char buf[1024];
        int total = 0;
        while (total < (int)sizeof(buf) - 1) {
            int n = neverc_tcp_read(conn, buf + total, sizeof(buf) - 1 - (size_t)total);
            if (n <= 0) break;
            total += n;
            if (strstr(buf, "\r\n\r\n")) break;
        }
        size_t consumed = 0;
        if (neverc_ws_handshake_server(conn, buf, (size_t)total, &consumed) != 0) {
            neverc_tcp_close(conn);
            conn = NULL;
        }
    }
    if (conn) {
        neverc_ws_conn_t *ws = neverc_ws_conn_new(conn);
        if (ws) {
            int opcode = 0, fin = 0;
            char ping[16];
            size_t plen = 0;
            check_int("read ping", neverc_ws_read_frame(ws, &opcode, &fin,
                ping, sizeof(ping), &plen), 0);
            check_int("ping opcode", opcode, NC_WS_OPCODE_PING);
            check_int("ping len", (int)plen, 4);
            neverc_ws_conn_free(ws);
        } else {
            neverc_tcp_close(conn);
        }
    }

    int status;
    waitpid(pid, &status, 0);
    check_int("automatic pong",
              WIFEXITED(status) && WEXITSTATUS(status) == 0, 1);

    neverc_tcp_listener_close(ln);
}

static void ws_http_handler(neverc_http_request_t *req,
                             neverc_http_response_writer_t *w) {
    neverc_ws_conn_t *ws = neverc_ws_upgrade_http(req, w);
    if (!ws) {
        neverc_http_set_status(w, 400);
        neverc_http_write_string(w, "bad websocket upgrade\n");
        return;
    }
    char msg[256];
    size_t mlen = 0;
    if (neverc_ws_read_message(ws, msg, sizeof(msg), &mlen) == 0) {
        char reply[280];
        snprintf(reply, sizeof(reply), "http+ws:%s", msg);
        neverc_ws_write_message(ws, reply);
    }
    neverc_ws_conn_free(ws);
}

static int get_free_port(void) {
    const char *err = NULL;
    neverc_tcp_listener_t *probe = neverc_tcp_listen("127.0.0.1:0", &err);
    if (!probe) return -1;
    neverc_tcp_addr_t pa;
    neverc_tcp_listener_addr(probe, &pa);
    int port = pa.port;
    neverc_tcp_listener_close(probe);
    return port;
}

static void test_http_ws_upgrade(void) {
    printf("[http_ws_upgrade]\n");
    int port = get_free_port();
    check_int("free port", port > 0, 1);
    if (port <= 0) return;

    pid_t server_pid = fork();
    if (server_pid == 0) {
        neverc_http_mux_t *mux = neverc_http_new_mux();
        neverc_http_mux_handle(mux, "/ws", ws_http_handler);
        char addr[32];
        snprintf(addr, sizeof(addr), "127.0.0.1:%d", port);
        neverc_http_listen_and_serve(addr, mux);
        _exit(0);
    }
    usleep(300000);

    const char *err = NULL;
    char addr[64];
    snprintf(addr, sizeof(addr), "127.0.0.1:%d", port);
    neverc_tcp_conn_t *c = neverc_tcp_dial(addr, &err);
    check_not_null("dial http", c);
    if (!c) {
        kill(server_pid, SIGTERM);
        waitpid(server_pid, NULL, 0);
        return;
    }
    neverc_tcp_set_timeout(c, 5000);

    const char *req =
        "GET /ws HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "\r\n";
    neverc_tcp_write(c, req, strlen(req));

    char resp[512];
    int total = 0;
    while (total < (int)sizeof(resp) - 1) {
        int n = neverc_tcp_read(c, resp + total, sizeof(resp) - 1 - (size_t)total);
        if (n <= 0) break;
        total += n;
        resp[total] = '\0';
        if (strstr(resp, "\r\n\r\n")) break;
    }
    check_int("101 resp", strstr(resp, "101") != NULL, 1);
    check_int("accept hdr", strstr(resp, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") != NULL, 1);

    check_int("send ws text", ws_client_send_masked_text(c, "via-http") == 0, 1);

    uint8_t fh[2];
    check_int("read frame hdr", ws_tcp_read_exact(c, fh, 2) == 0, 1);
    size_t plen = fh[1] & 0x7F;
    char body[64];
    if (plen > 0)
        check_int("read frame body", ws_tcp_read_exact(c, body, plen) == 0, 1);
    body[plen] = '\0';
    check_str("echo body", body, "http+ws:via-http");

    neverc_tcp_close(c);

    c = neverc_tcp_dial(addr, &err);
    check_not_null("dial http connection list", c);
    if (c) {
        neverc_tcp_set_timeout(c, 5000);
        const char *list_req =
            "GET /ws HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade , keep-alive\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
            "\r\n";
        neverc_tcp_write(c, list_req, strlen(list_req));
        total = 0;
        memset(resp, 0, sizeof(resp));
        while (total < (int)sizeof(resp) - 1) {
            int n = neverc_tcp_read(c, resp + total,
                                    sizeof(resp) - 1 - (size_t)total);
            if (n <= 0) break;
            total += n;
            resp[total] = '\0';
            if (strstr(resp, "\r\n\r\n")) break;
        }
        check_int("101 resp connection list", strstr(resp, "101") != NULL, 1);
        neverc_tcp_close(c);
    }

    c = neverc_tcp_dial(addr, &err);
    check_not_null("dial http reject", c);
    if (c) {
        neverc_tcp_set_timeout(c, 5000);
        const char *bad =
            "GET /ws HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Version: 130\r\n"
            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
            "\r\n";
        neverc_tcp_write(c, bad, strlen(bad));
        total = 0;
        memset(resp, 0, sizeof(resp));
        while (total < (int)sizeof(resp) - 1) {
            int n = neverc_tcp_read(c, resp + total,
                                    sizeof(resp) - 1 - (size_t)total);
            if (n <= 0) break;
            total += n;
            resp[total] = '\0';
            if (strstr(resp, "\r\n\r\n")) break;
        }
        check_int("reject version 130 upgrade",
                  strstr(resp, "101") == NULL, 1);
        neverc_tcp_close(c);
    }

    kill(server_pid, SIGTERM);
    waitpid(server_pid, NULL, 0);
}

#endif /* _WIN32 */

int main(void) {
    test_compute_accept();
    test_null_safety();
    test_utf8_prefix_validation();
    test_handshake_rejects();
    test_reject_unmasked_client_frame();
    test_close_code_message_too_big();
    test_close_invalid_utf8_reason_is_1007();
    test_local_buffer_too_small_keeps_stream();
    test_small_buffer_discards_fragment_keeps_stream();
    test_oversized_text_during_fragment_is_1002();
    test_oversized_stray_continuation_is_1002();
    test_read_message_overflow_clears_fragment();
    test_close_half_closes_write();
    test_frame_length_overflow();
    test_fragment_exceeds_read_limit();
    test_control_frame_short_copy();
    test_reject_rsv_bits();
    test_reject_fragmented_control();
    test_reject_oversize_control();
#ifndef _WIN32
    test_client_dial_and_masking();
    test_reject_masked_server_frame();
    test_reject_server_extensions();
    test_handshake_and_echo();
    test_ping_pong();
    test_http_ws_upgrade();
#endif

    printf("\n%d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf("\n");
    if (tests_failed == 0) puts("passed");
    return tests_failed > 0 ? 1 : 0;
}
