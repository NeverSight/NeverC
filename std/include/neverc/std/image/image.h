#ifndef NEVERC_IMAGE_IMAGE_H
#define NEVERC_IMAGE_IMAGE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { int x, y; } neverc_point_t;
typedef struct { neverc_point_t min, max; } neverc_rect_t;

typedef struct {
    uint8_t *pix;
    int stride;
    neverc_rect_t rect;
} neverc_image_rgba_t;

typedef struct {
    uint8_t *pix;
    int stride;
    neverc_rect_t rect;
} neverc_image_gray_t;

neverc_point_t neverc_pt(int x, int y);
/* Add/sub/mul saturate at INT_MAX/INT_MIN (INT_MIN * -1 -> INT_MAX). */
neverc_point_t neverc_point_add(neverc_point_t p, neverc_point_t q);
neverc_point_t neverc_point_sub(neverc_point_t p, neverc_point_t q);
neverc_point_t neverc_point_mul(neverc_point_t p, int k);
/* k == 0 returns p; INT_MIN / -1 saturates at INT_MIN. */
neverc_point_t neverc_point_div(neverc_point_t p, int k);
int            neverc_point_eq(neverc_point_t p, neverc_point_t q);
int            neverc_point_in(neverc_point_t p, neverc_rect_t r);
/* Returns r.min when r is empty or non-canonical. */
neverc_point_t neverc_point_mod(neverc_point_t p, neverc_rect_t r);

neverc_rect_t neverc_rect(int x0, int y0, int x1, int y1);
/* Saturates at INT_MAX/INT_MIN if max-min does not fit in int. */
int           neverc_rect_dx(neverc_rect_t r);
int           neverc_rect_dy(neverc_rect_t r);
/* Corner translation saturates at INT_MAX/INT_MIN. */
neverc_rect_t neverc_rect_add(neverc_rect_t r, neverc_point_t p);
neverc_rect_t neverc_rect_sub(neverc_rect_t r, neverc_point_t p);
/* n > 0 shrinks; n < 0 expands. Overflowing coordinates saturate;
 * a collapsed result is the empty rect {{0,0},{0,0}}. */
neverc_rect_t neverc_rect_inset(neverc_rect_t r, int n);
neverc_rect_t neverc_rect_intersect(neverc_rect_t r, neverc_rect_t s);
neverc_rect_t neverc_rect_union(neverc_rect_t r, neverc_rect_t s);
int           neverc_rect_empty(neverc_rect_t r);
int           neverc_rect_eq(neverc_rect_t r, neverc_rect_t s);
int           neverc_rect_overlaps(neverc_rect_t r, neverc_rect_t s);
int           neverc_rect_in(neverc_rect_t r, neverc_rect_t s);
neverc_rect_t neverc_rect_canon(neverc_rect_t r);

/* init allocates an owned pixel buffer. `img` must be uninitialized, zeroed,
 * or previously released with the matching free function; reinitializing a
 * live image without freeing it first leaks its buffer. free resets all fields
 * to zero and may be called repeatedly. */
int  neverc_image_rgba_init(neverc_image_rgba_t *img, neverc_rect_t r);
void neverc_image_rgba_free(neverc_image_rgba_t *img);
void neverc_image_rgba_set(neverc_image_rgba_t *img, int x, int y,
                           uint8_t r, uint8_t g, uint8_t b, uint8_t a);
void neverc_image_rgba_at(const neverc_image_rgba_t *img, int x, int y,
                          uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a);
int  neverc_image_rgba_pixel_offset(const neverc_image_rgba_t *img, int x, int y);
neverc_rect_t neverc_image_rgba_bounds(const neverc_image_rgba_t *img);

int  neverc_image_gray_init(neverc_image_gray_t *img, neverc_rect_t r);
void neverc_image_gray_free(neverc_image_gray_t *img);
void neverc_image_gray_set(neverc_image_gray_t *img, int x, int y, uint8_t v);
uint8_t neverc_image_gray_at(const neverc_image_gray_t *img, int x, int y);
int  neverc_image_gray_pixel_offset(const neverc_image_gray_t *img, int x, int y);
neverc_rect_t neverc_image_gray_bounds(const neverc_image_gray_t *img);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/image.h>
#endif


#endif /* NEVERC_IMAGE_IMAGE_H */
