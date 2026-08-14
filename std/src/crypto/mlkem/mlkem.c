/*
 * ML-KEM — Module-Lattice-based Key Encapsulation Mechanism (NIST FIPS 203).
 *
 * Pure C implementation of ML-KEM-768 and ML-KEM-1024.
 * NTT domain arithmetic over Z_q where q = 3329, n = 256.
 *
 * References:
 *   [FIPS 203] https://doi.org/10.6028/NIST.FIPS.203
 *   [Go std]   crypto/internal/fips140/mlkem
 */
#include "neverc/std/crypto/mlkem.h"
#include "neverc/std/crypto/sha3.h"
#include "neverc/std/crypto/rand.h"
#include "neverc/std/_platform.h"
#include <string.h>

/* ── Constants ────────────────────────────────────────────── */

#define Q    3329
#define N    256
#define ENC12 (N * 3 / 2)  /* 384 bytes: 12-bit encoding of a polynomial */

/* Barrett reduction constant: ⌊2^24 / q⌋ */
#define BARRETT_MUL 5039
#define BARRETT_SHF 24

/* ── Field arithmetic mod q ──────────────────────────────── */

typedef uint16_t fe_t;

static fe_t fe_reduce_once(uint16_t a) {
    uint16_t x = a - Q;
    x += (x >> 15) * Q;
    return x;
}

static fe_t fe_reduce(uint32_t a) {
    uint32_t quotient = (uint32_t)(((uint64_t)a * BARRETT_MUL) >> BARRETT_SHF);
    return fe_reduce_once((uint16_t)(a - quotient * Q));
}

static fe_t fe_add(fe_t a, fe_t b) { return fe_reduce_once((uint16_t)(a + b)); }
static fe_t fe_sub(fe_t a, fe_t b) { return fe_reduce_once((uint16_t)(a - b + Q)); }
static fe_t fe_mul(fe_t a, fe_t b) { return fe_reduce((uint32_t)a * (uint32_t)b); }

static fe_t fe_mul_sub(fe_t a, fe_t b, fe_t c) {
    return fe_reduce((uint32_t)a * (uint32_t)(b - c + Q));
}

static fe_t fe_add_mul(fe_t a, fe_t b, fe_t c, fe_t d) {
    return fe_reduce((uint32_t)a * (uint32_t)b + (uint32_t)c * (uint32_t)d);
}

static uint16_t compress_fe(fe_t x, uint8_t d) {
    uint32_t dividend = (uint32_t)x << d;
    uint32_t quotient = (uint32_t)(((uint64_t)dividend * BARRETT_MUL) >> BARRETT_SHF);
    uint32_t remainder = dividend - quotient * Q;
    quotient += (Q / 2 - remainder) >> 31 & 1;
    quotient += (Q + Q / 2 - remainder) >> 31 & 1;
    return (uint16_t)(quotient & ((1u << d) - 1));
}

static fe_t decompress_fe(uint16_t y, uint8_t d) {
    uint32_t dividend = (uint32_t)y * Q;
    uint32_t quotient = dividend >> d;
    quotient += (dividend >> (d - 1)) & 1;
    return (fe_t)quotient;
}

/* ── Polynomial types ────────────────────────────────────── */

typedef fe_t ring_elem[N];
typedef fe_t ntt_elem[N];

/* ── NTT tables (FIPS 203 Appendix A) ────────────────────── */

static const fe_t zetas[128] = {
    1, 1729, 2580, 3289, 2642, 630, 1897, 848, 1062, 1919, 193, 797, 2786,
    3260, 569, 1746, 296, 2447, 1339, 1476, 3046, 56, 2240, 1333, 1426, 2094,
    535, 2882, 2393, 2879, 1974, 821, 289, 331, 3253, 1756, 1197, 2304, 2277,
    2055, 650, 1977, 2513, 632, 2865, 33, 1320, 1915, 2319, 1435, 807, 452,
    1438, 2868, 1534, 2402, 2647, 2617, 1481, 648, 2474, 3110, 1227, 910, 17,
    2761, 583, 2649, 1637, 723, 2288, 1100, 1409, 2662, 3281, 233, 756, 2156,
    3015, 3050, 1703, 1651, 2789, 1789, 1847, 952, 1461, 2687, 939, 2308,
    2437, 2388, 733, 2337, 268, 641, 1584, 2298, 2037, 3220, 375, 2549, 2090,
    1645, 1063, 319, 2773, 757, 2099, 561, 2466, 2594, 2804, 1092, 403, 1026,
    1143, 2150, 2775, 886, 1722, 1212, 1874, 1029, 2110, 2935, 885, 2154
};

