#ifndef NEVERC_CMP_H
#define NEVERC_CMP_H

/*
 * NeverC cmp — ordered comparison utilities (mirrors Go cmp package).
 *
 * Provides Compare / Less / Min / Max for integer and floating-point types.
 * For floats: NaN is considered less than any non-NaN, and NaN == NaN.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int neverc_cmp_compare_int(int x, int y);
int neverc_cmp_compare_int64(int64_t x, int64_t y);
int neverc_cmp_compare_uint64(uint64_t x, uint64_t y);
int neverc_cmp_compare_float32(float x, float y);
int neverc_cmp_compare_float64(double x, double y);

int neverc_cmp_less_float32(float x, float y);
int neverc_cmp_less_float64(double x, double y);

int neverc_cmp_isnan_float32(float x);
int neverc_cmp_isnan_float64(double x);

int    neverc_cmp_min_int(int x, int y);
int    neverc_cmp_max_int(int x, int y);
int64_t neverc_cmp_min_int64(int64_t x, int64_t y);
int64_t neverc_cmp_max_int64(int64_t x, int64_t y);
float  neverc_cmp_min_float32(float x, float y);
float  neverc_cmp_max_float32(float x, float y);
double neverc_cmp_min_float64(double x, double y);
double neverc_cmp_max_float64(double x, double y);

int neverc_cmp_clamp_int(int x, int lo, int hi);
int64_t neverc_cmp_clamp_int64(int64_t x, int64_t lo, int64_t hi);
double neverc_cmp_clamp_float64(double x, double lo, double hi);

#ifdef __cplusplus
}
#endif

#ifdef __neverc__
struct __neverc_std_cmp_t { char __tag; };
extern struct __neverc_std_cmp_t __neverc_mod_cmp;
extern struct __neverc_std_cmp_t cmp;
#endif

#endif
