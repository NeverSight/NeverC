#include "neverc/std/math.h"
#include "_math_internal.h"

/*
 * FMA returns x * y + z, computed with only one rounding.
 * Ported from Go math.FMA — uses 128-bit intermediate arithmetic
 * to avoid double-rounding that occurs with naive x*y+z.
 */

static uint64_t nonzero(uint64_t x) { return x != 0 ? 1 : 0; }
static uint64_t zero_fn(uint64_t x) { return x == 0 ? 1 : 0; }

static void mul64(uint64_t a, uint64_t b, uint64_t *hi, uint64_t *lo) {
    uint64_t a_lo = a & 0xFFFFFFFF, a_hi = a >> 32;
    uint64_t b_lo = b & 0xFFFFFFFF, b_hi = b >> 32;
    uint64_t p0 = a_lo * b_lo;
    uint64_t p1 = a_lo * b_hi;
    uint64_t p2 = a_hi * b_lo;
    uint64_t p3 = a_hi * b_hi;
    uint64_t mid = p1 + (p0 >> 32);
    uint64_t carry = (mid < p1) ? 1ULL : 0ULL;
    mid += p2;
    carry += (mid < p2) ? 1ULL : 0ULL;
    *lo = (mid << 32) | (p0 & 0xFFFFFFFF);
    *hi = p3 + (mid >> 32) + (carry << 32);
}

static void add64c(uint64_t a, uint64_t b, uint64_t cin,
                   uint64_t *sum, uint64_t *cout) {
    uint64_t s = a + b + cin;
    *cout = ((a & b) | ((a | b) & ~s)) >> 63;
    *sum = s;
}

static void sub64c(uint64_t a, uint64_t b, uint64_t bin,
                   uint64_t *diff, uint64_t *bout) {
    uint64_t d = a - b - bin;
    *bout = ((~a & b) | (~(a ^ b) & d)) >> 63;
    *diff = d;
}

static int clz64(uint64_t x) {
    if (x == 0) return 64;
    int n = 0;
    if ((x & 0xFFFFFFFF00000000ULL) == 0) { n += 32; x <<= 32; }
    if ((x & 0xFFFF000000000000ULL) == 0) { n += 16; x <<= 16; }
    if ((x & 0xFF00000000000000ULL) == 0) { n +=  8; x <<=  8; }
    if ((x & 0xF000000000000000ULL) == 0) { n +=  4; x <<=  4; }
    if ((x & 0xC000000000000000ULL) == 0) { n +=  2; x <<=  2; }
    if ((x & 0x8000000000000000ULL) == 0) { n +=  1; }
    return n;
}

static void shl128(uint64_t u1, uint64_t u2, unsigned n,
                   uint64_t *r1, uint64_t *r2) {
    if (n >= 128) { *r1 = 0; *r2 = 0; return; }
    if (n >= 64) { *r1 = u2 << (n - 64); *r2 = 0; return; }
    if (n == 0) { *r1 = u1; *r2 = u2; return; }
    *r1 = (u1 << n) | (u2 >> (64 - n));
    *r2 = u2 << n;
}

static void shr128(uint64_t u1, uint64_t u2, unsigned n,
                   uint64_t *r1, uint64_t *r2) {
    if (n >= 128) { *r1 = 0; *r2 = 0; return; }
    if (n >= 64) { *r2 = u1 >> (n - 64); *r1 = 0; return; }
    if (n == 0) { *r1 = u1; *r2 = u2; return; }
    *r2 = (u2 >> n) | (u1 << (64 - n));
    *r1 = u1 >> n;
}

static void shrcompress(uint64_t u1, uint64_t u2, unsigned n,
                        uint64_t *r1, uint64_t *r2) {
    if (n == 0) { *r1 = u1; *r2 = u2; return; }
    if (n == 64) { *r1 = 0; *r2 = u1 | nonzero(u2); return; }
    if (n >= 128) { *r1 = 0; *r2 = nonzero(u1 | u2); return; }
    uint64_t s1, s2;
    shr128(u1, u2, n, &s1, &s2);
    if (n < 64) {
        s2 |= nonzero(u2 & ((1ULL << n) - 1));
    } else {
        s2 |= nonzero((u1 & ((1ULL << (n - 64)) - 1)) | u2);
    }
    *r1 = s1;
    *r2 = s2;
}

