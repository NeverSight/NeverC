/*
 * QUIC Frame Codec (RFC 9000 §19)
 *
 * All QUIC packets (except Version Negotiation and Retry) contain one or more
 * frames. This module provides parsing and serialization for all frame types.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/* External varint functions from quic_varint.c */
extern int neverc_quic_varint_decode(const uint8_t *buf, size_t len,
                                      uint64_t *value, size_t *consumed);
extern int neverc_quic_varint_encode(uint64_t value, uint8_t *buf, size_t cap,
                                      size_t *written);
extern size_t neverc_quic_varint_len(uint64_t value);

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

/* ======================================================================
 * Frame Structures
 * ====================================================================== */

typedef struct {
    uint64_t stream_id;
    uint64_t error_code;
    uint64_t final_size;
} quic_frame_reset_stream_t;

typedef struct {
    uint64_t stream_id;
    uint64_t error_code;
} quic_frame_stop_sending_t;

typedef struct {
    uint64_t offset;
    const uint8_t *data;
    size_t   data_len;
} quic_frame_crypto_t;

typedef struct {
    uint64_t stream_id;
    uint64_t offset;
    const uint8_t *data;
    size_t   data_len;
    int      fin;
} quic_frame_stream_t;

typedef struct {
    uint64_t max_data;
} quic_frame_max_data_t;

typedef struct {
    uint64_t stream_id;
    uint64_t max_data;
} quic_frame_max_stream_data_t;

typedef struct {
    uint64_t max_streams;
    int      is_bidi;
} quic_frame_max_streams_t;

typedef struct {
    uint64_t sequence;
    uint64_t retire_prior_to;
    uint8_t  conn_id_len;
    uint8_t  conn_id[20];
    uint8_t  stateless_reset_token[16];
} quic_frame_new_conn_id_t;

typedef struct {
    uint64_t sequence;
} quic_frame_retire_conn_id_t;

typedef struct {
    uint8_t data[8];
} quic_frame_path_challenge_t;

typedef struct {
    uint64_t error_code;
    uint64_t frame_type;  /* only for transport close */
    const char *reason;
    size_t   reason_len;
    int      is_app;      /* 1 = APPLICATION_CLOSE, 0 = CONNECTION_CLOSE */
} quic_frame_connection_close_t;

/* ACK range: [start, end) — packets start through end-1 are acknowledged */
typedef struct {
    uint64_t start;
    uint64_t end;
} quic_ack_range_t;

typedef struct {
    uint64_t largest_acked;
    uint64_t ack_delay;
    quic_ack_range_t *ranges;
    int      nranges;
    /* ECN counts (only for ACK_ECN frames) */
    uint64_t ect0;
    uint64_t ect1;
    uint64_t ecn_ce;
} quic_frame_ack_t;

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

int neverc_quic_parse_crypto_frame(const uint8_t *buf, size_t len,
                                    quic_frame_crypto_t *out, size_t *consumed) {
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
    if (dlen > rem) return -1;

    out->data = p;
    out->data_len = (size_t)dlen;
    p += dlen;
    rem -= (size_t)dlen;

    *consumed = (size_t)(p - buf);
    return 0;
}

int neverc_quic_parse_stream_frame(const uint8_t *buf, size_t len,
                                    quic_frame_stream_t *out, size_t *consumed) {
    if (len < 1) return -1;

    uint8_t type_byte = buf[0];
    if ((type_byte & 0xF8) != QUIC_FRAME_STREAM_BASE) return -1;

    int has_off = (type_byte & QUIC_STREAM_FLAG_OFF) != 0;
    int has_len = (type_byte & QUIC_STREAM_FLAG_LEN) != 0;
    out->fin = (type_byte & QUIC_STREAM_FLAG_FIN) != 0;

    const uint8_t *p = buf + 1;
    size_t rem = len - 1;

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
        if (dlen > rem) return -1;
        out->data = p;
        out->data_len = (size_t)dlen;
        p += dlen;
    } else {
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

int neverc_quic_parse_new_conn_id(const uint8_t *buf, size_t len,
                                    quic_frame_new_conn_id_t *out,
                                    size_t *consumed) {
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

    if (out->conn_id_len > 20 || rem < out->conn_id_len + 16) return -1;
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
    p += reason_len;

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
    if (ack->nranges < 1) return -1;

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
    uint64_t ftype = cc->is_app ? QUIC_FRAME_CONNECTION_CLOSE_APP
                                : QUIC_FRAME_CONNECTION_CLOSE;

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
    size_t pos = 0, w;
    neverc_quic_varint_encode(QUIC_FRAME_MAX_DATA, buf + pos, cap - pos, &w); pos += w;
    neverc_quic_varint_encode(max_data, buf + pos, cap - pos, &w); pos += w;
    *written = pos;
    return 0;
}

int neverc_quic_write_max_stream_data(uint8_t *buf, size_t cap,
                                       uint64_t stream_id, uint64_t max_data,
                                       size_t *written) {
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
    size_t pos = 0, w;
    neverc_quic_varint_encode(QUIC_FRAME_RESET_STREAM, buf + pos, cap - pos, &w); pos += w;
    neverc_quic_varint_encode(stream_id, buf + pos, cap - pos, &w); pos += w;
    neverc_quic_varint_encode(error_code, buf + pos, cap - pos, &w); pos += w;
    neverc_quic_varint_encode(final_size, buf + pos, cap - pos, &w); pos += w;
    *written = pos;
    return 0;
}

int neverc_quic_write_ping(uint8_t *buf, size_t cap, size_t *written) {
    if (cap < 1) return -1;
    buf[0] = QUIC_FRAME_PING;
    *written = 1;
    return 0;
}

int neverc_quic_write_handshake_done(uint8_t *buf, size_t cap, size_t *written) {
    if (cap < 1) return -1;
    buf[0] = QUIC_FRAME_HANDSHAKE_DONE;
    *written = 1;
    return 0;
}
