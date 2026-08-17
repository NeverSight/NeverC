#include "neverc/std/crypto/dsa.h"
#include "neverc/std/_platform.h"
#include <string.h>
#include <stdlib.h>

#ifndef NCI_DSA_RANDOM
#define NCI_DSA_RANDOM neverc_platform_random
#endif

static void dsa_bigint_secure_free(neverc_bigint_t *value) {
    if (!value) return;
    if (value->digits)
        neverc_platform_secure_zero(
            value->digits, value->cap * sizeof(*value->digits));
    neverc_bigint_free(value);
}

static int random_mod(neverc_bigint_t *r, const neverc_bigint_t *n) {
    int bits = neverc_bigint_bit_len(n);
    if (bits <= 1) return -1;
    int bytes = (bits + 7) / 8;
    unsigned char *buf = (unsigned char *)malloc((size_t)bytes);
    if (!buf) return -1;
    size_t hex_size = (size_t)bytes * 2 + 1;
    char *hex = (char *)malloc(hex_size);
    if (!hex) {
        neverc_platform_secure_zero(buf, (size_t)bytes);
        free(buf);
        return -1;
    }

    int result = -1;
    for (int attempt = 0; attempt < 128; ++attempt) {
        if (NCI_DSA_RANDOM(buf, (size_t)bytes) != 0)
            break;
        int pos = 0;
        for (int i = 0; i < bytes; i++) {
            hex[pos++] = "0123456789abcdef"[buf[i] >> 4];
            hex[pos++] = "0123456789abcdef"[buf[i] & 0x0F];
        }
        hex[pos] = '\0';
        if (neverc_bigint_set_string(r, hex, 16) != 0)
            break;
        if (!neverc_bigint_is_zero(r) && neverc_bigint_cmp(r, n) < 0) {
            result = 0;
            break;
        }
    }

    neverc_platform_secure_zero(buf, (size_t)bytes);
    neverc_platform_secure_zero(hex, hex_size);
    free(buf);
    free(hex);
    return result;
}

/* Fermat inverse a^(m-2) mod m, then require r*a ≡ 1 (mod m). Fermat is
 * wrong when m is composite; without the product check, w=0 makes
 * (r=1, s) verify for any hash when gcd(s, q) > 1. */
static int dsa_mod_inv(neverc_bigint_t *r, const neverc_bigint_t *a,
                       const neverc_bigint_t *m) {
    neverc_bigint_t exp, two, prod, one;
    neverc_bigint_init(&exp);
    neverc_bigint_init(&two);
    neverc_bigint_init(&prod);
    neverc_bigint_init(&one);
    neverc_bigint_set_int64(&two, 2);
    neverc_bigint_set_int64(&one, 1);
    neverc_bigint_sub(&exp, m, &two);
    neverc_bigint_exp(r, a, &exp, m);
    neverc_bigint_mul(&prod, r, a);
    neverc_bigint_mod(&prod, &prod, m);
    int ok = neverc_bigint_cmp(&prod, &one) == 0;
    neverc_bigint_free(&exp);
    neverc_bigint_free(&two);
    neverc_bigint_free(&prod);
    neverc_bigint_free(&one);
    return ok ? 0 : -1;
}

void neverc_dsa_public_key_init(neverc_dsa_public_key_t *k) {
    if (!k) return;
    neverc_bigint_init(&k->p); neverc_bigint_init(&k->q);
    neverc_bigint_init(&k->g); neverc_bigint_init(&k->y);
}
void neverc_dsa_public_key_free(neverc_dsa_public_key_t *k) {
    if (!k) return;
    neverc_bigint_free(&k->p); neverc_bigint_free(&k->q);
    neverc_bigint_free(&k->g); neverc_bigint_free(&k->y);
}
void neverc_dsa_private_key_init(neverc_dsa_private_key_t *k) {
    if (!k) return;
    neverc_dsa_public_key_init(&k->pub);
    neverc_bigint_init(&k->x);
}
void neverc_dsa_private_key_free(neverc_dsa_private_key_t *k) {
    if (!k) return;
    neverc_dsa_public_key_free(&k->pub);
    dsa_bigint_secure_free(&k->x);
}
void neverc_dsa_signature_init(neverc_dsa_signature_t *sig) {
    if (!sig) return;
    neverc_bigint_init(&sig->r); neverc_bigint_init(&sig->s);
}
void neverc_dsa_signature_free(neverc_dsa_signature_t *sig) {
    if (!sig) return;
    neverc_bigint_free(&sig->r); neverc_bigint_free(&sig->s);
}

