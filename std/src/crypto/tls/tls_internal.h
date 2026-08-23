#ifndef NEVERC_STD_CRYPTO_TLS_INTERNAL_H
#define NEVERC_STD_CRYPTO_TLS_INTERNAL_H

/*
 * Shared TLS 1.3 internals used across the split crypto/tls translation
 * units. Mirrors the role of Go's crypto/tls/common.go for constants,
 * connection state, and cross-module helpers.
 */

#include "neverc/std/crypto/gcm.h"
#include "neverc/std/crypto/tls.h"
#include "neverc/std/crypto/x509.h"
#include "neverc/std/net/tcp.h"

#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
typedef CRITICAL_SECTION tls_mutex_t;
#define tls_mutex_init(m)    InitializeCriticalSection(m)
#define tls_mutex_destroy(m) DeleteCriticalSection(m)
#define tls_mutex_lock(m)    EnterCriticalSection(m)
#define tls_mutex_unlock(m)  LeaveCriticalSection(m)
#else
#include <pthread.h>
typedef pthread_mutex_t tls_mutex_t;
#define tls_mutex_init(m)    pthread_mutex_init((m), NULL)
#define tls_mutex_destroy(m) pthread_mutex_destroy(m)
#define tls_mutex_lock(m)    pthread_mutex_lock(m)
#define tls_mutex_unlock(m)  pthread_mutex_unlock(m)
#endif

#define TLS_RECORD_HEADER_SIZE   5
#define TLS_MAX_PLAINTEXT        16384
#define TLS_MAX_CIPHERTEXT       (TLS_MAX_PLAINTEXT + 256)
#define TLS_MAX_HANDSHAKE        65536
/* Large enough for max ALPN + core extensions, small enough that a max-sized
 * session ticket forces a PSK drop rather than a stack overflow. */
#define TLS_CLIENT_HELLO_CAPACITY 4096
#define TLS_MAX_NON_ADVANCING_RECORDS 16
#define TLS_HASH_SIZE_SHA256     32
#define TLS_AEAD_TAG_SIZE        16
#define TLS_MAX_PENDING_WRITE    (TLS_MAX_HANDSHAKE * 2U + 65536U)
#define NCI_TLS_WANT_READ         1
#define NCI_TLS_WANT_WRITE        2
#define NCI_TLS_IO_WANT_READ     (-2)
#define NCI_TLS_IO_WANT_WRITE    (-3)

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

/* RFC 6066 HostName is a DNS name. IP literals are kept for certificate
 * identity but must not be sent as SNI. */
static inline int nci_tls_name_is_ip_literal(const char *name) {
    if (!name || !name[0])
        return 0;
    if (strchr(name, ':'))
        return 1;
    int dots = 0;
    int group = 0;
    for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
        if (*p == '.') {
            if (group == 0 || dots >= 3)
                return 0;
            dots++;
            group = 0;
        } else if (*p >= '0' && *p <= '9') {
            group++;
            if (group > 3)
                return 0;
        } else {
            return 0;
        }
    }
    return dots == 3 && group > 0;
}

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

typedef struct {
    uint8_t *ticket;
    size_t ticket_len;
    uint8_t psk[TLS_HASH_SIZE_SHA256];
    uint32_t lifetime;
    uint32_t age_add;
    uint64_t received_at_ms;
    char *server_name;
    char *alpn;
    uint8_t *peer_cert;
    size_t peer_cert_len;
    int valid;
} tls_client_session_t;

typedef struct {
    uint8_t ticket[TLS_SERVER_TICKET_SIZE];
    uint8_t psk[TLS_HASH_SIZE_SHA256];
    uint32_t lifetime;
    uint32_t age_add;
    uint64_t issued_at_ms;
    char server_name[TLS_MAX_SERVER_NAME + 1];
    size_t server_name_len;
    char alpn[256];
    size_t alpn_len;
    int valid;
} tls_server_session_t;

typedef struct {
    uint8_t ticket[TLS_MAX_SESSION_TICKET];
    size_t ticket_len;
    uint8_t psk[TLS_HASH_SIZE_SHA256];
    uint32_t obfuscated_age;
    char *alpn;
    uint8_t *peer_cert;
    size_t peer_cert_len;
    int valid;
} tls_client_psk_offer_t;

struct neverc_tls_config {
    _Atomic unsigned int ref_count;
    tls_mutex_t session_mutex;
    int session_mutex_initialized;
    uint8_t *cert_der;
    size_t   cert_der_len;
    uint8_t *key_der;
    size_t   key_der_len;
    int      key_type; /* 0=unknown, 1=RSA, 2=ECDSA, 3=Ed25519 */
    char    *server_name;
    char   **alpn_protos;
    int      alpn_count;
    int      skip_verify;
    int      client_auth;
    neverc_x509_cert_pool_t *root_certificates;
    tls_client_session_t client_session;
    tls_server_session_t
        server_sessions[TLS_SERVER_TICKET_COUNT];
    size_t server_session_next;
#if defined(NEVERC_TLS_TESTING)
    size_t   test_handshake_fragment_size;
#endif
};

