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
    if (label_len > 0)
        memcpy(info + pos, label, label_len);
    pos += label_len;
    info[pos++] = (uint8_t)(context_len);
    if (context_len > 0)
        memcpy(info + pos, context, context_len);
    pos += context_len;

    int result =
        neverc_hkdf_expand_sha256(out, out_len, secret, info, pos);
    neverc_platform_secure_zero(info, sizeof(info));
    return result;
}

int nci_tls_derive_secret_checked(
    const uint8_t *secret,
    const char *label, size_t label_len,
    const uint8_t *transcript_hash,
    uint8_t *out) {
    int result = nci_tls_hkdf_expand_label(
        secret, TLS_HASH_SIZE_SHA256,
        label, label_len,
        transcript_hash, TLS_HASH_SIZE_SHA256,
        out, TLS_HASH_SIZE_SHA256);
    if (result != 0 && out)
        neverc_platform_secure_zero(out, TLS_HASH_SIZE_SHA256);
    return result;
}

int nci_tls_derive_resumption_psk(
    const uint8_t resumption_master_secret[32],
    const uint8_t *ticket_nonce, size_t ticket_nonce_len,
    uint8_t psk[32]) {
    int result = nci_tls_hkdf_expand_label(
        resumption_master_secret, TLS_HASH_SIZE_SHA256,
        "resumption", 10, ticket_nonce, ticket_nonce_len,
        psk, TLS_HASH_SIZE_SHA256);
    if (result != 0 && psk)
        neverc_platform_secure_zero(psk, TLS_HASH_SIZE_SHA256);
    return result;
}

int nci_tls_compute_resumption_binder(
    const uint8_t psk[32],
    const uint8_t *truncated_client_hello,
    size_t truncated_client_hello_len,
    uint8_t binder[32]) {
    if (!psk || (!truncated_client_hello &&
                 truncated_client_hello_len != 0) ||
        !binder) {
        if (binder)
            neverc_platform_secure_zero(binder, TLS_HASH_SIZE_SHA256);
        return -1;
    }
    uint8_t early_secret[32];
    uint8_t empty_hash[32];
    uint8_t binder_key[32];
    uint8_t finished_key[32];
    uint8_t transcript_hash[32];
    neverc_sha256_sum(NULL, 0, empty_hash);
    neverc_sha256_sum(truncated_client_hello,
                      truncated_client_hello_len,
                      transcript_hash);
    int result = 0;
    if (neverc_hkdf_extract_sha256(
            early_secret, NULL, 0, psk, 32) != 0 ||
        nci_tls_hkdf_expand_label(
            early_secret, 32, "res binder", 10,
            empty_hash, 32, binder_key, 32) != 0 ||
        nci_tls_hkdf_expand_label(
            binder_key, 32, "finished", 8,
            NULL, 0, finished_key, 32) != 0) {
        result = -1;
    } else {
        neverc_hmac_sha256(finished_key, 32,
                           transcript_hash, 32, binder);
    }
    if (result != 0)
        neverc_platform_secure_zero(binder, TLS_HASH_SIZE_SHA256);
    neverc_platform_secure_zero(
        early_secret, sizeof(early_secret));
    neverc_platform_secure_zero(
        binder_key, sizeof(binder_key));
    neverc_platform_secure_zero(
        finished_key, sizeof(finished_key));
    neverc_platform_secure_zero(
        transcript_hash, sizeof(transcript_hash));
    neverc_platform_secure_zero(empty_hash, sizeof(empty_hash));
    return result;
}

int nci_tls_hkdf_extract_zero_ikm(
    uint8_t out[TLS_HASH_SIZE_SHA256],
    const uint8_t *salt, size_t salt_len) {
    uint8_t zeros[TLS_HASH_SIZE_SHA256];
    memset(zeros, 0, sizeof(zeros));
    int result = neverc_hkdf_extract_sha256(
        out, salt, salt_len, zeros, sizeof(zeros));
    neverc_platform_secure_zero(zeros, sizeof(zeros));
    if (result != 0 && out)
        neverc_platform_secure_zero(out, TLS_HASH_SIZE_SHA256);
    return result;
}

int nci_tls_derive_handshake_secret(
    const uint8_t shared_secret[32],
    const uint8_t *psk,
    uint8_t handshake_secret[32]) {
    if (!shared_secret || !handshake_secret) {
        if (handshake_secret)
            neverc_platform_secure_zero(
                handshake_secret, TLS_HASH_SIZE_SHA256);
        return -1;
    }
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
        result = nci_tls_derive_secret_checked(
            early_secret, "derived", 7, empty_hash, derived_secret);
        if (result == 0)
            result = neverc_hkdf_extract_sha256(
                handshake_secret, derived_secret, 32,
                shared_secret, 32);
    }
    neverc_platform_secure_zero(
        early_secret, sizeof(early_secret));
    neverc_platform_secure_zero(
        derived_secret, sizeof(derived_secret));
    neverc_platform_secure_zero(empty_hash, sizeof(empty_hash));
    if (result != 0)
        neverc_platform_secure_zero(
            handshake_secret, TLS_HASH_SIZE_SHA256);
    return result;
}

