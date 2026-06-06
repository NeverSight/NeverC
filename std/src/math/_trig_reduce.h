#ifndef _NEVERC_TRIG_REDUCE_H
#define _NEVERC_TRIG_REDUCE_H

/*
 * Payne-Hanek range reduction for large trig arguments.
 * Ported from Go math.trigReduce.
 *
 * For x >= reduceThreshold (2^29), the simple PI4A/PI4B/PI4C
 * 3-part reduction loses too many significant bits.
 * This implements exact reduction using 1217-bit representation of 4/pi.
 *
 * Reference: "ARGUMENT REDUCTION FOR HUGE ARGUMENTS: Good to the Last Bit"
 *            K. C. Ng et al, March 24, 1992
 */

#include "_math_internal.h"

#define TRIG_REDUCE_THRESHOLD (1 << 29)

static const uint64_t _mPi4[] = {
    0x0000000000000001,
    0x45f306dc9c882a53,
    0xf84eafa3ea69bb81,
    0xb6c52b3278872083,
    0xfca2c757bd778ac3,
    0x6e48dc74849ba5c0,
    0x0c925dd413a32439,
    0xfc3bd63962534e7d,
    0xd1046bea5d768909,
    0xd338e04d68befc82,
    0x7323ac7306a673e9,
    0x3908bf177bf25076,
    0x3ff12fffbc0b301f,
    0xde5e2316b414da3e,
    0xda6cfd9e4f96136e,
    0x9e8c7ecd3cbfd45a,
    0xea4f758fd7cbe2f6,
    0x7a0e73ef14a525d4,
    0xd7f6bf623f1aba10,
    0xac06608df8f6d757,
};

static inline int _nc_leading_zeros64(uint64_t x) {
    if (x == 0) return 64;
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_clzll(x);
#else
    int n = 0;
    if (x <= 0x00000000FFFFFFFFULL) { n += 32; x <<= 32; }
    if (x <= 0x0000FFFFFFFFFFFFULL) { n += 16; x <<= 16; }
    if (x <= 0x00FFFFFFFFFFFFFFULL) { n +=  8; x <<=  8; }
    if (x <= 0x0FFFFFFFFFFFFFFFULL) { n +=  4; x <<=  4; }
    if (x <= 0x3FFFFFFFFFFFFFFFULL) { n +=  2; x <<=  2; }
    if (x <= 0x7FFFFFFFFFFFFFFFULL) { n +=  1; }
    return n;
#endif
}

/*
 * nc_trig_reduce: Payne-Hanek range reduction of x by Pi/4.
 * x must be > 0.
 * Returns j (integer part mod 8) and z (fractional part * Pi/4).
 */
static inline void nc_trig_reduce(double x, uint64_t *j_out, double *z_out) {
    const double PI4 = NEVERC_MATH_PI / 4.0;

    if (x < PI4) {
        *j_out = 0;
        *z_out = x;
        return;
    }

    uint64_t ix = nc_f64_to_bits(x);
    int exp = (int)((ix >> NC_EXP_SHIFT) & 0x7FF) - NC_EXP_BIAS - NC_EXP_SHIFT;
    ix &= NC_FRAC_MASK;
    ix |= 1ULL << NC_EXP_SHIFT;

    unsigned digit = (unsigned)(exp + 61) / 64;
    unsigned bitshift = (unsigned)(exp + 61) % 64;

    uint64_t z0, z1, z2;
    if (bitshift == 0) {
        z0 = _mPi4[digit];
        z1 = _mPi4[digit + 1];
        z2 = _mPi4[digit + 2];
    } else {
        z0 = (_mPi4[digit] << bitshift) | (_mPi4[digit + 1] >> (64 - bitshift));
        z1 = (_mPi4[digit + 1] << bitshift) | (_mPi4[digit + 2] >> (64 - bitshift));
        z2 = (_mPi4[digit + 2] << bitshift) | (_mPi4[digit + 3] >> (64 - bitshift));
    }

#ifdef __SIZEOF_INT128__
    __uint128_t prod2 = (__uint128_t)z2 * ix;
    uint64_t z2hi = (uint64_t)(prod2 >> 64);

    __uint128_t prod1 = (__uint128_t)z1 * ix;
    uint64_t z1hi = (uint64_t)(prod1 >> 64);
    uint64_t z1lo = (uint64_t)prod1;

    uint64_t z0lo = z0 * ix;

    __uint128_t sum_lo = (__uint128_t)z1lo + z2hi;
    uint64_t lo = (uint64_t)sum_lo;
    uint64_t c = (uint64_t)(sum_lo >> 64);

    uint64_t hi = z0lo + z1hi + c;
#else
    /* Fallback: split 64x64 multiply into 32-bit parts */
    uint64_t z2hi, z1hi, z1lo, z0lo;
    {
        uint64_t a_lo = z2 & 0xFFFFFFFF, a_hi = z2 >> 32;
        uint64_t b_lo = ix & 0xFFFFFFFF, b_hi = ix >> 32;
        uint64_t t0 = a_lo * b_lo;
        uint64_t t1 = a_lo * b_hi + (t0 >> 32);
        uint64_t t2 = a_hi * b_lo + (t1 & 0xFFFFFFFF);
        z2hi = a_hi * b_hi + (t1 >> 32) + (t2 >> 32);
    }
    {
        uint64_t a_lo = z1 & 0xFFFFFFFF, a_hi = z1 >> 32;
        uint64_t b_lo = ix & 0xFFFFFFFF, b_hi = ix >> 32;
        uint64_t t0 = a_lo * b_lo;
        uint64_t t1 = a_lo * b_hi + (t0 >> 32);
        uint64_t t2 = a_hi * b_lo + (t1 & 0xFFFFFFFF);
        z1hi = a_hi * b_hi + (t1 >> 32) + (t2 >> 32);
        z1lo = (t2 << 32) | (t0 & 0xFFFFFFFF);
    }
    z0lo = z0 * ix;
    uint64_t lo = z1lo + z2hi;
    uint64_t c = (lo < z1lo) ? 1 : 0;
    uint64_t hi = z0lo + z1hi + c;
#endif

    uint64_t j = hi >> 61;

    hi = (hi << 3) | (lo >> 61);
    unsigned lz = (unsigned)_nc_leading_zeros64(hi);
    uint64_t e = (uint64_t)(NC_EXP_BIAS - (lz + 1));

    {
        unsigned shift_amt = (unsigned)(lz + 1);
        if (shift_amt >= 64)
            hi = lo << (shift_amt - 64);
        else
            hi = (hi << shift_amt) | (lo >> (64 - shift_amt));
    }
    hi >>= 64 - NC_EXP_SHIFT;
    hi |= e << NC_EXP_SHIFT;
    double z = nc_f64_from_bits(hi);

    if (j & 1) {
        j++;
        j &= 7;
        z -= 1.0;
    }

    *j_out = j;
    *z_out = z * PI4;
}

#endif /* _NEVERC_TRIG_REDUCE_H */
