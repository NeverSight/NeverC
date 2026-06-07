#ifndef NEVERC_BITS_H
#define NEVERC_BITS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Leading Zeros (CLZ) --- */
int neverc_bits_leading_zeros(unsigned int x);
int neverc_bits_leading_zeros8(uint8_t x);
int neverc_bits_leading_zeros16(uint16_t x);
int neverc_bits_leading_zeros32(uint32_t x);
int neverc_bits_leading_zeros64(uint64_t x);

/* --- Trailing Zeros (CTZ) --- */
int neverc_bits_trailing_zeros(unsigned int x);
int neverc_bits_trailing_zeros8(uint8_t x);
int neverc_bits_trailing_zeros16(uint16_t x);
int neverc_bits_trailing_zeros32(uint32_t x);
int neverc_bits_trailing_zeros64(uint64_t x);

/* --- Population Count (popcount) --- */
int neverc_bits_ones_count(unsigned int x);
int neverc_bits_ones_count8(uint8_t x);
int neverc_bits_ones_count16(uint16_t x);
int neverc_bits_ones_count32(uint32_t x);
int neverc_bits_ones_count64(uint64_t x);

/* --- Bit Length (position of highest set bit + 1) --- */
int neverc_bits_len(unsigned int x);
int neverc_bits_len8(uint8_t x);
int neverc_bits_len16(uint16_t x);
int neverc_bits_len32(uint32_t x);
int neverc_bits_len64(uint64_t x);

/* --- Rotate Left --- */
uint8_t  neverc_bits_rotate_left8(uint8_t x, int k);
uint16_t neverc_bits_rotate_left16(uint16_t x, int k);
uint32_t neverc_bits_rotate_left32(uint32_t x, int k);
uint64_t neverc_bits_rotate_left64(uint64_t x, int k);

/* --- Bit Reverse --- */
uint8_t  neverc_bits_reverse8(uint8_t x);
uint16_t neverc_bits_reverse16(uint16_t x);
uint32_t neverc_bits_reverse32(uint32_t x);
uint64_t neverc_bits_reverse64(uint64_t x);

/* --- Byte Swap --- */
uint16_t neverc_bits_reverse_bytes16(uint16_t x);
uint32_t neverc_bits_reverse_bytes32(uint32_t x);
uint64_t neverc_bits_reverse_bytes64(uint64_t x);

/* --- Multi-precision arithmetic --- */
void neverc_bits_add32(uint32_t x, uint32_t y, uint32_t carry,
                       uint32_t *sum, uint32_t *carry_out);
void neverc_bits_add64(uint64_t x, uint64_t y, uint64_t carry,
                       uint64_t *sum, uint64_t *carry_out);
void neverc_bits_sub32(uint32_t x, uint32_t y, uint32_t borrow,
                       uint32_t *diff, uint32_t *borrow_out);
void neverc_bits_sub64(uint64_t x, uint64_t y, uint64_t borrow,
                       uint64_t *diff, uint64_t *borrow_out);
void neverc_bits_mul32(uint32_t x, uint32_t y,
                       uint32_t *hi, uint32_t *lo);
void neverc_bits_mul64(uint64_t x, uint64_t y,
                       uint64_t *hi, uint64_t *lo);

/* --- Division --- */
void neverc_bits_div32(uint32_t hi, uint32_t lo, uint32_t y,
                       uint32_t *quo, uint32_t *rem);
void neverc_bits_div64(uint64_t hi, uint64_t lo, uint64_t y,
                       uint64_t *quo, uint64_t *rem);
uint32_t neverc_bits_rem32(uint32_t hi, uint32_t lo, uint32_t y);
uint64_t neverc_bits_rem64(uint64_t hi, uint64_t lo, uint64_t y);

#ifdef __cplusplus
}
#endif



/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/math.h>
#endif


#endif /* NEVERC_BITS_H */
