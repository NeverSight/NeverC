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
#include "tls_key.h"
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

/* ======================================================================
 * TLS 1.3 Constants (RFC 8446)
 * ====================================================================== */

#define TLS_RECORD_HEADER_SIZE   5
#define TLS_MAX_PLAINTEXT        16384
#define TLS_MAX_CIPHERTEXT       (TLS_MAX_PLAINTEXT + 256)
#define TLS_HASH_SIZE_SHA256     32
#define TLS_AEAD_TAG_SIZE        16

/* Content types */
#define TLS_CT_CHANGE_CIPHER_SPEC  20
#define TLS_CT_ALERT               21
#define TLS_CT_HANDSHAKE           22
#define TLS_CT_APPLICATION_DATA    23

/* Handshake types */
#define TLS_HS_CLIENT_HELLO        1
#define TLS_HS_SERVER_HELLO        2
#define TLS_HS_NEW_SESSION_TICKET  4
#define TLS_HS_ENCRYPTED_EXT       8
#define TLS_HS_CERTIFICATE        11
#define TLS_HS_CERTIFICATE_REQUEST 13
#define TLS_HS_CERT_VERIFY        15
#define TLS_HS_FINISHED           20

/* Extension types */
#define TLS_EXT_SERVER_NAME        0
#define TLS_EXT_SUPPORTED_GROUPS  10
#define TLS_EXT_SIGNATURE_ALGORITHMS 13
#define TLS_EXT_ALPN              16
#define TLS_EXT_SUPPORTED_VERSIONS 43
#define TLS_EXT_KEY_SHARE         51

/* Signature algorithms */
#define TLS_SIG_ECDSA_SHA256 \
    NEVERC_TLS_SIGNATURE_ECDSA_SECP256R1_SHA256
#define TLS_SIG_RSA_PSS_SHA256 \
    NEVERC_TLS_SIGNATURE_RSA_PSS_RSAE_SHA256
#define TLS_SIG_RSA_PSS_SHA384 \
    NEVERC_TLS_SIGNATURE_RSA_PSS_RSAE_SHA384
#define TLS_SIG_RSA_PSS_SHA512 \
    NEVERC_TLS_SIGNATURE_RSA_PSS_RSAE_SHA512
#define TLS_SIG_ED25519 NEVERC_TLS_SIGNATURE_ED25519

/* Alert descriptions */
#define TLS_ALERT_CLOSE_NOTIFY         0
#define TLS_ALERT_UNEXPECTED_MESSAGE  10
#define TLS_ALERT_BAD_RECORD_MAC      20
#define TLS_ALERT_HANDSHAKE_FAILURE   40
#define TLS_ALERT_BAD_CERTIFICATE     42
#define TLS_ALERT_ILLEGAL_PARAMETER   47
#define TLS_ALERT_DECODE_ERROR        50
#define TLS_ALERT_DECRYPT_ERROR       51
#define TLS_ALERT_PROTOCOL_VERSION    70
#define TLS_ALERT_INTERNAL_ERROR      80
#define TLS_ALERT_MISSING_EXTENSION  109
#define TLS_ALERT_UNSUPPORTED_EXTENSION 110

/* Legacy version for record layer */
#define TLS_LEGACY_VERSION  0x0303 /* TLS 1.2 in record header */

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

struct neverc_tls_config {
    uint8_t *cert_der;
    size_t   cert_der_len;
    uint8_t *key_der;
    size_t   key_der_len;
    int      key_type; /* 0=unknown, 1=RSA, 2=ECDSA, 3=Ed25519 */
    char    *server_name;
    char   **alpn_protos;
    int      alpn_count;
    int      skip_verify;
    neverc_x509_cert_pool_t *root_certificates;
};

