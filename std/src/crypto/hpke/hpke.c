/*
 * HPKE — Hybrid Public Key Encryption (RFC 9180).
 *
 * Implements Base mode (mode 0) with:
 *   KEM:  DHKEM(X25519, HKDF-SHA256)  [0x0020]
 *         DHKEM(P-256,  HKDF-SHA256)  [0x0010]
 *   KDF:  HKDF-SHA256 [0x0001], HKDF-SHA512 [0x0003]
 *   AEAD: AES-128-GCM [0x0001], AES-256-GCM [0x0002],
 *         ChaCha20Poly1305 [0x0003], Export-only [0xFFFF]
 */
#include "neverc/std/crypto/hpke.h"
#include "neverc/std/crypto/ecdh.h"
#include "neverc/std/crypto/hkdf.h"
#include "neverc/std/crypto/gcm.h"
#include "neverc/std/crypto/chacha20poly1305.h"
#include "neverc/std/crypto/rand.h"
#include "neverc/std/_platform.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* ── helpers ─────────────────────────────────────────────────── */

static void be16(uint8_t *dst, uint16_t v) {
    dst[0] = (uint8_t)(v >> 8);
    dst[1] = (uint8_t)(v);
}

static void be64(uint8_t *dst, uint64_t v) {
    for (int i = 7; i >= 0; i--) {
        dst[i] = (uint8_t)(v & 0xFF);
        v >>= 8;
    }
}

static int size_add(size_t *total, size_t value) {
    if (value > SIZE_MAX - *total)
        return -1;
    *total += value;
    return 0;
}

static int kem_enc_size(uint16_t kem_id) {
    switch (kem_id) {
    case NEVERC_HPKE_KEM_X25519_SHA256: return 32;
    case NEVERC_HPKE_KEM_P256_SHA256:   return 65;
    default: return -1;
    }
}

static int kem_secret_size(uint16_t kem_id) {
    switch (kem_id) {
    case NEVERC_HPKE_KEM_X25519_SHA256: return 32;
    case NEVERC_HPKE_KEM_P256_SHA256:   return 32;
    default: return -1;
    }
}

static neverc_ecdh_curve_t kem_to_curve(uint16_t kem_id) {
    switch (kem_id) {
    case NEVERC_HPKE_KEM_X25519_SHA256: return NEVERC_ECDH_CURVE_X25519;
    case NEVERC_HPKE_KEM_P256_SHA256:   return NEVERC_ECDH_CURVE_P256;
    default: return (neverc_ecdh_curve_t)-1;
    }
}

static int kdf_hash_size(uint16_t kdf_id) {
    switch (kdf_id) {
    case NEVERC_HPKE_KDF_SHA256: return 32;
    case NEVERC_HPKE_KDF_SHA512: return 64;
    default: return -1;
    }
}

static int aead_key_size(uint16_t aead_id) {
    switch (aead_id) {
    case NEVERC_HPKE_AEAD_AES128GCM:        return 16;
    case NEVERC_HPKE_AEAD_AES256GCM:        return 32;
    case NEVERC_HPKE_AEAD_CHACHA20POLY1305: return 32;
    case NEVERC_HPKE_AEAD_EXPORT_ONLY:      return 0;
    default: return -1;
    }
}

static int aead_nonce_size(uint16_t aead_id) {
    switch (aead_id) {
    case NEVERC_HPKE_AEAD_AES128GCM:
    case NEVERC_HPKE_AEAD_AES256GCM:
    case NEVERC_HPKE_AEAD_CHACHA20POLY1305: return 12;
    case NEVERC_HPKE_AEAD_EXPORT_ONLY:      return 0;
    default: return -1;
    }
}

/* ── Labeled Extract / Expand (RFC 9180 §4) ───────────────── */

