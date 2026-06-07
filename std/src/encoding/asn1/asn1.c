#include "neverc/std/encoding/asn1.h"
#include <stdlib.h>
#include <string.h>

int neverc_asn1_decode_element(const uint8_t *data, size_t len,
                               neverc_asn1_element_t *elem) {
    if (len < 2) return -1;
    size_t pos = 0;

    uint8_t tag_byte = data[pos++];
    elem->tag_class = tag_byte & 0xC0;
    elem->constructed = (tag_byte & 0x20) ? 1 : 0;
    elem->tag_number = tag_byte & 0x1F;

    if (elem->tag_number == 0x1F) {
        elem->tag_number = 0;
        while (pos < len) {
            uint8_t b = data[pos++];
            elem->tag_number = (elem->tag_number << 7) | (b & 0x7F);
            if (!(b & 0x80)) break;
        }
    }

    if (pos >= len) return -1;
    uint8_t len_byte = data[pos++];
    size_t value_len;

    if (len_byte < 0x80) {
        value_len = len_byte;
    } else {
        int nbytes = len_byte & 0x7F;
        if (nbytes > 4 || pos + nbytes > len) return -1;
        value_len = 0;
        for (int i = 0; i < nbytes; i++)
            value_len = (value_len << 8) | data[pos++];
    }

    if (pos + value_len > len) return -1;
    elem->value = data + pos;
    elem->value_len = value_len;
    elem->full = data;
    elem->full_len = pos + value_len;
    return (int)(pos + value_len);
}

int neverc_asn1_decode_int64(const neverc_asn1_element_t *elem, int64_t *val) {
    if (elem->tag_number != NEVERC_ASN1_INTEGER) return -1;
    if (elem->value_len == 0 || elem->value_len > 8) return -1;

    int64_t v = (elem->value[0] & 0x80) ? -1 : 0;
    for (size_t i = 0; i < elem->value_len; i++)
        v = (v << 8) | elem->value[i];
    *val = v;
    return 0;
}

int neverc_asn1_decode_bool(const neverc_asn1_element_t *elem, int *val) {
    if (elem->tag_number != NEVERC_ASN1_BOOLEAN) return -1;
    if (elem->value_len != 1) return -1;
    *val = elem->value[0] ? 1 : 0;
    return 0;
}

static int write_ulong(char *buf, int pos, int cap, unsigned long val) {
    if (val == 0) {
        if (pos < cap) buf[pos] = '0';
        return pos + 1;
    }
    char tmp[24];
    int n = 0;
    unsigned long v = val;
    while (v > 0) { tmp[n++] = '0' + (v % 10); v /= 10; }
    for (int i = n - 1; i >= 0; i--) {
        if (pos < cap) buf[pos] = tmp[i];
        pos++;
    }
    return pos;
}

char *neverc_asn1_decode_oid(const neverc_asn1_element_t *elem) {
    if (elem->tag_number != NEVERC_ASN1_OID || elem->value_len == 0)
        return NULL;

    char buf[256];
    int cap = (int)sizeof(buf) - 1;
    int pos = 0;
    int first = elem->value[0] / 40;
    int second = elem->value[0] % 40;
    pos = write_ulong(buf, pos, cap, (unsigned long)first);
    if (pos < cap) buf[pos] = '.';
    pos++;
    pos = write_ulong(buf, pos, cap, (unsigned long)second);

    size_t i = 1;
    while (i < elem->value_len) {
        unsigned long val = 0;
        while (i < elem->value_len) {
            uint8_t b = elem->value[i++];
            val = (val << 7) | (b & 0x7F);
            if (!(b & 0x80)) break;
        }
        if (pos < cap) buf[pos] = '.';
        pos++;
        pos = write_ulong(buf, pos, cap, val);
    }
    if (pos > cap) pos = cap;
    buf[pos] = '\0';

    char *result = (char *)malloc(pos + 1);
    memcpy(result, buf, pos + 1);
    return result;
}

int neverc_asn1_encode_tag(uint8_t *buf, size_t cap, int tag_class,
                           int constructed, int tag_number) {
    if (cap < 1) return -1;
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
    if (cap < 3) return -1;
    buf[0] = NEVERC_ASN1_BOOLEAN;
    buf[1] = 1;
    buf[2] = val ? 0xFF : 0x00;
    return 3;
}

int neverc_asn1_encode_octet_string(uint8_t *buf, size_t cap,
                                    const uint8_t *data, size_t len) {
    int tlen = neverc_asn1_encode_tag(buf, cap, NEVERC_ASN1_UNIVERSAL, 0,
                                       NEVERC_ASN1_OCTET_STRING);
    if (tlen < 0) return -1;
    int llen = neverc_asn1_encode_length(buf + tlen, cap - tlen, len);
    if (llen < 0) return -1;
    if ((size_t)(tlen + llen) + len > cap) return -1;
    memcpy(buf + tlen + llen, data, len);
    return tlen + llen + (int)len;
}

int neverc_asn1_encode_null(uint8_t *buf, size_t cap) {
    if (cap < 2) return -1;
    buf[0] = NEVERC_ASN1_NULL;
    buf[1] = 0;
    return 2;
}
