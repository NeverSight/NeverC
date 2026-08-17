#include "neverc/std/image/image.h"
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int sat_from_i64(int64_t v) {
    if (v > INT_MAX) return INT_MAX;
    if (v < INT_MIN) return INT_MIN;
    return (int)v;
}

static int sat_add(int a, int b) {
    return sat_from_i64((int64_t)a + (int64_t)b);
}

static int sat_sub(int a, int b) {
    return sat_from_i64((int64_t)a - (int64_t)b);
}

static int sat_mul(int a, int b) {
    return sat_from_i64((int64_t)a * (int64_t)b);
}

/* --- Point --- */

neverc_point_t neverc_pt(int x, int y) {
    return (neverc_point_t){x, y};
}

neverc_point_t neverc_point_add(neverc_point_t p, neverc_point_t q) {
    return (neverc_point_t){sat_add(p.x, q.x), sat_add(p.y, q.y)};
}

neverc_point_t neverc_point_sub(neverc_point_t p, neverc_point_t q) {
    return (neverc_point_t){sat_sub(p.x, q.x), sat_sub(p.y, q.y)};
}

neverc_point_t neverc_point_mul(neverc_point_t p, int k) {
    return (neverc_point_t){sat_mul(p.x, k), sat_mul(p.y, k)};
}

neverc_point_t neverc_point_div(neverc_point_t p, int k) {
    if (k == 0) return p;
    /* INT_MIN / -1 overflows a two's-complement int. */
    return (neverc_point_t){
        (k == -1 && p.x == INT_MIN) ? INT_MIN : p.x / k,
        (k == -1 && p.y == INT_MIN) ? INT_MIN : p.y / k
    };
}

int neverc_point_eq(neverc_point_t p, neverc_point_t q) {
    return p.x == q.x && p.y == q.y;
}

int neverc_point_in(neverc_point_t p, neverc_rect_t r) {
    return r.min.x <= p.x && p.x < r.max.x &&
           r.min.y <= p.y && p.y < r.max.y;
}

neverc_point_t neverc_point_mod(neverc_point_t p, neverc_rect_t r) {
    int64_t w = (int64_t)r.max.x - (int64_t)r.min.x;
    int64_t h = (int64_t)r.max.y - (int64_t)r.min.y;
    if (w <= 0 || h <= 0) return r.min;
    int64_t x = (int64_t)p.x - (int64_t)r.min.x;
    int64_t y = (int64_t)p.y - (int64_t)r.min.y;
    x %= w;
    y %= h;
    if (x < 0) x += w;
    if (y < 0) y += h;
    return (neverc_point_t){
        (int)(x + r.min.x),
        (int)(y + r.min.y)
    };
}

/* --- Rectangle --- */

neverc_rect_t neverc_rect(int x0, int y0, int x1, int y1) {
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    return (neverc_rect_t){{x0, y0}, {x1, y1}};
}

static int rect_delta(int min, int max) {
    int64_t d = (int64_t)max - (int64_t)min;
    if (d > INT_MAX) return INT_MAX;
    if (d < INT_MIN) return INT_MIN;
    return (int)d;
}

int neverc_rect_dx(neverc_rect_t r) { return rect_delta(r.min.x, r.max.x); }
int neverc_rect_dy(neverc_rect_t r) { return rect_delta(r.min.y, r.max.y); }

neverc_rect_t neverc_rect_add(neverc_rect_t r, neverc_point_t p) {
    return (neverc_rect_t){
        {sat_add(r.min.x, p.x), sat_add(r.min.y, p.y)},
        {sat_add(r.max.x, p.x), sat_add(r.max.y, p.y)}
    };
}

neverc_rect_t neverc_rect_sub(neverc_rect_t r, neverc_point_t p) {
    return (neverc_rect_t){
        {sat_sub(r.min.x, p.x), sat_sub(r.min.y, p.y)},
        {sat_sub(r.max.x, p.x), sat_sub(r.max.y, p.y)}
    };
}