static int labeled_extract(uint16_t kdf_id,
                           const uint8_t *suite_id, size_t sid_len,
                           const uint8_t *salt, size_t salt_len,
                           const char *label,
                           const uint8_t *ikm, size_t ikm_len,
                           uint8_t *prk) {
    if (!suite_id || !label || !prk || (!salt && salt_len > 0) ||
        (!ikm && ikm_len > 0))
        return -1;
    size_t lbl_len = strlen(label);
    uint8_t labeled_stack[512];
    size_t labeled_len = 7;
    if (size_add(&labeled_len, sid_len) != 0 ||
        size_add(&labeled_len, lbl_len) != 0 ||
        size_add(&labeled_len, ikm_len) != 0)
        return -1;
    uint8_t *labeled = labeled_stack;
    if (labeled_len > sizeof(labeled_stack)) {
        labeled = (uint8_t *)malloc(labeled_len);
        if (!labeled) return -1;
    }

    size_t off = 0;
    memcpy(labeled + off, "HPKE-v1", 7); off += 7;
    memcpy(labeled + off, suite_id, sid_len); off += sid_len;
    memcpy(labeled + off, label, lbl_len); off += lbl_len;
    if (ikm && ikm_len) { memcpy(labeled + off, ikm, ikm_len); off += ikm_len; }

    int ret = -1;
    if (kdf_id == NEVERC_HPKE_KDF_SHA256)
        ret = neverc_hkdf_extract_sha256(prk, salt, salt_len, labeled, off);
    else if (kdf_id == NEVERC_HPKE_KDF_SHA512)
        ret = neverc_hkdf_extract_sha512(prk, salt, salt_len, labeled, off);
    neverc_platform_secure_zero(labeled, labeled_len);
    neverc_platform_secure_zero(labeled, labeled_len);
    if (labeled != labeled_stack)
        free(labeled);
    return ret;
}

static int labeled_expand(uint16_t kdf_id,
                          const uint8_t *suite_id, size_t sid_len,
                          const uint8_t *prk,
                          const char *label,
                          const uint8_t *info, size_t info_len,
                          uint16_t length,
                          uint8_t *out) {
    if (!suite_id || !prk || !label || (!info && info_len > 0) ||
        (!out && length > 0))
        return -1;
    size_t lbl_len = strlen(label);
    uint8_t labeled_stack[512];
    size_t labeled_len = 2 + 7;
    if (size_add(&labeled_len, sid_len) != 0 ||
        size_add(&labeled_len, lbl_len) != 0 ||
        size_add(&labeled_len, info_len) != 0)
        return -1;
    uint8_t *labeled = labeled_stack;
    if (labeled_len > sizeof(labeled_stack)) {
        labeled = (uint8_t *)malloc(labeled_len);
        if (!labeled) return -1;
    }

    size_t off = 0;
    be16(labeled + off, length); off += 2;
    memcpy(labeled + off, "HPKE-v1", 7); off += 7;
    memcpy(labeled + off, suite_id, sid_len); off += sid_len;
    memcpy(labeled + off, label, lbl_len); off += lbl_len;
    if (info && info_len) { memcpy(labeled + off, info, info_len); off += info_len; }

    int ret = -1;
    if (kdf_id == NEVERC_HPKE_KDF_SHA256)
        ret = neverc_hkdf_expand_sha256(out, length, prk, labeled, off);
    else if (kdf_id == NEVERC_HPKE_KDF_SHA512)
        ret = neverc_hkdf_expand_sha512(out, length, prk, labeled, off);
    if (labeled != labeled_stack)
        free(labeled);
    return ret;
}

/* ── DHKEM ──────────────────────────────────────────────────── */

static void make_kem_suite_id(uint16_t kem_id, uint8_t *sid) {
    memcpy(sid, "KEM", 3);
    be16(sid + 3, kem_id);
}

static int extract_and_expand(uint16_t kem_id,
                              const uint8_t *dh, size_t dh_len,
                              const uint8_t *kem_ctx, size_t ctx_len,
                              uint8_t *shared_secret) {
    uint8_t kem_sid[5];
    make_kem_suite_id(kem_id, kem_sid);

    uint8_t prk[64] = {0};
    if (labeled_extract(NEVERC_HPKE_KDF_SHA256, kem_sid, 5,
                        NULL, 0, "eae_prk", dh, dh_len, prk) != 0) {
        neverc_platform_secure_zero(prk, sizeof(prk));
        return -1;
    }

    int ss_len = kem_secret_size(kem_id);
    int result = labeled_expand(NEVERC_HPKE_KDF_SHA256, kem_sid, 5,
                                prk, "shared_secret", kem_ctx, ctx_len,
                                (uint16_t)ss_len, shared_secret);
    neverc_platform_secure_zero(prk, sizeof(prk));
    return result;
}

