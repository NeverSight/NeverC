/*
 * MD5 implementation in C. Based on the public domain implementation by
 * Alexander Peslyak (Solar Designer).
 */

#include "include/csupport/md5.h"
#include "include/csupport/endian.h"
#include <string.h>

#define F(x, y, z) ((z) ^ ((x) & ((y) ^ (z))))
#define G(x, y, z) ((y) ^ ((z) & ((x) ^ (y))))
#define H(x, y, z) ((x) ^ (y) ^ (z))
#define I(x, y, z) ((y) ^ ((x) | ~(z)))

#define STEP(f, a, b, c, d, x, t, s)                                           \
  (a) += f((b), (c), (d)) + (x) + (t);                                         \
  (a) = (((a) << (s)) | (((a) & 0xffffffff) >> (32 - (s))));                   \
  (a) += (b);

#define SET(n)                                                                 \
  (ctx->block[(n)] = (csupport_md5_u32)ptr[(n) * 4] |                         \
                     ((csupport_md5_u32)ptr[(n) * 4 + 1] << 8) |              \
                     ((csupport_md5_u32)ptr[(n) * 4 + 2] << 16) |             \
                     ((csupport_md5_u32)ptr[(n) * 4 + 3] << 24))
#define GET(n) (ctx->block[(n)])

static const uint8_t *md5_body(csupport_md5_ctx_t *ctx, const uint8_t *data,
                               size_t size) {
  const uint8_t *ptr = data;
  csupport_md5_u32 a, b, c, d;
  csupport_md5_u32 saved_a, saved_b, saved_c, saved_d;

  a = ctx->a;
  b = ctx->b;
  c = ctx->c;
  d = ctx->d;

  do {
    saved_a = a; saved_b = b; saved_c = c; saved_d = d;

    STEP(F, a, b, c, d, SET(0), 0xd76aa478, 7)
    STEP(F, d, a, b, c, SET(1), 0xe8c7b756, 12)
    STEP(F, c, d, a, b, SET(2), 0x242070db, 17)
    STEP(F, b, c, d, a, SET(3), 0xc1bdceee, 22)
    STEP(F, a, b, c, d, SET(4), 0xf57c0faf, 7)
    STEP(F, d, a, b, c, SET(5), 0x4787c62a, 12)
    STEP(F, c, d, a, b, SET(6), 0xa8304613, 17)
    STEP(F, b, c, d, a, SET(7), 0xfd469501, 22)
    STEP(F, a, b, c, d, SET(8), 0x698098d8, 7)
    STEP(F, d, a, b, c, SET(9), 0x8b44f7af, 12)
    STEP(F, c, d, a, b, SET(10), 0xffff5bb1, 17)
    STEP(F, b, c, d, a, SET(11), 0x895cd7be, 22)
    STEP(F, a, b, c, d, SET(12), 0x6b901122, 7)
    STEP(F, d, a, b, c, SET(13), 0xfd987193, 12)
    STEP(F, c, d, a, b, SET(14), 0xa679438e, 17)
    STEP(F, b, c, d, a, SET(15), 0x49b40821, 22)

    STEP(G, a, b, c, d, GET(1), 0xf61e2562, 5)
    STEP(G, d, a, b, c, GET(6), 0xc040b340, 9)
    STEP(G, c, d, a, b, GET(11), 0x265e5a51, 14)
    STEP(G, b, c, d, a, GET(0), 0xe9b6c7aa, 20)
    STEP(G, a, b, c, d, GET(5), 0xd62f105d, 5)
    STEP(G, d, a, b, c, GET(10), 0x02441453, 9)
    STEP(G, c, d, a, b, GET(15), 0xd8a1e681, 14)
    STEP(G, b, c, d, a, GET(4), 0xe7d3fbc8, 20)
    STEP(G, a, b, c, d, GET(9), 0x21e1cde6, 5)
    STEP(G, d, a, b, c, GET(14), 0xc33707d6, 9)
    STEP(G, c, d, a, b, GET(3), 0xf4d50d87, 14)
    STEP(G, b, c, d, a, GET(8), 0x455a14ed, 20)
    STEP(G, a, b, c, d, GET(13), 0xa9e3e905, 5)
    STEP(G, d, a, b, c, GET(2), 0xfcefa3f8, 9)
    STEP(G, c, d, a, b, GET(7), 0x676f02d9, 14)
    STEP(G, b, c, d, a, GET(12), 0x8d2a4c8a, 20)

    STEP(H, a, b, c, d, GET(5), 0xfffa3942, 4)
    STEP(H, d, a, b, c, GET(8), 0x8771f681, 11)
    STEP(H, c, d, a, b, GET(11), 0x6d9d6122, 16)
    STEP(H, b, c, d, a, GET(14), 0xfde5380c, 23)
    STEP(H, a, b, c, d, GET(1), 0xa4beea44, 4)
    STEP(H, d, a, b, c, GET(4), 0x4bdecfa9, 11)
    STEP(H, c, d, a, b, GET(7), 0xf6bb4b60, 16)
    STEP(H, b, c, d, a, GET(10), 0xbebfbc70, 23)
    STEP(H, a, b, c, d, GET(13), 0x289b7ec6, 4)
    STEP(H, d, a, b, c, GET(0), 0xeaa127fa, 11)
    STEP(H, c, d, a, b, GET(3), 0xd4ef3085, 16)
    STEP(H, b, c, d, a, GET(6), 0x04881d05, 23)
    STEP(H, a, b, c, d, GET(9), 0xd9d4d039, 4)
    STEP(H, d, a, b, c, GET(12), 0xe6db99e5, 11)
    STEP(H, c, d, a, b, GET(15), 0x1fa27cf8, 16)
    STEP(H, b, c, d, a, GET(2), 0xc4ac5665, 23)

    STEP(I, a, b, c, d, GET(0), 0xf4292244, 6)
    STEP(I, d, a, b, c, GET(7), 0x432aff97, 10)
    STEP(I, c, d, a, b, GET(14), 0xab9423a7, 15)
    STEP(I, b, c, d, a, GET(5), 0xfc93a039, 21)
    STEP(I, a, b, c, d, GET(12), 0x655b59c3, 6)
    STEP(I, d, a, b, c, GET(3), 0x8f0ccc92, 10)
    STEP(I, c, d, a, b, GET(10), 0xffeff47d, 15)
    STEP(I, b, c, d, a, GET(1), 0x85845dd1, 21)
    STEP(I, a, b, c, d, GET(8), 0x6fa87e4f, 6)
    STEP(I, d, a, b, c, GET(15), 0xfe2ce6e0, 10)
    STEP(I, c, d, a, b, GET(6), 0xa3014314, 15)
    STEP(I, b, c, d, a, GET(13), 0x4e0811a1, 21)
    STEP(I, a, b, c, d, GET(4), 0xf7537e82, 6)
    STEP(I, d, a, b, c, GET(11), 0xbd3af235, 10)
    STEP(I, c, d, a, b, GET(2), 0x2ad7d2bb, 15)
    STEP(I, b, c, d, a, GET(9), 0xeb86d391, 21)

    a += saved_a; b += saved_b; c += saved_c; d += saved_d;
    ptr += 64;
  } while (size -= 64);

  ctx->a = a; ctx->b = b; ctx->c = c; ctx->d = d;
  return ptr;
}

