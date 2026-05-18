#ifndef CSUPPORT_LS_LH_LA256_H
#define CSUPPORT_LS_LH_LA256_H
#include "csupport/types.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define CSUPPORT_SHA256_HASH_LENGTH 32
#define CSUPPORT_SHA256_BLOCK_LENGTH 64

typedef struct {
  uint32_t state[8];
  union {
    uint8_t c[CSUPPORT_SHA256_BLOCK_LENGTH];
    uint32_t l[CSUPPORT_SHA256_BLOCK_LENGTH / 4];
  } buffer;
  unsigned buffer_offset;
  uint64_t byte_count;
} csupport_sha256_ctx_t;

void csupport_sha256_init(csupport_sha256_ctx_t *ctx);
void csupport_sha256_update(csupport_sha256_ctx_t *ctx, const uint8_t *data,
                            size_t len);
void csupport_sha256_update_string(csupport_sha256_ctx_t *ctx, const char *str,
                                   size_t len);
void csupport_sha256_final(csupport_sha256_ctx_t *ctx, uint8_t result[32]);
void csupport_sha256_result(csupport_sha256_ctx_t *ctx, uint8_t result[32]);
void csupport_sha256_hash(const uint8_t *data, size_t len, uint8_t result[32]);

#ifdef __cplusplus
}
#endif
#endif
