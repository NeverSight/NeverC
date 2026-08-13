#include "neverc/std/math/bits.h"

/*
 * Bit manipulation functions — mirrors Go math/bits package.
 * Uses compiler builtins (CLZ/CTZ/POPCNT/BSWAP → single HW instructions)
 * where available; falls back to Hacker's Delight algorithms otherwise.
 */

#if UINT_MAX > 0xFFFFFFFFu
#define NC_UINT_IS_64 1
#else
#define NC_UINT_IS_64 0
#endif

#ifndef NCI_HAS_BUILTINS
#if defined(__GNUC__) || defined(__clang__)
#define NCI_HAS_BUILTINS 1
#else
#define NCI_HAS_BUILTINS 0
#endif
#endif

#if !NCI_HAS_BUILTINS
static const uint8_t len8tab[256] = {
    0,1,2,2,3,3,3,3,4,4,4,4,4,4,4,4,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
    6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
};

static const uint32_t deBruijn32 = 0x077CB531U;
static const uint8_t deBruijn32tab[32] = {
    0,1,28,2,29,14,24,3,30,22,20,15,25,17,4,8,
    31,27,13,23,21,19,16,7,26,12,18,6,11,5,10,9,
};

static const uint64_t deBruijn64 = 0x03f79d71b4ca8b09ULL;
static const uint8_t deBruijn64tab[64] = {
    0,1,56,2,57,49,28,3,61,58,42,50,38,29,17,4,
    62,47,59,36,45,43,51,22,53,39,33,30,24,18,12,5,
    63,55,48,27,60,41,37,16,46,35,44,21,52,32,23,11,
    54,26,40,15,34,20,31,10,25,14,19,9,13,8,7,6,
};
#endif /* !NCI_HAS_BUILTINS */

/* --- Len (bit length) --- */

#if NCI_HAS_BUILTINS

int neverc_bits_len8(uint8_t x) {
    return x == 0 ? 0 : (int)(sizeof(unsigned) * 8) - __builtin_clz((unsigned)x);
}

int neverc_bits_len16(uint16_t x) {
    return x == 0 ? 0 : (int)(sizeof(unsigned) * 8) - __builtin_clz((unsigned)x);
}

int neverc_bits_len32(uint32_t x) {
    return x == 0 ? 0 : 32 - __builtin_clz(x);
}

int neverc_bits_len64(uint64_t x) {
    return x == 0 ? 0 : 64 - __builtin_clzll(x);
}

#else /* fallback */

int neverc_bits_len8(uint8_t x) { return (int)len8tab[x]; }

int neverc_bits_len16(uint16_t x) {
    int n = 0;
    if (x >= (1U << 8)) { x >>= 8; n = 8; }
    return n + (int)len8tab[x];
}

int neverc_bits_len32(uint32_t x) {
    int n = 0;
    if (x >= (1U << 16)) { x >>= 16; n = 16; }
    if (x >= (1U << 8))  { x >>= 8;  n += 8; }
    return n + (int)len8tab[x];
}

int neverc_bits_len64(uint64_t x) {
    int n = 0;
    if (x >= (1ULL << 32)) { x >>= 32; n = 32; }
    return n + neverc_bits_len32((uint32_t)x);
}

#endif

int neverc_bits_len(unsigned int x) {
#if NC_UINT_IS_64
    return neverc_bits_len64((uint64_t)x);
#else
    return neverc_bits_len32((uint32_t)x);
#endif
}

/* --- Leading Zeros --- */

#if NCI_HAS_BUILTINS

int neverc_bits_leading_zeros8(uint8_t x) {
    return x == 0 ? 8 : __builtin_clz((unsigned)x) - (int)(sizeof(unsigned) * 8 - 8);
}
int neverc_bits_leading_zeros16(uint16_t x) {
    return x == 0 ? 16 : __builtin_clz((unsigned)x) - (int)(sizeof(int) * 8 - 16);
}
int neverc_bits_leading_zeros32(uint32_t x) {
    return x == 0 ? 32 : __builtin_clz(x);
}
int neverc_bits_leading_zeros64(uint64_t x) {
    return x == 0 ? 64 : __builtin_clzll(x);
}

