#ifndef NEVERC_IMAGE_H
#define NEVERC_IMAGE_H

/*
 * NeverC image — umbrella header for image submodules.
 */

#include "image/color.h"
#include "image/image.h"
#include "image/draw.h"
#include "image/png.h"
#include "image/jpeg.h"
#include "image/gif.h"
#include "image/color/palette.h"

#ifdef __neverc__
struct __neverc_std_color_t { char __tag; };
struct __neverc_std_draw_t { char __tag; };
struct __neverc_std_png_t { char __tag; };
struct __neverc_std_jpeg_t { char __tag; };
struct __neverc_std_gif_t { char __tag; };
struct __neverc_std_palette_t { char __tag; };

struct __neverc_std_image_t {
    char __tag;
    struct __neverc_std_color_t color;
    struct __neverc_std_draw_t draw;
    struct __neverc_std_png_t png;
    struct __neverc_std_jpeg_t jpeg;
    struct __neverc_std_gif_t gif;
    struct __neverc_std_palette_t palette;
};
extern struct __neverc_std_image_t __neverc_mod_image;
extern struct __neverc_std_image_t image;
#endif

#endif /* NEVERC_IMAGE_H */
