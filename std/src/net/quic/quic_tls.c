/* TLS 1.3 handshake carried in QUIC CRYPTO frames (RFC 9001 section 4). */

#include "_quic_internal.h"

#include "../../crypto/tls/tls_internal.h"
#include "../../crypto/tls/tls_key_schedule.h"
#include "neverc/std/_platform.h"
#include "neverc/std/crypto/ecdh.h"
#include "neverc/std/crypto/hmac.h"
#include "neverc/std/crypto/rand.h"
#include "neverc/std/crypto/sha256.h"
#include "neverc/std/crypto/subtle.h"
#include "neverc/std/crypto/tls.h"
#include "neverc/std/crypto/x509.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define QT_CRYPTO_INITIAL_CAP 4096U
#define QT_CRYPTO_LIMIT (1024U * 1024U)
#define QT_HANDSHAKE_LIMIT 65536U
#define QT_EXT_QUIC_TRANSPORT_PARAMETERS 0x0039U

typedef struct {
    uint8_t *data;
    uint8_t *present;
    size_t len;
    size_t cap;
    uint64_t contiguous;
    uint64_t processed;
    uint64_t write_offset;
    uint64_t acked_offset;
} quic_crypto_buf_t;

typedef struct {
    quic_keys_t read;
    quic_keys_t write;
    int available;
} quic_level_keys_t;

typedef enum {
    QT_PHASE_IDLE = 0,
    QT_PHASE_CLIENT_WAIT_SERVER_HELLO,
    QT_PHASE_CLIENT_WAIT_ENCRYPTED_EXTENSIONS,
    QT_PHASE_CLIENT_WAIT_CERTIFICATE,
    QT_PHASE_CLIENT_WAIT_CERTIFICATE_VERIFY,
    QT_PHASE_CLIENT_WAIT_FINISHED,
    QT_PHASE_SERVER_WAIT_CLIENT_HELLO,
    QT_PHASE_SERVER_WAIT_FINISHED,
    QT_PHASE_ESTABLISHED,
    QT_PHASE_ERROR,
} quic_tls_phase_t;

typedef struct {
    const uint8_t *data;
    size_t len;
    size_t pos;
} qt_cursor_t;

struct quic_tls {
    quic_tls_phase_t phase;
    int is_server;
    quic_level_keys_t levels[QUIC_ENC_LEVEL_COUNT];
    quic_crypto_buf_t crypto_send[QUIC_ENC_LEVEL_COUNT];
    quic_crypto_buf_t crypto_recv[QUIC_ENC_LEVEL_COUNT];
    neverc_tls_config_t *config;
    const quic_transport_params_t *local_params;
    quic_transport_params_t *peer_params;
    neverc_ecdh_key_t ecdh_key;
    int ecdh_ready;
    uint8_t session_id[32];
    size_t session_id_len;
    neverc_sha256_ctx transcript;
    uint8_t handshake_secret[32];
    uint8_t client_hs_secret[32];
    uint8_t server_hs_secret[32];
    uint8_t client_app_secret[32];
    uint8_t server_app_secret[32];
    int app_keys_derived;
    uint8_t *peer_cert_der;
    size_t peer_cert_len;
    neverc_x509_cert_pool_t *peer_intermediates;
    neverc_x509_cert_t peer_cert;
    int peer_cert_parsed;
    char alpn[32];
    int key_phase;
    int read_key_phase;
    uint8_t pending_read_secret[32];
    int pending_read_secret_valid;
    int handshake_complete;
    char error_reason[256];
    uint32_t version;
};

static void qt_put_u16(uint8_t *output, uint16_t value) {
    output[0] = (uint8_t)(value >> 8);
    output[1] = (uint8_t)value;
}

static void qt_put_u24(uint8_t *output, uint32_t value) {
    output[0] = (uint8_t)(value >> 16);
    output[1] = (uint8_t)(value >> 8);
    output[2] = (uint8_t)value;
}

static uint16_t qt_get_u16(const uint8_t *input) {
    return (uint16_t)(((uint16_t)input[0] << 8) | input[1]);
}

static uint32_t qt_get_u24(const uint8_t *input) {
    return ((uint32_t)input[0] << 16) | ((uint32_t)input[1] << 8) |
           input[2];
}

static int qt_fail(quic_tls_t *tls, const char *reason) {
    if (!tls) return -1;
    tls->phase = QT_PHASE_ERROR;
    if (reason) {
        size_t length = strlen(reason);
        if (length >= sizeof(tls->error_reason))
            length = sizeof(tls->error_reason) - 1;
        memcpy(tls->error_reason, reason, length);
        tls->error_reason[length] = '\0';
    }
    return -1;
}

static int qt_add_size(size_t left, size_t right, size_t *result) {
    if (!result || left > SIZE_MAX - right) return -1;
    *result = left + right;
    return 0;
}

static int qt_crypto_reserve(quic_crypto_buf_t *buffer, size_t needed,
                             int receive) {
    if (!buffer || needed > QT_CRYPTO_LIMIT) return -1;
    if (needed <= buffer->cap) return 0;
    size_t next = buffer->cap ? buffer->cap : QT_CRYPTO_INITIAL_CAP;
    while (next < needed) {
        if (next > QT_CRYPTO_LIMIT / 2U) {
            next = QT_CRYPTO_LIMIT;
            break;
        }
        next *= 2U;
    }
    if (receive) {
        uint8_t *grown = (uint8_t *)malloc(next);
        uint8_t *presence = (uint8_t *)malloc(next);
        if (!grown || !presence) { free(grown); free(presence); return -1; }
        if (buffer->cap) {
            memcpy(grown, buffer->data, buffer->cap);
            memcpy(presence, buffer->present, buffer->cap);
        }
        memset(grown + buffer->cap, 0, next - buffer->cap);
        memset(presence + buffer->cap, 0, next - buffer->cap);
        if (buffer->data)
            neverc_platform_secure_zero(buffer->data, buffer->cap);
        free(buffer->data);
        free(buffer->present);
        buffer->data = grown;
        buffer->present = presence;
    } else {
        uint8_t *grown = (uint8_t *)realloc(buffer->data, next);
        if (!grown) return -1;
        memset(grown + buffer->cap, 0, next - buffer->cap);
        buffer->data = grown;
    }
    buffer->cap = next;
    return 0;
}

static int qt_crypto_append(quic_crypto_buf_t *buffer,
                            const uint8_t *data, size_t length) {
    size_t needed;
    if (!buffer || (!data && length != 0) ||
        qt_add_size(buffer->len, length, &needed) != 0 ||
        qt_crypto_reserve(buffer, needed, 0) != 0)
        return -1;
    if (length) memcpy(buffer->data + buffer->len, data, length);
    buffer->len = needed;
    return 0;
}

static int qt_crypto_receive(quic_crypto_buf_t *buffer, uint64_t offset,
                             const uint8_t *data, size_t length) {
    if (!buffer || (!data && length != 0) || offset > SIZE_MAX ||
        length > SIZE_MAX - (size_t)offset)
        return -1;
    size_t start = (size_t)offset;
    size_t end = start + length;
    if (qt_crypto_reserve(buffer, end, 1) != 0) return -1;
    for (size_t i = 0; i < length; i++) {
        size_t position = start + i;
        if (buffer->present[position]) {
            if (buffer->data[position] != data[i]) return -1;
        } else {
            buffer->data[position] = data[i];
            buffer->present[position] = 1;
        }
    }
    if (end > buffer->len) buffer->len = end;
    while (buffer->contiguous < buffer->len &&
           buffer->present[buffer->contiguous])
        buffer->contiguous++;
    return 0;
}

static void qt_crypto_free(quic_crypto_buf_t *buffer) {
    if (!buffer) return;
    if (buffer->data) neverc_platform_secure_zero(buffer->data, buffer->cap);
    free(buffer->data);
    free(buffer->present);
    memset(buffer, 0, sizeof(*buffer));
}

static int qt_read_bytes(qt_cursor_t *cursor, size_t length,
                         const uint8_t **output) {
    if (!cursor || !output || cursor->pos > cursor->len ||
        length > cursor->len - cursor->pos) return -1;
    *output = cursor->data + cursor->pos;
    cursor->pos += length;
    return 0;
}

static int qt_read_u8(qt_cursor_t *cursor, uint8_t *value) {
    const uint8_t *bytes;
    if (!value || qt_read_bytes(cursor, 1, &bytes) != 0) return -1;
    *value = bytes[0];
    return 0;
}

static int qt_read_u16(qt_cursor_t *cursor, uint16_t *value) {
    const uint8_t *bytes;
    if (!value || qt_read_bytes(cursor, 2, &bytes) != 0) return -1;
    *value = qt_get_u16(bytes);
    return 0;
}

static int qt_read_vector8(qt_cursor_t *cursor, qt_cursor_t *vector) {
    uint8_t length;
    const uint8_t *data;
    if (!vector || qt_read_u8(cursor, &length) != 0 ||
        qt_read_bytes(cursor, length, &data) != 0)
        return -1;
    vector->data = data;
    vector->len = length;
    vector->pos = 0;
    return 0;
}

