/*
 * QUIC-TLS Handshake Integration (RFC 9001)
 *
 * Coordinates TLS 1.3 handshake with QUIC packet protection. Manages the
 * key schedule across encryption levels:
 *
 *   Initial → Handshake → 1-RTT (Application Data)
 *
 * CRYPTO frames carry TLS handshake messages. Each encryption level has
 * its own send/receive buffers for CRYPTO data. TLS operates on these
 * buffers via quic_crypto_data callbacks.
 *
 * Key update (RFC 9001 §6):
 *   - Triggered by peer KEY_PHASE bit flip
 *   - Derives next-generation keys using HKDF-Expand-Label
 *   - Both sides maintain current + next keys for read/write
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/* From quic_packet_protection.c */
typedef struct {
    uint8_t key[16];
    uint8_t iv[12];
    uint8_t hp[16];
} quic_keys_t;

typedef struct {
    quic_keys_t client;
    quic_keys_t server;
} quic_initial_keys_t;

extern int neverc_quic_derive_initial_keys(const uint8_t *dcid, size_t dcid_len,
                                            uint32_t version,
                                            quic_initial_keys_t *keys);

extern int neverc_hkdf_extract_sha256(const uint8_t *salt, size_t salt_len,
                                       const uint8_t *ikm, size_t ikm_len,
                                       uint8_t *prk);
extern int neverc_hkdf_expand_sha256(const uint8_t *prk, size_t prk_len,
                                      const uint8_t *info, size_t info_len,
                                      uint8_t *okm, size_t okm_len);

/* ======================================================================
 * Encryption Levels (RFC 9001 §4.1)
 * ====================================================================== */

typedef enum {
    QUIC_ENC_INITIAL = 0,
    QUIC_ENC_HANDSHAKE = 1,
    QUIC_ENC_APPLICATION = 2,  /* 1-RTT */
    QUIC_ENC_EARLY_DATA = 3,   /* 0-RTT */
    QUIC_ENC_LEVEL_COUNT = 4,
} quic_enc_level_t;

/* ======================================================================
 * CRYPTO Data Buffers
 *
 * Each encryption level maintains ordered CRYPTO data that carries
 * TLS handshake messages. These are assembled from CRYPTO frames
 * which can arrive out of order within a level.
 * ====================================================================== */

#define QUIC_CRYPTO_BUF_INITIAL_CAP  4096

typedef struct {
    uint8_t *data;
    size_t   len;
    size_t   cap;
    uint64_t offset;       /* next expected offset from peer */
    uint64_t write_offset; /* next offset we'll send from */
} quic_crypto_buf_t;

static void crypto_buf_init(quic_crypto_buf_t *buf) {
    buf->data = (uint8_t *)malloc(QUIC_CRYPTO_BUF_INITIAL_CAP);
    buf->len = 0;
    buf->cap = QUIC_CRYPTO_BUF_INITIAL_CAP;
    buf->offset = 0;
    buf->write_offset = 0;
}

