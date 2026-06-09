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
    size_t lbl_len = strlen(label);
    size_t labeled_len = 7 + sid_len + lbl_len + ikm_len;
    uint8_t labeled[512];
    if (labeled_len > sizeof(labeled)) return -1;

    size_t off = 0;
    memcpy(labeled + off, "HPKE-v1", 7); off += 7;
    memcpy(labeled + off, suite_id, sid_len); off += sid_len;
    memcpy(labeled + off, label, lbl_len); off += lbl_len;
    if (ikm && ikm_len) { memcpy(labeled + off, ikm, ikm_len); off += ikm_len; }

    if (kdf_id == NEVERC_HPKE_KDF_SHA256)
        return neverc_hkdf_extract_sha256(prk, salt, salt_len, labeled, off);
    else if (kdf_id == NEVERC_HPKE_KDF_SHA512)
        return neverc_hkdf_extract_sha512(prk, salt, salt_len, labeled, off);
    return -1;
}

static int labeled_expand(uint16_t kdf_id,
                          const uint8_t *suite_id, size_t sid_len,
                          const uint8_t *prk,
                          const char *label,
                          const uint8_t *info, size_t info_len,
                          uint16_t length,
                          uint8_t *out) {
    size_t lbl_len = strlen(label);
    size_t labeled_len = 2 + 7 + sid_len + lbl_len + info_len;
    uint8_t labeled[512];
    if (labeled_len > sizeof(labeled)) return -1;

    size_t off = 0;
    be16(labeled + off, length); off += 2;
    memcpy(labeled + off, "HPKE-v1", 7); off += 7;
    memcpy(labeled + off, suite_id, sid_len); off += sid_len;
    memcpy(labeled + off, label, lbl_len); off += lbl_len;
    if (info && info_len) { memcpy(labeled + off, info, info_len); off += info_len; }

    int prk_len = kdf_hash_size(kdf_id);
    if (kdf_id == NEVERC_HPKE_KDF_SHA256)
        return neverc_hkdf_expand_sha256(out, length, prk, labeled, off);
    else if (kdf_id == NEVERC_HPKE_KDF_SHA512)
        return neverc_hkdf_expand_sha512(out, length, prk, labeled, off);
    (void)prk_len;
    return -1;
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

    uint8_t prk[64];
    if (labeled_extract(NEVERC_HPKE_KDF_SHA256, kem_sid, 5,
                        NULL, 0, "shared_secret", dh, dh_len, prk) != 0)
        return -1;

    int ss_len = kem_secret_size(kem_id);
    return labeled_expand(NEVERC_HPKE_KDF_SHA256, kem_sid, 5,
                          prk, "shared_secret", kem_ctx, ctx_len,
                          (uint16_t)ss_len, shared_secret);
}

/* Encapsulate: generate ephemeral key pair, compute shared secret.
 * enc: public key of ephemeral pair
 * shared_secret: derived shared secret */
static int kem_encap(uint16_t kem_id,
                     const uint8_t *pk, size_t pk_len,
                     uint8_t *enc, size_t *enc_len,
                     uint8_t *shared_secret) {
    neverc_ecdh_curve_t curve = kem_to_curve(kem_id);
    neverc_ecdh_key_t ek;
    if (neverc_ecdh_generate_key(curve, &ek) != 0)
        return -1;

    int pub_len = neverc_ecdh_public_key_bytes(&ek, enc, NEVERC_HPKE_MAX_ENC_SIZE);
    if (pub_len < 0) return -1;
    *enc_len = (size_t)pub_len;

    uint8_t dh[48];
    int dh_len = neverc_ecdh_compute(&ek, pk, pk_len, dh, sizeof(dh));
    if (dh_len < 0) return -1;

    uint8_t kem_ctx[130];
    memcpy(kem_ctx, enc, *enc_len);
    memcpy(kem_ctx + *enc_len, pk, pk_len);
    size_t ctx_len = *enc_len + pk_len;

    return extract_and_expand(kem_id, dh, (size_t)dh_len,
                              kem_ctx, ctx_len, shared_secret);
}

