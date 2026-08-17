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

#include "_quic_internal.h"

#include <string.h>

static quic_packet_type_t quic_long_packet_type(uint8_t encoded,
                                                 uint32_t version) {
    if (version != 0x6b3343cfU)
        return (quic_packet_type_t)encoded;
    static const quic_packet_type_t v2_types[4] = {
        QUIC_PKT_RETRY, QUIC_PKT_INITIAL, QUIC_PKT_0RTT,
        QUIC_PKT_HANDSHAKE};
    return v2_types[encoded & 3U];
}

static uint8_t quic_long_packet_type_bits(quic_packet_type_t type,
                                           uint32_t version) {
    if (version != 0x6b3343cfU) return (uint8_t)type & 3U;
    static const uint8_t v2_bits[4] = {1, 2, 3, 0};
    return v2_bits[(unsigned)type & 3U];
}

static int parse_long_header(const uint8_t *buf, size_t len,
                              quic_packet_header_t *hdr) {
    if (len < 7) return -1;

    uint8_t first = buf[0];
    hdr->version = ((uint32_t)buf[1] << 24) | ((uint32_t)buf[2] << 16) |
                   ((uint32_t)buf[3] << 8) | (uint32_t)buf[4];
    hdr->type = quic_long_packet_type((first >> 4) & 0x03,
                                      hdr->version);
    hdr->pkt_number_len = (first & 0x03) + 1;

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
        if (tlen > SIZE_MAX || (size_t)tlen > len - pos) return -1;
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
        if (plen > SIZE_MAX || (size_t)plen > len - pos ||
            plen < hdr->pkt_number_len)
            return -1;

        /* Packet number (1-4 bytes, after header protection removal) */
        if (pos + hdr->pkt_number_len > len) return -1;
        hdr->pkt_number = 0;
        for (int i = 0; i < hdr->pkt_number_len; i++) {
            hdr->pkt_number = (hdr->pkt_number << 8) | buf[pos + i];
        }
        pos += hdr->pkt_number_len;
        hdr->payload_len = (size_t)plen - hdr->pkt_number_len;
    } else {
        hdr->payload_len = len - pos;
    }

    hdr->header_len = pos;
    return 0;
}

static int parse_short_header(const uint8_t *buf, size_t len,
                               quic_packet_header_t *hdr,
                               uint8_t dcid_len) {
    if (dcid_len > QUIC_MAX_CID_LEN || len < 1 + dcid_len) return -1;

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
    if (!buf || !hdr || len < 1 || (buf[0] & 0x40) == 0) return -1;
    memset(hdr, 0, sizeof(*hdr));

    uint8_t first = buf[0];
    int is_long = (first & 0x80) != 0;

    if (is_long)
        return parse_long_header(buf, len, hdr);
    else
        return parse_short_header(buf, len, hdr, expected_dcid_len);
}

int neverc_quic_is_version_negotiation(const uint8_t *buf, size_t len) {
    if (!buf || len < 5 || (buf[0] & 0x80U) == 0) return 0;
    uint32_t version = ((uint32_t)buf[1] << 24) | ((uint32_t)buf[2] << 16) |
                       ((uint32_t)buf[3] << 8) | (uint32_t)buf[4];
    return version == 0;
}

static int quic_read_cid_at(const uint8_t *buf, size_t len, size_t *pos,
                            quic_conn_id_t *cid) {
    if (!buf || !pos || !cid || *pos >= len) return -1;
    cid->len = buf[(*pos)++];
    if (cid->len > QUIC_MAX_CID_LEN || cid->len > len - *pos) return -1;
    memcpy(cid->data, buf + *pos, cid->len);
    *pos += cid->len;
    return 0;
}

int neverc_quic_version_negotiation_supports(const uint8_t *buf, size_t len,
                                             uint32_t version) {
    if (!neverc_quic_is_version_negotiation(buf, len)) return 0;
    size_t pos = 5;
    quic_conn_id_t dcid;
    quic_conn_id_t scid;
    if (quic_read_cid_at(buf, len, &pos, &dcid) != 0 ||
        quic_read_cid_at(buf, len, &pos, &scid) != 0)
        return 0;
    if ((len - pos) % 4U != 0 || pos == len) return 0;
    while (pos + 4U <= len) {
        uint32_t listed = ((uint32_t)buf[pos] << 24) |
                          ((uint32_t)buf[pos + 1U] << 16) |
                          ((uint32_t)buf[pos + 2U] << 8) |
                          (uint32_t)buf[pos + 3U];
        if (listed == version) return 1;
        pos += 4U;
    }
    return 0;
}

