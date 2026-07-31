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
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

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
#define TLS_HS_CERT_VERIFY        13
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
#define TLS_ALERT_DECODE_ERROR        50
#define TLS_ALERT_INTERNAL_ERROR      80

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
    uint8_t             read_buf[TLS_MAX_CIPHERTEXT + TLS_RECORD_HEADER_SIZE];
    size_t              read_buf_len;
    uint8_t             decrypt_buf[TLS_MAX_PLAINTEXT + 1];
    size_t              decrypt_buf_len;
    size_t              decrypt_buf_pos;
    int                 closed;
};

struct neverc_tls_listener {
    neverc_tcp_listener_t *tcp_ln;
    neverc_tls_config_t   *cfg;
};

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
    free(cfg->key_der);
    free(cfg->server_name);
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

    if (pem_decode_first(cert_pem, "CERTIFICATE",
                         &cert_der, &cert_der_len) != 0 ||
        pem_decode_first(key_pem, "EC PRIVATE KEY",
                         &key_der, &key_der_len) != 0 ||
        nci_tls_validate_certificate_key_pair(
            cert_der, cert_der_len, key_der, key_der_len,
            key_type) != 0) {
        free(cert_der);
        free(key_der);
        return -1;
    }

    free(cfg->cert_der);
    free(cfg->key_der);
    cfg->cert_der = cert_der;
    cfg->cert_der_len = cert_der_len;
    cfg->key_der = key_der;
    cfg->key_der_len = key_der_len;
    cfg->key_type = key_type;
    return 0;
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
    return neverc_tcp_write(tcp, data, len);
}

static int tls_send_record(neverc_tcp_conn_t *tcp, uint8_t content_type,
                            const uint8_t *data, size_t len) {
    uint8_t hdr[5];
    hdr[0] = content_type;
    put_u16(hdr + 1, TLS_LEGACY_VERSION);
    put_u16(hdr + 3, (uint16_t)len);
    if (tls_raw_write(tcp, hdr, 5) < 0) return -1;
    if (len > 0 && tls_raw_write(tcp, data, len) < 0) return -1;
    return 0;
}

