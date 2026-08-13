#ifndef NEVERC_STD_CRYPTO_TLS_KEY_SCHEDULE_H
#define NEVERC_STD_CRYPTO_TLS_KEY_SCHEDULE_H

/*
 * TLS 1.3 key schedule (RFC 8446 §7), split out like Go's key_schedule.go.
 */

#include "tls_internal.h"

#include <stddef.h>
#include <stdint.h>

int nci_tls_hkdf_expand_label(
    const uint8_t *secret, size_t secret_len,
    const char *label, size_t label_len,
    const uint8_t *context, size_t context_len,
    uint8_t *out, size_t out_len);

int nci_tls_derive_secret_checked(
    const uint8_t *secret,
    const char *label, size_t label_len,
    const uint8_t *transcript_hash,
    uint8_t *out);

int nci_tls_derive_resumption_psk(
    const uint8_t resumption_master_secret[32],
    const uint8_t *ticket_nonce, size_t ticket_nonce_len,
    uint8_t psk[32]);

int nci_tls_compute_resumption_binder(
    const uint8_t psk[32],
    const uint8_t *truncated_client_hello,
    size_t truncated_client_hello_len,
    uint8_t binder[32]);

int nci_tls_hkdf_extract_zero_ikm(
    uint8_t out[TLS_HASH_SIZE_SHA256],
    const uint8_t *salt, size_t salt_len);

int nci_tls_derive_handshake_secret(
    const uint8_t shared_secret[32],
    const uint8_t *psk,
    uint8_t handshake_secret[32]);

int nci_tls_derive_traffic_keys_checked(
    const uint8_t *traffic_secret,
    tls_traffic_keys_t *keys,
    tls_cipher_id_t cipher);

int nci_tls_update_traffic_secret(
    uint8_t traffic_secret[TLS_HASH_SIZE_SHA256],
    tls_traffic_keys_t *keys);

#endif /* NEVERC_STD_CRYPTO_TLS_KEY_SCHEDULE_H */
