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
    uint32_t left;              /* frame origin in the logical screen */
    uint32_t top;
    uint32_t width;
    uint32_t height;
    uint8_t *indices;           /* palette indices, width*height */
    neverc_gif_color_t palette[NEVERC_GIF_MAX_PALETTE];
    int palette_size;
    int delay_centiseconds;     /* frame delay in 1/100s */
    int disposal_method;        /* GIF disposal method (0-3 are defined) */
    uint8_t transparent_index;
    int has_transparency;
} neverc_gif_frame_t;

typedef struct {
    uint32_t width;
    uint32_t height;
    int loop_count;             /* 0 = infinite loop */
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

void neverc_gif_free(neverc_gif_image_t *img);

/* Convert an RGBA image to a paletted GIF frame, choosing up to 256 palette
 * colors from the image with Wu's variance-minimizing quantizer. Dimensions
 * must fit GIF's 16-bit fields and the implementation's pixel safety limit. */
int neverc_gif_from_rgba(const uint8_t *rgba, uint32_t width, uint32_t height,
                         neverc_gif_frame_t *frame);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/image.h>
#endif


#endif /* NEVERC_IMAGE_GIF_H */
