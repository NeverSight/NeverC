#include "neverc/image/color.h"
#include <string.h>

neverc_color_rgba_t neverc_color_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    neverc_color_rgba_t c = {r, g, b, a};
    return c;
}

neverc_color_nrgba_t neverc_color_nrgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    neverc_color_nrgba_t c = {r, g, b, a};
    return c;
}

neverc_color_rgba_t neverc_color_nrgba_to_rgba(neverc_color_nrgba_t c) {
    neverc_color_rgba_t out;
    out.r = (uint8_t)((uint16_t)c.r * c.a / 255);
    out.g = (uint8_t)((uint16_t)c.g * c.a / 255);
    out.b = (uint8_t)((uint16_t)c.b * c.a / 255);
    out.a = c.a;
    return out;
}

neverc_color_nrgba_t neverc_color_rgba_to_nrgba(neverc_color_rgba_t c) {
    neverc_color_nrgba_t out;
    if (c.a == 0) { out.r = out.g = out.b = out.a = 0; return out; }
    out.r = (uint8_t)((uint16_t)c.r * 255 / c.a);
    out.g = (uint8_t)((uint16_t)c.g * 255 / c.a);
    out.b = (uint8_t)((uint16_t)c.b * 255 / c.a);
    out.a = c.a;
    return out;
}

neverc_color_gray_t neverc_color_rgba_to_gray(neverc_color_rgba_t c) {
    neverc_color_gray_t g;
    g.y = (uint8_t)((19595 * (uint32_t)c.r + 38470 * (uint32_t)c.g + 7471 * (uint32_t)c.b + 32768) >> 16);
    return g;
}

neverc_color_rgba_t neverc_color_gray_to_rgba(neverc_color_gray_t c) {
    neverc_color_rgba_t out = {c.y, c.y, c.y, 255};
    return out;
}

neverc_color_cmyk_t neverc_color_rgba_to_cmyk(neverc_color_rgba_t c) {
    neverc_color_cmyk_t out;
    uint8_t w = c.r;
    if (c.g > w) w = c.g;
    if (c.b > w) w = c.b;
    if (w == 0) {
        out.c = out.m = out.y = 0;
        out.k = 255;
        return out;
    }
    out.c = (uint8_t)((uint16_t)(w - c.r) * 255 / w);
    out.m = (uint8_t)((uint16_t)(w - c.g) * 255 / w);
    out.y = (uint8_t)((uint16_t)(w - c.b) * 255 / w);
    out.k = (uint8_t)(255 - w);
    return out;
}

neverc_color_rgba_t neverc_color_cmyk_to_rgba(neverc_color_cmyk_t c) {
    neverc_color_rgba_t out;
    uint16_t w = (uint16_t)(255 - c.k);
    out.r = (uint8_t)(w * (255 - c.c) / 255);
    out.g = (uint8_t)(w * (255 - c.m) / 255);
    out.b = (uint8_t)(w * (255 - c.y) / 255);
    out.a = 255;
    return out;
}

static float fmin3(float a, float b, float c) { float m = a; if (b < m) m = b; if (c < m) m = c; return m; }
static float fmax3(float a, float b, float c) { float m = a; if (b > m) m = b; if (c > m) m = c; return m; }

neverc_color_hsl_t neverc_color_rgba_to_hsl(neverc_color_rgba_t c) {
    neverc_color_hsl_t hsl;
    float r = c.r / 255.0f, g = c.g / 255.0f, b = c.b / 255.0f;
    float cmax = fmax3(r, g, b), cmin = fmin3(r, g, b);
    float d = cmax - cmin;

    hsl.l = (cmax + cmin) / 2.0f;

    if (d < 0.00001f) { hsl.h = hsl.s = 0; return hsl; }

    hsl.s = hsl.l > 0.5f ? d / (2.0f - cmax - cmin) : d / (cmax + cmin);

    if (cmax == r) hsl.h = (g - b) / d + (g < b ? 6.0f : 0.0f);
    else if (cmax == g) hsl.h = (b - r) / d + 2.0f;
    else hsl.h = (r - g) / d + 4.0f;
    hsl.h /= 6.0f;

    return hsl;
}

