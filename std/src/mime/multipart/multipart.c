#include "neverc/std/mime/multipart.h"
#include "neverc/std/_platform.h"
#include "../../bytes/strsearch.h"
#include <limits.h>
#include <string.h>
#include <stdio.h>

#ifndef NCI_MULTIPART_RANDOM
#define NCI_MULTIPART_RANDOM neverc_platform_random
#endif

static int ci_strncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return (unsigned char)ca - (unsigned char)cb;
        if (ca == '\0') return 0;
    }
    return 0;
}

/* Locate `marker` inside `data` via the shared substring engine: memchr to the
 * first byte then Boyer-Moore-Horspool (with a Two-Way guard for adversarial
 * inputs). Replaces a naive O(len*marker_len) scan that re-compared the whole
 * marker at every offset — pathological for MB-sized upload bodies. The finder
 * is preprocessed once by the caller and reused across every part. */
static const unsigned char *find_boundary_f(const nci_ss_finder_t *f,
                                             const unsigned char *data, size_t len) {
    size_t off = nci_ss_finder_next(f, data, len);
    return (off == SIZE_MAX) ? NULL : data + off;
}

static size_t multipart_boundary_length(const char *boundary) {
    if (!boundary) return 0;
    size_t length = 0;
    while (boundary[length] != '\0') {
        unsigned char c = (unsigned char)boundary[length];
        int valid = (c >= 'A' && c <= 'Z') ||
                    (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9') ||
                    c == '\'' || c == '(' || c == ')' || c == '+' ||
                    c == '_' || c == ',' || c == '-' || c == '.' ||
                    c == '/' || c == ':' || c == '=' || c == '?' ||
                    c == ' ';
        if (!valid || length == 70) return 0;
        length++;
    }
    if (length == 0 || boundary[length - 1] == ' ') return 0;
    return length;
}

static int multipart_header_name_valid(const char *name) {
    if (!name || *name == '\0') return 0;
    for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
        unsigned char c = *p;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '!' || c == '#' || c == '$' ||
            c == '%' || c == '&' || c == '\'' || c == '*' || c == '+' ||
            c == '-' || c == '.' || c == '^' || c == '_' || c == '`' ||
            c == '|' || c == '~')
            continue;
        return 0;
    }
    return 1;
}

static int multipart_header_value_valid(const unsigned char *s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        unsigned char c = s[i];
        if ((c < 0x20 && c != '\t') || c == 0x7f)
            return 0;
    }
    return 1;
}

/* True when `--boundary` at suffix_offset is a whole delimiter line.
 * extra_crlf: pretend a CRLF follows `data` (write appends `\r\n` after the
 * body, so a body that is exactly `--bnd` / `--bnd--` / `--bnd` + LWSP would
 * become a real delimiter on the wire). A prefix such as `--bndX` is not. */
static int delimiter_after_marker(const unsigned char *data, size_t length,
                                  size_t suffix_offset, int extra_crlf,
                                  int *closing, const unsigned char **after) {
    size_t k = suffix_offset;
    int is_close = (k + 1 < length && data[k] == '-' && data[k + 1] == '-');
    if (is_close)
        k += 2;
    while (k < length && (data[k] == ' ' || data[k] == '\t'))
        k++;

    if (k == length) {
        if (is_close || extra_crlf) {
            if (closing) *closing = is_close;
            if (after) *after = data + k;
            return 1;
        }
        return 0;
    }
    if (k + 1 < length && data[k] == '\r' && data[k + 1] == '\n') {
        if (closing) *closing = is_close;
        if (after) *after = data + k + 2;
        return 1;
    }
    if (data[k] == '\n') {
        if (closing) *closing = is_close;
        if (after) *after = data + k + 1;
        return 1;
    }
    /* Lone CR is a line ending only at EOF of the message, not when the
     * writer will append another `\r\n` (`--bnd\r` + `\r\n` is `\r\r\n`). */
    if (k + 1 == length && data[k] == '\r' && !extra_crlf) {
        if (closing) *closing = is_close;
        if (after) *after = data + k + 1;
        return 1;
    }
    return 0;
}

static const unsigned char *find_boundary_line(
    const nci_ss_finder_t *finder, const unsigned char *data, size_t length,
    size_t marker_length, int *closing, const unsigned char **after) {
    size_t search_offset = 0;
    while (search_offset <= length) {
        const unsigned char *candidate = find_boundary_f(
            finder, data + search_offset, length - search_offset);
        if (!candidate) return NULL;
        size_t offset = (size_t)(candidate - data);
        int line_start = offset == 0 || data[offset - 1] == '\n';
        if (line_start &&
            delimiter_after_marker(data, length, offset + marker_length, 0,
                                   closing, after))
            return candidate;
        search_offset = offset + 1;
    }
    return NULL;
}

