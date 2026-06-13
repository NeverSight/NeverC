/*
 * Benchmark: optimized math/big algorithms vs the previous implementations.
 *   GCD    : Lehmer (new) vs plain Euclid (old)
 *   exp    : fixed-window (new) vs binary square-and-multiply (old)
 *   string : divide-and-conquer base conversion (new) vs base^k chunking (prev)
 *            vs digit-at-a-time (old) — the D&C parse is O(M(n) log n) instead
 *            of the chunked O(n^2), so it pulls away as the number grows.
 *
 * The "old" routines are reproduced here from the pre-optimization big.c so the
 * A/B is measured against the real previous behavior. Build standalone, e.g.:
 *   cc -O2 -I std/include big_bench.c std/src/math/big/big.c -o big_bench
 */
#include "neverc/std/math/big.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

/* ---- deterministic RNG + random bigint construction (lsh/add only) ---- */
static uint64_t rng_state = 0x123456789abcdef0ULL;
static uint32_t rng32(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return (uint32_t)(rng_state >> 32);
}
static void make_random(neverc_bigint_t *z, int nwords) {
    neverc_bigint_t w;
    neverc_bigint_init(&w);
    neverc_bigint_set_int64(z, 0);
    for (int i = 0; i < nwords; i++) {
        neverc_bigint_lsh(z, z, 32);
        neverc_bigint_set_uint64(&w, rng32());
        neverc_bigint_add(z, z, &w);
    }
    neverc_bigint_free(&w);
}

/* ============================================================
 * OLD implementations
 * ============================================================ */
__attribute__((noinline))
static void old_gcd(neverc_bigint_t *z, const neverc_bigint_t *x,
                    const neverc_bigint_t *y) {
    neverc_bigint_t a, b, t;
    neverc_bigint_init(&a); neverc_bigint_init(&b); neverc_bigint_init(&t);
    neverc_bigint_abs(&a, x);
    neverc_bigint_abs(&b, y);
    while (!neverc_bigint_is_zero(&b)) {
        neverc_bigint_mod(&t, &a, &b);
        neverc_bigint_set(&a, &b);
        neverc_bigint_set(&b, &t);
    }
    neverc_bigint_set(z, &a);
    neverc_bigint_free(&a); neverc_bigint_free(&b); neverc_bigint_free(&t);
}

__attribute__((noinline))
static void old_exp(neverc_bigint_t *z, const neverc_bigint_t *base,
                    const neverc_bigint_t *exp, const neverc_bigint_t *m) {
    neverc_bigint_t result, b, e;
    neverc_bigint_init(&result); neverc_bigint_init(&b); neverc_bigint_init(&e);
    int domod = (m && m->len > 0);
    neverc_bigint_set_int64(&result, 1);
    neverc_bigint_abs(&b, base);
    neverc_bigint_set(&e, exp);
    if (domod) neverc_bigint_mod(&b, &b, m);
    int bits = neverc_bigint_bit_len(&e);
    for (int i = 0; i < bits; i++) {
        if (neverc_bigint_bit(&e, (unsigned)i)) {
            neverc_bigint_mul(&result, &result, &b);
            if (domod) neverc_bigint_mod(&result, &result, m);
        }
        neverc_bigint_mul(&b, &b, &b);
        if (domod) neverc_bigint_mod(&b, &b, m);
    }
    neverc_bigint_set(z, &result);
    neverc_bigint_free(&result); neverc_bigint_free(&b); neverc_bigint_free(&e);
}

__attribute__((noinline))
static void old_set_string10(neverc_bigint_t *z, const char *s) {
    neverc_bigint_t b, digit;
    neverc_bigint_init(&b); neverc_bigint_init(&digit);
    neverc_bigint_set_int64(&b, 10);
    neverc_bigint_set_int64(z, 0);
    for (; *s; s++) {
        neverc_bigint_mul(z, z, &b);
        neverc_bigint_set_int64(&digit, *s - '0');
        neverc_bigint_add(z, z, &digit);
    }
    neverc_bigint_free(&b); neverc_bigint_free(&digit);
}

