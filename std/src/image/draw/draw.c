#include "neverc/std/image/draw.h"
#include <string.h>

static uint8_t over_component(uint8_t dst, uint8_t src, uint8_t sa) {
    uint32_t s = src;
    uint32_t d = dst;
    uint32_t a = sa;
    return (uint8_t)((s * 255 + d * (255 - a) + 127) / 255);
}

void neverc_draw(neverc_image_rgba_t *dst, neverc_rect_t r,
                 const neverc_image_rgba_t *src, neverc_point_t sp,
                 neverc_draw_op_t op) {
    neverc_rect_t clip = neverc_rect_intersect(r, dst->rect);
    if (neverc_rect_empty(clip)) return;

    int dx0 = clip.min.x, dy0 = clip.min.y;
    int dx1 = clip.max.x, dy1 = clip.max.y;
    int sx = sp.x + (dx0 - r.min.x);
    int sy = sp.y + (dy0 - r.min.y);

    for (int y = dy0; y < dy1; y++) {
        int src_y = sy + (y - dy0);
        for (int x = dx0; x < dx1; x++) {
            int src_x = sx + (x - dx0);
            uint8_t sr, sg, sb, sa;
            neverc_image_rgba_at(src, src_x, src_y, &sr, &sg, &sb, &sa);

            if (op == NEVERC_DRAW_SRC) {
                neverc_image_rgba_set(dst, x, y, sr, sg, sb, sa);
            } else {
                if (sa == 255) {
                    neverc_image_rgba_set(dst, x, y, sr, sg, sb, 255);
                } else if (sa == 0) {
                    /* keep dst */
                } else {
                    uint8_t dr, dg, db, da;
                    neverc_image_rgba_at(dst, x, y, &dr, &dg, &db, &da);
                    uint8_t or_ = over_component(dr, sr, sa);
                    uint8_t og  = over_component(dg, sg, sa);
                    uint8_t ob  = over_component(db, sb, sa);
                    uint8_t oa  = over_component(da, sa, sa);
                    neverc_image_rgba_set(dst, x, y, or_, og, ob, oa);
                }
            }
        }
    }
}

void neverc_draw_uniform(neverc_image_rgba_t *dst, neverc_rect_t r,
                         uint8_t cr, uint8_t cg, uint8_t cb, uint8_t ca,
                         neverc_draw_op_t op) {
    neverc_rect_t clip = neverc_rect_intersect(r, dst->rect);
    if (neverc_rect_empty(clip)) return;

    for (int y = clip.min.y; y < clip.max.y; y++) {
        for (int x = clip.min.x; x < clip.max.x; x++) {
            if (op == NEVERC_DRAW_SRC) {
                neverc_image_rgba_set(dst, x, y, cr, cg, cb, ca);
            } else {
                if (ca == 255) {
                    neverc_image_rgba_set(dst, x, y, cr, cg, cb, 255);
                } else if (ca > 0) {
                    uint8_t dr, dg, db, da;
                    neverc_image_rgba_at(dst, x, y, &dr, &dg, &db, &da);
                    neverc_image_rgba_set(dst, x, y,
                        over_component(dr, cr, ca),
                        over_component(dg, cg, ca),
                        over_component(db, cb, ca),
                        over_component(da, ca, ca));
                }
            }
        }
    }
}

void neverc_draw_gray_over(neverc_image_rgba_t *dst, neverc_rect_t r,
                           const neverc_image_gray_t *mask, neverc_point_t mp,
                           uint8_t cr, uint8_t cg, uint8_t cb, uint8_t ca) {
    neverc_rect_t clip = neverc_rect_intersect(r, dst->rect);
    if (neverc_rect_empty(clip)) return;

    int mx = mp.x + (clip.min.x - r.min.x);
    int my = mp.y + (clip.min.y - r.min.y);

    for (int y = clip.min.y; y < clip.max.y; y++) {
        int mask_y = my + (y - clip.min.y);
        for (int x = clip.min.x; x < clip.max.x; x++) {
            int mask_x = mx + (x - clip.min.x);
            uint8_t mv = neverc_image_gray_at(mask, mask_x, mask_y);
            if (mv == 0) continue;
            uint32_t eff_a = ((uint32_t)ca * mv + 127) / 255;
            uint32_t eff_r = ((uint32_t)cr * mv + 127) / 255;
            uint32_t eff_g = ((uint32_t)cg * mv + 127) / 255;
            uint32_t eff_b = ((uint32_t)cb * mv + 127) / 255;

            uint8_t dr, dg, db, da;
            neverc_image_rgba_at(dst, x, y, &dr, &dg, &db, &da);
            neverc_image_rgba_set(dst, x, y,
                over_component(dr, (uint8_t)eff_r, (uint8_t)eff_a),
                over_component(dg, (uint8_t)eff_g, (uint8_t)eff_a),
                over_component(db, (uint8_t)eff_b, (uint8_t)eff_a),
                over_component(da, (uint8_t)eff_a, (uint8_t)eff_a));
        }
    }
}
