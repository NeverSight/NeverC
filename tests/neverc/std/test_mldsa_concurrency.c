#include "neverc/std/crypto/mldsa.h"
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { THREAD_COUNT = 4 };

static uint8_t seed[NEVERC_MLDSA_SEED_SIZE];
static neverc_mldsa44_pk_t expected_public_key;

static void *derive_key(void *unused) {
    (void)unused;
    neverc_mldsa44_sk_t secret_key;
    neverc_mldsa44_pk_t public_key;
    if (neverc_mldsa44_new_sk(&secret_key, seed) != 0)
        return (void *)(uintptr_t)1;
    neverc_mldsa44_sk_public_key(&secret_key, &public_key);
    if (memcmp(public_key.pk, expected_public_key.pk,
               sizeof(public_key.pk)) != 0)
        return (void *)(uintptr_t)1;
    return NULL;
}

int main(void) {
    for (size_t i = 0; i < sizeof(seed); i++)
        seed[i] = (uint8_t)(i * 29U + 7U);

    neverc_mldsa44_sk_t initial_key;
    if (neverc_mldsa44_new_sk(&initial_key, seed) != 0) {
        fputs("initial key derivation failed\n", stderr);
        return 1;
    }
    neverc_mldsa44_sk_public_key(&initial_key, &expected_public_key);

    pthread_t threads[THREAD_COUNT];
    for (int i = 0; i < THREAD_COUNT; i++) {
        if (pthread_create(&threads[i], NULL, derive_key, NULL) != 0) {
            fputs("pthread_create failed\n", stderr);
            return 1;
        }
    }
    for (int i = 0; i < THREAD_COUNT; i++) {
        void *result = NULL;
        if (pthread_join(threads[i], &result) != 0 || result != NULL) {
            fputs("concurrent ML-DSA key derivation failed\n", stderr);
            return 1;
        }
    }

    puts("passed");
    return 0;
}
