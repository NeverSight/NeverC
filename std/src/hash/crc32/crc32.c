#include "neverc/std/hash/crc32.h"

static int ieee_table_initialized = 0;
static neverc_crc32_table_t ieee_table;

void neverc_crc32_make_table(uint32_t poly, neverc_crc32_table_t table) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ poly;
            else
                crc >>= 1;
        }
        table[i] = crc;
    }
}

uint32_t neverc_crc32_update(uint32_t crc, const neverc_crc32_table_t table,
                             const void *data, size_t len) {
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
    if (!ieee_table_initialized) {
        neverc_crc32_make_table(NEVERC_CRC32_IEEE, ieee_table);
        ieee_table_initialized = 1;
    }
    return neverc_crc32_checksum(ieee_table, data, len);
}