static int tls_send_encrypted(neverc_tls_conn_t *conn,
                                uint8_t inner_type,
                                const uint8_t *data, size_t len) {
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
    if (tls_raw_write(conn->tcp, hdr, 5) < 0) rc = -1;
    if (rc == 0 && tls_raw_write(conn->tcp, ciphertext, ct_len) < 0) rc = -1;
    free(ciphertext);
    return rc;
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

    if (tls_recv_record(conn, &rec_type, rec_data, &rec_len) != 0)
        return -1;

    /* Skip Change Cipher Spec (compatibility) */
    if (rec_type == TLS_CT_CHANGE_CIPHER_SPEC) {
        return tls_recv_decrypt(conn, out_inner_type, out_data, out_len);
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

static int tls_store_server_certificate(neverc_tls_conn_t *conn,
                                        const uint8_t *message,
                                        size_t message_len) {
    if (!conn || !message || conn->peer_cert || message_len < 4)
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
    while (pos < list_end) {
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
    return 0;

fail:
    free(first_certificate);
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
    neverc_crypto_rand_read(client_random, 32);

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
    neverc_crypto_rand_read(session_id, 32);
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
    if (rec_type != TLS_CT_HANDSHAKE || rec_len < 4 ||
        rec_data[0] != TLS_HS_SERVER_HELLO)
        return -1;

    /* Hash ServerHello into transcript */
    neverc_sha256_update(&transcript, rec_data, rec_len);

    /* Parse ServerHello */
    size_t sh_pos = 4; /* skip hs header */
    /* uint16_t sh_version = get_u16(rec_data + sh_pos); */ sh_pos += 2;
    /* uint8_t server_random[32]; */ sh_pos += 32;
    uint8_t sh_session_id_len = rec_data[sh_pos++];
    sh_pos += sh_session_id_len;
    uint16_t selected_cipher = get_u16(rec_data + sh_pos); sh_pos += 2;
    sh_pos++; /* compression */

    tls_cipher_id_t cipher_id;
    if (selected_cipher == NEVERC_TLS_AES_128_GCM_SHA256)
        cipher_id = TLS_CIPHER_AES_128_GCM_SHA256;
    else if (selected_cipher == NEVERC_TLS_CHACHA20_POLY1305_SHA256)
        cipher_id = TLS_CIPHER_CHACHA20_POLY1305_SHA256;
    else
        return -1;
    conn->cipher_suite = selected_cipher;

    /* Parse ServerHello extensions to get key_share */
    uint8_t server_pubkey[32];
    int found_keyshare = 0;
    if (sh_pos + 2 <= rec_len) {
        uint16_t sh_ext_len = get_u16(rec_data + sh_pos); sh_pos += 2;
        size_t sh_ext_end = sh_pos + sh_ext_len;
        while (sh_pos + 4 <= sh_ext_end) {
            uint16_t ext_type = get_u16(rec_data + sh_pos); sh_pos += 2;
            uint16_t ext_data_len = get_u16(rec_data + sh_pos); sh_pos += 2;
            if (ext_type == TLS_EXT_KEY_SHARE && ext_data_len >= 36) {
                /* uint16_t group = get_u16(rec_data + sh_pos); */
                uint16_t ke_len = get_u16(rec_data + sh_pos + 2);
                if (ke_len == 32) {
                    memcpy(server_pubkey, rec_data + sh_pos + 4, 32);
                    found_keyshare = 1;
                }
            }
            sh_pos += ext_data_len;
        }
    }
    if (!found_keyshare) return -1;

    /* Compute shared secret via X25519 ECDH */
    uint8_t shared_secret[32];
    if (neverc_ecdh_compute(&ecdh_key, server_pubkey, 32,
                             shared_secret, 32) < 0)
        return -1;

    /* Derive handshake secrets */
    uint8_t early_secret[32];
    uint8_t zero_ikm[32] = {0};
    neverc_hkdf_extract_sha256(early_secret, NULL, 0, zero_ikm, 32);

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

    /* Set handshake traffic keys */
    derive_traffic_keys(server_hs_traffic_secret, &conn->read_keys, cipher_id);
    derive_traffic_keys(client_hs_traffic_secret, &conn->write_keys, cipher_id);

    /* Read encrypted handshake messages (EncryptedExtensions, Certificate,
     * CertificateVerify, Finished) */
    int got_finished = 0;
    int got_certificate_verify = 0;
    while (!got_finished) {
        uint8_t inner_type;
        uint8_t hs_data[TLS_MAX_PLAINTEXT];
        size_t hs_len;
        if (tls_recv_decrypt(conn, &inner_type, hs_data, &hs_len) != 0)
            return -1;

        if (inner_type != TLS_CT_HANDSHAKE) {
            if (inner_type == TLS_CT_ALERT) return -1;
            continue;
        }

        /* Process handshake messages in the decrypted data */
        size_t hpos = 0;
        while (hpos + 4 <= hs_len) {
            uint8_t hs_type = hs_data[hpos];
            uint32_t msg_len = get_u24(hs_data + hpos + 1);
            if ((size_t)msg_len > hs_len - hpos - 4)
                return -1;

            const uint8_t *message = hs_data + hpos + 4;
            if (hs_type == TLS_HS_CERTIFICATE) {
                if (tls_store_server_certificate(
                        conn, message, msg_len) != 0)
                    return -1;
            } else if (hs_type == TLS_HS_CERT_VERIFY) {
                if (!conn->peer_cert || got_certificate_verify ||
                    msg_len < 4)
                    return -1;
                uint16_t signature_scheme = get_u16(message);
                size_t signature_len = get_u16(message + 2);
                if (signature_len == 0 ||
                    signature_len != (size_t)msg_len - 4)
                    return -1;

                uint8_t transcript_hash[32];
                neverc_sha256_ctx transcript_copy = transcript;
                neverc_sha256_final(
                    &transcript_copy, transcript_hash);
                neverc_x509_cert_t certificate;
                if (neverc_x509_parse_certificate(
                        &certificate, conn->peer_cert,
                        conn->peer_cert_len) != 0)
                    return -1;
                int verify_result =
                    neverc_tls_verify_certificate_verify(
                        &certificate, signature_scheme, 1,
                        transcript_hash, sizeof(transcript_hash),
                        message + 4, signature_len);
                neverc_x509_cert_free(&certificate);
                if (verify_result != 0)
                    return -1;
                got_certificate_verify = 1;
            }

            /* Hash all handshake messages into transcript
             * (for Finished verification) */
            if (hs_type != TLS_HS_FINISHED)
                neverc_sha256_update(&transcript, hs_data + hpos,
                                      4 + msg_len);

            if (hs_type == TLS_HS_FINISHED) {
                /* Verify server Finished */
                if (!got_certificate_verify)
                    return -1;
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
                    return -1;

                /* Hash Finished into transcript */
                neverc_sha256_update(&transcript, hs_data + hpos,
                                      4 + msg_len);
                got_finished = 1;
            }

            hpos += 4 + msg_len;
        }
        if (hpos != hs_len)
            return -1;
    }

    /* Send client Finished */
    {
        uint8_t transcript_hash[32];
        {
            neverc_sha256_ctx copy = transcript;
            neverc_sha256_final(&copy, transcript_hash);
        }

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
    neverc_hkdf_extract_sha256(master_secret, master_derived, 32,
                                zero_ikm, 32);

    uint8_t transcript_hash_final[32];
    {
        neverc_sha256_ctx copy = transcript;
        neverc_sha256_final(&copy, transcript_hash_final);
    }

    uint8_t client_app_secret[32];
    uint8_t server_app_secret[32];
    derive_secret(master_secret, "c ap traffic", 12,
                   transcript_hash_final, client_app_secret);
    derive_secret(master_secret, "s ap traffic", 12,
                   transcript_hash_final, server_app_secret);

    derive_traffic_keys(server_app_secret, &conn->read_keys, cipher_id);
    derive_traffic_keys(client_app_secret, &conn->write_keys, cipher_id);

    conn->handshake_done = 1;
    return 0;
}

/* ======================================================================
 * TLS 1.3 Handshake — Server
 * ====================================================================== */

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
    if (rec_type != TLS_CT_HANDSHAKE || rec_len < 4 ||
        rec_data[0] != TLS_HS_CLIENT_HELLO)
        return -1;

    neverc_sha256_update(&transcript, rec_data, rec_len);

    /* Parse ClientHello to extract key_share */
    uint32_t ch_body_len = get_u24(rec_data + 1);
    size_t ch_pos = 4;
    ch_pos += 2; /* legacy version */
    /* uint8_t client_random[32]; */
    ch_pos += 32;
    uint8_t ch_sid_len = rec_data[ch_pos++];
    uint8_t ch_session_id[32];
    if (ch_sid_len > 0 && ch_sid_len <= 32)
        memcpy(ch_session_id, rec_data + ch_pos, ch_sid_len);
    ch_pos += ch_sid_len;

    /* cipher suites */
    uint16_t cs_len = get_u16(rec_data + ch_pos); ch_pos += 2;
    uint16_t selected_cipher = NEVERC_TLS_AES_128_GCM_SHA256;
    tls_cipher_id_t cipher_id = TLS_CIPHER_AES_128_GCM_SHA256;
    for (size_t i = 0; i + 1 < cs_len; i += 2) {
        uint16_t cs = get_u16(rec_data + ch_pos + i);
        if (cs == NEVERC_TLS_AES_128_GCM_SHA256 ||
            cs == NEVERC_TLS_CHACHA20_POLY1305_SHA256) {
            selected_cipher = cs;
            cipher_id = (cs == NEVERC_TLS_AES_128_GCM_SHA256)
                ? TLS_CIPHER_AES_128_GCM_SHA256
                : TLS_CIPHER_CHACHA20_POLY1305_SHA256;
            break;
        }
    }
    ch_pos += cs_len;
    ch_pos++; /* compression methods length */
    ch_pos++; /* null compression */

    conn->cipher_suite = selected_cipher;

    /* Parse extensions */
    uint8_t client_pubkey[32];
    int found_keyshare = 0;
    if (ch_pos + 2 <= 4 + ch_body_len) {
        uint16_t ext_total = get_u16(rec_data + ch_pos); ch_pos += 2;
        size_t ext_end = ch_pos + ext_total;
        while (ch_pos + 4 <= ext_end && ch_pos + 4 <= rec_len) {
            uint16_t ext_type = get_u16(rec_data + ch_pos); ch_pos += 2;
            uint16_t ext_len = get_u16(rec_data + ch_pos); ch_pos += 2;
            if (ext_type == TLS_EXT_KEY_SHARE) {
                /* client key shares list */
                size_t ks_pos = ch_pos;
                uint16_t shares_len = get_u16(rec_data + ks_pos); ks_pos += 2;
                size_t shares_end = ks_pos + shares_len;
                while (ks_pos + 4 <= shares_end) {
                    uint16_t group = get_u16(rec_data + ks_pos); ks_pos += 2;
                    uint16_t ke_len = get_u16(rec_data + ks_pos); ks_pos += 2;
                    if (group == NEVERC_TLS_GROUP_X25519 && ke_len == 32) {
                        memcpy(client_pubkey, rec_data + ks_pos, 32);
                        found_keyshare = 1;
                    }
                    ks_pos += ke_len;
                }
            }
            ch_pos += ext_len;
        }
    }
    if (!found_keyshare) return -1;

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
    neverc_crypto_rand_read(server_random, 32);
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
    uint8_t zero_ikm[32] = {0};
    neverc_hkdf_extract_sha256(early_secret, NULL, 0, zero_ikm, 32);

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

    /* Receive client Finished */
    {
        uint8_t inner_type;
        uint8_t hs_data[TLS_MAX_PLAINTEXT];
        size_t hs_len;
        if (tls_recv_decrypt(conn, &inner_type, hs_data, &hs_len) != 0)
            return -1;
        if (inner_type != TLS_CT_HANDSHAKE || hs_len < 36 ||
            hs_data[0] != TLS_HS_FINISHED)
            return -1;

        uint8_t transcript_hash[32];
        {
            neverc_sha256_ctx copy = transcript;
            neverc_sha256_final(&copy, transcript_hash);
        }

        uint8_t finished_key[32];
        hkdf_expand_label(client_hs_traffic_secret, 32,
                           "finished", 8, NULL, 0, finished_key, 32);

        uint8_t expected[32];
        neverc_hmac_sha256(finished_key, 32,
                            transcript_hash, 32,
                            expected);

        if (!neverc_subtle_constant_time_compare(
                hs_data + 4, expected, 32))
            return -1;

        neverc_sha256_update(&transcript, hs_data, hs_len);
    }

    /* Derive application traffic keys */
    uint8_t master_derived[32];
    derive_secret(handshake_secret, "derived", 7, empty_hash, master_derived);

    uint8_t master_secret[32];
    neverc_hkdf_extract_sha256(master_secret, master_derived, 32,
                                zero_ikm, 32);

    uint8_t transcript_hash_final[32];
    {
        neverc_sha256_ctx copy = transcript;
        neverc_sha256_final(&copy, transcript_hash_final);
    }

    uint8_t client_app_secret[32];
    uint8_t server_app_secret[32];
    derive_secret(master_secret, "c ap traffic", 12,
                   transcript_hash_final, client_app_secret);
    derive_secret(master_secret, "s ap traffic", 12,
                   transcript_hash_final, server_app_secret);

    derive_traffic_keys(client_app_secret, &conn->read_keys, cipher_id);
    derive_traffic_keys(server_app_secret, &conn->write_keys, cipher_id);

    conn->handshake_done = 1;
    return 0;
}

/* ======================================================================
 * Public API
 * ====================================================================== */

static const char k_tls_unavailable[] =
    "TLS transport is unavailable: certificate chain validation and "
    "server CertificateVerify signing are not implemented";

static void tls_set_unavailable(const char **errp) {
    if (errp)
        *errp = k_tls_unavailable;
}

neverc_tls_conn_t *neverc_tls_dial(const char *addr,
                                    neverc_tls_config_t *cfg,
                                    const char **errp) {
    (void)addr;
    (void)cfg;
    tls_set_unavailable(errp);
    return NULL;
}

neverc_tls_conn_t *neverc_tls_server(neverc_tcp_conn_t *tcp,
                                      neverc_tls_config_t *cfg,
                                      const char **errp) {
    (void)tcp;
    (void)cfg;
    tls_set_unavailable(errp);
    return NULL;
}

neverc_tls_conn_t *neverc_tls_client(neverc_tcp_conn_t *tcp,
                                      neverc_tls_config_t *cfg,
                                      const char **errp) {
    (void)tcp;
    (void)cfg;
    tls_set_unavailable(errp);
    return NULL;
}

int neverc_tls_read(neverc_tls_conn_t *conn, void *buf, size_t buflen) {
    if (!conn || conn->closed) return -1;

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

    /* Decrypt next record */
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

    if (inner_type != TLS_CT_APPLICATION_DATA) return -1;

    size_t n = buflen < data_len ? buflen : data_len;
    memcpy(buf, data, n);

    /* Buffer remaining data */
    if (data_len > n) {
        size_t rem = data_len - n;
        memcpy(conn->decrypt_buf, data + n, rem);
        conn->decrypt_buf_len = rem;
        conn->decrypt_buf_pos = 0;
    }

    return (int)n;
}

int neverc_tls_write(neverc_tls_conn_t *conn, const void *data, size_t len) {
    if (!conn || conn->closed || !data || len == 0) return -1;

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
    (void)addr;
    (void)cfg;
    tls_set_unavailable(errp);
    return NULL;
}

neverc_tls_conn_t *neverc_tls_accept(neverc_tls_listener_t *ln,
                                      const char **errp) {
    (void)ln;
    tls_set_unavailable(errp);
    return NULL;
}

void neverc_tls_listener_close(neverc_tls_listener_t *ln) {
    if (!ln) return;
    neverc_tcp_listener_close(ln->tcp_ln);
    free(ln);
}
