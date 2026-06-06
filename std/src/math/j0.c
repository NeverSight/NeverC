#include "neverc/math.h"

/*
 * Bessel function of the first and second kinds of order zero.
 * Ported from Go math.J0/Y0, originally from FreeBSD libm (e_j0.c).
 *
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 * Developed at SunPro, a Sun Microsystems, Inc. business.
 */

#define INV_SQRT_PI 0.56418958354775628694807945156077258584405062932899885684408

static const double
    R02 =  1.56249999999999947958e-02,
    R03 = -1.89979294238854721751e-04,
    R04 =  1.82954049532700665670e-06,
    R05 = -4.61832688532103189199e-09,
    S01 =  1.56191029464890010492e-02,
    S02 =  1.16926784663337450260e-04,
    S03 =  5.13546550207318111446e-07,
    S04 =  1.16614003333790000205e-09;

static const double
    U00 = -7.38042951086872317523e-02,
    U01 =  1.76666452509181115538e-01,
    U02 = -1.38185671945596898896e-02,
    U03 =  3.47453432093683650238e-04,
    U04 = -3.81407053724364161125e-06,
    U05 =  1.95590137035022920206e-08,
    U06 = -3.98205194132103398453e-11,
    V01 =  1.27304834834123699328e-02,
    V02 =  7.60068627350353253702e-05,
    V03 =  2.59150851840457805467e-07,
    V04 =  4.41110311332675467403e-10;

/* pzero/qzero asymptotic expansion coefficients */

static const double p0R8[6] = {
    0.00000000000000000000e+00,
    -7.03124999999900357484e-02,
    -8.08167041275349795626e+00,
    -2.57063105679704847262e+02,
    -2.48521641009428822144e+03,
    -5.25304380490729545272e+03,
};
static const double p0S8[5] = {
    1.16534364619668181717e+02,
    3.83374475364121826715e+03,
    4.05978572648472545552e+04,
    1.16752972564375915681e+05,
    4.76277284146730962675e+04,
};

static const double p0R5[6] = {
    -1.14125464691894502584e-11,
    -7.03124940873599280078e-02,
    -4.15961064470587782438e+00,
    -6.76747652265167261021e+01,
    -3.31231299649172967747e+02,
    -3.46433388365604912451e+02,
};
static const double p0S5[5] = {
    6.07539382692300335975e+01,
    1.05125230595704579173e+03,
    5.97897094333855784498e+03,
    9.62544514357774460223e+03,
    2.40605815922939109441e+03,
};

static const double p0R3[6] = {
    -2.54704601771951915620e-09,
    -7.03119616381481654654e-02,
    -2.40903221549529611423e+00,
    -2.19659774734883086467e+01,
    -5.80791704701737572236e+01,
    -3.14479470594888503854e+01,
};
static const double p0S3[5] = {
    3.58560338055209726349e+01,
    3.61513983050303863820e+02,
    1.19360783792111533330e+03,
    1.12799679856907414432e+03,
    1.73580930813335754692e+02,
};

static const double p0R2[6] = {
    -8.87534333032526411254e-08,
    -7.03030995483624743247e-02,
    -1.45073846780952986357e+00,
    -7.63569613823527770791e+00,
    -1.11931668860356747786e+01,
    -3.23364579351335335033e+00,
};
static const double p0S2[5] = {
    2.22202997532088808441e+01,
    1.36206794218215208048e+02,
    2.70470278658083486789e+02,
    1.53875394208320329881e+02,
    1.46576176948256193810e+01,
};

static double pzero(double x) {
    const double *p;
    const double *q;
    if (x >= 8) {
        p = p0R8; q = p0S8;
    } else if (x >= 4.5454) {
        p = p0R5; q = p0S5;
    } else if (x >= 2.8571) {
        p = p0R3; q = p0S3;
    } else {
        p = p0R2; q = p0S2;
    }
    double z = 1.0 / (x * x);
    double r = p[0] + z*(p[1] + z*(p[2] + z*(p[3] + z*(p[4] + z*p[5]))));
    double s = 1.0 + z*(q[0] + z*(q[1] + z*(q[2] + z*(q[3] + z*q[4]))));
    return 1.0 + r/s;
}

static const double q0R8[6] = {
    0.00000000000000000000e+00,
    7.32421874999935051953e-02,
    1.17682064682252693899e+01,
    5.57673380256401856059e+02,
    8.85919720756468632317e+03,
    3.70146267776887834771e+04,
};
static const double q0S8[6] = {
    1.63776026895689824414e+02,
    8.09834494656449805916e+03,
    1.42538291419120476348e+05,
    8.03309257119514397345e+05,
    8.40501579819060512818e+05,
    -3.43899293537866615225e+05,
};

static const double q0R5[6] = {
    1.84085963594515531381e-11,
    7.32421766612684765896e-02,
    5.83563508962056953777e+00,
    1.35111577286449829671e+02,
    1.02724376596164097464e+03,
    1.98997785864605384631e+03,
};
static const double q0S5[6] = {
    8.27766102236537761883e+01,
    2.07781416421392987104e+03,
    1.88472887785718085070e+04,
    5.67511122894947329769e+04,
    3.59767538425114471465e+04,
    -5.35434275601944773371e+03,
};

