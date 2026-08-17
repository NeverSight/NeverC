#include "neverc/std/encoding/pem.h"
#include "neverc/std/encoding/base64.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

/*
 * PEM encoding/decoding — C port of Go encoding/pem.
 * Encode wraps neverc_base64_encode. Decode matches Go's armor rules:
 * find the next END line, take the last line-start BEGIN before it, and
 * skip a candidate whose type/trailer/headers/body are malformed. A ':'
 * on the END line with no body/blank before it is type injection: Go
 * consumes that END as a header and aborts the whole Decode.
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
    if (type_len > 200)
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

static const char *pem_trim_line(const char *start, const char *eol) {
    const char *content_end = eol;
    if (content_end > start && content_end[-1] == '\r')
        --content_end;
    while (content_end > start &&
           (content_end[-1] == ' ' || content_end[-1] == '\t'))
        --content_end;
    return content_end;
}

static int pem_line_has_colon(const char *start, const char *content_end) {
    for (const char *c = start; c < content_end; ++c) {
        if (*c == ':')
            return 1;
    }
    return 0;
}

/*
 * RFC 1421 encapsulated headers are `Name: value` lines, terminated by a
 * blank line or by the first line that is not a header (the base64 body).
 * Standard base64 has no ':', so a body line is never mistaken for a header.
 *
 * Go rejects a block that has headers and then END with no blank line and
 * no body (endIndex < 0 after consuming the header lines). *bare_headers
 * reports that case so the caller can skip the candidate.
 *
 * *reached_end is set when every line before END is a header (or the
 * preamble is empty). Go's header loop then consumes the END line itself
 * if that line contains ':'; endTrailerIndex goes negative and Decode
 * returns no block at all.
 */
static const char *pem_skip_headers(const char *start, const char *end,
                                    int *bare_headers, int *reached_end) {
    int had_header = 0;
    const char *p = start;
    *bare_headers = 0;
    *reached_end = 1;
    while (p < end) {
        const char *nl = (const char *)memchr(p, '\n', (size_t)(end - p));
        const char *eol = nl ? nl : end;
        const char *content_end = pem_trim_line(p, eol);
        if (content_end == p || !pem_line_has_colon(p, content_end)) {
            *reached_end = 0;
            return content_end == p && nl ? nl + 1 : p;
        }
        had_header = 1;
        p = nl ? nl + 1 : end;
    }
    if (had_header)
        *bare_headers = 1;
    return p;
}

/* getLine of the END line, then bytes.Cut on ':'. */
static int pem_end_line_has_colon(const char *end_line, const char *pem_end) {
    const char *nl = (const char *)memchr(
        end_line, '\n', (size_t)(pem_end - end_line));
    const char *eol = nl ? nl : pem_end;
    return pem_line_has_colon(end_line, pem_trim_line(end_line, eol));
}

/* Go searches for "\n-----END " and returns the '-----END ' that follows. */
static const char *pem_find_end_prefix(const char *search, const char *end) {
    const char *p = search;
    while (p < end) {
        const char *nl = (const char *)memchr(p, '\n', (size_t)(end - p));
        if (!nl)
            return NULL;
        const char *cand = nl + 1;
        if ((size_t)(end - cand) >= 9 && memcmp(cand, END_PREFIX, 9) == 0)
            return cand;
        p = cand;
    }
    return NULL;
}

static const char *pem_find_last_begin(const char *search, const char *end_line) {
    const char *found = NULL;
    const char *p = search;
    while ((p = pem_find_bounded(p, end_line, BEGIN_PREFIX, 11)) != NULL) {
        if (p == search || p[-1] == '\n')
            found = p;
        ++p;
    }
    return found;
}

/*
 * Decode the PEM base64 body. Returns 1 on success, 0 if the body is
 * corrupt (caller skips this block), and -1 if the block is valid but
 * out_cap is too small.
 */
