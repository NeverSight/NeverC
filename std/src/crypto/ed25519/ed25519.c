#include "neverc/std/crypto/ed25519.h"
#include "neverc/std/crypto/sha512.h"
#include "neverc/std/math/big.h"
#include "neverc/std/_platform.h"
#include <string.h>
#include <stdlib.h>

/*
 * Ed25519 (RFC 8032) using bigint for all arithmetic.
 * Curve: -x^2 + y^2 = 1 + d*x^2*y^2 over GF(p), p = 2^255 - 19
 */

static neverc_bigint_t g_p, g_d, g_L;
static int g_init = 0; /* 0 = uninitialized, 1 = initializing, 2 = ready */

typedef struct { neverc_bigint_t x, y, z, t; } edpt;

static void ensure_init(void) {
    if (__atomic_load_n(&g_init, __ATOMIC_ACQUIRE) == 2)
        return;

    int expected = 0;
    if (__atomic_compare_exchange_n(&g_init, &expected, 1, 0,
                                    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        neverc_bigint_init(&g_p);
        neverc_bigint_init(&g_d);
        neverc_bigint_init(&g_L);
        neverc_bigint_set_string(
            &g_p,
            "7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffed",
            16);
        neverc_bigint_set_string(
            &g_d,
            "52036cee2b6ffe738cc740797779e89800700a4d4141d8ab75eb4dca135978a3",
            16);
        neverc_bigint_set_string(
            &g_L,
            "1000000000000000000000000000000014def9dea2f79cd65812631a5cf5d3ed",
            16);
        __atomic_store_n(&g_init, 2, __ATOMIC_RELEASE);
        return;
    }

    while (__atomic_load_n(&g_init, __ATOMIC_ACQUIRE) != 2) {
    }
}

static void edpt_init(edpt *p) {
    neverc_bigint_init(&p->x); neverc_bigint_init(&p->y);
    neverc_bigint_init(&p->z); neverc_bigint_init(&p->t);
}
static void edpt_free(edpt *p) {
    neverc_bigint_free(&p->x); neverc_bigint_free(&p->y);
    neverc_bigint_free(&p->z); neverc_bigint_free(&p->t);
}
static void edpt_zero(edpt *p) {
    neverc_bigint_set_int64(&p->x, 0);
    neverc_bigint_set_int64(&p->y, 1);
    neverc_bigint_set_int64(&p->z, 1);
    neverc_bigint_set_int64(&p->t, 0);
}

static void fmod(neverc_bigint_t *r, const neverc_bigint_t *a) {
    neverc_bigint_mod(r, a, &g_p);
}

static void fmul(neverc_bigint_t *r, const neverc_bigint_t *a, const neverc_bigint_t *b) {
    neverc_bigint_mul(r, a, b);
    fmod(r, r);
}

static void fsq(neverc_bigint_t *r, const neverc_bigint_t *a) {
    fmul(r, a, a);
}

static void fadd(neverc_bigint_t *r, const neverc_bigint_t *a, const neverc_bigint_t *b) {
    neverc_bigint_add(r, a, b);
    fmod(r, r);
}

static void fsub(neverc_bigint_t *r, const neverc_bigint_t *a, const neverc_bigint_t *b) {
    neverc_bigint_add(r, a, &g_p);
    neverc_bigint_sub(r, r, b);
    fmod(r, r);
}

static void finv(neverc_bigint_t *r, const neverc_bigint_t *a) {
    neverc_bigint_t exp, two;
    neverc_bigint_init(&exp); neverc_bigint_init(&two);
    neverc_bigint_set_int64(&two, 2);
    neverc_bigint_sub(&exp, &g_p, &two);
    neverc_bigint_exp(r, a, &exp, &g_p);
    neverc_bigint_free(&exp); neverc_bigint_free(&two);
}

static void edpt_add(edpt *r, const edpt *P, const edpt *Q) {
    neverc_bigint_t A, B, C, D, E, F, G, H, t1, t2;
    neverc_bigint_init(&A); neverc_bigint_init(&B);
    neverc_bigint_init(&C); neverc_bigint_init(&D);
    neverc_bigint_init(&E); neverc_bigint_init(&F);
    neverc_bigint_init(&G); neverc_bigint_init(&H);
    neverc_bigint_init(&t1); neverc_bigint_init(&t2);

    fsub(&t1, &P->y, &P->x);
    fsub(&t2, &Q->y, &Q->x);
    fmul(&A, &t1, &t2);

    fadd(&t1, &P->y, &P->x);
    fadd(&t2, &Q->y, &Q->x);
    fmul(&B, &t1, &t2);

    fmul(&C, &P->t, &Q->t);
    fmul(&C, &C, &g_d);
    fadd(&C, &C, &C);

    fmul(&D, &P->z, &Q->z);
    fadd(&D, &D, &D);

    fsub(&E, &B, &A);
    fsub(&F, &D, &C);
    fadd(&G, &D, &C);
    fadd(&H, &B, &A);

    fmul(&r->x, &E, &F);
    fmul(&r->y, &G, &H);
    fmul(&r->z, &F, &G);
    fmul(&r->t, &E, &H);

    neverc_bigint_free(&A); neverc_bigint_free(&B);
    neverc_bigint_free(&C); neverc_bigint_free(&D);
    neverc_bigint_free(&E); neverc_bigint_free(&F);
    neverc_bigint_free(&G); neverc_bigint_free(&H);
    neverc_bigint_free(&t1); neverc_bigint_free(&t2);
}

static void edpt_dbl(edpt *r, const edpt *P) {
    neverc_bigint_t A, B, C, D, E, F, G, H, t1;
    neverc_bigint_init(&A); neverc_bigint_init(&B);
    neverc_bigint_init(&C); neverc_bigint_init(&D);
    neverc_bigint_init(&E); neverc_bigint_init(&F);
    neverc_bigint_init(&G); neverc_bigint_init(&H);
    neverc_bigint_init(&t1);

    fsq(&A, &P->x);
    fsq(&B, &P->y);
    fsq(&C, &P->z); fadd(&C, &C, &C);

    neverc_bigint_set(&D, &A);
    neverc_bigint_sub(&D, &g_p, &D);
    fmod(&D, &D);

    fadd(&t1, &P->x, &P->y);
    fsq(&E, &t1);
    fsub(&E, &E, &A);
    fsub(&E, &E, &B);

    fadd(&G, &D, &B);
    fsub(&F, &G, &C);
    fsub(&H, &D, &B);

    fmul(&r->x, &E, &F);
    fmul(&r->y, &G, &H);
    fmul(&r->z, &F, &G);
    fmul(&r->t, &E, &H);

    neverc_bigint_free(&A); neverc_bigint_free(&B);
    neverc_bigint_free(&C); neverc_bigint_free(&D);
    neverc_bigint_free(&E); neverc_bigint_free(&F);
    neverc_bigint_free(&G); neverc_bigint_free(&H);
    neverc_bigint_free(&t1);
}

static void scalar_mult_ed(edpt *r, const unsigned char scalar[32], const edpt *p) {
    edpt acc, tmp, sum;
    edpt_init(&acc); edpt_init(&tmp); edpt_init(&sum);
    edpt_zero(&acc);
    neverc_bigint_set(&tmp.x, &p->x);
    neverc_bigint_set(&tmp.y, &p->y);
    neverc_bigint_set(&tmp.z, &p->z);
    neverc_bigint_set(&tmp.t, &p->t);

    for (int i = 0; i < 256; i++) {
        if ((scalar[i / 8] >> (i % 8)) & 1) {
            edpt_add(&sum, &acc, &tmp);
            neverc_bigint_set(&acc.x, &sum.x);
            neverc_bigint_set(&acc.y, &sum.y);
            neverc_bigint_set(&acc.z, &sum.z);
            neverc_bigint_set(&acc.t, &sum.t);
        }
        edpt_dbl(&sum, &tmp);
        neverc_bigint_set(&tmp.x, &sum.x);
        neverc_bigint_set(&tmp.y, &sum.y);
        neverc_bigint_set(&tmp.z, &sum.z);
        neverc_bigint_set(&tmp.t, &sum.t);
    }

    neverc_bigint_set(&r->x, &acc.x);
    neverc_bigint_set(&r->y, &acc.y);
    neverc_bigint_set(&r->z, &acc.z);
    neverc_bigint_set(&r->t, &acc.t);
    edpt_free(&acc); edpt_free(&tmp); edpt_free(&sum);
}

/* Little-endian bytes → bigint via 32-bit limbs. Avoids hex/set_string. */
static void bigint_set_le(neverc_bigint_t *z, const unsigned char *p, size_t n) {
    neverc_bigint_t acc, limb;
    neverc_bigint_init(&acc);
    neverc_bigint_init(&limb);
    neverc_bigint_set_int64(&acc, 0);
    if (n > 0) {
        size_t nlimbs = (n + 3) / 4;
        for (size_t i = nlimbs; i-- > 0; ) {
            uint32_t w = 0;
            for (size_t j = 0; j < 4; j++) {
                size_t idx = i * 4 + j;
                if (idx < n)
                    w |= (uint32_t)p[idx] << (8 * (unsigned)j);
            }
            neverc_bigint_lsh(&acc, &acc, 32);
            neverc_bigint_set_uint64(&limb, w);
            neverc_bigint_add(&acc, &acc, &limb);
        }
    }
    neverc_bigint_set(z, &acc);
    neverc_bigint_free(&acc);
    neverc_bigint_free(&limb);
}

static int bigint_get_le(const neverc_bigint_t *z, unsigned char *p, size_t n) {
    memset(p, 0, n);
    if (!z || z->neg)
        return -1;
    if (neverc_bigint_bit_len(z) > (int)n * 8)
        return -1;
    if (!z->digits)
        return 0;
    for (size_t i = 0; i < z->len && i * 4 < n; i++) {
        uint32_t w = z->digits[i];
        for (size_t j = 0; j < 4 && i * 4 + j < n; j++)
            p[i * 4 + j] = (unsigned char)(w >> (8 * (unsigned)j));
    }
    return 0;
}

static int edpt_encode(unsigned char s[32], const edpt *p) {
    neverc_bigint_t zinv, x, y;
    neverc_bigint_init(&zinv); neverc_bigint_init(&x); neverc_bigint_init(&y);
    finv(&zinv, &p->z);
    fmul(&x, &p->x, &zinv);
    fmul(&y, &p->y, &zinv);

    int ok = bigint_get_le(&y, s, 32) == 0;
    if (ok && neverc_bigint_bit(&x, 0))
        s[31] |= 0x80;
    if (!ok)
        memset(s, 0, 32);
    neverc_bigint_free(&zinv); neverc_bigint_free(&x); neverc_bigint_free(&y);
    return ok ? 0 : -1;
}

static int edpt_encoding_is_canonical(const unsigned char encoded[32]) {
    static const unsigned char field_prime[32] = {
        0xed, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f,
    };
    for (int i = 31; i >= 0; --i) {
        unsigned char value = encoded[i];
        if (i == 31)
            value &= 0x7f;
        if (value < field_prime[i])
            return 1;
        if (value > field_prime[i])
            return 0;
    }
    return 0;
}

static int edpt_decode(edpt *r, const unsigned char s[32]) {
    if (!edpt_encoding_is_canonical(s))
        return -1;
    ensure_init();
    unsigned char scopy[32];
    memcpy(scopy, s, 32);
    int x_sign = (scopy[31] >> 7) & 1;
    scopy[31] &= 0x7F;

    bigint_set_le(&r->y, scopy, 32);

    neverc_bigint_t y2, u, v, vinv, x2, x, two, rem;
    neverc_bigint_init(&y2); neverc_bigint_init(&u);
    neverc_bigint_init(&v); neverc_bigint_init(&vinv);
    neverc_bigint_init(&x2); neverc_bigint_init(&x);
    neverc_bigint_init(&two); neverc_bigint_init(&rem);

    fsq(&y2, &r->y);

    neverc_bigint_t one;
    neverc_bigint_init(&one);
    neverc_bigint_set_int64(&one, 1);
    fsub(&u, &y2, &one);
    fmul(&v, &y2, &g_d);
    fadd(&v, &v, &one);

    if (neverc_bigint_is_zero(&v)) {
        neverc_bigint_free(&y2); neverc_bigint_free(&u);
        neverc_bigint_free(&v); neverc_bigint_free(&vinv);
        neverc_bigint_free(&x2); neverc_bigint_free(&x);
        neverc_bigint_free(&two); neverc_bigint_free(&rem);
        neverc_bigint_free(&one);
        return -1;
    }
    finv(&vinv, &v);
    fmul(&x2, &u, &vinv);

    /* p ≡ 5 (mod 8), use Atkin's algorithm: x = x2^((p+3)/8), then adjust */
    neverc_bigint_t exp, eight, three_bi;
    neverc_bigint_init(&exp); neverc_bigint_init(&eight);
    neverc_bigint_init(&three_bi);
    neverc_bigint_set_int64(&eight, 8);
    neverc_bigint_set_int64(&three_bi, 3);
    neverc_bigint_add(&exp, &g_p, &three_bi);
    neverc_bigint_div(&exp, NULL, &exp, &eight);

    neverc_bigint_exp(&x, &x2, &exp, &g_p);

    neverc_bigint_t check;
    neverc_bigint_init(&check);
    fsq(&check, &x);
    if (neverc_bigint_cmp(&check, &x2) != 0) {
        /* Try multiplying by sqrt(-1) = 2^((p-1)/4) */
        neverc_bigint_t sqrtm1, pm1d4, four;
        neverc_bigint_init(&sqrtm1); neverc_bigint_init(&pm1d4);
        neverc_bigint_init(&four);
        neverc_bigint_set_int64(&four, 4);
        neverc_bigint_sub(&pm1d4, &g_p, &one);
        neverc_bigint_div(&pm1d4, NULL, &pm1d4, &four);
        neverc_bigint_set_int64(&two, 2);
        neverc_bigint_exp(&sqrtm1, &two, &pm1d4, &g_p);
        fmul(&x, &x, &sqrtm1);

        fsq(&check, &x);
        if (neverc_bigint_cmp(&check, &x2) != 0) {
            neverc_bigint_free(&y2); neverc_bigint_free(&u);
            neverc_bigint_free(&v); neverc_bigint_free(&vinv);
            neverc_bigint_free(&x2); neverc_bigint_free(&x);
            neverc_bigint_free(&two); neverc_bigint_free(&rem);
            neverc_bigint_free(&one); neverc_bigint_free(&exp);
            neverc_bigint_free(&eight); neverc_bigint_free(&three_bi);
            neverc_bigint_free(&check); neverc_bigint_free(&sqrtm1);
            neverc_bigint_free(&pm1d4); neverc_bigint_free(&four);
            return -1;
        }
        neverc_bigint_free(&sqrtm1); neverc_bigint_free(&pm1d4);
        neverc_bigint_free(&four);
    }
    neverc_bigint_free(&eight); neverc_bigint_free(&three_bi);

    neverc_bigint_set_int64(&two, 2);
    neverc_bigint_mod(&rem, &x, &two);
    int x_is_odd = !neverc_bigint_is_zero(&rem);
    if (x_is_odd != x_sign) {
        neverc_bigint_sub(&x, &g_p, &x);
        fmod(&x, &x);
    }
    if (neverc_bigint_is_zero(&x) && x_sign) {
        neverc_bigint_free(&y2); neverc_bigint_free(&u);
        neverc_bigint_free(&v); neverc_bigint_free(&vinv);
        neverc_bigint_free(&x2); neverc_bigint_free(&x);
        neverc_bigint_free(&two); neverc_bigint_free(&rem);
        neverc_bigint_free(&one); neverc_bigint_free(&exp);
        neverc_bigint_free(&check);
        return -1;
    }

    neverc_bigint_set(&r->x, &x);
    neverc_bigint_set_int64(&r->z, 1);
    fmul(&r->t, &r->x, &r->y);

    neverc_bigint_free(&y2); neverc_bigint_free(&u);
    neverc_bigint_free(&v); neverc_bigint_free(&vinv);
    neverc_bigint_free(&x2); neverc_bigint_free(&x);
    neverc_bigint_free(&two); neverc_bigint_free(&rem);
    neverc_bigint_free(&one); neverc_bigint_free(&exp);
    neverc_bigint_free(&check);
    return 0;
}

static int edpt_has_small_order(const edpt *point) {
    edpt twice, four_times, eight_times;
    edpt_init(&twice);
    edpt_init(&four_times);
    edpt_init(&eight_times);
    edpt_dbl(&twice, point);
    edpt_dbl(&four_times, &twice);
    edpt_dbl(&eight_times, &four_times);
    int small_order =
        neverc_bigint_is_zero(&eight_times.x) &&
        neverc_bigint_cmp(&eight_times.y, &eight_times.z) == 0;
    edpt_free(&twice);
    edpt_free(&four_times);
    edpt_free(&eight_times);
    return small_order;
}

static void get_basepoint(edpt *B) {
    ensure_init();
    neverc_bigint_set_string(&B->x,
        "216936d3cd6e53fec0a4e231fdd6dc5c692cc7609525a7b2c9562d608f25d51a", 16);
    neverc_bigint_set_string(&B->y,
        "6666666666666666666666666666666666666666666666666666666666666658", 16);
    neverc_bigint_set_int64(&B->z, 1);
    fmul(&B->t, &B->x, &B->y);
}

static void ed_bigint_secure_free(neverc_bigint_t *value) {
    if (!value) return;
    if (value->digits)
        neverc_platform_secure_zero(
            value->digits, value->cap * sizeof(*value->digits));
    neverc_bigint_free(value);
}

/* RFC 8032 5.1.3: interpret 64 little-endian bytes as an integer and
 * reduce mod L. Do not go through hex/set_string: a 128-digit parse is
 * the only 512-bit load in this file, and a failed or truncated parse
 * yields nonce=0 (R = identity). That still verifies against itself
 * but does not match RFC 8032 signatures. */
static int sc_reduce64(unsigned char out[32], const unsigned char in[64]) {
    neverc_bigint_t val, r;
    neverc_bigint_init(&val); neverc_bigint_init(&r);
    bigint_set_le(&val, in, 64);
    neverc_bigint_mod(&r, &val, &g_L);
    int ok = bigint_get_le(&r, out, 32) == 0;
    if (!ok)
        memset(out, 0, 32);
    ed_bigint_secure_free(&val);
    ed_bigint_secure_free(&r);
    return ok ? 0 : -1;
}

static int sc_muladd(unsigned char s[32], const unsigned char a[32],
                       const unsigned char b[32], const unsigned char c[32]) {
    neverc_bigint_t av, bv, cv, prod, result;
    neverc_bigint_init(&av); neverc_bigint_init(&bv);
    neverc_bigint_init(&cv); neverc_bigint_init(&prod);
    neverc_bigint_init(&result);

    bigint_set_le(&av, a, 32);
    bigint_set_le(&bv, b, 32);
    bigint_set_le(&cv, c, 32);

    neverc_bigint_mul(&prod, &av, &bv);
    neverc_bigint_add(&prod, &prod, &cv);
    neverc_bigint_mod(&result, &prod, &g_L);

    int ok = bigint_get_le(&result, s, 32) == 0;
    if (!ok)
        memset(s, 0, 32);

    ed_bigint_secure_free(&av);
    ed_bigint_secure_free(&bv);
    ed_bigint_secure_free(&cv);
    ed_bigint_secure_free(&prod);
    ed_bigint_secure_free(&result);
    return ok ? 0 : -1;
}

/* RFC 8032: "SigEd25519 no Ed25519 collisions" || octet(phflag) ||
 * octet(len(C)) || C. PureEdDSA omits this prefix. */
static void ed25519_dom2(neverc_sha512_ctx *ctx, int phflag,
                         const unsigned char *context, size_t context_len) {
    static const unsigned char prefix[] =
        "SigEd25519 no Ed25519 collisions";
    unsigned char hdr[2];
    hdr[0] = (unsigned char)phflag;
    hdr[1] = (unsigned char)context_len;
    neverc_sha512_update(ctx, prefix, 32);
    neverc_sha512_update(ctx, hdr, sizeof(hdr));
    if (context_len)
        neverc_sha512_update(ctx, context, context_len);
}

#ifndef NCI_ED25519_RANDOM
#define NCI_ED25519_RANDOM neverc_platform_random
#endif

int neverc_ed25519_new_key_from_seed(const unsigned char seed[32],
                                      unsigned char pub[32],
                                      unsigned char priv[64]) {
    if (!seed || !pub || !priv)
        return -1;
    ensure_init();
    unsigned char h[64];
    neverc_sha512_sum(seed, 32, h);
    h[0] &= 248;
    h[31] &= 127;
    h[31] |= 64;

    edpt B, A;
    edpt_init(&B); edpt_init(&A);
    get_basepoint(&B);
    scalar_mult_ed(&A, h, &B);
    int result = edpt_encode(pub, &A);
    if (result == 0) {
        memcpy(priv, seed, 32);
        memcpy(priv + 32, pub, 32);
    } else {
        memset(pub, 0, 32);
        memset(priv, 0, 64);
    }
    edpt_free(&B); edpt_free(&A);
    neverc_platform_secure_zero(h, sizeof(h));
    return result;
}

int neverc_ed25519_generate_key(unsigned char pub[32], unsigned char priv[64]) {
    if (!pub || !priv)
        return -1;
    memset(pub, 0, 32);
    memset(priv, 0, 64);
    unsigned char seed[32];
    if (NCI_ED25519_RANDOM(seed, sizeof(seed)) != 0) {
        neverc_platform_secure_zero(seed, sizeof(seed));
        return -1;
    }
    int result = neverc_ed25519_new_key_from_seed(seed, pub, priv);
    neverc_platform_secure_zero(seed, sizeof(seed));
    if (result != 0) {
        neverc_platform_secure_zero(pub, 32);
        neverc_platform_secure_zero(priv, 64);
    }
    return result;
}

void neverc_ed25519_seed(const unsigned char priv[64], unsigned char seed[32]) {
    if (!priv || !seed) return;
    memcpy(seed, priv, 32);
}

static int ed25519_sign_ex(const unsigned char priv[64],
                            const unsigned char *msg, size_t msg_len,
                            const unsigned char *context, size_t context_len,
                            int use_dom2, unsigned char sig[64]) {
    if (!priv || (!msg && msg_len != 0) || !sig)
        return -1;
    /* RFC 8032: Ed25519ctx context is 0..255 octets. Empty is allowed and
     * is not the same as PureEdDSA (which omits the DOM2 prefix). */
    if (use_dom2 && (context_len > 255 || (context_len > 0 && !context)))
        return -1;
    ensure_init();
    unsigned char h[64];
    neverc_sha512_sum(priv, 32, h);
    unsigned char a[32];
    memcpy(a, h, 32);
    a[0] &= 248;
    a[31] &= 127;
    a[31] |= 64;

    neverc_sha512_ctx ctx;
    neverc_sha512_init(&ctx);
    if (use_dom2)
        ed25519_dom2(&ctx, 0, context, context_len);
    neverc_sha512_update(&ctx, h + 32, 32);
    if (msg && msg_len > 0) neverc_sha512_update(&ctx, msg, msg_len);
    unsigned char nonce_hash[64];
    neverc_sha512_final(&ctx, nonce_hash);
    unsigned char nonce[32];
    int result = -1;
    if (sc_reduce64(nonce, nonce_hash) != 0) {
        memset(sig, 0, 64);
        goto sign_cleanup;
    }

    edpt B, R;
    edpt_init(&B); edpt_init(&R);
    get_basepoint(&B);
    scalar_mult_ed(&R, nonce, &B);
    if (edpt_encode(sig, &R) != 0) {
        edpt_free(&B); edpt_free(&R);
        memset(sig, 0, 64);
        goto sign_cleanup;
    }
    edpt_free(&B); edpt_free(&R);

    neverc_sha512_init(&ctx);
    if (use_dom2)
        ed25519_dom2(&ctx, 0, context, context_len);
    neverc_sha512_update(&ctx, sig, 32);
    neverc_sha512_update(&ctx, priv + 32, 32);
    if (msg && msg_len > 0) neverc_sha512_update(&ctx, msg, msg_len);
    unsigned char hram[64];
    neverc_sha512_final(&ctx, hram);
    unsigned char hram_reduced[32];
    if (sc_reduce64(hram_reduced, hram) != 0 ||
        sc_muladd(sig + 32, hram_reduced, a, nonce) != 0) {
        memset(sig, 0, 64);
        neverc_platform_secure_zero(hram, sizeof(hram));
        neverc_platform_secure_zero(hram_reduced, sizeof(hram_reduced));
        goto sign_cleanup;
    }
    neverc_platform_secure_zero(hram, sizeof(hram));
    neverc_platform_secure_zero(hram_reduced, sizeof(hram_reduced));
    result = 0;

sign_cleanup:
    neverc_platform_secure_zero(h, sizeof(h));
    neverc_platform_secure_zero(a, sizeof(a));
    neverc_platform_secure_zero(nonce_hash, sizeof(nonce_hash));
    neverc_platform_secure_zero(nonce, sizeof(nonce));
    neverc_platform_secure_zero(&ctx, sizeof(ctx));
    return result;
}

int neverc_ed25519_sign(const unsigned char priv[64],
                         const unsigned char *msg, size_t msg_len,
                         unsigned char sig[64]) {
    return ed25519_sign_ex(priv, msg, msg_len, NULL, 0, 0, sig);
}

int neverc_ed25519_sign_ctx(const unsigned char priv[64],
                             const unsigned char *msg, size_t msg_len,
                             const unsigned char *context, size_t context_len,
                             unsigned char sig[64]) {
    return ed25519_sign_ex(priv, msg, msg_len, context, context_len, 1, sig);
}

static int scalar_is_canonical(const unsigned char scalar[32]) {
    static const unsigned char order[32] = {
        0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58,
        0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10,
    };
    for (int i = 31; i >= 0; --i) {
        if (scalar[i] < order[i])
            return 1;
        if (scalar[i] > order[i])
            return 0;
    }
    return 0;
}

static int ed25519_verify_ex(const unsigned char pub[32],
                              const unsigned char *msg, size_t msg_len,
                              const unsigned char *context, size_t context_len,
                              int use_dom2, const unsigned char sig[64]) {
    if (!pub || !sig || (!msg && msg_len != 0) ||
        !scalar_is_canonical(sig + 32))
        return -1;
    if (use_dom2 && (context_len > 255 || (context_len > 0 && !context)))
        return -1;
    ensure_init();
    edpt A, R;
    edpt_init(&A);
    edpt_init(&R);
    if (edpt_decode(&A, pub) != 0 || edpt_has_small_order(&A) ||
        edpt_decode(&R, sig) != 0 || edpt_has_small_order(&R)) {
        edpt_free(&A);
        edpt_free(&R);
        return -1;
    }
    edpt_free(&R);

    neverc_sha512_ctx ctx;
    neverc_sha512_init(&ctx);
    if (use_dom2)
        ed25519_dom2(&ctx, 0, context, context_len);
    neverc_sha512_update(&ctx, sig, 32);
    neverc_sha512_update(&ctx, pub, 32);
    if (msg && msg_len > 0) neverc_sha512_update(&ctx, msg, msg_len);
    unsigned char hram[64];
    neverc_sha512_final(&ctx, hram);
    unsigned char h_scalar[32];
    if (sc_reduce64(h_scalar, hram) != 0) {
        edpt_free(&A);
        return -1;
    }

    edpt B, sB;
    edpt_init(&B); edpt_init(&sB);
    get_basepoint(&B);
    scalar_mult_ed(&sB, sig + 32, &B);

    neverc_bigint_sub(&A.x, &g_p, &A.x);
    fmod(&A.x, &A.x);
    neverc_bigint_sub(&A.t, &g_p, &A.t);
    fmod(&A.t, &A.t);

    edpt hA, check;
    edpt_init(&hA); edpt_init(&check);
    scalar_mult_ed(&hA, h_scalar, &A);
    edpt_add(&check, &sB, &hA);

    unsigned char check_bytes[32];
    int encoded = edpt_encode(check_bytes, &check);

    int diff = 0;
    for (int i = 0; i < 32; i++)
        diff |= check_bytes[i] ^ sig[i];

    edpt_free(&A); edpt_free(&B); edpt_free(&sB);
    edpt_free(&hA); edpt_free(&check);
    return (encoded == 0 && diff == 0) ? 0 : -1;
}

int neverc_ed25519_verify(const unsigned char pub[32],
                           const unsigned char *msg, size_t msg_len,
                           const unsigned char sig[64]) {
    return ed25519_verify_ex(pub, msg, msg_len, NULL, 0, 0, sig);
}

int neverc_ed25519_verify_ctx(const unsigned char pub[32],
                               const unsigned char *msg, size_t msg_len,
                               const unsigned char *context, size_t context_len,
                               const unsigned char sig[64]) {
    return ed25519_verify_ex(pub, msg, msg_len, context, context_len, 1, sig);
}
