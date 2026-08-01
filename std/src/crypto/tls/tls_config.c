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

#include "neverc/std/crypto/sha256.h"
#include "neverc/std/crypto/x509.h"
#include "neverc/std/encoding/pem.h"

static void tls_free_private_key(uint8_t *key_der, size_t key_der_len) {
    if (!key_der)
        return;
    neverc_platform_secure_zero(key_der, key_der_len);
    free(key_der);
}

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

void nci_tls_config_retain(neverc_tls_config_t *cfg) {
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

uint64_t nci_tls_wall_time_ms(void) {
    time_t now = time(NULL);
    if (now < 0 ||
        (uint64_t)now > UINT64_MAX / 1000u)
        return 0;
    return (uint64_t)now * 1000u;
}

void nci_tls_config_invalidate_client_session(
    neverc_tls_config_t *cfg) {
    if (!cfg || !cfg->session_mutex_initialized)
        return;
    tls_mutex_lock(&cfg->session_mutex);
    tls_clear_client_session(&cfg->client_session);
    tls_mutex_unlock(&cfg->session_mutex);
}

void nci_tls_config_invalidate_all_sessions(
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

int nci_tls_load_client_psk_offer(
    neverc_tls_config_t *cfg, tls_client_psk_offer_t *offer) {
    if (!offer)
        return 0;
    memset(offer, 0, sizeof(*offer));
    if (!cfg || !cfg->server_name ||
        !cfg->session_mutex_initialized)
        return 0;
    uint64_t now_ms = nci_tls_wall_time_ms();
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

void nci_tls_clear_client_psk_offer(
    tls_client_psk_offer_t *offer) {
    if (!offer)
        return;
    free(offer->alpn);
    free(offer->peer_cert);
    neverc_platform_secure_zero(offer, sizeof(*offer));
}

int nci_tls_store_client_session(
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
    replacement.received_at_ms = nci_tls_wall_time_ms();
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

void nci_tls_store_server_session(
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

int nci_tls_lookup_server_session(
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
    uint64_t now_ms = nci_tls_wall_time_ms();
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

    nci_tls_config_invalidate_all_sessions(cfg);
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
    nci_tls_config_invalidate_client_session(cfg);
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

    nci_tls_config_invalidate_all_sessions(cfg);
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
    nci_tls_config_invalidate_client_session(cfg);
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
    nci_tls_config_invalidate_client_session(cfg);
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
    nci_tls_config_invalidate_all_sessions(cfg);
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

int nci_tls_verify_certificate_chain(
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
    return nci_tls_verify_certificate_chain(
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
