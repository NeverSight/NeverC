#ifndef NEVERC_IMAGE_JPEG_H
#define NEVERC_IMAGE_JPEG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t width;
    uint32_t height;
    uint8_t  channels;    /* 1=grayscale, 3=RGB */
    uint8_t *pixels;      /* row-major, channels bytes per pixel */
    size_t   stride;
} neverc_jpeg_image_t;

/*
 * Decode a JPEG from memory.
 * Only supports baseline DCT, 8-bit, YCbCr→RGB and Grayscale.
 * Returns 0 on success. Caller must call neverc_jpeg_free().
 */
int neverc_jpeg_decode(const uint8_t *data, size_t len, neverc_jpeg_image_t *img);

/*
 * Encode an RGB/Grayscale image to JPEG.
 * quality: 1-100 (higher = better quality, larger file).
 * Returns 0 on success. *out_data is heap-allocated; caller must free().
 */
int neverc_jpeg_encode(const neverc_jpeg_image_t *img, int quality,
                       uint8_t **out_data, size_t *out_len);

void neverc_jpeg_free(neverc_jpeg_image_t *img);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/image.h>
#endif


#endif /* NEVERC_IMAGE_JPEG_H */
