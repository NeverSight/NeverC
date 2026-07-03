/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_CRYPTO_H
#define NEVERC_KRT_CRYPTO_H

#include <linux/types.h>

/* ------------------------------------------------------------------ */
/*  SHA-256  (FIPS 180-4)                                             */
/* ------------------------------------------------------------------ */

struct neverc_krt_sha256_ctx {
	u32 state[8];
	u64 count;
	u8  buf[64];
};

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