/* Body is written, then `\r\n` and the next dash-boundary. Reject only a
 * real delimiter line (including one completed by that trailing CRLF), not
 * a prefix such as `--bndX` or `--bnd\rnot-a-break`. */
static int body_injects_boundary(const unsigned char *body, size_t body_len,
                                 const unsigned char *delim, size_t dlen) {
    if (!body || body_len < dlen)
        return 0;
    for (size_t i = 0; i + dlen <= body_len; i++) {
        if ((i == 0 || body[i - 1] == '\n') &&
            memcmp(body + i, delim, dlen) == 0 &&
            delimiter_after_marker(body, body_len, i + dlen, 1, NULL, NULL))
            return 1;
    }
    return 0;
}

static int parse_headers(const unsigned char *data, size_t len,
                         neverc_multipart_part_t *part, size_t *body_offset) {
    part->header_count = 0;
    size_t i = 0;

    while (i < len) {
        size_t line_feed = i;
        while (line_feed < len && data[line_feed] != '\n') line_feed++;
        if (line_feed == len) return -1;
        size_t line_end = line_feed;
        if (line_end > i && data[line_end - 1] == '\r') line_end--;
        if (line_end == i) {
            *body_offset = line_feed + 1;
            return 0;
        }

        /* RFC 5322 folded header: a line that starts with WSP continues the
         * previous field. Reject a leading fold with no header to attach to. */
        if (data[i] == ' ' || data[i] == '\t') {
            if (part->header_count == 0)
                return -1;
            neverc_multipart_header_t *prev =
                &part->headers[part->header_count - 1];
            size_t vstart = i;
            while (vstart < line_end &&
                   (data[vstart] == ' ' || data[vstart] == '\t'))
                vstart++;
            size_t add = line_end - vstart;
            size_t cur = strlen(prev->value);
            if (add > 0) {
                if (cur + 1 + add >= sizeof(prev->value) ||
                    !multipart_header_value_valid(data + vstart, add))
                    return -1;
                prev->value[cur] = ' ';
                memcpy(prev->value + cur + 1, data + vstart, add);
                prev->value[cur + 1 + add] = '\0';
            }
            i = line_feed + 1;
            continue;
        }

        size_t colon = i;
        while (colon < line_end && data[colon] != ':') colon++;
        if (colon == i || colon == line_end ||
            part->header_count >= NEVERC_MULTIPART_MAX_HEADERS)
            return -1;
        neverc_multipart_header_t *h = &part->headers[part->header_count];
        size_t klen = colon - i;
        if (klen >= sizeof(h->key)) return -1;
        for (size_t k = i; k < colon; k++) {
            unsigned char c = data[k];
            if (c <= 0x20 || c >= 0x7f) return -1;
        }
        memcpy(h->key, data + i, klen);
        h->key[klen] = '\0';
        if (!multipart_header_name_valid(h->key)) return -1;

        size_t vstart = colon + 1;
        while (vstart < line_end &&
               (data[vstart] == ' ' || data[vstart] == '\t'))
            vstart++;
        size_t vlen = line_end - vstart;
        if (vlen >= sizeof(h->value) ||
            !multipart_header_value_valid(data + vstart, vlen))
            return -1;
        memcpy(h->value, data + vstart, vlen);
        h->value[vlen] = '\0';
        part->header_count++;
        i = line_feed + 1;
    }
    return -1;
}

int neverc_multipart_parse(const unsigned char *data, size_t data_len,
                           const char *boundary,
                           neverc_multipart_reader_t *out) {
    if (!data || !boundary || !out) return -1;
    memset(out, 0, sizeof(*out));

    size_t boundary_length = multipart_boundary_length(boundary);
    if (boundary_length == 0) return -1;
    unsigned char delim[72];
    delim[0] = '-';
    delim[1] = '-';
    memcpy(delim + 2, boundary, boundary_length);
    size_t dlen = boundary_length + 2;

    /* Preprocess the boundary delimiter once; reused for every part. */
    nci_ss_finder_t df;
    nci_ss_finder_init(&df, delim, dlen);

    int closing = 0;
    const unsigned char *pos = NULL;
    const unsigned char *first = find_boundary_line(
        &df, data, data_len, dlen, &closing, &pos);
    if (!first) return -1;
    if (closing) return 0;

    while (pos < data + data_len) {
        if (out->part_count >= NEVERC_MULTIPART_MAX_PARTS) goto fail;
        const unsigned char *after = NULL;
        size_t remaining = data_len - (size_t)(pos - data);
        const unsigned char *next = find_boundary_line(
            &df, pos, remaining, dlen, &closing, &after);
        if (!next) goto fail;

        /* Headers + body sit between pos and the next dash-boundary. Do not
         * strip the delimiter's preceding CRLF before parsing headers: when
         * the body is empty that CRLF is also the header terminator
         * (Go: "--b\\r\\nH: v\\r\\n\\r\\n--b--"). */
        size_t part_len = (size_t)(next - pos);

        neverc_multipart_part_t *part = &out->parts[out->part_count];
        size_t body_offset = 0;
        if (parse_headers(pos, part_len, part, &body_offset) != 0)
            goto fail;
        part->body = pos + body_offset;
        size_t body_len = part_len - body_offset;
        /* RFC 2046: the line break immediately before the next dash-boundary
         * belongs to the delimiter, not the body. */
        if (body_len >= 2 && part->body[body_len - 2] == '\r' &&
            part->body[body_len - 1] == '\n')
            body_len -= 2;
        else if (body_len >= 1 && part->body[body_len - 1] == '\n')
            body_len -= 1;
        part->body_len = body_len;
        out->part_count++;

        if (closing) return 0;
        pos = after;
    }

fail:
    memset(out, 0, sizeof(*out));
    return -1;
}