/* Previous parser: fold k decimal digits per multiprecision mul+add. Linear
 * space but O(n^2) time — this is exactly what the D&C path now replaces for
 * large inputs, so comparing against it isolates the divide-and-conquer gain. */
__attribute__((noinline))
static void prev_set_string10_chunked(neverc_bigint_t *z, const char *s) {
    neverc_bigint_t bk, digit;
    neverc_bigint_init(&bk); neverc_bigint_init(&digit);
    uint32_t chunk_base = 1; int k = 0;
    while ((uint64_t)chunk_base * 10u <= 0xFFFFFFFFULL) { chunk_base *= 10u; k++; }
    neverc_bigint_set_uint64(&bk, chunk_base);
    neverc_bigint_set_int64(z, 0);
    uint32_t acc = 0, pmul = 1; int cnt = 0;
    for (; *s; s++) {
        acc = acc * 10u + (uint32_t)(*s - '0');
        pmul *= 10u;
        if (++cnt == k) {
            neverc_bigint_mul(z, z, &bk);
            neverc_bigint_set_uint64(&digit, acc);
            neverc_bigint_add(z, z, &digit);
            acc = 0; pmul = 1; cnt = 0;
        }
    }
    if (cnt > 0) {
        neverc_bigint_set_uint64(&digit, pmul);
        neverc_bigint_mul(z, z, &digit);
        neverc_bigint_set_uint64(&digit, acc);
        neverc_bigint_add(z, z, &digit);
    }
    neverc_bigint_free(&bk); neverc_bigint_free(&digit);
}

__attribute__((noinline))
static int old_string10(const neverc_bigint_t *x, char *buf, size_t cap) {
    static const char digits[] = "0123456789";
    neverc_bigint_t v, rem, bval;
    neverc_bigint_init(&v); neverc_bigint_init(&rem); neverc_bigint_init(&bval);
    neverc_bigint_abs(&v, x);
    neverc_bigint_set_int64(&bval, 10);
    char *tmp = (char *)malloc(cap + 16);
    int pos = 0;
    while (v.len > 0) {
        neverc_bigint_div(&v, &rem, &v, &bval);
        int d = (rem.len > 0) ? (int)rem.digits[0] : 0;
        tmp[pos++] = digits[d];
    }
    if (pos == 0) tmp[pos++] = '0';
    int out = 0;
    for (int i = pos - 1; i >= 0; i--) buf[out++] = tmp[i];
    buf[out] = '\0';
    free(tmp);
    neverc_bigint_free(&v); neverc_bigint_free(&rem); neverc_bigint_free(&bval);
    return out;
}

/* Faithful reproduction of the previous divider (Knuth Algorithm D, base 2^32)
 * so the division A/B measures Burnikel-Ziegler (new) against it. Magnitudes
 * only; requires |x| >= |y| and y->len >= 2, which bench_div always supplies. */