static int qt_read_vector16(qt_cursor_t *cursor, qt_cursor_t *vector) {
    uint16_t length;
    const uint8_t *data;
    if (!vector || qt_read_u16(cursor, &length) != 0 ||
        qt_read_bytes(cursor, length, &data) != 0)
        return -1;
    vector->data = data;
    vector->len = length;
    vector->pos = 0;
    return 0;
}

static void qt_transcript_hash(const quic_tls_t *tls, uint8_t output[32]) {
    neverc_sha256_ctx copy = tls->transcript;
    neverc_sha256_final(&copy, output);
}

static int qt_derive_packet_keys(const quic_tls_t *tls, const uint8_t secret[32],
                                 quic_keys_t *keys, const uint8_t *retain_hp) {
    const char *key_label = "quic key";
    const char *iv_label = "quic iv";
    const char *hp_label = "quic hp";
    size_t key_label_len = 8;
    size_t iv_label_len = 7;
    size_t hp_label_len = 7;
    if (tls && tls->version == NEVERC_QUIC_VERSION_2) {
        key_label = "quicv2 key";
        iv_label = "quicv2 iv";
        hp_label = "quicv2 hp";
        key_label_len = 10;
        iv_label_len = 9;
        hp_label_len = 9;
    }
    if (!secret || !keys ||
        nci_tls_hkdf_expand_label(secret, 32, key_label, key_label_len,
                                  NULL, 0, keys->key,
                                  sizeof(keys->key)) != 0 ||
        nci_tls_hkdf_expand_label(secret, 32, iv_label, iv_label_len,
                                  NULL, 0, keys->iv,
                                  sizeof(keys->iv)) != 0)
        return -1;
    /* RFC 9001 §6: header-protection keys are not updated. */
    if (retain_hp) {
        memcpy(keys->hp, retain_hp, sizeof(keys->hp));
        return 0;
    }
    if (nci_tls_hkdf_expand_label(secret, 32, hp_label, hp_label_len,
                                  NULL, 0, keys->hp,
                                  sizeof(keys->hp)) != 0)
        return -1;
    return 0;
}

static int qt_install_secret_pair(quic_tls_t *tls, quic_enc_level_t level,
                                  const uint8_t client_secret[32],
                                  const uint8_t server_secret[32],
                                  int retain_hp) {
    quic_keys_t client_keys;
    quic_keys_t server_keys;
    const uint8_t *client_hp = NULL;
    const uint8_t *server_hp = NULL;
    if (retain_hp && tls->levels[level].available) {
        if (tls->is_server) {
            client_hp = tls->levels[level].read.hp;
            server_hp = tls->levels[level].write.hp;
        } else {
            server_hp = tls->levels[level].read.hp;
            client_hp = tls->levels[level].write.hp;
        }
    }
    if (qt_derive_packet_keys(tls, client_secret, &client_keys,
                              client_hp) != 0 ||
        qt_derive_packet_keys(tls, server_secret, &server_keys,
                              server_hp) != 0)
        return qt_fail(tls, "failed to derive QUIC packet keys");
    if (tls->is_server) {
        tls->levels[level].read = client_keys;
        tls->levels[level].write = server_keys;
    } else {
        tls->levels[level].read = server_keys;
        tls->levels[level].write = client_keys;
    }
    tls->levels[level].available = 1;
    neverc_platform_secure_zero(&client_keys, sizeof(client_keys));
    neverc_platform_secure_zero(&server_keys, sizeof(server_keys));
    return 0;
}

static int qt_derive_handshake_keys(quic_tls_t *tls,
                                    const uint8_t peer_public_key[32]) {
    uint8_t shared_secret[32];
    if (!tls->ecdh_ready ||
        neverc_ecdh_compute(&tls->ecdh_key, peer_public_key, 32,
                            shared_secret, sizeof(shared_secret)) != 32)
        return qt_fail(tls, "X25519 handshake key agreement failed");
    if (nci_tls_derive_handshake_secret(shared_secret, NULL,
                                        tls->handshake_secret) != 0) {
        neverc_platform_secure_zero(shared_secret, sizeof(shared_secret));
        return qt_fail(tls, "TLS handshake secret derivation failed");
    }
    uint8_t transcript_hash[32];
    qt_transcript_hash(tls, transcript_hash);
    int derivation_result = 0;
    if (nci_tls_derive_secret_checked(
            tls->handshake_secret, "c hs traffic", 12,
            transcript_hash, tls->client_hs_secret) != 0 ||
        nci_tls_derive_secret_checked(
            tls->handshake_secret, "s hs traffic", 12,
            transcript_hash, tls->server_hs_secret) != 0)
        derivation_result = -1;
    neverc_platform_secure_zero(shared_secret, sizeof(shared_secret));
    neverc_platform_secure_zero(transcript_hash, sizeof(transcript_hash));
    if (derivation_result != 0)
        return qt_fail(tls, "TLS traffic secret derivation failed");
    return qt_install_secret_pair(tls, QUIC_ENC_HANDSHAKE,
                                  tls->client_hs_secret,
                                  tls->server_hs_secret, 0);
}

static int qt_derive_application_keys(quic_tls_t *tls,
                                      const uint8_t server_finished_hash[32]) {
    if (tls->app_keys_derived) return 0;
    uint8_t empty_hash[32];
    uint8_t master_derived[32];
    uint8_t master_secret[32];
    neverc_sha256_sum(NULL, 0, empty_hash);
    int derivation_result = 0;
    if (nci_tls_derive_secret_checked(
            tls->handshake_secret, "derived", 7,
            empty_hash, master_derived) != 0 ||
        nci_tls_hkdf_extract_zero_ikm(
            master_secret, master_derived, 32) != 0 ||
        nci_tls_derive_secret_checked(
            master_secret, "c ap traffic", 12,
            server_finished_hash, tls->client_app_secret) != 0 ||
        nci_tls_derive_secret_checked(
            master_secret, "s ap traffic", 12,
            server_finished_hash, tls->server_app_secret) != 0)
        derivation_result = -1;
    neverc_platform_secure_zero(empty_hash, sizeof(empty_hash));
    neverc_platform_secure_zero(master_derived, sizeof(master_derived));
    neverc_platform_secure_zero(master_secret, sizeof(master_secret));
    if (derivation_result != 0)
        return qt_fail(tls, "TLS application secret derivation failed");
    if (qt_install_secret_pair(tls, QUIC_ENC_APPLICATION,
                               tls->client_app_secret,
                               tls->server_app_secret, 0) != 0)
        return -1;
    tls->app_keys_derived = 1;
    return 0;
}

static int qt_append_handshake_message(quic_tls_t *tls,
                                       quic_enc_level_t level,
                                       const uint8_t *message,
                                       size_t message_len,
                                       int hash_message) {
    if (!tls || !message || message_len < 4 ||
        message_len > QT_HANDSHAKE_LIMIT ||
        qt_crypto_append(&tls->crypto_send[level], message,
                         message_len) != 0)
        return qt_fail(tls, "QUIC CRYPTO send buffer exceeded its limit");
    if (hash_message)
        neverc_sha256_update(&tls->transcript, message, message_len);
    return 0;
}

static int qt_next_message(quic_tls_t *tls, quic_enc_level_t level,
                           const uint8_t **message, size_t *message_len) {
    if (!tls || !message || !message_len) return -1;
    quic_crypto_buf_t *buffer = &tls->crypto_recv[level];
    if (buffer->processed > buffer->contiguous) return -1;
    uint64_t available64 = buffer->contiguous - buffer->processed;
    if (available64 < 4) return 0;
    if (buffer->processed > SIZE_MAX) return -1;
    size_t start = (size_t)buffer->processed;
    size_t body_len = qt_get_u24(buffer->data + start + 1);
    if (body_len > QT_HANDSHAKE_LIMIT - 4U) return -1;
    size_t total = body_len + 4U;
    if (available64 < total) return 0;
    *message = buffer->data + start;
    *message_len = total;
    return 1;
}

static void qt_consume_message(quic_tls_t *tls, quic_enc_level_t level,
                               size_t message_len) {
    tls->crypto_recv[level].processed += message_len;
}

static int qt_extension_seen(uint16_t *seen, size_t *seen_count,
                             size_t seen_capacity, uint16_t type) {
    for (size_t i = 0; i < *seen_count; i++) {
        if (seen[i] == type) return -1;
    }
    if (*seen_count == seen_capacity) return -1;
    seen[(*seen_count)++] = type;
    return 0;
}

static int qt_select_server_alpn(quic_tls_t *tls,
                                 const uint8_t *offered,
                                 size_t offered_len) {
    if (!tls || !offered || offered_len < 2 ||
        qt_get_u16(offered) != offered_len - 2)
        return qt_fail(tls, "malformed client ALPN extension");
    for (int i = 0; i < tls->config->alpn_count; i++) {
        const char *server_protocol = tls->config->alpn_protos[i];
        size_t server_len = strlen(server_protocol);
        size_t position = 2;
        while (position < offered_len) {
            size_t client_len = offered[position++];
            if (client_len == 0 || client_len > offered_len - position)
                return qt_fail(tls, "malformed client ALPN protocol list");
            if (server_len == client_len &&
                memcmp(server_protocol, offered + position, client_len) == 0) {
                if (client_len >= sizeof(tls->alpn))
                    return qt_fail(tls, "negotiated ALPN is too long");
                memcpy(tls->alpn, server_protocol, client_len);
                tls->alpn[client_len] = '\0';
                return 0;
            }
            position += client_len;
        }
    }
    return qt_fail(tls, "no mutually supported QUIC ALPN protocol");
}

