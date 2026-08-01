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

#include "neverc/std/crypto/aes.h"
#include "neverc/std/crypto/chacha20poly1305.h"
#include "neverc/std/crypto/gcm.h"
#include "neverc/std/net/tcp.h"

void nci_tls_set_application_keys(
    neverc_tls_conn_t *conn, tls_cipher_id_t cipher,
    const uint8_t read_secret[TLS_HASH_SIZE_SHA256],
    const uint8_t write_secret[TLS_HASH_SIZE_SHA256]) {
    memcpy(conn->read_traffic_secret, read_secret, TLS_HASH_SIZE_SHA256);
    memcpy(conn->write_traffic_secret, write_secret, TLS_HASH_SIZE_SHA256);
    nci_tls_derive_traffic_keys(
        conn->read_traffic_secret, &conn->read_keys, cipher);
    nci_tls_derive_traffic_keys(
        conn->write_traffic_secret, &conn->write_keys, cipher);
    conn->application_keys_active = 1;
    conn->non_advancing_records = 0;
}

/* ======================================================================
 * TLS Record Layer
 * ====================================================================== */

int nci_tls_raw_write(neverc_tcp_conn_t *tcp, const void *data, size_t len) {
    int written = neverc_tcp_write(tcp, data, len);
    return written >= 0 && (size_t)written == len ? 0 : -1;
}

int nci_tls_send_record(neverc_tcp_conn_t *tcp, uint8_t content_type,
                            const uint8_t *data, size_t len) {
    if (!tcp || (!data && len != 0) || len > TLS_MAX_PLAINTEXT)
        return -1;
    uint8_t hdr[5];
    hdr[0] = content_type;
    tls_put_u16(hdr + 1, TLS_LEGACY_VERSION);
    tls_put_u16(hdr + 3, (uint16_t)len);
    if (nci_tls_raw_write(tcp, hdr, 5) != 0) return -1;
    if (len > 0 && nci_tls_raw_write(tcp, data, len) != 0) return -1;
    return 0;
}

int nci_tls_send_encrypted_unlocked(
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
    tls_put_u16(hdr + 1, TLS_LEGACY_VERSION);
    tls_put_u16(hdr + 3, (uint16_t)ct_len);

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
    if (nci_tls_raw_write(conn->tcp, hdr, 5) != 0) rc = -1;
    if (rc == 0 &&
        nci_tls_raw_write(conn->tcp, ciphertext, ct_len) != 0)
        rc = -1;
    free(ciphertext);
    return rc;
}

int nci_tls_send_encrypted(
    neverc_tls_conn_t *conn, uint8_t inner_type,
    const uint8_t *data, size_t len) {
    if (!conn || !conn->mutexes_initialized)
        return -1;
    tls_mutex_lock(&conn->write_mutex);
    int result = nci_tls_send_encrypted_unlocked(
        conn, inner_type, data, len);
    tls_mutex_unlock(&conn->write_mutex);
    return result;
}