/* Encapsulate: generate ephemeral key pair, compute shared secret.
 * enc: public key of ephemeral pair
 * shared_secret: derived shared secret */
static int kem_encap(uint16_t kem_id,
                     const uint8_t *pk, size_t pk_len,
                     uint8_t *enc, size_t *enc_len,
                     uint8_t *shared_secret) {
    int expected_len = kem_enc_size(kem_id);
    if (expected_len < 0 || !pk || pk_len != (size_t)expected_len ||
        !enc || !enc_len || !shared_secret)
        return -1;
    *enc_len = 0;
    neverc_ecdh_curve_t curve = kem_to_curve(kem_id);
    neverc_ecdh_key_t ek;
    memset(&ek, 0, sizeof(ek));
    if (neverc_ecdh_generate_key(curve, &ek) != 0)
        return -1;

    int result = -1;
    uint8_t dh[48] = {0};
    int pub_len = neverc_ecdh_public_key_bytes(&ek, enc, NEVERC_HPKE_MAX_ENC_SIZE);
    if (pub_len < 0) goto cleanup;
    *enc_len = (size_t)pub_len;

    int dh_len = neverc_ecdh_compute(&ek, pk, pk_len, dh, sizeof(dh));
    if (dh_len < 0) goto cleanup;

    uint8_t kem_ctx[130];
    memcpy(kem_ctx, enc, *enc_len);
    memcpy(kem_ctx + *enc_len, pk, pk_len);
    size_t ctx_len = *enc_len + pk_len;

    result = extract_and_expand(kem_id, dh, (size_t)dh_len,
                                kem_ctx, ctx_len, shared_secret);
cleanup:
    neverc_platform_secure_zero(dh, sizeof(dh));
    neverc_platform_secure_zero(&ek, sizeof(ek));
    if (result != 0) *enc_len = 0;
    return result;
}

/* Decapsulate: compute shared secret from enc + private key. */
static int kem_decap(uint16_t kem_id,
                     const uint8_t *enc, size_t enc_len,
                     const uint8_t *sk, size_t sk_len,
                     uint8_t *shared_secret) {
    int expected_len = kem_enc_size(kem_id);
    if (expected_len < 0 || !enc || enc_len != (size_t)expected_len ||
        !sk || sk_len != 32 || !shared_secret)
        return -1;
    neverc_ecdh_curve_t curve = kem_to_curve(kem_id);
    neverc_ecdh_key_t dk;
    memset(&dk, 0, sizeof(dk));
    if (neverc_ecdh_new_private_key(curve, sk, sk_len, &dk) != 0)
        return -1;

    int result = -1;
    uint8_t dh[48] = {0};
    int dh_len = neverc_ecdh_compute(&dk, enc, enc_len, dh, sizeof(dh));
    if (dh_len < 0) goto cleanup;

    uint8_t pk[97];
    int pk_len = neverc_ecdh_public_key_bytes(&dk, pk, sizeof(pk));
    if (pk_len < 0) goto cleanup;

    uint8_t kem_ctx[162];
    memcpy(kem_ctx, enc, enc_len);
    memcpy(kem_ctx + enc_len, pk, (size_t)pk_len);
    size_t ctx_len = enc_len + (size_t)pk_len;

    result = extract_and_expand(kem_id, dh, (size_t)dh_len,
                                kem_ctx, ctx_len, shared_secret);
cleanup:
    neverc_platform_secure_zero(dh, sizeof(dh));
    neverc_platform_secure_zero(&dk, sizeof(dk));
    return result;
}

/* ── Key Schedule (RFC 9180 §5.1) ──────────────────────────── */

