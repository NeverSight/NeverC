#include "neverc/std/crypto/elliptic.h"
#include <string.h>

static neverc_elliptic_curve_t g_p256;
static int g_p256_init = 0; /* 0 = uninitialized, 1 = initializing, 2 = ready */
static neverc_elliptic_curve_t g_p384;
static int g_p384_init = 0; /* 0 = uninitialized, 1 = initializing, 2 = ready */

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
    if (__atomic_load_n(&g_p256_init, __ATOMIC_ACQUIRE) == 2)
        return &g_p256;

    int expected = 0;
    if (__atomic_compare_exchange_n(&g_p256_init, &expected, 1, 0,
                                    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        memset(&g_p256, 0, sizeof(g_p256));
        neverc_bigint_init(&g_p256.p);
        neverc_bigint_init(&g_p256.n);
        neverc_bigint_init(&g_p256.b);
        neverc_bigint_init(&g_p256.gx);
        neverc_bigint_init(&g_p256.gy);

        neverc_bigint_set_string(
            &g_p256.p,
            "FFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF",
            16);
        neverc_bigint_set_string(
            &g_p256.n,
            "FFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551",
            16);
        neverc_bigint_set_string(
            &g_p256.b,
            "5AC635D8AA3A93E7B3EBBD55769886BC651D06B0CC53B0F63BCE3C3E27D2604B",
            16);
        neverc_bigint_set_string(
            &g_p256.gx,
            "6B17D1F2E12C4247F8BCE6E563A440F277037D812DEB33A0F4A13945D898C296",
            16);
        neverc_bigint_set_string(
            &g_p256.gy,
            "4FE342E2FE1A7F9B8EE7EB4A7C0F9E162BCE33576B315ECECBB6406837BF51F5",
            16);

        g_p256.bit_size = 256;
        g_p256.name = "P-256";
        __atomic_store_n(&g_p256_init, 2, __ATOMIC_RELEASE);
        return &g_p256;
    }

    while (__atomic_load_n(&g_p256_init, __ATOMIC_ACQUIRE) != 2) {
    }
    return &g_p256;
}

