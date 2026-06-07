#include "neverc/std/crypto/ecdh.h"
#include "neverc/std/crypto/elliptic.h"
#include "neverc/std/crypto/rand.h"
#include "neverc/std/_platform.h"
#include <string.h>
#include <stdio.h>

/* ---- X25519: TweetNaCl-style 16x int64 limb representation ---- */

typedef int64_t gf[16];

static void gf_copy(gf o, const gf a) { for (int i=0;i<16;i++) o[i]=a[i]; }
static void gf_zero(gf o) { for (int i=0;i<16;i++) o[i]=0; }
static void gf_one(gf o)  { gf_zero(o); o[0]=1; }

static void gf_add(gf o, const gf a, const gf b) {
    for (int i=0;i<16;i++) o[i]=a[i]+b[i];
}

static void gf_sub(gf o, const gf a, const gf b) {
    for (int i=0;i<16;i++) o[i]=a[i]-b[i];
}

static void gf_mul(gf o, const gf a, const gf b) {
    int64_t t[31];
    for (int i=0;i<31;i++) t[i]=0;
    for (int i=0;i<16;i++)
        for (int j=0;j<16;j++)
            t[i+j] += a[i]*b[j];
    for (int i=16;i<31;i++) t[i-16] += 38*t[i];
    for (int i=0;i<16;i++) o[i]=t[i];
    /* carry */
    int64_t c;
    for (int j=0;j<2;j++) {
        for (int i=0;i<15;i++) { c=o[i]>>16; o[i+1]+=c; o[i]-=c<<16; }
        c=o[15]>>16; o[0]+=38*c; o[15]-=c<<16;
    }
}

static void gf_sq(gf o, const gf a) { gf_mul(o, a, a); }

static void gf_mul_a24(gf o, const gf a) {
    /* a24 = (486662 - 2) / 4 = 121665 for Curve25519 */
    int64_t t[16], c;
    for (int i=0;i<16;i++) t[i] = a[i]*121665;
    for (int i=0;i<15;i++) { c=t[i]>>16; t[i+1]+=c; t[i]-=c<<16; }
    c=t[15]>>16; t[0]+=38*c; t[15]-=c<<16;
    for (int i=0;i<16;i++) o[i]=t[i];
}

static void gf_cswap(gf p, gf q, int b) {
    int64_t t, mask = -(int64_t)b;
    for (int i=0;i<16;i++) { t = mask & (p[i] ^ q[i]); p[i] ^= t; q[i] ^= t; }
}

static void gf_carry(gf o) {
    int64_t c;
    for (int i=0;i<15;i++) { c=o[i]>>16; o[i+1]+=c; o[i]-=c<<16; }
    c=o[15]>>16; o[0]+=38*c; o[15]-=c<<16;
}

static void gf_sel(gf p, gf q, int b) {
    /* select: if b==1, swap p and q; else leave them */
    int64_t t, mask = -(int64_t)b;
    for (int i=0;i<16;i++) { t=mask&(p[i]^q[i]); p[i]^=t; q[i]^=t; }
}

static void gf_pack(unsigned char out[32], const gf n) {
    gf m, t;
    gf_copy(t, n);
    gf_carry(t); gf_carry(t); gf_carry(t);

    for (int j=0;j<2;j++) {
        m[0] = t[0] - 0xffed;
        for (int i=1;i<15;i++) {
            m[i] = t[i] - 0xffff - ((m[i-1]>>16) & 1);
            m[i-1] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14]>>16) & 1);
        int b = (int)((m[15]>>16) & 1);
        m[14] &= 0xffff;
        gf_sel(t, m, 1-b);
    }
    for (int i=0;i<16;i++) {
        out[2*i]   = (unsigned char)(t[i] & 0xff);
        out[2*i+1] = (unsigned char)(t[i] >> 8);
    }
}

static void gf_unpack(gf o, const unsigned char in[32]) {
    for (int i=0;i<16;i++) o[i]=(int64_t)in[2*i] + ((int64_t)in[2*i+1]<<8);
    o[15] &= 0x7fff;
}

static void gf_invert(gf o, const gf a) {
    gf c;
    gf_copy(c, a);
    for (int i=253;i>=0;i--) {
        gf_sq(c, c);
        if (i!=2 && i!=4) gf_mul(c, c, a);
    }
    gf_copy(o, c);
}