static void make_hpke_suite_id(uint16_t kem_id, uint16_t kdf_id,
                               uint16_t aead_id, uint8_t *sid) {
    memcpy(sid, "HPKE", 4);
    be16(sid + 4, kem_id);
    be16(sid + 6, kdf_id);
    be16(sid + 8, aead_id);
}

static int key_schedule(neverc_hpke_ctx_t *ctx,
                        const uint8_t *shared_secret, size_t ss_len,
                        uint16_t kem_id, uint16_t kdf_id, uint16_t aead_id,
                        const uint8_t *info, size_t info_len) {
    int expected_ss_len = kem_secret_size(kem_id);
    int nh = kdf_hash_size(kdf_id);
    int nk = aead_key_size(aead_id);
    int nn = aead_nonce_size(aead_id);
    if (!ctx || !shared_secret || expected_ss_len < 0 ||
        ss_len != (size_t)expected_ss_len || nh < 0 || nk < 0 || nn < 0 ||
        (!info && info_len > 0))
        return -1;

    memset(ctx, 0, sizeof(*ctx));
    ctx->kem_id = kem_id;
    ctx->kdf_id = kdf_id;
    ctx->aead_id = aead_id;

    uint8_t sid[10];
    make_hpke_suite_id(kem_id, kdf_id, aead_id, sid);

    int result = -1;
    uint8_t psk_id_hash[64] = {0};
    uint8_t info_hash[64] = {0};
    uint8_t ks_context[1 + 64 + 64] = {0};
    uint8_t secret[64] = {0};
    if (labeled_extract(kdf_id, sid, 10, NULL, 0,
                        "psk_id_hash", NULL, 0, psk_id_hash) != 0)
        goto cleanup;

    if (labeled_extract(kdf_id, sid, 10, NULL, 0,
                        "info_hash", info, info_len, info_hash) != 0)
        goto cleanup;

    ks_context[0] = 0; /* mode = Base */
    memcpy(ks_context + 1, psk_id_hash, (size_t)nh);
    memcpy(ks_context + 1 + nh, info_hash, (size_t)nh);
    size_t ks_ctx_len = 1 + (size_t)nh * 2;

    if (labeled_extract(kdf_id, sid, 10, shared_secret, ss_len,
                        "secret", NULL, 0, secret) != 0)
        goto cleanup;

    ctx->key_len = nk;
    ctx->nonce_len = nn;

    if (nk > 0) {
        if (labeled_expand(kdf_id, sid, 10, secret, "key",
                           ks_context, ks_ctx_len, (uint16_t)nk, ctx->key) != 0)
            goto cleanup;
    }
    if (nn > 0) {
        if (labeled_expand(kdf_id, sid, 10, secret, "base_nonce",
                           ks_context, ks_ctx_len, (uint16_t)nn, ctx->base_nonce) != 0)
            goto cleanup;
    }

    ctx->exp_len = nh;
    if (labeled_expand(kdf_id, sid, 10, secret, "exp",
                       ks_context, ks_ctx_len, (uint16_t)nh, ctx->exp_secret) != 0)
        goto cleanup;

    result = 0;
cleanup:
    neverc_platform_secure_zero(psk_id_hash, sizeof(psk_id_hash));
    neverc_platform_secure_zero(info_hash, sizeof(info_hash));
    neverc_platform_secure_zero(ks_context, sizeof(ks_context));
    neverc_platform_secure_zero(secret, sizeof(secret));
    if (result != 0)
        neverc_platform_secure_zero(ctx, sizeof(*ctx));
    return result;
}

/* ── Nonce computation ──────────────────────────────────────── */

static void compute_nonce(const neverc_hpke_ctx_t *ctx, uint8_t *nonce) {
    memset(nonce, 0, (size_t)ctx->nonce_len);
    be64(nonce + ctx->nonce_len - 8, ctx->seq_num);
    for (int i = 0; i < ctx->nonce_len; i++)
        nonce[i] ^= ctx->base_nonce[i];
}

/* ── AEAD operations ────────────────────────────────────────── */

