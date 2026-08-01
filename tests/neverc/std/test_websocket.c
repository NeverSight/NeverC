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

static int ws_client_send_masked_text(neverc_tcp_conn_t *conn, const char *msg) {
    size_t len = strlen(msg);
    if (len > 125) return -1;
    uint8_t mask[4] = {0x37, 0xfa, 0x21, 0x3d};
    uint8_t hdr[6];
    hdr[0] = 0x81;
    hdr[1] = (uint8_t)(0x80 | len);
    memcpy(hdr + 2, mask, 4);
    if (neverc_tcp_write(conn, hdr, 6) < 0) return -1;
    char *masked = (char *)malloc(len);
    if (!masked) return -1;
    for (size_t i = 0; i < len; i++)
        masked[i] = (char)(msg[i] ^ mask[i % 4]);
    int rc = neverc_tcp_write(conn, masked, len);
    free(masked);
    return rc < 0 ? -1 : 0;
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
        if (neverc_tcp_read(c, fh, 2) != 2) _exit(4);
        size_t plen = fh[1] & 0x7F;
        char body[64];
        if (plen > 0 && neverc_tcp_read(c, body, plen) != (int)plen) _exit(5);
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
    if (neverc_tcp_write(conn, hdr, 6) < 0) return -1;
    const char *p = (const char *)payload;
    char *masked = (char *)malloc(len);
    if (!masked) return -1;
    for (size_t i = 0; i < len; i++)
        masked[i] = (char)(p[i] ^ mask[i % 4]);
    int rc = neverc_tcp_write(conn, masked, len);
    free(masked);
    return rc < 0 ? -1 : 0;
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
        if (neverc_tcp_read(c, fh, 2) != 2) _exit(4);
        int opcode = fh[0] & 0x0F;
        size_t plen = fh[1] & 0x7F;
        if (opcode != NC_WS_OPCODE_PONG) _exit(5);

        char payload[16];
        if (plen > 0 && neverc_tcp_read(c, payload, plen) != (int)plen) _exit(6);

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
            check_int("send pong", neverc_ws_send_pong(ws, ping, plen), 0);
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
    check_int("read frame hdr", neverc_tcp_read(c, fh, 2) == 2, 1);
    size_t plen = fh[1] & 0x7F;
    char body[64];
    if (plen > 0)
        check_int("read frame body", neverc_tcp_read(c, body, plen) == (int)plen, 1);
    body[plen] = '\0';
    check_str("echo body", body, "http+ws:via-http");

    neverc_tcp_close(c);
    kill(server_pid, SIGTERM);
    waitpid(server_pid, NULL, 0);
}

#endif /* _WIN32 */

int main(void) {
    test_compute_accept();
    test_null_safety();
#ifndef _WIN32
    test_client_dial_and_masking();
    test_handshake_and_echo();
    test_ping_pong();
    test_http_ws_upgrade();
#endif

    printf("\n%d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf("\n");
    return tests_failed > 0 ? 1 : 0;
}