static const fe_t gammas[128] = {
    17, 3312, 2761, 568, 583, 2746, 2649, 680, 1637, 1692, 723, 2606, 2288,
    1041, 1100, 2229, 1409, 1920, 2662, 667, 3281, 48, 233, 3096, 756, 2573,
    2156, 1173, 3015, 314, 3050, 279, 1703, 1626, 1651, 1678, 2789, 540, 1789,
    1540, 1847, 1482, 952, 2377, 1461, 1868, 2687, 642, 939, 2390, 2308, 1021,
    2437, 892, 2388, 941, 733, 2596, 2337, 992, 268, 3061, 641, 2688, 1584,
    1745, 2298, 1031, 2037, 1292, 3220, 109, 375, 2954, 2549, 780, 2090, 1239,
    1645, 1684, 1063, 2266, 319, 3010, 2773, 556, 757, 2572, 2099, 1230, 561,
    2768, 2466, 863, 2594, 735, 2804, 525, 1092, 2237, 403, 2926, 1026, 2303,
    1143, 2186, 2150, 1179, 2775, 554, 886, 2443, 1722, 1607, 1212, 2117,
    1874, 1455, 1029, 2300, 2110, 1219, 2935, 394, 885, 2444, 2154, 1175
};

/* ── NTT / inverse NTT (FIPS 203, Algorithms 9–10) ──────── */

static void ntt_forward(const ring_elem f_in, ntt_elem f_out) {
    memcpy(f_out, f_in, sizeof(ring_elem));
    int k = 1;
    for (int half = 128; half >= 2; half /= 2) {
        for (int start = 0; start < N; start += 2 * half) {
            fe_t zeta = zetas[k++];
            for (int j = 0; j < half; j++) {
                fe_t t = fe_mul(zeta, f_out[start + half + j]);
                f_out[start + half + j] = fe_sub(f_out[start + j], t);
                f_out[start + j] = fe_add(f_out[start + j], t);
            }
        }
    }
}

static void ntt_inverse(const ntt_elem f_in, ring_elem f_out) {
    memcpy(f_out, f_in, sizeof(ntt_elem));
    int k = 127;
    for (int half = 2; half <= 128; half *= 2) {
        for (int start = 0; start < N; start += 2 * half) {
            fe_t zeta = zetas[k--];
            for (int j = 0; j < half; j++) {
                fe_t t = f_out[start + j];
                f_out[start + j] = fe_add(t, f_out[start + half + j]);
                f_out[start + half + j] = fe_mul_sub(zeta, f_out[start + half + j], t);
            }
        }
    }
    for (int i = 0; i < N; i++)
        f_out[i] = fe_mul(f_out[i], 3303); /* 128^(-1) mod q */
}

/* NTT multiplication (FIPS 203, Algorithm 11) */
static void ntt_mul(const ntt_elem f, const ntt_elem g, ntt_elem h) {
    for (int i = 0; i < N; i += 2) {
        h[i]   = fe_add_mul(f[i], g[i], fe_mul(f[i+1], g[i+1]), gammas[i/2]);
        h[i+1] = fe_add_mul(f[i], g[i+1], f[i+1], g[i]);
    }
}

/* ── Encoding / Decoding ─────────────────────────────────── */

static void poly_byte_encode12(uint8_t *out, const fe_t *f) {
    for (int i = 0; i < N; i += 2) {
        uint32_t x = (uint32_t)f[i] | ((uint32_t)f[i+1] << 12);
        out[0] = (uint8_t)x;
        out[1] = (uint8_t)(x >> 8);
        out[2] = (uint8_t)(x >> 16);
        out += 3;
    }
}

static int poly_byte_decode12(fe_t *f, const uint8_t *b) {
    for (int i = 0; i < N; i += 2) {
        uint32_t d = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16);
        uint16_t d1 = d & 0xFFF;
        uint16_t d2 = d >> 12;
        if (d1 >= Q || d2 >= Q) return -1;
        f[i]   = d1;
        f[i+1] = d2;
        b += 3;
    }
    return 0;
}

static void ring_compress_encode1(uint8_t *out, const ring_elem f) {
    memset(out, 0, 32);
    for (int i = 0; i < N; i++)
        out[i / 8] |= (uint8_t)(compress_fe(f[i], 1) << (i % 8));
}

static void ring_decode_decompress1(ring_elem f, const uint8_t *b) {
    for (int i = 0; i < N; i++) {
        uint8_t bit = (b[i / 8] >> (i % 8)) & 1;
        f[i] = (fe_t)bit * ((Q + 1) / 2);
    }
}

static void ring_compress_encode4(uint8_t *out, const ring_elem f) {
    for (int i = 0; i < N; i += 2)
        out[i / 2] = (uint8_t)(compress_fe(f[i], 4) | (compress_fe(f[i+1], 4) << 4));
}

