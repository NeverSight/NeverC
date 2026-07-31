/*
 * ChaCha20-Poly1305 AEAD — RFC 8439 (formerly RFC 7539).
 * Combines ChaCha20 stream cipher with Poly1305 MAC.
 */
#include "neverc/std/crypto/chacha20poly1305.h"
#include "neverc/std/crypto/chacha20.h"
#include "neverc/std/crypto/poly1305.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static void put_le64(uint8_t *p, uint64_t v) {
    p[0] = (uint8_t)(v);       p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
    p[4] = (uint8_t)(v >> 32); p[5] = (uint8_t)(v >> 40);
    p[6] = (uint8_t)(v >> 48); p[7] = (uint8_t)(v >> 56);
}

static void pad16(uint8_t *mac_input, size_t *pos, size_t data_len) {
    size_t rem = data_len % 16;
    if (rem != 0) {
        uint8_t zeros[15];
        memset(zeros, 0, sizeof(zeros));
        memcpy(mac_input + *pos, zeros, 16 - rem);
        *pos += 16 - rem;
    }
}

/*
 * Construct the Poly1305 MAC input per RFC 8439 Section 2.8:
 *   AAD || pad(AAD) || ciphertext || pad(ct) || len(AAD) as LE64 || len(ct) as LE64
 *
 * Returns the total length written to mac_input.
 * Caller must provide buffer of aad_len + ct_len + 32 + 16 bytes.
 */
static size_t build_mac_data(uint8_t *mac_input,
                             const uint8_t *aad, size_t aad_len,
                             const uint8_t *ct, size_t ct_len) {
    size_t pos = 0;
    if (aad_len > 0) {
        memcpy(mac_input + pos, aad, aad_len);
        pos += aad_len;
    }
    pad16(mac_input, &pos, aad_len);
    if (ct_len > 0) {
        memcpy(mac_input + pos, ct, ct_len);
        pos += ct_len;
    }
    pad16(mac_input, &pos, ct_len);
    put_le64(mac_input + pos, (uint64_t)aad_len);
    pos += 8;
    put_le64(mac_input + pos, (uint64_t)ct_len);
    pos += 8;
    return pos;
}

static int mac_data_size(size_t aad_len, size_t ct_len, size_t *out) {
    if (aad_len > SIZE_MAX - 15 || ct_len > SIZE_MAX - 15)
        return -1;

    size_t padded_aad_len = (aad_len + 15) & ~(size_t)15;
    size_t padded_ct_len = (ct_len + 15) & ~(size_t)15;
    if (padded_aad_len > SIZE_MAX - padded_ct_len)
        return -1;

    size_t data_len = padded_aad_len + padded_ct_len;
    if (data_len > SIZE_MAX - 16)
        return -1;

    *out = data_len + 16;
    return 0;
}

size_t neverc_chacha20poly1305_seal(
    uint8_t *dst,
    const uint8_t key[32],
    const uint8_t nonce[12],
    const uint8_t *plaintext, size_t plaintext_len,
    const uint8_t *aad, size_t aad_len)
{
    uint8_t mac_buf_stack[512];
    uint8_t *mac_buf = mac_buf_stack;
    size_t mac_buf_size;
    if (mac_data_size(aad_len, plaintext_len, &mac_buf_size) != 0)
        return 0;
    if (mac_buf_size > sizeof(mac_buf_stack)) {
        mac_buf = (uint8_t *)malloc(mac_buf_size);
        if (!mac_buf)
            return 0;
    }

    /* Generate Poly1305 one-time key from block 0 */
    uint8_t poly_key[32];
    {
        neverc_chacha20_ctx ctx;
        neverc_chacha20_init(&ctx, key, nonce, 0);
        uint8_t block0[64];
        memset(block0, 0, 64);
        neverc_chacha20_xor(&ctx, block0, block0, 64);
        memcpy(poly_key, block0, 32);
    }

    /* Encrypt plaintext using ChaCha20 starting at counter=1 */
    {
        neverc_chacha20_ctx ctx;
        neverc_chacha20_init(&ctx, key, nonce, 1);
        neverc_chacha20_xor(&ctx, dst, plaintext, plaintext_len);
    }

    /* Build MAC input and compute tag. */
    size_t mac_len = build_mac_data(mac_buf, aad, aad_len,
                                    dst, plaintext_len);
    neverc_poly1305_auth(dst + plaintext_len, mac_buf, mac_len, poly_key);
    if (mac_buf != mac_buf_stack)
        free(mac_buf);

    return plaintext_len + 16;
}

int neverc_chacha20poly1305_open(
    uint8_t *dst,
    const uint8_t key[32],
    const uint8_t nonce[12],
    const uint8_t *ciphertext, size_t ciphertext_len,
    const uint8_t *aad, size_t aad_len)
{
    if (ciphertext_len < 16) return -1;
    size_t ct_len = ciphertext_len - 16;
    if (ct_len > INT_MAX) return -1;
    const uint8_t *tag = ciphertext + ct_len;

    uint8_t mac_buf_stack[512];
    uint8_t *mac_buf = mac_buf_stack;
    size_t mac_buf_size;
    if (mac_data_size(aad_len, ct_len, &mac_buf_size) != 0)
        return -1;
    if (mac_buf_size > sizeof(mac_buf_stack)) {
        mac_buf = (uint8_t *)malloc(mac_buf_size);
        if (!mac_buf)
            return -1;
    }

    /* Generate Poly1305 one-time key */
    uint8_t poly_key[32];
    {
        neverc_chacha20_ctx ctx;
        neverc_chacha20_init(&ctx, key, nonce, 0);
        uint8_t block0[64];
        memset(block0, 0, 64);
        neverc_chacha20_xor(&ctx, block0, block0, 64);
        memcpy(poly_key, block0, 32);
    }

    /* Verify tag before decrypting. */
    size_t mac_len = build_mac_data(mac_buf, aad, aad_len,
                                    ciphertext, ct_len);
    uint8_t computed_tag[16];
    neverc_poly1305_auth(computed_tag, mac_buf, mac_len, poly_key);
    if (mac_buf != mac_buf_stack)
        free(mac_buf);

    /* Constant-time comparison */
    uint8_t diff = 0;
    for (int i = 0; i < 16; i++)
        diff |= computed_tag[i] ^ tag[i];
    if (diff != 0) {
        return -1;
    }

    /* Decrypt */
    {
        neverc_chacha20_ctx ctx;
        neverc_chacha20_init(&ctx, key, nonce, 1);
        neverc_chacha20_xor(&ctx, dst, ciphertext, ct_len);
    }

    return (int)ct_len;
}