static int aead_seal(const neverc_hpke_ctx_t *ctx,
                     const uint8_t *nonce,
                     const uint8_t *aad, size_t aad_len,
                     const uint8_t *pt, size_t pt_len,
                     uint8_t *ct) {
    if (!ctx || (!nonce && ctx->nonce_len > 0) ||
        (!aad && aad_len > 0) || (!pt && pt_len > 0) || !ct ||
        pt_len > (size_t)INT_MAX - 16U)
        return -1;
    switch (ctx->aead_id) {
    case NEVERC_HPKE_AEAD_AES128GCM:
    case NEVERC_HPKE_AEAD_AES256GCM: {
        neverc_gcm_ctx gc;
        uint8_t tag[16] = {0};
        if (neverc_gcm_init(&gc, ctx->key, ctx->key_len) != 0) {
            neverc_platform_secure_zero(&gc, sizeof(gc));
            return -1;
        }
        if (neverc_gcm_seal(&gc, nonce, pt, pt_len, aad, aad_len, ct, tag) != 0) {
            neverc_platform_secure_zero(&gc, sizeof(gc));
            neverc_platform_secure_zero(tag, sizeof(tag));
            return -1;
        }
        memcpy(ct + pt_len, tag, 16);
        neverc_platform_secure_zero(&gc, sizeof(gc));
        neverc_platform_secure_zero(tag, sizeof(tag));
        return (int)(pt_len + 16);
    }
    case NEVERC_HPKE_AEAD_CHACHA20POLY1305: {
        size_t r = neverc_chacha20poly1305_seal(ct, ctx->key, nonce,
                                                pt, pt_len, aad, aad_len);
        if (r != pt_len + 16U)
            return -1;
        return (int)r;
    }
    default: return -1;
    }
}

static int aead_open(const neverc_hpke_ctx_t *ctx,
                     const uint8_t *nonce,
                     const uint8_t *aad, size_t aad_len,
                     const uint8_t *ct, size_t ct_len,
                     uint8_t *pt) {
    if (!ctx || (!nonce && ctx->nonce_len > 0) ||
        (!aad && aad_len > 0) || !ct ||
        (!pt && ct_len > 16U) || ct_len < 16 ||
        ct_len - 16U > (size_t)INT_MAX)
        return -1;
    switch (ctx->aead_id) {
    case NEVERC_HPKE_AEAD_AES128GCM:
    case NEVERC_HPKE_AEAD_AES256GCM: {
        neverc_gcm_ctx gc;
        if (neverc_gcm_init(&gc, ctx->key, ctx->key_len) != 0) {
            neverc_platform_secure_zero(&gc, sizeof(gc));
            return -1;
        }
        size_t pt_len_val = ct_len - 16;
        const uint8_t *tag = ct + pt_len_val;
        if (neverc_gcm_open(&gc, nonce, ct, pt_len_val, aad, aad_len, tag, pt) != 0) {
            neverc_platform_secure_zero(&gc, sizeof(gc));
            return -1;
        }
        neverc_platform_secure_zero(&gc, sizeof(gc));
        return (int)pt_len_val;
    }
    case NEVERC_HPKE_AEAD_CHACHA20POLY1305:
        return neverc_chacha20poly1305_open(pt, ctx->key, nonce,
                                           ct, ct_len, aad, aad_len);
    default: return -1;
    }
}

/* ── Export ──────────────────────────────────────────────────── */

static int hpke_export(const neverc_hpke_ctx_t *ctx,
                       const uint8_t *exporter_ctx, size_t ctx_len,
                       uint8_t *out, size_t out_len) {
    if (!ctx || (!exporter_ctx && ctx_len > 0) ||
        (!out && out_len > 0) || out_len > UINT16_MAX)
        return -1;
    uint8_t sid[10];
    make_hpke_suite_id(ctx->kem_id, ctx->kdf_id, ctx->aead_id, sid);
    return labeled_expand(ctx->kdf_id, sid, 10, ctx->exp_secret,
                          "sec", exporter_ctx, ctx_len,
                          (uint16_t)out_len, out);
}

/* ── Public API ─────────────────────────────────────────────── */

