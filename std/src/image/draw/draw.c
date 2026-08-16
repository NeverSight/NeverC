#include "neverc/std/image/draw.h"
#include <stdint.h>
#include <string.h>

/*
 * Porter-Duff "over" for a single 8-bit channel, premultiply-free form:
 *   out = src + dst * (255 - srcAlpha)   (rounded /255)
 * The constant /255 is strength-reduced by the compiler into a multiply+shift,
 * so this stays branch- and divide-free in the emitted code.
 */
static inline uint8_t over_component(uint8_t dst, uint8_t src, uint8_t sa) {
    uint32_t s = src;
    uint32_t d = dst;
    uint32_t a = sa;
    return (uint8_t)((s * 255 + d * (255 - a) + 127) / 255);
}

/* Intersect `a` with a (possibly 64-bit) rect. Used for source/mask bounds
 * translated by (r.min - origin): that add/sub overflows 32-bit int and wraps
 * into dst space, so a mapping that is actually far off-image looks like a
 * valid clip and the row loops read off src/mask->pix. The result is empty or
 * inside `a`, so it always fits in neverc_rect_t. */
static neverc_rect_t rect_intersect_i64(neverc_rect_t a,
                                        int64_t bx0, int64_t by0,
                                        int64_t bx1, int64_t by1) {
    int64_t x0 = (int64_t)a.min.x > bx0 ? (int64_t)a.min.x : bx0;
    int64_t y0 = (int64_t)a.min.y > by0 ? (int64_t)a.min.y : by0;
    int64_t x1 = (int64_t)a.max.x < bx1 ? (int64_t)a.max.x : bx1;
    int64_t y1 = (int64_t)a.max.y < by1 ? (int64_t)a.max.y : by1;
    if (x0 >= x1 || y0 >= y1)
        return (neverc_rect_t){{0, 0}, {0, 0}};
    return (neverc_rect_t){{(int)x0, (int)y0}, {(int)x1, (int)y1}};
}

/*
 * Hot paths below walk one row pointer at a time (incrementing by 4 bytes per
 * pixel) instead of recomputing `(y-miny)*stride + (x-minx)*4` and going through
 * the per-pixel accessor functions on every pixel. The constant `op`/`alpha`
 * branches are hoisted out of the inner loop. Output is bit-identical to the
 * scalar accessor version.
 */

void neverc_draw(neverc_image_rgba_t *dst, neverc_rect_t r,
                 const neverc_image_rgba_t *src, neverc_point_t sp,
                 neverc_draw_op_t op) {
    neverc_rect_t clip = neverc_rect_intersect(r, dst->rect);
    /* Also clip against the source bounds, translated into dst space, so the row
     * copies below never read past src->pix. A dst pixel (x,y) samples src at
     * (x - r.min.x + sp.x, y - r.min.y + sp.y), so staying inside src->rect
     * bounds the dst rect by src->rect shifted by (r.min - sp). Without this, a
     * source smaller than r over-reads its buffer (Go does this in
     * image/draw.clip). The translation is done in 64-bit so extreme origins
     * cannot wrap into dst and look like an in-bounds source read. */
    clip = rect_intersect_i64(clip,
        (int64_t)src->rect.min.x + (int64_t)r.min.x - (int64_t)sp.x,
        (int64_t)src->rect.min.y + (int64_t)r.min.y - (int64_t)sp.y,
        (int64_t)src->rect.max.x + (int64_t)r.min.x - (int64_t)sp.x,
        (int64_t)src->rect.max.y + (int64_t)r.min.y - (int64_t)sp.y);
    if (neverc_rect_empty(clip)) return;

    int dx0 = clip.min.x, dy0 = clip.min.y;
    int dx1 = clip.max.x, dy1 = clip.max.y;
    int64_t sx = (int64_t)sp.x + (int64_t)dx0 - (int64_t)r.min.x;
    int64_t sy = (int64_t)sp.y + (int64_t)dy0 - (int64_t)r.min.y;

    int w = dx1 - dx0;
    if (w <= 0) return;

    size_t dst_stride = (size_t)dst->stride;
    size_t src_stride = (size_t)src->stride;
    uint8_t *dbase = dst->pix + (size_t)(dy0 - dst->rect.min.y) * dst_stride
                              + (size_t)(dx0 - dst->rect.min.x) * 4;
    const uint8_t *sbase = src->pix
        + (size_t)(sy - (int64_t)src->rect.min.y) * src_stride
        + (size_t)(sx - (int64_t)src->rect.min.x) * 4;

    /* Same-buffer SRC shifted down would otherwise copy top-to-bottom and
     * reread rows already overwritten (classic overlapping blit). memmove
     * already covers the horizontal case within a row. */
    if (op == NEVERC_DRAW_SRC && dst->pix == src->pix &&
        dst->stride == src->stride &&
        (dy0 - dst->rect.min.y) > (int)(sy - (int64_t)src->rect.min.y)) {
        size_t rows = (size_t)(dy1 - dy0);
        if (rows > 0) {
            dbase += (rows - 1U) * dst_stride;
            sbase += (rows - 1U) * src_stride;
            for (size_t n = rows; n > 0; n--) {
                memmove(dbase, sbase, (size_t)w * 4);
                dbase -= dst_stride;
                sbase -= src_stride;
            }
        }
        return;
    }

    for (int y = dy0; y < dy1; y++) {
        uint8_t *drow = dbase;
        const uint8_t *srow = sbase;

        if (op == NEVERC_DRAW_SRC) {
            memmove(drow, srow, (size_t)w * 4);
        } else {
            for (int i = 0; i < w; i++) {
                uint8_t sa = srow[3];
                if (sa == 255) {
                    drow[0] = srow[0];
                    drow[1] = srow[1];
                    drow[2] = srow[2];
                    drow[3] = srow[3];
                } else if (sa != 0) {
                    drow[0] = over_component(drow[0], srow[0], sa);
                    drow[1] = over_component(drow[1], srow[1], sa);
                    drow[2] = over_component(drow[2], srow[2], sa);
                    drow[3] = over_component(drow[3], sa, sa);
                }
                drow += 4;
                srow += 4;
            }
        }

        dbase += dst_stride;
        sbase += src_stride;
    }
}

