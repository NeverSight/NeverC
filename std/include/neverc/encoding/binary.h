#ifndef NEVERC_BINARY_H
#define NEVERC_BINARY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Big-Endian (network byte order) --- */
uint16_t neverc_binary_big_endian_uint16(const uint8_t *b);
uint32_t neverc_binary_big_endian_uint32(const uint8_t *b);
uint64_t neverc_binary_big_endian_uint64(const uint8_t *b);
void     neverc_binary_big_endian_put_uint16(uint8_t *b, uint16_t v);
void     neverc_binary_big_endian_put_uint32(uint8_t *b, uint32_t v);
void     neverc_binary_big_endian_put_uint64(uint8_t *b, uint64_t v);

/* --- Little-Endian --- */
uint16_t neverc_binary_little_endian_uint16(const uint8_t *b);
uint32_t neverc_binary_little_endian_uint32(const uint8_t *b);
uint64_t neverc_binary_little_endian_uint64(const uint8_t *b);
void     neverc_binary_little_endian_put_uint16(uint8_t *b, uint16_t v);
void     neverc_binary_little_endian_put_uint32(uint8_t *b, uint32_t v);
void     neverc_binary_little_endian_put_uint64(uint8_t *b, uint64_t v);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/encoding.h>
#endif

#endif /* NEVERC_BINARY_H */