struct neverc_tls_conn {
    neverc_tcp_conn_t  *tcp;
    int                 owns_tcp; /* if we created the TCP conn */
    tls_traffic_keys_t  read_keys;
    tls_traffic_keys_t  write_keys;
    int                 handshake_done;
    uint16_t            cipher_suite;
    char               *alpn;
    uint8_t            *peer_cert;
    size_t              peer_cert_len;
    neverc_x509_cert_pool_t *peer_intermediates;
    uint8_t             read_buf[TLS_MAX_CIPHERTEXT + TLS_RECORD_HEADER_SIZE];
    size_t              read_buf_len;
    uint8_t             decrypt_buf[TLS_MAX_PLAINTEXT + 1];
    size_t              decrypt_buf_len;
    size_t              decrypt_buf_pos;
    int                 write_keys_active;
    int                 alert_sent;
    const char         *failure_reason;
    int                 closed;
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

neverc_tls_config_t *neverc_tls_config_new(void) {
    neverc_tls_config_t *cfg = (neverc_tls_config_t *)calloc(1, sizeof(*cfg));
    return cfg;
}

void neverc_tls_config_free(neverc_tls_config_t *cfg) {
    if (!cfg) return;
    free(cfg->cert_der);
    tls_free_private_key(cfg->key_der, cfg->key_der_len);
    free(cfg->server_name);
    neverc_x509_cert_pool_free(cfg->root_certificates);
    for (int i = 0; i < cfg->alpn_count; i++)
        free(cfg->alpn_protos[i]);
    free(cfg->alpn_protos);
    free(cfg);
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
    for (int i = 0; i < cfg->alpn_count; i++)
        free(cfg->alpn_protos[i]);
    free(cfg->alpn_protos);

    cfg->alpn_count = count;
    cfg->alpn_protos = (char **)calloc((size_t)count, sizeof(char *));
    for (int i = 0; i < count; i++)
        cfg->alpn_protos[i] = strdup(protocols[i]);
}

void neverc_tls_config_insecure_skip_verify(neverc_tls_config_t *cfg) {
    if (cfg) cfg->skip_verify = 1;
}

void neverc_tls_config_set_server_name(neverc_tls_config_t *cfg,
                                        const char *name) {
    if (!cfg) return;
    free(cfg->server_name);
    cfg->server_name = name ? strdup(name) : NULL;
}

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

int neverc_tls_verify_server_certificate_chain(
    const neverc_tls_config_t *config,
    const uint8_t *leaf_der,
    size_t leaf_der_len,
    const neverc_x509_cert_pool_t *intermediates,
    const neverc_x509_time_t *moment) {
    if (!config || !config->server_name ||
        config->server_name[0] == '\0' ||
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
    if (!roots) {
        owned_roots = neverc_x509_system_cert_pool();
        roots = owned_roots;
    }

    int result = roots ? neverc_x509_verify_with_pools(
        &leaf, intermediates, roots, moment,
        config->server_name,
        NEVERC_X509_EXT_KEY_USAGE_SERVER_AUTH) : -1;
    neverc_x509_cert_pool_free(owned_roots);
    neverc_x509_cert_free(&leaf);
    return result;
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
    /* Build HkdfLabel: u16(length) + u8("tls13 "+label len) + "tls13 "+label + u8(context len) + context */
    uint8_t info[512];
    if (!secret || secret_len != TLS_HASH_SIZE_SHA256 ||
        (!label && label_len != 0) || label_len > 249 ||
        (!context && context_len != 0) || context_len > 255 ||
        !out || out_len > 65535 ||
        2 + 1 + 6 + label_len + 1 + context_len > sizeof(info))
        return -1;
    size_t pos = 0;
    size_t full_label_len = 6 + label_len; /* "tls13 " prefix */

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

static void derive_secret(const uint8_t *secret,
                            const char *label, size_t label_len,
                            const uint8_t *transcript_hash,
                            uint8_t *out) {
    hkdf_expand_label(secret, TLS_HASH_SIZE_SHA256,
                       label, label_len,
                       transcript_hash, TLS_HASH_SIZE_SHA256,
                       out, TLS_HASH_SIZE_SHA256);
}

/* RFC 8446 §7.1: unavailable secrets are Hash.length zero bytes, not an
 * empty IKM. Early Secret = HKDF-Extract(0s, 0s) and Master Secret =
 * HKDF-Extract(Derive-Secret(..., "derived", ""), 0s). */
static int tls_hkdf_extract_zero_ikm(
    uint8_t out[TLS_HASH_SIZE_SHA256],
    const uint8_t *salt, size_t salt_len) {
    uint8_t zeros[TLS_HASH_SIZE_SHA256];
    memset(zeros, 0, sizeof(zeros));
    return neverc_hkdf_extract_sha256(
        out, salt, salt_len, zeros, sizeof(zeros));
}

static void derive_traffic_keys(const uint8_t *traffic_secret,
                                  tls_traffic_keys_t *keys,
                                  tls_cipher_id_t cipher) {
    keys->id = cipher;
    keys->seq = 0;

    size_t key_len = (cipher == TLS_CIPHER_AES_128_GCM_SHA256) ? 16 : 32;
    hkdf_expand_label(traffic_secret, TLS_HASH_SIZE_SHA256,
                       "key", 3, NULL, 0, keys->key, key_len);
    hkdf_expand_label(traffic_secret, TLS_HASH_SIZE_SHA256,
                       "iv", 2, NULL, 0, keys->iv, 12);

    if (cipher == TLS_CIPHER_AES_128_GCM_SHA256)
        neverc_gcm_init(&keys->gcm, keys->key, 16);
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

static int tls_send_encrypted(neverc_tls_conn_t *conn,
                                uint8_t inner_type,
                                const uint8_t *data, size_t len) {
    if (!conn || !conn->tcp || (!data && len != 0) ||
        len > TLS_MAX_PLAINTEXT)
        return -1;
    tls_traffic_keys_t *keys = &conn->write_keys;

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

static int tls_send_alert(
    neverc_tls_conn_t *conn, uint8_t description) {
    if (!conn || !conn->tcp || conn->alert_sent)
        return -1;
    uint8_t alert[2] = {2, description};
    int result = conn->write_keys_active ?
        tls_send_encrypted(conn, TLS_CT_ALERT, alert, sizeof(alert)) :
        tls_send_record(conn->tcp, TLS_CT_ALERT, alert, sizeof(alert));
    if (result == 0)
        conn->alert_sent = 1;
    return result;
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
    /* Read from TCP until we have a complete record */
    while (conn->read_buf_len < TLS_RECORD_HEADER_SIZE) {
        int n = neverc_tcp_read(conn->tcp,
                                 conn->read_buf + conn->read_buf_len,
                                 sizeof(conn->read_buf) - conn->read_buf_len);
        if (n <= 0) return -1;
        conn->read_buf_len += (size_t)n;
    }

    *out_type = conn->read_buf[0];
    uint16_t rec_len = get_u16(conn->read_buf + 3);
    if (rec_len > TLS_MAX_CIPHERTEXT) return -1;

    size_t total = TLS_RECORD_HEADER_SIZE + rec_len;
    while (conn->read_buf_len < total) {
        int n = neverc_tcp_read(conn->tcp,
                                 conn->read_buf + conn->read_buf_len,
                                 sizeof(conn->read_buf) - conn->read_buf_len);
        if (n <= 0) return -1;
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
            return -1;
    }

    if (rec_type != TLS_CT_APPLICATION_DATA) {
        *out_inner_type = rec_type;
        memcpy(out_data, rec_data, rec_len);
        *out_len = rec_len;
        return 0;
    }

    tls_traffic_keys_t *keys = &conn->read_keys;

    uint8_t nonce[12];
    memcpy(nonce, keys->iv, 12);
    for (int i = 0; i < 8; i++)
        nonce[12 - 1 - i] ^= (uint8_t)(keys->seq >> (i * 8));

    /* Reconstruct AAD (record header) */
    uint8_t aad[5];
    aad[0] = TLS_CT_APPLICATION_DATA;
    put_u16(aad + 1, TLS_LEGACY_VERSION);
    put_u16(aad + 3, (uint16_t)rec_len);

    if (rec_len < TLS_AEAD_TAG_SIZE) return -1;

    size_t ct_body_len = rec_len - TLS_AEAD_TAG_SIZE;
    uint8_t *plaintext = (uint8_t *)malloc(ct_body_len);
    if (!plaintext) return -1;

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
        return -1;
    }

    keys->seq++;

    /* Remove padding and find inner content type (last non-zero byte) */
    while (ct_body_len > 0 && plaintext[ct_body_len - 1] == 0)
        ct_body_len--;
    if (ct_body_len == 0) { free(plaintext); return -1; }

    *out_inner_type = plaintext[ct_body_len - 1];
    ct_body_len--;
    memcpy(out_data, plaintext, ct_body_len);
    *out_len = ct_body_len;

    free(plaintext);
    return 0;
}

/* ======================================================================
 * TLS 1.3 Handshake — Client
 * ====================================================================== */

static neverc_tls_conn_t *tls_conn_new(neverc_tcp_conn_t *tcp, int owns) {
    neverc_tls_conn_t *conn = (neverc_tls_conn_t *)calloc(1, sizeof(*conn));
    if (!conn) return NULL;
    conn->tcp = tcp;
    conn->owns_tcp = owns;
    return conn;
}

typedef struct {
    uint8_t server_public_key[32];
    uint16_t selected_cipher;
    tls_cipher_id_t cipher_id;
} tls_server_hello_info_t;

static int tls_parse_server_hello(
    const uint8_t *message, size_t message_len,
    const uint8_t *expected_session_id,
    size_t expected_session_id_len,
    tls_server_hello_info_t *result, uint8_t *alert);

static int tls_store_server_certificate(neverc_tls_conn_t *conn,
                                        const uint8_t *message,
                                        size_t message_len) {
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
    if (certificate_list_len == 0 ||
        certificate_list_len != message_len - pos)
        return -1;

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

    /* Build ClientHello */
    uint8_t ch[512];
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
            session_id, sizeof(session_id)) != 0)
        return tls_fail(conn, TLS_ALERT_INTERNAL_ERROR);
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
        put_u16(ch + ch_len, TLS_EXT_SERVER_NAME); ch_len += 2;
        put_u16(ch + ch_len, (uint16_t)(sni_len + 5)); ch_len += 2;
        put_u16(ch + ch_len, (uint16_t)(sni_len + 3)); ch_len += 2;
        ch[ch_len++] = 0; /* host_name type */
        put_u16(ch + ch_len, (uint16_t)sni_len); ch_len += 2;
        memcpy(ch + ch_len, cfg->server_name, sni_len); ch_len += sni_len;
    }

    /* Fill extensions length */
    put_u16(ch + ext_len_pos, (uint16_t)(ch_len - ext_start));

    /* Fill handshake header */
    ch[hs_hdr_pos] = TLS_HS_CLIENT_HELLO;
    put_u24(ch + hs_hdr_pos + 1, (uint32_t)(ch_len - 4));

    /* Hash ClientHello into transcript */
    neverc_sha256_update(&transcript, ch, ch_len);

    /* Send ClientHello record */
    if (tls_send_record(conn->tcp, TLS_CT_HANDSHAKE, ch, ch_len) != 0)
        return -1;

    /* Receive ServerHello */
    uint8_t rec_type;
    uint8_t rec_data[TLS_MAX_CIPHERTEXT];
    size_t rec_len;
    if (tls_recv_record(conn, &rec_type, rec_data, &rec_len) != 0)
        return -1;
    if (rec_type != TLS_CT_HANDSHAKE)
        return tls_fail(conn, TLS_ALERT_UNEXPECTED_MESSAGE);
    if (rec_len > 0 && rec_data[0] != TLS_HS_SERVER_HELLO)
        return tls_fail(conn, TLS_ALERT_UNEXPECTED_MESSAGE);

    tls_server_hello_info_t server_hello;
    uint8_t server_hello_alert = TLS_ALERT_DECODE_ERROR;
    if (tls_parse_server_hello(
            rec_data, rec_len, session_id, sizeof(session_id),
            &server_hello, &server_hello_alert) != 0)
        return tls_fail(conn, server_hello_alert);

    /* Hash ServerHello into transcript */
    neverc_sha256_update(&transcript, rec_data, rec_len);

    uint16_t selected_cipher =
        server_hello.selected_cipher;
    tls_cipher_id_t cipher_id =
        server_hello.cipher_id;
    conn->cipher_suite = selected_cipher;

    uint8_t server_pubkey[32];
    memcpy(server_pubkey, server_hello.server_public_key,
           sizeof(server_pubkey));

    /* Compute shared secret via X25519 ECDH */
    uint8_t shared_secret[32];
    if (neverc_ecdh_compute(&ecdh_key, server_pubkey, 32,
                             shared_secret, 32) < 0)
        return -1;

    /* Derive handshake secrets */
    uint8_t early_secret[32];
    if (tls_hkdf_extract_zero_ikm(early_secret, NULL, 0) != 0)
        return -1;

    uint8_t derived_secret[32];
    uint8_t empty_hash[32];
    {
        neverc_sha256_ctx h;
        neverc_sha256_init(&h);
        neverc_sha256_final(&h, empty_hash);
    }
    derive_secret(early_secret, "derived", 7, empty_hash, derived_secret);

    uint8_t handshake_secret[32];
    neverc_hkdf_extract_sha256(handshake_secret, derived_secret, 32,
                                shared_secret, 32);

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
    tls_debug_hex("EARLY_SECRET",
                  early_secret, sizeof(early_secret));
    tls_debug_hex("DERIVED_SECRET",
                  derived_secret, sizeof(derived_secret));
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
    uint8_t expected_handshake_type = TLS_HS_ENCRYPTED_EXT;
    while (!got_finished) {
        uint8_t inner_type;
        uint8_t hs_data[TLS_MAX_PLAINTEXT];
        size_t hs_len;
        if (tls_recv_decrypt(conn, &inner_type, hs_data, &hs_len) != 0)
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
        if (hs_len == 0)
            return tls_protocol_error(
                conn, TLS_ALERT_DECODE_ERROR,
                "server sent an empty handshake record");

        /* Process handshake messages in the decrypted data */
        size_t hpos = 0;
        while (hpos + 4 <= hs_len) {
            uint8_t hs_type = hs_data[hpos];
            uint32_t msg_len = get_u24(hs_data + hpos + 1);
            if ((size_t)msg_len > hs_len - hpos - 4)
                return tls_protocol_error(
                    conn, TLS_ALERT_DECODE_ERROR,
                    "fragmented or malformed server handshake message");
            if (hs_type != expected_handshake_type)
                return tls_protocol_error(
                    conn, TLS_ALERT_UNEXPECTED_MESSAGE,
                    "server handshake message arrived out of order");

            const uint8_t *message = hs_data + hpos + 4;
            if (hs_type == TLS_HS_ENCRYPTED_EXT) {
                if (msg_len < 2 ||
                    get_u16(message) != msg_len - 2)
                    return tls_protocol_error(
                        conn, TLS_ALERT_DECODE_ERROR,
                        "malformed server EncryptedExtensions message");
                expected_handshake_type = TLS_HS_CERTIFICATE;
            } else if (hs_type == TLS_HS_CERTIFICATE) {
                if (tls_store_server_certificate(
                        conn, message, msg_len) != 0)
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
                    signature_len != (size_t)msg_len - 4)
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

            /* Hash all handshake messages into transcript
             * (for Finished verification) */
            if (hs_type != TLS_HS_FINISHED)
                neverc_sha256_update(&transcript, hs_data + hpos,
                                      4 + msg_len);

            if (hs_type == TLS_HS_FINISHED) {
                /* Verify server Finished */
                uint8_t transcript_hash[32];
                {
                    neverc_sha256_ctx copy = transcript;
                    neverc_sha256_final(&copy, transcript_hash);
                }

                uint8_t finished_key[32];
                hkdf_expand_label(server_hs_traffic_secret, 32,
                                   "finished", 8, NULL, 0,
                                   finished_key, 32);

                uint8_t expected_verify[32];
                neverc_hmac_sha256(finished_key, 32,
                                    transcript_hash, 32,
                                    expected_verify);

                if (msg_len != 32 ||
                    !neverc_subtle_constant_time_compare(
                        hs_data + hpos + 4, expected_verify, 32))
                    return tls_protocol_error(
                        conn, TLS_ALERT_DECRYPT_ERROR,
                        "server Finished validation failed");

                /* Hash Finished into transcript */
                neverc_sha256_update(&transcript, hs_data + hpos,
                                      4 + msg_len);
                got_finished = 1;
            }

            hpos += 4 + msg_len;
        }
        if (hpos != hs_len)
            return tls_protocol_error(
                conn, TLS_ALERT_DECODE_ERROR,
                "trailing fragmented server handshake bytes");
    }

    /* Application traffic secrets use the transcript through server Finished
     * only (RFC 8446 §7.1). Keep a snapshot before Client Finished. */
    uint8_t transcript_hash_server_finished[32];
    {
        neverc_sha256_ctx copy = transcript;
        neverc_sha256_final(&copy, transcript_hash_server_finished);
    }

    /* Send client Finished */
    {
        uint8_t transcript_hash[32];
        memcpy(transcript_hash, transcript_hash_server_finished, 32);

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

        if (tls_send_encrypted(conn, TLS_CT_HANDSHAKE, finished_msg, 36) != 0)
            return -1;
    }

    /* Derive application traffic keys */
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

    derive_traffic_keys(server_app_secret, &conn->read_keys, cipher_id);
    derive_traffic_keys(client_app_secret, &conn->write_keys, cipher_id);

    conn->handshake_done = 1;
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
    uint8_t session_id[32];
    size_t session_id_len;
    uint8_t client_public_key[32];
    uint16_t selected_cipher;
    tls_cipher_id_t cipher_id;
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

static int tls_server_handshake(neverc_tls_conn_t *conn,
                                  neverc_tls_config_t *cfg) {
    if (!cfg || !cfg->cert_der || !cfg->key_der) return -1;

    neverc_sha256_ctx transcript;
    neverc_sha256_init(&transcript);

    /* Receive ClientHello */
    uint8_t rec_type;
    uint8_t rec_data[TLS_MAX_CIPHERTEXT];
    size_t rec_len;
    if (tls_recv_record(conn, &rec_type, rec_data, &rec_len) != 0)
        return -1;
    if (rec_type != TLS_CT_HANDSHAKE)
        return tls_fail(conn, TLS_ALERT_UNEXPECTED_MESSAGE);

    tls_client_hello_info_t client_hello;
    uint8_t client_hello_alert = TLS_ALERT_DECODE_ERROR;
    if (tls_parse_client_hello(
            rec_data, rec_len, &client_hello,
            &client_hello_alert) != 0)
        return tls_fail(conn, client_hello_alert);

    neverc_sha256_update(&transcript, rec_data, rec_len);

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

    put_u16(sh + sh_ext_len_pos, (uint16_t)(sh_len - sh_ext_start));
    put_u24(sh + sh_body_pos, (uint32_t)(sh_len - sh_body_pos - 3));

    neverc_sha256_update(&transcript, sh, sh_len);

    /* Send ServerHello */
    if (tls_send_record(conn->tcp, TLS_CT_HANDSHAKE, sh, sh_len) != 0)
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
    uint8_t early_secret[32];
    if (tls_hkdf_extract_zero_ikm(early_secret, NULL, 0) != 0)
        return -1;

    uint8_t empty_hash[32];
    {
        neverc_sha256_ctx h;
        neverc_sha256_init(&h);
        neverc_sha256_final(&h, empty_hash);
    }

    uint8_t derived_secret[32];
    derive_secret(early_secret, "derived", 7, empty_hash, derived_secret);

    uint8_t handshake_secret[32];
    neverc_hkdf_extract_sha256(handshake_secret, derived_secret, 32,
                                shared_secret, 32);

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
    /* 1. EncryptedExtensions (empty) */
    {
        uint8_t ee[6];
        ee[0] = TLS_HS_ENCRYPTED_EXT;
        put_u24(ee + 1, 2);
        put_u16(ee + 4, 0); /* no extensions */
        neverc_sha256_update(&transcript, ee, 6);
        if (tls_send_encrypted(conn, TLS_CT_HANDSHAKE, ee, 6) != 0)
            return -1;
    }

    /* 2. Certificate */
    {
        size_t cert_msg_len = 4 + 1 + 3 + 3 + cfg->cert_der_len + 2;
        uint8_t *cert_msg = (uint8_t *)calloc(1, cert_msg_len);
        if (!cert_msg) return -1;
        size_t cp = 0;
        cert_msg[cp++] = TLS_HS_CERTIFICATE;
        put_u24(cert_msg + cp, (uint32_t)(cert_msg_len - 4)); cp += 3;
        cert_msg[cp++] = 0; /* request_context empty */
        put_u24(cert_msg + cp, (uint32_t)(3 + cfg->cert_der_len + 2)); cp += 3;
        put_u24(cert_msg + cp, (uint32_t)cfg->cert_der_len); cp += 3;
        memcpy(cert_msg + cp, cfg->cert_der, cfg->cert_der_len);
        cp += cfg->cert_der_len;
        put_u16(cert_msg + cp, 0); /* no cert extensions */

        neverc_sha256_update(&transcript, cert_msg, cert_msg_len);
        int rc = tls_send_encrypted(conn, TLS_CT_HANDSHAKE,
                                      cert_msg, cert_msg_len);
        free(cert_msg);
        if (rc != 0) return -1;
    }

    /* 3. CertificateVerify (ECDSA signature over transcript) */
    {
        uint8_t transcript_hash[32];
        {
            neverc_sha256_ctx copy = transcript;
            neverc_sha256_final(&copy, transcript_hash);
        }

        uint8_t cv[256];
        cv[0] = TLS_HS_CERT_VERIFY;
        uint16_t signature_scheme = 0;
        size_t sig_len = 0;
        if (neverc_tls_sign_certificate_verify(
                cfg, 1, transcript_hash, sizeof(transcript_hash),
                &signature_scheme, cv + 8, sizeof(cv) - 8,
                &sig_len) != 0)
            return -1;

        put_u24(cv + 1, (uint32_t)(2 + 2 + sig_len));
        put_u16(cv + 4, signature_scheme);
        put_u16(cv + 6, (uint16_t)sig_len);
        size_t cv_len = 8 + sig_len;

        neverc_sha256_update(&transcript, cv, cv_len);
        if (tls_send_encrypted(conn, TLS_CT_HANDSHAKE, cv, cv_len) != 0)
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
        if (tls_send_encrypted(conn, TLS_CT_HANDSHAKE, finished_msg, 36) != 0)
            return -1;
    }

    /* Snapshot transcript through server Finished for application secrets. */
    uint8_t transcript_hash_server_finished[32];
    {
        neverc_sha256_ctx copy = transcript;
        neverc_sha256_final(&copy, transcript_hash_server_finished);
    }

    /* Receive client Finished */
    {
        uint8_t inner_type;
        uint8_t hs_data[TLS_MAX_PLAINTEXT];
        size_t hs_len;
        if (tls_recv_decrypt(conn, &inner_type, hs_data, &hs_len) != 0)
            return -1;
        if (inner_type != TLS_CT_HANDSHAKE)
            return tls_fail(conn, TLS_ALERT_UNEXPECTED_MESSAGE);
        if (hs_len == 0)
            return tls_fail(conn, TLS_ALERT_DECODE_ERROR);
        if (hs_data[0] != TLS_HS_FINISHED)
            return tls_fail(conn, TLS_ALERT_UNEXPECTED_MESSAGE);
        if (hs_len != 36 || get_u24(hs_data + 1) != 32)
            return tls_fail(conn, TLS_ALERT_DECODE_ERROR);

        uint8_t transcript_hash[32];
        memcpy(transcript_hash, transcript_hash_server_finished, 32);

        uint8_t finished_key[32];
        hkdf_expand_label(client_hs_traffic_secret, 32,
                           "finished", 8, NULL, 0, finished_key, 32);

        uint8_t expected[32];
        neverc_hmac_sha256(finished_key, 32,
                            transcript_hash, 32,
                            expected);

        if (!neverc_subtle_constant_time_compare(
                hs_data + 4, expected, 32))
            return tls_fail(conn, TLS_ALERT_DECRYPT_ERROR);

        neverc_sha256_update(&transcript, hs_data, hs_len);
    }

    /* Derive application traffic keys */
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

    derive_traffic_keys(client_app_secret, &conn->read_keys, cipher_id);
    derive_traffic_keys(server_app_secret, &conn->write_keys, cipher_id);

    conn->handshake_done = 1;
    return 0;
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

int neverc_tls_read(neverc_tls_conn_t *conn, void *buf, size_t buflen) {
    if (!conn || conn->closed || !buf || buflen == 0) return -1;

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

    for (;;) {
        uint8_t inner_type;
        uint8_t data[TLS_MAX_PLAINTEXT];
        size_t data_len;

        if (tls_recv_decrypt(conn, &inner_type, data, &data_len) != 0)
            return -1;

        if (inner_type == TLS_CT_ALERT) {
            if (data_len >= 2 && data[1] == TLS_ALERT_CLOSE_NOTIFY) {
                conn->closed = 1;
                return 0;
            }
            return -1;
        }

        /* Ignore post-handshake messages such as NewSessionTicket until
         * KeyUpdate/ticket handling is implemented. */
        if (inner_type == TLS_CT_HANDSHAKE)
            continue;

        if (inner_type != TLS_CT_APPLICATION_DATA)
            return -1;

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

int neverc_tls_write(neverc_tls_conn_t *conn, const void *data, size_t len) {
    if (!conn || conn->closed || !data || len == 0 ||
        len > (size_t)INT_MAX)
        return -1;

    const uint8_t *p = (const uint8_t *)data;
    size_t remaining = len;

    while (remaining > 0) {
        size_t chunk = remaining < TLS_MAX_PLAINTEXT
                     ? remaining : TLS_MAX_PLAINTEXT;
        if (tls_send_encrypted(conn, TLS_CT_APPLICATION_DATA, p, chunk) != 0)
            return -1;
        p += chunk;
        remaining -= chunk;
    }

    return (int)len;
}

void neverc_tls_close(neverc_tls_conn_t *conn) {
    if (!conn) return;

    if (conn->tcp && conn->handshake_done && !conn->closed) {
        uint8_t alert[2] = {1, TLS_ALERT_CLOSE_NOTIFY};
        tls_send_encrypted(conn, TLS_CT_ALERT, alert, 2);
    }

    if (conn->tcp && conn->owns_tcp)
        neverc_tcp_close(conn->tcp);
    free(conn->alpn);
    free(conn->peer_cert);
    neverc_x509_cert_pool_free(conn->peer_intermediates);
    free(conn);
}

const char *neverc_tls_alpn(neverc_tls_conn_t *conn) {
    return conn ? conn->alpn : NULL;
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
    neverc_tcp_listener_close(ln->tcp_ln);
    free(ln);
}
