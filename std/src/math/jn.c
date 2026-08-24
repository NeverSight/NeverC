#include "neverc/std/math.h"
#include "_math_internal.h"

/*
 * Bessel function of the first and second kinds of order n.
 * Ported from Go math.Jn/Yn, originally from FreeBSD libm (e_jn.c).
 *
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 * Developed at SunPro, a Sun Microsystems, Inc. business.
 */

#define INV_SQRT_PI 0.56418958354775628694807945156077258584405062932899885684408

/*
 * Large-order support.
 *
 * The fdlibm recurrence below is accurate for ordinary integer orders, but it
 * is O(n) and cannot represent |INT_MIN| as an int.  For n >= 500 use the
 * large-order split from Netlib Cephes 2.8 (jv.c, June 2000):
 *
 *   x/n^2 > 0.3                         Hankel expansion (AMS 55 9.2.5)
 *   |(x-n)/cbrt(n)| <= 0.7              transition expansion (9.3.23)
 *   otherwise                           uniform Olver expansion (9.3.35)
 *
 * Cephes documents about 12 decimal digits for the transitional expansions
 * once n > 500.  The same coefficient sums produce both kinds: J uses
 * Ai/Ai', while Y uses -Bi/-Bi', as in GSL 2.8 bessel_olver.c (9.3.36).
 * The code is rewritten to evaluate J and Y together and uses only NeverC's
 * self-hosted math primitives.
 */

#define NC_BESSEL_LARGE_ORDER 500
#define NC_BESSEL_EPS 1.11022302462515654042e-16
#define NC_BESSEL_LOG_MAX 7.09782712893383973096e2
#define NC_BESSEL_LOG_ZERO (-7.45133219101941108420e2)

static double nc_bessel_polevl(double x, const double *coef, int degree) {
    double result = coef[0];
    for (int i = 1; i <= degree; i++) result = result * x + coef[i];
    return result;
}

/* Evaluate x^degree + coef[0] x^(degree-1) + ... + coef[degree-1]. */
static double nc_bessel_p1evl(double x, const double *coef, int degree) {
    double result = x + coef[0];
    for (int i = 1; i < degree; i++) result = result * x + coef[i];
    return result;
}

/* Restore value*exp(log_scale) without overflowing the Airy function before
 * the Bessel normalization has had a chance to shrink it. */
static double nc_bessel_restore_exp(double value, double log_scale) {
    if (value == 0.0 || log_scale == 0.0 || nc_isnan(value) ||
        nc_isinf_any(value))
        return value;
    double log_value = neverc_math_log(nc_abs(value)) + log_scale;
    if (log_value > NC_BESSEL_LOG_MAX)
        return nc_copysign(nc_inf(1), value);
    if (log_value < NC_BESSEL_LOG_ZERO)
        return nc_copysign(0.0, value);
    return nc_copysign(neverc_math_exp(log_value), value);
}

/* Cephes 2.8 airy.c IEEE rational approximants.  For positive arguments the
 * original source multiplies/divides by exp(zeta), zeta=2*x^(3/2)/3.  Keep
 * that exponential factored out here: the returned Ai/Ai' and Bi/Bi'
 * mantissas become the true values only after multiplication by their
 * corresponding log scale.  This is the scaled-Airy form of the Olver
 * expansion and lets the outer Bessel normalization participate before the
 * final IEEE overflow/underflow decision. */
