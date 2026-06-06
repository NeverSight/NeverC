#ifndef NEVERC_CRC32_H
#define NEVERC_CRC32_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * CRC-32 checksum (mirrors Go hash/crc32 package).
 * Uses the standard IEEE polynomial by default.
 */

#define NEVERC_CRC32_IEEE       0xEDB88320U
#define NEVERC_CRC32_CASTAGNOLI 0x82F63B78U
#define NEVERC_CRC32_KOOPMAN    0xEB31D82EU

typedef uint32_t neverc_crc32_table_t[256];

void     neverc_crc32_make_table(uint32_t poly, neverc_crc32_table_t table);
uint32_t neverc_crc32_update(uint32_t crc, const neverc_crc32_table_t table,
                             const void *data, size_t len);
uint32_t neverc_crc32_checksum(const neverc_crc32_table_t table,
                               const void *data, size_t len);
uint32_t neverc_crc32_ieee(const void *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* NEVERC_CRC32_H */
