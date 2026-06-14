#include "neverc/std/hash/crc32.h"
#include <string.h>

/*
 * Slicing-by-8: process 8 bytes per iteration using 8 interleaved tables.
 * ~8x faster than byte-at-a-time on modern CPUs.
 * Reference: "High Octane CRC Generation with the Intel Slicing-by-8 Algorithm"
 */

static int ieee_table_initialized = 0;
static neverc_crc32_table_t ieee_table;

static uint32_t ieee_s8[8][256];
static int ieee_s8_initialized = 0;

void neverc_crc32_make_table(uint32_t poly, neverc_crc32_table_t table) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ ((crc & 1) ? poly : 0);
        table[i] = crc;
    }
}

static void build_slicing8(uint32_t poly, uint32_t tab[8][256]) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ ((crc & 1) ? poly : 0);
        tab[0][i] = crc;
    }
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = tab[0][i];
        for (int k = 1; k < 8; k++) {
            crc = tab[0][crc & 0xFF] ^ (crc >> 8);
            tab[k][i] = crc;
        }
    }
}

static uint32_t crc32_slicing8(uint32_t crc, const uint32_t tab[8][256],
                                const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    crc = ~crc;

    while (len >= 8) {
        uint32_t lo, hi;
        memcpy(&lo, p, 4);
        memcpy(&hi, p + 4, 4);
        crc ^= lo;
        crc = tab[7][(crc      ) & 0xFF]
            ^ tab[6][(crc >>  8) & 0xFF]
            ^ tab[5][(crc >> 16) & 0xFF]
            ^ tab[4][(crc >> 24)        ]
            ^ tab[3][(hi       ) & 0xFF]
            ^ tab[2][(hi  >>  8) & 0xFF]
            ^ tab[1][(hi  >> 16) & 0xFF]
            ^ tab[0][(hi  >> 24)        ];
        p += 8;
        len -= 8;
    }

    while (len-- > 0)
        crc = tab[0][(crc ^ *p++) & 0xFF] ^ (crc >> 8);

    return ~crc;
}

static void build_slicing8_from_table(const neverc_crc32_table_t base,
                                       uint32_t tab[8][256]) {
    for (int i = 0; i < 256; i++)
        tab[0][i] = base[i];
    for (int i = 0; i < 256; i++) {
        uint32_t crc = tab[0][i];
        for (int k = 1; k < 8; k++) {
            crc = tab[0][crc & 0xFF] ^ (crc >> 8);
            tab[k][i] = crc;
        }
    }
}

/*
 * Single-entry cache: avoids rebuilding the 8KB slicing-8 tables when the
 * same polynomial table is passed across repeated calls (the common case).
 */
static const uint32_t *cached_crc32_src;
static uint32_t cached_crc32_s8[8][256];

uint32_t neverc_crc32_update(uint32_t crc, const neverc_crc32_table_t table,
                             const void *data, size_t len) {
    if (len < 64) {
        const uint8_t *p = (const uint8_t *)data;
        crc = ~crc;
        for (size_t i = 0; i < len; i++)
            crc = table[(crc ^ p[i]) & 0xff] ^ (crc >> 8);
        return ~crc;
    }
    /* Rebuild the cached slicing-8 tables when the source table pointer
     * changes OR when the same buffer was refilled with a different
     * polynomial. cached_crc32_s8[0] is a copy of the source table, so a
     * mismatch on a few sentinel entries means the cache is stale; without
     * this check, reusing one neverc_crc32_table_t buffer across polynomials
     * would silently return wrong checksums for len >= 64. */
    if (table != cached_crc32_src ||
        cached_crc32_s8[0][1]   != table[1]   ||
        cached_crc32_s8[0][128] != table[128] ||
        cached_crc32_s8[0][255] != table[255]) {
        build_slicing8_from_table(table, cached_crc32_s8);
        cached_crc32_src = table;
    }
    return crc32_slicing8(crc, cached_crc32_s8, data, len);
}

uint32_t neverc_crc32_checksum(const neverc_crc32_table_t table,
                               const void *data, size_t len) {
    return neverc_crc32_update(0, table, data, len);
}

uint32_t neverc_crc32_ieee(const void *data, size_t len) {
    if (!ieee_s8_initialized) {
        build_slicing8(NEVERC_CRC32_IEEE, ieee_s8);
        neverc_crc32_make_table(NEVERC_CRC32_IEEE, ieee_table);
        ieee_table_initialized = 1;
        ieee_s8_initialized = 1;
    }
    return crc32_slicing8(0, ieee_s8, data, len);
}
