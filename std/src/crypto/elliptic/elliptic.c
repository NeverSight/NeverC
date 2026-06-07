#include "neverc/std/crypto/elliptic.h"
#include <string.h>

static neverc_elliptic_curve_t g_p256;
static int g_p256_init = 0;
static neverc_elliptic_curve_t g_p384;
static int g_p384_init = 0;

static void mod_inv(neverc_bigint_t *r, const neverc_bigint_t *a, const neverc_bigint_t *p) {
    neverc_bigint_t exp;
    neverc_bigint_init(&exp);
    neverc_bigint_t two;
    neverc_bigint_init(&two);
    neverc_bigint_set_int64(&two, 2);
    neverc_bigint_sub(&exp, p, &two);
    neverc_bigint_exp(r, a, &exp, p);
    neverc_bigint_free(&exp);
    neverc_bigint_free(&two);
}

const neverc_elliptic_curve_t *neverc_elliptic_p256(void) {
    if (g_p256_init) return &g_p256;
    memset(&g_p256, 0, sizeof(g_p256));
    neverc_bigint_init(&g_p256.p);
    neverc_bigint_init(&g_p256.n);
    neverc_bigint_init(&g_p256.b);
    neverc_bigint_init(&g_p256.gx);
    neverc_bigint_init(&g_p256.gy);

    neverc_bigint_set_string(&g_p256.p,
        "FFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF", 16);
    neverc_bigint_set_string(&g_p256.n,
        "FFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551", 16);
    neverc_bigint_set_string(&g_p256.b,
        "5AC635D8AA3A93E7B3EBBD55769886BC651D06B0CC53B0F63BCE3C3E27D2604B", 16);
    neverc_bigint_set_string(&g_p256.gx,
        "6B17D1F2E12C4247F8BCE6E563A440F277037D812DEB33A0F4A13945D898C296", 16);
    neverc_bigint_set_string(&g_p256.gy,
        "4FE342E2FE1A7F9B8EE7EB4A7C0F9E162BCE33576B315ECECBB6406837BF51F5", 16);

    g_p256.bit_size = 256;
    g_p256.name = "P-256";
    g_p256_init = 1;
    return &g_p256;
}

const neverc_elliptic_curve_t *neverc_elliptic_p384(void) {
    if (g_p384_init) return &g_p384;
    memset(&g_p384, 0, sizeof(g_p384));
    neverc_bigint_init(&g_p384.p);
    neverc_bigint_init(&g_p384.n);
    neverc_bigint_init(&g_p384.b);
    neverc_bigint_init(&g_p384.gx);
    neverc_bigint_init(&g_p384.gy);

    neverc_bigint_set_string(&g_p384.p,
        "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFE"
        "FFFFFFFF0000000000000000FFFFFFFF", 16);
    neverc_bigint_set_string(&g_p384.n,
        "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFC7634D81F4372DDF"
        "581A0DB248B0A77AECEC196ACCC52973", 16);
    neverc_bigint_set_string(&g_p384.b,
        "B3312FA7E23EE7E4988E056BE3F82D19181D9C6EFE8141120314088F5013875A"
        "C656398D8A2ED19D2A85C8EDD3EC2AEF", 16);
    neverc_bigint_set_string(&g_p384.gx,
        "AA87CA22BE8B05378EB1C71EF320AD746E1D3B628BA79B9859F741E082542A38"
        "5502F25DBF55296C3A545E3872760AB7", 16);
    neverc_bigint_set_string(&g_p384.gy,
        "3617DE4A96262C6F5D9E98BF9292DC29F8F41DBD289A147CE9DA3113B5F0B8C0"
        "0A60B1CE1D7E819D7A431D7C90EA0E5F", 16);

    g_p384.bit_size = 384;
    g_p384.name = "P-384";
    g_p384_init = 1;
    return &g_p384;
}

void neverc_elliptic_point_init(neverc_elliptic_point_t *pt) {
    neverc_bigint_init(&pt->x);
    neverc_bigint_init(&pt->y);
}

void neverc_elliptic_point_free(neverc_elliptic_point_t *pt) {
    neverc_bigint_free(&pt->x);
    neverc_bigint_free(&pt->y);
}

