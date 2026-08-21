/*
 * Complex number math — ported from Go math/cmplx package.
 * Originally from Cephes Math Library (Stephen L. Moshier).
 * All functions use neverc_math_* — zero libc math dependency.
 */
#include "neverc/std/math/cmplx.h"
#include <stdint.h>

#define RE(z) ((z).re)
#define IM(z) ((z).im)
#define MK(r,i) neverc_cmplx(r,i)

/* Go math/cmplx.reducePi Cody-Waite reduction for |x| < 2^30.
 * Atan2 yields at most π, so Atan's ½atan2 argument always fits. */
static double cmplx_reduce_pi(double x) {
    const double pi1 = 3.141592502593994;       /* 0x400921fb40000000 */
    const double pi2 = 1.5099578831723193e-07;  /* 0x3e84442d00000000 */
    const double pi3 = 1.0780605716316238e-14;  /* 0x3d08469898cc5170 */
    double t = x / NEVERC_MATH_PI;
    t += 0.5;
    t = (double)(int64_t)t;
    return ((x - t * pi1) - t * pi2) - t * pi3;
}

/* ===== Basic ===== */

double neverc_cmplx_abs(neverc_cmplx_t z) {
    return neverc_math_hypot(RE(z), IM(z));
}

double neverc_cmplx_phase(neverc_cmplx_t z) {
    return neverc_math_atan2(IM(z), RE(z));
}

neverc_cmplx_t neverc_cmplx_conj(neverc_cmplx_t z) {
    return MK(RE(z), -IM(z));
}

void neverc_cmplx_polar(neverc_cmplx_t z, double *r, double *theta) {
    if (r) *r = neverc_cmplx_abs(z);
    if (theta) *theta = neverc_cmplx_phase(z);
}

neverc_cmplx_t neverc_cmplx_rect(double r, double theta) {
    double s, c;
    neverc_math_sincos(theta, &s, &c);
    return MK(r * c, r * s);
}

/* ===== Special values ===== */

int neverc_cmplx_isnan(neverc_cmplx_t z) {
    if (neverc_math_isinf(RE(z), 0) || neverc_math_isinf(IM(z), 0))
        return 0;
    return neverc_math_isnan(RE(z)) || neverc_math_isnan(IM(z));
}

int neverc_cmplx_isinf(neverc_cmplx_t z) {
    return neverc_math_isinf(RE(z), 0) || neverc_math_isinf(IM(z), 0);
}

neverc_cmplx_t neverc_cmplx_nan_val(void) {
    return MK(neverc_math_nan(), neverc_math_nan());
}

neverc_cmplx_t neverc_cmplx_inf_val(void) {
    return MK(neverc_math_inf(1), neverc_math_inf(1));
}

/* ===== Exponential & Logarithmic ===== */

neverc_cmplx_t neverc_cmplx_exp(neverc_cmplx_t z) {
    double re = RE(z), im = IM(z);

    if (neverc_math_isinf(re, 0)) {
        if (re > 0 && im == 0.0)
            return z;
        if (neverc_math_isinf(im, 0) || neverc_math_isnan(im)) {
            if (re < 0)
                return MK(0.0, neverc_math_copysign(0.0, im));
            else
                return MK(neverc_math_inf(1), neverc_math_nan());
        }
    }
    if (neverc_math_isnan(re) && im == 0.0)
        return MK(neverc_math_nan(), im);

    double r = neverc_math_exp(re);
    double s, c;
    neverc_math_sincos(im, &s, &c);
    return MK(r * c, r * s);
}

neverc_cmplx_t neverc_cmplx_log(neverc_cmplx_t z) {
    return MK(neverc_math_log(neverc_cmplx_abs(z)), neverc_cmplx_phase(z));
}

neverc_cmplx_t neverc_cmplx_log10(neverc_cmplx_t z) {
    neverc_cmplx_t w = neverc_cmplx_log(z);
    return MK(NEVERC_MATH_LOG10E * RE(w), NEVERC_MATH_LOG10E * IM(w));
}

/* ===== Power & Root ===== */

