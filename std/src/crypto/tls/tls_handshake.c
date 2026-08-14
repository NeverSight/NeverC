#include "neverc/std/crypto/tls.h"
#include "neverc/std/_platform.h"
#include "tls_internal.h"
#include "tls_key.h"
#include "tls_key_schedule.h"

#include <limits.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "neverc/std/crypto/ecdh.h"
#include "neverc/std/crypto/hmac.h"
#include "neverc/std/crypto/rand.h"
#include "neverc/std/crypto/sha256.h"
#include "neverc/std/crypto/subtle.h"
#include "neverc/std/crypto/x509.h"
#include "neverc/std/net/tcp.h"

/* ======================================================================
 * TLS 1.3 Handshake — Client
 * ====================================================================== */

neverc_tls_conn_t *nci_tls_conn_new(neverc_tcp_conn_t *tcp, int owns) {
    neverc_tls_conn_t *conn = (neverc_tls_conn_t *)calloc(1, sizeof(*conn));
    if (!conn) return NULL;
    atomic_init(&conn->closed, 0);
    atomic_init(&conn->failure_reason, NULL);
#ifdef _WIN32
    tls_mutex_init(&conn->read_mutex);
    tls_mutex_init(&conn->write_mutex);
#else
    if (tls_mutex_init(&conn->read_mutex) != 0) {
        free(conn);
        return NULL;
    }
    if (tls_mutex_init(&conn->write_mutex) != 0) {
        tls_mutex_destroy(&conn->read_mutex);
        free(conn);
        return NULL;
    }
#endif
    conn->mutexes_initialized = 1;
    conn->tcp = tcp;
    conn->owns_tcp = owns;
    return conn;
}

typedef struct {
    uint8_t server_public_key[32];
    uint16_t selected_cipher;
    tls_cipher_id_t cipher_id;
    int selected_psk;
} tls_server_hello_info_t;

static int tls_parse_server_hello(
    const uint8_t *message, size_t message_len,
    const uint8_t *expected_session_id,
    size_t expected_session_id_len,
    tls_server_hello_info_t *result, uint8_t *alert);
static int tls_set_owned_string(
    char **dst, const char *src, size_t len);
static int tls_parse_encrypted_extensions(
    neverc_tls_conn_t *conn, neverc_tls_config_t *cfg,
    const uint8_t *message, size_t message_len,
    uint8_t *alert);

/* Returns 0 for a non-empty chain, 1 for an allowed empty chain, and -1 for
 * malformed input or allocation failure. Initial-handshake request contexts
 * are required to be empty. */
static int tls_store_peer_certificate(neverc_tls_conn_t *conn,
                                      const uint8_t *message,
                                      size_t message_len,
                                      int allow_empty) {
    if (!conn || !message || conn->peer_cert ||
        conn->peer_intermediates || message_len < 4)
        return -1;

    size_t pos = 0;
    size_t request_context_len = message[pos++];
    if (request_context_len != 0 ||
        request_context_len > message_len - pos)
        return -1;
    pos += request_context_len;
    if (message_len - pos < 3)
        return -1;

    size_t certificate_list_len = tls_get_u24(message + pos);
    pos += 3;
    if (certificate_list_len != message_len - pos)
        return -1;
    if (certificate_list_len == 0)
        return allow_empty ? 1 : -1;

    size_t list_end = pos + certificate_list_len;
    uint8_t *first_certificate = NULL;
    size_t first_certificate_len = 0;
    neverc_x509_cert_pool_t *intermediates =
        neverc_x509_cert_pool_new();
    if (!intermediates)
        return -1;
    size_t certificate_count = 0;
    while (pos < list_end) {
        if (++certificate_count > 16)
            goto fail;
        if (list_end - pos < 3)
            goto fail;
        size_t certificate_len = tls_get_u24(message + pos);
        pos += 3;
        if (certificate_len == 0 ||
            certificate_len > list_end - pos)
            goto fail;
        if (!first_certificate) {
            first_certificate = (uint8_t *)malloc(certificate_len);
            if (!first_certificate)
                goto fail;
            memcpy(first_certificate, message + pos, certificate_len);
            first_certificate_len = certificate_len;
        } else if (neverc_x509_cert_pool_add_der(
                       intermediates, message + pos,
                       certificate_len) != 0) {
            goto fail;
        }
        pos += certificate_len;

        if (list_end - pos < 2)
            goto fail;
        size_t extensions_len = tls_get_u16(message + pos);
        pos += 2;
        if (extensions_len > list_end - pos)
            goto fail;
        pos += extensions_len;
    }

    conn->peer_cert = first_certificate;
    conn->peer_cert_len = first_certificate_len;
    conn->peer_intermediates = intermediates;
    return 0;

fail:
    free(first_certificate);
    neverc_x509_cert_pool_free(intermediates);
    return -1;
}

static int tls_parse_certificate_request(
    const uint8_t *message, size_t message_len,
    uint8_t *alert) {
    if (!message || !alert || message_len < 3)
        return -1;
    *alert = TLS_ALERT_DECODE_ERROR;

    size_t pos = 0;
    size_t request_context_len = message[pos++];
    if (request_context_len != 0 ||
        request_context_len > message_len - pos) {
        *alert = TLS_ALERT_ILLEGAL_PARAMETER;
        return -1;
    }
    pos += request_context_len;
    if (message_len - pos < 2)
        return -1;
    size_t extensions_len = tls_get_u16(message + pos);
    pos += 2;
    if (extensions_len != message_len - pos)
        return -1;

    uint16_t seen_extensions[32];
    size_t seen_count = 0;
    int saw_signature_algorithms = 0;
    int supports_ecdsa_p256_sha256 = 0;
    size_t extensions_end = pos + extensions_len;
    while (pos < extensions_end) {
        if (extensions_end - pos < 4 ||
            seen_count == sizeof(seen_extensions) /
                              sizeof(seen_extensions[0]))
            return -1;
        uint16_t extension_type = tls_get_u16(message + pos);
        size_t extension_len = tls_get_u16(message + pos + 2);
        pos += 4;
        if (extension_len > extensions_end - pos)
            return -1;
        for (size_t i = 0; i < seen_count; ++i) {
            if (seen_extensions[i] == extension_type) {
                *alert = TLS_ALERT_ILLEGAL_PARAMETER;
                return -1;
            }
        }
        seen_extensions[seen_count++] = extension_type;

        if (extension_type == TLS_EXT_SIGNATURE_ALGORITHMS) {
            if (extension_len < 4) return -1;
            size_t algorithms_len = tls_get_u16(message + pos);
            if (algorithms_len != extension_len - 2 ||
                algorithms_len == 0 ||
                (algorithms_len & 1u) != 0)
                return -1;
            for (size_t off = 0; off < algorithms_len; off += 2) {
                if (tls_get_u16(message + pos + 2 + off) ==
                    TLS_SIG_ECDSA_SHA256)
                    supports_ecdsa_p256_sha256 = 1;
            }
            saw_signature_algorithms = 1;
        }
        pos += extension_len;
    }

    if (!saw_signature_algorithms) {
        *alert = TLS_ALERT_MISSING_EXTENSION;
        return -1;
    }
    if (!supports_ecdsa_p256_sha256) {
        *alert = TLS_ALERT_HANDSHAKE_FAILURE;
        return -1;
    }
    return 0;
}

static int tls_send_certificate_request(
    neverc_tls_conn_t *conn, neverc_sha256_ctx *transcript) {
    uint8_t message[15];
    size_t pos = 0;
    message[pos++] = TLS_HS_CERTIFICATE_REQUEST;
    tls_put_u24(message + pos, 11);
    pos += 3;
    message[pos++] = 0; /* certificate_request_context */
    tls_put_u16(message + pos, 8);
    pos += 2;
    tls_put_u16(message + pos, TLS_EXT_SIGNATURE_ALGORITHMS);
    pos += 2;
    tls_put_u16(message + pos, 4);
    pos += 2;
    tls_put_u16(message + pos, 2);
    pos += 2;
    tls_put_u16(message + pos, TLS_SIG_ECDSA_SHA256);
    pos += 2;

    if (pos != sizeof(message))
        return -1;
    neverc_sha256_update(transcript, message, sizeof(message));
    return nci_tls_send_encrypted_handshake(
        conn, message, sizeof(message));
}

static int tls_send_local_certificate(
    neverc_tls_conn_t *conn, const neverc_tls_config_t *cfg,
    neverc_sha256_ctx *transcript, int allow_empty,
    int *sent_nonempty) {
    if (!conn || !transcript || !sent_nonempty)
        return -1;
    int has_certificate =
        cfg && cfg->cert_der && cfg->cert_der_len > 0;
    if (!has_certificate && !allow_empty)
        return -1;
    if (has_certificate &&
        cfg->cert_der_len > 0xFFFFFFu - 5u)
        return -1;

    size_t certificate_list_len = has_certificate ?
        3 + cfg->cert_der_len + 2 : 0;
    size_t message_len =
        4 + 1 + 3 + certificate_list_len;
    if (message_len > TLS_MAX_PLAINTEXT)
        return -1;
    uint8_t *message = (uint8_t *)calloc(1, message_len);
    if (!message)
        return -1;

    size_t pos = 0;
    message[pos++] = TLS_HS_CERTIFICATE;
    tls_put_u24(message + pos, (uint32_t)(message_len - 4));
    pos += 3;
    message[pos++] = 0; /* certificate_request_context */
    tls_put_u24(message + pos, (uint32_t)certificate_list_len);
    pos += 3;
    if (has_certificate) {
        tls_put_u24(message + pos, (uint32_t)cfg->cert_der_len);
        pos += 3;
        memcpy(message + pos, cfg->cert_der, cfg->cert_der_len);
        pos += cfg->cert_der_len;
        tls_put_u16(message + pos, 0);
        pos += 2;
    }
    if (pos != message_len) {
        free(message);
        return -1;
    }

    neverc_sha256_update(transcript, message, message_len);
    int result = nci_tls_send_encrypted_handshake(
        conn, message, message_len);
    free(message);
    if (result != 0)
        return -1;
    *sent_nonempty = has_certificate;
    return 0;
}

static int tls_send_local_certificate_verify(
    neverc_tls_conn_t *conn, const neverc_tls_config_t *cfg,
    int from_server, neverc_sha256_ctx *transcript) {
    if (!conn || !cfg || !transcript)
        return -1;

    uint8_t transcript_hash[32];
    {
        neverc_sha256_ctx copy = *transcript;
        neverc_sha256_final(&copy, transcript_hash);
    }

    uint8_t message[256];
    message[0] = TLS_HS_CERT_VERIFY;
    uint16_t signature_scheme = 0;
    size_t signature_len = 0;
    if (neverc_tls_sign_certificate_verify(
            cfg, from_server, transcript_hash,
            sizeof(transcript_hash), &signature_scheme,
            message + 8, sizeof(message) - 8,
            &signature_len) != 0)
        return -1;
    if (signature_len == 0 || signature_len > UINT16_MAX)
        return -1;

    tls_put_u24(message + 1, (uint32_t)(4 + signature_len));
    tls_put_u16(message + 4, signature_scheme);
    tls_put_u16(message + 6, (uint16_t)signature_len);
    size_t message_len = 8 + signature_len;
    neverc_sha256_update(transcript, message, message_len);
    neverc_platform_secure_zero(
        transcript_hash, sizeof(transcript_hash));
    return nci_tls_send_encrypted_handshake(
        conn, message, message_len);
}

