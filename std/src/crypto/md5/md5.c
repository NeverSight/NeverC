/*
 * MD5 implementation per RFC 1321.
 * Pure C, no libc dependency beyond stdint/string.
 */
#include "neverc/std/crypto/md5.h"
#include <string.h>

#define F(x, y, z) (((x) & (y)) | (~(x) & (z)))
#define G(x, y, z) (((x) & (z)) | ((y) & ~(z)))
#define H(x, y, z) ((x) ^ (y) ^ (z))
#define I(x, y, z) ((y) ^ ((x) | ~(z)))
#define ROTL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

#define FF(a,b,c,d,x,s,ac) { (a)+=F((b),(c),(d))+(x)+(ac); (a)=ROTL((a),(s))+(b); }
#define GG(a,b,c,d,x,s,ac) { (a)+=G((b),(c),(d))+(x)+(ac); (a)=ROTL((a),(s))+(b); }
#define HH(a,b,c,d,x,s,ac) { (a)+=H((b),(c),(d))+(x)+(ac); (a)=ROTL((a),(s))+(b); }
#define II(a,b,c,d,x,s,ac) { (a)+=I((b),(c),(d))+(x)+(ac); (a)=ROTL((a),(s))+(b); }

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 |
           (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}

static void put_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;        p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static void put_le64(uint8_t *p, uint64_t v) {
    put_le32(p, (uint32_t)v);
    put_le32(p + 4, (uint32_t)(v >> 32));
}

static void md5_block(uint32_t state[4], const uint8_t block[64]) {
    uint32_t M[16];
    for (int i = 0; i < 16; i++)
        M[i] = le32(block + 4 * i);

    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];

    FF(a,b,c,d,M[ 0], 7,0xd76aa478) FF(d,a,b,c,M[ 1],12,0xe8c7b756)
    FF(c,d,a,b,M[ 2],17,0x242070db) FF(b,c,d,a,M[ 3],22,0xc1bdceee)
    FF(a,b,c,d,M[ 4], 7,0xf57c0faf) FF(d,a,b,c,M[ 5],12,0x4787c62a)
    FF(c,d,a,b,M[ 6],17,0xa8304613) FF(b,c,d,a,M[ 7],22,0xfd469501)
    FF(a,b,c,d,M[ 8], 7,0x698098d8) FF(d,a,b,c,M[ 9],12,0x8b44f7af)
    FF(c,d,a,b,M[10],17,0xffff5bb1) FF(b,c,d,a,M[11],22,0x895cd7be)
    FF(a,b,c,d,M[12], 7,0x6b901122) FF(d,a,b,c,M[13],12,0xfd987193)
    FF(c,d,a,b,M[14],17,0xa679438e) FF(b,c,d,a,M[15],22,0x49b40821)

    GG(a,b,c,d,M[ 1], 5,0xf61e2562) GG(d,a,b,c,M[ 6], 9,0xc040b340)
    GG(c,d,a,b,M[11],14,0x265e5a51) GG(b,c,d,a,M[ 0],20,0xe9b6c7aa)
    GG(a,b,c,d,M[ 5], 5,0xd62f105d) GG(d,a,b,c,M[10], 9,0x02441453)
    GG(c,d,a,b,M[15],14,0xd8a1e681) GG(b,c,d,a,M[ 4],20,0xe7d3fbc8)
    GG(a,b,c,d,M[ 9], 5,0x21e1cde6) GG(d,a,b,c,M[14], 9,0xc33707d6)
    GG(c,d,a,b,M[ 3],14,0xf4d50d87) GG(b,c,d,a,M[ 8],20,0x455a14ed)
    GG(a,b,c,d,M[13], 5,0xa9e3e905) GG(d,a,b,c,M[ 2], 9,0xfcefa3f8)
    GG(c,d,a,b,M[ 7],14,0x676f02d9) GG(b,c,d,a,M[12],20,0x8d2a4c8a)

    HH(a,b,c,d,M[ 5], 4,0xfffa3942) HH(d,a,b,c,M[ 8],11,0x8771f681)
    HH(c,d,a,b,M[11],16,0x6d9d6122) HH(b,c,d,a,M[14],23,0xfde5380c)
    HH(a,b,c,d,M[ 1], 4,0xa4beea44) HH(d,a,b,c,M[ 4],11,0x4bdecfa9)
    HH(c,d,a,b,M[ 7],16,0xf6bb4b60) HH(b,c,d,a,M[10],23,0xbebfbc70)
    HH(a,b,c,d,M[13], 4,0x289b7ec6) HH(d,a,b,c,M[ 0],11,0xeaa127fa)
    HH(c,d,a,b,M[ 3],16,0xd4ef3085) HH(b,c,d,a,M[ 6],23,0x04881d05)
    HH(a,b,c,d,M[ 9], 4,0xd9d4d039) HH(d,a,b,c,M[12],11,0xe6db99e5)
    HH(c,d,a,b,M[15],16,0x1fa27cf8) HH(b,c,d,a,M[ 2],23,0xc4ac5665)

    II(a,b,c,d,M[ 0], 6,0xf4292244) II(d,a,b,c,M[ 7],10,0x432aff97)
    II(c,d,a,b,M[14],15,0xab9423a7) II(b,c,d,a,M[ 5],21,0xfc93a039)
    II(a,b,c,d,M[12], 6,0x655b59c3) II(d,a,b,c,M[ 3],10,0x8f0ccc92)
    II(c,d,a,b,M[10],15,0xffeff47d) II(b,c,d,a,M[ 1],21,0x85845dd1)
    II(a,b,c,d,M[ 8], 6,0x6fa87e4f) II(d,a,b,c,M[15],10,0xfe2ce6e0)
    II(c,d,a,b,M[ 6],15,0xa3014314) II(b,c,d,a,M[13],21,0x4e0811a1)
    II(a,b,c,d,M[ 4], 6,0xf7537e82) II(d,a,b,c,M[11],10,0xbd3af235)
    II(c,d,a,b,M[ 2],15,0x2ad7d2bb) II(b,c,d,a,M[ 9],21,0xeb86d391)

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
}