static const double nc_airy_an[8] = {
    3.46538101525629032477e-1, 1.20075952739645805542e1,
    7.62796053615234516538e1, 1.68089224934630576269e2,
    1.59756391350164413639e2, 7.05360906840444183113e1,
    1.40264691163389668864e1, 9.99999999999999995305e-1
};
static const double nc_airy_ad[8] = {
    5.67594532638770212846e-1, 1.47562562584847203173e1,
    8.45138970141474626562e1, 1.77318088145400459522e2,
    1.64234692871529701831e2, 7.14778400825575695274e1,
    1.40959135607834029598e1, 1.00000000000000000470e0
};
static const double nc_airy_apn[8] = {
    6.13759184814035759225e-1, 1.47454670787755323881e1,
    8.20584123476060982430e1, 1.71184781360976385540e2,
    1.59317847137141783523e2, 6.99778599330103016170e1,
    1.39470856980481566958e1, 1.00000000000000000550e0
};
static const double nc_airy_apd[8] = {
    3.34203677749736953049e-1, 1.11810297306158156705e1,
    7.11727352147859965283e1, 1.58778084372838313640e2,
    1.53206427475809220834e2, 6.86752304592780337944e1,
    1.38498634758259442477e1, 9.99999999999999994502e-1
};
static const double nc_airy_bn16[5] = {
   -2.53240795869364152689e-1, 5.75285167332467384228e-1,
   -3.29907036873225371650e-1, 6.44404068948199951727e-2,
   -3.82519546641336734394e-3
};
static const double nc_airy_bd16[5] = {
   -7.15685095054035237902e0, 1.06039580715664694291e1,
   -5.23246636471251500874e0, 9.57395864378383833152e-1,
   -5.50828147163549611107e-2
};
static const double nc_airy_bppn[5] = {
    4.65461162774651610328e-1, -1.08992173800493920734e0,
    6.38800117371827987759e-1, -1.26844349553102907034e-1,
    7.62487844342109852105e-3
};
static const double nc_airy_bppd[5] = {
   -8.70622787633159124240e0, 1.38993162704553213172e1,
   -7.14116144616431159572e0, 1.34008595960680518666e0,
   -7.84273211323341930448e-2
};
static const double nc_airy_afn[9] = {
   -1.31696323418331795333e-1, -6.26456544431912369773e-1,
   -6.93158036036933542233e-1, -2.79779981545119124951e-1,
   -4.91900132609500318020e-2, -4.06265923594885404393e-3,
   -1.59276496239262096340e-4, -2.77649108155232920844e-6,
   -1.67787698489114633780e-8
};
static const double nc_airy_afd[9] = {
    1.33560420706553243746e1, 3.26825032795224613948e1,
    2.67367040941499554804e1, 9.18707402907259625840e0,
    1.47529146771666414581e0, 1.15687173795188044134e-1,
    4.40291641615211203805e-3, 7.54720348287414296618e-5,
    4.51850092970580378464e-7
};
static const double nc_airy_agn[11] = {
    1.97339932091685679179e-2, 3.91103029615688277255e-1,
    1.06579897599595591108e0, 9.39169229816650230044e-1,
    3.51465656105547619242e-1, 6.33888919628925490927e-2,
    5.85804113048388458567e-3, 2.82851600836737019778e-4,
    6.98793669997260967291e-6, 8.11789239554389293311e-8,
    3.41551784765923618484e-10
};
static const double nc_airy_agd[10] = {
    9.30892908077441974853e0, 1.98352928718312140417e1,
    1.55646628932864612953e1, 5.47686069422975497931e0,
    9.54293611618961883998e-1, 8.64580826352392193095e-2,
    4.12656523824222607191e-3, 1.01259085116509135510e-4,
    1.17166733214413521882e-6, 4.91834570062930015649e-9
};
static const double nc_airy_apfn[9] = {
    1.85365624022535566142e-1, 8.86712188052584095637e-1,
    9.87391981747398547272e-1, 4.01241082318003734092e-1,
    7.10304926289631174579e-2, 5.90618657995661810071e-3,
    2.33051409401776799569e-4, 4.08718778289035454598e-6,
    2.48379932900442457853e-8
};
static const double nc_airy_apfd[9] = {
    1.47345854687502542552e1, 3.75423933435489594466e1,
    3.14657751203046424330e1, 1.09969125207298778536e1,
    1.78885054766999417817e0, 1.41733275753662636873e-1,
    5.44066067017226003627e-3, 9.39421290654511171663e-5,
    5.65978713036027009243e-7
};
static const double nc_airy_apgn[11] = {
   -3.55615429033082288335e-2, -6.37311518129435504426e-1,
   -1.70856738884312371053e0, -1.50221872117316635393e0,
   -5.63606665822102676611e-1, -1.02101031120216891789e-1,
   -9.48396695961445269093e-3, -4.60325307486780994357e-4,
   -1.14300836484517375919e-5, -1.33415518685547420648e-7,
   -5.63803833958893494476e-10
};
static const double nc_airy_apgd[10] = {
    9.85865801696130355144e0, 2.16401867356585941885e1,
    1.73130776389749389525e1, 6.17872175280828766327e0,
    1.08848694396321495475e0, 9.95005543440888479402e-2,
    4.78468199683886610842e-3, 1.18159633322838625562e-4,
    1.37480673554219441465e-6, 5.79912514929147598821e-9
};

