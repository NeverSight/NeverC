#include "neverc/std/mime/multipart.h"
#include "neverc/std/_platform.h"
#include <string.h>
#include <stdio.h>

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

static const unsigned char *find_boundary(const unsigned char *data, size_t len,
                                           const char *marker, size_t marker_len) {
    if (len < marker_len) return NULL;
    for (size_t i = 0; i <= len - marker_len; i++) {
        if (memcmp(data + i, marker, marker_len) == 0)
            return data + i;
    }
    return NULL;
}

static int parse_headers(const unsigned char *data, size_t len,
                         neverc_multipart_part_t *part, size_t *body_offset) {
    part->header_count = 0;
    size_t i = 0;

    while (i < len) {
        /* Empty line = end of headers */
        if (data[i] == '\r' && i + 1 < len && data[i+1] == '\n') {
            *body_offset = i + 2;
            return 0;
        }
        if (data[i] == '\n') {
            *body_offset = i + 1;
            return 0;
        }

        /* Find colon */
        size_t colon = i;
        while (colon < len && data[colon] != ':' && data[colon] != '\n') colon++;
        if (colon >= len || data[colon] != ':') break;

        if (part->header_count >= NEVERC_MULTIPART_MAX_HEADERS) break;

        neverc_multipart_header_t *h = &part->headers[part->header_count];
        size_t klen = colon - i;
        if (klen >= sizeof(h->key)) klen = sizeof(h->key) - 1;
        memcpy(h->key, data + i, klen);
        h->key[klen] = '\0';

        /* Skip colon and optional whitespace */
        size_t vstart = colon + 1;
        while (vstart < len && (data[vstart] == ' ' || data[vstart] == '\t')) vstart++;

        /* Find end of value */
        size_t vend = vstart;
        while (vend < len && data[vend] != '\r' && data[vend] != '\n') vend++;

        size_t vlen = vend - vstart;
        if (vlen >= sizeof(h->value)) vlen = sizeof(h->value) - 1;
        memcpy(h->value, data + vstart, vlen);
        h->value[vlen] = '\0';

        part->header_count++;

        /* Skip line ending */
        if (vend < len && data[vend] == '\r') vend++;
        if (vend < len && data[vend] == '\n') vend++;
        i = vend;
    }
    *body_offset = i;
    return 0;
}

int neverc_multipart_parse(const unsigned char *data, size_t data_len,
                           const char *boundary,
                           neverc_multipart_reader_t *out) {
    if (!data || !boundary || !out) return -1;
    memset(out, 0, sizeof(*out));

    size_t blen = strlen(boundary);
    char delim[256], end_delim[256];
    int dlen = snprintf(delim, sizeof(delim), "--%s", boundary);
    int edlen = snprintf(end_delim, sizeof(end_delim), "--%s--", boundary);

    /* Find first boundary */
    const unsigned char *pos = find_boundary(data, data_len, delim, (size_t)dlen);
    if (!pos) return -1;

    /* Skip past first boundary line */
    pos += (size_t)dlen;
    size_t remaining = data_len - (size_t)(pos - data);
    if (remaining >= 2 && pos[0] == '\r' && pos[1] == '\n') { pos += 2; remaining -= 2; }
    else if (remaining >= 1 && pos[0] == '\n') { pos += 1; remaining -= 1; }

    while (remaining > 0 && out->part_count < NEVERC_MULTIPART_MAX_PARTS) {
        /* Find next boundary */
        const unsigned char *next = find_boundary(pos, remaining, delim, (size_t)dlen);
        if (!next) break;

        /* Part data is between pos and next (minus preceding \r\n) */
        size_t part_len = (size_t)(next - pos);
        if (part_len >= 2 && pos[part_len-2] == '\r' && pos[part_len-1] == '\n') part_len -= 2;
        else if (part_len >= 1 && pos[part_len-1] == '\n') part_len -= 1;

        /* Parse headers and body */
        neverc_multipart_part_t *part = &out->parts[out->part_count];
        size_t body_offset = 0;
        parse_headers(pos, part_len, part, &body_offset);
        part->body = pos + body_offset;
        part->body_len = part_len - body_offset;
        out->part_count++;

        /* Check if this is the closing boundary */
        if (memcmp(next, end_delim, (size_t)edlen) == 0) break;

        /* Move past boundary */
        pos = next + (size_t)dlen;
        remaining = data_len - (size_t)(pos - data);
        if (remaining >= 2 && pos[0] == '\r' && pos[1] == '\n') { pos += 2; remaining -= 2; }
        else if (remaining >= 1 && pos[0] == '\n') { pos += 1; remaining -= 1; }
    }

    return 0;
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
    neverc_platform_random(rnd, sizeof(rnd));
    int pos = 0;
    for (int i = 0; i < 16 && (size_t)pos + 2 < cap; i++) {
        static const char hex[] = "0123456789abcdef";
        buf[pos++] = hex[rnd[i] >> 4];
        buf[pos++] = hex[rnd[i] & 0x0f];
    }
    buf[pos] = '\0';
    return pos;
}

int neverc_multipart_write(const neverc_multipart_part_t *parts, int count,
                           const char *boundary,
                           unsigned char *out, size_t out_cap) {
    if (!parts || !boundary || !out || count <= 0) return -1;
    size_t pos = 0;

    for (int p = 0; p < count; p++) {
        /* Write boundary */
        int n = snprintf((char*)out + pos, out_cap - pos, "--%s\r\n", boundary);
        if (n < 0 || pos + (size_t)n >= out_cap) return -1;
        pos += (size_t)n;

        /* Write headers */
        const neverc_multipart_part_t *part = &parts[p];
        for (int h = 0; h < part->header_count; h++) {
            n = snprintf((char*)out + pos, out_cap - pos, "%s: %s\r\n",
                         part->headers[h].key, part->headers[h].value);
            if (n < 0 || pos + (size_t)n >= out_cap) return -1;
            pos += (size_t)n;
        }

        /* Empty line before body */
        if (pos + 2 >= out_cap) return -1;
        out[pos++] = '\r'; out[pos++] = '\n';

        /* Write body */
        if (part->body && part->body_len > 0) {
            if (pos + part->body_len >= out_cap) return -1;
            memcpy(out + pos, part->body, part->body_len);
            pos += part->body_len;
        }
        if (pos + 2 >= out_cap) return -1;
        out[pos++] = '\r'; out[pos++] = '\n';
    }

    /* Closing boundary */
    int n = snprintf((char*)out + pos, out_cap - pos, "--%s--\r\n", boundary);
    if (n < 0 || pos + (size_t)n >= out_cap) return -1;
    pos += (size_t)n;

    return (int)pos;
}
