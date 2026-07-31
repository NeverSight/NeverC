#include "neverc/std/crypto/rsa.h"
#include "neverc/std/crypto/sha256.h"
#include "neverc/std/crypto/sha384.h"
#include "neverc/std/crypto/sha512.h"
#include "neverc/std/_platform.h"
#include <string.h>
#include <stdlib.h>

#define secure_random neverc_platform_random

void neverc_rsa_public_key_init(neverc_rsa_public_key_t *k) {
    neverc_bigint_init(&k->n);
    neverc_bigint_init(&k->e);
}

void neverc_rsa_public_key_free(neverc_rsa_public_key_t *k) {
    neverc_bigint_free(&k->n);
    neverc_bigint_free(&k->e);
}

void neverc_rsa_private_key_init(neverc_rsa_private_key_t *k) {
    neverc_rsa_public_key_init(&k->pub);
    neverc_bigint_init(&k->d);
    neverc_bigint_init(&k->p);
    neverc_bigint_init(&k->q);
    neverc_bigint_init(&k->dp);
    neverc_bigint_init(&k->dq);
    neverc_bigint_init(&k->qinv);
}

void neverc_rsa_private_key_free(neverc_rsa_private_key_t *k) {
    neverc_rsa_public_key_free(&k->pub);
    neverc_bigint_free(&k->d);
    neverc_bigint_free(&k->p);
    neverc_bigint_free(&k->q);
    neverc_bigint_free(&k->dp);
    neverc_bigint_free(&k->dq);
    neverc_bigint_free(&k->qinv);
}

static void random_bigint(neverc_bigint_t *r, int bits) {
    int bytes = (bits + 7) / 8;
    unsigned char *buf = (unsigned char *)malloc((size_t)bytes);
    secure_random(buf, (size_t)bytes);
    buf[0] |= 0x80;
    buf[bytes - 1] |= 0x01;

    char hex[2048];
    int pos = 0;
    for (int i = 0; i < bytes && pos < (int)sizeof(hex) - 2; i++) {
        hex[pos++] = "0123456789abcdef"[buf[i] >> 4];
        hex[pos++] = "0123456789abcdef"[buf[i] & 0x0F];
    }
    hex[pos] = '\0';
    neverc_bigint_set_string(r, hex, 16);
    free(buf);
}

static int miller_rabin(const neverc_bigint_t *n, int rounds) {
    neverc_bigint_t one, two, nm1, d, a, x;
    neverc_bigint_init(&one); neverc_bigint_init(&two);
    neverc_bigint_init(&nm1); neverc_bigint_init(&d);
    neverc_bigint_init(&a); neverc_bigint_init(&x);

    neverc_bigint_set_int64(&one, 1);
    neverc_bigint_set_int64(&two, 2);
    neverc_bigint_sub(&nm1, n, &one);

    neverc_bigint_set(&d, &nm1);
    int r = 0;
    while (neverc_bigint_bit(&d, 0) == 0) {
        neverc_bigint_rsh(&d, &d, 1);
        r++;
    }

    int result = 1;
    int witnesses[] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37};
    int nw = rounds < 12 ? rounds : 12;

    for (int i = 0; i < nw && result; i++) {
        neverc_bigint_set_int64(&a, witnesses[i]);
        if (neverc_bigint_cmp(&a, &nm1) >= 0) continue;

        neverc_bigint_exp(&x, &a, &d, n);

        if (neverc_bigint_cmp(&x, &one) == 0 ||
            neverc_bigint_cmp(&x, &nm1) == 0) continue;

        int found = 0;
        for (int j = 0; j < r - 1; j++) {
            neverc_bigint_mul(&x, &x, &x);
            neverc_bigint_mod(&x, &x, n);
            if (neverc_bigint_cmp(&x, &nm1) == 0) { found = 1; break; }
        }
        if (!found) result = 0;
    }

    neverc_bigint_free(&one); neverc_bigint_free(&two);
    neverc_bigint_free(&nm1); neverc_bigint_free(&d);
    neverc_bigint_free(&a); neverc_bigint_free(&x);
    return result;
}

