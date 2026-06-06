#include "neverc/math.h"
#include "_math_internal.h"

/*
 * Bessel function of the first and second kinds of order one.
 * Ported from Go math.J1/Y1, originally from FreeBSD libm (e_j1.c).
 *
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 * Developed at SunPro, a Sun Microsystems, Inc. business.
 */

#define INV_SQRT_PI 0.56418958354775628694807945156077258584405062932899885684408

static const double
    R1_00 = -6.25000000000000000000e-02,
    R1_01 =  1.40705666955189706048e-03,
    R1_02 = -1.59955631084035597520e-05,
    R1_03 =  4.96727999609584448412e-08,
    S1_01 =  1.91537599538363460805e-02,
    S1_02 =  1.85946785588630915560e-04,
    S1_03 =  1.17718464042623683263e-06,
    S1_04 =  5.04636257076217042715e-09,
    S1_05 =  1.23542274426137913908e-11;

static const double
    Y1_U00 = -1.96057090646238940668e-01,
    Y1_U01 =  5.04438716639811282616e-02,
    Y1_U02 = -1.91256895875763547298e-03,
    Y1_U03 =  2.35252600561610495928e-05,
    Y1_U04 = -9.19099158039878874504e-08,
    Y1_V00 =  1.99167318236649903973e-02,
    Y1_V01 =  2.02552581025135171496e-04,
    Y1_V02 =  1.35608801097516229404e-06,
    Y1_V03 =  6.22741452364621501295e-09,
    Y1_V04 =  1.66559246207992079114e-11;

/* pone asymptotic expansion coefficients */

static const double p1R8[6] = {
    0.00000000000000000000e+00,
    1.17187499999988647970e-01,
    1.32394806593073575129e+01,
    4.12051854307378562225e+02,
    3.87474538913960532227e+03,
    7.91447954031891731574e+03,
};
static const double p1S8[5] = {
    1.14207370375678408436e+02,
    3.65093083420853463394e+03,
    3.69562060269033463555e+04,
    9.76027935934950801311e+04,
    3.08042720627888811578e+04,
};

static const double p1R5[6] = {
    1.31990519556243522749e-11,
    1.17187493190614097638e-01,
    6.80275127868432871736e+00,
    1.08308182990189109773e+02,
    5.17636139533199752805e+02,
    5.28715201363337541807e+02,
};
static const double p1S5[5] = {
    5.92805987221131331921e+01,
    9.91401418733614377743e+02,
    5.35326695291487976647e+03,
    7.84469031749551231769e+03,
    1.50404688810361062679e+03,
};

static const double p1R3[6] = {
    3.02503916137373618024e-09,
    1.17186865567253592491e-01,
    3.93297750033315640650e+00,
    3.51194035591636932736e+01,
    9.10550110750781271918e+01,
    4.85590685197364919645e+01,
};
static const double p1S3[5] = {
    3.47913095001251519989e+01,
    3.36762458747825746741e+02,
    1.04687139975775130551e+03,
    8.90811346398256432622e+02,
    1.03787932439639277504e+02,
};

static const double p1R2[6] = {
    1.07710830106873743082e-07,
    1.17176219462683348094e-01,
    2.36851496667608785174e+00,
    1.22426109148261232917e+01,
    1.76939711271687727390e+01,
    5.07352312588818499250e+00,
};
static const double p1S2[5] = {
    2.14364859363821409488e+01,
    1.25290227168402751090e+02,
    2.32276469057162813669e+02,
    1.17679373287147100768e+02,
    8.36463893371618283368e+00,
};

static double pone(double x) {
    const double *p;
    const double *q;
    if (x >= 8) {
        p = p1R8; q = p1S8;
    } else if (x >= 4.5454) {
        p = p1R5; q = p1S5;
    } else if (x >= 2.8571) {
        p = p1R3; q = p1S3;
    } else {
        p = p1R2; q = p1S2;
    }
    double z = 1.0 / (x * x);
    double r = p[0] + z*(p[1] + z*(p[2] + z*(p[3] + z*(p[4] + z*p[5]))));
    double s = 1.0 + z*(q[0] + z*(q[1] + z*(q[2] + z*(q[3] + z*q[4]))));
    return 1.0 + r/s;
}

/* qone asymptotic expansion coefficients */

static const double q1R8[6] = {
    0.00000000000000000000e+00,
    -1.02539062499992714161e-01,
    -1.62717534544589987888e+01,
    -7.59601722513950107896e+02,
    -1.18498066702429587167e+04,
    -4.84385124285750353010e+04,
};
static const double q1S8[6] = {
    1.61395369700722909556e+02,
    7.82538599923348465381e+03,
    1.33875336287249578163e+05,
    7.19657723683240939863e+05,
    6.66601232617776375264e+05,
    -2.94490264303834643215e+05,
};

static const double q1R5[6] = {
    -2.08979931141764104297e-11,
    -1.02539050241375426231e-01,
    -8.05644828123936029840e+00,
    -1.83669607474888380239e+02,
    -1.37319376065508163265e+03,
    -2.61244440453215656817e+03,
};
static const double q1S5[6] = {
    8.12765501384335777857e+01,
    1.99179873460485964642e+03,
    1.74684851924908907677e+04,
    4.98514270910352279316e+04,
    2.79480751638918118260e+04,
    -4.71918354795128470869e+03,
};