int nci_tls_client_handshake(neverc_tls_conn_t *conn,
                                  neverc_tls_config_t *cfg) {
    neverc_sha256_ctx transcript;
    neverc_sha256_init(&transcript);

    /* Generate ephemeral X25519 key pair */
    neverc_ecdh_key_t ecdh_key;
    if (neverc_ecdh_generate_key(NEVERC_ECDH_CURVE_X25519, &ecdh_key) != 0)
        return -1;

    uint8_t my_pubkey[32];
    neverc_ecdh_public_key_bytes(&ecdh_key, my_pubkey, 32);

    /* Client random */
    uint8_t client_random[32];
    if (neverc_crypto_rand_read(
            client_random, sizeof(client_random)) != 0)
        return nci_tls_fail(conn, TLS_ALERT_INTERNAL_ERROR);

    tls_client_psk_offer_t psk_offer;
    int psk_offered =
        nci_tls_load_client_psk_offer(cfg, &psk_offer);

    /* Build ClientHello */
    uint8_t ch[TLS_CLIENT_HELLO_CAPACITY];
    size_t ch_len = 0;

    /* Handshake header placeholder (filled in later) */
    size_t hs_hdr_pos = ch_len;
    ch_len += 4;

    /* client_version = TLS 1.2 (legacy) */
    tls_put_u16(ch + ch_len, TLS_LEGACY_VERSION);
    ch_len += 2;

    /* random */
    memcpy(ch + ch_len, client_random, 32);
    ch_len += 32;

    /* session_id (legacy, empty for TLS 1.3) */
    uint8_t session_id[32];
    if (neverc_crypto_rand_read(
            session_id, sizeof(session_id)) != 0) {
        nci_tls_clear_client_psk_offer(&psk_offer);
        return nci_tls_fail(conn, TLS_ALERT_INTERNAL_ERROR);
    }
    ch[ch_len++] = 32;
    memcpy(ch + ch_len, session_id, 32);
    ch_len += 32;

    /* cipher_suites */
    tls_put_u16(ch + ch_len, 4); /* 2 suites * 2 bytes */
    ch_len += 2;
    tls_put_u16(ch + ch_len, NEVERC_TLS_AES_128_GCM_SHA256);
    ch_len += 2;
    tls_put_u16(ch + ch_len, NEVERC_TLS_CHACHA20_POLY1305_SHA256);
    ch_len += 2;

    /* compression_methods (null only) */
    ch[ch_len++] = 1;
    ch[ch_len++] = 0;

    /* Extensions */
    size_t ext_len_pos = ch_len;
    ch_len += 2; /* placeholder for total extensions length */
    size_t ext_start = ch_len;

    /* Extension: supported_versions */
    tls_put_u16(ch + ch_len, TLS_EXT_SUPPORTED_VERSIONS); ch_len += 2;
    tls_put_u16(ch + ch_len, 3); ch_len += 2; /* ext data len */
    ch[ch_len++] = 2; /* list len */
    tls_put_u16(ch + ch_len, NEVERC_TLS_VERSION_13); ch_len += 2;

    /* Extension: supported_groups */
    tls_put_u16(ch + ch_len, TLS_EXT_SUPPORTED_GROUPS); ch_len += 2;
    tls_put_u16(ch + ch_len, 4); ch_len += 2;
    tls_put_u16(ch + ch_len, 2); ch_len += 2;
    tls_put_u16(ch + ch_len, NEVERC_TLS_GROUP_X25519); ch_len += 2;

    /* Extension: signature_algorithms */
    tls_put_u16(ch + ch_len, TLS_EXT_SIGNATURE_ALGORITHMS); ch_len += 2;
    tls_put_u16(ch + ch_len, 12); ch_len += 2;
    tls_put_u16(ch + ch_len, 10); ch_len += 2;
    tls_put_u16(ch + ch_len, TLS_SIG_RSA_PSS_SHA256); ch_len += 2;
    tls_put_u16(ch + ch_len, TLS_SIG_ECDSA_SHA256); ch_len += 2;
    tls_put_u16(ch + ch_len, TLS_SIG_ED25519); ch_len += 2;
    tls_put_u16(ch + ch_len, TLS_SIG_RSA_PSS_SHA384); ch_len += 2;
    tls_put_u16(ch + ch_len, TLS_SIG_RSA_PSS_SHA512); ch_len += 2;

    /* Extension: key_share (X25519) */
    tls_put_u16(ch + ch_len, TLS_EXT_KEY_SHARE); ch_len += 2;
    tls_put_u16(ch + ch_len, 38); ch_len += 2; /* ext data len */
    tls_put_u16(ch + ch_len, 36); ch_len += 2; /* client_shares len */
    tls_put_u16(ch + ch_len, NEVERC_TLS_GROUP_X25519); ch_len += 2;
    tls_put_u16(ch + ch_len, 32); ch_len += 2; /* key_exchange len */
    memcpy(ch + ch_len, my_pubkey, 32); ch_len += 32;

    /* Extension: server_name (SNI) */
    if (cfg && cfg->server_name) {
        size_t sni_len = strlen(cfg->server_name);
        if (sni_len == 0 || sni_len > 255) {
            nci_tls_clear_client_psk_offer(&psk_offer);
            return nci_tls_fail(conn, TLS_ALERT_INTERNAL_ERROR);
        }
        if (tls_set_owned_string(
                &conn->server_name, cfg->server_name,
                sni_len) != 0) {
            nci_tls_clear_client_psk_offer(&psk_offer);
            return nci_tls_fail(conn, TLS_ALERT_INTERNAL_ERROR);
        }
        tls_put_u16(ch + ch_len, TLS_EXT_SERVER_NAME); ch_len += 2;
        tls_put_u16(ch + ch_len, (uint16_t)(sni_len + 5)); ch_len += 2;
        tls_put_u16(ch + ch_len, (uint16_t)(sni_len + 3)); ch_len += 2;
        ch[ch_len++] = 0; /* host_name type */
        tls_put_u16(ch + ch_len, (uint16_t)sni_len); ch_len += 2;
        memcpy(ch + ch_len, cfg->server_name, sni_len); ch_len += sni_len;
    }

    /* Extension: ALPN (must precede pre_shared_key). */
    if (cfg && cfg->alpn_count > 0 && cfg->alpn_protos) {
        size_t list_len = 0;
        for (int i = 0; i < cfg->alpn_count; ++i) {
            const char *proto = cfg->alpn_protos[i];
            size_t proto_len;
            if (!proto || proto[0] == '\0') {
                nci_tls_clear_client_psk_offer(&psk_offer);
                return nci_tls_fail(conn, TLS_ALERT_INTERNAL_ERROR);
            }
            proto_len = strlen(proto);
            if (proto_len > 255 ||
                list_len > TLS_MAX_ALPN_LIST - 1 - proto_len) {
                nci_tls_clear_client_psk_offer(&psk_offer);
                return nci_tls_fail(conn, TLS_ALERT_INTERNAL_ERROR);
            }
            list_len += 1 + proto_len;
        }
        if (list_len == 0 || list_len > UINT16_MAX ||
            ch_len > sizeof(ch) - (6 + list_len)) {
            nci_tls_clear_client_psk_offer(&psk_offer);
            return nci_tls_fail(conn, TLS_ALERT_INTERNAL_ERROR);
        }
        tls_put_u16(ch + ch_len, TLS_EXT_ALPN); ch_len += 2;
        tls_put_u16(ch + ch_len, (uint16_t)(2 + list_len)); ch_len += 2;
        tls_put_u16(ch + ch_len, (uint16_t)list_len); ch_len += 2;
        for (int i = 0; i < cfg->alpn_count; ++i) {
            const char *proto = cfg->alpn_protos[i];
            size_t proto_len = strlen(proto);
            ch[ch_len++] = (uint8_t)proto_len;
            memcpy(ch + ch_len, proto, proto_len);
            ch_len += proto_len;
        }
    }

    size_t binder_transcript_len = 0;
    size_t binder_pos = 0;
    if (psk_offered) {
        const size_t psk_encoding_len =
            53 + psk_offer.ticket_len;
        /* Oversize or non-fitting tickets must fall back to a full handshake
         * rather than aborting the connection with an internal_error alert. */
        if (psk_offer.ticket_len == 0 ||
            psk_offer.ticket_len > TLS_MAX_SESSION_TICKET ||
            ch_len > sizeof(ch) - psk_encoding_len) {
            nci_tls_clear_client_psk_offer(&psk_offer);
            psk_offered = 0;
        }
    }
    if (psk_offered) {
        /* Offer only PSK-with-(EC)DHE; PSK must be the final extension. */
        tls_put_u16(ch + ch_len, TLS_EXT_PSK_KEY_EXCHANGE_MODES);
        ch_len += 2;
        tls_put_u16(ch + ch_len, 2);
        ch_len += 2;
        ch[ch_len++] = 1;
        ch[ch_len++] = TLS_PSK_MODE_DHE;

        tls_put_u16(ch + ch_len, TLS_EXT_PRE_SHARED_KEY);
        ch_len += 2;
        size_t psk_ext_len_pos = ch_len;
        ch_len += 2;
        tls_put_u16(ch + ch_len,
                (uint16_t)(2 + psk_offer.ticket_len + 4));
        ch_len += 2;
        tls_put_u16(ch + ch_len,
                (uint16_t)psk_offer.ticket_len);
        ch_len += 2;
        memcpy(ch + ch_len, psk_offer.ticket,
               psk_offer.ticket_len);
        ch_len += psk_offer.ticket_len;
        tls_put_u32(ch + ch_len, psk_offer.obfuscated_age);
        ch_len += 4;
        binder_transcript_len = ch_len;
        tls_put_u16(ch + ch_len, 1 + TLS_HASH_SIZE_SHA256);
        ch_len += 2;
        ch[ch_len++] = TLS_HASH_SIZE_SHA256;
        binder_pos = ch_len;
        memset(ch + ch_len, 0, TLS_HASH_SIZE_SHA256);
        ch_len += TLS_HASH_SIZE_SHA256;
        tls_put_u16(ch + psk_ext_len_pos,
                (uint16_t)(ch_len - psk_ext_len_pos - 2));
    }

    /* Fill extensions length */
    tls_put_u16(ch + ext_len_pos, (uint16_t)(ch_len - ext_start));

    /* Fill handshake header */
    ch[hs_hdr_pos] = TLS_HS_CLIENT_HELLO;
    tls_put_u24(ch + hs_hdr_pos + 1, (uint32_t)(ch_len - 4));

    if (psk_offered &&
        nci_tls_compute_resumption_binder(
            psk_offer.psk, ch, binder_transcript_len,
            ch + binder_pos) != 0) {
        nci_tls_clear_client_psk_offer(&psk_offer);
        return nci_tls_fail(conn, TLS_ALERT_INTERNAL_ERROR);
    }

    /* Hash ClientHello into transcript */
    neverc_sha256_update(&transcript, ch, ch_len);

    /* Send ClientHello record */
    if (nci_tls_send_plain_handshake(conn, ch, ch_len) != 0) {
        nci_tls_clear_client_psk_offer(&psk_offer);
        return -1;
    }

    /* Receive ServerHello */
    const uint8_t *server_hello_message = NULL;
    size_t server_hello_message_len = 0;
    if (nci_tls_recv_plain_handshake_message(
            conn, TLS_HS_SERVER_HELLO,
            &server_hello_message,
            &server_hello_message_len) != 0) {
        nci_tls_clear_client_psk_offer(&psk_offer);
        return -1;
    }

    tls_server_hello_info_t server_hello;
    uint8_t server_hello_alert = TLS_ALERT_DECODE_ERROR;
    if (tls_parse_server_hello(
            server_hello_message, server_hello_message_len,
            session_id, sizeof(session_id),
            &server_hello, &server_hello_alert) != 0) {
        nci_tls_clear_client_psk_offer(&psk_offer);
        return nci_tls_fail(conn, server_hello_alert);
    }

    /* Hash ServerHello into transcript */
    neverc_sha256_update(
        &transcript,
        server_hello_message, server_hello_message_len);
    if (nci_tls_consume_handshake_message(
            conn, server_hello_message_len) != 0) {
        nci_tls_clear_client_psk_offer(&psk_offer);
        return nci_tls_fail(conn, TLS_ALERT_INTERNAL_ERROR);
    }
    if (conn->handshake_len != 0) {
        nci_tls_clear_client_psk_offer(&psk_offer);
        return nci_tls_protocol_error(
            conn, TLS_ALERT_UNEXPECTED_MESSAGE,
            "trailing plaintext data after ServerHello");
    }

    uint16_t selected_cipher =
        server_hello.selected_cipher;
    tls_cipher_id_t cipher_id =
        server_hello.cipher_id;
    conn->cipher_suite = selected_cipher;
    if (server_hello.selected_psk && !psk_offered) {
        nci_tls_clear_client_psk_offer(&psk_offer);
        return nci_tls_fail(conn, TLS_ALERT_ILLEGAL_PARAMETER);
    }
    conn->did_resume = server_hello.selected_psk;
    if (psk_offered && !server_hello.selected_psk)
        nci_tls_config_invalidate_client_session(cfg);

    uint8_t server_pubkey[32];
    memcpy(server_pubkey, server_hello.server_public_key,
           sizeof(server_pubkey));

    /* Compute shared secret via X25519 ECDH */
    uint8_t shared_secret[32];
    if (neverc_ecdh_compute(&ecdh_key, server_pubkey, 32,
                             shared_secret, 32) < 0) {
        nci_tls_clear_client_psk_offer(&psk_offer);
        return nci_tls_fail(conn, TLS_ALERT_ILLEGAL_PARAMETER);
    }

    /* Derive handshake secrets */
    uint8_t handshake_secret[32];
    if (nci_tls_derive_handshake_secret(
            shared_secret,
            server_hello.selected_psk ?
                psk_offer.psk : NULL,
            handshake_secret) != 0) {
        nci_tls_clear_client_psk_offer(&psk_offer);
        return nci_tls_fail(conn, TLS_ALERT_INTERNAL_ERROR);
    }
    if (server_hello.selected_psk) {
        conn->peer_cert = psk_offer.peer_cert;
        conn->peer_cert_len = psk_offer.peer_cert_len;
        psk_offer.peer_cert = NULL;
        psk_offer.peer_cert_len = 0;
        conn->resumption_alpn = psk_offer.alpn;
        psk_offer.alpn = NULL;
    }
    nci_tls_clear_client_psk_offer(&psk_offer);

    /* Transcript hash up to ServerHello */
    uint8_t transcript_hash_sh[32];
    {
        neverc_sha256_ctx copy = transcript;
        neverc_sha256_final(&copy, transcript_hash_sh);
    }

    uint8_t client_hs_traffic_secret[32];
    uint8_t server_hs_traffic_secret[32];
    if (nci_tls_derive_secret_checked(
            handshake_secret, "c hs traffic", 12,
            transcript_hash_sh, client_hs_traffic_secret) != 0 ||
        nci_tls_derive_secret_checked(
            handshake_secret, "s hs traffic", 12,
            transcript_hash_sh, server_hs_traffic_secret) != 0) {
        neverc_platform_secure_zero(
            client_hs_traffic_secret, sizeof(client_hs_traffic_secret));
        neverc_platform_secure_zero(
            server_hs_traffic_secret, sizeof(server_hs_traffic_secret));
        neverc_platform_secure_zero(
            handshake_secret, sizeof(handshake_secret));
        neverc_platform_secure_zero(shared_secret, sizeof(shared_secret));
        neverc_platform_secure_zero(&ecdh_key, sizeof(ecdh_key));
        return nci_tls_fail(conn, TLS_ALERT_INTERNAL_ERROR);
    }

#if defined(NEVERC_TLS_DEBUG_KEYS)
    tls_debug_hex("CLIENT_RANDOM", client_random, sizeof(client_random));
    tls_debug_hex("CLIENT_PRIVATE_KEY",
                  ecdh_key.private_key, 32);
    tls_debug_hex("SERVER_PUBLIC_KEY",
                  server_pubkey, sizeof(server_pubkey));
    tls_debug_hex("SHARED_SECRET",
                  shared_secret, sizeof(shared_secret));
    tls_debug_hex("TRANSCRIPT_HASH_SERVER_HELLO",
                  transcript_hash_sh, sizeof(transcript_hash_sh));
    tls_debug_hex("HANDSHAKE_SECRET",
                  handshake_secret, sizeof(handshake_secret));
    tls_debug_hex("CLIENT_HANDSHAKE_TRAFFIC_SECRET",
                  client_hs_traffic_secret,
                  sizeof(client_hs_traffic_secret));
    tls_debug_hex("SERVER_HANDSHAKE_TRAFFIC_SECRET",
                  server_hs_traffic_secret,
                  sizeof(server_hs_traffic_secret));
#endif

    /* Set handshake traffic keys */
    if (nci_tls_derive_traffic_keys_checked(
            server_hs_traffic_secret, &conn->read_keys, cipher_id) != 0 ||
        nci_tls_derive_traffic_keys_checked(
            client_hs_traffic_secret, &conn->write_keys, cipher_id) != 0) {
        neverc_platform_secure_zero(
            &conn->read_keys, sizeof(conn->read_keys));
        neverc_platform_secure_zero(
            &conn->write_keys, sizeof(conn->write_keys));
        neverc_platform_secure_zero(
            client_hs_traffic_secret, sizeof(client_hs_traffic_secret));
        neverc_platform_secure_zero(
            server_hs_traffic_secret, sizeof(server_hs_traffic_secret));
        neverc_platform_secure_zero(
            handshake_secret, sizeof(handshake_secret));
        neverc_platform_secure_zero(shared_secret, sizeof(shared_secret));
        neverc_platform_secure_zero(&ecdh_key, sizeof(ecdh_key));
        return nci_tls_fail(conn, TLS_ALERT_INTERNAL_ERROR);
    }
    conn->write_keys_active = 1;

    /* Read encrypted handshake messages (EncryptedExtensions, Certificate,
     * CertificateVerify, Finished) */
    int got_finished = 0;
    int certificate_requested = 0;
    uint8_t expected_handshake_type = TLS_HS_ENCRYPTED_EXT;
    while (!got_finished) {
        const uint8_t *handshake_message = NULL;
        size_t handshake_message_len = 0;
        int available = nci_tls_next_handshake_message(
            conn, &handshake_message, &handshake_message_len);
        if (available < 0)
            return nci_tls_protocol_error(
                conn, TLS_ALERT_DECODE_ERROR,
                "server handshake message exceeds the configured limit");
        if (available == 0) {
            uint8_t inner_type;
            uint8_t record_data[TLS_MAX_PLAINTEXT];
            size_t record_len;
            if (nci_tls_recv_decrypt(
                    conn, &inner_type,
                    record_data, &record_len) != 0)
                return nci_tls_error(
                    conn, "failed to decrypt server handshake record");
            if (inner_type != TLS_CT_HANDSHAKE) {
                if (inner_type == TLS_CT_ALERT)
                    return nci_tls_error(
                        conn, "server sent an alert during handshake");
                return nci_tls_protocol_error(
                    conn, TLS_ALERT_UNEXPECTED_MESSAGE,
                    "server sent non-handshake data during handshake");
            }
            if (record_len == 0)
                return nci_tls_protocol_error(
                    conn, TLS_ALERT_DECODE_ERROR,
                    "server sent an empty handshake record");
            if (record_len >
                TLS_MAX_HANDSHAKE - conn->handshake_len)
                return nci_tls_protocol_error(
                    conn, TLS_ALERT_DECODE_ERROR,
                    "server handshake reassembly exceeds the limit");
            if (nci_tls_append_handshake_bytes(
                    conn, record_data, record_len) != 0)
                return nci_tls_protocol_error(
                    conn, TLS_ALERT_INTERNAL_ERROR,
                    "server handshake reassembly allocation failed");
            continue;
        }

        uint8_t hs_type = handshake_message[0];
        size_t msg_len = handshake_message_len - 4;
        int is_certificate_request =
            hs_type == TLS_HS_CERTIFICATE_REQUEST &&
            expected_handshake_type == TLS_HS_CERTIFICATE &&
            !certificate_requested;
        if (!is_certificate_request &&
            hs_type != expected_handshake_type)
            return nci_tls_protocol_error(
                conn, TLS_ALERT_UNEXPECTED_MESSAGE,
                "server handshake message arrived out of order");

        const uint8_t *message = handshake_message + 4;
        if (is_certificate_request) {
            uint8_t request_alert = TLS_ALERT_DECODE_ERROR;
            if (tls_parse_certificate_request(
                    message, msg_len, &request_alert) != 0)
                return nci_tls_protocol_error(
                    conn, request_alert,
                    "malformed or unsupported CertificateRequest");
            certificate_requested = 1;
        } else if (hs_type == TLS_HS_ENCRYPTED_EXT) {
            uint8_t ee_alert = TLS_ALERT_DECODE_ERROR;
            if (tls_parse_encrypted_extensions(
                    conn, cfg, message, msg_len,
                    &ee_alert) != 0)
                return nci_tls_protocol_error(
                    conn, ee_alert,
                    "malformed or invalid server EncryptedExtensions");
            expected_handshake_type = conn->did_resume ?
                TLS_HS_FINISHED : TLS_HS_CERTIFICATE;
        } else if (hs_type == TLS_HS_CERTIFICATE) {
            if (tls_store_peer_certificate(
                    conn, message, msg_len, 0) != 0)
                return nci_tls_protocol_error(
                    conn, TLS_ALERT_DECODE_ERROR,
                    "malformed server Certificate message");
            expected_handshake_type = TLS_HS_CERT_VERIFY;
        } else if (hs_type == TLS_HS_CERT_VERIFY) {
            if (!conn->peer_cert || msg_len < 4)
                return nci_tls_protocol_error(
                    conn, TLS_ALERT_DECODE_ERROR,
                    "malformed server CertificateVerify message");
            uint16_t signature_scheme = tls_get_u16(message);
            size_t signature_len = tls_get_u16(message + 2);
            if (signature_len == 0 ||
                signature_len != msg_len - 4)
                return nci_tls_protocol_error(
                    conn, TLS_ALERT_DECODE_ERROR,
                    "malformed server CertificateVerify signature");

            uint8_t transcript_hash[32];
            neverc_sha256_ctx transcript_copy = transcript;
            neverc_sha256_final(
                &transcript_copy, transcript_hash);
            neverc_x509_cert_t certificate;
            if (neverc_x509_parse_certificate(
                    &certificate, conn->peer_cert,
                    conn->peer_cert_len) != 0)
                return nci_tls_protocol_error(
                    conn, TLS_ALERT_BAD_CERTIFICATE,
                    "failed to parse server certificate");
            int signature_result =
                neverc_tls_verify_certificate_verify(
                    &certificate, signature_scheme, 1,
                    transcript_hash, sizeof(transcript_hash),
                    message + 4, signature_len);
            neverc_x509_cert_free(&certificate);
            if (signature_result != 0)
                return nci_tls_protocol_error(
                    conn, TLS_ALERT_DECRYPT_ERROR,
                    "server CertificateVerify validation failed");
            if (!cfg || !cfg->skip_verify) {
                int chain_result =
                    neverc_tls_verify_server_certificate_chain(
                        cfg, conn->peer_cert,
                        conn->peer_cert_len,
                        conn->peer_intermediates, NULL);
                if (chain_result != 0)
                    return nci_tls_protocol_error(
                        conn, TLS_ALERT_BAD_CERTIFICATE,
                        "server certificate chain validation failed");
            }
            expected_handshake_type = TLS_HS_FINISHED;
        }

        /* Hash all handshake messages into transcript before advancing. */
        if (hs_type != TLS_HS_FINISHED)
            neverc_sha256_update(
                &transcript,
                handshake_message, handshake_message_len);

        if (hs_type == TLS_HS_FINISHED) {
            uint8_t transcript_hash[32];
            neverc_sha256_ctx copy = transcript;
            neverc_sha256_final(&copy, transcript_hash);

            uint8_t finished_key[32];
            if (nci_tls_hkdf_expand_label(
                    server_hs_traffic_secret, 32,
                    "finished", 8, NULL, 0,
                    finished_key, sizeof(finished_key)) != 0)
                return nci_tls_fail(conn, TLS_ALERT_INTERNAL_ERROR);
            uint8_t expected_verify[32];
            neverc_hmac_sha256(
                finished_key, sizeof(finished_key),
                transcript_hash, sizeof(transcript_hash),
                expected_verify);

            if (msg_len != sizeof(expected_verify) ||
                !neverc_subtle_constant_time_compare(
                    message, expected_verify,
                    sizeof(expected_verify)))
                return nci_tls_protocol_error(
                    conn, TLS_ALERT_DECRYPT_ERROR,
                    "server Finished validation failed");
            neverc_sha256_update(
                &transcript,
                handshake_message, handshake_message_len);
            got_finished = 1;
        }

        if (nci_tls_consume_handshake_message(
                conn, handshake_message_len) != 0)
            return nci_tls_protocol_error(
                conn, TLS_ALERT_INTERNAL_ERROR,
                "failed to advance server handshake buffer");
        if (got_finished && conn->handshake_len != 0)
            return nci_tls_protocol_error(
                conn, TLS_ALERT_UNEXPECTED_MESSAGE,
                "trailing data after server Finished");
    }

    /* Application traffic secrets use the transcript through server Finished
     * only (RFC 8446 §7.1). Keep a snapshot before Client Finished. */
    uint8_t transcript_hash_server_finished[32];
    {
        neverc_sha256_ctx copy = transcript;
        neverc_sha256_final(&copy, transcript_hash_server_finished);
    }

    if (certificate_requested) {
        int sent_client_certificate = 0;
        if (tls_send_local_certificate(
                conn, cfg, &transcript, 1,
                &sent_client_certificate) != 0)
            return nci_tls_error(
                conn, "failed to send client Certificate message");
        if (sent_client_certificate &&
            tls_send_local_certificate_verify(
                conn, cfg, 0, &transcript) != 0)
            return nci_tls_error(
                conn, "failed to send client CertificateVerify message");
    }

    /* Send client Finished */
    {
        uint8_t transcript_hash[32];
        neverc_sha256_ctx copy = transcript;
        neverc_sha256_final(&copy, transcript_hash);

        uint8_t finished_key[32];
        if (nci_tls_hkdf_expand_label(client_hs_traffic_secret, 32,
                           "finished", 8, NULL, 0, finished_key, 32) != 0)
            return nci_tls_fail(conn, TLS_ALERT_INTERNAL_ERROR);

        uint8_t verify_data[32];
        neverc_hmac_sha256(finished_key, 32,
                            transcript_hash, 32,
                            verify_data);

        uint8_t finished_msg[36];
        finished_msg[0] = TLS_HS_FINISHED;
        tls_put_u24(finished_msg + 1, 32);
        memcpy(finished_msg + 4, verify_data, 32);

        neverc_sha256_update(&transcript, finished_msg, 36);

        if (nci_tls_send_encrypted_handshake(
                conn, finished_msg, sizeof(finished_msg)) != 0)
            return -1;
    }

    /* Derive application traffic keys */
    uint8_t empty_hash[32];
    neverc_sha256_sum(NULL, 0, empty_hash);
    uint8_t master_derived[32];
    uint8_t master_secret[32];
    uint8_t client_app_secret[32];
    uint8_t server_app_secret[32];
    uint8_t transcript_hash_client_finished[32];
    int key_schedule_result = -1;
    if (nci_tls_derive_secret_checked(
            handshake_secret, "derived", 7,
            empty_hash, master_derived) != 0 ||
        nci_tls_hkdf_extract_zero_ikm(
            master_secret, master_derived, 32) != 0 ||
        nci_tls_derive_secret_checked(
            master_secret, "c ap traffic", 12,
            transcript_hash_server_finished, client_app_secret) != 0 ||
        nci_tls_derive_secret_checked(
            master_secret, "s ap traffic", 12,
            transcript_hash_server_finished, server_app_secret) != 0)
        goto client_key_schedule_cleanup;

    {
        neverc_sha256_ctx copy = transcript;
        neverc_sha256_final(
            &copy, transcript_hash_client_finished);
    }
    if (nci_tls_derive_secret_checked(
            master_secret, "res master", 10,
            transcript_hash_client_finished,
            conn->resumption_master_secret) != 0 ||
        nci_tls_set_application_keys(
            conn, cipher_id,
            server_app_secret, client_app_secret) != 0)
        goto client_key_schedule_cleanup;

    nci_tls_clear_handshake_buffer(conn);
    conn->handshake_done = 1;
    key_schedule_result = 0;

client_key_schedule_cleanup:
    neverc_platform_secure_zero(
        client_app_secret, sizeof(client_app_secret));
    neverc_platform_secure_zero(
        server_app_secret, sizeof(server_app_secret));
    neverc_platform_secure_zero(master_secret, sizeof(master_secret));
    neverc_platform_secure_zero(master_derived, sizeof(master_derived));
    neverc_platform_secure_zero(
        client_hs_traffic_secret, sizeof(client_hs_traffic_secret));
    neverc_platform_secure_zero(
        server_hs_traffic_secret, sizeof(server_hs_traffic_secret));
    neverc_platform_secure_zero(
        handshake_secret, sizeof(handshake_secret));
    neverc_platform_secure_zero(shared_secret, sizeof(shared_secret));
    neverc_platform_secure_zero(&ecdh_key, sizeof(ecdh_key));
    neverc_platform_secure_zero(empty_hash, sizeof(empty_hash));
    neverc_platform_secure_zero(
        transcript_hash_client_finished,
        sizeof(transcript_hash_client_finished));
    return key_schedule_result;
}