/* Odd primes below 1000: a candidate divisible by any of them is composite. */
static const uint32_t small_primes[] = {
    3,5,7,11,13,17,19,23,29,31,37,41,43,47,53,59,61,67,71,73,79,83,89,97,
    101,103,107,109,113,127,131,137,139,149,151,157,163,167,173,179,181,191,
    193,197,199,211,223,227,229,233,239,241,251,257,263,269,271,277,281,283,
    293,307,311,313,317,331,337,347,349,353,359,367,373,379,383,389,397,401,
    409,419,421,431,433,439,443,449,457,461,463,467,479,487,491,499,503,509,
    521,523,541,547,557,563,569,571,577,587,593,599,601,607,613,617,619,631,
    641,643,647,653,659,661,673,677,683,691,701,709,719,727,733,739,743,751,
    757,761,769,773,787,797,809,811,821,823,827,829,839,853,857,859,863,877,
    881,883,887,907,911,919,929,937,941,947,953,967,971,977,983,991,997
};

/* a mod m for a small m, walking the public limb array high-to-low. */
static uint32_t bigint_mod_u32(const neverc_bigint_t *a, uint32_t m) {
    uint64_t r = 0;
    for (size_t i = a->len; i-- > 0; )
        r = ((r << 32) | a->digits[i]) % m;
    return (uint32_t)r;
}

/* Trial division by the small primes: a cheap O(len) reject that screens out
 * ~80% of random odd candidates before the (far costlier) Miller-Rabin rounds.
 * The RSA factors are hundreds of bits, so no true factor equals a small prime. */
static int has_small_factor(const neverc_bigint_t *p) {
    for (size_t i = 0; i < sizeof(small_primes)/sizeof(small_primes[0]); i++)
        if (bigint_mod_u32(p, small_primes[i]) == 0) return 1;
    return 0;
}

static void gen_prime(neverc_bigint_t *p, int bits) {
    for (;;) {
        random_bigint(p, bits);
        if (neverc_bigint_bit(p, 0) == 0) {
            neverc_bigint_t one;
            neverc_bigint_init(&one);
            neverc_bigint_set_int64(&one, 1);
            neverc_bigint_add(p, p, &one);
            neverc_bigint_free(&one);
        }
        if (has_small_factor(p)) continue;   /* cheap pre-screen before Miller-Rabin */
        if (miller_rabin(p, 12)) return;
    }
}

/* r = a^-1 mod m by the extended Euclidean algorithm. Returns 0 on success and
 * -1 when gcd(a, m) != 1, in which case no inverse exists and r is untouched:
 * the Bezout coefficient the loop produces solves a*s = gcd (mod m), not 1, so
 * returning it would hand the caller a silently wrong "inverse". */
static int mod_inverse(neverc_bigint_t *r, const neverc_bigint_t *a, const neverc_bigint_t *m) {
    neverc_bigint_t old_r, rr, old_s, s, q, tmp, prod;
    neverc_bigint_init(&old_r); neverc_bigint_init(&rr);
    neverc_bigint_init(&old_s); neverc_bigint_init(&s);
    neverc_bigint_init(&q); neverc_bigint_init(&tmp);
    neverc_bigint_init(&prod);

    neverc_bigint_set(&old_r, a);
    neverc_bigint_set(&rr, m);
    neverc_bigint_set_int64(&old_s, 1);
    neverc_bigint_set_int64(&s, 0);

    while (!neverc_bigint_is_zero(&rr)) {
        neverc_bigint_div(&q, &tmp, &old_r, &rr);

        neverc_bigint_set(&old_r, &rr);
        neverc_bigint_set(&rr, &tmp);

        neverc_bigint_mul(&prod, &q, &s);
        neverc_bigint_sub(&tmp, &old_s, &prod);
        neverc_bigint_set(&old_s, &s);
        neverc_bigint_set(&s, &tmp);
    }

    neverc_bigint_set_int64(&tmp, 1);
    int invertible = (neverc_bigint_cmp(&old_r, &tmp) == 0);   /* old_r = gcd(a,m) */
    if (invertible)
        neverc_bigint_mod(r, &old_s, m);

    neverc_bigint_free(&old_r); neverc_bigint_free(&rr);
    neverc_bigint_free(&old_s); neverc_bigint_free(&s);
    neverc_bigint_free(&q); neverc_bigint_free(&tmp);
    neverc_bigint_free(&prod);
    return invertible ? 0 : -1;
}

