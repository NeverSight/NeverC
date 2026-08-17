/*
 * ML-DSA-44 — Module-Lattice-based Digital Signature (NIST FIPS 204).
 *
 * Parameters: k=4, l=4, eta=2, gamma1=2^17, gamma2=95232, tau=39, omega=80.
 * q = 8380417, n = 256.
 */
#include "neverc/std/crypto/mldsa.h"
#include "neverc/std/crypto/sha3.h"
#include "neverc/std/crypto/rand.h"
#include "neverc/std/crypto/subtle.h"
#include "neverc/std/_platform.h"
#include <string.h>
#include <stdlib.h>

/* ── Parameters ──────────────────────────────────────────── */

#define Q       8380417
#define N       256
#define K       4
#define L       4
#define ETA     2
#define GAMMA1  (1 << 17)  /* 131072 */
#define GAMMA2  ((Q - 1) / 88)  /* 95232 */
#define TAU     39
#define OMEGA   80
#define LAMBDA  128
#define D_BITS  13  /* power2round decomposition bits */
#define W1_POLY_SIZE (N * 6 / 8)

/* ── Field arithmetic mod q ──────────────────────────────── */

typedef int32_t fe_t;
typedef fe_t poly_t[N];

static fe_t fe_mod(int64_t a) {
    int32_t r = (int32_t)(a % Q);
    if (r < 0) r += Q;
    return r;
}

static fe_t fe_add(fe_t a, fe_t b) { return fe_mod((int64_t)a + b); }
static fe_t fe_sub(fe_t a, fe_t b) { return fe_mod((int64_t)a - b); }
static fe_t fe_mul(fe_t a, fe_t b) { return fe_mod((int64_t)a * b); }

/* ── NTT (FIPS 204, Algorithms 41–42) ────────────────────── */

/* ζ = 1753, root of unity of order 512 mod q */
static const int32_t ntt_zetas[256] = {
    1, -3572223, 3765607, 3761513, -3201494, -2883726, -3145678, -3201430,
    -601683, 3542485, 2682288, 2129892, 3764867, -1005239, 557458, -1221177,
    -3370349, -4063053, 2663378, -1674615, -3524442, -434125, 676590, -1335936,
    -3227876, 1714295, 2453983, 1460718, -642628, -3585098, 2815639, 2283733,
    3602218, 3182878, 2740543, -3586446, -3110818, 2101410, 3704823, 1159875,
    394148, 928749, 1095468, -3506380, 2071829, -4018989, 3241972, 2156050,
    3415069, 1759347, -817536, -3574466, 3756790, -1935799, -1716988, -3950053,
    -2897314, 3192354, 556856, 3870317, 2917338, 1853806, 3345963, 1858416,
    3073009, 1277625, -2635473, 3852015, 4183372, -3222807, -3121440, -274060,
    2508980, 2028118, 1937570, -3815725, 2811291, -2983781, -1109516, 4158088,
    1528066, 482649, 1148858, -2962264, -565603, 169688, 2462444, -3334383,
    -4166425, -3488383, 1987814, -3197248, 1736313, 235407, -3250154, 3258457,
    -2579253, 1787943, -2391089, -2254727, 3482206, -4182915, -1300016, -2362063,
    -1317678, 2461387, 3035980, 621164, 3901472, -1226661, 2925816, 3374250,
    1356448, -2775755, 2683270, -2778788, -3467665, 2312838, -653275, -459163,
    348812, -327848, 1011223, -2354215, -3818627, -1922253, -2236726, 1744507,
    1753, -1935420, -2659525, -1455890, 2660408, -1780227, -59148, 2772600,
    1182243, 87208, 636927, -3965306, -3956745, -2296397, -3284915, -3716946,
    -27812, 822541, 1009365, -2454145, -1979497, 1596822, -3956944, -3759465,
    -1685153, -3410568, 2678278, -3768948, -3551006, 635956, -250446, -2455377,
    -4146264, -1772588, 2192938, -1727088, 2387513, -3611750, -268456, -3180456,
    3747250, 2296099, 1239911, -3838479, 3195676, 2642980, 1254190, -12417,
    2998219, 141835, -89301, 2513018, -1354892, 613238, -1310261, -2218467,
    -458740, -1921994, 4040196, -3472069, 2039144, -1879878, -818761, -2178965,
    -1623354, 2105286, -2374402, -2033807, 586241, -1179613, 527981, -2743411,
    -1476985, 1994046, 2491325, -1393159, 507927, -1187885, -724804, -1834526,
    -3033742, -338420, 2647994, 3009748, -2612853, 4148469, 749577, -4022750,
    3980599, 2569011, -1615530, 1723229, 1665318, 2028038, 1163598, -3369273,
    3994671, -11879, -1370517, 3020393, 3363542, 214880, 545376, -770441,
    3105558, -1103344, 508145, -553718, 860144, 3430436, 140244, -1514152,
    -2185084, 3123762, 2358373, -2193087, -3014420, -1716814, 2926054, -392707,
    -303005, 3531229, -3974485, -3773731, 1900052, -781875, 1054478, -731434
};

