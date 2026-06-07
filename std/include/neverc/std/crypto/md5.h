#ifndef NEVERC_MD5_H
#define NEVERC_MD5_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_MD5_DIGEST_SIZE 16
#define NEVERC_MD5_BLOCK_SIZE  64

typedef struct {
    uint32_t state[4];
    uint64_t count;
    uint8_t  buf[64];
} neverc_md5_ctx;

void neverc_md5_init(neverc_md5_ctx *ctx);
void neverc_md5_update(neverc_md5_ctx *ctx, const uint8_t *data, size_t len);
void neverc_md5_final(neverc_md5_ctx *ctx, uint8_t digest[16]);

/* One-shot: compute MD5 of data into digest */
void neverc_md5_sum(const uint8_t *data, size_t len, uint8_t digest[16]);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/crypto.h>
#endif

#endif /* NEVERC_MD5_H */