/* Attempts before giving up on drawing a usable prime pair. Each draw fails
 * independently with probability ~2/e, so 64 puts the odds of exhausting them
 * far below any other failure mode in the program. */
#define RSA_KEYGEN_ATTEMPTS 64

/* The public exponent. With the real value the retry below fires on about one
 * draw in 65537, far too rare for a test to reach, so test_rsa_retry.c builds
 * this file with a small exponent to make the retry the common case. */
#ifndef NCI_RSA_PUBLIC_EXPONENT
#define NCI_RSA_PUBLIC_EXPONENT 65537
#endif

int neverc_rsa_generate_key(neverc_rsa_private_key_t *key, int bits) {
    if (bits < 512) return -1;
    int half = bits / 2;

    neverc_bigint_set_int64(&key->pub.e, NCI_RSA_PUBLIC_EXPONENT);

    neverc_bigint_t one, pm1, qm1, phi;
    neverc_bigint_init(&one); neverc_bigint_init(&pm1);
    neverc_bigint_init(&qm1); neverc_bigint_init(&phi);
    neverc_bigint_set_int64(&one, 1);

    /* e is fixed, so a prime with p = 1 (mod e) leaves e non-invertible modulo
     * phi and admits no private exponent at all. Roughly one prime in e lands
     * there; draw a fresh pair rather than emit a key whose d is meaningless. */
    int ok = 0;
    for (int attempt = 0; attempt < RSA_KEYGEN_ATTEMPTS && !ok; attempt++) {
        gen_prime(&key->p, half);
        gen_prime(&key->q, half);
        if (neverc_bigint_cmp(&key->p, &key->q) == 0) continue;

        neverc_bigint_sub(&pm1, &key->p, &one);
        neverc_bigint_sub(&qm1, &key->q, &one);
        neverc_bigint_mul(&phi, &pm1, &qm1);

        if (mod_inverse(&key->d, &key->pub.e, &phi) != 0) continue;
        if (mod_inverse(&key->qinv, &key->q, &key->p) != 0) continue;
        ok = 1;
    }

    if (ok) {
        neverc_bigint_mul(&key->pub.n, &key->p, &key->q);
        neverc_bigint_mod(&key->dp, &key->d, &pm1);
        neverc_bigint_mod(&key->dq, &key->d, &qm1);
    }

    neverc_bigint_free(&one); neverc_bigint_free(&pm1);
    neverc_bigint_free(&qm1); neverc_bigint_free(&phi);
    return ok ? 0 : -1;
}

int neverc_rsa_key_size(const neverc_rsa_public_key_t *pub) {
    return (neverc_bigint_bit_len(&pub->n) + 7) / 8;
}

static void bytes_to_bigint(neverc_bigint_t *r, const unsigned char *data, size_t len) {
    char hex[2048];
    int pos = 0;
    for (size_t i = 0; i < len && pos < (int)sizeof(hex) - 2; i++) {
        hex[pos++] = "0123456789abcdef"[data[i] >> 4];
        hex[pos++] = "0123456789abcdef"[data[i] & 0x0F];
    }
    hex[pos] = '\0';
    neverc_bigint_set_string(r, hex, 16);
}