/* ======================================================================
 * TLS 1.3 Handshake — Server
 * ====================================================================== */

typedef struct {
    const uint8_t *data;
    size_t len;
    size_t pos;
} tls_cursor_t;

typedef struct {
    const uint8_t *identity;
    size_t identity_len;
    uint32_t obfuscated_age;
    const uint8_t *binder;
    size_t binder_len;
} tls_offered_psk_t;

typedef struct {
    const uint8_t *data;
    size_t len;
} tls_alpn_offer_t;

typedef struct {
    uint8_t session_id[32];
    size_t session_id_len;
    uint8_t client_public_key[32];
    uint16_t selected_cipher;
    tls_cipher_id_t cipher_id;
    tls_offered_psk_t offered_psks[TLS_MAX_PSK_IDENTITIES];
    size_t offered_psk_count;
    size_t binder_transcript_len;
    int offered_psk_dhe;
    char server_name[TLS_MAX_SERVER_NAME + 1];
    size_t server_name_len;
    tls_alpn_offer_t alpn_protocols[TLS_MAX_ALPN_PROTOCOLS];
    size_t alpn_count;
} tls_client_hello_info_t;

static int tls_cursor_read_u8(tls_cursor_t *cursor, uint8_t *value) {
    if (!cursor || !value || cursor->pos >= cursor->len)
        return -1;
    *value = cursor->data[cursor->pos++];
    return 0;
}

static int tls_cursor_read_u16(tls_cursor_t *cursor, uint16_t *value) {
    if (!cursor || !value || cursor->pos > cursor->len ||
        cursor->len - cursor->pos < 2)
        return -1;
    *value = tls_get_u16(cursor->data + cursor->pos);
    cursor->pos += 2;
    return 0;
}

static int tls_cursor_read_u32(tls_cursor_t *cursor, uint32_t *value) {
    if (!cursor || !value || cursor->pos > cursor->len ||
        cursor->len - cursor->pos < 4)
        return -1;
    *value = tls_get_u32(cursor->data + cursor->pos);
    cursor->pos += 4;
    return 0;
}

static int tls_cursor_read_bytes(
    tls_cursor_t *cursor, size_t length, const uint8_t **value) {
    if (!cursor || !value || cursor->pos > cursor->len ||
        length > cursor->len - cursor->pos)
        return -1;
    *value = cursor->data + cursor->pos;
    cursor->pos += length;
    return 0;
}

static int tls_cursor_read_u8_vector(
    tls_cursor_t *cursor, tls_cursor_t *vector) {
    uint8_t length;
    const uint8_t *data;
    if (tls_cursor_read_u8(cursor, &length) != 0 ||
        tls_cursor_read_bytes(cursor, length, &data) != 0)
        return -1;
    vector->data = data;
    vector->len = length;
    vector->pos = 0;
    return 0;
}

static int tls_cursor_read_u16_vector(
    tls_cursor_t *cursor, tls_cursor_t *vector) {
    uint16_t length;
    const uint8_t *data;
    if (tls_cursor_read_u16(cursor, &length) != 0 ||
        tls_cursor_read_bytes(cursor, length, &data) != 0)
        return -1;
    vector->data = data;
    vector->len = length;
    vector->pos = 0;
    return 0;
}

static int tls_set_owned_string(
    char **dst, const char *src, size_t len) {
    if (!dst || (!src && len != 0) || len > TLS_MAX_ALPN_LIST)
        return -1;
    if (!src || len == 0) {
        free(*dst);
        *dst = NULL;
        return 0;
    }
    char *copy = (char *)malloc(len + 1);
    if (!copy)
        return -1;
    memcpy(copy, src, len);
    copy[len] = '\0';
    free(*dst);
    *dst = copy;
    return 0;
}

static int tls_hostname_is_valid(
    const uint8_t *name, size_t len) {
    if (!name || len == 0 || len > TLS_MAX_SERVER_NAME)
        return 0;
    for (size_t i = 0; i < len; ++i) {
        uint8_t c = name[i];
        if (c == 0 || c < 0x20 || c == 0x7f)
            return 0;
    }
    return 1;
}

static int tls_parse_server_name_extension(
    tls_cursor_t *extension_data,
    char *server_name, size_t *server_name_len,
    uint8_t *alert) {
    if (!extension_data || !server_name || !server_name_len ||
        !alert)
        return -1;
    *alert = TLS_ALERT_DECODE_ERROR;
    *server_name_len = 0;
    server_name[0] = '\0';

    tls_cursor_t name_list;
    if (tls_cursor_read_u16_vector(
            extension_data, &name_list) != 0 ||
        extension_data->pos != extension_data->len ||
        name_list.len == 0)
        return -1;

    int saw_host_name = 0;
    while (name_list.pos < name_list.len) {
        uint8_t name_type;
        tls_cursor_t host_name;
        if (tls_cursor_read_u8(&name_list, &name_type) != 0 ||
            tls_cursor_read_u16_vector(
                &name_list, &host_name) != 0 ||
            host_name.len == 0)
            return -1;
        if (name_type != 0)
            continue;
        if (saw_host_name) {
            *alert = TLS_ALERT_ILLEGAL_PARAMETER;
            return -1;
        }
        if (!tls_hostname_is_valid(host_name.data, host_name.len)) {
            *alert = TLS_ALERT_ILLEGAL_PARAMETER;
            return -1;
        }
        memcpy(server_name, host_name.data, host_name.len);
        server_name[host_name.len] = '\0';
        *server_name_len = host_name.len;
        saw_host_name = 1;
    }
    if (!saw_host_name) {
        *alert = TLS_ALERT_ILLEGAL_PARAMETER;
        return -1;
    }
    return 0;
}

static int tls_parse_alpn_extension(
    tls_cursor_t *extension_data,
    tls_alpn_offer_t *protocols, size_t max_protocols,
    size_t *protocol_count, uint8_t *alert) {
    if (!extension_data || !protocols || !protocol_count ||
        !alert || max_protocols == 0)
        return -1;
    *alert = TLS_ALERT_DECODE_ERROR;
    *protocol_count = 0;

    tls_cursor_t protocol_list;
    if (tls_cursor_read_u16_vector(
            extension_data, &protocol_list) != 0 ||
        extension_data->pos != extension_data->len ||
        protocol_list.len == 0)
        return -1;

    while (protocol_list.pos < protocol_list.len) {
        tls_cursor_t protocol;
        if (tls_cursor_read_u8_vector(
                &protocol_list, &protocol) != 0 ||
            protocol.len == 0)
            return -1;
        if (*protocol_count >= max_protocols) {
            *alert = TLS_ALERT_ILLEGAL_PARAMETER;
            return -1;
        }
        protocols[*protocol_count].data = protocol.data;
        protocols[*protocol_count].len = protocol.len;
        (*protocol_count)++;
    }
    if (*protocol_count == 0)
        return -1;
    return 0;
}

static int tls_alpn_offers_contain(
    const tls_alpn_offer_t *protocols, size_t protocol_count,
    const char *candidate, size_t candidate_len) {
    if (!candidate || candidate_len == 0)
        return 0;
    for (size_t i = 0; i < protocol_count; ++i) {
        if (protocols[i].len == candidate_len &&
            memcmp(protocols[i].data, candidate,
                   candidate_len) == 0)
            return 1;
    }
    return 0;
}

/* Server-preference ALPN selection. Both sides offering with no
 * overlap is a fatal no_application_protocol (RFC 7301). Unlike Go,
 * there is no http/1.1↔h2 compatibility escape hatch. */
static int tls_negotiate_alpn(
    const neverc_tls_config_t *cfg,
    const tls_alpn_offer_t *client_protocols,
    size_t client_protocol_count,
    const char **selected, size_t *selected_len,
    uint8_t *alert) {
    if (!selected || !selected_len || !alert)
        return -1;
    *selected = NULL;
    *selected_len = 0;
    *alert = TLS_ALERT_INTERNAL_ERROR;

    int server_has =
        cfg && cfg->alpn_count > 0 && cfg->alpn_protos;
    int client_has = client_protocol_count > 0;
    if (!server_has || !client_has)
        return 0;

    for (int i = 0; i < cfg->alpn_count; ++i) {
        const char *proto = cfg->alpn_protos[i];
        if (!proto || proto[0] == '\0')
            continue;
        size_t proto_len = strlen(proto);
        if (proto_len > 255)
            continue;
        if (tls_alpn_offers_contain(
                client_protocols, client_protocol_count,
                proto, proto_len)) {
            *selected = proto;
            *selected_len = proto_len;
            return 0;
        }
    }
    *alert = TLS_ALERT_NO_APPLICATION_PROTOCOL;
    return -1;
}