#else

int neverc_bits_leading_zeros8(uint8_t x) { return 8 - neverc_bits_len8(x); }
int neverc_bits_leading_zeros16(uint16_t x) { return 16 - neverc_bits_len16(x); }
int neverc_bits_leading_zeros32(uint32_t x) { return 32 - neverc_bits_len32(x); }
int neverc_bits_leading_zeros64(uint64_t x) { return 64 - neverc_bits_len64(x); }

#endif

int neverc_bits_leading_zeros(unsigned int x) {
#if NC_UINT_IS_64
    return neverc_bits_leading_zeros64((uint64_t)x);
#else
    return neverc_bits_leading_zeros32((uint32_t)x);
#endif
}

/* --- Trailing Zeros --- */

#if NCI_HAS_BUILTINS

int neverc_bits_trailing_zeros8(uint8_t x) {
    return x == 0 ? 8 : __builtin_ctz((unsigned)x);
}
int neverc_bits_trailing_zeros16(uint16_t x) {
    return x == 0 ? 16 : __builtin_ctz((unsigned)x);
}
int neverc_bits_trailing_zeros32(uint32_t x) {
    return x == 0 ? 32 : __builtin_ctz(x);
}
int neverc_bits_trailing_zeros64(uint64_t x) {
    return x == 0 ? 64 : __builtin_ctzll(x);
}

#else

int neverc_bits_trailing_zeros8(uint8_t x) {
    return x == 0 ? 8
                  : neverc_bits_trailing_zeros32(
                        (uint32_t)x & (UINT32_C(0) - (uint32_t)x));
}
int neverc_bits_trailing_zeros16(uint16_t x) {
    return x == 0 ? 16
                  : neverc_bits_trailing_zeros32(
                        (uint32_t)x & (UINT32_C(0) - (uint32_t)x));
}
int neverc_bits_trailing_zeros32(uint32_t x) {
    if (x == 0) return 32;
    return (int)deBruijn32tab[
        ((x & (UINT32_C(0) - x)) * deBruijn32) >> 27];
}
int neverc_bits_trailing_zeros64(uint64_t x) {
    if (x == 0) return 64;
    return (int)deBruijn64tab[
        ((x & (UINT64_C(0) - x)) * deBruijn64) >> 58];
}

#endif

int neverc_bits_trailing_zeros(unsigned int x) {
#if NC_UINT_IS_64
    return neverc_bits_trailing_zeros64((uint64_t)x);
#else
    return neverc_bits_trailing_zeros32((uint32_t)x);
#endif
}

/* --- OnesCount (popcount) --- */

#if NCI_HAS_BUILTINS

int neverc_bits_ones_count8(uint8_t x) {
    return __builtin_popcount((unsigned)x);
}
int neverc_bits_ones_count16(uint16_t x) {
    return __builtin_popcount((unsigned)x);
}
int neverc_bits_ones_count32(uint32_t x) {
    return __builtin_popcount(x);
}
int neverc_bits_ones_count64(uint64_t x) {
    return __builtin_popcountll(x);
}

#else

int neverc_bits_ones_count8(uint8_t x) {
    x = x - ((x >> 1) & 0x55);
    x = (x & 0x33) + ((x >> 2) & 0x33);
    return (int)((x + (x >> 4)) & 0x0F);
}
int neverc_bits_ones_count16(uint16_t x) {
    return neverc_bits_ones_count32((uint32_t)x);
}
int neverc_bits_ones_count32(uint32_t x) {
    x = x - ((x >> 1) & 0x55555555U);
    x = (x & 0x33333333U) + ((x >> 2) & 0x33333333U);
    x = (x + (x >> 4)) & 0x0F0F0F0FU;
    return (int)((x * 0x01010101U) >> 24);
}
int neverc_bits_ones_count64(uint64_t x) {
    x = x - ((x >> 1) & 0x5555555555555555ULL);
    x = (x & 0x3333333333333333ULL) + ((x >> 2) & 0x3333333333333333ULL);
    x = (x + (x >> 4)) & 0x0F0F0F0F0F0F0F0FULL;
    return (int)((x * 0x0101010101010101ULL) >> 56);
}

