#include "neverc/std/encoding/binary.h"

/*
 * Binary encoding — mirrors Go encoding/binary BigEndian/LittleEndian.
 * Pure byte manipulation, endian-safe on all architectures.
 */

/* --- Big-Endian --- */

uint16_t neverc_binary_big_endian_uint16(const uint8_t *b) {
    return ((uint16_t)b[0] << 8) | (uint16_t)b[1];
}

uint32_t neverc_binary_big_endian_uint32(const uint8_t *b) {
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] << 8)  | (uint32_t)b[3];
}

uint64_t neverc_binary_big_endian_uint64(const uint8_t *b) {
    return ((uint64_t)b[0] << 56) | ((uint64_t)b[1] << 48) |
           ((uint64_t)b[2] << 40) | ((uint64_t)b[3] << 32) |
           ((uint64_t)b[4] << 24) | ((uint64_t)b[5] << 16) |
           ((uint64_t)b[6] << 8)  | (uint64_t)b[7];
}

void neverc_binary_big_endian_put_uint16(uint8_t *b, uint16_t v) {
    b[0] = (uint8_t)(v >> 8);
    b[1] = (uint8_t)(v);
}

void neverc_binary_big_endian_put_uint32(uint8_t *b, uint32_t v) {
    b[0] = (uint8_t)(v >> 24);
    b[1] = (uint8_t)(v >> 16);
    b[2] = (uint8_t)(v >> 8);
    b[3] = (uint8_t)(v);
}

void neverc_binary_big_endian_put_uint64(uint8_t *b, uint64_t v) {
    b[0] = (uint8_t)(v >> 56);
    b[1] = (uint8_t)(v >> 48);
    b[2] = (uint8_t)(v >> 40);
    b[3] = (uint8_t)(v >> 32);
    b[4] = (uint8_t)(v >> 24);
    b[5] = (uint8_t)(v >> 16);
    b[6] = (uint8_t)(v >> 8);
    b[7] = (uint8_t)(v);
}

/* --- Little-Endian --- */

uint16_t neverc_binary_little_endian_uint16(const uint8_t *b) {
    return (uint16_t)b[0] | ((uint16_t)b[1] << 8);
}

uint32_t neverc_binary_little_endian_uint32(const uint8_t *b) {
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

uint64_t neverc_binary_little_endian_uint64(const uint8_t *b) {
    return (uint64_t)b[0] | ((uint64_t)b[1] << 8) |
           ((uint64_t)b[2] << 16) | ((uint64_t)b[3] << 24) |
           ((uint64_t)b[4] << 32) | ((uint64_t)b[5] << 40) |
           ((uint64_t)b[6] << 48) | ((uint64_t)b[7] << 56);
}

void neverc_binary_little_endian_put_uint16(uint8_t *b, uint16_t v) {
    b[0] = (uint8_t)(v);
    b[1] = (uint8_t)(v >> 8);
}

void neverc_binary_little_endian_put_uint32(uint8_t *b, uint32_t v) {
    b[0] = (uint8_t)(v);
    b[1] = (uint8_t)(v >> 8);
    b[2] = (uint8_t)(v >> 16);
    b[3] = (uint8_t)(v >> 24);
}

void neverc_binary_little_endian_put_uint64(uint8_t *b, uint64_t v) {
    b[0] = (uint8_t)(v);
    b[1] = (uint8_t)(v >> 8);
    b[2] = (uint8_t)(v >> 16);
    b[3] = (uint8_t)(v >> 24);
    b[4] = (uint8_t)(v >> 32);
    b[5] = (uint8_t)(v >> 40);
    b[6] = (uint8_t)(v >> 48);
    b[7] = (uint8_t)(v >> 56);
}

int neverc_binary_put_uvarint(uint8_t *buf, size_t buf_len, uint64_t x) {
    size_t i = 0;
    while (x >= 0x80) {
        if (!buf || i >= buf_len) return -1;
        buf[i++] = (uint8_t)(x | 0x80);
        x >>= 7;
    }
    if (!buf || i >= buf_len) return -1;
    buf[i++] = (uint8_t)x;
    return (int)i;
}

int neverc_binary_uvarint(const uint8_t *buf, size_t n, uint64_t *out) {
    if (!buf && n != 0) return -1;
    uint64_t x = 0;
    unsigned s = 0;
    for (size_t i = 0; i < n; i++) {
        if (i == NEVERC_BINARY_MAX_VARINT_LEN64)
            return -(int)(i + 1);
        uint8_t b = buf[i];
        if (b < 0x80) {
            if (i == NEVERC_BINARY_MAX_VARINT_LEN64 - 1 && b > 1)
                return -(int)(i + 1);
            if (out) *out = x | ((uint64_t)b << s);
            return (int)(i + 1);
        }
        /* 10th byte still a continuation: the value cannot fit in uint64
         * (Go encoding/binary.ReadUvarint overflow). */
        if (i == NEVERC_BINARY_MAX_VARINT_LEN64 - 1)
            return -(int)(i + 1);
        x |= (uint64_t)(b & 0x7f) << s;
        s += 7;
    }
    return 0;
}

int neverc_binary_put_varint(uint8_t *buf, size_t buf_len, int64_t x) {
    uint64_t ux = ((uint64_t)x) << 1;
    if (x < 0)
        ux = ~ux;
    return neverc_binary_put_uvarint(buf, buf_len, ux);
}

int neverc_binary_varint(const uint8_t *buf, size_t n, int64_t *out) {
    uint64_t ux = 0;
    int nread = neverc_binary_uvarint(buf, n, &ux);
    if (nread <= 0) return nread;
    int64_t x = (int64_t)(ux >> 1);
    if (ux & 1)
        x = ~x;
    if (out) *out = x;
    return nread;
}
