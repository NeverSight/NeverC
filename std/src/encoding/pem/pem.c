#include "neverc/std/encoding/pem.h"
#include "neverc/std/encoding/base64.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

/*
 * PEM encoding/decoding — simplified C port of Go encoding/pem.
 * Uses neverc_base64_encode/decode for the payload.
 */

#define PEM_LINE_LEN 64

static const char *BEGIN_PREFIX = "-----BEGIN ";
static const char *END_PREFIX   = "-----END ";
static const char *DASHES       = "-----";

int neverc_pem_encode(char *out, size_t out_cap,
                      const char *type_str,
                      const uint8_t *data, size_t data_len)
{
    if (!out || !type_str || (!data && data_len != 0) ||
        data_len > SIZE_MAX - 2)
        return -1;
    size_t type_len = strlen(type_str);
    if (type_len == 0 || type_len > 200)
        return -1;
    for (size_t i = 0; i < type_len; ++i) {
        unsigned char c = (unsigned char)type_str[i];
        if (c < 0x20 || c > 0x7e)
            return -1;
    }

    if ((data_len + 2) / 3 > SIZE_MAX / 4)
        return -1;
    size_t b64_len = neverc_base64_encoded_len(data_len);

    size_t line_breaks = (b64_len > 0) ? ((b64_len - 1) / PEM_LINE_LEN) : 0;
    size_t body_newlines = b64_len > 0 ? line_breaks + 1 : 0;
    size_t fixed = 11 + 6 + 9 + 6 + 1;
    if (type_len > (SIZE_MAX - fixed) / 2)
        return -1;
    size_t needed = fixed + type_len * 2;
    if (b64_len > SIZE_MAX - needed ||
        body_newlines > SIZE_MAX - needed - b64_len)
        return -1;
    needed += b64_len + body_newlines;
    if (out_cap < needed || needed - 1 > INT_MAX)
        return -1;

    char *base64 = (char *)malloc(b64_len + 1);
    if (!base64)
        return -1;
    if (neverc_base64_encode(base64, data, data_len) != b64_len) {
        free(base64);
        return -1;
    }

    char *p = out;
    memcpy(p, BEGIN_PREFIX, 11); p += 11;
    memcpy(p, type_str, type_len); p += type_len;
    memcpy(p, DASHES, 5); p += 5;
    *p++ = '\n';

    for (size_t i = 0; i < b64_len; i += PEM_LINE_LEN) {
        size_t chunk = b64_len - i;
        if (chunk > PEM_LINE_LEN) chunk = PEM_LINE_LEN;
        memcpy(p, base64 + i, chunk);
        p += chunk;
        *p++ = '\n';
    }

    memcpy(p, END_PREFIX, 9); p += 9;
    memcpy(p, type_str, type_len); p += type_len;
    memcpy(p, DASHES, 5); p += 5;
    *p++ = '\n';
    *p = '\0';

    free(base64);
    return (int)(p - out);
}

static const char *pem_find_bounded(
    const char *begin, const char *end,
    const char *needle, size_t needle_len) {
    if (!begin || !end || !needle || needle_len == 0 ||
        begin > end || (size_t)(end - begin) < needle_len)
        return NULL;
    size_t last = (size_t)(end - begin) - needle_len;
    for (size_t offset = 0; offset <= last; ++offset) {
        const char *p = begin + offset;
        if (*p == *needle && memcmp(p, needle, needle_len) == 0)
            return p;
    }
    return NULL;
}