/* X25519 scalar multiplication — TweetNaCl algorithm (RFC 7748) */
static void x25519_scalar_mult(unsigned char out[32],
                                const unsigned char scalar[32],
                                const unsigned char point[32]) {
    unsigned char z[32];
    gf x, a, b, c, d, e, f;

    memcpy(z, scalar, 32);
    z[0]  &= 248;
    z[31] &= 127;
    z[31] |= 64;

    gf_unpack(x, point);
    gf_copy(b, x);
    gf_one(a);
    gf_zero(c);
    gf_one(d);

    for (int i = 254; i >= 0; i--) {
        int r = (z[i >> 3] >> (i & 7)) & 1;
        gf_cswap(a, b, r);
        gf_cswap(c, d, r);

        gf_add(e, a, c);
        gf_sub(a, a, c);
        gf_add(c, b, d);
        gf_sub(b, b, d);
        gf_sq(d, e);
        gf_sq(f, a);
        gf_mul(a, c, a);
        gf_mul(c, b, e);
        gf_add(e, a, c);
        gf_sub(a, a, c);
        gf_sq(b, a);
        gf_sub(c, d, f);
        gf_mul_a24(a, c);
        gf_add(a, a, d);
        gf_mul(c, c, a);
        gf_mul(a, d, f);
        gf_mul(d, b, x);
        gf_sq(b, e);

        gf_cswap(a, b, r);
        gf_cswap(c, d, r);
    }

    gf_invert(c, c);
    gf_mul(a, a, c);
    gf_pack(out, a);
}

static int is_all_zero(const unsigned char *buf, size_t len) {
    unsigned char acc = 0;
    for (size_t i = 0; i < len; i++) acc |= buf[i];
    return acc == 0;
}

/* ---- NIST curve ECDH helpers ---- */

static int nist_privkey_size(neverc_ecdh_curve_t curve) {
    return curve == NEVERC_ECDH_CURVE_P256 ? 32 : 48;
}

static int nist_pubkey_size(neverc_ecdh_curve_t curve) {
    return curve == NEVERC_ECDH_CURVE_P256 ? 65 : 97;
}

static int nist_shared_size(neverc_ecdh_curve_t curve) {
    return curve == NEVERC_ECDH_CURVE_P256 ? 32 : 48;
}

static const neverc_elliptic_curve_t *get_nist_curve(neverc_ecdh_curve_t curve) {
    return curve == NEVERC_ECDH_CURVE_P256 ? neverc_elliptic_p256() : neverc_elliptic_p384();
}

static void bigint_to_bytes(const neverc_bigint_t *n, unsigned char *out, int len) {
    char hexbuf[200];
    int ret = neverc_bigint_string(n, 16, hexbuf, sizeof(hexbuf));
    if (ret <= 0) { memset(out, 0, (size_t)len); return; }
    size_t hlen = strlen(hexbuf);
    memset(out, 0, (size_t)len);
    for (size_t i = 0; i < hlen && i < (size_t)len * 2; i++) {
        char c = hexbuf[hlen - 1 - i];
        int v = (c >= '0' && c <= '9') ? c - '0' :
                (c >= 'a' && c <= 'f') ? c - 'a' + 10 :
                (c >= 'A' && c <= 'F') ? c - 'A' + 10 : 0;
        out[len - 1 - (int)(i / 2)] |= (unsigned char)(v << (4 * (i & 1)));
    }
}

static void bytes_to_bigint(neverc_bigint_t *n, const unsigned char *data, int len) {
    char hex[200];
    int pos = 0;
    for (int i = 0; i < len && pos < 198; i++)
        pos += snprintf(hex + pos, (size_t)(200 - pos), "%02x", data[i]);
    hex[pos] = '\0';
    neverc_bigint_set_string(n, hex, 16);
}

/* ---- Public API ---- */

int neverc_ecdh_generate_key(neverc_ecdh_curve_t curve, neverc_ecdh_key_t *key) {
    if (!key) return -1;
    memset(key, 0, sizeof(*key));
    key->curve = curve;

    if (curve == NEVERC_ECDH_CURVE_X25519) {
        key->privkey_len = 32;
        key->pubkey_len = 32;
        neverc_crypto_rand_read(key->private_key, 32);
        unsigned char basepoint[32] = {9};
        x25519_scalar_mult(key->public_key, key->private_key, basepoint);
        return 0;
    }

    const neverc_elliptic_curve_t *ec = get_nist_curve(curve);
    int psize = nist_privkey_size(curve);
    key->privkey_len = psize;
    key->pubkey_len = nist_pubkey_size(curve);

    /* Generate random scalar in [1, n-1] */
    neverc_bigint_t scalar;
    neverc_bigint_init(&scalar);
    do {
        unsigned char rbuf[48];
        neverc_crypto_rand_read(rbuf, (size_t)psize);
        bytes_to_bigint(&scalar, rbuf, psize);
        neverc_bigint_mod(&scalar, &scalar, &ec->n);
    } while (neverc_bigint_is_zero(&scalar));

    bigint_to_bytes(&scalar, key->private_key, psize);

    neverc_elliptic_point_t pub;
    neverc_elliptic_point_init(&pub);
    neverc_elliptic_scalar_base_mult(ec, &pub, &scalar);
    size_t out_len = 0;
    neverc_elliptic_marshal(ec, &pub, key->public_key, (size_t)key->pubkey_len, &out_len);

    neverc_elliptic_point_free(&pub);
    neverc_bigint_free(&scalar);
    return 0;
}

