#ifndef NEVERC_BINARY_H
#define NEVERC_BINARY_H

#include <stddef.h>
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

/* Protobuf-style varints (mirrors Go encoding/binary).
 * put_* returns bytes written, or -1 if buf is too small.
 * *varint returns bytes consumed, 0 if the input is truncated, or a
 * negative count if the encoding overflows uint64. */
#define NEVERC_BINARY_MAX_VARINT_LEN16 3
#define NEVERC_BINARY_MAX_VARINT_LEN32 5
#define NEVERC_BINARY_MAX_VARINT_LEN64 10

int neverc_binary_put_uvarint(uint8_t *buf, size_t buf_len, uint64_t x);
int neverc_binary_uvarint(const uint8_t *buf, size_t n, uint64_t *out);
int neverc_binary_put_varint(uint8_t *buf, size_t buf_len, int64_t x);
int neverc_binary_varint(const uint8_t *buf, size_t n, int64_t *out);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/encoding.h>
#endif

#endif /* NEVERC_BINARY_H */