static int qt_parse_client_hello(quic_tls_t *tls, const uint8_t *message,
                                 size_t message_len,
                                 uint8_t peer_public_key[32]) {
    if (!tls || !message || message_len < 4 ||
        message[0] != TLS_HS_CLIENT_HELLO ||
        qt_get_u24(message + 1) != message_len - 4)
        return qt_fail(tls, "malformed QUIC TLS ClientHello");
    qt_cursor_t body = {message + 4, message_len - 4, 0};
    uint16_t legacy_version;
    const uint8_t *ignored;
    qt_cursor_t session_id;
    qt_cursor_t cipher_suites;
    qt_cursor_t compression;
    qt_cursor_t extensions;
    if (qt_read_u16(&body, &legacy_version) != 0 ||
        qt_read_bytes(&body, 32, &ignored) != 0 ||
        qt_read_vector8(&body, &session_id) != 0 || session_id.len > 32 ||
        qt_read_vector16(&body, &cipher_suites) != 0 ||
        cipher_suites.len < 2 || (cipher_suites.len & 1U) != 0 ||
        qt_read_vector8(&body, &compression) != 0 || compression.len != 1 ||
        compression.data[0] != 0 ||
        qt_read_vector16(&body, &extensions) != 0 || body.pos != body.len ||
        legacy_version != TLS_LEGACY_VERSION)
        return qt_fail(tls, "invalid QUIC TLS ClientHello structure");
    int aes_offered = 0;
    while (cipher_suites.pos < cipher_suites.len) {
        uint16_t suite;
        if (qt_read_u16(&cipher_suites, &suite) != 0) return -1;
        if (suite == NEVERC_TLS_AES_128_GCM_SHA256) aes_offered = 1;
    }
    if (!aes_offered)
        return qt_fail(tls, "client did not offer TLS_AES_128_GCM_SHA256");
    memcpy(tls->session_id, session_id.data, session_id.len);
    tls->session_id_len = session_id.len;

    uint16_t seen[64];
    size_t seen_count = 0;
    int tls13 = 0;
    int x25519_group = 0;
    int x25519_key = 0;
    int ecdsa_signature = 0;
    int transport_parameters = 0;
    int alpn = 0;
    while (extensions.pos < extensions.len) {
        uint16_t type;
        qt_cursor_t extension;
        if (qt_read_u16(&extensions, &type) != 0 ||
            qt_read_vector16(&extensions, &extension) != 0 ||
            qt_extension_seen(seen, &seen_count, 64, type) != 0)
            return qt_fail(tls, "duplicate or malformed ClientHello extension");
        if (type == TLS_EXT_SUPPORTED_VERSIONS) {
            qt_cursor_t versions;
            if (qt_read_vector8(&extension, &versions) != 0 ||
                extension.pos != extension.len || (versions.len & 1U) != 0)
                return qt_fail(tls, "malformed supported_versions extension");
            while (versions.pos < versions.len) {
                uint16_t version;
                if (qt_read_u16(&versions, &version) != 0) return -1;
                if (version == NEVERC_TLS_VERSION_13) tls13 = 1;
            }
        } else if (type == TLS_EXT_SUPPORTED_GROUPS) {
            qt_cursor_t groups;
            if (qt_read_vector16(&extension, &groups) != 0 ||
                extension.pos != extension.len || (groups.len & 1U) != 0)
                return qt_fail(tls, "malformed supported_groups extension");
            while (groups.pos < groups.len) {
                uint16_t group;
                if (qt_read_u16(&groups, &group) != 0) return -1;
                if (group == NEVERC_TLS_GROUP_X25519) x25519_group = 1;
            }
        } else if (type == TLS_EXT_SIGNATURE_ALGORITHMS) {
            qt_cursor_t signatures;
            if (qt_read_vector16(&extension, &signatures) != 0 ||
                extension.pos != extension.len ||
                (signatures.len & 1U) != 0)
                return qt_fail(tls, "malformed signature_algorithms extension");
            while (signatures.pos < signatures.len) {
                uint16_t signature;
                if (qt_read_u16(&signatures, &signature) != 0) return -1;
                if (signature == NEVERC_TLS_SIGNATURE_ECDSA_SECP256R1_SHA256)
                    ecdsa_signature = 1;
            }
        } else if (type == TLS_EXT_KEY_SHARE) {
            qt_cursor_t shares;
            if (qt_read_vector16(&extension, &shares) != 0 ||
                extension.pos != extension.len)
                return qt_fail(tls, "malformed key_share extension");
            while (shares.pos < shares.len) {
                uint16_t group;
                qt_cursor_t key;
                if (qt_read_u16(&shares, &group) != 0 ||
                    qt_read_vector16(&shares, &key) != 0)
                    return qt_fail(tls, "malformed client key share");
                if (group == NEVERC_TLS_GROUP_X25519) {
                    if (x25519_key || key.len != 32)
                        return qt_fail(tls, "invalid duplicate X25519 key share");
                    memcpy(peer_public_key, key.data, 32);
                    x25519_key = 1;
                }
            }
        } else if (type == TLS_EXT_ALPN) {
            if (qt_select_server_alpn(tls, extension.data,
                                      extension.len) != 0)
                return -1;
            alpn = 1;
        } else if (type == QT_EXT_QUIC_TRANSPORT_PARAMETERS) {
            if (neverc_quic_transport_params_decode(extension.data,
                                                     extension.len,
                                                     tls->peer_params) != 0 ||
                neverc_quic_transport_params_require_client(
                    tls->peer_params) != 0)
                return qt_fail(tls, "invalid client QUIC transport parameters");
            transport_parameters = 1;
        } else if (type == TLS_EXT_SERVER_NAME) {
            /* SNI is accepted but certificate selection is currently static. */
        } else {
            /* Unknown ClientHello extensions are ignored as required by TLS. */
        }
    }
    if (!tls13 || !x25519_group || !x25519_key || !ecdsa_signature ||
        !transport_parameters || !alpn)
        return qt_fail(tls, "ClientHello is missing a required QUIC TLS extension");
    return 0;
}

static int qt_build_client_hello(quic_tls_t *tls) {
    uint8_t message[8192];
    size_t position = 4;
    if (neverc_ecdh_generate_key(NEVERC_ECDH_CURVE_X25519,
                                 &tls->ecdh_key) != 0)
        return qt_fail(tls, "failed to generate client X25519 key");
    tls->ecdh_ready = 1;
    uint8_t public_key[32];
    if (neverc_ecdh_public_key_bytes(&tls->ecdh_key, public_key,
                                     sizeof(public_key)) != 32)
        return qt_fail(tls, "failed to export client X25519 key");
    qt_put_u16(message + position, TLS_LEGACY_VERSION);
    position += 2;
    if (neverc_crypto_rand_read(message + position, 32) != 0)
        return qt_fail(tls, "failed to generate ClientHello random");
    position += 32;
    /* RFC 9001 §8.4 / Go crypto/tls: QUIC clients MUST NOT request TLS 1.3
     * middlebox compatibility mode, so legacy_session_id is empty. */
    tls->session_id_len = 0;
    message[position++] = 0;
    qt_put_u16(message + position, 2);
    position += 2;
    qt_put_u16(message + position, NEVERC_TLS_AES_128_GCM_SHA256);
    position += 2;
    message[position++] = 1;
    message[position++] = 0;
    size_t extensions_length_at = position;
    position += 2;
    size_t extensions_start = position;

#define QT_CLIENT_EXT(type_value, data_length) do { \
    qt_put_u16(message + position, (type_value)); position += 2; \
    qt_put_u16(message + position, (uint16_t)(data_length)); position += 2; \
} while (0)
    QT_CLIENT_EXT(TLS_EXT_SUPPORTED_VERSIONS, 3);
    message[position++] = 2;
    qt_put_u16(message + position, NEVERC_TLS_VERSION_13);
    position += 2;
    QT_CLIENT_EXT(TLS_EXT_SUPPORTED_GROUPS, 4);
    qt_put_u16(message + position, 2);
    position += 2;
    qt_put_u16(message + position, NEVERC_TLS_GROUP_X25519);
    position += 2;
    QT_CLIENT_EXT(TLS_EXT_SIGNATURE_ALGORITHMS, 4);
    qt_put_u16(message + position, 2);
    position += 2;
    qt_put_u16(message + position,
               NEVERC_TLS_SIGNATURE_ECDSA_SECP256R1_SHA256);
    position += 2;
    QT_CLIENT_EXT(TLS_EXT_KEY_SHARE, 38);
    qt_put_u16(message + position, 36);
    position += 2;
    qt_put_u16(message + position, NEVERC_TLS_GROUP_X25519);
    position += 2;
    qt_put_u16(message + position, 32);
    position += 2;
    memcpy(message + position, public_key, sizeof(public_key));
    position += sizeof(public_key);

    if (tls->config->server_name) {
        size_t server_name_len = strlen(tls->config->server_name);
        if (server_name_len == 0 || server_name_len > 255)
            return qt_fail(tls, "invalid QUIC TLS server name");
        QT_CLIENT_EXT(TLS_EXT_SERVER_NAME, server_name_len + 5U);
        qt_put_u16(message + position, (uint16_t)(server_name_len + 3U));
        position += 2;
        message[position++] = 0;
        qt_put_u16(message + position, (uint16_t)server_name_len);
        position += 2;
        memcpy(message + position, tls->config->server_name,
               server_name_len);
        position += server_name_len;
    }
    size_t alpn_list_len = 0;
    for (int i = 0; i < tls->config->alpn_count; i++) {
        size_t length = strlen(tls->config->alpn_protos[i]);
        if (length == 0 || length > 255 ||
            alpn_list_len > UINT16_MAX - 1U - length)
            return qt_fail(tls, "invalid QUIC ALPN configuration");
        alpn_list_len += 1U + length;
    }
    QT_CLIENT_EXT(TLS_EXT_ALPN, alpn_list_len + 2U);
    qt_put_u16(message + position, (uint16_t)alpn_list_len);
    position += 2;
    for (int i = 0; i < tls->config->alpn_count; i++) {
        size_t length = strlen(tls->config->alpn_protos[i]);
        message[position++] = (uint8_t)length;
        memcpy(message + position, tls->config->alpn_protos[i], length);
        position += length;
    }
    uint8_t encoded_parameters[4096];
    size_t parameters_len;
    if (neverc_quic_transport_params_encode(tls->local_params,
                                             encoded_parameters,
                                             sizeof(encoded_parameters),
                                             &parameters_len) != 0 ||
        parameters_len > UINT16_MAX)
        return qt_fail(tls, "failed to encode client QUIC transport parameters");
    QT_CLIENT_EXT(QT_EXT_QUIC_TRANSPORT_PARAMETERS, parameters_len);
    memcpy(message + position, encoded_parameters, parameters_len);
    position += parameters_len;