static void nc_bessel_airy(double x, int have_oscillatory_phase,
                           double oscillatory_sin, double oscillatory_cos,
                           double *ai, double *aip,
                           double *bi, double *bip,
                           double *ai_log_scale, double *bi_log_scale) {
    const double c1 = 0.35502805388781723926;
    const double c2 = 0.258819403792806798405;
    const double sqrt3 = 1.732050807568877293527;
    int done = 0;
    *ai_log_scale = 0.0;
    *bi_log_scale = 0.0;

    if (x < -2.09) {
        double t = neverc_math_sqrt(-x);
        double zeta = -2.0 * x * t / 3.0;
        t = neverc_math_sqrt(t);
        double k = INV_SQRT_PI / t;
        double z = 1.0 / zeta;
        double zz = z * z;
        double uf = 1.0 + zz * nc_bessel_polevl(zz, nc_airy_afn, 8) /
                                nc_bessel_p1evl(zz, nc_airy_afd, 9);
        double ug = z * nc_bessel_polevl(zz, nc_airy_agn, 10) /
                            nc_bessel_p1evl(zz, nc_airy_agd, 10);
        double s, c;
        if (have_oscillatory_phase) {
            /* The caller computed theta before n^(2/3)*zeta was rounded.
             * Keep x only for the Airy amplitude/rational corrections. */
            s = oscillatory_sin;
            c = oscillatory_cos;
        } else {
            neverc_math_sincos(zeta + 0.25 * NEVERC_MATH_PI, &s, &c);
        }
        *ai = k * (s * uf - c * ug);
        *bi = k * (c * uf + s * ug);

        uf = 1.0 + zz * nc_bessel_polevl(zz, nc_airy_apfn, 8) /
                          nc_bessel_p1evl(zz, nc_airy_apfd, 9);
        ug = z * nc_bessel_polevl(zz, nc_airy_apgn, 10) /
                     nc_bessel_p1evl(zz, nc_airy_apgd, 10);
        k = INV_SQRT_PI * t;
        *aip = -k * (c * uf + s * ug);
        *bip =  k * (s * uf - c * ug);
        return;
    }

    if (x >= 2.09) {
        double t = neverc_math_sqrt(x);
        double zeta = 2.0 * x * t / 3.0;
        t = neverc_math_sqrt(t);
        double z = 1.0 / zeta;
        double f = nc_bessel_polevl(z, nc_airy_an, 7) /
                   nc_bessel_polevl(z, nc_airy_ad, 7);
        *ai = INV_SQRT_PI * f / (2.0 * t);
        f = nc_bessel_polevl(z, nc_airy_apn, 7) /
            nc_bessel_polevl(z, nc_airy_apd, 7);
        *aip = -0.5 * INV_SQRT_PI * t * f;
        *ai_log_scale = -zeta;
        done = 5; /* Ai and Ai' are complete; compute Bi by series if needed. */

        if (x > 8.3203353) {
            f = z * nc_bessel_polevl(z, nc_airy_bn16, 4) /
                    nc_bessel_p1evl(z, nc_airy_bd16, 5);
            *bi = INV_SQRT_PI * (1.0 + f) / t;
            f = z * nc_bessel_polevl(z, nc_airy_bppn, 4) /
                    nc_bessel_p1evl(z, nc_airy_bppd, 5);
            *bip = INV_SQRT_PI * t * (1.0 + f);
            *bi_log_scale = zeta;
            return;
        }
    }

    double f = 1.0;
    double g = x;
    double uf = 1.0;
    double ug = x;
    double k = 1.0;
    double z = x * x * x;
    for (int iter = 0; iter < 100; iter++) {
        uf *= z;
        k += 1.0;
        uf /= k;
        ug *= z;
        k += 1.0;
        ug /= k;
        uf /= k;
        f += uf;
        k += 1.0;
        ug /= k;
        g += ug;
        if (nc_abs(uf / f) <= NC_BESSEL_EPS) break;
    }
    uf = c1 * f;
    ug = c2 * g;
    if ((done & 1) == 0) *ai = uf - ug;
    if ((done & 2) == 0) *bi = sqrt3 * (uf + ug);

    k = 4.0;
    uf = x * x / 2.0;
    ug = z / 3.0;
    f = uf;
    g = 1.0 + ug;
    uf /= 3.0;
    for (int iter = 0; iter < 100; iter++) {
        uf *= z;
        ug /= k;
        k += 1.0;
        ug *= z;
        uf /= k;
        f += uf;
        k += 1.0;
        ug /= k;
        uf /= k;
        g += ug;
        k += 1.0;
        if (nc_abs(ug / g) <= NC_BESSEL_EPS) break;
    }
    uf = c1 * f;
    ug = c2 * g;
    if ((done & 4) == 0) *aip = uf - ug;
    if ((done & 8) == 0) *bip = sqrt3 * (uf + ug);
}

