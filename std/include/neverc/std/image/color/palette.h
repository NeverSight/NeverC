#ifndef NEVERC_IMAGE_COLOR_PALETTE_H
#define NEVERC_IMAGE_COLOR_PALETTE_H

/*
 * Standard color palettes.
 * Provides Plan9 (256-color) and WebSafe (216-color) palettes.
 * API modeled after Go's image/color/palette package.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t r, g, b, a;
} neverc_palette_color_t;

#define NEVERC_PALETTE_PLAN9_LEN   256
#define NEVERC_PALETTE_WEBSAFE_LEN 216

extern const neverc_palette_color_t neverc_palette_plan9[NEVERC_PALETTE_PLAN9_LEN];
extern const neverc_palette_color_t neverc_palette_websafe[NEVERC_PALETTE_WEBSAFE_LEN];

/* Find the closest palette entry to an RGBA color. Returns the index. */
int neverc_palette_plan9_index(uint8_t r, uint8_t g, uint8_t b);
int neverc_palette_websafe_index(uint8_t r, uint8_t g, uint8_t b);

#ifdef __cplusplus
}
#endif

#endif /* NEVERC_IMAGE_COLOR_PALETTE_H */