static void ntt_forward(const poly_t f_in, poly_t f_out) {
    memcpy(f_out, f_in, sizeof(poly_t));
    int k = 0;
    for (int len = 128; len >= 1; len >>= 1) {
        for (int start = 0; start < N; start += 2 * len) {
            k++;
            int32_t zeta = ntt_zetas[k];
            for (int j = start; j < start + len; j++) {
                int32_t t = fe_mul(zeta, f_out[j + len]);
                f_out[j + len] = fe_sub(f_out[j], t);
                f_out[j] = fe_add(f_out[j], t);
            }
        }
    }
}

static void ntt_inverse(const poly_t f_in, poly_t f_out) {
    memcpy(f_out, f_in, sizeof(poly_t));
    int k = 255;
    for (int len = 1; len <= 128; len <<= 1) {
        for (int start = 0; start < N; start += 2 * len) {
            int32_t zeta = -ntt_zetas[k];
            k--;
            for (int j = start; j < start + len; j++) {
                int32_t t = f_out[j];
                f_out[j] = fe_add(t, f_out[j + len]);
                f_out[j + len] = fe_mul(zeta, fe_sub(t, f_out[j + len]));
            }
        }
    }
    int32_t f_inv = 8347681; /* 256^(-1) mod q */
    for (int i = 0; i < N; i++)
        f_out[i] = fe_mul(f_out[i], f_inv);
}

static void ntt_pointwise(const poly_t a, const poly_t b, poly_t c) {
    for (int i = 0; i < N; i++)
        c[i] = fe_mul(a[i], b[i]);
}

static void poly_add(const poly_t a, const poly_t b, poly_t c) {
    for (int i = 0; i < N; i++) c[i] = fe_add(a[i], b[i]);
}

static void poly_sub(const poly_t a, const poly_t b, poly_t c) {
    for (int i = 0; i < N; i++) c[i] = fe_sub(a[i], b[i]);
}

/* ── Sampling ────────────────────────────────────────────── */

static void expand_a_row(const uint8_t rho[32], int i, int j, poly_t out) {
    neverc_sha3_ctx xof;
    neverc_shake128_init(&xof);
    neverc_shake128_update(&xof, rho, 32);
    uint8_t idx[2] = {(uint8_t)j, (uint8_t)i};
    neverc_shake128_update(&xof, idx, 2);

    int c = 0;
    uint8_t buf[24];
    while (c < N) {
        neverc_shake128_squeeze(&xof, buf, sizeof(buf));
        for (int off = 0; off + 2 < (int)sizeof(buf) && c < N; off += 3) {
            int32_t d = ((int32_t)buf[off]) |
                        ((int32_t)buf[off+1] << 8) |
                        ((int32_t)(buf[off+2] & 0x7F) << 16);
            if (d < Q) out[c++] = d;
        }
    }
    neverc_platform_secure_zero(buf, sizeof(buf));
    neverc_platform_secure_zero(&xof, sizeof(xof));
}

static void sample_eta2(const uint8_t *sigma, size_t sigma_len,
                        uint16_t counter, poly_t out) {
    neverc_sha3_ctx prf;
    neverc_shake256_init(&prf);
    neverc_shake256_update(&prf, sigma, sigma_len);
    uint8_t cnt[2] = {(uint8_t)(counter & 0xFF), (uint8_t)(counter >> 8)};
    neverc_shake256_update(&prf, cnt, 2);

    int coefficient = 0;
    while (coefficient < N) {
        uint8_t byte;
        neverc_shake256_squeeze(&prf, &byte, 1);
        uint8_t candidates[2] = {
            (uint8_t)(byte & 0x0F), (uint8_t)(byte >> 4)
        };
        for (int i = 0; i < 2 && coefficient < N; i++) {
            if (candidates[i] < 15) {
                int value = ETA - (candidates[i] % (2 * ETA + 1));
                out[coefficient++] = fe_mod(value);
            }
        }
    }
    neverc_platform_secure_zero(&prf, sizeof(prf));
}

/* ── Power2Round (FIPS 204, Algorithm 35) ────────────────── */

static void power2round(fe_t r, fe_t *r1, fe_t *r0) {
    *r1 = (r + (1 << (D_BITS - 1)) - 1) >> D_BITS;
    *r0 = r - (*r1 << D_BITS);
}

/* ── HighBits / LowBits (FIPS 204, Algorithm 36–37) ──────── */

static void decompose(fe_t r, fe_t *r1, fe_t *r0) {
    *r0 = (int32_t)(r % (2 * GAMMA2));
    if (*r0 > GAMMA2) *r0 -= 2 * GAMMA2;
    if (r - *r0 == Q - 1) { *r1 = 0; *r0 = *r0 - 1; }
    else { *r1 = (r - *r0) / (2 * GAMMA2); }
}

static fe_t high_bits(fe_t r) { fe_t r1, r0; decompose(r, &r1, &r0); return r1; }
static fe_t low_bits(fe_t r) { fe_t r1, r0; decompose(r, &r1, &r0); return r0; (void)r1; }

/* ── MakeHint / UseHint (FIPS 204, Algorithm 38–39) ──────── */