static void ring_decode_decompress4(ring_elem f, const uint8_t *b) {
    for (int i = 0; i < N; i += 2) {
        f[i]   = decompress_fe(b[i / 2] & 0xF, 4);
        f[i+1] = decompress_fe(b[i / 2] >> 4, 4);
    }
}

static void ring_compress_encode10(uint8_t *out, const ring_elem f) {
    for (int i = 0; i < N; i += 4) {
        uint64_t x = 0;
        x |= (uint64_t)compress_fe(f[i], 10);
        x |= (uint64_t)compress_fe(f[i+1], 10) << 10;
        x |= (uint64_t)compress_fe(f[i+2], 10) << 20;
        x |= (uint64_t)compress_fe(f[i+3], 10) << 30;
        out[0] = (uint8_t)x;
        out[1] = (uint8_t)(x >> 8);
        out[2] = (uint8_t)(x >> 16);
        out[3] = (uint8_t)(x >> 24);
        out[4] = (uint8_t)(x >> 32);
        out += 5;
    }
}

static void ring_decode_decompress10(ring_elem f, const uint8_t *b) {
    for (int i = 0; i < N; i += 4) {
        uint64_t x = (uint64_t)b[0] | ((uint64_t)b[1] << 8) |
                     ((uint64_t)b[2] << 16) | ((uint64_t)b[3] << 24) |
                     ((uint64_t)b[4] << 32);
        f[i]   = decompress_fe((uint16_t)(x & 0x3FF), 10);
        f[i+1] = decompress_fe((uint16_t)((x >> 10) & 0x3FF), 10);
        f[i+2] = decompress_fe((uint16_t)((x >> 20) & 0x3FF), 10);
        f[i+3] = decompress_fe((uint16_t)((x >> 30) & 0x3FF), 10);
        b += 5;
    }
}

/* ── Sampling ────────────────────────────────────────────── */

static void sample_ntt(const uint8_t rho[32], uint8_t ii, uint8_t jj,
                       ntt_elem out) {
    neverc_sha3_ctx xof;
    neverc_shake128_init(&xof);
    neverc_shake128_update(&xof, rho, 32);
    uint8_t ij[2] = {ii, jj};
    neverc_shake128_update(&xof, ij, 2);

    int j = 0;
    uint8_t buf[24];
    while (j < N) {
        neverc_shake128_squeeze(&xof, buf, 24);
        for (int off = 0; off + 2 < 24 && j < N; off += 3) {
            uint16_t d1 = ((uint16_t)buf[off] | ((uint16_t)buf[off+1] << 8)) & 0xFFF;
            uint16_t d2 = ((uint16_t)buf[off+1] | ((uint16_t)buf[off+2] << 8)) >> 4;
            if (d1 < Q) out[j++] = d1;
            if (j < N && d2 < Q) out[j++] = d2;
        }
    }
    neverc_platform_secure_zero(buf, sizeof(buf));
    neverc_platform_secure_zero(&xof, sizeof(xof));
}

/* SamplePolyCBD with eta=2 (FIPS 203, Algorithm 8) */
static void sample_poly_cbd(const uint8_t *sigma, size_t sigma_len,
                             uint8_t b_val, ring_elem out) {
    neverc_sha3_ctx prf;
    neverc_shake256_init(&prf);
    neverc_shake256_update(&prf, sigma, sigma_len);
    neverc_shake256_update(&prf, &b_val, 1);
    uint8_t B[128]; /* 64*2 for eta=2 */
    neverc_shake256_squeeze(&prf, B, 128);

    for (int i = 0; i < N; i += 2) {
        uint8_t byte = B[i / 2];
        uint8_t b0 = byte & 1, b1 = (byte >> 1) & 1;
        uint8_t b2 = (byte >> 2) & 1, b3 = (byte >> 3) & 1;
        uint8_t b4 = (byte >> 4) & 1, b5 = (byte >> 5) & 1;
        uint8_t b6 = (byte >> 6) & 1, b7 = byte >> 7;
        out[i]   = fe_sub((fe_t)(b0 + b1), (fe_t)(b2 + b3));
        out[i+1] = fe_sub((fe_t)(b4 + b5), (fe_t)(b6 + b7));
    }
    neverc_platform_secure_zero(B, sizeof(B));
    neverc_platform_secure_zero(&prf, sizeof(prf));
}

/* ── Poly arithmetic helpers ─────────────────────────────── */

static void poly_add(const fe_t *a, const fe_t *b, fe_t *c) {
    for (int i = 0; i < N; i++) c[i] = fe_add(a[i], b[i]);
}

static void poly_sub(const fe_t *a, const fe_t *b, fe_t *c) {
    for (int i = 0; i < N; i++) c[i] = fe_sub(a[i], b[i]);
}

