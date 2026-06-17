/* SPDX-License-Identifier: GPL-2.0 */
/* neverc_krt_crypto.c — implementations extracted from neverc_krt_crypto.h. */
#include <nvk.h>

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