static int make_hint(fe_t z, fe_t r) {
    fe_t r1 = high_bits(r);
    fe_t v1 = high_bits(fe_add(r, z));
    return r1 != v1 ? 1 : 0;
}

static fe_t use_hint(int h, fe_t r) {
    fe_t r1, r0;
    decompose(r, &r1, &r0);
    if (h == 0) return r1;
    int m = (Q - 1) / (2 * GAMMA2);
    if (r0 > 0) return (r1 + 1) % m;
    return (r1 - 1 + m) % m;
}

/* ── Challenge polynomial (FIPS 204, Algorithm 34) ───────── */

static void sample_in_ball(const uint8_t *seed, size_t seed_len, poly_t c) {
    memset(c, 0, sizeof(poly_t));
    neverc_sha3_ctx xof;
    neverc_shake256_init(&xof);
    neverc_shake256_update(&xof, seed, seed_len);

    uint8_t buf[8];
    neverc_shake256_squeeze(&xof, buf, 8);
    uint64_t signs = 0;
    for (int i = 0; i < 8; i++) signs |= ((uint64_t)buf[i]) << (8 * i);

    for (int i = N - TAU; i < N; i++) {
        uint8_t jb;
        int j;
        do {
            neverc_shake256_squeeze(&xof, &jb, 1);
            j = jb;
        } while (j > i);

        c[i] = c[j];
        c[j] = (signs & 1) ? fe_mod(-1) : 1;
        signs >>= 1;
    }
    neverc_platform_secure_zero(buf, sizeof(buf));
    neverc_platform_secure_zero(&xof, sizeof(xof));
}

/* ── ExpandMask (FIPS 204, Algorithm 33) ─────────────────── */

static void expand_mask(const uint8_t *rho_prime, size_t rp_len,
                        uint16_t counter, poly_t out) {
    neverc_sha3_ctx xof;
    neverc_shake256_init(&xof);
    neverc_shake256_update(&xof, rho_prime, rp_len);
    uint8_t cnt[2] = {(uint8_t)(counter & 0xFF), (uint8_t)(counter >> 8)};
    neverc_shake256_update(&xof, cnt, 2);

    /* gamma1 = 2^17: coefficients stored as gamma1 - c (18 bits each) */
    uint8_t buf[576]; /* 256 * 18 / 8 */
    neverc_shake256_squeeze(&xof, buf, sizeof(buf));

    /* Unpack 18 bits per coefficient */
    int bit_pos = 0;
    for (int i = 0; i < N; i++) {
        int byte_idx = bit_pos / 8;
        int bit_off  = bit_pos % 8;
        uint32_t raw = (uint32_t)buf[byte_idx];
        if (byte_idx + 1 < (int)sizeof(buf))
            raw |= (uint32_t)buf[byte_idx + 1] << 8;
        if (byte_idx + 2 < (int)sizeof(buf))
            raw |= (uint32_t)buf[byte_idx + 2] << 16;
        raw = (raw >> bit_off) & 0x3FFFF;
        out[i] = fe_mod((int64_t)GAMMA1 - (int64_t)raw);
        bit_pos += 18;
    }
    neverc_platform_secure_zero(buf, sizeof(buf));
    neverc_platform_secure_zero(&xof, sizeof(xof));
}

/* ── Encoding / Decoding ─────────────────────────────────── */

/* Pack t1 coefficients (10 bits each) */
static void pack_t1(uint8_t *out, const poly_t t1) {
    for (int i = 0; i < N; i += 4) {
        uint32_t v = (uint32_t)t1[i] | ((uint32_t)t1[i+1] << 10) |
                     ((uint32_t)t1[i+2] << 20) | ((uint32_t)t1[i+3] << 30);
        out[0] = (uint8_t)v;
        out[1] = (uint8_t)(v >> 8);
        out[2] = (uint8_t)(v >> 16);
        out[3] = (uint8_t)(v >> 24);
        out[4] = (uint8_t)(t1[i+3] >> 2);
        out += 5;
    }
}

static void unpack_t1(poly_t t1, const uint8_t *in) {
    for (int i = 0; i < N; i += 4) {
        uint64_t v = (uint64_t)in[0] | ((uint64_t)in[1] << 8) |
                     ((uint64_t)in[2] << 16) | ((uint64_t)in[3] << 24) |
                     ((uint64_t)in[4] << 32);
        t1[i]   = (fe_t)(v & 0x3FF);
        t1[i+1] = (fe_t)((v >> 10) & 0x3FF);
        t1[i+2] = (fe_t)((v >> 20) & 0x3FF);
        t1[i+3] = (fe_t)((v >> 30) & 0x3FF);
        in += 5;
    }
}

/* Pack z coefficients (γ1=2^17, 18 bits each, stored as gamma1-z) */
static void pack_z(uint8_t *out, const poly_t z) {
    memset(out, 0, N * 18 / 8);
    int bit_pos = 0;
    for (int i = 0; i < N; i++) {
        int32_t v = z[i];
        if (v > (int32_t)(Q / 2)) v -= Q;
        uint32_t enc = (uint32_t)(GAMMA1 - v) & 0x3FFFF;
        int byte_idx = bit_pos / 8;
        int bit_off  = bit_pos % 8;
        out[byte_idx]   |= (uint8_t)(enc << bit_off);
        out[byte_idx+1] |= (uint8_t)(enc >> (8 - bit_off));
        if (bit_off + 18 > 16)
            out[byte_idx+2] |= (uint8_t)(enc >> (16 - bit_off));
        bit_pos += 18;
    }
}

