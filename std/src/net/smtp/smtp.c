#include "neverc/std/net/smtp.h"
#include "neverc/std/net/tcp.h"
#include "neverc/std/crypto/tls.h"
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
    neverc_tls_conn_t *tls;
    char server_name[256];
    char last_response[SMTP_BUF_SIZE];
    char pending[SMTP_BUF_SIZE];
    size_t pending_len;
    int  did_hello;
    int  supports_auth_plain;
    int  supports_auth_login;
    int  supports_8bitmime;
    int  supports_smtputf8;
    int  supports_starttls;
    int  data_at_line_start;
    int  in_data;
    int  dead;
};

static int smtp_write_all(neverc_smtp_client_t *c, const void *data, size_t len) {
    const char *p = (const char *)data;
    size_t sent = 0;
    while (sent < len) {
        int n = c->tls
            ? neverc_tls_write(c->tls, p + sent, len - sent)
            : neverc_tcp_write(c->conn, p + sent, len - sent);
        if (n <= 0) return -1;
        sent += (size_t)n;
    }
    return 0;
}

static int smtp_read_some(neverc_smtp_client_t *c, void *buf, size_t cap) {
    return c->tls ? neverc_tls_read(c->tls, buf, cap)
                  : neverc_tcp_read(c->conn, buf, cap);
}

static int smtp_is_status_code(const char *line, size_t line_len) {
    return line_len >= 3 &&
           line[0] >= '1' && line[0] <= '5' &&
           line[1] >= '0' && line[1] <= '9' &&
           line[2] >= '0' && line[2] <= '9';
}

static int smtp_reply_code(const char *line, size_t line_len) {
    if (!smtp_is_status_code(line, line_len)) return -1;
    return (line[0] - '0') * 100 + (line[1] - '0') * 10 + (line[2] - '0');
}

/* RFC 5321 §4.2: "DDD" / "DDD text" is final; "DDD-text" continues. */
static int smtp_line_is_final(const char *line, size_t line_len) {
    if (!smtp_is_status_code(line, line_len)) return 0;
    return line_len == 3 || line[3] == ' ';
}

static int smtp_line_is_continuation(const char *line, size_t line_len) {
    return smtp_is_status_code(line, line_len) && line_len > 3 &&
           line[3] == '-';
}

/* Read a complete SMTP response (multi-line aware, RFC 5321 §4.2).
 * Every reply-line must carry the same 3-digit code; a 250- prefix
 * followed by 550 must not be reported as success. Bytes after the
 * terminating final line stay in pending[] so a later response that
 * arrived in the same TCP read is not discarded. */
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
    int reply_code = -1;
    for (;;) {
        int complete = 0;
        int malformed = 0;
        const char *line_start = c->last_response;
        reply_code = -1;
        while (1) {
            const char *crlf = strstr(line_start, "\r\n");
            if (!crlf) break;
            size_t line_len = (size_t)(crlf - line_start);
            int code = smtp_reply_code(line_start, line_len);
            if (code < 0 ||
                (reply_code >= 0 && code != reply_code) ||
                (!smtp_line_is_final(line_start, line_len) &&
                 !smtp_line_is_continuation(line_start, line_len))) {
                malformed = 1;
                break;
            }
            if (reply_code < 0) reply_code = code;
            if (smtp_line_is_final(line_start, line_len)) {
                complete = 1;
                consumed = (size_t)(crlf + 2 - c->last_response);
                break;
            }
            line_start = crlf + 2;
        }
        if (malformed) return -1;
        if (complete) break;

        if (total >= SMTP_BUF_SIZE - 1) return -1;
        int n = smtp_read_some(c, c->last_response + total,
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
    return reply_code;
}

/* Reject SMTP atoms that could inject extra commands, source routes, or
 * break path syntax. Leading '@', extra '@', ',' / ';', and ':' outside
 * an address-literal are obsolete source-route punctuation
 * (RFC 5321 §4.1.2). Colons remain valid inside "[...]" IPv6 literals. */
static int smtp_safe_atom(const char *s) {
    if (!s || !s[0] || s[0] == '@') return 0;
    size_t n = 0;
    int in_literal = 0;
    int at_count = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++, n++) {
        if (n >= 255 || *p <= 32 || *p >= 127 || *p == '<' || *p == '>' ||
            *p == ',' || *p == ';' || *p == '%' || *p == '!')
            return 0;
        if (*p == '[') {
            if (in_literal) return 0;
            in_literal = 1;
            continue;
        }
        if (*p == ']') {
            if (!in_literal) return 0;
            in_literal = 0;
            /* Leftover after a closed address-literal is not part of the atom. */
            if (p[1] != '\0')
                return 0;
            continue;
        }
        if (*p == ':' && !in_literal)
            return 0;
        if (*p == '@') {
            at_count++;
            if (at_count > 1)
                return 0;
        }
    }
    return !in_literal;
}