/* ── ML-KEM core (parameterized by k) ───────────────────── */

/* Use static arrays to avoid stack overflow on platforms with small stacks. */
static ntt_elem g_a_hat[16]; /* k*k matrix (max k=4) */
static ntt_elem g_e_hat[4];
static ntt_elem g_t_hat[4];
static ntt_elem g_prod;
static ntt_elem g_acc;
static ring_elem g_tmp_ring;

/* K-PKE.KeyGen (FIPS 203, Algorithm 13) */
static void kpke_keygen(int k, const uint8_t d[32],
                        uint8_t *ek_out,
                        ntt_elem s_hat[]) {
    uint8_t seeds[64];
    neverc_sha3_ctx g;
    neverc_sha3_512_init(&g);
    neverc_sha3_512_update(&g, d, 32);
    uint8_t kbyte = (uint8_t)k;
    neverc_sha3_512_update(&g, &kbyte, 1);
    neverc_sha3_512_final(&g, seeds);
    const uint8_t *rho = seeds;
    const uint8_t *sigma = seeds + 32;

    for (int i = 0; i < k; i++)
        for (int j = 0; j < k; j++)
            sample_ntt(rho, (uint8_t)j, (uint8_t)i, g_a_hat[i * k + j]);

    uint8_t counter = 0;
    for (int i = 0; i < k; i++) {
        sample_poly_cbd(sigma, 32, counter++, g_tmp_ring);
        ntt_forward(g_tmp_ring, s_hat[i]);
    }
    for (int i = 0; i < k; i++) {
        sample_poly_cbd(sigma, 32, counter++, g_tmp_ring);
        ntt_forward(g_tmp_ring, g_e_hat[i]);
    }

    for (int i = 0; i < k; i++) {
        memset(g_acc, 0, sizeof(g_acc));
        for (int j = 0; j < k; j++) {
            ntt_mul(g_a_hat[i * k + j], s_hat[j], g_prod);
            poly_add(g_acc, g_prod, g_acc);
        }
        poly_add(g_acc, g_e_hat[i], g_t_hat[i]);
    }

    for (int i = 0; i < k; i++)
        poly_byte_encode12(ek_out + i * ENC12, g_t_hat[i]);
    memcpy(ek_out + k * ENC12, rho, 32);
    neverc_platform_secure_zero(seeds, sizeof(seeds));
    neverc_platform_secure_zero(&g, sizeof(g));
}

/* Static arrays for encrypt/decrypt to avoid stack overflow */
static ntt_elem g_enc_t_hat[4];
static ntt_elem g_enc_r_hat[4];
static ring_elem g_enc_e1[4];
static ring_elem g_enc_e2;
static ring_elem g_enc_u[4];
static ring_elem g_enc_v;

/* K-PKE.Encrypt (FIPS 203, Algorithm 14) */
static void kpke_encrypt(int k,
                          const uint8_t *ek, size_t ek_len,
                          const uint8_t m[32],
                          const uint8_t *r, size_t r_len,
                          uint8_t *ct_out) {
    for (int i = 0; i < k; i++)
        poly_byte_decode12(g_enc_t_hat[i], ek + i * ENC12);
    const uint8_t *rho = ek + k * ENC12;

    for (int i = 0; i < k; i++)
        for (int j = 0; j < k; j++)
            sample_ntt(rho, (uint8_t)i, (uint8_t)j, g_a_hat[i * k + j]);

    uint8_t counter = 0;
    for (int i = 0; i < k; i++) {
        sample_poly_cbd(r, r_len, counter++, g_tmp_ring);
        ntt_forward(g_tmp_ring, g_enc_r_hat[i]);
    }
    for (int i = 0; i < k; i++)
        sample_poly_cbd(r, r_len, counter++, g_enc_e1[i]);
    sample_poly_cbd(r, r_len, counter, g_enc_e2);

    for (int i = 0; i < k; i++) {
        memset(g_acc, 0, sizeof(g_acc));
        for (int j = 0; j < k; j++) {
            ntt_mul(g_a_hat[i * k + j], g_enc_r_hat[j], g_prod);
            poly_add(g_acc, g_prod, g_acc);
        }
        ntt_inverse(g_acc, g_tmp_ring);
        poly_add(g_tmp_ring, g_enc_e1[i], g_enc_u[i]);
    }

    memset(g_acc, 0, sizeof(g_acc));
    for (int i = 0; i < k; i++) {
        ntt_mul(g_enc_t_hat[i], g_enc_r_hat[i], g_prod);
        poly_add(g_acc, g_prod, g_acc);
    }
    ntt_inverse(g_acc, g_enc_v);
    poly_add(g_enc_v, g_enc_e2, g_enc_v);

    ring_elem m_poly;
    ring_decode_decompress1(m_poly, m);
    poly_add(g_enc_v, m_poly, g_enc_v);

    int du = (k == 3) ? 10 : 11;
    int dv = (k == 3) ? 4 : 5;
    int u_enc_size = (k == 3) ? 320 : 352;

    uint8_t *ptr = ct_out;
    for (int i = 0; i < k; i++) {
        if (du == 10)
            ring_compress_encode10(ptr, g_enc_u[i]);
        else {
            uint8_t bit_buf = 0, bit_idx = 0;
            int out_idx = 0;
            for (int c = 0; c < N; c++) {
                uint16_t cv = compress_fe(g_enc_u[i][c], (uint8_t)du);
                for (uint8_t ci = 0; ci < (uint8_t)du; ci++) {
                    bit_buf |= (uint8_t)((cv >> ci) & 1) << bit_idx;
                    if (++bit_idx == 8) { ptr[out_idx++] = bit_buf; bit_buf = 0; bit_idx = 0; }
                }
            }
        }
        ptr += u_enc_size;
    }
    if (dv == 4)
        ring_compress_encode4(ptr, g_enc_v);
    else {
        uint8_t bit_buf = 0, bit_idx = 0;
        int out_idx = 0;
        for (int c = 0; c < N; c++) {
            uint16_t cv = compress_fe(g_enc_v[c], (uint8_t)dv);
            for (uint8_t ci = 0; ci < (uint8_t)dv; ci++) {
                bit_buf |= (uint8_t)((cv >> ci) & 1) << bit_idx;
                if (++bit_idx == 8) { ptr[out_idx++] = bit_buf; bit_buf = 0; bit_idx = 0; }
            }
        }
    }
    neverc_platform_secure_zero(m_poly, sizeof(m_poly));
}

