#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int realloc_calls;
static size_t last_realloc_size;

static void *fail_realloc(void *ptr, size_t size) {
    (void)ptr;
    last_realloc_size = size;
    realloc_calls++;
    return NULL;
}

#define NC_NET_REALLOC fail_realloc
#include "_net_internal.h"

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n",               \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

int main(void) {
    const char byte = 'x';
    nc_buf_t buf;
    nc_buf_init(&buf);

    CHECK(nc_buf_append(&buf, &byte, 1) == -1);
    CHECK(realloc_calls == 1);
    CHECK(buf.data == NULL);
    CHECK(buf.len == 0);
    CHECK(buf.cap == 0);

    buf.data = (char *)malloc(1);
    CHECK(buf.data != NULL);
    buf.data[0] = 'a';
    buf.len = 1;
    buf.cap = 1;
    char *original = buf.data;

    CHECK(nc_buf_append(&buf, &byte, 1) == -1);
    CHECK(buf.data == original);
    CHECK(buf.data[0] == 'a');
    CHECK(buf.len == 1);
    CHECK(buf.cap == 1);

    realloc_calls = 0;
    buf.len = SIZE_MAX;
    CHECK(nc_buf_append(&buf, NULL, 0) == -1);
    CHECK(realloc_calls == 0);
    buf.len = 1;

    CHECK(nc_buf_append(&buf, NULL, 1) == -1);
    CHECK(realloc_calls == 0);

    realloc_calls = 0;
    last_realloc_size = 0;
    buf.len = (SIZE_MAX / 2) + 8;
    buf.cap = buf.len;
    CHECK(nc_buf_append(&buf, &byte, 1) == -1);
    CHECK(realloc_calls == 1);
    CHECK(last_realloc_size == buf.len + 1 + 1);
    CHECK(last_realloc_size > buf.cap);
    CHECK(buf.data == original);
    CHECK(buf.len == (SIZE_MAX / 2) + 8);
    CHECK(buf.cap == (SIZE_MAX / 2) + 8);

    nc_buf_free(&buf);
    puts("passed");
    return 0;
}