neverc_cmplx_t neverc_cmplx_sqrt(neverc_cmplx_t z) {
    double a = RE(z), b = IM(z);

    if (b == 0.0) {
        if (a == 0.0) return MK(0.0, b);
        if (a < 0.0) return MK(0.0, neverc_math_copysign(neverc_math_sqrt(-a), b));
        return MK(neverc_math_sqrt(a), b);
    }
    if (neverc_math_isinf(b, 0))
        return MK(neverc_math_inf(1), b);

    if (a == 0.0) {
        if (b < 0.0) {
            double r = neverc_math_sqrt(-0.5 * b);
            return MK(r, -r);
        }
        double r = neverc_math_sqrt(0.5 * b);
        return MK(r, r);
    }

    double scale;
    if (neverc_math_abs(a) > 4.0 || neverc_math_abs(b) > 4.0) {
        a *= 0.25;
        b *= 0.25;
        scale = 2.0;
    } else {
        a *= 1.8014398509481984e16;
        b *= 1.8014398509481984e16;
        scale = 7.450580596923828125e-9;
    }

    double r = neverc_math_hypot(a, b);
    double t;
    if (a > 0.0) {
        t = neverc_math_sqrt(0.5 * r + 0.5 * a);
        r = scale * neverc_math_abs((0.5 * b) / t);
        t *= scale;
    } else {
        r = neverc_math_sqrt(0.5 * r - 0.5 * a);
        t = scale * neverc_math_abs((0.5 * b) / r);
        r *= scale;
    }

    if (b < 0.0)
        return MK(t, -r);
    return MK(t, r);
}

neverc_cmplx_t neverc_cmplx_pow(neverc_cmplx_t x, neverc_cmplx_t y) {
    /* IEEE 754 / Go math.Pow: z^0 = 1 for every z. exp(0*log(Inf|NaN)) is NaN. */
    if (RE(y) == 0.0 && IM(y) == 0.0)
        return MK(1.0, 0.0);
    if (RE(x) == 0.0 && IM(x) == 0.0) {
        /* Go cmplx.Pow / IsNaN: Inf in either part wins over NaN, so
         * 0^(Inf+NaNi) is +0 rather than NaN. */
        if (neverc_cmplx_isnan(y))
            return neverc_cmplx_nan_val();
        double yr = RE(y), yi = IM(y);
        if (yr == 0.0)
            return MK(1.0, 0.0);
        if (yr < 0.0) {
            if (yi == 0.0)
                return MK(neverc_math_inf(1), 0.0);
            return neverc_cmplx_inf_val();
        }
        return MK(0.0, 0.0);
    }
    neverc_cmplx_t modarg = neverc_cmplx_log(x);
    double rr = RE(modarg) * RE(y) - IM(modarg) * IM(y);
    double ri = RE(modarg) * IM(y) + IM(modarg) * RE(y);
    return neverc_cmplx_exp(MK(rr, ri));
}

/* ===== Trigonometric ===== */

neverc_cmplx_t neverc_cmplx_sin(neverc_cmplx_t z) {
    double re = RE(z), im = IM(z);
    /* Go math/cmplx.Sin / C99 G.6: 0*Inf from the algebraic form is NaN. */
    if (im == 0.0 && (neverc_math_isinf(re, 0) || neverc_math_isnan(re)))
        return MK(neverc_math_nan(), im);
    if (neverc_math_isinf(im, 0)) {
        if (re == 0.0)
            return z;
        if (neverc_math_isinf(re, 0) || neverc_math_isnan(re))
            return MK(neverc_math_nan(), im);
    } else if (re == 0.0 && neverc_math_isnan(im))
        return z;

    double s, c;
    neverc_math_sincos(re, &s, &c);
    double sh = neverc_math_sinh(im);
    double ch = neverc_math_cosh(im);
    return MK(s * ch, c * sh);
}