int neverc_ecdh_new_private_key(neverc_ecdh_curve_t curve,
                                const unsigned char *privkey, size_t len,
                                neverc_ecdh_key_t *key) {
    if (!key || !privkey) return -1;
    memset(key, 0, sizeof(*key));
    key->curve = curve;

    if (curve == NEVERC_ECDH_CURVE_X25519) {
        if (len != 32) return -1;
        key->privkey_len = 32;
        key->pubkey_len = 32;
        memcpy(key->private_key, privkey, 32);
        unsigned char basepoint[32] = {9};
        x25519_scalar_mult(key->public_key, key->private_key, basepoint);
        return 0;
    }

    int psize = nist_privkey_size(curve);
    if ((int)len != psize) return -1;

    const neverc_elliptic_curve_t *ec = get_nist_curve(curve);
    key->privkey_len = psize;
    key->pubkey_len = nist_pubkey_size(curve);
    memcpy(key->private_key, privkey, (size_t)psize);

    neverc_bigint_t scalar;
    neverc_bigint_init(&scalar);
    bytes_to_bigint(&scalar, privkey, psize);

    if (neverc_bigint_is_zero(&scalar) || neverc_bigint_cmp(&scalar, &ec->n) >= 0) {
        neverc_bigint_free(&scalar);
        return -1;
    }

    neverc_elliptic_point_t pub;
    neverc_elliptic_point_init(&pub);
    neverc_elliptic_scalar_base_mult(ec, &pub, &scalar);
    size_t out_len = 0;
    neverc_elliptic_marshal(ec, &pub, key->public_key, (size_t)key->pubkey_len, &out_len);

    neverc_elliptic_point_free(&pub);
    neverc_bigint_free(&scalar);
    return 0;
}

int neverc_ecdh_new_public_key(neverc_ecdh_curve_t curve,
                               const unsigned char *pubkey, size_t len,
                               neverc_ecdh_key_t *key) {
    if (!key || !pubkey) return -1;
    memset(key, 0, sizeof(*key));
    key->curve = curve;

    if (curve == NEVERC_ECDH_CURVE_X25519) {
        if (len != 32) return -1;
        key->pubkey_len = 32;
        memcpy(key->public_key, pubkey, 32);
        return 0;
    }

    int expected = nist_pubkey_size(curve);
    if ((int)len != expected || pubkey[0] != 0x04) return -1;

    const neverc_elliptic_curve_t *ec = get_nist_curve(curve);
    neverc_elliptic_point_t pt;
    neverc_elliptic_point_init(&pt);
    if (neverc_elliptic_unmarshal(ec, &pt, pubkey, len) != 0) {
        neverc_elliptic_point_free(&pt);
        return -1;
    }
    if (!neverc_elliptic_is_on_curve(ec, &pt)) {
        neverc_elliptic_point_free(&pt);
        return -1;
    }
    neverc_elliptic_point_free(&pt);

    key->pubkey_len = expected;
    memcpy(key->public_key, pubkey, (size_t)expected);
    return 0;
}

int neverc_ecdh_compute(const neverc_ecdh_key_t *local_private,
                        const unsigned char *remote_pubkey, size_t remote_len,
                        unsigned char *out, size_t out_cap) {
    if (!local_private || !remote_pubkey || !out) return -1;

    if (local_private->curve == NEVERC_ECDH_CURVE_X25519) {
        if (remote_len != 32 || out_cap < 32) return -1;
        x25519_scalar_mult(out, local_private->private_key, remote_pubkey);
        if (is_all_zero(out, 32)) return -1;
        return 32;
    }

    neverc_ecdh_curve_t curve = local_private->curve;
    int shared_size = nist_shared_size(curve);
    if ((int)out_cap < shared_size) return -1;

    const neverc_elliptic_curve_t *ec = get_nist_curve(curve);

    neverc_elliptic_point_t remote;
    neverc_elliptic_point_init(&remote);
    if (neverc_elliptic_unmarshal(ec, &remote, remote_pubkey, remote_len) != 0) {
        neverc_elliptic_point_free(&remote);
        return -1;
    }

    neverc_bigint_t scalar;
    neverc_bigint_init(&scalar);
    bytes_to_bigint(&scalar, local_private->private_key, local_private->privkey_len);

    neverc_elliptic_point_t result;
    neverc_elliptic_point_init(&result);
    neverc_elliptic_scalar_mult(ec, &result, &remote, &scalar);

    bigint_to_bytes(&result.x, out, shared_size);

    neverc_elliptic_point_free(&result);
    neverc_elliptic_point_free(&remote);
    neverc_bigint_free(&scalar);
    return shared_size;
}

int neverc_ecdh_public_key_bytes(const neverc_ecdh_key_t *key,
                                 unsigned char *out, size_t out_cap) {
    if (!key || !out || (int)out_cap < key->pubkey_len) return -1;
    memcpy(out, key->public_key, (size_t)key->pubkey_len);
    return key->pubkey_len;
}

int neverc_ecdh_private_key_bytes(const neverc_ecdh_key_t *key,
                                  unsigned char *out, size_t out_cap) {
    if (!key || !out || (int)out_cap < key->privkey_len) return -1;
    memcpy(out, key->private_key, (size_t)key->privkey_len);
    return key->privkey_len;
}
