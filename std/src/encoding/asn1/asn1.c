#include "neverc/std/encoding/asn1.h"
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int neverc_asn1_decode_element(const uint8_t *data, size_t len,
                               neverc_asn1_element_t *elem) {
    if (!elem) return -1;
    memset(elem, 0, sizeof(*elem));
    if (!data || len < 2) return -1;
    size_t pos = 0;

    uint8_t tag_byte = data[pos++];
    int tag_class = tag_byte & 0xC0;
    int constructed = (tag_byte & 0x20) ? 1 : 0;
    int tag_number = tag_byte & 0x1F;

    if (tag_number == 0x1F) {
        /* High-tag-number form. Accumulate in a 64-bit unsigned (the shift is
         * well-defined there — int overflowed its sign bit, which is UB, and
         * `long` is only 32-bit on Windows/LLP64) and reject tags that exceed
         * the int field so the value can't wrap into a negative/garbage tag. */
        uint64_t tn = 0;
        int got = 0;
        int terminated = 0;
        while (pos < len) {
            uint8_t b = data[pos++];
            if (!got && b == 0x80) return -1;
            tn = (tn << 7) | (uint64_t)(b & 0x7F);
            got = 1;
            if (tn > 0x7FFFFFFF) return -1;   /* tag number too large */
            if (!(b & 0x80)) {
                terminated = 1;
                break;
            }
        }
        if (!got || !terminated || tn < 0x1F) return -1;
        tag_number = (int)tn;
    }
    /* Tag zero is reserved for BER's end-of-contents marker. DER forbids
     * indefinite lengths, so an EOC element can never be valid DER input. */
    if (tag_class == NEVERC_ASN1_UNIVERSAL && tag_number == 0)
        return -1;

    if (pos >= len) return -1;
    uint8_t len_byte = data[pos++];
    size_t value_len;

    if (len_byte < 0x80) {
        value_len = len_byte;
    } else {
        int nbytes = len_byte & 0x7F;
        if (nbytes == 0 || nbytes > 4 || (size_t)nbytes > len - pos)
            return -1;
        if (data[pos] == 0) return -1;
        value_len = 0;
        for (int i = 0; i < nbytes; i++) {
            if (value_len > (SIZE_MAX >> 8)) return -1;
            value_len = (value_len << 8) | data[pos++];
        }
        if (value_len < 0x80) return -1;
    }

    if (value_len > len - pos || value_len > (size_t)INT_MAX - pos)
        return -1;
    elem->tag_class = tag_class;
    elem->tag_number = tag_number;
    elem->constructed = constructed;
    elem->value = data + pos;
    elem->value_len = value_len;
    elem->full = data;
    elem->full_len = pos + value_len;
    return (int)(pos + value_len);
}

int neverc_asn1_decode_int64(const neverc_asn1_element_t *elem, int64_t *val) {
    if (!elem || !val || !elem->value) return -1;
    if (elem->tag_class != NEVERC_ASN1_UNIVERSAL || elem->constructed ||
        elem->tag_number != NEVERC_ASN1_INTEGER)
        return -1;
    if (elem->value_len == 0 || elem->value_len > 8) return -1;
    if (elem->value_len > 1 &&
        ((elem->value[0] == 0x00 && (elem->value[1] & 0x80U) == 0) ||
         (elem->value[0] == 0xff && (elem->value[1] & 0x80U) != 0)))
        return -1;

    /* DER INTEGER is big-endian two's complement. Accumulate in uint64_t so the
     * shift is always well-defined — `(v << 8)` on a negative int64_t is UB. The
     * leading 0xFF... sign fill makes the final reinterpret reproduce the signed
     * value on every two's-complement target (all of ours).
     *
     * An 8-byte payload that starts with 0x00 0x80.. is a canonical encoding of
     * 2^63..2^64-1, which does not fit in int64_t. The uint64→int64 conversion
     * would wrap those into negatives (2^63 → INT64_MIN). Reject a sign change. */
    int negative = (elem->value[0] & 0x80) != 0;
    uint64_t v = negative ? ~(uint64_t)0 : 0;
    for (size_t i = 0; i < elem->value_len; i++)
        v = (v << 8) | elem->value[i];
    int64_t decoded = (int64_t)v;
    if (negative != (decoded < 0))
        return -1;
    *val = decoded;
    return 0;
}