#undef QT_CLIENT_EXT
    if (position - extensions_start > UINT16_MAX || position > sizeof(message))
        return qt_fail(tls, "ClientHello exceeded its bounded buffer");
    qt_put_u16(message + extensions_length_at,
               (uint16_t)(position - extensions_start));
    message[0] = TLS_HS_CLIENT_HELLO;
    qt_put_u24(message + 1, (uint32_t)(position - 4));
    neverc_sha256_update(&tls->transcript, message, position);
    return qt_append_handshake_message(tls, QUIC_ENC_INITIAL,
                                       message, position, 0);
}

static int qt_build_server_flight(quic_tls_t *tls,
                                  const uint8_t client_public_key[32]) {
    if (neverc_ecdh_generate_key(NEVERC_ECDH_CURVE_X25519,
                                 &tls->ecdh_key) != 0)
        return qt_fail(tls, "failed to generate server X25519 key");
    tls->ecdh_ready = 1;
    uint8_t public_key[32];
    if (neverc_ecdh_public_key_bytes(&tls->ecdh_key, public_key,
                                     sizeof(public_key)) != 32)
        return qt_fail(tls, "failed to export server X25519 key");

    uint8_t server_hello[160];
    size_t position = 4;
    qt_put_u16(server_hello + position, TLS_LEGACY_VERSION);
    position += 2;
    if (neverc_crypto_rand_read(server_hello + position, 32) != 0)
        return qt_fail(tls, "failed to generate ServerHello random");
    position += 32;
    server_hello[position++] = (uint8_t)tls->session_id_len;
    memcpy(server_hello + position, tls->session_id, tls->session_id_len);
    position += tls->session_id_len;
    qt_put_u16(server_hello + position, NEVERC_TLS_AES_128_GCM_SHA256);
    position += 2;
    server_hello[position++] = 0;
    size_t extension_length_at = position;
    position += 2;
    size_t extension_start = position;
    qt_put_u16(server_hello + position, TLS_EXT_SUPPORTED_VERSIONS);
    position += 2;
    qt_put_u16(server_hello + position, 2);
    position += 2;
    qt_put_u16(server_hello + position, NEVERC_TLS_VERSION_13);
    position += 2;
    qt_put_u16(server_hello + position, TLS_EXT_KEY_SHARE);
    position += 2;
    qt_put_u16(server_hello + position, 36);
    position += 2;
    qt_put_u16(server_hello + position, NEVERC_TLS_GROUP_X25519);
    position += 2;
    qt_put_u16(server_hello + position, 32);
    position += 2;
    memcpy(server_hello + position, public_key, sizeof(public_key));
    position += sizeof(public_key);
    qt_put_u16(server_hello + extension_length_at,
               (uint16_t)(position - extension_start));
    server_hello[0] = TLS_HS_SERVER_HELLO;
    qt_put_u24(server_hello + 1, (uint32_t)(position - 4));
    if (qt_append_handshake_message(tls, QUIC_ENC_INITIAL,
                                    server_hello, position, 1) != 0 ||
        qt_derive_handshake_keys(tls, client_public_key) != 0)
        return -1;

    uint8_t encoded_parameters[4096];
    size_t parameters_len;
    if (neverc_quic_transport_params_encode(tls->local_params,
                                             encoded_parameters,
                                             sizeof(encoded_parameters),
                                             &parameters_len) != 0 ||
        parameters_len > UINT16_MAX)
        return qt_fail(tls, "failed to encode server QUIC transport parameters");
    size_t alpn_len = strlen(tls->alpn);
    size_t ee_body_len = 2U + 4U + 2U + 1U + alpn_len +
                         4U + parameters_len;
    if (ee_body_len > UINT16_MAX || ee_body_len > QT_HANDSHAKE_LIMIT - 4U)
        return qt_fail(tls, "server EncryptedExtensions exceeded its limit");
    uint8_t *encrypted_extensions =
        (uint8_t *)malloc(ee_body_len + 4U);
    if (!encrypted_extensions)
        return qt_fail(tls, "failed to allocate EncryptedExtensions");
    position = 4;
    qt_put_u16(encrypted_extensions + position, (uint16_t)(ee_body_len - 2U));
    position += 2;
    qt_put_u16(encrypted_extensions + position, TLS_EXT_ALPN);
    position += 2;
    qt_put_u16(encrypted_extensions + position, (uint16_t)(3U + alpn_len));
    position += 2;
    qt_put_u16(encrypted_extensions + position, (uint16_t)(1U + alpn_len));
    position += 2;
    encrypted_extensions[position++] = (uint8_t)alpn_len;
    memcpy(encrypted_extensions + position, tls->alpn, alpn_len);
    position += alpn_len;
    qt_put_u16(encrypted_extensions + position,
               QT_EXT_QUIC_TRANSPORT_PARAMETERS);
    position += 2;
    qt_put_u16(encrypted_extensions + position, (uint16_t)parameters_len);
    position += 2;
    memcpy(encrypted_extensions + position, encoded_parameters, parameters_len);
    position += parameters_len;
    encrypted_extensions[0] = TLS_HS_ENCRYPTED_EXT;
    qt_put_u24(encrypted_extensions + 1, (uint32_t)(position - 4));
    int result = qt_append_handshake_message(tls, QUIC_ENC_HANDSHAKE,
                                              encrypted_extensions,
                                              position, 1);
    free(encrypted_extensions);
    if (result != 0) return -1;

    size_t certificate_list_len = 3U + tls->config->cert_der_len + 2U;
    size_t certificate_len = 4U + 1U + 3U + certificate_list_len;
    if (tls->config->cert_der_len == 0 ||
        certificate_len > QT_HANDSHAKE_LIMIT)
        return qt_fail(tls, "server certificate is missing or too large");
    uint8_t *certificate = (uint8_t *)calloc(1, certificate_len);
    if (!certificate) return qt_fail(tls, "failed to allocate Certificate message");
    position = 0;
    certificate[position++] = TLS_HS_CERTIFICATE;
    qt_put_u24(certificate + position, (uint32_t)(certificate_len - 4U));
    position += 3;
    certificate[position++] = 0;
    qt_put_u24(certificate + position, (uint32_t)certificate_list_len);
    position += 3;
    qt_put_u24(certificate + position, (uint32_t)tls->config->cert_der_len);
    position += 3;
    memcpy(certificate + position, tls->config->cert_der,
           tls->config->cert_der_len);
    position += tls->config->cert_der_len;
    qt_put_u16(certificate + position, 0);
    position += 2;
    result = qt_append_handshake_message(tls, QUIC_ENC_HANDSHAKE,
                                         certificate, position, 1);
    free(certificate);
    if (result != 0) return -1;

    uint8_t transcript_hash[32];
    qt_transcript_hash(tls, transcript_hash);
    uint8_t certificate_verify[512];
    uint16_t signature_scheme;
    size_t signature_len;
    if (neverc_tls_sign_certificate_verify(
            tls->config, 1, transcript_hash, sizeof(transcript_hash),
            &signature_scheme, certificate_verify + 8,
            sizeof(certificate_verify) - 8U, &signature_len) != 0 ||
        signature_len > UINT16_MAX)
        return qt_fail(tls, "failed to sign QUIC TLS CertificateVerify");
    certificate_verify[0] = TLS_HS_CERT_VERIFY;
    qt_put_u24(certificate_verify + 1, (uint32_t)(4U + signature_len));
    qt_put_u16(certificate_verify + 4, signature_scheme);
    qt_put_u16(certificate_verify + 6, (uint16_t)signature_len);
    if (qt_append_handshake_message(tls, QUIC_ENC_HANDSHAKE,
                                    certificate_verify,
                                    8U + signature_len, 1) != 0)
        return -1;

    qt_transcript_hash(tls, transcript_hash);
    uint8_t finished_key[32];
    uint8_t verify_data[32];
    if (nci_tls_hkdf_expand_label(tls->server_hs_secret, 32,
                                  "finished", 8, NULL, 0,
                                  finished_key, sizeof(finished_key)) != 0)
        return qt_fail(tls, "failed to derive server Finished key");
    neverc_hmac_sha256(finished_key, sizeof(finished_key), transcript_hash,
                       sizeof(transcript_hash), verify_data);
    uint8_t finished[36];
    finished[0] = TLS_HS_FINISHED;
    qt_put_u24(finished + 1, 32);
    memcpy(finished + 4, verify_data, sizeof(verify_data));
    if (qt_append_handshake_message(tls, QUIC_ENC_HANDSHAKE,
                                    finished, sizeof(finished), 1) != 0)
        return -1;
    qt_transcript_hash(tls, transcript_hash);
    result = qt_derive_application_keys(tls, transcript_hash);
    neverc_platform_secure_zero(transcript_hash, sizeof(transcript_hash));
    neverc_platform_secure_zero(finished_key, sizeof(finished_key));
    neverc_platform_secure_zero(verify_data, sizeof(verify_data));
    return result;
}

