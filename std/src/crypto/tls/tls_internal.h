#ifndef NEVERC_STD_CRYPTO_TLS_INTERNAL_H
#define NEVERC_STD_CRYPTO_TLS_INTERNAL_H

/*
 * Shared TLS 1.3 internals used across the split crypto/tls translation
 * units. Mirrors the role of Go's crypto/tls/common.go for constants and
 * byte helpers that multiple handshake/record modules need.
 */

#include "neverc/std/crypto/gcm.h"
#include "neverc/std/crypto/tls.h"

#include <stddef.h>
#include <stdint.h>

#define TLS_RECORD_HEADER_SIZE   5
#define TLS_MAX_PLAINTEXT        16384
#define TLS_MAX_CIPHERTEXT       (TLS_MAX_PLAINTEXT + 256)
#define TLS_MAX_HANDSHAKE        65536
#define TLS_MAX_NON_ADVANCING_RECORDS 16
#define TLS_HASH_SIZE_SHA256     32
#define TLS_AEAD_TAG_SIZE        16

#define TLS_CT_CHANGE_CIPHER_SPEC  20
#define TLS_CT_ALERT               21
#define TLS_CT_HANDSHAKE           22
#define TLS_CT_APPLICATION_DATA    23

#define TLS_HS_CLIENT_HELLO        1
#define TLS_HS_SERVER_HELLO        2
#define TLS_HS_NEW_SESSION_TICKET  4
#define TLS_HS_ENCRYPTED_EXT       8
#define TLS_HS_CERTIFICATE        11
#define TLS_HS_CERTIFICATE_REQUEST 13
#define TLS_HS_CERT_VERIFY        15
#define TLS_HS_FINISHED           20
#define TLS_HS_KEY_UPDATE         24

#define TLS_EXT_SERVER_NAME        0
#define TLS_EXT_SUPPORTED_GROUPS  10
#define TLS_EXT_SIGNATURE_ALGORITHMS 13
#define TLS_EXT_ALPN              16
#define TLS_EXT_PRE_SHARED_KEY    41
#define TLS_EXT_EARLY_DATA        42
#define TLS_EXT_SUPPORTED_VERSIONS 43
#define TLS_EXT_PSK_KEY_EXCHANGE_MODES 45
#define TLS_EXT_KEY_SHARE         51

#define TLS_PSK_MODE_DHE           1
#define TLS_MAX_PSK_IDENTITIES     4
#define TLS_MAX_SESSION_TICKET  2048
#define TLS_SERVER_TICKET_COUNT    8
#define TLS_SERVER_TICKET_SIZE    32
#define TLS_TICKET_NONCE_SIZE      8
#define TLS_TICKET_LIFETIME     3600
#define TLS_TICKET_AGE_SKEW_MS 10000
#define TLS_MAX_ALPN_PROTOCOLS     32
#define TLS_MAX_ALPN_LIST        2048
#define TLS_MAX_SERVER_NAME       255

#define TLS_SIG_ECDSA_SHA256 \
    NEVERC_TLS_SIGNATURE_ECDSA_SECP256R1_SHA256
#define TLS_SIG_RSA_PSS_SHA256 \
    NEVERC_TLS_SIGNATURE_RSA_PSS_RSAE_SHA256
#define TLS_SIG_RSA_PSS_SHA384 \
    NEVERC_TLS_SIGNATURE_RSA_PSS_RSAE_SHA384
#define TLS_SIG_RSA_PSS_SHA512 \
    NEVERC_TLS_SIGNATURE_RSA_PSS_RSAE_SHA512
#define TLS_SIG_ED25519 NEVERC_TLS_SIGNATURE_ED25519

#define TLS_ALERT_CLOSE_NOTIFY         0
#define TLS_ALERT_UNEXPECTED_MESSAGE  10
#define TLS_ALERT_BAD_RECORD_MAC      20
#define TLS_ALERT_RECORD_OVERFLOW     22
#define TLS_ALERT_HANDSHAKE_FAILURE   40
#define TLS_ALERT_BAD_CERTIFICATE     42
#define TLS_ALERT_ILLEGAL_PARAMETER   47
#define TLS_ALERT_DECODE_ERROR        50
#define TLS_ALERT_DECRYPT_ERROR       51
#define TLS_ALERT_PROTOCOL_VERSION    70
#define TLS_ALERT_INTERNAL_ERROR      80
#define TLS_ALERT_USER_CANCELED       90
#define TLS_ALERT_MISSING_EXTENSION  109
#define TLS_ALERT_UNSUPPORTED_EXTENSION 110
#define TLS_ALERT_CERTIFICATE_REQUIRED 116
#define TLS_ALERT_NO_APPLICATION_PROTOCOL 120

#define TLS_LEGACY_VERSION  0x0303

typedef enum {
    TLS_CIPHER_AES_128_GCM_SHA256,
    TLS_CIPHER_CHACHA20_POLY1305_SHA256,
} tls_cipher_id_t;

typedef struct {
    tls_cipher_id_t id;
    uint8_t key[32];
    uint8_t iv[12];
    uint64_t seq;
    neverc_gcm_ctx gcm;
} tls_traffic_keys_t;

static inline void tls_put_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v);
}

static inline void tls_put_u24(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 16);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v);
}

static inline uint16_t tls_get_u16(const uint8_t *p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

static inline uint32_t tls_get_u24(const uint8_t *p) {
    return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
}

static inline void tls_put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static inline uint32_t tls_get_u32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}

#endif /* NEVERC_STD_CRYPTO_TLS_INTERNAL_H */
