/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_CRYPTO_H
#define NEVERC_KRT_CRYPTO_H

#include <linux/types.h>
#include <nvk_rt.h>
#include <linux/compiler.h>
#include <linux/string.h>

/*
 * Secure zeroing — prevents the compiler from optimizing away dead stores
 * to sensitive buffers (keys, intermediate hashes).
 */
static __always_inline void _neverc_krt_secure_zero(void *p, size_t n)
{
	volatile unsigned char *vp = (volatile unsigned char *)p;
	size_t i;
	for (i = 0; i < n; i++)
		vp[i] = 0;
}

/* ------------------------------------------------------------------ */
/*  SHA-256  (FIPS 180-4)                                             */
/* ------------------------------------------------------------------ */

struct neverc_krt_sha256_ctx {
	u32 state[8];
	u64 count;
	u8  buf[64];
};

static __always_inline u32 _neverc_krt_ror32(u32 v, int n)
{ return (v >> n) | (v << (32 - n)); }

static __always_inline u32 _neverc_krt_sha_ch(u32 x, u32 y, u32 z)
{ return (x & y) ^ (~x & z); }

static __always_inline u32 _neverc_krt_sha_maj(u32 x, u32 y, u32 z)
{ return (x & y) ^ (x & z) ^ (y & z); }

static __always_inline u32 _neverc_krt_sha_s0(u32 x)
{ return _neverc_krt_ror32(x, 2) ^ _neverc_krt_ror32(x, 13) ^ _neverc_krt_ror32(x, 22); }

static __always_inline u32 _neverc_krt_sha_s1(u32 x)
{ return _neverc_krt_ror32(x, 6) ^ _neverc_krt_ror32(x, 11) ^ _neverc_krt_ror32(x, 25); }

static __always_inline u32 _neverc_krt_sha_g0(u32 x)
{ return _neverc_krt_ror32(x, 7) ^ _neverc_krt_ror32(x, 18) ^ (x >> 3); }

static __always_inline u32 _neverc_krt_sha_g1(u32 x)
{ return _neverc_krt_ror32(x, 17) ^ _neverc_krt_ror32(x, 19) ^ (x >> 10); }

static __always_inline u32 _neverc_krt_be32(const u8 *p)
{
	return ((u32)p[0] << 24) | ((u32)p[1] << 16) |
	       ((u32)p[2] << 8)  | (u32)p[3];
}

static __always_inline void _neverc_krt_put_be32(u8 *p, u32 v)
{
	p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16);
	p[2] = (u8)(v >> 8);  p[3] = (u8)v;
}



void neverc_krt_sha256_init(struct neverc_krt_sha256_ctx *ctx);


void neverc_krt_sha256_update(struct neverc_krt_sha256_ctx *ctx,
			      const void *data, size_t len);


void neverc_krt_sha256_final(struct neverc_krt_sha256_ctx *ctx, u8 *digest);


void neverc_krt_sha256(const void *data, size_t len, u8 *digest);


int neverc_krt_sha256_eq(const u8 *a, const u8 *b);



/* ------------------------------------------------------------------ */
/*  HMAC-SHA256  (RFC 2104)                                           */
/* ------------------------------------------------------------------ */

#define NEVERC_KRT_HMAC_SHA256_LEN  32

void neverc_krt_hmac_sha256(const void *key, size_t key_len,
			    const void *data, size_t data_len,
			    u8 *mac);



/* ------------------------------------------------------------------ */
/*  ChaCha20  (RFC 8439) — constant-time stream cipher                */
/* ------------------------------------------------------------------ */

struct neverc_krt_chacha20_ctx {
	u32 state[16];
};

static __always_inline u32 _neverc_krt_rotl32(u32 v, int n)
{ return (v << n) | (v >> (32 - n)); }

#define _NEVERC_KRT_QR(a, b, c, d)                  \
	do {                                  \
		a += b; d ^= a; d = _neverc_krt_rotl32(d, 16); \
		c += d; b ^= c; b = _neverc_krt_rotl32(b, 12); \
		a += b; d ^= a; d = _neverc_krt_rotl32(d, 8);  \
		c += d; b ^= c; b = _neverc_krt_rotl32(b, 7);  \
	} while (0)

static __always_inline u32 _neverc_krt_le32(const u8 *p)
{
	return (u32)p[0] | ((u32)p[1] << 8) |
	       ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static __always_inline void _neverc_krt_put_le32(u8 *p, u32 v)
{
	p[0] = (u8)v; p[1] = (u8)(v >> 8);
	p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24);
}

void neverc_krt_chacha20_init(struct neverc_krt_chacha20_ctx *ctx,
			      const u8 key[32], const u8 nonce[12],
			      u32 counter);




void neverc_krt_chacha20_crypt(struct neverc_krt_chacha20_ctx *ctx,
			       void *out, const void *in, size_t len);


void neverc_krt_chacha20_encrypt(const u8 key[32], const u8 nonce[12],
				 u32 counter,
				 void *out, const void *in, size_t len);



/* ------------------------------------------------------------------ */
/*  Module self-integrity verification                                */
/* ------------------------------------------------------------------ */

int neverc_krt_crypto_verify_region(const void *addr, size_t len,
				    const u8 expected_hash[32]);


#endif /* NEVERC_KRT_CRYPTO_H */
