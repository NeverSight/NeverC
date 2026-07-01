/*
 * QUIC Packet Header Parsing (RFC 9000 §17)
 *
 * Two packet header forms:
 *   - Long Header: used during connection establishment
 *     Form(1) Fixed(1) Type(2) Reserved(2) PktNumLen(2) | Version(32) |
 *     DCID_Len(8) | DCID | SCID_Len(8) | SCID | [Type-specific] | Payload
 *
 *   - Short Header: used after handshake (1-RTT)
 *     Form(0) Fixed(1) Spin(1) Reserved(2) KeyPhase(1) PktNumLen(2) |
 *     DCID | Packet Number | Payload
 *
 * Long header packet types:
 *   0x00 = Initial
 *   0x01 = 0-RTT
 *   0x02 = Handshake
 *   0x03 = Retry
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

#define QUIC_MAX_CID_LEN 20

typedef enum {
    QUIC_PKT_INITIAL   = 0,
    QUIC_PKT_0RTT      = 1,
    QUIC_PKT_HANDSHAKE = 2,
    QUIC_PKT_RETRY     = 3,
    QUIC_PKT_1RTT      = 4,  /* short header */
} quic_packet_type_t;

typedef struct {
    uint8_t data[QUIC_MAX_CID_LEN];
    uint8_t len;
} quic_conn_id_t;

typedef struct {
    quic_packet_type_t type;
    uint32_t           version;
    quic_conn_id_t     dcid;
    quic_conn_id_t     scid;
    uint32_t           pkt_number;
    uint8_t            pkt_number_len;
    uint8_t            key_phase;    /* 1-RTT only */
    uint8_t            spin_bit;    /* 1-RTT only */

    /* For Initial packets: token */
    const uint8_t     *token;
    size_t             token_len;

    /* Payload offset and length */
    size_t             header_len;
    size_t             payload_len;
} quic_packet_header_t;

/* External varint functions */
extern int neverc_quic_varint_decode(const uint8_t *buf, size_t len,
                                      uint64_t *value, size_t *consumed);

static int parse_long_header(const uint8_t *buf, size_t len,
                              quic_packet_header_t *hdr) {
    if (len < 7) return -1;

    uint8_t first = buf[0];
    hdr->type = (quic_packet_type_t)((first >> 4) & 0x03);
    hdr->pkt_number_len = (first & 0x03) + 1;

    hdr->version = ((uint32_t)buf[1] << 24) | ((uint32_t)buf[2] << 16) |
                   ((uint32_t)buf[3] << 8) | (uint32_t)buf[4];

    size_t pos = 5;

    /* DCID */
    if (pos >= len) return -1;
    hdr->dcid.len = buf[pos++];
    if (hdr->dcid.len > QUIC_MAX_CID_LEN || pos + hdr->dcid.len > len)
        return -1;
    memcpy(hdr->dcid.data, buf + pos, hdr->dcid.len);
    pos += hdr->dcid.len;

    /* SCID */
    if (pos >= len) return -1;
    hdr->scid.len = buf[pos++];
    if (hdr->scid.len > QUIC_MAX_CID_LEN || pos + hdr->scid.len > len)
        return -1;
    memcpy(hdr->scid.data, buf + pos, hdr->scid.len);
    pos += hdr->scid.len;

    /* Type-specific fields */
    if (hdr->type == QUIC_PKT_INITIAL) {
        uint64_t tlen;
        size_t consumed;
        if (neverc_quic_varint_decode(buf + pos, len - pos, &tlen, &consumed) != 0)
            return -1;
        pos += consumed;
        hdr->token = buf + pos;
        hdr->token_len = (size_t)tlen;
        pos += (size_t)tlen;
    } else {
        hdr->token = NULL;
        hdr->token_len = 0;
    }

    if (hdr->type != QUIC_PKT_RETRY) {
        /* Payload length (varint) */
        uint64_t plen;
        size_t consumed;
        if (neverc_quic_varint_decode(buf + pos, len - pos, &plen, &consumed) != 0)
            return -1;
        pos += consumed;
        hdr->payload_len = (size_t)plen;

        /* Packet number (1-4 bytes, after header protection removal) */
        if (pos + hdr->pkt_number_len > len) return -1;
        hdr->pkt_number = 0;
        for (int i = 0; i < hdr->pkt_number_len; i++) {
            hdr->pkt_number = (hdr->pkt_number << 8) | buf[pos + i];
        }
        pos += hdr->pkt_number_len;
    }

    hdr->header_len = pos;
    return 0;
}

static int parse_short_header(const uint8_t *buf, size_t len,
                               quic_packet_header_t *hdr,
                               uint8_t dcid_len) {
    if (len < 1 + dcid_len) return -1;

    uint8_t first = buf[0];
    hdr->type = QUIC_PKT_1RTT;
    hdr->spin_bit = (first >> 5) & 0x01;
    hdr->key_phase = (first >> 2) & 0x01;
    hdr->pkt_number_len = (first & 0x03) + 1;
    hdr->version = 0;

    size_t pos = 1;
    hdr->dcid.len = dcid_len;
    memcpy(hdr->dcid.data, buf + pos, dcid_len);
    pos += dcid_len;

    hdr->scid.len = 0;

    /* Packet number */
    if (pos + hdr->pkt_number_len > len) return -1;
    hdr->pkt_number = 0;
    for (int i = 0; i < hdr->pkt_number_len; i++) {
        hdr->pkt_number = (hdr->pkt_number << 8) | buf[pos + i];
    }
    pos += hdr->pkt_number_len;

    hdr->header_len = pos;
    hdr->payload_len = len - pos;
    return 0;
}

int neverc_quic_parse_packet_header(const uint8_t *buf, size_t len,
                                     quic_packet_header_t *hdr,
                                     uint8_t expected_dcid_len) {
    if (len < 1) return -1;
    memset(hdr, 0, sizeof(*hdr));

    uint8_t first = buf[0];
    int is_long = (first & 0x80) != 0;

    if (is_long)
        return parse_long_header(buf, len, hdr);
    else
        return parse_short_header(buf, len, hdr, expected_dcid_len);
}

int neverc_quic_write_long_header(uint8_t *buf, size_t cap,
                                    const quic_packet_header_t *hdr,
                                    size_t *written) {
    size_t need = 7 + hdr->dcid.len + hdr->scid.len;
    if (cap < need) return -1;

    size_t pos = 0;
    buf[pos++] = 0xC0 | ((uint8_t)hdr->type << 4) |
                 (hdr->pkt_number_len - 1);
    buf[pos++] = (uint8_t)(hdr->version >> 24);
    buf[pos++] = (uint8_t)(hdr->version >> 16);
    buf[pos++] = (uint8_t)(hdr->version >> 8);
    buf[pos++] = (uint8_t)(hdr->version);
    buf[pos++] = hdr->dcid.len;
    memcpy(buf + pos, hdr->dcid.data, hdr->dcid.len);
    pos += hdr->dcid.len;
    buf[pos++] = hdr->scid.len;
    memcpy(buf + pos, hdr->scid.data, hdr->scid.len);
    pos += hdr->scid.len;

    *written = pos;
    return 0;
}