static void unpack_z(poly_t z, const uint8_t *in) {
    int bit_pos = 0;
    for (int i = 0; i < N; i++) {
        int byte_idx = bit_pos / 8;
        int bit_off  = bit_pos % 8;
        uint32_t raw = (uint32_t)in[byte_idx] |
                       ((uint32_t)in[byte_idx+1] << 8) |
                       ((uint32_t)in[byte_idx+2] << 16);
        raw = (raw >> bit_off) & 0x3FFFF;
        z[i] = fe_mod((int64_t)GAMMA1 - (int64_t)raw);
        bit_pos += 18;
    }
}

/* ML-DSA-44 has 0 <= w1 <= 43, so SimpleBitPack uses six bits each. */
static void pack_w1(uint8_t out[W1_POLY_SIZE], const poly_t w1) {
    for (int i = 0; i < N; i += 4) {
        uint32_t a0 = (uint32_t)w1[i];
        uint32_t a1 = (uint32_t)w1[i + 1];
        uint32_t a2 = (uint32_t)w1[i + 2];
        uint32_t a3 = (uint32_t)w1[i + 3];
        out[0] = (uint8_t)(a0 | (a1 << 6));
        out[1] = (uint8_t)((a1 >> 2) | (a2 << 4));
        out[2] = (uint8_t)((a2 >> 4) | (a3 << 2));
        out += 3;
    }
}

/* ── Infinity norm check ─────────────────────────────────── */

static int check_norm(const poly_t p, int32_t bound) {
    for (int i = 0; i < N; i++) {
        int32_t v = p[i];
        if (v > Q / 2) v -= Q;
        if (v < 0) v = -v;
        if (v >= bound) return 0;
    }
    return 1;
}

/* ── Static work arrays ──────────────────────────────────── */

static poly_t g_dsa_a[K * L];  /* NTT(A) matrix */
static poly_t g_dsa_s1[L], g_dsa_s2[K];
static poly_t g_dsa_t[K], g_dsa_t1[K], g_dsa_t0[K];
static poly_t g_dsa_tmp, g_dsa_tmp2;

/* ── KeyGen (FIPS 204, Algorithm 1) ──────────────────────── */

static void mldsa44_keygen_from_seed(const uint8_t seed[32],
                                      uint8_t *pk_out,
                                      poly_t s1_ntt[], poly_t s2_ntt[],
                                      poly_t t0_out[]) {
    /* H(seed || k || l) → (ρ, ρ', K), FIPS 204 Algorithm 6. */
    uint8_t expanded[128];
    neverc_sha3_ctx h;
    neverc_shake256_init(&h);
    neverc_shake256_update(&h, seed, 32);
    uint8_t dimensions[2] = {K, L};
    neverc_shake256_update(&h, dimensions, sizeof(dimensions));
    neverc_shake256_squeeze(&h, expanded, 128);

    const uint8_t *rho = expanded;        /* 32 bytes */
    const uint8_t *sigma = expanded + 32; /* 64 bytes */

    /* Generate A from rho */
    for (int i = 0; i < K; i++)
        for (int j = 0; j < L; j++)
            expand_a_row(rho, i, j, g_dsa_a[i * L + j]);

    /* NTT(A) - A is already sampled in NTT domain */

    /* Generate s1, s2 from sigma */
    uint16_t counter = 0;
    for (int i = 0; i < L; i++)
        sample_eta2(sigma, 64, counter++, g_dsa_s1[i]);
    for (int i = 0; i < K; i++)
        sample_eta2(sigma, 64, counter++, g_dsa_s2[i]);

    /* NTT(s1) */
    poly_t s1_hat[L];
    for (int i = 0; i < L; i++)
        ntt_forward(g_dsa_s1[i], s1_hat[i]);

    /* t = A * s1 + s2 */
    for (int i = 0; i < K; i++) {
        memset(g_dsa_tmp, 0, sizeof(poly_t));
        for (int j = 0; j < L; j++) {
            ntt_pointwise(g_dsa_a[i * L + j], s1_hat[j], g_dsa_tmp2);
            poly_add(g_dsa_tmp, g_dsa_tmp2, g_dsa_tmp);
        }
        ntt_inverse(g_dsa_tmp, g_dsa_t[i]);
        poly_add(g_dsa_t[i], g_dsa_s2[i], g_dsa_t[i]);
    }

    /* Power2Round(t) → (t1, t0) */
    for (int i = 0; i < K; i++) {
        for (int j = 0; j < N; j++)
            power2round(g_dsa_t[i][j], &g_dsa_t1[i][j], &g_dsa_t0[i][j]);
    }

    /* Encode pk = rho || t1 */
    if (pk_out) {
        memcpy(pk_out, rho, 32);
        for (int i = 0; i < K; i++)
            pack_t1(pk_out + 32 + i * (N * 10 / 8), g_dsa_t1[i]);
    }

    /* Output NTT(s1), NTT(s2), t0 for signing */
    if (s1_ntt) {
        for (int i = 0; i < L; i++)
            memcpy(s1_ntt[i], s1_hat[i], sizeof(poly_t));
    }
    if (s2_ntt) {
        for (int i = 0; i < K; i++)
            ntt_forward(g_dsa_s2[i], s2_ntt[i]);
    }
    if (t0_out) {
        for (int i = 0; i < K; i++)
            memcpy(t0_out[i], g_dsa_t0[i], sizeof(poly_t));
    }
    neverc_platform_secure_zero(expanded, sizeof(expanded));
    neverc_platform_secure_zero(&h, sizeof(h));
    neverc_platform_secure_zero(s1_hat, sizeof(s1_hat));
}