/* Debye polynomials and coefficient sums from Cephes 2.8 jv.c. */
static const double nc_bessel_lambda[11] = {
    1.0, 1.041666666666666666666667e-1,
    8.355034722222222222222222e-2, 1.282265745563271604938272e-1,
    2.918490264641404642489712e-1, 8.816272674437576524187671e-1,
    3.321408281862767544702647e0, 1.499576298686255465867237e1,
    7.892301301158651813848139e1, 4.744515388682643231611949e2,
    3.207490090890661934704328e3
};
static const double nc_bessel_mu[11] = {
    1.0, -1.458333333333333333333333e-1,
   -9.874131944444444444444444e-2, -1.433120539158950617283951e-1,
   -3.172272026784135480967078e-1, -9.424291479571202491373028e-1,
   -3.511203040826354261542798e0, -1.572726362036804512982712e1,
   -8.228143909718594444224656e1, -4.923553705236705240352022e2,
   -3.316218568547972508762102e3
};
static const double nc_bessel_p1[2] = {
   -2.083333333333333333333333e-1, 1.250000000000000000000000e-1
};
static const double nc_bessel_p2[3] = {
    3.342013888888888888888889e-1, -4.010416666666666666666667e-1,
    7.031250000000000000000000e-2
};
static const double nc_bessel_p3[4] = {
   -1.025812596450617283950617e0, 1.846462673611111111111111e0,
   -8.912109375000000000000000e-1, 7.324218750000000000000000e-2
};
static const double nc_bessel_p4[5] = {
    4.669584423426247427983539e0, -1.120700261622299382716049e1,
    8.789123535156250000000000e0, -2.364086914062500000000000e0,
    1.121520996093750000000000e-1
};
static const double nc_bessel_p5[6] = {
   -2.8212072558200244877e1, 8.4636217674600734632e1,
   -9.1818241543240017361e1, 4.2534998745388454861e1,
   -7.3687943594796316964e0, 2.27108001708984375e-1
};
static const double nc_bessel_p6[7] = {
    2.1257013003921712286e2, -7.6525246814118164230e2,
    1.0599904525279998779e3, -6.9957962737613254123e2,
    2.1819051174421159048e2, -2.6491430486951555525e1,
    5.7250142097473144531e-1
};
static const double nc_bessel_p7[8] = {
   -1.9194576623184069963e3, 8.0617221817373093845e3,
   -1.3586550006434137439e4, 1.1655393336864533248e4,
   -5.3056469786134031084e3, 1.2009029132163524628e3,
   -1.0809091978839465550e2, 1.7277275025844573975e0
};

static const double nc_bessel_pf2[2] = {
   -9.0000000000000000000e-2, 8.5714285714285714286e-2
};
static const double nc_bessel_pf3[3] = {
    1.3671428571428571429e-1, -5.4920634920634920635e-2,
   -4.4444444444444444444e-3
};
static const double nc_bessel_pf4[4] = {
    1.3500000000000000000e-3, -1.6036054421768707483e-1,
    4.2590187590187590188e-2, 2.7330447330447330447e-3
};
static const double nc_bessel_pg1[2] = {
   -2.4285714285714285714e-1, 1.4285714285714285714e-2
};
static const double nc_bessel_pg2[3] = {
   -9.0000000000000000000e-3, 1.9396825396825396825e-1,
   -1.1746031746031746032e-2
};
static const double nc_bessel_pg3[3] = {
    1.9607142857142857143e-2, -1.5983694083694083694e-1,
    6.3838383838383838384e-3
};

