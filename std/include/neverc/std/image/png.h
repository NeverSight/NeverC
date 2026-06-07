#ifndef NEVERC_IMAGE_PNG_H
#define NEVERC_IMAGE_PNG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NEVERC_PNG_COLOR_GRAYSCALE       = 0,
    NEVERC_PNG_COLOR_TRUECOLOR       = 2,
    NEVERC_PNG_COLOR_INDEXED         = 3,
    NEVERC_PNG_COLOR_GRAYSCALE_ALPHA = 4,
    NEVERC_PNG_COLOR_TRUECOLOR_ALPHA = 6
} neverc_png_color_type_t;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint8_t  bit_depth;
    uint8_t  color_type;
    uint8_t  channels;
    uint8_t *pixels;
    size_t   stride;
} neverc_png_image_t;

/*
 * Decode a PNG from a memory buffer.
 * Returns 0 on success, -1 on error.
 * On success, img->pixels is heap-allocated; caller must call neverc_png_free().
 */
int neverc_png_decode(const uint8_t *data, size_t len, neverc_png_image_t *img);

/*
 * Encode an image to PNG format.
 * Returns 0 on success. *out_data is heap-allocated; caller must free().
 */
int neverc_png_encode(const neverc_png_image_t *img, uint8_t **out_data, size_t *out_len);

/* Free pixel data from a decoded image. */
void neverc_png_free(neverc_png_image_t *img);

/* Get pixel at (x, y). Returns pointer to pixel data (channels bytes). */
const uint8_t *neverc_png_pixel_at(const neverc_png_image_t *img, uint32_t x, uint32_t y);

/* Set pixel at (x, y) from src buffer (must be img->channels bytes). */
void neverc_png_pixel_set(neverc_png_image_t *img, uint32_t x, uint32_t y, const uint8_t *src);

#ifdef __cplusplus
}
#endif

#endif /* NEVERC_IMAGE_PNG_H */
