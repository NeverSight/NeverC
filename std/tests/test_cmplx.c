/*
 * NeverC Complex Math Library — Test Suite
 * Verifies mathematical identities, known values, and special case handling.
 */
#include "neverc/cmplx.h"
#include <stdio.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define EPSILON 1e-12

static void check_double(const char *name, double got, double expected) {
    tests_run++;
    if (neverc_math_isnan(expected) && neverc_math_isnan(got)) { tests_passed++; return; }
    if (neverc_math_isinf(expected, 0) && neverc_math_isinf(got, 0)) {
        if ((expected > 0) == (got > 0)) { tests_passed++; return; }
    }
    double diff = neverc_math_abs(got - expected);
    double rel = (expected != 0.0) ? neverc_math_abs((got - expected) / expected) : diff;
    if (diff < EPSILON || rel < EPSILON) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s: got %.17g, expected %.17g\n", name, got, expected); }
}

static void check_cmplx(const char *name, neverc_cmplx_t got, double ER, double EI) {
    char buf[256];
    snprintf(buf, sizeof(buf), "%s.re", name);
    check_double(buf, got.re, ER);
    snprintf(buf, sizeof(buf), "%s.im", name);
    check_double(buf, got.im, EI);
}

static void check_true(const char *name, int cond) {
    tests_run++;
    if (cond) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s\n", name); }
}

#define C(r,i) neverc_cmplx(r,i)
#define NC_NAN neverc_math_nan()
#define NC_INF neverc_math_inf(1)

/* ===== Basic operations ===== */

static void test_basic(void) {
    printf("[basic: abs/phase/conj/polar/rect]\n");

    check_double("abs(3+4i)", neverc_cmplx_abs(C(3.0, 4.0)), 5.0);
    check_double("abs(5+12i)", neverc_cmplx_abs(C(5.0, 12.0)), 13.0);
    check_double("abs(0+0i)", neverc_cmplx_abs(C(0.0, 0.0)), 0.0);
    check_double("abs(1+0i)", neverc_cmplx_abs(C(1.0, 0.0)), 1.0);
    check_double("abs(0+1i)", neverc_cmplx_abs(C(0.0, 1.0)), 1.0);

    check_double("phase(1+0i)", neverc_cmplx_phase(C(1.0, 0.0)), 0.0);
    check_double("phase(0+1i)", neverc_cmplx_phase(C(0.0, 1.0)), NEVERC_MATH_PI / 2.0);
    check_double("phase(-1+0i)", neverc_cmplx_phase(C(-1.0, 0.0)), NEVERC_MATH_PI);
    check_double("phase(0-1i)", neverc_cmplx_phase(C(0.0, -1.0)), -NEVERC_MATH_PI / 2.0);

    check_cmplx("conj(3+4i)", neverc_cmplx_conj(C(3.0, 4.0)), 3.0, -4.0);
    check_cmplx("conj(1-2i)", neverc_cmplx_conj(C(1.0, -2.0)), 1.0, 2.0);

    double r, theta;
    neverc_cmplx_polar(C(1.0, 1.0), &r, &theta);
    check_double("polar(1+1i).r", r, NEVERC_MATH_SQRT2);
    check_double("polar(1+1i).theta", theta, NEVERC_MATH_PI / 4.0);

    check_cmplx("rect(1,0)", neverc_cmplx_rect(1.0, 0.0), 1.0, 0.0);
    check_cmplx("rect(1,pi/2)", neverc_cmplx_rect(1.0, NEVERC_MATH_PI / 2.0), 0.0, 1.0);
    check_cmplx("rect(2,pi)", neverc_cmplx_rect(2.0, NEVERC_MATH_PI), -2.0, 0.0);

    /* polar/rect round-trip */
    neverc_cmplx_t z = C(3.0, 4.0);
    neverc_cmplx_polar(z, &r, &theta);
    neverc_cmplx_t back = neverc_cmplx_rect(r, theta);
    check_cmplx("polar-rect roundtrip(3+4i)", back, 3.0, 4.0);
}

/* ===== Exponential ===== */