const neverc_elliptic_curve_t *neverc_elliptic_p384(void) {
    if (__atomic_load_n(&g_p384_init, __ATOMIC_ACQUIRE) == 2)
        return &g_p384;

    int expected = 0;
    if (__atomic_compare_exchange_n(&g_p384_init, &expected, 1, 0,
                                    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        memset(&g_p384, 0, sizeof(g_p384));
        neverc_bigint_init(&g_p384.p);
        neverc_bigint_init(&g_p384.n);
        neverc_bigint_init(&g_p384.b);
        neverc_bigint_init(&g_p384.gx);
        neverc_bigint_init(&g_p384.gy);

        neverc_bigint_set_string(
            &g_p384.p,
            "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFE"
            "FFFFFFFF0000000000000000FFFFFFFF",
            16);
        neverc_bigint_set_string(
            &g_p384.n,
            "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFC7634D81F4372DDF"
            "581A0DB248B0A77AECEC196ACCC52973",
            16);
        neverc_bigint_set_string(
            &g_p384.b,
            "B3312FA7E23EE7E4988E056BE3F82D19181D9C6EFE8141120314088F5013875A"
            "C656398D8A2ED19D2A85C8EDD3EC2AEF",
            16);
        neverc_bigint_set_string(
            &g_p384.gx,
            "AA87CA22BE8B05378EB1C71EF320AD746E1D3B628BA79B9859F741E082542A38"
            "5502F25DBF55296C3A545E3872760AB7",
            16);
        neverc_bigint_set_string(
            &g_p384.gy,
            "3617DE4A96262C6F5D9E98BF9292DC29F8F41DBD289A147CE9DA3113B5F0B8C0"
            "0A60B1CE1D7E819D7A431D7C90EA0E5F",
            16);

        g_p384.bit_size = 384;
        g_p384.name = "P-384";
        __atomic_store_n(&g_p384_init, 2, __ATOMIC_RELEASE);
        return &g_p384;
    }

    while (__atomic_load_n(&g_p384_init, __ATOMIC_ACQUIRE) != 2) {
    }
    return &g_p384;
}

void neverc_elliptic_point_init(neverc_elliptic_point_t *pt) {
    if (!pt) return;
    neverc_bigint_init(&pt->x);
    neverc_bigint_init(&pt->y);
}

void neverc_elliptic_point_free(neverc_elliptic_point_t *pt) {
    if (!pt) return;
    neverc_bigint_free(&pt->x);
    neverc_bigint_free(&pt->y);
}

static void point_reduce(neverc_elliptic_point_t *out,
                         const neverc_elliptic_point_t *in,
                         const neverc_bigint_t *p) {
    neverc_bigint_mod(&out->x, &in->x, p);
    neverc_bigint_mod(&out->y, &in->y, p);
}

static int point_is_infinity(const neverc_elliptic_point_t *pt) {
    return neverc_bigint_is_zero(&pt->x) && neverc_bigint_is_zero(&pt->y);
}

int neverc_elliptic_is_on_curve(const neverc_elliptic_curve_t *curve,
                                 const neverc_elliptic_point_t *pt) {
    if (!curve || !pt || pt->x.neg || pt->y.neg ||
        neverc_bigint_cmp(&pt->x, &curve->p) >= 0 ||
        neverc_bigint_cmp(&pt->y, &curve->p) >= 0)
        return 0;
    /* (0,0) is this implementation's internal infinity sentinel, not an
     * encodable affine point. Public-key validation must reject it. */
    if (neverc_bigint_is_zero(&pt->x) && neverc_bigint_is_zero(&pt->y))
        return 0;

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
    if (!curve || !r || !p1 || !p2)
        return;

    neverc_elliptic_point_t a, b;
    neverc_elliptic_point_init(&a);
    neverc_elliptic_point_init(&b);
    point_reduce(&a, p1, &curve->p);
    point_reduce(&b, p2, &curve->p);

    if (point_is_infinity(&a)) {
        neverc_bigint_set(&r->x, &b.x);
        neverc_bigint_set(&r->y, &b.y);
        neverc_elliptic_point_free(&a);
        neverc_elliptic_point_free(&b);
        return;
    }
    if (point_is_infinity(&b)) {
        neverc_bigint_set(&r->x, &a.x);
        neverc_bigint_set(&r->y, &a.y);
        neverc_elliptic_point_free(&a);
        neverc_elliptic_point_free(&b);
        return;
    }
    if (neverc_bigint_cmp(&a.x, &b.x) == 0 &&
        neverc_bigint_cmp(&a.y, &b.y) == 0) {
        neverc_elliptic_double(curve, r, &a);
        neverc_elliptic_point_free(&a);
        neverc_elliptic_point_free(&b);
        return;
    }
    if (neverc_bigint_cmp(&a.x, &b.x) == 0) {
        neverc_bigint_set_int64(&r->x, 0);
        neverc_bigint_set_int64(&r->y, 0);
        neverc_elliptic_point_free(&a);
        neverc_elliptic_point_free(&b);
        return;
    }

    neverc_bigint_t dx, dy, lam, lam2, rx, ry;
    neverc_bigint_init(&dx); neverc_bigint_init(&dy);
    neverc_bigint_init(&lam); neverc_bigint_init(&lam2);
    neverc_bigint_init(&rx); neverc_bigint_init(&ry);

    neverc_bigint_sub(&dx, &b.x, &a.x);
    neverc_bigint_mod(&dx, &dx, &curve->p);
    neverc_bigint_sub(&dy, &b.y, &a.y);
    neverc_bigint_mod(&dy, &dy, &curve->p);

    neverc_bigint_t inv;
    neverc_bigint_init(&inv);
    mod_inv(&inv, &dx, &curve->p);
    neverc_bigint_mul(&lam, &dy, &inv);
    neverc_bigint_mod(&lam, &lam, &curve->p);

    neverc_bigint_mul(&lam2, &lam, &lam);
    neverc_bigint_mod(&lam2, &lam2, &curve->p);

    neverc_bigint_sub(&rx, &lam2, &a.x);
    neverc_bigint_sub(&rx, &rx, &b.x);
    neverc_bigint_mod(&rx, &rx, &curve->p);

    neverc_bigint_sub(&ry, &a.x, &rx);
    neverc_bigint_mul(&ry, &lam, &ry);
    neverc_bigint_sub(&ry, &ry, &a.y);
    neverc_bigint_mod(&ry, &ry, &curve->p);

    neverc_bigint_set(&r->x, &rx);
    neverc_bigint_set(&r->y, &ry);

    neverc_bigint_free(&dx); neverc_bigint_free(&dy);
    neverc_bigint_free(&lam); neverc_bigint_free(&lam2);
    neverc_bigint_free(&rx); neverc_bigint_free(&ry);
    neverc_bigint_free(&inv);
    neverc_elliptic_point_free(&a);
    neverc_elliptic_point_free(&b);
}

void neverc_elliptic_double(const neverc_elliptic_curve_t *curve,
                             neverc_elliptic_point_t *r,
                             const neverc_elliptic_point_t *p) {
    if (!curve || !r || !p)
        return;

    neverc_elliptic_point_t a;
    neverc_elliptic_point_init(&a);
    point_reduce(&a, p, &curve->p);
    if (neverc_bigint_is_zero(&a.y)) {
        neverc_bigint_set_int64(&r->x, 0);
        neverc_bigint_set_int64(&r->y, 0);
        neverc_elliptic_point_free(&a);
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

    neverc_bigint_mul(&x2, &a.x, &a.x);
    neverc_bigint_mod(&x2, &x2, &curve->p);
    neverc_bigint_mul(&num, &three, &x2);
    neverc_bigint_sub(&num, &num, &three);
    neverc_bigint_mod(&num, &num, &curve->p);

    neverc_bigint_mul(&den, &two, &a.y);
    neverc_bigint_mod(&den, &den, &curve->p);
    mod_inv(&inv, &den, &curve->p);
    neverc_bigint_mul(&lam, &num, &inv);
    neverc_bigint_mod(&lam, &lam, &curve->p);

    neverc_bigint_mul(&lam2, &lam, &lam);
    neverc_bigint_mod(&lam2, &lam2, &curve->p);

    neverc_bigint_mul(&rx, &two, &a.x);
    neverc_bigint_sub(&rx, &lam2, &rx);
    neverc_bigint_mod(&rx, &rx, &curve->p);

    neverc_bigint_sub(&ry, &a.x, &rx);
    neverc_bigint_mul(&ry, &lam, &ry);
    neverc_bigint_sub(&ry, &ry, &a.y);
    neverc_bigint_mod(&ry, &ry, &curve->p);

    neverc_bigint_set(&r->x, &rx);
    neverc_bigint_set(&r->y, &ry);

    neverc_bigint_free(&x2); neverc_bigint_free(&num);
    neverc_bigint_free(&den); neverc_bigint_free(&lam);
    neverc_bigint_free(&lam2); neverc_bigint_free(&rx);
    neverc_bigint_free(&ry); neverc_bigint_free(&two);
    neverc_bigint_free(&three); neverc_bigint_free(&inv);
    neverc_elliptic_point_free(&a);
}

/* ------------------------------------------------------------------ *
 * Jacobian-coordinate scalar multiplication.
 *
 * The affine neverc_elliptic_add / _double above each need a modular inverse
 * (mod_inv = a^(p-2) mod p, a full modexp), so the old affine double-and-add
 * paid ~2 inversions per scalar bit — hundreds of modexps for one P-256 mult.
 *
 * Jacobian coordinates (X,Y,Z) represent affine (X/Z^2, Y/Z^3), so point
 * doubling and addition use only field mul/sqr/add — no inversion. The whole
 * ladder runs inversion-free and a single inversion at the end converts back to
 * affine. The doubling uses the a = -3 formula (dbl-2001-b) that the NIST P
 * curves admit, and addition uses mixed Jacobian+affine (madd-2007-bl). This is
 * the standard fast ECC scalar multiplication (SEC1 / Guide to ECC).
 * ------------------------------------------------------------------ */

typedef struct { neverc_bigint_t X, Y, Z; } jac_t;

static void jac_init(jac_t *j) {
    neverc_bigint_init(&j->X); neverc_bigint_init(&j->Y); neverc_bigint_init(&j->Z);
}
static void jac_free(jac_t *j) {
    neverc_bigint_free(&j->X); neverc_bigint_free(&j->Y); neverc_bigint_free(&j->Z);
}

/* Field ops modulo the curve prime p (results normalized into [0,p)). The
 * inputs to fadd/fsub are always reduced field elements in [0,p), so a sum lies
 * in [0,2p) and a difference in (-p,p): a single conditional subtract/add
 * normalizes them without the full trial division that neverc_bigint_mod runs. */
static void fadd(neverc_bigint_t *r, const neverc_bigint_t *a, const neverc_bigint_t *b, const neverc_bigint_t *p) {
    neverc_bigint_add(r, a, b);
    if (neverc_bigint_cmp(r, p) >= 0) neverc_bigint_sub(r, r, p);
}
static void fsub(neverc_bigint_t *r, const neverc_bigint_t *a, const neverc_bigint_t *b, const neverc_bigint_t *p) {
    neverc_bigint_sub(r, a, b);
    if (r->neg) neverc_bigint_add(r, r, p);
}
static void fmul(neverc_bigint_t *r, const neverc_bigint_t *a, const neverc_bigint_t *b, const neverc_bigint_t *p) {
    neverc_bigint_mul(r, a, b); neverc_bigint_mod(r, r, p);
}
static void fsqr(neverc_bigint_t *r, const neverc_bigint_t *a, const neverc_bigint_t *p) {
    neverc_bigint_mul(r, a, a); neverc_bigint_mod(r, r, p);   /* a*a routes to nat_sqr */
}
static void fmulc(neverc_bigint_t *r, const neverc_bigint_t *a, uint64_t c, const neverc_bigint_t *p) {
    neverc_bigint_t cc; neverc_bigint_init(&cc); neverc_bigint_set_uint64(&cc, c);
    neverc_bigint_mul(r, a, &cc); neverc_bigint_mod(r, r, p); neverc_bigint_free(&cc);
}

/* o = 2*P in Jacobian (a = -3). Safe when o aliases P. Z3 == 0 keeps infinity. */
static void jac_double(const neverc_elliptic_curve_t *cv, jac_t *o, const jac_t *P) {
    const neverc_bigint_t *p = &cv->p;
    neverc_bigint_t delta, gamma, beta, alpha, t0, t1, t2, X3, Y3, Z3;
    neverc_bigint_t *all[] = {&delta,&gamma,&beta,&alpha,&t0,&t1,&t2,&X3,&Y3,&Z3};
    for (size_t i = 0; i < sizeof(all)/sizeof(all[0]); i++) neverc_bigint_init(all[i]);

    fsqr(&delta, &P->Z, p);                       /* delta = Z^2 */
    fsqr(&gamma, &P->Y, p);                       /* gamma = Y^2 */
    fmul(&beta, &P->X, &gamma, p);                /* beta  = X*gamma */
    fsub(&t0, &P->X, &delta, p);
    fadd(&t1, &P->X, &delta, p);
    fmul(&alpha, &t0, &t1, p); fmulc(&alpha, &alpha, 3, p);   /* alpha = 3(X-Z^2)(X+Z^2) */
    fsqr(&X3, &alpha, p);
    fmulc(&t2, &beta, 8, p); fsub(&X3, &X3, &t2, p);          /* X3 = alpha^2 - 8 beta */
    fadd(&t0, &P->Y, &P->Z, p); fsqr(&t0, &t0, p);
    fsub(&t0, &t0, &gamma, p); fsub(&Z3, &t0, &delta, p);     /* Z3 = (Y+Z)^2 - gamma - delta */
    fmulc(&t1, &beta, 4, p); fsub(&t1, &t1, &X3, p);
    fmul(&Y3, &alpha, &t1, p);
    fsqr(&t2, &gamma, p); fmulc(&t2, &t2, 8, p);
    fsub(&Y3, &Y3, &t2, p);                                   /* Y3 = alpha(4beta-X3) - 8 gamma^2 */

    neverc_bigint_set(&o->X, &X3);
    neverc_bigint_set(&o->Y, &Y3);
    neverc_bigint_set(&o->Z, &Z3);
    for (size_t i = 0; i < sizeof(all)/sizeof(all[0]); i++) neverc_bigint_free(all[i]);
}

/* o = P + Q, Q = (qx,qy) affine (Zq = 1). Mixed addition. Safe when o aliases P. */
static void jac_add_affine(const neverc_elliptic_curve_t *cv, jac_t *o, const jac_t *P,
                           const neverc_bigint_t *qx, const neverc_bigint_t *qy) {
    const neverc_bigint_t *p = &cv->p;
    if (neverc_bigint_is_zero(&P->Z)) {           /* P = infinity -> o = Q */
        neverc_bigint_set(&o->X, qx);
        neverc_bigint_set(&o->Y, qy);
        neverc_bigint_set_int64(&o->Z, 1);
        return;
    }
    neverc_bigint_t Z1Z1, U2, S2, H, r, HH, I, J, V, X3, Y3, Z3, t;
    neverc_bigint_t *all[] = {&Z1Z1,&U2,&S2,&H,&r,&HH,&I,&J,&V,&X3,&Y3,&Z3,&t};
    for (size_t i = 0; i < sizeof(all)/sizeof(all[0]); i++) neverc_bigint_init(all[i]);

    fsqr(&Z1Z1, &P->Z, p);                        /* Z1^2 */
    fmul(&U2, qx, &Z1Z1, p);                      /* U2 = qx*Z1^2 */
    fmul(&S2, qy, &P->Z, p); fmul(&S2, &S2, &Z1Z1, p);   /* S2 = qy*Z1^3 */
    fsub(&H, &U2, &P->X, p);                      /* H = U2 - X1 */
    fsub(&r, &S2, &P->Y, p); fmulc(&r, &r, 2, p); /* r = 2(S2 - Y1) */

    if (neverc_bigint_is_zero(&H)) {              /* qx == X1/Z1^2 */
        int dbl = neverc_bigint_is_zero(&r);      /* qy == Y1/Z1^3 -> P == Q */
        for (size_t i = 0; i < sizeof(all)/sizeof(all[0]); i++) neverc_bigint_free(all[i]);
        if (dbl) { jac_double(cv, o, P); }
        else { neverc_bigint_set_int64(&o->X, 1); neverc_bigint_set_int64(&o->Y, 1);
               neverc_bigint_set_int64(&o->Z, 0); }    /* P == -Q -> infinity */
        return;
    }

    fsqr(&HH, &H, p);
    fmulc(&I, &HH, 4, p);
    fmul(&J, &H, &I, p);
    fmul(&V, &P->X, &I, p);
    fsqr(&X3, &r, p); fsub(&X3, &X3, &J, p);
    fmulc(&t, &V, 2, p); fsub(&X3, &X3, &t, p);   /* X3 = r^2 - J - 2V */
    fsub(&t, &V, &X3, p); fmul(&Y3, &r, &t, p);
    fmul(&t, &P->Y, &J, p); fmulc(&t, &t, 2, p);
    fsub(&Y3, &Y3, &t, p);                         /* Y3 = r(V-X3) - 2 Y1 J */
    fadd(&Z3, &P->Z, &H, p); fsqr(&Z3, &Z3, p);
    fsub(&Z3, &Z3, &Z1Z1, p); fsub(&Z3, &Z3, &HH, p);   /* Z3 = (Z1+H)^2 - Z1Z1 - HH */

    neverc_bigint_set(&o->X, &X3);
    neverc_bigint_set(&o->Y, &Y3);
    neverc_bigint_set(&o->Z, &Z3);
    for (size_t i = 0; i < sizeof(all)/sizeof(all[0]); i++) neverc_bigint_free(all[i]);
}

void neverc_elliptic_scalar_mult(const neverc_elliptic_curve_t *curve,
                                  neverc_elliptic_point_t *r,
                                  const neverc_elliptic_point_t *p,
                                  const neverc_bigint_t *k) {
    if (!curve || !r || !p || !k)
        return;

    neverc_elliptic_point_t base;
    neverc_elliptic_point_init(&base);
    point_reduce(&base, p, &curve->p);
    /* k == 0, P == infinity, or P off-curve -> infinity. Jacobian field
     * helpers assume reduced on-curve inputs; unreduced (x+p, y) must
     * still compute k*P rather than garbage. */
    if (neverc_bigint_is_zero(k) || point_is_infinity(&base) ||
        !neverc_elliptic_is_on_curve(curve, &base)) {
        neverc_bigint_set_int64(&r->x, 0);
        neverc_bigint_set_int64(&r->y, 0);
        neverc_elliptic_point_free(&base);
        return;
    }

    jac_t acc;
    jac_init(&acc);
    neverc_bigint_set_int64(&acc.X, 1);           /* infinity: Z = 0 */
    neverc_bigint_set_int64(&acc.Y, 1);
    neverc_bigint_set_int64(&acc.Z, 0);

    int bits = neverc_bigint_bit_len(k);
    for (int i = bits - 1; i >= 0; i--) {         /* left-to-right double-and-add */
        jac_double(curve, &acc, &acc);
        if (neverc_bigint_bit(k, (unsigned)i))
            jac_add_affine(curve, &acc, &acc, &base.x, &base.y);
    }

    if (neverc_bigint_is_zero(&acc.Z)) {          /* result is infinity */
        neverc_bigint_set_int64(&r->x, 0);
        neverc_bigint_set_int64(&r->y, 0);
    } else {                                       /* one inversion back to affine */
        neverc_bigint_t zinv, zinv2, zinv3, x, y;
        neverc_bigint_init(&zinv); neverc_bigint_init(&zinv2); neverc_bigint_init(&zinv3);
        neverc_bigint_init(&x); neverc_bigint_init(&y);
        mod_inv(&zinv, &acc.Z, &curve->p);
        fsqr(&zinv2, &zinv, &curve->p);
        fmul(&zinv3, &zinv2, &zinv, &curve->p);
        fmul(&x, &acc.X, &zinv2, &curve->p);
        fmul(&y, &acc.Y, &zinv3, &curve->p);
        neverc_bigint_set(&r->x, &x);
        neverc_bigint_set(&r->y, &y);
        neverc_bigint_free(&zinv); neverc_bigint_free(&zinv2); neverc_bigint_free(&zinv3);
        neverc_bigint_free(&x); neverc_bigint_free(&y);
    }
    jac_free(&acc);
    neverc_elliptic_point_free(&base);
}

void neverc_elliptic_scalar_base_mult(const neverc_elliptic_curve_t *curve,
                                       neverc_elliptic_point_t *r,
                                       const neverc_bigint_t *k) {
    if (!curve || !r || !k)
        return;
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
    if (out_len) *out_len = 0;
    if (!curve || !pt || !out || !neverc_elliptic_is_on_curve(curve, pt))
        return -1;
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
    if (!curve || !pt || !data) return -1;
    int byte_len = (curve->bit_size + 7) / 8;
    size_t expected = 1 + (size_t)byte_len * 2;
    if (data_len != expected || data[0] != 0x04) return -1;

    neverc_elliptic_point_t parsed;
    neverc_elliptic_point_init(&parsed);

    char hex[256];
    int pos = 0;
    for (int i = 0; i < byte_len; i++) {
        hex[pos++] = "0123456789abcdef"[data[1 + i] >> 4];
        hex[pos++] = "0123456789abcdef"[data[1 + i] & 0x0F];
    }
    hex[pos] = '\0';
    if (neverc_bigint_set_string(&parsed.x, hex, 16) != 0) {
        neverc_elliptic_point_free(&parsed);
        return -1;
    }

    pos = 0;
    for (int i = 0; i < byte_len; i++) {
        hex[pos++] = "0123456789abcdef"[data[1 + byte_len + i] >> 4];
        hex[pos++] = "0123456789abcdef"[data[1 + byte_len + i] & 0x0F];
    }
    hex[pos] = '\0';
    if (neverc_bigint_set_string(&parsed.y, hex, 16) != 0) {
        neverc_elliptic_point_free(&parsed);
        return -1;
    }

    if (!neverc_elliptic_is_on_curve(curve, &parsed)) {
        neverc_elliptic_point_free(&parsed);
        return -1;
    }
    neverc_bigint_set(&pt->x, &parsed.x);
    neverc_bigint_set(&pt->y, &parsed.y);
    neverc_elliptic_point_free(&parsed);
    return 0;
}
