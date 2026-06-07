#ifndef NEVERC_POLY1305_H
#define NEVERC_POLY1305_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_POLY1305_KEY_SIZE 32
#define NEVERC_POLY1305_TAG_SIZE 16

void neverc_poly1305_auth(uint8_t tag[16], const uint8_t *msg, size_t msg_len,
                          const uint8_t key[32]);
int  neverc_poly1305_verify(const uint8_t tag[16], const uint8_t *msg, size_t msg_len,
                            const uint8_t key[32]);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/crypto.h>
#endif

#endif /* NEVERC_POLY1305_H */
