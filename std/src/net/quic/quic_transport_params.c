/*
 * QUIC Transport Parameters (RFC 9000 §18)
 *
 * Transport parameters are exchanged during the TLS handshake to communicate
 * per-connection settings. They are encoded as a sequence of:
 *   Parameter ID (varint) | Length (varint) | Value (Length bytes)
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

extern int neverc_quic_varint_decode(const uint8_t *buf, size_t len,
                                      uint64_t *value, size_t *consumed);
extern int neverc_quic_varint_encode(uint64_t value, uint8_t *buf, size_t cap,
                                      size_t *written);
extern size_t neverc_quic_varint_len(uint64_t value);

/* Transport parameter IDs (RFC 9000 §18.2) */
#define QUIC_TP_ORIGINAL_DCID                  0x00
#define QUIC_TP_MAX_IDLE_TIMEOUT               0x01
#define QUIC_TP_STATELESS_RESET_TOKEN          0x02
#define QUIC_TP_MAX_UDP_PAYLOAD_SIZE           0x03
#define QUIC_TP_INITIAL_MAX_DATA               0x04
#define QUIC_TP_INITIAL_MAX_STREAM_DATA_BIDI_LOCAL  0x05
#define QUIC_TP_INITIAL_MAX_STREAM_DATA_BIDI_REMOTE 0x06
#define QUIC_TP_INITIAL_MAX_STREAM_DATA_UNI    0x07
#define QUIC_TP_INITIAL_MAX_STREAMS_BIDI       0x08
#define QUIC_TP_INITIAL_MAX_STREAMS_UNI        0x09
#define QUIC_TP_ACK_DELAY_EXPONENT             0x0a
#define QUIC_TP_MAX_ACK_DELAY                  0x0b
#define QUIC_TP_DISABLE_ACTIVE_MIGRATION       0x0c
#define QUIC_TP_PREFERRED_ADDRESS              0x0d
#define QUIC_TP_ACTIVE_CONNECTION_ID_LIMIT     0x0e
#define QUIC_TP_INITIAL_SCID                   0x0f
#define QUIC_TP_RETRY_SCID                     0x10
#define QUIC_TP_MAX_DATAGRAM_FRAME_SIZE        0x20  /* RFC 9221 */

typedef struct {
    /* Connection IDs */
    uint8_t  original_dcid[20];
    uint8_t  original_dcid_len;
    uint8_t  initial_scid[20];
    uint8_t  initial_scid_len;
    uint8_t  retry_scid[20];
    uint8_t  retry_scid_len;
    int      has_retry_scid;

    /* Timeouts and limits */
    uint64_t max_idle_timeout;           /* ms, 0 = disabled */
    uint64_t max_udp_payload_size;       /* default 65527 */
    uint64_t initial_max_data;           /* connection-level flow control */
    uint64_t initial_max_stream_data_bidi_local;
    uint64_t initial_max_stream_data_bidi_remote;
    uint64_t initial_max_stream_data_uni;
    uint64_t initial_max_streams_bidi;
    uint64_t initial_max_streams_uni;
    uint64_t ack_delay_exponent;         /* default 3 */
    uint64_t max_ack_delay;              /* ms, default 25 */
    uint64_t active_connection_id_limit; /* default 2 */
    uint64_t max_datagram_frame_size;    /* 0 = datagrams not supported */

    /* Flags */
    int      disable_active_migration;

    /* Stateless reset token (server only) */
    uint8_t  stateless_reset_token[16];
    int      has_stateless_reset_token;
} quic_transport_params_t;

void neverc_quic_transport_params_default(quic_transport_params_t *tp) {
    memset(tp, 0, sizeof(*tp));
    tp->max_udp_payload_size = 65527;
    tp->ack_delay_exponent = 3;
    tp->max_ack_delay = 25;
    tp->active_connection_id_limit = 2;
    tp->initial_max_data = 10 * 1024 * 1024;           /* 10MB */
    tp->initial_max_stream_data_bidi_local = 1024 * 1024;  /* 1MB */
    tp->initial_max_stream_data_bidi_remote = 1024 * 1024;
    tp->initial_max_stream_data_uni = 1024 * 1024;
    tp->initial_max_streams_bidi = 100;
    tp->initial_max_streams_uni = 100;
    tp->max_idle_timeout = 30000;
}