static int tls_check_selected_alpn(
    const neverc_tls_config_t *cfg,
    const uint8_t *selected, size_t selected_len,
    uint8_t *alert) {
    if (!alert)
        return -1;
    *alert = TLS_ALERT_ILLEGAL_PARAMETER;
    if (!selected || selected_len == 0)
        return 0;
    if (!cfg || cfg->alpn_count <= 0 || !cfg->alpn_protos) {
        *alert = TLS_ALERT_UNSUPPORTED_EXTENSION;
        return -1;
    }
    for (int i = 0; i < cfg->alpn_count; ++i) {
        const char *proto = cfg->alpn_protos[i];
        if (!proto)
            continue;
        size_t proto_len = strlen(proto);
        if (proto_len == selected_len &&
            memcmp(proto, selected, selected_len) == 0)
            return 0;
    }
    return -1;
}

static int tls_parse_encrypted_extensions(
    neverc_tls_conn_t *conn, neverc_tls_config_t *cfg,
    const uint8_t *message, size_t message_len,
    uint8_t *alert) {
    if (!conn || !message || !alert)
        return -1;
    *alert = TLS_ALERT_DECODE_ERROR;
    if (message_len < 2 || tls_get_u16(message) != message_len - 2)
        return -1;

    tls_cursor_t extensions = {
        message + 2, message_len - 2, 0};
    uint8_t seen_extensions[8192] = {0};
    const uint8_t *selected_alpn = NULL;
    size_t selected_alpn_len = 0;

    while (extensions.pos < extensions.len) {
        uint16_t extension_type;
        tls_cursor_t extension_data;
        if (tls_cursor_read_u16(
                &extensions, &extension_type) != 0 ||
            tls_cursor_read_u16_vector(
                &extensions, &extension_data) != 0)
            return -1;

        size_t seen_byte = (size_t)extension_type >> 3;
        uint8_t seen_bit =
            (uint8_t)(1u << (extension_type & 7));
        if ((seen_extensions[seen_byte] & seen_bit) != 0) {
            *alert = TLS_ALERT_ILLEGAL_PARAMETER;
            return -1;
        }
        seen_extensions[seen_byte] |= seen_bit;

        if (extension_type == TLS_EXT_ALPN) {
            tls_alpn_offer_t protocols[1];
            size_t protocol_count = 0;
            if (tls_parse_alpn_extension(
                    &extension_data, protocols, 1,
                    &protocol_count, alert) != 0)
                return -1;
            if (protocol_count != 1) {
                *alert = TLS_ALERT_DECODE_ERROR;
                return -1;
            }
            selected_alpn = protocols[0].data;
            selected_alpn_len = protocols[0].len;
        } else if (extension_type == TLS_EXT_SERVER_NAME) {
            /* RFC 6066: EncryptedExtensions server_name is empty. */
            if (extension_data.len != 0) {
                *alert = TLS_ALERT_ILLEGAL_PARAMETER;
                return -1;
            }
        } else if (extension_type == TLS_EXT_SUPPORTED_GROUPS) {
            /* Allowed in EncryptedExtensions; contents unused. */
        } else if (extension_type == TLS_EXT_EARLY_DATA ||
                   extension_type == TLS_EXT_KEY_SHARE ||
                   extension_type == TLS_EXT_PRE_SHARED_KEY ||
                   extension_type == TLS_EXT_SUPPORTED_VERSIONS ||
                   extension_type == TLS_EXT_SIGNATURE_ALGORITHMS ||
                   extension_type == TLS_EXT_PSK_KEY_EXCHANGE_MODES) {
            /* Recognized but not legal here, or early_data without an offer. */
            *alert = TLS_ALERT_ILLEGAL_PARAMETER;
            return -1;
        } else {
            /* Ignore unrecognized EncryptedExtensions. */
        }
    }

    if (conn->did_resume) {
        const char *resumption_alpn = conn->resumption_alpn;
        size_t resumption_alpn_len =
            resumption_alpn ? strlen(resumption_alpn) : 0;
        if ((resumption_alpn_len == 0 &&
             selected_alpn_len != 0) ||
            (resumption_alpn_len != 0 &&
             (!selected_alpn ||
              selected_alpn_len != resumption_alpn_len ||
              memcmp(selected_alpn, resumption_alpn,
                     selected_alpn_len) != 0))) {
            *alert = TLS_ALERT_ILLEGAL_PARAMETER;
            return -1;
        }
    }
    if (tls_check_selected_alpn(
            cfg, selected_alpn, selected_alpn_len, alert) != 0)
        return -1;
    if (selected_alpn_len > 0 &&
        tls_set_owned_string(
            &conn->alpn, (const char *)selected_alpn,
            selected_alpn_len) != 0) {
        *alert = TLS_ALERT_INTERNAL_ERROR;
        return -1;
    }
    return 0;
}


static int tls_parse_server_hello(
    const uint8_t *message, size_t message_len,
    const uint8_t *expected_session_id,
    size_t expected_session_id_len,
    tls_server_hello_info_t *result, uint8_t *alert) {
    if (!message || !expected_session_id || !result || !alert ||
        expected_session_id_len > 32)
        return -1;
    *alert = TLS_ALERT_DECODE_ERROR;
    memset(result, 0, sizeof(*result));
    if (message_len < 4 || message[0] != TLS_HS_SERVER_HELLO ||
        (size_t)tls_get_u24(message + 1) != message_len - 4)
        return -1;

    tls_cursor_t body = {message + 4, message_len - 4, 0};
    uint16_t legacy_version;
    const uint8_t *random;
    if (tls_cursor_read_u16(&body, &legacy_version) != 0 ||
        tls_cursor_read_bytes(&body, 32, &random) != 0)
        return -1;
    if (legacy_version != TLS_LEGACY_VERSION) {
        *alert = TLS_ALERT_ILLEGAL_PARAMETER;
        return -1;
    }
    /* RFC 8446 §4.1.3: this random means HelloRetryRequest, not ServerHello. */
    static const uint8_t hello_retry_request_random[32] = {
        0xCF, 0x21, 0xAD, 0x74, 0xE5, 0x9A, 0x61, 0x11,
        0xBE, 0x1D, 0x8C, 0x02, 0x1E, 0x65, 0xB8, 0x91,
        0xC2, 0xA2, 0x11, 0x16, 0x7A, 0xBB, 0x8C, 0x5E,
        0x07, 0x9E, 0x09, 0xE2, 0xC8, 0xA8, 0x33, 0x9C
    };
    if (memcmp(random, hello_retry_request_random, 32) == 0) {
        *alert = TLS_ALERT_HANDSHAKE_FAILURE;
        return -1;
    }

    tls_cursor_t session_id;
    if (tls_cursor_read_u8_vector(&body, &session_id) != 0 ||
        session_id.len > 32)
        return -1;
    if (session_id.len != expected_session_id_len ||
        memcmp(session_id.data, expected_session_id,
               expected_session_id_len) != 0) {
        *alert = TLS_ALERT_ILLEGAL_PARAMETER;
        return -1;
    }

    uint16_t selected_cipher;
    uint8_t compression_method;
    if (tls_cursor_read_u16(&body, &selected_cipher) != 0 ||
        tls_cursor_read_u8(&body, &compression_method) != 0)
        return -1;
    if (selected_cipher == NEVERC_TLS_AES_128_GCM_SHA256) {
        result->cipher_id = TLS_CIPHER_AES_128_GCM_SHA256;
    } else if (selected_cipher ==
               NEVERC_TLS_CHACHA20_POLY1305_SHA256) {
        result->cipher_id =
            TLS_CIPHER_CHACHA20_POLY1305_SHA256;
    } else {
        *alert = TLS_ALERT_HANDSHAKE_FAILURE;
        return -1;
    }
    result->selected_cipher = selected_cipher;
    if (compression_method != 0) {
        *alert = TLS_ALERT_ILLEGAL_PARAMETER;
        return -1;
    }

    tls_cursor_t extensions;
    if (tls_cursor_read_u16_vector(&body, &extensions) != 0 ||
        body.pos != body.len)
        return -1;

    uint8_t seen_extensions[8192] = {0};
    int saw_supported_version = 0;
    int saw_key_share = 0;
    while (extensions.pos < extensions.len) {
        uint16_t extension_type;
        tls_cursor_t extension_data;
        if (tls_cursor_read_u16(
                &extensions, &extension_type) != 0 ||
            tls_cursor_read_u16_vector(
                &extensions, &extension_data) != 0)
            return -1;

        size_t seen_byte = (size_t)extension_type >> 3;
        uint8_t seen_bit =
            (uint8_t)(1u << (extension_type & 7));
        if ((seen_extensions[seen_byte] & seen_bit) != 0) {
            *alert = TLS_ALERT_ILLEGAL_PARAMETER;
            return -1;
        }
        seen_extensions[seen_byte] |= seen_bit;

        if (extension_type == TLS_EXT_SUPPORTED_VERSIONS) {
            uint16_t selected_version;
            if (tls_cursor_read_u16(
                    &extension_data, &selected_version) != 0 ||
                extension_data.pos != extension_data.len)
                return -1;
            if (selected_version != NEVERC_TLS_VERSION_13) {
                *alert = TLS_ALERT_ILLEGAL_PARAMETER;
                return -1;
            }
            saw_supported_version = 1;
        } else if (extension_type == TLS_EXT_KEY_SHARE) {
            uint16_t selected_group;
            tls_cursor_t key_exchange;
            if (tls_cursor_read_u16(
                    &extension_data, &selected_group) != 0 ||
                tls_cursor_read_u16_vector(
                    &extension_data, &key_exchange) != 0 ||
                extension_data.pos != extension_data.len)
                return -1;
            if (selected_group != NEVERC_TLS_GROUP_X25519 ||
                key_exchange.len !=
                    sizeof(result->server_public_key)) {
                *alert = TLS_ALERT_ILLEGAL_PARAMETER;
                return -1;
            }
            memcpy(result->server_public_key,
                   key_exchange.data, key_exchange.len);
            saw_key_share = 1;
        } else if (extension_type == TLS_EXT_PRE_SHARED_KEY) {
            uint16_t selected_identity;
            if (tls_cursor_read_u16(
                    &extension_data, &selected_identity) != 0 ||
                extension_data.pos != extension_data.len)
                return -1;
            if (selected_identity != 0) {
                *alert = TLS_ALERT_ILLEGAL_PARAMETER;
                return -1;
            }
            result->selected_psk = 1;
        } else {
            *alert = TLS_ALERT_UNSUPPORTED_EXTENSION;
            return -1;
        }
    }

    if (!saw_supported_version || !saw_key_share) {
        *alert = TLS_ALERT_MISSING_EXTENSION;
        return -1;
    }
    return 0;
}