static void test_exp(void) {
    printf("[exp/log/log10]\n");

    /* e^0 = 1 */
    check_cmplx("exp(0)", neverc_cmplx_exp(C(0.0, 0.0)), 1.0, 0.0);

    /* e^(i*pi) = -1 (Euler's identity) */
    neverc_cmplx_t euler = neverc_cmplx_exp(C(0.0, NEVERC_MATH_PI));
    check_cmplx("exp(i*pi)=-1", euler, -1.0, 0.0);

    /* e^(1+0i) = e */
    check_cmplx("exp(1+0i)", neverc_cmplx_exp(C(1.0, 0.0)), NEVERC_MATH_E, 0.0);

    /* e^(0+i*pi/2) = i */
    neverc_cmplx_t z = neverc_cmplx_exp(C(0.0, NEVERC_MATH_PI / 2.0));
    check_cmplx("exp(i*pi/2)=i", z, 0.0, 1.0);

    /* exp(log(z)) = z round-trip */
    neverc_cmplx_t vals[] = { C(1.0, 2.0), C(-1.0, 3.0), C(0.5, -0.5), C(10.0, 0.0) };
    for (int i = 0; i < 4; i++) {
        char buf[128];
        snprintf(buf, sizeof(buf), "exp(log(z)) roundtrip %d", i);
        neverc_cmplx_t rt = neverc_cmplx_exp(neverc_cmplx_log(vals[i]));
        check_cmplx(buf, rt, vals[i].re, vals[i].im);
    }

    /* log(1) = 0 */
    check_cmplx("log(1+0i)", neverc_cmplx_log(C(1.0, 0.0)), 0.0, 0.0);

    /* log(e) = 1 */
    check_cmplx("log(e+0i)", neverc_cmplx_log(C(NEVERC_MATH_E, 0.0)), 1.0, 0.0);

    /* log(-1) = i*pi */
    z = neverc_cmplx_log(C(-1.0, 0.0));
    check_double("log(-1).re", z.re, 0.0);
    check_double("log(-1).im", z.im, NEVERC_MATH_PI);

    /* log10(10+0i) = 1+0i */
    check_cmplx("log10(10+0i)", neverc_cmplx_log10(C(10.0, 0.0)), 1.0, 0.0);

    /* log10(100+0i) = 2+0i */
    check_cmplx("log10(100+0i)", neverc_cmplx_log10(C(100.0, 0.0)), 2.0, 0.0);
}

/* ===== Sqrt ===== */

static void test_sqrt(void) {
    printf("[sqrt]\n");

    check_cmplx("sqrt(4+0i)", neverc_cmplx_sqrt(C(4.0, 0.0)), 2.0, 0.0);
    check_cmplx("sqrt(0+0i)", neverc_cmplx_sqrt(C(0.0, 0.0)), 0.0, 0.0);
    check_cmplx("sqrt(1+0i)", neverc_cmplx_sqrt(C(1.0, 0.0)), 1.0, 0.0);

    /* sqrt(-1) = i */
    neverc_cmplx_t z = neverc_cmplx_sqrt(C(-1.0, 0.0));
    check_double("sqrt(-1).re", z.re, 0.0);
    check_double("sqrt(-1).im", neverc_math_abs(z.im), 1.0);

    /* sqrt(-4) = 2i */
    z = neverc_cmplx_sqrt(C(-4.0, 0.0));
    check_double("sqrt(-4).re", z.re, 0.0);
    check_double("sqrt(-4).im", neverc_math_abs(z.im), 2.0);

    /* sqrt(z)^2 = z roundtrip */
    neverc_cmplx_t test_vals[] = { C(3.0, 4.0), C(-2.0, 1.0), C(0.0, 5.0), C(-7.0, -3.0) };
    for (int i = 0; i < 4; i++) {
        char buf[128];
        snprintf(buf, sizeof(buf), "sqrt(z)^2=z roundtrip %d", i);
        neverc_cmplx_t sq = neverc_cmplx_sqrt(test_vals[i]);
        neverc_cmplx_t sq2 = C(sq.re*sq.re - sq.im*sq.im, 2.0*sq.re*sq.im);
        check_cmplx(buf, sq2, test_vals[i].re, test_vals[i].im);
    }

    /* sqrt(0+2i) = 1+i */
    z = neverc_cmplx_sqrt(C(0.0, 2.0));
    check_double("sqrt(0+2i).re", z.re, 1.0);
    check_double("sqrt(0+2i).im", z.im, 1.0);
}