static void crypto_buf_free(quic_crypto_buf_t *buf) {
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

static int crypto_buf_append(quic_crypto_buf_t *buf,
                              const uint8_t *data, size_t len) {
    if (buf->len + len > buf->cap) {
        size_t new_cap = buf->cap * 2;
        while (new_cap < buf->len + len) new_cap *= 2;
        uint8_t *new_data = (uint8_t *)realloc(buf->data, new_cap);
        if (!new_data) return -1;
        buf->data = new_data;
        buf->cap = new_cap;
    }
    memcpy(buf->data + buf->len, data, len);
    buf->len += len;
    return 0;
}

/* ======================================================================
 * QUIC-TLS Key Schedule State
 * ====================================================================== */

typedef struct {
    quic_keys_t read;
    quic_keys_t write;
    int         available;  /* keys have been derived */
} quic_level_keys_t;

typedef enum {
    QUIC_TLS_STATE_IDLE = 0,
    QUIC_TLS_STATE_CLIENT_INITIAL,
    QUIC_TLS_STATE_SERVER_INITIAL,
    QUIC_TLS_STATE_HANDSHAKE,
    QUIC_TLS_STATE_ESTABLISHED,
    QUIC_TLS_STATE_ERROR,
} quic_tls_state_t;

typedef struct quic_tls {
    quic_tls_state_t   state;
    int                is_server;

    /* Key material per encryption level */
    quic_level_keys_t  levels[QUIC_ENC_LEVEL_COUNT];

    /* CRYPTO buffers per level (send and receive) */
    quic_crypto_buf_t  crypto_send[QUIC_ENC_LEVEL_COUNT];
    quic_crypto_buf_t  crypto_recv[QUIC_ENC_LEVEL_COUNT];

    /* Key update state (1-RTT) */
    quic_keys_t        next_read_key;
    quic_keys_t        next_write_key;
    int                key_phase;       /* current key phase bit */
    uint64_t           key_update_pn;   /* packet number at last key update */

    /* Handshake transcript hash (SHA-256 of all handshake messages) */
    uint8_t            handshake_hash[32];
    int                handshake_complete;

    /* Traffic secrets for key update derivation */
    uint8_t            client_app_secret[32];
    uint8_t            server_app_secret[32];

    /* ALPN result */
    char               alpn[32];

    /* Error */
    uint64_t           error_code;
    char               error_reason[256];
} quic_tls_t;

/* ======================================================================
 * Lifecycle
 * ====================================================================== */

quic_tls_t *neverc_quic_tls_create(int is_server) {
    quic_tls_t *tls = (quic_tls_t *)calloc(1, sizeof(*tls));
    if (!tls) return NULL;

    tls->is_server = is_server;
    tls->state = QUIC_TLS_STATE_IDLE;

    for (int i = 0; i < QUIC_ENC_LEVEL_COUNT; i++) {
        crypto_buf_init(&tls->crypto_send[i]);
        crypto_buf_init(&tls->crypto_recv[i]);
    }

    return tls;
}

void neverc_quic_tls_destroy(quic_tls_t *tls) {
    if (!tls) return;
    for (int i = 0; i < QUIC_ENC_LEVEL_COUNT; i++) {
        crypto_buf_free(&tls->crypto_send[i]);
        crypto_buf_free(&tls->crypto_recv[i]);
    }
    /* Zero sensitive key material */
    memset(tls->levels, 0, sizeof(tls->levels));
    memset(tls->client_app_secret, 0, sizeof(tls->client_app_secret));
    memset(tls->server_app_secret, 0, sizeof(tls->server_app_secret));
    free(tls);
}

/* ======================================================================
 * Initial Key Setup
 *
 * Called with the client's Destination Connection ID to derive the
 * Initial encryption keys. Both client and server call this.
 * ====================================================================== */

int neverc_quic_tls_set_initial_dcid(quic_tls_t *tls,
                                       const uint8_t *dcid, size_t dcid_len,
                                       uint32_t version) {
    if (!tls) return -1;

    quic_initial_keys_t initial_keys;
    if (neverc_quic_derive_initial_keys(dcid, dcid_len, version,
                                         &initial_keys) != 0)
        return -1;

    if (tls->is_server) {
        tls->levels[QUIC_ENC_INITIAL].read = initial_keys.client;
        tls->levels[QUIC_ENC_INITIAL].write = initial_keys.server;
    } else {
        tls->levels[QUIC_ENC_INITIAL].read = initial_keys.server;
        tls->levels[QUIC_ENC_INITIAL].write = initial_keys.client;
    }
    tls->levels[QUIC_ENC_INITIAL].available = 1;

    memset(&initial_keys, 0, sizeof(initial_keys));
    return 0;
}

/* ======================================================================
 * CRYPTO Frame Handling
 *
 * When a CRYPTO frame arrives at a given level, feed its data here.
 * The TLS state machine processes it and may produce outgoing CRYPTO
 * data and transition encryption levels.
 * ====================================================================== */

int neverc_quic_tls_receive_crypto(quic_tls_t *tls,
                                     quic_enc_level_t level,
                                     uint64_t offset,
                                     const uint8_t *data, size_t len) {
    if (!tls || level >= QUIC_ENC_LEVEL_COUNT) return -1;

    quic_crypto_buf_t *recv = &tls->crypto_recv[level];

    /* For now, require in-order delivery within a level */
    if (offset != recv->offset) {
        /* Out-of-order: would need a reassembly buffer in production */
        if (offset < recv->offset) return 0; /* duplicate, ignore */
        return -1; /* gap, not supported yet */
    }

    if (crypto_buf_append(recv, data, len) != 0)
        return -1;
    recv->offset += len;

    return 0;
}

/* Get pending CRYPTO data to send at a given level */
int neverc_quic_tls_get_crypto_data(quic_tls_t *tls,
                                      quic_enc_level_t level,
                                      const uint8_t **out_data,
                                      size_t *out_len) {
    if (!tls || level >= QUIC_ENC_LEVEL_COUNT) return -1;

    quic_crypto_buf_t *send = &tls->crypto_send[level];
    *out_data = send->data;
    *out_len = send->len;
    return 0;
}

/* Mark crypto data as sent (advance write offset) */
void neverc_quic_tls_crypto_data_sent(quic_tls_t *tls,
                                        quic_enc_level_t level,
                                        size_t bytes_sent) {
    if (!tls || level >= QUIC_ENC_LEVEL_COUNT) return;
    quic_crypto_buf_t *send = &tls->crypto_send[level];
    send->write_offset += bytes_sent;
}

/* ======================================================================
 * Handshake Key Installation
 *
 * Called by the TLS stack callback when new keys are derived.
 * In a real implementation, this is triggered by TLS state transitions:
 *   - After ClientHello processed → install Handshake keys
 *   - After Finished processed → install 1-RTT keys
 * ====================================================================== */

int neverc_quic_tls_install_keys(quic_tls_t *tls,
                                   quic_enc_level_t level,
                                   const quic_keys_t *read_key,
                                   const quic_keys_t *write_key) {
    if (!tls || level >= QUIC_ENC_LEVEL_COUNT) return -1;

    if (read_key) {
        tls->levels[level].read = *read_key;
    }
    if (write_key) {
        tls->levels[level].write = *write_key;
    }
    tls->levels[level].available = 1;

    return 0;
}

/* ======================================================================
 * Key Update (RFC 9001 §6)
 *
 * Derives next-generation application traffic keys:
 *   next_secret = HKDF-Expand-Label(current_secret, "quic ku", "", 32)
 *   next_key = HKDF-Expand-Label(next_secret, "quic key", "", 16)
 *   next_iv = HKDF-Expand-Label(next_secret, "quic iv", "", 12)
 * ====================================================================== */

static int hkdf_expand_label(const uint8_t *secret, size_t secret_len,
                              const char *label, size_t label_len,
                              const uint8_t *context, size_t context_len,
                              uint8_t *out, size_t out_len) {
    uint8_t info[512];
    size_t pos = 0;

    info[pos++] = (uint8_t)(out_len >> 8);
    info[pos++] = (uint8_t)(out_len & 0xFF);

    size_t full_label_len = 6 + label_len;
    info[pos++] = (uint8_t)full_label_len;
    memcpy(info + pos, "tls13 ", 6);
    pos += 6;
    memcpy(info + pos, label, label_len);
    pos += label_len;

    info[pos++] = (uint8_t)context_len;
    if (context_len > 0) {
        memcpy(info + pos, context, context_len);
        pos += context_len;
    }

    return neverc_hkdf_expand_sha256(secret, secret_len, info, pos, out, out_len);
}

int neverc_quic_tls_key_update(quic_tls_t *tls) {
    if (!tls || !tls->handshake_complete) return -1;

    /* Derive next client application secret */
    uint8_t new_client_secret[32];
    if (hkdf_expand_label(tls->client_app_secret, 32,
                           "quic ku", 7, NULL, 0,
                           new_client_secret, 32) != 0)
        return -1;

    /* Derive next server application secret */
    uint8_t new_server_secret[32];
    if (hkdf_expand_label(tls->server_app_secret, 32,
                           "quic ku", 7, NULL, 0,
                           new_server_secret, 32) != 0)
        return -1;

    /* Derive keys from new secrets */
    quic_keys_t new_read, new_write;
    const uint8_t *read_secret = tls->is_server ? new_client_secret : new_server_secret;
    const uint8_t *write_secret = tls->is_server ? new_server_secret : new_client_secret;

    hkdf_expand_label(read_secret, 32, "quic key", 8, NULL, 0, new_read.key, 16);
    hkdf_expand_label(read_secret, 32, "quic iv", 7, NULL, 0, new_read.iv, 12);
    hkdf_expand_label(read_secret, 32, "quic hp", 7, NULL, 0, new_read.hp, 16);

    hkdf_expand_label(write_secret, 32, "quic key", 8, NULL, 0, new_write.key, 16);
    hkdf_expand_label(write_secret, 32, "quic iv", 7, NULL, 0, new_write.iv, 12);
    hkdf_expand_label(write_secret, 32, "quic hp", 7, NULL, 0, new_write.hp, 16);

    /* Install new keys */
    tls->levels[QUIC_ENC_APPLICATION].read = new_read;
    tls->levels[QUIC_ENC_APPLICATION].write = new_write;
    tls->key_phase ^= 1;

    /* Update stored secrets */
    memcpy(tls->client_app_secret, new_client_secret, 32);
    memcpy(tls->server_app_secret, new_server_secret, 32);

    /* Zero temporaries */
    memset(new_client_secret, 0, 32);
    memset(new_server_secret, 0, 32);

    return 0;
}

/* ======================================================================
 * Handshake Completion
 *
 * Called when TLS handshake is complete:
 *   - Mark state as established
 *   - Store application traffic secrets for future key updates
 *   - Discard Initial and Handshake keys (RFC 9001 §4.9)
 * ====================================================================== */

int neverc_quic_tls_handshake_complete(quic_tls_t *tls,
                                         const uint8_t *client_app_secret,
                                         const uint8_t *server_app_secret,
                                         const char *negotiated_alpn) {
    if (!tls) return -1;

    tls->state = QUIC_TLS_STATE_ESTABLISHED;
    tls->handshake_complete = 1;
    tls->key_phase = 0;

    if (client_app_secret)
        memcpy(tls->client_app_secret, client_app_secret, 32);
    if (server_app_secret)
        memcpy(tls->server_app_secret, server_app_secret, 32);
    if (negotiated_alpn) {
        size_t alen = strlen(negotiated_alpn);
        if (alen >= sizeof(tls->alpn)) alen = sizeof(tls->alpn) - 1;
        memcpy(tls->alpn, negotiated_alpn, alen);
        tls->alpn[alen] = '\0';
    }

    /* Discard Initial and Handshake keys (RFC 9001 §4.9.1, §4.9.2) */
    memset(&tls->levels[QUIC_ENC_INITIAL], 0, sizeof(quic_level_keys_t));
    memset(&tls->levels[QUIC_ENC_HANDSHAKE], 0, sizeof(quic_level_keys_t));

    /* Free crypto buffers for Initial/Handshake (no longer needed) */
    crypto_buf_free(&tls->crypto_send[QUIC_ENC_INITIAL]);
    crypto_buf_free(&tls->crypto_recv[QUIC_ENC_INITIAL]);
    crypto_buf_free(&tls->crypto_send[QUIC_ENC_HANDSHAKE]);
    crypto_buf_free(&tls->crypto_recv[QUIC_ENC_HANDSHAKE]);

    return 0;
}

/* ======================================================================
 * Queries
 * ====================================================================== */

int neverc_quic_tls_is_established(const quic_tls_t *tls) {
    return tls && tls->handshake_complete;
}

const char *neverc_quic_tls_alpn(const quic_tls_t *tls) {
    if (!tls || tls->alpn[0] == '\0') return NULL;
    return tls->alpn;
}

int neverc_quic_tls_get_key_phase(const quic_tls_t *tls) {
    return tls ? tls->key_phase : 0;
}

const quic_keys_t *neverc_quic_tls_get_read_keys(const quic_tls_t *tls,
                                                    quic_enc_level_t level) {
    if (!tls || level >= QUIC_ENC_LEVEL_COUNT) return NULL;
    if (!tls->levels[level].available) return NULL;
    return &tls->levels[level].read;
}

const quic_keys_t *neverc_quic_tls_get_write_keys(const quic_tls_t *tls,
                                                     quic_enc_level_t level) {
    if (!tls || level >= QUIC_ENC_LEVEL_COUNT) return NULL;
    if (!tls->levels[level].available) return NULL;
    return &tls->levels[level].write;
}
