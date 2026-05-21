// Basic test: -fbuiltin-mimalloc should compile without errors.
// When bootstrap bitcode is not available, the pass is a no-op.
// RUN: %neverc -fbuiltin-mimalloc %s -o %t && %t
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    void *p = malloc(1024);
    if (!p) {
        fprintf(stderr, "malloc failed\n");
        return 1;
    }
    memset(p, 0xAB, 1024);
    if (((unsigned char *)p)[512] != 0xAB) {
        fprintf(stderr, "memset verification failed\n");
        free(p);
        return 1;
    }
    free(p);

    void *q = calloc(256, sizeof(int));
    if (!q) {
        fprintf(stderr, "calloc failed\n");
        return 1;
    }
    for (int i = 0; i < 256; i++) {
        if (((int *)q)[i] != 0) {
            fprintf(stderr, "calloc zero-init failed at index %d\n", i);
            free(q);
            return 1;
        }
    }
    free(q);

    void *r = malloc(64);
    r = realloc(r, 4096);
    if (!r) {
        fprintf(stderr, "realloc failed\n");
        return 1;
    }
    free(r);

    printf("test_mimalloc_basic: ALL PASSED\n");
    return 0;
}
