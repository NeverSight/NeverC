/*
 * AES block cipher modes: CBC and CTR.
 * Uses neverc_aes for the underlying block cipher.
 */
#include "neverc/cipher.h"
#include "neverc/aes.h"
#include <string.h>

static void xor_block(uint8_t *dst, const uint8_t *a, const uint8_t *b) {
    for (int i = 0; i < 16; i++) dst[i] = a[i] ^ b[i];
}

static void increment_counter(uint8_t ctr[16]) {
    for (int i = 15; i >= 0; i--) {
        if (++ctr[i] != 0) break;
    }
}

int neverc_cipher_cbc_encrypt(
    const uint8_t *key, int key_len,
    uint8_t iv[16],
    uint8_t *dst, const uint8_t *src, size_t len)
{
    if (len % 16 != 0) return -1;

    neverc_aes_ctx_t ctx;
    if (neverc_aes_init(&ctx, key, key_len) != 0) return -1;

    for (size_t off = 0; off < len; off += 16) {
        uint8_t block[16];
        xor_block(block, src + off, iv);
        neverc_aes_encrypt_block(&ctx, dst + off, block);
        memcpy(iv, dst + off, 16);
    }
    return 0;
}

int neverc_cipher_cbc_decrypt(
    const uint8_t *key, int key_len,
    uint8_t iv[16],
    uint8_t *dst, const uint8_t *src, size_t len)
{
    if (len % 16 != 0) return -1;

    neverc_aes_ctx_t ctx;
    if (neverc_aes_init(&ctx, key, key_len) != 0) return -1;

    for (size_t off = 0; off < len; off += 16) {
        uint8_t decrypted[16];
        neverc_aes_decrypt_block(&ctx, decrypted, src + off);
        xor_block(dst + off, decrypted, iv);
        memcpy(iv, src + off, 16);
    }
    return 0;
}

void neverc_cipher_ctr(
    const uint8_t *key, int key_len,
    uint8_t iv[16],
    uint8_t *dst, const uint8_t *src, size_t len)
{
    neverc_aes_ctx_t ctx;
    neverc_aes_init(&ctx, key, key_len);

    size_t off = 0;
    while (off < len) {
        uint8_t keystream[16];
        neverc_aes_encrypt_block(&ctx, keystream, iv);
        increment_counter(iv);

        size_t chunk = len - off;
        if (chunk > 16) chunk = 16;
        for (size_t i = 0; i < chunk; i++)
            dst[off + i] = src[off + i] ^ keystream[i];
        off += chunk;
    }
}
