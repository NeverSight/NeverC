#include "neverc/crypto/rc4.h"
#include <string.h>

/*
 * RC4 stream cipher, ported from Go crypto/rc4.
 *
 * KSA (Key Scheduling Algorithm): initialize S-box permutation from key.
 * PRGA (Pseudo-Random Generation Algorithm): generate keystream bytes.
 *
 * NOTE: RC4 is cryptographically broken (biases in early output,
 * statistical weaknesses). Included for compatibility, not security.
 */

int neverc_rc4_init(neverc_rc4_cipher_t *c, const uint8_t *key, size_t key_len) {
    if (key_len < 1 || key_len > 256) return -1;

    for (int i = 0; i < 256; i++)
        c->s[i] = (uint32_t)i;

    uint8_t j = 0;
    for (int i = 0; i < 256; i++) {
        j += (uint8_t)c->s[i] + key[i % key_len];
        uint32_t tmp = c->s[i];
        c->s[i] = c->s[j];
        c->s[j] = tmp;
    }
    c->i = 0;
    c->j = 0;
    return 0;
}

void neverc_rc4_xor_keystream(neverc_rc4_cipher_t *c,
                              uint8_t *dst, const uint8_t *src, size_t len) {
    uint8_t i = c->i, j = c->j;
    for (size_t k = 0; k < len; k++) {
        i += 1;
        uint32_t x = c->s[i];
        j += (uint8_t)x;
        uint32_t y = c->s[j];
        c->s[i] = y;
        c->s[j] = x;
        dst[k] = src[k] ^ (uint8_t)c->s[(uint8_t)(x + y)];
    }
    c->i = i;
    c->j = j;
}

void neverc_rc4_reset(neverc_rc4_cipher_t *c) {
    memset(c, 0, sizeof(*c));
}
