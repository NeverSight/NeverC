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

#define TLS_RECORD_WOULD_BLOCK (-2)

static void nci_tls_secure_free(void *buffer, size_t length) {
    if (!buffer) return;
    neverc_platform_secure_zero(buffer, length);
    free(buffer);
}

int nci_tls_set_application_keys(
    neverc_tls_conn_t *conn, tls_cipher_id_t cipher,
    const uint8_t read_secret[TLS_HASH_SIZE_SHA256],
    const uint8_t write_secret[TLS_HASH_SIZE_SHA256]) {
    if (!conn || !read_secret || !write_secret)
        return -1;
    tls_traffic_keys_t read_keys;
    tls_traffic_keys_t write_keys;
    memset(&read_keys, 0, sizeof(read_keys));
    memset(&write_keys, 0, sizeof(write_keys));
    if (nci_tls_derive_traffic_keys_checked(
            read_secret, &read_keys, cipher) != 0 ||
        nci_tls_derive_traffic_keys_checked(
            write_secret, &write_keys, cipher) != 0) {
        neverc_platform_secure_zero(&read_keys, sizeof(read_keys));
        neverc_platform_secure_zero(&write_keys, sizeof(write_keys));
        return -1;
    }

    memcpy(conn->read_traffic_secret, read_secret, TLS_HASH_SIZE_SHA256);
    memcpy(conn->write_traffic_secret, write_secret, TLS_HASH_SIZE_SHA256);
    neverc_platform_secure_zero(&conn->read_keys, sizeof(conn->read_keys));
    neverc_platform_secure_zero(&conn->write_keys, sizeof(conn->write_keys));
    memcpy(&conn->read_keys, &read_keys, sizeof(read_keys));
    memcpy(&conn->write_keys, &write_keys, sizeof(write_keys));
    neverc_platform_secure_zero(&read_keys, sizeof(read_keys));
    neverc_platform_secure_zero(&write_keys, sizeof(write_keys));
    conn->application_keys_active = 1;
    conn->non_advancing_records = 0;
    return 0;
}

/* ======================================================================
 * TLS Record Layer
 * ====================================================================== */

int nci_tls_raw_write(neverc_tcp_conn_t *tcp, const void *data, size_t len) {
    int written = neverc_tcp_write(tcp, data, len);
    return written >= 0 && (size_t)written == len ? 0 : -1;
}

static int nci_tls_conn_raw_write(neverc_tls_conn_t *conn,
                                  const void *data, size_t len) {
    if (conn->nonblocking_io) {
        if ((!data && len > 0) ||
            conn->pending_write_len > TLS_MAX_PENDING_WRITE ||
            len > TLS_MAX_PENDING_WRITE - conn->pending_write_len)
            return -1;
        size_t required = conn->pending_write_len + len;
        if (required > conn->pending_write_cap) {
            size_t capacity = conn->pending_write_cap
                ? conn->pending_write_cap : 4096;
            while (capacity < required) {
                if (capacity > SIZE_MAX / 2) {
                    capacity = required;
                    break;
                }
                capacity *= 2;
            }
            uint8_t *resized = (uint8_t *)realloc(
                conn->pending_write_buf, capacity);
            if (!resized) return -1;
            conn->pending_write_buf = resized;
            conn->pending_write_cap = capacity;
        }
        if (len > 0)
            memcpy(conn->pending_write_buf + conn->pending_write_len,
                   data, len);
        conn->pending_write_len = required;
        return 0;
    }
    if (!conn->write_context)
        return nci_tls_raw_write(conn->tcp, data, len);
    size_t written = 0;
    while (written < len) {
        neverc_net_result_t result = neverc_tcp_write_context(
            conn->tcp, conn->write_context,
            (const uint8_t *)data + written, len - written);
        written += result.transferred;
        if (result.status != NEVERC_NET_OK || result.transferred == 0)
            return -1;
    }
    return 0;
}