/* FIPS 186-4: g and y must satisfy elem^q ≡ 1 (mod p). This rejects
 * order-2 elements (p-1) and any other value outside the q-subgroup. */
static int dsa_in_order_q_subgroup(const neverc_bigint_t *elem,
                                   const neverc_bigint_t *q,
                                   const neverc_bigint_t *p) {
    neverc_bigint_t result, one;
    neverc_bigint_init(&result);
    neverc_bigint_init(&one);
    neverc_bigint_set_int64(&one, 1);
    neverc_bigint_exp(&result, elem, q, p);
    int ok = neverc_bigint_cmp(&result, &one) == 0;
    neverc_bigint_free(&result);
    neverc_bigint_free(&one);
    return ok;
}

/* Fixed-base Miller-Rabin. p and q are attacker-supplied, so this is a
 * parameter screen rather than a cryptographic proof; combined with
 * q | (p-1) it rejects composite-q forgeries that g^q ≡ 1 alone would
 * accept, and composite-p forgeries that pass the subgroup check with
 * gcd(g, p) == 1 (e.g. p=91, q=3, g=9). */
static int dsa_is_probable_prime(const neverc_bigint_t *q) {
    neverc_bigint_t one, nm1, d, a, x;
    neverc_bigint_init(&one);
    neverc_bigint_init(&nm1);
    neverc_bigint_init(&d);
    neverc_bigint_init(&a);
    neverc_bigint_init(&x);

    neverc_bigint_set_int64(&one, 1);
    neverc_bigint_sub(&nm1, q, &one);
    neverc_bigint_set(&d, &nm1);
    int r = 0;
    while (neverc_bigint_bit(&d, 0) == 0) {
        neverc_bigint_rsh(&d, &d, 1);
        r++;
    }

    int result = 1;
    int witnesses[] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37};
    int nw = (int)(sizeof(witnesses) / sizeof(witnesses[0]));

    for (int i = 0; i < nw && result; i++) {
        neverc_bigint_set_int64(&a, witnesses[i]);
        if (neverc_bigint_cmp(&a, &nm1) >= 0)
            continue;

        neverc_bigint_exp(&x, &a, &d, q);

        if (neverc_bigint_cmp(&x, &one) == 0 ||
            neverc_bigint_cmp(&x, &nm1) == 0)
            continue;

        int found = 0;
        for (int j = 0; j < r - 1; j++) {
            neverc_bigint_mul(&x, &x, &x);
            neverc_bigint_mod(&x, &x, q);
            if (neverc_bigint_cmp(&x, &nm1) == 0) {
                found = 1;
                break;
            }
        }
        if (!found)
            result = 0;
    }

    neverc_bigint_free(&one);
    neverc_bigint_free(&nm1);
    neverc_bigint_free(&d);
    neverc_bigint_free(&a);
    neverc_bigint_free(&x);
    return result;
}

static int dsa_q_divides_p_minus_1(const neverc_dsa_public_key_t *key) {
    neverc_bigint_t one, pm1, rem;
    neverc_bigint_init(&one);
    neverc_bigint_init(&pm1);
    neverc_bigint_init(&rem);
    neverc_bigint_set_int64(&one, 1);
    neverc_bigint_sub(&pm1, &key->p, &one);
    neverc_bigint_mod(&rem, &pm1, &key->q);
    int ok = neverc_bigint_is_zero(&rem);
    neverc_bigint_free(&one);
    neverc_bigint_free(&pm1);
    neverc_bigint_free(&rem);
    return ok;
}