static int smtp_line_has_break(const char *s, size_t len) {
    for (size_t i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)s[i];
        if (ch == '\r' || ch == '\n') return 1;
    }
    return 0;
}

/* Go smtp.PlainAuth isLocalhost: TLS is the only way to trust EHLO.
 * Without TLS, a MITM can strip STARTTLS and advertise AUTH PLAIN. */
static int smtp_is_localhost(const char *name) {
    return name && (strcmp(name, "localhost") == 0 ||
                    strcmp(name, "127.0.0.1") == 0 ||
                    strcmp(name, "::1") == 0);
}

/* Send a command and read the response. Returns status code.
 * AUTH LOGIN continuations may be empty (base64 of an empty credential
 * is a blank CRLF line). Ordinary SMTP commands must not be. */
static int smtp_cmd_line(neverc_smtp_client_t *c, const char *cmd,
                         int allow_empty) {
    size_t len = strlen(cmd);
    char sendbuf[SMTP_BUF_SIZE];
    if ((len == 0 && !allow_empty) || len >= sizeof(sendbuf) - 3 ||
        smtp_line_has_break(cmd, len))
        return -1;
    memcpy(sendbuf, cmd, len);
    sendbuf[len] = '\r';
    sendbuf[len + 1] = '\n';
    len += 2;
    if (smtp_write_all(c, sendbuf, len) != 0)
        return -1;
    return smtp_read_response(c);
}

static int smtp_cmd(neverc_smtp_client_t *c, const char *cmd) {
    return smtp_cmd_line(c, cmd, 0);
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

/* Go smtp.Dial uses net.SplitHostPort. "[::1]:587" is host "::1"; a
 * hostname:port with exactly one colon is the name before that colon. */
static int smtp_copy_server_name(const char *addr, char *dst, size_t cap) {
    if (!addr || !dst || cap == 0)
        return -1;
    dst[0] = '\0';
    if (addr[0] == '[') {
        const char *end = strchr(addr, ']');
        if (!end || end == addr + 1)
            return -1;
        size_t n = (size_t)(end - addr - 1);
        if (n >= cap)
            return -1;
        memcpy(dst, addr + 1, n);
        dst[n] = '\0';
        return 0;
    }
    const char *colon = strrchr(addr, ':');
    if (!colon || colon == addr)
        return -1;
    for (const char *p = addr; p < colon; p++) {
        if (*p == ':')
            return -1;
    }
    size_t n = (size_t)(colon - addr);
    if (n >= cap)
        return -1;
    memcpy(dst, addr, n);
    dst[n] = '\0';
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

    if (neverc_tcp_set_timeout(conn, 30000) != 0) {
        neverc_tcp_close(conn);
        if (errp) *errp = "failed to set timeout";
        return NULL;
    }

    neverc_smtp_client_t *c =
        (neverc_smtp_client_t *)calloc(1, sizeof(*c));
    if (!c) {
        neverc_tcp_close(conn);
        if (errp) *errp = "out of memory";
        return NULL;
    }
    c->conn = conn;

    /* Go smtp.Dial uses net.SplitHostPort: "[::1]:587" is host "::1",
     * not "[::1]". strrchr(':') left the brackets in ServerName. */
    if (smtp_copy_server_name(addr, c->server_name,
                              sizeof(c->server_name)) != 0)
        c->server_name[0] = '\0';

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
        /* QUIT during DATA would be written as message body. After a
         * failed STARTTLS handshake the TCP stream is not SMTP. */
        if (!c->in_data && !c->dead)
            neverc_smtp_quit(c);
        if (c->tls)
            neverc_tls_close(c->tls);
        neverc_tcp_close(c->conn);
    }
    free(c);
}

