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
#include "../../../std/src/image/jpeg/jpeg.c"
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
    uint8_t pixels[8 * 8 * 3];
    memset(pixels, 127, sizeof(pixels));
    neverc_jpeg_image_t image = {
        .width = 8, .height = 8, .channels = 3,
        .pixels = pixels, .stride = 8 * 3
    };

    reset_allocator(0);
    uint8_t *encoded = NULL;
    size_t encoded_length = 0;
    CHECK(neverc_jpeg_encode(&image, 80, &encoded, &encoded_length) == 0);
    size_t encode_allocations = allocation_count;
    free(encoded);
    for (size_t failure = 1; failure <= encode_allocations; failure++) {
        reset_allocator(failure);
        encoded = (uint8_t *)1;
        encoded_length = 99;
        CHECK(neverc_jpeg_encode(&image, 80, &encoded, &encoded_length) == -1);
        CHECK(encoded == NULL && encoded_length == 0);
    }
    puts("passed");
    return 0;
}