__attribute__((noinline))
static void old_divmod_knuth(neverc_bigint_t *q, neverc_bigint_t *r,
                             const neverc_bigint_t *x, const neverc_bigint_t *y) {
    size_t n = y->len, m = x->len - n;
    unsigned s = 0;
    { uint32_t t = y->digits[n-1]; while (!(t & 0x80000000u)) { t <<= 1; s++; } }
    uint32_t *v = (uint32_t*)malloc(n*4);
    uint32_t *u = (uint32_t*)malloc((m+n+2)*4);
    uint32_t *qd = (uint32_t*)calloc(m+1,4);
    { uint32_t c=0; for (size_t i=0;i<n;i++){ uint64_t cur=((uint64_t)y->digits[i]<<s)|c; v[i]=(uint32_t)cur; c=(uint32_t)(cur>>32);} }
    { uint32_t c=0; for (size_t i=0;i<x->len;i++){ uint64_t cur=((uint64_t)x->digits[i]<<s)|c; u[i]=(uint32_t)cur; c=(uint32_t)(cur>>32);} u[x->len]=c; }
    uint32_t vn1=v[n-1], vn2=v[n-2];
    for (size_t jj=m+1; jj-->0;) {
        size_t j=jj;
        uint64_t num=((uint64_t)u[j+n]<<32)|u[j+n-1];
        uint64_t qhat=num/vn1, rhat=num-qhat*vn1;
        while (qhat>0xFFFFFFFFULL || qhat*vn2 > ((rhat<<32)|u[j+n-2])) { qhat--; rhat+=vn1; if (rhat>0xFFFFFFFFULL) break; }
        int64_t k=0,t;
        for (size_t i=0;i<n;i++){ uint64_t p=qhat*v[i]; t=(int64_t)u[j+i]-k-(int64_t)(uint32_t)(p&0xFFFFFFFFULL); u[j+i]=(uint32_t)t; k=(int64_t)(uint32_t)(p>>32)-(t>>32);} 
        t=(int64_t)u[j+n]-k; u[j+n]=(uint32_t)t;
        if (t<0){ qhat--; uint64_t c=0; for(size_t i=0;i<n;i++){uint64_t s2=(uint64_t)u[j+i]+v[i]+c; u[j+i]=(uint32_t)s2; c=s2>>32;} u[j+n]=(uint32_t)((uint64_t)u[j+n]+c);} 
        qd[j]=(uint32_t)qhat;
    }
    if (q){ free(q->digits); q->digits=(uint32_t*)malloc((m+1)*4); memcpy(q->digits,qd,(m+1)*4); q->cap=q->len=m+1; q->neg=0; while(q->len>0&&q->digits[q->len-1]==0)q->len--; }
    if (r){ free(r->digits); r->digits=(uint32_t*)malloc(n*4); uint32_t c=0; for(size_t i=n;i-->0;){uint32_t cur=u[i]; r->digits[i]=s?((cur>>s)|c):cur; c=s?(cur<<(32-s)):0;} r->cap=r->len=n; r->neg=0; while(r->len>0&&r->digits[r->len-1]==0)r->len--; }
    free(v); free(u); free(qd);
}

/* Previous formatter: repeated single-word division by 10^9 (O(n^2)). The new
 * neverc_bigint_string uses divide-and-conquer above its threshold, so this
 * isolates the format-side gain at large sizes. */
__attribute__((noinline))
static int prev_string10_chunked_fmt(const neverc_bigint_t *x, char *buf, size_t cap) {
    (void)cap;
    if (x->len == 0) { buf[0]='0'; buf[1]='\0'; return 1; }
    static const char digits[] = "0123456789";
    uint32_t chunk = 1000000000u; int k = 9;
    char *tmp = (char*)malloc((size_t)neverc_bigint_bit_len(x) + 2*(size_t)k + 8);
    size_t pos = 0;
    neverc_bigint_t v; neverc_bigint_init(&v); neverc_bigint_abs(&v, x);
    while (v.len > 0) {
        uint32_t rem = 0;
        for (size_t i = v.len; i-- > 0; ) { uint64_t cur=((uint64_t)rem<<32)|v.digits[i]; v.digits[i]=(uint32_t)(cur/chunk); rem=(uint32_t)(cur%chunk); }
        while (v.len > 0 && v.digits[v.len-1]==0) v.len--;
        if (v.len > 0) { for (int j=0;j<k;j++){tmp[pos++]=digits[rem%10];rem/=10;} }
        else { do { tmp[pos++]=digits[rem%10]; rem/=10; } while (rem); }
    }
    int out = 0; if (x->neg) buf[out++]='-';
    for (size_t i = pos; i-- > 0; ) buf[out++]=tmp[i];
    buf[out]='\0';
    neverc_bigint_free(&v); free(tmp);
    return out;
}

/* Previous multiply: Karatsuba over schoolbook, no Toom-Cook and no dedicated
 * squaring (x*x went through the generic path). Reproduced verbatim from the
 * pre-optimization nat_mul so bench_mul/bench_sqr measure the real A/B. */