#endif

int neverc_bits_ones_count(unsigned int x) {
#if NC_UINT_IS_64
    return neverc_bits_ones_count64((uint64_t)x);
#else
    return neverc_bits_ones_count32((uint32_t)x);
#endif
}

/* --- Rotate Left --- */

unsigned int neverc_bits_rotate_left(unsigned int x, int k) {
#if NC_UINT_IS_64
    return (unsigned int)neverc_bits_rotate_left64((uint64_t)x, k);
#else
    return (unsigned int)neverc_bits_rotate_left32((uint32_t)x, k);
#endif
}

uint8_t neverc_bits_rotate_left8(uint8_t x, int k) {
    unsigned s = (unsigned)k & 7;
    return (uint8_t)((x << s) | (x >> ((8 - s) & 7)));
}
uint16_t neverc_bits_rotate_left16(uint16_t x, int k) {
    unsigned s = (unsigned)k & 15;
    return (uint16_t)((x << s) | (x >> ((16 - s) & 15)));
}
uint32_t neverc_bits_rotate_left32(uint32_t x, int k) {
    unsigned s = (unsigned)k & 31;
    return (x << s) | (x >> ((32 - s) & 31));
}

uint64_t neverc_bits_rotate_left64(uint64_t x, int k) {
    unsigned s = (unsigned)k & 63;
    return (x << s) | (x >> ((64 - s) & 63));
}

/* --- Bit Reverse --- */

unsigned int neverc_bits_reverse(unsigned int x) {
#if NC_UINT_IS_64
    return (unsigned int)neverc_bits_reverse64((uint64_t)x);
#else
    return (unsigned int)neverc_bits_reverse32((uint32_t)x);
#endif
}

uint8_t neverc_bits_reverse8(uint8_t x) {
    x = ((x >> 1) & 0x55) | ((x & 0x55) << 1);
    x = ((x >> 2) & 0x33) | ((x & 0x33) << 2);
    return (x >> 4) | (x << 4);
}
uint16_t neverc_bits_reverse16(uint16_t x) {
    return (uint16_t)((neverc_bits_reverse8((uint8_t)x) << 8) |
                      neverc_bits_reverse8((uint8_t)(x >> 8)));
}
uint32_t neverc_bits_reverse32(uint32_t x) {
    x = ((x >> 1) & 0x55555555U) | ((x & 0x55555555U) << 1);
    x = ((x >> 2) & 0x33333333U) | ((x & 0x33333333U) << 2);
    x = ((x >> 4) & 0x0F0F0F0FU) | ((x & 0x0F0F0F0FU) << 4);
    return neverc_bits_reverse_bytes32(x);
}

uint64_t neverc_bits_reverse64(uint64_t x) {
    x = ((x >> 1) & 0x5555555555555555ULL) | ((x & 0x5555555555555555ULL) << 1);
    x = ((x >> 2) & 0x3333333333333333ULL) | ((x & 0x3333333333333333ULL) << 2);
    x = ((x >> 4) & 0x0F0F0F0F0F0F0F0FULL) | ((x & 0x0F0F0F0F0F0F0F0FULL) << 4);
    return neverc_bits_reverse_bytes64(x);
}

/* --- Byte Swap --- */

uint16_t neverc_bits_reverse_bytes16(uint16_t x) {
#if NCI_HAS_BUILTINS
    return __builtin_bswap16(x);
#else
    return (x >> 8) | (x << 8);
#endif
}

uint32_t neverc_bits_reverse_bytes32(uint32_t x) {
#if NCI_HAS_BUILTINS
    return __builtin_bswap32(x);
#else
    return ((x >> 24)) |
           ((x >> 8) & 0x0000FF00U) |
           ((x << 8) & 0x00FF0000U) |
           ((x << 24));
#endif
}

