/*
 * kdf_bench.c — A/B benchmark: naive HMAC-loop KDF vs precomputed-midstate KDF.
 *
 * The "old" path reproduces the previous implementation verbatim: every PBKDF2
 * iteration / HKDF block calls a full HMAC that re-absorbs the (key^ipad) and
 * (key^opad) blocks (4 SHA-256 compressions per PRF call).  The "new" path is
 * the library's neverc_pbkdf2_sha256 / neverc_hkdf_expand_* which precompute
 * the two key midstates once (2 compressions per PRF call).
 *
 * Build (from repo root):
 *   build-neverc/bin/neverc -Istd/include -O2 -fno-builtin-std -o /tmp/kdf_bench \
 *     tests/neverc/std/kdf_bench.c \
 *     std/src/crypto/pbkdf2/pbkdf2.c std/src/crypto/hkdf/hkdf.c \
 *     std/src/crypto/hmac/hmac.c std/src/crypto/sha256/sha256.c \
 *     std/src/crypto/sha512/sha512.c std/src/crypto/sha1/sha1.c \
 *     std/src/crypto/md5/md5.c std/src/crypto/subtle/subtle.c -lm
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "neverc/std/crypto/sha256.h"
#include "neverc/std/crypto/sha512.h"
#include "neverc/std/crypto/pbkdf2.h"
#include "neverc/std/crypto/hkdf.h"

/* ============================================================
 * OLD: naive HMAC-SHA256 (re-absorbs pads every call), noinline
 * ============================================================ */
__attribute__((noinline))
static void old_hmac_sha256(const uint8_t *key, size_t key_len,
                            const uint8_t *data, size_t data_len,
                            uint8_t out[32]) {
    uint8_t k_prime[64];
    memset(k_prime, 0, 64);
    if (key_len > 64) neverc_sha256_sum(key, key_len, k_prime);
    else              memcpy(k_prime, key, key_len);

    uint8_t ipad[64], opad[64];
    for (int i = 0; i < 64; i++) { ipad[i] = k_prime[i] ^ 0x36; opad[i] = k_prime[i] ^ 0x5c; }

    neverc_sha256_ctx inner;
    neverc_sha256_init(&inner);
    neverc_sha256_update(&inner, ipad, 64);
    neverc_sha256_update(&inner, data, data_len);
    uint8_t inner_hash[32];
    neverc_sha256_final(&inner, inner_hash);

    neverc_sha256_ctx outer;
    neverc_sha256_init(&outer);
    neverc_sha256_update(&outer, opad, 64);
    neverc_sha256_update(&outer, inner_hash, 32);
    neverc_sha256_final(&outer, out);
}

__attribute__((noinline))
static int old_pbkdf2_sha256(uint8_t *dk, size_t dk_len,
                             const uint8_t *password, size_t password_len,
                             const uint8_t *salt, size_t salt_len,
                             int iterations) {
    if (iterations < 1 || dk_len == 0) return -1;
    uint32_t block_num = 1;
    size_t off = 0;
    while (off < dk_len) {
        uint8_t salt_block[256 + 4];
        size_t sb_len = salt_len + 4;
        if (salt_len > 256) return -1;
        if (salt && salt_len) memcpy(salt_block, salt, salt_len);
        salt_block[salt_len]   = (uint8_t)(block_num >> 24);
        salt_block[salt_len+1] = (uint8_t)(block_num >> 16);
        salt_block[salt_len+2] = (uint8_t)(block_num >> 8);
        salt_block[salt_len+3] = (uint8_t)(block_num);

        uint8_t u[32], t[32];
        old_hmac_sha256(password, password_len, salt_block, sb_len, u);
        memcpy(t, u, 32);
        for (int i = 1; i < iterations; i++) {
            old_hmac_sha256(password, password_len, u, 32, u);
            for (int j = 0; j < 32; j++) t[j] ^= u[j];
        }
        size_t n = dk_len - off; if (n > 32) n = 32;
        memcpy(dk + off, t, n);
        off += n; block_num++;
    }
    return 0;
}