static size_t old_efflen(const uint32_t *a, size_t n){ while(n>0&&a[n-1]==0)n--; return n; }
static void old_mul_basic(uint32_t *rd, const uint32_t *x, size_t xn, const uint32_t *y, size_t yn){
    for(size_t i=0;i<xn;i++){ uint64_t carry=0,xi=x[i]; size_t kk=i;
        for(size_t j=0;j<yn;j++,kk++){ uint64_t p=xi*y[j]+rd[kk]+carry; rd[kk]=(uint32_t)p; carry=p>>32; }
        while(carry){ uint64_t s=(uint64_t)rd[kk]+carry; rd[kk]=(uint32_t)s; carry=s>>32; kk++; } }
}
static void old_add_into(uint32_t *r, size_t rn, const uint32_t *a, size_t an){
    uint64_t c=0; size_t i=0; for(;i<an;i++){uint64_t s=(uint64_t)r[i]+a[i]+c; r[i]=(uint32_t)s; c=s>>32;}
    for(;c&&i<rn;i++){uint64_t s=(uint64_t)r[i]+c; r[i]=(uint32_t)s; c=s>>32;}
}
static void old_sub_into(uint32_t *r, size_t rn, const uint32_t *a, size_t an){
    int64_t b=0; size_t i=0; for(;i<an;i++){int64_t d=(int64_t)r[i]-(int64_t)a[i]-b; if(d<0){d+=((int64_t)1<<32);b=1;}else b=0; r[i]=(uint32_t)d;}
    for(;b&&i<rn;i++){int64_t d=(int64_t)r[i]-b; if(d<0){d+=((int64_t)1<<32);b=1;}else b=0; r[i]=(uint32_t)d;}
}
#define OLD_KARA 40
static void old_nat_mul(uint32_t *rd, const uint32_t *x, size_t xn, const uint32_t *y, size_t yn){
    if(xn<yn){const uint32_t*t=x;x=y;y=t;size_t s=xn;xn=yn;yn=s;}
    if(yn==0) return;
    if(yn<OLD_KARA){ old_mul_basic(rd,x,xn,y,yn); return; }
    size_t k=yn/2; const uint32_t*xl=x,*xh=x+k; size_t xln=k,xhn=xn-k;
    const uint32_t*yl=y,*yh=y+k; size_t yln=k,yhn=yn-k;
    size_t z0n=xln+yln,z2n=xhn+yhn,sxn=(xhn>xln?xhn:xln)+1,syn=(yhn>yln?yhn:yln)+1,z1n=sxn+syn;
    uint32_t*z0=(uint32_t*)calloc(z0n,4),*z2=(uint32_t*)calloc(z2n,4),*sx=(uint32_t*)calloc(sxn,4),*sy=(uint32_t*)calloc(syn,4),*z1=(uint32_t*)calloc(z1n,4);
    if(!z0||!z2||!sx||!sy||!z1){free(z0);free(z2);free(sx);free(sy);free(z1);old_mul_basic(rd,x,xn,y,yn);return;}
    old_nat_mul(z0,xl,xln,yl,yln); old_nat_mul(z2,xh,xhn,yh,yhn);
    memcpy(sx,xl,xln*4); old_add_into(sx,sxn,xh,xhn);
    memcpy(sy,yl,yln*4); old_add_into(sy,syn,yh,yhn);
    old_nat_mul(z1,sx,sxn,sy,syn); old_sub_into(z1,z1n,z0,z0n); old_sub_into(z1,z1n,z2,z2n);
    old_add_into(rd,xn+yn,z0,z0n); old_add_into(rd+k,xn+yn-k,z1,old_efflen(z1,z1n)); old_add_into(rd+2*k,xn+yn-2*k,z2,z2n);
    free(z0);free(z2);free(sx);free(sy);free(z1);
}
/* old_mul: bigint wrapper around Karatsuba (generic, no sqr special-case).
 * Takes ownership of the computed limb buffer directly (the struct is public),
 * so the A/B isn't polluted by an O(n^2) reload step. */
__attribute__((noinline))
static void old_mul(neverc_bigint_t *z, const neverc_bigint_t *x, const neverc_bigint_t *y){
    if(x->len==0||y->len==0){ z->len=0; z->neg=0; return; }
    size_t rlen=x->len+y->len;
    uint32_t *rd=(uint32_t*)calloc(rlen,4);
    old_nat_mul(rd, x->digits, x->len, y->digits, y->len);
    size_t rn=rlen; while(rn>0&&rd[rn-1]==0) rn--;
    free(z->digits); z->digits=rd; z->len=rn; z->cap=rlen; z->neg=(x->neg!=y->neg)&&rn>0;
}