/* Static arrays for decrypt */
static ring_elem g_dec_u[4];
static ring_elem g_dec_v;

/* K-PKE.Decrypt (FIPS 203, Algorithm 15) */
static void kpke_decrypt(int k,
                          const ntt_elem s_hat[],
                          const uint8_t *ct, size_t ct_len,
                          uint8_t m[32]) {
    int du = (k == 3) ? 10 : 11;
    int dv = (k == 3) ? 4 : 5;
    int u_enc_size = (k == 3) ? 320 : 352;

    const uint8_t *ptr = ct;
    for (int i = 0; i < k; i++) {
        if (du == 10)
            ring_decode_decompress10(g_dec_u[i], ptr);
        else {
            uint8_t bit_idx = 0;
            const uint8_t *src = ptr;
            for (int c = 0; c < N; c++) {
                uint16_t cv = 0;
                for (uint8_t ci = 0; ci < (uint8_t)du; ci++) {
                    cv |= (uint16_t)((src[0] >> bit_idx) & 1) << ci;
                    bit_idx++;
                    if (bit_idx == 8) { src++; bit_idx = 0; }
                }
                g_dec_u[i][c] = decompress_fe(cv, (uint8_t)du);
            }
        }
        ptr += u_enc_size;
    }

    if (dv == 4)
        ring_decode_decompress4(g_dec_v, ptr);
    else {
        uint8_t bit_idx = 0;
        const uint8_t *src = ptr;
        for (int c = 0; c < N; c++) {
            uint16_t cv = 0;
            for (uint8_t ci = 0; ci < (uint8_t)dv; ci++) {
                cv |= (uint16_t)((src[0] >> bit_idx) & 1) << ci;
                bit_idx++;
                if (bit_idx == 8) { src++; bit_idx = 0; }
            }
            g_dec_v[c] = decompress_fe(cv, (uint8_t)dv);
        }
    }

    memset(g_acc, 0, sizeof(g_acc));
    for (int i = 0; i < k; i++) {
        ntt_forward(g_dec_u[i], g_prod);
        ntt_mul(s_hat[i], g_prod, g_e_hat[0]);
        poly_add(g_acc, g_e_hat[0], g_acc);
    }

    ntt_inverse(g_acc, g_tmp_ring);
    poly_sub(g_dec_v, g_tmp_ring, g_tmp_ring);
    ring_compress_encode1(m, g_tmp_ring);
    (void)ct_len;
}

/* ── ML-KEM full scheme (FIPS 203, Algorithms 16–18) ──────── */

static ntt_elem g_s_hat[4]; /* secret key in NTT domain */