const char *neverc_multipart_part_header(const neverc_multipart_part_t *part,
                                         const char *key) {
    if (!part || !key) return NULL;
    size_t klen = strlen(key);
    for (int i = 0; i < part->header_count; i++) {
        if (ci_strncmp(part->headers[i].key, key, klen + 1) == 0)
            return part->headers[i].value;
    }
    return NULL;
}

int neverc_multipart_generate_boundary(char *buf, size_t cap) {
    if (!buf || cap < 40) return -1;
    unsigned char rnd[16];
    if (NCI_MULTIPART_RANDOM(rnd, sizeof(rnd)) != 0) {
        neverc_platform_secure_zero(rnd, sizeof(rnd));
        buf[0] = '\0';
        return -1;
    }
    int pos = 0;
    for (int i = 0; i < 16 && (size_t)pos + 2 < cap; i++) {
        static const char hex[] = "0123456789abcdef";
        buf[pos++] = hex[rnd[i] >> 4];
        buf[pos++] = hex[rnd[i] & 0x0f];
    }
    buf[pos] = '\0';
    neverc_platform_secure_zero(rnd, sizeof(rnd));
    return pos;
}

int neverc_multipart_write(const neverc_multipart_part_t *parts, int count,
                           const char *boundary,
                           unsigned char *out, size_t out_cap) {
    if (!parts || !boundary || !out || count <= 0 ||
        count > NEVERC_MULTIPART_MAX_PARTS ||
        multipart_boundary_length(boundary) == 0)
        return -1;
    size_t pos = 0;

    for (int p = 0; p < count; p++) {
        /* Write boundary */
        int n = snprintf((char*)out + pos, out_cap - pos, "--%s\r\n", boundary);
        if (n < 0 || (size_t)n >= out_cap - pos) return -1;
        pos += (size_t)n;

        /* Write headers */
        const neverc_multipart_part_t *part = &parts[p];
        if (part->header_count < 0 ||
            part->header_count > NEVERC_MULTIPART_MAX_HEADERS ||
            (part->body_len > 0 && !part->body))
            return -1;
        for (int h = 0; h < part->header_count; h++) {
            const char *key = part->headers[h].key;
            const char *value = part->headers[h].value;
            if (!memchr(key, '\0', sizeof(part->headers[h].key)) ||
                !memchr(value, '\0', sizeof(part->headers[h].value)) ||
                !multipart_header_name_valid(key) || strchr(value, '\r') ||
                strchr(value, '\n'))
                return -1;
            n = snprintf((char*)out + pos, out_cap - pos, "%s: %s\r\n",
                         key, value);
            if (n < 0 || (size_t)n >= out_cap - pos) return -1;
            pos += (size_t)n;
        }

        /* Empty line before body */
        if (out_cap - pos <= 2) return -1;
        out[pos++] = '\r'; out[pos++] = '\n';

        /* Write body. Reject a body that would inject a real delimiter line
         * after the header CRLF (start-of-body `--bnd\r\n` or `\n--bnd`). */
        if (part->body_len > 0) {
            size_t blen = multipart_boundary_length(boundary);
            unsigned char delim[72];
            delim[0] = '-';
            delim[1] = '-';
            memcpy(delim + 2, boundary, blen);
            size_t dlen = blen + 2;
            if (body_injects_boundary(part->body, part->body_len, delim, dlen))
                return -1;
            if (part->body_len >= out_cap - pos) return -1;
            memcpy(out + pos, part->body, part->body_len);
            pos += part->body_len;
        }
        if (out_cap - pos <= 2) return -1;
        out[pos++] = '\r'; out[pos++] = '\n';
    }

    /* Closing boundary */
    int n = snprintf((char*)out + pos, out_cap - pos, "--%s--\r\n", boundary);
    if (n < 0 || (size_t)n >= out_cap - pos || pos > (size_t)INT_MAX ||
        (size_t)n > (size_t)INT_MAX - pos)
        return -1;
    pos += (size_t)n;

    return (int)pos;
}