static void bigint_to_bytes(const neverc_bigint_t *v, unsigned char *out, int byte_len) {
    char hex[2048];
    neverc_bigint_string(v, 16, hex, sizeof(hex));
    size_t hlen = strlen(hex);
    memset(out, 0, (size_t)byte_len);
    for (size_t i = 0; i < hlen; i++) {
        int d;
        char c = hex[hlen - 1 - i];
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else d = c - 'A' + 10;
        out[byte_len - 1 - (int)(i / 2)] |= (unsigned char)(d << ((i % 2) * 4));
    }
}

int neverc_rsa_encrypt_pkcs1v15(const neverc_rsa_public_key_t *pub,
                                 const unsigned char *msg, size_t msg_len,
                                 unsigned char *out, size_t out_cap, size_t *out_len) {
    int k = neverc_rsa_key_size(pub);
    if ((int)msg_len > k - 11) return -1;
    if ((int)out_cap < k) return -1;

    unsigned char *em = (unsigned char *)malloc((size_t)k);
    em[0] = 0x00;
    em[1] = 0x02;
    int ps_len = k - (int)msg_len - 3;
    secure_random(em + 2, (size_t)ps_len);
    for (int i = 0; i < ps_len; i++)
        if (em[2 + i] == 0) em[2 + i] = 1;
    em[2 + ps_len] = 0x00;
    memcpy(em + 3 + ps_len, msg, msg_len);

    neverc_bigint_t m, c;
    neverc_bigint_init(&m); neverc_bigint_init(&c);
    bytes_to_bigint(&m, em, (size_t)k);
    neverc_bigint_exp(&c, &m, &pub->e, &pub->n);
    bigint_to_bytes(&c, out, k);

    if (out_len) *out_len = (size_t)k;
    neverc_bigint_free(&m); neverc_bigint_free(&c);
    free(em);
    return 0;
}

/*
 * Private-key exponentiation m = c^d mod n via the CRT (Garner's recombination):
 *   m1 = c^dp mod p,  m2 = c^dq mod q,  h = qinv*(m1-m2) mod p,  m = m2 + h*q.
 * The two exponentiations are over half-size (p,q) moduli, so each costs ~1/8 of
 * the full c^d mod n; together with the recombination this is the standard
 * ~3-4x faster RSA private operation. dp/dq/qinv are precomputed at key gen.
 * Falls back to the plain full-width exponent if the CRT factors are absent.
 */
static void rsa_private_exp(neverc_bigint_t *out, const neverc_bigint_t *base,
                            const neverc_rsa_private_key_t *priv) {
    if (priv->p.len == 0 || priv->q.len == 0) {
        neverc_bigint_exp(out, base, &priv->d, &priv->pub.n);
        return;
    }
    neverc_bigint_t m1, m2, h, t;
    neverc_bigint_init(&m1); neverc_bigint_init(&m2);
    neverc_bigint_init(&h);  neverc_bigint_init(&t);

    neverc_bigint_exp(&m1, base, &priv->dp, &priv->p);   /* c^dp mod p */
    neverc_bigint_exp(&m2, base, &priv->dq, &priv->q);   /* c^dq mod q */

    neverc_bigint_sub(&h, &m1, &m2);                      /* m1 - m2 (may be < 0) */
    neverc_bigint_mod(&h, &h, &priv->p);                 /* normalize into [0,p) */
    neverc_bigint_mul(&h, &h, &priv->qinv);
    neverc_bigint_mod(&h, &h, &priv->p);                 /* h = qinv*(m1-m2) mod p */

    neverc_bigint_mul(&t, &h, &priv->q);
    neverc_bigint_add(out, &m2, &t);                     /* m = m2 + h*q, in [0,n) */

    neverc_bigint_free(&m1); neverc_bigint_free(&m2);
    neverc_bigint_free(&h);  neverc_bigint_free(&t);
}