static int smtp_require_command_phase(neverc_smtp_client_t *c) {
    return (!c || c->in_data || c->dead) ? -1 : 0;
}

static int smtp_token_eq(const char *s, size_t n, const char *tok) {
    size_t i;
    if (!s || !tok) return 0;
    for (i = 0; tok[i]; i++) {
        unsigned char a;
        unsigned char b;
        if (i >= n) return 0;
        a = (unsigned char)s[i];
        b = (unsigned char)tok[i];
        if (a >= 'a' && a <= 'z') a = (unsigned char)(a - 'a' + 'A');
        if (b >= 'a' && b <= 'z') b = (unsigned char)(b - 'a' + 'A');
        if (a != b) return 0;
    }
    return i == n;
}

/* RFC 5321 / 4954: keyword is the first token after "DDD" / "DDD-". AUTH
 * mechanisms are that line's parameters only — never the rest of the EHLO
 * blob, and never parameters of HELP/XFORWARD/etc. */
static void smtp_parse_ehlo_line(neverc_smtp_client_t *c,
                                 const char *line, size_t line_len) {
    size_t pos;
    size_t kw_end;
    if (line_len < 4 || !smtp_is_status_code(line, line_len)) return;
    pos = 4;
    kw_end = pos;
    while (kw_end < line_len && line[kw_end] != ' ' && line[kw_end] != '=')
        kw_end++;
    if (smtp_token_eq(line + pos, kw_end - pos, "8BITMIME"))
        c->supports_8bitmime = 1;
    if (smtp_token_eq(line + pos, kw_end - pos, "SMTPUTF8"))
        c->supports_smtputf8 = 1;
    if (smtp_token_eq(line + pos, kw_end - pos, "STARTTLS"))
        c->supports_starttls = 1;
    if (!smtp_token_eq(line + pos, kw_end - pos, "AUTH")) return;
    pos = kw_end;
    if (pos < line_len && (line[pos] == ' ' || line[pos] == '='))
        pos++;
    while (pos < line_len) {
        size_t start;
        while (pos < line_len && line[pos] == ' ') pos++;
        start = pos;
        while (pos < line_len && line[pos] != ' ') pos++;
        if (start == pos) break;
        if (smtp_token_eq(line + start, pos - start, "PLAIN"))
            c->supports_auth_plain = 1;
        if (smtp_token_eq(line + start, pos - start, "LOGIN"))
            c->supports_auth_login = 1;
    }
}

int neverc_smtp_hello(neverc_smtp_client_t *c, const char *local_name) {
    if (smtp_require_command_phase(c) != 0) return -1;
    if (!local_name) local_name = "localhost";
    if (!smtp_safe_atom(local_name)) return -1;

    /* Try EHLO first */
    int code = smtp_cmdf(c, "EHLO %s", local_name);
    if (code == 250) {
        c->did_hello = 1;
        c->supports_auth_plain = 0;
        c->supports_auth_login = 0;
        c->supports_8bitmime = 0;
        c->supports_smtputf8 = 0;
        c->supports_starttls = 0;
        const char *line = c->last_response;
        while (line && *line) {
            const char *nl = strstr(line, "\r\n");
            size_t line_len = nl ? (size_t)(nl - line) : strlen(line);
            smtp_parse_ehlo_line(c, line, line_len);
            if (nl) line = nl + 2;
            else break;
        }
        return 0;
    }

    /* Fall back to HELO. AUTH is an ESMTP extension, so HELO leaves
     * supports_auth_* cleared and neverc_smtp_auth must fail closed. */
    code = smtp_cmdf(c, "HELO %s", local_name);
    if (code == 250) {
        c->did_hello = 1;
        c->supports_auth_plain = 0;
        c->supports_auth_login = 0;
        c->supports_8bitmime = 0;
        c->supports_smtputf8 = 0;
        c->supports_starttls = 0;
        return 0;
    }

    return -1;
}

static int ensure_hello(neverc_smtp_client_t *c) {
    if (c->did_hello) return 0;
    return neverc_smtp_hello(c, "localhost");
}

