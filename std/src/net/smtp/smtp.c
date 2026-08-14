#include "neverc/std/net/smtp.h"
#include "neverc/std/net/tcp.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

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
    int  did_hello;
    int  supports_auth_plain;
    int  supports_auth_login;
    int  supports_8bitmime;
    int  supports_starttls;
    int  data_at_line_start;
};

/* Read a complete SMTP response (multi-line aware, RFC 5321 §4.2).
 * Multi-line: "DDD-text\r\n" (dash), Final: "DDD text\r\n" (space).
 * Returns the 3-digit status code, or -1 on error. */
static int smtp_read_response(neverc_smtp_client_t *c) {
    c->last_response[0] = '\0';
    size_t total = 0;

    for (;;) {
        if (total >= SMTP_BUF_SIZE - 1) return -1;

        int n = neverc_tcp_read(c->conn,
                                 c->last_response + total,
                                 SMTP_BUF_SIZE - total - 1);
        if (n <= 0) return -1;
        total += (size_t)n;
        c->last_response[total] = '\0';

        /* Scan all complete lines to check if the final line is present.
         * A final line has "DDD " (space at pos 3) instead of "DDD-". */
        int complete = 0;
        const char *line_start = c->last_response;
        while (1) {
            const char *crlf = strstr(line_start, "\r\n");
            if (!crlf) break;

            size_t line_len = (size_t)(crlf - line_start);
            /* Final line: at least 4 chars, digit-digit-digit-space */
            if (line_len >= 4 &&
                line_start[0] >= '1' && line_start[0] <= '5' &&
                line_start[1] >= '0' && line_start[1] <= '9' &&
                line_start[2] >= '0' && line_start[2] <= '9' &&
                line_start[3] == ' ') {
                complete = 1;
                break;
            }
            line_start = crlf + 2;
        }
        if (complete) break;
    }

    int code = 0;
    if (total >= 3 &&
        c->last_response[0] >= '1' && c->last_response[0] <= '5')
        code = (c->last_response[0] - '0') * 100 +
               (c->last_response[1] - '0') * 10 +
               (c->last_response[2] - '0');
    return code;
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
    if (neverc_tcp_write(c->conn, sendbuf, len) <= 0)
        return -1;
    return smtp_read_response(c);
}

static int smtp_cmdf(neverc_smtp_client_t *c, const char *fmt, ...) {
    char buf[SMTP_BUF_SIZE];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf) - 3, fmt, ap);
    va_end(ap);
    if (n <= 0) return -1;
    return smtp_cmd(c, buf);
}

/* Base64 encode (minimal implementation for SMTP AUTH) */
static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static size_t b64_encode(const void *src, size_t srclen,
                          char *dst, size_t dstlen) {
    const unsigned char *s = (const unsigned char *)src;
    size_t di = 0;
    size_t i = 0;

    while (i < srclen && di + 4 < dstlen) {
        uint32_t a = s[i++];
        uint32_t b = (i < srclen) ? s[i++] : 0;
        uint32_t c = (i < srclen) ? s[i++] : 0;
        uint32_t triple = (a << 16) | (b << 8) | c;

        dst[di++] = b64_table[(triple >> 18) & 0x3F];
        dst[di++] = b64_table[(triple >> 12) & 0x3F];
        dst[di++] = (i > srclen + 1) ? '=' : b64_table[(triple >> 6) & 0x3F];
        dst[di++] = (i > srclen) ? '=' : b64_table[triple & 0x3F];
    }
    dst[di] = '\0';
    return di;
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
        char plain[512];
        int plen = snprintf(plain + 1, sizeof(plain) - 2, "%s", username);
        plain[0] = '\0';
        plen++; /* include leading NUL */
        plain[plen] = '\0';
        plen++;
        int pwlen = snprintf(plain + plen, sizeof(plain) - (size_t)plen,
                              "%s", password);
        plen += pwlen;

        char encoded[1024];
        b64_encode(plain, (size_t)plen, encoded, sizeof(encoded));

        int code = smtp_cmdf(c, "AUTH PLAIN %s", encoded);
        return (code == 235) ? 0 : -1;
    }

    if (method == NEVERC_SMTP_AUTH_LOGIN) {
        int code = smtp_cmd(c, "AUTH LOGIN");
        if (code != 334) return -1;

        char encoded[512];
        b64_encode(username, strlen(username), encoded, sizeof(encoded));
        code = smtp_cmd(c, encoded);
        if (code != 334) return -1;

        b64_encode(password, strlen(password), encoded, sizeof(encoded));
        code = smtp_cmd(c, encoded);
        return (code == 235) ? 0 : -1;
    }

    return -1;
}

int neverc_smtp_mail(neverc_smtp_client_t *c, const char *from) {
    if (!c || !from) return -1;
    if (ensure_hello(c) != 0) return -1;
    int code = smtp_cmdf(c, "MAIL FROM:<%s>", from);
    return (code == 250) ? 0 : -1;
}

int neverc_smtp_rcpt(neverc_smtp_client_t *c, const char *to) {
    if (!c || !to) return -1;
    int code = smtp_cmdf(c, "RCPT TO:<%s>", to);
    return (code == 250) ? 0 : -1;
}

int neverc_smtp_data(neverc_smtp_client_t *c) {
    if (!c) return -1;
    int code = smtp_cmd(c, "DATA");
    if (code == 354)
        c->data_at_line_start = 1;
    return (code == 354) ? 0 : -1;
}

static int smtp_write_data_stuffed(neverc_smtp_client_t *c,
                                   const void *data, size_t len) {
    const uint8_t *bytes = (const uint8_t *)data;
    for (size_t i = 0; i < len; i++) {
        uint8_t ch = bytes[i];
        if (c->data_at_line_start && ch == '.') {
            if (neverc_tcp_write(c->conn, ".", 1) <= 0)
                return -1;
        }
        if (neverc_tcp_write(c->conn, &ch, 1) <= 0)
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
    if (!c || !data || len == 0) return 0;
    return smtp_write_data_stuffed(c, data, len);
}

int neverc_smtp_data_close(neverc_smtp_client_t *c) {
    if (!c) return -1;
    /* Send the terminating ".\r\n" */
    if (neverc_tcp_write(c->conn, "\r\n.\r\n", 5) <= 0)
        return -1;
    int code = smtp_read_response(c);
    return (code == 250) ? 0 : -1;
}

int neverc_smtp_reset(neverc_smtp_client_t *c) {
    if (!c) return -1;
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