int neverc_rsa_decrypt_pkcs1v15(const neverc_rsa_private_key_t *priv,
                                 const unsigned char *ct, size_t ct_len,
                                 unsigned char *out, size_t out_cap, size_t *out_len) {
    int k = neverc_rsa_key_size(&priv->pub);
    if ((int)ct_len != k) return -1;

    neverc_bigint_t c, m;
    neverc_bigint_init(&c); neverc_bigint_init(&m);
    bytes_to_bigint(&c, ct, ct_len);
    rsa_private_exp(&m, &c, priv);

    unsigned char *em = (unsigned char *)malloc((size_t)k);
    bigint_to_bytes(&m, em, k);

    if (em[0] != 0x00 || em[1] != 0x02) {
        neverc_bigint_free(&c); neverc_bigint_free(&m);
        free(em);
        return -1;
    }

    int i = 2;
    while (i < k && em[i] != 0) i++;
    if (i >= k) {
        neverc_bigint_free(&c); neverc_bigint_free(&m);
        free(em);
        return -1;
    }
    i++;

    size_t msg_len = (size_t)(k - i);
    if (msg_len > out_cap) {
        neverc_bigint_free(&c); neverc_bigint_free(&m);
        free(em);
        return -1;
    }

    memcpy(out, em + i, msg_len);
    if (out_len) *out_len = msg_len;

    neverc_bigint_free(&c); neverc_bigint_free(&m);
    free(em);
    return 0;
}

static const unsigned char sha256_digest_info[] = {
    0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86,
    0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01, 0x05,
    0x00, 0x04, 0x20
};
#define SHA256_DI_LEN 19

int neverc_rsa_sign_pkcs1v15_sha256(const neverc_rsa_private_key_t *priv,
                                     const unsigned char *hash, size_t hash_len,
                                     unsigned char *sig, size_t sig_cap, size_t *sig_len) {
    if (hash_len != 32) return -1;
    int k = neverc_rsa_key_size(&priv->pub);
    int t_len = SHA256_DI_LEN + 32;
    if (k < t_len + 11) return -1;
    if ((int)sig_cap < k) return -1;

    unsigned char *em = (unsigned char *)malloc((size_t)k);
    em[0] = 0x00;
    em[1] = 0x01;
    int ps_len = k - t_len - 3;
    memset(em + 2, 0xFF, (size_t)ps_len);
    em[2 + ps_len] = 0x00;
    memcpy(em + 3 + ps_len, sha256_digest_info, SHA256_DI_LEN);
    memcpy(em + 3 + ps_len + SHA256_DI_LEN, hash, 32);

    neverc_bigint_t m, s;
    neverc_bigint_init(&m); neverc_bigint_init(&s);
    bytes_to_bigint(&m, em, (size_t)k);
    rsa_private_exp(&s, &m, priv);
    bigint_to_bytes(&s, sig, k);

    if (sig_len) *sig_len = (size_t)k;
    neverc_bigint_free(&m); neverc_bigint_free(&s);
    free(em);
    return 0;
}

int neverc_rsa_verify_pkcs1v15_sha256(const neverc_rsa_public_key_t *pub,
                                       const unsigned char *hash, size_t hash_len,
                                       const unsigned char *sig, size_t sig_len) {
    if (hash_len != 32) return -1;
    int k = neverc_rsa_key_size(pub);
    if ((int)sig_len != k) return -1;

    neverc_bigint_t s, m;
    neverc_bigint_init(&s); neverc_bigint_init(&m);
    bytes_to_bigint(&s, sig, sig_len);
    neverc_bigint_exp(&m, &s, &pub->e, &pub->n);

    unsigned char *em = (unsigned char *)malloc((size_t)k);
    bigint_to_bytes(&m, em, k);

    int ok = 1;
    if (em[0] != 0x00 || em[1] != 0x01) ok = 0;

    int t_len = SHA256_DI_LEN + 32;
    int ps_len = k - t_len - 3;
    for (int i = 0; i < ps_len && ok; i++)
        if (em[2 + i] != 0xFF) ok = 0;
    if (ok && em[2 + ps_len] != 0x00) ok = 0;
    if (ok && memcmp(em + 3 + ps_len, sha256_digest_info, SHA256_DI_LEN) != 0) ok = 0;
    if (ok && memcmp(em + 3 + ps_len + SHA256_DI_LEN, hash, 32) != 0) ok = 0;

    neverc_bigint_free(&s); neverc_bigint_free(&m);
    free(em);
    return ok ? 0 : -1;
}