void csupport_md5_init(csupport_md5_ctx_t *ctx) {
  ctx->a = 0x67452301;
  ctx->b = 0xefcdab89;
  ctx->c = 0x98badcfe;
  ctx->d = 0x10325476;
  ctx->lo = 0;
  ctx->hi = 0;
}

void csupport_md5_update(csupport_md5_ctx_t *ctx, const uint8_t *data,
                         size_t size) {
  csupport_md5_u32 saved_lo;
  unsigned long used, free_space;

  saved_lo = ctx->lo;
  if ((ctx->lo = (saved_lo + (csupport_md5_u32)size) & 0x1fffffff) < saved_lo)
    ctx->hi++;
  ctx->hi += (csupport_md5_u32)(size >> 29);

  used = saved_lo & 0x3f;
  if (used) {
    free_space = 64 - used;
    if (size < free_space) {
      memcpy(&ctx->buffer[used], data, size);
      return;
    }
    memcpy(&ctx->buffer[used], data, free_space);
    data += free_space;
    size -= free_space;
    md5_body(ctx, ctx->buffer, 64);
  }
  if (size >= 64) {
    data = md5_body(ctx, data, size & ~(unsigned long)0x3f);
    size &= 0x3f;
  }
  memcpy(ctx->buffer, data, size);
}

void csupport_md5_final(csupport_md5_ctx_t *ctx, uint8_t result[16]) {
  unsigned long used, free_space;

  used = ctx->lo & 0x3f;
  ctx->buffer[used++] = 0x80;
  free_space = 64 - used;

  if (free_space < 8) {
    memset(&ctx->buffer[used], 0, free_space);
    md5_body(ctx, ctx->buffer, 64);
    used = 0;
    free_space = 64;
  }

  memset(&ctx->buffer[used], 0, free_space - 8);
  ctx->lo <<= 3;
  csupport_write32le(&ctx->buffer[56], ctx->lo);
  csupport_write32le(&ctx->buffer[60], ctx->hi);
  md5_body(ctx, ctx->buffer, 64);

  csupport_write32le(&result[0], ctx->a);
  csupport_write32le(&result[4], ctx->b);
  csupport_write32le(&result[8], ctx->c);
  csupport_write32le(&result[12], ctx->d);
}

void csupport_md5_hash(const uint8_t *data, size_t size, uint8_t result[16]) {
  csupport_md5_ctx_t ctx;
  csupport_md5_init(&ctx);
  csupport_md5_update(&ctx, data, size);
  csupport_md5_final(&ctx, result);
}

