#include "neverc/std/crypto/tls.h"
#include "neverc/std/_platform.h"
#include "neverc/std/crypto/ecdh.h"
#include "neverc/std/crypto/aes.h"
#include "neverc/std/crypto/gcm.h"
#include "neverc/std/crypto/chacha20poly1305.h"
#include "neverc/std/crypto/sha256.h"
#include "neverc/std/crypto/sha384.h"
#include "neverc/std/crypto/hmac.h"
#include "neverc/std/crypto/hkdf.h"
#include "neverc/std/crypto/rand.h"
#include "neverc/std/crypto/subtle.h"
#include "neverc/std/crypto/x509.h"
#include "neverc/std/encoding/pem.h"
#include "tls_internal.h"
#include "tls_key.h"
#include "tls_key_schedule.h"
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdatomic.h>
#include <time.h>

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

/* ======================================================================
 * Byte helpers
 * ====================================================================== */

static inline void put_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v);
}

static inline void put_u24(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 16);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v);
}

static inline uint16_t get_u16(const uint8_t *p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

static inline uint32_t get_u24(const uint8_t *p) {
    return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
}

static inline void put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static inline uint32_t get_u32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}

#if defined(NEVERC_TLS_DEBUG_KEYS)
static void tls_debug_hex(
    const char *label, const uint8_t *data, size_t length) {
    fprintf(stderr, "%s ", label);
    for (size_t i = 0; i < length; ++i)
        fprintf(stderr, "%02x", data[i]);
    fputc('\n', stderr);
}
#endif

/* ======================================================================
 * Internal structures
 * ====================================================================== */

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
    int                 owns_tcp; /* if we created the TCP conn */
    int                 is_server;
    tls_mutex_t         read_mutex;
    tls_mutex_t         write_mutex;
    int                 mutexes_initialized;
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
    uint8_t             decrypt_buf[TLS_MAX_PLAINTEXT + 1];
    size_t              decrypt_buf_len;
    size_t              decrypt_buf_pos;
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

static void tls_free_private_key(uint8_t *key_der, size_t key_der_len) {
    if (!key_der)
        return;
    neverc_platform_secure_zero(key_der, key_der_len);
    free(key_der);
}

/* ======================================================================
 * TLS Config
 * ====================================================================== */

static void tls_clear_client_session(
    tls_client_session_t *session) {
    if (!session)
        return;
    if (session->ticket) {
        neverc_platform_secure_zero(
            session->ticket, session->ticket_len);
        free(session->ticket);
    }
    free(session->server_name);
    free(session->alpn);
    free(session->peer_cert);
    neverc_platform_secure_zero(session, sizeof(*session));
}

static void tls_config_retain(neverc_tls_config_t *cfg) {
    if (cfg) {
        (void)atomic_fetch_add_explicit(
            &cfg->ref_count, 1, memory_order_relaxed);
    }
}

neverc_tls_config_t *neverc_tls_config_new(void) {
    neverc_tls_config_t *cfg = (neverc_tls_config_t *)calloc(1, sizeof(*cfg));
    if (!cfg)
        return NULL;
    atomic_init(&cfg->ref_count, 1);
#ifdef _WIN32
    tls_mutex_init(&cfg->session_mutex);
#else
    if (tls_mutex_init(&cfg->session_mutex) != 0) {
        free(cfg);
        return NULL;
    }
#endif
    cfg->session_mutex_initialized = 1;
    return cfg;
}

void neverc_tls_config_free(neverc_tls_config_t *cfg) {
    if (!cfg) return;
    if (atomic_fetch_sub_explicit(
            &cfg->ref_count, 1, memory_order_acq_rel) != 1)
        return;
    tls_clear_client_session(&cfg->client_session);
    neverc_platform_secure_zero(
        cfg->server_sessions, sizeof(cfg->server_sessions));
    if (cfg->session_mutex_initialized) {
        tls_mutex_destroy(&cfg->session_mutex);
        cfg->session_mutex_initialized = 0;
    }
    free(cfg->cert_der);
    tls_free_private_key(cfg->key_der, cfg->key_der_len);
    free(cfg->server_name);
    neverc_x509_cert_pool_free(cfg->root_certificates);
    for (int i = 0; i < cfg->alpn_count; i++)
        free(cfg->alpn_protos[i]);
    free(cfg->alpn_protos);
    free(cfg);
}

static uint64_t tls_wall_time_ms(void) {
    time_t now = time(NULL);
    if (now < 0 ||
        (uint64_t)now > UINT64_MAX / 1000u)
        return 0;
    return (uint64_t)now * 1000u;
}

static void tls_config_invalidate_client_session(
    neverc_tls_config_t *cfg) {
    if (!cfg || !cfg->session_mutex_initialized)
        return;
    tls_mutex_lock(&cfg->session_mutex);
    tls_clear_client_session(&cfg->client_session);
    tls_mutex_unlock(&cfg->session_mutex);
}

static void tls_config_invalidate_all_sessions(
    neverc_tls_config_t *cfg) {
    if (!cfg || !cfg->session_mutex_initialized)
        return;
    tls_mutex_lock(&cfg->session_mutex);
    tls_clear_client_session(&cfg->client_session);
    neverc_platform_secure_zero(
        cfg->server_sessions, sizeof(cfg->server_sessions));
    cfg->server_session_next = 0;
    tls_mutex_unlock(&cfg->session_mutex);
}

static int tls_load_client_psk_offer(
    neverc_tls_config_t *cfg, tls_client_psk_offer_t *offer) {
    if (!offer)
        return 0;
    memset(offer, 0, sizeof(*offer));
    if (!cfg || !cfg->server_name ||
        !cfg->session_mutex_initialized)
        return 0;
    uint64_t now_ms = tls_wall_time_ms();
    if (now_ms == 0)
        return 0;

    tls_mutex_lock(&cfg->session_mutex);
    tls_client_session_t *session = &cfg->client_session;
    int usable = session->valid &&
        session->ticket && session->ticket_len > 0 &&
        session->ticket_len <= sizeof(offer->ticket) &&
        session->server_name &&
        strcmp(session->server_name, cfg->server_name) == 0 &&
        session->lifetime > 0 &&
        now_ms >= session->received_at_ms &&
        now_ms - session->received_at_ms <
            (uint64_t)session->lifetime * 1000u;
    if (usable) {
        if (session->peer_cert_len > 0)
            offer->peer_cert =
                (uint8_t *)malloc(session->peer_cert_len);
        if (session->alpn)
            offer->alpn = strdup(session->alpn);
        if ((session->peer_cert_len == 0 ||
             offer->peer_cert) &&
            (!session->alpn || offer->alpn)) {
            memcpy(offer->ticket, session->ticket,
                   session->ticket_len);
            offer->ticket_len = session->ticket_len;
            memcpy(offer->psk, session->psk,
                   sizeof(offer->psk));
            offer->obfuscated_age =
                (uint32_t)(now_ms - session->received_at_ms) +
                session->age_add;
            if (session->peer_cert_len > 0)
                memcpy(offer->peer_cert,
                       session->peer_cert,
                       session->peer_cert_len);
            offer->peer_cert_len =
                session->peer_cert_len;
            offer->valid = 1;
        } else {
            free(offer->alpn);
            offer->alpn = NULL;
            free(offer->peer_cert);
            offer->peer_cert = NULL;
        }
    } else if (session->valid) {
        tls_clear_client_session(session);
    }
    tls_mutex_unlock(&cfg->session_mutex);
    return offer->valid;
}

static void tls_clear_client_psk_offer(
    tls_client_psk_offer_t *offer) {
    if (!offer)
        return;
    free(offer->alpn);
    free(offer->peer_cert);
    neverc_platform_secure_zero(offer, sizeof(*offer));
}

static int tls_store_client_session(
    neverc_tls_config_t *cfg,
    const uint8_t *ticket, size_t ticket_len,
    const uint8_t psk[TLS_HASH_SIZE_SHA256],
    uint32_t lifetime, uint32_t age_add,
    const char *alpn,
    const uint8_t *peer_cert, size_t peer_cert_len) {
    if (!cfg || !cfg->session_mutex_initialized ||
        !cfg->server_name || !ticket || ticket_len == 0 ||
        ticket_len > TLS_MAX_SESSION_TICKET || !psk ||
        lifetime == 0 || lifetime > 604800u ||
        (alpn && (alpn[0] == '\0' ||
                  strlen(alpn) > 255)) ||
        (!peer_cert && peer_cert_len != 0))
        return -1;

    tls_client_session_t replacement;
    memset(&replacement, 0, sizeof(replacement));
    replacement.ticket = (uint8_t *)malloc(ticket_len);
    replacement.server_name = strdup(cfg->server_name);
    if (alpn)
        replacement.alpn = strdup(alpn);
    if (peer_cert_len > 0)
        replacement.peer_cert =
            (uint8_t *)malloc(peer_cert_len);
    if (!replacement.ticket || !replacement.server_name ||
        (alpn && !replacement.alpn) ||
        (peer_cert_len > 0 && !replacement.peer_cert)) {
        tls_clear_client_session(&replacement);
        return -1;
    }
    memcpy(replacement.ticket, ticket, ticket_len);
    replacement.ticket_len = ticket_len;
    memcpy(replacement.psk, psk, sizeof(replacement.psk));
    replacement.lifetime = lifetime;
    replacement.age_add = age_add;
    replacement.received_at_ms = tls_wall_time_ms();
    if (peer_cert_len > 0)
        memcpy(replacement.peer_cert, peer_cert,
               peer_cert_len);
    replacement.peer_cert_len = peer_cert_len;
    replacement.valid = replacement.received_at_ms != 0;
    if (!replacement.valid) {
        tls_clear_client_session(&replacement);
        return -1;
    }

    tls_mutex_lock(&cfg->session_mutex);
    tls_clear_client_session(&cfg->client_session);
    cfg->client_session = replacement;
    tls_mutex_unlock(&cfg->session_mutex);
    return 0;
}

static void tls_store_server_session(
    neverc_tls_config_t *cfg,
    const uint8_t ticket[TLS_SERVER_TICKET_SIZE],
    const uint8_t psk[TLS_HASH_SIZE_SHA256],
    uint32_t lifetime, uint32_t age_add,
    uint64_t issued_at_ms,
    const char *server_name, size_t server_name_len,
    const char *alpn, size_t alpn_len) {
    if (!cfg || !cfg->session_mutex_initialized ||
        server_name_len > TLS_MAX_SERVER_NAME ||
        alpn_len > 255 ||
        (!server_name && server_name_len != 0) ||
        (!alpn && alpn_len != 0))
        return;
    tls_mutex_lock(&cfg->session_mutex);
    size_t index =
        cfg->server_session_next++ % TLS_SERVER_TICKET_COUNT;
    tls_server_session_t *session =
        &cfg->server_sessions[index];
    neverc_platform_secure_zero(session, sizeof(*session));
    memcpy(session->ticket, ticket, TLS_SERVER_TICKET_SIZE);
    memcpy(session->psk, psk, TLS_HASH_SIZE_SHA256);
    session->lifetime = lifetime;
    session->age_add = age_add;
    session->issued_at_ms = issued_at_ms;
    if (server_name_len > 0)
        memcpy(session->server_name, server_name,
               server_name_len);
    session->server_name_len = server_name_len;
    if (alpn_len > 0)
        memcpy(session->alpn, alpn, alpn_len);
    session->alpn_len = alpn_len;
    session->valid = 1;
    tls_mutex_unlock(&cfg->session_mutex);
}

static int tls_lookup_server_session(
    neverc_tls_config_t *cfg,
    const uint8_t *ticket, size_t ticket_len,
    uint32_t obfuscated_age,
    const uint8_t *server_name, size_t server_name_len,
    const char *alpn, size_t alpn_len,
    uint8_t psk[TLS_HASH_SIZE_SHA256]) {
    if (!cfg || !cfg->session_mutex_initialized ||
        !ticket || ticket_len != TLS_SERVER_TICKET_SIZE ||
        !psk || server_name_len > TLS_MAX_SERVER_NAME ||
        alpn_len > 255 ||
        (!server_name && server_name_len != 0) ||
        (!alpn && alpn_len != 0))
        return 0;
    uint64_t now_ms = tls_wall_time_ms();
    if (now_ms == 0)
        return 0;

    int found = 0;
    tls_mutex_lock(&cfg->session_mutex);
    for (size_t i = 0; i < TLS_SERVER_TICKET_COUNT; ++i) {
        tls_server_session_t *session =
            &cfg->server_sessions[i];
        if (!session->valid)
            continue;
        if (now_ms < session->issued_at_ms ||
            now_ms - session->issued_at_ms >=
                (uint64_t)session->lifetime * 1000u) {
            neverc_platform_secure_zero(
                session, sizeof(*session));
            continue;
        }
        if (!neverc_subtle_constant_time_compare(
                session->ticket, ticket,
                TLS_SERVER_TICKET_SIZE))
            continue;
        if (session->server_name_len != server_name_len ||
            (server_name_len > 0 &&
             memcmp(session->server_name, server_name,
                    server_name_len) != 0) ||
            session->alpn_len != alpn_len ||
            (alpn_len > 0 &&
             memcmp(session->alpn, alpn, alpn_len) != 0))
            continue;

        uint32_t client_age =
            obfuscated_age - session->age_add;
        uint32_t actual_age =
            (uint32_t)(now_ms - session->issued_at_ms);
        int64_t age_delta =
            (int64_t)(int32_t)(client_age - actual_age);
        if (age_delta < -TLS_TICKET_AGE_SKEW_MS ||
            age_delta > TLS_TICKET_AGE_SKEW_MS)
            break;
        memcpy(psk, session->psk, TLS_HASH_SIZE_SHA256);
        found = 1;
        break;
    }
    tls_mutex_unlock(&cfg->session_mutex);
    return found;
}

