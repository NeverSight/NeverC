/*
 * PBKDF2 — Password-Based Key Derivation Function 2 (RFC 2898 / RFC 8018).
 * Uses HMAC-SHA256 as the PRF.
 */
#include "neverc/pbkdf2.h"
#include "neverc/hmac.h"
#include <string.h>

int neverc_pbkdf2_sha256(uint8_t *dk, size_t dk_len,
                         const uint8_t *password, size_t password_len,
                         const uint8_t *salt, size_t salt_len,
                         int iterations) {
    if (iterations < 1 || dk_len == 0) return -1;

    uint32_t block_num = 1;
    size_t off = 0;

    while (off < dk_len) {
        /* U_1 = HMAC(password, salt || INT_32_BE(block_num)) */
        uint8_t salt_block[256 + 4];
        size_t sb_len = salt_len + 4;
        if (salt_len > 256) return -1;
        if (salt != NULL && salt_len > 0)
            memcpy(salt_block, salt, salt_len);
        salt_block[salt_len]     = (uint8_t)(block_num >> 24);
        salt_block[salt_len + 1] = (uint8_t)(block_num >> 16);
        salt_block[salt_len + 2] = (uint8_t)(block_num >> 8);
        salt_block[salt_len + 3] = (uint8_t)(block_num);

        uint8_t u[32], t[32];
        neverc_hmac_sha256(password, password_len, salt_block, sb_len, u);
        memcpy(t, u, 32);

        for (int i = 1; i < iterations; i++) {
            neverc_hmac_sha256(password, password_len, u, 32, u);
            for (int j = 0; j < 32; j++) t[j] ^= u[j];
        }

        size_t n = dk_len - off;
        if (n > 32) n = 32;
        memcpy(dk + off, t, n);
        off += n;
        block_num++;
    }

    return 0;
}