/* ML-KEM.Encaps (Algorithm 17) */
static int mlkem_encaps(int k, const uint8_t *ek, size_t ek_size,
                        uint8_t shared_key[32], uint8_t *ct_out) {
    for (int i = 0; i < k; i++)
        if (poly_byte_decode12(g_enc_t_hat[i], ek + i * ENC12) != 0)
            return -1;

    uint8_t m[32];
    if (neverc_crypto_rand_read(m, sizeof(m)) != 0) {
        neverc_platform_secure_zero(m, sizeof(m));
        return -1;
    }

    uint8_t ek_hash[32];
    neverc_sha3_256_sum(ek, ek_size, ek_hash);

    uint8_t kr[64];
    neverc_sha3_ctx g;
    neverc_sha3_512_init(&g);
    neverc_sha3_512_update(&g, m, 32);
    neverc_sha3_512_update(&g, ek_hash, 32);
    neverc_sha3_512_final(&g, kr);

    memcpy(shared_key, kr, 32);
    kpke_encrypt(k, ek, ek_size, m, kr + 32, 32, ct_out);
    neverc_platform_secure_zero(m, sizeof(m));
    neverc_platform_secure_zero(kr, sizeof(kr));
    neverc_platform_secure_zero(&g, sizeof(g));
    return 0;
}

static uint8_t g_ek_regen[1600];
static uint8_t g_ct_prime[1600];
static int32_t g_mlkem_lock;

static void mlkem_lock(void) {
    while (!NEVERC_ATOMIC_CAS32(&g_mlkem_lock, 0, 1)) {
    }
}

static void mlkem_unlock(void) {
    NEVERC_ATOMIC_STORE32(&g_mlkem_lock, 0);
}

static void wipe_mlkem_scratch(void) {
    neverc_platform_secure_zero(g_a_hat, sizeof(g_a_hat));
    neverc_platform_secure_zero(g_e_hat, sizeof(g_e_hat));
    neverc_platform_secure_zero(g_t_hat, sizeof(g_t_hat));
    neverc_platform_secure_zero(g_prod, sizeof(g_prod));
    neverc_platform_secure_zero(g_acc, sizeof(g_acc));
    neverc_platform_secure_zero(g_tmp_ring, sizeof(g_tmp_ring));
    neverc_platform_secure_zero(g_enc_t_hat, sizeof(g_enc_t_hat));
    neverc_platform_secure_zero(g_enc_r_hat, sizeof(g_enc_r_hat));
    neverc_platform_secure_zero(g_enc_e1, sizeof(g_enc_e1));
    neverc_platform_secure_zero(g_enc_e2, sizeof(g_enc_e2));
    neverc_platform_secure_zero(g_enc_u, sizeof(g_enc_u));
    neverc_platform_secure_zero(g_enc_v, sizeof(g_enc_v));
    neverc_platform_secure_zero(g_dec_u, sizeof(g_dec_u));
    neverc_platform_secure_zero(g_dec_v, sizeof(g_dec_v));
    neverc_platform_secure_zero(g_s_hat, sizeof(g_s_hat));
    neverc_platform_secure_zero(g_ek_regen, sizeof(g_ek_regen));
    neverc_platform_secure_zero(g_ct_prime, sizeof(g_ct_prime));
}

/* ML-KEM.Decaps (Algorithm 18) */
static int mlkem_decaps(int k, const uint8_t *seed,
                        const uint8_t *ek, size_t ek_size,
                        const uint8_t *ct, size_t ct_size,
                        uint8_t shared_key[32]) {
    const uint8_t *d = seed;
    const uint8_t *z = seed + 32;

    kpke_keygen(k, d, g_ek_regen, g_s_hat);

    uint8_t m_prime[32];
    kpke_decrypt(k, g_s_hat, ct, ct_size, m_prime);

    uint8_t ek_hash[32];
    neverc_sha3_256_sum(ek, ek_size, ek_hash);

    uint8_t kr[64];
    neverc_sha3_ctx g;
    neverc_sha3_512_init(&g);
    neverc_sha3_512_update(&g, m_prime, 32);
    neverc_sha3_512_update(&g, ek_hash, 32);
    neverc_sha3_512_final(&g, kr);

    kpke_encrypt(k, ek, ek_size, m_prime, kr + 32, 32, g_ct_prime);

    uint8_t difference = 0;
    for (size_t i = 0; i < ct_size; i++)
        difference |= ct[i] ^ g_ct_prime[i];

    uint8_t rejection_key[32];
    neverc_sha3_ctx j;
    neverc_shake256_init(&j);
    neverc_shake256_update(&j, z, 32);
    neverc_shake256_update(&j, ct, ct_size);
    neverc_shake256_squeeze(&j, rejection_key, sizeof(rejection_key));

    /* difference == 0 must select all bytes from K; any mismatch selects J.
     * The shift already yields 0xff/0x00, so negating it would turn the
     * successful 0xff mask into 0x01 and corrupt 31/32 bits of every byte. */
    uint8_t match_mask =
        (uint8_t)(((uint16_t)difference - 1U) >> 8);
    for (size_t i = 0; i < 32; i++) {
        shared_key[i] = (uint8_t)((kr[i] & match_mask) |
                                  (rejection_key[i] & (uint8_t)~match_mask));
    }
    neverc_platform_secure_zero(m_prime, sizeof(m_prime));
    neverc_platform_secure_zero(kr, sizeof(kr));
    neverc_platform_secure_zero(rejection_key, sizeof(rejection_key));
    neverc_platform_secure_zero(&g, sizeof(g));
    neverc_platform_secure_zero(&j, sizeof(j));
    return 0;
}

