#include "neverc/std/hash/crc32.h"
#include <string.h>

/*
 * Slicing-by-8: process 8 bytes per iteration using 8 interleaved tables.
 * ~8x faster than byte-at-a-time on modern CPUs.
 * Reference: "High Octane CRC Generation with the Intel Slicing-by-8 Algorithm"
 */

/*
 * Build-once, then immutable slicing-8 tables. They are published with
 * release/acquire ordering so concurrent readers never observe a half-built
 * table. Once published they are never mutated again, so reads are race-free.
 */
static uint32_t ieee_s8[8][256];
static int ieee_s8_ready;   /* 0 = unbuilt, 1 = building, 2 = published */

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

static uint32_t crc32_load_le32(const uint8_t *p) {
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

static uint32_t crc32_slicing8(uint32_t crc, const uint32_t tab[8][256],
                                const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    crc = ~crc;

    while (len >= 8) {
        uint32_t lo = crc32_load_le32(p);
        uint32_t hi = crc32_load_le32(p + 4);
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
 * Process-lifetime slot for the first table that reaches the slicing-8 path.
 * Built exactly once (CAS-claimed) and immutable afterwards, so it is safe to
 * read concurrently. Other tables (or a buffer refilled with a new polynomial,
 * detected via sentinel entries) fall back to a private per-call build, which
 * is fully reentrant.
 */
static uint32_t shared_s8[8][256];
static int shared_s8_ready;   /* 0 = unbuilt, 1 = building, 2 = published */

uint32_t neverc_crc32_update(uint32_t crc, const neverc_crc32_table_t table,
                             const void *data, size_t len) {
    if (!table) return crc;
    if (!data) len = 0;
    if (len < 64) {
        const uint8_t *p = (const uint8_t *)data;
        crc = ~crc;
        for (size_t i = 0; i < len; i++)
            crc = table[(crc ^ p[i]) & 0xff] ^ (crc >> 8);
        return ~crc;
    }

    /* Fast path: reuse the published shared table when its contents match.
     * shared_s8[0] is a copy of the source table, so the sentinels also catch
     * a buffer that was refilled with a different polynomial. */
    if (__atomic_load_n(&shared_s8_ready, __ATOMIC_ACQUIRE) == 2 &&
        shared_s8[0][1]   == table[1]   &&
        shared_s8[0][128] == table[128] &&
        shared_s8[0][255] == table[255]) {
        return crc32_slicing8(crc, shared_s8, data, len);
    }

    /* Claim the slot once for the first table that needs it. */
    int expected = 0;
    if (__atomic_load_n(&shared_s8_ready, __ATOMIC_RELAXED) == 0 &&
        __atomic_compare_exchange_n(&shared_s8_ready, &expected, 1, 0,
                                    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        build_slicing8_from_table(table, shared_s8);
        __atomic_store_n(&shared_s8_ready, 2, __ATOMIC_RELEASE);
        return crc32_slicing8(crc, shared_s8, data, len);
    }

    /* Slot busy or owned by a different table: build a private table on the
     * stack. No shared mutable state is touched, so this is reentrant. */
    uint32_t s8[8][256];
    build_slicing8_from_table(table, s8);
    return crc32_slicing8(crc, s8, data, len);
}

uint32_t neverc_crc32_checksum(const neverc_crc32_table_t table,
                               const void *data, size_t len) {
    return neverc_crc32_update(0, table, data, len);
}

uint32_t neverc_crc32_ieee(const void *data, size_t len) {
    if (!data) len = 0;
    if (__atomic_load_n(&ieee_s8_ready, __ATOMIC_ACQUIRE) == 2)
        return crc32_slicing8(0, ieee_s8, data, len);

    /* First use: the CAS winner builds and publishes the shared table. */
    int expected = 0;
    if (__atomic_compare_exchange_n(&ieee_s8_ready, &expected, 1, 0,
                                    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        build_slicing8(NEVERC_CRC32_IEEE, ieee_s8);
        __atomic_store_n(&ieee_s8_ready, 2, __ATOMIC_RELEASE);
        return crc32_slicing8(0, ieee_s8, data, len);
    }

    /* Another thread is still publishing: use a private table this call. */
    uint32_t s8[8][256];
    build_slicing8(NEVERC_CRC32_IEEE, s8);
    return crc32_slicing8(0, s8, data, len);
}
