/*
 * AES block cipher modes: CBC and CTR.
 * Uses neverc_aes for the underlying block cipher.
 */
#include "neverc/std/crypto/cipher.h"
#include "neverc/std/crypto/aes.h"
#include "neverc/std/_platform.h"
#include <string.h>

static void xor_block(uint8_t *dst, const uint8_t *a, const uint8_t *b) {
    for (int i = 0; i < 16; i++) dst[i] = a[i] ^ b[i];
}

static void increment_counter(uint8_t ctr[16]) {
    for (int i = 15; i >= 0; i--) {
        if (++ctr[i] != 0) break;
    }
}

/* dst after src with overlap: a later block would read source bytes that
 * an earlier write already clobbered. Slide into dst, then treat as in-place. */
static const uint8_t *src_for_overlapping_dst(uint8_t *dst, const uint8_t *src,
                                              size_t len) {
    if (len == 0)
        return src;
    uintptr_t d = (uintptr_t)dst;
    uintptr_t s = (uintptr_t)src;
    if (d > s && (d - s) < (uintptr_t)len) {
        memmove(dst, src, len);
        return dst;
    }
    return src;
}

int neverc_cipher_cbc_encrypt(
    const uint8_t *key, int key_len,
    uint8_t iv[16],
    uint8_t *dst, const uint8_t *src, size_t len)
{
    if (!key || !iv || (!dst && len != 0) || (!src && len != 0) ||
        len % 16 != 0)
        return -1;

    neverc_aes_ctx_t ctx;
    if (neverc_aes_init(&ctx, key, key_len) != 0) {
        neverc_platform_secure_zero(&ctx, sizeof(ctx));
        return -1;
    }

    src = src_for_overlapping_dst(dst, src, len);

    uint8_t block[16];
    for (size_t off = 0; off < len; off += 16) {
        xor_block(block, src + off, iv);
        neverc_aes_encrypt_block(&ctx, dst + off, block);
        memcpy(iv, dst + off, 16);
    }
    neverc_platform_secure_zero(block, sizeof(block));
    neverc_platform_secure_zero(&ctx, sizeof(ctx));
    return 0;
}

int neverc_cipher_cbc_decrypt(
    const uint8_t *key, int key_len,
    uint8_t iv[16],
    uint8_t *dst, const uint8_t *src, size_t len)
{
    if (!key || !iv || (!dst && len != 0) || (!src && len != 0) ||
        len % 16 != 0)
        return -1;

    neverc_aes_ctx_t ctx;
    if (neverc_aes_init(&ctx, key, key_len) != 0) {
        neverc_platform_secure_zero(&ctx, sizeof(ctx));
        return -1;
    }

    src = src_for_overlapping_dst(dst, src, len);

    uint8_t ciphertext_block[16], decrypted[16];
    for (size_t off = 0; off < len; off += 16) {
        /* Preserve the input block before writing so dst == src is safe. */
        memcpy(ciphertext_block, src + off, sizeof(ciphertext_block));
        neverc_aes_decrypt_block(&ctx, decrypted, ciphertext_block);
        xor_block(dst + off, decrypted, iv);
        memcpy(iv, ciphertext_block, 16);
    }
    neverc_platform_secure_zero(ciphertext_block, sizeof(ciphertext_block));
    neverc_platform_secure_zero(decrypted, sizeof(decrypted));
    neverc_platform_secure_zero(&ctx, sizeof(ctx));
    return 0;
}

int neverc_cipher_ctr_checked(
    const uint8_t *key, int key_len,
    uint8_t iv[16],
    uint8_t *dst, const uint8_t *src, size_t len)
{
    if (!key || !iv || (!dst && len != 0) || (!src && len != 0))
        return -1;
    neverc_aes_ctx_t ctx;
    if (neverc_aes_init(&ctx, key, key_len) != 0) {
        neverc_platform_secure_zero(&ctx, sizeof(ctx));
        return -1;
    }

    src = src_for_overlapping_dst(dst, src, len);

    size_t off = 0;
    uint8_t keystream[16];
    while (off < len) {
        neverc_aes_encrypt_block(&ctx, keystream, iv);
        increment_counter(iv);

        size_t chunk = len - off;
        if (chunk > 16) chunk = 16;
        for (size_t i = 0; i < chunk; i++)
            dst[off + i] = src[off + i] ^ keystream[i];
        off += chunk;
    }
    neverc_platform_secure_zero(keystream, sizeof(keystream));
    neverc_platform_secure_zero(&ctx, sizeof(ctx));
    return 0;
}

void neverc_cipher_ctr(
    const uint8_t *key, int key_len,
    uint8_t iv[16],
    uint8_t *dst, const uint8_t *src, size_t len)
{
    (void)neverc_cipher_ctr_checked(key, key_len, iv, dst, src, len);
}