/* ── Sign (FIPS 204, Algorithm 2) — rejection sampling ──── */

static poly_t g_sign_s1_hat[L], g_sign_s2_hat[K], g_sign_t0[K];
static poly_t g_sign_y[L], g_sign_w[K], g_sign_cs1[L], g_sign_cs2[K];
static poly_t g_sign_z[L], g_sign_r0[K];
static poly_t g_sign_ct0[K], g_sign_hint[K];
static int32_t g_mldsa_lock;

static void mldsa_lock(void) {
    while (!NEVERC_ATOMIC_CAS32(&g_mldsa_lock, 0, 1)) {
    }
}

static void mldsa_unlock(void) {
    NEVERC_ATOMIC_STORE32(&g_mldsa_lock, 0);
}

static void wipe_mldsa_scratch(void) {
    neverc_platform_secure_zero(g_dsa_a, sizeof(g_dsa_a));
    neverc_platform_secure_zero(g_dsa_s1, sizeof(g_dsa_s1));
    neverc_platform_secure_zero(g_dsa_s2, sizeof(g_dsa_s2));
    neverc_platform_secure_zero(g_dsa_t, sizeof(g_dsa_t));
    neverc_platform_secure_zero(g_dsa_t1, sizeof(g_dsa_t1));
    neverc_platform_secure_zero(g_dsa_t0, sizeof(g_dsa_t0));
    neverc_platform_secure_zero(g_dsa_tmp, sizeof(g_dsa_tmp));
    neverc_platform_secure_zero(g_dsa_tmp2, sizeof(g_dsa_tmp2));
    neverc_platform_secure_zero(g_sign_s1_hat, sizeof(g_sign_s1_hat));
    neverc_platform_secure_zero(g_sign_s2_hat, sizeof(g_sign_s2_hat));
    neverc_platform_secure_zero(g_sign_t0, sizeof(g_sign_t0));
    neverc_platform_secure_zero(g_sign_y, sizeof(g_sign_y));
    neverc_platform_secure_zero(g_sign_w, sizeof(g_sign_w));
    neverc_platform_secure_zero(g_sign_cs1, sizeof(g_sign_cs1));
    neverc_platform_secure_zero(g_sign_cs2, sizeof(g_sign_cs2));
    neverc_platform_secure_zero(g_sign_z, sizeof(g_sign_z));
    neverc_platform_secure_zero(g_sign_r0, sizeof(g_sign_r0));
    neverc_platform_secure_zero(g_sign_ct0, sizeof(g_sign_ct0));
    neverc_platform_secure_zero(g_sign_hint, sizeof(g_sign_hint));
}

