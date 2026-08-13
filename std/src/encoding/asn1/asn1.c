#include "neverc/std/encoding/asn1.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

int neverc_asn1_decode_element(const uint8_t *data, size_t len,
                               neverc_asn1_element_t *elem) {
    if (!data || !elem || len < 2) return -1;
    size_t pos = 0;

    uint8_t tag_byte = data[pos++];
    elem->tag_class = tag_byte & 0xC0;
    elem->constructed = (tag_byte & 0x20) ? 1 : 0;
    elem->tag_number = tag_byte & 0x1F;

    if (elem->tag_number == 0x1F) {
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
        elem->tag_number = (int)tn;
    }

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
        for (int i = 0; i < nbytes; i++)
            value_len = (value_len << 8) | data[pos++];
        if (value_len < 0x80) return -1;
    }

    if (value_len > len - pos || value_len > (size_t)INT_MAX - pos)
        return -1;
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
     * value on every two's-complement target (all of ours). */
    uint64_t v = (elem->value[0] & 0x80) ? ~(uint64_t)0 : 0;
    for (size_t i = 0; i < elem->value_len; i++)
        v = (v << 8) | elem->value[i];
    *val = (int64_t)v;
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

int neverc_asn1_encode_tag(uint8_t *buf, size_t cap, int tag_class,
                           int constructed, int tag_number) {
    if (!buf || cap < 1 || tag_number < 0 ||
        (tag_class != NEVERC_ASN1_UNIVERSAL &&
         tag_class != NEVERC_ASN1_APPLICATION &&
         tag_class != NEVERC_ASN1_CONTEXT &&
         tag_class != NEVERC_ASN1_PRIVATE))
        return -1;
    if (tag_number < 0x1F) {
        buf[0] = (uint8_t)(tag_class | (constructed ? 0x20 : 0) | tag_number);
        return 1;
    }
    buf[0] = (uint8_t)(tag_class | (constructed ? 0x20 : 0) | 0x1F);
    int pos = 1;
    uint8_t tmp[8];
    int n = 0;
    unsigned int tn = (unsigned int)tag_number;
    while (tn > 0) { tmp[n++] = tn & 0x7F; tn >>= 7; }
    for (int i = n - 1; i >= 0; i--) {
        if ((size_t)pos >= cap) return -1;
        buf[pos++] = tmp[i] | (i > 0 ? 0x80 : 0);
    }
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
    uint8_t bytes[9];
    int n = 0;

    if (val == 0) {
        bytes[0] = 0; n = 1;
    } else if (val > 0) {
        uint64_t v = (uint64_t)val;
        uint8_t tmp[8]; int tn = 0;
        while (v > 0) { tmp[tn++] = v & 0xFF; v >>= 8; }
        if (tmp[tn-1] & 0x80) bytes[n++] = 0;
        for (int i = tn - 1; i >= 0; i--) bytes[n++] = tmp[i];
    } else {
        int64_t v = val;
        uint8_t tmp[8]; int tn = 0;
        while (v != -1 || tn == 0) {
            tmp[tn++] = (uint8_t)(v & 0xFF);
            v >>= 8;
            if (tn >= 8) break;
        }
        if (!(tmp[tn-1] & 0x80)) bytes[n++] = 0xFF;
        for (int i = tn - 1; i >= 0; i--) bytes[n++] = tmp[i];
    }

    int tlen = neverc_asn1_encode_tag(buf, cap, NEVERC_ASN1_UNIVERSAL, 0,
                                       NEVERC_ASN1_INTEGER);
    if (tlen < 0) return -1;
    int llen = neverc_asn1_encode_length(buf + tlen, cap - tlen, n);
    if (llen < 0) return -1;
    if ((size_t)(tlen + llen + n) > cap) return -1;
    memcpy(buf + tlen + llen, bytes, n);
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
    if (!buf || (!data && len != 0) || len > (size_t)INT_MAX) return -1;
    int tlen = neverc_asn1_encode_tag(buf, cap, NEVERC_ASN1_UNIVERSAL, 0,
                                       NEVERC_ASN1_OCTET_STRING);
    if (tlen < 0) return -1;
    int llen = neverc_asn1_encode_length(buf + tlen, cap - tlen, len);
    if (llen < 0) return -1;
    size_t prefix = (size_t)tlen + (size_t)llen;
    if (len > (size_t)INT_MAX - prefix || len > cap - prefix) return -1;
    if (len != 0) memcpy(buf + tlen + llen, data, len);
    return (int)(prefix + len);
}

int neverc_asn1_encode_null(uint8_t *buf, size_t cap) {
    if (!buf || cap < 2) return -1;
    buf[0] = NEVERC_ASN1_NULL;
    buf[1] = 0;
    return 2;
}