void neverc_draw_uniform(neverc_image_rgba_t *dst, neverc_rect_t r,
                         uint8_t cr, uint8_t cg, uint8_t cb, uint8_t ca,
                         neverc_draw_op_t op) {
    neverc_rect_t clip = neverc_rect_intersect(r, dst->rect);
    if (neverc_rect_empty(clip)) return;

    int x0 = clip.min.x, y0 = clip.min.y;
    int x1 = clip.max.x, y1 = clip.max.y;
    int w = x1 - x0;
    if (w <= 0) return;

    /* OVER with a fully transparent color is a no-op. */
    int opaque_fill = (op == NEVERC_DRAW_SRC) || (ca == 255);
    if (!opaque_fill && ca == 0) return;

    size_t dst_stride = (size_t)dst->stride;
    uint8_t *dbase = dst->pix + (size_t)(y0 - dst->rect.min.y) * dst_stride
                              + (size_t)(x0 - dst->rect.min.x) * 4;

    if (opaque_fill) {
        uint8_t fa = (op == NEVERC_DRAW_SRC) ? ca : 255;
        for (int y = y0; y < y1; y++) {
            uint8_t *drow = dbase;
            for (int i = 0; i < w; i++) {
                drow[0] = cr;
                drow[1] = cg;
                drow[2] = cb;
                drow[3] = fa;
                drow += 4;
            }
            dbase += dst_stride;
        }
        return;
    }

    for (int y = y0; y < y1; y++) {
        uint8_t *drow = dbase;
        for (int i = 0; i < w; i++) {
            drow[0] = over_component(drow[0], cr, ca);
            drow[1] = over_component(drow[1], cg, ca);
            drow[2] = over_component(drow[2], cb, ca);
            drow[3] = over_component(drow[3], ca, ca);
            drow += 4;
        }
        dbase += dst_stride;
    }
}

void neverc_draw_gray_over(neverc_image_rgba_t *dst, neverc_rect_t r,
                           const neverc_image_gray_t *mask, neverc_point_t mp,
                           uint8_t cr, uint8_t cg, uint8_t cb, uint8_t ca) {
    neverc_rect_t clip = neverc_rect_intersect(r, dst->rect);
    /* Clip against the mask bounds (translated into dst space) so the mrow[]
     * reads stay inside mask->pix when the mask is smaller than r. 64-bit
     * translation, same overflow class as neverc_draw. */
    clip = rect_intersect_i64(clip,
        (int64_t)mask->rect.min.x + (int64_t)r.min.x - (int64_t)mp.x,
        (int64_t)mask->rect.min.y + (int64_t)r.min.y - (int64_t)mp.y,
        (int64_t)mask->rect.max.x + (int64_t)r.min.x - (int64_t)mp.x,
        (int64_t)mask->rect.max.y + (int64_t)r.min.y - (int64_t)mp.y);
    if (neverc_rect_empty(clip)) return;

    int x0 = clip.min.x, y0 = clip.min.y;
    int x1 = clip.max.x, y1 = clip.max.y;
    int w = x1 - x0;
    if (w <= 0) return;

    int64_t mx = (int64_t)mp.x + (int64_t)x0 - (int64_t)r.min.x;
    int64_t my = (int64_t)mp.y + (int64_t)y0 - (int64_t)r.min.y;

    size_t dst_stride = (size_t)dst->stride;
    size_t mask_stride = (size_t)mask->stride;
    uint8_t *dbase = dst->pix + (size_t)(y0 - dst->rect.min.y) * dst_stride
                              + (size_t)(x0 - dst->rect.min.x) * 4;
    const uint8_t *mbase = mask->pix
        + (size_t)(my - (int64_t)mask->rect.min.y) * mask_stride
        + (size_t)(mx - (int64_t)mask->rect.min.x);

    for (int y = y0; y < y1; y++) {
        uint8_t *drow = dbase;
        const uint8_t *mrow = mbase;
        for (int i = 0; i < w; i++) {
            uint8_t mv = mrow[i];
            if (mv != 0) {
                uint32_t eff_a = ((uint32_t)ca * mv + 127) / 255;
                uint32_t eff_r = ((uint32_t)cr * mv + 127) / 255;
                uint32_t eff_g = ((uint32_t)cg * mv + 127) / 255;
                uint32_t eff_b = ((uint32_t)cb * mv + 127) / 255;
                drow[0] = over_component(drow[0], (uint8_t)eff_r, (uint8_t)eff_a);
                drow[1] = over_component(drow[1], (uint8_t)eff_g, (uint8_t)eff_a);
                drow[2] = over_component(drow[2], (uint8_t)eff_b, (uint8_t)eff_a);
                drow[3] = over_component(drow[3], (uint8_t)eff_a, (uint8_t)eff_a);
            }
            drow += 4;
        }
        dbase += dst_stride;
        mbase += mask_stride;
    }
}
