/*
 * QUIC Frame Codec (RFC 9000 §19)
 *
 * All QUIC packets (except Version Negotiation and Retry) contain one or more
 * frames. This module provides parsing and serialization for all frame types.
 */

#include "_quic_internal.h"

#include <string.h>
#include <stdlib.h>

/* ======================================================================
 * Frame Type Constants (RFC 9000 §19.1)
 * ====================================================================== */

#define QUIC_FRAME_PADDING              0x00
#define QUIC_FRAME_PING                 0x01
#define QUIC_FRAME_ACK                  0x02
#define QUIC_FRAME_ACK_ECN              0x03
#define QUIC_FRAME_RESET_STREAM         0x04
#define QUIC_FRAME_STOP_SENDING         0x05
#define QUIC_FRAME_CRYPTO               0x06
#define QUIC_FRAME_NEW_TOKEN            0x07
#define QUIC_FRAME_STREAM_BASE          0x08  /* 0x08..0x0f */
#define QUIC_FRAME_MAX_DATA             0x10
#define QUIC_FRAME_MAX_STREAM_DATA      0x11
#define QUIC_FRAME_MAX_STREAMS_BIDI     0x12
#define QUIC_FRAME_MAX_STREAMS_UNI      0x13
#define QUIC_FRAME_DATA_BLOCKED         0x14
#define QUIC_FRAME_STREAM_DATA_BLOCKED  0x15
#define QUIC_FRAME_STREAMS_BLOCKED_BIDI 0x16
#define QUIC_FRAME_STREAMS_BLOCKED_UNI  0x17
#define QUIC_FRAME_NEW_CONNECTION_ID     0x18
#define QUIC_FRAME_RETIRE_CONNECTION_ID  0x19
#define QUIC_FRAME_PATH_CHALLENGE       0x1a
#define QUIC_FRAME_PATH_RESPONSE        0x1b
#define QUIC_FRAME_CONNECTION_CLOSE     0x1c
#define QUIC_FRAME_CONNECTION_CLOSE_APP 0x1d
#define QUIC_FRAME_HANDSHAKE_DONE       0x1e
#define QUIC_FRAME_DATAGRAM             0x30  /* RFC 9221 */
#define QUIC_FRAME_DATAGRAM_LEN         0x31  /* RFC 9221, with length */

/* Stream frame flag bits */
#define QUIC_STREAM_FLAG_OFF 0x04
#define QUIC_STREAM_FLAG_LEN 0x02
#define QUIC_STREAM_FLAG_FIN 0x01
#define QUIC_VARINT_MAX ((UINT64_C(1) << 62) - 1)
#define QUIC_MAX_ACK_RANGES 256

/* ======================================================================
 * Encryption-level permission (RFC 9000 §12.4)
 * ====================================================================== */

int neverc_quic_frame_allowed(uint64_t frame_type, quic_enc_level_t level) {
    int initial_or_handshake = level == QUIC_ENC_INITIAL ||
                               level == QUIC_ENC_HANDSHAKE;
    int application = level == QUIC_ENC_APPLICATION;
    int early_or_app = application || level == QUIC_ENC_EARLY_DATA;

    if (frame_type == QUIC_FRAME_PADDING || frame_type == QUIC_FRAME_PING)
        return 1;
    if (frame_type == QUIC_FRAME_ACK || frame_type == QUIC_FRAME_ACK_ECN ||
        frame_type == QUIC_FRAME_CRYPTO)
        return initial_or_handshake || application;
    if (frame_type == QUIC_FRAME_CONNECTION_CLOSE)
        return 1;
    if (frame_type == QUIC_FRAME_CONNECTION_CLOSE_APP)
        return early_or_app;
    if (frame_type == QUIC_FRAME_HANDSHAKE_DONE ||
        frame_type == QUIC_FRAME_NEW_TOKEN)
        return application;
    if (frame_type == QUIC_FRAME_RESET_STREAM ||
        frame_type == QUIC_FRAME_STOP_SENDING ||
        frame_type == QUIC_FRAME_MAX_DATA ||
        frame_type == QUIC_FRAME_MAX_STREAM_DATA ||
        frame_type == QUIC_FRAME_MAX_STREAMS_BIDI ||
        frame_type == QUIC_FRAME_MAX_STREAMS_UNI ||
        frame_type == QUIC_FRAME_DATA_BLOCKED ||
        frame_type == QUIC_FRAME_STREAM_DATA_BLOCKED ||
        frame_type == QUIC_FRAME_STREAMS_BLOCKED_BIDI ||
        frame_type == QUIC_FRAME_STREAMS_BLOCKED_UNI ||
        frame_type == QUIC_FRAME_NEW_CONNECTION_ID ||
        frame_type == QUIC_FRAME_RETIRE_CONNECTION_ID ||
        frame_type == QUIC_FRAME_DATAGRAM ||
        frame_type == QUIC_FRAME_DATAGRAM_LEN)
        return early_or_app;
    /* RFC 9000 Table 3: PATH_CHALLENGE / PATH_RESPONSE are 1-RTT only. */
    if (frame_type == QUIC_FRAME_PATH_CHALLENGE ||
        frame_type == QUIC_FRAME_PATH_RESPONSE)
        return application;
    if (frame_type >= QUIC_FRAME_STREAM_BASE &&
        frame_type <= QUIC_FRAME_STREAM_BASE + 7U)
        return early_or_app;
    return 0;
}