static void nc_bessel_transition(double n, double x,
                                  double *j_value, double *y_value) {
    const double cbrt2 = 1.25992104989487316476721060728;
    const double cbrt4 = 1.58740105196819947475170563927;
    double cbrtn = neverc_math_cbrt(n);
    double z = (x - n) / cbrtn;
    double ai, aip, bi, bip, ai_scale, bi_scale;
    nc_bessel_airy(-cbrt2 * z, 0, 0.0, 0.0, &ai, &aip, &bi, &bip,
                   &ai_scale, &bi_scale);

    double zz = z * z;
    double z3 = zz * z;
    double f[5];
    double g[4];
    f[0] = 1.0;
    f[1] = -z / 5.0;
    f[2] = nc_bessel_polevl(z3, nc_bessel_pf2, 1) * zz;
    f[3] = nc_bessel_polevl(z3, nc_bessel_pf3, 2);
    f[4] = nc_bessel_polevl(z3, nc_bessel_pf4, 3) * z;
    g[0] = 0.3 * zz;
    g[1] = nc_bessel_polevl(z3, nc_bessel_pg1, 1);
    g[2] = nc_bessel_polevl(z3, nc_bessel_pg2, 2) * z;
    g[3] = nc_bessel_polevl(z3, nc_bessel_pg3, 2) * zz;

    double pp = 0.0;
    double qq = 0.0;
    double nk = 1.0;
    double n23 = neverc_math_cbrt(n * n);
    for (int k = 0; k <= 4; k++) {
        pp += f[k] * nk;
        if (k != 4) qq += g[k] * nk;
        nk /= n23;
    }

    double j_scaled = cbrt2 * ai * pp / cbrtn + cbrt4 * aip * qq / n;
    double y_scaled = -(cbrt2 * bi * pp / cbrtn + cbrt4 * bip * qq / n);
    *j_value = nc_bessel_restore_exp(j_scaled, ai_scale);
    *y_value = nc_bessel_restore_exp(y_scaled, bi_scale);
}

