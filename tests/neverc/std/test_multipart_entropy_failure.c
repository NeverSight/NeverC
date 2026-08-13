#include "neverc/std/mime/multipart.h"
#include <stdio.h>
#include <string.h>

static int entropy_fails(unsigned char *buffer, size_t length) {
    if (buffer) memset(buffer, 0xa5, length);
    return -1;
}

#define NCI_MULTIPART_RANDOM entropy_fails
#include "../../../std/src/mime/multipart/multipart.c"
#undef NCI_MULTIPART_RANDOM

int main(void) {
    char boundary[64];
    memset(boundary, 'x', sizeof(boundary));

    if (neverc_multipart_generate_boundary(
            boundary, sizeof(boundary)) != -1 ||
        boundary[0] != '\0') {
        fputs("entropy failure was not propagated\n", stderr);
        return 1;
    }

    puts("passed");
    return 0;
}
