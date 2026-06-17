/* SPDX-License-Identifier: GPL-2.0 */
#include <nvk.h>

/* ---- internal helpers ---- */

static __always_inline void _neverc_krt_secure_zero(void *p, size_t n)
{
	volatile unsigned char *vp = (volatile unsigned char *)p;
	size_t i;
	for (i = 0; i < n; i++)
		vp[i] = 0;
}

/* ---- SHA-256 internal helpers ---- */

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

/* ---- ChaCha20 internal helpers ---- */

static __always_inline u32 _neverc_krt_rotl32(u32 v, int n)
{ return (v << n) | (v >> (32 - n)); }

#define _NEVERC_KRT_QR(a, b, c, d)                              \
	do {                                                     \
		a += b; d ^= a; d = _neverc_krt_rotl32(d, 16);  \
		c += d; b ^= c; b = _neverc_krt_rotl32(b, 12);  \
		a += b; d ^= a; d = _neverc_krt_rotl32(d, 8);   \
		c += d; b ^= c; b = _neverc_krt_rotl32(b, 7);   \
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

/* ---- SHA-256 constants ---- */

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

static void _neverc_krt_sha256_transform(u32 *state, const u8 *block)
{
	u32 w[64], a, b, c, d, e, f, g, h;
	int i;

	for (i = 0; i < 16; i++)
		w[i] = _neverc_krt_be32(block + i * 4);
	for (i = 16; i < 64; i++)
		w[i] = _neverc_krt_sha_g1(w[i-2]) + w[i-7] +
		       _neverc_krt_sha_g0(w[i-15]) + w[i-16];

	a = state[0]; b = state[1]; c = state[2]; d = state[3];
	e = state[4]; f = state[5]; g = state[6]; h = state[7];

	for (i = 0; i < 64; i++) {
		u32 t1 = h + _neverc_krt_sha_s1(e) + _neverc_krt_sha_ch(e, f, g) +
			 _neverc_krt_sha256_k[i] + w[i];
		u32 t2 = _neverc_krt_sha_s0(a) + _neverc_krt_sha_maj(a, b, c);
		h = g; g = f; f = e; e = d + t1;
		d = c; c = b; b = a; a = t1 + t2;
	}

	state[0] += a; state[1] += b; state[2] += c; state[3] += d;
	state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

void neverc_krt_sha256_init(struct neverc_krt_sha256_ctx *ctx)
{
	ctx->state[0] = 0x6A09E667U; ctx->state[1] = 0xBB67AE85U;
	ctx->state[2] = 0x3C6EF372U; ctx->state[3] = 0xA54FF53AU;
	ctx->state[4] = 0x510E527FU; ctx->state[5] = 0x9B05688CU;
	ctx->state[6] = 0x1F83D9ABU; ctx->state[7] = 0x5BE0CD19U;
	ctx->count = 0;
}

void neverc_krt_sha256_update(struct neverc_krt_sha256_ctx *ctx,
			      const void *data, size_t len)
{
	const u8 *src = (const u8 *)data;
	size_t fill = (size_t)(ctx->count & 63);

	ctx->count += len;

	if (fill && fill + len >= 64) {
		size_t n = 64 - fill;
		__builtin_memcpy(ctx->buf + fill, src, n);
		_neverc_krt_sha256_transform(ctx->state, ctx->buf);
		src += n;
		len -= n;
		fill = 0;
	}

	while (len >= 64) {
		_neverc_krt_sha256_transform(ctx->state, src);
		src += 64;
		len -= 64;
	}

	if (len)
		__builtin_memcpy(ctx->buf + fill, src, len);
}

void neverc_krt_sha256_final(struct neverc_krt_sha256_ctx *ctx, u8 *digest)
{
	u64 bits = ctx->count << 3;
	size_t fill = (size_t)(ctx->count & 63);
	u8 pad[64];
	int i;

	__builtin_memset(pad, 0, sizeof(pad));
	pad[0] = 0x80;

	if (fill < 56) {
		neverc_krt_sha256_update(ctx, pad, 56 - fill);
	} else {
		neverc_krt_sha256_update(ctx, pad, 64 - fill + 56);
	}

	u8 blen[8];
	_neverc_krt_put_be32(blen, (u32)(bits >> 32));
	_neverc_krt_put_be32(blen + 4, (u32)bits);
	neverc_krt_sha256_update(ctx, blen, 8);

	for (i = 0; i < 8; i++)
		_neverc_krt_put_be32(digest + i * 4, ctx->state[i]);
}

void neverc_krt_sha256(const void *data, size_t len, u8 *digest)
{
	struct neverc_krt_sha256_ctx ctx;
	neverc_krt_sha256_init(&ctx);
	neverc_krt_sha256_update(&ctx, data, len);
	neverc_krt_sha256_final(&ctx, digest);
}

int neverc_krt_sha256_eq(const u8 *a, const u8 *b)
{
	unsigned int diff = 0;
	int i;
	for (i = 0; i < 32; i++)
		diff |= a[i] ^ b[i];
	return diff == 0;
}

void neverc_krt_hmac_sha256(const void *key, size_t key_len,
			    const void *data, size_t data_len,
			    u8 *mac)
{
	u8 kbuf[64], ipad[64], opad[64], ihash[32];
	struct neverc_krt_sha256_ctx ctx;
	int i;

	__builtin_memset(kbuf, 0, 64);
	if (key_len > 64) {
		neverc_krt_sha256(key, key_len, kbuf);
	} else {
		__builtin_memcpy(kbuf, key, key_len);
	}

	for (i = 0; i < 64; i++) {
		ipad[i] = kbuf[i] ^ 0x36;
		opad[i] = kbuf[i] ^ 0x5C;
	}

	neverc_krt_sha256_init(&ctx);
	neverc_krt_sha256_update(&ctx, ipad, 64);
	neverc_krt_sha256_update(&ctx, data, data_len);
	neverc_krt_sha256_final(&ctx, ihash);

	neverc_krt_sha256_init(&ctx);
	neverc_krt_sha256_update(&ctx, opad, 64);
	neverc_krt_sha256_update(&ctx, ihash, 32);
	neverc_krt_sha256_final(&ctx, mac);

	_neverc_krt_secure_zero(kbuf, 64);
	_neverc_krt_secure_zero(ipad, 64);
	_neverc_krt_secure_zero(opad, 64);
	_neverc_krt_secure_zero(ihash, 32);
	_neverc_krt_secure_zero(&ctx, sizeof(ctx));
}

void neverc_krt_chacha20_init(struct neverc_krt_chacha20_ctx *ctx,
			      const u8 key[32], const u8 nonce[12],
			      u32 counter)
{
	ctx->state[0]  = 0x61707865U; /* "expa" */
	ctx->state[1]  = 0x3320646EU; /* "nd 3" */
	ctx->state[2]  = 0x79622D32U; /* "2-by" */
	ctx->state[3]  = 0x6B206574U; /* "te k" */
	ctx->state[4]  = _neverc_krt_le32(key);
	ctx->state[5]  = _neverc_krt_le32(key + 4);
	ctx->state[6]  = _neverc_krt_le32(key + 8);
	ctx->state[7]  = _neverc_krt_le32(key + 12);
	ctx->state[8]  = _neverc_krt_le32(key + 16);
	ctx->state[9]  = _neverc_krt_le32(key + 20);
	ctx->state[10] = _neverc_krt_le32(key + 24);
	ctx->state[11] = _neverc_krt_le32(key + 28);
	ctx->state[12] = counter;
	ctx->state[13] = _neverc_krt_le32(nonce);
	ctx->state[14] = _neverc_krt_le32(nonce + 4);
	ctx->state[15] = _neverc_krt_le32(nonce + 8);
}

static void _neverc_krt_chacha20_block(const u32 *input, u8 *output)
{
	u32 x[16];
	int i;

	for (i = 0; i < 16; i++)
		x[i] = input[i];

	for (i = 0; i < 10; i++) {
		_NEVERC_KRT_QR(x[0], x[4], x[ 8], x[12]);
		_NEVERC_KRT_QR(x[1], x[5], x[ 9], x[13]);
		_NEVERC_KRT_QR(x[2], x[6], x[10], x[14]);
		_NEVERC_KRT_QR(x[3], x[7], x[11], x[15]);
		_NEVERC_KRT_QR(x[0], x[5], x[10], x[15]);
		_NEVERC_KRT_QR(x[1], x[6], x[11], x[12]);
		_NEVERC_KRT_QR(x[2], x[7], x[ 8], x[13]);
		_NEVERC_KRT_QR(x[3], x[4], x[ 9], x[14]);
	}

	for (i = 0; i < 16; i++)
		_neverc_krt_put_le32(output + i * 4, x[i] + input[i]);
}

void neverc_krt_chacha20_crypt(struct neverc_krt_chacha20_ctx *ctx,
			       void *out, const void *in, size_t len)
{
	u8 *dst = (u8 *)out;
	const u8 *src = (const u8 *)in;
	u8 block[64];

	while (len > 0) {
		_neverc_krt_chacha20_block(ctx->state, block);
		ctx->state[12]++;

		size_t chunk = len < 64 ? len : 64;
		size_t i;
		for (i = 0; i < chunk; i++)
			dst[i] = src[i] ^ block[i];

		dst += chunk;
		src += chunk;
		len -= chunk;
	}

	_neverc_krt_secure_zero(block, sizeof(block));
}

void neverc_krt_chacha20_encrypt(const u8 key[32], const u8 nonce[12],
				 u32 counter,
				 void *out, const void *in, size_t len)
{
	struct neverc_krt_chacha20_ctx ctx;
	neverc_krt_chacha20_init(&ctx, key, nonce, counter);
	neverc_krt_chacha20_crypt(&ctx, out, in, len);
	_neverc_krt_secure_zero(&ctx, sizeof(ctx));
}

int neverc_krt_crypto_verify_region(const void *addr, size_t len,
				    const u8 expected_hash[32])
{
	u8 hash[32];
	neverc_krt_sha256(addr, len, hash);
	return neverc_krt_sha256_eq(hash, expected_hash);
}

