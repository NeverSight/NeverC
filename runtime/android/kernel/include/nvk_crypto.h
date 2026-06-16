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

static const u32 _neverc_krt_sha256_k[64] = {
	0x428A2F98U, 0x71374491U, 0xB5C0FBCFU, 0xE9B5DBA5U,
	0x3956C25BU, 0x59F111F1U, 0x923F82A4U, 0xAB1C5ED5U,
	0xD807AA98U, 0x12835B01U, 0x243185BEU, 0x550C7DC3U,
	0x72BE5D74U, 0x80DEB1FEU, 0x9BDC06A7U, 0xC19BF174U,
	0xE49B69C1U, 0xEFBE4786U, 0x0FC19DC6U, 0x240CA1CCU,
	0x2DE92C6FU, 0x4A7484AAU, 0x5CB0A9DCU, 0x76F988DAU,
	0x983E5152U, 0xA831C66DU, 0xB00327C8U, 0xBF597FC7U,
	0xC6E00BF3U, 0xD5A79147U, 0x06CA6351U, 0x14292967U,
	0x27B70A85U, 0x2E1B2138U, 0x4D2C6DFCU, 0x53380D13U,
	0x650A7354U, 0x766A0ABBU, 0x81C2C92EU, 0x92722C85U,
	0xA2BFE8A1U, 0xA81A664BU, 0xC24B8B70U, 0xC76C51A3U,
	0xD192E819U, 0xD6990624U, 0xF40E3585U, 0x106AA070U,
	0x19A4C116U, 0x1E376C08U, 0x2748774CU, 0x34B0BCB5U,
	0x391C0CB3U, 0x4ED8AA4AU, 0x5B9CCA4FU, 0x682E6FF3U,
	0x748F82EEU, 0x78A5636FU, 0x84C87814U, 0x8CC70208U,
	0x90BEFFFAU, 0xA4506CEBU, 0xBEF9A3F7U, 0xC67178F2U,
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

void _neverc_krt_sha256_transform(u32 *state, const u8 *block);


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


void _neverc_krt_chacha20_block(const u32 *input, u8 *output);


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