static int qt_parse_server_hello(quic_tls_t *tls, const uint8_t *message,
                                 size_t message_len,
                                 uint8_t server_public_key[32]) {
    if (!tls || !message || message_len < 4 ||
        message[0] != TLS_HS_SERVER_HELLO ||
        qt_get_u24(message + 1) != message_len - 4)
        return qt_fail(tls, "malformed QUIC TLS ServerHello");
    qt_cursor_t body = {message + 4, message_len - 4, 0};
    uint16_t legacy_version;
    const uint8_t *ignored;
    qt_cursor_t session_id;
    uint16_t cipher;
    uint8_t compression;
    qt_cursor_t extensions;
    if (qt_read_u16(&body, &legacy_version) != 0 ||
        qt_read_bytes(&body, 32, &ignored) != 0 ||
        qt_read_vector8(&body, &session_id) != 0 ||
        qt_read_u16(&body, &cipher) != 0 ||
        qt_read_u8(&body, &compression) != 0 ||
        qt_read_vector16(&body, &extensions) != 0 || body.pos != body.len ||
        legacy_version != TLS_LEGACY_VERSION ||
        cipher != NEVERC_TLS_AES_128_GCM_SHA256 || compression != 0 ||
        session_id.len != tls->session_id_len ||
        memcmp(session_id.data, tls->session_id, session_id.len) != 0)
        return qt_fail(tls, "invalid QUIC TLS ServerHello structure");
    uint16_t seen[16];
    size_t seen_count = 0;
    int version_seen = 0;
    int key_seen = 0;
    while (extensions.pos < extensions.len) {
        uint16_t type;
        qt_cursor_t extension;
        if (qt_read_u16(&extensions, &type) != 0 ||
            qt_read_vector16(&extensions, &extension) != 0 ||
            qt_extension_seen(seen, &seen_count, 16, type) != 0)
            return qt_fail(tls, "malformed or duplicate ServerHello extension");
        if (type == TLS_EXT_SUPPORTED_VERSIONS) {
            uint16_t version;
            if (qt_read_u16(&extension, &version) != 0 ||
                extension.pos != extension.len ||
                version != NEVERC_TLS_VERSION_13)
                return qt_fail(tls, "server selected an invalid TLS version");
            version_seen = 1;
        } else if (type == TLS_EXT_KEY_SHARE) {
            uint16_t group;
            qt_cursor_t key;
            if (qt_read_u16(&extension, &group) != 0 ||
                qt_read_vector16(&extension, &key) != 0 ||
                extension.pos != extension.len ||
                group != NEVERC_TLS_GROUP_X25519 || key.len != 32)
                return qt_fail(tls, "server selected an invalid key share");
            memcpy(server_public_key, key.data, 32);
            key_seen = 1;
        } else {
            return qt_fail(tls, "server sent a forbidden ServerHello extension");
        }
    }
    if (!version_seen || !key_seen)
        return qt_fail(tls, "ServerHello is missing required extensions");
    return 0;
}

static int qt_parse_encrypted_extensions(quic_tls_t *tls,
                                         const uint8_t *body,
                                         size_t body_len) {
    qt_cursor_t cursor = {body, body_len, 0};
    qt_cursor_t extensions;
    if (qt_read_vector16(&cursor, &extensions) != 0 || cursor.pos != cursor.len)
        return qt_fail(tls, "malformed QUIC TLS EncryptedExtensions");
    uint16_t seen[32];
    size_t seen_count = 0;
    int alpn_seen = 0;
    int params_seen = 0;
    while (extensions.pos < extensions.len) {
        uint16_t type;
        qt_cursor_t extension;
        if (qt_read_u16(&extensions, &type) != 0 ||
            qt_read_vector16(&extensions, &extension) != 0 ||
            qt_extension_seen(seen, &seen_count, 32, type) != 0)
            return qt_fail(tls, "duplicate or malformed EncryptedExtensions entry");
        if (type == TLS_EXT_ALPN) {
            qt_cursor_t protocols;
            qt_cursor_t protocol;
            if (qt_read_vector16(&extension, &protocols) != 0 ||
                extension.pos != extension.len ||
                qt_read_vector8(&protocols, &protocol) != 0 ||
                protocols.pos != protocols.len || protocol.len == 0 ||
                protocol.len >= sizeof(tls->alpn))
                return qt_fail(tls, "server selected malformed QUIC ALPN");
            int offered = 0;
            for (int i = 0; i < tls->config->alpn_count; i++) {
                size_t length = strlen(tls->config->alpn_protos[i]);
                if (length == protocol.len &&
                    memcmp(tls->config->alpn_protos[i], protocol.data,
                           length) == 0)
                    offered = 1;
            }
            if (!offered) return qt_fail(tls, "server selected an unoffered ALPN");
            memcpy(tls->alpn, protocol.data, protocol.len);
            tls->alpn[protocol.len] = '\0';
            alpn_seen = 1;
        } else if (type == QT_EXT_QUIC_TRANSPORT_PARAMETERS) {
            if (neverc_quic_transport_params_decode(extension.data,
                                                     extension.len,
                                                     tls->peer_params) != 0 ||
                neverc_quic_transport_params_require_server(
                    tls->peer_params) != 0)
                return qt_fail(tls, "invalid server QUIC transport parameters");
            params_seen = 1;
        } else {
            /* Other TLS extensions are ignored at this layer. */
        }
    }
    if (!alpn_seen || !params_seen)
        return qt_fail(tls, "server omitted ALPN or QUIC transport parameters");
    return 0;
}

static int qt_parse_certificate(quic_tls_t *tls, const uint8_t *body,
                                size_t body_len) {
    if (!tls || !body || body_len < 4 || tls->peer_cert_der) return -1;
    size_t position = 0;
    size_t context_len = body[position++];
    if (context_len != 0 || context_len > body_len - position)
        return qt_fail(tls, "invalid server Certificate request context");
    position += context_len;
    if (body_len - position < 3) return qt_fail(tls, "truncated Certificate list");
    size_t list_len = qt_get_u24(body + position);
    position += 3;
    if (list_len == 0 || list_len != body_len - position)
        return qt_fail(tls, "empty or malformed server Certificate list");
    size_t list_end = position + list_len;
    tls->peer_intermediates = neverc_x509_cert_pool_new();
    if (!tls->peer_intermediates)
        return qt_fail(tls, "failed to allocate certificate pool");
    size_t certificate_count = 0;
    while (position < list_end) {
        if (++certificate_count > 16 || list_end - position < 3)
            return qt_fail(tls, "server certificate chain is too deep");
        size_t certificate_len = qt_get_u24(body + position);
        position += 3;
        if (certificate_len == 0 || certificate_len > list_end - position)
            return qt_fail(tls, "malformed certificate entry");
        if (certificate_count == 1) {
            tls->peer_cert_der = (uint8_t *)malloc(certificate_len);
            if (!tls->peer_cert_der)
                return qt_fail(tls, "failed to copy peer certificate");
            memcpy(tls->peer_cert_der, body + position, certificate_len);
            tls->peer_cert_len = certificate_len;
        } else if (neverc_x509_cert_pool_add_der(tls->peer_intermediates,
                                                 body + position,
                                                 certificate_len) != 0) {
            return qt_fail(tls, "invalid intermediate certificate");
        }
        position += certificate_len;
        if (list_end - position < 2)
            return qt_fail(tls, "truncated certificate extensions");
        size_t extensions_len = qt_get_u16(body + position);
        position += 2;
        if (extensions_len > list_end - position)
            return qt_fail(tls, "truncated certificate entry extensions");
        position += extensions_len;
    }
    if (neverc_x509_parse_certificate(&tls->peer_cert,
                                      tls->peer_cert_der,
                                      tls->peer_cert_len) != 0)
        return qt_fail(tls, "failed to parse server certificate");
    tls->peer_cert_parsed = 1;
    if (!tls->config->skip_verify &&
        neverc_tls_verify_server_certificate_chain(
            tls->config, tls->peer_cert_der, tls->peer_cert_len,
            tls->peer_intermediates, NULL) != 0)
        return qt_fail(tls, "server certificate chain or hostname is invalid");
    return 0;
}