neverc_rect_t neverc_rect_inset(neverc_rect_t r, int n) {
    int64_t minx = (int64_t)r.min.x + n;
    int64_t miny = (int64_t)r.min.y + n;
    int64_t maxx = (int64_t)r.max.x - n;
    int64_t maxy = (int64_t)r.max.y - n;
    if (minx > maxx || miny > maxy)
        return (neverc_rect_t){{0, 0}, {0, 0}};
    return (neverc_rect_t){
        {sat_from_i64(minx), sat_from_i64(miny)},
        {sat_from_i64(maxx), sat_from_i64(maxy)}
    };
}

static int nc_max(int a, int b) { return a > b ? a : b; }
static int nc_min(int a, int b) { return a < b ? a : b; }

neverc_rect_t neverc_rect_intersect(neverc_rect_t r, neverc_rect_t s) {
    neverc_rect_t out = {
        {nc_max(r.min.x, s.min.x), nc_max(r.min.y, s.min.y)},
        {nc_min(r.max.x, s.max.x), nc_min(r.max.y, s.max.y)}
    };
    if (out.min.x > out.max.x || out.min.y > out.max.y)
        return (neverc_rect_t){{0,0},{0,0}};
    return out;
}

neverc_rect_t neverc_rect_union(neverc_rect_t r, neverc_rect_t s) {
    if (neverc_rect_empty(r)) return s;
    if (neverc_rect_empty(s)) return r;
    return (neverc_rect_t){
        {nc_min(r.min.x, s.min.x), nc_min(r.min.y, s.min.y)},
        {nc_max(r.max.x, s.max.x), nc_max(r.max.y, s.max.y)}
    };
}

int neverc_rect_empty(neverc_rect_t r) {
    return r.min.x >= r.max.x || r.min.y >= r.max.y;
}

int neverc_rect_eq(neverc_rect_t r, neverc_rect_t s) {
    return (r.min.x == s.min.x && r.min.y == s.min.y &&
            r.max.x == s.max.x && r.max.y == s.max.y) ||
           (neverc_rect_empty(r) && neverc_rect_empty(s));
}

int neverc_rect_overlaps(neverc_rect_t r, neverc_rect_t s) {
    return !neverc_rect_empty(r) && !neverc_rect_empty(s) &&
           r.min.x < s.max.x && s.min.x < r.max.x &&
           r.min.y < s.max.y && s.min.y < r.max.y;
}

int neverc_rect_in(neverc_rect_t r, neverc_rect_t s) {
    if (neverc_rect_empty(r)) return 1;
    return s.min.x <= r.min.x && r.max.x <= s.max.x &&
           s.min.y <= r.min.y && r.max.y <= s.max.y;
}

neverc_rect_t neverc_rect_canon(neverc_rect_t r) {
    if (r.max.x < r.min.x) { int t = r.min.x; r.min.x = r.max.x; r.max.x = t; }
    if (r.max.y < r.min.y) { int t = r.min.y; r.min.y = r.max.y; r.max.y = t; }
    return r;
}

/* --- RGBA Image --- */

static int image_layout(neverc_rect_t r, int bytes_per_pixel,
                        int *stride, size_t *size) {
    int64_t width = (int64_t)r.max.x - (int64_t)r.min.x;
    int64_t height = (int64_t)r.max.y - (int64_t)r.min.y;
    if (width <= 0 || height <= 0 ||
        width > INT_MAX / bytes_per_pixel) {
        return -1;
    }

    int row_stride = (int)width * bytes_per_pixel;
    if (height > INT_MAX / row_stride) return -1;

    *stride = row_stride;
    *size = (size_t)row_stride * (size_t)height;
    return 0;
}

int neverc_image_rgba_init(neverc_image_rgba_t *img, neverc_rect_t r) {
    if (!img) return -1;
    memset(img, 0, sizeof(*img));

    size_t sz;
    if (image_layout(r, 4, &img->stride, &sz) != 0) return -1;
    img->pix = (uint8_t *)calloc(1, sz);
    if (!img->pix) {
        img->stride = 0;
        return -1;
    }
    img->rect = r;
    return 0;
}