static int mldsa44_sign_internal(const uint8_t seed[32],
                                  const uint8_t *pk, size_t pk_len,
                                  const uint8_t *msg, size_t msg_len,
                                  uint8_t *sig_out) {
    /* Regenerate keys from seed */
    mldsa44_keygen_from_seed(seed, NULL,
                              g_sign_s1_hat, g_sign_s2_hat, g_sign_t0);

    /* Compute tr = H(pk) */
    uint8_t tr[64];
    neverc_sha3_ctx hctx;
    neverc_shake256_init(&hctx);
    neverc_shake256_update(&hctx, pk, pk_len);
    neverc_shake256_squeeze(&hctx, tr, 64);

    /* mu = H(tr || msg) */
    uint8_t mu[64];
    neverc_shake256_init(&hctx);
    neverc_shake256_update(&hctx, tr, 64);
    const uint8_t pure_mldsa_domain[2] = {0, 0}; /* mode, empty context */
    neverc_shake256_update(
        &hctx, pure_mldsa_domain, sizeof(pure_mldsa_domain));
    neverc_shake256_update(&hctx, msg, msg_len);
    neverc_shake256_squeeze(&hctx, mu, 64);

    /* rho' = H(K || mu) using the key material */
    uint8_t rho_prime[64];
    uint8_t expanded[128];
    neverc_shake256_init(&hctx);
    neverc_shake256_update(&hctx, seed, 32);
    uint8_t dimensions[2] = {K, L};
    neverc_shake256_update(&hctx, dimensions, sizeof(dimensions));
    neverc_shake256_squeeze(&hctx, expanded, 128);
    const uint8_t *K_key = expanded + 96; /* last 32 bytes */

    neverc_shake256_init(&hctx);
    neverc_shake256_update(&hctx, K_key, 32);
    const uint8_t deterministic_rnd[32] = {0};
    neverc_shake256_update(
        &hctx, deterministic_rnd, sizeof(deterministic_rnd));
    neverc_shake256_update(&hctx, mu, 64);
    neverc_shake256_squeeze(&hctx, rho_prime, 64);

    /* Re-expand A */
    const uint8_t *rho = pk; /* first 32 bytes of pk */
    for (int i = 0; i < K; i++)
        for (int j = 0; j < L; j++)
            expand_a_row(rho, i, j, g_dsa_a[i * L + j]);

    /* NTT(t0) */
    poly_t t0_hat[K];
    for (int i = 0; i < K; i++)
        ntt_forward(g_sign_t0[i], t0_hat[i]);

    /* Rejection sampling loop */
    poly_t y_hat[L], w1[K], c_poly, c_hat;
    uint8_t c_tilde[32];
    uint8_t encoded_w1[W1_POLY_SIZE];
    int result = -1;
    uint16_t kappa = 0;
    for (int attempt = 0; attempt < 1000; attempt++, kappa += L) {
        /* y = ExpandMask(ρ', κ) */
        for (int i = 0; i < L; i++)
            expand_mask(rho_prime, 64, kappa + (uint16_t)i, g_sign_y[i]);

        /* w = A * NTT(y) */
        for (int i = 0; i < L; i++)
            ntt_forward(g_sign_y[i], y_hat[i]);

        for (int i = 0; i < K; i++) {
            memset(g_dsa_tmp, 0, sizeof(poly_t));
            for (int j = 0; j < L; j++) {
                ntt_pointwise(g_dsa_a[i * L + j], y_hat[j], g_dsa_tmp2);
                poly_add(g_dsa_tmp, g_dsa_tmp2, g_dsa_tmp);
            }
            ntt_inverse(g_dsa_tmp, g_sign_w[i]);
        }

        /* w1 = HighBits(w) */
        for (int i = 0; i < K; i++)
            for (int j = 0; j < N; j++)
                w1[i][j] = high_bits(g_sign_w[i][j]);

        /* Compute challenge hash: c_tilde = H(mu || w1_packed) */
        neverc_shake256_init(&hctx);
        neverc_shake256_update(&hctx, mu, 64);
        for (int i = 0; i < K; i++) {
            pack_w1(encoded_w1, w1[i]);
            neverc_shake256_update(
                &hctx, encoded_w1, sizeof(encoded_w1));
        }
        neverc_shake256_squeeze(&hctx, c_tilde, 32);

        /* c = SampleInBall(c_tilde) */
        sample_in_ball(c_tilde, 32, c_poly);

        /* c_hat = NTT(c) */
        ntt_forward(c_poly, c_hat);

        /* z = y + c * s1 */
        for (int i = 0; i < L; i++) {
            ntt_pointwise(c_hat, g_sign_s1_hat[i], g_dsa_tmp);
            ntt_inverse(g_dsa_tmp, g_sign_cs1[i]);
            poly_add(g_sign_y[i], g_sign_cs1[i], g_sign_z[i]);
        }

        /* Check ||z||_inf < γ1 - β */
        int32_t beta = TAU * ETA;
        int z_ok = 1;
        for (int i = 0; i < L; i++)
            if (!check_norm(g_sign_z[i], GAMMA1 - beta)) { z_ok = 0; break; }
        if (!z_ok) continue;

        /* r0 = LowBits(w - c*s2) */
        for (int i = 0; i < K; i++) {
            ntt_pointwise(c_hat, g_sign_s2_hat[i], g_dsa_tmp);
            ntt_inverse(g_dsa_tmp, g_sign_cs2[i]);
            poly_sub(g_sign_w[i], g_sign_cs2[i], g_dsa_tmp);
            for (int j = 0; j < N; j++)
                g_sign_r0[i][j] = low_bits(g_dsa_tmp[j]);
        }

        int r0_ok = 1;
        for (int i = 0; i < K; i++)
            if (!check_norm(g_sign_r0[i], GAMMA2 - beta)) { r0_ok = 0; break; }
        if (!r0_ok) continue;

        /* ct0 = c * t0 */
        for (int i = 0; i < K; i++) {
            ntt_pointwise(c_hat, t0_hat[i], g_dsa_tmp);
            ntt_inverse(g_dsa_tmp, g_sign_ct0[i]);
        }

        int ct0_ok = 1;
        for (int i = 0; i < K; i++)
            if (!check_norm(g_sign_ct0[i], GAMMA2)) { ct0_ok = 0; break; }
        if (!ct0_ok) continue;

        /* Compute hints */
        int hint_count = 0;
        for (int i = 0; i < K; i++) {
            for (int j = 0; j < N; j++) {
                fe_t wcs2 = fe_sub(g_sign_w[i][j], g_sign_cs2[i][j]);
                g_sign_hint[i][j] = make_hint(g_sign_ct0[i][j], wcs2);
                hint_count += g_sign_hint[i][j];
            }
        }
        if (hint_count > OMEGA) continue;

        /* Encode signature: c_tilde || z || h */
        memcpy(sig_out, c_tilde, LAMBDA / 4);
        uint8_t *zp = sig_out + LAMBDA / 4;
        for (int i = 0; i < L; i++) {
            pack_z(zp, g_sign_z[i]);
            zp += N * 18 / 8;
        }

        /* Pack hints */
        uint8_t *hp = zp;
        memset(hp, 0, OMEGA + K);
        int idx = 0;
        for (int i = 0; i < K; i++) {
            for (int j = 0; j < N; j++) {
                if (g_sign_hint[i][j]) {
                    if (idx >= OMEGA) continue;
                    hp[idx++] = (uint8_t)j;
                }
            }
            hp[OMEGA + i] = (uint8_t)idx;
        }

        result = 0;
        break;
    }

    neverc_platform_secure_zero(tr, sizeof(tr));
    neverc_platform_secure_zero(mu, sizeof(mu));
    neverc_platform_secure_zero(rho_prime, sizeof(rho_prime));
    neverc_platform_secure_zero(expanded, sizeof(expanded));
    neverc_platform_secure_zero(&hctx, sizeof(hctx));
    neverc_platform_secure_zero(t0_hat, sizeof(t0_hat));
    neverc_platform_secure_zero(y_hat, sizeof(y_hat));
    neverc_platform_secure_zero(w1, sizeof(w1));
    neverc_platform_secure_zero(c_poly, sizeof(c_poly));
    neverc_platform_secure_zero(c_hat, sizeof(c_hat));
    neverc_platform_secure_zero(c_tilde, sizeof(c_tilde));
    neverc_platform_secure_zero(encoded_w1, sizeof(encoded_w1));
    return result;
}