uint64_t neverc_bits_reverse_bytes64(uint64_t x) {
#if NCI_HAS_BUILTINS
    return __builtin_bswap64(x);
#else
    return ((x >> 56)) |
           ((x >> 40) & 0x000000000000FF00ULL) |
           ((x >> 24) & 0x0000000000FF0000ULL) |
           ((x >>  8) & 0x00000000FF000000ULL) |
           ((x <<  8) & 0x000000FF00000000ULL) |
           ((x << 24) & 0x0000FF0000000000ULL) |
           ((x << 40) & 0x00FF000000000000ULL) |
           ((x << 56));
#endif
}

unsigned int neverc_bits_reverse_bytes(unsigned int x) {
#if NC_UINT_IS_64
    return (unsigned int)neverc_bits_reverse_bytes64((uint64_t)x);
#else
    return (unsigned int)neverc_bits_reverse_bytes32((uint32_t)x);
#endif
}

/* --- Multi-precision arithmetic --- */

void neverc_bits_add64(uint64_t x, uint64_t y, uint64_t carry,
                       uint64_t *sum, uint64_t *carry_out) {
    uint64_t s = x + y + carry;
    *sum = s;
    *carry_out = ((x & y) | ((x | y) & ~s)) >> 63;
}

void neverc_bits_sub64(uint64_t x, uint64_t y, uint64_t borrow,
                       uint64_t *diff, uint64_t *borrow_out) {
    uint64_t d = x - y - borrow;
    *diff = d;
    *borrow_out = ((~x & y) | (~(x ^ y) & d)) >> 63;
}

void neverc_bits_add32(uint32_t x, uint32_t y, uint32_t carry,
                       uint32_t *sum, uint32_t *carry_out) {
    uint64_t s = (uint64_t)x + (uint64_t)y + (uint64_t)carry;
    *sum = (uint32_t)s;
    *carry_out = (uint32_t)(s >> 32);
}

void neverc_bits_sub32(uint32_t x, uint32_t y, uint32_t borrow,
                       uint32_t *diff, uint32_t *borrow_out) {
    uint64_t d = (uint64_t)x - (uint64_t)y - (uint64_t)borrow;
    *diff = (uint32_t)d;
    *borrow_out = (uint32_t)((d >> 63) & 1);
}

void neverc_bits_mul32(uint32_t x, uint32_t y,
                       uint32_t *hi, uint32_t *lo) {
    uint64_t p = (uint64_t)x * (uint64_t)y;
    *hi = (uint32_t)(p >> 32);
    *lo = (uint32_t)p;
}

void neverc_bits_mul64(uint64_t x, uint64_t y,
                       uint64_t *hi, uint64_t *lo) {
#ifdef __SIZEOF_INT128__
    __uint128_t r = (__uint128_t)x * y;
    *hi = (uint64_t)(r >> 64);
    *lo = (uint64_t)r;
#else
    uint64_t x0 = x & 0xFFFFFFFFULL;
    uint64_t x1 = x >> 32;
    uint64_t y0 = y & 0xFFFFFFFFULL;
    uint64_t y1 = y >> 32;
    uint64_t w0 = x0 * y0;
    uint64_t t = x1 * y0 + (w0 >> 32);
    uint64_t w1 = t & 0xFFFFFFFFULL;
    uint64_t w2 = t >> 32;
    w1 += x0 * y1;
    *hi = x1 * y1 + w2 + (w1 >> 32);
    *lo = x * y;
#endif
}

void neverc_bits_div32(uint32_t hi, uint32_t lo, uint32_t y,
                       uint32_t *quo, uint32_t *rem) {
    uint64_t n = ((uint64_t)hi << 32) | (uint64_t)lo;
    *quo = (uint32_t)(n / y);
    *rem = (uint32_t)(n % y);
}