/* RFC 9000 §12.4: PADDING is a single 0x00 byte, not a longer varint. */
int neverc_quic_frame_type_encoding_ok(uint64_t frame_type, size_t encoded_len) {
    if (encoded_len < 1) return -1;
    if (frame_type == QUIC_FRAME_PADDING && encoded_len != 1) return -1;
    return 0;
}

/* ======================================================================
 * Frame Parsing
 * ====================================================================== */

static int consume_varint(const uint8_t **buf, size_t *rem, uint64_t *val) {
    size_t consumed;
    if (neverc_quic_varint_decode(*buf, *rem, val, &consumed) != 0)
        return -1;
    *buf += consumed;
    *rem -= consumed;
    return 0;
}

/* RFC 9000 §19.6 / §19.8: offset + length MUST NOT exceed 2^62-1. */
static int quic_end_offset_ok(uint64_t offset, uint64_t len) {
    return offset <= QUIC_VARINT_MAX && len <= QUIC_VARINT_MAX - offset;
}

int neverc_quic_parse_crypto_frame(const uint8_t *buf, size_t len,
                                    quic_frame_crypto_t *out, size_t *consumed) {
    if (!buf || !out || !consumed) return -1;
    const uint8_t *p = buf;
    size_t rem = len;

    /* Skip frame type (0x06) */
    uint64_t ftype;
    if (consume_varint(&p, &rem, &ftype) != 0) return -1;
    if (ftype != QUIC_FRAME_CRYPTO) return -1;

    /* Offset */
    if (consume_varint(&p, &rem, &out->offset) != 0) return -1;

    /* Length */
    uint64_t dlen;
    if (consume_varint(&p, &rem, &dlen) != 0) return -1;
    if (dlen > rem || !quic_end_offset_ok(out->offset, dlen)) return -1;

    out->data = p;
    out->data_len = (size_t)dlen;
    p += (size_t)dlen;
    rem -= (size_t)dlen;

    *consumed = (size_t)(p - buf);
    return 0;
}

int neverc_quic_parse_stream_frame(const uint8_t *buf, size_t len,
                                    quic_frame_stream_t *out, size_t *consumed) {
    if (!buf || !out || !consumed || len < 1) return -1;

    /* RFC 9000 §12.4 / §16: STREAM type 0x08..0x0f is a varint. A
     * non-minimal encoding (e.g. 0x40 0x0a) must still recover OFF/LEN/FIN
     * from the decoded type, not the first wire byte. */
    const uint8_t *p = buf;
    size_t rem = len;
    uint64_t ftype;
    if (consume_varint(&p, &rem, &ftype) != 0) return -1;
    if (ftype < QUIC_FRAME_STREAM_BASE ||
        ftype > QUIC_FRAME_STREAM_BASE + 7U)
        return -1;

    int has_off = (ftype & QUIC_STREAM_FLAG_OFF) != 0;
    int has_len = (ftype & QUIC_STREAM_FLAG_LEN) != 0;
    out->fin = (ftype & QUIC_STREAM_FLAG_FIN) != 0;

    /* Stream ID */
    if (consume_varint(&p, &rem, &out->stream_id) != 0) return -1;

    /* Offset (optional) */
    if (has_off) {
        if (consume_varint(&p, &rem, &out->offset) != 0) return -1;
    } else {
        out->offset = 0;
    }

    /* Length (optional — if absent, extends to end of packet) */
    if (has_len) {
        uint64_t dlen;
        if (consume_varint(&p, &rem, &dlen) != 0) return -1;
        if (dlen > rem || !quic_end_offset_ok(out->offset, dlen)) return -1;
        out->data = p;
        out->data_len = (size_t)dlen;
        p += (size_t)dlen;
    } else {
        if (!quic_end_offset_ok(out->offset, (uint64_t)rem)) return -1;
        out->data = p;
        out->data_len = rem;
        p += rem;
    }

    *consumed = (size_t)(p - buf);
    return 0;
}