/* ── Verify (FIPS 204, Algorithm 3) ──────────────────────── */

static int mldsa44_verify_internal(const uint8_t *pk, size_t pk_len,
                                    const uint8_t *msg, size_t msg_len,
                                    const uint8_t *sig) {
    const uint8_t *rho = pk;
    poly_t t1[K];
    for (int i = 0; i < K; i++)
        unpack_t1(t1[i], pk + 32 + i * (N * 10 / 8));

    /* Parse signature */
    const uint8_t *c_tilde = sig;
    const uint8_t *z_bytes = sig + LAMBDA / 4;
    const uint8_t *h_bytes = z_bytes + L * (N * 18 / 8);

    poly_t z[L];
    for (int i = 0; i < L; i++)
        unpack_z(z[i], z_bytes + i * (N * 18 / 8));

    /* Check ||z||_inf < γ1 - β */
    int32_t beta = TAU * ETA;
    for (int i = 0; i < L; i++)
        if (!check_norm(z[i], GAMMA1 - beta)) return -1;

    /* Decode hints */
    poly_t hints[K];
    memset(hints, 0, sizeof(hints));
    int prev = 0;
    for (int i = 0; i < K; i++) {
        int limit = h_bytes[OMEGA + i];
        if (limit < prev || limit > OMEGA) return -1;
        for (int j = prev; j < limit; j++) {
            int idx = h_bytes[j];
            if (j > prev && idx <= h_bytes[j-1]) return -1;
            hints[i][idx] = 1;
        }
        prev = limit;
    }
    for (int i = prev; i < OMEGA; i++)
        if (h_bytes[i] != 0) return -1;

    /* tr = H(pk) */
    uint8_t tr[64];
    neverc_sha3_ctx hctx;
    neverc_shake256_init(&hctx);
    neverc_shake256_update(&hctx, pk, pk_len);
    neverc_shake256_squeeze(&hctx, tr, 64);

    /* mu = H(tr || msg) */
    uint8_t mu[64];
    neverc_shake256_init(&hctx);
    neverc_shake256_update(&hctx, tr, 64);
    const uint8_t pure_mldsa_domain[2] = {0, 0}; /* mode, empty context */
    neverc_shake256_update(
        &hctx, pure_mldsa_domain, sizeof(pure_mldsa_domain));
    neverc_shake256_update(&hctx, msg, msg_len);
    neverc_shake256_squeeze(&hctx, mu, 64);

    /* c = SampleInBall(c_tilde) */
    poly_t c_poly, c_hat;
    sample_in_ball(c_tilde, LAMBDA / 4, c_poly);
    ntt_forward(c_poly, c_hat);

    /* Re-expand A and compute: w' = A * NTT(z) - NTT(c) * NTT(t1 * 2^d) */
    for (int i = 0; i < K; i++)
        for (int j = 0; j < L; j++)
            expand_a_row(rho, i, j, g_dsa_a[i * L + j]);

    poly_t z_hat[L];
    for (int i = 0; i < L; i++)
        ntt_forward(z[i], z_hat[i]);

    poly_t w_prime[K];
    for (int i = 0; i < K; i++) {
        memset(g_dsa_tmp, 0, sizeof(poly_t));
        for (int j = 0; j < L; j++) {
            ntt_pointwise(g_dsa_a[i * L + j], z_hat[j], g_dsa_tmp2);
            poly_add(g_dsa_tmp, g_dsa_tmp2, g_dsa_tmp);
        }

        /* NTT(t1 * 2^d) */
        poly_t t1_scaled;
        for (int j = 0; j < N; j++)
            t1_scaled[j] = fe_mul(t1[i][j], 1 << D_BITS);
        poly_t t1_hat;
        ntt_forward(t1_scaled, t1_hat);

        ntt_pointwise(c_hat, t1_hat, g_dsa_tmp2);
        poly_sub(g_dsa_tmp, g_dsa_tmp2, g_dsa_tmp);
        ntt_inverse(g_dsa_tmp, w_prime[i]);
    }

    /* w1' = UseHint(h, w') */
    poly_t w1_prime[K];
    for (int i = 0; i < K; i++)
        for (int j = 0; j < N; j++)
            w1_prime[i][j] = use_hint(hints[i][j], w_prime[i][j]);

    /* Recompute c_tilde' = H(mu || w1') and compare */
    uint8_t c_tilde2[32];
    neverc_shake256_init(&hctx);
    neverc_shake256_update(&hctx, mu, 64);
    uint8_t encoded_w1[W1_POLY_SIZE];
    for (int i = 0; i < K; i++) {
        pack_w1(encoded_w1, w1_prime[i]);
        neverc_shake256_update(
            &hctx, encoded_w1, sizeof(encoded_w1));
    }
    neverc_shake256_squeeze(&hctx, c_tilde2, 32);

    /* subtle compare returns 1 when equal, 0 when different (not memcmp). */
    if (neverc_subtle_constant_time_compare(c_tilde, c_tilde2, LAMBDA / 4) == 0)
        return -1;
    return 0;
}