void neverc_md5_init(neverc_md5_ctx *ctx) {
    if (!ctx) return;
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xefcdab89;
    ctx->state[2] = 0x98badcfe;
    ctx->state[3] = 0x10325476;
    ctx->count = 0;
}

void neverc_md5_update(neverc_md5_ctx *ctx, const uint8_t *data, size_t len) {
    if (!ctx || len == 0) return;
    if (!data) return;
    size_t buffered = (size_t)(ctx->count & 63);
    ctx->count += len;

    if (buffered > 0) {
        size_t need = 64 - buffered;
        if (len < need) {
            memcpy(ctx->buf + buffered, data, len);
            return;
        }
        memcpy(ctx->buf + buffered, data, need);
        md5_block(ctx->state, ctx->buf);
        data += need;
        len -= need;
    }
    while (len >= 64) {
        md5_block(ctx->state, data);
        data += 64;
        len -= 64;
    }
    if (len > 0)
        memcpy(ctx->buf, data, len);
}

void neverc_md5_final(neverc_md5_ctx *ctx, uint8_t digest[16]) {
    if (!ctx || !digest) return;
    uint64_t bits = ctx->count * 8;
    size_t buffered = (size_t)(ctx->count & 63);

    ctx->buf[buffered++] = 0x80;
    if (buffered > 56) {
        memset(ctx->buf + buffered, 0, 64 - buffered);
        md5_block(ctx->state, ctx->buf);
        buffered = 0;
    }
    memset(ctx->buf + buffered, 0, 56 - buffered);
    put_le64(ctx->buf + 56, bits);
    md5_block(ctx->state, ctx->buf);

    for (int i = 0; i < 4; i++)
        put_le32(digest + 4 * i, ctx->state[i]);
}

void neverc_md5_sum(const uint8_t *data, size_t len, uint8_t digest[16]) {
    neverc_md5_ctx ctx;
    neverc_md5_init(&ctx);
    neverc_md5_update(&ctx, data, len);
    neverc_md5_final(&ctx, digest);
}
