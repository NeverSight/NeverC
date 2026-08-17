#ifndef NEVERC_RC4_H
#define NEVERC_RC4_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * RC4 stream cipher (Bruce Schneier's Applied Cryptography).
 * WARNING: RC4 is cryptographically broken. Do not use for security.
 */

typedef struct {
    uint32_t s[256];
    uint8_t i, j;
} neverc_rc4_cipher_t;

/* key_len must be 1..256. NULL cipher/key or a bad length returns -1
 * and wipes the cipher so a previous key cannot keep encrypting. */
int  neverc_rc4_init(neverc_rc4_cipher_t *c, const uint8_t *key, size_t key_len);
void neverc_rc4_xor_keystream(neverc_rc4_cipher_t *c,
                              uint8_t *dst, const uint8_t *src, size_t len);
void neverc_rc4_reset(neverc_rc4_cipher_t *c);

#ifdef __cplusplus
}
#endif

#ifdef __neverc__
#include <neverc/std/crypto.h>
#endif

#endif /* NEVERC_RC4_H */
