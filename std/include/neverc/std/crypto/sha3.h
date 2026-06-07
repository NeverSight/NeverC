#ifndef NEVERC_SHA3_H
#define NEVERC_SHA3_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * SHA-3 (Keccak) hash family — FIPS 202.
 * Supports SHA3-224, SHA3-256, SHA3-384, SHA3-512, and SHAKE128/SHAKE256.
 */

typedef struct {
    uint64_t state[25];
    uint8_t  buf[200];
    size_t   rate;
    size_t   buf_len;
    uint8_t  suffix;
    int      squeezed;
} neverc_sha3_ctx;

/* SHA3-224: 28-byte digest, 144-byte rate */
void neverc_sha3_224_init(neverc_sha3_ctx *ctx);
void neverc_sha3_224_update(neverc_sha3_ctx *ctx, const uint8_t *data, size_t len);
void neverc_sha3_224_final(neverc_sha3_ctx *ctx, uint8_t digest[28]);
void neverc_sha3_224_sum(const uint8_t *data, size_t len, uint8_t digest[28]);

/* SHA3-256: 32-byte digest, 136-byte rate */
void neverc_sha3_256_init(neverc_sha3_ctx *ctx);
void neverc_sha3_256_update(neverc_sha3_ctx *ctx, const uint8_t *data, size_t len);
void neverc_sha3_256_final(neverc_sha3_ctx *ctx, uint8_t digest[32]);
void neverc_sha3_256_sum(const uint8_t *data, size_t len, uint8_t digest[32]);

/* SHA3-384: 48-byte digest, 104-byte rate */
void neverc_sha3_384_init(neverc_sha3_ctx *ctx);
void neverc_sha3_384_update(neverc_sha3_ctx *ctx, const uint8_t *data, size_t len);
void neverc_sha3_384_final(neverc_sha3_ctx *ctx, uint8_t digest[48]);
void neverc_sha3_384_sum(const uint8_t *data, size_t len, uint8_t digest[48]);

/* SHA3-512: 64-byte digest, 72-byte rate */
void neverc_sha3_512_init(neverc_sha3_ctx *ctx);
void neverc_sha3_512_update(neverc_sha3_ctx *ctx, const uint8_t *data, size_t len);
void neverc_sha3_512_final(neverc_sha3_ctx *ctx, uint8_t digest[64]);
void neverc_sha3_512_sum(const uint8_t *data, size_t len, uint8_t digest[64]);

/* SHAKE128: extendable-output function, 168-byte rate */
void neverc_shake128_init(neverc_sha3_ctx *ctx);
void neverc_shake128_update(neverc_sha3_ctx *ctx, const uint8_t *data, size_t len);
void neverc_shake128_squeeze(neverc_sha3_ctx *ctx, uint8_t *out, size_t outlen);

/* SHAKE256: extendable-output function, 136-byte rate */
void neverc_shake256_init(neverc_sha3_ctx *ctx);
void neverc_shake256_update(neverc_sha3_ctx *ctx, const uint8_t *data, size_t len);
void neverc_shake256_squeeze(neverc_sha3_ctx *ctx, uint8_t *out, size_t outlen);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/crypto.h>
#endif

#endif /* NEVERC_SHA3_H */