int nci_tls_flush_pending_write(neverc_tls_conn_t *conn) {
    if (!conn || !conn->tcp) return -1;
    while (conn->pending_write_pos < conn->pending_write_len) {
        neverc_net_result_t result = neverc_tcp_try_write(
            conn->tcp,
            conn->pending_write_buf + conn->pending_write_pos,
            conn->pending_write_len - conn->pending_write_pos);
        conn->pending_write_pos += result.transferred;
        if (result.status == NEVERC_NET_WOULD_BLOCK)
            return NCI_TLS_WANT_WRITE;
        if (result.status != NEVERC_NET_OK || result.transferred == 0)
            return -1;
    }
    if (conn->pending_write_buf && conn->pending_write_len > 0)
        neverc_platform_secure_zero(
            conn->pending_write_buf, conn->pending_write_len);
    conn->pending_write_len = 0;
    conn->pending_write_pos = 0;
    return 0;
}

int nci_tls_send_plain_record(
    neverc_tls_conn_t *conn, uint8_t content_type,
    const uint8_t *data, size_t len) {
    if (!conn || !conn->tcp || (!data && len != 0) ||
        len > TLS_MAX_PLAINTEXT)
        return -1;
    uint8_t header[TLS_RECORD_HEADER_SIZE];
    header[0] = content_type;
    tls_put_u16(header + 1, TLS_LEGACY_VERSION);
    tls_put_u16(header + 3, (uint16_t)len);
    if (nci_tls_conn_raw_write(conn, header, sizeof(header)) != 0)
        return -1;
    return len == 0 ? 0 : nci_tls_conn_raw_write(conn, data, len);
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
        len >= TLS_MAX_PLAINTEXT || conn->write_closed)
        return -1;
    tls_traffic_keys_t *keys = &conn->write_keys;
    if (keys->seq == UINT64_MAX) {
        conn->write_closed = 1;
        return -1;
    }

    /* Build nonce: IV XOR sequence number (big-endian in last 8 bytes) */
    uint8_t nonce[12];
    memcpy(nonce, keys->iv, 12);
    for (int i = 0; i < 8; i++)
        nonce[12 - 1 - i] ^= (uint8_t)(keys->seq >> (i * 8));

    /* Build plaintext with inner content type appended */
    size_t pt_len = len + 1; /* data + inner content type */
    uint8_t *plaintext = (uint8_t *)malloc(pt_len);
    if (!plaintext) {
        neverc_platform_secure_zero(nonce, sizeof(nonce));
        return -1;
    }
    if (len > 0) memcpy(plaintext, data, len);
    plaintext[len] = inner_type;

    /* Record header (sent as APPLICATION_DATA for encrypted records) */
    size_t ct_len = pt_len + TLS_AEAD_TAG_SIZE;
    uint8_t *record = (uint8_t *)malloc(TLS_RECORD_HEADER_SIZE + ct_len);
    if (!record) {
        neverc_platform_secure_zero(plaintext, pt_len);
        free(plaintext);
        neverc_platform_secure_zero(nonce, sizeof(nonce));
        return -1;
    }
    uint8_t *hdr = record;
    uint8_t *ciphertext = record + TLS_RECORD_HEADER_SIZE;
    hdr[0] = TLS_CT_APPLICATION_DATA;
    tls_put_u16(hdr + 1, TLS_LEGACY_VERSION);
    tls_put_u16(hdr + 3, (uint16_t)ct_len);

    int encrypt_result = -1;
    if (keys->id == TLS_CIPHER_AES_128_GCM_SHA256) {
        uint8_t tag[16];
        encrypt_result = neverc_gcm_seal(
            &keys->gcm, nonce, plaintext, pt_len,
            hdr, TLS_RECORD_HEADER_SIZE, ciphertext, tag);
        if (encrypt_result == 0)
            memcpy(ciphertext + pt_len, tag, sizeof(tag));
        neverc_platform_secure_zero(tag, sizeof(tag));
    } else if (keys->id == TLS_CIPHER_CHACHA20_POLY1305_SHA256) {
        size_t sealed_len = neverc_chacha20poly1305_seal(
            ciphertext, keys->key, nonce, plaintext, pt_len,
            hdr, TLS_RECORD_HEADER_SIZE);
        encrypt_result = sealed_len == ct_len ? 0 : -1;
    }

    neverc_platform_secure_zero(plaintext, pt_len);
    free(plaintext);
    neverc_platform_secure_zero(nonce, sizeof(nonce));
    if (encrypt_result != 0) {
        neverc_platform_secure_zero(
            record, TLS_RECORD_HEADER_SIZE + ct_len);
        free(record);
        return -1;
    }

    int rc = nci_tls_conn_raw_write(
        conn, record, TLS_RECORD_HEADER_SIZE + ct_len);
    neverc_platform_secure_zero(
        record, TLS_RECORD_HEADER_SIZE + ct_len);
    free(record);
    if (rc == 0) {
        keys->seq++;
    } else {
        /* A blocking transport may have written a prefix. The connection is
         * no longer retryable, and closing it prevents nonce reuse. */
        conn->write_closed = 1;
    }
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
int neverc_tls_test_record_write_failure(void) {
    static const tls_cipher_id_t ciphers[] = {
        TLS_CIPHER_AES_128_GCM_SHA256,
        TLS_CIPHER_CHACHA20_POLY1305_SHA256
    };
    uint8_t traffic_secret[TLS_HASH_SIZE_SHA256] = {0};
    for (size_t i = 0; i < sizeof(ciphers) / sizeof(ciphers[0]); i++) {
        neverc_tls_conn_t conn;
        memset(&conn, 0, sizeof(conn));
        conn.tcp = (neverc_tcp_conn_t *)(uintptr_t)1;
        conn.nonblocking_io = 1;
        conn.pending_write_len = TLS_MAX_PENDING_WRITE;
        if (nci_tls_derive_traffic_keys_checked(
                traffic_secret, &conn.write_keys, ciphers[i]) != 0) {
            neverc_platform_secure_zero(&conn, sizeof(conn));
            return -1;
        }
        conn.write_keys.seq = 7;

        if (nci_tls_send_encrypted_unlocked(
                &conn, TLS_CT_APPLICATION_DATA,
                (const uint8_t *)"x", 1) != -1 ||
            conn.write_keys.seq != 7 || !conn.write_closed) {
            neverc_platform_secure_zero(&conn, sizeof(conn));
            return -1;
        }

        memset(&conn, 0, sizeof(conn));
        conn.tcp = (neverc_tcp_conn_t *)(uintptr_t)1;
        if (nci_tls_derive_traffic_keys_checked(
                traffic_secret, &conn.write_keys, ciphers[i]) != 0) {
            neverc_platform_secure_zero(&conn, sizeof(conn));
            return -1;
        }
        conn.write_keys.seq = UINT64_MAX;
        if (nci_tls_send_encrypted_unlocked(
                &conn, TLS_CT_APPLICATION_DATA,
                (const uint8_t *)"x", 1) != -1 ||
            !conn.write_closed) {
            neverc_platform_secure_zero(&conn, sizeof(conn));
            return -1;
        }
        neverc_platform_secure_zero(&conn, sizeof(conn));
    }

    neverc_tls_conn_t invalid;
    memset(&invalid, 0, sizeof(invalid));
    invalid.tcp = (neverc_tcp_conn_t *)(uintptr_t)1;
    invalid.nonblocking_io = 1;
    invalid.write_keys.id = (tls_cipher_id_t)99;
    invalid.write_keys.seq = 9;
    if (nci_tls_send_encrypted_unlocked(
            &invalid, TLS_CT_APPLICATION_DATA,
            (const uint8_t *)"x", 1) != -1 ||
        invalid.write_keys.seq != 9 || invalid.write_closed ||
        invalid.pending_write_len != 0) {
        neverc_platform_secure_zero(&invalid, sizeof(invalid));
        return -1;
    }
    neverc_platform_secure_zero(&invalid, sizeof(invalid));
    return 0;
}

