/*
 * QUIC Transport Parameters (RFC 9000 §18)
 *
 * Transport parameters are exchanged during the TLS handshake to communicate
 * per-connection settings. They are encoded as a sequence of:
 *   Parameter ID (varint) | Length (varint) | Value (Length bytes)
 */

#include "_quic_internal.h"

#include <string.h>

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
#define QUIC_TP_MAX_PARAMETERS                 64
#define QUIC_VARINT_MAX ((UINT64_C(1) << 62) - 1)

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

static int decode_tp_varint(const uint8_t *value, size_t length,
                            uint64_t *output) {
    size_t consumed = 0;
    return neverc_quic_varint_decode(
               value, length, output, &consumed) == 0 &&
           consumed == length ? 0 : -1;
}

uint64_t neverc_quic_effective_idle_timeout_ms(uint64_t local_ms,
                                              uint64_t peer_ms) {
    if (peer_ms == 0) return local_ms;
    if (local_ms == 0) return peer_ms;
    return local_ms < peer_ms ? local_ms : peer_ms;
}

int neverc_quic_transport_params_decode(const uint8_t *buf, size_t len,
                                         quic_transport_params_t *tp) {
    if (!tp || (len > 0 && !buf)) return -1;
    /* RFC 9000 §18.2: omitted initial_max_* and max_idle_timeout are 0.
     * Substituting local send defaults would grant flow-control credit the
     * peer never advertised. Only parameters with non-zero RFC defaults
     * are filled in here. */
    memset(tp, 0, sizeof(*tp));
    tp->max_udp_payload_size = 65527;
    tp->ack_delay_exponent = 3;
    tp->max_ack_delay = 25;
    tp->active_connection_id_limit = 2;

    const uint8_t *p = buf;
    size_t rem = len;
    uint64_t seen[QUIC_TP_MAX_PARAMETERS];
    size_t seen_count = 0;

    while (rem > 0) {
        uint64_t param_id, param_len;
        size_t consumed;

        if (neverc_quic_varint_decode(p, rem, &param_id, &consumed) != 0)
            return -1;
        p += consumed; rem -= consumed;

        for (size_t i = 0; i < seen_count; i++)
            if (seen[i] == param_id) return -1;
        if (seen_count == QUIC_TP_MAX_PARAMETERS) return -1;
        seen[seen_count++] = param_id;

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
            tp->has_original_dcid = 1;
            break;
        case QUIC_TP_INITIAL_SCID:
            if (vlen > 20) return -1;
            memcpy(tp->initial_scid, val, vlen);
            tp->initial_scid_len = (uint8_t)vlen;
            tp->has_initial_scid = 1;
            break;
        case QUIC_TP_RETRY_SCID:
            if (vlen > 20) return -1;
            memcpy(tp->retry_scid, val, vlen);
            tp->retry_scid_len = (uint8_t)vlen;
            tp->has_retry_scid = 1;
            break;
        case QUIC_TP_MAX_IDLE_TIMEOUT: {
            uint64_t v;
            if (decode_tp_varint(val, vlen, &v) != 0) return -1;
            tp->max_idle_timeout = v;
            break;
        }
        case QUIC_TP_MAX_UDP_PAYLOAD_SIZE: {
            uint64_t v;
            if (decode_tp_varint(val, vlen, &v) != 0) return -1;
            if (v < 1200 || v > 65527) return -1;
            tp->max_udp_payload_size = v;
            break;
        }
        case QUIC_TP_INITIAL_MAX_DATA: {
            uint64_t v;
            if (decode_tp_varint(val, vlen, &v) != 0) return -1;
            tp->initial_max_data = v;
            break;
        }
        case QUIC_TP_INITIAL_MAX_STREAM_DATA_BIDI_LOCAL: {
            uint64_t v;
            if (decode_tp_varint(val, vlen, &v) != 0) return -1;
            tp->initial_max_stream_data_bidi_local = v;
            break;
        }
        case QUIC_TP_INITIAL_MAX_STREAM_DATA_BIDI_REMOTE: {
            uint64_t v;
            if (decode_tp_varint(val, vlen, &v) != 0) return -1;
            tp->initial_max_stream_data_bidi_remote = v;
            break;
        }
        case QUIC_TP_INITIAL_MAX_STREAM_DATA_UNI: {
            uint64_t v;
            if (decode_tp_varint(val, vlen, &v) != 0) return -1;
            tp->initial_max_stream_data_uni = v;
            break;
        }
        case QUIC_TP_INITIAL_MAX_STREAMS_BIDI: {
            uint64_t v;
            if (decode_tp_varint(val, vlen, &v) != 0 ||
                v > (UINT64_C(1) << 60)) return -1;
            tp->initial_max_streams_bidi = v;
            break;
        }
        case QUIC_TP_INITIAL_MAX_STREAMS_UNI: {
            uint64_t v;
            if (decode_tp_varint(val, vlen, &v) != 0 ||
                v > (UINT64_C(1) << 60)) return -1;
            tp->initial_max_streams_uni = v;
            break;
        }
        case QUIC_TP_ACK_DELAY_EXPONENT: {
            uint64_t v;
            if (decode_tp_varint(val, vlen, &v) != 0) return -1;
            if (v > 20) return -1;
            tp->ack_delay_exponent = v;
            break;
        }
        case QUIC_TP_MAX_ACK_DELAY: {
            uint64_t v;
            if (decode_tp_varint(val, vlen, &v) != 0) return -1;
            if (v >= 16384) return -1;
            tp->max_ack_delay = v;
            break;
        }
        case QUIC_TP_ACTIVE_CONNECTION_ID_LIMIT: {
            uint64_t v;
            if (decode_tp_varint(val, vlen, &v) != 0) return -1;
            /* RFC 9000 §18.2: values below 2 are invalid; no upper bound. */
            if (v < 2) return -1;
            tp->active_connection_id_limit = v;
            break;
        }
        case QUIC_TP_PREFERRED_ADDRESS: {
            /* RFC 9000 §18.2: IPv4+port + IPv6+port + CID length 1..20 +
             * CID + 16-byte stateless reset token. Presence is role-checked. */
            if (vlen < 42 || vlen > 61) return -1;
            uint8_t cid_len = val[24];
            if (cid_len < 1 || cid_len > 20 ||
                vlen != (size_t)41 + cid_len)
                return -1;
            tp->has_preferred_address = 1;
            break;
        }
        case QUIC_TP_DISABLE_ACTIVE_MIGRATION:
            if (vlen != 0) return -1;
            tp->disable_active_migration = 1;
            break;
        case QUIC_TP_STATELESS_RESET_TOKEN:
            if (vlen != 16) return -1;
            memcpy(tp->stateless_reset_token, val, 16);
            tp->has_stateless_reset_token = 1;
            break;
        case QUIC_TP_MAX_DATAGRAM_FRAME_SIZE: {
            uint64_t v;
            if (decode_tp_varint(val, vlen, &v) != 0) return -1;
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

int neverc_quic_transport_params_client_forbidden(
    const quic_transport_params_t *tp) {
    if (!tp) return -1;
    /* RFC 9000 §18.2: original_destination_connection_id, preferred_address,
     * retry_source_connection_id, and stateless_reset_token are server-only. */
    return tp->has_original_dcid || tp->has_preferred_address ||
           tp->has_retry_scid || tp->has_stateless_reset_token ? -1 : 0;
}

int neverc_quic_transport_params_require_client(
    const quic_transport_params_t *tp) {
    /* RFC 9000 §7.3 / §18.2: clients MUST send initial_source_connection_id
     * and MUST NOT send the server-only parameters. */
    if (!tp || !tp->has_initial_scid) return -1;
    return neverc_quic_transport_params_client_forbidden(tp);
}

int neverc_quic_transport_params_require_server(
    const quic_transport_params_t *tp) {
    /* This stack never issues Retry, so retry_source_connection_id is
     * TRANSPORT_PARAMETER_ERROR from a server (RFC 9000 §18.2). */
    if (!tp || !tp->has_original_dcid || !tp->has_initial_scid ||
        tp->has_retry_scid)
        return -1;
    return 0;
}

static int write_tp_varint(uint8_t *buf, size_t cap, size_t *pos,
                            uint64_t param_id, uint64_t value) {
    if (!buf || !pos || param_id > QUIC_VARINT_MAX ||
        value > QUIC_VARINT_MAX)
        return -1;
    size_t vlen = neverc_quic_varint_len(value);
    size_t need = neverc_quic_varint_len(param_id) +
                  neverc_quic_varint_len(vlen) + vlen;
    if (*pos > cap || need > cap - *pos) return -1;

    size_t w;
    neverc_quic_varint_encode(param_id, buf + *pos, cap - *pos, &w); *pos += w;
    neverc_quic_varint_encode(vlen, buf + *pos, cap - *pos, &w); *pos += w;
    neverc_quic_varint_encode(value, buf + *pos, cap - *pos, &w); *pos += w;
    return 0;
}

static int write_tp_bytes(uint8_t *buf, size_t cap, size_t *pos,
                           uint64_t param_id, const uint8_t *data, size_t dlen) {
    if (!buf || !pos || (dlen && !data) ||
        param_id > QUIC_VARINT_MAX || dlen > QUIC_VARINT_MAX ||
        dlen > SIZE_MAX - 16)
        return -1;
    size_t need = neverc_quic_varint_len(param_id) +
                  neverc_quic_varint_len(dlen) + dlen;
    if (*pos > cap || need > cap - *pos) return -1;

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
    if (written) *written = 0;
    if (!tp || !buf || !written || tp->original_dcid_len > 20 ||
        tp->initial_scid_len > 20 || tp->retry_scid_len > 20 ||
        tp->max_udp_payload_size < 1200 ||
        tp->max_udp_payload_size > 65527 ||
        tp->initial_max_streams_bidi > (UINT64_C(1) << 60) ||
        tp->initial_max_streams_uni > (UINT64_C(1) << 60) ||
        tp->ack_delay_exponent > 20 || tp->max_ack_delay >= 16384 ||
        tp->active_connection_id_limit < 2 ||
        tp->active_connection_id_limit > 8)
        return -1;
    size_t pos = 0;

    if (tp->has_original_dcid || tp->original_dcid_len > 0)
        if (write_tp_bytes(buf, cap, &pos, QUIC_TP_ORIGINAL_DCID,
                            tp->original_dcid, tp->original_dcid_len) != 0) return -1;

    if (tp->has_initial_scid || tp->initial_scid_len > 0)
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
        if (write_tp_bytes(buf, cap, &pos,
                           QUIC_TP_DISABLE_ACTIVE_MIGRATION,
                           NULL, 0) != 0) return -1;
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