int neverc_quic_parse_ack_frame(const uint8_t *buf, size_t len,
                                 quic_frame_ack_t *out, size_t *consumed) {
    const uint8_t *p = buf;
    size_t rem = len;
    if (!buf || !out || !consumed) return -1;
    out->ranges = NULL;
    out->nranges = 0;

    uint64_t ftype;
    if (consume_varint(&p, &rem, &ftype) != 0) return -1;
    if (ftype != QUIC_FRAME_ACK && ftype != QUIC_FRAME_ACK_ECN) return -1;

    int has_ecn = (ftype == QUIC_FRAME_ACK_ECN);

    /* Largest Acknowledged */
    if (consume_varint(&p, &rem, &out->largest_acked) != 0) return -1;

    /* ACK Delay */
    if (consume_varint(&p, &rem, &out->ack_delay) != 0) return -1;

    /* ACK Range Count */
    uint64_t range_count;
    if (consume_varint(&p, &rem, &range_count) != 0) return -1;

    /* First ACK Range */
    uint64_t first_range;
    if (consume_varint(&p, &rem, &first_range) != 0) return -1;

    if (first_range > out->largest_acked) return -1;

    if (range_count >= QUIC_MAX_ACK_RANGES) return -1;
    int nranges = (int)(range_count + 1);
    out->ranges = (quic_ack_range_t *)calloc((size_t)nranges, sizeof(quic_ack_range_t));
    if (!out->ranges) return -1;
    out->nranges = nranges;

    /* First range: [largest - first_range, largest] */
    uint64_t range_max = out->largest_acked;
    uint64_t range_min = range_max - first_range;
    out->ranges[0].start = range_min;
    out->ranges[0].end = range_max + 1;

    /* Additional ranges */
    for (uint64_t i = 0; i < range_count; i++) {
        uint64_t gap;
        if (consume_varint(&p, &rem, &gap) != 0) goto fail;

        if (range_min < gap + 2) goto fail;
        range_max = range_min - gap - 2;

        uint64_t ack_range;
        if (consume_varint(&p, &rem, &ack_range) != 0) goto fail;

        if (ack_range > range_max) goto fail;
        range_min = range_max - ack_range;

        out->ranges[i + 1].start = range_min;
        out->ranges[i + 1].end = range_max + 1;
    }

    /* ECN counts */
    if (has_ecn) {
        if (consume_varint(&p, &rem, &out->ect0) != 0) goto fail;
        if (consume_varint(&p, &rem, &out->ect1) != 0) goto fail;
        if (consume_varint(&p, &rem, &out->ecn_ce) != 0) goto fail;
    } else {
        out->ect0 = out->ect1 = out->ecn_ce = 0;
    }

    *consumed = (size_t)(p - buf);
    return 0;

fail:
    free(out->ranges);
    out->ranges = NULL;
    out->nranges = 0;
    return -1;
}

int neverc_quic_parse_reset_stream(const uint8_t *buf, size_t len,
                                    quic_frame_reset_stream_t *out,
                                    size_t *consumed) {
    if (!buf || !out || !consumed) return -1;
    const uint8_t *p = buf;
    size_t rem = len;

    uint64_t ftype;
    if (consume_varint(&p, &rem, &ftype) != 0) return -1;
    if (ftype != QUIC_FRAME_RESET_STREAM) return -1;

    if (consume_varint(&p, &rem, &out->stream_id) != 0) return -1;
    if (consume_varint(&p, &rem, &out->error_code) != 0) return -1;
    if (consume_varint(&p, &rem, &out->final_size) != 0) return -1;

    *consumed = (size_t)(p - buf);
    return 0;
}

