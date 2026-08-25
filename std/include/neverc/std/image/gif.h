#ifndef NEVERC_IMAGE_GIF_H
#define NEVERC_IMAGE_GIF_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_GIF_MAX_PALETTE 256

typedef struct {
    uint8_t r, g, b;
} neverc_gif_color_t;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint8_t *indices;           /* palette indices, width*height */
    neverc_gif_color_t palette[NEVERC_GIF_MAX_PALETTE];
    int palette_size;
    int delay_centiseconds;     /* frame delay in 1/100s */
    uint8_t transparent_index;
    int has_transparency;
} neverc_gif_frame_t;

/* Per-frame wire metadata kept outside the released frame layout. */
typedef struct {
    uint32_t left;              /* frame origin in the logical screen */
    uint32_t top;
    int disposal_method;        /* GIF disposal method (0-3 are defined) */
} neverc_gif_frame_info_t;

typedef struct {
    uint32_t width;
    uint32_t height;
    int loop_count;             /* Netscape 2.0 loop count: -1 = play once
                                 * (extension absent), 0 = infinite */
    neverc_gif_frame_t *frames;
    int num_frames;
    neverc_gif_color_t background;
} neverc_gif_image_t;

/*
 * Decode a GIF from memory (supports single-frame and animated).
 * Returns 0 on success. Caller must call neverc_gif_free().
 */
int neverc_gif_decode(const uint8_t *data, size_t len, neverc_gif_image_t *img);

/*
 * Encode a single-frame paletted image to GIF.
 * Returns 0 on success. *out_data is heap-allocated; caller must free().
 */
int neverc_gif_encode(const neverc_gif_frame_t *frame,
                      uint8_t **out_data, size_t *out_len);

/* Encode with explicit frame origin/disposal metadata. A NULL info uses the
 * same zero origin/disposal defaults as neverc_gif_encode(). */
int neverc_gif_encode_ex(const neverc_gif_frame_t *frame,
                         const neverc_gif_frame_info_t *info,
                         uint8_t **out_data, size_t *out_len);

/* Read metadata for a frame in an image returned by neverc_gif_decode().
 * Returns 0 on success. On error, a non-NULL output is entirely zeroed. */
int neverc_gif_frame_info(const neverc_gif_image_t *img, int frame_index,
                          neverc_gif_frame_info_t *info);

void neverc_gif_free(neverc_gif_image_t *img);

/* Convert an RGBA image to a paletted GIF frame, choosing up to 256 palette
 * colors from the image with Wu's variance-minimizing quantizer. Pixels with
 * alpha 0 become a transparent GIF index. Dimensions must fit GIF's 16-bit
 * fields and the implementation's pixel safety limit. */
int neverc_gif_from_rgba(const uint8_t *rgba, uint32_t width, uint32_t height,
                         neverc_gif_frame_t *frame);

/* Convert a paletted frame to tightly packed RGBA (4 bytes/pixel).
 * Transparent index becomes alpha 0 (RGB kept). *out_rgba is
 * heap-allocated; caller must free(). On error, *out_rgba is NULL. */
int neverc_gif_frame_to_rgba(const neverc_gif_frame_t *frame,
                             uint8_t **out_rgba, size_t *out_len);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/image.h>
#endif


#endif /* NEVERC_IMAGE_GIF_H */