/* ===== Pow ===== */

static void test_pow(void) {
    printf("[pow]\n");

    check_cmplx("pow(2,3)", neverc_cmplx_pow(C(2.0, 0.0), C(3.0, 0.0)), 8.0, 0.0);
    check_cmplx("pow(z,0)=1", neverc_cmplx_pow(C(3.0, 4.0), C(0.0, 0.0)), 1.0, 0.0);
    check_cmplx("pow(0,0)=1", neverc_cmplx_pow(C(0.0, 0.0), C(0.0, 0.0)), 1.0, 0.0);

    /* pow(i, 2) = -1 */
    neverc_cmplx_t z = neverc_cmplx_pow(C(0.0, 1.0), C(2.0, 0.0));
    check_cmplx("pow(i,2)=-1", z, -1.0, 0.0);

    /* pow(e, i*pi) = -1 (Euler) */
    z = neverc_cmplx_pow(C(NEVERC_MATH_E, 0.0), C(0.0, NEVERC_MATH_PI));
    check_cmplx("pow(e,i*pi)=-1", z, -1.0, 0.0);
}

/* ===== Trigonometric ===== */

static void test_trig(void) {
    printf("[sin/cos/tan]\n");

    /* sin(0) = 0, cos(0) = 1 */
    check_cmplx("sin(0)", neverc_cmplx_sin(C(0.0, 0.0)), 0.0, 0.0);
    check_cmplx("cos(0)", neverc_cmplx_cos(C(0.0, 0.0)), 1.0, 0.0);

    /* For real arguments, should match real functions */
    double test_angles[] = { 0.5, 1.0, 2.0, -1.5, NEVERC_MATH_PI };
    for (int i = 0; i < 5; i++) {
        double x = test_angles[i];
        char buf[128];
        snprintf(buf, sizeof(buf), "sin(%.1f+0i).re=sin(%.1f)", x, x);
        check_double(buf, neverc_cmplx_sin(C(x, 0.0)).re, neverc_math_sin(x));
        snprintf(buf, sizeof(buf), "cos(%.1f+0i).re=cos(%.1f)", x, x);
        check_double(buf, neverc_cmplx_cos(C(x, 0.0)).re, neverc_math_cos(x));
    }

    /* sin^2(z) + cos^2(z) = 1 */
    neverc_cmplx_t zvecs[] = { C(1.0, 2.0), C(-0.5, 1.5), C(3.0, -1.0), C(0.0, 1.0) };
    for (int i = 0; i < 4; i++) {
        char buf[128];
        neverc_cmplx_t s = neverc_cmplx_sin(zvecs[i]);
        neverc_cmplx_t c = neverc_cmplx_cos(zvecs[i]);
        double s2_re = s.re*s.re - s.im*s.im;
        double s2_im = 2.0*s.re*s.im;
        double c2_re = c.re*c.re - c.im*c.im;
        double c2_im = 2.0*c.re*c.im;
        snprintf(buf, sizeof(buf), "sin^2+cos^2=1 (%d)", i);
        check_double(buf, s2_re + c2_re, 1.0);
        snprintf(buf, sizeof(buf), "sin^2+cos^2=1 im(%d)", i);
        check_double(buf, s2_im + c2_im, 0.0);
    }

    /* tan = sin/cos consistency */
    for (int i = 0; i < 4; i++) {
        char buf[128];
        neverc_cmplx_t t = neverc_cmplx_tan(zvecs[i]);
        neverc_cmplx_t s = neverc_cmplx_sin(zvecs[i]);
        neverc_cmplx_t c = neverc_cmplx_cos(zvecs[i]);
        double d2 = c.re*c.re + c.im*c.im;
        double expected_re = (s.re*c.re + s.im*c.im) / d2;
        double expected_im = (s.im*c.re - s.re*c.im) / d2;
        snprintf(buf, sizeof(buf), "tan=sin/cos (%d)", i);
        check_cmplx(buf, t, expected_re, expected_im);
    }

    /* sin(i) = i*sinh(1) */
    neverc_cmplx_t s_i = neverc_cmplx_sin(C(0.0, 1.0));
    check_double("sin(i).re", s_i.re, 0.0);
    check_double("sin(i).im", s_i.im, neverc_math_sinh(1.0));

    /* cos(i) = cosh(1) */
    neverc_cmplx_t c_i = neverc_cmplx_cos(C(0.0, 1.0));
    check_double("cos(i).re", c_i.re, neverc_math_cosh(1.0));
    check_double("cos(i).im", c_i.im, 0.0);
}

