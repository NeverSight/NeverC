#ifndef NEVERC_SUBTLE_H
#define NEVERC_SUBTLE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Constant-time byte sequence comparison.
 * Returns 1 if x and y are equal, 0 otherwise.
 * This is NOT memcmp semantics (memcmp returns 0 when equal).
 */
int neverc_subtle_constant_time_compare(const uint8_t *x, const uint8_t *y, size_t len);
/* v must be either 0 or 1. */
int neverc_subtle_constant_time_select(int v, int x, int y);
int neverc_subtle_constant_time_byte_eq(uint8_t x, uint8_t y);
int neverc_subtle_constant_time_eq(int32_t x, int32_t y);
/* v must be either 0 or 1. */
void neverc_subtle_constant_time_copy(int v, uint8_t *x, const uint8_t *y, size_t len);
int neverc_subtle_constant_time_less_or_eq(int x, int y);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/crypto.h>
#endif

#endif /* NEVERC_SUBTLE_H */
