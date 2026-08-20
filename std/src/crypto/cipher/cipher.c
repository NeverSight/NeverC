/*
 * AES block cipher modes: CBC and CTR.
 * Uses neverc_aes for the underlying block cipher.
 */
#include "neverc/std/crypto/cipher.h"
#include "neverc/std/crypto/aes.h"
#include "neverc/std/crypto/subtle.h"
#include "neverc/std/_platform.h"
#include <string.h>

static void xor_block(uint8_t *dst, const uint8_t *a, const uint8_t *b) {
    for (int i = 0; i < 16; i++) dst[i] = a[i] ^ b[i];
}

/* Increment only the documented 32-bit counter (nonce || counter).
 * Returns 1 when the counter wraps; the caller must poison the IV so a
 * follow-up call cannot emit nonce+1 || 0 (alias of the next nonce). */
static int increment_counter_low32(uint8_t ctr[16]) {
    for (int i = 15; i >= 12; i--) {
        if (++ctr[i] != 0) return 0;
    }
    return 1;
}

static const uint8_t k_ctr_exhausted[16] = {
    0x4e, 0x43, 0x49, 0x43, 0x54, 0x52, 0x58, 0xff,
    0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00
};

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
    if (memcmp(iv, k_ctr_exhausted, 16) == 0)
        return -1;
    if (len > 0) {
        uint32_t ctr = ((uint32_t)iv[12] << 24) | ((uint32_t)iv[13] << 16) |
                       ((uint32_t)iv[14] << 8) | (uint32_t)iv[15];
        /* Low 32 bits are the counter (nonce || counter). Wrapping them
         * into the nonce collides with another 96-bit nonce's block 0.
         * Compare byte length to remaining*16; (len+15)/16 wraps at
         * SIZE_MAX and would accept a request that reuses keystream. */
        uint64_t remaining_blocks = (uint64_t)(0xFFFFFFFFu - ctr) + 1u;
        if ((uint64_t)len > remaining_blocks * 16u)
            return -1;
    }
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
        int wrapped = increment_counter_low32(iv);

        size_t chunk = len - off;
        if (chunk > 16) chunk = 16;
        for (size_t i = 0; i < chunk; i++)
            dst[off + i] = src[off + i] ^ keystream[i];
        off += chunk;
        if (wrapped) {
            memcpy(iv, k_ctr_exhausted, 16);
            if (off < len) {
                neverc_platform_secure_zero(keystream, sizeof(keystream));
                neverc_platform_secure_zero(&ctx, sizeof(ctx));
                return -1;
            }
            break;
        }
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

int neverc_cipher_pkcs7_pad(uint8_t *dst, size_t dst_cap,
                            const uint8_t *src, size_t src_len,
                            size_t *padded_len)
{
    if (padded_len)
        *padded_len = 0;
    if (!padded_len || (dst_cap > 0 && !dst) || (src_len > 0 && !src))
        return -1;

    size_t pad_len = 16u - (src_len & 15u);
    if (src_len > SIZE_MAX - pad_len)
        return -1;
    size_t out_len = src_len + pad_len;
    if (out_len > dst_cap)
        return -1;

    if (src_len > 0)
        memmove(dst, src, src_len);
    memset(dst + src_len, (int)pad_len, pad_len);
    *padded_len = out_len;
    return 0;
}

int neverc_cipher_pkcs7_unpad(uint8_t *dst, size_t dst_cap,
                              const uint8_t *src, size_t src_len,
                              size_t *unpadded_len)
{
    if (unpadded_len)
        *unpadded_len = 0;
    if (!unpadded_len || (dst_cap > 0 && !dst) || (src_len > 0 && !src) ||
        src_len < 16 || (src_len & 15u) != 0)
        return -1;

    /* Scan a fixed 16-byte window so invalid n=0, n=17, and mismatched
     * padding bytes take the same path and return the same error. */
    uint8_t pad_len = src[src_len - 1];
    unsigned int pad_len_ok = ((unsigned int)pad_len - 1u) < 16u;
    uint8_t use_pad = (uint8_t)neverc_subtle_constant_time_select(
        (int)pad_len_ok, (int)pad_len, 16);

    uint8_t mismatch = 0;
    for (int i = 0; i < 16; i++) {
        unsigned int from_end = (unsigned int)(15 - i);
        unsigned int in_padding =
            ((unsigned int)(from_end - (unsigned int)use_pad) >> 31);
        unsigned int mask = 0u - in_padding;
        mismatch = (uint8_t)(mismatch | (uint8_t)(mask & (src[src_len - 16 + (size_t)i] ^ use_pad)));
    }

    unsigned int good = pad_len_ok &
        (unsigned int)neverc_subtle_constant_time_byte_eq(mismatch, 0);
    size_t out_len = src_len - (size_t)use_pad;
    good &= (out_len <= dst_cap) ? 1u : 0u;
    if (good != 1u)
        return -1;

    out_len = src_len - (size_t)pad_len;

    if (out_len > 0)
        memmove(dst, src, out_len);
    *unpadded_len = out_len;
    return 0;
}