/* ===== Hyperbolic ===== */

static void test_hyp(void) {
    printf("[sinh/cosh/tanh]\n");

    /* For real args, should match real functions */
    double xvals[] = { 0.5, 1.0, 2.0, -1.0 };
    for (int i = 0; i < 4; i++) {
        double x = xvals[i];
        char buf[128];
        snprintf(buf, sizeof(buf), "sinh(%.1f+0i).re", x);
        check_double(buf, neverc_cmplx_sinh(C(x, 0.0)).re, neverc_math_sinh(x));
        snprintf(buf, sizeof(buf), "cosh(%.1f+0i).re", x);
        check_double(buf, neverc_cmplx_cosh(C(x, 0.0)).re, neverc_math_cosh(x));
        snprintf(buf, sizeof(buf), "tanh(%.1f+0i).re", x);
        check_double(buf, neverc_cmplx_tanh(C(x, 0.0)).re, neverc_math_tanh(x));
    }

    /* cosh^2(z) - sinh^2(z) = 1 */
    neverc_cmplx_t zvecs[] = { C(1.0, 0.5), C(-0.5, 1.0), C(0.0, 1.0) };
    for (int i = 0; i < 3; i++) {
        char buf[128];
        neverc_cmplx_t sh = neverc_cmplx_sinh(zvecs[i]);
        neverc_cmplx_t ch = neverc_cmplx_cosh(zvecs[i]);
        double ch2_re = ch.re*ch.re - ch.im*ch.im;
        double sh2_re = sh.re*sh.re - sh.im*sh.im;
        double ch2_im = 2.0*ch.re*ch.im;
        double sh2_im = 2.0*sh.re*sh.im;
        snprintf(buf, sizeof(buf), "cosh^2-sinh^2=1 re(%d)", i);
        check_double(buf, ch2_re - sh2_re, 1.0);
        snprintf(buf, sizeof(buf), "cosh^2-sinh^2=1 im(%d)", i);
        check_double(buf, ch2_im - sh2_im, 0.0);
    }

    /* sinh(iz) = i*sin(z) */
    neverc_cmplx_t z = C(1.0, 2.0);
    neverc_cmplx_t iz = C(-z.im, z.re);
    neverc_cmplx_t sh_iz = neverc_cmplx_sinh(iz);
    neverc_cmplx_t sin_z = neverc_cmplx_sin(z);
    neverc_cmplx_t i_sin_z = C(-sin_z.im, sin_z.re);
    check_cmplx("sinh(iz)=i*sin(z)", sh_iz, i_sin_z.re, i_sin_z.im);
}

/* ===== Inverse trig ===== */