static int qt_verify_certificate_verify(quic_tls_t *tls,
                                        const uint8_t *body,
                                        size_t body_len) {
    if (!tls || !body || body_len < 4 || !tls->peer_cert_parsed)
        return qt_fail(tls, "CertificateVerify arrived before Certificate");
    uint16_t scheme = qt_get_u16(body);
    size_t signature_len = qt_get_u16(body + 2);
    if (signature_len == 0 || signature_len != body_len - 4)
        return qt_fail(tls, "malformed CertificateVerify signature");
    uint8_t transcript_hash[32];
    qt_transcript_hash(tls, transcript_hash);
    int result = neverc_tls_verify_certificate_verify(
        &tls->peer_cert, scheme, 1, transcript_hash,
        sizeof(transcript_hash), body + 4, signature_len);
    neverc_platform_secure_zero(transcript_hash, sizeof(transcript_hash));
    if (result != 0)
        return qt_fail(tls, "server CertificateVerify signature is invalid");
    return 0;
}

static int qt_verify_finished(quic_tls_t *tls, const uint8_t *body,
                              size_t body_len, int from_server) {
    if (!tls || !body || body_len != 32) return qt_fail(tls, "malformed Finished");
    const uint8_t *secret = from_server ? tls->server_hs_secret :
                                          tls->client_hs_secret;
    uint8_t transcript_hash[32];
    uint8_t finished_key[32];
    uint8_t expected[32];
    qt_transcript_hash(tls, transcript_hash);
    if (nci_tls_hkdf_expand_label(secret, 32, "finished", 8,
                                  NULL, 0, finished_key,
                                  sizeof(finished_key)) != 0)
        return qt_fail(tls, "failed to derive peer Finished key");
    neverc_hmac_sha256(finished_key, sizeof(finished_key), transcript_hash,
                       sizeof(transcript_hash), expected);
    int valid = neverc_subtle_constant_time_compare(body, expected,
                                                    sizeof(expected));
    neverc_platform_secure_zero(transcript_hash, sizeof(transcript_hash));
    neverc_platform_secure_zero(finished_key, sizeof(finished_key));
    neverc_platform_secure_zero(expected, sizeof(expected));
    if (!valid) return qt_fail(tls, "peer Finished verification failed");
    return 0;
}

static int qt_build_client_finished(quic_tls_t *tls) {
    uint8_t transcript_hash[32];
    uint8_t finished_key[32];
    uint8_t finished[36];
    qt_transcript_hash(tls, transcript_hash);
    if (nci_tls_hkdf_expand_label(tls->client_hs_secret, 32,
                                  "finished", 8, NULL, 0,
                                  finished_key, sizeof(finished_key)) != 0)
        return qt_fail(tls, "failed to derive client Finished key");
    finished[0] = TLS_HS_FINISHED;
    qt_put_u24(finished + 1, 32);
    neverc_hmac_sha256(finished_key, sizeof(finished_key), transcript_hash,
                       sizeof(transcript_hash), finished + 4);
    int result = qt_append_handshake_message(tls, QUIC_ENC_HANDSHAKE,
                                              finished, sizeof(finished), 1);
    neverc_platform_secure_zero(transcript_hash, sizeof(transcript_hash));
    neverc_platform_secure_zero(finished_key, sizeof(finished_key));
    return result;
}

static int qt_finish_handshake(quic_tls_t *tls) {
    tls->phase = QT_PHASE_ESTABLISHED;
    tls->handshake_complete = 1;
    tls->key_phase = 0;
    return 0;
}

static int qt_process_client(quic_tls_t *tls) {
    for (;;) {
        quic_enc_level_t level =
            tls->phase == QT_PHASE_CLIENT_WAIT_SERVER_HELLO ?
                QUIC_ENC_INITIAL : QUIC_ENC_HANDSHAKE;
        const uint8_t *message;
        size_t message_len;
        int available = qt_next_message(tls, level, &message, &message_len);
        if (available < 0) return qt_fail(tls, "oversized TLS handshake message");
        if (available == 0) return 0;
        uint8_t type = message[0];
        const uint8_t *body = message + 4;
        size_t body_len = message_len - 4;
        if (tls->phase == QT_PHASE_CLIENT_WAIT_SERVER_HELLO) {
            uint8_t server_public_key[32];
            if (type != TLS_HS_SERVER_HELLO ||
                qt_parse_server_hello(tls, message, message_len,
                                      server_public_key) != 0)
                return qt_fail(tls, "expected a valid ServerHello");
            neverc_sha256_update(&tls->transcript, message, message_len);
            if (qt_derive_handshake_keys(tls, server_public_key) != 0)
                return -1;
            tls->phase = QT_PHASE_CLIENT_WAIT_ENCRYPTED_EXTENSIONS;
        } else if (tls->phase == QT_PHASE_CLIENT_WAIT_ENCRYPTED_EXTENSIONS) {
            if (type != TLS_HS_ENCRYPTED_EXT ||
                qt_parse_encrypted_extensions(tls, body, body_len) != 0)
                return qt_fail(tls, "expected valid EncryptedExtensions");
            neverc_sha256_update(&tls->transcript, message, message_len);
            tls->phase = QT_PHASE_CLIENT_WAIT_CERTIFICATE;
        } else if (tls->phase == QT_PHASE_CLIENT_WAIT_CERTIFICATE) {
            if (type != TLS_HS_CERTIFICATE ||
                qt_parse_certificate(tls, body, body_len) != 0)
                return qt_fail(tls, "expected a valid server Certificate");
            neverc_sha256_update(&tls->transcript, message, message_len);
            tls->phase = QT_PHASE_CLIENT_WAIT_CERTIFICATE_VERIFY;
        } else if (tls->phase == QT_PHASE_CLIENT_WAIT_CERTIFICATE_VERIFY) {
            if (type != TLS_HS_CERT_VERIFY ||
                qt_verify_certificate_verify(tls, body, body_len) != 0)
                return qt_fail(tls, "expected valid CertificateVerify");
            neverc_sha256_update(&tls->transcript, message, message_len);
            tls->phase = QT_PHASE_CLIENT_WAIT_FINISHED;
        } else if (tls->phase == QT_PHASE_CLIENT_WAIT_FINISHED) {
            if (type != TLS_HS_FINISHED ||
                qt_verify_finished(tls, body, body_len, 1) != 0)
                return qt_fail(tls, "expected a valid server Finished");
            neverc_sha256_update(&tls->transcript, message, message_len);
            uint8_t server_finished_hash[32];
            qt_transcript_hash(tls, server_finished_hash);
            if (qt_derive_application_keys(tls, server_finished_hash) != 0 ||
                qt_build_client_finished(tls) != 0) {
                neverc_platform_secure_zero(server_finished_hash,
                                            sizeof(server_finished_hash));
                return -1;
            }
            neverc_platform_secure_zero(server_finished_hash,
                                        sizeof(server_finished_hash));
            qt_consume_message(tls, level, message_len);
            /* RFC 9001 §4.1: Handshake CRYPTO may still arrive after
             * Finished (retransmits, out-of-order fragments). `len` is the
             * highest received offset, not the contiguous prefix — using it
             * here aborted interop with quic-go/quiche. Extra *contiguous*
             * TLS messages after Finished remain unexpected. */
            if (tls->crypto_recv[level].contiguous >
                tls->crypto_recv[level].processed)
                return qt_fail(tls, "unexpected Handshake CRYPTO after Finished");
            return qt_finish_handshake(tls);
        } else {
            return 0;
        }
        qt_consume_message(tls, level, message_len);
    }
}