int neverc_tls_test_fuzz_handshake_reassembly(
    const uint8_t *data, size_t data_len) {
    neverc_tls_conn_t conn;
    memset(&conn, 0, sizeof(conn));
    if (!data || data_len == 0)
        return 0;

    size_t fragment_size = (size_t)(data[0] & 31u) + 1;
    size_t pos = 1;
    while (pos < data_len) {
        size_t remaining = data_len - pos;
        size_t chunk = remaining < fragment_size ?
                       remaining : fragment_size;
        if (nci_tls_append_handshake_bytes(
                &conn, data + pos, chunk) != 0)
            break;
        pos += chunk;

        for (;;) {
            const uint8_t *message = NULL;
            size_t message_len = 0;
            int available = nci_tls_next_handshake_message(
                &conn, &message, &message_len);
            if (available <= 0)
                break;
            if (!message || message_len < 4 ||
                nci_tls_consume_handshake_message(
                    &conn, message_len) != 0) {
                nci_tls_clear_handshake_buffer(&conn);
                return 0;
            }
        }
    }
    nci_tls_clear_handshake_buffer(&conn);
    return 0;
}

int neverc_tls_test_reject_ccs_after_handshake(void) {
    neverc_tcp_conn_t *reader = NULL;
    neverc_tcp_conn_t *writer = NULL;
    if (neverc_tcp_pipe(&reader, &writer) != 0 || !reader || !writer) {
        neverc_tcp_close(reader);
        neverc_tcp_close(writer);
        return -1;
    }

    neverc_tls_conn_t *conn = nci_tls_conn_new(reader, 1);
    if (!conn) {
        neverc_tcp_close(reader);
        neverc_tcp_close(writer);
        return -1;
    }
    conn->application_keys_active = 1;

    static const uint8_t ccs_record[] = {
        TLS_CT_CHANGE_CIPHER_SPEC, 0x03, 0x03, 0x00, 0x01, 0x01
    };
    if (neverc_tcp_write(writer, ccs_record, sizeof(ccs_record)) !=
        (int)sizeof(ccs_record)) {
        neverc_tls_close(conn);
        neverc_tcp_close(writer);
        return -1;
    }

    uint8_t inner_type = 0;
    uint8_t data[TLS_MAX_PLAINTEXT];
    size_t data_len = 0;
    int result = nci_tls_recv_decrypt(conn, &inner_type, data, &data_len);
    const char *reason = conn->failure_reason;
    neverc_tls_close(conn);
    neverc_tcp_close(writer);
    if (result == 0)
        return -1;
    if (!reason ||
        strstr(reason, "change_cipher_spec after handshake") == NULL)
        return -1;
    return 0;
}

