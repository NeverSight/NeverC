/*===- SHA1.c - SHA-1 implementation ----------------------------*- C -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions. See https://llvm.org/LICENSE.txt for license information.       *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                   *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/
#include "include/csupport/ls_lh_la1.h"
#include <string.h>
#include <assert.h>

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define CSUPPORT_BIG_ENDIAN 1
#else
#define CSUPPORT_BIG_ENDIAN 0
#endif

static inline uint32_t csupport_byteswap32(uint32_t v) {
  return ((v >> 24) & 0xFF) | ((v >> 8) & 0xFF00) |
         ((v << 8) & 0xFF0000) | ((v << 24) & 0xFF000000u);
}

static inline uint32_t csupport_read32be(const uint8_t *p) {
  uint32_t v;
  memcpy(&v, p, 4);
#if !CSUPPORT_BIG_ENDIAN
  v = csupport_byteswap32(v);
#endif
  return v;
}

static inline uint32_t sha1_rol(uint32_t n, int bits) {
  return (n << bits) | (n >> (32 - bits));
}

static inline uint32_t sha1_blk0(uint32_t *buf, int i) { return buf[i]; }

static inline uint32_t sha1_blk(uint32_t *buf, int i) {
  buf[i & 15] = sha1_rol(buf[(i + 13) & 15] ^ buf[(i + 8) & 15] ^
                              buf[(i + 2) & 15] ^ buf[i & 15],
                          1);
  return buf[i & 15];
}

#define SHA1_R0(a, b, c, d, e, i, buf)                                         \
  do {                                                                         \
    (e) += (((b) & ((c) ^ (d))) ^ (d)) + sha1_blk0(buf, i) + 0x5A827999 +     \
           sha1_rol((a), 5);                                                   \
    (b) = sha1_rol((b), 30);                                                   \
  } while (0)

#define SHA1_R1(a, b, c, d, e, i, buf)                                         \
  do {                                                                         \
    (e) += (((b) & ((c) ^ (d))) ^ (d)) + sha1_blk(buf, i) + 0x5A827999 +      \
           sha1_rol((a), 5);                                                   \
    (b) = sha1_rol((b), 30);                                                   \
  } while (0)

#define SHA1_R2(a, b, c, d, e, i, buf)                                         \
  do {                                                                         \
    (e) += ((b) ^ (c) ^ (d)) + sha1_blk(buf, i) + 0x6ED9EBA1 +                \
           sha1_rol((a), 5);                                                   \
    (b) = sha1_rol((b), 30);                                                   \
  } while (0)

#define SHA1_R3(a, b, c, d, e, i, buf)                                         \
  do {                                                                         \
    (e) += ((((b) | (c)) & (d)) | ((b) & (c))) + sha1_blk(buf, i) +           \
           0x8F1BBCDC + sha1_rol((a), 5);                                      \
    (b) = sha1_rol((b), 30);                                                   \
  } while (0)

#define SHA1_R4(a, b, c, d, e, i, buf)                                         \
  do {                                                                         \
    (e) += ((b) ^ (c) ^ (d)) + sha1_blk(buf, i) + 0xCA62C1D6 +                \
           sha1_rol((a), 5);                                                   \
    (b) = sha1_rol((b), 30);                                                   \
  } while (0)

void csupport_sha1_init(csupport_sha1_ctx_t *ctx) {
  ctx->state[0] = 0x67452301;
  ctx->state[1] = 0xEFCDAB89;
  ctx->state[2] = 0x98BADCFE;
  ctx->state[3] = 0x10325476;
  ctx->state[4] = 0xC3D2E1F0;
  ctx->byte_count = 0;
  ctx->buffer_offset = 0;
}

static void sha1_hash_block(csupport_sha1_ctx_t *ctx) {
  uint32_t a = ctx->state[0];
  uint32_t b = ctx->state[1];
  uint32_t c = ctx->state[2];
  uint32_t d = ctx->state[3];
  uint32_t e = ctx->state[4];

  SHA1_R0(a, b, c, d, e, 0, ctx->buffer.l);
  SHA1_R0(e, a, b, c, d, 1, ctx->buffer.l);
  SHA1_R0(d, e, a, b, c, 2, ctx->buffer.l);
  SHA1_R0(c, d, e, a, b, 3, ctx->buffer.l);
  SHA1_R0(b, c, d, e, a, 4, ctx->buffer.l);
  SHA1_R0(a, b, c, d, e, 5, ctx->buffer.l);
  SHA1_R0(e, a, b, c, d, 6, ctx->buffer.l);
  SHA1_R0(d, e, a, b, c, 7, ctx->buffer.l);
  SHA1_R0(c, d, e, a, b, 8, ctx->buffer.l);
  SHA1_R0(b, c, d, e, a, 9, ctx->buffer.l);
  SHA1_R0(a, b, c, d, e, 10, ctx->buffer.l);
  SHA1_R0(e, a, b, c, d, 11, ctx->buffer.l);
  SHA1_R0(d, e, a, b, c, 12, ctx->buffer.l);
  SHA1_R0(c, d, e, a, b, 13, ctx->buffer.l);
  SHA1_R0(b, c, d, e, a, 14, ctx->buffer.l);
  SHA1_R0(a, b, c, d, e, 15, ctx->buffer.l);
  SHA1_R1(e, a, b, c, d, 16, ctx->buffer.l);
  SHA1_R1(d, e, a, b, c, 17, ctx->buffer.l);
  SHA1_R1(c, d, e, a, b, 18, ctx->buffer.l);
  SHA1_R1(b, c, d, e, a, 19, ctx->buffer.l);

  SHA1_R2(a, b, c, d, e, 20, ctx->buffer.l);
  SHA1_R2(e, a, b, c, d, 21, ctx->buffer.l);
  SHA1_R2(d, e, a, b, c, 22, ctx->buffer.l);
  SHA1_R2(c, d, e, a, b, 23, ctx->buffer.l);
  SHA1_R2(b, c, d, e, a, 24, ctx->buffer.l);
  SHA1_R2(a, b, c, d, e, 25, ctx->buffer.l);
  SHA1_R2(e, a, b, c, d, 26, ctx->buffer.l);
  SHA1_R2(d, e, a, b, c, 27, ctx->buffer.l);
  SHA1_R2(c, d, e, a, b, 28, ctx->buffer.l);
  SHA1_R2(b, c, d, e, a, 29, ctx->buffer.l);
  SHA1_R2(a, b, c, d, e, 30, ctx->buffer.l);
  SHA1_R2(e, a, b, c, d, 31, ctx->buffer.l);
  SHA1_R2(d, e, a, b, c, 32, ctx->buffer.l);
  SHA1_R2(c, d, e, a, b, 33, ctx->buffer.l);
  SHA1_R2(b, c, d, e, a, 34, ctx->buffer.l);
  SHA1_R2(a, b, c, d, e, 35, ctx->buffer.l);
  SHA1_R2(e, a, b, c, d, 36, ctx->buffer.l);
  SHA1_R2(d, e, a, b, c, 37, ctx->buffer.l);
  SHA1_R2(c, d, e, a, b, 38, ctx->buffer.l);
  SHA1_R2(b, c, d, e, a, 39, ctx->buffer.l);

  SHA1_R3(a, b, c, d, e, 40, ctx->buffer.l);
  SHA1_R3(e, a, b, c, d, 41, ctx->buffer.l);
  SHA1_R3(d, e, a, b, c, 42, ctx->buffer.l);
  SHA1_R3(c, d, e, a, b, 43, ctx->buffer.l);
  SHA1_R3(b, c, d, e, a, 44, ctx->buffer.l);
  SHA1_R3(a, b, c, d, e, 45, ctx->buffer.l);
  SHA1_R3(e, a, b, c, d, 46, ctx->buffer.l);
  SHA1_R3(d, e, a, b, c, 47, ctx->buffer.l);
  SHA1_R3(c, d, e, a, b, 48, ctx->buffer.l);
  SHA1_R3(b, c, d, e, a, 49, ctx->buffer.l);
  SHA1_R3(a, b, c, d, e, 50, ctx->buffer.l);
  SHA1_R3(e, a, b, c, d, 51, ctx->buffer.l);
  SHA1_R3(d, e, a, b, c, 52, ctx->buffer.l);
  SHA1_R3(c, d, e, a, b, 53, ctx->buffer.l);
  SHA1_R3(b, c, d, e, a, 54, ctx->buffer.l);
  SHA1_R3(a, b, c, d, e, 55, ctx->buffer.l);
  SHA1_R3(e, a, b, c, d, 56, ctx->buffer.l);
  SHA1_R3(d, e, a, b, c, 57, ctx->buffer.l);
  SHA1_R3(c, d, e, a, b, 58, ctx->buffer.l);
  SHA1_R3(b, c, d, e, a, 59, ctx->buffer.l);

  SHA1_R4(a, b, c, d, e, 60, ctx->buffer.l);
  SHA1_R4(e, a, b, c, d, 61, ctx->buffer.l);
  SHA1_R4(d, e, a, b, c, 62, ctx->buffer.l);
  SHA1_R4(c, d, e, a, b, 63, ctx->buffer.l);
  SHA1_R4(b, c, d, e, a, 64, ctx->buffer.l);
  SHA1_R4(a, b, c, d, e, 65, ctx->buffer.l);
  SHA1_R4(e, a, b, c, d, 66, ctx->buffer.l);
  SHA1_R4(d, e, a, b, c, 67, ctx->buffer.l);
  SHA1_R4(c, d, e, a, b, 68, ctx->buffer.l);
  SHA1_R4(b, c, d, e, a, 69, ctx->buffer.l);
  SHA1_R4(a, b, c, d, e, 70, ctx->buffer.l);
  SHA1_R4(e, a, b, c, d, 71, ctx->buffer.l);
  SHA1_R4(d, e, a, b, c, 72, ctx->buffer.l);
  SHA1_R4(c, d, e, a, b, 73, ctx->buffer.l);
  SHA1_R4(b, c, d, e, a, 74, ctx->buffer.l);
  SHA1_R4(a, b, c, d, e, 75, ctx->buffer.l);
  SHA1_R4(e, a, b, c, d, 76, ctx->buffer.l);
  SHA1_R4(d, e, a, b, c, 77, ctx->buffer.l);
  SHA1_R4(c, d, e, a, b, 78, ctx->buffer.l);
  SHA1_R4(b, c, d, e, a, 79, ctx->buffer.l);

  ctx->state[0] += a;
  ctx->state[1] += b;
  ctx->state[2] += c;
  ctx->state[3] += d;
  ctx->state[4] += e;
}

static void sha1_add_uncounted(csupport_sha1_ctx_t *ctx, uint8_t data) {
#if CSUPPORT_BIG_ENDIAN
  ctx->buffer.c[ctx->buffer_offset] = data;
#else
  ctx->buffer.c[ctx->buffer_offset ^ 3] = data;
#endif
  ctx->buffer_offset++;
  if (ctx->buffer_offset == CSUPPORT_SHA1_BLOCK_LENGTH) {
    sha1_hash_block(ctx);
    ctx->buffer_offset = 0;
  }
}

void csupport_sha1_update(csupport_sha1_ctx_t *ctx, const uint8_t *data,
                           size_t len) {
  ctx->byte_count += len;

  if (ctx->buffer_offset > 0) {
    size_t remainder = CSUPPORT_SHA1_BLOCK_LENGTH - ctx->buffer_offset;
    if (remainder > len)
      remainder = len;
    for (size_t i = 0; i < remainder; i++)
      sha1_add_uncounted(ctx, data[i]);
    data += remainder;
    len -= remainder;
  }

  while (len >= CSUPPORT_SHA1_BLOCK_LENGTH) {
    assert(ctx->buffer_offset == 0);
    for (size_t i = 0; i < CSUPPORT_SHA1_BLOCK_LENGTH / 4; i++)
      ctx->buffer.l[i] = csupport_read32be(&data[i * 4]);
    sha1_hash_block(ctx);
    data += CSUPPORT_SHA1_BLOCK_LENGTH;
    len -= CSUPPORT_SHA1_BLOCK_LENGTH;
  }

  for (size_t i = 0; i < len; i++)
    sha1_add_uncounted(ctx, data[i]);
}

void csupport_sha1_update_string(csupport_sha1_ctx_t *ctx, const char *str,
                                  size_t len) {
  csupport_sha1_update(ctx, (const uint8_t *)str, len);
}

static void sha1_pad(csupport_sha1_ctx_t *ctx) {
  sha1_add_uncounted(ctx, 0x80);
  while (ctx->buffer_offset != 56)
    sha1_add_uncounted(ctx, 0x00);

  sha1_add_uncounted(ctx, 0);
  sha1_add_uncounted(ctx, 0);
  sha1_add_uncounted(ctx, 0);
  sha1_add_uncounted(ctx, (uint8_t)(ctx->byte_count >> 29));
  sha1_add_uncounted(ctx, (uint8_t)(ctx->byte_count >> 21));
  sha1_add_uncounted(ctx, (uint8_t)(ctx->byte_count >> 13));
  sha1_add_uncounted(ctx, (uint8_t)(ctx->byte_count >> 5));
  sha1_add_uncounted(ctx, (uint8_t)(ctx->byte_count << 3));
}

void csupport_sha1_final(csupport_sha1_ctx_t *ctx, uint8_t result[20]) {
  sha1_pad(ctx);
  for (int i = 0; i < 5; i++) {
    uint32_t v;
#if CSUPPORT_BIG_ENDIAN
    v = ctx->state[i];
#else
    v = csupport_byteswap32(ctx->state[i]);
#endif
    memcpy(result + i * 4, &v, 4);
  }
}

void csupport_sha1_result(csupport_sha1_ctx_t *ctx, uint8_t result[20]) {
  csupport_sha1_ctx_t saved = *ctx;
  csupport_sha1_final(ctx, result);
  *ctx = saved;
}

void csupport_sha1_hash(const uint8_t *data, size_t len, uint8_t result[20]) {
  csupport_sha1_ctx_t ctx;
  csupport_sha1_init(&ctx);
  csupport_sha1_update(&ctx, data, len);
  csupport_sha1_final(&ctx, result);
}