static int dsa_group_valid(const neverc_dsa_public_key_t *key) {
    if (!key || neverc_bigint_sign(&key->p) <= 0 ||
        neverc_bigint_sign(&key->q) <= 0 ||
        neverc_bigint_sign(&key->g) <= 0)
        return 0;
    /* q must be an odd prime (>= 3); p must be a larger odd prime.
     * g=1 makes every (r=1,s) verify when y=1. Composite q with g of
     * smaller order, q that does not divide p-1, or composite p with
     * g^q ≡ 1 (mod p), makes verify a forgery oracle. Caps match RSA:
     * p up to 16384 bits (512 limbs), q up to 256 bits (8 limbs). */
    if (neverc_bigint_bit_len(&key->q) < 2 ||
        neverc_bigint_bit(&key->q, 0) == 0 ||
        neverc_bigint_bit(&key->p, 0) == 0 ||
        key->p.len > 512 || key->q.len > 8 ||
        neverc_bigint_cmp(&key->q, &key->p) >= 0 ||
        neverc_bigint_cmp(&key->g, &key->p) >= 0)
        return 0;
    neverc_bigint_t one;
    neverc_bigint_init(&one);
    neverc_bigint_set_int64(&one, 1);
    int ok = neverc_bigint_cmp(&key->g, &one) > 0;
    neverc_bigint_free(&one);
    if (!ok)
        return 0;
    if (!dsa_q_divides_p_minus_1(key) ||
        !dsa_is_probable_prime(&key->q) ||
        !dsa_is_probable_prime(&key->p))
        return 0;
    return dsa_in_order_q_subgroup(&key->g, &key->q, &key->p);
}

static int dsa_public_valid(const neverc_dsa_public_key_t *key) {
    if (!dsa_group_valid(key) || neverc_bigint_sign(&key->y) <= 0 ||
        neverc_bigint_cmp(&key->y, &key->p) >= 0)
        return 0;
    neverc_bigint_t one;
    neverc_bigint_init(&one);
    neverc_bigint_set_int64(&one, 1);
    int ok = neverc_bigint_cmp(&key->y, &one) > 0;
    neverc_bigint_free(&one);
    if (!ok)
        return 0;
    return dsa_in_order_q_subgroup(&key->y, &key->q, &key->p);
}

static int hash_to_int_dsa(neverc_bigint_t *r, const unsigned char *hash,
                            size_t hash_len, const neverc_bigint_t *q) {
    if (!r || !hash || hash_len == 0 || !q)
        return -1;
    int q_bytes = (neverc_bigint_bit_len(q) + 7) / 8;
    if (q_bytes <= 0)
        return -1;
    size_t use = hash_len < (size_t)q_bytes ? hash_len : (size_t)q_bytes;
    char hex[256];
    int pos = 0;
    for (size_t i = 0; i < use && pos < (int)sizeof(hex) - 2; i++) {
        hex[pos++] = "0123456789abcdef"[hash[i] >> 4];
        hex[pos++] = "0123456789abcdef"[hash[i] & 0x0F];
    }
    hex[pos] = '\0';
    if (neverc_bigint_set_string(r, hex, 16) != 0)
        return -1;
    if (neverc_bigint_cmp(r, q) >= 0)
        neverc_bigint_mod(r, r, q);
    return 0;
}