static int tls_parse_client_hello(
    const uint8_t *message, size_t message_len,
    tls_client_hello_info_t *result, uint8_t *alert) {
    if (!message || !result || !alert)
        return -1;
    *alert = TLS_ALERT_DECODE_ERROR;
    memset(result, 0, sizeof(*result));
    if (message_len < 4 || message[0] != TLS_HS_CLIENT_HELLO ||
        (size_t)tls_get_u24(message + 1) != message_len - 4)
        return -1;

    tls_cursor_t body = {message + 4, message_len - 4, 0};
    uint16_t legacy_version;
    const uint8_t *random;
    if (tls_cursor_read_u16(&body, &legacy_version) != 0 ||
        tls_cursor_read_bytes(&body, 32, &random) != 0)
        return -1;
    (void)random;
    if (legacy_version != TLS_LEGACY_VERSION) {
        *alert = TLS_ALERT_ILLEGAL_PARAMETER;
        return -1;
    }

    tls_cursor_t session_id;
    if (tls_cursor_read_u8_vector(&body, &session_id) != 0 ||
        session_id.len > sizeof(result->session_id))
        return -1;
    memcpy(result->session_id, session_id.data, session_id.len);
    result->session_id_len = session_id.len;

    tls_cursor_t cipher_suites;
    if (tls_cursor_read_u16_vector(&body, &cipher_suites) != 0 ||
        cipher_suites.len < 2 || (cipher_suites.len & 1) != 0)
        return -1;
    int offered_aes_128_gcm = 0;
    int offered_chacha20_poly1305 = 0;
    while (cipher_suites.pos < cipher_suites.len) {
        uint16_t cipher_suite;
        if (tls_cursor_read_u16(
                &cipher_suites, &cipher_suite) != 0)
            return -1;
        if (cipher_suite == NEVERC_TLS_AES_128_GCM_SHA256)
            offered_aes_128_gcm = 1;
        else if (cipher_suite ==
                 NEVERC_TLS_CHACHA20_POLY1305_SHA256)
            offered_chacha20_poly1305 = 1;
    }

    tls_cursor_t compression_methods;
    if (tls_cursor_read_u8_vector(
            &body, &compression_methods) != 0)
        return -1;
    if (compression_methods.len != 1 ||
        compression_methods.data[0] != 0) {
        *alert = TLS_ALERT_ILLEGAL_PARAMETER;
        return -1;
    }

    tls_cursor_t extensions;
    if (tls_cursor_read_u16_vector(&body, &extensions) != 0 ||
        body.pos != body.len)
        return -1;

    uint8_t seen_extensions[8192] = {0};
    int supports_tls13 = 0;
    int saw_supported_versions = 0;
    int saw_supported_groups = 0;
    int supports_x25519 = 0;
    int saw_signature_algorithms = 0;
    int supports_ecdsa_p256_sha256 = 0;
    int saw_key_share = 0;
    int has_x25519_key_share = 0;
    while (extensions.pos < extensions.len) {
        uint16_t extension_type;
        tls_cursor_t extension_data;
        if (tls_cursor_read_u16(
                &extensions, &extension_type) != 0 ||
            tls_cursor_read_u16_vector(
                &extensions, &extension_data) != 0)
            return -1;

        size_t seen_byte = (size_t)extension_type >> 3;
        uint8_t seen_bit =
            (uint8_t)(1u << (extension_type & 7));
        if ((seen_extensions[seen_byte] & seen_bit) != 0) {
            *alert = TLS_ALERT_ILLEGAL_PARAMETER;
            return -1;
        }
        seen_extensions[seen_byte] |= seen_bit;

        if (extension_type == TLS_EXT_SUPPORTED_VERSIONS) {
            saw_supported_versions = 1;
            tls_cursor_t versions;
            if (tls_cursor_read_u8_vector(
                    &extension_data, &versions) != 0 ||
                extension_data.pos != extension_data.len ||
                versions.len < 2 || (versions.len & 1) != 0)
                return -1;
            while (versions.pos < versions.len) {
                uint16_t version;
                if (tls_cursor_read_u16(&versions, &version) != 0)
                    return -1;
                if (version == NEVERC_TLS_VERSION_13)
                    supports_tls13 = 1;
            }
        } else if (extension_type == TLS_EXT_SUPPORTED_GROUPS) {
            saw_supported_groups = 1;
            tls_cursor_t groups;
            if (tls_cursor_read_u16_vector(
                    &extension_data, &groups) != 0 ||
                extension_data.pos != extension_data.len ||
                groups.len < 2 || (groups.len & 1) != 0)
                return -1;
            while (groups.pos < groups.len) {
                uint16_t group;
                if (tls_cursor_read_u16(&groups, &group) != 0)
                    return -1;
                if (group == NEVERC_TLS_GROUP_X25519)
                    supports_x25519 = 1;
            }
        } else if (extension_type ==
                   TLS_EXT_SIGNATURE_ALGORITHMS) {
            saw_signature_algorithms = 1;
            tls_cursor_t signature_algorithms;
            if (tls_cursor_read_u16_vector(
                    &extension_data,
                    &signature_algorithms) != 0 ||
                extension_data.pos != extension_data.len ||
                signature_algorithms.len < 2 ||
                (signature_algorithms.len & 1) != 0)
                return -1;
            while (signature_algorithms.pos <
                   signature_algorithms.len) {
                uint16_t signature_algorithm;
                if (tls_cursor_read_u16(
                        &signature_algorithms,
                        &signature_algorithm) != 0)
                    return -1;
                if (signature_algorithm == TLS_SIG_ECDSA_SHA256)
                    supports_ecdsa_p256_sha256 = 1;
            }
        } else if (extension_type == TLS_EXT_KEY_SHARE) {
            saw_key_share = 1;
            tls_cursor_t key_shares;
            if (tls_cursor_read_u16_vector(
                    &extension_data, &key_shares) != 0 ||
                extension_data.pos != extension_data.len ||
                key_shares.len == 0)
                return -1;
            while (key_shares.pos < key_shares.len) {
                uint16_t group;
                tls_cursor_t key_exchange;
                if (tls_cursor_read_u16(&key_shares, &group) != 0 ||
                    tls_cursor_read_u16_vector(
                        &key_shares, &key_exchange) != 0 ||
                    key_exchange.len == 0)
                    return -1;
                if (group == NEVERC_TLS_GROUP_X25519) {
                    if (has_x25519_key_share ||
                        key_exchange.len !=
                            sizeof(result->client_public_key)) {
                        *alert = TLS_ALERT_ILLEGAL_PARAMETER;
                        return -1;
                    }
                    memcpy(result->client_public_key,
                           key_exchange.data, key_exchange.len);
                    has_x25519_key_share = 1;
                }
            }
        } else if (extension_type == TLS_EXT_SERVER_NAME) {
            if (tls_parse_server_name_extension(
                    &extension_data, result->server_name,
                    &result->server_name_len, alert) != 0)
                return -1;
        } else if (extension_type == TLS_EXT_ALPN) {
            if (tls_parse_alpn_extension(
                    &extension_data, result->alpn_protocols,
                    TLS_MAX_ALPN_PROTOCOLS,
                    &result->alpn_count, alert) != 0)
                return -1;
        } else if (extension_type ==
                   TLS_EXT_PSK_KEY_EXCHANGE_MODES) {
            tls_cursor_t modes;
            if (tls_cursor_read_u8_vector(
                    &extension_data, &modes) != 0 ||
                extension_data.pos != extension_data.len ||
                modes.len == 0)
                return -1;
            while (modes.pos < modes.len) {
                uint8_t mode;
                if (tls_cursor_read_u8(&modes, &mode) != 0)
                    return -1;
                if (mode == TLS_PSK_MODE_DHE)
                    result->offered_psk_dhe = 1;
            }
        } else if (extension_type == TLS_EXT_PRE_SHARED_KEY) {
            if (extensions.pos != extensions.len) {
                *alert = TLS_ALERT_ILLEGAL_PARAMETER;
                return -1;
            }
            tls_cursor_t identities;
            tls_cursor_t binders;
            if (tls_cursor_read_u16_vector(
                    &extension_data, &identities) != 0 ||
                identities.len == 0)
                return -1;
            result->binder_transcript_len =
                (size_t)(extension_data.data - message) +
                extension_data.pos;
            if (tls_cursor_read_u16_vector(
                    &extension_data, &binders) != 0 ||
                extension_data.pos != extension_data.len ||
                binders.len == 0)
                return -1;

            while (identities.pos < identities.len) {
                if (result->offered_psk_count >=
                    TLS_MAX_PSK_IDENTITIES) {
                    *alert = TLS_ALERT_ILLEGAL_PARAMETER;
                    return -1;
                }
                tls_cursor_t identity;
                uint32_t obfuscated_age;
                if (tls_cursor_read_u16_vector(
                        &identities, &identity) != 0 ||
                    identity.len == 0 ||
                    tls_cursor_read_u32(
                        &identities, &obfuscated_age) != 0)
                    return -1;
                tls_offered_psk_t *offered =
                    &result->offered_psks[
                        result->offered_psk_count++];
                offered->identity = identity.data;
                offered->identity_len = identity.len;
                offered->obfuscated_age = obfuscated_age;
            }

            size_t binder_count = 0;
            while (binders.pos < binders.len) {
                tls_cursor_t binder;
                if (binder_count >=
                        result->offered_psk_count ||
                    tls_cursor_read_u8_vector(
                        &binders, &binder) != 0 ||
                    binder.len != TLS_HASH_SIZE_SHA256)
                    return -1;
                result->offered_psks[binder_count].binder =
                    binder.data;
                result->offered_psks[binder_count].binder_len =
                    binder.len;
                ++binder_count;
            }
            if (binder_count != result->offered_psk_count)
                return -1;
        }
    }

    if (!saw_supported_versions ||
        !saw_supported_groups ||
        !saw_signature_algorithms) {
        *alert = TLS_ALERT_MISSING_EXTENSION;
        return -1;
    }
    if (!supports_tls13) {
        *alert = TLS_ALERT_PROTOCOL_VERSION;
        return -1;
    }
    if (!saw_key_share) {
        *alert = TLS_ALERT_MISSING_EXTENSION;
        return -1;
    }
    if (!supports_x25519 || !has_x25519_key_share ||
        !supports_ecdsa_p256_sha256) {
        *alert = TLS_ALERT_HANDSHAKE_FAILURE;
        return -1;
    }
    if (result->offered_psk_count > 0 &&
        !result->offered_psk_dhe) {
        *alert = TLS_ALERT_MISSING_EXTENSION;
        return -1;
    }
    if (offered_aes_128_gcm) {
        result->selected_cipher =
            NEVERC_TLS_AES_128_GCM_SHA256;
        result->cipher_id =
            TLS_CIPHER_AES_128_GCM_SHA256;
    } else if (offered_chacha20_poly1305) {
        result->selected_cipher =
            NEVERC_TLS_CHACHA20_POLY1305_SHA256;
        result->cipher_id =
            TLS_CIPHER_CHACHA20_POLY1305_SHA256;
    } else {
        *alert = TLS_ALERT_HANDSHAKE_FAILURE;
        return -1;
    }
    return 0;
}

int nci_tls_send_new_session_ticket(neverc_tls_conn_t *conn);

