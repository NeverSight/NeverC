#include "tls_key_schedule.h"

#include "neverc/std/_platform.h"
#include "neverc/std/crypto/hkdf.h"
#include "neverc/std/crypto/hmac.h"
#include "neverc/std/crypto/sha256.h"

#include <string.h>

int nci_tls_hkdf_expand_label(
    const uint8_t *secret, size_t secret_len,
    const char *label, size_t label_len,
    const uint8_t *context, size_t context_len,
    uint8_t *out, size_t out_len) {
    uint8_t info[512];
    if (!secret || secret_len != TLS_HASH_SIZE_SHA256 ||
        (!label && label_len != 0) || label_len > 249 ||
        (!context && context_len != 0) || context_len > 255 ||
        !out || out_len > 65535 ||
        2 + 1 + 6 + label_len + 1 + context_len > sizeof(info))
        return -1;
    size_t pos = 0;
    size_t full_label_len = 6 + label_len;

    info[pos++] = (uint8_t)(out_len >> 8);
    info[pos++] = (uint8_t)(out_len);
    info[pos++] = (uint8_t)(full_label_len);
    memcpy(info + pos, "tls13 ", 6);
    pos += 6;
    memcpy(info + pos, label, label_len);
    pos += label_len;
    info[pos++] = (uint8_t)(context_len);
    if (context_len > 0)
        memcpy(info + pos, context, context_len);
    pos += context_len;

    return neverc_hkdf_expand_sha256(out, out_len, secret, info, pos);
}

void nci_tls_derive_secret(
    const uint8_t *secret,
    const char *label, size_t label_len,
    const uint8_t *transcript_hash,
    uint8_t *out) {
    nci_tls_hkdf_expand_label(
        secret, TLS_HASH_SIZE_SHA256,
        label, label_len,
        transcript_hash, TLS_HASH_SIZE_SHA256,
        out, TLS_HASH_SIZE_SHA256);
}

int nci_tls_derive_resumption_psk(
    const uint8_t resumption_master_secret[32],
    const uint8_t *ticket_nonce, size_t ticket_nonce_len,
    uint8_t psk[32]) {
    return nci_tls_hkdf_expand_label(
        resumption_master_secret, TLS_HASH_SIZE_SHA256,
        "resumption", 10, ticket_nonce, ticket_nonce_len,
        psk, TLS_HASH_SIZE_SHA256);
}

int nci_tls_compute_resumption_binder(
    const uint8_t psk[32],
    const uint8_t *truncated_client_hello,
    size_t truncated_client_hello_len,
    uint8_t binder[32]) {
    uint8_t early_secret[32];
    uint8_t empty_hash[32];
    uint8_t binder_key[32];
    uint8_t finished_key[32];
    uint8_t transcript_hash[32];
    neverc_sha256_sum(NULL, 0, empty_hash);
    neverc_sha256_sum(truncated_client_hello,
                      truncated_client_hello_len,
                      transcript_hash);
    if (neverc_hkdf_extract_sha256(
            early_secret, NULL, 0, psk, 32) != 0 ||
        nci_tls_hkdf_expand_label(
            early_secret, 32, "res binder", 10,
            empty_hash, 32, binder_key, 32) != 0 ||
        nci_tls_hkdf_expand_label(
            binder_key, 32, "finished", 8,
            NULL, 0, finished_key, 32) != 0) {
        neverc_platform_secure_zero(
            early_secret, sizeof(early_secret));
        neverc_platform_secure_zero(
            binder_key, sizeof(binder_key));
        neverc_platform_secure_zero(
            finished_key, sizeof(finished_key));
        return -1;
    }
    neverc_hmac_sha256(finished_key, 32,
                       transcript_hash, 32, binder);
    neverc_platform_secure_zero(
        early_secret, sizeof(early_secret));
    neverc_platform_secure_zero(
        binder_key, sizeof(binder_key));
    neverc_platform_secure_zero(
        finished_key, sizeof(finished_key));
    return 0;
}

int nci_tls_hkdf_extract_zero_ikm(
    uint8_t out[TLS_HASH_SIZE_SHA256],
    const uint8_t *salt, size_t salt_len) {
    uint8_t zeros[TLS_HASH_SIZE_SHA256];
    memset(zeros, 0, sizeof(zeros));
    return neverc_hkdf_extract_sha256(
        out, salt, salt_len, zeros, sizeof(zeros));
}

int nci_tls_derive_handshake_secret(
    const uint8_t shared_secret[32],
    const uint8_t *psk,
    uint8_t handshake_secret[32]) {
    uint8_t early_secret[32];
    uint8_t empty_hash[32];
    uint8_t derived_secret[32];
    neverc_sha256_sum(NULL, 0, empty_hash);
    int result;
    if (psk) {
        result = neverc_hkdf_extract_sha256(
            early_secret, NULL, 0, psk, 32);
    } else {
        result = nci_tls_hkdf_extract_zero_ikm(
            early_secret, NULL, 0);
    }
    if (result == 0) {
        nci_tls_derive_secret(early_secret, "derived", 7,
                              empty_hash, derived_secret);
        result = neverc_hkdf_extract_sha256(
            handshake_secret, derived_secret, 32,
            shared_secret, 32);
    }
    neverc_platform_secure_zero(
        early_secret, sizeof(early_secret));
    neverc_platform_secure_zero(
        derived_secret, sizeof(derived_secret));
    return result;
}

void nci_tls_derive_traffic_keys(
    const uint8_t *traffic_secret,
    tls_traffic_keys_t *keys,
    tls_cipher_id_t cipher) {
    keys->id = cipher;
    keys->seq = 0;

    size_t key_len =
        (cipher == TLS_CIPHER_AES_128_GCM_SHA256) ? 16 : 32;
    nci_tls_hkdf_expand_label(
        traffic_secret, TLS_HASH_SIZE_SHA256,
        "key", 3, NULL, 0, keys->key, key_len);
    nci_tls_hkdf_expand_label(
        traffic_secret, TLS_HASH_SIZE_SHA256,
        "iv", 2, NULL, 0, keys->iv, 12);

    if (cipher == TLS_CIPHER_AES_128_GCM_SHA256)
        neverc_gcm_init(&keys->gcm, keys->key, 16);
}

int nci_tls_update_traffic_secret(
    uint8_t traffic_secret[TLS_HASH_SIZE_SHA256],
    tls_traffic_keys_t *keys) {
    uint8_t next_secret[TLS_HASH_SIZE_SHA256] = {0};
    if (nci_tls_hkdf_expand_label(
            traffic_secret, TLS_HASH_SIZE_SHA256,
            "traffic upd", 11, NULL, 0,
            next_secret, sizeof(next_secret)) != 0) {
        neverc_platform_secure_zero(
            next_secret, sizeof(next_secret));
        return -1;
    }

    memcpy(traffic_secret, next_secret, sizeof(next_secret));
    nci_tls_derive_traffic_keys(traffic_secret, keys, keys->id);
    neverc_platform_secure_zero(next_secret, sizeof(next_secret));
    return 0;
}