int neverc_smtp_starttls(neverc_smtp_client_t *c,
                         struct neverc_tls_config *cfg) {
    neverc_tls_config_t *owned = NULL;
    neverc_tls_config_t *use = (neverc_tls_config_t *)cfg;
    if (smtp_require_command_phase(c) != 0 || c->tls)
        return -1;
    if (ensure_hello(c) != 0) return -1;
    if (!c->supports_starttls) return -1;

    /* RFC 3207: 220 Ready to start TLS, then the handshake immediately. */
    int code = smtp_cmd(c, "STARTTLS");
    if (code != 220) return -1;
    if (c->pending_len != 0) {
        c->dead = 1;
        return -1;
    }

    if (!use) {
        owned = neverc_tls_config_new();
        if (!owned) {
            c->dead = 1;
            return -1;
        }
        if (c->server_name[0])
            neverc_tls_config_set_server_name(owned, c->server_name);
        use = owned;
    }

    const char *tls_err = NULL;
    neverc_tls_conn_t *tls = neverc_tls_client(c->conn, use, &tls_err);
    neverc_tls_config_free(owned);
    (void)tls_err;
    if (!tls) {
        c->dead = 1;
        return -1;
    }
    c->tls = tls;
    c->did_hello = 0;
    c->supports_auth_plain = 0;
    c->supports_auth_login = 0;
    c->supports_8bitmime = 0;
    c->supports_smtputf8 = 0;
    c->supports_starttls = 0;
    if (neverc_smtp_hello(c, "localhost") != 0) {
        c->dead = 1;
        return -1;
    }
    return 0;
}

static int smtp_maybe_starttls(neverc_smtp_client_t *c) {
    if (ensure_hello(c) != 0) return -1;
    if (!c->supports_starttls || c->tls) return 0;
    return neverc_smtp_starttls(c, NULL);
}

int neverc_smtp_auth(neverc_smtp_client_t *c,
                      neverc_smtp_auth_method_t method,
                      const char *username,
                      const char *password) {
    if (smtp_require_command_phase(c) != 0 || !username || !password)
        return -1;
    if (ensure_hello(c) != 0) return -1;
    /* Go smtp.SendMail / PLAIN: do not send passwords on a connection
     * that advertised STARTTLS but was never upgraded. */
    if (c->supports_starttls && !c->tls) return -1;
    /* Go smtp.PlainAuth: without TLS, EHLO (including AUTH and the
     * absence of STARTTLS) is untrusted. Localhost is the only exception. */
    if (!c->tls && !smtp_is_localhost(c->server_name)) return -1;

    if (method == NEVERC_SMTP_AUTH_PLAIN) {
        if (!c->supports_auth_plain) return -1;
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
        if (code != 235) {
            /* Same leftover class as LOGIN: a 334 challenge or extra reply
             * lines after AUTH must not become MAIL FROM (Go Client.Auth
             * sends '*'). Bare 535 already finished AUTH; keep the session. */
            if (c->pending_len != 0 || (code >= 300 && code < 400)) {
                c->dead = 1;
                (void)smtp_cmd_line(c, "*", 1);
            }
            return -1;
        }
        return 0;
    }

    if (method == NEVERC_SMTP_AUTH_LOGIN) {
        if (!c->supports_auth_login) return -1;
        char user_b64[512];
        char pass_b64[512];
        if (smtp_b64_encode(username, strlen(username), user_b64,
                            sizeof(user_b64)) != 0 ||
            smtp_b64_encode(password, strlen(password), pass_b64,
                            sizeof(pass_b64)) != 0)
            return -1;
        int code = smtp_cmd(c, "AUTH LOGIN");
        if (code != 334) {
            /* Same leftover class as PLAIN: extra reply lines after a
             * finished 5xx must not become MAIL FROM. Bare 535 keeps
             * the session. */
            if (c->pending_len != 0 || (code >= 300 && code < 400)) {
                c->dead = 1;
                (void)smtp_cmd_line(c, "*", 1);
            }
            return -1;
        }
        /* Same leftover class as STARTTLS: extra reply lines after 334
         * must not become the next SASL step (Go Client.Auth sends '*'). */
        if (c->pending_len != 0) {
            c->dead = 1;
            (void)smtp_cmd_line(c, "*", 1);
            return -1;
        }
        code = smtp_cmd_line(c, user_b64, 1);
        if (code != 334) {
            /* Go Client.Auth sends '*' only to abort an in-progress SASL
             * challenge. 535 already finished AUTH; keep the session. */
            if (c->pending_len != 0 || (code >= 300 && code < 400)) {
                c->dead = 1;
                (void)smtp_cmd_line(c, "*", 1);
            }
            return -1;
        }
        if (c->pending_len != 0) {
            c->dead = 1;
            (void)smtp_cmd_line(c, "*", 1);
            return -1;
        }
        code = smtp_cmd_line(c, pass_b64, 1);
        if (code != 235) {
            if (c->pending_len != 0 || (code >= 300 && code < 400)) {
                c->dead = 1;
                (void)smtp_cmd_line(c, "*", 1);
            }
            return -1;
        }
        return 0;
    }

    return -1;
}