static uint8_t *read_file_contents(const char *path, size_t *out_len) {
    if (!path || !out_len) return NULL;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long fsize = ftell(f);
    if (fsize <= 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    size_t size = (size_t)fsize;
    if (size == SIZE_MAX) {
        fclose(f);
        return NULL;
    }
    uint8_t *data = (uint8_t *)malloc(size + 1);
    if (!data) { fclose(f); return NULL; }
    size_t read_len = fread(data, 1, size, f);
    if (read_len != size || ferror(f)) {
        free(data);
        fclose(f);
        return NULL;
    }
    fclose(f);
    data[size] = '\0';
    *out_len = size;
    return data;
}

static int pem_decode_first(const char *pem, const char *label,
                            uint8_t **out_der, size_t *out_len) {
    if (!pem || !label || !out_der || !out_len)
        return -1;

    size_t pem_len = strlen(pem);
    if (pem_len == 0)
        return -1;
    uint8_t *der = (uint8_t *)malloc(pem_len);
    if (!der)
        return -1;

    size_t offset = 0;
    while (offset < pem_len) {
        char decoded_label[64];
        size_t decoded_len = 0;
        size_t rest_offset = 0;
        if (neverc_pem_decode(
                pem + offset, pem_len - offset,
                decoded_label, sizeof(decoded_label),
                der, pem_len, &decoded_len, &rest_offset) != 0 ||
            rest_offset == 0) {
            free(der);
            return -1;
        }
        if (strcmp(decoded_label, label) == 0) {
            if (decoded_len == 0) {
                free(der);
                return -1;
            }
            *out_der = der;
            *out_len = decoded_len;
            return 0;
        }
        offset += rest_offset;
    }
    free(der);
    return -1;
}

int neverc_tls_config_load_cert(neverc_tls_config_t *cfg,
                                 const char *cert_path,
                                 const char *key_path) {
    if (!cfg || !cert_path || !key_path) return -1;

    size_t cert_len = 0, key_len = 0;
    uint8_t *cert_pem = read_file_contents(cert_path, &cert_len);
    uint8_t *key_pem = read_file_contents(key_path, &key_len);
    if (!cert_pem || !key_pem) {
        free(cert_pem);
        free(key_pem);
        return -1;
    }

    int rc = neverc_tls_config_load_cert_mem(cfg, (const char *)cert_pem,
                                              (const char *)key_pem);
    free(cert_pem);
    free(key_pem);
    return rc;
}

int neverc_tls_config_load_cert_mem(neverc_tls_config_t *cfg,
                                     const char *cert_pem,
                                     const char *key_pem) {
    if (!cfg || !cert_pem || !key_pem) return -1;

    uint8_t *cert_der = NULL;
    size_t cert_der_len = 0;
    uint8_t *key_der = NULL;
    size_t key_der_len = 0;
    int key_type = NCI_TLS_KEY_ECDSA_P256;

    int cert_result =
        pem_decode_first(cert_pem, "CERTIFICATE",
                         &cert_der, &cert_der_len);
    int key_result =
        pem_decode_first(key_pem, "EC PRIVATE KEY",
                         &key_der, &key_der_len);
    if (key_result != 0)
        key_result =
            pem_decode_first(key_pem, "PRIVATE KEY",
                             &key_der, &key_der_len);

    if (cert_result != 0 || key_result != 0 ||
        nci_tls_validate_certificate_key_pair(
            cert_der, cert_der_len, key_der, key_der_len,
            key_type) != 0) {
        free(cert_der);
        tls_free_private_key(key_der, key_der_len);
        return -1;
    }

    tls_config_invalidate_all_sessions(cfg);
    free(cfg->cert_der);
    tls_free_private_key(cfg->key_der, cfg->key_der_len);
    cfg->cert_der = cert_der;
    cfg->cert_der_len = cert_der_len;
    cfg->key_der = key_der;
    cfg->key_der_len = key_der_len;
    cfg->key_type = key_type;
    return 0;
}

int neverc_tls_config_add_root_certificates_mem(
    neverc_tls_config_t *cfg, const char *pem, size_t pem_len) {
    if (!cfg || !pem || pem_len == 0)
        return -1;

    int created_pool = 0;
    if (!cfg->root_certificates) {
        cfg->root_certificates = neverc_x509_cert_pool_new();
        if (!cfg->root_certificates)
            return -1;
        created_pool = 1;
    }

    int added = neverc_x509_cert_pool_add_pem(
        cfg->root_certificates, pem, pem_len);
    if (added <= 0) {
        if (created_pool) {
            neverc_x509_cert_pool_free(cfg->root_certificates);
            cfg->root_certificates = NULL;
        }
        return -1;
    }
    tls_config_invalidate_client_session(cfg);
    return 0;
}

int neverc_tls_config_add_root_certificates(
    neverc_tls_config_t *cfg, const char *pem_path) {
    if (!cfg || !pem_path)
        return -1;
    size_t pem_len = 0;
    uint8_t *pem = read_file_contents(pem_path, &pem_len);
    if (!pem)
        return -1;
    int result = neverc_tls_config_add_root_certificates_mem(
        cfg, (const char *)pem, pem_len);
    free(pem);
    return result;
}

void neverc_tls_config_set_alpn(neverc_tls_config_t *cfg,
                                 const char **protocols, int count) {
    if (!cfg) return;
    if (count < 0 || count > TLS_MAX_ALPN_PROTOCOLS ||
        (count > 0 && !protocols))
        return;

    char **copies = NULL;
    size_t protocol_list_len = 0;
    if (count > 0) {
        copies = (char **)calloc((size_t)count, sizeof(char *));
        if (!copies)
            return;
        for (int i = 0; i < count; ++i) {
            size_t proto_len;
            if (!protocols[i] || protocols[i][0] == '\0')
                goto fail;
            proto_len = strlen(protocols[i]);
            if (proto_len > 255)
                goto fail;
            if (protocol_list_len >
                TLS_MAX_ALPN_LIST - (1 + proto_len))
                goto fail;
            protocol_list_len += 1 + proto_len;
            copies[i] = strdup(protocols[i]);
            if (!copies[i])
                goto fail;
        }
    }

    tls_config_invalidate_all_sessions(cfg);
    for (int i = 0; i < cfg->alpn_count; i++)
        free(cfg->alpn_protos[i]);
    free(cfg->alpn_protos);
    cfg->alpn_protos = copies;
    cfg->alpn_count = count;
    return;

fail:
    if (copies) {
        for (int i = 0; i < count; ++i)
            free(copies[i]);
        free(copies);
    }
}

void neverc_tls_config_insecure_skip_verify(neverc_tls_config_t *cfg) {
    if (!cfg || cfg->skip_verify)
        return;
    tls_config_invalidate_client_session(cfg);
    cfg->skip_verify = 1;
}

void neverc_tls_config_set_server_name(neverc_tls_config_t *cfg,
                                        const char *name) {
    if (!cfg)
        return;
    char *copy = NULL;
    if (name) {
        if (strlen(name) > TLS_MAX_SERVER_NAME)
            return;
        copy = strdup(name);
        if (!copy)
            return;
    }
    tls_config_invalidate_client_session(cfg);
    free(cfg->server_name);
    cfg->server_name = copy;
}

int neverc_tls_config_set_client_auth(
    neverc_tls_config_t *cfg, int mode) {
    if (!cfg ||
        (mode != NEVERC_TLS_CLIENT_AUTH_NONE &&
         mode != NEVERC_TLS_CLIENT_AUTH_REQUIRE_AND_VERIFY))
        return -1;
    if (cfg->client_auth == mode)
        return 0;
    tls_config_invalidate_all_sessions(cfg);
    cfg->client_auth = mode;
    return 0;
}

#if defined(NEVERC_TLS_TESTING)
int neverc_tls_test_config_set_handshake_fragment_size(
    neverc_tls_config_t *cfg, size_t fragment_size) {
    if (!cfg || fragment_size > TLS_MAX_PLAINTEXT)
        return -1;
    cfg->test_handshake_fragment_size = fragment_size;
    return 0;
}

int neverc_tls_test_did_resume(neverc_tls_conn_t *conn) {
    return conn ? conn->did_resume : 0;
}

int neverc_tls_test_corrupt_client_session(
    neverc_tls_config_t *cfg) {
    if (!cfg || !cfg->session_mutex_initialized)
        return -1;
    int result = -1;
    tls_mutex_lock(&cfg->session_mutex);
    if (cfg->client_session.valid) {
        cfg->client_session.psk[0] ^= 0x80;
        result = 0;
    }
    tls_mutex_unlock(&cfg->session_mutex);
    return result;
}

int neverc_tls_test_expire_client_session(
    neverc_tls_config_t *cfg) {
    if (!cfg || !cfg->session_mutex_initialized)
        return -1;
    int result = -1;
    tls_mutex_lock(&cfg->session_mutex);
    if (cfg->client_session.valid) {
        cfg->client_session.received_at_ms = 1;
        result = 0;
    }
    tls_mutex_unlock(&cfg->session_mutex);
    return result;
}

int neverc_tls_test_expire_server_sessions(
    neverc_tls_config_t *cfg) {
    if (!cfg || !cfg->session_mutex_initialized)
        return -1;
    int result = -1;
    tls_mutex_lock(&cfg->session_mutex);
    for (size_t i = 0; i < TLS_SERVER_TICKET_COUNT; ++i) {
        if (cfg->server_sessions[i].valid) {
            cfg->server_sessions[i].issued_at_ms = 1;
            result = 0;
        }
    }
    tls_mutex_unlock(&cfg->session_mutex);
    return result;
}

int neverc_tls_test_set_client_session_alpn(
    neverc_tls_config_t *cfg, const char *alpn) {
    if (!cfg || !cfg->session_mutex_initialized)
        return -1;
    char *copy = alpn ? strdup(alpn) : NULL;
    if (alpn && !copy)
        return -1;

    int result = -1;
    tls_mutex_lock(&cfg->session_mutex);
    if (cfg->client_session.valid) {
        free(cfg->client_session.alpn);
        cfg->client_session.alpn = copy;
        copy = NULL;
        result = 0;
    }
    tls_mutex_unlock(&cfg->session_mutex);
    free(copy);
    return result;
}

int neverc_tls_test_resize_client_session_ticket(
    neverc_tls_config_t *cfg, size_t ticket_len) {
    if (!cfg || !cfg->session_mutex_initialized ||
        ticket_len == 0 ||
        ticket_len > TLS_MAX_SESSION_TICKET)
        return -1;
    uint8_t *ticket = (uint8_t *)malloc(ticket_len);
    if (!ticket)
        return -1;
    memset(ticket, 0xa5, ticket_len);

    int result = -1;
    tls_mutex_lock(&cfg->session_mutex);
    if (cfg->client_session.valid) {
        if (cfg->client_session.ticket) {
            neverc_platform_secure_zero(
                cfg->client_session.ticket,
                cfg->client_session.ticket_len);
            free(cfg->client_session.ticket);
        }
        cfg->client_session.ticket = ticket;
        cfg->client_session.ticket_len = ticket_len;
        ticket = NULL;
        result = 0;
    }
    tls_mutex_unlock(&cfg->session_mutex);
    if (ticket) {
        neverc_platform_secure_zero(ticket, ticket_len);
        free(ticket);
    }
    return result;
}
#endif

static int tls_current_x509_time(neverc_x509_time_t *result) {
    if (!result)
        return -1;
    time_t now = time(NULL);
    if (now == (time_t)-1)
        return -1;

    struct tm utc;
#if defined(_WIN32)
    if (gmtime_s(&utc, &now) != 0)
        return -1;
#else
    if (!gmtime_r(&now, &utc))
        return -1;
#endif
    int year = utc.tm_year + 1900;
    if (year < 0 || year > UINT16_MAX ||
        utc.tm_mon < 0 || utc.tm_mon > 11 ||
        utc.tm_mday < 1 || utc.tm_mday > 31 ||
        utc.tm_hour < 0 || utc.tm_hour > 23 ||
        utc.tm_min < 0 || utc.tm_min > 59 ||
        utc.tm_sec < 0)
        return -1;

    result->year = (uint16_t)year;
    result->month = (uint8_t)(utc.tm_mon + 1);
    result->day = (uint8_t)utc.tm_mday;
    result->hour = (uint8_t)utc.tm_hour;
    result->minute = (uint8_t)utc.tm_min;
    result->second =
        (uint8_t)(utc.tm_sec > 59 ? 59 : utc.tm_sec);
    return 0;
}

static int tls_verify_certificate_chain(
    const neverc_tls_config_t *config,
    const uint8_t *leaf_der,
    size_t leaf_der_len,
    const neverc_x509_cert_pool_t *intermediates,
    const neverc_x509_time_t *moment,
    const char *hostname,
    uint32_t required_ext_key_usage,
    int allow_system_roots) {
    if (!config ||
        !leaf_der || leaf_der_len == 0)
        return -1;

    neverc_x509_cert_t leaf;
    if (neverc_x509_parse_certificate(
            &leaf, leaf_der, leaf_der_len) != 0)
        return -1;

    neverc_x509_time_t current_time;
    if (!moment) {
        if (tls_current_x509_time(&current_time) != 0) {
            neverc_x509_cert_free(&leaf);
            return -1;
        }
        moment = &current_time;
    }

    neverc_x509_cert_pool_t *owned_roots = NULL;
    const neverc_x509_cert_pool_t *roots =
        config->root_certificates;
    if (!roots && allow_system_roots) {
        owned_roots = neverc_x509_system_cert_pool();
        roots = owned_roots;
    }

    int result = roots ? neverc_x509_verify_with_pools(
        &leaf, intermediates, roots, moment,
        hostname, required_ext_key_usage) : -1;
    neverc_x509_cert_pool_free(owned_roots);
    neverc_x509_cert_free(&leaf);
    return result;
}

int neverc_tls_verify_server_certificate_chain(
    const neverc_tls_config_t *config,
    const uint8_t *leaf_der,
    size_t leaf_der_len,
    const neverc_x509_cert_pool_t *intermediates,
    const neverc_x509_time_t *moment) {
    if (!config || !config->server_name ||
        config->server_name[0] == '\0')
        return -1;
    return tls_verify_certificate_chain(
        config, leaf_der, leaf_der_len, intermediates, moment,
        config->server_name,
        NEVERC_X509_EXT_KEY_USAGE_SERVER_AUTH, 1);
}

int neverc_tls_sign_certificate_verify(
    const neverc_tls_config_t *config,
    int from_server,
    const uint8_t *transcript_hash,
    size_t transcript_hash_len,
    uint16_t *signature_scheme,
    uint8_t *signature,
    size_t signature_capacity,
    size_t *signature_len) {
    static const char server_context[] =
        "TLS 1.3, server CertificateVerify";
    static const char client_context[] =
        "TLS 1.3, client CertificateVerify";
    if (!config || config->key_type != NCI_TLS_KEY_ECDSA_P256 ||
        !config->key_der || config->key_der_len == 0 ||
        (from_server != 0 && from_server != 1) ||
        !transcript_hash || transcript_hash_len != 32 ||
        !signature_scheme || !signature || !signature_len)
        return -1;

    const char *context = from_server ?
                          server_context : client_context;
    size_t context_len = strlen(context);
    uint8_t signed_content[
        64 + sizeof(server_context) - 1 + 1 + 32];
    memset(signed_content, 0x20, 64);
    memcpy(signed_content + 64, context, context_len);
    signed_content[64 + context_len] = 0;
    memcpy(signed_content + 64 + context_len + 1,
           transcript_hash, transcript_hash_len);

    uint8_t digest[NEVERC_SHA256_DIGEST_SIZE];
    neverc_sha256_sum(
        signed_content, 64 + context_len + 1 + transcript_hash_len,
        digest);
    int result = nci_tls_sign_ecdsa_p256_sha256(
        config->key_der, config->key_der_len,
        digest, sizeof(digest), signature, signature_capacity,
        signature_len);
    neverc_platform_secure_zero(digest, sizeof(digest));
    if (result == 0) {
        *signature_scheme =
            NEVERC_TLS_SIGNATURE_ECDSA_SECP256R1_SHA256;
    }
    return result;
}

/* ======================================================================
 * TLS 1.3 Key Schedule (RFC 8446 §7.1)
 *
 * HKDF-Expand-Label(Secret, Label, Context, Length) =
 *   HKDF-Expand(Secret, HkdfLabel, Length)
 * where HkdfLabel = length(2) + "tls13 " + label + context_hash
 * ====================================================================== */

static int hkdf_expand_label(const uint8_t *secret, size_t secret_len,
                              const char *label, size_t label_len,
                              const uint8_t *context, size_t context_len,
                              uint8_t *out, size_t out_len) {
    return nci_tls_hkdf_expand_label(
        secret, secret_len, label, label_len,
        context, context_len, out, out_len);
}

static void derive_secret(const uint8_t *secret,
                            const char *label, size_t label_len,
                            const uint8_t *transcript_hash,
                            uint8_t *out) {
    nci_tls_derive_secret(
        secret, label, label_len, transcript_hash, out);
}

static int tls_derive_resumption_psk(
    const uint8_t resumption_master_secret[32],
    const uint8_t *ticket_nonce, size_t ticket_nonce_len,
    uint8_t psk[32]) {
    return nci_tls_derive_resumption_psk(
        resumption_master_secret, ticket_nonce,
        ticket_nonce_len, psk);
}

static int tls_compute_resumption_binder(
    const uint8_t psk[32],
    const uint8_t *truncated_client_hello,
    size_t truncated_client_hello_len,
    uint8_t binder[32]) {
    return nci_tls_compute_resumption_binder(
        psk, truncated_client_hello,
        truncated_client_hello_len, binder);
}

/* RFC 8446 §7.1: unavailable secrets are Hash.length zero bytes, not an
 * empty IKM. Early Secret = HKDF-Extract(0s, 0s) and Master Secret =
 * HKDF-Extract(Derive-Secret(..., "derived", ""), 0s). */
static int tls_hkdf_extract_zero_ikm(
    uint8_t out[TLS_HASH_SIZE_SHA256],
    const uint8_t *salt, size_t salt_len) {
    return nci_tls_hkdf_extract_zero_ikm(
        out, salt, salt_len);
}

static int tls_derive_handshake_secret(
    const uint8_t shared_secret[32],
    const uint8_t *psk,
    uint8_t handshake_secret[32]) {
    return nci_tls_derive_handshake_secret(
        shared_secret, psk, handshake_secret);
}

static void derive_traffic_keys(const uint8_t *traffic_secret,
                                  tls_traffic_keys_t *keys,
                                  tls_cipher_id_t cipher) {
    nci_tls_derive_traffic_keys(
        traffic_secret, keys, cipher);
}

static int tls_update_traffic_secret(
    uint8_t traffic_secret[TLS_HASH_SIZE_SHA256],
    tls_traffic_keys_t *keys) {
    return nci_tls_update_traffic_secret(
        traffic_secret, keys);
}

static void tls_set_application_keys(
    neverc_tls_conn_t *conn, tls_cipher_id_t cipher,
    const uint8_t read_secret[TLS_HASH_SIZE_SHA256],
    const uint8_t write_secret[TLS_HASH_SIZE_SHA256]) {
    memcpy(conn->read_traffic_secret, read_secret, TLS_HASH_SIZE_SHA256);
    memcpy(conn->write_traffic_secret, write_secret, TLS_HASH_SIZE_SHA256);
    derive_traffic_keys(
        conn->read_traffic_secret, &conn->read_keys, cipher);
    derive_traffic_keys(
        conn->write_traffic_secret, &conn->write_keys, cipher);
    conn->application_keys_active = 1;
    conn->non_advancing_records = 0;
}

/* ======================================================================
 * TLS Record Layer
 * ====================================================================== */

static int tls_raw_write(neverc_tcp_conn_t *tcp, const void *data, size_t len) {
    int written = neverc_tcp_write(tcp, data, len);
    return written >= 0 && (size_t)written == len ? 0 : -1;
}

static int tls_send_record(neverc_tcp_conn_t *tcp, uint8_t content_type,
                            const uint8_t *data, size_t len) {
    if (!tcp || (!data && len != 0) || len > TLS_MAX_PLAINTEXT)
        return -1;
    uint8_t hdr[5];
    hdr[0] = content_type;
    put_u16(hdr + 1, TLS_LEGACY_VERSION);
    put_u16(hdr + 3, (uint16_t)len);
    if (tls_raw_write(tcp, hdr, 5) != 0) return -1;
    if (len > 0 && tls_raw_write(tcp, data, len) != 0) return -1;
    return 0;
}

static int tls_send_encrypted_unlocked(
    neverc_tls_conn_t *conn, uint8_t inner_type,
    const uint8_t *data, size_t len) {
    if (!conn || !conn->tcp || (!data && len != 0) ||
        len > TLS_MAX_PLAINTEXT || conn->write_closed)
        return -1;
    tls_traffic_keys_t *keys = &conn->write_keys;
    if (keys->seq == UINT64_MAX)
        return -1;

    /* Build nonce: IV XOR sequence number (big-endian in last 8 bytes) */
    uint8_t nonce[12];
    memcpy(nonce, keys->iv, 12);
    for (int i = 0; i < 8; i++)
        nonce[12 - 1 - i] ^= (uint8_t)(keys->seq >> (i * 8));

    /* Build plaintext with inner content type appended */
    size_t pt_len = len + 1; /* data + inner content type */
    uint8_t *plaintext = (uint8_t *)malloc(pt_len);
    if (!plaintext) return -1;
    if (len > 0) memcpy(plaintext, data, len);
    plaintext[len] = inner_type;

    /* Record header (sent as APPLICATION_DATA for encrypted records) */
    size_t ct_len = pt_len + TLS_AEAD_TAG_SIZE;
    uint8_t hdr[5];
    hdr[0] = TLS_CT_APPLICATION_DATA;
    put_u16(hdr + 1, TLS_LEGACY_VERSION);
    put_u16(hdr + 3, (uint16_t)ct_len);

    uint8_t *ciphertext = (uint8_t *)malloc(ct_len);
    if (!ciphertext) { free(plaintext); return -1; }

    if (keys->id == TLS_CIPHER_AES_128_GCM_SHA256) {
        uint8_t tag[16];
        neverc_gcm_seal(&keys->gcm, nonce, plaintext, pt_len,
                         hdr, 5, ciphertext, tag);
        memcpy(ciphertext + pt_len, tag, 16);
    } else {
        neverc_chacha20poly1305_seal(ciphertext, keys->key, nonce,
                                      plaintext, pt_len, hdr, 5);
    }

    free(plaintext);
    keys->seq++;

    int rc = 0;
    if (tls_raw_write(conn->tcp, hdr, 5) != 0) rc = -1;
    if (rc == 0 &&
        tls_raw_write(conn->tcp, ciphertext, ct_len) != 0)
        rc = -1;
    free(ciphertext);
    return rc;
}

static int tls_send_encrypted(
    neverc_tls_conn_t *conn, uint8_t inner_type,
    const uint8_t *data, size_t len) {
    if (!conn || !conn->mutexes_initialized)
        return -1;
    tls_mutex_lock(&conn->write_mutex);
    int result = tls_send_encrypted_unlocked(
        conn, inner_type, data, len);
    tls_mutex_unlock(&conn->write_mutex);
    return result;
}

static int tls_append_handshake_bytes(
    neverc_tls_conn_t *conn, const uint8_t *data, size_t data_len) {
    if (!conn || (!data && data_len != 0) ||
        conn->handshake_len > TLS_MAX_HANDSHAKE ||
        data_len > TLS_MAX_HANDSHAKE - conn->handshake_len)
        return -1;
    if (data_len == 0)
        return 0;

    size_t required = conn->handshake_len + data_len;
    if (required > conn->handshake_cap) {
        size_t capacity = conn->handshake_cap ?
                          conn->handshake_cap : 1024;
        while (capacity < required) {
            if (capacity >= TLS_MAX_HANDSHAKE / 2) {
                capacity = TLS_MAX_HANDSHAKE;
                break;
            }
            capacity *= 2;
        }
        uint8_t *resized =
            (uint8_t *)realloc(conn->handshake_buf, capacity);
        if (!resized)
            return -1;
        conn->handshake_buf = resized;
        conn->handshake_cap = capacity;
    }
    memcpy(conn->handshake_buf + conn->handshake_len,
           data, data_len);
    conn->handshake_len = required;
    return 0;
}

/* Returns 1 when a complete message is available, 0 when more record bytes
 * are needed, and -1 when the advertised message length exceeds the bound. */
static int tls_next_handshake_message(
    neverc_tls_conn_t *conn, const uint8_t **message,
    size_t *message_len) {
    if (!conn || !message || !message_len)
        return -1;
    *message = NULL;
    *message_len = 0;
    if (conn->handshake_len < 4)
        return 0;

    size_t body_len = get_u24(conn->handshake_buf + 1);
    if (body_len > TLS_MAX_HANDSHAKE - 4)
        return -1;
    size_t total_len = 4 + body_len;
    if (conn->handshake_len < total_len)
        return 0;
    *message = conn->handshake_buf;
    *message_len = total_len;
    return 1;
}

static int tls_consume_handshake_message(
    neverc_tls_conn_t *conn, size_t message_len) {
    if (!conn || message_len > conn->handshake_len)
        return -1;
    size_t remaining = conn->handshake_len - message_len;
    if (remaining > 0) {
        memmove(conn->handshake_buf,
                conn->handshake_buf + message_len,
                remaining);
    }
    conn->handshake_len = remaining;
    return 0;
}

static void tls_clear_handshake_buffer(neverc_tls_conn_t *conn) {
    if (!conn || !conn->handshake_buf)
        return;
    neverc_platform_secure_zero(
        conn->handshake_buf, conn->handshake_cap);
    free(conn->handshake_buf);
    conn->handshake_buf = NULL;
    conn->handshake_len = 0;
    conn->handshake_cap = 0;
}

#if defined(NEVERC_TLS_TESTING)
int neverc_tls_test_handshake_reassembly(void) {
    static const uint8_t messages[] = {
        TLS_HS_ENCRYPTED_EXT, 0, 0, 2, 0, 0,
        TLS_HS_KEY_UPDATE, 0, 0, 1, 0
    };
    static const uint8_t oversized_header[] = {
        TLS_HS_CERTIFICATE, 0x01, 0x00, 0x00
    };
    neverc_tls_conn_t conn;
    memset(&conn, 0, sizeof(conn));
    const uint8_t *message = NULL;
    size_t message_len = 0;
    int result = -1;

    if (tls_append_handshake_bytes(
            &conn, messages, 3) != 0 ||
        tls_next_handshake_message(
            &conn, &message, &message_len) != 0 ||
        tls_append_handshake_bytes(
            &conn, messages + 3, sizeof(messages) - 3) != 0 ||
        tls_next_handshake_message(
            &conn, &message, &message_len) != 1 ||
        message_len != 6 ||
        memcmp(message, messages, message_len) != 0 ||
        tls_consume_handshake_message(
            &conn, message_len) != 0 ||
        tls_next_handshake_message(
            &conn, &message, &message_len) != 1 ||
        message_len != 5 ||
        memcmp(message, messages + 6, message_len) != 0 ||
        tls_consume_handshake_message(
            &conn, message_len) != 0 ||
        conn.handshake_len != 0)
        goto done;

    if (tls_append_handshake_bytes(
            &conn, oversized_header,
            sizeof(oversized_header)) != 0 ||
        tls_next_handshake_message(
            &conn, &message, &message_len) != -1)
        goto done;
    result = 0;

done:
    tls_clear_handshake_buffer(&conn);
    return result;
}
#endif

static size_t tls_handshake_fragment_size(
    const neverc_tls_conn_t *conn, size_t data_len) {
#if defined(NEVERC_TLS_TESTING)
    if (conn && conn->test_handshake_fragment_size > 0 &&
        conn->test_handshake_fragment_size < data_len)
        return conn->test_handshake_fragment_size;
#else
    (void)conn;
#endif
    return data_len;
}

static int tls_send_plain_handshake(
    neverc_tls_conn_t *conn, const uint8_t *data, size_t data_len) {
    if (!conn || !conn->tcp || !data || data_len == 0)
        return -1;
    size_t fragment_size =
        tls_handshake_fragment_size(conn, data_len);
    for (size_t offset = 0; offset < data_len;) {
        size_t remaining = data_len - offset;
        size_t chunk = remaining < fragment_size ?
                       remaining : fragment_size;
        if (tls_send_record(
                conn->tcp, TLS_CT_HANDSHAKE,
                data + offset, chunk) != 0)
            return -1;
        offset += chunk;
    }
    return 0;
}

static int tls_send_encrypted_handshake(
    neverc_tls_conn_t *conn, const uint8_t *data, size_t data_len) {
    if (!conn || !data || data_len == 0)
        return -1;
    size_t fragment_size =
        tls_handshake_fragment_size(conn, data_len);
    for (size_t offset = 0; offset < data_len;) {
        size_t remaining = data_len - offset;
        size_t chunk = remaining < fragment_size ?
                       remaining : fragment_size;
        if (tls_send_encrypted(
                conn, TLS_CT_HANDSHAKE,
                data + offset, chunk) != 0)
            return -1;
        offset += chunk;
    }
    return 0;
}

static int tls_send_alert_level(
    neverc_tls_conn_t *conn, uint8_t level, uint8_t description) {
    if (!conn || !conn->mutexes_initialized)
        return -1;
    tls_mutex_lock(&conn->write_mutex);
    if (!conn->tcp || conn->alert_sent || conn->write_closed) {
        tls_mutex_unlock(&conn->write_mutex);
        return -1;
    }
    uint8_t alert[2] = {level, description};
    int result = conn->write_keys_active ?
        tls_send_encrypted_unlocked(
            conn, TLS_CT_ALERT, alert, sizeof(alert)) :
        tls_send_record(conn->tcp, TLS_CT_ALERT, alert, sizeof(alert));
    conn->alert_sent = 1;
    if (description == TLS_ALERT_CLOSE_NOTIFY)
        conn->write_closed = 1;
    else
        conn->closed = 1;
    tls_mutex_unlock(&conn->write_mutex);
    return result;
}

static int tls_send_alert(
    neverc_tls_conn_t *conn, uint8_t description) {
    return tls_send_alert_level(conn, 2, description);
}

static int tls_send_close_notify(neverc_tls_conn_t *conn) {
    return tls_send_alert_level(
        conn, 1, TLS_ALERT_CLOSE_NOTIFY);
}

static int tls_fail(
    neverc_tls_conn_t *conn, uint8_t description) {
    (void)tls_send_alert(conn, description);
    return -1;
}

static int tls_error(
    neverc_tls_conn_t *conn, const char *reason) {
    if (conn)
        conn->failure_reason = reason;
    return -1;
}

static int tls_protocol_error(
    neverc_tls_conn_t *conn, uint8_t description,
    const char *reason) {
    (void)tls_send_alert(conn, description);
    return tls_error(conn, reason);
}

static int tls_recv_record(neverc_tls_conn_t *conn, uint8_t *out_type,
                             uint8_t *out_data, size_t *out_len) {
    if (!conn || !conn->tcp || !out_type || !out_data || !out_len)
        return -1;

    /* Read from TCP until we have a complete record */
    while (conn->read_buf_len < TLS_RECORD_HEADER_SIZE) {
        int n = neverc_tcp_read(conn->tcp,
                                 conn->read_buf + conn->read_buf_len,
                                 sizeof(conn->read_buf) - conn->read_buf_len);
        if (n <= 0) {
            conn->closed = 1;
            return tls_error(
                conn, "TLS peer closed without close_notify");
        }
        conn->read_buf_len += (size_t)n;
    }

    *out_type = conn->read_buf[0];
    uint16_t record_version = get_u16(conn->read_buf + 1);
    int is_ciphertext =
        conn->write_keys_active &&
        *out_type == TLS_CT_APPLICATION_DATA;
    if (is_ciphertext &&
        record_version != TLS_LEGACY_VERSION)
        return tls_protocol_error(
            conn, TLS_ALERT_PROTOCOL_VERSION,
            "invalid TLS record version");
    uint16_t rec_len = get_u16(conn->read_buf + 3);
    size_t record_limit = is_ciphertext ?
                          TLS_MAX_CIPHERTEXT :
                          TLS_MAX_PLAINTEXT;
    if (rec_len > record_limit)
        return tls_protocol_error(
            conn, TLS_ALERT_RECORD_OVERFLOW,
            "TLS record exceeds the configured limit");
    size_t total = TLS_RECORD_HEADER_SIZE + rec_len;
    while (conn->read_buf_len < total) {
        int n = neverc_tcp_read(conn->tcp,
                                 conn->read_buf + conn->read_buf_len,
                                 sizeof(conn->read_buf) - conn->read_buf_len);
        if (n <= 0) {
            conn->closed = 1;
            return tls_error(
                conn, "truncated TLS record");
        }
        conn->read_buf_len += (size_t)n;
    }

    memcpy(out_data, conn->read_buf + TLS_RECORD_HEADER_SIZE, rec_len);
    *out_len = rec_len;

    /* Consume the record from the buffer */
    size_t remaining = conn->read_buf_len - total;
    if (remaining > 0)
        memmove(conn->read_buf, conn->read_buf + total, remaining);
    conn->read_buf_len = remaining;

    return 0;
}

static int tls_recv_decrypt(neverc_tls_conn_t *conn,
                              uint8_t *out_inner_type,
                              uint8_t *out_data, size_t *out_len) {
    if (!conn || !out_inner_type || !out_data || !out_len)
        return -1;

    uint8_t rec_type;
    uint8_t rec_data[TLS_MAX_CIPHERTEXT];
    size_t rec_len;

    for (;;) {
        if (tls_recv_record(
                conn, &rec_type, rec_data, &rec_len) != 0)
            return -1;
        if (rec_type != TLS_CT_CHANGE_CIPHER_SPEC)
            break;
        if (rec_len != 1 || rec_data[0] != 1)
            return tls_protocol_error(
                conn, TLS_ALERT_DECODE_ERROR,
                "malformed TLS change_cipher_spec record");
        if (++conn->non_advancing_records >
            TLS_MAX_NON_ADVANCING_RECORDS)
            return tls_protocol_error(
                conn, TLS_ALERT_UNEXPECTED_MESSAGE,
                "too many non-advancing TLS records");
    }

    if (rec_type != TLS_CT_APPLICATION_DATA)
        return tls_protocol_error(
            conn, TLS_ALERT_UNEXPECTED_MESSAGE,
            "received plaintext record after TLS keys became active");

    tls_traffic_keys_t *keys = &conn->read_keys;
    if (keys->seq == UINT64_MAX)
        return tls_protocol_error(
            conn, TLS_ALERT_INTERNAL_ERROR,
            "TLS read sequence number exhausted");

    uint8_t nonce[12];
    memcpy(nonce, keys->iv, 12);
    for (int i = 0; i < 8; i++)
        nonce[12 - 1 - i] ^= (uint8_t)(keys->seq >> (i * 8));

    /* Reconstruct AAD (record header) */
    uint8_t aad[5];
    aad[0] = TLS_CT_APPLICATION_DATA;
    put_u16(aad + 1, TLS_LEGACY_VERSION);
    put_u16(aad + 3, (uint16_t)rec_len);

    if (rec_len <= TLS_AEAD_TAG_SIZE)
        return tls_protocol_error(
            conn, TLS_ALERT_BAD_RECORD_MAC,
            "TLS ciphertext is too short");

    size_t ct_body_len = rec_len - TLS_AEAD_TAG_SIZE;
    uint8_t *plaintext = (uint8_t *)malloc(ct_body_len);
    if (!plaintext)
        return tls_protocol_error(
            conn, TLS_ALERT_INTERNAL_ERROR,
            "TLS record allocation failed");

    int decrypt_ok = -1;
    if (keys->id == TLS_CIPHER_AES_128_GCM_SHA256) {
        uint8_t *tag = rec_data + ct_body_len;
        decrypt_ok = neverc_gcm_open(&keys->gcm, nonce,
                                       rec_data, ct_body_len,
                                       aad, 5, tag, plaintext);
    } else {
        int ptlen = neverc_chacha20poly1305_open(plaintext, keys->key, nonce,
                                                   rec_data, rec_len,
                                                   aad, 5);
        decrypt_ok = (ptlen >= 0) ? 0 : -1;
        if (decrypt_ok == 0) ct_body_len = (size_t)ptlen;
    }

    if (decrypt_ok != 0) {
        free(plaintext);
        return tls_protocol_error(
            conn, TLS_ALERT_BAD_RECORD_MAC,
            "TLS record authentication failed");
    }

    keys->seq++;

    /* Remove padding and find inner content type (last non-zero byte) */
    while (ct_body_len > 0 && plaintext[ct_body_len - 1] == 0)
        ct_body_len--;
    if (ct_body_len == 0) {
        free(plaintext);
        return tls_protocol_error(
            conn, TLS_ALERT_UNEXPECTED_MESSAGE,
            "TLS inner plaintext has no content type");
    }

    *out_inner_type = plaintext[ct_body_len - 1];
    ct_body_len--;
    if (ct_body_len > TLS_MAX_PLAINTEXT) {
        free(plaintext);
        return tls_protocol_error(
            conn, TLS_ALERT_RECORD_OVERFLOW,
            "TLS inner plaintext exceeds the configured limit");
    }
    if (*out_inner_type != TLS_CT_ALERT &&
        *out_inner_type != TLS_CT_HANDSHAKE &&
        *out_inner_type != TLS_CT_APPLICATION_DATA) {
        free(plaintext);
        return tls_protocol_error(
            conn, TLS_ALERT_UNEXPECTED_MESSAGE,
            "TLS inner plaintext has an invalid content type");
    }
    memcpy(out_data, plaintext, ct_body_len);
    *out_len = ct_body_len;

    free(plaintext);
    return 0;
}

static int tls_recv_plain_handshake_message(
    neverc_tls_conn_t *conn, uint8_t expected_type,
    const uint8_t **message, size_t *message_len) {
    if (!conn || !message || !message_len)
        return -1;

    for (;;) {
        int available = tls_next_handshake_message(
            conn, message, message_len);
        if (available < 0)
            return tls_protocol_error(
                conn, TLS_ALERT_DECODE_ERROR,
                "TLS handshake message exceeds the configured limit");
        if (available > 0) {
            if ((*message)[0] != expected_type)
                return tls_protocol_error(
                    conn, TLS_ALERT_UNEXPECTED_MESSAGE,
                    "TLS handshake message arrived out of order");
            return 0;
        }

        uint8_t record_type;
        uint8_t record_data[TLS_MAX_CIPHERTEXT];
        size_t record_len;
        if (tls_recv_record(
                conn, &record_type,
                record_data, &record_len) != 0)
            return -1;
        if (record_type == TLS_CT_ALERT)
            return tls_error(
                conn, "peer sent an alert during TLS handshake");
        if (record_type != TLS_CT_HANDSHAKE)
            return tls_protocol_error(
                conn, TLS_ALERT_UNEXPECTED_MESSAGE,
                "peer sent a non-handshake plaintext record");
        if (record_len == 0)
            return tls_protocol_error(
                conn, TLS_ALERT_DECODE_ERROR,
                "peer sent an empty TLS handshake record");
        if (record_len >
            TLS_MAX_HANDSHAKE - conn->handshake_len)
            return tls_protocol_error(
                conn, TLS_ALERT_DECODE_ERROR,
                "TLS handshake reassembly exceeds the configured limit");
        if (tls_append_handshake_bytes(
                conn, record_data, record_len) != 0)
            return tls_protocol_error(
                conn, TLS_ALERT_INTERNAL_ERROR,
                "TLS handshake reassembly allocation failed");
    }
}

/* ======================================================================
 * TLS 1.3 Handshake — Client
 * ====================================================================== */

static neverc_tls_conn_t *tls_conn_new(neverc_tcp_conn_t *tcp, int owns) {
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

    size_t certificate_list_len = get_u24(message + pos);
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
        size_t certificate_len = get_u24(message + pos);
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
        size_t extensions_len = get_u16(message + pos);
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
    size_t extensions_len = get_u16(message + pos);
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
        uint16_t extension_type = get_u16(message + pos);
        size_t extension_len = get_u16(message + pos + 2);
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
            size_t algorithms_len = get_u16(message + pos);
            if (algorithms_len != extension_len - 2 ||
                algorithms_len == 0 ||
                (algorithms_len & 1u) != 0)
                return -1;
            for (size_t off = 0; off < algorithms_len; off += 2) {
                if (get_u16(message + pos + 2 + off) ==
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
    put_u24(message + pos, 11);
    pos += 3;
    message[pos++] = 0; /* certificate_request_context */
    put_u16(message + pos, 8);
    pos += 2;
    put_u16(message + pos, TLS_EXT_SIGNATURE_ALGORITHMS);
    pos += 2;
    put_u16(message + pos, 4);
    pos += 2;
    put_u16(message + pos, 2);
    pos += 2;
    put_u16(message + pos, TLS_SIG_ECDSA_SHA256);
    pos += 2;

    if (pos != sizeof(message))
        return -1;
    neverc_sha256_update(transcript, message, sizeof(message));
    return tls_send_encrypted_handshake(
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
    put_u24(message + pos, (uint32_t)(message_len - 4));
    pos += 3;
    message[pos++] = 0; /* certificate_request_context */
    put_u24(message + pos, (uint32_t)certificate_list_len);
    pos += 3;
    if (has_certificate) {
        put_u24(message + pos, (uint32_t)cfg->cert_der_len);
        pos += 3;
        memcpy(message + pos, cfg->cert_der, cfg->cert_der_len);
        pos += cfg->cert_der_len;
        put_u16(message + pos, 0);
        pos += 2;
    }
    if (pos != message_len) {
        free(message);
        return -1;
    }

    neverc_sha256_update(transcript, message, message_len);
    int result = tls_send_encrypted_handshake(
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

    put_u24(message + 1, (uint32_t)(4 + signature_len));
    put_u16(message + 4, signature_scheme);
    put_u16(message + 6, (uint16_t)signature_len);
    size_t message_len = 8 + signature_len;
    neverc_sha256_update(transcript, message, message_len);
    neverc_platform_secure_zero(
        transcript_hash, sizeof(transcript_hash));
    return tls_send_encrypted_handshake(
        conn, message, message_len);
}

static int tls_client_handshake(neverc_tls_conn_t *conn,
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
        return tls_fail(conn, TLS_ALERT_INTERNAL_ERROR);

    tls_client_psk_offer_t psk_offer;
    int psk_offered =
        tls_load_client_psk_offer(cfg, &psk_offer);

    /* Build ClientHello */
    uint8_t ch[TLS_CLIENT_HELLO_CAPACITY];
    size_t ch_len = 0;

    /* Handshake header placeholder (filled in later) */
    size_t hs_hdr_pos = ch_len;
    ch_len += 4;

    /* client_version = TLS 1.2 (legacy) */
    put_u16(ch + ch_len, TLS_LEGACY_VERSION);
    ch_len += 2;

    /* random */
    memcpy(ch + ch_len, client_random, 32);
    ch_len += 32;

    /* session_id (legacy, empty for TLS 1.3) */
    uint8_t session_id[32];
    if (neverc_crypto_rand_read(
            session_id, sizeof(session_id)) != 0) {
        tls_clear_client_psk_offer(&psk_offer);
        return tls_fail(conn, TLS_ALERT_INTERNAL_ERROR);
    }
    ch[ch_len++] = 32;
    memcpy(ch + ch_len, session_id, 32);
    ch_len += 32;

    /* cipher_suites */
    put_u16(ch + ch_len, 4); /* 2 suites * 2 bytes */
    ch_len += 2;
    put_u16(ch + ch_len, NEVERC_TLS_AES_128_GCM_SHA256);
    ch_len += 2;
    put_u16(ch + ch_len, NEVERC_TLS_CHACHA20_POLY1305_SHA256);
    ch_len += 2;

    /* compression_methods (null only) */
    ch[ch_len++] = 1;
    ch[ch_len++] = 0;

    /* Extensions */
    size_t ext_len_pos = ch_len;
    ch_len += 2; /* placeholder for total extensions length */
    size_t ext_start = ch_len;

    /* Extension: supported_versions */
    put_u16(ch + ch_len, TLS_EXT_SUPPORTED_VERSIONS); ch_len += 2;
    put_u16(ch + ch_len, 3); ch_len += 2; /* ext data len */
    ch[ch_len++] = 2; /* list len */
    put_u16(ch + ch_len, NEVERC_TLS_VERSION_13); ch_len += 2;

    /* Extension: supported_groups */
    put_u16(ch + ch_len, TLS_EXT_SUPPORTED_GROUPS); ch_len += 2;
    put_u16(ch + ch_len, 4); ch_len += 2;
    put_u16(ch + ch_len, 2); ch_len += 2;
    put_u16(ch + ch_len, NEVERC_TLS_GROUP_X25519); ch_len += 2;

    /* Extension: signature_algorithms */
    put_u16(ch + ch_len, TLS_EXT_SIGNATURE_ALGORITHMS); ch_len += 2;
    put_u16(ch + ch_len, 12); ch_len += 2;
    put_u16(ch + ch_len, 10); ch_len += 2;
    put_u16(ch + ch_len, TLS_SIG_RSA_PSS_SHA256); ch_len += 2;
    put_u16(ch + ch_len, TLS_SIG_ECDSA_SHA256); ch_len += 2;
    put_u16(ch + ch_len, TLS_SIG_ED25519); ch_len += 2;
    put_u16(ch + ch_len, TLS_SIG_RSA_PSS_SHA384); ch_len += 2;
    put_u16(ch + ch_len, TLS_SIG_RSA_PSS_SHA512); ch_len += 2;

    /* Extension: key_share (X25519) */
    put_u16(ch + ch_len, TLS_EXT_KEY_SHARE); ch_len += 2;
    put_u16(ch + ch_len, 38); ch_len += 2; /* ext data len */
    put_u16(ch + ch_len, 36); ch_len += 2; /* client_shares len */
    put_u16(ch + ch_len, NEVERC_TLS_GROUP_X25519); ch_len += 2;
    put_u16(ch + ch_len, 32); ch_len += 2; /* key_exchange len */
    memcpy(ch + ch_len, my_pubkey, 32); ch_len += 32;

    /* Extension: server_name (SNI) */
    if (cfg && cfg->server_name) {
        size_t sni_len = strlen(cfg->server_name);
        if (sni_len == 0 || sni_len > 255) {
            tls_clear_client_psk_offer(&psk_offer);
            return tls_fail(conn, TLS_ALERT_INTERNAL_ERROR);
        }
        if (tls_set_owned_string(
                &conn->server_name, cfg->server_name,
                sni_len) != 0) {
            tls_clear_client_psk_offer(&psk_offer);
            return tls_fail(conn, TLS_ALERT_INTERNAL_ERROR);
        }
        put_u16(ch + ch_len, TLS_EXT_SERVER_NAME); ch_len += 2;
        put_u16(ch + ch_len, (uint16_t)(sni_len + 5)); ch_len += 2;
        put_u16(ch + ch_len, (uint16_t)(sni_len + 3)); ch_len += 2;
        ch[ch_len++] = 0; /* host_name type */
        put_u16(ch + ch_len, (uint16_t)sni_len); ch_len += 2;
        memcpy(ch + ch_len, cfg->server_name, sni_len); ch_len += sni_len;
    }

    /* Extension: ALPN (must precede pre_shared_key). */
    if (cfg && cfg->alpn_count > 0 && cfg->alpn_protos) {
        size_t list_len = 0;
        for (int i = 0; i < cfg->alpn_count; ++i) {
            const char *proto = cfg->alpn_protos[i];
            size_t proto_len;
            if (!proto || proto[0] == '\0') {
                tls_clear_client_psk_offer(&psk_offer);
                return tls_fail(conn, TLS_ALERT_INTERNAL_ERROR);
            }
            proto_len = strlen(proto);
            if (proto_len > 255 ||
                list_len > TLS_MAX_ALPN_LIST - 1 - proto_len) {
                tls_clear_client_psk_offer(&psk_offer);
                return tls_fail(conn, TLS_ALERT_INTERNAL_ERROR);
            }
            list_len += 1 + proto_len;
        }
        if (list_len == 0 || list_len > UINT16_MAX ||
            ch_len > sizeof(ch) - (6 + list_len)) {
            tls_clear_client_psk_offer(&psk_offer);
            return tls_fail(conn, TLS_ALERT_INTERNAL_ERROR);
        }
        put_u16(ch + ch_len, TLS_EXT_ALPN); ch_len += 2;
        put_u16(ch + ch_len, (uint16_t)(2 + list_len)); ch_len += 2;
        put_u16(ch + ch_len, (uint16_t)list_len); ch_len += 2;
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
            tls_clear_client_psk_offer(&psk_offer);
            psk_offered = 0;
        }
    }
    if (psk_offered) {
        /* Offer only PSK-with-(EC)DHE; PSK must be the final extension. */
        put_u16(ch + ch_len, TLS_EXT_PSK_KEY_EXCHANGE_MODES);
        ch_len += 2;
        put_u16(ch + ch_len, 2);
        ch_len += 2;
        ch[ch_len++] = 1;
        ch[ch_len++] = TLS_PSK_MODE_DHE;

        put_u16(ch + ch_len, TLS_EXT_PRE_SHARED_KEY);
        ch_len += 2;
        size_t psk_ext_len_pos = ch_len;
        ch_len += 2;
        put_u16(ch + ch_len,
                (uint16_t)(2 + psk_offer.ticket_len + 4));
        ch_len += 2;
        put_u16(ch + ch_len,
                (uint16_t)psk_offer.ticket_len);
        ch_len += 2;
        memcpy(ch + ch_len, psk_offer.ticket,
               psk_offer.ticket_len);
        ch_len += psk_offer.ticket_len;
        put_u32(ch + ch_len, psk_offer.obfuscated_age);
        ch_len += 4;
        binder_transcript_len = ch_len;
        put_u16(ch + ch_len, 1 + TLS_HASH_SIZE_SHA256);
        ch_len += 2;
        ch[ch_len++] = TLS_HASH_SIZE_SHA256;
        binder_pos = ch_len;
        memset(ch + ch_len, 0, TLS_HASH_SIZE_SHA256);
        ch_len += TLS_HASH_SIZE_SHA256;
        put_u16(ch + psk_ext_len_pos,
                (uint16_t)(ch_len - psk_ext_len_pos - 2));
    }

    /* Fill extensions length */
    put_u16(ch + ext_len_pos, (uint16_t)(ch_len - ext_start));

    /* Fill handshake header */
    ch[hs_hdr_pos] = TLS_HS_CLIENT_HELLO;
    put_u24(ch + hs_hdr_pos + 1, (uint32_t)(ch_len - 4));

    if (psk_offered &&
        tls_compute_resumption_binder(
            psk_offer.psk, ch, binder_transcript_len,
            ch + binder_pos) != 0) {
        tls_clear_client_psk_offer(&psk_offer);
        return tls_fail(conn, TLS_ALERT_INTERNAL_ERROR);
    }

    /* Hash ClientHello into transcript */
    neverc_sha256_update(&transcript, ch, ch_len);

    /* Send ClientHello record */
    if (tls_send_plain_handshake(conn, ch, ch_len) != 0) {
        tls_clear_client_psk_offer(&psk_offer);
        return -1;
    }

    /* Receive ServerHello */
    const uint8_t *server_hello_message = NULL;
    size_t server_hello_message_len = 0;
    if (tls_recv_plain_handshake_message(
            conn, TLS_HS_SERVER_HELLO,
            &server_hello_message,
            &server_hello_message_len) != 0) {
        tls_clear_client_psk_offer(&psk_offer);
        return -1;
    }

    tls_server_hello_info_t server_hello;
    uint8_t server_hello_alert = TLS_ALERT_DECODE_ERROR;
    if (tls_parse_server_hello(
            server_hello_message, server_hello_message_len,
            session_id, sizeof(session_id),
            &server_hello, &server_hello_alert) != 0) {
        tls_clear_client_psk_offer(&psk_offer);
        return tls_fail(conn, server_hello_alert);
    }

    /* Hash ServerHello into transcript */
    neverc_sha256_update(
        &transcript,
        server_hello_message, server_hello_message_len);
    if (tls_consume_handshake_message(
            conn, server_hello_message_len) != 0) {
        tls_clear_client_psk_offer(&psk_offer);
        return tls_fail(conn, TLS_ALERT_INTERNAL_ERROR);
    }
    if (conn->handshake_len != 0) {
        tls_clear_client_psk_offer(&psk_offer);
        return tls_protocol_error(
            conn, TLS_ALERT_UNEXPECTED_MESSAGE,
            "trailing plaintext data after ServerHello");
    }

    uint16_t selected_cipher =
        server_hello.selected_cipher;
    tls_cipher_id_t cipher_id =
        server_hello.cipher_id;
    conn->cipher_suite = selected_cipher;
    if (server_hello.selected_psk && !psk_offered) {
        tls_clear_client_psk_offer(&psk_offer);
        return tls_fail(conn, TLS_ALERT_ILLEGAL_PARAMETER);
    }
    conn->did_resume = server_hello.selected_psk;
    if (psk_offered && !server_hello.selected_psk)
        tls_config_invalidate_client_session(cfg);

    uint8_t server_pubkey[32];
    memcpy(server_pubkey, server_hello.server_public_key,
           sizeof(server_pubkey));

    /* Compute shared secret via X25519 ECDH */
    uint8_t shared_secret[32];
    if (neverc_ecdh_compute(&ecdh_key, server_pubkey, 32,
                             shared_secret, 32) < 0) {
        tls_clear_client_psk_offer(&psk_offer);
        return -1;
    }

    /* Derive handshake secrets */
    uint8_t handshake_secret[32];
    if (tls_derive_handshake_secret(
            shared_secret,
            server_hello.selected_psk ?
                psk_offer.psk : NULL,
            handshake_secret) != 0) {
        tls_clear_client_psk_offer(&psk_offer);
        return tls_fail(conn, TLS_ALERT_INTERNAL_ERROR);
    }
    if (server_hello.selected_psk) {
        conn->peer_cert = psk_offer.peer_cert;
        conn->peer_cert_len = psk_offer.peer_cert_len;
        psk_offer.peer_cert = NULL;
        psk_offer.peer_cert_len = 0;
        conn->resumption_alpn = psk_offer.alpn;
        psk_offer.alpn = NULL;
    }
    tls_clear_client_psk_offer(&psk_offer);

    /* Transcript hash up to ServerHello */
    uint8_t transcript_hash_sh[32];
    {
        neverc_sha256_ctx copy = transcript;
        neverc_sha256_final(&copy, transcript_hash_sh);
    }

    uint8_t client_hs_traffic_secret[32];
    uint8_t server_hs_traffic_secret[32];
    derive_secret(handshake_secret, "c hs traffic", 12,
                   transcript_hash_sh, client_hs_traffic_secret);
    derive_secret(handshake_secret, "s hs traffic", 12,
                   transcript_hash_sh, server_hs_traffic_secret);

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
    derive_traffic_keys(server_hs_traffic_secret, &conn->read_keys, cipher_id);
    derive_traffic_keys(client_hs_traffic_secret, &conn->write_keys, cipher_id);
    conn->write_keys_active = 1;

    /* Read encrypted handshake messages (EncryptedExtensions, Certificate,
     * CertificateVerify, Finished) */
    int got_finished = 0;
    int certificate_requested = 0;
    uint8_t expected_handshake_type = TLS_HS_ENCRYPTED_EXT;
    while (!got_finished) {
        const uint8_t *handshake_message = NULL;
        size_t handshake_message_len = 0;
        int available = tls_next_handshake_message(
            conn, &handshake_message, &handshake_message_len);
        if (available < 0)
            return tls_protocol_error(
                conn, TLS_ALERT_DECODE_ERROR,
                "server handshake message exceeds the configured limit");
        if (available == 0) {
            uint8_t inner_type;
            uint8_t record_data[TLS_MAX_PLAINTEXT];
            size_t record_len;
            if (tls_recv_decrypt(
                    conn, &inner_type,
                    record_data, &record_len) != 0)
                return tls_error(
                    conn, "failed to decrypt server handshake record");
            if (inner_type != TLS_CT_HANDSHAKE) {
                if (inner_type == TLS_CT_ALERT)
                    return tls_error(
                        conn, "server sent an alert during handshake");
                return tls_protocol_error(
                    conn, TLS_ALERT_UNEXPECTED_MESSAGE,
                    "server sent non-handshake data during handshake");
            }
            if (record_len == 0)
                return tls_protocol_error(
                    conn, TLS_ALERT_DECODE_ERROR,
                    "server sent an empty handshake record");
            if (record_len >
                TLS_MAX_HANDSHAKE - conn->handshake_len)
                return tls_protocol_error(
                    conn, TLS_ALERT_DECODE_ERROR,
                    "server handshake reassembly exceeds the limit");
            if (tls_append_handshake_bytes(
                    conn, record_data, record_len) != 0)
                return tls_protocol_error(
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
            return tls_protocol_error(
                conn, TLS_ALERT_UNEXPECTED_MESSAGE,
                "server handshake message arrived out of order");

        const uint8_t *message = handshake_message + 4;
        if (is_certificate_request) {
            uint8_t request_alert = TLS_ALERT_DECODE_ERROR;
            if (tls_parse_certificate_request(
                    message, msg_len, &request_alert) != 0)
                return tls_protocol_error(
                    conn, request_alert,
                    "malformed or unsupported CertificateRequest");
            certificate_requested = 1;
        } else if (hs_type == TLS_HS_ENCRYPTED_EXT) {
            uint8_t ee_alert = TLS_ALERT_DECODE_ERROR;
            if (tls_parse_encrypted_extensions(
                    conn, cfg, message, msg_len,
                    &ee_alert) != 0)
                return tls_protocol_error(
                    conn, ee_alert,
                    "malformed or invalid server EncryptedExtensions");
            expected_handshake_type = conn->did_resume ?
                TLS_HS_FINISHED : TLS_HS_CERTIFICATE;
        } else if (hs_type == TLS_HS_CERTIFICATE) {
            if (tls_store_peer_certificate(
                    conn, message, msg_len, 0) != 0)
                return tls_protocol_error(
                    conn, TLS_ALERT_DECODE_ERROR,
                    "malformed server Certificate message");
            expected_handshake_type = TLS_HS_CERT_VERIFY;
        } else if (hs_type == TLS_HS_CERT_VERIFY) {
            if (!conn->peer_cert || msg_len < 4)
                return tls_protocol_error(
                    conn, TLS_ALERT_DECODE_ERROR,
                    "malformed server CertificateVerify message");
            uint16_t signature_scheme = get_u16(message);
            size_t signature_len = get_u16(message + 2);
            if (signature_len == 0 ||
                signature_len != msg_len - 4)
                return tls_protocol_error(
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
                return tls_protocol_error(
                    conn, TLS_ALERT_BAD_CERTIFICATE,
                    "failed to parse server certificate");
            int signature_result =
                neverc_tls_verify_certificate_verify(
                    &certificate, signature_scheme, 1,
                    transcript_hash, sizeof(transcript_hash),
                    message + 4, signature_len);
            neverc_x509_cert_free(&certificate);
            if (signature_result != 0)
                return tls_protocol_error(
                    conn, TLS_ALERT_DECRYPT_ERROR,
                    "server CertificateVerify validation failed");
            if (!cfg || !cfg->skip_verify) {
                int chain_result =
                    neverc_tls_verify_server_certificate_chain(
                        cfg, conn->peer_cert,
                        conn->peer_cert_len,
                        conn->peer_intermediates, NULL);
                if (chain_result != 0)
                    return tls_protocol_error(
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
            hkdf_expand_label(
                server_hs_traffic_secret, 32,
                "finished", 8, NULL, 0,
                finished_key, sizeof(finished_key));
            uint8_t expected_verify[32];
            neverc_hmac_sha256(
                finished_key, sizeof(finished_key),
                transcript_hash, sizeof(transcript_hash),
                expected_verify);

            if (msg_len != sizeof(expected_verify) ||
                !neverc_subtle_constant_time_compare(
                    message, expected_verify,
                    sizeof(expected_verify)))
                return tls_protocol_error(
                    conn, TLS_ALERT_DECRYPT_ERROR,
                    "server Finished validation failed");
            neverc_sha256_update(
                &transcript,
                handshake_message, handshake_message_len);
            got_finished = 1;
        }

        if (tls_consume_handshake_message(
                conn, handshake_message_len) != 0)
            return tls_protocol_error(
                conn, TLS_ALERT_INTERNAL_ERROR,
                "failed to advance server handshake buffer");
        if (got_finished && conn->handshake_len != 0)
            return tls_protocol_error(
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
            return tls_error(
                conn, "failed to send client Certificate message");
        if (sent_client_certificate &&
            tls_send_local_certificate_verify(
                conn, cfg, 0, &transcript) != 0)
            return tls_error(
                conn, "failed to send client CertificateVerify message");
    }

    /* Send client Finished */
    {
        uint8_t transcript_hash[32];
        neverc_sha256_ctx copy = transcript;
        neverc_sha256_final(&copy, transcript_hash);

        uint8_t finished_key[32];
        hkdf_expand_label(client_hs_traffic_secret, 32,
                           "finished", 8, NULL, 0, finished_key, 32);

        uint8_t verify_data[32];
        neverc_hmac_sha256(finished_key, 32,
                            transcript_hash, 32,
                            verify_data);

        uint8_t finished_msg[36];
        finished_msg[0] = TLS_HS_FINISHED;
        put_u24(finished_msg + 1, 32);
        memcpy(finished_msg + 4, verify_data, 32);

        neverc_sha256_update(&transcript, finished_msg, 36);

        if (tls_send_encrypted_handshake(
                conn, finished_msg, sizeof(finished_msg)) != 0)
            return -1;
    }

    /* Derive application traffic keys */
    uint8_t empty_hash[32];
    neverc_sha256_sum(NULL, 0, empty_hash);
    uint8_t master_derived[32];
    derive_secret(handshake_secret, "derived", 7, empty_hash, master_derived);

    uint8_t master_secret[32];
    if (tls_hkdf_extract_zero_ikm(
            master_secret, master_derived, 32) != 0)
        return -1;

    uint8_t client_app_secret[32];
    uint8_t server_app_secret[32];
    derive_secret(master_secret, "c ap traffic", 12,
                   transcript_hash_server_finished, client_app_secret);
    derive_secret(master_secret, "s ap traffic", 12,
                   transcript_hash_server_finished, server_app_secret);

    uint8_t transcript_hash_client_finished[32];
    {
        neverc_sha256_ctx copy = transcript;
        neverc_sha256_final(
            &copy, transcript_hash_client_finished);
    }
    derive_secret(master_secret, "res master", 10,
                  transcript_hash_client_finished,
                  conn->resumption_master_secret);

    tls_set_application_keys(
        conn, cipher_id, server_app_secret, client_app_secret);

    tls_clear_handshake_buffer(conn);
    conn->handshake_done = 1;
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
    return 0;
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
    *value = get_u16(cursor->data + cursor->pos);
    cursor->pos += 2;
    return 0;
}

static int tls_cursor_read_u32(tls_cursor_t *cursor, uint32_t *value) {
    if (!cursor || !value || cursor->pos > cursor->len ||
        cursor->len - cursor->pos < 4)
        return -1;
    *value = get_u32(cursor->data + cursor->pos);
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
    if (message_len < 2 || get_u16(message) != message_len - 2)
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
        (size_t)get_u24(message + 1) != message_len - 4)
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
        (size_t)get_u24(message + 1) != message_len - 4)
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

static int tls_send_new_session_ticket(neverc_tls_conn_t *conn);

static int tls_server_handshake(neverc_tls_conn_t *conn,
                                  neverc_tls_config_t *cfg) {
    if (!cfg || !cfg->cert_der || !cfg->key_der) return -1;

    neverc_sha256_ctx transcript;
    neverc_sha256_init(&transcript);

    /* Receive ClientHello */
    const uint8_t *client_hello_message = NULL;
    size_t client_hello_message_len = 0;
    if (tls_recv_plain_handshake_message(
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
        return tls_fail(conn, client_hello_alert);

    if (client_hello.server_name_len > 0 &&
        tls_set_owned_string(
            &conn->server_name, client_hello.server_name,
            client_hello.server_name_len) != 0)
        return tls_fail(conn, TLS_ALERT_INTERNAL_ERROR);

    const char *selected_alpn = NULL;
    size_t selected_alpn_len = 0;
    uint8_t alpn_alert = TLS_ALERT_INTERNAL_ERROR;
    if (tls_negotiate_alpn(
            cfg, client_hello.alpn_protocols,
            client_hello.alpn_count, &selected_alpn,
            &selected_alpn_len, &alpn_alert) != 0)
        return tls_fail(conn, alpn_alert);
    if (selected_alpn_len > 0 &&
        tls_set_owned_string(
            &conn->alpn, selected_alpn,
            selected_alpn_len) != 0)
        return tls_fail(conn, TLS_ALERT_INTERNAL_ERROR);

    int selected_psk_index = -1;
    uint8_t selected_psk[TLS_HASH_SIZE_SHA256] = {0};
    if (cfg->client_auth == NEVERC_TLS_CLIENT_AUTH_NONE) {
        for (size_t i = 0;
             i < client_hello.offered_psk_count; ++i) {
            tls_offered_psk_t *offered =
                &client_hello.offered_psks[i];
            if (!tls_lookup_server_session(
                    cfg, offered->identity,
                    offered->identity_len,
                    offered->obfuscated_age,
                    (const uint8_t *)client_hello.server_name,
                    client_hello.server_name_len,
                    selected_alpn, selected_alpn_len,
                    selected_psk))
                continue;
            uint8_t expected_binder[TLS_HASH_SIZE_SHA256];
            if (tls_compute_resumption_binder(
                    selected_psk, client_hello_message,
                    client_hello.binder_transcript_len,
                    expected_binder) != 0)
                return tls_fail(conn, TLS_ALERT_INTERNAL_ERROR);
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
                return tls_fail(conn, TLS_ALERT_DECRYPT_ERROR);
            }
            selected_psk_index = (int)i;
            break;
        }
    }
    conn->did_resume = selected_psk_index >= 0;

    neverc_sha256_update(
        &transcript,
        client_hello_message, client_hello_message_len);
    if (tls_consume_handshake_message(
            conn, client_hello_message_len) != 0)
        return tls_fail(conn, TLS_ALERT_INTERNAL_ERROR);
    if (conn->handshake_len != 0)
        return tls_protocol_error(
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

    put_u16(sh + sh_len, TLS_LEGACY_VERSION); sh_len += 2;

    uint8_t server_random[32];
    if (neverc_crypto_rand_read(
            server_random, sizeof(server_random)) != 0)
        return tls_fail(conn, TLS_ALERT_INTERNAL_ERROR);
    memcpy(sh + sh_len, server_random, 32); sh_len += 32;

    /* Echo session_id */
    sh[sh_len++] = ch_sid_len;
    if (ch_sid_len > 0) {
        memcpy(sh + sh_len, ch_session_id, ch_sid_len);
        sh_len += ch_sid_len;
    }

    put_u16(sh + sh_len, selected_cipher); sh_len += 2;
    sh[sh_len++] = 0; /* compression */

    /* Extensions */
    size_t sh_ext_len_pos = sh_len; sh_len += 2;
    size_t sh_ext_start = sh_len;

    /* supported_versions */
    put_u16(sh + sh_len, TLS_EXT_SUPPORTED_VERSIONS); sh_len += 2;
    put_u16(sh + sh_len, 2); sh_len += 2;
    put_u16(sh + sh_len, NEVERC_TLS_VERSION_13); sh_len += 2;

    /* key_share */
    put_u16(sh + sh_len, TLS_EXT_KEY_SHARE); sh_len += 2;
    put_u16(sh + sh_len, 36); sh_len += 2;
    put_u16(sh + sh_len, NEVERC_TLS_GROUP_X25519); sh_len += 2;
    put_u16(sh + sh_len, 32); sh_len += 2;
    memcpy(sh + sh_len, server_pubkey, 32); sh_len += 32;

    if (selected_psk_index >= 0) {
        put_u16(sh + sh_len, TLS_EXT_PRE_SHARED_KEY);
        sh_len += 2;
        put_u16(sh + sh_len, 2);
        sh_len += 2;
        put_u16(sh + sh_len,
                (uint16_t)selected_psk_index);
        sh_len += 2;
    }

    put_u16(sh + sh_ext_len_pos, (uint16_t)(sh_len - sh_ext_start));
    put_u24(sh + sh_body_pos, (uint32_t)(sh_len - sh_body_pos - 3));

    neverc_sha256_update(&transcript, sh, sh_len);

    /* Send ServerHello */
    if (tls_send_plain_handshake(conn, sh, sh_len) != 0)
        return -1;

    /* Send Change Cipher Spec (compatibility) */
    {
        uint8_t ccs = 1;
        tls_send_record(conn->tcp, TLS_CT_CHANGE_CIPHER_SPEC, &ccs, 1);
    }

    /* Compute shared secret */
    uint8_t shared_secret[32];
    if (neverc_ecdh_compute(&ecdh_key, client_pubkey, 32,
                             shared_secret, 32) < 0)
        return -1;

    /* Key schedule */
    uint8_t handshake_secret[32];
    if (tls_derive_handshake_secret(
            shared_secret,
            selected_psk_index >= 0 ? selected_psk : NULL,
            handshake_secret) != 0)
        return tls_fail(conn, TLS_ALERT_INTERNAL_ERROR);
    neverc_platform_secure_zero(
        selected_psk, sizeof(selected_psk));

    uint8_t transcript_hash_sh[32];
    {
        neverc_sha256_ctx copy = transcript;
        neverc_sha256_final(&copy, transcript_hash_sh);
    }

    uint8_t client_hs_traffic_secret[32];
    uint8_t server_hs_traffic_secret[32];
    derive_secret(handshake_secret, "c hs traffic", 12,
                   transcript_hash_sh, client_hs_traffic_secret);
    derive_secret(handshake_secret, "s hs traffic", 12,
                   transcript_hash_sh, server_hs_traffic_secret);

    derive_traffic_keys(server_hs_traffic_secret, &conn->write_keys, cipher_id);
    derive_traffic_keys(client_hs_traffic_secret, &conn->read_keys, cipher_id);
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
                return tls_fail(conn, TLS_ALERT_INTERNAL_ERROR);
            put_u16(ee + ee_len, TLS_EXT_ALPN);
            ee_len += 2;
            put_u16(ee + ee_len,
                    (uint16_t)(3 + selected_alpn_len));
            ee_len += 2;
            put_u16(ee + ee_len,
                    (uint16_t)(1 + selected_alpn_len));
            ee_len += 2;
            ee[ee_len++] = (uint8_t)selected_alpn_len;
            memcpy(ee + ee_len, selected_alpn,
                   selected_alpn_len);
            ee_len += selected_alpn_len;
        }

        put_u16(ee + ext_len_pos,
                (uint16_t)(ee_len - ext_start));
        put_u24(ee + body_len_pos,
                (uint32_t)(ee_len - body_len_pos - 3));
        neverc_sha256_update(&transcript, ee, ee_len);
        if (tls_send_encrypted_handshake(
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
        hkdf_expand_label(server_hs_traffic_secret, 32,
                           "finished", 8, NULL, 0, finished_key, 32);

        uint8_t verify_data[32];
        neverc_hmac_sha256(finished_key, 32,
                            transcript_hash, 32,
                            verify_data);

        uint8_t finished_msg[36];
        finished_msg[0] = TLS_HS_FINISHED;
        put_u24(finished_msg + 1, 32);
        memcpy(finished_msg + 4, verify_data, 32);

        neverc_sha256_update(&transcript, finished_msg, 36);
        if (tls_send_encrypted_handshake(
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
        cfg->client_auth ==
            NEVERC_TLS_CLIENT_AUTH_REQUIRE_AND_VERIFY ?
        TLS_HS_CERTIFICATE : TLS_HS_FINISHED;
    while (!got_client_finished) {
        const uint8_t *handshake_message = NULL;
        size_t handshake_message_len = 0;
        int available = tls_next_handshake_message(
            conn, &handshake_message, &handshake_message_len);
        if (available < 0)
            return tls_protocol_error(
                conn, TLS_ALERT_DECODE_ERROR,
                "client handshake message exceeds the configured limit");
        if (available == 0) {
            uint8_t inner_type;
            uint8_t record_data[TLS_MAX_PLAINTEXT];
            size_t record_len;
            if (tls_recv_decrypt(
                    conn, &inner_type,
                    record_data, &record_len) != 0)
                return -1;
            if (inner_type != TLS_CT_HANDSHAKE) {
                if (inner_type == TLS_CT_ALERT)
                    return tls_error(
                        conn, "client sent an alert during handshake");
                return tls_protocol_error(
                    conn, TLS_ALERT_UNEXPECTED_MESSAGE,
                    "client sent non-handshake data during handshake");
            }
            if (record_len == 0)
                return tls_protocol_error(
                    conn, TLS_ALERT_DECODE_ERROR,
                    "client sent an empty handshake record");
            if (record_len >
                TLS_MAX_HANDSHAKE - conn->handshake_len)
                return tls_protocol_error(
                    conn, TLS_ALERT_DECODE_ERROR,
                    "client handshake reassembly exceeds the limit");
            if (tls_append_handshake_bytes(
                    conn, record_data, record_len) != 0)
                return tls_protocol_error(
                    conn, TLS_ALERT_INTERNAL_ERROR,
                    "client handshake reassembly allocation failed");
            continue;
        }

        uint8_t message_type = handshake_message[0];
        size_t message_body_len = handshake_message_len - 4;
        if (message_type != expected_client_handshake)
            return tls_protocol_error(
                conn, TLS_ALERT_UNEXPECTED_MESSAGE,
                "client handshake message arrived out of order");

        const uint8_t *message = handshake_message + 4;
        if (message_type == TLS_HS_CERTIFICATE) {
            int certificate_result = tls_store_peer_certificate(
                conn, message, message_body_len, 1);
            if (certificate_result == 1)
                return tls_fail(
                    conn, TLS_ALERT_CERTIFICATE_REQUIRED);
            if (certificate_result != 0)
                return tls_fail(conn, TLS_ALERT_DECODE_ERROR);
            neverc_sha256_update(
                &transcript,
                handshake_message, handshake_message_len);
            expected_client_handshake = TLS_HS_CERT_VERIFY;
        } else if (message_type == TLS_HS_CERT_VERIFY) {
            if (!conn->peer_cert || message_body_len < 4)
                return tls_fail(conn, TLS_ALERT_DECODE_ERROR);
            uint16_t signature_scheme = get_u16(message);
            size_t signature_len = get_u16(message + 2);
            if (signature_len == 0 ||
                signature_len != message_body_len - 4)
                return tls_fail(conn, TLS_ALERT_DECODE_ERROR);

            uint8_t transcript_hash[32];
            neverc_sha256_ctx copy = transcript;
            neverc_sha256_final(&copy, transcript_hash);
            neverc_x509_cert_t certificate;
            if (neverc_x509_parse_certificate(
                    &certificate, conn->peer_cert,
                    conn->peer_cert_len) != 0)
                return tls_fail(
                    conn, TLS_ALERT_BAD_CERTIFICATE);
            int signature_result =
                neverc_tls_verify_certificate_verify(
                    &certificate, signature_scheme, 0,
                    transcript_hash, sizeof(transcript_hash),
                    message + 4, signature_len);
            neverc_x509_cert_free(&certificate);
            if (signature_result != 0)
                return tls_fail(
                    conn, TLS_ALERT_DECRYPT_ERROR);
            if (tls_verify_certificate_chain(
                    cfg, conn->peer_cert,
                    conn->peer_cert_len,
                    conn->peer_intermediates, NULL, NULL,
                    NEVERC_X509_EXT_KEY_USAGE_CLIENT_AUTH,
                    0) != 0)
                return tls_fail(
                    conn, TLS_ALERT_BAD_CERTIFICATE);

            neverc_sha256_update(
                &transcript,
                handshake_message, handshake_message_len);
            expected_client_handshake = TLS_HS_FINISHED;
        } else {
            if (message_body_len != 32)
                return tls_fail(conn, TLS_ALERT_DECODE_ERROR);

            uint8_t transcript_hash[32];
            neverc_sha256_ctx copy = transcript;
            neverc_sha256_final(&copy, transcript_hash);
            uint8_t finished_key[32];
            hkdf_expand_label(
                client_hs_traffic_secret, 32,
                "finished", 8, NULL, 0,
                finished_key, sizeof(finished_key));
            uint8_t expected[32];
            neverc_hmac_sha256(
                finished_key, sizeof(finished_key),
                transcript_hash, sizeof(transcript_hash),
                expected);
            if (!neverc_subtle_constant_time_compare(
                    message, expected, sizeof(expected)))
                return tls_fail(
                    conn, TLS_ALERT_DECRYPT_ERROR);

            neverc_sha256_update(
                &transcript,
                handshake_message, handshake_message_len);
            got_client_finished = 1;
        }

        if (tls_consume_handshake_message(
                conn, handshake_message_len) != 0)
            return tls_protocol_error(
                conn, TLS_ALERT_INTERNAL_ERROR,
                "failed to advance client handshake buffer");
        if (got_client_finished && conn->handshake_len != 0)
            return tls_protocol_error(
                conn, TLS_ALERT_UNEXPECTED_MESSAGE,
                "trailing data after client Finished");
    }

    /* Derive application traffic keys */
    uint8_t empty_hash[32];
    neverc_sha256_sum(NULL, 0, empty_hash);
    uint8_t master_derived[32];
    derive_secret(handshake_secret, "derived", 7, empty_hash, master_derived);

    uint8_t master_secret[32];
    if (tls_hkdf_extract_zero_ikm(
            master_secret, master_derived, 32) != 0)
        return -1;

    uint8_t client_app_secret[32];
    uint8_t server_app_secret[32];
    derive_secret(master_secret, "c ap traffic", 12,
                   transcript_hash_server_finished, client_app_secret);
    derive_secret(master_secret, "s ap traffic", 12,
                   transcript_hash_server_finished, server_app_secret);

    uint8_t transcript_hash_client_finished[32];
    {
        neverc_sha256_ctx copy = transcript;
        neverc_sha256_final(
            &copy, transcript_hash_client_finished);
    }
    derive_secret(master_secret, "res master", 10,
                  transcript_hash_client_finished,
                  conn->resumption_master_secret);

    tls_set_application_keys(
        conn, cipher_id, client_app_secret, server_app_secret);

    tls_clear_handshake_buffer(conn);
    conn->handshake_done = 1;
    if (cfg->client_auth == NEVERC_TLS_CLIENT_AUTH_NONE &&
        tls_send_new_session_ticket(conn) != 0)
        return tls_error(
            conn, "failed to send TLS NewSessionTicket");
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
    return 0;
}

/* ======================================================================
 * TLS 1.3 Post-Handshake Messages
 * ====================================================================== */

static int tls_send_key_update_message(
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
    if (tls_send_encrypted_unlocked(
            conn, TLS_CT_HANDSHAKE,
            message, sizeof(message)) != 0) {
        conn->closed = 1;
        int result =
            tls_error(conn, "failed to send TLS KeyUpdate");
        tls_mutex_unlock(&conn->write_mutex);
        return result;
    }
    if (tls_update_traffic_secret(
            conn->write_traffic_secret,
            &conn->write_keys) != 0) {
        conn->closed = 1;
        int result =
            tls_error(conn, "failed to rotate TLS write keys");
        tls_mutex_unlock(&conn->write_mutex);
        return result;
    }
    tls_mutex_unlock(&conn->write_mutex);
    return 0;
}

static int tls_send_new_session_ticket(neverc_tls_conn_t *conn) {
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
    uint32_t age_add = get_u32(age_add_bytes);
    uint64_t issued_at_ms = tls_wall_time_ms();
    if (issued_at_ms == 0 ||
        tls_derive_resumption_psk(
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
    put_u24(message + pos, (uint32_t)(sizeof(message) - 4));
    pos += 3;
    put_u32(message + pos, TLS_TICKET_LIFETIME);
    pos += 4;
    put_u32(message + pos, age_add);
    pos += 4;
    message[pos++] = TLS_TICKET_NONCE_SIZE;
    memcpy(message + pos, nonce, sizeof(nonce));
    pos += sizeof(nonce);
    put_u16(message + pos, TLS_SERVER_TICKET_SIZE);
    pos += 2;
    memcpy(message + pos, ticket, sizeof(ticket));
    pos += sizeof(ticket);
    put_u16(message + pos, 0);
    pos += 2;
    if (pos != sizeof(message)) {
        neverc_platform_secure_zero(psk, sizeof(psk));
        return -1;
    }

    tls_store_server_session(
        conn->config, ticket, psk,
        TLS_TICKET_LIFETIME, age_add, issued_at_ms,
        conn->server_name,
        conn->server_name ? strlen(conn->server_name) : 0,
        conn->alpn,
        conn->alpn ? strlen(conn->alpn) : 0);
    neverc_platform_secure_zero(psk, sizeof(psk));
    return tls_send_encrypted_handshake(
        conn, message, sizeof(message));
}

static int tls_parse_new_session_ticket(
    neverc_tls_conn_t *conn,
    const uint8_t *body, size_t body_len) {
    if (!conn || !conn->config || !body || body_len < 13)
        return -1;

    uint32_t lifetime = get_u32(body);
    uint32_t age_add = get_u32(body + 4);
    size_t pos = 8;
    size_t nonce_len = body[pos++];
    if (nonce_len > body_len - pos)
        return -1;
    const uint8_t *nonce = body + pos;
    pos += nonce_len;

    if (body_len - pos < 2)
        return -1;
    size_t ticket_len = get_u16(body + pos);
    pos += 2;
    if (ticket_len == 0 || ticket_len > body_len - pos)
        return -1;
    const uint8_t *ticket = body + pos;
    pos += ticket_len;

    if (body_len - pos < 2)
        return -1;
    size_t extensions_len = get_u16(body + pos);
    pos += 2;
    if (extensions_len != body_len - pos)
        return -1;

    size_t extensions_end = pos + extensions_len;
    int saw_early_data = 0;
    while (pos < extensions_end) {
        if (extensions_end - pos < 4)
            return -1;
        uint16_t extension_type = get_u16(body + pos);
        size_t extension_len = get_u16(body + pos + 2);
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
    if (tls_derive_resumption_psk(
            conn->resumption_master_secret,
            nonce, nonce_len, psk) != 0)
        return -1;
    int store_result = tls_store_client_session(
        conn->config, ticket, ticket_len, psk,
        lifetime, age_add, conn->alpn,
        conn->peer_cert, conn->peer_cert_len);
    neverc_platform_secure_zero(psk, sizeof(psk));
    /* Session caching is opportunistic; allocation pressure must not turn a
     * valid post-handshake ticket into a connection failure. */
    (void)store_result;
    return 0;
}

static int tls_append_post_handshake(
    neverc_tls_conn_t *conn, const uint8_t *data, size_t data_len) {
    if (!conn || (!data && data_len != 0) ||
        conn->post_handshake_len > TLS_MAX_HANDSHAKE ||
        data_len > TLS_MAX_HANDSHAKE - conn->post_handshake_len)
        return tls_protocol_error(
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
            return tls_protocol_error(
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

static int tls_handle_post_handshake(
    neverc_tls_conn_t *conn, const uint8_t *data, size_t data_len) {
    if (data_len == 0)
        return tls_protocol_error(
            conn, TLS_ALERT_UNEXPECTED_MESSAGE,
            "received an empty TLS post-handshake record");
    if (++conn->non_advancing_records >
        TLS_MAX_NON_ADVANCING_RECORDS)
        return tls_protocol_error(
            conn, TLS_ALERT_UNEXPECTED_MESSAGE,
            "too many non-advancing TLS records");
    if (tls_append_post_handshake(conn, data, data_len) != 0)
        return -1;

    while (conn->post_handshake_len >= 4) {
        uint8_t message_type = conn->post_handshake_buf[0];
        size_t body_len = get_u24(conn->post_handshake_buf + 1);
        if (body_len > TLS_MAX_HANDSHAKE - 4)
            return tls_protocol_error(
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
                return tls_protocol_error(
                    conn, TLS_ALERT_UNEXPECTED_MESSAGE,
                    "TLS KeyUpdate was interleaved with another message");
            if (body_len != 1)
                return tls_protocol_error(
                    conn, TLS_ALERT_DECODE_ERROR,
                    "malformed TLS KeyUpdate");
            if (body[0] > 1)
                return tls_protocol_error(
                    conn, TLS_ALERT_ILLEGAL_PARAMETER,
                    "invalid TLS KeyUpdate request");
            if (body[0] == 1 &&
                tls_send_key_update_message(conn, 0) != 0)
                return -1;
            if (tls_update_traffic_secret(
                    conn->read_traffic_secret,
                    &conn->read_keys) != 0) {
                conn->closed = 1;
                return tls_error(
                    conn, "failed to rotate TLS read keys");
            }
        } else if (message_type == TLS_HS_NEW_SESSION_TICKET) {
            if (conn->is_server)
                return tls_protocol_error(
                    conn, TLS_ALERT_UNEXPECTED_MESSAGE,
                    "TLS client sent NewSessionTicket");
            if (tls_parse_new_session_ticket(
                    conn, body, body_len) != 0)
                return tls_protocol_error(
                    conn, TLS_ALERT_DECODE_ERROR,
                    "malformed TLS NewSessionTicket");
        } else {
            return tls_protocol_error(
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

static int tls_handle_peer_alert(
    neverc_tls_conn_t *conn, const uint8_t *data, size_t data_len) {
    if (data_len != 2)
        return tls_protocol_error(
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

/* ======================================================================
 * Public API
 * ====================================================================== */

static const char k_tls_unavailable[] =
    "TLS transport is unavailable pending end-to-end interoperability "
    "and security validation";

static void tls_set_unavailable(const char **errp) {
    if (errp)
        *errp = k_tls_unavailable;
}

#if defined(NEVERC_TLS_ENABLE_EXPERIMENTAL_TRANSPORT)
static const char k_tls_invalid_argument[] =
    "TLS transport received an invalid argument";
static const char k_tls_handshake_failed[] =
    "TLS 1.3 handshake failed";
static const char k_tls_allocation_failed[] =
    "TLS transport allocation failed";

static neverc_tls_conn_t *tls_start_handshake(
    neverc_tcp_conn_t *tcp, neverc_tls_config_t *cfg,
    int from_server, int owns_tcp, const char **errp) {
    if (errp)
        *errp = NULL;
    if (!tcp || !cfg ||
        (from_server &&
         (!cfg->cert_der || !cfg->key_der)) ||
        (from_server &&
         cfg->client_auth ==
             NEVERC_TLS_CLIENT_AUTH_REQUIRE_AND_VERIFY &&
         !cfg->root_certificates) ||
        (!from_server && !cfg->skip_verify &&
         (!cfg->server_name || cfg->server_name[0] == '\0'))) {
        if (errp)
            *errp = k_tls_invalid_argument;
        return NULL;
    }

    neverc_tls_conn_t *conn = tls_conn_new(tcp, owns_tcp);
    if (!conn) {
        if (errp)
            *errp = k_tls_allocation_failed;
        return NULL;
    }
    conn->is_server = from_server != 0;
    tls_config_retain(cfg);
    conn->config = cfg;
#if defined(NEVERC_TLS_TESTING)
    conn->test_handshake_fragment_size =
        cfg->test_handshake_fragment_size;
#endif
    int result = from_server ?
        tls_server_handshake(conn, cfg) :
        tls_client_handshake(conn, cfg);
    if (result != 0) {
        const char *failure_reason = conn->failure_reason;
        conn->owns_tcp = 0;
        neverc_tls_close(conn);
        if (errp)
            *errp = failure_reason ?
                failure_reason : k_tls_handshake_failed;
        return NULL;
    }
    return conn;
}
#endif

neverc_tls_conn_t *neverc_tls_dial(const char *addr,
                                    neverc_tls_config_t *cfg,
                                    const char **errp) {
#if defined(NEVERC_TLS_ENABLE_EXPERIMENTAL_TRANSPORT)
    if (!addr || !cfg) {
        if (errp)
            *errp = k_tls_invalid_argument;
        return NULL;
    }
    const char *tcp_error = NULL;
    neverc_tcp_conn_t *tcp = neverc_tcp_dial(addr, &tcp_error);
    if (!tcp) {
        if (errp)
            *errp = tcp_error ? tcp_error : k_tls_handshake_failed;
        return NULL;
    }
    neverc_tls_conn_t *conn =
        tls_start_handshake(tcp, cfg, 0, 1, errp);
    if (!conn)
        neverc_tcp_close(tcp);
    return conn;
#else
    (void)addr;
    (void)cfg;
    tls_set_unavailable(errp);
    return NULL;
#endif
}

neverc_tls_conn_t *neverc_tls_server(neverc_tcp_conn_t *tcp,
                                      neverc_tls_config_t *cfg,
                                      const char **errp) {
#if defined(NEVERC_TLS_ENABLE_EXPERIMENTAL_TRANSPORT)
    return tls_start_handshake(tcp, cfg, 1, 0, errp);
#else
    (void)tcp;
    (void)cfg;
    tls_set_unavailable(errp);
    return NULL;
#endif
}

neverc_tls_conn_t *neverc_tls_client(neverc_tcp_conn_t *tcp,
                                      neverc_tls_config_t *cfg,
                                      const char **errp) {
#if defined(NEVERC_TLS_ENABLE_EXPERIMENTAL_TRANSPORT)
    return tls_start_handshake(tcp, cfg, 0, 0, errp);
#else
    (void)tcp;
    (void)cfg;
    tls_set_unavailable(errp);
    return NULL;
#endif
}

static int tls_read_unlocked(
    neverc_tls_conn_t *conn, void *buf, size_t buflen) {
    if (!conn || !buf || buflen == 0)
        return -1;
    if (conn->closed)
        return -1;

    /* Return buffered data first */
    if (conn->decrypt_buf_len > conn->decrypt_buf_pos) {
        size_t avail = conn->decrypt_buf_len - conn->decrypt_buf_pos;
        size_t n = buflen < avail ? buflen : avail;
        memcpy(buf, conn->decrypt_buf + conn->decrypt_buf_pos, n);
        conn->decrypt_buf_pos += n;
        if (conn->decrypt_buf_pos >= conn->decrypt_buf_len) {
            conn->decrypt_buf_pos = 0;
            conn->decrypt_buf_len = 0;
        }
        return (int)n;
    }
    if (conn->peer_closed)
        return 0;

    for (;;) {
        uint8_t inner_type;
        uint8_t data[TLS_MAX_PLAINTEXT];
        size_t data_len;

        if (tls_recv_decrypt(conn, &inner_type, data, &data_len) != 0)
            return -1;

        if (conn->post_handshake_len > 0 &&
            inner_type != TLS_CT_HANDSHAKE)
            return tls_protocol_error(
                conn, TLS_ALERT_UNEXPECTED_MESSAGE,
                "TLS handshake messages were interleaved");

        if (inner_type == TLS_CT_ALERT) {
            if (++conn->non_advancing_records >
                TLS_MAX_NON_ADVANCING_RECORDS)
                return tls_protocol_error(
                    conn, TLS_ALERT_UNEXPECTED_MESSAGE,
                    "too many non-advancing TLS records");
            int alert_result =
                tls_handle_peer_alert(conn, data, data_len);
            if (alert_result > 0)
                return 0;
            if (alert_result < 0)
                return -1;
            continue;
        }

        if (inner_type == TLS_CT_HANDSHAKE) {
            if (tls_handle_post_handshake(
                    conn, data, data_len) != 0)
                return -1;
            continue;
        }

        if (inner_type != TLS_CT_APPLICATION_DATA)
            return tls_protocol_error(
                conn, TLS_ALERT_UNEXPECTED_MESSAGE,
                "received an unexpected TLS record");
        if (data_len == 0) {
            if (++conn->non_advancing_records >
                TLS_MAX_NON_ADVANCING_RECORDS)
                return tls_protocol_error(
                    conn, TLS_ALERT_UNEXPECTED_MESSAGE,
                    "too many non-advancing TLS records");
            continue;
        }
        conn->non_advancing_records = 0;

        size_t n = buflen < data_len ? buflen : data_len;
        memcpy(buf, data, n);

        if (data_len > n) {
            size_t rem = data_len - n;
            memcpy(conn->decrypt_buf, data + n, rem);
            conn->decrypt_buf_len = rem;
            conn->decrypt_buf_pos = 0;
        }

        return (int)n;
    }
}

int neverc_tls_read(
    neverc_tls_conn_t *conn, void *buf, size_t buflen) {
    if (!conn || !conn->mutexes_initialized)
        return -1;
    tls_mutex_lock(&conn->read_mutex);
    int result = tls_read_unlocked(conn, buf, buflen);
    tls_mutex_unlock(&conn->read_mutex);
    return result;
}

int neverc_tls_write(neverc_tls_conn_t *conn, const void *data, size_t len) {
    if (!conn || !conn->mutexes_initialized ||
        !data || len == 0 ||
        len > (size_t)INT_MAX)
        return -1;
    tls_mutex_lock(&conn->write_mutex);
    if (conn->closed || conn->write_closed ||
        !conn->handshake_done || !conn->application_keys_active) {
        tls_mutex_unlock(&conn->write_mutex);
        return -1;
    }

    const uint8_t *p = (const uint8_t *)data;
    size_t remaining = len;

    while (remaining > 0) {
        size_t chunk = remaining < TLS_MAX_PLAINTEXT
                     ? remaining : TLS_MAX_PLAINTEXT;
        if (tls_send_encrypted_unlocked(
                conn, TLS_CT_APPLICATION_DATA, p, chunk) != 0) {
            conn->closed = 1;
            tls_mutex_unlock(&conn->write_mutex);
            return -1;
        }
        p += chunk;
        remaining -= chunk;
    }

    tls_mutex_unlock(&conn->write_mutex);
    return (int)len;
}

int neverc_tls_key_update(
    neverc_tls_conn_t *conn, int request_peer_update) {
    return tls_send_key_update_message(
        conn, request_peer_update);
}

void neverc_tls_close(neverc_tls_conn_t *conn) {
    if (!conn) return;
    neverc_tls_config_t *config = conn->config;

    if (conn->tcp && conn->handshake_done && !conn->closed &&
        !conn->write_closed && !conn->alert_sent)
        (void)tls_send_close_notify(conn);

    if (conn->tcp && conn->owns_tcp)
        neverc_tcp_close(conn->tcp);
    free(conn->alpn);
    free(conn->server_name);
    free(conn->resumption_alpn);
    free(conn->peer_cert);
    neverc_x509_cert_pool_free(conn->peer_intermediates);
    tls_clear_handshake_buffer(conn);
    if (conn->post_handshake_buf) {
        neverc_platform_secure_zero(
            conn->post_handshake_buf,
            conn->post_handshake_cap);
        free(conn->post_handshake_buf);
    }
    if (conn->mutexes_initialized) {
        tls_mutex_destroy(&conn->read_mutex);
        tls_mutex_destroy(&conn->write_mutex);
        conn->mutexes_initialized = 0;
    }
    neverc_platform_secure_zero(conn, sizeof(*conn));
    free(conn);
    neverc_tls_config_free(config);
}

const char *neverc_tls_alpn(neverc_tls_conn_t *conn) {
    return conn ? conn->alpn : NULL;
}

const char *neverc_tls_server_name(neverc_tls_conn_t *conn) {
    return conn ? conn->server_name : NULL;
}

uint16_t neverc_tls_cipher_suite(neverc_tls_conn_t *conn) {
    return conn ? conn->cipher_suite : 0;
}

const uint8_t *neverc_tls_peer_certificate(neverc_tls_conn_t *conn,
                                            size_t *out_len) {
    if (!conn || !conn->peer_cert) {
        if (out_len) *out_len = 0;
        return NULL;
    }
    if (out_len) *out_len = conn->peer_cert_len;
    return conn->peer_cert;
}

/* --- TLS Listener --- */

neverc_tls_listener_t *neverc_tls_listen(const char *addr,
                                          neverc_tls_config_t *cfg,
                                          const char **errp) {
#if defined(NEVERC_TLS_ENABLE_EXPERIMENTAL_TRANSPORT)
    if (!addr || !cfg || !cfg->cert_der || !cfg->key_der) {
        if (errp)
            *errp = k_tls_invalid_argument;
        return NULL;
    }
    neverc_tcp_listener_t *tcp_listener =
        neverc_tcp_listen(addr, errp);
    if (!tcp_listener)
        return NULL;
    neverc_tls_listener_t *listener =
        (neverc_tls_listener_t *)calloc(1, sizeof(*listener));
    if (!listener) {
        neverc_tcp_listener_close(tcp_listener);
        if (errp)
            *errp = k_tls_allocation_failed;
        return NULL;
    }
    listener->tcp_ln = tcp_listener;
    tls_config_retain(cfg);
    listener->cfg = cfg;
    if (errp)
        *errp = NULL;
    return listener;
#else
    (void)addr;
    (void)cfg;
    tls_set_unavailable(errp);
    return NULL;
#endif
}

neverc_tls_conn_t *neverc_tls_accept(neverc_tls_listener_t *ln,
                                      const char **errp) {
#if defined(NEVERC_TLS_ENABLE_EXPERIMENTAL_TRANSPORT)
    if (!ln || !ln->tcp_ln || !ln->cfg) {
        if (errp)
            *errp = k_tls_invalid_argument;
        return NULL;
    }
    neverc_tcp_conn_t *tcp =
        neverc_tcp_accept(ln->tcp_ln, errp);
    if (!tcp)
        return NULL;
    neverc_tls_conn_t *conn =
        tls_start_handshake(tcp, ln->cfg, 1, 1, errp);
    if (!conn)
        neverc_tcp_close(tcp);
    return conn;
#else
    (void)ln;
    tls_set_unavailable(errp);
    return NULL;
#endif
}

void neverc_tls_listener_close(neverc_tls_listener_t *ln) {
    if (!ln) return;
    neverc_tls_config_t *config = ln->cfg;
    neverc_tcp_listener_close(ln->tcp_ln);
    free(ln);
    neverc_tls_config_free(config);
}
