#include "neverc/std/crypto/mlkem.h"
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { THREAD_COUNT = 4, ROUND_COUNT = 4 };

static neverc_mlkem768_dk_t decapsulation_key;
static neverc_mlkem768_ek_t encapsulation_key;

static void *run_round_trips(void *unused) {
    (void)unused;
    for (int i = 0; i < ROUND_COUNT; i++) {
        uint8_t sender_key[NEVERC_MLKEM_SHARED_KEY_SIZE];
        uint8_t recipient_key[NEVERC_MLKEM_SHARED_KEY_SIZE];
        uint8_t ciphertext[NEVERC_MLKEM768_CT_SIZE];
        if (neverc_mlkem768_encapsulate(
                &encapsulation_key, sender_key, ciphertext) != 0 ||
            neverc_mlkem768_decapsulate(
                &decapsulation_key, ciphertext, recipient_key) != 0 ||
            memcmp(sender_key, recipient_key, sizeof(sender_key)) != 0)
            return (void *)(uintptr_t)1;
    }
    return NULL;
}

int main(void) {
    uint8_t seed[NEVERC_MLKEM_SEED_SIZE];
    for (size_t i = 0; i < sizeof(seed); i++)
        seed[i] = (uint8_t)(i * 17U + 3U);
    if (neverc_mlkem768_new_dk(&decapsulation_key, seed) != 0) {
        fputs("deterministic key generation failed\n", stderr);
        return 1;
    }
    neverc_mlkem768_dk_encapsulation_key(
        &decapsulation_key, &encapsulation_key);

    pthread_t threads[THREAD_COUNT];
    for (int i = 0; i < THREAD_COUNT; i++) {
        if (pthread_create(
                &threads[i], NULL, run_round_trips, NULL) != 0) {
            fputs("pthread_create failed\n", stderr);
            return 1;
        }
    }
    for (int i = 0; i < THREAD_COUNT; i++) {
        void *result = NULL;
        if (pthread_join(threads[i], &result) != 0 || result != NULL) {
            fputs("concurrent ML-KEM round trip failed\n", stderr);
            return 1;
        }
    }

    puts("passed");
    return 0;
}
