#include "neverc/std/encoding/base64.h"
#include "neverc/std/net/smtp.h"
#include "neverc/std/net/tcp.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#endif

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

static void check_true(const char *name, int cond) {
    tests_run++;
    if (cond) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s\n", name); }
}

static void check_null(const char *name, const void *ptr) {
    tests_run++;
    if (!ptr) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: expected NULL\n", name); }
}

/* ===== Mock SMTP Server ===== */

static int g_smtp_port = 0;
static volatile int g_smtp_running = 1;
static volatile int g_smtp_ehlo_starttls = 0;
static volatile int g_smtp_login_leftover = 0;

#ifdef _WIN32
static DWORD WINAPI mock_smtp_server(LPVOID arg) {
#else
static void *mock_smtp_server(void *arg) {
#endif
    (void)arg;
    const char *err = NULL;
    char addr[32];
    snprintf(addr, sizeof(addr), "127.0.0.1:%d", g_smtp_port);

    neverc_tcp_listener_t *ln = neverc_tcp_listen(addr, &err);
    if (!ln) {
#ifdef _WIN32
        return 0;
#else
        return NULL;
#endif
    }

    while (g_smtp_running) {
        neverc_tcp_conn_t *conn = neverc_tcp_accept(ln, &err);
        if (!conn) break;

        neverc_tcp_set_timeout(conn, 5000);

        /* Send greeting */
        const char *greeting = "220 mock.smtp.test ESMTP\r\n";
        neverc_tcp_write(conn, greeting, strlen(greeting));

        char buf[4096];
        while (g_smtp_running) {
            int n = neverc_tcp_read(conn, buf, sizeof(buf) - 1);
            if (n <= 0) break;
            buf[n] = '\0';

            if (strncmp(buf, "EHLO", 4) == 0) {
                /* Final line is "250" with no SP-text (RFC 5321 §4.2).
                 * noauth.local advertises no AUTH; HELP text names PLAIN so a
                 * cross-line strstr would fail-open. plainonly.local omits LOGIN.
                 * starttls.local advertises STARTTLS so AUTH/send_mail must
                 * fail closed instead of sending passwords in cleartext. */
                const char *resp =
                    strstr(buf, "noauth.local")
                    ? "250-mock.smtp.test\r\n"
                      "250-8BITMIME\r\n"
                      "250 HELP AUTH PLAIN LOGIN is documented\r\n"
                    : strstr(buf, "plainonly.local")
                    ? "250-mock.smtp.test\r\n"
                      "250-AUTH PLAIN\r\n"
                      "250 8BITMIME\r\n"
                    : (strstr(buf, "starttls.local") || g_smtp_ehlo_starttls)
                    ? "250-mock.smtp.test\r\n"
                      "250-STARTTLS\r\n"
                      "250-AUTH PLAIN LOGIN\r\n"
                      "250\r\n"
                    : "250-mock.smtp.test\r\n"
                      "250-8BITMIME\r\n"
                      "250-AUTH PLAIN LOGIN\r\n"
                      "250\r\n";
                neverc_tcp_write(conn, resp, strlen(resp));
            } else if (strncmp(buf, "HELO", 4) == 0) {
                const char *resp = "250 Hello\r\n";
                neverc_tcp_write(conn, resp, strlen(resp));
            } else if (strncmp(buf, "AUTH PLAIN ", 11) == 0) {
                uint8_t raw[10];
                raw[0] = 0;
                memcpy(raw + 1, "user", 4);
                raw[5] = 0;
                memcpy(raw + 6, "pass", 4);
                char expected[32];
                size_t elen = neverc_base64_encode(expected, raw, sizeof(raw));
                const char *got = buf + 11;
                const char *cr = strstr(got, "\r\n");
                size_t glen = cr ? (size_t)(cr - got) : strlen(got);
                const char *resp = (elen != (size_t)-1 && elen == glen &&
                                    memcmp(got, expected, glen) == 0)
                    ? "235 Authentication successful\r\n"
                    : "535 invalid credentials\r\n";
                neverc_tcp_write(conn, resp, strlen(resp));
            } else if (strncmp(buf, "AUTH LOGIN", 10) == 0) {
                if (g_smtp_login_leftover) {
                    const char *resp = "334 VXNlcm5hbWU6\r\n235 leftover\r\n";
                    neverc_tcp_write(conn, resp, strlen(resp));
                    continue;
                }
                const char *resp = "334 VXNlcm5hbWU6\r\n";
                neverc_tcp_write(conn, resp, strlen(resp));
                n = neverc_tcp_read(conn, buf, sizeof(buf) - 1);
                if (n <= 0) break;
                buf[n] = '\0';
                char user_b64[16];
                size_t ulen = neverc_base64_encode(
                    user_b64, (const uint8_t *)"user", 4);
                const char *cr = strstr(buf, "\r\n");
                size_t glen = cr ? (size_t)(cr - buf) : strlen(buf);
                if (ulen != glen || memcmp(buf, user_b64, glen) != 0) {
                    resp = "535 invalid credentials\r\n";
                    neverc_tcp_write(conn, resp, strlen(resp));
                    continue;
                }
                resp = "334 UGFzc3dvcmQ6\r\n";
                neverc_tcp_write(conn, resp, strlen(resp));
                n = neverc_tcp_read(conn, buf, sizeof(buf) - 1);
                if (n <= 0) break;
                buf[n] = '\0';
                char pass_b64[16];
                size_t plen = neverc_base64_encode(
                    pass_b64, (const uint8_t *)"pass", 4);
                cr = strstr(buf, "\r\n");
                glen = cr ? (size_t)(cr - buf) : strlen(buf);
                resp = (plen == glen && memcmp(buf, pass_b64, glen) == 0)
                    ? "235 Authentication successful\r\n"
                    : "535 invalid credentials\r\n";
                neverc_tcp_write(conn, resp, strlen(resp));
            } else if (strncmp(buf, "STARTTLS", 8) == 0) {
                const char *resp = "220 Ready to start TLS\r\n";
                neverc_tcp_write(conn, resp, strlen(resp));
                /* Consume ClientHello, then drop the socket so the TLS
                 * handshake fails closed without waiting out SMTP timeouts. */
                (void)neverc_tcp_read(conn, buf, sizeof(buf) - 1);
                break;
            } else if (strncmp(buf, "MAIL FROM:", 10) == 0) {
                const char *resp = strstr(buf, "leftover@")
                    ? "250 OK\r\n500 leftover-injected\r\n"
                    : strstr(buf, "mismatch@")
                    /* RFC 5321: every reply-line must share the same code.
                     * A 250- prefix plus a 550 final line is not success. */
                    ? "250-looks-ok\r\n550 command rejected\r\n"
                    : "250 OK\r\n";
                neverc_tcp_write(conn, resp, strlen(resp));
            } else if (strncmp(buf, "RCPT TO:", 8) == 0) {
                const char *resp = "250 OK\r\n";
                neverc_tcp_write(conn, resp, strlen(resp));
            } else if (strncmp(buf, "DATA", 4) == 0) {
                const char *resp = "354 Start mail input\r\n";
                neverc_tcp_write(conn, resp, strlen(resp));
                /* Detect "\r\n.\r\n" or a leading ".\r\n" across reads.
                 * strstr on one TCP chunk misses a terminator split as
                 * "...\r\n" then ".\r\n". */
                char window[5];
                size_t wlen = 0;
                int saw_term = 0;
                while (!saw_term) {
                    n = neverc_tcp_read(conn, buf, sizeof(buf) - 1);
                    if (n <= 0) break;
                    for (int i = 0; i < n; i++) {
                        if (wlen == sizeof(window)) {
                            memmove(window, window + 1, sizeof(window) - 1);
                            wlen--;
                        }
                        window[wlen++] = buf[i];
                        if (wlen == 3 && memcmp(window, ".\r\n", 3) == 0) {
                            saw_term = 1;
                            break;
                        }
                        if (wlen == 5 &&
                            memcmp(window, "\r\n.\r\n", 5) == 0) {
                            saw_term = 1;
                            break;
                        }
                    }
                }
                resp = "250 Message accepted\r\n";
                neverc_tcp_write(conn, resp, strlen(resp));
            } else if (strncmp(buf, "RSET", 4) == 0) {
                const char *resp = "250 OK\r\n";
                neverc_tcp_write(conn, resp, strlen(resp));
            } else if (strncmp(buf, "NOOP", 4) == 0) {
                const char *resp = "250 OK\r\n";
                neverc_tcp_write(conn, resp, strlen(resp));
            } else if (strncmp(buf, "QUIT", 4) == 0) {
                const char *resp = "221 Bye\r\n";
                neverc_tcp_write(conn, resp, strlen(resp));
                break;
            } else {
                const char *resp = "500 Unknown command\r\n";
                neverc_tcp_write(conn, resp, strlen(resp));
            }
        }

        neverc_tcp_close(conn);
    }

    neverc_tcp_listener_close(ln);
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

/* ===== Tests ===== */

static void test_dial_invalid(void) {
    printf("[dial_invalid]\n");
    const char *err = NULL;
    neverc_smtp_client_t *c = neverc_smtp_dial("invalid:99999", &err);
    check_null("invalid dial", c);
    check_true("has error", err != NULL);
}

static void test_smtp_session(void) {
    printf("[smtp_session]\n");

    char addr[32];
    snprintf(addr, sizeof(addr), "127.0.0.1:%d", g_smtp_port);

    const char *err = NULL;
    neverc_smtp_client_t *c = neverc_smtp_dial(addr, &err);
    check_true("dial success", c != NULL);
    if (!c) return;

    /* EHLO */
    int rc = neverc_smtp_hello(c, "test.client");
    check_true("EHLO success", rc == 0);

    /* AUTH PLAIN */
    rc = neverc_smtp_auth(c, NEVERC_SMTP_AUTH_PLAIN, "user", "pass");
    check_true("AUTH PLAIN success", rc == 0);

    /* MAIL FROM */
    rc = neverc_smtp_mail(c, "sender@example.com");
    check_true("MAIL FROM success", rc == 0);

    /* RFC 5321 null reverse-path. */
    rc = neverc_smtp_reset(c);
    check_true("RSET before null reverse-path", rc == 0);
    rc = neverc_smtp_mail(c, "");
    check_true("MAIL FROM null reverse-path", rc == 0);
    rc = neverc_smtp_rcpt(c, "recipient@example.com");
    check_true("RCPT after null reverse-path", rc == 0);
    rc = neverc_smtp_reset(c);
    check_true("RSET after null reverse-path", rc == 0);
    rc = neverc_smtp_mail(c, "sender@example.com");
    check_true("MAIL FROM after null reverse-path", rc == 0);

    /* RCPT TO */
    rc = neverc_smtp_rcpt(c, "recipient@example.com");
    check_true("RCPT TO success", rc == 0);

    /* DATA */
    rc = neverc_smtp_data(c);
    check_true("DATA start", rc == 0);

    const char *msg =
        "From: sender@example.com\r\n"
        "To: recipient@example.com\r\n"
        "Subject: Test\r\n"
        "\r\n"
        "Hello, World!\r\n";
    rc = neverc_smtp_write_data(c, msg, strlen(msg));
    check_true("write data", rc == 0);

    rc = neverc_smtp_data_close(c);
    check_true("DATA close", rc == 0);

    /* RSET */
    rc = neverc_smtp_reset(c);
    check_true("RSET", rc == 0);

    /* NOOP */
    rc = neverc_smtp_noop(c);
    check_true("NOOP", rc == 0);

    /* last_response */
    const char *resp = neverc_smtp_last_response(c);
    check_true("last response not null", resp != NULL);

    neverc_smtp_close(c);
}

static void test_smtp_auth_login(void) {
    printf("[smtp_auth_login]\n");

    char addr[32];
    snprintf(addr, sizeof(addr), "127.0.0.1:%d", g_smtp_port);

    const char *err = NULL;
    neverc_smtp_client_t *c = neverc_smtp_dial(addr, &err);
    check_true("dial for login", c != NULL);
    if (!c) return;

    int rc = neverc_smtp_auth(c, NEVERC_SMTP_AUTH_LOGIN, "user", "pass");
    check_true("AUTH LOGIN success", rc == 0);

    neverc_smtp_close(c);
}

static void test_send_mail(void) {
    printf("[send_mail]\n");

    char addr[32];
    snprintf(addr, sizeof(addr), "127.0.0.1:%d", g_smtp_port);

    const char *to[] = {"alice@example.com", "bob@example.com"};
    const char *msg =
        "From: sender@example.com\r\n"
        "To: alice@example.com, bob@example.com\r\n"
        "Subject: Batch Test\r\n"
        "\r\n"
        "Batch mail body\r\n";

    const char *err = NULL;
    int rc = neverc_smtp_send_mail(
        addr, NEVERC_SMTP_AUTH_PLAIN, "user", "pass",
        "sender@example.com", to, 2,
        msg, strlen(msg), &err);

    check_true("send_mail success", rc == 0);
}

static void test_dot_stuffing(void) {
    printf("[dot_stuffing]\n");

    char addr[32];
    snprintf(addr, sizeof(addr), "127.0.0.1:%d", g_smtp_port);

    const char *err = NULL;
    neverc_smtp_client_t *c = neverc_smtp_dial(addr, &err);
    check_true("dial for dot stuffing", c != NULL);
    if (!c) return;

    check_true("EHLO", neverc_smtp_hello(c, "test.client") == 0);
    check_true("MAIL FROM", neverc_smtp_mail(c, "sender@example.com") == 0);
    check_true("RCPT TO", neverc_smtp_rcpt(c, "recipient@example.com") == 0);
    check_true("DATA", neverc_smtp_data(c) == 0);

    const char *msg =
        "Subject: dot test\r\n"
        "\r\n"
        ".\r\n"
        "..second line\r\n";
    check_true("write dotted body", neverc_smtp_write_data(c, msg, strlen(msg)) == 0);
    check_true("DATA close after dot stuffing",
               neverc_smtp_data_close(c) == 0);

    neverc_smtp_close(c);
}

static void test_smtp_reject_injection(void) {
    printf("[smtp_reject_injection]\n");

    char addr[32];
    snprintf(addr, sizeof(addr), "127.0.0.1:%d", g_smtp_port);
    const char *err = NULL;
    neverc_smtp_client_t *c = neverc_smtp_dial(addr, &err);
    check_true("dial for injection tests", c != NULL);
    if (!c) return;

    check_true("hello crlf rejected",
               neverc_smtp_hello(c, "host\r\nMAIL FROM:<x>") == -1);
    check_true("hello lf rejected",
               neverc_smtp_hello(c, "host\nMAIL FROM:<x>") == -1);
    check_true("mail crlf rejected",
               neverc_smtp_mail(c, "a@b.com\r\nRCPT TO:<x@y>") == -1);
    check_true("mail cr rejected",
               neverc_smtp_mail(c, "a@b.com\rRCPT TO:<x@y>") == -1);
    check_true("mail angle rejected",
               neverc_smtp_mail(c, "a@b.com>") == -1);
    check_true("mail source route rejected",
               neverc_smtp_mail(c, "@evil.com:user@x.com") == -1);
    check_true("mail colon source route rejected",
               neverc_smtp_mail(c, "user@x.com:relay") == -1);
    check_true("mail percent-hack rejected",
               neverc_smtp_mail(c, "user%evil.com@x.com") == -1);
    check_true("mail bang-path rejected",
               neverc_smtp_mail(c, "evil.com!user@x.com") == -1);
    check_true("mail extra at-sign rejected",
               neverc_smtp_mail(c, "a@b@c.com") == -1);
    check_true("hello unclosed ipv6 literal rejected",
               neverc_smtp_hello(c, "[2001:db8::1") == -1);
    check_true("hello leftover after literal rejected",
               neverc_smtp_hello(c, "[2001:db8::1]leftover") == -1);
    check_true("mail leftover after literal rejected",
               neverc_smtp_mail(c, "user@[192.168.1.1]smuggle") == -1);
    check_true("mail recipient list rejected",
               neverc_smtp_mail(c, "a@b.com,c@d.com") == -1);
    check_true("rcpt crlf rejected",
               neverc_smtp_rcpt(c, "victim@x.com\r\nMAIL FROM:<evil>") == -1);
    neverc_smtp_close(c);

    const char *to[] = {"ok@example.com"};
    check_true("send_mail rejects injected from",
               neverc_smtp_send_mail(addr, NEVERC_SMTP_AUTH_NONE, NULL, NULL,
                                     "from@x.com\r\nRSET", to, 1,
                                     "x", 1, &err) == -1);
    const char *injected_to[] = {"ok@example.com\r\nRCPT TO:<evil@x.com>"};
    check_true("send_mail rejects injected recipient",
               neverc_smtp_send_mail(addr, NEVERC_SMTP_AUTH_NONE, NULL, NULL,
                                     "from@x.com", injected_to, 1,
                                     "x", 1, &err) == -1);

    neverc_smtp_client_t *auth = neverc_smtp_dial(addr, &err);
    check_true("dial for auth overflow", auth != NULL);
    if (auth) {
        char huge[600];
        memset(huge, 'u', sizeof(huge) - 1);
        huge[sizeof(huge) - 1] = '\0';
        check_true("AUTH PLAIN rejects oversized username",
                   neverc_smtp_auth(auth, NEVERC_SMTP_AUTH_PLAIN, huge, "pw") == -1);
        check_true("AUTH LOGIN rejects oversized username",
                   neverc_smtp_auth(auth, NEVERC_SMTP_AUTH_LOGIN, huge, "pw") == -1);
        neverc_smtp_close(auth);
    }

    neverc_smtp_client_t *login_empty = neverc_smtp_dial(addr, &err);
    check_true("dial for empty AUTH LOGIN password", login_empty != NULL);
    if (login_empty) {
        /* smtp_cmd rejects empty commands, but AUTH LOGIN continuations
         * are blank CRLF when the credential is empty. Failing to write
         * that line leaves the server in AUTH and the next MAIL FROM is
         * consumed as the password. */
        check_true("AUTH LOGIN empty password rejected",
                   neverc_smtp_auth(login_empty, NEVERC_SMTP_AUTH_LOGIN,
                                    "user", "") == -1);
        check_true("MAIL FROM after empty AUTH LOGIN password",
                   neverc_smtp_mail(login_empty, "sender@example.com") == 0);
        neverc_smtp_close(login_empty);
    }

    login_empty = neverc_smtp_dial(addr, &err);
    check_true("dial for empty AUTH LOGIN username", login_empty != NULL);
    if (login_empty) {
        check_true("AUTH LOGIN empty username rejected",
                   neverc_smtp_auth(login_empty, NEVERC_SMTP_AUTH_LOGIN,
                                    "", "pass") == -1);
        check_true("MAIL FROM after empty AUTH LOGIN username",
                   neverc_smtp_mail(login_empty, "sender@example.com") == 0);
        neverc_smtp_close(login_empty);
    }

    neverc_smtp_client_t *v6 = neverc_smtp_dial(addr, &err);
    check_true("dial for ipv6 literals", v6 != NULL);
    if (v6) {
        check_true("EHLO IPv6 literal",
                   neverc_smtp_hello(v6, "[2001:db8::1]") == 0);
        check_true("MAIL FROM IPv6 address-literal",
                   neverc_smtp_mail(v6, "user@[IPv6:2001:db8::1]") == 0);
        neverc_smtp_close(v6);
    }

    check_true("write_data null client",
               neverc_smtp_write_data(NULL, "x", 1) == -1);

    neverc_smtp_client_t *idle = neverc_smtp_dial(addr, &err);
    check_true("dial for data-state", idle != NULL);
    if (idle) {
        check_true("write_data before DATA",
                   neverc_smtp_write_data(idle, "x", 1) == -1);
        check_true("data_close before DATA",
                   neverc_smtp_data_close(idle) == -1);
        check_true("EHLO for null write",
                   neverc_smtp_hello(idle, "test.client") == 0);
        check_true("MAIL for null write",
                   neverc_smtp_mail(idle, "sender@example.com") == 0);
        check_true("RCPT for null write",
                   neverc_smtp_rcpt(idle, "recipient@example.com") == 0);
        check_true("DATA for null write", neverc_smtp_data(idle) == 0);
        check_true("write_data null nonzero",
                   neverc_smtp_write_data(idle, NULL, 4) == -1);
        check_true("reset during DATA rejected",
                   neverc_smtp_reset(idle) == -1);
        check_true("noop during DATA rejected",
                   neverc_smtp_noop(idle) == -1);
        check_true("mail during DATA rejected",
                   neverc_smtp_mail(idle, "other@example.com") == -1);
        check_true("quit during DATA rejected",
                   neverc_smtp_quit(idle) == -1);
        check_true("write_data after rejected commands",
                   neverc_smtp_write_data(idle, "ok\r\n", 4) == 0);
        check_true("data_close after rejected commands",
                   neverc_smtp_data_close(idle) == 0);
        neverc_smtp_close(idle);
    }
}

static void test_smtp_starttls_fail_closed(void) {
    printf("[smtp_starttls_fail_closed]\n");

    char addr[32];
    snprintf(addr, sizeof(addr), "127.0.0.1:%d", g_smtp_port);
    const char *err = NULL;

    neverc_smtp_client_t *c = neverc_smtp_dial(addr, &err);
    check_true("dial for STARTTLS AUTH", c != NULL);
    if (!c) return;

    check_true("EHLO advertises STARTTLS",
               neverc_smtp_hello(c, "starttls.local") == 0);
    check_true("AUTH PLAIN fail-closed before STARTTLS",
               neverc_smtp_auth(c, NEVERC_SMTP_AUTH_PLAIN, "user", "pass") == -1);
    check_true("AUTH LOGIN fail-closed before STARTTLS",
               neverc_smtp_auth(c, NEVERC_SMTP_AUTH_LOGIN, "user", "pass") == -1);
    check_true("MAIL FROM still allowed without AUTH",
               neverc_smtp_mail(c, "sender@example.com") == 0);
    neverc_smtp_close(c);

    c = neverc_smtp_dial(addr, &err);
    check_true("dial for STARTTLS handshake", c != NULL);
    if (!c) return;
    check_true("EHLO before STARTTLS command",
               neverc_smtp_hello(c, "starttls.local") == 0);
    check_true("STARTTLS handshake fail-closed",
               neverc_smtp_starttls(c, NULL) == -1);
    check_true("AUTH after failed STARTTLS still closed",
               neverc_smtp_auth(c, NEVERC_SMTP_AUTH_PLAIN, "user", "pass") == -1);
    check_true("MAIL after failed STARTTLS handshake rejected",
               neverc_smtp_mail(c, "sender@example.com") == -1);
    neverc_smtp_close(c);

    c = neverc_smtp_dial(addr, &err);
    check_true("dial for STARTTLS unadvertised", c != NULL);
    if (!c) return;
    check_true("EHLO without STARTTLS",
               neverc_smtp_hello(c, "test.client") == 0);
    check_true("STARTTLS rejected when unadvertised",
               neverc_smtp_starttls(c, NULL) == -1);
    check_true("AUTH PLAIN after unadvertised STARTTLS",
               neverc_smtp_auth(c, NEVERC_SMTP_AUTH_PLAIN, "user", "pass") == 0);
    neverc_smtp_close(c);

    const char *to[] = {"alice@example.com"};
    const char *msg = "Subject: x\r\n\r\nbody\r\n";
    g_smtp_ehlo_starttls = 1;
    int rc = neverc_smtp_send_mail(
        addr, NEVERC_SMTP_AUTH_PLAIN, "user", "pass",
        "sender@example.com", to, 1, msg, strlen(msg), &err);
    g_smtp_ehlo_starttls = 0;
    check_true("send_mail AUTH fail-closed when STARTTLS advertised", rc == -1);

    g_smtp_ehlo_starttls = 1;
    rc = neverc_smtp_send_mail(
        addr, NEVERC_SMTP_AUTH_NONE, NULL, NULL,
        "sender@example.com", to, 1, msg, strlen(msg), &err);
    g_smtp_ehlo_starttls = 0;
    check_true("send_mail fail-closed STARTTLS without AUTH", rc == -1);
}

static void test_smtp_auth_plaintext_non_localhost(void) {
    printf("[smtp_auth_plaintext_non_localhost]\n");

    /* Go smtp.PlainAuth: EHLO is untrusted without TLS. Dialing via a
     * name that still reaches the loopback listener ("LocalHost" is not
     * isLocalhost) must not send PLAIN/LOGIN credentials in the clear. */
    char addr[64];
    snprintf(addr, sizeof(addr), "LocalHost:%d", g_smtp_port);
    const char *err = NULL;
    neverc_smtp_client_t *c = neverc_smtp_dial(addr, &err);
    check_true("dial LocalHost for plaintext AUTH", c != NULL);
    if (!c) return;

    check_true("AUTH PLAIN fail-closed off localhost without TLS",
               neverc_smtp_auth(c, NEVERC_SMTP_AUTH_PLAIN, "user", "pass") == -1);
    check_true("AUTH LOGIN fail-closed off localhost without TLS",
               neverc_smtp_auth(c, NEVERC_SMTP_AUTH_LOGIN, "user", "pass") == -1);
    check_true("MAIL FROM still allowed after rejected plaintext AUTH",
               neverc_smtp_mail(c, "sender@example.com") == 0);
    neverc_smtp_close(c);

    const char *to[] = {"alice@example.com"};
    const char *msg = "Subject: x\r\n\r\nbody\r\n";
    check_true("send_mail AUTH fail-closed off localhost without TLS",
               neverc_smtp_send_mail(addr, NEVERC_SMTP_AUTH_PLAIN, "user",
                                     "pass", "sender@example.com", to, 1,
                                     msg, strlen(msg), &err) == -1);
}

static void test_smtp_auth_requires_advertised(void) {
    printf("[smtp_auth_requires_advertised]\n");

    char addr[32];
    snprintf(addr, sizeof(addr), "127.0.0.1:%d", g_smtp_port);
    const char *err = NULL;
    neverc_smtp_client_t *c = neverc_smtp_dial(addr, &err);
    check_true("dial for auth advertisement", c != NULL);
    if (!c) return;

    check_true("EHLO without AUTH",
               neverc_smtp_hello(c, "noauth.local") == 0);
    check_true("AUTH PLAIN rejected when unadvertised",
               neverc_smtp_auth(c, NEVERC_SMTP_AUTH_PLAIN, "user", "pass") == -1);
    check_true("AUTH LOGIN rejected when unadvertised",
               neverc_smtp_auth(c, NEVERC_SMTP_AUTH_LOGIN, "user", "pass") == -1);
    check_true("MAIL FROM after rejected AUTH",
               neverc_smtp_mail(c, "sender@example.com") == 0);
    neverc_smtp_close(c);

    c = neverc_smtp_dial(addr, &err);
    check_true("dial for PLAIN-only AUTH", c != NULL);
    if (!c) return;
    check_true("EHLO PLAIN only",
               neverc_smtp_hello(c, "plainonly.local") == 0);
    check_true("AUTH LOGIN rejected when only PLAIN advertised",
               neverc_smtp_auth(c, NEVERC_SMTP_AUTH_LOGIN, "user", "pass") == -1);
    check_true("AUTH PLAIN allowed when advertised",
               neverc_smtp_auth(c, NEVERC_SMTP_AUTH_PLAIN, "user", "pass") == 0);
    neverc_smtp_close(c);
}

static void test_smtp_multiline_code_mismatch(void) {
    printf("[smtp_multiline_code_mismatch]\n");

    char addr[32];
    snprintf(addr, sizeof(addr), "127.0.0.1:%d", g_smtp_port);
    const char *err = NULL;
    neverc_smtp_client_t *c = neverc_smtp_dial(addr, &err);
    check_true("dial for multiline mismatch", c != NULL);
    if (!c) return;

    check_true("EHLO before mismatch",
               neverc_smtp_hello(c, "test.client") == 0);
    check_true("MAIL rejects 250- then 550",
               neverc_smtp_mail(c, "mismatch@example.com") == -1);
    neverc_smtp_close(c);
}

static void test_smtp_response_leftover(void) {
    printf("[smtp_response_leftover]\n");

    char addr[32];
    snprintf(addr, sizeof(addr), "127.0.0.1:%d", g_smtp_port);
    const char *err = NULL;
    neverc_smtp_client_t *c = neverc_smtp_dial(addr, &err);
    check_true("dial for leftover", c != NULL);
    if (!c) return;

    check_true("EHLO before leftover",
               neverc_smtp_hello(c, "test.client") == 0);
    check_true("MAIL leftover accepted",
               neverc_smtp_mail(c, "leftover@example.com") == 0);
    check_true("RCPT consumes leftover 500",
               neverc_smtp_rcpt(c, "victim@example.com") == -1);
    neverc_smtp_close(c);

    g_smtp_login_leftover = 1;
    c = neverc_smtp_dial(addr, &err);
    check_true("dial for AUTH LOGIN leftover", c != NULL);
    if (c) {
        check_true("EHLO before AUTH LOGIN leftover",
                   neverc_smtp_hello(c, "test.client") == 0);
        check_true("AUTH LOGIN leftover after 334 rejected",
                   neverc_smtp_auth(c, NEVERC_SMTP_AUTH_LOGIN,
                                    "user", "pass") == -1);
        check_true("MAIL after AUTH LOGIN leftover is dead",
                   neverc_smtp_mail(c, "ok@example.com") == -1);
        neverc_smtp_close(c);
    }
    g_smtp_login_leftover = 0;
}

int main(void) {
    printf("=== NeverC SMTP tests ===\n");

#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif

    /* Start mock SMTP server */
    const char *err = NULL;
    neverc_tcp_listener_t *ln = neverc_tcp_listen("127.0.0.1:0", &err);
    if (!ln) {
        printf("FAIL: cannot create listener: %s\n", err ? err : "unknown");
        return 1;
    }
    neverc_tcp_addr_t addr;
    neverc_tcp_listener_addr(ln, &addr);
    g_smtp_port = addr.port;
    neverc_tcp_listener_close(ln);

    printf("  mock smtp port: %d\n", g_smtp_port);

#ifdef _WIN32
    HANDLE srv = CreateThread(NULL, 0, mock_smtp_server, NULL, 0, NULL);
    Sleep(200);
#else
    pthread_t srv;
    pthread_create(&srv, NULL, mock_smtp_server, NULL);
    usleep(200000);
#endif

    test_dial_invalid();
    test_smtp_session();
    test_smtp_auth_login();
    test_send_mail();
    test_dot_stuffing();
    test_smtp_reject_injection();
    test_smtp_starttls_fail_closed();
    test_smtp_auth_plaintext_non_localhost();
    test_smtp_auth_requires_advertised();
    test_smtp_multiline_code_mismatch();
    test_smtp_response_leftover();

    g_smtp_running = 0;

    /* Connect to break the accept loop */
    neverc_tcp_conn_t *wake = neverc_tcp_dial(
        addr.addr, &err);
    if (wake) neverc_tcp_close(wake);

#ifdef _WIN32
    WaitForSingleObject(srv, 3000);
    CloseHandle(srv);
#else
    pthread_join(srv, NULL);
#endif

    printf("\n%d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0)
        printf(", %d FAILED", tests_failed);
    printf("\n");
    if (tests_failed == 0) puts("passed");

    return tests_failed > 0 ? 1 : 0;
}
