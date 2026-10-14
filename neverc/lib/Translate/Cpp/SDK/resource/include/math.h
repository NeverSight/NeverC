/* NeverC C++ SDK scalar math declarations for libc++ header parsing.
 * Copyright (C) 2026 NeverC contributors.
 * SPDX-License-Identifier: AGPL-3.0-only
 * Core-v2 has no runtime math import through this declaration shim.
 */
#ifndef _NEVERC_CPP_CORE_V2_SDK
#include_next <math.h>
#else
#ifndef NEVERC_CPP_SDK_MATH_H
#define NEVERC_CPP_SDK_MATH_H
#define FP_NAN 0
#define FP_INFINITE 1
#define FP_ZERO 2
#define FP_SUBNORMAL 3
#define FP_NORMAL 4
#define INFINITY (__builtin_inff())
#define NAN (__builtin_nanf(""))
#define HUGE_VAL (__builtin_huge_val())
#define HUGE_VALF (__builtin_huge_valf())
#define HUGE_VALL (__builtin_huge_vall())
#if __FLT_EVAL_METHOD__ == 2
typedef long double float_t;
typedef long double double_t;
#elif __FLT_EVAL_METHOD__ == 1
typedef double float_t;
typedef double double_t;
#else
typedef float float_t;
typedef double double_t;
#endif
#ifdef __cplusplus
extern "C" {
#endif
double acos(double);
double asin(double);
double atan(double);
double ceil(double);
double cos(double);
double cosh(double);
double exp(double);
double fabs(double);
double floor(double);
double log(double);
double log10(double);
double sin(double);
double sinh(double);
double sqrt(double);
double tan(double);
double tanh(double);
double acosh(double);
double asinh(double);
double atanh(double);
double cbrt(double);
double erf(double);
double erfc(double);
double exp2(double);
double expm1(double);
double lgamma(double);
double log1p(double);
double log2(double);
double logb(double);
double nearbyint(double);
double rint(double);
double round(double);
double tgamma(double);
double trunc(double);
double atan2(double, double);
double fmod(double, double);
double pow(double, double);
double copysign(double, double);
double fdim(double, double);
double fmax(double, double);
double fmin(double, double);
double hypot(double, double);
double nextafter(double, double);
double remainder(double, double);
double fma(double, double, double);
double frexp(double, int *);
double ldexp(double, int);
double modf(double, double *);
double nan(const char *);
double nexttoward(double, long double);
double remquo(double, double, int *);
double scalbn(double, int);
double scalbln(double, long);
int ilogb(double);
long lrint(double);
long lround(double);
long long llrint(double);
long long llround(double);
float acosf(float);
float asinf(float);
float atanf(float);
float ceilf(float);
float cosf(float);
float coshf(float);
float expf(float);
float fabsf(float);
float floorf(float);
float logf(float);
float log10f(float);
float sinf(float);
float sinhf(float);
float sqrtf(float);
float tanf(float);
float tanhf(float);
float acoshf(float);
float asinhf(float);
float atanhf(float);
float cbrtf(float);
float erff(float);
float erfcf(float);
float exp2f(float);
float expm1f(float);
float lgammaf(float);
float log1pf(float);
float log2f(float);
float logbf(float);
float nearbyintf(float);
float rintf(float);
float roundf(float);
float tgammaf(float);
float truncf(float);
float atan2f(float, float);
float fmodf(float, float);
float powf(float, float);
float copysignf(float, float);
float fdimf(float, float);
float fmaxf(float, float);
float fminf(float, float);
float hypotf(float, float);
float nextafterf(float, float);
float remainderf(float, float);
float fmaf(float, float, float);
float frexpf(float, int *);
float ldexpf(float, int);
float modff(float, float *);
float nanf(const char *);
float nexttowardf(float, long double);
float remquof(float, float, int *);
float scalbnf(float, int);
float scalblnf(float, long);
int ilogbf(float);
long lrintf(float);
long lroundf(float);
long long llrintf(float);
long long llroundf(float);
long double acosl(long double);
long double asinl(long double);
long double atanl(long double);
long double ceill(long double);
long double cosl(long double);
long double coshl(long double);
long double expl(long double);
long double fabsl(long double);
long double floorl(long double);
long double logl(long double);
long double log10l(long double);
long double sinl(long double);
long double sinhl(long double);
long double sqrtl(long double);
long double tanl(long double);
long double tanhl(long double);
long double acoshl(long double);
long double asinhl(long double);
long double atanhl(long double);
long double cbrtl(long double);
long double erfl(long double);
long double erfcl(long double);
long double exp2l(long double);
long double expm1l(long double);
long double lgammal(long double);
long double log1pl(long double);
long double log2l(long double);
long double logbl(long double);
long double nearbyintl(long double);
long double rintl(long double);
long double roundl(long double);
long double tgammal(long double);
long double truncl(long double);
long double atan2l(long double, long double);
long double fmodl(long double, long double);
long double powl(long double, long double);
long double copysignl(long double, long double);
long double fdiml(long double, long double);
long double fmaxl(long double, long double);
long double fminl(long double, long double);
long double hypotl(long double, long double);
long double nextafterl(long double, long double);
long double remainderl(long double, long double);
long double fmal(long double, long double, long double);
long double frexpl(long double, int *);
long double ldexpl(long double, int);
long double modfl(long double, long double *);
long double nanl(const char *);
long double nexttowardl(long double, long double);
long double remquol(long double, long double, int *);
long double scalbnl(long double, int);
long double scalblnl(long double, long);
int ilogbl(long double);
long lrintl(long double);
long lroundl(long double);
long long llrintl(long double);
long long llroundl(long double);
#ifdef __cplusplus
}
#endif
#endif /* NEVERC_CPP_SDK_MATH_H */
#endif /* _NEVERC_CPP_CORE_V2_SDK */