int neverc_quic_write_version_negotiation(
    uint8_t *buf, size_t cap, uint8_t first_byte,
    const quic_conn_id_t *destination, const quic_conn_id_t *source,
    const uint32_t *versions, size_t nversions, size_t *written) {
    if (written) *written = 0;
    if (!buf || !destination || !source || !written ||
        (nversions > 0 && !versions) || nversions == 0 ||
        destination->len > QUIC_MAX_CID_LEN ||
        source->len > QUIC_MAX_CID_LEN || (first_byte & 0x80U) == 0)
        return -1;
    size_t need = 5U + 1U + destination->len + 1U + source->len;
    if (nversions > (SIZE_MAX - need) / 4U) return -1;
    need += nversions * 4U;
    if (cap < need) return -1;
    size_t pos = 0;
    buf[pos++] = first_byte;
    buf[pos++] = 0;
    buf[pos++] = 0;
    buf[pos++] = 0;
    buf[pos++] = 0;
    buf[pos++] = destination->len;
    memcpy(buf + pos, destination->data, destination->len);
    pos += destination->len;
    buf[pos++] = source->len;
    memcpy(buf + pos, source->data, source->len);
    pos += source->len;
    for (size_t i = 0; i < nversions; i++) {
        buf[pos++] = (uint8_t)(versions[i] >> 24);
        buf[pos++] = (uint8_t)(versions[i] >> 16);
        buf[pos++] = (uint8_t)(versions[i] >> 8);
        buf[pos++] = (uint8_t)versions[i];
    }
    *written = pos;
    return 0;
}

int neverc_quic_write_long_header(uint8_t *buf, size_t cap,
                                    const quic_packet_header_t *hdr,
                                    size_t *written) {
    if (written) *written = 0;
    if (!buf || !hdr || !written || hdr->type > QUIC_PKT_RETRY ||
        hdr->dcid.len > QUIC_MAX_CID_LEN ||
        hdr->scid.len > QUIC_MAX_CID_LEN ||
        hdr->pkt_number_len < 1 || hdr->pkt_number_len > 4 ||
        hdr->payload_len > UINT64_MAX - hdr->pkt_number_len)
        return -1;
    if (hdr->type == QUIC_PKT_INITIAL &&
        hdr->token_len > SIZE_MAX - 8)
        return -1;
    size_t token_prefix = hdr->type == QUIC_PKT_INITIAL
        ? neverc_quic_varint_len(hdr->token_len) + hdr->token_len : 0;
    size_t length_prefix = hdr->type == QUIC_PKT_RETRY ? 0 :
        neverc_quic_varint_len(
            (uint64_t)hdr->pkt_number_len + hdr->payload_len);
    size_t need = 7 + hdr->dcid.len + hdr->scid.len;
    if (need > SIZE_MAX - token_prefix ||
        need + token_prefix > SIZE_MAX - length_prefix ||
        need + token_prefix + length_prefix >
            SIZE_MAX - (hdr->type == QUIC_PKT_RETRY
                            ? 0 : hdr->pkt_number_len))
        return -1;
    need += token_prefix + length_prefix;
    if (hdr->type != QUIC_PKT_RETRY) need += hdr->pkt_number_len;
    if (cap < need) return -1;

    size_t pos = 0;
    buf[pos++] = 0xC0 |
                 (quic_long_packet_type_bits(hdr->type, hdr->version) << 4) |
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

    size_t encoded = 0;
    if (hdr->type == QUIC_PKT_INITIAL) {
        if (neverc_quic_varint_encode(hdr->token_len, buf + pos,
                                      cap - pos, &encoded) != 0)
            return -1;
        pos += encoded;
        if (hdr->token_len > 0) {
            if (!hdr->token) return -1;
            memcpy(buf + pos, hdr->token, hdr->token_len);
            pos += hdr->token_len;
        }
    }
    if (hdr->type != QUIC_PKT_RETRY) {
        if (neverc_quic_varint_encode(
                (uint64_t)hdr->pkt_number_len + hdr->payload_len,
                buf + pos, cap - pos, &encoded) != 0)
            return -1;
        pos += encoded;
        for (uint8_t i = 0; i < hdr->pkt_number_len; i++)
            buf[pos + i] = (uint8_t)(
                hdr->pkt_number >>
                (8U * (hdr->pkt_number_len - i - 1U)));
        pos += hdr->pkt_number_len;
    }

    *written = pos;
    return 0;
}

uint64_t neverc_quic_decode_packet_number(uint64_t largest_received,
                                           uint64_t truncated,
                                           unsigned packet_number_bits) {
    if (packet_number_bits == 0 || packet_number_bits > 32)
        return truncated;
    uint64_t expected = largest_received < UINT64_MAX ?
        largest_received + 1 : UINT64_MAX;
    uint64_t window = UINT64_C(1) << packet_number_bits;
    uint64_t half_window = window / 2;
    uint64_t mask = window - 1;
    uint64_t candidate = (expected & ~mask) | (truncated & mask);
    int too_small = expected >= half_window &&
                    candidate <= expected - half_window;
    if (too_small && candidate <= QUIC_VARINT_MAX - window)
        candidate += window;
    else if (candidate >= window &&
             expected <= UINT64_MAX - half_window &&
             candidate > expected + half_window)
        candidate -= window;
    return candidate;
}