static void nc_bessel_uniform(double n, int nmod4, double x,
                               double *j_value, double *y_value) {
    double cbrtn = neverc_math_cbrt(n);
    double turn = (x - n) / cbrtn;
    if (nc_abs(turn) <= 0.7) {
        nc_bessel_transition(n, x, j_value, y_value);
        return;
    }

    double z = x / n;
    if (z == 0.0) {
        *j_value = 0.0;
        *y_value = nc_inf(-1);
        return;
    }

    double a = 1.0 - z;
    if (z > 0.98 && z < 1.02) {
        /* In the turning-point neighborhood n-x is exact by Sterbenz's
         * lemma.  Forming 1-round(x/n) first would discard that information,
         * which is visible for int-scale offsets at orders near 2^31. */
        a = (n - x) / n;
        z = 1.0 - a;
    }
    double zz = a * (1.0 + z);
    double sz;
    double t;
    double zeta;
    double norm;
    int region_sign;
    int have_airy_phase = 0;
    double airy_phase_sin = 0.0;
    double airy_phase_cos = 0.0;
    if (nc_abs(a) < 0.02) {
        /* GSL 2.8 bessel_olver.c: the direct log/acos expressions lose most
         * of zeta when z is close to one.  This eight-term series supplies
         * both signed zeta and the normalization without subtracting peers. */
        const double c0 = 1.25992104989487316476721060728;
        const double c1 = 0.37797631496846194943016318218;
        const double c2 = 0.230385563409348235843147082474;
        const double c3 = 0.165909603649648694839821892031;
        const double c4 = 0.12931387086451008907;
        const double c5 = 0.10568046188858133991;
        const double c6 = 0.08916997952268186978;
        const double c7 = 0.07700014900618802456;
        double pre = c0 + a * (c1 + a * (c2 + a * (c3 + a *
                     (c4 + a * (c5 + a * (c6 + a * c7))))));
        zeta = a * pre;
        double abs_zeta = nc_abs(zeta);
        t = abs_zeta * neverc_math_sqrt(abs_zeta);
        sz = neverc_math_sqrt(nc_abs(zz));
        norm = neverc_math_sqrt(2.0 *
               neverc_math_sqrt(pre / (1.0 + z)));
        region_sign = a > 0.0 ? 1 : -1;
        if (a < 0.0) {
            /* For z>1 the negative-Airy phase is theta=2*n*t/3+pi/4.
             * t came from the cancellation-free GSL series above, so reduce
             * it before the separately rounded Airy argument can lose bits. */
            double phase0 = 2.0 * n * t / 3.0;
            double s, c;
            neverc_math_sincos(phase0, &s, &c);
            airy_phase_sin = (s + c) / NEVERC_MATH_SQRT2;
            airy_phase_cos = (c - s) / NEVERC_MATH_SQRT2;
            have_airy_phase = 1;
        }
    } else if (zz > 0.0) {
        sz = neverc_math_sqrt(zz);
        t = 1.5 * (neverc_math_log((1.0 + sz) / z) - sz);
        zeta = neverc_math_cbrt(t * t);
        norm = neverc_math_sqrt(neverc_math_sqrt(4.0 * zeta / zz));
        region_sign = 1;
    } else {
        sz = neverc_math_sqrt(-zz);
        t = 1.5 * (sz - neverc_math_acos(1.0 / z));
        zeta = -neverc_math_cbrt(t * t);
        norm = neverc_math_sqrt(neverc_math_sqrt(4.0 * zeta / zz));
        region_sign = -1;

        /* Algebraically,
         *   2*n*t/3 = x - n*pi/2
         *             + n*(asin(r) - r/(1+sqrt(1-r^2))), r=n/x.
         * Reducing rounded n^(2/3)*zeta would magnify its ulp by a 3/2
         * power and destroy this phase.  Reduce x and the small correction
         * independently, and apply the integer-order quadrant exactly. */
        double r = n / x;
        double root = neverc_math_sqrt((1.0 - r) * (1.0 + r));
        double delta = n * (neverc_math_asin(r) - r / (1.0 + root));
        double sx, cx;
        neverc_math_sincos(x, &sx, &cx);
        double base_sin, base_cos;
        switch (nmod4) {
        case 0: base_sin =  sx; base_cos =  cx; break;
        case 1: base_sin = -cx; base_cos =  sx; break;
        case 2: base_sin = -sx; base_cos = -cx; break;
        case 3: base_sin =  cx; base_cos = -sx; break;
        default: base_sin = 0.0; base_cos = 0.0; break;
        }
        double sd, cd;
        neverc_math_sincos(delta, &sd, &cd);
        double phase0_sin = base_sin * cd + base_cos * sd;
        double phase0_cos = base_cos * cd - base_sin * sd;
        airy_phase_sin = (phase0_sin + phase0_cos) / NEVERC_MATH_SQRT2;
        airy_phase_cos = (phase0_cos - phase0_sin) / NEVERC_MATH_SQRT2;
        have_airy_phase = 1;
    }

    double n23 = neverc_math_cbrt(n * n);
    double ai, aip, bi, bip, ai_scale, bi_scale;
    double airy_arg = n23 * zeta;
    if (nc_isinf_any(airy_arg)) {
        *j_value = 0.0;
        *y_value = nc_inf(-1);
        return;
    }
    nc_bessel_airy(airy_arg, have_airy_phase,
                   airy_phase_sin, airy_phase_cos, &ai, &aip, &bi, &bip,
                   &ai_scale, &bi_scale);

    double z32i = nc_abs(1.0 / t);
    double sqz = neverc_math_cbrt(t);
    double zzi = 1.0 / zz;
    double u[8];
    u[0] = 1.0;
    u[1] = nc_bessel_polevl(zzi, nc_bessel_p1, 1) / sz;
    u[2] = nc_bessel_polevl(zzi, nc_bessel_p2, 2) / zz;
    u[3] = nc_bessel_polevl(zzi, nc_bessel_p3, 3) / (sz * zz);
    double zz2 = zz * zz;
    u[4] = nc_bessel_polevl(zzi, nc_bessel_p4, 4) / zz2;
    u[5] = nc_bessel_polevl(zzi, nc_bessel_p5, 5) / (zz2 * sz);
    double zz3 = zz2 * zz;
    u[6] = nc_bessel_polevl(zzi, nc_bessel_p6, 6) / zz3;
    u[7] = nc_bessel_polevl(zzi, nc_bessel_p7, 7) / (zz3 * sz);

    double pp = 0.0;
    double qq = 0.0;
    double np = 1.0;
    double previous_a = nc_inf(1);
    double previous_b = nc_inf(1);
    int sum_a = 1;
    int sum_b = 1;
    for (int k = 0; k <= 3; k++) {
        int tk = 2 * k;
        int tkp1 = tk + 1;
        double zp = 1.0;
        double ak = 0.0;
        double bk = 0.0;
        for (int s = 0; s <= tk; s++) {
            if (sum_a) {
                int sign = ((s & 3) > 1) ? region_sign : 1;
                ak += (double)sign * nc_bessel_mu[s] * zp * u[tk - s];
            }
            if (sum_b) {
                int m = tkp1 - s;
                int sign = (((m + 1) & 3) > 1) ? region_sign : 1;
                bk += (double)sign * nc_bessel_lambda[s] * zp * u[m];
            }
            zp *= z32i;
        }

        if (sum_a) {
            ak *= np;
            double magnitude = nc_abs(ak);
            if (magnitude < previous_a) {
                previous_a = magnitude;
                pp += ak;
            } else {
                sum_a = 0;
            }
        }
        if (sum_b) {
            bk += nc_bessel_lambda[tkp1] * zp;
            bk *= -np / sqz;
            double magnitude = nc_abs(bk);
            if (magnitude < previous_b) {
                previous_b = magnitude;
                qq += bk;
            } else {
                sum_b = 0;
            }
        }
        if (np < NC_BESSEL_EPS) break;
        np /= n * n;
    }

    double derivative_scale = n23 * n;
    double j_scaled = norm * (ai * pp / cbrtn + aip * qq / derivative_scale);
    double y_scaled = -norm * (bi * pp / cbrtn + bip * qq / derivative_scale);
    *j_value = nc_bessel_restore_exp(j_scaled, ai_scale);
    *y_value = nc_bessel_restore_exp(y_scaled, bi_scale);
}