int neverc_smtp_mail(neverc_smtp_client_t *c, const char *from) {
    /* RFC 5321 / Go smtp.Client.Mail: a null reverse-path is "<>". */
    if (smtp_require_command_phase(c) != 0 || !from ||
        (from[0] && !smtp_safe_atom(from)))
        return -1;
    if (ensure_hello(c) != 0) return -1;
    /* Go smtp.Client.Mail: advertise BODY=8BITMIME / SMTPUTF8 when EHLO
     * listed those extensions. */
    char extra[32] = "";
    if (c->supports_8bitmime)
        strcat(extra, " BODY=8BITMIME");
    if (c->supports_smtputf8)
        strcat(extra, " SMTPUTF8");
    int code = smtp_cmdf(c, "MAIL FROM:<%s>%s", from, extra);
    return (code == 250) ? 0 : -1;
}

int neverc_smtp_rcpt(neverc_smtp_client_t *c, const char *to) {
    if (smtp_require_command_phase(c) != 0 || !to || !smtp_safe_atom(to))
        return -1;
    if (ensure_hello(c) != 0) return -1;
    int code = smtp_cmdf(c, "RCPT TO:<%s>", to);
    /* RFC 5321 §4.3.2 / Go smtp.Client.Rcpt: RCPT success is 25x (250, 251). */
    return (code >= 250 && code <= 259) ? 0 : -1;
}

int neverc_smtp_data(neverc_smtp_client_t *c) {
    if (smtp_require_command_phase(c) != 0) return -1;
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
            if (smtp_write_all(c, ".", 1) != 0)
                return -1;
        }
        if (smtp_write_all(c, &ch, 1) != 0)
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
    if (smtp_write_all(c, term, strlen(term)) != 0)
        return -1;
    int code = smtp_read_response(c);
    c->in_data = 0;
    return (code == 250) ? 0 : -1;
}

int neverc_smtp_reset(neverc_smtp_client_t *c) {
    if (smtp_require_command_phase(c) != 0) return -1;
    int code = smtp_cmd(c, "RSET");
    return (code == 250) ? 0 : -1;
}

int neverc_smtp_noop(neverc_smtp_client_t *c) {
    if (smtp_require_command_phase(c) != 0) return -1;
    int code = smtp_cmd(c, "NOOP");
    return (code == 250) ? 0 : -1;
}

int neverc_smtp_quit(neverc_smtp_client_t *c) {
    if (smtp_require_command_phase(c) != 0 || !c->conn) return -1;
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
    if (!from || (from[0] && !smtp_safe_atom(from)) || nto < 0 ||
        (nto > 0 && !to))
        return -1;
    for (int i = 0; i < nto; i++) {
        if (!smtp_safe_atom(to[i])) return -1;
    }

    neverc_smtp_client_t *c = neverc_smtp_dial(addr, errp);
    if (!c) return -1;

    /* Go smtp.SendMail: if Extension("STARTTLS"), StartTLS fail-closed
     * before AUTH. */
    if (smtp_maybe_starttls(c) != 0) {
        if (errp) *errp = "STARTTLS failed";
        neverc_smtp_close(c);
        return -1;
    }

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