/* ── Public API ──────────────────────────────────────────── */

int neverc_mldsa44_generate_key(neverc_mldsa44_sk_t *sk) {
    if (!sk) return -1;
    memset(sk, 0, sizeof(*sk));
    if (neverc_crypto_rand_read(sk->seed, sizeof(sk->seed)) != 0) {
        neverc_platform_secure_zero(sk, sizeof(*sk));
        return -1;
    }
    mldsa_lock();
    mldsa44_keygen_from_seed(sk->seed, sk->pk, NULL, NULL, NULL);
    wipe_mldsa_scratch();
    mldsa_unlock();
    return 0;
}

int neverc_mldsa44_new_sk(neverc_mldsa44_sk_t *sk, const uint8_t seed[32]) {
    if (!sk || !seed) return -1;
    memmove(sk->seed, seed, 32);
    mldsa_lock();
    mldsa44_keygen_from_seed(sk->seed, sk->pk, NULL, NULL, NULL);
    wipe_mldsa_scratch();
    mldsa_unlock();
    return 0;
}

void neverc_mldsa44_sk_public_key(const neverc_mldsa44_sk_t *sk,
                                   neverc_mldsa44_pk_t *pk) {
    if (!sk || !pk) return;
    memcpy(pk->pk, sk->pk, NEVERC_MLDSA44_PK_SIZE);
}

int neverc_mldsa44_new_pk(neverc_mldsa44_pk_t *pk,
                           const uint8_t *encoded, size_t len) {
    if (!pk) return -1;
    if (!encoded || len != NEVERC_MLDSA44_PK_SIZE) {
        memset(pk, 0, sizeof(*pk));
        return -1;
    }
    memmove(pk->pk, encoded, len);
    return 0;
}

int neverc_mldsa44_sign(const neverc_mldsa44_sk_t *sk,
                         const uint8_t *message, size_t msg_len,
                         uint8_t sig[NEVERC_MLDSA44_SIG_SIZE]) {
    if (!sig) return -1;
    if (!sk || (!message && msg_len != 0)) {
        neverc_platform_secure_zero(sig, NEVERC_MLDSA44_SIG_SIZE);
        return -1;
    }
    mldsa_lock();
    int result = mldsa44_sign_internal(
        sk->seed, sk->pk, NEVERC_MLDSA44_PK_SIZE, message, msg_len, sig);
    wipe_mldsa_scratch();
    mldsa_unlock();
    if (result != 0)
        neverc_platform_secure_zero(sig, NEVERC_MLDSA44_SIG_SIZE);
    return result;
}

int neverc_mldsa44_verify(const neverc_mldsa44_pk_t *pk,
                           const uint8_t *message, size_t msg_len,
                           const uint8_t sig[NEVERC_MLDSA44_SIG_SIZE]) {
    if (!pk || (!message && msg_len != 0) || !sig) return -1;
    mldsa_lock();
    int result = mldsa44_verify_internal(
        pk->pk, NEVERC_MLDSA44_PK_SIZE, message, msg_len, sig);
    wipe_mldsa_scratch();
    mldsa_unlock();
    return result;
}

void neverc_mldsa44_sk_bytes(const neverc_mldsa44_sk_t *sk, uint8_t seed[32]) {
    if (!sk || !seed) return;
    memcpy(seed, sk->seed, 32);
}

void neverc_mldsa44_pk_bytes(const neverc_mldsa44_pk_t *pk,
                              uint8_t *out, size_t *out_len) {
    if (out_len) *out_len = 0;
    if (!pk || !out || !out_len) return;
    memcpy(out, pk->pk, NEVERC_MLDSA44_PK_SIZE);
    *out_len = NEVERC_MLDSA44_PK_SIZE;
}
