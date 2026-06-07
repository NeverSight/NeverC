#include "neverc/std/hash/crc64.h"

void neverc_crc64_make_table(uint64_t poly, neverc_crc64_table_t table) {
    for (int i = 0; i < 256; i++) {
        uint64_t crc = (uint64_t)i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ poly;
            else
                crc >>= 1;
        }
        table[i] = crc;
    }
}

uint64_t neverc_crc64_update(uint64_t crc, const neverc_crc64_table_t table,
                              const uint8_t *data, size_t len) {
    crc = ~crc;
    for (size_t i = 0; i < len; i++)
        crc = table[(uint8_t)(crc) ^ data[i]] ^ (crc >> 8);
    return ~crc;
}

uint64_t neverc_crc64_checksum(const neverc_crc64_table_t table,
                                const uint8_t *data, size_t len) {
    return neverc_crc64_update(0, table, data, len);
}
