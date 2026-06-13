/*
 * draw_bench.c — A/B benchmark: old per-pixel-accessor image/draw vs new
 * row-pointer image/draw. Verifies bit-identical output, then times both.
 *
 * Build:  cc -O2 -o draw_bench draw_bench.c
 * Run:    ./draw_bench
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

/* ── minimal image types (mirrors std/image/image.h layout) ─────────────── */

typedef struct { int x, y; } pt_t;
typedef struct { pt_t min, max; } rect_t;
typedef struct { uint8_t *pix; int stride; rect_t rect; } rgba_t;
typedef struct { uint8_t *pix; int stride; rect_t rect; } gray_t;

typedef enum { DRAW_OVER = 0, DRAW_SRC = 1 } op_t;

static rect_t rect_intersect(rect_t r, rect_t s) {
    if (r.min.x < s.min.x) r.min.x = s.min.x;
    if (r.min.y < s.min.y) r.min.y = s.min.y;
    if (r.max.x > s.max.x) r.max.x = s.max.x;
    if (r.max.y > s.max.y) r.max.y = s.max.y;
    if (r.min.x > r.max.x) r.min.x = r.max.x;
    if (r.min.y > r.max.y) r.min.y = r.max.y;
    return r;
}
static int rect_empty(rect_t r) { return r.min.x >= r.max.x || r.min.y >= r.max.y; }

static pt_t PT(int x, int y) { pt_t p = {x, y}; return p; }
static rect_t RECT(int x0, int y0, int x1, int y1) { rect_t r = {{x0, y0}, {x1, y1}}; return r; }

/* ═══════════════════════════════ OLD ═══════════════════════════════════ */

static int old_off(const rgba_t *img, int x, int y) {
    return (y - img->rect.min.y) * img->stride + (x - img->rect.min.x) * 4;
}
static void old_set(rgba_t *img, int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    int o = old_off(img, x, y);
    img->pix[o] = r; img->pix[o+1] = g; img->pix[o+2] = b; img->pix[o+3] = a;
}
static void old_at(const rgba_t *img, int x, int y, uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a) {
    int o = old_off(img, x, y);
    *r = img->pix[o]; *g = img->pix[o+1]; *b = img->pix[o+2]; *a = img->pix[o+3];
}
static int old_goff(const gray_t *img, int x, int y) {
    return (y - img->rect.min.y) * img->stride + (x - img->rect.min.x);
}
static uint8_t old_gat(const gray_t *img, int x, int y) { return img->pix[old_goff(img, x, y)]; }

static uint8_t old_oc(uint8_t dst, uint8_t src, uint8_t sa) {
    uint32_t s = src, d = dst, a = sa;
    return (uint8_t)((s * 255 + d * (255 - a) + 127) / 255);
}

static void old_draw(rgba_t *dst, rect_t r, const rgba_t *src, pt_t sp, op_t op) {
    rect_t clip = rect_intersect(r, dst->rect);
    if (rect_empty(clip)) return;
    int dx0 = clip.min.x, dy0 = clip.min.y, dx1 = clip.max.x, dy1 = clip.max.y;
    int sx = sp.x + (dx0 - r.min.x), sy = sp.y + (dy0 - r.min.y);
    for (int y = dy0; y < dy1; y++) {
        int src_y = sy + (y - dy0);
        for (int x = dx0; x < dx1; x++) {
            int src_x = sx + (x - dx0);
            uint8_t sr, sg, sb, sa;
            old_at(src, src_x, src_y, &sr, &sg, &sb, &sa);
            if (op == DRAW_SRC) {
                old_set(dst, x, y, sr, sg, sb, sa);
            } else if (sa == 255) {
                old_set(dst, x, y, sr, sg, sb, 255);
            } else if (sa != 0) {
                uint8_t dr, dg, db, da;
                old_at(dst, x, y, &dr, &dg, &db, &da);
                old_set(dst, x, y, old_oc(dr, sr, sa), old_oc(dg, sg, sa),
                        old_oc(db, sb, sa), old_oc(da, sa, sa));
            }
        }
    }
}

