#ifndef NEVERC_CRC64_H
#define NEVERC_CRC64_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Predefined polynomials */
#define NEVERC_CRC64_ISO  0xD800000000000000ULL
#define NEVERC_CRC64_ECMA 0xC96C5795D7870F42ULL

typedef uint64_t neverc_crc64_table_t[256];

/* Build a CRC-64 lookup table from polynomial. */
void neverc_crc64_make_table(uint64_t poly, neverc_crc64_table_t table);

/* Update CRC-64 with new data. */
uint64_t neverc_crc64_update(uint64_t crc, const neverc_crc64_table_t table,
                              const uint8_t *data, size_t len);

/* Compute CRC-64 checksum of data. */
uint64_t neverc_crc64_checksum(const neverc_crc64_table_t table,
                                const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* NEVERC_CRC64_H */