/* ============================================================
 * Benchmarks
 * ============================================================ */
static volatile int sink;

static void bench_mul(void) {
    printf("\n=== Multiply: Toom-Cook-3 (new) vs Karatsuba (old) ===\n");
    printf("%-26s  %10s  %10s  %8s\n", "words", "karatsuba", "toom3", "speedup");
    int ws[] = {40, 80, 120, 160, 240, 320, 480, 640, 960, 1440, 1920};
    int nw = (int)(sizeof(ws)/sizeof(ws[0]));
    for (int s = 0; s < nw; s++) {
        int w = ws[s];
        neverc_bigint_t x, y, r;
        neverc_bigint_init(&x); neverc_bigint_init(&y); neverc_bigint_init(&r);
        rng_state = 0x2222333344445555ULL + (uint64_t)w;
        make_random(&x, w);
        make_random(&y, w);
        int iters = (int)(2.0e8 / ((double)w * w)); if (iters < 5) iters = 5; if (iters > 200000) iters = 200000;
        double t0 = now_sec();
        for (int i = 0; i < iters; i++) { old_mul(&r, &x, &y); sink ^= (int)r.len; }   /* Karatsuba */
        double t_old = now_sec() - t0;
        t0 = now_sec();
        for (int i = 0; i < iters; i++) { neverc_bigint_mul(&r, &x, &y); sink ^= (int)r.len; } /* Toom-3 */
        double t_new = now_sec() - t0;
        printf("words=%-20d  %8.4f ms  %8.4f ms  %6.2fx\n",
               w, t_old/iters*1000, t_new/iters*1000, t_old/t_new);
        neverc_bigint_free(&x); neverc_bigint_free(&y); neverc_bigint_free(&r);
    }
}

static void bench_sqr(void) {
    printf("\n=== Square: dedicated nat_sqr (new) vs generic multiply (old) ===\n");
    printf("%-26s  %10s  %10s  %8s\n", "words", "generic", "sqr", "speedup");
    int ws[] = {16, 40, 80, 160, 480, 960};
    int nw = (int)(sizeof(ws)/sizeof(ws[0]));
    for (int s = 0; s < nw; s++) {
        int w = ws[s];
        neverc_bigint_t x, r;
        neverc_bigint_init(&x); neverc_bigint_init(&r);
        rng_state = 0x1133557799bbddffULL + (uint64_t)w;
        make_random(&x, w);
        int iters = (int)(2.0e8 / ((double)w * w)); if (iters < 5) iters = 5; if (iters > 200000) iters = 200000;
        double t0 = now_sec();
        for (int i = 0; i < iters; i++) { old_mul(&r, &x, &x); sink ^= (int)r.len; }   /* generic x*x */
        double t_old = now_sec() - t0;
        t0 = now_sec();
        for (int i = 0; i < iters; i++) { neverc_bigint_mul(&r, &x, &x); sink ^= (int)r.len; } /* sqr path */
        double t_new = now_sec() - t0;
        printf("words=%-20d  %8.4f ms  %8.4f ms  %6.2fx\n",
               w, t_old/iters*1000, t_new/iters*1000, t_old/t_new);
        neverc_bigint_free(&x); neverc_bigint_free(&r);
    }
}