int neverc_elliptic_is_on_curve(const neverc_elliptic_curve_t *curve,
                                 const neverc_elliptic_point_t *pt) {
    if (neverc_bigint_is_zero(&pt->x) && neverc_bigint_is_zero(&pt->y))
        return 1;

    neverc_bigint_t y2, x3, ax, rhs, three;
    neverc_bigint_init(&y2); neverc_bigint_init(&x3);
    neverc_bigint_init(&ax); neverc_bigint_init(&rhs);
    neverc_bigint_init(&three);

    neverc_bigint_mul(&y2, &pt->y, &pt->y);
    neverc_bigint_mod(&y2, &y2, &curve->p);

    neverc_bigint_mul(&x3, &pt->x, &pt->x);
    neverc_bigint_mod(&x3, &x3, &curve->p);
    neverc_bigint_mul(&x3, &x3, &pt->x);
    neverc_bigint_mod(&x3, &x3, &curve->p);

    neverc_bigint_set_int64(&three, 3);
    neverc_bigint_mul(&ax, &three, &pt->x);
    neverc_bigint_mod(&ax, &ax, &curve->p);

    neverc_bigint_sub(&rhs, &x3, &ax);
    neverc_bigint_add(&rhs, &rhs, &curve->b);
    neverc_bigint_mod(&rhs, &rhs, &curve->p);

    int eq = (neverc_bigint_cmp(&y2, &rhs) == 0);
    neverc_bigint_free(&y2); neverc_bigint_free(&x3);
    neverc_bigint_free(&ax); neverc_bigint_free(&rhs);
    neverc_bigint_free(&three);
    return eq;
}

void neverc_elliptic_add(const neverc_elliptic_curve_t *curve,
                          neverc_elliptic_point_t *r,
                          const neverc_elliptic_point_t *p1,
                          const neverc_elliptic_point_t *p2) {
    if (neverc_bigint_is_zero(&p1->x) && neverc_bigint_is_zero(&p1->y)) {
        neverc_bigint_set(&r->x, &p2->x);
        neverc_bigint_set(&r->y, &p2->y);
        return;
    }
    if (neverc_bigint_is_zero(&p2->x) && neverc_bigint_is_zero(&p2->y)) {
        neverc_bigint_set(&r->x, &p1->x);
        neverc_bigint_set(&r->y, &p1->y);
        return;
    }
    if (neverc_bigint_cmp(&p1->x, &p2->x) == 0 &&
        neverc_bigint_cmp(&p1->y, &p2->y) == 0) {
        neverc_elliptic_double(curve, r, p1);
        return;
    }
    if (neverc_bigint_cmp(&p1->x, &p2->x) == 0) {
        neverc_bigint_set_int64(&r->x, 0);
        neverc_bigint_set_int64(&r->y, 0);
        return;
    }

    neverc_bigint_t dx, dy, lam, lam2, rx, ry;
    neverc_bigint_init(&dx); neverc_bigint_init(&dy);
    neverc_bigint_init(&lam); neverc_bigint_init(&lam2);
    neverc_bigint_init(&rx); neverc_bigint_init(&ry);

    neverc_bigint_sub(&dx, &p2->x, &p1->x);
    neverc_bigint_mod(&dx, &dx, &curve->p);
    neverc_bigint_sub(&dy, &p2->y, &p1->y);
    neverc_bigint_mod(&dy, &dy, &curve->p);

    neverc_bigint_t inv;
    neverc_bigint_init(&inv);
    mod_inv(&inv, &dx, &curve->p);
    neverc_bigint_mul(&lam, &dy, &inv);
    neverc_bigint_mod(&lam, &lam, &curve->p);

    neverc_bigint_mul(&lam2, &lam, &lam);
    neverc_bigint_mod(&lam2, &lam2, &curve->p);

    neverc_bigint_sub(&rx, &lam2, &p1->x);
    neverc_bigint_sub(&rx, &rx, &p2->x);
    neverc_bigint_mod(&rx, &rx, &curve->p);

    neverc_bigint_sub(&ry, &p1->x, &rx);
    neverc_bigint_mul(&ry, &lam, &ry);
    neverc_bigint_sub(&ry, &ry, &p1->y);
    neverc_bigint_mod(&ry, &ry, &curve->p);

    neverc_bigint_set(&r->x, &rx);
    neverc_bigint_set(&r->y, &ry);

    neverc_bigint_free(&dx); neverc_bigint_free(&dy);
    neverc_bigint_free(&lam); neverc_bigint_free(&lam2);
    neverc_bigint_free(&rx); neverc_bigint_free(&ry);
    neverc_bigint_free(&inv);
}