static int lz128(uint64_t u1, uint64_t u2) {
    int l = clz64(u1);
    if (l == 64) l += clz64(u2);
    return l;
}

static void fma_split(uint64_t b, uint32_t *sign, int32_t *exp, uint64_t *mant) {
    *sign = (uint32_t)(b >> 63);
    *exp = (int32_t)((b >> 52) & 0x7FF);
    *mant = b & NC_FRAC_MASK;
    if (*exp == 0) {
        unsigned shift = (unsigned)(clz64(*mant) - 11);
        *mant <<= shift;
        *exp = 1 - (int32_t)shift;
    } else {
        *mant |= 1ULL << 52;
    }
}

double neverc_math_fma(double x, double y, double z) {
    uint64_t bx = nc_f64_to_bits(x);
    uint64_t by = nc_f64_to_bits(y);
    uint64_t bz = nc_f64_to_bits(z);

    if (x == 0.0 || y == 0.0 || (bx & NC_EXP_MASK) == NC_EXP_MASK ||
        (by & NC_EXP_MASK) == NC_EXP_MASK) {
        return x * y + z;
    }
    if (z == 0.0) return x * y;
    if ((bz & NC_EXP_MASK) == NC_EXP_MASK) return z;

    uint32_t xs, ys, zs;
    int32_t xe, ye, ze;
    uint64_t xm, ym, zm;
    fma_split(bx, &xs, &xe, &xm);
    fma_split(by, &ys, &ye, &ym);
    fma_split(bz, &zs, &ze, &zm);

    int32_t pe = xe + ye - NC_EXP_BIAS + 1;
    uint64_t pm1, pm2;
    mul64(xm << 10, ym << 11, &pm1, &pm2);
    uint64_t zm1 = zm << 10, zm2 = 0;
    uint32_t ps = xs ^ ys;

    unsigned is62zero = (unsigned)((~pm1 >> 62) & 1);
    shl128(pm1, pm2, is62zero, &pm1, &pm2);
    pe -= (int32_t)is62zero;

    if (pe < ze || (pe == ze && pm1 < zm1)) {
        uint32_t ts = ps; int32_t te = pe;
        uint64_t tm1 = pm1, tm2 = pm2;
        ps = zs; pe = ze; pm1 = zm1; pm2 = zm2;
        zs = ts; ze = te; zm1 = tm1; zm2 = tm2;
    }

    if (ps != zs && pe == ze && pm1 == zm1 && pm2 == zm2)
        return 0.0;

    shrcompress(zm1, zm2, (unsigned)(pe - ze), &zm1, &zm2);

    uint64_t m, c;
    if (ps == zs) {
        add64c(pm2, zm2, 0, &pm2, &c);
        add64c(pm1, zm1, c, &pm1, &c);
        pe -= (int32_t)(~pm1 >> 63);
        unsigned shift = (unsigned)(64 + (pm1 >> 63));
        shrcompress(pm1, pm2, shift, &pm1, &m);
    } else {
        sub64c(pm2, zm2, 0, &pm2, &c);
        sub64c(pm1, zm1, c, &pm1, &c);
        int nz = lz128(pm1, pm2);
        pe -= (int32_t)nz;
        shl128(pm1, pm2, (unsigned)(nz - 1), &m, &pm2);
        m |= nonzero(pm2);
    }

    if (pe > 1022 + NC_EXP_BIAS ||
        (pe == 1022 + NC_EXP_BIAS && ((m + (1ULL << 9)) >> 63) == 1)) {
        return nc_f64_from_bits((uint64_t)ps << 63 | NC_EXP_MASK);
    }
    if (pe < 0) {
        unsigned n = (unsigned)(-pe);
        m = (m >> n) | nonzero(m & ((1ULL << n) - 1));
        pe = 0;
    }
    m = ((m + (1ULL << 9)) >> 10) & ~zero_fn((m & ((1ULL << 10) - 1)) ^ (1ULL << 9));
    pe &= -(int32_t)nonzero(m);
    return nc_f64_from_bits(((uint64_t)ps << 63) + ((uint64_t)pe << 52) + m);
}