int neverc_dsa_sign(const neverc_dsa_private_key_t *key,
                     const unsigned char *hash, size_t hash_len,
                     neverc_dsa_signature_t *sig) {
    if (!sig)
        return -1;
    neverc_bigint_set_int64(&sig->r, 0);
    neverc_bigint_set_int64(&sig->s, 0);
    if (!key || !hash || hash_len == 0)
        return -1;
    if (!dsa_group_valid(&key->pub) ||
        neverc_bigint_sign(&key->x) <= 0 ||
        neverc_bigint_cmp(&key->x, &key->pub.q) >= 0)
        return -1;

    neverc_bigint_t k, kinv, z, tmp;
    neverc_bigint_init(&k); neverc_bigint_init(&kinv);
    neverc_bigint_init(&z); neverc_bigint_init(&tmp);

    if (hash_to_int_dsa(&z, hash, hash_len, &key->pub.q) != 0) {
        neverc_bigint_free(&k); neverc_bigint_free(&kinv);
        neverc_bigint_free(&z); neverc_bigint_free(&tmp);
        return -1;
    }

    int result = -1;
    for (int attempt = 0; attempt < 128; ++attempt) {
        if (random_mod(&k, &key->pub.q) != 0)
            break;

        neverc_bigint_exp(&tmp, &key->pub.g, &k, &key->pub.p);
        neverc_bigint_mod(&sig->r, &tmp, &key->pub.q);
        if (neverc_bigint_is_zero(&sig->r)) continue;

        if (dsa_mod_inv(&kinv, &k, &key->pub.q) != 0)
            continue;

        neverc_bigint_mul(&tmp, &key->x, &sig->r);
        neverc_bigint_add(&tmp, &z, &tmp);
        neverc_bigint_mul(&sig->s, &kinv, &tmp);
        neverc_bigint_mod(&sig->s, &sig->s, &key->pub.q);
        if (!neverc_bigint_is_zero(&sig->s)) {
            result = 0;
            break;
        }
    }

    if (result != 0) {
        neverc_bigint_set_int64(&sig->r, 0);
        neverc_bigint_set_int64(&sig->s, 0);
    }
    if (k.digits)
        neverc_platform_secure_zero(k.digits, k.cap * sizeof(*k.digits));
    if (kinv.digits)
        neverc_platform_secure_zero(kinv.digits, kinv.cap * sizeof(*kinv.digits));
    if (tmp.digits)
        neverc_platform_secure_zero(tmp.digits, tmp.cap * sizeof(*tmp.digits));
    neverc_bigint_free(&k); neverc_bigint_free(&kinv);
    neverc_bigint_free(&z); neverc_bigint_free(&tmp);
    return result;
}

int neverc_dsa_verify(const neverc_dsa_public_key_t *key,
                       const unsigned char *hash, size_t hash_len,
                       const neverc_dsa_signature_t *sig) {
    if (!key || !hash || hash_len == 0 || !sig || !dsa_public_valid(key))
        return -1;
    if (neverc_bigint_sign(&sig->r) <= 0 || neverc_bigint_sign(&sig->s) <= 0)
        return -1;
    if (neverc_bigint_cmp(&sig->r, &key->q) >= 0 ||
        neverc_bigint_cmp(&sig->s, &key->q) >= 0)
        return -1;

    neverc_bigint_t w, z, u1, u2, v1, v2, v;
    neverc_bigint_init(&w); neverc_bigint_init(&z);
    neverc_bigint_init(&u1); neverc_bigint_init(&u2);
    neverc_bigint_init(&v1); neverc_bigint_init(&v2);
    neverc_bigint_init(&v);

    if (dsa_mod_inv(&w, &sig->s, &key->q) != 0 ||
        hash_to_int_dsa(&z, hash, hash_len, &key->q) != 0) {
        neverc_bigint_free(&w); neverc_bigint_free(&z);
        neverc_bigint_free(&u1); neverc_bigint_free(&u2);
        neverc_bigint_free(&v1); neverc_bigint_free(&v2);
        neverc_bigint_free(&v);
        return -1;
    }

    neverc_bigint_mul(&u1, &z, &w);
    neverc_bigint_mod(&u1, &u1, &key->q);

    neverc_bigint_mul(&u2, &sig->r, &w);
    neverc_bigint_mod(&u2, &u2, &key->q);

    neverc_bigint_exp(&v1, &key->g, &u1, &key->p);
    neverc_bigint_exp(&v2, &key->y, &u2, &key->p);
    neverc_bigint_mul(&v, &v1, &v2);
    neverc_bigint_mod(&v, &v, &key->p);
    neverc_bigint_mod(&v, &v, &key->q);

    int ok = (neverc_bigint_cmp(&v, &sig->r) == 0) ? 0 : -1;

    neverc_bigint_free(&w); neverc_bigint_free(&z);
    neverc_bigint_free(&u1); neverc_bigint_free(&u2);
    neverc_bigint_free(&v1); neverc_bigint_free(&v2);
    neverc_bigint_free(&v);
    return ok;
}