static int validate_encapsulation_key(
    int k, const uint8_t *encoded, size_t encoded_size) {
    if (!encoded || encoded_size != (size_t)k * ENC12 + 32)
        return -1;
    int all_zero = 1;
    for (size_t i = 0; i < encoded_size; i++) {
        if (encoded[i] != 0) {
            all_zero = 0;
            break;
        }
    }
    if (all_zero)
        return -1;
    ntt_elem decoded;
    for (int i = 0; i < k; i++) {
        if (poly_byte_decode12(decoded, encoded + i * ENC12) != 0) {
            neverc_platform_secure_zero(decoded, sizeof(decoded));
            return -1;
        }
    }
    neverc_platform_secure_zero(decoded, sizeof(decoded));
    return 0;
}

/* ── Public API: ML-KEM-768 ──────────────────────────────── */

int neverc_mlkem768_generate_key(neverc_mlkem768_dk_t *dk) {
    if (!dk) return -1;
    memset(dk, 0, sizeof(*dk));
    if (neverc_crypto_rand_read(dk->seed, sizeof(dk->seed)) != 0) {
        neverc_platform_secure_zero(dk, sizeof(*dk));
        return -1;
    }
    mlkem_lock();
    kpke_keygen(3, dk->seed, dk->ek, g_s_hat);
    wipe_mlkem_scratch();
    mlkem_unlock();
    return 0;
}

int neverc_mlkem768_new_dk(neverc_mlkem768_dk_t *dk, const uint8_t seed[64]) {
    if (!dk || !seed) return -1;
    memmove(dk->seed, seed, 64);
    mlkem_lock();
    kpke_keygen(3, dk->seed, dk->ek, g_s_hat);
    wipe_mlkem_scratch();
    mlkem_unlock();
    return 0;
}

void neverc_mlkem768_dk_encapsulation_key(const neverc_mlkem768_dk_t *dk,
                                          neverc_mlkem768_ek_t *ek) {
    if (!dk || !ek) return;
    memcpy(ek->ek, dk->ek, NEVERC_MLKEM768_EK_SIZE);
}

int neverc_mlkem768_new_ek(neverc_mlkem768_ek_t *ek,
                           const uint8_t *encoded, size_t len) {
    if (!ek) return -1;
    if (validate_encapsulation_key(3, encoded, len) != 0) {
        memset(ek, 0, sizeof(*ek));
        return -1;
    }
    memmove(ek->ek, encoded, len);
    return 0;
}

int neverc_mlkem768_encapsulate(const neverc_mlkem768_ek_t *ek,
                                uint8_t shared_key[32],
                                uint8_t ciphertext[NEVERC_MLKEM768_CT_SIZE]) {
    if (!ek || !shared_key || !ciphertext) return -1;
    if (validate_encapsulation_key(
            3, ek->ek, NEVERC_MLKEM768_EK_SIZE) != 0) {
        neverc_platform_secure_zero(shared_key, 32);
        neverc_platform_secure_zero(ciphertext, NEVERC_MLKEM768_CT_SIZE);
        return -1;
    }
    mlkem_lock();
    int result = mlkem_encaps(3, ek->ek, NEVERC_MLKEM768_EK_SIZE,
                              shared_key, ciphertext);
    wipe_mlkem_scratch();
    mlkem_unlock();
    if (result != 0) {
        neverc_platform_secure_zero(shared_key, 32);
        neverc_platform_secure_zero(ciphertext, NEVERC_MLKEM768_CT_SIZE);
    }
    return result;
}

int neverc_mlkem768_decapsulate(const neverc_mlkem768_dk_t *dk,
                                const uint8_t ciphertext[NEVERC_MLKEM768_CT_SIZE],
                                uint8_t shared_key[32]) {
    if (!dk || !ciphertext || !shared_key) return -1;
    mlkem_lock();
    int result = mlkem_decaps(3, dk->seed, dk->ek, NEVERC_MLKEM768_EK_SIZE,
                              ciphertext, NEVERC_MLKEM768_CT_SIZE, shared_key);
    wipe_mlkem_scratch();
    mlkem_unlock();
    return result;
}

