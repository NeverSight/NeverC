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

static void mod_inv_dsa(neverc_bigint_t *r, const neverc_bigint_t *a, const neverc_bigint_t *p) {
    neverc_bigint_t exp, two;
    neverc_bigint_init(&exp); neverc_bigint_init(&two);
    neverc_bigint_set_int64(&two, 2);
    neverc_bigint_sub(&exp, p, &two);
    neverc_bigint_exp(r, a, &exp, p);
    neverc_bigint_free(&exp); neverc_bigint_free(&two);
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

static void hash_to_int_dsa(neverc_bigint_t *r, const unsigned char *hash,
                             size_t hash_len, const neverc_bigint_t *q) {
    int q_bytes = (neverc_bigint_bit_len(q) + 7) / 8;
    size_t use = hash_len < (size_t)q_bytes ? hash_len : (size_t)q_bytes;
    char hex[256];
    int pos = 0;
    for (size_t i = 0; i < use && pos < (int)sizeof(hex) - 2; i++) {
        hex[pos++] = "0123456789abcdef"[hash[i] >> 4];
        hex[pos++] = "0123456789abcdef"[hash[i] & 0x0F];
    }
    hex[pos] = '\0';
    neverc_bigint_set_string(r, hex, 16);
    if (neverc_bigint_cmp(r, q) >= 0)
        neverc_bigint_mod(r, r, q);
}

int neverc_dsa_sign(const neverc_dsa_private_key_t *key,
                     const unsigned char *hash, size_t hash_len,
                     neverc_dsa_signature_t *sig) {
    if (!key || !hash || hash_len == 0 || !sig)
        return -1;

    neverc_bigint_t k, kinv, z, tmp;
    neverc_bigint_init(&k); neverc_bigint_init(&kinv);
    neverc_bigint_init(&z); neverc_bigint_init(&tmp);
    neverc_bigint_set_int64(&sig->r, 0);
    neverc_bigint_set_int64(&sig->s, 0);

    hash_to_int_dsa(&z, hash, hash_len, &key->pub.q);

    int result = -1;
    for (int attempt = 0; attempt < 128; ++attempt) {
        if (random_mod(&k, &key->pub.q) != 0)
            break;

        neverc_bigint_exp(&tmp, &key->pub.g, &k, &key->pub.p);
        neverc_bigint_mod(&sig->r, &tmp, &key->pub.q);
        if (neverc_bigint_is_zero(&sig->r)) continue;

        mod_inv_dsa(&kinv, &k, &key->pub.q);

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
    if (!key || !hash || hash_len == 0 || !sig)
        return -1;
    if (neverc_bigint_is_zero(&sig->r) || neverc_bigint_is_zero(&sig->s))
        return -1;
    if (neverc_bigint_cmp(&sig->r, &key->q) >= 0 ||
        neverc_bigint_cmp(&sig->s, &key->q) >= 0)
        return -1;

    neverc_bigint_t w, z, u1, u2, v1, v2, v;
    neverc_bigint_init(&w); neverc_bigint_init(&z);
    neverc_bigint_init(&u1); neverc_bigint_init(&u2);
    neverc_bigint_init(&v1); neverc_bigint_init(&v2);
    neverc_bigint_init(&v);

    mod_inv_dsa(&w, &sig->s, &key->q);
    hash_to_int_dsa(&z, hash, hash_len, &key->q);

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