int neverc_quic_parse_stop_sending(const uint8_t *buf, size_t len,
                                    quic_frame_stop_sending_t *out,
                                    size_t *consumed) {
    if (!buf || !out || !consumed) return -1;
    const uint8_t *p = buf;
    size_t rem = len;

    uint64_t ftype;
    if (consume_varint(&p, &rem, &ftype) != 0) return -1;
    if (ftype != QUIC_FRAME_STOP_SENDING) return -1;

    if (consume_varint(&p, &rem, &out->stream_id) != 0) return -1;
    if (consume_varint(&p, &rem, &out->error_code) != 0) return -1;

    *consumed = (size_t)(p - buf);
    return 0;
}

int neverc_quic_parse_new_token(const uint8_t *buf, size_t len,
                                size_t *consumed) {
    const uint8_t *p = buf;
    size_t rem = len;
    uint64_t ftype;
    uint64_t token_len;

    if (!buf || !consumed || consume_varint(&p, &rem, &ftype) != 0 ||
        ftype != QUIC_FRAME_NEW_TOKEN ||
        consume_varint(&p, &rem, &token_len) != 0 ||
        token_len == 0 || token_len > rem)
        return -1;
    p += (size_t)token_len;
    *consumed = (size_t)(p - buf);
    return 0;
}

int neverc_quic_parse_new_conn_id(const uint8_t *buf, size_t len,
                                    quic_frame_new_conn_id_t *out,
                                    size_t *consumed) {
    if (!buf || !out || !consumed) return -1;
    const uint8_t *p = buf;
    size_t rem = len;

    uint64_t ftype;
    if (consume_varint(&p, &rem, &ftype) != 0) return -1;
    if (ftype != QUIC_FRAME_NEW_CONNECTION_ID) return -1;

    if (consume_varint(&p, &rem, &out->sequence) != 0) return -1;
    if (consume_varint(&p, &rem, &out->retire_prior_to) != 0) return -1;

    if (rem < 1) return -1;
    out->conn_id_len = *p++;
    rem--;

    /* RFC 9000 §19.15: Retire Prior To MUST NOT exceed Sequence Number,
     * and Connection ID Length MUST be 1..20. */
    if (out->retire_prior_to > out->sequence) return -1;
    if (out->conn_id_len < 1 || out->conn_id_len > 20 ||
        rem < out->conn_id_len + 16)
        return -1;
    memcpy(out->conn_id, p, out->conn_id_len);
    p += out->conn_id_len;
    rem -= out->conn_id_len;

    memcpy(out->stateless_reset_token, p, 16);
    p += 16;
    rem -= 16;

    *consumed = (size_t)(p - buf);
    return 0;
}

int neverc_quic_parse_connection_close(const uint8_t *buf, size_t len,
                                        quic_frame_connection_close_t *out,
                                        size_t *consumed) {
    if (!buf || !out || !consumed) return -1;
    const uint8_t *p = buf;
    size_t rem = len;

    uint64_t ftype;
    if (consume_varint(&p, &rem, &ftype) != 0) return -1;
    if (ftype != QUIC_FRAME_CONNECTION_CLOSE &&
        ftype != QUIC_FRAME_CONNECTION_CLOSE_APP) return -1;

    out->is_app = (ftype == QUIC_FRAME_CONNECTION_CLOSE_APP);

    if (consume_varint(&p, &rem, &out->error_code) != 0) return -1;

    /* Frame Type field — only in transport CONNECTION_CLOSE */
    if (!out->is_app) {
        if (consume_varint(&p, &rem, &out->frame_type) != 0) return -1;
    } else {
        out->frame_type = 0;
    }

    /* Reason Phrase Length + Reason Phrase */
    uint64_t reason_len;
    if (consume_varint(&p, &rem, &reason_len) != 0) return -1;
    if (reason_len > rem) return -1;

    out->reason = (const char *)p;
    out->reason_len = (size_t)reason_len;
    p += (size_t)reason_len;

    *consumed = (size_t)(p - buf);
    return 0;
}

/* ======================================================================
 * Frame Writing
 * ====================================================================== */

