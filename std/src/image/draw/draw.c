#include "neverc/std/image/draw.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * Porter-Duff "over" for a single 8-bit channel, premultiply-free form:
 *   out = src + dst * (255 - srcAlpha)   (rounded /255)
 * Valid premultiplied input has src <= srcAlpha, so the result is in
 * [0, 255]. Clamp anyway: draw_uniform / draw_gray_over pass the caller's
 * RGB and alpha independently, and src > alpha makes the unclamped sum
 * 256..510, which wrapped through uint8_t (white OVER white with a=128
 * became 126).
 */
static inline uint8_t over_component(uint8_t dst, uint8_t src, uint8_t sa) {
    uint32_t s = src;
    uint32_t d = dst;
    uint32_t a = sa;
    uint32_t v = (s * 255 + d * (255 - a) + 127) / 255;
    return (uint8_t)(v > 255u ? 255u : v);
}

static inline void over_pixel(uint8_t *d, const uint8_t *s) {
    uint8_t sa = s[3];
    if (sa == 255) {
        d[0] = s[0];
        d[1] = s[1];
        d[2] = s[2];
        d[3] = s[3];
    } else if (sa != 0) {
        d[0] = over_component(d[0], s[0], sa);
        d[1] = over_component(d[1], s[1], sa);
        d[2] = over_component(d[2], s[2], sa);
        d[3] = over_component(d[3], sa, sa);
    }
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

/* Width of `clip` in pixels, or 0 if empty / too wide for `bpp * width` to
 * fit in size_t. Done in 64-bit: `max.x - min.x` as 32-bit int overflows for
 * a clip spanning INT_MIN..INT_MAX (undefined), and the result times bpp
 * would wrap the memmove length. */
static size_t clip_width_bytes_safe(neverc_rect_t clip, int bpp) {
    int64_t w64 = (int64_t)clip.max.x - (int64_t)clip.min.x;
    if (w64 <= 0 || bpp < 1) return 0;
    if ((uint64_t)w64 > SIZE_MAX / (unsigned)bpp) return 0;
    return (size_t)w64;
}

static size_t clip_height_safe(neverc_rect_t clip) {
    int64_t h64 = (int64_t)clip.max.y - (int64_t)clip.min.y;
    if (h64 <= 0) return 0;
    return (size_t)h64;
}

/* Row copies use `stride` as the pitch. A clip wider than stride would
 * memmove past the next row (and off a small pix buffer with a huge rect).
 * `(rows-1)*stride + row_bytes` must also fit in size_t so the last-row
 * pointer is defined. */
static int blit_row_ok(size_t rows, size_t stride, size_t row_bytes) {
    if (rows == 0 || stride < row_bytes) return 0;
    if (rows > 1 && stride > (SIZE_MAX - row_bytes) / (rows - 1U)) return 0;
    return 1;
}

/* 32-bit `dy - rect.min.y` is undefined when min.y is INT_MIN.
 * `span_bytes` is the horizontal blit length: the span must fit in the
 * row pitch starting at `col`. `stride < row_bytes` only catches a clip
 * that is itself wider than the pitch (left-aligned). A 1-pixel clip at
 * a large x of a rect that is wider than stride/bpp used to pass that
 * check, then write at pix+(x-min.x)*bpp — past the row and off a
 * correctly-sized stride*height buffer. */
static int blit_ptr_ok(int64_t row, int64_t col, size_t stride,
                       size_t pixel_bytes, size_t span_bytes, size_t *off) {
    if (row < 0 || col < 0 || span_bytes == 0) return 0;
    if (pixel_bytes != 0 && (uint64_t)col > SIZE_MAX / pixel_bytes) return 0;
    size_t col_off = (size_t)col * pixel_bytes;
    if (col_off > stride || stride - col_off < span_bytes) return 0;
    if (stride != 0 && (uint64_t)row > SIZE_MAX / stride) return 0;
    size_t row_off = (size_t)row * stride;
    if (row_off > SIZE_MAX - col_off) return 0;
    *off = row_off + col_off;
    return 1;
}

/* Image views may start at different offsets in the same allocation (as with
 * a sub-image), so pointer equality is not enough to detect aliasing.  Treat
 * the touched rows as enclosing byte spans.  This can conservatively report
 * overlap through stride padding, which only costs a temporary snapshot. */
static int byte_spans_overlap(const uint8_t *a, size_t aspan,
                              const uint8_t *b, size_t bspan) {
    if (aspan == 0 || bspan == 0) return 0;
    uintptr_t au = (uintptr_t)a;
    uintptr_t bu = (uintptr_t)b;
    if (au > UINTPTR_MAX - aspan || bu > UINTPTR_MAX - bspan)
        return 1;
    return au < bu + bspan && bu < au + aspan;
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
    if (!dst || !dst->pix || !src || !src->pix) return;
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
    if (dst->stride < 4 || src->stride < 4) return;

    int dx0 = clip.min.x, dy0 = clip.min.y;
    int64_t sx = (int64_t)sp.x + (int64_t)dx0 - (int64_t)r.min.x;
    int64_t sy = (int64_t)sp.y + (int64_t)dy0 - (int64_t)r.min.y;

    size_t w = clip_width_bytes_safe(clip, 4);
    if (w == 0) return;
    size_t row_bytes = w * 4U;
    size_t rows = clip_height_safe(clip);
    if (rows == 0) return;

    size_t dst_stride = (size_t)dst->stride;
    size_t src_stride = (size_t)src->stride;
    if (!blit_row_ok(rows, dst_stride, row_bytes) ||
        !blit_row_ok(rows, src_stride, row_bytes))
        return;

    /* Same-buffer blit: Go drawCopySrc walks bottom-to-top when dest is
     * below src (copy/memmove covers horizontal overlap). drawCopyOver
     * also walks right-to-left when dest is below src or on the same row
     * and strictly to the right — otherwise OVER rereads pixels it just
     * wrote. Compare buffer rows/cols so aliased images with different
     * origins still get the right direction. */
    int64_t drow = (int64_t)dy0 - (int64_t)dst->rect.min.y;
    int64_t srow = sy - (int64_t)src->rect.min.y;
    int64_t dcol = (int64_t)dx0 - (int64_t)dst->rect.min.x;
    int64_t scol = sx - (int64_t)src->rect.min.x;
    size_t doff, soff;
    if (!blit_ptr_ok(drow, dcol, dst_stride, 4, row_bytes, &doff) ||
        !blit_ptr_ok(srow, scol, src_stride, 4, row_bytes, &soff))
        return;
    uint8_t *dbase = dst->pix + doff;
    const uint8_t *sbase = src->pix + soff;
    int same_buf = dst->pix == src->pix && dst->stride == src->stride;
    int y_backward = same_buf && drow > srow;
    int over_backward = same_buf &&
        (drow > srow || (drow == srow && dcol > scol));

    /* For offset views (or views with different strides), logical row/column
     * comparisons do not describe the physical copy direction.  Snapshot the
     * clipped source before writing so both SRC and OVER observe the original
     * pixels.  Keep the allocation-free directional fast path for identical
     * base/stride images. */
    uint8_t *snapshot = NULL;
    size_t dst_span = (rows - 1U) * dst_stride + row_bytes;
    size_t src_span = (rows - 1U) * src_stride + row_bytes;
    if (!same_buf && byte_spans_overlap(dbase, dst_span, sbase, src_span)) {
        if (rows > SIZE_MAX / row_bytes) return;
        snapshot = (uint8_t *)malloc(rows * row_bytes);
        if (!snapshot) return;
        for (size_t n = 0; n < rows; n++)
            memcpy(snapshot + n * row_bytes, sbase + n * src_stride,
                   row_bytes);
        sbase = snapshot;
        src_stride = row_bytes;
    }

    if (op == NEVERC_DRAW_SRC) {
        if (y_backward) {
            dbase += (rows - 1U) * dst_stride;
            sbase += (rows - 1U) * src_stride;
            for (size_t n = rows; n > 0; n--) {
                memmove(dbase, sbase, row_bytes);
                dbase -= dst_stride;
                sbase -= src_stride;
            }
        } else {
            for (size_t n = 0; n < rows; n++) {
                memmove(dbase, sbase, row_bytes);
                dbase += dst_stride;
                sbase += src_stride;
            }
        }
        free(snapshot);
        return;
    }

    if (over_backward) {
        dbase += (rows - 1U) * dst_stride;
        sbase += (rows - 1U) * src_stride;
        for (size_t n = rows; n > 0; n--) {
            uint8_t *dptr = dbase + (w - 1U) * 4;
            const uint8_t *sptr = sbase + (w - 1U) * 4;
            for (size_t i = 0; i < w; i++) {
                over_pixel(dptr, sptr);
                dptr -= 4;
                sptr -= 4;
            }
            dbase -= dst_stride;
            sbase -= src_stride;
        }
        free(snapshot);
        return;
    }

    for (size_t n = 0; n < rows; n++) {
        uint8_t *dptr = dbase;
        const uint8_t *sptr = sbase;
        for (size_t i = 0; i < w; i++) {
            over_pixel(dptr, sptr);
            dptr += 4;
            sptr += 4;
        }
        dbase += dst_stride;
        sbase += src_stride;
    }
    free(snapshot);
}

void neverc_draw_uniform(neverc_image_rgba_t *dst, neverc_rect_t r,
                         uint8_t cr, uint8_t cg, uint8_t cb, uint8_t ca,
                         neverc_draw_op_t op) {
    if (!dst || !dst->pix) return;
    neverc_rect_t clip = neverc_rect_intersect(r, dst->rect);
    if (neverc_rect_empty(clip)) return;
    if (dst->stride < 4) return;

    int x0 = clip.min.x, y0 = clip.min.y;
    size_t w = clip_width_bytes_safe(clip, 4);
    if (w == 0) return;
    size_t row_bytes = w * 4U;
    size_t rows = clip_height_safe(clip);
    if (rows == 0) return;

    /* OVER with a fully transparent color is a no-op. */
    int opaque_fill = (op == NEVERC_DRAW_SRC) || (ca == 255);
    if (!opaque_fill && ca == 0) return;

    size_t dst_stride = (size_t)dst->stride;
    if (!blit_row_ok(rows, dst_stride, row_bytes)) return;
    int64_t drow = (int64_t)y0 - (int64_t)dst->rect.min.y;
    int64_t dcol = (int64_t)x0 - (int64_t)dst->rect.min.x;
    size_t doff;
    if (!blit_ptr_ok(drow, dcol, dst_stride, 4, row_bytes, &doff)) return;
    uint8_t *dbase = dst->pix + doff;

    if (opaque_fill) {
        uint8_t fa = (op == NEVERC_DRAW_SRC) ? ca : 255;
        for (size_t n = 0; n < rows; n++) {
            uint8_t *drow = dbase;
            for (size_t i = 0; i < w; i++) {
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

    for (size_t n = 0; n < rows; n++) {
        uint8_t *drow = dbase;
        for (size_t i = 0; i < w; i++) {
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
    if (!dst || !dst->pix || !mask || !mask->pix) return;
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
    if (dst->stride < 4 || mask->stride < 1) return;

    int x0 = clip.min.x, y0 = clip.min.y;
    /* bpp 4: dst row is 4 bytes/pixel; that is the stricter size_t bound
     * (mask rows are 1 byte/pixel). */
    size_t w = clip_width_bytes_safe(clip, 4);
    if (w == 0) return;
    size_t row_bytes = w * 4U;
    size_t rows = clip_height_safe(clip);
    if (rows == 0) return;
    /* Coverage is ca * mask; a fully transparent color is a no-op even
     * when the mask is opaque. Without this, over_component(dst, src, 0)
     * adds src into dst (and used to wrap). */
    if (ca == 0) return;

    int64_t mx = (int64_t)mp.x + (int64_t)x0 - (int64_t)r.min.x;
    int64_t my = (int64_t)mp.y + (int64_t)y0 - (int64_t)r.min.y;

    size_t dst_stride = (size_t)dst->stride;
    size_t mask_stride = (size_t)mask->stride;
    if (!blit_row_ok(rows, dst_stride, row_bytes) ||
        !blit_row_ok(rows, mask_stride, w))
        return;
    int64_t drow = (int64_t)y0 - (int64_t)dst->rect.min.y;
    int64_t dcol = (int64_t)x0 - (int64_t)dst->rect.min.x;
    int64_t mrow0 = my - (int64_t)mask->rect.min.y;
    int64_t mcol = mx - (int64_t)mask->rect.min.x;
    size_t doff, moff;
    if (!blit_ptr_ok(drow, dcol, dst_stride, 4, row_bytes, &doff) ||
        !blit_ptr_ok(mrow0, mcol, mask_stride, 1, w, &moff))
        return;
    uint8_t *dbase = dst->pix + doff;
    const uint8_t *mbase = mask->pix + moff;

    for (size_t n = 0; n < rows; n++) {
        uint8_t *drow = dbase;
        const uint8_t *mrow = mbase;
        for (size_t i = 0; i < w; i++) {
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