void neverc_bits_div64(uint64_t hi, uint64_t lo, uint64_t y,
                       uint64_t *quo, uint64_t *rem) {
    if (hi == 0) {
        *quo = lo / y;
        *rem = lo % y;
        return;
    }
#ifdef __SIZEOF_INT128__
    __uint128_t n = ((__uint128_t)hi << 64) | lo;
    *quo = (uint64_t)(n / y);
    *rem = (uint64_t)(n % y);
#else
    uint64_t q = 0, r = hi;
    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((lo >> i) & 1);
        if (r >= y) { r -= y; q |= (1ULL << i); }
    }
    *quo = q;
    *rem = r;
#endif
}

uint32_t neverc_bits_rem32(uint32_t hi, uint32_t lo, uint32_t y) {
    uint32_t q, r;
    neverc_bits_div32(hi, lo, y, &q, &r);
    (void)q;
    return r;
}

uint64_t neverc_bits_rem64(uint64_t hi, uint64_t lo, uint64_t y) {
    uint64_t q, r;
    neverc_bits_div64(hi, lo, y, &q, &r);
    (void)q;
    return r;
}

/* --- Generic uint-sized dispatchers (after all sized versions) --- */

void neverc_bits_add(unsigned int x, unsigned int y, unsigned int carry,
                     unsigned int *sum, unsigned int *carry_out) {
#if NC_UINT_IS_64
    uint64_t s64, c64;
    neverc_bits_add64((uint64_t)x, (uint64_t)y, (uint64_t)carry, &s64, &c64);
    *sum = (unsigned int)s64; *carry_out = (unsigned int)c64;
#else
    uint32_t s32, c32;
    neverc_bits_add32((uint32_t)x, (uint32_t)y, (uint32_t)carry, &s32, &c32);
    *sum = (unsigned int)s32; *carry_out = (unsigned int)c32;
#endif
}

void neverc_bits_sub(unsigned int x, unsigned int y, unsigned int borrow,
                     unsigned int *diff, unsigned int *borrow_out) {
#if NC_UINT_IS_64
    uint64_t d64, b64;
    neverc_bits_sub64((uint64_t)x, (uint64_t)y, (uint64_t)borrow, &d64, &b64);
    *diff = (unsigned int)d64; *borrow_out = (unsigned int)b64;
#else
    uint32_t d32, b32;
    neverc_bits_sub32((uint32_t)x, (uint32_t)y, (uint32_t)borrow, &d32, &b32);
    *diff = (unsigned int)d32; *borrow_out = (unsigned int)b32;
#endif
}

void neverc_bits_mul(unsigned int x, unsigned int y,
                     unsigned int *hi, unsigned int *lo) {
#if NC_UINT_IS_64
    uint64_t h64, l64;
    neverc_bits_mul64((uint64_t)x, (uint64_t)y, &h64, &l64);
    *hi = (unsigned int)h64; *lo = (unsigned int)l64;
#else
    uint32_t h32, l32;
    neverc_bits_mul32((uint32_t)x, (uint32_t)y, &h32, &l32);
    *hi = (unsigned int)h32; *lo = (unsigned int)l32;
#endif
}

void neverc_bits_div(unsigned int hi, unsigned int lo, unsigned int y,
                     unsigned int *quo, unsigned int *rem) {
#if NC_UINT_IS_64
    uint64_t q64, r64;
    neverc_bits_div64((uint64_t)hi, (uint64_t)lo, (uint64_t)y, &q64, &r64);
    *quo = (unsigned int)q64; *rem = (unsigned int)r64;
#else
    uint32_t q32, r32;
    neverc_bits_div32((uint32_t)hi, (uint32_t)lo, (uint32_t)y, &q32, &r32);
    *quo = (unsigned int)q32; *rem = (unsigned int)r32;
#endif
}

unsigned int neverc_bits_rem(unsigned int hi, unsigned int lo, unsigned int y) {
    unsigned int q, r;
    neverc_bits_div(hi, lo, y, &q, &r);
    (void)q;
    return r;
}