int neverc_quic_write_crypto_frame(uint8_t *buf, size_t cap,
                                    uint64_t offset,
                                    const uint8_t *data, size_t data_len,
                                    size_t *written) {
    if (written) *written = 0;
    if (!buf || !written || (data_len && !data) ||
        !quic_end_offset_ok(offset, data_len) ||
        data_len > SIZE_MAX - 17)
        return -1;
    size_t need = neverc_quic_varint_len(QUIC_FRAME_CRYPTO) +
                  neverc_quic_varint_len(offset) +
                  neverc_quic_varint_len(data_len) + data_len;
    if (cap < need) return -1;

    size_t pos = 0, w;
    neverc_quic_varint_encode(QUIC_FRAME_CRYPTO, buf + pos, cap - pos, &w); pos += w;
    neverc_quic_varint_encode(offset, buf + pos, cap - pos, &w); pos += w;
    neverc_quic_varint_encode(data_len, buf + pos, cap - pos, &w); pos += w;
    memcpy(buf + pos, data, data_len);
    pos += data_len;

    *written = pos;
    return 0;
}

int neverc_quic_write_stream_frame(uint8_t *buf, size_t cap,
                                    const quic_frame_stream_t *frame,
                                    size_t *written) {
    if (written) *written = 0;
    if (!buf || !frame || !written ||
        (frame->data_len && !frame->data) ||
        frame->stream_id > QUIC_VARINT_MAX ||
        !quic_end_offset_ok(frame->offset, frame->data_len) ||
        frame->data_len > SIZE_MAX - 25)
        return -1;
    uint8_t type_byte = QUIC_FRAME_STREAM_BASE;
    if (frame->offset > 0) type_byte |= QUIC_STREAM_FLAG_OFF;
    type_byte |= QUIC_STREAM_FLAG_LEN;  /* always include length for safety */
    if (frame->fin) type_byte |= QUIC_STREAM_FLAG_FIN;

    size_t need = 1 + neverc_quic_varint_len(frame->stream_id);
    if (frame->offset > 0)
        need += neverc_quic_varint_len(frame->offset);
    need += neverc_quic_varint_len(frame->data_len) + frame->data_len;

    if (cap < need) return -1;

    size_t pos = 0, w;
    buf[pos++] = type_byte;
    neverc_quic_varint_encode(frame->stream_id, buf + pos, cap - pos, &w); pos += w;
    if (frame->offset > 0) {
        neverc_quic_varint_encode(frame->offset, buf + pos, cap - pos, &w); pos += w;
    }
    neverc_quic_varint_encode(frame->data_len, buf + pos, cap - pos, &w); pos += w;
    memcpy(buf + pos, frame->data, frame->data_len);
    pos += frame->data_len;

    *written = pos;
    return 0;
}

int neverc_quic_write_ack_frame(uint8_t *buf, size_t cap,
                                 const quic_frame_ack_t *ack,
                                 size_t *written) {
    if (written) *written = 0;
    if (!buf || !ack || !written || !ack->ranges ||
        ack->nranges < 1 || ack->nranges > QUIC_MAX_ACK_RANGES ||
        ack->largest_acked > QUIC_VARINT_MAX ||
        ack->ack_delay > QUIC_VARINT_MAX ||
        ack->ranges[0].start >= ack->ranges[0].end ||
        ack->ranges[0].end - 1 != ack->largest_acked)
        return -1;

    size_t need = neverc_quic_varint_len(QUIC_FRAME_ACK) +
        neverc_quic_varint_len(ack->largest_acked) +
        neverc_quic_varint_len(ack->ack_delay) +
        neverc_quic_varint_len((uint64_t)(ack->nranges - 1)) +
        neverc_quic_varint_len(
            ack->ranges[0].end - 1 - ack->ranges[0].start);
    for (int i = 1; i < ack->nranges; i++) {
        if (ack->ranges[i].start >= ack->ranges[i].end ||
            ack->ranges[i - 1].start <= ack->ranges[i].end)
            return -1;
        uint64_t gap =
            ack->ranges[i - 1].start - ack->ranges[i].end - 1;
        uint64_t range_len =
            ack->ranges[i].end - 1 - ack->ranges[i].start;
        if (gap > QUIC_VARINT_MAX || range_len > QUIC_VARINT_MAX ||
            need > SIZE_MAX - neverc_quic_varint_len(gap) ||
            need + neverc_quic_varint_len(gap) >
                SIZE_MAX - neverc_quic_varint_len(range_len))
            return -1;
        need += neverc_quic_varint_len(gap) +
                neverc_quic_varint_len(range_len);
    }
    if (cap < need) return -1;

    size_t pos = 0, w;

    /* Type */
    neverc_quic_varint_encode(QUIC_FRAME_ACK, buf + pos, cap - pos, &w); pos += w;
    /* Largest Acknowledged */
    neverc_quic_varint_encode(ack->largest_acked, buf + pos, cap - pos, &w); pos += w;
    /* ACK Delay */
    neverc_quic_varint_encode(ack->ack_delay, buf + pos, cap - pos, &w); pos += w;
    /* ACK Range Count */
    uint64_t range_count = (uint64_t)(ack->nranges - 1);
    neverc_quic_varint_encode(range_count, buf + pos, cap - pos, &w); pos += w;

    /* First ACK Range */
    uint64_t first_range = ack->ranges[0].end - 1 - ack->ranges[0].start;
    neverc_quic_varint_encode(first_range, buf + pos, cap - pos, &w); pos += w;

    /* Additional ranges: gap + range pairs */
    for (int i = 1; i < ack->nranges; i++) {
        uint64_t gap = ack->ranges[i - 1].start - ack->ranges[i].end - 1;
        uint64_t range_len = ack->ranges[i].end - 1 - ack->ranges[i].start;
        neverc_quic_varint_encode(gap, buf + pos, cap - pos, &w); pos += w;
        neverc_quic_varint_encode(range_len, buf + pos, cap - pos, &w); pos += w;
    }

    *written = pos;
    return 0;
}

