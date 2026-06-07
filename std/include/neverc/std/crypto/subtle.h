#ifndef NEVERC_SUBTLE_H
#define NEVERC_SUBTLE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int neverc_subtle_constant_time_compare(const uint8_t *x, const uint8_t *y, size_t len);
int neverc_subtle_constant_time_select(int v, int x, int y);
int neverc_subtle_constant_time_byte_eq(uint8_t x, uint8_t y);
int neverc_subtle_constant_time_eq(int32_t x, int32_t y);
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