void neverc_mlkem768_dk_bytes(const neverc_mlkem768_dk_t *dk, uint8_t seed[64]) {
    if (!dk || !seed) return;
    memcpy(seed, dk->seed, 64);
}

void neverc_mlkem768_ek_bytes(const neverc_mlkem768_ek_t *ek,
                              uint8_t *out, size_t *out_len) {
    if (out_len) *out_len = 0;
    if (!ek || !out || !out_len) return;
    memcpy(out, ek->ek, NEVERC_MLKEM768_EK_SIZE);
    *out_len = NEVERC_MLKEM768_EK_SIZE;
}

/* ── Public API: ML-KEM-1024 ─────────────────────────────── */

int neverc_mlkem1024_generate_key(neverc_mlkem1024_dk_t *dk) {
    if (!dk) return -1;
    memset(dk, 0, sizeof(*dk));
    if (neverc_crypto_rand_read(dk->seed, sizeof(dk->seed)) != 0) {
        neverc_platform_secure_zero(dk, sizeof(*dk));
        return -1;
    }
    mlkem_lock();
    kpke_keygen(4, dk->seed, dk->ek, g_s_hat);
    wipe_mlkem_scratch();
    mlkem_unlock();
    return 0;
}

int neverc_mlkem1024_new_dk(neverc_mlkem1024_dk_t *dk, const uint8_t seed[64]) {
    if (!dk || !seed) return -1;
    memmove(dk->seed, seed, 64);
    mlkem_lock();
    kpke_keygen(4, dk->seed, dk->ek, g_s_hat);
    wipe_mlkem_scratch();
    mlkem_unlock();
    return 0;
}

void neverc_mlkem1024_dk_encapsulation_key(const neverc_mlkem1024_dk_t *dk,
                                           neverc_mlkem1024_ek_t *ek) {
    if (!dk || !ek) return;
    memcpy(ek->ek, dk->ek, NEVERC_MLKEM1024_EK_SIZE);
}

int neverc_mlkem1024_new_ek(neverc_mlkem1024_ek_t *ek,
                            const uint8_t *encoded, size_t len) {
    if (!ek) return -1;
    if (validate_encapsulation_key(4, encoded, len) != 0) {
        memset(ek, 0, sizeof(*ek));
        return -1;
    }
    memmove(ek->ek, encoded, len);
    return 0;
}

int neverc_mlkem1024_encapsulate(const neverc_mlkem1024_ek_t *ek,
                                 uint8_t shared_key[32],
                                 uint8_t ciphertext[NEVERC_MLKEM1024_CT_SIZE]) {
    if (!ek || !shared_key || !ciphertext) return -1;
    if (validate_encapsulation_key(
            4, ek->ek, NEVERC_MLKEM1024_EK_SIZE) != 0) {
        neverc_platform_secure_zero(shared_key, 32);
        neverc_platform_secure_zero(ciphertext, NEVERC_MLKEM1024_CT_SIZE);
        return -1;
    }
    mlkem_lock();
    int result = mlkem_encaps(4, ek->ek, NEVERC_MLKEM1024_EK_SIZE,
                              shared_key, ciphertext);
    wipe_mlkem_scratch();
    mlkem_unlock();
    if (result != 0) {
        neverc_platform_secure_zero(shared_key, 32);
        neverc_platform_secure_zero(ciphertext, NEVERC_MLKEM1024_CT_SIZE);
    }
    return result;
}

int neverc_mlkem1024_decapsulate(const neverc_mlkem1024_dk_t *dk,
                                 const uint8_t ciphertext[NEVERC_MLKEM1024_CT_SIZE],
                                 uint8_t shared_key[32]) {
    if (!dk || !ciphertext || !shared_key) return -1;
    mlkem_lock();
    int result = mlkem_decaps(4, dk->seed, dk->ek, NEVERC_MLKEM1024_EK_SIZE,
                              ciphertext, NEVERC_MLKEM1024_CT_SIZE, shared_key);
    wipe_mlkem_scratch();
    mlkem_unlock();
    return result;
}

void neverc_mlkem1024_dk_bytes(const neverc_mlkem1024_dk_t *dk, uint8_t seed[64]) {
    if (!dk || !seed) return;
    memcpy(seed, dk->seed, 64);
}

void neverc_mlkem1024_ek_bytes(const neverc_mlkem1024_ek_t *ek,
                               uint8_t *out, size_t *out_len) {
    if (out_len) *out_len = 0;
    if (!ek || !out || !out_len) return;
    memcpy(out, ek->ek, NEVERC_MLKEM1024_EK_SIZE);
    *out_len = NEVERC_MLKEM1024_EK_SIZE;
}