int neverc_quic_write_connection_close(uint8_t *buf, size_t cap,
                                        const quic_frame_connection_close_t *cc,
                                        size_t *written) {
    if (written) *written = 0;
    if (!buf || !cc || !written ||
        (cc->reason_len && !cc->reason) ||
        cc->error_code > QUIC_VARINT_MAX ||
        cc->frame_type > QUIC_VARINT_MAX ||
        cc->reason_len > QUIC_VARINT_MAX ||
        cc->reason_len > SIZE_MAX - 33)
        return -1;
    uint64_t ftype = cc->is_app ? QUIC_FRAME_CONNECTION_CLOSE_APP
                                : QUIC_FRAME_CONNECTION_CLOSE;

    size_t need = neverc_quic_varint_len(ftype) +
        neverc_quic_varint_len(cc->error_code) +
        neverc_quic_varint_len(cc->reason_len) + cc->reason_len;
    if (!cc->is_app) need += neverc_quic_varint_len(cc->frame_type);
    if (cap < need) return -1;

    size_t pos = 0, w;
    neverc_quic_varint_encode(ftype, buf + pos, cap - pos, &w); pos += w;
    neverc_quic_varint_encode(cc->error_code, buf + pos, cap - pos, &w); pos += w;
    if (!cc->is_app) {
        neverc_quic_varint_encode(cc->frame_type, buf + pos, cap - pos, &w); pos += w;
    }
    neverc_quic_varint_encode(cc->reason_len, buf + pos, cap - pos, &w); pos += w;
    if (cc->reason_len > 0) {
        if (pos + cc->reason_len > cap) return -1;
        memcpy(buf + pos, cc->reason, cc->reason_len);
        pos += cc->reason_len;
    }

    *written = pos;
    return 0;
}

int neverc_quic_write_max_data(uint8_t *buf, size_t cap,
                                uint64_t max_data, size_t *written) {
    if (written) *written = 0;
    if (!buf || !written || max_data > QUIC_VARINT_MAX ||
        cap < 1 + neverc_quic_varint_len(max_data))
        return -1;
    size_t pos = 0, w;
    neverc_quic_varint_encode(QUIC_FRAME_MAX_DATA, buf + pos, cap - pos, &w); pos += w;
    neverc_quic_varint_encode(max_data, buf + pos, cap - pos, &w); pos += w;
    *written = pos;
    return 0;
}