static int qt_process_server(quic_tls_t *tls) {
    for (;;) {
        quic_enc_level_t level =
            tls->phase == QT_PHASE_SERVER_WAIT_CLIENT_HELLO ?
                QUIC_ENC_INITIAL : QUIC_ENC_HANDSHAKE;
        const uint8_t *message;
        size_t message_len;
        int available = qt_next_message(tls, level, &message, &message_len);
        if (available < 0) return qt_fail(tls, "oversized TLS handshake message");
        if (available == 0) return 0;
        if (tls->phase == QT_PHASE_SERVER_WAIT_CLIENT_HELLO) {
            uint8_t client_public_key[32];
            if (message[0] != TLS_HS_CLIENT_HELLO ||
                qt_parse_client_hello(tls, message, message_len,
                                      client_public_key) != 0)
                return qt_fail(tls, "expected a valid ClientHello");
            neverc_sha256_update(&tls->transcript, message, message_len);
            if (qt_build_server_flight(tls, client_public_key) != 0)
                return -1;
            tls->phase = QT_PHASE_SERVER_WAIT_FINISHED;
        } else if (tls->phase == QT_PHASE_SERVER_WAIT_FINISHED) {
            if (message[0] != TLS_HS_FINISHED || message_len != 36 ||
                qt_verify_finished(tls, message + 4,
                                   message_len - 4, 0) != 0)
                return qt_fail(tls, "expected a valid client Finished");
            neverc_sha256_update(&tls->transcript, message, message_len);
            qt_consume_message(tls, level, message_len);
            /* Same contiguous-vs-len rule as the client Finished path. */
            if (tls->crypto_recv[level].contiguous >
                tls->crypto_recv[level].processed)
                return qt_fail(tls, "unexpected Handshake CRYPTO after Finished");
            return qt_finish_handshake(tls);
        } else {
            return 0;
        }
        qt_consume_message(tls, level, message_len);
    }
}

quic_tls_t *neverc_quic_tls_create(int is_server) {
    quic_tls_t *tls = (quic_tls_t *)calloc(1, sizeof(*tls));
    if (!tls) return NULL;
    tls->is_server = is_server != 0;
    tls->phase = QT_PHASE_IDLE;
    tls->version = NEVERC_QUIC_VERSION_1;
    neverc_sha256_init(&tls->transcript);
    return tls;
}

void neverc_quic_tls_destroy(quic_tls_t *tls) {
    if (!tls) return;
    for (int i = 0; i < QUIC_ENC_LEVEL_COUNT; i++) {
        qt_crypto_free(&tls->crypto_send[i]);
        qt_crypto_free(&tls->crypto_recv[i]);
    }
    if (tls->peer_cert_parsed) neverc_x509_cert_free(&tls->peer_cert);
    neverc_x509_cert_pool_free(tls->peer_intermediates);
    free(tls->peer_cert_der);
    neverc_tls_config_free(tls->config);
    neverc_platform_secure_zero(&tls->ecdh_key, sizeof(tls->ecdh_key));
    neverc_platform_secure_zero(tls->handshake_secret,
                                sizeof(tls->handshake_secret));
    neverc_platform_secure_zero(tls->client_hs_secret,
                                sizeof(tls->client_hs_secret));
    neverc_platform_secure_zero(tls->server_hs_secret,
                                sizeof(tls->server_hs_secret));
    neverc_platform_secure_zero(tls->client_app_secret,
                                sizeof(tls->client_app_secret));
    neverc_platform_secure_zero(tls->server_app_secret,
                                sizeof(tls->server_app_secret));
    neverc_platform_secure_zero(tls, sizeof(*tls));
    free(tls);
}

int neverc_quic_tls_set_initial_dcid(quic_tls_t *tls,
                                     const uint8_t *dcid,
                                     size_t dcid_len, uint32_t version) {
    if (!tls || !dcid || dcid_len == 0 || dcid_len > QUIC_MAX_CID_LEN)
        return -1;
    tls->version = version;
    quic_initial_keys_t keys;
    if (neverc_quic_derive_initial_keys(dcid, dcid_len, version, &keys) != 0)
        return qt_fail(tls, "failed to derive QUIC Initial keys");
    if (tls->is_server) {
        tls->levels[QUIC_ENC_INITIAL].read = keys.client;
        tls->levels[QUIC_ENC_INITIAL].write = keys.server;
    } else {
        tls->levels[QUIC_ENC_INITIAL].read = keys.server;
        tls->levels[QUIC_ENC_INITIAL].write = keys.client;
    }
    tls->levels[QUIC_ENC_INITIAL].available = 1;
    neverc_platform_secure_zero(&keys, sizeof(keys));
    return 0;
}

int neverc_quic_tls_configure(quic_tls_t *tls,
                              const neverc_quic_config_t *config,
                              const char *server_name,
                              const quic_transport_params_t *local_params,
                              quic_transport_params_t *peer_params) {
    if (!tls || !config || !local_params || !peer_params || tls->config)
        return -1;
    tls->config = neverc_tls_config_new();
    if (!tls->config) return qt_fail(tls, "failed to allocate TLS config");
    tls->local_params = local_params;
    tls->peer_params = peer_params;
    const char *default_alpn[] = {"h3", NULL};
    const char **alpn = config->alpn ? config->alpn : default_alpn;
    int alpn_count = 0;
    while (alpn[alpn_count]) {
        if (alpn_count == TLS_MAX_ALPN_PROTOCOLS)
            return qt_fail(tls, "too many QUIC ALPN protocols");
        alpn_count++;
    }
    if (alpn_count == 0) return qt_fail(tls, "QUIC requires ALPN");
    neverc_tls_config_set_alpn(tls->config, alpn, alpn_count);
    if (tls->config->alpn_count != alpn_count)
        return qt_fail(tls, "failed to copy QUIC ALPN configuration");
    if (tls->is_server) {
        if (!config->cert_file || !config->key_file ||
            neverc_tls_config_load_cert(tls->config, config->cert_file,
                                        config->key_file) != 0)
            return qt_fail(tls, "QUIC server certificate/key loading failed");
        tls->phase = QT_PHASE_SERVER_WAIT_CLIENT_HELLO;
    } else {
        const char *name = config->server_name ? config->server_name : server_name;
        if (!name || !name[0])
            return qt_fail(tls, "QUIC client requires a server name");
        neverc_tls_config_set_server_name(tls->config, name);
        if (config->root_cert_file &&
            neverc_tls_config_add_root_certificates(
                tls->config, config->root_cert_file) != 0)
            return qt_fail(tls, "failed to load QUIC client trust roots");
        if (config->insecure_skip_verify)
            neverc_tls_config_insecure_skip_verify(tls->config);
        tls->phase = QT_PHASE_CLIENT_WAIT_SERVER_HELLO;
    }
    return 0;
}

int neverc_quic_tls_start(quic_tls_t *tls) {
    if (!tls || tls->is_server ||
        tls->phase != QT_PHASE_CLIENT_WAIT_SERVER_HELLO)
        return -1;
    return qt_build_client_hello(tls);
}

int neverc_quic_tls_receive_crypto(quic_tls_t *tls, quic_enc_level_t level,
                                   uint64_t offset, const uint8_t *data,
                                   size_t len) {
    if (!tls || level >= QUIC_ENC_LEVEL_COUNT ||
        (!data && len != 0) || offset > QT_CRYPTO_LIMIT ||
        len > QT_CRYPTO_LIMIT - (size_t)offset)
        return -1;
    /* RFC 9001 §4.1: Handshake CRYPTO is retransmitted at Handshake keys
     * after 1-RTT is in use. Duplicates belong on the CRYPTO stream. */
    if (qt_crypto_receive(&tls->crypto_recv[level], offset, data, len) != 0)
        return qt_fail(tls, "conflicting or oversized QUIC CRYPTO data");
    return 0;
}

int neverc_quic_tls_process(quic_tls_t *tls) {
    if (!tls || tls->phase == QT_PHASE_ERROR) return -1;
    if (tls->phase == QT_PHASE_ESTABLISHED) return 0;
    return tls->is_server ? qt_process_server(tls) : qt_process_client(tls);
}

int neverc_quic_tls_get_crypto_data(quic_tls_t *tls,
                                    quic_enc_level_t level,
                                    uint64_t *offset,
                                    const uint8_t **data, size_t *len) {
    if (!tls || level >= QUIC_ENC_LEVEL_COUNT || !offset || !data || !len)
        return -1;
    quic_crypto_buf_t *buffer = &tls->crypto_send[level];
    if (buffer->write_offset > buffer->len ||
        (buffer->len != 0 && !buffer->data))
        return -1;
    *offset = buffer->write_offset;
    *len = buffer->len - (size_t)buffer->write_offset;
    *data = buffer->data
        ? buffer->data + (size_t)buffer->write_offset : NULL;
    return 0;
}

void neverc_quic_tls_crypto_data_sent(quic_tls_t *tls,
                                      quic_enc_level_t level,
                                      size_t bytes_sent) {
    if (!tls || level >= QUIC_ENC_LEVEL_COUNT) return;
    quic_crypto_buf_t *buffer = &tls->crypto_send[level];
    if (bytes_sent <= buffer->len - (size_t)buffer->write_offset)
        buffer->write_offset += bytes_sent;
}

