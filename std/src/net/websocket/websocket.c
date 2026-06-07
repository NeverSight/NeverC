#include "neverc/std/net/websocket.h"
#include "neverc/std/net/http.h"
#include "neverc/std/crypto/sha1.h"
#include "neverc/std/encoding/base64.h"
#include "../_net_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <strings.h>
#else
static int strcasecmp(const char *a, const char *b) {
    return _stricmp(a, b);
}
#endif

#define WS_MAGIC "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

struct neverc_ws_conn {
    neverc_tcp_conn_t *tcp;
};

/* ======================================================================
 * Helpers
 * ====================================================================== */

static int strcasecmp_n(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        int ca = (unsigned char)a[i];
        int cb = (unsigned char)b[i];
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return ca - cb;
        if (ca == '\0') return 0;
    }
    return 0;
}

static const char *find_header_value(const char *raw, const char *hdr_end,
                                      const char *name) {
    size_t nlen = strlen(name);
    const char *p = raw;
    const char *end = hdr_end;

    /* Skip request line */
    while (p + 1 < end && !(p[0] == '\r' && p[1] == '\n')) p++;
    if (p + 1 >= end) return NULL;
    p += 2;

    while (p < end) {
        const char *hline_end = p;
        while (hline_end + 1 < end &&
               !(hline_end[0] == '\r' && hline_end[1] == '\n'))
            hline_end++;
        if (hline_end + 1 >= end) break;

        const char *colon = NULL;
        for (const char *q = p; q < hline_end; q++) {
            if (*q == ':') { colon = q; break; }
        }
        if (colon) {
            size_t hlen = (size_t)(colon - p);
            if (hlen == nlen && strcasecmp_n(p, name, nlen) == 0) {
                const char *val = colon + 1;
                while (val < hline_end && *val == ' ') val++;
                return val;
            }
        }
        p = hline_end + 2;
    }
    return NULL;
}

static int tcp_write_all(neverc_tcp_conn_t *conn, const void *data, size_t len) {
    const char *p = (const char *)data;
    size_t sent = 0;
    while (sent < len) {
        int n = neverc_tcp_write(conn, p + sent, len - sent);
        if (n <= 0) return -1;
        sent += (size_t)n;
    }
    return 0;
}

/* ======================================================================
 * Handshake
 * ====================================================================== */

int neverc_ws_compute_accept(const char *key, char *accept, size_t accept_cap) {
    if (!key || !accept || accept_cap < 29) return -1;

    char combined[128];
    int clen = snprintf(combined, sizeof(combined), "%s%s", key, WS_MAGIC);
    if (clen <= 0 || (size_t)clen >= sizeof(combined)) return -1;

    uint8_t digest[20];
    neverc_sha1_sum((const uint8_t *)combined, (size_t)clen, digest);

    size_t need = neverc_base64_encoded_len(20);
    if (need >= accept_cap) return -1;
    neverc_base64_encode(accept, digest, 20);
    accept[need] = '\0';
    return 0;
}