static void old_uniform(rgba_t *dst, rect_t r, uint8_t cr, uint8_t cg, uint8_t cb, uint8_t ca, op_t op) {
    rect_t clip = rect_intersect(r, dst->rect);
    if (rect_empty(clip)) return;
    for (int y = clip.min.y; y < clip.max.y; y++)
        for (int x = clip.min.x; x < clip.max.x; x++) {
            if (op == DRAW_SRC) old_set(dst, x, y, cr, cg, cb, ca);
            else if (ca == 255) old_set(dst, x, y, cr, cg, cb, 255);
            else if (ca > 0) {
                uint8_t dr, dg, db, da;
                old_at(dst, x, y, &dr, &dg, &db, &da);
                old_set(dst, x, y, old_oc(dr, cr, ca), old_oc(dg, cg, ca),
                        old_oc(db, cb, ca), old_oc(da, ca, ca));
            }
        }
}

static void old_gray_over(rgba_t *dst, rect_t r, const gray_t *mask, pt_t mp,
                          uint8_t cr, uint8_t cg, uint8_t cb, uint8_t ca) {
    rect_t clip = rect_intersect(r, dst->rect);
    if (rect_empty(clip)) return;
    int mx = mp.x + (clip.min.x - r.min.x), my = mp.y + (clip.min.y - r.min.y);
    for (int y = clip.min.y; y < clip.max.y; y++) {
        int mask_y = my + (y - clip.min.y);
        for (int x = clip.min.x; x < clip.max.x; x++) {
            int mask_x = mx + (x - clip.min.x);
            uint8_t mv = old_gat(mask, mask_x, mask_y);
            if (mv == 0) continue;
            uint32_t ea = ((uint32_t)ca*mv+127)/255, er = ((uint32_t)cr*mv+127)/255;
            uint32_t eg = ((uint32_t)cg*mv+127)/255, eb = ((uint32_t)cb*mv+127)/255;
            uint8_t dr, dg, db, da;
            old_at(dst, x, y, &dr, &dg, &db, &da);
            old_set(dst, x, y, old_oc(dr,(uint8_t)er,(uint8_t)ea), old_oc(dg,(uint8_t)eg,(uint8_t)ea),
                    old_oc(db,(uint8_t)eb,(uint8_t)ea), old_oc(da,(uint8_t)ea,(uint8_t)ea));
        }
    }
}

/* ═══════════════════════════════ NEW ═══════════════════════════════════ */

static inline uint8_t new_oc(uint8_t dst, uint8_t src, uint8_t sa) {
    uint32_t s = src, d = dst, a = sa;
    return (uint8_t)((s * 255 + d * (255 - a) + 127) / 255);
}

static void new_draw(rgba_t *dst, rect_t r, const rgba_t *src, pt_t sp, op_t op) {
    rect_t clip = rect_intersect(r, dst->rect);
    if (rect_empty(clip)) return;
    int dx0 = clip.min.x, dy0 = clip.min.y, dx1 = clip.max.x, dy1 = clip.max.y;
    int sx = sp.x + (dx0 - r.min.x), sy = sp.y + (dy0 - r.min.y);
    int w = dx1 - dx0;
    if (w <= 0) return;
    size_t ds = (size_t)dst->stride, ss = (size_t)src->stride;
    uint8_t *db = dst->pix + (size_t)(dy0 - dst->rect.min.y)*ds + (size_t)(dx0 - dst->rect.min.x)*4;
    const uint8_t *sb = src->pix + (size_t)(sy - src->rect.min.y)*ss + (size_t)(sx - src->rect.min.x)*4;
    for (int y = dy0; y < dy1; y++) {
        uint8_t *drow = db; const uint8_t *srow = sb;
        if (op == DRAW_SRC) {
            memmove(drow, srow, (size_t)w*4);
        } else {
            for (int i = 0; i < w; i++) {
                uint8_t sa = srow[3];
                if (sa == 255) { drow[0]=srow[0]; drow[1]=srow[1]; drow[2]=srow[2]; drow[3]=srow[3]; }
                else if (sa != 0) {
                    drow[0]=new_oc(drow[0],srow[0],sa); drow[1]=new_oc(drow[1],srow[1],sa);
                    drow[2]=new_oc(drow[2],srow[2],sa); drow[3]=new_oc(drow[3],sa,sa);
                }
                drow += 4; srow += 4;
            }
        }
        db += ds; sb += ss;
    }
}