/* Decapsulate: compute shared secret from enc + private key. */
static int kem_decap(uint16_t kem_id,
                     const uint8_t *enc, size_t enc_len,
                     const uint8_t *sk, size_t sk_len,
                     uint8_t *shared_secret) {
    neverc_ecdh_curve_t curve = kem_to_curve(kem_id);
    neverc_ecdh_key_t dk;
    if (neverc_ecdh_new_private_key(curve, sk, sk_len, &dk) != 0)
        return -1;

    uint8_t dh[48];
    int dh_len = neverc_ecdh_compute(&dk, enc, enc_len, dh, sizeof(dh));
    if (dh_len < 0) return -1;

    uint8_t pk[97];
    int pk_len = neverc_ecdh_public_key_bytes(&dk, pk, sizeof(pk));
    if (pk_len < 0) return -1;

    uint8_t kem_ctx[162];
    memcpy(kem_ctx, enc, enc_len);
    memcpy(kem_ctx + enc_len, pk, (size_t)pk_len);
    size_t ctx_len = enc_len + (size_t)pk_len;

    return extract_and_expand(kem_id, dh, (size_t)dh_len,
                              kem_ctx, ctx_len, shared_secret);
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
    ctx->kem_id = kem_id;
    ctx->kdf_id = kdf_id;
    ctx->aead_id = aead_id;
    ctx->seq_num = 0;

    uint8_t sid[10];
    make_hpke_suite_id(kem_id, kdf_id, aead_id, sid);

    int nh = kdf_hash_size(kdf_id);
    if (nh < 0) return -1;

    uint8_t psk_id_hash[64];
    if (labeled_extract(kdf_id, sid, 10, NULL, 0,
                        "psk_id_hash", NULL, 0, psk_id_hash) != 0)
        return -1;

    uint8_t info_hash[64];
    if (labeled_extract(kdf_id, sid, 10, NULL, 0,
                        "info_hash", info, info_len, info_hash) != 0)
        return -1;

    uint8_t ks_context[1 + 64 + 64];
    ks_context[0] = 0; /* mode = Base */
    memcpy(ks_context + 1, psk_id_hash, (size_t)nh);
    memcpy(ks_context + 1 + nh, info_hash, (size_t)nh);
    size_t ks_ctx_len = 1 + (size_t)nh * 2;

    uint8_t secret[64];
    if (labeled_extract(kdf_id, sid, 10, shared_secret, ss_len,
                        "secret", NULL, 0, secret) != 0)
        return -1;

    int nk = aead_key_size(aead_id);
    int nn = aead_nonce_size(aead_id);
    if (nk < 0 || nn < 0) return -1;

    ctx->key_len = nk;
    ctx->nonce_len = nn;

    if (nk > 0) {
        if (labeled_expand(kdf_id, sid, 10, secret, "key",
                           ks_context, ks_ctx_len, (uint16_t)nk, ctx->key) != 0)
            return -1;
    }
    if (nn > 0) {
        if (labeled_expand(kdf_id, sid, 10, secret, "base_nonce",
                           ks_context, ks_ctx_len, (uint16_t)nn, ctx->base_nonce) != 0)
            return -1;
    }

    ctx->exp_len = nh;
    if (labeled_expand(kdf_id, sid, 10, secret, "exp",
                       ks_context, ks_ctx_len, (uint16_t)nh, ctx->exp_secret) != 0)
        return -1;

    return 0;
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
    switch (ctx->aead_id) {
    case NEVERC_HPKE_AEAD_AES128GCM:
    case NEVERC_HPKE_AEAD_AES256GCM: {
        neverc_gcm_ctx gc;
        if (neverc_gcm_init(&gc, ctx->key, ctx->key_len) != 0) return -1;
        uint8_t tag[16];
        if (neverc_gcm_seal(&gc, nonce, pt, pt_len, aad, aad_len, ct, tag) != 0)
            return -1;
        memcpy(ct + pt_len, tag, 16);
        return (int)(pt_len + 16);
    }
    case NEVERC_HPKE_AEAD_CHACHA20POLY1305: {
        size_t r = neverc_chacha20poly1305_seal(ct, ctx->key, nonce,
                                                pt, pt_len, aad, aad_len);
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
    if (ct_len < 16) return -1;
    switch (ctx->aead_id) {
    case NEVERC_HPKE_AEAD_AES128GCM:
    case NEVERC_HPKE_AEAD_AES256GCM: {
        neverc_gcm_ctx gc;
        if (neverc_gcm_init(&gc, ctx->key, ctx->key_len) != 0) return -1;
        size_t pt_len_val = ct_len - 16;
        const uint8_t *tag = ct + pt_len_val;
        if (neverc_gcm_open(&gc, nonce, ct, pt_len_val, aad, aad_len, tag, pt) != 0)
            return -1;
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
    uint8_t shared_secret[64];
    if (kem_encap(kem_id, pubkey, pubkey_len, enc, enc_len, shared_secret) != 0)
        return -1;
    int ss_len = kem_secret_size(kem_id);
    return key_schedule(&s->ctx, shared_secret, (size_t)ss_len,
                        kem_id, kdf_id, aead_id, info, info_len);
}

int neverc_hpke_recipient_new(neverc_hpke_recipient_t *r,
                              const uint8_t *enc, size_t enc_len,
                              uint16_t kem_id, uint16_t kdf_id, uint16_t aead_id,
                              const uint8_t *privkey, size_t privkey_len,
                              const uint8_t *info, size_t info_len) {
    uint8_t shared_secret[64];
    if (kem_decap(kem_id, enc, enc_len, privkey, privkey_len, shared_secret) != 0)
        return -1;
    int ss_len = kem_secret_size(kem_id);
    return key_schedule(&r->ctx, shared_secret, (size_t)ss_len,
                        kem_id, kdf_id, aead_id, info, info_len);
}

int neverc_hpke_sender_seal(neverc_hpke_sender_t *s,
                            const uint8_t *aad, size_t aad_len,
                            const uint8_t *plaintext, size_t pt_len,
                            uint8_t *ciphertext) {
    if (s->ctx.aead_id == NEVERC_HPKE_AEAD_EXPORT_ONLY) return -1;
    uint8_t nonce[12];
    compute_nonce(&s->ctx, nonce);
    int r = aead_seal(&s->ctx, nonce, aad, aad_len, plaintext, pt_len, ciphertext);
    if (r >= 0) s->ctx.seq_num++;
    return r;
}

int neverc_hpke_recipient_open(neverc_hpke_recipient_t *r,
                               const uint8_t *aad, size_t aad_len,
                               const uint8_t *ciphertext, size_t ct_len,
                               uint8_t *plaintext) {
    if (r->ctx.aead_id == NEVERC_HPKE_AEAD_EXPORT_ONLY) return -1;
    uint8_t nonce[12];
    compute_nonce(&r->ctx, nonce);
    int ret = aead_open(&r->ctx, nonce, aad, aad_len, ciphertext, ct_len, plaintext);
    if (ret >= 0) r->ctx.seq_num++;
    return ret;
}

int neverc_hpke_sender_export(const neverc_hpke_sender_t *s,
                              const uint8_t *exporter_ctx, size_t ctx_len,
                              uint8_t *out, size_t out_len) {
    return hpke_export(&s->ctx, exporter_ctx, ctx_len, out, out_len);
}

int neverc_hpke_recipient_export(const neverc_hpke_recipient_t *r,
                                 const uint8_t *exporter_ctx, size_t ctx_len,
                                 uint8_t *out, size_t out_len) {
    return hpke_export(&r->ctx, exporter_ctx, ctx_len, out, out_len);
}

int neverc_hpke_seal(uint16_t kem_id, uint16_t kdf_id, uint16_t aead_id,
                     const uint8_t *pubkey, size_t pubkey_len,
                     const uint8_t *info, size_t info_len,
                     const uint8_t *plaintext, size_t pt_len,
                     uint8_t *output, size_t *output_len) {
    neverc_hpke_sender_t s;
    uint8_t enc[NEVERC_HPKE_MAX_ENC_SIZE];
    size_t enc_len;
    if (neverc_hpke_sender_new(&s, enc, &enc_len, kem_id, kdf_id, aead_id,
                               pubkey, pubkey_len, info, info_len) != 0)
        return -1;

    memcpy(output, enc, enc_len);
    int ct_len = neverc_hpke_sender_seal(&s, NULL, 0, plaintext, pt_len,
                                         output + enc_len);
    if (ct_len < 0) return -1;
    *output_len = enc_len + (size_t)ct_len;
    return 0;
}

int neverc_hpke_open(uint16_t kem_id, uint16_t kdf_id, uint16_t aead_id,
                     const uint8_t *privkey, size_t privkey_len,
                     const uint8_t *info, size_t info_len,
                     const uint8_t *ciphertext, size_t ct_len,
                     uint8_t *plaintext, size_t *pt_len) {
    int enc_sz = kem_enc_size(kem_id);
    if (enc_sz < 0 || ct_len < (size_t)enc_sz + 16) return -1;

    const uint8_t *enc = ciphertext;
    const uint8_t *ct = ciphertext + enc_sz;
    size_t actual_ct_len = ct_len - (size_t)enc_sz;

    neverc_hpke_recipient_t r;
    if (neverc_hpke_recipient_new(&r, enc, (size_t)enc_sz,
                                  kem_id, kdf_id, aead_id,
                                  privkey, privkey_len, info, info_len) != 0)
        return -1;

    int ret = neverc_hpke_recipient_open(&r, NULL, 0, ct, actual_ct_len, plaintext);
    if (ret < 0) return -1;
    *pt_len = (size_t)ret;
    return 0;
}
