#include "neverc/std/hash/crc64.h"
#include <stdlib.h>
#include <string.h>

/*
 * Slicing-by-8: process 8 bytes per iteration using 8 interleaved tables.
 * Same technique as CRC32 slicing-by-8, adapted for 64-bit CRC.
 * ~8x faster than byte-at-a-time on modern CPUs.
 */

void neverc_crc64_make_table(uint64_t poly, neverc_crc64_table_t table) {
    if (!table) return;
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

static uint64_t crc64_load_le64(const uint8_t *p) {
    return (uint64_t)p[0]
         | ((uint64_t)p[1] << 8)
         | ((uint64_t)p[2] << 16)
         | ((uint64_t)p[3] << 24)
         | ((uint64_t)p[4] << 32)
         | ((uint64_t)p[5] << 40)
         | ((uint64_t)p[6] << 48)
         | ((uint64_t)p[7] << 56);
}

static uint64_t crc64_slicing8(uint64_t crc, const uint64_t tab[8][256],
                                const uint8_t *data, size_t len) {
    crc = ~crc;

    while (len >= 8) {
        uint64_t w = crc64_load_le64(data);
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

/*
 * Process-lifetime slicing-8 expansions of caller tables, one slot per
 * distinct table, so that alternating or custom polynomials do not rebuild
 * 16 KiB of tables on every call. A table is fully built before a release CAS
 * publishes its pointer into an empty slot, and is never mutated or freed
 * afterwards, so readers need only an acquire load. Lookups compare the whole
 * 256-entry source table (a caller may refill one buffer with a new
 * polynomial, and distinct polynomials share some entries); entry 128, which
 * is the polynomial itself for neverc_crc64_make_table output, is checked
 * first so a slot holding another polynomial costs a single load.
 */
#define CRC64_S8_SLOTS 8
static uint64_t (*crc64_s8_cache[CRC64_S8_SLOTS])[256];

/* With every slot taken by other tables, a private per-call build only pays
 * off for longer inputs; it costs about as much as 300 bytes of byte-wise
 * CRC. Shorter inputs use the caller's table directly. */
#define CRC64_PRIVATE_S8_MIN 512

/* Returns the cached slicing-8 expansion of `table`, building and publishing
 * it into a free slot on first use, or NULL when every slot holds another
 * table or allocation fails. */
static uint64_t (*crc64_cached_s8(const neverc_crc64_table_t table))[256] {
    uint64_t (*built)[256] = NULL;
    for (int i = 0; i < CRC64_S8_SLOTS; i++) {
        uint64_t (*slot)[256] =
            __atomic_load_n(&crc64_s8_cache[i], __ATOMIC_ACQUIRE);
        if (!slot) {
            if (!built) {
                built = (uint64_t (*)[256])malloc(sizeof(uint64_t[8][256]));
                if (!built) return NULL;
                build_slicing8_from_table(table, built);
            }
            if (__atomic_compare_exchange_n(&crc64_s8_cache[i], &slot, built,
                                            0, __ATOMIC_ACQ_REL,
                                            __ATOMIC_ACQUIRE))
                return built;
            /* Another thread filled the slot first; `slot` now holds its
             * table, which may be this one. Otherwise keep the build for the
             * next free slot. */
        }
        if (slot[0][128] == table[128] &&
            memcmp(slot[0], table, sizeof(neverc_crc64_table_t)) == 0) {
            free(built);   /* never published */
            return slot;
        }
    }
    free(built);
    return NULL;
}

uint64_t neverc_crc64_update(uint64_t crc, const neverc_crc64_table_t table,
                              const uint8_t *data, size_t len) {
    if (!table) return crc;
    if (!data) len = 0;
    if (len >= 64) {
        uint64_t (*s8)[256] = crc64_cached_s8(table);
        if (s8) return crc64_slicing8(crc, s8, data, len);
        if (len >= CRC64_PRIVATE_S8_MIN) {
            /* No shared mutable state is touched, so this is reentrant. */
            uint64_t priv[8][256];
            build_slicing8_from_table(table, priv);
            return crc64_slicing8(crc, priv, data, len);
        }
    }

    crc = ~crc;
    for (size_t i = 0; i < len; i++)
        crc = table[(uint8_t)(crc) ^ data[i]] ^ (crc >> 8);
    return ~crc;
}

uint64_t neverc_crc64_checksum(const neverc_crc64_table_t table,
                                const uint8_t *data, size_t len) {
    return neverc_crc64_update(0, table, data, len);
}