int neverc_tls_test_discard_ccs_before_handshake(void) {
    neverc_tcp_conn_t *reader = NULL;
    neverc_tcp_conn_t *writer = NULL;
    if (neverc_tcp_pipe(&reader, &writer) != 0 || !reader || !writer) {
        neverc_tcp_close(reader);
        neverc_tcp_close(writer);
        return -1;
    }

    neverc_tls_conn_t *conn = nci_tls_conn_new(reader, 1);
    if (!conn) {
        neverc_tcp_close(reader);
        neverc_tcp_close(writer);
        return -1;
    }

    static const uint8_t ccs_then_handshake[] = {
        TLS_CT_CHANGE_CIPHER_SPEC, 0x03, 0x03, 0x00, 0x01, 0x01,
        TLS_CT_HANDSHAKE, 0x03, 0x03, 0x00, 0x06,
        TLS_HS_ENCRYPTED_EXT, 0, 0, 2, 0, 0
    };
    if (neverc_tcp_write(writer, ccs_then_handshake,
                         sizeof(ccs_then_handshake)) !=
        (int)sizeof(ccs_then_handshake)) {
        neverc_tls_close(conn);
        neverc_tcp_close(writer);
        return -1;
    }

    const uint8_t *message = NULL;
    size_t message_len = 0;
    int result = nci_tls_recv_plain_handshake_message(
        conn, TLS_HS_ENCRYPTED_EXT, &message, &message_len);
    int valid = result == 0 && message_len == 6 && message &&
                message[0] == TLS_HS_ENCRYPTED_EXT;
    neverc_tls_close(conn);
    neverc_tcp_close(writer);
    return valid ? 0 : -1;
}

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
        if (nci_tls_send_plain_record(
                conn, TLS_CT_HANDSHAKE,
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
        nci_tls_send_plain_record(conn, TLS_CT_ALERT, alert, sizeof(alert));
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

static int nci_tls_read_record_bytes(neverc_tls_conn_t *conn,
                                     uint8_t *output, size_t capacity) {
    if (conn->nonblocking_io) {
        neverc_net_result_t result = neverc_tcp_try_read(
            conn->tcp, output, capacity);
        if (result.status == NEVERC_NET_OK && result.transferred > 0)
            return (int)result.transferred;
        if (result.status == NEVERC_NET_WOULD_BLOCK)
            return TLS_RECORD_WOULD_BLOCK;
        return -1;
    }
    if (conn->read_context) {
        neverc_net_result_t result = neverc_tcp_read_context(
            conn->tcp, conn->read_context, output, capacity);
        return result.status == NEVERC_NET_OK
            ? (int)result.transferred
            : -1;
    }
    return neverc_tcp_read(conn->tcp, output, capacity);
}

int nci_tls_recv_record(neverc_tls_conn_t *conn, uint8_t *out_type,
                             uint8_t *out_data, size_t *out_len) {
    if (!conn || !conn->tcp || !out_type || !out_data || !out_len)
        return -1;

    /* Read from TCP until we have a complete record */
    while (conn->read_buf_len < TLS_RECORD_HEADER_SIZE) {
        int n = nci_tls_read_record_bytes(
            conn, conn->read_buf + conn->read_buf_len,
            sizeof(conn->read_buf) - conn->read_buf_len);
        if (n == TLS_RECORD_WOULD_BLOCK && conn->nonblocking_io)
            return NCI_TLS_WANT_READ;
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
    (void)record_version;
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
        int n = nci_tls_read_record_bytes(
            conn, conn->read_buf + conn->read_buf_len,
            sizeof(conn->read_buf) - conn->read_buf_len);
        if (n == TLS_RECORD_WOULD_BLOCK && conn->nonblocking_io)
            return NCI_TLS_WANT_READ;
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
        int record_result = nci_tls_recv_record(
            conn, &rec_type, rec_data, &rec_len);
        if (record_result != 0) return record_result;
        if (rec_type != TLS_CT_CHANGE_CIPHER_SPEC)
            break;
        if (rec_len != 1 || rec_data[0] != 1)
            return nci_tls_protocol_error(
                conn, TLS_ALERT_DECODE_ERROR,
                "malformed TLS change_cipher_spec record");
        /* RFC 8446 D.4: CCS is only a middlebox dummy before application
         * traffic keys. After that point it is unexpected_message. */
        if (conn->application_keys_active)
            return nci_tls_protocol_error(
                conn, TLS_ALERT_UNEXPECTED_MESSAGE,
                "TLS change_cipher_spec after handshake completion");
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

    if (rec_len <= TLS_AEAD_TAG_SIZE) {
        neverc_platform_secure_zero(nonce, sizeof(nonce));
        neverc_platform_secure_zero(aad, sizeof(aad));
        return nci_tls_protocol_error(
            conn, TLS_ALERT_BAD_RECORD_MAC,
            "TLS ciphertext is too short");
    }

    size_t ct_body_len = rec_len - TLS_AEAD_TAG_SIZE;
    uint8_t *plaintext = (uint8_t *)malloc(ct_body_len);
    if (!plaintext) {
        neverc_platform_secure_zero(nonce, sizeof(nonce));
        neverc_platform_secure_zero(aad, sizeof(aad));
        return nci_tls_protocol_error(
            conn, TLS_ALERT_INTERNAL_ERROR,
            "TLS record allocation failed");
    }

    int decrypt_ok = -1;
    if (keys->id == TLS_CIPHER_AES_128_GCM_SHA256) {
        uint8_t *tag = rec_data + ct_body_len;
        decrypt_ok = neverc_gcm_open(&keys->gcm, nonce,
                                       rec_data, ct_body_len,
                                       aad, 5, tag, plaintext);
    } else if (keys->id == TLS_CIPHER_CHACHA20_POLY1305_SHA256) {
        int ptlen = neverc_chacha20poly1305_open(plaintext, keys->key, nonce,
                                                   rec_data, rec_len,
                                                   aad, 5);
        decrypt_ok = (ptlen >= 0) ? 0 : -1;
        if (decrypt_ok == 0) ct_body_len = (size_t)ptlen;
    }
    neverc_platform_secure_zero(nonce, sizeof(nonce));
    neverc_platform_secure_zero(aad, sizeof(aad));

    if (decrypt_ok != 0) {
        nci_tls_secure_free(plaintext, ct_body_len);
        return nci_tls_protocol_error(
            conn, TLS_ALERT_BAD_RECORD_MAC,
            "TLS record authentication failed");
    }

    keys->seq++;

    /* RFC 8446 §5.4: InnerPlaintext is content + type + padding and MUST
     * not exceed 2^14 + 1. Check before stripping zeros; otherwise extra
     * padding can hide a record_overflow. */
    if (ct_body_len > TLS_MAX_PLAINTEXT + 1) {
        nci_tls_secure_free(plaintext, rec_len - TLS_AEAD_TAG_SIZE);
        return nci_tls_protocol_error(
            conn, TLS_ALERT_RECORD_OVERFLOW,
            "TLS inner plaintext exceeds the configured limit");
    }

    /* Remove padding and find inner content type (last non-zero byte) */
    while (ct_body_len > 0 && plaintext[ct_body_len - 1] == 0)
        ct_body_len--;
    if (ct_body_len == 0) {
        nci_tls_secure_free(plaintext, rec_len - TLS_AEAD_TAG_SIZE);
        return nci_tls_protocol_error(
            conn, TLS_ALERT_UNEXPECTED_MESSAGE,
            "TLS inner plaintext has no content type");
    }
    *out_inner_type = plaintext[ct_body_len - 1];
    ct_body_len--;
    if (*out_inner_type != TLS_CT_ALERT &&
        *out_inner_type != TLS_CT_HANDSHAKE &&
        *out_inner_type != TLS_CT_APPLICATION_DATA) {
        nci_tls_secure_free(plaintext, rec_len - TLS_AEAD_TAG_SIZE);
        return nci_tls_protocol_error(
            conn, TLS_ALERT_UNEXPECTED_MESSAGE,
            "TLS inner plaintext has an invalid content type");
    }
    memcpy(out_data, plaintext, ct_body_len);
    *out_len = ct_body_len;

    nci_tls_secure_free(plaintext, rec_len - TLS_AEAD_TAG_SIZE);
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
        int record_result = nci_tls_recv_record(
            conn, &record_type, record_data, &record_len);
        if (record_result != 0) return record_result;
        if (record_type == TLS_CT_ALERT)
            return nci_tls_error(
                conn, "peer sent an alert during TLS handshake");
        if (record_type == TLS_CT_CHANGE_CIPHER_SPEC) {
            /* RFC 8446 D.4: a 1-byte 0x01 CCS MUST be discarded after the
             * first ClientHello and before the peer Finished. */
            if (record_len != 1 || record_data[0] != 1)
                return nci_tls_protocol_error(
                    conn, TLS_ALERT_DECODE_ERROR,
                    "malformed TLS change_cipher_spec record");
            if (conn->application_keys_active)
                return nci_tls_protocol_error(
                    conn, TLS_ALERT_UNEXPECTED_MESSAGE,
                    "TLS change_cipher_spec after handshake completion");
            if (++conn->non_advancing_records >
                TLS_MAX_NON_ADVANCING_RECORDS)
                return nci_tls_protocol_error(
                    conn, TLS_ALERT_UNEXPECTED_MESSAGE,
                    "too many non-advancing TLS records");
            continue;
        }
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