neverc_cmplx_t neverc_cmplx_cos(neverc_cmplx_t z) {
    double re = RE(z), im = IM(z);
    /* Go math/cmplx.Cos / C99 G.6: 0*Inf from the algebraic form is NaN. */
    if (im == 0.0 && (neverc_math_isinf(re, 0) || neverc_math_isnan(re)))
        return MK(neverc_math_nan(), -im * neverc_math_copysign(0.0, re));
    if (neverc_math_isinf(im, 0)) {
        if (re == 0.0)
            return MK(neverc_math_inf(1), -re * neverc_math_copysign(0.0, im));
        if (neverc_math_isinf(re, 0) || neverc_math_isnan(re))
            return MK(neverc_math_inf(1), neverc_math_nan());
    } else if (re == 0.0 && neverc_math_isnan(im))
        return MK(neverc_math_nan(), 0.0);

    double s, c;
    neverc_math_sincos(re, &s, &c);
    double sh = neverc_math_sinh(im);
    double ch = neverc_math_cosh(im);
    return MK(c * ch, -s * sh);
}

neverc_cmplx_t neverc_cmplx_tan(neverc_cmplx_t z) {
    double re = RE(z), im = IM(z);
    /* Go math/cmplx.Tan: Inf imag saturates to ±i; 0+NaN i is unchanged.
     * Double-angle form avoids Inf/Inf = NaN from the sin/cos ratio. */
    if (neverc_math_isinf(im, 0)) {
        if (neverc_math_isinf(re, 0) || neverc_math_isnan(re))
            return MK(neverc_math_copysign(0.0, re), neverc_math_copysign(1.0, im));
        return MK(neverc_math_copysign(0.0, neverc_math_sin(2.0 * re)),
                  neverc_math_copysign(1.0, im));
    }
    if (re == 0.0 && neverc_math_isnan(im))
        return z;

    /* cos(2x)+cosh(2y) = 2(cos²x + sinh²y). The sum-of-trig form
     * cancels to 0 in float64 near the real poles (odd multiples of
     * π/2), so tan(π/2 + iε) became Inf instead of i coth(ε). */
    double c = neverc_math_cos(re);
    double sh = neverc_math_sinh(im);
    double d = 2.0 * (c * c + sh * sh);
    if (d == 0.0)
        return neverc_cmplx_inf_val();
    return MK(neverc_math_sin(2.0 * re) / d, neverc_math_sinh(2.0 * im) / d);
}

/* ===== Hyperbolic ===== */

neverc_cmplx_t neverc_cmplx_sinh(neverc_cmplx_t z) {
    double re = RE(z), im = IM(z);
    /* Go math/cmplx.Sinh / C99 G.6.2.5: Inf*0 from the algebraic form is NaN. */
    if (re == 0.0 && (neverc_math_isinf(im, 0) || neverc_math_isnan(im)))
        return MK(re, neverc_math_nan());
    if (neverc_math_isinf(re, 0)) {
        if (im == 0.0)
            return MK(re, im);
        if (neverc_math_isinf(im, 0) || neverc_math_isnan(im))
            return MK(re, neverc_math_nan());
    } else if (im == 0.0 && neverc_math_isnan(re))
        return MK(neverc_math_nan(), im);

    double s, c;
    neverc_math_sincos(im, &s, &c);
    double sh = neverc_math_sinh(re);
    double ch = neverc_math_cosh(re);
    return MK(sh * c, ch * s);
}

neverc_cmplx_t neverc_cmplx_cosh(neverc_cmplx_t z) {
    double re = RE(z), im = IM(z);
    /* Go math/cmplx.Cosh / C99 G.6.2.4: Inf*0 from the algebraic form is NaN. */
    if (re == 0.0 && (neverc_math_isinf(im, 0) || neverc_math_isnan(im)))
        return MK(neverc_math_nan(), re * neverc_math_copysign(0.0, im));
    if (neverc_math_isinf(re, 0)) {
        if (im == 0.0)
            return MK(neverc_math_inf(1), im * neverc_math_copysign(0.0, re));
        if (neverc_math_isinf(im, 0) || neverc_math_isnan(im))
            return MK(neverc_math_inf(1), neverc_math_nan());
    } else if (im == 0.0 && neverc_math_isnan(re))
        return MK(neverc_math_nan(), im);

    double s, c;
    neverc_math_sincos(im, &s, &c);
    double sh = neverc_math_sinh(re);
    double ch = neverc_math_cosh(re);
    return MK(ch * c, sh * s);
}