__attribute__((noinline))
static int old_hkdf_expand_sha256(uint8_t *okm, size_t okm_len,
                                  const uint8_t prk[32],
                                  const uint8_t *info, size_t info_len) {
    if (okm_len > 255 * 32) return -1;
    uint8_t t[32]; size_t t_len = 0, off = 0; uint8_t counter = 1;
    while (off < okm_len) {
        uint8_t in[32 + 256 + 1]; size_t il = 0;
        if (t_len) { memcpy(in, t, t_len); il += t_len; }
        if (info && info_len) { memcpy(in + il, info, info_len); il += info_len; }
        in[il++] = counter;
        old_hmac_sha256(prk, 32, in, il, t);
        t_len = 32;
        size_t n = okm_len - off; if (n > 32) n = 32;
        memcpy(okm + off, t, n);
        off += n; counter++;
    }
    return 0;
}

/* ============================================================
 * Timing helpers
 * ============================================================ */
static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static uint64_t checksum(const uint8_t *p, size_t n) {
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ULL; }
    return h;
}

int main(void) {
    const uint8_t password[] = "correct horse battery staple";
    const uint8_t salt[] = "NaCl-bench-salt-0123456789";

    printf("=== PBKDF2-SHA256: old (naive HMAC) vs new (precomputed midstate) ===\n");
    printf("%-12s  %12s  %12s  %8s  %s\n", "iterations", "old (ms)", "new (ms)", "speedup", "match");
    int iter_set[] = {10000, 50000, 100000, 200000};
    for (size_t s = 0; s < sizeof(iter_set)/sizeof(iter_set[0]); s++) {
        int iters = iter_set[s];
        uint8_t dk_old[64], dk_new[64];

        double t0 = now_ms();
        old_pbkdf2_sha256(dk_old, sizeof(dk_old), password, sizeof(password)-1,
                          salt, sizeof(salt)-1, iters);
        double t1 = now_ms();
        neverc_pbkdf2_sha256(dk_new, sizeof(dk_new), password, sizeof(password)-1,
                             salt, sizeof(salt)-1, iters);
        double t2 = now_ms();

        int match = memcmp(dk_old, dk_new, sizeof(dk_old)) == 0;
        printf("%-12d  %12.2f  %12.2f  %7.2fx  %s\n",
               iters, t1 - t0, t2 - t1, (t1 - t0) / (t2 - t1),
               match ? "OK" : "MISMATCH");
        if (!match) return 1;
    }

    printf("\n=== HKDF-Expand-SHA256: old vs new (large OKM) ===\n");
    printf("%-12s  %12s  %12s  %8s  %s\n", "okm_bytes", "old (ms)", "new (ms)", "speedup", "match");
    uint8_t prk[32];
    for (int i = 0; i < 32; i++) prk[i] = (uint8_t)(i * 7 + 1);
    const uint8_t info[] = "hkdf-expand-benchmark-info-string";
    int len_set[] = {1024, 4096, 8160}; /* 8160 = 255*32 max */
    int reps = 20000;
    for (size_t s = 0; s < sizeof(len_set)/sizeof(len_set[0]); s++) {
        int L = len_set[s];
        uint8_t *okm_old = malloc(L), *okm_new = malloc(L);

        double t0 = now_ms();
        for (int r = 0; r < reps; r++)
            old_hkdf_expand_sha256(okm_old, L, prk, info, sizeof(info)-1);
        double t1 = now_ms();
        for (int r = 0; r < reps; r++)
            neverc_hkdf_expand_sha256(okm_new, L, prk, info, sizeof(info)-1);
        double t2 = now_ms();

        int match = memcmp(okm_old, okm_new, L) == 0;
        printf("%-12d  %12.2f  %12.2f  %7.2fx  %s\n",
               L, t1 - t0, t2 - t1, (t1 - t0) / (t2 - t1),
               match ? "OK" : "MISMATCH");
        (void)checksum(okm_new, L);
        free(okm_old); free(okm_new);
        if (!match) return 1;
    }

    printf("\nAll outputs match. Benchmark complete.\n");
    return 0;
}
