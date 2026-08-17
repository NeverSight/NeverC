#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_ALLOCATION_LIMIT ((size_t)1024 * 1024)

static size_t largest_request;
static size_t allocation_limit = TEST_ALLOCATION_LIMIT;

static void record_request(size_t size) {
    if (size > largest_request) largest_request = size;
}

static void *controlled_malloc(size_t size) {
    record_request(size);
    return size > allocation_limit ? NULL : malloc(size);
}

static void *controlled_calloc(size_t count, size_t size) {
    if (size != 0 && count > SIZE_MAX / size) {
        largest_request = SIZE_MAX;
        return NULL;
    }
    size_t total = count * size;
    record_request(total);
    return total > allocation_limit ? NULL : calloc(count, size);
}

static void *controlled_realloc(void *ptr, size_t size) {
    record_request(size);
    return size > allocation_limit ? NULL : realloc(ptr, size);
}

#define malloc controlled_malloc
#define calloc controlled_calloc
#define realloc controlled_realloc
#include "../../../std/src/image/gif/gif.c"
#undef malloc
#undef calloc
#undef realloc

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n",              \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static int check_oversized_geometry(void) {
    uint8_t pixel = 0;
    neverc_gif_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.width = UINT16_MAX;
    frame.height = UINT16_MAX;
    frame.indices = &pixel;
    frame.palette_size = 2;

    uint8_t *encoded = NULL;
    size_t encoded_len = 0;
    largest_request = 0;
    CHECK(neverc_gif_encode(
              &frame, &encoded, &encoded_len) == -1);
    CHECK(largest_request == 0);

    allocation_limit = 0;
    largest_request = 0;
    CHECK(neverc_gif_from_rgba(
              &pixel, UINT16_MAX, UINT16_MAX, &frame) == -1);
    CHECK(largest_request == 0);
    allocation_limit = TEST_ALLOCATION_LIMIT;

    static const uint8_t oversized_frame[] = {
        'G', 'I', 'F', '8', '9', 'a',
        1, 0, 1, 0, 0x80, 0, 0,
        0, 0, 0, 255, 255, 255,
        0x2c,
        0, 0, 0, 0,
        0xff, 0xff, 0xff, 0xff,
        0
    };
    neverc_gif_image_t image;
    largest_request = 0;
    CHECK(neverc_gif_decode(
              oversized_frame, sizeof(oversized_frame), &image) == -1);
    CHECK(largest_request < TEST_ALLOCATION_LIMIT);
    CHECK(image.width == 0);
    CHECK(image.height == 0);

    static const uint8_t oversized_lsd[] = {
        'G', 'I', 'F', '8', '9', 'a',
        0xff, 0xff, 0xff, 0xff,
        0x00, 0, 0,
        0x2c,
        0, 0, 0, 0,
        1, 0, 1, 0,
        0
    };
    neverc_gif_image_t huge;
    largest_request = 0;
    CHECK(neverc_gif_decode(
              oversized_lsd, sizeof(oversized_lsd), &huge) == -1);
    CHECK(largest_request == 0);
    CHECK(huge.width == 0);
    CHECK(huge.height == 0);
    return 0;
}

static int check_frame_budget(void) {
    uint8_t index = 0;
    neverc_gif_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.width = 1;
    frame.height = 1;
    frame.indices = &index;
    frame.palette_size = 2;

    uint8_t *single = NULL;
    size_t single_len = 0;
    CHECK(neverc_gif_encode(&frame, &single, &single_len) == 0);
    CHECK(single_len > 1);

    size_t descriptor = 0;
    while (descriptor < single_len && single[descriptor] != 0x2c)
        descriptor++;
    CHECK(descriptor < single_len - 1);
    size_t frame_len = single_len - descriptor - 1U;
    CHECK(frame_len <=
          (SIZE_MAX - descriptor - 1U) / (GIF_MAX_FRAMES + 1U));
    size_t repeated_len =
        descriptor + frame_len * (GIF_MAX_FRAMES + 1U) + 1U;
    uint8_t *repeated = (uint8_t *)malloc(repeated_len);
    CHECK(repeated != NULL);
    memcpy(repeated, single, descriptor);
    size_t offset = descriptor;
    for (size_t i = 0; i < GIF_MAX_FRAMES + 1U; i++) {
        memcpy(repeated + offset, single + descriptor, frame_len);
        offset += frame_len;
    }
    repeated[offset++] = 0x3b;
    CHECK(offset == repeated_len);
    free(single);

    neverc_gif_image_t image;
    largest_request = 0;
    CHECK(neverc_gif_decode(repeated, repeated_len, &image) == -1);
    CHECK(largest_request <= TEST_ALLOCATION_LIMIT);
    free(repeated);
    return 0;
}

int main(void) {
    if (check_oversized_geometry() != 0 ||
        check_frame_budget() != 0)
        return 1;
    puts("passed");
    return 0;
}