typedef enum {
    RSA_HASH_SHA256,
    RSA_HASH_SHA384,
    RSA_HASH_SHA512
} rsa_hash_kind_t;

static size_t rsa_hash_size(rsa_hash_kind_t hash_kind) {
    switch (hash_kind) {
    case RSA_HASH_SHA256:
        return NEVERC_SHA256_DIGEST_SIZE;
    case RSA_HASH_SHA384:
        return NEVERC_SHA384_DIGEST_SIZE;
    case RSA_HASH_SHA512:
        return NEVERC_SHA512_DIGEST_SIZE;
    }
    return 0;
}

static void rsa_hash_sum(rsa_hash_kind_t hash_kind,
                         const unsigned char *data, size_t data_len,
                         unsigned char *digest) {
    switch (hash_kind) {
    case RSA_HASH_SHA256:
        neverc_sha256_sum(data, data_len, digest);
        break;
    case RSA_HASH_SHA384:
        neverc_sha384_sum(data, data_len, digest);
        break;
    case RSA_HASH_SHA512:
        neverc_sha512_sum(data, data_len, digest);
        break;
    }
}

static void mgf1(rsa_hash_kind_t hash_kind,
                 const unsigned char *seed, size_t seed_len,
                 unsigned char *mask, size_t mask_len) {
    size_t digest_len = rsa_hash_size(hash_kind);
    uint32_t counter = 0;
    size_t offset = 0;
    while (offset < mask_len) {
        unsigned char input[NEVERC_SHA512_DIGEST_SIZE + 4];
        unsigned char digest[NEVERC_SHA512_DIGEST_SIZE];
        memcpy(input, seed, seed_len);
        input[seed_len] = (unsigned char)(counter >> 24);
        input[seed_len + 1] = (unsigned char)(counter >> 16);
        input[seed_len + 2] = (unsigned char)(counter >> 8);
        input[seed_len + 3] = (unsigned char)counter;
        rsa_hash_sum(hash_kind, input, seed_len + 4, digest);

        size_t remaining = mask_len - offset;
        size_t count = remaining < digest_len ? remaining : digest_len;
        memcpy(mask + offset, digest, count);
        offset += count;
        ++counter;
    }
}

static int constant_time_equal(const unsigned char *left,
                               const unsigned char *right, size_t len) {
    unsigned char difference = 0;
    for (size_t i = 0; i < len; ++i)
        difference |= left[i] ^ right[i];
    return difference == 0;
}

