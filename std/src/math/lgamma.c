#include "neverc/std/math.h"
#include "_math_internal.h"

/*
 * Log-gamma function, ported from Go math.Lgamma.
 * Originally from FreeBSD libm (e_lgamma_r.c), Sun Microsystems.
 *
 * Method:
 *   x < 0:     reflection formula via sinPi
 *   0 < x < 2: argument reduction + polynomial in three sub-regions
 *   2 <= x < 8: recurrence lgamma(1+s) = log(s) + lgamma(s)
 *   8 <= x < 2^58: Stirling approximation (x-0.5)*(log(x)-1) + series
 *   x >= 2^58: x*(log(x)-1)
 */

static const double lgA[] = {
    7.72156649015328655494e-02,  3.22467033424113591611e-01,
    6.73523010531292681824e-02,  2.05808084325167332806e-02,
    7.38555086081402883957e-03,  2.89051383673415629091e-03,
    1.19270763183362067845e-03,  5.10069792153511336608e-04,
    2.20862790713908385557e-04,  1.08011567247583939954e-04,
    2.52144565451257326939e-05,  4.48640949618915160150e-05,
};
static const double lgR[] = {
    1.0,
    1.39200533467621045958e+00,  7.21935547567138069525e-01,
    1.71933865632803078993e-01,  1.86459191715652901344e-02,
    7.77942496381893596434e-04,  7.32668430744625636189e-06,
};
static const double lgS[] = {
    -7.72156649015328655494e-02, 2.14982415960608852501e-01,
     3.25778796408930981787e-01, 1.46350472652464452805e-01,
     2.66422703033638609560e-02, 1.84028451407337715652e-03,
     3.19475326584100867617e-05,
};
static const double lgT[] = {
     4.83836122723810047042e-01, -1.47587722994593911752e-01,
     6.46249402391333854778e-02, -3.27885410759859649565e-02,
     1.79706750811820387126e-02, -1.03142241298341437450e-02,
     6.10053870246291332635e-03, -3.68452016781138256760e-03,
     2.25964780900612472250e-03, -1.40346469989232843813e-03,
     8.81081882437654011382e-04, -5.38595305356740546715e-04,
     3.15632070903625950361e-04, -3.12754168375120860518e-04,
     3.35529192635519073543e-04,
};
static const double lgU[] = {
    -7.72156649015328655494e-02, 6.32827064025093366517e-01,
     1.45492250137234768737e+00, 9.77717527963372745603e-01,
     2.28963728064692451092e-01, 1.33810918536787660377e-02,
};
static const double lgV[] = {
    1.0,
    2.45597793713041134822e+00, 2.12848976379893395361e+00,
    7.69285150456672783825e-01, 1.04222645593369134254e-01,
    3.21709242282423911810e-03,
};
static const double lgW[] = {
    4.18938533204672725052e-01,  8.33333333333329678849e-02,
    -2.77777777728775536470e-03, 7.93650558643019558500e-04,
    -5.95187557450339963135e-04, 8.36339918996282139126e-04,
    -1.63092934096575273989e-03,
};

static double sinPi(double x) {
    const double Two52 = (double)(1ULL << 52);
    const double Two53 = (double)(1ULL << 53);

    if (x < 0.25)
        return -neverc_math_sin(NEVERC_MATH_PI * x);

    double z = neverc_math_floor(x);
    int n;
    if (z != x) {
        x = neverc_math_fmod(x, 2.0);
        n = (int)(x * 4.0);
    } else {
        if (x >= Two53) {
            x = 0; n = 0;
        } else {
            if (x < Two52) z = x + Two52;
            n = (int)(1 & nc_f64_to_bits(z));
            x = (double)n;
            n <<= 2;
        }
    }

    switch (n) {
    case 0: x = neverc_math_sin(NEVERC_MATH_PI * x); break;
    case 1: case 2: x = neverc_math_cos(NEVERC_MATH_PI * (0.5 - x)); break;
    case 3: case 4: x = neverc_math_sin(NEVERC_MATH_PI * (1.0 - x)); break;
    case 5: case 6: x = -neverc_math_cos(NEVERC_MATH_PI * (x - 1.5)); break;
    default: x = neverc_math_sin(NEVERC_MATH_PI * (x - 2.0)); break;
    }
    return -x;
}