neverc_cmplx_t neverc_cmplx_tanh(neverc_cmplx_t z) {
    double re = RE(z), im = IM(z);
    /* Go math/cmplx.Tanh: Inf real saturates to ±1; NaN+0i is unchanged. */
    if (neverc_math_isinf(re, 0)) {
        if (neverc_math_isinf(im, 0) || neverc_math_isnan(im))
            return MK(neverc_math_copysign(1.0, re), neverc_math_copysign(0.0, im));
        return MK(neverc_math_copysign(1.0, re),
                  neverc_math_copysign(0.0, neverc_math_sin(2.0 * im)));
    }
    if (im == 0.0 && neverc_math_isnan(re))
        return z;

    double d = neverc_math_cosh(2.0 * re) + neverc_math_cos(2.0 * im);
    if (d == 0.0)
        return neverc_cmplx_inf_val();
    return MK(neverc_math_sinh(2.0 * re) / d, neverc_math_sin(2.0 * im) / d);
}

/* ===== Inverse Trigonometric ===== */

neverc_cmplx_t neverc_cmplx_asin(neverc_cmplx_t z) {
    /* asin(z) = -i * log(i*z + sqrt(1 - z*z)) */
    double a = RE(z), b = IM(z);
    /* Inf/NaN: the algebraic form does Inf*0 and returns NaN (Go math/cmplx.Asin). */
    if (b == 0.0 && neverc_math_abs(a) <= 1.0)
        return MK(neverc_math_asin(a), b);
    if (a == 0.0 && neverc_math_abs(b) <= 1.0)
        return MK(a, neverc_math_asinh(b));
    if (neverc_math_isnan(b)) {
        if (a == 0.0)
            return MK(a, neverc_math_nan());
        if (neverc_math_isinf(a, 0))
            return MK(neverc_math_nan(), a);
        return neverc_cmplx_nan_val();
    }
    if (neverc_math_isinf(b, 0)) {
        if (neverc_math_isnan(a))
            return z;
        if (neverc_math_isinf(a, 0))
            return MK(neverc_math_copysign(NEVERC_MATH_PI / 4.0, a), b);
        return MK(neverc_math_copysign(0.0, a), b);
    }
    if (neverc_math_isinf(a, 0))
        return MK(neverc_math_copysign(NEVERC_MATH_PI / 2.0, a),
                  neverc_math_copysign(a, b));

    neverc_cmplx_t z2 = MK(a*a - b*b, 2.0*a*b);
    neverc_cmplx_t one_minus_z2 = MK(1.0 - RE(z2), -IM(z2));
    neverc_cmplx_t sq = neverc_cmplx_sqrt(one_minus_z2);
    neverc_cmplx_t iz_plus_sq = MK(-b + RE(sq), a + IM(sq));
    neverc_cmplx_t w = neverc_cmplx_log(iz_plus_sq);
    return MK(IM(w), -RE(w));
}

neverc_cmplx_t neverc_cmplx_acos(neverc_cmplx_t z) {
    /* acos(z) = pi/2 - asin(z) */
    neverc_cmplx_t w = neverc_cmplx_asin(z);
    return MK(NEVERC_MATH_PI / 2.0 - RE(w), -IM(w));
}

