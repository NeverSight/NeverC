#ifndef NEVERC_IMAGE_DRAW_H
#define NEVERC_IMAGE_DRAW_H

#include "neverc/std/image/image.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NEVERC_DRAW_SRC  = 0,
    NEVERC_DRAW_OVER = 1,
} neverc_draw_op_t;

/* RGBA inputs use premultiplied-alpha channels: r, g, and b must each be no
 * greater than alpha. `neverc_draw_gray_over` applies mask coverage to the
 * supplied premultiplied color before compositing. */

void neverc_draw(neverc_image_rgba_t *dst, neverc_rect_t r,
                 const neverc_image_rgba_t *src, neverc_point_t sp,
                 neverc_draw_op_t op);

void neverc_draw_uniform(neverc_image_rgba_t *dst, neverc_rect_t r,
                         uint8_t cr, uint8_t cg, uint8_t cb, uint8_t ca,
                         neverc_draw_op_t op);

void neverc_draw_gray_over(neverc_image_rgba_t *dst, neverc_rect_t r,
                           const neverc_image_gray_t *mask, neverc_point_t mp,
                           uint8_t cr, uint8_t cg, uint8_t cb, uint8_t ca);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/image.h>
#endif


#endif /* NEVERC_IMAGE_DRAW_H */