static int pem_base64_value(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static int pem_is_space(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/*
 * RFC 1421 encapsulated headers are `Name: value` lines, terminated by a
 * blank line or by the first line that is not a header (the base64 body).
 * Standard base64 has no ':', so a body line is never mistaken for a header.
 */
static const char *pem_skip_headers(const char *start, const char *end) {
    const char *p = start;
    while (p < end) {
        const char *nl = (const char *)memchr(p, '\n', (size_t)(end - p));
        const char *eol = nl ? nl : end;
        const char *content_end = eol;
        if (content_end > p && content_end[-1] == '\r')
            --content_end;
        while (content_end > p &&
               (content_end[-1] == ' ' || content_end[-1] == '\t'))
            --content_end;
        if (content_end == p)
            return nl ? nl + 1 : end;
        int has_colon = 0;
        for (const char *c = p; c < content_end; ++c) {
            if (*c == ':') {
                has_colon = 1;
                break;
            }
        }
        if (!has_colon)
            return p;
        p = nl ? nl + 1 : end;
    }
    return p;
}

int neverc_pem_decode(const char *pem_data, size_t pem_len,
                      char *type_buf, size_t type_cap,
                      uint8_t *out_buf, size_t out_cap,
                      size_t *bytes_written,
                      size_t *rest_offset)
{
    if (bytes_written) *bytes_written = 0;
    if (rest_offset) *rest_offset = pem_len;
    if (!pem_data || !type_buf || type_cap == 0 ||
        (!out_buf && out_cap != 0))
        return -1;

    const char *pem_end = pem_data + pem_len;
    const char *begin = pem_data;
    while ((begin = pem_find_bounded(
                begin, pem_end, BEGIN_PREFIX, 11)) != NULL) {
        if (begin == pem_data || begin[-1] == '\n')
            break;
        ++begin;
    }
    if (!begin)
        return -1;

    const char *line_end =
        (const char *)memchr(begin, '\n', (size_t)(pem_end - begin));
    if (!line_end)
        return -1;
    const char *header_end = line_end;
    if (header_end > begin && header_end[-1] == '\r')
        --header_end;
    while (header_end > begin &&
           (header_end[-1] == ' ' || header_end[-1] == '\t'))
        --header_end;

    const char *type_start = begin + 11;
    size_t dash_len = strlen(DASHES);
    if ((size_t)(header_end - type_start) <= dash_len ||
        memcmp(header_end - dash_len, DASHES, dash_len) != 0)
        return -1;
    const char *type_end = header_end - dash_len;
    size_t type_len = (size_t)(type_end - type_start);
    if (type_len == 0 || type_len >= type_cap)
        return -1;
    memcpy(type_buf, type_start, type_len);
    type_buf[type_len] = '\0';

    char end_marker[256];
    size_t end_prefix_len = strlen(END_PREFIX);
    size_t end_marker_len = end_prefix_len + type_len + dash_len;
    if (end_marker_len >= sizeof(end_marker))
        return -1;
    memcpy(end_marker, END_PREFIX, end_prefix_len);
    memcpy(end_marker + end_prefix_len, type_start, type_len);
    memcpy(end_marker + end_prefix_len + type_len, DASHES, dash_len);

    const char *body_start = line_end + 1;
    const char *end = body_start;
    for (;;) {
        end = pem_find_bounded(
            end, pem_end, end_marker, end_marker_len);
        if (!end)
            return -1;
        if (end == body_start || end[-1] == '\n')
            break;
        ++end;
    }

    const char *after_end = end + end_marker_len;
    while (after_end < pem_end &&
           (*after_end == ' ' || *after_end == '\t'))
        ++after_end;
    if (after_end < pem_end && *after_end == '\r')
        ++after_end;
    if (after_end < pem_end) {
        if (*after_end != '\n')
            return -1;
        ++after_end;
    }

    body_start = pem_skip_headers(body_start, end);

    int quartet[4];
    size_t quartet_len = 0;
    size_t decoded_len = 0;
    int saw_padding = 0;
    for (const char *p = body_start; p < end; ++p) {
        unsigned char c = (unsigned char)*p;
        if (pem_is_space(c))
            continue;
        if (saw_padding)
            return -1;

        if (c == '=') {
            quartet[quartet_len++] = -1;
        } else {
            int value = pem_base64_value(c);
            if (value < 0)
                return -1;
            quartet[quartet_len++] = value;
        }
        if (quartet_len != 4)
            continue;

        if (quartet[0] < 0 || quartet[1] < 0 ||
            (quartet[2] < 0 && quartet[3] >= 0))
            return -1;
        size_t emit = quartet[2] < 0 ? 1 :
                      quartet[3] < 0 ? 2 : 3;
        if ((emit == 1 && (quartet[1] & 0x0f) != 0) ||
            (emit == 2 && (quartet[2] & 0x03) != 0) ||
            emit > out_cap - decoded_len)
            return -1;

        out_buf[decoded_len++] =
            (uint8_t)((quartet[0] << 2) | (quartet[1] >> 4));
        if (emit >= 2) {
            out_buf[decoded_len++] =
                (uint8_t)((quartet[1] << 4) | (quartet[2] >> 2));
        }
        if (emit == 3) {
            out_buf[decoded_len++] =
                (uint8_t)((quartet[2] << 6) | quartet[3]);
        } else {
            saw_padding = 1;
        }
        quartet_len = 0;
    }
    if (quartet_len != 0)
        return -1;

    if (bytes_written)
        *bytes_written = decoded_len;
    if (rest_offset)
        *rest_offset = (size_t)(after_end - pem_data);
    return 0;
}
