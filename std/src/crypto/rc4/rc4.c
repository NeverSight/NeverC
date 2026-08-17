#include "neverc/std/crypto/rc4.h"
#include "neverc/std/_platform.h"
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
    if (!c) return -1;
    if (!key || key_len < 1 || key_len > 256) {
        neverc_platform_secure_zero(c, sizeof(*c));
        return -1;
    }

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
    if (!c || (len > 0 && (!dst || !src))) return;
    /* A scheduled S-box is a permutation of 0..255, so s[0]==s[1]==0 is
     * impossible after init. Reset/failed init is all zeros; XOR against
     * that is the identity — refuse it. */
    if (c->s[0] == 0 && c->s[1] == 0)
        return;
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
    if (!c) return;
    memset(c, 0, sizeof(*c));
}