struct neverc_tls_conn {
    neverc_tcp_conn_t  *tcp;
    neverc_tls_config_t *config;
    int                 owns_tcp;
    int                 is_server;
    tls_mutex_t         read_mutex;
    tls_mutex_t         write_mutex;
    int                 mutexes_initialized;
    neverc_context_t    *read_context;
    neverc_context_t    *write_context;
    tls_traffic_keys_t  read_keys;
    tls_traffic_keys_t  write_keys;
    int                 handshake_done;
    uint16_t            cipher_suite;
    char               *alpn;
    char               *server_name;
    char               *resumption_alpn;
    uint8_t            *peer_cert;
    size_t              peer_cert_len;
    neverc_x509_cert_pool_t *peer_intermediates;
    uint8_t             read_buf[TLS_MAX_CIPHERTEXT + TLS_RECORD_HEADER_SIZE];
    size_t              read_buf_len;
    uint8_t            *pending_write_buf;
    size_t              pending_write_len;
    size_t              pending_write_pos;
    size_t              pending_write_cap;
    int                 nonblocking_io;
    void               *async_handshake;
    uint8_t             decrypt_buf[TLS_MAX_PLAINTEXT + 1];
    size_t              decrypt_buf_len;
    size_t              decrypt_buf_pos;
    uint8_t            *preload_app_buf;
    size_t              preload_app_len;
    size_t              preload_app_pos;
    uint8_t            *post_handshake_buf;
    size_t              post_handshake_len;
    size_t              post_handshake_cap;
    uint8_t            *handshake_buf;
    size_t              handshake_len;
    size_t              handshake_cap;
    unsigned int        non_advancing_records;
    uint8_t             read_traffic_secret[TLS_HASH_SIZE_SHA256];
    uint8_t             write_traffic_secret[TLS_HASH_SIZE_SHA256];
    uint8_t             resumption_master_secret[TLS_HASH_SIZE_SHA256];
    int                 resumption_secret_active;
    int                 did_resume;
    int                 application_keys_active;
    int                 write_keys_active;
    /* Set when the first plaintext handshake record is received. RFC 8446
     * D.4 forbids ChangeCipherSpec before that ClientHello on the server. */
    int                 received_handshake_record;
    int                 alert_sent;
    int                 peer_closed;
    int                 write_closed;
    _Atomic(const char *) failure_reason;
    _Atomic int         closed;
#if defined(NEVERC_TLS_TESTING)
    size_t              test_handshake_fragment_size;
#endif
};

struct neverc_tls_listener {
    neverc_tcp_listener_t *tcp_ln;
    neverc_tls_config_t   *cfg;
};

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

/* --- config/session (tls_config.c) --- */
void nci_tls_config_retain(neverc_tls_config_t *cfg);
void nci_tls_config_invalidate_client_session(neverc_tls_config_t *cfg);
void nci_tls_config_invalidate_all_sessions(neverc_tls_config_t *cfg);
int nci_tls_load_client_psk_offer(
    neverc_tls_config_t *cfg, tls_client_psk_offer_t *offer);
void nci_tls_clear_client_psk_offer(tls_client_psk_offer_t *offer);
int nci_tls_store_client_session(
    neverc_tls_conn_t *conn,
    const uint8_t *ticket, size_t ticket_len,
    const uint8_t psk[TLS_HASH_SIZE_SHA256],
    uint32_t lifetime, uint32_t age_add);
void nci_tls_store_server_session(
    neverc_tls_config_t *cfg,
    const uint8_t ticket[TLS_SERVER_TICKET_SIZE],
    const uint8_t psk[TLS_HASH_SIZE_SHA256],
    uint32_t lifetime, uint32_t age_add,
    uint64_t issued_at_ms,
    const char *server_name, size_t server_name_len,
    const char *alpn, size_t alpn_len);
int nci_tls_lookup_server_session(
    neverc_tls_config_t *cfg,
    const uint8_t *ticket, size_t ticket_len,
    uint32_t obfuscated_age,
    const uint8_t *server_name, size_t server_name_len,
    const char *alpn, size_t alpn_len,
    uint8_t psk[TLS_HASH_SIZE_SHA256]);
uint64_t nci_tls_wall_time_ms(void);
int nci_tls_verify_certificate_chain(
    const neverc_tls_config_t *config,
    const uint8_t *leaf_der, size_t leaf_der_len,
    const neverc_x509_cert_pool_t *intermediates,
    const neverc_x509_time_t *moment,
    const char *hostname,
    uint32_t required_ext_key_usage,
    int allow_system_roots);

