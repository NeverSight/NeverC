#include "neverc/std/net/smtp.h"
#include "neverc/std/net/tcp.h"
#include "neverc/std/encoding/base64.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

/* ======================================================================
 * SMTP Client — RFC 5321 implementation
 * ====================================================================== */

#define SMTP_BUF_SIZE 4096

struct neverc_smtp_client {
    neverc_tcp_conn_t *conn;
    char server_name[256];
    char last_response[SMTP_BUF_SIZE];
    char pending[SMTP_BUF_SIZE];
    size_t pending_len;
    int  did_hello;
    int  supports_auth_plain;
    int  supports_auth_login;
    int  supports_8bitmime;
    int  supports_starttls;
    int  data_at_line_start;
    int  in_data;
};

static int smtp_write_all(neverc_tcp_conn_t *conn, const void *data, size_t len) {
    const char *p = (const char *)data;
    size_t sent = 0;
    while (sent < len) {
        int n = neverc_tcp_write(conn, p + sent, len - sent);
        if (n <= 0) return -1;
        sent += (size_t)n;
    }
    return 0;
}

static int smtp_is_status_code(const char *line, size_t line_len) {
    return line_len >= 3 &&
           line[0] >= '1' && line[0] <= '5' &&
           line[1] >= '0' && line[1] <= '9' &&
           line[2] >= '0' && line[2] <= '9';
}

/* RFC 5321 §4.2: the final reply-line is "DDD" or "DDD text". */
static int smtp_line_is_final(const char *line, size_t line_len) {
    if (!smtp_is_status_code(line, line_len)) return 0;
    return line_len == 3 || line[3] == ' ';
}

/* Read a complete SMTP response (multi-line aware, RFC 5321 §4.2).
 * Bytes after the terminating final line stay in pending[] so a later
 * response that arrived in the same TCP read is not discarded. */
static int smtp_read_response(neverc_smtp_client_t *c) {
    size_t total = 0;
    if (c->pending_len > 0) {
        memcpy(c->last_response, c->pending, c->pending_len);
        total = c->pending_len;
        c->pending_len = 0;
        c->last_response[total] = '\0';
    } else {
        c->last_response[0] = '\0';
    }

    size_t consumed = 0;
    for (;;) {
        int complete = 0;
        const char *line_start = c->last_response;
        while (1) {
            const char *crlf = strstr(line_start, "\r\n");
            if (!crlf) break;
            size_t line_len = (size_t)(crlf - line_start);
            if (smtp_line_is_final(line_start, line_len)) {
                complete = 1;
                consumed = (size_t)(crlf + 2 - c->last_response);
                break;
            }
            line_start = crlf + 2;
        }
        if (complete) break;

        if (total >= SMTP_BUF_SIZE - 1) return -1;
        int n = neverc_tcp_read(c->conn,
                                 c->last_response + total,
                                 SMTP_BUF_SIZE - total - 1);
        if (n <= 0) return -1;
        total += (size_t)n;
        c->last_response[total] = '\0';
    }

    if (consumed < total) {
        size_t leftover = total - consumed;
        if (leftover >= sizeof(c->pending)) return -1;
        memcpy(c->pending, c->last_response + consumed, leftover);
        c->pending_len = leftover;
    }
    c->last_response[consumed] = '\0';

    if (!smtp_is_status_code(c->last_response, consumed)) return -1;
    return (c->last_response[0] - '0') * 100 +
           (c->last_response[1] - '0') * 10 +
           (c->last_response[2] - '0');
}

/* Reject SMTP atoms that could inject extra commands or break path syntax. */
static int smtp_safe_atom(const char *s) {
    if (!s || !s[0]) return 0;
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++, n++) {
        if (n >= 255 || *p <= 32 || *p >= 127 || *p == '<' || *p == '>')
            return 0;
    }
    return 1;
}

/* Send a command and read the response. Returns status code. */
static int smtp_cmd(neverc_smtp_client_t *c, const char *cmd) {
    size_t len = strlen(cmd);
    /* Send command + CRLF in a single write to avoid fragmentation */
    char sendbuf[SMTP_BUF_SIZE];
    if (len >= sizeof(sendbuf) - 3) return -1;
    memcpy(sendbuf, cmd, len);
    if (len < 2 || cmd[len-2] != '\r' || cmd[len-1] != '\n') {
        sendbuf[len] = '\r';
        sendbuf[len+1] = '\n';
        len += 2;
    }
    if (smtp_write_all(c->conn, sendbuf, len) != 0)
        return -1;
    return smtp_read_response(c);
}

static int smtp_cmdf(neverc_smtp_client_t *c, const char *fmt, ...) {
    char buf[SMTP_BUF_SIZE];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf) - 3, fmt, ap);
    va_end(ap);
    if (n <= 0 || (size_t)n >= sizeof(buf) - 3) return -1;
    return smtp_cmd(c, buf);
}