int neverc_hpke_sender_new(neverc_hpke_sender_t *s,
                           uint8_t *enc, size_t *enc_len,
                           uint16_t kem_id, uint16_t kdf_id, uint16_t aead_id,
                           const uint8_t *pubkey, size_t pubkey_len,
                           const uint8_t *info, size_t info_len) {
    if (enc_len) *enc_len = 0;
    if (!s || !enc || !enc_len || !pubkey || (!info && info_len > 0))
        return -1;
    memset(s, 0, sizeof(*s));
    uint8_t local_enc[NEVERC_HPKE_MAX_ENC_SIZE];
    size_t local_enc_len = 0;
    uint8_t shared_secret[64] = {0};
    if (kem_encap(kem_id, pubkey, pubkey_len, local_enc, &local_enc_len,
                  shared_secret) != 0) {
        neverc_platform_secure_zero(shared_secret, sizeof(shared_secret));
        return -1;
    }
    int ss_len = kem_secret_size(kem_id);
    int ret = key_schedule(&s->ctx, shared_secret, (size_t)ss_len,
                           kem_id, kdf_id, aead_id, info, info_len);
    neverc_platform_secure_zero(shared_secret, sizeof(shared_secret));
    if (ret != 0) {
        neverc_platform_secure_zero(s, sizeof(*s));
        return -1;
    }
    memcpy(enc, local_enc, local_enc_len);
    *enc_len = local_enc_len;
    return 0;
}

int neverc_hpke_recipient_new(neverc_hpke_recipient_t *r,
                              const uint8_t *enc, size_t enc_len,
                              uint16_t kem_id, uint16_t kdf_id, uint16_t aead_id,
                              const uint8_t *privkey, size_t privkey_len,
                              const uint8_t *info, size_t info_len) {
    if (!r || !enc || !privkey || (!info && info_len > 0))
        return -1;
    memset(r, 0, sizeof(*r));
    uint8_t shared_secret[64] = {0};
    if (kem_decap(kem_id, enc, enc_len, privkey, privkey_len, shared_secret) != 0) {
        neverc_platform_secure_zero(shared_secret, sizeof(shared_secret));
        return -1;
    }
    int ss_len = kem_secret_size(kem_id);
    int ret = key_schedule(&r->ctx, shared_secret, (size_t)ss_len,
                           kem_id, kdf_id, aead_id, info, info_len);
    neverc_platform_secure_zero(shared_secret, sizeof(shared_secret));
    if (ret != 0)
        neverc_platform_secure_zero(r, sizeof(*r));
    return ret;
}

int neverc_hpke_sender_seal(neverc_hpke_sender_t *s,
                            const uint8_t *aad, size_t aad_len,
                            const uint8_t *plaintext, size_t pt_len,
                            uint8_t *ciphertext) {
    if (!s || (!aad && aad_len > 0) || (!plaintext && pt_len > 0) ||
        !ciphertext || s->ctx.aead_id == NEVERC_HPKE_AEAD_EXPORT_ONLY ||
        s->ctx.nonce_len < 8 ||
        s->ctx.nonce_len > NEVERC_HPKE_MAX_NONCE_SIZE ||
        s->ctx.seq_num == UINT64_MAX)
        return -1;
    uint8_t nonce[12];
    compute_nonce(&s->ctx, nonce);
    int r = aead_seal(&s->ctx, nonce, aad, aad_len, plaintext, pt_len, ciphertext);
    neverc_platform_secure_zero(nonce, sizeof(nonce));
    if (r >= 0) s->ctx.seq_num++;
    return r;
}

int neverc_hpke_recipient_open(neverc_hpke_recipient_t *r,
                               const uint8_t *aad, size_t aad_len,
                               const uint8_t *ciphertext, size_t ct_len,
                               uint8_t *plaintext) {
    if (!r || (!aad && aad_len > 0) || !ciphertext ||
        (!plaintext && ct_len > 16U) ||
        r->ctx.aead_id == NEVERC_HPKE_AEAD_EXPORT_ONLY ||
        r->ctx.nonce_len < 8 ||
        r->ctx.nonce_len > NEVERC_HPKE_MAX_NONCE_SIZE ||
        r->ctx.seq_num == UINT64_MAX)
        return -1;
    uint8_t nonce[12];
    compute_nonce(&r->ctx, nonce);
    int ret = aead_open(&r->ctx, nonce, aad, aad_len, ciphertext, ct_len, plaintext);
    neverc_platform_secure_zero(nonce, sizeof(nonce));
    if (ret >= 0) r->ctx.seq_num++;
    return ret;
}