void neverc_elliptic_double(const neverc_elliptic_curve_t *curve,
                             neverc_elliptic_point_t *r,
                             const neverc_elliptic_point_t *p) {
    if (neverc_bigint_is_zero(&p->y)) {
        neverc_bigint_set_int64(&r->x, 0);
        neverc_bigint_set_int64(&r->y, 0);
        return;
    }

    neverc_bigint_t x2, num, den, lam, lam2, rx, ry, two, three, inv;
    neverc_bigint_init(&x2); neverc_bigint_init(&num);
    neverc_bigint_init(&den); neverc_bigint_init(&lam);
    neverc_bigint_init(&lam2); neverc_bigint_init(&rx);
    neverc_bigint_init(&ry); neverc_bigint_init(&two);
    neverc_bigint_init(&three); neverc_bigint_init(&inv);

    neverc_bigint_set_int64(&two, 2);
    neverc_bigint_set_int64(&three, 3);

    neverc_bigint_mul(&x2, &p->x, &p->x);
    neverc_bigint_mod(&x2, &x2, &curve->p);
    neverc_bigint_mul(&num, &three, &x2);
    neverc_bigint_sub(&num, &num, &three);
    neverc_bigint_mod(&num, &num, &curve->p);

    neverc_bigint_mul(&den, &two, &p->y);
    neverc_bigint_mod(&den, &den, &curve->p);
    mod_inv(&inv, &den, &curve->p);
    neverc_bigint_mul(&lam, &num, &inv);
    neverc_bigint_mod(&lam, &lam, &curve->p);

    neverc_bigint_mul(&lam2, &lam, &lam);
    neverc_bigint_mod(&lam2, &lam2, &curve->p);

    neverc_bigint_mul(&rx, &two, &p->x);
    neverc_bigint_sub(&rx, &lam2, &rx);
    neverc_bigint_mod(&rx, &rx, &curve->p);

    neverc_bigint_sub(&ry, &p->x, &rx);
    neverc_bigint_mul(&ry, &lam, &ry);
    neverc_bigint_sub(&ry, &ry, &p->y);
    neverc_bigint_mod(&ry, &ry, &curve->p);

    neverc_bigint_set(&r->x, &rx);
    neverc_bigint_set(&r->y, &ry);

    neverc_bigint_free(&x2); neverc_bigint_free(&num);
    neverc_bigint_free(&den); neverc_bigint_free(&lam);
    neverc_bigint_free(&lam2); neverc_bigint_free(&rx);
    neverc_bigint_free(&ry); neverc_bigint_free(&two);
    neverc_bigint_free(&three); neverc_bigint_free(&inv);
}

void neverc_elliptic_scalar_mult(const neverc_elliptic_curve_t *curve,
                                  neverc_elliptic_point_t *r,
                                  const neverc_elliptic_point_t *p,
                                  const neverc_bigint_t *k) {
    neverc_elliptic_point_t acc, tmp;
    neverc_elliptic_point_init(&acc);
    neverc_elliptic_point_init(&tmp);
    neverc_bigint_set(&tmp.x, &p->x);
    neverc_bigint_set(&tmp.y, &p->y);

    int bits = neverc_bigint_bit_len(k);
    for (int i = 0; i < bits; i++) {
        if (neverc_bigint_bit(k, (unsigned)i)) {
            neverc_elliptic_point_t sum;
            neverc_elliptic_point_init(&sum);
            neverc_elliptic_add(curve, &sum, &acc, &tmp);
            neverc_bigint_set(&acc.x, &sum.x);
            neverc_bigint_set(&acc.y, &sum.y);
            neverc_elliptic_point_free(&sum);
        }
        neverc_elliptic_point_t dbl;
        neverc_elliptic_point_init(&dbl);
        neverc_elliptic_double(curve, &dbl, &tmp);
        neverc_bigint_set(&tmp.x, &dbl.x);
        neverc_bigint_set(&tmp.y, &dbl.y);
        neverc_elliptic_point_free(&dbl);
    }

    neverc_bigint_set(&r->x, &acc.x);
    neverc_bigint_set(&r->y, &acc.y);
    neverc_elliptic_point_free(&acc);
    neverc_elliptic_point_free(&tmp);
}

