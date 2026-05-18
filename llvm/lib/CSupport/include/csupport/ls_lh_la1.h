#ifndef CSUPPORT_LS_LH_LA1_H
#define CSUPPORT_LS_LH_LA1_H
#include "csupport/types.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define CSUPPORT_SHA1_HASH_LENGTH 20
#define CSUPPORT_SHA1_BLOCK_LENGTH 64

typedef struct {
  uint32_t state[5];
  union {
    uint8_t c[CSUPPORT_SHA1_BLOCK_LENGTH];
    uint32_t l[CSUPPORT_SHA1_BLOCK_LENGTH / 4];
  } buffer;
  unsigned buffer_offset;
  uint64_t byte_count;
} csupport_sha1_ctx_t;

void csupport_sha1_init(csupport_sha1_ctx_t *ctx);
void csupport_sha1_update(csupport_sha1_ctx_t *ctx, const uint8_t *data,
                          size_t len);
void csupport_sha1_update_string(csupport_sha1_ctx_t *ctx, const char *str,
                                 size_t len);
void csupport_sha1_final(csupport_sha1_ctx_t *ctx, uint8_t result[20]);
void csupport_sha1_result(csupport_sha1_ctx_t *ctx, uint8_t result[20]);
void csupport_sha1_hash(const uint8_t *data, size_t len, uint8_t result[20]);

#ifdef __cplusplus
}
#endif
#endif
