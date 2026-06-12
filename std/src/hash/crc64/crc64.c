#include "neverc/std/hash/crc64.h"
#include <string.h>

/*
 * Slicing-by-8: process 8 bytes per iteration using 8 interleaved tables.
 * Same technique as CRC32 slicing-by-8, adapted for 64-bit CRC.
 * ~8x faster than byte-at-a-time on modern CPUs.
 */

void neverc_crc64_make_table(uint64_t poly, neverc_crc64_table_t table) {
    for (int i = 0; i < 256; i++) {
        uint64_t crc = (uint64_t)i;
        for (int j = 0; j < 8; j++)
            crc = (crc & 1) ? (crc >> 1) ^ poly : crc >> 1;
        table[i] = crc;
    }
}

static void build_slicing8_from_table(const neverc_crc64_table_t base,
                                       uint64_t tab[8][256]) {
    for (int i = 0; i < 256; i++)
        tab[0][i] = base[i];
    for (int i = 0; i < 256; i++) {
        uint64_t crc = tab[0][i];
        for (int k = 1; k < 8; k++) {
            crc = tab[0][(uint8_t)crc] ^ (crc >> 8);
            tab[k][i] = crc;
        }
    }
}

static uint64_t crc64_slicing8(uint64_t crc, const uint64_t tab[8][256],
                                const uint8_t *data, size_t len) {
    crc = ~crc;

    while (len >= 8) {
        uint64_t w;
        memcpy(&w, data, 8);
        crc ^= w;
        crc = tab[7][(uint8_t)(crc      )]
            ^ tab[6][(uint8_t)(crc >>  8)]
            ^ tab[5][(uint8_t)(crc >> 16)]
            ^ tab[4][(uint8_t)(crc >> 24)]
            ^ tab[3][(uint8_t)(crc >> 32)]
            ^ tab[2][(uint8_t)(crc >> 40)]
            ^ tab[1][(uint8_t)(crc >> 48)]
            ^ tab[0][(uint8_t)(crc >> 56)];
        data += 8;
        len -= 8;
    }

    while (len-- > 0)
        crc = tab[0][(uint8_t)(crc ^ *data++)] ^ (crc >> 8);

    return ~crc;
}

static const uint64_t *cached_crc64_src;
static uint64_t cached_crc64_s8[8][256];

uint64_t neverc_crc64_update(uint64_t crc, const neverc_crc64_table_t table,
                              const uint8_t *data, size_t len) {
    if (len < 64) {
        crc = ~crc;
        for (size_t i = 0; i < len; i++)
            crc = table[(uint8_t)(crc) ^ data[i]] ^ (crc >> 8);
        return ~crc;
    }
    if (table != cached_crc64_src) {
        build_slicing8_from_table(table, cached_crc64_s8);
        cached_crc64_src = table;
    }
    return crc64_slicing8(crc, cached_crc64_s8, data, len);
}

uint64_t neverc_crc64_checksum(const neverc_crc64_table_t table,
                                const uint8_t *data, size_t len) {
    return neverc_crc64_update(0, table, data, len);
}