int neverc_ws_handshake_server(neverc_tcp_conn_t *conn, const char *raw_request,
                                size_t raw_len, size_t *consumed) {
    if (!conn || !raw_request || !consumed) return -1;
    *consumed = 0;

    const char *hdr_end = NULL;
    for (size_t i = 0; i + 3 < raw_len; i++) {
        if (raw_request[i] == '\r' && raw_request[i+1] == '\n' &&
            raw_request[i+2] == '\r' && raw_request[i+3] == '\n') {
            hdr_end = raw_request + i;
            break;
        }
    }
    if (!hdr_end) return -1;

    if (strncmp(raw_request, "GET ", 4) != 0) return -1;

    /* hdr_end points at the first \r of the terminating \r\n\r\n, which is
     * also the last header line's trailing \r. Include its \n for parsing. */
    const char *hdr_scan_end = hdr_end + 2;

    const char *upgrade = find_header_value(raw_request, hdr_scan_end, "Upgrade");
    if (!upgrade) return -1;
    {
        char uval[32];
        size_t ui = 0;
        while (upgrade[ui] && upgrade[ui] != '\r' && ui < sizeof(uval) - 1) {
            uval[ui] = upgrade[ui];
            ui++;
        }
        uval[ui] = '\0';
        if (strcasecmp_n(uval, "websocket", 9) != 0) return -1;
    }

    const char *conn_hdr = find_header_value(raw_request, hdr_scan_end, "Connection");
    if (!conn_hdr) return -1;
    int has_upgrade = 0;
    {
        size_t i = 0;
        while (conn_hdr[i] && conn_hdr[i] != '\r') {
            size_t start = i;
            while (conn_hdr[i] && conn_hdr[i] != ',' && conn_hdr[i] != '\r') i++;
            size_t toklen = i - start;
            while (toklen > 0 && conn_hdr[start] == ' ') { start++; toklen--; }
            if (toklen == 7 && strcasecmp_n(conn_hdr + start, "upgrade", 7) == 0)
                has_upgrade = 1;
            if (conn_hdr[i] == ',') i++;
        }
    }
    if (!has_upgrade) return -1;

    const char *version = find_header_value(raw_request, hdr_scan_end,
                                            "Sec-WebSocket-Version");
    if (!version || strncmp(version, "13", 2) != 0) return -1;

    const char *ws_key = find_header_value(raw_request, hdr_scan_end,
                                            "Sec-WebSocket-Key");
    if (!ws_key) return -1;

    char key_buf[64];
    size_t ki = 0;
    while (ws_key[ki] && ws_key[ki] != '\r' && ki < sizeof(key_buf) - 1) {
        key_buf[ki] = ws_key[ki];
        ki++;
    }
    key_buf[ki] = '\0';

    char accept[64];
    if (neverc_ws_compute_accept(key_buf, accept, sizeof(accept)) != 0)
        return -1;

    char response[512];
    int n = snprintf(response, sizeof(response),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n"
        "\r\n", accept);
    if (n <= 0 || (size_t)n >= sizeof(response)) return -1;

    if (tcp_write_all(conn, response, (size_t)n) != 0) return -1;

    *consumed = (size_t)(hdr_end + 4 - raw_request);
    return 0;
}

static int ws_validate_http_upgrade(const neverc_http_request_t *req,
                                     char *key_buf, size_t key_cap) {
    if (!req || !key_buf || key_cap < 2) return -1;
    if (!req->method || strcmp(req->method, "GET") != 0) return -1;

    const char *upgrade = neverc_http_request_header(req, "Upgrade");
    if (!upgrade || strcasecmp(upgrade, "websocket") != 0) return -1;

    const char *conn_hdr = neverc_http_request_header(req, "Connection");
    if (!conn_hdr) return -1;
    int has_upgrade = 0;
    for (size_t i = 0; conn_hdr[i]; ) {
        while (conn_hdr[i] == ' ') i++;
        size_t start = i;
        while (conn_hdr[i] && conn_hdr[i] != ',') i++;
        size_t toklen = i - start;
        if (toklen == 7 && strcasecmp_n(conn_hdr + start, "upgrade", 7) == 0)
            has_upgrade = 1;
        if (conn_hdr[i] == ',') i++;
    }
    if (!has_upgrade) return -1;

    const char *version = neverc_http_request_header(req, "Sec-WebSocket-Version");
    if (!version || strncmp(version, "13", 2) != 0) return -1;

    const char *ws_key = neverc_http_request_header(req, "Sec-WebSocket-Key");
    if (!ws_key || !ws_key[0]) return -1;

    size_t ki = 0;
    while (ws_key[ki] && ki < key_cap - 1) {
        key_buf[ki] = ws_key[ki];
        ki++;
    }
    key_buf[ki] = '\0';
    return 0;
}