void neverc_elliptic_scalar_base_mult(const neverc_elliptic_curve_t *curve,
                                       neverc_elliptic_point_t *r,
                                       const neverc_bigint_t *k) {
    neverc_elliptic_point_t g;
    neverc_elliptic_point_init(&g);
    neverc_bigint_set(&g.x, &curve->gx);
    neverc_bigint_set(&g.y, &curve->gy);
    neverc_elliptic_scalar_mult(curve, r, &g, k);
    neverc_elliptic_point_free(&g);
}

int neverc_elliptic_marshal(const neverc_elliptic_curve_t *curve,
                             const neverc_elliptic_point_t *pt,
                             unsigned char *out, size_t out_cap, size_t *out_len) {
    int byte_len = (curve->bit_size + 7) / 8;
    size_t needed = 1 + (size_t)byte_len * 2;
    if (out_cap < needed) return -1;

    out[0] = 0x04;
    char hex[256];
    neverc_bigint_string(&pt->x, 16, hex, sizeof(hex));
    size_t hlen = strlen(hex);
    memset(out + 1, 0, (size_t)byte_len);
    for (size_t i = 0; i < hlen && i < (size_t)byte_len * 2; i++) {
        int v;
        char c = hex[hlen - 1 - i];
        if (c >= '0' && c <= '9') v = c - '0';
        else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
        else v = c - 'A' + 10;
        out[1 + byte_len - 1 - (int)(i / 2)] |= (unsigned char)(v << ((i % 2) * 4));
    }

    neverc_bigint_string(&pt->y, 16, hex, sizeof(hex));
    hlen = strlen(hex);
    memset(out + 1 + byte_len, 0, (size_t)byte_len);
    for (size_t i = 0; i < hlen && i < (size_t)byte_len * 2; i++) {
        int v;
        char c = hex[hlen - 1 - i];
        if (c >= '0' && c <= '9') v = c - '0';
        else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
        else v = c - 'A' + 10;
        out[1 + byte_len + byte_len - 1 - (int)(i / 2)] |= (unsigned char)(v << ((i % 2) * 4));
    }

    if (out_len) *out_len = needed;
    return 0;
}

int neverc_elliptic_unmarshal(const neverc_elliptic_curve_t *curve,
                               neverc_elliptic_point_t *pt,
                               const unsigned char *data, size_t data_len) {
    int byte_len = (curve->bit_size + 7) / 8;
    size_t expected = 1 + (size_t)byte_len * 2;
    if (data_len != expected || data[0] != 0x04) return -1;

    char hex[256];
    int pos = 0;
    for (int i = 0; i < byte_len; i++) {
        hex[pos++] = "0123456789abcdef"[data[1 + i] >> 4];
        hex[pos++] = "0123456789abcdef"[data[1 + i] & 0x0F];
    }
    hex[pos] = '\0';
    neverc_bigint_set_string(&pt->x, hex, 16);

    pos = 0;
    for (int i = 0; i < byte_len; i++) {
        hex[pos++] = "0123456789abcdef"[data[1 + byte_len + i] >> 4];
        hex[pos++] = "0123456789abcdef"[data[1 + byte_len + i] & 0x0F];
    }
    hex[pos] = '\0';
    neverc_bigint_set_string(&pt->y, hex, 16);

    return neverc_elliptic_is_on_curve(curve, pt) ? 0 : -1;
}