static int smtp_b64_encode(const void *src, size_t srclen,
                           char *dst, size_t dstlen) {
    size_t need = neverc_base64_encoded_len(srclen);
    if (need == SIZE_MAX || need >= dstlen) return -1;
    if (neverc_base64_encode(dst, (const uint8_t *)src, srclen) != need)
        return -1;
    dst[need] = '\0';
    return 0;
}

/* ======================================================================
 * Public API
 * ====================================================================== */

neverc_smtp_client_t *neverc_smtp_dial(const char *addr, const char **errp) {
    if (!addr) {
        if (errp) *errp = "null address";
        return NULL;
    }

    const char *tcp_err = NULL;
    neverc_tcp_conn_t *conn = neverc_tcp_dial(addr, &tcp_err);
    if (!conn) {
        if (errp) *errp = tcp_err ? tcp_err : "connection failed";
        return NULL;
    }

    neverc_tcp_set_timeout(conn, 30000);

    neverc_smtp_client_t *c =
        (neverc_smtp_client_t *)calloc(1, sizeof(*c));
    if (!c) {
        neverc_tcp_close(conn);
        if (errp) *errp = "out of memory";
        return NULL;
    }
    c->conn = conn;

    /* Extract hostname from addr */
    const char *colon = strrchr(addr, ':');
    if (colon) {
        size_t hlen = (size_t)(colon - addr);
        if (hlen >= sizeof(c->server_name)) hlen = sizeof(c->server_name) - 1;
        memcpy(c->server_name, addr, hlen);
        c->server_name[hlen] = '\0';
    } else {
        snprintf(c->server_name, sizeof(c->server_name), "%s", addr);
    }

    /* Read the greeting (220) */
    int code = smtp_read_response(c);
    if (code != 220) {
        if (errp) *errp = "unexpected greeting";
        neverc_tcp_close(conn);
        free(c);
        return NULL;
    }

    return c;
}

void neverc_smtp_close(neverc_smtp_client_t *c) {
    if (!c) return;
    if (c->conn) {
        neverc_smtp_quit(c);
        neverc_tcp_close(c->conn);
    }
    free(c);
}

int neverc_smtp_hello(neverc_smtp_client_t *c, const char *local_name) {
    if (!c) return -1;
    if (!local_name) local_name = "localhost";
    if (!smtp_safe_atom(local_name)) return -1;

    /* Try EHLO first */
    int code = smtp_cmdf(c, "EHLO %s", local_name);
    if (code == 250) {
        c->did_hello = 1;
        /* Parse EHLO extensions */
        char *line = c->last_response;
        while (line && *line) {
            char *nl = strstr(line, "\r\n");
            if (strstr(line, "AUTH") && (strstr(line, "PLAIN") || strstr(line, "plain")))
                c->supports_auth_plain = 1;
            if (strstr(line, "AUTH") && (strstr(line, "LOGIN") || strstr(line, "login")))
                c->supports_auth_login = 1;
            if (strstr(line, "8BITMIME"))
                c->supports_8bitmime = 1;
            if (strstr(line, "STARTTLS"))
                c->supports_starttls = 1;
            if (nl) line = nl + 2;
            else break;
        }
        return 0;
    }

    /* Fall back to HELO */
    code = smtp_cmdf(c, "HELO %s", local_name);
    if (code == 250) {
        c->did_hello = 1;
        return 0;
    }

    return -1;
}

static int ensure_hello(neverc_smtp_client_t *c) {
    if (c->did_hello) return 0;
    return neverc_smtp_hello(c, "localhost");
}

int neverc_smtp_auth(neverc_smtp_client_t *c,
                      neverc_smtp_auth_method_t method,
                      const char *username,
                      const char *password) {
    if (!c || !username || !password) return -1;
    if (ensure_hello(c) != 0) return -1;

    if (method == NEVERC_SMTP_AUTH_PLAIN) {
        /* AUTH PLAIN: base64("\0username\0password") */
        size_t ulen = strlen(username);
        size_t pwlen = strlen(password);
        if (ulen > SIZE_MAX - pwlen - 2) return -1;
        size_t plen = ulen + pwlen + 2;
        char plain[512];
        if (plen > sizeof(plain)) return -1;
        plain[0] = '\0';
        memcpy(plain + 1, username, ulen);
        plain[1 + ulen] = '\0';
        memcpy(plain + 2 + ulen, password, pwlen);

        char encoded[1024];
        if (smtp_b64_encode(plain, plen, encoded, sizeof(encoded)) != 0)
            return -1;

        int code = smtp_cmdf(c, "AUTH PLAIN %s", encoded);
        return (code == 235) ? 0 : -1;
    }

    if (method == NEVERC_SMTP_AUTH_LOGIN) {
        char encoded[512];
        if (smtp_b64_encode(username, strlen(username), encoded,
                            sizeof(encoded)) != 0 ||
            neverc_base64_encoded_len(strlen(password)) >= sizeof(encoded))
            return -1;
        int code = smtp_cmd(c, "AUTH LOGIN");
        if (code != 334) return -1;
        code = smtp_cmd(c, encoded);
        if (code != 334) return -1;

        if (smtp_b64_encode(password, strlen(password), encoded,
                            sizeof(encoded)) != 0)
            return -1;
        code = smtp_cmd(c, encoded);
        return (code == 235) ? 0 : -1;
    }

    return -1;
}