static void new_uniform(rgba_t *dst, rect_t r, uint8_t cr, uint8_t cg, uint8_t cb, uint8_t ca, op_t op) {
    rect_t clip = rect_intersect(r, dst->rect);
    if (rect_empty(clip)) return;
    int x0=clip.min.x,y0=clip.min.y,x1=clip.max.x,y1=clip.max.y, w=x1-x0;
    if (w <= 0) return;
    int opaque = (op == DRAW_SRC) || (ca == 255);
    if (!opaque && ca == 0) return;
    size_t ds = (size_t)dst->stride;
    uint8_t *db = dst->pix + (size_t)(y0 - dst->rect.min.y)*ds + (size_t)(x0 - dst->rect.min.x)*4;
    if (opaque) {
        uint8_t fa = (op == DRAW_SRC) ? ca : 255;
        for (int y=y0;y<y1;y++){ uint8_t *d=db; for(int i=0;i<w;i++){d[0]=cr;d[1]=cg;d[2]=cb;d[3]=fa;d+=4;} db+=ds; }
        return;
    }
    for (int y=y0;y<y1;y++){ uint8_t *d=db;
        for(int i=0;i<w;i++){ d[0]=new_oc(d[0],cr,ca); d[1]=new_oc(d[1],cg,ca); d[2]=new_oc(d[2],cb,ca); d[3]=new_oc(d[3],ca,ca); d+=4; }
        db+=ds; }
}

static void new_gray_over(rgba_t *dst, rect_t r, const gray_t *mask, pt_t mp,
                          uint8_t cr, uint8_t cg, uint8_t cb, uint8_t ca) {
    rect_t clip = rect_intersect(r, dst->rect);
    if (rect_empty(clip)) return;
    int x0=clip.min.x,y0=clip.min.y,x1=clip.max.x,y1=clip.max.y, w=x1-x0;
    if (w <= 0) return;
    int mx = mp.x + (x0 - r.min.x), my = mp.y + (y0 - r.min.y);
    size_t ds=(size_t)dst->stride, ms=(size_t)mask->stride;
    uint8_t *db = dst->pix + (size_t)(y0 - dst->rect.min.y)*ds + (size_t)(x0 - dst->rect.min.x)*4;
    const uint8_t *mb = mask->pix + (size_t)(my - mask->rect.min.y)*ms + (size_t)(mx - mask->rect.min.x);
    for (int y=y0;y<y1;y++){ uint8_t *d=db; const uint8_t *m=mb;
        for(int i=0;i<w;i++){ uint8_t mv=m[i];
            if (mv){ uint32_t ea=((uint32_t)ca*mv+127)/255, er=((uint32_t)cr*mv+127)/255, eg=((uint32_t)cg*mv+127)/255, eb=((uint32_t)cb*mv+127)/255;
                d[0]=new_oc(d[0],(uint8_t)er,(uint8_t)ea); d[1]=new_oc(d[1],(uint8_t)eg,(uint8_t)ea);
                d[2]=new_oc(d[2],(uint8_t)eb,(uint8_t)ea); d[3]=new_oc(d[3],(uint8_t)ea,(uint8_t)ea); }
            d += 4; }
        db+=ds; mb+=ms; }
}

/* ════════════════════════════ harness ══════════════════════════════════ */

static uint64_t rng = 0x123456789abcdef0ULL;
static uint8_t rb(void) { rng ^= rng<<13; rng ^= rng>>7; rng ^= rng<<17; return (uint8_t)(rng>>24); }

static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec*1e3 + t.tv_nsec/1e6; }

static rgba_t mk_rgba(int w, int h) {
    rgba_t img; img.stride = w*4; img.rect = RECT(0,0,w,h);
    img.pix = malloc((size_t)w*h*4); return img;
}
static gray_t mk_gray(int w, int h) {
    gray_t img; img.stride = w; img.rect = RECT(0,0,w,h);
    img.pix = malloc((size_t)w*h); return img;
}
static void fill_rand_rgba(rgba_t *im) { int n=im->stride*(im->rect.max.y-im->rect.min.y); for(int i=0;i<n;i++) im->pix[i]=rb(); }
static void fill_rand_gray(gray_t *im) { int n=im->stride*(im->rect.max.y-im->rect.min.y); for(int i=0;i<n;i++) im->pix[i]=rb(); }