static const double q1R3[6] = {
    -5.07831226461766561369e-09,
    -1.02537829820837089745e-01,
    -4.61011581139473403113e+00,
    -5.78472216562783643212e+01,
    -2.28244540737631695038e+02,
    -2.19210128478909325622e+02,
};
static const double q1S3[6] = {
    4.76651550323729509273e+01,
    6.73865112676699709482e+02,
    3.38015286679526343505e+03,
    5.54772909720722782367e+03,
    1.90311919338810798763e+03,
    -1.35201191444307340817e+02,
};

static const double q1R2[6] = {
    -1.78381727510958865572e-07,
    -1.02517042607985553460e-01,
    -2.75220568278187460720e+00,
    -1.96636162643703720221e+01,
    -4.23253133372830490089e+01,
    -2.13719211703704061733e+01,
};
static const double q1S2[6] = {
    2.95333629060523854548e+01,
    2.52981549982190529136e+02,
    7.57502834868645436472e+02,
    7.39393205320467245656e+02,
    1.55949003336666123687e+02,
    -4.95949898822628210127e+00,
};

static double qone(double x) {
    const double *p;
    const double *q;
    if (x >= 8) {
        p = q1R8; q = q1S8;
    } else if (x >= 4.5454) {
        p = q1R5; q = q1S5;
    } else if (x >= 2.8571) {
        p = q1R3; q = q1S3;
    } else {
        p = q1R2; q = q1S2;
    }
    double z = 1.0 / (x * x);
    double r = p[0] + z*(p[1] + z*(p[2] + z*(p[3] + z*(p[4] + z*p[5]))));
    double s = 1.0 + z*(q[0] + z*(q[1] + z*(q[2] + z*(q[3] + z*(q[4] + z*q[5])))));
    return (0.375 + r/s) / x;
}

double neverc_math_j1(double x) {
    const double TwoM27 = 1.0 / (1 << 27);
    const double Two129 = 6.805647338418769269267492148635364229120e+38;

    if (nc_isnan(x))             return x;
    if (nc_isinf_any(x) || x == 0.0) return 0.0;

    int sign = 0;
    if (x < 0) {
        x = -x;
        sign = 1;
    }
    if (x >= 2.0) {
        double s = neverc_math_sin(x);
        double c = neverc_math_cos(x);
        double ss = -s - c;
        double cc = s - c;

        if (x < NEVERC_MATH_MAX_FLOAT64 / 2.0) {
            double z = neverc_math_cos(x + x);
            if (s * c > 0)
                cc = z / ss;
            else
                ss = z / cc;
        }

        double z;
        if (x > Two129)
            z = INV_SQRT_PI * cc / neverc_math_sqrt(x);
        else {
            double u = pone(x);
            double v = qone(x);
            z = INV_SQRT_PI * (u*cc - v*ss) / neverc_math_sqrt(x);
        }
        return sign ? -z : z;
    }
    if (x < TwoM27)
        return sign ? -0.5*x : 0.5*x;

    double z = x * x;
    double r = z * (R1_00 + z*(R1_01 + z*(R1_02 + z*R1_03)));
    double s = 1.0 + z*(S1_01 + z*(S1_02 + z*(S1_03 + z*(S1_04 + z*S1_05))));
    r *= x;
    z = 0.5*x + r/s;
    return sign ? -z : z;
}

double neverc_math_y1(double x) {
    const double TwoM54 = 1.0 / ((double)(1LL << 54));
    const double Two129 = 6.805647338418769269267492148635364229120e+38;

    if (x < 0 || nc_isnan(x))  return nc_nan();
    if (nc_isinf_any(x) && x > 0)  return 0.0;
    if (x == 0.0)            return nc_inf(-1);

    if (x >= 2.0) {
        double s = neverc_math_sin(x);
        double c = neverc_math_cos(x);
        double ss = -s - c;
        double cc = s - c;

        if (x < NEVERC_MATH_MAX_FLOAT64 / 2.0) {
            double z = neverc_math_cos(x + x);
            if (s * c > 0)
                cc = z / ss;
            else
                ss = z / cc;
        }

        double z;
        if (x > Two129)
            z = INV_SQRT_PI * ss / neverc_math_sqrt(x);
        else {
            double u = pone(x);
            double v = qone(x);
            z = INV_SQRT_PI * (u*ss + v*cc) / neverc_math_sqrt(x);
        }
        return z;
    }
    if (x <= TwoM54)
        return -(2.0/NEVERC_MATH_PI) / x;

    double z = x * x;
    double u = Y1_U00 + z*(Y1_U01 + z*(Y1_U02 + z*(Y1_U03 + z*Y1_U04)));
    double v = 1.0 + z*(Y1_V00 + z*(Y1_V01 + z*(Y1_V02 + z*(Y1_V03 + z*Y1_V04))));
    return x*(u/v) + (2.0/NEVERC_MATH_PI)*(neverc_math_j1(x)*neverc_math_log(x) - 1.0/x);
}
