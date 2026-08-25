#include "neverc/std/image/gif.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t width;
    uint32_t height;
    uint8_t *indices;
    neverc_gif_color_t palette[NEVERC_GIF_MAX_PALETTE];
    int palette_size;
    int delay_centiseconds;
    uint8_t transparent_index;
    int has_transparency;
} v3389_gif_frame_t;

#if UINTPTR_MAX == UINT64_MAX
_Static_assert(sizeof(v3389_gif_frame_t) == 800,
               "v3389 64-bit GIF frame size");
#elif UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof(v3389_gif_frame_t) == 796,
               "v3389 32-bit GIF frame size");
#endif

#define ABI_TYPE_EQ(current, legacy)                                     \
    _Static_assert(sizeof(current) == sizeof(legacy), "v3389 size ABI"); \
    _Static_assert(_Alignof(current) == _Alignof(legacy),                \
                   "v3389 alignment ABI")
#define ABI_FIELD_EQ(current, legacy, field)                             \
    _Static_assert(offsetof(current, field) == offsetof(legacy, field),  \
                   "v3389 field offset ABI")

ABI_TYPE_EQ(neverc_gif_frame_t, v3389_gif_frame_t);
ABI_FIELD_EQ(neverc_gif_frame_t, v3389_gif_frame_t, width);
ABI_FIELD_EQ(neverc_gif_frame_t, v3389_gif_frame_t, height);
ABI_FIELD_EQ(neverc_gif_frame_t, v3389_gif_frame_t, indices);
ABI_FIELD_EQ(neverc_gif_frame_t, v3389_gif_frame_t, palette);
ABI_FIELD_EQ(neverc_gif_frame_t, v3389_gif_frame_t, palette_size);
ABI_FIELD_EQ(neverc_gif_frame_t, v3389_gif_frame_t, delay_centiseconds);
ABI_FIELD_EQ(neverc_gif_frame_t, v3389_gif_frame_t, transparent_index);
ABI_FIELD_EQ(neverc_gif_frame_t, v3389_gif_frame_t, has_transparency);

enum { CANARY_SIZE = 32 };

typedef struct {
    uint8_t before[CANARY_SIZE];
    union {
        neverc_gif_frame_t now;
        v3389_gif_frame_t old;
    } value;
    uint8_t after[CANARY_SIZE];
} guarded_frame_t;

static int canaries_ok(const guarded_frame_t *guarded) {
    for (size_t i = 0; i < CANARY_SIZE; ++i) {
        if (guarded->before[i] != 0xa5 || guarded->after[i] != 0x5a)
            return 0;
    }
    return 1;
}

static int info_is_zero(const neverc_gif_frame_info_t *info) {
    const uint8_t *bytes = (const uint8_t *)info;
    for (size_t i = 0; i < sizeof(*info); ++i) {
        if (bytes[i] != 0)
            return 0;
    }
    return 1;
}

int main(void) {
    guarded_frame_t guarded;
    uint8_t indices[4] = {0, 1, 1, 0};
    uint8_t *encoded = NULL;
    size_t encoded_len = 0;
    neverc_gif_frame_info_t wanted = {2, 3, 2};

    memset(&guarded, 0, sizeof(guarded));
    memset(guarded.before, 0xa5, sizeof(guarded.before));
    memset(guarded.after, 0x5a, sizeof(guarded.after));
    guarded.value.now.width = 2;
    guarded.value.now.height = 2;
    guarded.value.now.indices = indices;
    guarded.value.now.palette_size = 2;
    guarded.value.now.palette[0] = (neverc_gif_color_t){0, 0, 0};
    guarded.value.now.palette[1] = (neverc_gif_color_t){255, 255, 255};
    guarded.value.now.delay_centiseconds = 7;

    if (neverc_gif_encode_ex(&guarded.value.now, &wanted,
                             &encoded, &encoded_len) != 0 ||
        !encoded || encoded_len == 0 || !canaries_ok(&guarded))
        return 1;

    neverc_gif_image_t image;
    if (neverc_gif_decode(encoded, encoded_len, &image) != 0) {
        free(encoded);
        return 1;
    }
    free(encoded);
    encoded = NULL;
    encoded_len = 0;

    neverc_gif_frame_info_t actual;
    memset(&actual, 0xcc, sizeof(actual));
    if (image.width != 4 || image.height != 5 || image.num_frames != 1 ||
        neverc_gif_frame_info(&image, 0, &actual) != 0 ||
        actual.left != 2 || actual.top != 3 || actual.disposal_method != 2 ||
        image.frames[0].width != 2 || image.frames[0].height != 2 ||
        !canaries_ok(&guarded)) {
        neverc_gif_free(&image);
        return 1;
    }

    memset(&actual, 0xcc, sizeof(actual));
    if (neverc_gif_frame_info(&image, -1, &actual) != -1 ||
        !info_is_zero(&actual)) {
        neverc_gif_free(&image);
        return 1;
    }
    neverc_gif_free(&image);

    wanted.disposal_method = 4;
    if (neverc_gif_encode_ex(&guarded.value.now, &wanted,
                             &encoded, &encoded_len) != -1 ||
        encoded != NULL || encoded_len != 0 || !canaries_ok(&guarded))
        return 1;

    puts("passed");
    return 0;
}
