#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *fail_realloc(void *ptr, size_t size) {
    (void)ptr;
    (void)size;
    return NULL;
}

#define NC_H2_REALLOC fail_realloc
#include "../../../std/src/net/http/http2/http2_server.c"

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n",               \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

int main(void) {
    uint8_t *buffer = (uint8_t *)malloc(8);
    CHECK(buffer != NULL);
    memset(buffer, 0x5a, 8);

    uint8_t *original = buffer;
    size_t length = 8;
    size_t capacity = 8;
    const uint8_t extra = 0xa5;

    CHECK(h2_buffer_append(&buffer, &length, &capacity, &extra, 1) == -1);
    CHECK(buffer == original);
    CHECK(length == 8);
    CHECK(capacity == 8);
    for (size_t i = 0; i < length; i++)
        CHECK(buffer[i] == 0x5a);

    free(buffer);
    puts("passed");
    return 0;
}