int nci_tls_append_handshake_bytes(
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
int nci_tls_next_handshake_message(
    neverc_tls_conn_t *conn, const uint8_t **message,
    size_t *message_len) {
    if (!conn || !message || !message_len)
        return -1;
    *message = NULL;
    *message_len = 0;
    if (conn->handshake_len < 4)
        return 0;

    size_t body_len = tls_get_u24(conn->handshake_buf + 1);
    if (body_len > TLS_MAX_HANDSHAKE - 4)
        return -1;
    size_t total_len = 4 + body_len;
    if (conn->handshake_len < total_len)
        return 0;
    *message = conn->handshake_buf;
    *message_len = total_len;
    return 1;
}

int nci_tls_consume_handshake_message(
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

void nci_tls_clear_handshake_buffer(neverc_tls_conn_t *conn) {
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

    if (nci_tls_append_handshake_bytes(
            &conn, messages, 3) != 0 ||
        nci_tls_next_handshake_message(
            &conn, &message, &message_len) != 0 ||
        nci_tls_append_handshake_bytes(
            &conn, messages + 3, sizeof(messages) - 3) != 0 ||
        nci_tls_next_handshake_message(
            &conn, &message, &message_len) != 1 ||
        message_len != 6 ||
        memcmp(message, messages, message_len) != 0 ||
        nci_tls_consume_handshake_message(
            &conn, message_len) != 0 ||
        nci_tls_next_handshake_message(
            &conn, &message, &message_len) != 1 ||
        message_len != 5 ||
        memcmp(message, messages + 6, message_len) != 0 ||
        nci_tls_consume_handshake_message(
            &conn, message_len) != 0 ||
        conn.handshake_len != 0)
        goto done;

    if (nci_tls_append_handshake_bytes(
            &conn, oversized_header,
            sizeof(oversized_header)) != 0 ||
        nci_tls_next_handshake_message(
            &conn, &message, &message_len) != -1)
        goto done;
    result = 0;

done:
    nci_tls_clear_handshake_buffer(&conn);
    return result;
}
#endif

size_t nci_tls_handshake_fragment_size(
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

int nci_tls_send_plain_handshake(
    neverc_tls_conn_t *conn, const uint8_t *data, size_t data_len) {
    if (!conn || !conn->tcp || !data || data_len == 0)
        return -1;
    size_t fragment_size =
        nci_tls_handshake_fragment_size(conn, data_len);
    for (size_t offset = 0; offset < data_len;) {
        size_t remaining = data_len - offset;
        size_t chunk = remaining < fragment_size ?
                       remaining : fragment_size;
        if (nci_tls_send_record(
                conn->tcp, TLS_CT_HANDSHAKE,
                data + offset, chunk) != 0)
            return -1;
        offset += chunk;
    }
    return 0;
}

int nci_tls_send_encrypted_handshake(
    neverc_tls_conn_t *conn, const uint8_t *data, size_t data_len) {
    if (!conn || !data || data_len == 0)
        return -1;
    size_t fragment_size =
        nci_tls_handshake_fragment_size(conn, data_len);
    for (size_t offset = 0; offset < data_len;) {
        size_t remaining = data_len - offset;
        size_t chunk = remaining < fragment_size ?
                       remaining : fragment_size;
        if (nci_tls_send_encrypted(
                conn, TLS_CT_HANDSHAKE,
                data + offset, chunk) != 0)
            return -1;
        offset += chunk;
    }
    return 0;
}

int nci_tls_send_alert_level(
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
        nci_tls_send_encrypted_unlocked(
            conn, TLS_CT_ALERT, alert, sizeof(alert)) :
        nci_tls_send_record(conn->tcp, TLS_CT_ALERT, alert, sizeof(alert));
    conn->alert_sent = 1;
    if (description == TLS_ALERT_CLOSE_NOTIFY)
        conn->write_closed = 1;
    else
        conn->closed = 1;
    tls_mutex_unlock(&conn->write_mutex);
    return result;
}

int nci_tls_send_alert(
    neverc_tls_conn_t *conn, uint8_t description) {
    return nci_tls_send_alert_level(conn, 2, description);
}

int nci_tls_send_close_notify(neverc_tls_conn_t *conn) {
    return nci_tls_send_alert_level(
        conn, 1, TLS_ALERT_CLOSE_NOTIFY);
}

int nci_tls_fail(
    neverc_tls_conn_t *conn, uint8_t description) {
    (void)nci_tls_send_alert(conn, description);
    return -1;
}

int nci_tls_error(
    neverc_tls_conn_t *conn, const char *reason) {
    if (conn)
        conn->failure_reason = reason;
    return -1;
}

int nci_tls_protocol_error(
    neverc_tls_conn_t *conn, uint8_t description,
    const char *reason) {
    (void)nci_tls_send_alert(conn, description);
    return nci_tls_error(conn, reason);
}

int nci_tls_recv_record(neverc_tls_conn_t *conn, uint8_t *out_type,
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
            return nci_tls_error(
                conn, "TLS peer closed without close_notify");
        }
        conn->read_buf_len += (size_t)n;
    }

    *out_type = conn->read_buf[0];
    uint16_t record_version = tls_get_u16(conn->read_buf + 1);
    int is_ciphertext =
        conn->write_keys_active &&
        *out_type == TLS_CT_APPLICATION_DATA;
    if (is_ciphertext &&
        record_version != TLS_LEGACY_VERSION)
        return nci_tls_protocol_error(
            conn, TLS_ALERT_PROTOCOL_VERSION,
            "invalid TLS record version");
    uint16_t rec_len = tls_get_u16(conn->read_buf + 3);
    size_t record_limit = is_ciphertext ?
                          TLS_MAX_CIPHERTEXT :
                          TLS_MAX_PLAINTEXT;
    if (rec_len > record_limit)
        return nci_tls_protocol_error(
            conn, TLS_ALERT_RECORD_OVERFLOW,
            "TLS record exceeds the configured limit");
    size_t total = TLS_RECORD_HEADER_SIZE + rec_len;
    while (conn->read_buf_len < total) {
        int n = neverc_tcp_read(conn->tcp,
                                 conn->read_buf + conn->read_buf_len,
                                 sizeof(conn->read_buf) - conn->read_buf_len);
        if (n <= 0) {
            conn->closed = 1;
            return nci_tls_error(
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

int nci_tls_recv_decrypt(neverc_tls_conn_t *conn,
                              uint8_t *out_inner_type,
                              uint8_t *out_data, size_t *out_len) {
    if (!conn || !out_inner_type || !out_data || !out_len)
        return -1;

    uint8_t rec_type;
    uint8_t rec_data[TLS_MAX_CIPHERTEXT];
    size_t rec_len;

    for (;;) {
        if (nci_tls_recv_record(
                conn, &rec_type, rec_data, &rec_len) != 0)
            return -1;
        if (rec_type != TLS_CT_CHANGE_CIPHER_SPEC)
            break;
        if (rec_len != 1 || rec_data[0] != 1)
            return nci_tls_protocol_error(
                conn, TLS_ALERT_DECODE_ERROR,
                "malformed TLS change_cipher_spec record");
        if (++conn->non_advancing_records >
            TLS_MAX_NON_ADVANCING_RECORDS)
            return nci_tls_protocol_error(
                conn, TLS_ALERT_UNEXPECTED_MESSAGE,
                "too many non-advancing TLS records");
    }

    if (rec_type != TLS_CT_APPLICATION_DATA)
        return nci_tls_protocol_error(
            conn, TLS_ALERT_UNEXPECTED_MESSAGE,
            "received plaintext record after TLS keys became active");

    tls_traffic_keys_t *keys = &conn->read_keys;
    if (keys->seq == UINT64_MAX)
        return nci_tls_protocol_error(
            conn, TLS_ALERT_INTERNAL_ERROR,
            "TLS read sequence number exhausted");

    uint8_t nonce[12];
    memcpy(nonce, keys->iv, 12);
    for (int i = 0; i < 8; i++)
        nonce[12 - 1 - i] ^= (uint8_t)(keys->seq >> (i * 8));

    /* Reconstruct AAD (record header) */
    uint8_t aad[5];
    aad[0] = TLS_CT_APPLICATION_DATA;
    tls_put_u16(aad + 1, TLS_LEGACY_VERSION);
    tls_put_u16(aad + 3, (uint16_t)rec_len);

    if (rec_len <= TLS_AEAD_TAG_SIZE)
        return nci_tls_protocol_error(
            conn, TLS_ALERT_BAD_RECORD_MAC,
            "TLS ciphertext is too short");

    size_t ct_body_len = rec_len - TLS_AEAD_TAG_SIZE;
    uint8_t *plaintext = (uint8_t *)malloc(ct_body_len);
    if (!plaintext)
        return nci_tls_protocol_error(
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
        return nci_tls_protocol_error(
            conn, TLS_ALERT_BAD_RECORD_MAC,
            "TLS record authentication failed");
    }

    keys->seq++;

    /* Remove padding and find inner content type (last non-zero byte) */
    while (ct_body_len > 0 && plaintext[ct_body_len - 1] == 0)
        ct_body_len--;
    if (ct_body_len == 0) {
        free(plaintext);
        return nci_tls_protocol_error(
            conn, TLS_ALERT_UNEXPECTED_MESSAGE,
            "TLS inner plaintext has no content type");
    }

    *out_inner_type = plaintext[ct_body_len - 1];
    ct_body_len--;
    if (ct_body_len > TLS_MAX_PLAINTEXT) {
        free(plaintext);
        return nci_tls_protocol_error(
            conn, TLS_ALERT_RECORD_OVERFLOW,
            "TLS inner plaintext exceeds the configured limit");
    }
    if (*out_inner_type != TLS_CT_ALERT &&
        *out_inner_type != TLS_CT_HANDSHAKE &&
        *out_inner_type != TLS_CT_APPLICATION_DATA) {
        free(plaintext);
        return nci_tls_protocol_error(
            conn, TLS_ALERT_UNEXPECTED_MESSAGE,
            "TLS inner plaintext has an invalid content type");
    }
    memcpy(out_data, plaintext, ct_body_len);
    *out_len = ct_body_len;

    free(plaintext);
    return 0;
}

int nci_tls_recv_plain_handshake_message(
    neverc_tls_conn_t *conn, uint8_t expected_type,
    const uint8_t **message, size_t *message_len) {
    if (!conn || !message || !message_len)
        return -1;

    for (;;) {
        int available = nci_tls_next_handshake_message(
            conn, message, message_len);
        if (available < 0)
            return nci_tls_protocol_error(
                conn, TLS_ALERT_DECODE_ERROR,
                "TLS handshake message exceeds the configured limit");
        if (available > 0) {
            if ((*message)[0] != expected_type)
                return nci_tls_protocol_error(
                    conn, TLS_ALERT_UNEXPECTED_MESSAGE,
                    "TLS handshake message arrived out of order");
            return 0;
        }

        uint8_t record_type;
        uint8_t record_data[TLS_MAX_CIPHERTEXT];
        size_t record_len;
        if (nci_tls_recv_record(
                conn, &record_type,
                record_data, &record_len) != 0)
            return -1;
        if (record_type == TLS_CT_ALERT)
            return nci_tls_error(
                conn, "peer sent an alert during TLS handshake");
        if (record_type != TLS_CT_HANDSHAKE)
            return nci_tls_protocol_error(
                conn, TLS_ALERT_UNEXPECTED_MESSAGE,
                "peer sent a non-handshake plaintext record");
        if (record_len == 0)
            return nci_tls_protocol_error(
                conn, TLS_ALERT_DECODE_ERROR,
                "peer sent an empty TLS handshake record");
        if (record_len >
            TLS_MAX_HANDSHAKE - conn->handshake_len)
            return nci_tls_protocol_error(
                conn, TLS_ALERT_DECODE_ERROR,
                "TLS handshake reassembly exceeds the configured limit");
        if (nci_tls_append_handshake_bytes(
                conn, record_data, record_len) != 0)
            return nci_tls_protocol_error(
                conn, TLS_ALERT_INTERNAL_ERROR,
                "TLS handshake reassembly allocation failed");
    }
}