int main(void) {
    const int W = 1024, H = 1024;
    rgba_t src = mk_rgba(W,H), base = mk_rgba(W,H);
    rgba_t da = mk_rgba(W,H), dbi = mk_rgba(W,H);
    gray_t mask = mk_gray(W,H);
    fill_rand_rgba(&src); fill_rand_rgba(&base); fill_rand_gray(&mask);
    size_t bytes = (size_t)W*H*4;

    printf("=== image/draw A/B  (%dx%d) ===\n\n", W, H);

    struct { const char *name; op_t op; } cases[] = {
        {"draw OVER (rgba)", DRAW_OVER}, {"draw SRC  (rgba)", DRAW_SRC},
    };
    int correct = 1;

    for (int c = 0; c < 2; c++) {
        memcpy(da.pix, base.pix, bytes); memcpy(dbi.pix, base.pix, bytes);
        old_draw(&da, RECT(0,0,W,H), &src, PT(0,0), cases[c].op);
        new_draw(&dbi, RECT(0,0,W,H), &src, PT(0,0), cases[c].op);
        int eq = memcmp(da.pix, dbi.pix, bytes) == 0; correct &= eq;
        int iters = 200;
        memcpy(da.pix, base.pix, bytes);
        double t0 = now_ms(); for(int k=0;k<iters;k++) old_draw(&da, RECT(0,0,W,H), &src, PT(0,0), cases[c].op); double t1 = now_ms();
        memcpy(dbi.pix, base.pix, bytes);
        double t2 = now_ms(); for(int k=0;k<iters;k++) new_draw(&dbi, RECT(0,0,W,H), &src, PT(0,0), cases[c].op); double t3 = now_ms();
        printf("%-18s  old %7.2f ms   new %7.2f ms   %5.2fx   %s\n",
               cases[c].name, (t1-t0)/iters, (t3-t2)/iters, (t1-t0)/(t3-t2), eq?"OK":"MISMATCH");
    }

    /* uniform OVER */
    {
        memcpy(da.pix, base.pix, bytes); memcpy(dbi.pix, base.pix, bytes);
        old_uniform(&da, RECT(0,0,W,H), 30,60,90,128, DRAW_OVER);
        new_uniform(&dbi, RECT(0,0,W,H), 30,60,90,128, DRAW_OVER);
        int eq = memcmp(da.pix, dbi.pix, bytes)==0; correct &= eq;
        int iters=300;
        memcpy(da.pix, base.pix, bytes);
        double t0=now_ms(); for(int k=0;k<iters;k++) old_uniform(&da,RECT(0,0,W,H),30,60,90,128,DRAW_OVER); double t1=now_ms();
        memcpy(dbi.pix, base.pix, bytes);
        double t2=now_ms(); for(int k=0;k<iters;k++) new_uniform(&dbi,RECT(0,0,W,H),30,60,90,128,DRAW_OVER); double t3=now_ms();
        printf("%-18s  old %7.2f ms   new %7.2f ms   %5.2fx   %s\n",
               "uniform OVER", (t1-t0)/iters, (t3-t2)/iters, (t1-t0)/(t3-t2), eq?"OK":"MISMATCH");
    }

    /* gray over (glyph blend) */
    {
        memcpy(da.pix, base.pix, bytes); memcpy(dbi.pix, base.pix, bytes);
        old_gray_over(&da, RECT(0,0,W,H), &mask, PT(0,0), 200,40,40,255);
        new_gray_over(&dbi, RECT(0,0,W,H), &mask, PT(0,0), 200,40,40,255);
        int eq = memcmp(da.pix, dbi.pix, bytes)==0; correct &= eq;
        int iters=200;
        memcpy(da.pix, base.pix, bytes);
        double t0=now_ms(); for(int k=0;k<iters;k++) old_gray_over(&da,RECT(0,0,W,H),&mask,PT(0,0),200,40,40,255); double t1=now_ms();
        memcpy(dbi.pix, base.pix, bytes);
        double t2=now_ms(); for(int k=0;k<iters;k++) new_gray_over(&dbi,RECT(0,0,W,H),&mask,PT(0,0),200,40,40,255); double t3=now_ms();
        printf("%-18s  old %7.2f ms   new %7.2f ms   %5.2fx   %s\n",
               "gray OVER (mask)", (t1-t0)/iters, (t3-t2)/iters, (t1-t0)/(t3-t2), eq?"OK":"MISMATCH");
    }

    printf("\nCorrectness: %s\n", correct ? "ALL OUTPUTS BIT-IDENTICAL" : "MISMATCH DETECTED");
    free(src.pix); free(base.pix); free(da.pix); free(dbi.pix); free(mask.pix);
    return correct ? 0 : 1;
}
