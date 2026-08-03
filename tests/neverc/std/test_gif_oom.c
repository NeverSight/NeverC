#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t allocation_count;
static size_t fail_at;

static int allocation_fails(void) {
    allocation_count++;
    return fail_at != 0 && allocation_count == fail_at;
}

static void *controlled_malloc(size_t size) {
    return allocation_fails() ? NULL : malloc(size);
}

static void *controlled_calloc(size_t count, size_t size) {
    return allocation_fails() ? NULL : calloc(count, size);
}

static void *controlled_realloc(void *ptr, size_t size) {
    return allocation_fails() ? NULL : realloc(ptr, size);
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

static void reset_allocator(size_t failure) {
    allocation_count = 0;
    fail_at = failure;
}

int main(void) {
    uint8_t indices[256];
    for (size_t i = 0; i < sizeof(indices); i++) indices[i] = (uint8_t)(i & 3);
    neverc_gif_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.width = 16;
    frame.height = 16;
    frame.palette_size = 4;
    frame.indices = indices;

    reset_allocator(0);
    uint8_t *encoded = NULL;
    size_t encoded_length = 0;
    CHECK(neverc_gif_encode(&frame, &encoded, &encoded_length) == 0);
    size_t encode_allocations = allocation_count;
    for (size_t failure = 1; failure <= encode_allocations; failure++) {
        reset_allocator(failure);
        uint8_t *failed_output = (uint8_t *)1;
        size_t failed_length = 99;
        CHECK(neverc_gif_encode(&frame, &failed_output, &failed_length) == -1);
        CHECK(failed_output == NULL && failed_length == 0);
    }

    reset_allocator(0);
    neverc_gif_image_t image;
    CHECK(neverc_gif_decode(encoded, encoded_length, &image) == 0);
    size_t decode_allocations = allocation_count;
    neverc_gif_free(&image);
    for (size_t failure = 1; failure <= decode_allocations; failure++) {
        reset_allocator(failure);
        memset(&image, 0xA5, sizeof(image));
        CHECK(neverc_gif_decode(encoded, encoded_length, &image) == -1);
        CHECK(image.frames == NULL && image.num_frames == 0);
    }
    free(encoded);
    puts("passed");
    return 0;
}