int neverc_quic_write_max_stream_data(uint8_t *buf, size_t cap,
                                       uint64_t stream_id, uint64_t max_data,
                                       size_t *written) {
    if (written) *written = 0;
    if (!buf || !written || stream_id > QUIC_VARINT_MAX ||
        max_data > QUIC_VARINT_MAX ||
        cap < 1 + neverc_quic_varint_len(stream_id) +
                    neverc_quic_varint_len(max_data))
        return -1;
    size_t pos = 0, w;
    neverc_quic_varint_encode(QUIC_FRAME_MAX_STREAM_DATA, buf + pos, cap - pos, &w); pos += w;
    neverc_quic_varint_encode(stream_id, buf + pos, cap - pos, &w); pos += w;
    neverc_quic_varint_encode(max_data, buf + pos, cap - pos, &w); pos += w;
    *written = pos;
    return 0;
}

int neverc_quic_write_reset_stream(uint8_t *buf, size_t cap,
                                    uint64_t stream_id, uint64_t error_code,
                                    uint64_t final_size, size_t *written) {
    if (written) *written = 0;
    if (!buf || !written || stream_id > QUIC_VARINT_MAX ||
        error_code > QUIC_VARINT_MAX || final_size > QUIC_VARINT_MAX ||
        cap < 1 + neverc_quic_varint_len(stream_id) +
                    neverc_quic_varint_len(error_code) +
                    neverc_quic_varint_len(final_size))
        return -1;
    size_t pos = 0, w;
    neverc_quic_varint_encode(QUIC_FRAME_RESET_STREAM, buf + pos, cap - pos, &w); pos += w;
    neverc_quic_varint_encode(stream_id, buf + pos, cap - pos, &w); pos += w;
    neverc_quic_varint_encode(error_code, buf + pos, cap - pos, &w); pos += w;
    neverc_quic_varint_encode(final_size, buf + pos, cap - pos, &w); pos += w;
    *written = pos;
    return 0;
}

int neverc_quic_write_ping(uint8_t *buf, size_t cap, size_t *written) {
    if (written) *written = 0;
    if (!buf || !written || cap < 1) return -1;
    buf[0] = QUIC_FRAME_PING;
    *written = 1;
    return 0;
}

int neverc_quic_write_handshake_done(uint8_t *buf, size_t cap, size_t *written) {
    if (written) *written = 0;
    if (!buf || !written || cap < 1) return -1;
    buf[0] = QUIC_FRAME_HANDSHAKE_DONE;
    *written = 1;
    return 0;
}

int neverc_quic_parse_retire_conn_id(const uint8_t *buf, size_t len,
                                     quic_frame_retire_conn_id_t *out,
                                     size_t *consumed) {
    const uint8_t *p = buf;
    size_t rem = len;
    uint64_t ftype;
    if (!buf || !out || !consumed || consume_varint(&p, &rem, &ftype) != 0 ||
        ftype != QUIC_FRAME_RETIRE_CONNECTION_ID)
        return -1;
    if (consume_varint(&p, &rem, &out->sequence) != 0) return -1;
    *consumed = (size_t)(p - buf);
    return 0;
}

int neverc_quic_write_retire_conn_id(uint8_t *buf, size_t cap,
                                     uint64_t sequence, size_t *written) {
    if (written) *written = 0;
    if (!buf || !written || sequence > QUIC_VARINT_MAX ||
        cap < 1 + neverc_quic_varint_len(sequence))
        return -1;
    size_t pos = 0, w;
    neverc_quic_varint_encode(QUIC_FRAME_RETIRE_CONNECTION_ID, buf + pos,
                              cap - pos, &w);
    pos += w;
    neverc_quic_varint_encode(sequence, buf + pos, cap - pos, &w);
    pos += w;
    *written = pos;
    return 0;
}

int neverc_quic_parse_stream_count_frame(const uint8_t *buf, size_t len,
                                         uint64_t *maximum, size_t *consumed) {
    const uint8_t *p = buf;
    size_t rem = len;
    uint64_t ftype;
    if (!buf || !maximum || !consumed || consume_varint(&p, &rem, &ftype) != 0)
        return -1;
    if (ftype != QUIC_FRAME_MAX_STREAMS_BIDI &&
        ftype != QUIC_FRAME_MAX_STREAMS_UNI &&
        ftype != QUIC_FRAME_STREAMS_BLOCKED_BIDI &&
        ftype != QUIC_FRAME_STREAMS_BLOCKED_UNI)
        return -1;
    if (consume_varint(&p, &rem, maximum) != 0 ||
        *maximum > QUIC_MAX_STREAM_COUNT)
        return -1;
    *consumed = (size_t)(p - buf);
    return 0;
}