int neverc_quic_transport_params_decode(const uint8_t *buf, size_t len,
                                         quic_transport_params_t *tp) {
    neverc_quic_transport_params_default(tp);

    const uint8_t *p = buf;
    size_t rem = len;

    while (rem > 0) {
        uint64_t param_id, param_len;
        size_t consumed;

        if (neverc_quic_varint_decode(p, rem, &param_id, &consumed) != 0)
            return -1;
        p += consumed; rem -= consumed;

        if (neverc_quic_varint_decode(p, rem, &param_len, &consumed) != 0)
            return -1;
        p += consumed; rem -= consumed;

        if (param_len > rem) return -1;
        const uint8_t *val = p;
        size_t vlen = (size_t)param_len;

        switch (param_id) {
        case QUIC_TP_ORIGINAL_DCID:
            if (vlen > 20) return -1;
            memcpy(tp->original_dcid, val, vlen);
            tp->original_dcid_len = (uint8_t)vlen;
            break;
        case QUIC_TP_INITIAL_SCID:
            if (vlen > 20) return -1;
            memcpy(tp->initial_scid, val, vlen);
            tp->initial_scid_len = (uint8_t)vlen;
            break;
        case QUIC_TP_RETRY_SCID:
            if (vlen > 20) return -1;
            memcpy(tp->retry_scid, val, vlen);
            tp->retry_scid_len = (uint8_t)vlen;
            tp->has_retry_scid = 1;
            break;
        case QUIC_TP_MAX_IDLE_TIMEOUT: {
            uint64_t v; size_t c;
            if (neverc_quic_varint_decode(val, vlen, &v, &c) != 0) return -1;
            tp->max_idle_timeout = v;
            break;
        }
        case QUIC_TP_MAX_UDP_PAYLOAD_SIZE: {
            uint64_t v; size_t c;
            if (neverc_quic_varint_decode(val, vlen, &v, &c) != 0) return -1;
            if (v < 1200) return -1;
            tp->max_udp_payload_size = v;
            break;
        }
        case QUIC_TP_INITIAL_MAX_DATA: {
            uint64_t v; size_t c;
            if (neverc_quic_varint_decode(val, vlen, &v, &c) != 0) return -1;
            tp->initial_max_data = v;
            break;
        }
        case QUIC_TP_INITIAL_MAX_STREAM_DATA_BIDI_LOCAL: {
            uint64_t v; size_t c;
            if (neverc_quic_varint_decode(val, vlen, &v, &c) != 0) return -1;
            tp->initial_max_stream_data_bidi_local = v;
            break;
        }
        case QUIC_TP_INITIAL_MAX_STREAM_DATA_BIDI_REMOTE: {
            uint64_t v; size_t c;
            if (neverc_quic_varint_decode(val, vlen, &v, &c) != 0) return -1;
            tp->initial_max_stream_data_bidi_remote = v;
            break;
        }
        case QUIC_TP_INITIAL_MAX_STREAM_DATA_UNI: {
            uint64_t v; size_t c;
            if (neverc_quic_varint_decode(val, vlen, &v, &c) != 0) return -1;
            tp->initial_max_stream_data_uni = v;
            break;
        }
        case QUIC_TP_INITIAL_MAX_STREAMS_BIDI: {
            uint64_t v; size_t c;
            if (neverc_quic_varint_decode(val, vlen, &v, &c) != 0) return -1;
            tp->initial_max_streams_bidi = v;
            break;
        }
        case QUIC_TP_INITIAL_MAX_STREAMS_UNI: {
            uint64_t v; size_t c;
            if (neverc_quic_varint_decode(val, vlen, &v, &c) != 0) return -1;
            tp->initial_max_streams_uni = v;
            break;
        }
        case QUIC_TP_ACK_DELAY_EXPONENT: {
            uint64_t v; size_t c;
            if (neverc_quic_varint_decode(val, vlen, &v, &c) != 0) return -1;
            if (v > 20) return -1;
            tp->ack_delay_exponent = v;
            break;
        }
        case QUIC_TP_MAX_ACK_DELAY: {
            uint64_t v; size_t c;
            if (neverc_quic_varint_decode(val, vlen, &v, &c) != 0) return -1;
            if (v >= 16384) return -1;
            tp->max_ack_delay = v;
            break;
        }
        case QUIC_TP_ACTIVE_CONNECTION_ID_LIMIT: {
            uint64_t v; size_t c;
            if (neverc_quic_varint_decode(val, vlen, &v, &c) != 0) return -1;
            if (v < 2) return -1;
            tp->active_connection_id_limit = v;
            break;
        }
        case QUIC_TP_DISABLE_ACTIVE_MIGRATION:
            tp->disable_active_migration = 1;
            break;
        case QUIC_TP_STATELESS_RESET_TOKEN:
            if (vlen != 16) return -1;
            memcpy(tp->stateless_reset_token, val, 16);
            tp->has_stateless_reset_token = 1;
            break;
        case QUIC_TP_MAX_DATAGRAM_FRAME_SIZE: {
            uint64_t v; size_t c;
            if (neverc_quic_varint_decode(val, vlen, &v, &c) != 0) return -1;
            tp->max_datagram_frame_size = v;
            break;
        }
        default:
            /* Unknown parameter — skip (MUST ignore per RFC 9000 §18.1) */
            break;
        }

        p += vlen;
        rem -= vlen;
    }

    return 0;
}