neverc_cmplx_t neverc_cmplx_atan(neverc_cmplx_t z) {
    /* atan(z) = (1/2i) log((1+iz)/(1-iz)); evaluated as in Go math/cmplx.Atan. */
    double a = RE(z), b = IM(z);
    /* Inf: (1±Inf)/(1∓Inf) is NaN; C99/Go: atan(±Inf) = ±π/2, atan(±i∞) = ±π/2. */
    if (b == 0.0)
        return MK(neverc_math_atan(a), b);
    if (a == 0.0 && neverc_math_abs(b) <= 1.0)
        return MK(a, neverc_math_atanh(b));
    if (neverc_math_isinf(b, 0) || neverc_math_isinf(a, 0)) {
        if (neverc_math_isnan(a))
            return MK(neverc_math_nan(), neverc_math_copysign(0.0, b));
        return MK(neverc_math_copysign(NEVERC_MATH_PI / 2.0, a),
                  neverc_math_copysign(0.0, b));
    }
    if (neverc_math_isnan(a) || neverc_math_isnan(b))
        return neverc_cmplx_nan_val();

    /* Go math/cmplx.Atan: Re = reducePi(½ atan2(2x, 1-x²-y²)),
     * Im = ¼ log(r₊/r₋). reducePi maps +π/2 → −π/2, so atan(+0+2i)
     * is −π/2 + i½ln3, not +π/2. a==0 / imag-denominator 0 are NaN. */
    double x2 = a * a;
    double aa = 1.0 - x2 - b * b;
    if (aa == 0.0)
        return neverc_cmplx_nan_val();
    double w = cmplx_reduce_pi(0.5 * neverc_math_atan2(2.0 * a, aa));
    double t = b - 1.0;
    double bb = x2 + t * t;
    if (bb == 0.0)
        return neverc_cmplx_nan_val();
    t = b + 1.0;
    return MK(w, 0.25 * neverc_math_log((x2 + t * t) / bb));
}

neverc_cmplx_t neverc_cmplx_asinh(neverc_cmplx_t z) {
    /* asinh(z) = log(z + sqrt(1 + z*z)); special cases from Go math/cmplx.Asinh. */
    double a = RE(z), b = IM(z);
    if (b == 0.0 && neverc_math_abs(a) <= 1.0)
        return MK(neverc_math_asinh(a), b);
    if (a == 0.0 && neverc_math_abs(b) <= 1.0)
        return MK(a, neverc_math_asin(b));
    if (neverc_math_isinf(a, 0)) {
        if (neverc_math_isinf(b, 0))
            return MK(a, neverc_math_copysign(NEVERC_MATH_PI / 4.0, b));
        if (neverc_math_isnan(b))
            return z;
        return MK(a, neverc_math_copysign(0.0, b));
    }
    if (neverc_math_isnan(a)) {
        if (b == 0.0)
            return z;
        if (neverc_math_isinf(b, 0))
            return MK(b, a);
        return neverc_cmplx_nan_val();
    }
    if (neverc_math_isinf(b, 0))
        return MK(neverc_math_copysign(b, a),
                  neverc_math_copysign(NEVERC_MATH_PI / 2.0, b));

    neverc_cmplx_t z2 = MK(a * a - b * b, 2.0 * a * b);
    neverc_cmplx_t one_plus_z2 = MK(1.0 + RE(z2), IM(z2));
    neverc_cmplx_t sq = neverc_cmplx_sqrt(one_plus_z2);
    return neverc_cmplx_log(MK(a + RE(sq), b + IM(sq)));
}

neverc_cmplx_t neverc_cmplx_acosh(neverc_cmplx_t z) {
    /* acosh(z) = ±i * acos(z) with real part ≥ 0. Go math/cmplx.Acosh. */
    double a = RE(z), b = IM(z);
    if (a == 0.0 && b == 0.0)
        return MK(0.0, neverc_math_copysign(NEVERC_MATH_PI / 2.0, b));
    neverc_cmplx_t w = neverc_cmplx_acos(z);
    if (IM(w) <= 0.0)
        return MK(-IM(w), RE(w));
    return MK(IM(w), -RE(w));
}

neverc_cmplx_t neverc_cmplx_atanh(neverc_cmplx_t z) {
    /* atanh(z) = -i * atan(i*z) */
    neverc_cmplx_t w = neverc_cmplx_atan(MK(-IM(z), RE(z)));
    return MK(IM(w), -RE(w));
}

neverc_cmplx_t neverc_cmplx_cot(neverc_cmplx_t z) {
    /* Go math/cmplx.Cot: (sin 2x - i sinh 2y) / (cosh 2y - cos 2x).
     * Identity 2(sin²x + sinh²y) avoids the same 1-1 cancellation as tan. */
    double re = RE(z), im = IM(z);
    double s = neverc_math_sin(re);
    double sh = neverc_math_sinh(im);
    double d = 2.0 * (s * s + sh * sh);
    if (d == 0.0)
        return neverc_cmplx_inf_val();
    return MK(neverc_math_sin(2.0 * re) / d, -neverc_math_sinh(2.0 * im) / d);
}
