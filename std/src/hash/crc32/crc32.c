#include "neverc/std/hash/crc32.h"
#include <stdlib.h>
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
    if (!table) return;
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
 * Process-lifetime slicing-8 expansions of caller tables, one slot per
 * distinct table, so that alternating or custom polynomials do not rebuild
 * 8 KiB of tables on every call. A table is fully built before a release CAS
 * publishes its pointer into an empty slot, and is never mutated or freed
 * afterwards, so readers need only an acquire load. Lookups compare the whole
 * 256-entry source table (a caller may refill one buffer with a new
 * polynomial, and distinct polynomials share some entries); entry 128, which
 * is the polynomial itself for neverc_crc32_make_table output, is checked
 * first so a slot holding another polynomial costs a single load.
 */
#define CRC32_S8_SLOTS 8
static uint32_t (*crc32_s8_cache[CRC32_S8_SLOTS])[256];

/* With every slot taken by other tables, a private per-call build only pays
 * off for longer inputs; it costs about as much as 300 bytes of byte-wise
 * CRC. Shorter inputs use the caller's table directly. */
#define CRC32_PRIVATE_S8_MIN 512

/* Returns the cached slicing-8 expansion of `table`, building and publishing
 * it into a free slot on first use, or NULL when every slot holds another
 * table or allocation fails. */
static uint32_t (*crc32_cached_s8(const neverc_crc32_table_t table))[256] {
    uint32_t (*built)[256] = NULL;
    for (int i = 0; i < CRC32_S8_SLOTS; i++) {
        uint32_t (*slot)[256] =
            __atomic_load_n(&crc32_s8_cache[i], __ATOMIC_ACQUIRE);
        if (!slot) {
            if (!built) {
                built = (uint32_t (*)[256])malloc(sizeof(uint32_t[8][256]));
                if (!built) return NULL;
                build_slicing8_from_table(table, built);
            }
            if (__atomic_compare_exchange_n(&crc32_s8_cache[i], &slot, built,
                                            0, __ATOMIC_ACQ_REL,
                                            __ATOMIC_ACQUIRE))
                return built;
            /* Another thread filled the slot first; `slot` now holds its
             * table, which may be this one. Otherwise keep the build for the
             * next free slot. */
        }
        if (slot[0][128] == table[128] &&
            memcmp(slot[0], table, sizeof(neverc_crc32_table_t)) == 0) {
            free(built);   /* never published */
            return slot;
        }
    }
    free(built);
    return NULL;
}

uint32_t neverc_crc32_update(uint32_t crc, const neverc_crc32_table_t table,
                             const void *data, size_t len) {
    if (!table) return crc;
    if (!data) len = 0;
    if (len >= 64) {
        uint32_t (*s8)[256] = crc32_cached_s8(table);
        if (s8) return crc32_slicing8(crc, s8, data, len);
        if (len >= CRC32_PRIVATE_S8_MIN) {
            /* No shared mutable state is touched, so this is reentrant. */
            uint32_t priv[8][256];
            build_slicing8_from_table(table, priv);
            return crc32_slicing8(crc, priv, data, len);
        }
    }

    const uint8_t *p = (const uint8_t *)data;
    crc = ~crc;
    for (size_t i = 0; i < len; i++)
        crc = table[(crc ^ p[i]) & 0xff] ^ (crc >> 8);
    return ~crc;
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
