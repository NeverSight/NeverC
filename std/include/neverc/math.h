#ifndef NEVERC_MATH_H
#define NEVERC_MATH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===== Mathematical Constants (matching Go math package) ===== */

#define NEVERC_MATH_E          2.71828182845904523536028747135266249775724709369995957496696763
#define NEVERC_MATH_PI         3.14159265358979323846264338327950288419716939937510582097494459
#define NEVERC_MATH_PHI        1.61803398874989484820458683436563811772030917980576286213544862

#define NEVERC_MATH_SQRT2      1.41421356237309504880168872420969807856967187537694807317667974
#define NEVERC_MATH_SQRT_E     1.64872127070012814684865078781416357165377610071014801157507931
#define NEVERC_MATH_SQRT_PI    1.77245385090551602729816748334114518279754945612238712821380779
#define NEVERC_MATH_SQRT_PHI   1.27201964951406896425242246173749149171560804184009624861664038

#define NEVERC_MATH_LN2        0.693147180559945309417232121458176568075500134360255254120680009
#define NEVERC_MATH_LOG2E      1.44269504088896340735992468100189213742664595415298593413544940
#define NEVERC_MATH_LN10       2.30258509299404568401799145468436420760110148862877297603332790
#define NEVERC_MATH_LOG10E     0.43429448190325182765112891891660508229439700580366656611445378

/* Floating-point limits */
#define NEVERC_MATH_MAX_FLOAT32              3.40282346638528859811704183484516925440e+38f
#define NEVERC_MATH_SMALLEST_NONZERO_FLOAT32 1.401298464324817070923729583289916131280e-45f
#define NEVERC_MATH_MAX_FLOAT64              1.79769313486231570814527423731704356798070e+308
#define NEVERC_MATH_SMALLEST_NONZERO_FLOAT64 4.9406564584124654417656879286822137236505980e-324

/* Integer limits */
#define NEVERC_MATH_MAX_INT8    127
#define NEVERC_MATH_MIN_INT8    (-128)
#define NEVERC_MATH_MAX_INT16   32767
#define NEVERC_MATH_MIN_INT16   (-32768)
#define NEVERC_MATH_MAX_INT32   2147483647
#define NEVERC_MATH_MIN_INT32   (-2147483647 - 1)
#define NEVERC_MATH_MAX_INT64   9223372036854775807LL
#define NEVERC_MATH_MIN_INT64   (-9223372036854775807LL - 1)
#define NEVERC_MATH_MAX_UINT8   255U
#define NEVERC_MATH_MAX_UINT16  65535U
#define NEVERC_MATH_MAX_UINT32  4294967295U
#define NEVERC_MATH_MAX_UINT64  18446744073709551615ULL

/* ===== Basic Arithmetic ===== */

double neverc_math_abs(double x);
double neverc_math_dim(double x, double y);
double neverc_math_max(double x, double y);
double neverc_math_min(double x, double y);

/* ===== Trigonometric ===== */

double neverc_math_sin(double x);
double neverc_math_cos(double x);
double neverc_math_tan(double x);
void   neverc_math_sincos(double x, double *sin_val, double *cos_val);

/* ===== Inverse Trigonometric ===== */

double neverc_math_asin(double x);
double neverc_math_acos(double x);
double neverc_math_atan(double x);
double neverc_math_atan2(double y, double x);

/* ===== Hyperbolic ===== */

double neverc_math_sinh(double x);
double neverc_math_cosh(double x);
double neverc_math_tanh(double x);
double neverc_math_asinh(double x);
double neverc_math_acosh(double x);
double neverc_math_atanh(double x);

/* ===== Exponential & Logarithmic ===== */

double neverc_math_exp(double x);
double neverc_math_exp2(double x);
double neverc_math_expm1(double x);
double neverc_math_log(double x);
double neverc_math_log2(double x);
double neverc_math_log10(double x);
double neverc_math_log1p(double x);
double neverc_math_logb(double x);
int    neverc_math_ilogb(double x);

/* ===== Power & Root ===== */

double neverc_math_sqrt(double x);
double neverc_math_cbrt(double x);
double neverc_math_pow(double x, double y);
double neverc_math_pow10(int n);
double neverc_math_hypot(double p, double q);

/* ===== Rounding & Remainder ===== */

double neverc_math_ceil(double x);
double neverc_math_floor(double x);
double neverc_math_trunc(double x);
double neverc_math_round(double x);
double neverc_math_roundtoeven(double x);
double neverc_math_fmod(double x, double y);
double neverc_math_remainder(double x, double y);

/* ===== Decomposition ===== */

double neverc_math_modf(double x, double *iptr);
double neverc_math_frexp(double x, int *exp);
double neverc_math_ldexp(double frac, int exp);
double neverc_math_nextafter(double x, double y);
float  neverc_math_nextafter32(float x, float y);

/* ===== Sign & Bit Manipulation ===== */

double neverc_math_copysign(double f, double sign);
int    neverc_math_signbit(double x);

/* ===== FMA ===== */

double neverc_math_fma(double x, double y, double z);

/* ===== Error Function ===== */

double neverc_math_erf(double x);
double neverc_math_erfc(double x);
double neverc_math_erfinv(double x);
double neverc_math_erfcinv(double x);

/* ===== Gamma ===== */

double neverc_math_gamma(double x);
double neverc_math_lgamma(double x);
double neverc_math_lgamma_sign(double x, int *sign);

/* ===== Special Values ===== */

double neverc_math_nan(void);
double neverc_math_inf(int sign);
int    neverc_math_isnan(double x);
int    neverc_math_isinf(double x, int sign);

/* ===== Bessel Functions ===== */

double neverc_math_j0(double x);
double neverc_math_y0(double x);
double neverc_math_j1(double x);
double neverc_math_y1(double x);
double neverc_math_jn(int n, double x);
double neverc_math_yn(int n, double x);

/* ===== Bit-level Helpers ===== */

uint64_t neverc_math_float64bits(double f);
double   neverc_math_float64frombits(uint64_t bits);
uint32_t neverc_math_float32bits(float f);
float    neverc_math_float32frombits(uint32_t bits);

#ifdef __cplusplus
}
#endif

#endif /* NEVERC_MATH_H */