static void nc_bessel_hankel(double n, int nmod4, double x,
                              double *j_value, double *y_value) {
    double m = 4.0 * n * n;
    double z = 8.0 * x;
    double k = 1.0;
    double order = 1.0;
    double p = 1.0;
    double term = (m - 1.0) / z;
    double q = term;
    double sign = 1.0;
    double best = 1.0;
    double pp = p;
    double qq = q;
    int found = 0;

    for (int iter = 0; iter < 1000; iter++) {
        k += 2.0;
        order += 1.0;
        sign = -sign;
        term *= (m - k * k) / (order * z);
        p += sign * term;
        k += 2.0;
        order += 1.0;
        term *= (m - k * k) / (order * z);
        q += sign * term;
        double relative = nc_abs(term / p);
        if (relative < best) {
            best = relative;
            pp = p;
            qq = q;
            found = 1;
        } else if (found) {
            break;
        }
        if (relative <= NC_BESSEL_EPS) break;
    }

    /* Never form x-(n/2+1/4)pi: at large x both offsets disappear in the
     * rounding of x.  Reduce x once, form x-pi/4 algebraically, then rotate
     * by the exact integer quadrant n mod 4. */
    double sx, cx;
    neverc_math_sincos(x, &sx, &cx);
    double cos_base = (cx + sx) / NEVERC_MATH_SQRT2;
    double sin_base = (sx - cx) / NEVERC_MATH_SQRT2;
    double s, c;
    switch (nmod4) {
    case 0: c =  cos_base; s =  sin_base; break;
    case 1: c =  sin_base; s = -cos_base; break;
    case 2: c = -cos_base; s = -sin_base; break;
    case 3: c = -sin_base; s =  cos_base; break;
    default: c = 0.0; s = 0.0; break;
    }
    double scale = neverc_math_sqrt(2.0 / (NEVERC_MATH_PI * x));
    *j_value = scale * (pp * c - qq * s);
    *y_value = scale * (pp * s + qq * c);
}

static void nc_bessel_large_order(int64_t order, double x,
                                   double *j_value, double *y_value) {
    double n = (double)order;
    if ((x / n) / n > 0.3) {
        nc_bessel_hankel(n, (int)(order & 3), x, j_value, y_value);
    } else {
        nc_bessel_uniform(n, (int)(order & 3), x, j_value, y_value);
    }
}