void neverc_image_rgba_free(neverc_image_rgba_t *img) {
    if (img && img->pix) { free(img->pix); img->pix = NULL; }
}

int neverc_image_rgba_pixel_offset(const neverc_image_rgba_t *img, int x, int y) {
    if (!img || !img->pix || img->stride < 4 ||
        !neverc_point_in(neverc_pt(x, y), img->rect))
        return -1;
    int64_t off = ((int64_t)y - img->rect.min.y) * img->stride +
                  ((int64_t)x - img->rect.min.x) * 4;
    if (off < 0 || off > INT_MAX) return -1;
    return (int)off;
}

void neverc_image_rgba_set(neverc_image_rgba_t *img, int x, int y,
                           uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (!img || !img->pix || !neverc_point_in(neverc_pt(x, y), img->rect))
        return;
    int off = neverc_image_rgba_pixel_offset(img, x, y);
    if (off < 0) return;
    img->pix[off]   = r;
    img->pix[off+1] = g;
    img->pix[off+2] = b;
    img->pix[off+3] = a;
}

void neverc_image_rgba_at(const neverc_image_rgba_t *img, int x, int y,
                          uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a) {
    if (!r || !g || !b || !a) return;
    if (!img || !img->pix || !neverc_point_in(neverc_pt(x, y), img->rect)) {
        *r = *g = *b = *a = 0; return;
    }
    int off = neverc_image_rgba_pixel_offset(img, x, y);
    if (off < 0) { *r = *g = *b = *a = 0; return; }
    *r = img->pix[off]; *g = img->pix[off+1];
    *b = img->pix[off+2]; *a = img->pix[off+3];
}

neverc_rect_t neverc_image_rgba_bounds(const neverc_image_rgba_t *img) {
    if (!img) return (neverc_rect_t){{0, 0}, {0, 0}};
    return img->rect;
}

/* --- Gray Image --- */

int neverc_image_gray_init(neverc_image_gray_t *img, neverc_rect_t r) {
    if (!img) return -1;
    memset(img, 0, sizeof(*img));

    size_t sz;
    if (image_layout(r, 1, &img->stride, &sz) != 0) return -1;
    img->pix = (uint8_t *)calloc(1, sz);
    if (!img->pix) {
        img->stride = 0;
        return -1;
    }
    img->rect = r;
    return 0;
}

void neverc_image_gray_free(neverc_image_gray_t *img) {
    if (img && img->pix) { free(img->pix); img->pix = NULL; }
}

int neverc_image_gray_pixel_offset(const neverc_image_gray_t *img, int x, int y) {
    if (!img || !img->pix || img->stride < 1 ||
        !neverc_point_in(neverc_pt(x, y), img->rect))
        return -1;
    int64_t off = ((int64_t)y - img->rect.min.y) * img->stride +
                  ((int64_t)x - img->rect.min.x);
    if (off < 0 || off > INT_MAX) return -1;
    return (int)off;
}

void neverc_image_gray_set(neverc_image_gray_t *img, int x, int y, uint8_t v) {
    if (!img || !img->pix || !neverc_point_in(neverc_pt(x, y), img->rect))
        return;
    int off = neverc_image_gray_pixel_offset(img, x, y);
    if (off < 0) return;
    img->pix[off] = v;
}

uint8_t neverc_image_gray_at(const neverc_image_gray_t *img, int x, int y) {
    if (!img || !img->pix || !neverc_point_in(neverc_pt(x, y), img->rect))
        return 0;
    int off = neverc_image_gray_pixel_offset(img, x, y);
    if (off < 0) return 0;
    return img->pix[off];
}

neverc_rect_t neverc_image_gray_bounds(const neverc_image_gray_t *img) {
    if (!img) return (neverc_rect_t){{0, 0}, {0, 0}};
    return img->rect;
}