double neverc_math_lgamma_sign(double x, int *sign) {
    const double Ymin  = 1.461632144968362245;
    const double Two52 = (double)(1ULL << 52);
    const double Two58 = (double)(1ULL << 58);
    const double Tiny  = 1.0 / (double)(1ULL << 60) / (double)(1ULL << 10);
    const double Tc    = 1.46163214496836224576e+00;
    const double Tf    = -1.21486290535849611461e-01;
    const double Tt    = -3.63867699703950536541e-18;

    if (sign) *sign = 1;

    if (nc_isnan(x)) return x;
    if (nc_isinf_any(x)) return nc_inf(1);
    if (x == 0.0) return nc_inf(1);

    int neg = 0;
    if (x < 0.0) { x = -x; neg = 1; }

    if (x < Tiny) {
        if (neg && sign) *sign = -1;
        return -neverc_math_log(x);
    }

    double nadj = 0.0;
    if (neg) {
        if (x >= Two52) return nc_inf(1);
        double t = sinPi(x);
        if (t == 0.0) return nc_inf(1);
        nadj = neverc_math_log(NEVERC_MATH_PI / nc_abs(t * x));
        if (t < 0.0 && sign) *sign = -1;
    }

    double lgamma_val;

    if (x == 1.0 || x == 2.0) {
        lgamma_val = 0.0;
    } else if (x < 2.0) {
        double y;
        int i;
        if (x <= 0.9) {
            lgamma_val = -neverc_math_log(x);
            if (x >= (Ymin - 1.0 + 0.27)) {
                y = 1.0 - x; i = 0;
            } else if (x >= (Ymin - 1.0 - 0.27)) {
                y = x - (Tc - 1.0); i = 1;
            } else {
                y = x; i = 2;
            }
        } else {
            lgamma_val = 0.0;
            if (x >= (Ymin + 0.27)) {
                y = 2.0 - x; i = 0;
            } else if (x >= (Ymin - 0.27)) {
                y = x - Tc; i = 1;
            } else {
                y = x - 1.0; i = 2;
            }
        }
        switch (i) {
        case 0: {
            double z2 = y * y;
            double p1 = lgA[0]+z2*(lgA[2]+z2*(lgA[4]+z2*(lgA[6]+z2*(lgA[8]+z2*lgA[10]))));
            double p2 = z2*(lgA[1]+z2*(lgA[3]+z2*(lgA[5]+z2*(lgA[7]+z2*(lgA[9]+z2*lgA[11])))));
            double p = y*p1 + p2;
            lgamma_val += (p - 0.5*y);
            break;
        }
        case 1: {
            double z2 = y * y;
            double w = z2 * y;
            double p1 = lgT[0]+w*(lgT[3]+w*(lgT[6]+w*(lgT[9]+w*lgT[12])));
            double p2 = lgT[1]+w*(lgT[4]+w*(lgT[7]+w*(lgT[10]+w*lgT[13])));
            double p3 = lgT[2]+w*(lgT[5]+w*(lgT[8]+w*(lgT[11]+w*lgT[14])));
            double p = z2*p1 - (Tt - w*(p2+y*p3));
            lgamma_val += (Tf + p);
            break;
        }
        case 2: {
            double p1 = y*(lgU[0]+y*(lgU[1]+y*(lgU[2]+y*(lgU[3]+y*(lgU[4]+y*lgU[5])))));
            double p2 = 1.0+y*(lgV[1]+y*(lgV[2]+y*(lgV[3]+y*(lgV[4]+y*lgV[5]))));
            lgamma_val += (-0.5*y + p1/p2);
            break;
        }
        }
    } else if (x < 8.0) {
        int ii = (int)x;
        double y = x - (double)ii;
        double p = y*(lgS[0]+y*(lgS[1]+y*(lgS[2]+y*(lgS[3]+y*(lgS[4]+y*(lgS[5]+y*lgS[6]))))));
        double q = 1.0+y*(lgR[1]+y*(lgR[2]+y*(lgR[3]+y*(lgR[4]+y*(lgR[5]+y*lgR[6])))));
        lgamma_val = 0.5*y + p/q;
        double z = 1.0;
        switch (ii) {
        case 7: z *= (y + 6.0); /* fall through */
        case 6: z *= (y + 5.0); /* fall through */
        case 5: z *= (y + 4.0); /* fall through */
        case 4: z *= (y + 3.0); /* fall through */
        case 3: z *= (y + 2.0);
            lgamma_val += neverc_math_log(z);
        }
    } else if (x < Two58) {
        double t = neverc_math_log(x);
        double z = 1.0 / x;
        double y = z * z;
        double w = lgW[0]+z*(lgW[1]+y*(lgW[2]+y*(lgW[3]+y*(lgW[4]+y*(lgW[5]+y*lgW[6])))));
        lgamma_val = (x - 0.5)*(t - 1.0) + w;
    } else {
        lgamma_val = x * (neverc_math_log(x) - 1.0);
    }

    if (neg) lgamma_val = nadj - lgamma_val;
    return lgamma_val;
}

double neverc_math_lgamma(double x) {
    return neverc_math_lgamma_sign(x, (void *)0);
}