static int pem_decode_body(uint8_t *out_buf, size_t out_cap,
                           const char *body_start, const char *end,
                           size_t *decoded_len) {
    int quartet[4];
    size_t quartet_len = 0;
    size_t n = 0;
    int saw_padding = 0;

    for (const char *p = body_start; p < end; ++p) {
        unsigned char c = (unsigned char)*p;
        if (pem_is_space(c))
            continue;
        if (saw_padding)
            return 0;

        if (c == '=') {
            quartet[quartet_len++] = -1;
        } else {
            int value = pem_base64_value(c);
            if (value < 0)
                return 0;
            quartet[quartet_len++] = value;
        }
        if (quartet_len != 4)
            continue;

        if (quartet[0] < 0 || quartet[1] < 0 ||
            (quartet[2] < 0 && quartet[3] >= 0))
            return 0;
        size_t emit = quartet[2] < 0 ? 1 :
                      quartet[3] < 0 ? 2 : 3;
        if ((emit == 1 && (quartet[1] & 0x0f) != 0) ||
            (emit == 2 && (quartet[2] & 0x03) != 0))
            return 0;
        if (emit > out_cap - n)
            return -1;

        out_buf[n++] =
            (uint8_t)((quartet[0] << 2) | (quartet[1] >> 4));
        if (emit >= 2) {
            out_buf[n++] =
                (uint8_t)((quartet[1] << 4) | (quartet[2] >> 2));
        }
        if (emit == 3) {
            out_buf[n++] =
                (uint8_t)((quartet[2] << 6) | quartet[3]);
        } else {
            saw_padding = 1;
        }
        quartet_len = 0;
    }
    /* StdEncoding requires a complete padded quartet; leftover data is corrupt. */
    if (quartet_len != 0)
        return 0;

    *decoded_len = n;
    return 1;
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
    const char *search = pem_data;
    if (pem_len >= 3U &&
        (unsigned char)pem_data[0] == 0xEF &&
        (unsigned char)pem_data[1] == 0xBB &&
        (unsigned char)pem_data[2] == 0xBF)
        search = pem_data + 3;
    size_t dash_len = 5;

    for (;;) {
        const char *end_line = pem_find_end_prefix(search, pem_end);
        if (!end_line)
            return -1;

        const char *begin = pem_find_last_begin(search, end_line);
        if (!begin) {
            search = end_line + 9;
            continue;
        }

        const char *line_end =
            (const char *)memchr(begin, '\n', (size_t)(end_line - begin));
        if (!line_end) {
            search = end_line + 9;
            continue;
        }
        const char *header_end = line_end;
        if (header_end > begin && header_end[-1] == '\r')
            --header_end;
        while (header_end > begin &&
               (header_end[-1] == ' ' || header_end[-1] == '\t'))
            --header_end;

        const char *type_start = begin + 11;
        if (header_end < type_start ||
            (size_t)(header_end - type_start) < dash_len ||
            memcmp(header_end - dash_len, DASHES, dash_len) != 0) {
            search = end_line + 9;
            continue;
        }
        const char *type_end = header_end - dash_len;
        size_t type_len = (size_t)(type_end - type_start);

        int bare_headers = 0;
        int reached_end = 0;
        const char *body_start = pem_skip_headers(
            line_end + 1, end_line, &bare_headers, &reached_end);
        /*
         * Go parses headers before checking the END trailer. A ':' on the
         * END line (type injection or a truncated `-----END FOO:`) is then
         * a header; endTrailerIndex goes negative and Decode aborts.
         */
        if (reached_end && pem_end_line_has_colon(end_line, pem_end))
            return -1;
        if (bare_headers) {
            search = end_line + 9;
            continue;
        }
        /* C type is a NUL-terminated string. Skip types that would truncate
         * or that contain controls/non-ASCII (encode rejects the same set;
         * ESC/CR in a type is terminal/log injection if printed). */
        int type_ok = 1;
        for (size_t i = 0; i < type_len; ++i) {
            unsigned char c = (unsigned char)type_start[i];
            if (c < 0x20 || c > 0x7e) {
                type_ok = 0;
                break;
            }
        }
        if (!type_ok) {
            search = end_line + 9;
            continue;
        }

        const char *end_type = end_line + 9;
        if ((size_t)(pem_end - end_type) < type_len + dash_len ||
            memcmp(end_type, type_start, type_len) != 0 ||
            memcmp(end_type + type_len, DASHES, dash_len) != 0) {
            search = end_line + 9;
            continue;
        }

        /* Remainder of the END line must be only spaces/tabs, like Go getLine. */
        const char *after_dashes = end_type + type_len + dash_len;
        const char *eol = (const char *)memchr(
            after_dashes, '\n', (size_t)(pem_end - after_dashes));
        const char *content_end = eol ? eol : pem_end;
        if (eol && content_end > after_dashes && content_end[-1] == '\r')
            --content_end;
        while (content_end > after_dashes &&
               (content_end[-1] == ' ' || content_end[-1] == '\t'))
            --content_end;
        if (content_end != after_dashes) {
            search = end_line + 9;
            continue;
        }
        const char *after_end = eol ? eol + 1 : pem_end;

        size_t decoded_len = 0;
        int body = pem_decode_body(
            out_buf, out_cap, body_start, end_line, &decoded_len);
        if (body == 0) {
            search = end_line + 9;
            continue;
        }
        if (body < 0)
            return -1;
        if (type_len >= type_cap)
            return -1;

        memcpy(type_buf, type_start, type_len);
        type_buf[type_len] = '\0';
        if (bytes_written)
            *bytes_written = decoded_len;
        if (rest_offset)
            *rest_offset = (size_t)(after_end - pem_data);
        return 0;
    }
}