static void bench_div(void) {
    printf("\n=== Division: Burnikel-Ziegler (new) vs Knuth (old) ===\n");
    printf("%-26s  %10s  %10s  %8s\n", "divisor words (x=2*y)", "old", "new", "speedup");

    int ws[] = {128, 256, 512, 1024, 2048};
    int nw = (int)(sizeof(ws)/sizeof(ws[0]));
    for (int s = 0; s < nw; s++) {
        int w = ws[s];
        neverc_bigint_t x, y, q, r;
        neverc_bigint_init(&x); neverc_bigint_init(&y);
        neverc_bigint_init(&q); neverc_bigint_init(&r);
        rng_state = 0xc0ffee1234567890ULL + (uint64_t)w;
        make_random(&x, 2*w);
        make_random(&y, w);
        if (neverc_bigint_is_zero(&y)) neverc_bigint_set_int64(&y, 1);
        y.digits[y.len-1] |= 0x80000000u;         /* top bit set: Knuth precond */

        int iters = (int)(3.0e9 / ((double)w * w)); if (iters < 5) iters = 5; if (iters > 4000) iters = 4000;
        double t0 = now_sec();
        for (int i = 0; i < iters; i++) { old_divmod_knuth(&q, &r, &x, &y); sink ^= (int)q.len; }
        double t_old = now_sec() - t0;
        t0 = now_sec();
        for (int i = 0; i < iters; i++) { neverc_bigint_div(&q, &r, &x, &y); sink ^= (int)q.len; }
        double t_new = now_sec() - t0;

        printf("words=%-20d  %8.3f ms  %8.3f ms  %6.1fx\n",
               w, t_old/iters*1000, t_new/iters*1000, t_old/t_new);
        neverc_bigint_free(&x); neverc_bigint_free(&y);
        neverc_bigint_free(&q); neverc_bigint_free(&r);
    }
}

static void bench_gcd(void) {
    printf("\n=== GCD: Lehmer (new) vs Euclid (old) ===\n");
    printf("%-26s  %10s  %10s  %8s\n", "case", "old", "new", "speedup");

    int word_sizes[] = {8, 32, 128, 512};
    int nws = (int)(sizeof(word_sizes) / sizeof(word_sizes[0]));

    for (int s = 0; s < nws; s++) {
        int w = word_sizes[s];
        int iters = (int)(40000000.0 / ((double)w * w)) ;
        if (iters < 20) iters = 20;
        if (iters > 4000) iters = 4000;

        /* fixed pool of random pairs so both sides see identical work */
        enum { POOL = 16 };
        neverc_bigint_t A[POOL], B[POOL], g;
        neverc_bigint_init(&g);
        rng_state = 0xfeedface00c0ffeeULL;
        for (int i = 0; i < POOL; i++) {
            neverc_bigint_init(&A[i]); neverc_bigint_init(&B[i]);
            make_random(&A[i], w);
            make_random(&B[i], w - (int)(rng32() % (unsigned)(w / 2 + 1)));
        }

        double t0 = now_sec();
        for (int i = 0; i < iters; i++) { old_gcd(&g, &A[i % POOL], &B[i % POOL]); sink ^= (int)g.len; }
        double t_old = now_sec() - t0;

        t0 = now_sec();
        for (int i = 0; i < iters; i++) { neverc_bigint_gcd(&g, &A[i % POOL], &B[i % POOL]); sink ^= (int)g.len; }
        double t_new = now_sec() - t0;

        printf("words=%-20d  %8.2f ms  %8.2f ms  %6.1fx\n",
               w, t_old * 1000, t_new * 1000, t_old / t_new);

        for (int i = 0; i < POOL; i++) { neverc_bigint_free(&A[i]); neverc_bigint_free(&B[i]); }
        neverc_bigint_free(&g);
    }

    /* Fibonacci worst case */
    {
        neverc_bigint_t f0, f1, t, g;
        neverc_bigint_init(&f0); neverc_bigint_init(&f1);
        neverc_bigint_init(&t); neverc_bigint_init(&g);
        neverc_bigint_set_int64(&f0, 0); neverc_bigint_set_int64(&f1, 1);
        for (int i = 0; i < 3000; i++) {
            neverc_bigint_add(&t, &f0, &f1);
            neverc_bigint_set(&f0, &f1); neverc_bigint_set(&f1, &t);
        }
        int iters = 200;
        double t0 = now_sec();
        for (int i = 0; i < iters; i++) { old_gcd(&g, &f0, &f1); sink ^= (int)g.len; }
        double t_old = now_sec() - t0;
        t0 = now_sec();
        for (int i = 0; i < iters; i++) { neverc_bigint_gcd(&g, &f0, &f1); sink ^= (int)g.len; }
        double t_new = now_sec() - t0;
        printf("%-26s  %8.2f ms  %8.2f ms  %6.1fx\n",
               "fib(3000) pair", t_old * 1000, t_new * 1000, t_old / t_new);
        neverc_bigint_free(&f0); neverc_bigint_free(&f1);
        neverc_bigint_free(&t); neverc_bigint_free(&g);
    }
}

