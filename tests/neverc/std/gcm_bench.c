/*
 * AES-GCM public-API benchmark. Build each revision with identical flags and
 * alternate binaries to compare medians; keep separate units without LTO.
 * From the repository root:
 *   cc -std=c23 -O3 -fno-lto -Istd/include tests/neverc/std/gcm_bench.c \
 *      std/src/crypto/gcm/gcm.c std/src/crypto/aes/aes.c \
 *      std/src/crypto/subtle/subtle.c -o /tmp/neverc-gcm-bench
 *
 * Reports CPU ns/call including validation, GHASH, AES and state clearing.
 * Seal advances the nonce each call. Open authenticates the last sealed record.
 * GCM_BENCH_BYTES and GCM_BENCH_SAMPLES can be overridden with -D.
 */
#include "neverc/std/crypto/gcm.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifndef GCM_BENCH_BYTES
#define GCM_BENCH_BYTES 262144
#endif
#ifndef GCM_BENCH_SAMPLES
#define GCM_BENCH_SAMPLES 5
#endif

static volatile uint32_t checksum;

static double median(double samples[GCM_BENCH_SAMPLES]) {
    for (unsigned i = 1; i < GCM_BENCH_SAMPLES; i++) {
        double value = samples[i];
        unsigned j = i;
        while (j > 0 && samples[j - 1] > value) {
            samples[j] = samples[j - 1];
            j--;
        }
        samples[j] = value;
    }
    return samples[GCM_BENCH_SAMPLES / 2];
}

static void set_nonce(uint8_t nonce[12], uint64_t sequence) {
    for (unsigned i = 0; i < 8; i++) nonce[4 + i] = (uint8_t)(sequence >> (8 * i));
}

int main(void) {
    static const size_t sizes[] = {0, 16, 256, 4096, 16384};
    uint8_t key[32], pt[16384], ct[16384], decoded[16384], aad[32];
    for (unsigned i = 0; i < sizeof(key); i++) key[i] = (uint8_t)(i * 7 + 13);
    for (unsigned i = 0; i < sizeof(pt); i++) pt[i] = (uint8_t)(i * 3 + 17);
    for (unsigned i = 0; i < sizeof(aad); i++) aad[i] = (uint8_t)(i * 5 + 19);
    printf("key_bits,bytes,aad_bytes,seal_ns,open_ns\n");
    for (int key_len = 16; key_len <= 32; key_len += 8) {
        neverc_gcm_ctx ctx;
        uint8_t nonce[12] = {0}, tag[16];
        uint64_t sequence = 0;
        if (neverc_gcm_init(&ctx, key, key_len) != 0) return 1;
        for (unsigned i = 0; i < 200; i++) {
            set_nonce(nonce, sequence++);
            if (neverc_gcm_seal(&ctx, nonce, pt, 256, aad, sizeof(aad), ct, tag)) return 1;
        }
        for (unsigned n = 0; n < sizeof(sizes) / sizeof(sizes[0]); n++) {
            size_t len = sizes[n];
            size_t iterations = GCM_BENCH_BYTES / (len ? len : 16);
            if (iterations == 0) iterations = 1;
            double seal[GCM_BENCH_SAMPLES], open[GCM_BENCH_SAMPLES];
            for (unsigned sample = 0; sample < GCM_BENCH_SAMPLES; sample++) {
                clock_t start = clock();
                for (size_t i = 0; i < iterations; i++) {
                    set_nonce(nonce, sequence++);
                    if (neverc_gcm_seal(&ctx, nonce, pt, len, aad, sizeof(aad), ct, tag)) return 1;
                }
                seal[sample] = (double)(clock() - start) * 1e9 / CLOCKS_PER_SEC / iterations;
                checksum ^= tag[0];
                start = clock();
                for (size_t i = 0; i < iterations; i++)
                    if (neverc_gcm_open(&ctx, nonce, ct, len, aad, sizeof(aad), tag, decoded)) return 1;
                open[sample] = (double)(clock() - start) * 1e9 / CLOCKS_PER_SEC / iterations;
                if (memcmp(decoded, pt, len) != 0) return 1;
                checksum ^= tag[15];
            }
            printf("%d,%zu,%zu,%.2f,%.2f\n", key_len * 8, len, sizeof(aad), median(seal), median(open));
        }
    }
    printf("checksum=%08x\n", (unsigned)checksum);
    return 0;
}