static int write_tp_varint(uint8_t *buf, size_t cap, size_t *pos,
                            uint64_t param_id, uint64_t value) {
    size_t vlen = neverc_quic_varint_len(value);
    size_t need = neverc_quic_varint_len(param_id) +
                  neverc_quic_varint_len(vlen) + vlen;
    if (*pos + need > cap) return -1;

    size_t w;
    neverc_quic_varint_encode(param_id, buf + *pos, cap - *pos, &w); *pos += w;
    neverc_quic_varint_encode(vlen, buf + *pos, cap - *pos, &w); *pos += w;
    neverc_quic_varint_encode(value, buf + *pos, cap - *pos, &w); *pos += w;
    return 0;
}

static int write_tp_bytes(uint8_t *buf, size_t cap, size_t *pos,
                           uint64_t param_id, const uint8_t *data, size_t dlen) {
    size_t need = neverc_quic_varint_len(param_id) +
                  neverc_quic_varint_len(dlen) + dlen;
    if (*pos + need > cap) return -1;

    size_t w;
    neverc_quic_varint_encode(param_id, buf + *pos, cap - *pos, &w); *pos += w;
    neverc_quic_varint_encode(dlen, buf + *pos, cap - *pos, &w); *pos += w;
    memcpy(buf + *pos, data, dlen);
    *pos += dlen;
    return 0;
}

int neverc_quic_transport_params_encode(const quic_transport_params_t *tp,
                                         uint8_t *buf, size_t cap,
                                         size_t *written) {
    size_t pos = 0;

    if (tp->original_dcid_len > 0)
        if (write_tp_bytes(buf, cap, &pos, QUIC_TP_ORIGINAL_DCID,
                            tp->original_dcid, tp->original_dcid_len) != 0) return -1;

    if (tp->initial_scid_len > 0)
        if (write_tp_bytes(buf, cap, &pos, QUIC_TP_INITIAL_SCID,
                            tp->initial_scid, tp->initial_scid_len) != 0) return -1;

    if (tp->has_retry_scid)
        if (write_tp_bytes(buf, cap, &pos, QUIC_TP_RETRY_SCID,
                            tp->retry_scid, tp->retry_scid_len) != 0) return -1;

    if (tp->max_idle_timeout > 0)
        if (write_tp_varint(buf, cap, &pos, QUIC_TP_MAX_IDLE_TIMEOUT,
                             tp->max_idle_timeout) != 0) return -1;

    if (tp->max_udp_payload_size != 65527)
        if (write_tp_varint(buf, cap, &pos, QUIC_TP_MAX_UDP_PAYLOAD_SIZE,
                             tp->max_udp_payload_size) != 0) return -1;

    if (write_tp_varint(buf, cap, &pos, QUIC_TP_INITIAL_MAX_DATA,
                         tp->initial_max_data) != 0) return -1;

    if (write_tp_varint(buf, cap, &pos, QUIC_TP_INITIAL_MAX_STREAM_DATA_BIDI_LOCAL,
                         tp->initial_max_stream_data_bidi_local) != 0) return -1;

    if (write_tp_varint(buf, cap, &pos, QUIC_TP_INITIAL_MAX_STREAM_DATA_BIDI_REMOTE,
                         tp->initial_max_stream_data_bidi_remote) != 0) return -1;

    if (write_tp_varint(buf, cap, &pos, QUIC_TP_INITIAL_MAX_STREAM_DATA_UNI,
                         tp->initial_max_stream_data_uni) != 0) return -1;

    if (write_tp_varint(buf, cap, &pos, QUIC_TP_INITIAL_MAX_STREAMS_BIDI,
                         tp->initial_max_streams_bidi) != 0) return -1;

    if (write_tp_varint(buf, cap, &pos, QUIC_TP_INITIAL_MAX_STREAMS_UNI,
                         tp->initial_max_streams_uni) != 0) return -1;

    if (tp->ack_delay_exponent != 3)
        if (write_tp_varint(buf, cap, &pos, QUIC_TP_ACK_DELAY_EXPONENT,
                             tp->ack_delay_exponent) != 0) return -1;

    if (tp->max_ack_delay != 25)
        if (write_tp_varint(buf, cap, &pos, QUIC_TP_MAX_ACK_DELAY,
                             tp->max_ack_delay) != 0) return -1;

    if (tp->active_connection_id_limit != 2)
        if (write_tp_varint(buf, cap, &pos, QUIC_TP_ACTIVE_CONNECTION_ID_LIMIT,
                             tp->active_connection_id_limit) != 0) return -1;

    if (tp->disable_active_migration) {
        size_t w;
        neverc_quic_varint_encode(QUIC_TP_DISABLE_ACTIVE_MIGRATION, buf + pos, cap - pos, &w); pos += w;
        neverc_quic_varint_encode(0, buf + pos, cap - pos, &w); pos += w;
    }

    if (tp->has_stateless_reset_token)
        if (write_tp_bytes(buf, cap, &pos, QUIC_TP_STATELESS_RESET_TOKEN,
                            tp->stateless_reset_token, 16) != 0) return -1;

    if (tp->max_datagram_frame_size > 0)
        if (write_tp_varint(buf, cap, &pos, QUIC_TP_MAX_DATAGRAM_FRAME_SIZE,
                             tp->max_datagram_frame_size) != 0) return -1;

    *written = pos;
    return 0;
}