int nci_tls_server_handshake(neverc_tls_conn_t *conn,
                                  neverc_tls_config_t *cfg) {
    if (!cfg || !cfg->cert_der || !cfg->key_der) return -1;

    neverc_sha256_ctx transcript;
    neverc_sha256_init(&transcript);

    /* Receive ClientHello */
    const uint8_t *client_hello_message = NULL;
    size_t client_hello_message_len = 0;
    if (nci_tls_recv_plain_handshake_message(
            conn, TLS_HS_CLIENT_HELLO,
            &client_hello_message,
            &client_hello_message_len) != 0)
        return -1;

    tls_client_hello_info_t client_hello;
    uint8_t client_hello_alert = TLS_ALERT_DECODE_ERROR;
    if (tls_parse_client_hello(
            client_hello_message, client_hello_message_len,
            &client_hello,
            &client_hello_alert) != 0)
        return nci_tls_fail(conn, client_hello_alert);

    if (client_hello.server_name_len > 0 &&
        tls_set_owned_string(
            &conn->server_name, client_hello.server_name,
            client_hello.server_name_len) != 0)
        return nci_tls_fail(conn, TLS_ALERT_INTERNAL_ERROR);

    const char *selected_alpn = NULL;
    size_t selected_alpn_len = 0;
    uint8_t alpn_alert = TLS_ALERT_INTERNAL_ERROR;
    if (tls_negotiate_alpn(
            cfg, client_hello.alpn_protocols,
            client_hello.alpn_count, &selected_alpn,
            &selected_alpn_len, &alpn_alert) != 0)
        return nci_tls_fail(conn, alpn_alert);
    if (selected_alpn_len > 0 &&
        tls_set_owned_string(
            &conn->alpn, selected_alpn,
            selected_alpn_len) != 0)
        return nci_tls_fail(conn, TLS_ALERT_INTERNAL_ERROR);

    int selected_psk_index = -1;
    uint8_t selected_psk[TLS_HASH_SIZE_SHA256] = {0};
    if (cfg->client_auth == NEVERC_TLS_CLIENT_AUTH_NONE) {
        for (size_t i = 0;
             i < client_hello.offered_psk_count; ++i) {
            tls_offered_psk_t *offered =
                &client_hello.offered_psks[i];
            if (!nci_tls_lookup_server_session(
                    cfg, offered->identity,
                    offered->identity_len,
                    offered->obfuscated_age,
                    (const uint8_t *)client_hello.server_name,
                    client_hello.server_name_len,
                    selected_alpn, selected_alpn_len,
                    selected_psk))
                continue;
            uint8_t expected_binder[TLS_HASH_SIZE_SHA256];
            if (nci_tls_compute_resumption_binder(
                    selected_psk, client_hello_message,
                    client_hello.binder_transcript_len,
                    expected_binder) != 0)
                return nci_tls_fail(conn, TLS_ALERT_INTERNAL_ERROR);
            int binder_valid =
                offered->binder_len == sizeof(expected_binder) &&
                neverc_subtle_constant_time_compare(
                    offered->binder, expected_binder,
                    sizeof(expected_binder));
            neverc_platform_secure_zero(
                expected_binder, sizeof(expected_binder));
            if (!binder_valid) {
                neverc_platform_secure_zero(
                    selected_psk, sizeof(selected_psk));
                return nci_tls_fail(conn, TLS_ALERT_DECRYPT_ERROR);
            }
            selected_psk_index = (int)i;
            break;
        }
    }
    conn->did_resume = selected_psk_index >= 0;

    neverc_sha256_update(
        &transcript,
        client_hello_message, client_hello_message_len);
    if (nci_tls_consume_handshake_message(
            conn, client_hello_message_len) != 0)
        return nci_tls_fail(conn, TLS_ALERT_INTERNAL_ERROR);
    if (conn->handshake_len != 0)
        return nci_tls_protocol_error(
            conn, TLS_ALERT_UNEXPECTED_MESSAGE,
            "trailing plaintext data after ClientHello");

    uint8_t ch_sid_len =
        (uint8_t)client_hello.session_id_len;
    uint8_t ch_session_id[32];
    if (ch_sid_len > 0)
        memcpy(ch_session_id, client_hello.session_id,
               ch_sid_len);

    uint16_t selected_cipher =
        client_hello.selected_cipher;
    tls_cipher_id_t cipher_id =
        client_hello.cipher_id;
    conn->cipher_suite = selected_cipher;

    uint8_t client_pubkey[32];
    memcpy(client_pubkey, client_hello.client_public_key,
           sizeof(client_pubkey));

    /* Generate server X25519 key pair */
    neverc_ecdh_key_t ecdh_key;
    if (neverc_ecdh_generate_key(NEVERC_ECDH_CURVE_X25519, &ecdh_key) != 0)
        return -1;

    uint8_t server_pubkey[32];
    neverc_ecdh_public_key_bytes(&ecdh_key, server_pubkey, 32);

    /* Build ServerHello */
    uint8_t sh[256];
    size_t sh_len = 0;

    sh[sh_len++] = TLS_HS_SERVER_HELLO;
    size_t sh_body_pos = sh_len;
    sh_len += 3;

    tls_put_u16(sh + sh_len, TLS_LEGACY_VERSION); sh_len += 2;

    uint8_t server_random[32];
    if (neverc_crypto_rand_read(
            server_random, sizeof(server_random)) != 0)
        return nci_tls_fail(conn, TLS_ALERT_INTERNAL_ERROR);
    memcpy(sh + sh_len, server_random, 32); sh_len += 32;

    /* Echo session_id */
    sh[sh_len++] = ch_sid_len;
    if (ch_sid_len > 0) {
        memcpy(sh + sh_len, ch_session_id, ch_sid_len);
        sh_len += ch_sid_len;
    }

    tls_put_u16(sh + sh_len, selected_cipher); sh_len += 2;
    sh[sh_len++] = 0; /* compression */

    /* Extensions */
    size_t sh_ext_len_pos = sh_len; sh_len += 2;
    size_t sh_ext_start = sh_len;

    /* supported_versions */
    tls_put_u16(sh + sh_len, TLS_EXT_SUPPORTED_VERSIONS); sh_len += 2;
    tls_put_u16(sh + sh_len, 2); sh_len += 2;
    tls_put_u16(sh + sh_len, NEVERC_TLS_VERSION_13); sh_len += 2;

    /* key_share */
    tls_put_u16(sh + sh_len, TLS_EXT_KEY_SHARE); sh_len += 2;
    tls_put_u16(sh + sh_len, 36); sh_len += 2;
    tls_put_u16(sh + sh_len, NEVERC_TLS_GROUP_X25519); sh_len += 2;
    tls_put_u16(sh + sh_len, 32); sh_len += 2;
    memcpy(sh + sh_len, server_pubkey, 32); sh_len += 32;

    if (selected_psk_index >= 0) {
        tls_put_u16(sh + sh_len, TLS_EXT_PRE_SHARED_KEY);
        sh_len += 2;
        tls_put_u16(sh + sh_len, 2);
        sh_len += 2;
        tls_put_u16(sh + sh_len,
                (uint16_t)selected_psk_index);
        sh_len += 2;
    }

    tls_put_u16(sh + sh_ext_len_pos, (uint16_t)(sh_len - sh_ext_start));
    tls_put_u24(sh + sh_body_pos, (uint32_t)(sh_len - sh_body_pos - 3));

    neverc_sha256_update(&transcript, sh, sh_len);

    /* Send ServerHello */
    if (nci_tls_send_plain_handshake(conn, sh, sh_len) != 0)
        return -1;

    /* Send Change Cipher Spec (compatibility) */
    {
        uint8_t ccs = 1;
        if (nci_tls_send_record(
                conn->tcp, TLS_CT_CHANGE_CIPHER_SPEC, &ccs, 1) != 0)
            return -1;
    }

    /* Compute shared secret */
    uint8_t shared_secret[32];
    if (neverc_ecdh_compute(&ecdh_key, client_pubkey, 32,
                             shared_secret, 32) < 0)
        return nci_tls_fail(conn, TLS_ALERT_ILLEGAL_PARAMETER);

    /* Key schedule */
    uint8_t handshake_secret[32];
    if (nci_tls_derive_handshake_secret(
            shared_secret,
            selected_psk_index >= 0 ? selected_psk : NULL,
            handshake_secret) != 0)
        return nci_tls_fail(conn, TLS_ALERT_INTERNAL_ERROR);
    neverc_platform_secure_zero(
        selected_psk, sizeof(selected_psk));

    uint8_t transcript_hash_sh[32];
    {
        neverc_sha256_ctx copy = transcript;
        neverc_sha256_final(&copy, transcript_hash_sh);
    }

    uint8_t client_hs_traffic_secret[32];
    uint8_t server_hs_traffic_secret[32];
    if (nci_tls_derive_secret_checked(
            handshake_secret, "c hs traffic", 12,
            transcript_hash_sh, client_hs_traffic_secret) != 0 ||
        nci_tls_derive_secret_checked(
            handshake_secret, "s hs traffic", 12,
            transcript_hash_sh, server_hs_traffic_secret) != 0 ||
        nci_tls_derive_traffic_keys_checked(
            server_hs_traffic_secret, &conn->write_keys, cipher_id) != 0 ||
        nci_tls_derive_traffic_keys_checked(
            client_hs_traffic_secret, &conn->read_keys, cipher_id) != 0) {
        neverc_platform_secure_zero(
            &conn->read_keys, sizeof(conn->read_keys));
        neverc_platform_secure_zero(
            &conn->write_keys, sizeof(conn->write_keys));
        neverc_platform_secure_zero(
            client_hs_traffic_secret, sizeof(client_hs_traffic_secret));
        neverc_platform_secure_zero(
            server_hs_traffic_secret, sizeof(server_hs_traffic_secret));
        neverc_platform_secure_zero(
            handshake_secret, sizeof(handshake_secret));
        neverc_platform_secure_zero(shared_secret, sizeof(shared_secret));
        neverc_platform_secure_zero(&ecdh_key, sizeof(ecdh_key));
        return nci_tls_fail(conn, TLS_ALERT_INTERNAL_ERROR);
    }
    conn->write_keys_active = 1;

    /* Send encrypted handshake messages */
    /* 1. EncryptedExtensions (ALPN when negotiated) */
    {
        uint8_t ee[4 + 2 + 2 + 2 + 2 + 1 + 255];
        size_t ee_len = 0;
        ee[ee_len++] = TLS_HS_ENCRYPTED_EXT;
        size_t body_len_pos = ee_len;
        ee_len += 3;
        size_t ext_len_pos = ee_len;
        ee_len += 2;
        size_t ext_start = ee_len;

        if (selected_alpn_len > 0) {
            if (selected_alpn_len > 255 || !selected_alpn)
                return nci_tls_fail(conn, TLS_ALERT_INTERNAL_ERROR);
            tls_put_u16(ee + ee_len, TLS_EXT_ALPN);
            ee_len += 2;
            tls_put_u16(ee + ee_len,
                    (uint16_t)(3 + selected_alpn_len));
            ee_len += 2;
            tls_put_u16(ee + ee_len,
                    (uint16_t)(1 + selected_alpn_len));
            ee_len += 2;
            ee[ee_len++] = (uint8_t)selected_alpn_len;
            memcpy(ee + ee_len, selected_alpn,
                   selected_alpn_len);
            ee_len += selected_alpn_len;
        }

        tls_put_u16(ee + ext_len_pos,
                (uint16_t)(ee_len - ext_start));
        tls_put_u24(ee + body_len_pos,
                (uint32_t)(ee_len - body_len_pos - 3));
        neverc_sha256_update(&transcript, ee, ee_len);
        if (nci_tls_send_encrypted_handshake(
                conn, ee, ee_len) != 0)
            return -1;
    }

    /* 2. Request and verify a client certificate when configured. */
    if (!conn->did_resume &&
        cfg->client_auth ==
            NEVERC_TLS_CLIENT_AUTH_REQUIRE_AND_VERIFY &&
        tls_send_certificate_request(conn, &transcript) != 0)
        return -1;

    /* 3. Server Certificate and CertificateVerify. */
    if (!conn->did_resume) {
        int sent_server_certificate = 0;
        if (tls_send_local_certificate(
                conn, cfg, &transcript, 0,
                &sent_server_certificate) != 0 ||
            !sent_server_certificate)
            return -1;
        if (tls_send_local_certificate_verify(
                conn, cfg, 1, &transcript) != 0)
            return -1;
    }

    /* 4. Server Finished */
    {
        uint8_t transcript_hash[32];
        {
            neverc_sha256_ctx copy = transcript;
            neverc_sha256_final(&copy, transcript_hash);
        }

        uint8_t finished_key[32];
        if (nci_tls_hkdf_expand_label(server_hs_traffic_secret, 32,
                           "finished", 8, NULL, 0, finished_key, 32) != 0)
            return nci_tls_fail(conn, TLS_ALERT_INTERNAL_ERROR);

        uint8_t verify_data[32];
        neverc_hmac_sha256(finished_key, 32,
                            transcript_hash, 32,
                            verify_data);

        uint8_t finished_msg[36];
        finished_msg[0] = TLS_HS_FINISHED;
        tls_put_u24(finished_msg + 1, 32);
        memcpy(finished_msg + 4, verify_data, 32);

        neverc_sha256_update(&transcript, finished_msg, 36);
        if (nci_tls_send_encrypted_handshake(
                conn, finished_msg, sizeof(finished_msg)) != 0)
            return -1;
    }

    /* Snapshot transcript through server Finished for application secrets. */
    uint8_t transcript_hash_server_finished[32];
    {
        neverc_sha256_ctx copy = transcript;
        neverc_sha256_final(&copy, transcript_hash_server_finished);
    }

    /* Receive optional client authentication flight and client Finished. */
    int got_client_finished = 0;
    uint8_t expected_client_handshake =
        (!conn->did_resume &&
         cfg->client_auth ==
             NEVERC_TLS_CLIENT_AUTH_REQUIRE_AND_VERIFY) ?
        TLS_HS_CERTIFICATE : TLS_HS_FINISHED;
    while (!got_client_finished) {
        const uint8_t *handshake_message = NULL;
        size_t handshake_message_len = 0;
        int available = nci_tls_next_handshake_message(
            conn, &handshake_message, &handshake_message_len);
        if (available < 0)
            return nci_tls_protocol_error(
                conn, TLS_ALERT_DECODE_ERROR,
                "client handshake message exceeds the configured limit");
        if (available == 0) {
            uint8_t inner_type;
            uint8_t record_data[TLS_MAX_PLAINTEXT];
            size_t record_len;
            if (nci_tls_recv_decrypt(
                    conn, &inner_type,
                    record_data, &record_len) != 0)
                return -1;
            if (inner_type != TLS_CT_HANDSHAKE) {
                if (inner_type == TLS_CT_ALERT)
                    return nci_tls_error(
                        conn, "client sent an alert during handshake");
                return nci_tls_protocol_error(
                    conn, TLS_ALERT_UNEXPECTED_MESSAGE,
                    "client sent non-handshake data during handshake");
            }
            if (record_len == 0)
                return nci_tls_protocol_error(
                    conn, TLS_ALERT_DECODE_ERROR,
                    "client sent an empty handshake record");
            if (record_len >
                TLS_MAX_HANDSHAKE - conn->handshake_len)
                return nci_tls_protocol_error(
                    conn, TLS_ALERT_DECODE_ERROR,
                    "client handshake reassembly exceeds the limit");
            if (nci_tls_append_handshake_bytes(
                    conn, record_data, record_len) != 0)
                return nci_tls_protocol_error(
                    conn, TLS_ALERT_INTERNAL_ERROR,
                    "client handshake reassembly allocation failed");
            continue;
        }

        uint8_t message_type = handshake_message[0];
        size_t message_body_len = handshake_message_len - 4;
        if (message_type != expected_client_handshake)
            return nci_tls_protocol_error(
                conn, TLS_ALERT_UNEXPECTED_MESSAGE,
                "client handshake message arrived out of order");

        const uint8_t *message = handshake_message + 4;
        if (message_type == TLS_HS_CERTIFICATE) {
            int certificate_result = tls_store_peer_certificate(
                conn, message, message_body_len, 1);
            if (certificate_result == 1)
                return nci_tls_fail(
                    conn, TLS_ALERT_CERTIFICATE_REQUIRED);
            if (certificate_result != 0)
                return nci_tls_fail(conn, TLS_ALERT_DECODE_ERROR);
            neverc_sha256_update(
                &transcript,
                handshake_message, handshake_message_len);
            expected_client_handshake = TLS_HS_CERT_VERIFY;
        } else if (message_type == TLS_HS_CERT_VERIFY) {
            if (!conn->peer_cert || message_body_len < 4)
                return nci_tls_fail(conn, TLS_ALERT_DECODE_ERROR);
            uint16_t signature_scheme = tls_get_u16(message);
            size_t signature_len = tls_get_u16(message + 2);
            if (signature_len == 0 ||
                signature_len != message_body_len - 4)
                return nci_tls_fail(conn, TLS_ALERT_DECODE_ERROR);

            uint8_t transcript_hash[32];
            neverc_sha256_ctx copy = transcript;
            neverc_sha256_final(&copy, transcript_hash);
            neverc_x509_cert_t certificate;
            if (neverc_x509_parse_certificate(
                    &certificate, conn->peer_cert,
                    conn->peer_cert_len) != 0)
                return nci_tls_fail(
                    conn, TLS_ALERT_BAD_CERTIFICATE);
            int signature_result =
                neverc_tls_verify_certificate_verify(
                    &certificate, signature_scheme, 0,
                    transcript_hash, sizeof(transcript_hash),
                    message + 4, signature_len);
            neverc_x509_cert_free(&certificate);
            if (signature_result != 0)
                return nci_tls_fail(
                    conn, TLS_ALERT_DECRYPT_ERROR);
            if (nci_tls_verify_certificate_chain(
                    cfg, conn->peer_cert,
                    conn->peer_cert_len,
                    conn->peer_intermediates, NULL, NULL,
                    NEVERC_X509_EXT_KEY_USAGE_CLIENT_AUTH,
                    0) != 0)
                return nci_tls_fail(
                    conn, TLS_ALERT_BAD_CERTIFICATE);

            neverc_sha256_update(
                &transcript,
                handshake_message, handshake_message_len);
            expected_client_handshake = TLS_HS_FINISHED;
        } else {
            if (message_body_len != 32)
                return nci_tls_fail(conn, TLS_ALERT_DECODE_ERROR);

            uint8_t transcript_hash[32];
            neverc_sha256_ctx copy = transcript;
            neverc_sha256_final(&copy, transcript_hash);
            uint8_t finished_key[32];
            if (nci_tls_hkdf_expand_label(
                    client_hs_traffic_secret, 32,
                    "finished", 8, NULL, 0,
                    finished_key, sizeof(finished_key)) != 0)
                return nci_tls_fail(conn, TLS_ALERT_INTERNAL_ERROR);
            uint8_t expected[32];
            neverc_hmac_sha256(
                finished_key, sizeof(finished_key),
                transcript_hash, sizeof(transcript_hash),
                expected);
            if (!neverc_subtle_constant_time_compare(
                    message, expected, sizeof(expected)))
                return nci_tls_fail(
                    conn, TLS_ALERT_DECRYPT_ERROR);

            neverc_sha256_update(
                &transcript,
                handshake_message, handshake_message_len);
            got_client_finished = 1;
        }

        if (nci_tls_consume_handshake_message(
                conn, handshake_message_len) != 0)
            return nci_tls_protocol_error(
                conn, TLS_ALERT_INTERNAL_ERROR,
                "failed to advance client handshake buffer");
        if (got_client_finished && conn->handshake_len != 0)
            return nci_tls_protocol_error(
                conn, TLS_ALERT_UNEXPECTED_MESSAGE,
                "trailing data after client Finished");
    }

    /* Derive application traffic keys */
    uint8_t empty_hash[32];
    neverc_sha256_sum(NULL, 0, empty_hash);
    uint8_t master_derived[32];
    uint8_t master_secret[32];
    uint8_t client_app_secret[32];
    uint8_t server_app_secret[32];
    uint8_t transcript_hash_client_finished[32];
    int key_schedule_result = -1;
    if (nci_tls_derive_secret_checked(
            handshake_secret, "derived", 7,
            empty_hash, master_derived) != 0 ||
        nci_tls_hkdf_extract_zero_ikm(
            master_secret, master_derived, 32) != 0 ||
        nci_tls_derive_secret_checked(
            master_secret, "c ap traffic", 12,
            transcript_hash_server_finished, client_app_secret) != 0 ||
        nci_tls_derive_secret_checked(
            master_secret, "s ap traffic", 12,
            transcript_hash_server_finished, server_app_secret) != 0)
        goto server_key_schedule_cleanup;

    {
        neverc_sha256_ctx copy = transcript;
        neverc_sha256_final(
            &copy, transcript_hash_client_finished);
    }
    if (nci_tls_derive_secret_checked(
            master_secret, "res master", 10,
            transcript_hash_client_finished,
            conn->resumption_master_secret) != 0 ||
        nci_tls_set_application_keys(
            conn, cipher_id,
            client_app_secret, server_app_secret) != 0)
        goto server_key_schedule_cleanup;

    nci_tls_clear_handshake_buffer(conn);
    conn->handshake_done = 1;
    if (cfg->client_auth == NEVERC_TLS_CLIENT_AUTH_NONE &&
        nci_tls_send_new_session_ticket(conn) != 0) {
        (void)nci_tls_error(
            conn, "failed to send TLS NewSessionTicket");
        goto server_key_schedule_cleanup;
    }
    key_schedule_result = 0;

server_key_schedule_cleanup:
    neverc_platform_secure_zero(
        client_app_secret, sizeof(client_app_secret));
    neverc_platform_secure_zero(
        server_app_secret, sizeof(server_app_secret));
    neverc_platform_secure_zero(master_secret, sizeof(master_secret));
    neverc_platform_secure_zero(master_derived, sizeof(master_derived));
    neverc_platform_secure_zero(
        client_hs_traffic_secret, sizeof(client_hs_traffic_secret));
    neverc_platform_secure_zero(
        server_hs_traffic_secret, sizeof(server_hs_traffic_secret));
    neverc_platform_secure_zero(
        handshake_secret, sizeof(handshake_secret));
    neverc_platform_secure_zero(shared_secret, sizeof(shared_secret));
    neverc_platform_secure_zero(&ecdh_key, sizeof(ecdh_key));
    neverc_platform_secure_zero(empty_hash, sizeof(empty_hash));
    neverc_platform_secure_zero(
        transcript_hash_client_finished,
        sizeof(transcript_hash_client_finished));
    return key_schedule_result;
}

typedef enum {
    TLS_ASYNC_SERVER_WAIT_CLIENT_HELLO,
    TLS_ASYNC_SERVER_WAIT_CLIENT_FLIGHT,
    TLS_ASYNC_SERVER_FLUSH_FINAL,
    TLS_ASYNC_SERVER_COMPLETE,
} tls_async_server_phase_t;

typedef struct {
    tls_async_server_phase_t phase;
    neverc_sha256_ctx transcript;
    tls_cipher_id_t cipher_id;
    uint8_t handshake_secret[32];
    uint8_t client_hs_traffic_secret[32];
    uint8_t server_hs_traffic_secret[32];
    uint8_t transcript_hash_server_finished[32];
    uint8_t expected_client_handshake;
} tls_async_server_state_t;