int neverc_asn1_decode_bool(const neverc_asn1_element_t *elem, int *val) {
    if (!elem || !val || !elem->value) return -1;
    if (elem->tag_class != NEVERC_ASN1_UNIVERSAL || elem->constructed ||
        elem->tag_number != NEVERC_ASN1_BOOLEAN)
        return -1;
    if (elem->value_len != 1) return -1;
    if (elem->value[0] != 0x00 && elem->value[0] != 0xff) return -1;
    *val = elem->value[0] ? 1 : 0;
    return 0;
}

static int oid_read_subidentifier(const uint8_t *data, size_t length,
                                  size_t *offset, uint64_t *value) {
    uint64_t result = 0;
    int first = 1;
    while (*offset < length) {
        uint8_t byte = data[(*offset)++];
        if (first && byte == 0x80) return -1;
        first = 0;
        uint64_t digit = byte & 0x7fU;
        if (result > (UINT64_MAX - digit) / 128U) return -1;
        result = result * 128U + digit;
        if ((byte & 0x80U) == 0) {
            *value = result;
            return 0;
        }
    }
    return -1;
}

static int oid_append_arc(char *buffer, size_t capacity, size_t *length,
                          uint64_t value) {
    char digits[20];
    size_t count = 0;
    do {
        digits[count++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0);

    if (*length != 0) {
        if (*length >= capacity) return -1;
        buffer[(*length)++] = '.';
    }
    if (count > capacity - *length) return -1;
    while (count != 0)
        buffer[(*length)++] = digits[--count];
    return 0;
}

char *neverc_asn1_decode_oid(const neverc_asn1_element_t *elem) {
    if (!elem || elem->tag_class != NEVERC_ASN1_UNIVERSAL ||
        elem->constructed || elem->tag_number != NEVERC_ASN1_OID ||
        !elem->value || elem->value_len == 0 ||
        elem->value_len > (SIZE_MAX - 32U) / 4U)
        return NULL;

    size_t capacity = elem->value_len * 4U + 32U;
    char *result = (char *)malloc(capacity);
    if (!result) return NULL;

    size_t offset = 0;
    size_t result_len = 0;
    uint64_t combined;
    if (oid_read_subidentifier(elem->value, elem->value_len,
                               &offset, &combined) != 0) {
        free(result);
        return NULL;
    }

    uint64_t first = combined < 40U ? 0U : combined < 80U ? 1U : 2U;
    uint64_t second = combined - first * 40U;
    if (oid_append_arc(result, capacity - 1U, &result_len, first) != 0 ||
        oid_append_arc(result, capacity - 1U, &result_len, second) != 0) {
        free(result);
        return NULL;
    }

    while (offset < elem->value_len) {
        uint64_t arc;
        if (oid_read_subidentifier(elem->value, elem->value_len,
                                   &offset, &arc) != 0 ||
            oid_append_arc(result, capacity - 1U, &result_len, arc) != 0) {
            free(result);
            return NULL;
        }
    }
    result[result_len] = '\0';
    return result;
}

static int asn1_encoded_length_size(size_t length) {
    if (length > (size_t)INT_MAX) return -1;
    if (length < 0x80) return 1;
    int nbytes = 0;
    size_t tmp = length;
    while (tmp > 0) {
        nbytes++;
        tmp >>= 8;
    }
    return 1 + nbytes;
}

/* Universal tags used by the typed encoders are all < 31, so the tag is
 * one byte. Reject before writing so a too-small cap cannot leave a
 * truncated TLV prefix in buf. */
static int asn1_tlv_fits(size_t cap, size_t payload) {
    int llen = asn1_encoded_length_size(payload);
    if (llen < 0) return 0;
    size_t prefix = 1U + (size_t)llen;
    return payload <= (size_t)INT_MAX - prefix && prefix + payload <= cap;
}

int neverc_asn1_encode_tag(uint8_t *buf, size_t cap, int tag_class,
                           int constructed, int tag_number) {
    if (!buf || cap < 1 || tag_number < 0 ||
        (tag_class == NEVERC_ASN1_UNIVERSAL && tag_number == 0) ||
        (tag_class != NEVERC_ASN1_UNIVERSAL &&
         tag_class != NEVERC_ASN1_APPLICATION &&
         tag_class != NEVERC_ASN1_CONTEXT &&
         tag_class != NEVERC_ASN1_PRIVATE))
        return -1;
    uint8_t lead = (uint8_t)(tag_class | (constructed ? 0x20 : 0));
    if (tag_number < 0x1F) {
        buf[0] = (uint8_t)(lead | tag_number);
        return 1;
    }
    uint8_t tmp[8];
    int n = 0;
    unsigned int tn = (unsigned int)tag_number;
    while (tn > 0) { tmp[n++] = tn & 0x7F; tn >>= 7; }
    if ((size_t)(1 + n) > cap) return -1;
    buf[0] = (uint8_t)(lead | 0x1F);
    int pos = 1;
    for (int i = n - 1; i >= 0; i--)
        buf[pos++] = tmp[i] | (i > 0 ? 0x80 : 0);
    return pos;
}

int neverc_asn1_encode_length(uint8_t *buf, size_t cap, size_t length) {
    if (!buf || length > (size_t)INT_MAX) return -1;
    if (length < 0x80) {
        if (cap < 1) return -1;
        buf[0] = (uint8_t)length;
        return 1;
    }
    int nbytes = 0;
    size_t tmp = length;
    while (tmp > 0) { nbytes++; tmp >>= 8; }
    if ((size_t)(1 + nbytes) > cap) return -1;
    buf[0] = (uint8_t)(0x80 | nbytes);
    for (int i = nbytes - 1; i >= 0; i--)
        buf[1 + (nbytes - 1 - i)] = (uint8_t)(length >> (i * 8));
    return 1 + nbytes;
}

int neverc_asn1_encode_int64(uint8_t *buf, size_t cap, int64_t val) {
    if (!buf) return -1;
    uint8_t bytes[8];
    uint64_t bits = (uint64_t)val;
    for (int i = 0; i < 8; i++)
        bytes[i] = (uint8_t)(bits >> (56 - i * 8));

    int start = 0;
    while (start < 7 &&
           ((bytes[start] == 0x00 && (bytes[start + 1] & 0x80U) == 0) ||
            (bytes[start] == 0xff && (bytes[start + 1] & 0x80U) != 0)))
        start++;
    int n = 8 - start;
    /* INTEGER tag 2 is always one byte; n is 1..8 so the length is short. */
    if ((size_t)(2 + n) > cap) return -1;

    int tlen = neverc_asn1_encode_tag(buf, cap, NEVERC_ASN1_UNIVERSAL, 0,
                                       NEVERC_ASN1_INTEGER);
    if (tlen < 0) return -1;
    int llen = neverc_asn1_encode_length(buf + tlen, cap - tlen, n);
    if (llen < 0) return -1;
    memcpy(buf + tlen + llen, bytes + start, (size_t)n);
    return tlen + llen + n;
}

int neverc_asn1_encode_bool(uint8_t *buf, size_t cap, int val) {
    if (!buf || cap < 3) return -1;
    buf[0] = NEVERC_ASN1_BOOLEAN;
    buf[1] = 1;
    buf[2] = val ? 0xFF : 0x00;
    return 3;
}

int neverc_asn1_encode_octet_string(uint8_t *buf, size_t cap,
                                    const uint8_t *data, size_t len) {
    if (!buf || (!data && len != 0) || !asn1_tlv_fits(cap, len)) return -1;
    int tlen = neverc_asn1_encode_tag(buf, cap, NEVERC_ASN1_UNIVERSAL, 0,
                                       NEVERC_ASN1_OCTET_STRING);
    if (tlen < 0) return -1;
    int llen = neverc_asn1_encode_length(buf + tlen, cap - tlen, len);
    if (llen < 0) return -1;
    if (len != 0) memcpy(buf + tlen + llen, data, len);
    return tlen + llen + (int)len;
}

int neverc_asn1_encode_null(uint8_t *buf, size_t cap) {
    if (!buf || cap < 2) return -1;
    buf[0] = NEVERC_ASN1_NULL;
    buf[1] = 0;
    return 2;
}

static int asn1_utf8_valid(const uint8_t *data, size_t length) {
    if (!data && length > 0) return 0;
    size_t i = 0;
    while (i < length) {
        uint8_t first = data[i++];
        if (first <= 0x7f) continue;
        unsigned continuation;
        uint32_t codepoint;
        if (first >= 0xc2 && first <= 0xdf) {
            continuation = 1;
            codepoint = first & 0x1fU;
        } else if (first >= 0xe0 && first <= 0xef) {
            continuation = 2;
            codepoint = first & 0x0fU;
        } else if (first >= 0xf0 && first <= 0xf4) {
            continuation = 3;
            codepoint = first & 0x07U;
        } else {
            return 0;
        }
        if (continuation > length - i) return 0;
        for (unsigned j = 0; j < continuation; j++) {
            uint8_t byte = data[i++];
            if ((byte & 0xc0U) != 0x80U) return 0;
            codepoint = (codepoint << 6) | (byte & 0x3fU);
        }
        if ((continuation == 2 && codepoint < 0x800) ||
            (continuation == 3 && codepoint < 0x10000) ||
            (codepoint >= 0xd800 && codepoint <= 0xdfff) ||
            codepoint > 0x10ffff)
            return 0;
    }
    return 1;
}

static int asn1_printable_char(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == ' ' || c == '\'' ||
           c == '(' || c == ')' || c == '+' || c == ',' ||
           c == '-' || c == '.' || c == '/' || c == ':' ||
           c == '=' || c == '?';
}

static int asn1_encode_primitive(uint8_t *buf, size_t cap, int tag,
                                 const uint8_t *data, size_t len) {
    if (!buf || (!data && len != 0) || !asn1_tlv_fits(cap, len)) return -1;
    int tlen = neverc_asn1_encode_tag(buf, cap, NEVERC_ASN1_UNIVERSAL, 0, tag);
    if (tlen < 0) return -1;
    int llen = neverc_asn1_encode_length(buf + tlen, cap - (size_t)tlen, len);
    if (llen < 0) return -1;
    if (len != 0) memcpy(buf + tlen + llen, data, len);
    return tlen + llen + (int)len;
}

static int asn1_decode_primitive_string(const neverc_asn1_element_t *elem,
                                        int tag, const uint8_t **text,
                                        size_t *len) {
    if (!elem || !text || !len || elem->tag_class != NEVERC_ASN1_UNIVERSAL ||
        elem->constructed || elem->tag_number != tag ||
        (elem->value_len > 0 && !elem->value))
        return -1;
    *text = elem->value;
    *len = elem->value_len;
    return 0;
}

int neverc_asn1_decode_bit_string(const neverc_asn1_element_t *elem,
                                  const uint8_t **bytes, size_t *byte_len,
                                  int *unused_bits) {
    if (!elem || !bytes || !byte_len || !unused_bits ||
        elem->tag_class != NEVERC_ASN1_UNIVERSAL || elem->constructed ||
        elem->tag_number != NEVERC_ASN1_BIT_STRING || !elem->value ||
        elem->value_len == 0)
        return -1;
    unsigned unused = elem->value[0];
    if (unused > 7U || (elem->value_len == 1 && unused != 0))
        return -1;
    if (unused != 0 &&
        (elem->value[elem->value_len - 1] &
         (uint8_t)((1U << unused) - 1U)) != 0)
        return -1;
    *unused_bits = (int)unused;
    *bytes = elem->value + 1;
    *byte_len = elem->value_len - 1;
    return 0;
}

int neverc_asn1_encode_bit_string(uint8_t *buf, size_t cap,
                                  const uint8_t *data, size_t len,
                                  int unused_bits) {
    if (!buf || unused_bits < 0 || unused_bits > 7 ||
        (!data && len != 0) || (len == 0 && unused_bits != 0) ||
        len > (size_t)INT_MAX - 1U)
        return -1;
    if (len > 0 && unused_bits > 0 &&
        (data[len - 1] & (uint8_t)((1U << (unsigned)unused_bits) - 1U)) != 0)
        return -1;

    size_t value_len = len + 1U;
    if (!asn1_tlv_fits(cap, value_len)) return -1;
    int tlen = neverc_asn1_encode_tag(buf, cap, NEVERC_ASN1_UNIVERSAL, 0,
                                       NEVERC_ASN1_BIT_STRING);
    if (tlen < 0) return -1;
    int llen = neverc_asn1_encode_length(
        buf + tlen, cap - (size_t)tlen, value_len);
    if (llen < 0) return -1;
    size_t prefix = (size_t)tlen + (size_t)llen;
    buf[prefix] = (uint8_t)unused_bits;
    if (len != 0) memcpy(buf + prefix + 1U, data, len);
    return (int)(prefix + value_len);
}

static int oid_write_subidentifier(uint8_t *out, size_t cap, size_t *len,
                                   uint64_t value) {
    uint8_t tmp[10];
    int n = 0;
    do {
        tmp[n++] = (uint8_t)(value & 0x7fU);
        value >>= 7;
    } while (value != 0);
    if (*len > cap || (size_t)n > cap - *len) return -1;
    for (int i = n - 1; i >= 0; i--)
        out[(*len)++] = (uint8_t)(tmp[i] | (i > 0 ? 0x80U : 0U));
    return 0;
}

static int oid_parse_arc(const char *oid, size_t *offset, uint64_t *arc) {
    size_t i = *offset;
    if (oid[i] < '0' || oid[i] > '9') return -1;
    if (oid[i] == '0' && oid[i + 1] >= '0' && oid[i + 1] <= '9')
        return -1;
    uint64_t value = 0;
    while (oid[i] >= '0' && oid[i] <= '9') {
        uint64_t digit = (uint64_t)(oid[i] - '0');
        if (value > (UINT64_MAX - digit) / 10U) return -1;
        value = value * 10U + digit;
        i++;
    }
    *offset = i;
    *arc = value;
    return 0;
}

int neverc_asn1_encode_oid(uint8_t *buf, size_t cap, const char *oid) {
    if (!buf || !oid || oid[0] == '\0') return -1;

    uint8_t payload[1280];
    size_t payload_len = 0;
    size_t offset = 0;
    uint64_t first = 0, second = 0;
    if (oid_parse_arc(oid, &offset, &first) != 0 || oid[offset] != '.' ||
        first > 2U)
        return -1;
    offset++;
    if (oid_parse_arc(oid, &offset, &second) != 0)
        return -1;
    if ((first < 2U && second > 39U) ||
        (first == 2U && second > UINT64_MAX - 80U))
        return -1;
    if (oid_write_subidentifier(payload, sizeof(payload), &payload_len,
                                first * 40U + second) != 0)
        return -1;

    while (oid[offset] == '.') {
        offset++;
        uint64_t arc = 0;
        if (oid_parse_arc(oid, &offset, &arc) != 0 ||
            oid_write_subidentifier(payload, sizeof(payload), &payload_len,
                                    arc) != 0)
            return -1;
    }
    if (oid[offset] != '\0') return -1;
    return asn1_encode_primitive(buf, cap, NEVERC_ASN1_OID,
                                 payload, payload_len);
}

int neverc_asn1_decode_utf8_string(const neverc_asn1_element_t *elem,
                                   const uint8_t **text, size_t *len) {
    if (asn1_decode_primitive_string(elem, NEVERC_ASN1_UTF8_STRING,
                                     text, len) != 0)
        return -1;
    if (!asn1_utf8_valid(*text, *len)) return -1;
    return 0;
}

int neverc_asn1_encode_utf8_string(uint8_t *buf, size_t cap,
                                   const uint8_t *text, size_t len) {
    if (!asn1_utf8_valid(text, len)) return -1;
    return asn1_encode_primitive(buf, cap, NEVERC_ASN1_UTF8_STRING, text, len);
}

int neverc_asn1_decode_printable_string(const neverc_asn1_element_t *elem,
                                        const uint8_t **text, size_t *len) {
    if (asn1_decode_primitive_string(elem, NEVERC_ASN1_PRINTABLE_STR,
                                     text, len) != 0)
        return -1;
    for (size_t i = 0; i < *len; i++)
        if (!asn1_printable_char((*text)[i])) return -1;
    return 0;
}

int neverc_asn1_encode_printable_string(uint8_t *buf, size_t cap,
                                        const uint8_t *text, size_t len) {
    if ((!text && len != 0)) return -1;
    for (size_t i = 0; i < len; i++)
        if (!asn1_printable_char(text[i])) return -1;
    return asn1_encode_primitive(buf, cap, NEVERC_ASN1_PRINTABLE_STR,
                                 text, len);
}

int neverc_asn1_decode_ia5_string(const neverc_asn1_element_t *elem,
                                  const uint8_t **text, size_t *len) {
    if (asn1_decode_primitive_string(elem, NEVERC_ASN1_IA5_STRING,
                                     text, len) != 0)
        return -1;
    for (size_t i = 0; i < *len; i++)
        if ((*text)[i] > 0x7f) return -1;
    return 0;
}

int neverc_asn1_encode_ia5_string(uint8_t *buf, size_t cap,
                                  const uint8_t *text, size_t len) {
    if ((!text && len != 0)) return -1;
    for (size_t i = 0; i < len; i++)
        if (text[i] > 0x7f) return -1;
    return asn1_encode_primitive(buf, cap, NEVERC_ASN1_IA5_STRING, text, len);
}
