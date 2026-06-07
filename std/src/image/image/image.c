#include "neverc/std/image/image.h"
#include <stdlib.h>
#include <string.h>

/* --- Point --- */

neverc_point_t neverc_pt(int x, int y) {
    return (neverc_point_t){x, y};
}

neverc_point_t neverc_point_add(neverc_point_t p, neverc_point_t q) {
    return (neverc_point_t){p.x + q.x, p.y + q.y};
}

neverc_point_t neverc_point_sub(neverc_point_t p, neverc_point_t q) {
    return (neverc_point_t){p.x - q.x, p.y - q.y};
}

neverc_point_t neverc_point_mul(neverc_point_t p, int k) {
    return (neverc_point_t){p.x * k, p.y * k};
}

neverc_point_t neverc_point_div(neverc_point_t p, int k) {
    return (neverc_point_t){p.x / k, p.y / k};
}

int neverc_point_eq(neverc_point_t p, neverc_point_t q) {
    return p.x == q.x && p.y == q.y;
}

int neverc_point_in(neverc_point_t p, neverc_rect_t r) {
    return r.min.x <= p.x && p.x < r.max.x &&
           r.min.y <= p.y && p.y < r.max.y;
}

neverc_point_t neverc_point_mod(neverc_point_t p, neverc_rect_t r) {
    int w = r.max.x - r.min.x;
    int h = r.max.y - r.min.y;
    p.x -= r.min.x;
    p.y -= r.min.y;
    p.x = p.x % w;
    if (p.x < 0) p.x += w;
    p.y = p.y % h;
    if (p.y < 0) p.y += h;
    p.x += r.min.x;
    p.y += r.min.y;
    return p;
}

/* --- Rectangle --- */

neverc_rect_t neverc_rect(int x0, int y0, int x1, int y1) {
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    return (neverc_rect_t){{x0, y0}, {x1, y1}};
}

int neverc_rect_dx(neverc_rect_t r) { return r.max.x - r.min.x; }
int neverc_rect_dy(neverc_rect_t r) { return r.max.y - r.min.y; }

neverc_rect_t neverc_rect_add(neverc_rect_t r, neverc_point_t p) {
    return (neverc_rect_t){
        {r.min.x + p.x, r.min.y + p.y},
        {r.max.x + p.x, r.max.y + p.y}
    };
}

neverc_rect_t neverc_rect_sub(neverc_rect_t r, neverc_point_t p) {
    return (neverc_rect_t){
        {r.min.x - p.x, r.min.y - p.y},
        {r.max.x - p.x, r.max.y - p.y}
    };
}

neverc_rect_t neverc_rect_inset(neverc_rect_t r, int n) {
    r.min.x += n; r.min.y += n;
    r.max.x -= n; r.max.y -= n;
    if (r.min.x > r.max.x || r.min.y > r.max.y)
        return (neverc_rect_t){{0,0},{0,0}};
    return r;
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

int neverc_image_rgba_init(neverc_image_rgba_t *img, neverc_rect_t r) {
    int w = r.max.x - r.min.x;
    int h = r.max.y - r.min.y;
    if (w <= 0 || h <= 0) {
        memset(img, 0, sizeof(*img));
        return -1;
    }
    img->stride = w * 4;
    size_t sz = (size_t)img->stride * (size_t)h;
    img->pix = (uint8_t *)calloc(1, sz);
    if (!img->pix) return -1;
    img->rect = r;
    return 0;
}

void neverc_image_rgba_free(neverc_image_rgba_t *img) {
    if (img && img->pix) { free(img->pix); img->pix = NULL; }
}

int neverc_image_rgba_pixel_offset(const neverc_image_rgba_t *img, int x, int y) {
    return (y - img->rect.min.y) * img->stride + (x - img->rect.min.x) * 4;
}

void neverc_image_rgba_set(neverc_image_rgba_t *img, int x, int y,
                           uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (!neverc_point_in(neverc_pt(x, y), img->rect)) return;
    int off = neverc_image_rgba_pixel_offset(img, x, y);
    img->pix[off]   = r;
    img->pix[off+1] = g;
    img->pix[off+2] = b;
    img->pix[off+3] = a;
}

void neverc_image_rgba_at(const neverc_image_rgba_t *img, int x, int y,
                          uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a) {
    if (!neverc_point_in(neverc_pt(x, y), img->rect)) {
        *r = *g = *b = *a = 0; return;
    }
    int off = neverc_image_rgba_pixel_offset(img, x, y);
    *r = img->pix[off]; *g = img->pix[off+1];
    *b = img->pix[off+2]; *a = img->pix[off+3];
}

neverc_rect_t neverc_image_rgba_bounds(const neverc_image_rgba_t *img) {
    return img->rect;
}

/* --- Gray Image --- */

int neverc_image_gray_init(neverc_image_gray_t *img, neverc_rect_t r) {
    int w = r.max.x - r.min.x;
    int h = r.max.y - r.min.y;
    if (w <= 0 || h <= 0) {
        memset(img, 0, sizeof(*img));
        return -1;
    }
    img->stride = w;
    size_t sz = (size_t)img->stride * (size_t)h;
    img->pix = (uint8_t *)calloc(1, sz);
    if (!img->pix) return -1;
    img->rect = r;
    return 0;
}

void neverc_image_gray_free(neverc_image_gray_t *img) {
    if (img && img->pix) { free(img->pix); img->pix = NULL; }
}

int neverc_image_gray_pixel_offset(const neverc_image_gray_t *img, int x, int y) {
    return (y - img->rect.min.y) * img->stride + (x - img->rect.min.x);
}

void neverc_image_gray_set(neverc_image_gray_t *img, int x, int y, uint8_t v) {
    if (!neverc_point_in(neverc_pt(x, y), img->rect)) return;
    img->pix[neverc_image_gray_pixel_offset(img, x, y)] = v;
}

uint8_t neverc_image_gray_at(const neverc_image_gray_t *img, int x, int y) {
    if (!neverc_point_in(neverc_pt(x, y), img->rect)) return 0;
    return img->pix[neverc_image_gray_pixel_offset(img, x, y)];
}

neverc_rect_t neverc_image_gray_bounds(const neverc_image_gray_t *img) {
    return img->rect;
}
