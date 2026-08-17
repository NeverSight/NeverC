#ifndef NEVERC_CHACHA20_H
#define NEVERC_CHACHA20_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_CHACHA20_KEY_SIZE   32
#define NEVERC_CHACHA20_NONCE_SIZE 12
#define NEVERC_CHACHA20_BLOCK_SIZE 64

typedef struct {
    uint32_t state[16];
    uint8_t  buf[64];
    int      buf_used;
} neverc_chacha20_ctx;

void neverc_chacha20_init(neverc_chacha20_ctx *ctx,
                          const uint8_t key[32],
                          const uint8_t nonce[12],
                          uint32_t counter);
void neverc_chacha20_xor(neverc_chacha20_ctx *ctx,
                         uint8_t *dst, const uint8_t *src, size_t len);
/* Checked XOR. Returns 0 on success and -1 if the context is unusable,
 * a span is invalid, or the request would wrap the 32-bit block counter. */
int neverc_chacha20_xor_checked(neverc_chacha20_ctx *ctx,
                                uint8_t *dst, const uint8_t *src, size_t len);
void neverc_chacha20_block(const uint32_t state[16], uint8_t out[64]);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/crypto.h>
#endif

#endif /* NEVERC_CHACHA20_H */