double neverc_math_jn(int n, double x) {
    const double TwoM29 = 1.0 / (1 << 29);
    const double Two302 = 8.148143905337944345073782753637512644205e+90;
    int64_t N;

    if (nc_isnan(x)) return x;

    if (n < 0) {
        /* Hold |INT_MIN| in 64-bit; its even parity needs no sign flip. */
        N = (n == NEVERC_MATH_MIN_INT) ? (1LL << 31) : -(int64_t)n;
        x = -x;
    } else {
        N = n;
    }
    if (N == 0) return neverc_math_j0(x);
    /* After reflecting negative n (Jn(-n,x)=(-1)^n Jn(n,-x)), odd order is
     * odd in x: Jn(±0)=±0 and Jn(±Inf)=±0. Even order stays +0. */
    if (nc_isinf_any(x) || x == 0.0)
        return (N & 1) ? nc_copysign(0.0, x) : 0.0;
    if (N == 1) return neverc_math_j1(x);

    int sign = 0;
    if (x < 0) {
        x = -x;
        if (N & 1) sign = 1;
    }

    double b;
    if (x >= Two302) {
        /* Here x >> n^2 for every int order. Reduce the phase with N mod 4
         * instead of subtracting n*pi/2 from a 302-bit-scale argument. */
        double s = neverc_math_sin(x);
        double c = neverc_math_cos(x);
        double temp;
        switch ((int)(N & 3)) {
        case 0: temp =  c + s; break;
        case 1: temp = -c + s; break;
        case 2: temp = -c - s; break;
        case 3: temp =  c - s; break;
        default: temp = 0; break;
        }
        b = INV_SQRT_PI * temp / neverc_math_sqrt(x);
    } else if (N >= NC_BESSEL_LARGE_ORDER) {
        double unused_y;
        nc_bessel_large_order(N, x, &b, &unused_y);
    } else if ((double)N <= x) {
        n = (int)N;
        b = neverc_math_j1(x);
        double a = neverc_math_j0(x);
        for (int i = 1; i < n; i++) {
            double tmp = b;
            b = b * ((2.0 * (double)i) / x) - a;
            a = tmp;
        }
    } else {
        n = (int)N;
        if (x < TwoM29) {
            if (n > 33) {
                b = 0;
            } else {
                double temp = x * 0.5;
                b = temp;
                double a = 1.0;
                for (int i = 2; i <= n; i++) {
                    a *= (double)i;
                    b *= temp;
                }
                b /= a;
            }
        } else {
            double w = (2.0 * (double)n) / x;
            double h = 2.0 / x;
            double q0 = w;
            double z = w + h;
            double q1 = w * z - 1.0;
            int k = 1;
            while (q1 < 1e9) {
                k++;
                z += h;
                double tmp = q1;
                q1 = z * q1 - q0;
                q0 = tmp;
            }
            int64_t m = (int64_t)n * 2;
            double t = 0.0;
            for (int64_t i = ((int64_t)n + k) * 2; i >= m; i -= 2)
                t = 1.0 / ((double)i / x - t);

            double a = t;
            b = 1.0;

            double tmp = (double)n;
            double v = 2.0 / x;
            tmp = tmp * neverc_math_log(nc_abs(v * tmp));
            if (tmp < 7.09782712893383973096e+02) {
                for (int i = n - 1; i > 0; i--) {
                    double di = 2.0 * (double)i;
                    double tmpa = b;
                    b = b * di / x - a;
                    a = tmpa;
                }
            } else {
                for (int i = n - 1; i > 0; i--) {
                    double di = 2.0 * (double)i;
                    double tmpa = b;
                    b = b * di / x - a;
                    a = tmpa;
                    if (b > 1e100) {
                        a /= b;
                        t /= b;
                        b = 1.0;
                    }
                }
            }
            b = t * neverc_math_j0(x) / b;
        }
    }
    return sign ? -b : b;
}

double neverc_math_yn(int n, double x) {
    const double Two302 = 8.148143905337944345073782753637512644205e+90;

    if (x < 0 || nc_isnan(x)) return nc_nan();
    if (nc_isinf_any(x) && x > 0) return 0.0;

    if (n == 0) return neverc_math_y0(x);
    if (x == 0.0) {
        if (n < 0 && (n & 1) == 1) return nc_inf(1);
        return nc_inf(-1);
    }

    int sign = 0;
    int64_t N;
    if (n < 0) {
        N = (n == NEVERC_MATH_MIN_INT) ? (1LL << 31) : -(int64_t)n;
        if (N & 1) sign = 1;
    } else {
        N = n;
    }
    if (N == 1) {
        double r = neverc_math_y1(x);
        return sign ? -r : r;
    }

    double b;
    if (x >= Two302) {
        double s = neverc_math_sin(x);
        double c = neverc_math_cos(x);
        double temp;
        switch ((int)(N & 3)) {
        case 0: temp =  s - c; break;
        case 1: temp = -s - c; break;
        case 2: temp = -s + c; break;
        case 3: temp =  s + c; break;
        default: temp = 0; break;
        }
        b = INV_SQRT_PI * temp / neverc_math_sqrt(x);
    } else if (N >= NC_BESSEL_LARGE_ORDER) {
        double unused_j;
        nc_bessel_large_order(N, x, &unused_j, &b);
    } else {
        n = (int)N;
        double a = neverc_math_y0(x);
        b = neverc_math_y1(x);
        for (int i = 1; i < n && !nc_isinf_any(b); i++) {
            double tmp = b;
            b = ((2.0 * (double)i) / x) * b - a;
            a = tmp;
        }
    }
    return sign ? -b : b;
}
