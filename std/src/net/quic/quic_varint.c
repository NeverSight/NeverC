/*
 * QUIC Variable-Length Integer Encoding (RFC 9000 §16)
 *
 * QUIC uses a variable-length encoding for integers up to 2^62-1.
 * The two most significant bits of the first byte encode the length:
 *   00 = 1 byte  (6-bit value, max 63)
 *   01 = 2 bytes (14-bit value, max 16383)
 *   10 = 4 bytes (30-bit value, max 1073741823)
 *   11 = 8 bytes (62-bit value, max 4611686018427387903)
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

int neverc_quic_varint_decode(const uint8_t *buf, size_t len,
                               uint64_t *value, size_t *consumed) {
    if (consumed) *consumed = 0;
    if (!buf || !value || len < 1) return -1;

    uint8_t prefix = buf[0] >> 6;
    size_t need;

    if (prefix == 0) need = 1;
    else if (prefix == 1) need = 2;
    else if (prefix == 2) need = 4;
    else need = 8;

    if (len < need) return -1;

    uint64_t v;
    if (need == 1) {
        v = buf[0] & 0x3F;
    } else if (need == 2) {
        v = ((uint64_t)(buf[0] & 0x3F) << 8) | buf[1];
    } else if (need == 4) {
        v = ((uint64_t)(buf[0] & 0x3F) << 24) |
            ((uint64_t)buf[1] << 16) |
            ((uint64_t)buf[2] << 8) |
            (uint64_t)buf[3];
    } else {
        v = ((uint64_t)(buf[0] & 0x3F) << 56) |
            ((uint64_t)buf[1] << 48) |
            ((uint64_t)buf[2] << 40) |
            ((uint64_t)buf[3] << 32) |
            ((uint64_t)buf[4] << 24) |
            ((uint64_t)buf[5] << 16) |
            ((uint64_t)buf[6] << 8) |
            (uint64_t)buf[7];
    }

    *value = v;
    if (consumed) *consumed = need;
    return 0;
}

int neverc_quic_varint_encode(uint64_t value, uint8_t *buf, size_t cap,
                               size_t *written) {
    if (written) *written = 0;
    if (!buf) return -1;
    size_t need;
    if (value <= 63) need = 1;
    else if (value <= 16383) need = 2;
    else if (value <= 1073741823) need = 4;
    else if (value <= 4611686018427387903ULL) need = 8;
    else return -1;

    if (cap < need) return -1;

    if (need == 1) {
        buf[0] = (uint8_t)(value & 0x3F);
    } else if (need == 2) {
        buf[0] = (uint8_t)(0x40 | ((value >> 8) & 0x3F));
        buf[1] = (uint8_t)(value & 0xFF);
    } else if (need == 4) {
        buf[0] = (uint8_t)(0x80 | ((value >> 24) & 0x3F));
        buf[1] = (uint8_t)((value >> 16) & 0xFF);
        buf[2] = (uint8_t)((value >> 8) & 0xFF);
        buf[3] = (uint8_t)(value & 0xFF);
    } else {
        buf[0] = (uint8_t)(0xC0 | ((value >> 56) & 0x3F));
        buf[1] = (uint8_t)((value >> 48) & 0xFF);
        buf[2] = (uint8_t)((value >> 40) & 0xFF);
        buf[3] = (uint8_t)((value >> 32) & 0xFF);
        buf[4] = (uint8_t)((value >> 24) & 0xFF);
        buf[5] = (uint8_t)((value >> 16) & 0xFF);
        buf[6] = (uint8_t)((value >> 8) & 0xFF);
        buf[7] = (uint8_t)(value & 0xFF);
    }

    if (written) *written = need;
    return 0;
}

size_t neverc_quic_varint_len(uint64_t value) {
    if (value <= 63) return 1;
    if (value <= 16383) return 2;
    if (value <= 1073741823) return 4;
    if (value <= 4611686018427387903ULL) return 8;
    return 0;
}