static void bench_exp(void) {
    printf("\n=== Modular exp: Montgomery+window (new) vs binary (old) ===\n");
    printf("%-26s  %10s  %10s  %8s\n", "modulus bits", "old", "new", "speedup");

    int mod_words[] = {8, 16, 32, 64};   /* 256/512/1024/2048-bit modulus */
    int nm = (int)(sizeof(mod_words) / sizeof(mod_words[0]));
    for (int s = 0; s < nm; s++) {
        int w = mod_words[s];
        neverc_bigint_t base, exp, mod, r;
        neverc_bigint_init(&base); neverc_bigint_init(&exp);
        neverc_bigint_init(&mod); neverc_bigint_init(&r);
        rng_state = 0x1234abcd5678ef00ULL + (uint64_t)w;
        make_random(&base, w);
        make_random(&exp, w);
        make_random(&mod, w);
        if (neverc_bigint_is_zero(&mod)) neverc_bigint_set_int64(&mod, 1);
        if (mod.len > 0) mod.digits[0] |= 1u;   /* crypto moduli are odd -> Montgomery */

        int iters = (int)(8000.0 / (double)(w * w) * 64.0);
        if (iters < 4) iters = 4;
        if (iters > 400) iters = 400;

        double t0 = now_sec();
        for (int i = 0; i < iters; i++) { old_exp(&r, &base, &exp, &mod); sink ^= (int)r.len; }
        double t_old = now_sec() - t0;
        t0 = now_sec();
        for (int i = 0; i < iters; i++) { neverc_bigint_exp(&r, &base, &exp, &mod); sink ^= (int)r.len; }
        double t_new = now_sec() - t0;

        printf("bits=%-21d  %8.2f ms  %8.2f ms  %6.1fx\n",
               w * 32, t_old * 1000, t_new * 1000, t_old / t_new);

        neverc_bigint_free(&base); neverc_bigint_free(&exp);
        neverc_bigint_free(&mod); neverc_bigint_free(&r);
    }
}