int neverc_smtp_mail(neverc_smtp_client_t *c, const char *from) {
    if (!c || !from || !smtp_safe_atom(from)) return -1;
    if (ensure_hello(c) != 0) return -1;
    int code = smtp_cmdf(c, "MAIL FROM:<%s>", from);
    return (code == 250) ? 0 : -1;
}

int neverc_smtp_rcpt(neverc_smtp_client_t *c, const char *to) {
    if (!c || !to || !smtp_safe_atom(to)) return -1;
    if (ensure_hello(c) != 0) return -1;
    int code = smtp_cmdf(c, "RCPT TO:<%s>", to);
    return (code == 250) ? 0 : -1;
}

int neverc_smtp_data(neverc_smtp_client_t *c) {
    if (!c) return -1;
    if (ensure_hello(c) != 0) return -1;
    int code = smtp_cmd(c, "DATA");
    if (code == 354) {
        c->data_at_line_start = 1;
        c->in_data = 1;
    }
    return (code == 354) ? 0 : -1;
}

static int smtp_write_data_stuffed(neverc_smtp_client_t *c,
                                   const void *data, size_t len) {
    const uint8_t *bytes = (const uint8_t *)data;
    for (size_t i = 0; i < len; i++) {
        uint8_t ch = bytes[i];
        if (c->data_at_line_start && ch == '.') {
            if (smtp_write_all(c->conn, ".", 1) != 0)
                return -1;
        }
        if (smtp_write_all(c->conn, &ch, 1) != 0)
            return -1;
        if (ch == '\n')
            c->data_at_line_start = 1;
        else if (ch != '\r')
            c->data_at_line_start = 0;
    }
    return 0;
}

int neverc_smtp_write_data(neverc_smtp_client_t *c,
                             const void *data, size_t len) {
    if (!c || !c->in_data) return -1;
    if (len == 0) return 0;
    if (!data) return -1;
    return smtp_write_data_stuffed(c, data, len);
}

int neverc_smtp_data_close(neverc_smtp_client_t *c) {
    if (!c || !c->in_data) return -1;
    const char *term = c->data_at_line_start ? ".\r\n" : "\r\n.\r\n";
    if (smtp_write_all(c->conn, term, strlen(term)) != 0)
        return -1;
    int code = smtp_read_response(c);
    c->in_data = 0;
    return (code == 250) ? 0 : -1;
}

int neverc_smtp_reset(neverc_smtp_client_t *c) {
    if (!c) return -1;
    c->in_data = 0;
    int code = smtp_cmd(c, "RSET");
    return (code == 250) ? 0 : -1;
}

int neverc_smtp_noop(neverc_smtp_client_t *c) {
    if (!c) return -1;
    int code = smtp_cmd(c, "NOOP");
    return (code == 250) ? 0 : -1;
}

int neverc_smtp_quit(neverc_smtp_client_t *c) {
    if (!c || !c->conn) return -1;
    int code = smtp_cmd(c, "QUIT");
    return (code == 221) ? 0 : -1;
}

const char *neverc_smtp_last_response(neverc_smtp_client_t *c) {
    return c ? c->last_response : NULL;
}

int neverc_smtp_send_mail(const char *addr,
                            neverc_smtp_auth_method_t auth_method,
                            const char *username,
                            const char *password,
                            const char *from,
                            const char **to, int nto,
                            const void *msg, size_t msg_len,
                            const char **errp) {
    if (!from || !smtp_safe_atom(from) || nto < 0 || (nto > 0 && !to))
        return -1;
    for (int i = 0; i < nto; i++) {
        if (!smtp_safe_atom(to[i])) return -1;
    }

    neverc_smtp_client_t *c = neverc_smtp_dial(addr, errp);
    if (!c) return -1;

    if (auth_method != NEVERC_SMTP_AUTH_NONE) {
        if (neverc_smtp_auth(c, auth_method, username, password) != 0) {
            if (errp) *errp = "authentication failed";
            neverc_smtp_close(c);
            return -1;
        }
    }

    if (neverc_smtp_mail(c, from) != 0) {
        if (errp) *errp = "MAIL FROM failed";
        neverc_smtp_close(c);
        return -1;
    }

    for (int i = 0; i < nto; i++) {
        if (neverc_smtp_rcpt(c, to[i]) != 0) {
            if (errp) *errp = "RCPT TO failed";
            neverc_smtp_close(c);
            return -1;
        }
    }

    if (neverc_smtp_data(c) != 0) {
        if (errp) *errp = "DATA failed";
        neverc_smtp_close(c);
        return -1;
    }

    if (neverc_smtp_write_data(c, msg, msg_len) != 0) {
        if (errp) *errp = "write data failed";
        neverc_smtp_close(c);
        return -1;
    }

    if (neverc_smtp_data_close(c) != 0) {
        if (errp) *errp = "data close failed";
        neverc_smtp_close(c);
        return -1;
    }

    neverc_smtp_close(c);
    return 0;
}