void neverc_quic_tls_crypto_data_acked(quic_tls_t *tls,
                                       quic_enc_level_t level,
                                       uint64_t offset, size_t length) {
    if (!tls || level >= QUIC_ENC_LEVEL_COUNT ||
        offset > UINT64_MAX - length)
        return;
    quic_crypto_buf_t *buffer = &tls->crypto_send[level];
    if (offset <= buffer->acked_offset && offset + length > buffer->acked_offset)
        buffer->acked_offset = offset + length;
}

void neverc_quic_tls_crypto_data_lost(quic_tls_t *tls,
                                      quic_enc_level_t level,
                                      uint64_t offset) {
    if (!tls || level >= QUIC_ENC_LEVEL_COUNT) return;
    quic_crypto_buf_t *buffer = &tls->crypto_send[level];
    if (offset < buffer->write_offset && offset >= buffer->acked_offset)
        buffer->write_offset = offset;
}

void neverc_quic_tls_crypto_rewind_unacked(quic_tls_t *tls,
                                           quic_enc_level_t level) {
    if (!tls || level >= QUIC_ENC_LEVEL_COUNT) return;
    quic_crypto_buf_t *buffer = &tls->crypto_send[level];
    if (buffer->acked_offset < buffer->write_offset)
        buffer->write_offset = buffer->acked_offset;
}

int neverc_quic_tls_install_keys(quic_tls_t *tls, quic_enc_level_t level,
                                 const quic_keys_t *read_key,
                                 const quic_keys_t *write_key) {
    if (!tls || level >= QUIC_ENC_LEVEL_COUNT || (!read_key && !write_key))
        return -1;
    if (read_key) tls->levels[level].read = *read_key;
    if (write_key) tls->levels[level].write = *write_key;
    tls->levels[level].available = 1;
    return 0;
}

int neverc_quic_tls_key_update(quic_tls_t *tls) {
    if (!tls || !tls->handshake_complete) return -1;
    uint8_t next_client[32];
    uint8_t next_server[32];
    const char *ku_label = tls->version == NEVERC_QUIC_VERSION_2 ?
        "quicv2 ku" : "quic ku";
    size_t ku_label_len = tls->version == NEVERC_QUIC_VERSION_2 ? 9 : 7;
    if (nci_tls_hkdf_expand_label(tls->client_app_secret, 32,
                                  ku_label, ku_label_len, NULL, 0,
                                  next_client, sizeof(next_client)) != 0 ||
        nci_tls_hkdf_expand_label(tls->server_app_secret, 32,
                                  ku_label, ku_label_len, NULL, 0,
                                  next_server, sizeof(next_server)) != 0 ||
        qt_install_secret_pair(tls, QUIC_ENC_APPLICATION,
                               next_client, next_server, 1) != 0)
        return qt_fail(tls, "QUIC key update failed");
    memcpy(tls->client_app_secret, next_client, sizeof(next_client));
    memcpy(tls->server_app_secret, next_server, sizeof(next_server));
    neverc_platform_secure_zero(next_client, sizeof(next_client));
    neverc_platform_secure_zero(next_server, sizeof(next_server));
    tls->key_phase ^= 1;
    tls->read_key_phase ^= 1;
    return 0;
}

int neverc_quic_tls_handshake_complete(
    quic_tls_t *tls, const uint8_t *client_app_secret,
    const uint8_t *server_app_secret, const char *negotiated_alpn) {
    if (!tls) return -1;
    if (client_app_secret)
        memcpy(tls->client_app_secret, client_app_secret, 32);
    if (server_app_secret)
        memcpy(tls->server_app_secret, server_app_secret, 32);
    if (negotiated_alpn) {
        size_t length = strlen(negotiated_alpn);
        if (length >= sizeof(tls->alpn)) return -1;
        memcpy(tls->alpn, negotiated_alpn, length + 1U);
    }
    return qt_finish_handshake(tls);
}

int neverc_quic_tls_is_established(const quic_tls_t *tls) {
    return tls && tls->handshake_complete;
}

const char *neverc_quic_tls_alpn(const quic_tls_t *tls) {
    return tls && tls->alpn[0] ? tls->alpn : NULL;
}

int neverc_quic_tls_get_key_phase(const quic_tls_t *tls) {
    return tls ? tls->key_phase : 0;
}

int neverc_quic_tls_get_read_key_phase(const quic_tls_t *tls) {
    return tls ? tls->read_key_phase : 0;
}

int neverc_quic_tls_prepare_read_key_update(quic_tls_t *tls,
                                             quic_keys_t *next_keys) {
    if (!tls || !next_keys || !tls->handshake_complete) return -1;
    const uint8_t *current = tls->is_server ? tls->client_app_secret :
                                              tls->server_app_secret;
    neverc_platform_secure_zero(tls->pending_read_secret,
                                sizeof(tls->pending_read_secret));
    tls->pending_read_secret_valid = 0;
    const char *ku_label = tls->version == NEVERC_QUIC_VERSION_2 ?
        "quicv2 ku" : "quic ku";
    size_t ku_label_len = tls->version == NEVERC_QUIC_VERSION_2 ? 9 : 7;
    if (nci_tls_hkdf_expand_label(current, 32, ku_label, ku_label_len, NULL, 0,
                                  tls->pending_read_secret,
                                  sizeof(tls->pending_read_secret)) != 0 ||
        qt_derive_packet_keys(tls, tls->pending_read_secret, next_keys,
                              tls->levels[QUIC_ENC_APPLICATION].read.hp) != 0) {
        neverc_platform_secure_zero(tls->pending_read_secret,
                                    sizeof(tls->pending_read_secret));
        return -1;
    }
    tls->pending_read_secret_valid = 1;
    return 0;
}

int neverc_quic_tls_commit_read_key_update(quic_tls_t *tls,
                                            const quic_keys_t *next_keys) {
    if (!tls || !next_keys || !tls->pending_read_secret_valid) return -1;
    uint8_t *peer = tls->is_server ? tls->client_app_secret :
                                     tls->server_app_secret;
    uint8_t *ours = tls->is_server ? tls->server_app_secret :
                                     tls->client_app_secret;
    uint8_t next_write[32];
    quic_keys_t write_keys;
    const char *ku_label = tls->version == NEVERC_QUIC_VERSION_2 ?
        "quicv2 ku" : "quic ku";
    size_t ku_label_len = tls->version == NEVERC_QUIC_VERSION_2 ? 9 : 7;
    /* RFC 9001 §6.3: after unprotecting with the next key phase, the
     * endpoint MUST also switch send keys and toggle the send Key Phase. */
    if (nci_tls_hkdf_expand_label(ours, 32, ku_label, ku_label_len, NULL, 0,
                                  next_write, sizeof(next_write)) != 0 ||
        qt_derive_packet_keys(tls, next_write, &write_keys,
                              tls->levels[QUIC_ENC_APPLICATION].write.hp) != 0) {
        neverc_platform_secure_zero(next_write, sizeof(next_write));
        neverc_quic_tls_discard_read_key_update(tls);
        return -1;
    }
    memcpy(peer, tls->pending_read_secret, 32);
    memcpy(ours, next_write, 32);
    tls->levels[QUIC_ENC_APPLICATION].read = *next_keys;
    tls->levels[QUIC_ENC_APPLICATION].write = write_keys;
    tls->read_key_phase ^= 1;
    tls->key_phase ^= 1;
    neverc_platform_secure_zero(next_write, sizeof(next_write));
    neverc_platform_secure_zero(&write_keys, sizeof(write_keys));
    neverc_quic_tls_discard_read_key_update(tls);
    return 0;
}

void neverc_quic_tls_discard_read_key_update(quic_tls_t *tls) {
    if (!tls) return;
    neverc_platform_secure_zero(tls->pending_read_secret,
                                sizeof(tls->pending_read_secret));
    tls->pending_read_secret_valid = 0;
}

void neverc_quic_tls_discard_keys(quic_tls_t *tls, quic_enc_level_t level) {
    if (!tls || level >= QUIC_ENC_LEVEL_COUNT) return;
    neverc_platform_secure_zero(&tls->levels[level],
                                sizeof(tls->levels[level]));
}

const quic_keys_t *neverc_quic_tls_get_read_keys(
    const quic_tls_t *tls, quic_enc_level_t level) {
    if (!tls || level >= QUIC_ENC_LEVEL_COUNT ||
        !tls->levels[level].available)
        return NULL;
    /* RFC 9001 §5.7: do not decrypt inbound 1-RTT until the handshake
     * is complete. Write keys may exist earlier. */
    if (level == QUIC_ENC_APPLICATION && !tls->handshake_complete)
        return NULL;
    return &tls->levels[level].read;
}

const quic_keys_t *neverc_quic_tls_get_write_keys(
    const quic_tls_t *tls, quic_enc_level_t level) {
    if (!tls || level >= QUIC_ENC_LEVEL_COUNT ||
        !tls->levels[level].available)
        return NULL;
    return &tls->levels[level].write;
}

const char *neverc_quic_tls_error(const quic_tls_t *tls) {
    return tls && tls->error_reason[0] ? tls->error_reason :
                                        "QUIC TLS handshake failed";
}
