#include "neverc/crypto/ecdsa.h"
#include "neverc/_platform.h"
#include <string.h>
#include <stdlib.h>

#define ecdsa_random neverc_platform_random

static void random_mod_n(neverc_bigint_t *r, const neverc_bigint_t *n) {
    int bits = neverc_bigint_bit_len(n);
    int bytes = (bits + 7) / 8;
    unsigned char *buf = (unsigned char *)malloc((size_t)bytes);
    char hex[1024];

    for (;;) {
        ecdsa_random(buf, (size_t)bytes);
        int pos = 0;
        for (int i = 0; i < bytes && pos < (int)sizeof(hex) - 2; i++) {
            hex[pos++] = "0123456789abcdef"[buf[i] >> 4];
            hex[pos++] = "0123456789abcdef"[buf[i] & 0x0F];
        }
        hex[pos] = '\0';
        neverc_bigint_set_string(r, hex, 16);
        if (!neverc_bigint_is_zero(r) && neverc_bigint_cmp(r, n) < 0)
            break;
    }
    free(buf);
}

static void mod_inv_ec(neverc_bigint_t *r, const neverc_bigint_t *a, const neverc_bigint_t *p) {
    neverc_bigint_t exp, two;
    neverc_bigint_init(&exp); neverc_bigint_init(&two);
    neverc_bigint_set_int64(&two, 2);
    neverc_bigint_sub(&exp, p, &two);
    neverc_bigint_exp(r, a, &exp, p);
    neverc_bigint_free(&exp); neverc_bigint_free(&two);
}

void neverc_ecdsa_public_key_init(neverc_ecdsa_public_key_t *k) {
    k->curve = NULL;
    neverc_elliptic_point_init(&k->pub);
}
void neverc_ecdsa_public_key_free(neverc_ecdsa_public_key_t *k) {
    neverc_elliptic_point_free(&k->pub);
}
void neverc_ecdsa_private_key_init(neverc_ecdsa_private_key_t *k) {
    neverc_ecdsa_public_key_init(&k->pub);
    neverc_bigint_init(&k->d);
}
void neverc_ecdsa_private_key_free(neverc_ecdsa_private_key_t *k) {
    neverc_ecdsa_public_key_free(&k->pub);
    neverc_bigint_free(&k->d);
}
void neverc_ecdsa_signature_init(neverc_ecdsa_signature_t *sig) {
    neverc_bigint_init(&sig->r);
    neverc_bigint_init(&sig->s);
}
void neverc_ecdsa_signature_free(neverc_ecdsa_signature_t *sig) {
    neverc_bigint_free(&sig->r);
    neverc_bigint_free(&sig->s);
}

int neverc_ecdsa_generate_key(neverc_ecdsa_private_key_t *key,
                               const neverc_elliptic_curve_t *curve) {
    key->pub.curve = curve;
    random_mod_n(&key->d, &curve->n);
    neverc_elliptic_scalar_base_mult(curve, &key->pub.pub, &key->d);
    return 0;
}

static void hash_to_int(neverc_bigint_t *r, const unsigned char *hash, size_t hash_len,
                          const neverc_bigint_t *n) {
    char hex[256];
    int pos = 0;
    int order_bytes = (neverc_bigint_bit_len(n) + 7) / 8;
    size_t use = hash_len < (size_t)order_bytes ? hash_len : (size_t)order_bytes;

    for (size_t i = 0; i < use && pos < (int)sizeof(hex) - 2; i++) {
        hex[pos++] = "0123456789abcdef"[hash[i] >> 4];
        hex[pos++] = "0123456789abcdef"[hash[i] & 0x0F];
    }
    hex[pos] = '\0';
    neverc_bigint_set_string(r, hex, 16);

    if (neverc_bigint_cmp(r, n) >= 0)
        neverc_bigint_mod(r, r, n);
}

int neverc_ecdsa_sign(const neverc_ecdsa_private_key_t *key,
                       const unsigned char *hash, size_t hash_len,
                       neverc_ecdsa_signature_t *sig) {
    const neverc_elliptic_curve_t *curve = key->pub.curve;
    neverc_bigint_t k, e, kinv, tmp;
    neverc_bigint_init(&k); neverc_bigint_init(&e);
    neverc_bigint_init(&kinv); neverc_bigint_init(&tmp);

    hash_to_int(&e, hash, hash_len, &curve->n);

    for (;;) {
        random_mod_n(&k, &curve->n);

        neverc_elliptic_point_t R;
        neverc_elliptic_point_init(&R);
        neverc_elliptic_scalar_base_mult(curve, &R, &k);

        neverc_bigint_mod(&sig->r, &R.x, &curve->n);
        neverc_elliptic_point_free(&R);

        if (neverc_bigint_is_zero(&sig->r)) continue;

        mod_inv_ec(&kinv, &k, &curve->n);

        neverc_bigint_mul(&tmp, &key->d, &sig->r);
        neverc_bigint_add(&tmp, &e, &tmp);
        neverc_bigint_mul(&sig->s, &kinv, &tmp);
        neverc_bigint_mod(&sig->s, &sig->s, &curve->n);

        if (!neverc_bigint_is_zero(&sig->s)) break;
    }

    neverc_bigint_free(&k); neverc_bigint_free(&e);
    neverc_bigint_free(&kinv); neverc_bigint_free(&tmp);
    return 0;
}

int neverc_ecdsa_verify(const neverc_ecdsa_public_key_t *key,
                         const unsigned char *hash, size_t hash_len,
                         const neverc_ecdsa_signature_t *sig) {
    const neverc_elliptic_curve_t *curve = key->curve;

    if (neverc_bigint_is_zero(&sig->r) || neverc_bigint_is_zero(&sig->s))
        return -1;
    if (neverc_bigint_cmp(&sig->r, &curve->n) >= 0 ||
        neverc_bigint_cmp(&sig->s, &curve->n) >= 0)
        return -1;

    neverc_bigint_t e, sinv, u1, u2;
    neverc_bigint_init(&e); neverc_bigint_init(&sinv);
    neverc_bigint_init(&u1); neverc_bigint_init(&u2);

    hash_to_int(&e, hash, hash_len, &curve->n);
    mod_inv_ec(&sinv, &sig->s, &curve->n);

    neverc_bigint_mul(&u1, &e, &sinv);
    neverc_bigint_mod(&u1, &u1, &curve->n);

    neverc_bigint_mul(&u2, &sig->r, &sinv);
    neverc_bigint_mod(&u2, &u2, &curve->n);

    neverc_elliptic_point_t p1, p2, R;
    neverc_elliptic_point_init(&p1);
    neverc_elliptic_point_init(&p2);
    neverc_elliptic_point_init(&R);

    neverc_elliptic_scalar_base_mult(curve, &p1, &u1);
    neverc_elliptic_scalar_mult(curve, &p2, &key->pub, &u2);
    neverc_elliptic_add(curve, &R, &p1, &p2);

    neverc_bigint_t v;
    neverc_bigint_init(&v);
    neverc_bigint_mod(&v, &R.x, &curve->n);

    int ok = (neverc_bigint_cmp(&v, &sig->r) == 0) ? 0 : -1;

    neverc_bigint_free(&e); neverc_bigint_free(&sinv);
    neverc_bigint_free(&u1); neverc_bigint_free(&u2);
    neverc_bigint_free(&v);
    neverc_elliptic_point_free(&p1);
    neverc_elliptic_point_free(&p2);
    neverc_elliptic_point_free(&R);
    return ok;
}
