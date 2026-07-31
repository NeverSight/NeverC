/*===-- csupport/md5.h - MD5 hash implementation ----------------*- C -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions. See https://llvm.org/LICENSE.txt for license information. *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                   *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef CSUPPORT_MD5_H
#define CSUPPORT_MD5_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CSUPPORT_MD5_HASH_LENGTH 16
#define CSUPPORT_MD5_BLOCK_LENGTH 64

typedef uint32_t csupport_md5_u32;

typedef struct {
  csupport_md5_u32 a, b, c, d;
  csupport_md5_u32 lo, hi;
  uint8_t buffer[CSUPPORT_MD5_BLOCK_LENGTH];
  csupport_md5_u32 block[CSUPPORT_MD5_BLOCK_LENGTH / sizeof(csupport_md5_u32)];
} csupport_md5_ctx_t;

void csupport_md5_init(csupport_md5_ctx_t *ctx);
void csupport_md5_update(csupport_md5_ctx_t *ctx, const uint8_t *data,
                         size_t size);
void csupport_md5_final(csupport_md5_ctx_t *ctx,
                        uint8_t result[CSUPPORT_MD5_HASH_LENGTH]);
void csupport_md5_hash(const uint8_t *data, size_t size,
                       uint8_t result[CSUPPORT_MD5_HASH_LENGTH]);

#ifdef __cplusplus
}
#endif

#endif /* CSUPPORT_MD5_H */
