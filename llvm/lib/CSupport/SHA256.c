/*===- SHA256.c - SHA-256 implementation -------------------------*- C -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions. See https://llvm.org/LICENSE.txt for license information.       *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                   *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/
#include "include/csupport/ls_lh_la256.h"
#include <string.h>
#include <assert.h>

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define CSUPPORT_BIG_ENDIAN_256 1
#else
#define CSUPPORT_BIG_ENDIAN_256 0
#endif

static inline uint32_t sha256_byteswap32(uint32_t v) {
  return ((v >> 24) & 0xFF) | ((v >> 8) & 0xFF00) |
         ((v << 8) & 0xFF0000) | ((v << 24) & 0xFF000000u);
}

static inline uint32_t sha256_read32be(const uint8_t *p) {
  uint32_t v;
  memcpy(&v, p, 4);
#if !CSUPPORT_BIG_ENDIAN_256
  v = sha256_byteswap32(v);
#endif
  return v;
}

#define SHA256_SHR(x, c) ((x) >> (c))
#define SHA256_ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

#define SHA256_CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define SHA256_MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))

#define SHA256_SIGMA_0(x) (SHA256_ROTR(x, 2) ^ SHA256_ROTR(x, 13) ^ SHA256_ROTR(x, 22))
#define SHA256_SIGMA_1(x) (SHA256_ROTR(x, 6) ^ SHA256_ROTR(x, 11) ^ SHA256_ROTR(x, 25))
#define SHA256_SIGMA_2(x) (SHA256_ROTR(x, 17) ^ SHA256_ROTR(x, 19) ^ SHA256_SHR(x, 10))
#define SHA256_SIGMA_3(x) (SHA256_ROTR(x, 7) ^ SHA256_ROTR(x, 18) ^ SHA256_SHR(x, 3))

#define SHA256_F_EXPAND(A, B, C, D, E, F, G, H, M1, M2, M3, M4, k)           \
  do {                                                                         \
    (H) += SHA256_SIGMA_1(E) + SHA256_CH(E, F, G) + (M1) + (k);               \
    (D) += (H);                                                                \
    (H) += SHA256_SIGMA_0(A) + SHA256_MAJ(A, B, C);                            \
    (M1) += SHA256_SIGMA_2(M2) + (M3) + SHA256_SIGMA_3(M4);                    \
  } while (0)

void csupport_sha256_init(csupport_sha256_ctx_t *ctx) {
  ctx->state[0] = 0x6A09E667;
  ctx->state[1] = 0xBB67AE85;
  ctx->state[2] = 0x3C6EF372;
  ctx->state[3] = 0xA54FF53A;
  ctx->state[4] = 0x510E527F;
  ctx->state[5] = 0x9B05688C;
  ctx->state[6] = 0x1F83D9AB;
  ctx->state[7] = 0x5BE0CD19;
  ctx->byte_count = 0;
  ctx->buffer_offset = 0;
}

static void sha256_hash_block(csupport_sha256_ctx_t *ctx) {
  uint32_t a = ctx->state[0], b = ctx->state[1];
  uint32_t c = ctx->state[2], d = ctx->state[3];
  uint32_t e = ctx->state[4], f = ctx->state[5];
  uint32_t g = ctx->state[6], h = ctx->state[7];

  uint32_t w00 = ctx->buffer.l[0],  w01 = ctx->buffer.l[1];
  uint32_t w02 = ctx->buffer.l[2],  w03 = ctx->buffer.l[3];
  uint32_t w04 = ctx->buffer.l[4],  w05 = ctx->buffer.l[5];
  uint32_t w06 = ctx->buffer.l[6],  w07 = ctx->buffer.l[7];
  uint32_t w08 = ctx->buffer.l[8],  w09 = ctx->buffer.l[9];
  uint32_t w10 = ctx->buffer.l[10], w11 = ctx->buffer.l[11];
  uint32_t w12 = ctx->buffer.l[12], w13 = ctx->buffer.l[13];
  uint32_t w14 = ctx->buffer.l[14], w15 = ctx->buffer.l[15];

  SHA256_F_EXPAND(a, b, c, d, e, f, g, h, w00, w14, w09, w01, 0x428A2F98);
  SHA256_F_EXPAND(h, a, b, c, d, e, f, g, w01, w15, w10, w02, 0x71374491);
  SHA256_F_EXPAND(g, h, a, b, c, d, e, f, w02, w00, w11, w03, 0xB5C0FBCF);
  SHA256_F_EXPAND(f, g, h, a, b, c, d, e, w03, w01, w12, w04, 0xE9B5DBA5);
  SHA256_F_EXPAND(e, f, g, h, a, b, c, d, w04, w02, w13, w05, 0x3956C25B);
  SHA256_F_EXPAND(d, e, f, g, h, a, b, c, w05, w03, w14, w06, 0x59F111F1);
  SHA256_F_EXPAND(c, d, e, f, g, h, a, b, w06, w04, w15, w07, 0x923F82A4);
  SHA256_F_EXPAND(b, c, d, e, f, g, h, a, w07, w05, w00, w08, 0xAB1C5ED5);
  SHA256_F_EXPAND(a, b, c, d, e, f, g, h, w08, w06, w01, w09, 0xD807AA98);
  SHA256_F_EXPAND(h, a, b, c, d, e, f, g, w09, w07, w02, w10, 0x12835B01);
  SHA256_F_EXPAND(g, h, a, b, c, d, e, f, w10, w08, w03, w11, 0x243185BE);
  SHA256_F_EXPAND(f, g, h, a, b, c, d, e, w11, w09, w04, w12, 0x550C7DC3);
  SHA256_F_EXPAND(e, f, g, h, a, b, c, d, w12, w10, w05, w13, 0x72BE5D74);
  SHA256_F_EXPAND(d, e, f, g, h, a, b, c, w13, w11, w06, w14, 0x80DEB1FE);
  SHA256_F_EXPAND(c, d, e, f, g, h, a, b, w14, w12, w07, w15, 0x9BDC06A7);
  SHA256_F_EXPAND(b, c, d, e, f, g, h, a, w15, w13, w08, w00, 0xC19BF174);

  SHA256_F_EXPAND(a, b, c, d, e, f, g, h, w00, w14, w09, w01, 0xE49B69C1);
  SHA256_F_EXPAND(h, a, b, c, d, e, f, g, w01, w15, w10, w02, 0xEFBE4786);
  SHA256_F_EXPAND(g, h, a, b, c, d, e, f, w02, w00, w11, w03, 0x0FC19DC6);
  SHA256_F_EXPAND(f, g, h, a, b, c, d, e, w03, w01, w12, w04, 0x240CA1CC);
  SHA256_F_EXPAND(e, f, g, h, a, b, c, d, w04, w02, w13, w05, 0x2DE92C6F);
  SHA256_F_EXPAND(d, e, f, g, h, a, b, c, w05, w03, w14, w06, 0x4A7484AA);
  SHA256_F_EXPAND(c, d, e, f, g, h, a, b, w06, w04, w15, w07, 0x5CB0A9DC);
  SHA256_F_EXPAND(b, c, d, e, f, g, h, a, w07, w05, w00, w08, 0x76F988DA);
  SHA256_F_EXPAND(a, b, c, d, e, f, g, h, w08, w06, w01, w09, 0x983E5152);
  SHA256_F_EXPAND(h, a, b, c, d, e, f, g, w09, w07, w02, w10, 0xA831C66D);
  SHA256_F_EXPAND(g, h, a, b, c, d, e, f, w10, w08, w03, w11, 0xB00327C8);
  SHA256_F_EXPAND(f, g, h, a, b, c, d, e, w11, w09, w04, w12, 0xBF597FC7);
  SHA256_F_EXPAND(e, f, g, h, a, b, c, d, w12, w10, w05, w13, 0xC6E00BF3);
  SHA256_F_EXPAND(d, e, f, g, h, a, b, c, w13, w11, w06, w14, 0xD5A79147);
  SHA256_F_EXPAND(c, d, e, f, g, h, a, b, w14, w12, w07, w15, 0x06CA6351);
  SHA256_F_EXPAND(b, c, d, e, f, g, h, a, w15, w13, w08, w00, 0x14292967);

  SHA256_F_EXPAND(a, b, c, d, e, f, g, h, w00, w14, w09, w01, 0x27B70A85);
  SHA256_F_EXPAND(h, a, b, c, d, e, f, g, w01, w15, w10, w02, 0x2E1B2138);
  SHA256_F_EXPAND(g, h, a, b, c, d, e, f, w02, w00, w11, w03, 0x4D2C6DFC);
  SHA256_F_EXPAND(f, g, h, a, b, c, d, e, w03, w01, w12, w04, 0x53380D13);
  SHA256_F_EXPAND(e, f, g, h, a, b, c, d, w04, w02, w13, w05, 0x650A7354);
  SHA256_F_EXPAND(d, e, f, g, h, a, b, c, w05, w03, w14, w06, 0x766A0ABB);
  SHA256_F_EXPAND(c, d, e, f, g, h, a, b, w06, w04, w15, w07, 0x81C2C92E);
  SHA256_F_EXPAND(b, c, d, e, f, g, h, a, w07, w05, w00, w08, 0x92722C85);
  SHA256_F_EXPAND(a, b, c, d, e, f, g, h, w08, w06, w01, w09, 0xA2BFE8A1);
  SHA256_F_EXPAND(h, a, b, c, d, e, f, g, w09, w07, w02, w10, 0xA81A664B);
  SHA256_F_EXPAND(g, h, a, b, c, d, e, f, w10, w08, w03, w11, 0xC24B8B70);
  SHA256_F_EXPAND(f, g, h, a, b, c, d, e, w11, w09, w04, w12, 0xC76C51A3);
  SHA256_F_EXPAND(e, f, g, h, a, b, c, d, w12, w10, w05, w13, 0xD192E819);
  SHA256_F_EXPAND(d, e, f, g, h, a, b, c, w13, w11, w06, w14, 0xD6990624);
  SHA256_F_EXPAND(c, d, e, f, g, h, a, b, w14, w12, w07, w15, 0xF40E3585);
  SHA256_F_EXPAND(b, c, d, e, f, g, h, a, w15, w13, w08, w00, 0x106AA070);

  SHA256_F_EXPAND(a, b, c, d, e, f, g, h, w00, w14, w09, w01, 0x19A4C116);
  SHA256_F_EXPAND(h, a, b, c, d, e, f, g, w01, w15, w10, w02, 0x1E376C08);
  SHA256_F_EXPAND(g, h, a, b, c, d, e, f, w02, w00, w11, w03, 0x2748774C);
  SHA256_F_EXPAND(f, g, h, a, b, c, d, e, w03, w01, w12, w04, 0x34B0BCB5);
  SHA256_F_EXPAND(e, f, g, h, a, b, c, d, w04, w02, w13, w05, 0x391C0CB3);
  SHA256_F_EXPAND(d, e, f, g, h, a, b, c, w05, w03, w14, w06, 0x4ED8AA4A);
  SHA256_F_EXPAND(c, d, e, f, g, h, a, b, w06, w04, w15, w07, 0x5B9CCA4F);
  SHA256_F_EXPAND(b, c, d, e, f, g, h, a, w07, w05, w00, w08, 0x682E6FF3);
  SHA256_F_EXPAND(a, b, c, d, e, f, g, h, w08, w06, w01, w09, 0x748F82EE);
  SHA256_F_EXPAND(h, a, b, c, d, e, f, g, w09, w07, w02, w10, 0x78A5636F);
  SHA256_F_EXPAND(g, h, a, b, c, d, e, f, w10, w08, w03, w11, 0x84C87814);
  SHA256_F_EXPAND(f, g, h, a, b, c, d, e, w11, w09, w04, w12, 0x8CC70208);
  SHA256_F_EXPAND(e, f, g, h, a, b, c, d, w12, w10, w05, w13, 0x90BEFFFA);
  SHA256_F_EXPAND(d, e, f, g, h, a, b, c, w13, w11, w06, w14, 0xA4506CEB);
  SHA256_F_EXPAND(c, d, e, f, g, h, a, b, w14, w12, w07, w15, 0xBEF9A3F7);
  SHA256_F_EXPAND(b, c, d, e, f, g, h, a, w15, w13, w08, w00, 0xC67178F2);

  ctx->state[0] += a; ctx->state[1] += b;
  ctx->state[2] += c; ctx->state[3] += d;
  ctx->state[4] += e; ctx->state[5] += f;
  ctx->state[6] += g; ctx->state[7] += h;
}

static void sha256_add_uncounted(csupport_sha256_ctx_t *ctx, uint8_t data) {
#if CSUPPORT_BIG_ENDIAN_256
  ctx->buffer.c[ctx->buffer_offset] = data;
#else
  ctx->buffer.c[ctx->buffer_offset ^ 3] = data;
#endif
  ctx->buffer_offset++;
  if (ctx->buffer_offset == CSUPPORT_SHA256_BLOCK_LENGTH) {
    sha256_hash_block(ctx);
    ctx->buffer_offset = 0;
  }
}

void csupport_sha256_update(csupport_sha256_ctx_t *ctx, const uint8_t *data,
                             size_t len) {
  ctx->byte_count += len;

  if (ctx->buffer_offset > 0) {
    size_t remainder = CSUPPORT_SHA256_BLOCK_LENGTH - ctx->buffer_offset;
    if (remainder > len)
      remainder = len;
    for (size_t i = 0; i < remainder; i++)
      sha256_add_uncounted(ctx, data[i]);
    data += remainder;
    len -= remainder;
  }

  while (len >= CSUPPORT_SHA256_BLOCK_LENGTH) {
    assert(ctx->buffer_offset == 0);
    for (size_t i = 0; i < CSUPPORT_SHA256_BLOCK_LENGTH / 4; i++)
      ctx->buffer.l[i] = sha256_read32be(&data[i * 4]);
    sha256_hash_block(ctx);
    data += CSUPPORT_SHA256_BLOCK_LENGTH;
    len -= CSUPPORT_SHA256_BLOCK_LENGTH;
  }

  for (size_t i = 0; i < len; i++)
    sha256_add_uncounted(ctx, data[i]);
}

void csupport_sha256_update_string(csupport_sha256_ctx_t *ctx, const char *str,
                                    size_t len) {
  csupport_sha256_update(ctx, (const uint8_t *)str, len);
}

static void sha256_pad(csupport_sha256_ctx_t *ctx) {
  sha256_add_uncounted(ctx, 0x80);
  while (ctx->buffer_offset != 56)
    sha256_add_uncounted(ctx, 0x00);

  uint64_t bit_len = ctx->byte_count << 3;
  sha256_add_uncounted(ctx, (uint8_t)(bit_len >> 56));
  sha256_add_uncounted(ctx, (uint8_t)(bit_len >> 48));
  sha256_add_uncounted(ctx, (uint8_t)(bit_len >> 40));
  sha256_add_uncounted(ctx, (uint8_t)(bit_len >> 32));
  sha256_add_uncounted(ctx, (uint8_t)(bit_len >> 24));
  sha256_add_uncounted(ctx, (uint8_t)(bit_len >> 16));
  sha256_add_uncounted(ctx, (uint8_t)(bit_len >> 8));
  sha256_add_uncounted(ctx, (uint8_t)(bit_len));
}

void csupport_sha256_final(csupport_sha256_ctx_t *ctx, uint8_t result[32]) {
  sha256_pad(ctx);
  for (int i = 0; i < 8; i++) {
    uint32_t v;
#if CSUPPORT_BIG_ENDIAN_256
    v = ctx->state[i];
#else
    v = sha256_byteswap32(ctx->state[i]);
#endif
    memcpy(result + i * 4, &v, 4);
  }
}

void csupport_sha256_result(csupport_sha256_ctx_t *ctx, uint8_t result[32]) {
  csupport_sha256_ctx_t saved = *ctx;
  csupport_sha256_final(ctx, result);
  *ctx = saved;
}

void csupport_sha256_hash(const uint8_t *data, size_t len,
                           uint8_t result[32]) {
  csupport_sha256_ctx_t ctx;
  csupport_sha256_init(&ctx);
  csupport_sha256_update(&ctx, data, len);
  csupport_sha256_final(&ctx, result);
}
