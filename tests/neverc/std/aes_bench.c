/*
 * AES public-API microbenchmark. Compile this same file against each revision
 * with identical flags, then alternate binaries to compare their medians.
 * Keep the AES implementation in a separate translation unit without LTO.
 *
 * From the repository root:
 *   cc -std=c23 -O3 -fno-lto -Istd/include tests/neverc/std/aes_bench.c \
 *      std/src/crypto/aes/aes.c -o /tmp/neverc-aes-bench
 *   /tmp/neverc-aes-bench
 *
 * Reports CPU time in ns/call, including API checks and state clearing.
 * Override AES_BENCH_ITERATIONS/AES_BENCH_SAMPLES with -D for longer runs.
 */
#include "neverc/std/crypto/aes.h"
#include <stdio.h>
#include <time.h>

#ifndef AES_BENCH_ITERATIONS
#define AES_BENCH_ITERATIONS 100000
#endif
#ifndef AES_BENCH_SAMPLES
#define AES_BENCH_SAMPLES 5
#endif

static volatile uint32_t checksum;

static double median(double samples[AES_BENCH_SAMPLES]) {
    for (int i = 1; i < AES_BENCH_SAMPLES; i++) {
        double value = samples[i];
        int j = i;
        while (j > 0 && samples[j - 1] > value) {
            samples[j] = samples[j - 1];
            j--;
        }
        samples[j] = value;
    }
    return samples[AES_BENCH_SAMPLES / 2];
}

static double elapsed_ns(clock_t start) {
    return (double)(clock() - start) * 1e9 / CLOCKS_PER_SEC /
           AES_BENCH_ITERATIONS;
}

int main(void) {
    uint8_t key[32], block[16];
    for (unsigned i = 0; i < sizeof(key); i++) key[i] = (uint8_t)(i * 7 + 13);
    for (unsigned i = 0; i < sizeof(block); i++) block[i] = (uint8_t)(i * 3 + 17);
    printf("key_bits,init_ns,encrypt_ns,decrypt_ns\n");
    for (int key_len = 16; key_len <= 32; key_len += 8) {
        neverc_aes_ctx_t ctx;
        double init[AES_BENCH_SAMPLES], enc[AES_BENCH_SAMPLES];
        double dec[AES_BENCH_SAMPLES];
        if (neverc_aes_init(&ctx, key, key_len) != 0) return 1;
        for (int i = 0; i < 2000; i++)
            neverc_aes_encrypt_block(&ctx, block, block);
        for (int sample = 0; sample < AES_BENCH_SAMPLES; sample++) {
            clock_t start = clock();
            for (int i = 0; i < AES_BENCH_ITERATIONS; i++) {
                key[0] = (uint8_t)i;
                if (neverc_aes_init(&ctx, key, key_len) != 0) return 1;
            }
            init[sample] = elapsed_ns(start);
            checksum ^= ctx.enc_key[4 * ctx.rounds];

            start = clock();
            for (int i = 0; i < AES_BENCH_ITERATIONS; i++)
                neverc_aes_encrypt_block(&ctx, block, block);
            enc[sample] = elapsed_ns(start);
            checksum ^= block[0];

            start = clock();
            for (int i = 0; i < AES_BENCH_ITERATIONS; i++)
                neverc_aes_decrypt_block(&ctx, block, block);
            dec[sample] = elapsed_ns(start);
            checksum ^= block[0];
        }
        printf("%d,%.2f,%.2f,%.2f\n", key_len * 8,
               median(init), median(enc), median(dec));
    }
    printf("checksum=%08x\n", (unsigned)checksum);
    return 0;
}
