#ifndef NEVERC_BITS_H
#define NEVERC_BITS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Leading Zeros (CLZ) --- */
int neverc_bits_leading_zeros32(uint32_t x);
int neverc_bits_leading_zeros64(uint64_t x);

/* --- Trailing Zeros (CTZ) --- */
int neverc_bits_trailing_zeros32(uint32_t x);
int neverc_bits_trailing_zeros64(uint64_t x);

/* --- Population Count (popcount) --- */
int neverc_bits_ones_count32(uint32_t x);
int neverc_bits_ones_count64(uint64_t x);

/* --- Bit Length (position of highest set bit + 1) --- */
int neverc_bits_len32(uint32_t x);
int neverc_bits_len64(uint64_t x);

/* --- Rotate Left --- */
uint32_t neverc_bits_rotate_left32(uint32_t x, int k);
uint64_t neverc_bits_rotate_left64(uint64_t x, int k);

/* --- Bit Reverse --- */
uint32_t neverc_bits_reverse32(uint32_t x);
uint64_t neverc_bits_reverse64(uint64_t x);

/* --- Byte Swap --- */
uint16_t neverc_bits_reverse_bytes16(uint16_t x);
uint32_t neverc_bits_reverse_bytes32(uint32_t x);
uint64_t neverc_bits_reverse_bytes64(uint64_t x);

/* --- Multi-precision arithmetic --- */
void neverc_bits_add64(uint64_t x, uint64_t y, uint64_t carry,
                       uint64_t *sum, uint64_t *carry_out);
void neverc_bits_sub64(uint64_t x, uint64_t y, uint64_t borrow,
                       uint64_t *diff, uint64_t *borrow_out);
void neverc_bits_mul64(uint64_t x, uint64_t y,
                       uint64_t *hi, uint64_t *lo);

#ifdef __cplusplus
}
#endif

#endif /* NEVERC_BITS_H */