static float hue2rgb(float p, float q, float t) {
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1.0f/6) return p + (q - p) * 6.0f * t;
    if (t < 1.0f/2) return q;
    if (t < 2.0f/3) return p + (q - p) * (2.0f/3 - t) * 6.0f;
    return p;
}

neverc_color_rgba_t neverc_color_hsl_to_rgba(neverc_color_hsl_t c) {
    neverc_color_rgba_t out;
    if (c.s < 0.00001f) {
        uint8_t v = (uint8_t)(c.l * 255.0f);
        out.r = out.g = out.b = v;
        out.a = 255;
        return out;
    }
    float q = c.l < 0.5f ? c.l * (1.0f + c.s) : c.l + c.s - c.l * c.s;
    float p = 2.0f * c.l - q;
    out.r = (uint8_t)(hue2rgb(p, q, c.h + 1.0f/3) * 255.0f + 0.5f);
    out.g = (uint8_t)(hue2rgb(p, q, c.h) * 255.0f + 0.5f);
    out.b = (uint8_t)(hue2rgb(p, q, c.h - 1.0f/3) * 255.0f + 0.5f);
    out.a = 255;
    return out;
}

int neverc_color_equal(neverc_color_rgba_t a, neverc_color_rgba_t b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

uint32_t neverc_color_rgba_to_hex(neverc_color_rgba_t c) {
    return ((uint32_t)c.r << 24) | ((uint32_t)c.g << 16) | ((uint32_t)c.b << 8) | c.a;
}

neverc_color_rgba_t neverc_color_hex_to_rgba(uint32_t hex) {
    neverc_color_rgba_t c;
    c.r = (uint8_t)(hex >> 24);
    c.g = (uint8_t)(hex >> 16);
    c.b = (uint8_t)(hex >> 8);
    c.a = (uint8_t)(hex);
    return c;
}

static int hchar(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int neverc_color_parse_hex(const char *s, neverc_color_rgba_t *c) {
    if (!s) return -1;
    if (*s == '#') s++;
    size_t len = strlen(s);

    if (len == 6) {
        c->r = (uint8_t)((hchar(s[0]) << 4) | hchar(s[1]));
        c->g = (uint8_t)((hchar(s[2]) << 4) | hchar(s[3]));
        c->b = (uint8_t)((hchar(s[4]) << 4) | hchar(s[5]));
        c->a = 255;
        return 0;
    }
    if (len == 8) {
        c->r = (uint8_t)((hchar(s[0]) << 4) | hchar(s[1]));
        c->g = (uint8_t)((hchar(s[2]) << 4) | hchar(s[3]));
        c->b = (uint8_t)((hchar(s[4]) << 4) | hchar(s[5]));
        c->a = (uint8_t)((hchar(s[6]) << 4) | hchar(s[7]));
        return 0;
    }
    if (len == 3) {
        c->r = (uint8_t)(hchar(s[0]) * 17);
        c->g = (uint8_t)(hchar(s[1]) * 17);
        c->b = (uint8_t)(hchar(s[2]) * 17);
        c->a = 255;
        return 0;
    }
    return -1;
}

neverc_color_rgba_t neverc_color_lerp(neverc_color_rgba_t a, neverc_color_rgba_t b, float t) {
    neverc_color_rgba_t out;
    out.r = (uint8_t)(a.r + (float)(b.r - a.r) * t + 0.5f);
    out.g = (uint8_t)(a.g + (float)(b.g - a.g) * t + 0.5f);
    out.b = (uint8_t)(a.b + (float)(b.b - a.b) * t + 0.5f);
    out.a = (uint8_t)(a.a + (float)(b.a - a.a) * t + 0.5f);
    return out;
}
