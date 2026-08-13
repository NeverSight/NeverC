#include <stdint.h>
#include <stdio.h>

#include "neverc/std/math/rand.h"

int main(void) {
    neverc_rand_seed(UINT64_C(0x72616e642d756273));

    for (int i = 0; i < 10000; i++) {
        if (neverc_rand_uint32n(UINT32_C(0x80000000)) >=
            UINT32_C(0x80000000)) {
            fprintf(stderr, "uint32n boundary result out of range\n");
            return 1;
        }
        if (neverc_rand_uint32n(UINT32_C(0x80000001)) >=
            UINT32_C(0x80000001)) {
            fprintf(stderr, "uint32n rejection result out of range\n");
            return 1;
        }
        if (neverc_rand_uint64n(UINT64_C(1) << 63) >=
            (UINT64_C(1) << 63)) {
            fprintf(stderr, "uint64n boundary result out of range\n");
            return 1;
        }
        if (neverc_rand_uint64n((UINT64_C(1) << 63) + 1U) >=
            (UINT64_C(1) << 63) + 1U) {
            fprintf(stderr, "uint64n rejection result out of range\n");
            return 1;
        }
    }

    return 0;
}