int nci_tls_derive_traffic_keys_checked(
    const uint8_t *traffic_secret,
    tls_traffic_keys_t *keys,
    tls_cipher_id_t cipher) {
    if (!traffic_secret || !keys ||
        (cipher != TLS_CIPHER_AES_128_GCM_SHA256 &&
         cipher != TLS_CIPHER_CHACHA20_POLY1305_SHA256))
        return -1;

    tls_traffic_keys_t derived;
    memset(&derived, 0, sizeof(derived));
    derived.id = cipher;
    size_t key_len =
        (cipher == TLS_CIPHER_AES_128_GCM_SHA256) ? 16 : 32;
    int result = nci_tls_hkdf_expand_label(
        traffic_secret, TLS_HASH_SIZE_SHA256,
        "key", 3, NULL, 0, derived.key, key_len);
    if (result == 0)
        result = nci_tls_hkdf_expand_label(
            traffic_secret, TLS_HASH_SIZE_SHA256,
            "iv", 2, NULL, 0, derived.iv, 12);
    if (result == 0 && cipher == TLS_CIPHER_AES_128_GCM_SHA256)
        result = neverc_gcm_init(&derived.gcm, derived.key, 16);
    if (result == 0) {
        neverc_platform_secure_zero(keys, sizeof(*keys));
        memcpy(keys, &derived, sizeof(derived));
    }
    neverc_platform_secure_zero(&derived, sizeof(derived));
    return result;
}

int nci_tls_update_traffic_secret(
    uint8_t traffic_secret[TLS_HASH_SIZE_SHA256],
    tls_traffic_keys_t *keys) {
    if (!traffic_secret || !keys)
        return -1;
    uint8_t next_secret[TLS_HASH_SIZE_SHA256] = {0};
    if (nci_tls_hkdf_expand_label(
            traffic_secret, TLS_HASH_SIZE_SHA256,
            "traffic upd", 11, NULL, 0,
            next_secret, sizeof(next_secret)) != 0) {
        neverc_platform_secure_zero(
            next_secret, sizeof(next_secret));
        return -1;
    }

    if (nci_tls_derive_traffic_keys_checked(
            next_secret, keys, keys->id) != 0) {
        neverc_platform_secure_zero(next_secret, sizeof(next_secret));
        return -1;
    }
    memcpy(traffic_secret, next_secret, sizeof(next_secret));
    neverc_platform_secure_zero(next_secret, sizeof(next_secret));
    return 0;
}

#if defined(NEVERC_TLS_TESTING)
static int nci_tls_test_bytes_are_zero(
    const uint8_t *data, size_t length) {
    for (size_t i = 0; i < length; i++) {
        if (data[i] != 0)
            return 0;
    }
    return 1;
}

int neverc_tls_test_key_schedule_failures(void) {
    uint8_t secret[TLS_HASH_SIZE_SHA256] = {0};
    uint8_t output[TLS_HASH_SIZE_SHA256];
    memset(output, 0xA5, sizeof(output));
    if (nci_tls_derive_secret_checked(
            NULL, "derived", 7, secret, output) != -1 ||
        !nci_tls_test_bytes_are_zero(output, sizeof(output)))
        return -1;

    memset(output, 0xA5, sizeof(output));
    if (nci_tls_compute_resumption_binder(
            secret, NULL, 1, output) != -1 ||
        !nci_tls_test_bytes_are_zero(output, sizeof(output)))
        return -1;

    memset(output, 0xA5, sizeof(output));
    if (nci_tls_derive_handshake_secret(
            NULL, NULL, output) != -1 ||
        !nci_tls_test_bytes_are_zero(output, sizeof(output)))
        return -1;

    memset(output, 0xA5, sizeof(output));
    if (nci_tls_hkdf_extract_zero_ikm(
            output, NULL, 1) != -1 ||
        !nci_tls_test_bytes_are_zero(output, sizeof(output)))
        return -1;

    tls_traffic_keys_t keys;
    tls_traffic_keys_t original_keys;
    memset(&keys, 0xA5, sizeof(keys));
    keys.id = (tls_cipher_id_t)99;
    memcpy(&original_keys, &keys, sizeof(keys));
    if (nci_tls_derive_traffic_keys_checked(
            secret, &keys, (tls_cipher_id_t)99) != -1 ||
        memcmp(&keys, &original_keys, sizeof(keys)) != 0)
        return -1;

    uint8_t original_secret[TLS_HASH_SIZE_SHA256];
    memcpy(original_secret, secret, sizeof(secret));
    if (nci_tls_update_traffic_secret(secret, &keys) != -1 ||
        memcmp(secret, original_secret, sizeof(secret)) != 0 ||
        memcmp(&keys, &original_keys, sizeof(keys)) != 0)
        return -1;

    neverc_platform_secure_zero(&keys, sizeof(keys));
    neverc_platform_secure_zero(&original_keys, sizeof(original_keys));
    neverc_platform_secure_zero(secret, sizeof(secret));
    neverc_platform_secure_zero(original_secret, sizeof(original_secret));
    return 0;
}
#endif