static int tls_async_prepare_server_flight(
    neverc_tls_conn_t *conn, neverc_tls_config_t *cfg,
    tls_async_server_state_t *async,
    const uint8_t *client_hello_message,
    size_t client_hello_message_len) {
    tls_client_hello_info_t client_hello;
    uint8_t client_hello_alert = TLS_ALERT_DECODE_ERROR;
    uint8_t selected_psk[32] = {0};
    uint8_t shared_secret[32] = {0};
    neverc_ecdh_key_t ecdh_key;
    memset(&ecdh_key, 0, sizeof(ecdh_key));
    int result = -1;

    if (tls_parse_client_hello(
            client_hello_message, client_hello_message_len,
            &client_hello, &client_hello_alert) != 0) {
        (void)nci_tls_fail(conn, client_hello_alert);
        goto cleanup;
    }
    if (client_hello.server_name_len > 0 &&
        tls_set_owned_string(
            &conn->server_name, client_hello.server_name,
            client_hello.server_name_len) != 0) {
        (void)nci_tls_fail(conn, TLS_ALERT_INTERNAL_ERROR);
        goto cleanup;
    }

    const char *selected_alpn = NULL;
    size_t selected_alpn_len = 0;
    uint8_t alpn_alert = TLS_ALERT_INTERNAL_ERROR;
    if (tls_negotiate_alpn(
            cfg, client_hello.alpn_protocols,
            client_hello.alpn_count, &selected_alpn,
            &selected_alpn_len, &alpn_alert) != 0) {
        (void)nci_tls_fail(conn, alpn_alert);
        goto cleanup;
    }
    if (selected_alpn_len > 0 &&
        tls_set_owned_string(
            &conn->alpn, selected_alpn,
            selected_alpn_len) != 0) {
        (void)nci_tls_fail(conn, TLS_ALERT_INTERNAL_ERROR);
        goto cleanup;
    }

    int selected_psk_index = -1;
    if (cfg->client_auth == NEVERC_TLS_CLIENT_AUTH_NONE) {
        for (size_t i = 0; i < client_hello.offered_psk_count; i++) {
            tls_offered_psk_t *offered = &client_hello.offered_psks[i];
            if (!nci_tls_lookup_server_session(
                    cfg, offered->identity, offered->identity_len,
                    offered->obfuscated_age,
                    (const uint8_t *)client_hello.server_name,
                    client_hello.server_name_len,
                    selected_alpn, selected_alpn_len, selected_psk))
                continue;
            uint8_t expected_binder[32];
            if (nci_tls_compute_resumption_binder(
                    selected_psk, client_hello_message,
                    client_hello.binder_transcript_len,
                    expected_binder) != 0) {
                (void)nci_tls_fail(conn, TLS_ALERT_INTERNAL_ERROR);
                goto cleanup;
            }
            int binder_valid = offered->binder_len == sizeof(expected_binder) &&
                neverc_subtle_constant_time_compare(
                    offered->binder, expected_binder,
                    sizeof(expected_binder));
            neverc_platform_secure_zero(
                expected_binder, sizeof(expected_binder));
            if (!binder_valid) {
                (void)nci_tls_fail(conn, TLS_ALERT_DECRYPT_ERROR);
                goto cleanup;
            }
            selected_psk_index = (int)i;
            break;
        }
    }
    conn->did_resume = selected_psk_index >= 0;

    neverc_sha256_update(
        &async->transcript,
        client_hello_message, client_hello_message_len);
    if (nci_tls_consume_handshake_message(
            conn, client_hello_message_len) != 0 ||
        conn->handshake_len != 0) {
        (void)nci_tls_protocol_error(
            conn, TLS_ALERT_UNEXPECTED_MESSAGE,
            "trailing plaintext data after ClientHello");
        goto cleanup;
    }

    async->cipher_id = client_hello.cipher_id;
    conn->cipher_suite = client_hello.selected_cipher;
    if (neverc_ecdh_generate_key(
            NEVERC_ECDH_CURVE_X25519, &ecdh_key) != 0)
        goto cleanup;
    uint8_t server_public_key[32];
    if (neverc_ecdh_public_key_bytes(
            &ecdh_key, server_public_key,
            sizeof(server_public_key)) != 32)
        goto cleanup;

    uint8_t server_hello[256];
    size_t server_hello_len = 0;
    server_hello[server_hello_len++] = TLS_HS_SERVER_HELLO;
    size_t body_length_position = server_hello_len;
    server_hello_len += 3;
    tls_put_u16(server_hello + server_hello_len, TLS_LEGACY_VERSION);
    server_hello_len += 2;
    if (neverc_crypto_rand_read(
            server_hello + server_hello_len, 32) != 0) {
        (void)nci_tls_fail(conn, TLS_ALERT_INTERNAL_ERROR);
        goto cleanup;
    }
    server_hello_len += 32;
    server_hello[server_hello_len++] =
        (uint8_t)client_hello.session_id_len;
    if (client_hello.session_id_len > 0) {
        memcpy(server_hello + server_hello_len,
               client_hello.session_id,
               client_hello.session_id_len);
        server_hello_len += client_hello.session_id_len;
    }
    tls_put_u16(server_hello + server_hello_len,
                client_hello.selected_cipher);
    server_hello_len += 2;
    server_hello[server_hello_len++] = 0;
    size_t extensions_length_position = server_hello_len;
    server_hello_len += 2;
    size_t extensions_start = server_hello_len;
    tls_put_u16(server_hello + server_hello_len,
                TLS_EXT_SUPPORTED_VERSIONS);
    server_hello_len += 2;
    tls_put_u16(server_hello + server_hello_len, 2);
    server_hello_len += 2;
    tls_put_u16(server_hello + server_hello_len,
                NEVERC_TLS_VERSION_13);
    server_hello_len += 2;
    tls_put_u16(server_hello + server_hello_len, TLS_EXT_KEY_SHARE);
    server_hello_len += 2;
    tls_put_u16(server_hello + server_hello_len, 36);
    server_hello_len += 2;
    tls_put_u16(server_hello + server_hello_len,
                NEVERC_TLS_GROUP_X25519);
    server_hello_len += 2;
    tls_put_u16(server_hello + server_hello_len, 32);
    server_hello_len += 2;
    memcpy(server_hello + server_hello_len,
           server_public_key, sizeof(server_public_key));
    server_hello_len += sizeof(server_public_key);
    if (selected_psk_index >= 0) {
        tls_put_u16(server_hello + server_hello_len,
                    TLS_EXT_PRE_SHARED_KEY);
        server_hello_len += 2;
        tls_put_u16(server_hello + server_hello_len, 2);
        server_hello_len += 2;
        tls_put_u16(server_hello + server_hello_len,
                    (uint16_t)selected_psk_index);
        server_hello_len += 2;
    }
    tls_put_u16(server_hello + extensions_length_position,
                (uint16_t)(server_hello_len - extensions_start));
    tls_put_u24(server_hello + body_length_position,
                (uint32_t)(server_hello_len -
                           body_length_position - 3));
    neverc_sha256_update(
        &async->transcript, server_hello, server_hello_len);
    if (nci_tls_send_plain_handshake(
            conn, server_hello, server_hello_len) != 0)
        goto cleanup;
    uint8_t change_cipher_spec = 1;
    if (nci_tls_send_plain_record(
            conn, TLS_CT_CHANGE_CIPHER_SPEC,
            &change_cipher_spec, 1) != 0)
        goto cleanup;

    if (neverc_ecdh_compute(
            &ecdh_key, client_hello.client_public_key, 32,
            shared_secret, sizeof(shared_secret)) != 32) {
        (void)nci_tls_fail(conn, TLS_ALERT_ILLEGAL_PARAMETER);
        goto cleanup;
    }
    if (nci_tls_derive_handshake_secret(
            shared_secret,
            selected_psk_index >= 0 ? selected_psk : NULL,
            async->handshake_secret) != 0) {
        (void)nci_tls_fail(conn, TLS_ALERT_INTERNAL_ERROR);
        goto cleanup;
    }
    uint8_t transcript_hash_server_hello[32];
    neverc_sha256_ctx transcript_copy = async->transcript;
    neverc_sha256_final(
        &transcript_copy, transcript_hash_server_hello);
    if (nci_tls_derive_secret_checked(
            async->handshake_secret, "c hs traffic", 12,
            transcript_hash_server_hello,
            async->client_hs_traffic_secret) != 0 ||
        nci_tls_derive_secret_checked(
            async->handshake_secret, "s hs traffic", 12,
            transcript_hash_server_hello,
            async->server_hs_traffic_secret) != 0 ||
        nci_tls_derive_traffic_keys_checked(
            async->server_hs_traffic_secret,
            &conn->write_keys, async->cipher_id) != 0 ||
        nci_tls_derive_traffic_keys_checked(
            async->client_hs_traffic_secret,
            &conn->read_keys, async->cipher_id) != 0) {
        neverc_platform_secure_zero(
            &conn->read_keys, sizeof(conn->read_keys));
        neverc_platform_secure_zero(
            &conn->write_keys, sizeof(conn->write_keys));
        (void)nci_tls_fail(conn, TLS_ALERT_INTERNAL_ERROR);
        goto cleanup;
    }
    conn->write_keys_active = 1;

    uint8_t encrypted_extensions[4 + 2 + 2 + 2 + 2 + 1 + 255];
    size_t encrypted_extensions_len = 0;
    encrypted_extensions[encrypted_extensions_len++] =
        TLS_HS_ENCRYPTED_EXT;
    size_t ee_body_length_position = encrypted_extensions_len;
    encrypted_extensions_len += 3;
    size_t ee_extensions_length_position = encrypted_extensions_len;
    encrypted_extensions_len += 2;
    size_t ee_extensions_start = encrypted_extensions_len;
    if (selected_alpn_len > 0) {
        if (!selected_alpn || selected_alpn_len > 255) goto cleanup;
        tls_put_u16(encrypted_extensions + encrypted_extensions_len,
                    TLS_EXT_ALPN);
        encrypted_extensions_len += 2;
        tls_put_u16(encrypted_extensions + encrypted_extensions_len,
                    (uint16_t)(3 + selected_alpn_len));
        encrypted_extensions_len += 2;
        tls_put_u16(encrypted_extensions + encrypted_extensions_len,
                    (uint16_t)(1 + selected_alpn_len));
        encrypted_extensions_len += 2;
        encrypted_extensions[encrypted_extensions_len++] =
            (uint8_t)selected_alpn_len;
        memcpy(encrypted_extensions + encrypted_extensions_len,
               selected_alpn, selected_alpn_len);
        encrypted_extensions_len += selected_alpn_len;
    }
    tls_put_u16(encrypted_extensions + ee_extensions_length_position,
                (uint16_t)(encrypted_extensions_len -
                           ee_extensions_start));
    tls_put_u24(encrypted_extensions + ee_body_length_position,
                (uint32_t)(encrypted_extensions_len -
                           ee_body_length_position - 3));
    neverc_sha256_update(
        &async->transcript,
        encrypted_extensions, encrypted_extensions_len);
    if (nci_tls_send_encrypted_handshake(
            conn, encrypted_extensions,
            encrypted_extensions_len) != 0)
        goto cleanup;

    if (!conn->did_resume &&
        cfg->client_auth ==
            NEVERC_TLS_CLIENT_AUTH_REQUIRE_AND_VERIFY &&
        tls_send_certificate_request(
            conn, &async->transcript) != 0)
        goto cleanup;
    if (!conn->did_resume) {
        int sent_certificate = 0;
        if (tls_send_local_certificate(
                conn, cfg, &async->transcript, 0,
                &sent_certificate) != 0 || !sent_certificate ||
            tls_send_local_certificate_verify(
                conn, cfg, 1, &async->transcript) != 0)
            goto cleanup;
    }

    uint8_t transcript_hash[32];
    transcript_copy = async->transcript;
    neverc_sha256_final(&transcript_copy, transcript_hash);
    uint8_t finished_key[32];
    if (nci_tls_hkdf_expand_label(
            async->server_hs_traffic_secret, 32,
            "finished", 8, NULL, 0,
            finished_key, sizeof(finished_key)) != 0)
        goto cleanup;
    uint8_t finished_message[36];
    finished_message[0] = TLS_HS_FINISHED;
    tls_put_u24(finished_message + 1, 32);
    neverc_hmac_sha256(
        finished_key, sizeof(finished_key),
        transcript_hash, sizeof(transcript_hash),
        finished_message + 4);
    neverc_sha256_update(
        &async->transcript,
        finished_message, sizeof(finished_message));
    if (nci_tls_send_encrypted_handshake(
            conn, finished_message,
            sizeof(finished_message)) != 0)
        goto cleanup;
    transcript_copy = async->transcript;
    neverc_sha256_final(
        &transcript_copy,
        async->transcript_hash_server_finished);
    async->expected_client_handshake =
        (!conn->did_resume &&
         cfg->client_auth ==
             NEVERC_TLS_CLIENT_AUTH_REQUIRE_AND_VERIFY)
        ? TLS_HS_CERTIFICATE : TLS_HS_FINISHED;
    async->phase = TLS_ASYNC_SERVER_WAIT_CLIENT_FLIGHT;
    result = 0;

cleanup:
    neverc_platform_secure_zero(selected_psk, sizeof(selected_psk));
    neverc_platform_secure_zero(shared_secret, sizeof(shared_secret));
    neverc_platform_secure_zero(&ecdh_key, sizeof(ecdh_key));
    return result;
}

static int tls_async_finish_server_handshake(
    neverc_tls_conn_t *conn, neverc_tls_config_t *cfg,
    tls_async_server_state_t *async) {
    uint8_t empty_hash[32];
    neverc_sha256_sum(NULL, 0, empty_hash);
    uint8_t master_derived[32];
    uint8_t master_secret[32];
    uint8_t client_app_secret[32];
    uint8_t server_app_secret[32];
    uint8_t transcript_hash_client_finished[32];
    neverc_sha256_ctx transcript_copy;
    int result = -1;
    if (nci_tls_derive_secret_checked(
            async->handshake_secret, "derived", 7,
            empty_hash, master_derived) != 0 ||
        nci_tls_hkdf_extract_zero_ikm(
            master_secret, master_derived, 32) != 0 ||
        nci_tls_derive_secret_checked(
            master_secret, "c ap traffic", 12,
            async->transcript_hash_server_finished,
            client_app_secret) != 0 ||
        nci_tls_derive_secret_checked(
            master_secret, "s ap traffic", 12,
            async->transcript_hash_server_finished,
            server_app_secret) != 0)
        goto cleanup;

    transcript_copy = async->transcript;
    neverc_sha256_final(
        &transcript_copy, transcript_hash_client_finished);
    if (nci_tls_derive_secret_checked(
            master_secret, "res master", 10,
            transcript_hash_client_finished,
            conn->resumption_master_secret) != 0 ||
        nci_tls_set_application_keys(
            conn, async->cipher_id,
            client_app_secret, server_app_secret) != 0)
        goto cleanup;

    nci_tls_clear_handshake_buffer(conn);
    conn->handshake_done = 1;
    if (cfg->client_auth == NEVERC_TLS_CLIENT_AUTH_NONE &&
        nci_tls_send_new_session_ticket(conn) != 0) {
        (void)nci_tls_error(
            conn, "failed to queue TLS NewSessionTicket");
        goto cleanup;
    }
    async->phase = TLS_ASYNC_SERVER_FLUSH_FINAL;
    result = 0;

cleanup:
    neverc_platform_secure_zero(
        client_app_secret, sizeof(client_app_secret));
    neverc_platform_secure_zero(
        server_app_secret, sizeof(server_app_secret));
    neverc_platform_secure_zero(master_secret, sizeof(master_secret));
    neverc_platform_secure_zero(master_derived, sizeof(master_derived));
    neverc_platform_secure_zero(empty_hash, sizeof(empty_hash));
    neverc_platform_secure_zero(
        transcript_hash_client_finished,
        sizeof(transcript_hash_client_finished));
    neverc_platform_secure_zero(&transcript_copy, sizeof(transcript_copy));
    return result;
}

static int tls_async_process_client_flight(
    neverc_tls_conn_t *conn, neverc_tls_config_t *cfg,
    tls_async_server_state_t *async) {
    for (;;) {
        const uint8_t *handshake_message = NULL;
        size_t handshake_message_len = 0;
        int available = nci_tls_next_handshake_message(
            conn, &handshake_message, &handshake_message_len);
        if (available < 0)
            return nci_tls_protocol_error(
                conn, TLS_ALERT_DECODE_ERROR,
                "client handshake message exceeds the configured limit");
        if (available == 0) {
            uint8_t inner_type;
            uint8_t record_data[TLS_MAX_PLAINTEXT];
            size_t record_len;
            int receive_result = nci_tls_recv_decrypt(
                conn, &inner_type, record_data, &record_len);
            if (receive_result != 0) return receive_result;
            if (inner_type == TLS_CT_ALERT)
                return nci_tls_error(
                    conn, "client sent an alert during handshake");
            if (inner_type != TLS_CT_HANDSHAKE || record_len == 0)
                return nci_tls_protocol_error(
                    conn, TLS_ALERT_UNEXPECTED_MESSAGE,
                    "client sent non-handshake data during handshake");
            if (record_len > TLS_MAX_HANDSHAKE - conn->handshake_len ||
                nci_tls_append_handshake_bytes(
                    conn, record_data, record_len) != 0)
                return nci_tls_protocol_error(
                    conn, TLS_ALERT_INTERNAL_ERROR,
                    "client handshake reassembly failed");
            continue;
        }

        uint8_t message_type = handshake_message[0];
        size_t message_body_len = handshake_message_len - 4;
        const uint8_t *message = handshake_message + 4;
        if (message_type != async->expected_client_handshake)
            return nci_tls_protocol_error(
                conn, TLS_ALERT_UNEXPECTED_MESSAGE,
                "client handshake message arrived out of order");

        if (message_type == TLS_HS_CERTIFICATE) {
            int certificate_result = tls_store_peer_certificate(
                conn, message, message_body_len, 1);
            if (certificate_result == 1)
                return nci_tls_fail(
                    conn, TLS_ALERT_CERTIFICATE_REQUIRED);
            if (certificate_result != 0)
                return nci_tls_fail(conn, TLS_ALERT_DECODE_ERROR);
            neverc_sha256_update(
                &async->transcript,
                handshake_message, handshake_message_len);
            async->expected_client_handshake = TLS_HS_CERT_VERIFY;
        } else if (message_type == TLS_HS_CERT_VERIFY) {
            if (!conn->peer_cert || message_body_len < 4)
                return nci_tls_fail(conn, TLS_ALERT_DECODE_ERROR);
            uint16_t signature_scheme = tls_get_u16(message);
            size_t signature_len = tls_get_u16(message + 2);
            if (signature_len == 0 ||
                signature_len != message_body_len - 4)
                return nci_tls_fail(conn, TLS_ALERT_DECODE_ERROR);
            uint8_t transcript_hash[32];
            neverc_sha256_ctx transcript_copy = async->transcript;
            neverc_sha256_final(&transcript_copy, transcript_hash);
            neverc_x509_cert_t certificate;
            if (neverc_x509_parse_certificate(
                    &certificate, conn->peer_cert,
                    conn->peer_cert_len) != 0)
                return nci_tls_fail(
                    conn, TLS_ALERT_BAD_CERTIFICATE);
            int signature_result =
                neverc_tls_verify_certificate_verify(
                    &certificate, signature_scheme, 0,
                    transcript_hash, sizeof(transcript_hash),
                    message + 4, signature_len);
            neverc_x509_cert_free(&certificate);
            if (signature_result != 0)
                return nci_tls_fail(
                    conn, TLS_ALERT_DECRYPT_ERROR);
            if (nci_tls_verify_certificate_chain(
                    cfg, conn->peer_cert, conn->peer_cert_len,
                    conn->peer_intermediates, NULL, NULL,
                    NEVERC_X509_EXT_KEY_USAGE_CLIENT_AUTH,
                    0) != 0)
                return nci_tls_fail(
                    conn, TLS_ALERT_BAD_CERTIFICATE);
            neverc_sha256_update(
                &async->transcript,
                handshake_message, handshake_message_len);
            async->expected_client_handshake = TLS_HS_FINISHED;
        } else {
            if (message_body_len != 32)
                return nci_tls_fail(conn, TLS_ALERT_DECODE_ERROR);
            uint8_t transcript_hash[32];
            neverc_sha256_ctx transcript_copy = async->transcript;
            neverc_sha256_final(&transcript_copy, transcript_hash);
            uint8_t finished_key[32];
            if (nci_tls_hkdf_expand_label(
                    async->client_hs_traffic_secret, 32,
                    "finished", 8, NULL, 0,
                    finished_key, sizeof(finished_key)) != 0)
                return nci_tls_fail(
                    conn, TLS_ALERT_INTERNAL_ERROR);
            uint8_t expected[32];
            neverc_hmac_sha256(
                finished_key, sizeof(finished_key),
                transcript_hash, sizeof(transcript_hash), expected);
            if (!neverc_subtle_constant_time_compare(
                    message, expected, sizeof(expected)))
                return nci_tls_fail(
                    conn, TLS_ALERT_DECRYPT_ERROR);
            neverc_sha256_update(
                &async->transcript,
                handshake_message, handshake_message_len);
        }
        if (nci_tls_consume_handshake_message(
                conn, handshake_message_len) != 0)
            return nci_tls_protocol_error(
                conn, TLS_ALERT_INTERNAL_ERROR,
                "failed to advance client handshake buffer");
        if (message_type == TLS_HS_FINISHED) {
            if (conn->handshake_len != 0)
                return nci_tls_protocol_error(
                    conn, TLS_ALERT_UNEXPECTED_MESSAGE,
                    "trailing data after client Finished");
            return tls_async_finish_server_handshake(
                conn, cfg, async);
        }
    }
}