neverc_ws_conn_t *neverc_ws_upgrade_http(neverc_http_request_t *req,
                                          neverc_http_response_writer_t *w) {
    if (!req || !w) return NULL;

    char key_buf[64];
    if (ws_validate_http_upgrade(req, key_buf, sizeof(key_buf)) != 0)
        return NULL;

    char accept[64];
    if (neverc_ws_compute_accept(key_buf, accept, sizeof(accept)) != 0)
        return NULL;

    char response[512];
    int n = snprintf(response, sizeof(response),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n"
        "\r\n", accept);
    if (n <= 0 || (size_t)n >= sizeof(response)) return NULL;

    neverc_tcp_conn_t *tcp = neverc_http_hijack(w);
    if (!tcp) return NULL;
    if (tcp_write_all(tcp, response, (size_t)n) != 0) {
        neverc_tcp_close(tcp);
        return NULL;
    }

    return neverc_ws_conn_new(tcp);
}

/* ======================================================================
 * Connection
 * ====================================================================== */

neverc_ws_conn_t *neverc_ws_conn_new(neverc_tcp_conn_t *conn) {
    if (!conn) return NULL;
    neverc_ws_conn_t *ws = (neverc_ws_conn_t *)calloc(1, sizeof(*ws));
    if (!ws) return NULL;
    ws->tcp = conn;
    return ws;
}

void neverc_ws_conn_free(neverc_ws_conn_t *conn) {
    if (!conn) return;
    if (conn->tcp) neverc_tcp_close(conn->tcp);
    free(conn);
}

int neverc_ws_set_timeout(neverc_ws_conn_t *conn, int ms) {
    if (!conn || !conn->tcp) return -1;
    return neverc_tcp_set_timeout(conn->tcp, ms);
}

/* ======================================================================
 * Frame I/O (RFC 6455)
 * ====================================================================== */

static int read_exact(neverc_tcp_conn_t *conn, void *buf, size_t len) {
    char *p = (char *)buf;
    size_t got = 0;
    while (got < len) {
        int n = neverc_tcp_read(conn, p + got, len - got);
        if (n <= 0) return -1;
        got += (size_t)n;
    }
    return 0;
}

static int write_frame(neverc_ws_conn_t *conn, int opcode,
                        const void *payload, size_t len, int mask) {
    if (!conn || !conn->tcp) return -1;

    uint8_t hdr[14];
    size_t hlen = 2;

    hdr[0] = (uint8_t)(0x80 | (opcode & 0x0F));
    if (len < 126) {
        hdr[1] = (uint8_t)(len & 0x7F);
        if (mask) hdr[1] |= 0x80;
    } else if (len < 65536) {
        hdr[1] = 126;
        if (mask) hdr[1] |= 0x80;
        hdr[2] = (uint8_t)((len >> 8) & 0xFF);
        hdr[3] = (uint8_t)(len & 0xFF);
        hlen = 4;
    } else {
        hdr[1] = 127;
        if (mask) hdr[1] |= 0x80;
        for (int i = 0; i < 8; i++)
            hdr[2 + i] = (uint8_t)((len >> (56 - i * 8)) & 0xFF);
        hlen = 10;
    }

    if (tcp_write_all(conn->tcp, hdr, hlen) != 0) return -1;
    if (len > 0 && tcp_write_all(conn->tcp, payload, len) != 0) return -1;
    return 0;
}

int neverc_ws_read_frame(neverc_ws_conn_t *conn, int *opcode, int *fin,
                          void *buf, size_t buflen, size_t *out_len) {
    if (!conn || !conn->tcp || !opcode || !buf || !out_len) return -1;
    *out_len = 0;

    uint8_t h2[2];
    if (read_exact(conn->tcp, h2, 2) != 0) return -1;

    *opcode = h2[0] & 0x0F;
    if (fin) *fin = (h2[0] & 0x80) != 0;
    int masked = (h2[1] & 0x80) != 0;
    uint64_t plen = h2[1] & 0x7F;

    if (plen == 126) {
        uint8_t ext[2];
        if (read_exact(conn->tcp, ext, 2) != 0) return -1;
        plen = ((uint64_t)ext[0] << 8) | ext[1];
    } else if (plen == 127) {
        uint8_t ext[8];
        if (read_exact(conn->tcp, ext, 8) != 0) return -1;
        plen = 0;
        for (int i = 0; i < 8; i++)
            plen = (plen << 8) | ext[i];
    }

    uint8_t mask_key[4] = {0};
    if (masked) {
        if (read_exact(conn->tcp, mask_key, 4) != 0) return -1;
    }

    if (plen > buflen) return -1;
    if (plen > 0) {
        if (read_exact(conn->tcp, buf, (size_t)plen) != 0) return -1;
        if (masked) {
            uint8_t *p = (uint8_t *)buf;
            for (size_t i = 0; i < (size_t)plen; i++)
                p[i] ^= mask_key[i % 4];
        }
    }

    *out_len = (size_t)plen;
    return 0;
}