static const double q0R3[6] = {
    4.37741014089738620906e-09,
    7.32411180042911447163e-02,
    3.34423137516170720929e+00,
    4.26218440745412650017e+01,
    1.70808091340565596283e+02,
    1.66733948696651168575e+02,
};
static const double q0S3[6] = {
    4.87588729724587182091e+01,
    7.09689221056606015736e+02,
    3.70414822620111362994e+03,
    6.46042516752568917582e+03,
    2.51633368920368957333e+03,
    -1.49247451836156386662e+02,
};

static const double q0R2[6] = {
    1.50444444886983272379e-07,
    7.32234265963079278272e-02,
    1.99819174093815998816e+00,
    1.44956029347885735348e+01,
    3.16662317504781540833e+01,
    1.62527075710929267416e+01,
};
static const double q0S2[6] = {
    3.03655848355219184498e+01,
    2.69348118608049844624e+02,
    8.44783757595320139444e+02,
    8.82935845112488550512e+02,
    2.12666388511798828631e+02,
    -5.31095493882666946917e+00,
};

static double qzero(double x) {
    const double *p;
    const double *q;
    if (x >= 8) {
        p = q0R8; q = q0S8;
    } else if (x >= 4.5454) {
        p = q0R5; q = q0S5;
    } else if (x >= 2.8571) {
        p = q0R3; q = q0S3;
    } else {
        p = q0R2; q = q0S2;
    }
    double z = 1.0 / (x * x);
    double r = p[0] + z*(p[1] + z*(p[2] + z*(p[3] + z*(p[4] + z*p[5]))));
    double s = 1.0 + z*(q[0] + z*(q[1] + z*(q[2] + z*(q[3] + z*(q[4] + z*q[5])))));
    return (-0.125 + r/s) / x;
}

double neverc_math_j0(double x) {
    const double TwoM27 = 1.0 / (1 << 27);
    const double TwoM13 = 1.0 / (1 << 13);
    const double Two129 = 6.805647338418769269267492148635364229120e+38;

    if (isnan(x)) return x;
    if (isinf(x))  return 0.0;
    if (x == 0.0)  return 1.0;

    x = fabs(x);
    if (x >= 2.0) {
        double s = sin(x);
        double c = cos(x);
        double ss = s - c;
        double cc = s + c;

        if (x < NEVERC_MATH_MAX_FLOAT64 / 2.0) {
            double z = -cos(x + x);
            if (s * c < 0)
                cc = z / ss;
            else
                ss = z / cc;
        }

        double z;
        if (x > Two129)
            z = INV_SQRT_PI * cc / sqrt(x);
        else {
            double u = pzero(x);
            double v = qzero(x);
            z = INV_SQRT_PI * (u*cc - v*ss) / sqrt(x);
        }
        return z;
    }
    if (x < TwoM13) {
        if (x < TwoM27)
            return 1.0;
        return 1.0 - 0.25*x*x;
    }
    double z = x * x;
    double r = z * (R02 + z*(R03 + z*(R04 + z*R05)));
    double s = 1.0 + z*(S01 + z*(S02 + z*(S03 + z*S04)));
    if (x < 1.0)
        return 1.0 + z*(-0.25 + (r/s));
    double u = 0.5 * x;
    return (1.0+u)*(1.0-u) + z*(r/s);
}

double neverc_math_y0(double x) {
    const double TwoM27 = 1.0 / (1 << 27);
    const double Two129 = 6.805647338418769269267492148635364229120e+38;

    if (x < 0 || isnan(x)) return NAN;
    if (isinf(x) && x > 0) return 0.0;
    if (x == 0.0)           return -INFINITY;

    if (x >= 2.0) {
        double s = sin(x);
        double c = cos(x);
        double ss = s - c;
        double cc = s + c;

        if (x < NEVERC_MATH_MAX_FLOAT64 / 2.0) {
            double z = -cos(x + x);
            if (s * c < 0)
                cc = z / ss;
            else
                ss = z / cc;
        }

        double z;
        if (x > Two129)
            z = INV_SQRT_PI * ss / sqrt(x);
        else {
            double u = pzero(x);
            double v = qzero(x);
            z = INV_SQRT_PI * (u*ss + v*cc) / sqrt(x);
        }
        return z;
    }
    if (x <= TwoM27)
        return U00 + (2.0/NEVERC_MATH_PI)*log(x);

    double z = x * x;
    double u = U00 + z*(U01 + z*(U02 + z*(U03 + z*(U04 + z*(U05 + z*U06)))));
    double v = 1.0 + z*(V01 + z*(V02 + z*(V03 + z*V04)));
    return u/v + (2.0/NEVERC_MATH_PI)*neverc_math_j0(x)*log(x);
}