static void test_inv_trig(void) {
    printf("[asin/acos/atan]\n");

    /* sin(asin(z)) = z roundtrip */
    neverc_cmplx_t zvecs[] = { C(0.5, 0.3), C(-0.2, 0.8), C(0.0, 0.5) };
    for (int i = 0; i < 3; i++) {
        char buf[128];
        neverc_cmplx_t as = neverc_cmplx_asin(zvecs[i]);
        neverc_cmplx_t rt = neverc_cmplx_sin(as);
        snprintf(buf, sizeof(buf), "sin(asin(z))=z (%d)", i);
        check_cmplx(buf, rt, zvecs[i].re, zvecs[i].im);
    }

    /* cos(acos(z)) = z roundtrip */
    for (int i = 0; i < 3; i++) {
        char buf[128];
        neverc_cmplx_t ac = neverc_cmplx_acos(zvecs[i]);
        neverc_cmplx_t rt = neverc_cmplx_cos(ac);
        snprintf(buf, sizeof(buf), "cos(acos(z))=z (%d)", i);
        check_cmplx(buf, rt, zvecs[i].re, zvecs[i].im);
    }

    /* tan(atan(z)) = z roundtrip */
    for (int i = 0; i < 3; i++) {
        char buf[128];
        neverc_cmplx_t at = neverc_cmplx_atan(zvecs[i]);
        neverc_cmplx_t rt = neverc_cmplx_tan(at);
        snprintf(buf, sizeof(buf), "tan(atan(z))=z (%d)", i);
        check_cmplx(buf, rt, zvecs[i].re, zvecs[i].im);
    }

    /* asin(0) = 0 */
    check_cmplx("asin(0)", neverc_cmplx_asin(C(0.0, 0.0)), 0.0, 0.0);

    /* acos(1) = 0 */
    check_cmplx("acos(1)", neverc_cmplx_acos(C(1.0, 0.0)), 0.0, 0.0);

    /* atan(0) = 0 */
    check_cmplx("atan(0)", neverc_cmplx_atan(C(0.0, 0.0)), 0.0, 0.0);

    /* Real-valued consistency: asin(0.5+0i) should have re = asin(0.5) */
    double as_real = neverc_cmplx_asin(C(0.5, 0.0)).re;
    check_double("asin(0.5+0i).re", as_real, neverc_math_asin(0.5));
}

/* ===== Special values ===== */

static void test_special(void) {
    printf("[special: isnan/isinf]\n");

    check_true("isnan(NaN+0i)", neverc_cmplx_isnan(C(NC_NAN, 0.0)));
    check_true("isnan(0+NaN*i)", neverc_cmplx_isnan(C(0.0, NC_NAN)));
    check_true("!isnan(1+2i)", !neverc_cmplx_isnan(C(1.0, 2.0)));
    check_true("!isnan(Inf+0i)", !neverc_cmplx_isnan(C(NC_INF, 0.0)));
    check_true("!isnan(Inf+NaN*i)", !neverc_cmplx_isnan(C(NC_INF, NC_NAN)));

    check_true("isinf(Inf+0i)", neverc_cmplx_isinf(C(NC_INF, 0.0)));
    check_true("isinf(0-Inf*i)", neverc_cmplx_isinf(C(0.0, -NC_INF)));
    check_true("!isinf(1+2i)", !neverc_cmplx_isinf(C(1.0, 2.0)));
    check_true("!isinf(NaN+0i)", !neverc_cmplx_isinf(C(NC_NAN, 0.0)));
}

/* ===== Known computed values ===== */

static void test_known_values(void) {
    printf("[known values]\n");

    /* exp(1+pi*i) = -e */
    neverc_cmplx_t z = neverc_cmplx_exp(C(1.0, NEVERC_MATH_PI));
    check_cmplx("exp(1+pi*i)=-e", z, -NEVERC_MATH_E, 0.0);

    /* log(i) = i*pi/2 */
    z = neverc_cmplx_log(C(0.0, 1.0));
    check_double("log(i).re", z.re, 0.0);
    check_double("log(i).im", z.im, NEVERC_MATH_PI / 2.0);

    /* sqrt(i) = (1+i)/sqrt(2) */
    z = neverc_cmplx_sqrt(C(0.0, 1.0));
    double inv_sqrt2 = 1.0 / NEVERC_MATH_SQRT2;
    check_double("sqrt(i).re", z.re, inv_sqrt2);
    check_double("sqrt(i).im", z.im, inv_sqrt2);

    /* sqrt(3+4i) = 2+i */
    z = neverc_cmplx_sqrt(C(3.0, 4.0));
    check_double("sqrt(3+4i).re", z.re, 2.0);
    check_double("sqrt(3+4i).im", z.im, 1.0);

    /* sin(pi/6 + 0i) = 0.5 */
    check_double("sin(pi/6).re", neverc_cmplx_sin(C(NEVERC_MATH_PI / 6.0, 0.0)).re, 0.5);
}

int main(void) {
    printf("=== NeverC Complex Math Tests ===\n\n");

    test_basic();
    test_exp();
    test_sqrt();
    test_pow();
    test_trig();
    test_hyp();
    test_inv_trig();
    test_special();
    test_known_values();

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    return tests_failed > 0 ? 1 : 0;
}