int nci_tls_server_handshake_begin(
    neverc_tls_conn_t *conn, neverc_tls_config_t *cfg) {
    if (!conn || !cfg || !cfg->cert_der || !cfg->key_der ||
        conn->async_handshake)
        return -1;
    tls_async_server_state_t *async =
        (tls_async_server_state_t *)calloc(1, sizeof(*async));
    if (!async) return -1;
    async->phase = TLS_ASYNC_SERVER_WAIT_CLIENT_HELLO;
    neverc_sha256_init(&async->transcript);
    conn->nonblocking_io = 1;
    conn->async_handshake = async;
    return 0;
}

int nci_tls_server_handshake_step(neverc_tls_conn_t *conn) {
    if (!conn || !conn->async_handshake || !conn->config)
        return -1;
    tls_async_server_state_t *async =
        (tls_async_server_state_t *)conn->async_handshake;

    int flush_result = nci_tls_flush_pending_write(conn);
    if (flush_result != 0) return flush_result;
    for (;;) {
        if (async->phase == TLS_ASYNC_SERVER_WAIT_CLIENT_HELLO) {
            const uint8_t *client_hello = NULL;
            size_t client_hello_len = 0;
            int receive_result = nci_tls_recv_plain_handshake_message(
                conn, TLS_HS_CLIENT_HELLO,
                &client_hello, &client_hello_len);
            if (receive_result != 0) return receive_result;
            if (tls_async_prepare_server_flight(
                    conn, conn->config, async,
                    client_hello, client_hello_len) != 0)
                return -1;
            flush_result = nci_tls_flush_pending_write(conn);
            if (flush_result != 0) return flush_result;
            continue;
        }
        if (async->phase == TLS_ASYNC_SERVER_WAIT_CLIENT_FLIGHT) {
            int process_result = tls_async_process_client_flight(
                conn, conn->config, async);
            if (process_result != 0) return process_result;
            continue;
        }
        if (async->phase == TLS_ASYNC_SERVER_FLUSH_FINAL) {
            flush_result = nci_tls_flush_pending_write(conn);
            if (flush_result != 0) return flush_result;
            async->phase = TLS_ASYNC_SERVER_COMPLETE;
            continue;
        }
        return 0;
    }
}

void nci_tls_async_handshake_free(neverc_tls_conn_t *conn) {
    if (!conn || !conn->async_handshake) return;
    tls_async_server_state_t *async =
        (tls_async_server_state_t *)conn->async_handshake;
    neverc_platform_secure_zero(async, sizeof(*async));
    free(async);
    conn->async_handshake = NULL;
}

/* ======================================================================
 * TLS 1.3 Post-Handshake Messages
 * ====================================================================== */

int nci_tls_send_key_update_message(
    neverc_tls_conn_t *conn, int request_peer_update) {
    if (!conn || !conn->mutexes_initialized ||
        (request_peer_update != 0 && request_peer_update != 1))
        return -1;
    tls_mutex_lock(&conn->write_mutex);
    if (!conn->tcp || !conn->handshake_done ||
        !conn->application_keys_active || conn->closed ||
        conn->write_closed) {
        tls_mutex_unlock(&conn->write_mutex);
        return -1;
    }

    uint8_t message[5] = {
        TLS_HS_KEY_UPDATE, 0, 0, 1,
        (uint8_t)request_peer_update
    };
    if (nci_tls_send_encrypted_unlocked(
            conn, TLS_CT_HANDSHAKE,
            message, sizeof(message)) != 0) {
        conn->closed = 1;
        int result =
            nci_tls_error(conn, "failed to send TLS KeyUpdate");
        tls_mutex_unlock(&conn->write_mutex);
        return result;
    }
    if (nci_tls_update_traffic_secret(
            conn->write_traffic_secret,
            &conn->write_keys) != 0) {
        conn->closed = 1;
        int result =
            nci_tls_error(conn, "failed to rotate TLS write keys");
        tls_mutex_unlock(&conn->write_mutex);
        return result;
    }
    tls_mutex_unlock(&conn->write_mutex);
    return 0;
}

int nci_tls_send_new_session_ticket(neverc_tls_conn_t *conn) {
    if (!conn || !conn->is_server || !conn->config ||
        !conn->handshake_done ||
        !conn->application_keys_active)
        return -1;

    uint8_t ticket[TLS_SERVER_TICKET_SIZE];
    uint8_t nonce[TLS_TICKET_NONCE_SIZE];
    uint8_t age_add_bytes[4];
    uint8_t psk[TLS_HASH_SIZE_SHA256];
    if (neverc_crypto_rand_read(ticket, sizeof(ticket)) != 0 ||
        neverc_crypto_rand_read(nonce, sizeof(nonce)) != 0 ||
        neverc_crypto_rand_read(
            age_add_bytes, sizeof(age_add_bytes)) != 0)
        return -1;
    uint32_t age_add = tls_get_u32(age_add_bytes);
    uint64_t issued_at_ms = nci_tls_wall_time_ms();
    if (issued_at_ms == 0 ||
        nci_tls_derive_resumption_psk(
            conn->resumption_master_secret,
            nonce, sizeof(nonce), psk) != 0) {
        neverc_platform_secure_zero(psk, sizeof(psk));
        return -1;
    }

    uint8_t message[
        4 + 4 + 4 + 1 + TLS_TICKET_NONCE_SIZE +
        2 + TLS_SERVER_TICKET_SIZE + 2];
    size_t pos = 0;
    message[pos++] = TLS_HS_NEW_SESSION_TICKET;
    tls_put_u24(message + pos, (uint32_t)(sizeof(message) - 4));
    pos += 3;
    tls_put_u32(message + pos, TLS_TICKET_LIFETIME);
    pos += 4;
    tls_put_u32(message + pos, age_add);
    pos += 4;
    message[pos++] = TLS_TICKET_NONCE_SIZE;
    memcpy(message + pos, nonce, sizeof(nonce));
    pos += sizeof(nonce);
    tls_put_u16(message + pos, TLS_SERVER_TICKET_SIZE);
    pos += 2;
    memcpy(message + pos, ticket, sizeof(ticket));
    pos += sizeof(ticket);
    tls_put_u16(message + pos, 0);
    pos += 2;
    if (pos != sizeof(message)) {
        neverc_platform_secure_zero(psk, sizeof(psk));
        return -1;
    }

    nci_tls_store_server_session(
        conn->config, ticket, psk,
        TLS_TICKET_LIFETIME, age_add, issued_at_ms,
        conn->server_name,
        conn->server_name ? strlen(conn->server_name) : 0,
        conn->alpn,
        conn->alpn ? strlen(conn->alpn) : 0);
    neverc_platform_secure_zero(psk, sizeof(psk));
    return nci_tls_send_encrypted_handshake(
        conn, message, sizeof(message));
}

int nci_tls_parse_new_session_ticket(
    neverc_tls_conn_t *conn,
    const uint8_t *body, size_t body_len) {
    if (!conn || !conn->config || !body || body_len < 13)
        return -1;

    uint32_t lifetime = tls_get_u32(body);
    uint32_t age_add = tls_get_u32(body + 4);
    size_t pos = 8;
    size_t nonce_len = body[pos++];
    if (nonce_len > body_len - pos)
        return -1;
    const uint8_t *nonce = body + pos;
    pos += nonce_len;

    if (body_len - pos < 2)
        return -1;
    size_t ticket_len = tls_get_u16(body + pos);
    pos += 2;
    if (ticket_len == 0 || ticket_len > body_len - pos)
        return -1;
    const uint8_t *ticket = body + pos;
    pos += ticket_len;

    if (body_len - pos < 2)
        return -1;
    size_t extensions_len = tls_get_u16(body + pos);
    pos += 2;
    if (extensions_len != body_len - pos)
        return -1;

    size_t extensions_end = pos + extensions_len;
    int saw_early_data = 0;
    while (pos < extensions_end) {
        if (extensions_end - pos < 4)
            return -1;
        uint16_t extension_type = tls_get_u16(body + pos);
        size_t extension_len = tls_get_u16(body + pos + 2);
        pos += 4;
        if (extension_len > extensions_end - pos)
            return -1;
        if (extension_type == TLS_EXT_EARLY_DATA) {
            if (saw_early_data || extension_len != 4)
                return -1;
            saw_early_data = 1;
        }
        pos += extension_len;
    }
    if (pos != body_len)
        return -1;

    /* RFC 8446 limits ticket lifetimes to seven days. Oversized tickets or
     * zero-lifetime tickets are valid messages but are not cacheable here. */
    if (lifetime == 0 || lifetime > 604800u ||
        ticket_len > TLS_MAX_SESSION_TICKET)
        return 0;

    uint8_t psk[TLS_HASH_SIZE_SHA256];
    if (nci_tls_derive_resumption_psk(
            conn->resumption_master_secret,
            nonce, nonce_len, psk) != 0)
        return -1;
    int store_result = nci_tls_store_client_session(
        conn->config, ticket, ticket_len, psk,
        lifetime, age_add, conn->alpn,
        conn->peer_cert, conn->peer_cert_len);
    neverc_platform_secure_zero(psk, sizeof(psk));
    /* Session caching is opportunistic; allocation pressure must not turn a
     * valid post-handshake ticket into a connection failure. */
    (void)store_result;
    return 0;
}

int nci_tls_append_post_handshake(
    neverc_tls_conn_t *conn, const uint8_t *data, size_t data_len) {
    if (!conn || (!data && data_len != 0) ||
        conn->post_handshake_len > TLS_MAX_HANDSHAKE ||
        data_len > TLS_MAX_HANDSHAKE - conn->post_handshake_len)
        return nci_tls_protocol_error(
            conn, TLS_ALERT_DECODE_ERROR,
            "TLS post-handshake message exceeds the configured limit");

    size_t required = conn->post_handshake_len + data_len;
    if (required > conn->post_handshake_cap) {
        size_t capacity = conn->post_handshake_cap ?
                          conn->post_handshake_cap : 256;
        while (capacity < required) {
            if (capacity >= TLS_MAX_HANDSHAKE / 2) {
                capacity = TLS_MAX_HANDSHAKE;
                break;
            }
            capacity *= 2;
        }
        uint8_t *resized = (uint8_t *)realloc(
            conn->post_handshake_buf, capacity);
        if (!resized)
            return nci_tls_protocol_error(
                conn, TLS_ALERT_INTERNAL_ERROR,
                "TLS post-handshake allocation failed");
        conn->post_handshake_buf = resized;
        conn->post_handshake_cap = capacity;
    }
    if (data_len > 0) {
        memcpy(conn->post_handshake_buf + conn->post_handshake_len,
               data, data_len);
    }
    conn->post_handshake_len = required;
    return 0;
}

int nci_tls_handle_post_handshake(
    neverc_tls_conn_t *conn, const uint8_t *data, size_t data_len) {
    if (data_len == 0)
        return nci_tls_protocol_error(
            conn, TLS_ALERT_UNEXPECTED_MESSAGE,
            "received an empty TLS post-handshake record");
    if (nci_tls_append_post_handshake(conn, data, data_len) != 0)
        return -1;

    while (conn->post_handshake_len >= 4) {
        uint8_t message_type = conn->post_handshake_buf[0];
        size_t body_len = tls_get_u24(conn->post_handshake_buf + 1);
        if (body_len > TLS_MAX_HANDSHAKE - 4)
            return nci_tls_protocol_error(
                conn, TLS_ALERT_DECODE_ERROR,
                "TLS post-handshake message is too large");
        size_t message_len = 4 + body_len;
        if (conn->post_handshake_len < message_len)
            return 0;

        const uint8_t *body = conn->post_handshake_buf + 4;
        if (message_type == TLS_HS_KEY_UPDATE) {
            /* Sending keys change immediately after KeyUpdate, so it must
             * terminate the record encrypted under the old keys. */
            if (message_len != conn->post_handshake_len)
                return nci_tls_protocol_error(
                    conn, TLS_ALERT_UNEXPECTED_MESSAGE,
                    "TLS KeyUpdate was interleaved with another message");
            if (body_len != 1)
                return nci_tls_protocol_error(
                    conn, TLS_ALERT_DECODE_ERROR,
                    "malformed TLS KeyUpdate");
            if (body[0] > 1)
                return nci_tls_protocol_error(
                    conn, TLS_ALERT_ILLEGAL_PARAMETER,
                    "invalid TLS KeyUpdate request");
            if (body[0] == 1 &&
                nci_tls_send_key_update_message(conn, 0) != 0)
                return -1;
            if (nci_tls_update_traffic_secret(
                    conn->read_traffic_secret,
                    &conn->read_keys) != 0) {
                conn->closed = 1;
                return nci_tls_error(
                    conn, "failed to rotate TLS read keys");
            }
        } else if (message_type == TLS_HS_NEW_SESSION_TICKET) {
            if (conn->is_server)
                return nci_tls_protocol_error(
                    conn, TLS_ALERT_UNEXPECTED_MESSAGE,
                    "TLS client sent NewSessionTicket");
            if (nci_tls_parse_new_session_ticket(
                    conn, body, body_len) != 0)
                return nci_tls_protocol_error(
                    conn, TLS_ALERT_DECODE_ERROR,
                    "malformed TLS NewSessionTicket");
        } else {
            return nci_tls_protocol_error(
                conn, TLS_ALERT_UNEXPECTED_MESSAGE,
                "received an unsupported TLS post-handshake message");
        }

        size_t remaining =
            conn->post_handshake_len - message_len;
        if (remaining > 0) {
            memmove(conn->post_handshake_buf,
                    conn->post_handshake_buf + message_len,
                    remaining);
        }
        conn->post_handshake_len = remaining;
    }
    return 0;
}

int nci_tls_handle_peer_alert(
    neverc_tls_conn_t *conn, const uint8_t *data, size_t data_len) {
    if (data_len != 2 || (data[0] != 1 && data[0] != 2))
        return nci_tls_protocol_error(
            conn, TLS_ALERT_DECODE_ERROR,
            "malformed TLS alert");
    if (data[1] == TLS_ALERT_CLOSE_NOTIFY) {
        conn->peer_closed = 1;
        return 1;
    }
    if (data[1] == TLS_ALERT_USER_CANCELED)
        return 0;

    conn->closed = 1;
    conn->failure_reason = "TLS peer sent a fatal alert";
    return -1;
}