int neverc_hpke_sender_export(const neverc_hpke_sender_t *s,
                              const uint8_t *exporter_ctx, size_t ctx_len,
                              uint8_t *out, size_t out_len) {
    if (!s) return -1;
    return hpke_export(&s->ctx, exporter_ctx, ctx_len, out, out_len);
}

int neverc_hpke_recipient_export(const neverc_hpke_recipient_t *r,
                                 const uint8_t *exporter_ctx, size_t ctx_len,
                                 uint8_t *out, size_t out_len) {
    if (!r) return -1;
    return hpke_export(&r->ctx, exporter_ctx, ctx_len, out, out_len);
}

int neverc_hpke_seal(uint16_t kem_id, uint16_t kdf_id, uint16_t aead_id,
                     const uint8_t *pubkey, size_t pubkey_len,
                     const uint8_t *info, size_t info_len,
                     const uint8_t *plaintext, size_t pt_len,
                     uint8_t *output, size_t *output_len) {
    if (output_len) *output_len = 0;
    if (!pubkey || (!info && info_len > 0) ||
        (!plaintext && pt_len > 0) || !output || !output_len ||
        pt_len > (size_t)INT_MAX - 16U ||
        aead_id == NEVERC_HPKE_AEAD_EXPORT_ONLY)
        return -1;
    neverc_hpke_sender_t s;
    uint8_t enc[NEVERC_HPKE_MAX_ENC_SIZE];
    size_t enc_len;
    if (neverc_hpke_sender_new(&s, enc, &enc_len, kem_id, kdf_id, aead_id,
                               pubkey, pubkey_len, info, info_len) != 0)
        return -1;

    memcpy(output, enc, enc_len);
    int ct_len = neverc_hpke_sender_seal(&s, NULL, 0, plaintext, pt_len,
                                         output + enc_len);
    if (ct_len < 0) {
        neverc_platform_secure_zero(&s, sizeof(s));
        return -1;
    }
    *output_len = enc_len + (size_t)ct_len;
    neverc_platform_secure_zero(&s, sizeof(s));
    return 0;
}

int neverc_hpke_open(uint16_t kem_id, uint16_t kdf_id, uint16_t aead_id,
                     const uint8_t *privkey, size_t privkey_len,
                     const uint8_t *info, size_t info_len,
                     const uint8_t *ciphertext, size_t ct_len,
                     uint8_t *plaintext, size_t *pt_len) {
    if (pt_len) *pt_len = 0;
    int enc_sz = kem_enc_size(kem_id);
    if (!privkey || (!info && info_len > 0) || !ciphertext ||
        !pt_len || enc_sz < 0 ||
        aead_id == NEVERC_HPKE_AEAD_EXPORT_ONLY ||
        (!plaintext && ct_len > (size_t)enc_sz + 16U))
        return -1;
    if (ct_len < (size_t)enc_sz + 16U) return -1;

    const uint8_t *enc = ciphertext;
    const uint8_t *ct = ciphertext + enc_sz;
    size_t actual_ct_len = ct_len - (size_t)enc_sz;

    neverc_hpke_recipient_t r;
    if (neverc_hpke_recipient_new(&r, enc, (size_t)enc_sz,
                                  kem_id, kdf_id, aead_id,
                                  privkey, privkey_len, info, info_len) != 0)
        return -1;

    int ret = neverc_hpke_recipient_open(&r, NULL, 0, ct, actual_ct_len, plaintext);
    if (ret < 0) {
        neverc_platform_secure_zero(&r, sizeof(r));
        return -1;
    }
    *pt_len = (size_t)ret;
    neverc_platform_secure_zero(&r, sizeof(r));
    return 0;
}
