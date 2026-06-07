#include "neverc/crypto/rsa.h"
#include "neverc/crypto/sha256.h"
#include "neverc/_platform.h"
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
        if (miller_rabin(p, 12)) return;
    }
}

static void mod_inverse(neverc_bigint_t *r, const neverc_bigint_t *a, const neverc_bigint_t *m) {
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

    neverc_bigint_mod(r, &old_s, m);

    neverc_bigint_free(&old_r); neverc_bigint_free(&rr);
    neverc_bigint_free(&old_s); neverc_bigint_free(&s);
    neverc_bigint_free(&q); neverc_bigint_free(&tmp);
    neverc_bigint_free(&prod);
}

int neverc_rsa_generate_key(neverc_rsa_private_key_t *key, int bits) {
    if (bits < 512) return -1;
    int half = bits / 2;

    gen_prime(&key->p, half);
    gen_prime(&key->q, half);

    neverc_bigint_mul(&key->pub.n, &key->p, &key->q);
    neverc_bigint_set_int64(&key->pub.e, 65537);

    neverc_bigint_t one, pm1, qm1, phi;
    neverc_bigint_init(&one); neverc_bigint_init(&pm1);
    neverc_bigint_init(&qm1); neverc_bigint_init(&phi);

    neverc_bigint_set_int64(&one, 1);
    neverc_bigint_sub(&pm1, &key->p, &one);
    neverc_bigint_sub(&qm1, &key->q, &one);
    neverc_bigint_mul(&phi, &pm1, &qm1);

    mod_inverse(&key->d, &key->pub.e, &phi);

    neverc_bigint_mod(&key->dp, &key->d, &pm1);
    neverc_bigint_mod(&key->dq, &key->d, &qm1);
    mod_inverse(&key->qinv, &key->q, &key->p);

    neverc_bigint_free(&one); neverc_bigint_free(&pm1);
    neverc_bigint_free(&qm1); neverc_bigint_free(&phi);
    return 0;
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

int neverc_rsa_decrypt_pkcs1v15(const neverc_rsa_private_key_t *priv,
                                 const unsigned char *ct, size_t ct_len,
                                 unsigned char *out, size_t out_cap, size_t *out_len) {
    int k = neverc_rsa_key_size(&priv->pub);
    if ((int)ct_len != k) return -1;

    neverc_bigint_t c, m;
    neverc_bigint_init(&c); neverc_bigint_init(&m);
    bytes_to_bigint(&c, ct, ct_len);
    neverc_bigint_exp(&m, &c, &priv->d, &priv->pub.n);

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
    neverc_bigint_exp(&s, &m, &priv->d, &priv->pub.n);
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
