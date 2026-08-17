#ifndef NEVERC_IMAGE_COLOR_H
#define NEVERC_IMAGE_COLOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { uint8_t r, g, b, a; } neverc_color_rgba_t;
typedef struct { uint8_t r, g, b, a; } neverc_color_nrgba_t;
typedef struct { uint16_t r, g, b, a; } neverc_color_rgba64_t;
typedef struct { uint8_t y; } neverc_color_gray_t;
typedef struct { uint16_t y; } neverc_color_gray16_t;
typedef struct { uint8_t c, m, y, k; } neverc_color_cmyk_t;
typedef struct { float h, s, l; } neverc_color_hsl_t;

neverc_color_rgba_t neverc_color_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
neverc_color_nrgba_t neverc_color_nrgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a);

neverc_color_rgba_t neverc_color_nrgba_to_rgba(neverc_color_nrgba_t c);
neverc_color_nrgba_t neverc_color_rgba_to_nrgba(neverc_color_rgba_t c);

neverc_color_gray_t neverc_color_rgba_to_gray(neverc_color_rgba_t c);
neverc_color_rgba_t neverc_color_gray_to_rgba(neverc_color_gray_t c);

neverc_color_cmyk_t neverc_color_rgba_to_cmyk(neverc_color_rgba_t c);
neverc_color_rgba_t neverc_color_cmyk_to_rgba(neverc_color_cmyk_t c);

neverc_color_hsl_t neverc_color_rgba_to_hsl(neverc_color_rgba_t c);
neverc_color_rgba_t neverc_color_hsl_to_rgba(neverc_color_hsl_t c);

int neverc_color_equal(neverc_color_rgba_t a, neverc_color_rgba_t b);

uint32_t neverc_color_rgba_to_hex(neverc_color_rgba_t c);
neverc_color_rgba_t neverc_color_hex_to_rgba(uint32_t hex);

int neverc_color_parse_hex(const char *s, neverc_color_rgba_t *c);

/* t is clamped to [0, 1]; NaN is treated as 0. */
neverc_color_rgba_t neverc_color_lerp(neverc_color_rgba_t a, neverc_color_rgba_t b, float t);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/image.h>
#endif


#endif
