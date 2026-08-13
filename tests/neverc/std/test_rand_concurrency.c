#include "neverc/std/math/rand.h"
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>

enum { THREAD_COUNT = 8, DRAWS_PER_THREAD = 20000 };

static void *draw_random_values(void *argument) {
    uintptr_t index = (uintptr_t)argument;
    for (int i = 0; i < DRAWS_PER_THREAD; i++) {
        (void)neverc_rand_uint64();
        if (neverc_rand_uint32n(1009) >= 1009)
            return (void *)(uintptr_t)1;
        if ((i & 1023) == 0 && index == 0)
            neverc_rand_seed((uint64_t)i + UINT64_C(0x72616e64));
    }
    return NULL;
}

int main(void) {
    pthread_t threads[THREAD_COUNT];
    neverc_rand_seed(UINT64_C(0x636f6e6375727265));

    for (uintptr_t i = 0; i < THREAD_COUNT; i++) {
        if (pthread_create(&threads[i], NULL, draw_random_values,
                           (void *)i) != 0) {
            fputs("pthread_create failed\n", stderr);
            return 1;
        }
    }
    for (int i = 0; i < THREAD_COUNT; i++) {
        void *result = NULL;
        if (pthread_join(threads[i], &result) != 0 || result != NULL) {
            fputs("concurrent random draw failed\n", stderr);
            return 1;
        }
    }

    puts("passed");
    return 0;
}