/* --- record layer (tls_record.c) --- */
int nci_tls_raw_write(neverc_tcp_conn_t *tcp, const void *data, size_t len);
int nci_tls_send_record(
    neverc_tcp_conn_t *tcp, uint8_t content_type,
    const uint8_t *data, size_t len);
int nci_tls_send_plain_record(
    neverc_tls_conn_t *conn, uint8_t content_type,
    const uint8_t *data, size_t len);
int nci_tls_flush_pending_write(neverc_tls_conn_t *conn);
int nci_tls_set_application_keys(
    neverc_tls_conn_t *conn, tls_cipher_id_t cipher,
    const uint8_t read_secret[TLS_HASH_SIZE_SHA256],
    const uint8_t write_secret[TLS_HASH_SIZE_SHA256]);
int nci_tls_send_encrypted_unlocked(
    neverc_tls_conn_t *conn, uint8_t inner_type,
    const uint8_t *data, size_t len);
int nci_tls_send_encrypted(
    neverc_tls_conn_t *conn, uint8_t inner_type,
    const uint8_t *data, size_t len);
int nci_tls_append_handshake_bytes(
    neverc_tls_conn_t *conn, const uint8_t *data, size_t data_len);
int nci_tls_next_handshake_message(
    neverc_tls_conn_t *conn, const uint8_t **message,
    size_t *message_len);
int nci_tls_consume_handshake_message(
    neverc_tls_conn_t *conn, size_t message_len);
void nci_tls_clear_handshake_buffer(neverc_tls_conn_t *conn);
int nci_tls_send_plain_handshake(
    neverc_tls_conn_t *conn, const uint8_t *data, size_t data_len);
int nci_tls_send_encrypted_handshake(
    neverc_tls_conn_t *conn, const uint8_t *data, size_t data_len);
int nci_tls_send_alert_level(
    neverc_tls_conn_t *conn, uint8_t level, uint8_t description);
int nci_tls_send_alert(neverc_tls_conn_t *conn, uint8_t description);
int nci_tls_send_close_notify(neverc_tls_conn_t *conn);
int nci_tls_fail(neverc_tls_conn_t *conn, uint8_t description);
int nci_tls_error(neverc_tls_conn_t *conn, const char *reason);
int nci_tls_protocol_error(
    neverc_tls_conn_t *conn, uint8_t description,
    const char *reason);
int nci_tls_recv_record(
    neverc_tls_conn_t *conn, uint8_t *out_type,
    uint8_t *out_data, size_t *out_len);
int nci_tls_recv_decrypt(
    neverc_tls_conn_t *conn, uint8_t *out_inner_type,
    uint8_t *out_data, size_t *out_len);
int nci_tls_recv_plain_handshake_message(
    neverc_tls_conn_t *conn, uint8_t expected_type,
    const uint8_t **message, size_t *message_len);

#if defined(NEVERC_TLS_TESTING)
int neverc_tls_test_handshake_reassembly(void);
#endif

/* --- handshake (tls_handshake.c) --- */
neverc_tls_conn_t *nci_tls_conn_new(neverc_tcp_conn_t *tcp, int owns);
int nci_tls_client_handshake(
    neverc_tls_conn_t *conn, neverc_tls_config_t *cfg);
int nci_tls_server_handshake(
    neverc_tls_conn_t *conn, neverc_tls_config_t *cfg);
int nci_tls_server_handshake_begin(
    neverc_tls_conn_t *conn, neverc_tls_config_t *cfg);
int nci_tls_server_handshake_step(neverc_tls_conn_t *conn);
void nci_tls_async_handshake_free(neverc_tls_conn_t *conn);
int nci_tls_send_key_update_message(
    neverc_tls_conn_t *conn, int request_peer_update);
int nci_tls_handle_post_handshake(
    neverc_tls_conn_t *conn,
    const uint8_t *data, size_t data_len);
int nci_tls_handle_peer_alert(
    neverc_tls_conn_t *conn,
    const uint8_t *data, size_t data_len);
int nci_tls_fail_handshake_alert(
    neverc_tls_conn_t *conn,
    const uint8_t *data, size_t data_len);

#if defined(NEVERC_TLS_ENABLE_EXPERIMENTAL_TRANSPORT)
neverc_tls_conn_t *nci_tls_start_handshake(
    neverc_tcp_conn_t *tcp, neverc_tls_config_t *cfg,
    int from_server, int owns_tcp, neverc_context_t *ctx,
    const char **errp);
#endif

#endif /* NEVERC_STD_CRYPTO_TLS_INTERNAL_H */