int neverc_ws_write_text(neverc_ws_conn_t *conn, const void *data, size_t len) {
    return write_frame(conn, NC_WS_OPCODE_TEXT, data, len, 0);
}

int neverc_ws_write_binary(neverc_ws_conn_t *conn, const void *data, size_t len) {
    return write_frame(conn, NC_WS_OPCODE_BINARY, data, len, 0);
}

int neverc_ws_send_ping(neverc_ws_conn_t *conn, const void *data, size_t len) {
    return write_frame(conn, NC_WS_OPCODE_PING, data, len, 0);
}

int neverc_ws_send_pong(neverc_ws_conn_t *conn, const void *data, size_t len) {
    return write_frame(conn, NC_WS_OPCODE_PONG, data, len, 0);
}

int neverc_ws_send_close(neverc_ws_conn_t *conn, uint16_t code,
                          const char *reason) {
    uint8_t payload[128];
    size_t plen = 2;
    payload[0] = (uint8_t)((code >> 8) & 0xFF);
    payload[1] = (uint8_t)(code & 0xFF);
    if (reason) {
        size_t rlen = strlen(reason);
        if (rlen + 2 > sizeof(payload)) rlen = sizeof(payload) - 2;
        memcpy(payload + 2, reason, rlen);
        plen += rlen;
    }
    return write_frame(conn, NC_WS_OPCODE_CLOSE, payload, plen, 0);
}

int neverc_ws_read_message(neverc_ws_conn_t *conn, char *buf, size_t buflen,
                            size_t *out_len) {
    if (!conn || !buf || !out_len) return -1;
    *out_len = 0;

    nc_buf_t acc;
    nc_buf_init(&acc);

    for (;;) {
        int opcode = 0;
        int frame_fin = 0;
        size_t chunk_len = 0;
        char chunk[65536];
        int rc = neverc_ws_read_frame(conn, &opcode, &frame_fin, chunk,
                                       sizeof(chunk), &chunk_len);
        if (rc != 0) {
            nc_buf_free(&acc);
            return -1;
        }

        if (opcode == NC_WS_OPCODE_CLOSE) {
            nc_buf_free(&acc);
            return -1;
        }
        if (opcode == NC_WS_OPCODE_PING) {
            neverc_ws_send_pong(conn, chunk, chunk_len);
            continue;
        }
        if (opcode == NC_WS_OPCODE_PONG)
            continue;

        if (opcode != NC_WS_OPCODE_TEXT &&
            opcode != NC_WS_OPCODE_BINARY &&
            opcode != NC_WS_OPCODE_CONTINUATION) {
            nc_buf_free(&acc);
            return -1;
        }

        if (acc.len + chunk_len >= buflen) {
            nc_buf_free(&acc);
            return -1;
        }
        nc_buf_append(&acc, chunk, chunk_len);

        if (frame_fin) {
            memcpy(buf, acc.data, acc.len);
            buf[acc.len] = '\0';
            *out_len = acc.len;
            nc_buf_free(&acc);
            return 0;
        }
    }
}

int neverc_ws_write_message(neverc_ws_conn_t *conn, const char *msg) {
    if (!msg) return -1;
    return neverc_ws_write_text(conn, msg, strlen(msg));
}