static int rsa_verify_pss(const neverc_rsa_public_key_t *pub,
                          const unsigned char *hash, size_t hash_len,
                          const unsigned char *sig, size_t sig_len,
                          rsa_hash_kind_t hash_kind) {
    const size_t digest_len = rsa_hash_size(hash_kind);
    const size_t salt_len = digest_len;
    if (!pub || !hash || !sig || hash_len != digest_len ||
        digest_len == 0 ||
        neverc_bigint_sign(&pub->n) <= 0 ||
        neverc_bigint_sign(&pub->e) <= 0)
        return -1;

    int modulus_bits = neverc_bigint_bit_len(&pub->n);
    int key_bytes = neverc_rsa_key_size(pub);
    if (modulus_bits <= 1 || key_bytes <= 0 ||
        key_bytes > 512 || sig_len != (size_t)key_bytes)
        return -1;

    size_t encoded_bits = (size_t)modulus_bits - 1;
    size_t encoded_len = (encoded_bits + 7) / 8;
    if (encoded_len < digest_len + salt_len + 2 ||
        encoded_len > (size_t)key_bytes)
        return -1;

    neverc_bigint_t signature, message;
    neverc_bigint_init(&signature);
    neverc_bigint_init(&message);
    bytes_to_bigint(&signature, sig, sig_len);
    int result = -1;
    if (neverc_bigint_cmp(&signature, &pub->n) >= 0)
        goto cleanup_bigints;
    neverc_bigint_exp(&message, &signature, &pub->e, &pub->n);

    unsigned char *encoded =
        (unsigned char *)malloc((size_t)key_bytes);
    unsigned char *database_mask =
        (unsigned char *)malloc(encoded_len - digest_len - 1);
    if (!encoded || !database_mask) {
        free(encoded);
        free(database_mask);
        goto cleanup_bigints;
    }
    bigint_to_bytes(&message, encoded, key_bytes);

    size_t leading_len = (size_t)key_bytes - encoded_len;
    for (size_t i = 0; i < leading_len; ++i) {
        if (encoded[i] != 0)
            goto cleanup_encoded;
    }
    unsigned char *encoded_message = encoded + leading_len;
    size_t database_len = encoded_len - digest_len - 1;
    unsigned char *encoded_hash = encoded_message + database_len;
    if (encoded_message[encoded_len - 1] != 0xbc)
        goto cleanup_encoded;

    unsigned unused_bits = (unsigned)(encoded_len * 8 - encoded_bits);
    if (unused_bits > 0) {
        unsigned char forbidden =
            (unsigned char)(0xffu << (8 - unused_bits));
        if ((encoded_message[0] & forbidden) != 0)
            goto cleanup_encoded;
    }

    mgf1(hash_kind, encoded_hash, digest_len,
         database_mask, database_len);
    for (size_t i = 0; i < database_len; ++i)
        encoded_message[i] ^= database_mask[i];
    if (unused_bits > 0)
        encoded_message[0] &=
            (unsigned char)(0xffu >> unused_bits);

    size_t padding_len = encoded_len - digest_len - salt_len - 2;
    unsigned invalid_padding = encoded_message[padding_len] ^ 0x01u;
    for (size_t i = 0; i < padding_len; ++i)
        invalid_padding |= encoded_message[i];
    if (invalid_padding != 0)
        goto cleanup_encoded;

    unsigned char message_prime[8 + NEVERC_SHA512_DIGEST_SIZE * 2] = {0};
    memcpy(message_prime + 8, hash, digest_len);
    memcpy(message_prime + 8 + digest_len,
           encoded_message + padding_len + 1, salt_len);
    unsigned char expected_hash[NEVERC_SHA512_DIGEST_SIZE];
    rsa_hash_sum(hash_kind, message_prime,
                 8 + digest_len + salt_len, expected_hash);
    if (constant_time_equal(
            encoded_hash, expected_hash, digest_len))
        result = 0;

cleanup_encoded:
    free(database_mask);
    free(encoded);
cleanup_bigints:
    neverc_bigint_free(&signature);
    neverc_bigint_free(&message);
    return result;
}

int neverc_rsa_verify_pss_sha256(const neverc_rsa_public_key_t *pub,
                                  const unsigned char *hash, size_t hash_len,
                                  const unsigned char *sig, size_t sig_len) {
    return rsa_verify_pss(pub, hash, hash_len, sig, sig_len,
                          RSA_HASH_SHA256);
}

int neverc_rsa_verify_pss_sha384(const neverc_rsa_public_key_t *pub,
                                  const unsigned char *hash, size_t hash_len,
                                  const unsigned char *sig, size_t sig_len) {
    return rsa_verify_pss(pub, hash, hash_len, sig, sig_len,
                          RSA_HASH_SHA384);
}

int neverc_rsa_verify_pss_sha512(const neverc_rsa_public_key_t *pub,
                                  const unsigned char *hash, size_t hash_len,
                                  const unsigned char *sig, size_t sig_len) {
    return rsa_verify_pss(pub, hash, hash_len, sig, sig_len,
                          RSA_HASH_SHA512);
}