static void bench_string(void) {
    printf("\n=== String base-10: current (new) vs per-digit (old) ===\n");
    printf("%-26s  %10s  %10s  %8s\n", "decimal digits", "old", "new", "speedup");

    int sizes[] = {64, 256, 1024, 4096};
    int ns = (int)(sizeof(sizes) / sizeof(sizes[0]));
    for (int s = 0; s < ns; s++) {
        int ndig = sizes[s];
        char *dec = (char *)malloc((size_t)ndig + 1);
        rng_state = 0x99aabbccddee0011ULL + (uint64_t)ndig;
        dec[0] = (char)('1' + (rng32() % 9));
        for (int i = 1; i < ndig; i++) dec[i] = (char)('0' + (rng32() % 10));
        dec[ndig] = '\0';

        size_t bufcap = (size_t)ndig + 8;
        char *out = (char *)malloc(bufcap);
        neverc_bigint_t a;
        neverc_bigint_init(&a);

        int iters = (int)(4000000.0 / ((double)ndig * ndig) * 64.0);
        if (iters < 20) iters = 20;
        if (iters > 20000) iters = 20000;

        /* parse */
        double t0 = now_sec();
        for (int i = 0; i < iters; i++) { old_set_string10(&a, dec); sink ^= (int)a.len; }
        double t_old_p = now_sec() - t0;
        t0 = now_sec();
        for (int i = 0; i < iters; i++) { neverc_bigint_set_string(&a, dec, 10); sink ^= (int)a.len; }
        double t_new_p = now_sec() - t0;

        /* format (a holds the parsed value) */
        t0 = now_sec();
        for (int i = 0; i < iters; i++) { sink ^= old_string10(&a, out, bufcap); }
        double t_old_f = now_sec() - t0;
        t0 = now_sec();
        for (int i = 0; i < iters; i++) { sink ^= neverc_bigint_string(&a, 10, out, bufcap); }
        double t_new_f = now_sec() - t0;

        printf("parse  n=%-16d  %8.2f ms  %8.2f ms  %6.1fx\n",
               ndig, t_old_p * 1000, t_new_p * 1000, t_old_p / t_new_p);
        printf("format n=%-16d  %8.2f ms  %8.2f ms  %6.1fx\n",
               ndig, t_old_f * 1000, t_new_f * 1000, t_old_f / t_new_f);

        neverc_bigint_free(&a);
        free(dec); free(out);
    }

    /* Isolate the divide-and-conquer parse gain over the previous chunked parse
     * at large sizes (where the O(n^2) chunked fold dominates). */
    printf("\n--- parse: D&C (new) vs chunked fold (prev) ---\n");
    printf("%-26s  %10s  %10s  %8s\n", "decimal digits", "chunked", "D&C", "speedup");
    int big[] = {4096, 16384, 65536, 262144};
    int nb = (int)(sizeof(big) / sizeof(big[0]));
    for (int s = 0; s < nb; s++) {
        int ndig = big[s];
        char *dec = (char *)malloc((size_t)ndig + 1);
        rng_state = 0x0badc0de0badf00dULL + (uint64_t)ndig;
        dec[0] = (char)('1' + (rng32() % 9));
        for (int i = 1; i < ndig; i++) dec[i] = (char)('0' + (rng32() % 10));
        dec[ndig] = '\0';
        neverc_bigint_t a; neverc_bigint_init(&a);

        int iters = (int)(2.0e9 / ((double)ndig * ndig)); if (iters < 3) iters = 3;
        double t0 = now_sec();
        for (int i = 0; i < iters; i++) { prev_set_string10_chunked(&a, dec); sink ^= (int)a.len; }
        double t_prev = now_sec() - t0;
        t0 = now_sec();
        for (int i = 0; i < iters; i++) { neverc_bigint_set_string(&a, dec, 10); sink ^= (int)a.len; }
        double t_new = now_sec() - t0;
        printf("parse  n=%-16d  %8.2f ms  %8.2f ms  %6.1fx\n",
               ndig, t_prev / iters * 1000, t_new / iters * 1000, t_prev / t_new);
        neverc_bigint_free(&a);
        free(dec);
    }

    /* Isolate the divide-and-conquer formatter over the previous chunked single-
     * word loop at large sizes (where the O(n^2) loop dominates). */
    printf("\n--- format: D&C (new) vs chunked single-word (prev) ---\n");
    printf("%-26s  %10s  %10s  %8s\n", "decimal digits", "chunked", "D&C", "speedup");
    for (int s = 0; s < nb; s++) {
        int ndig = big[s];
        char *dec = (char *)malloc((size_t)ndig + 1);
        rng_state = 0x5ca1ab1e0ddba11ULL + (uint64_t)ndig;
        dec[0] = (char)('1' + (rng32() % 9));
        for (int i = 1; i < ndig; i++) dec[i] = (char)('0' + (rng32() % 10));
        dec[ndig] = '\0';
        neverc_bigint_t a; neverc_bigint_init(&a);
        neverc_bigint_set_string(&a, dec, 10);
        size_t bufcap = (size_t)ndig + 8;
        char *out = (char *)malloc(bufcap);

        int iters = (int)(2.0e9 / ((double)ndig * ndig)); if (iters < 3) iters = 3;
        double t0 = now_sec();
        for (int i = 0; i < iters; i++) { sink ^= prev_string10_chunked_fmt(&a, out, bufcap); }
        double t_prev = now_sec() - t0;
        t0 = now_sec();
        for (int i = 0; i < iters; i++) { sink ^= neverc_bigint_string(&a, 10, out, bufcap); }
        double t_new = now_sec() - t0;
        printf("format n=%-16d  %8.2f ms  %8.2f ms  %6.1fx\n",
               ndig, t_prev / iters * 1000, t_new / iters * 1000, t_prev / t_new);
        neverc_bigint_free(&a);
        free(dec); free(out);
    }
}

int main(void) {
    printf("=== math/big algorithm benchmarks ===\n");
    bench_mul();
    bench_sqr();
    bench_gcd();
    bench_exp();
    bench_div();
    bench_string();
    printf("\n=== Done (sink=%d) ===\n", sink);
    return 0;
}
