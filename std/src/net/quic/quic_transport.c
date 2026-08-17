/* QUIC packet I/O, frame dispatch, ACK generation, and transport timers. */

#include "_quic_internal.h"

#include "neverc/std/_platform.h"
#include "neverc/std/crypto/rand.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define QUIC_PACKET_OVERHEAD_RESERVE 96U

typedef struct {
    quic_tx_kind_t kind;
    quic_enc_level_t level;
    uint64_t offset;
    uint64_t stream_id;
    size_t length;
    int fin;
    int ack_only;
    int control_type;
    const neverc_udp_addr_t *destination;
} quic_send_meta_t;

static quic_packet_type_t qt_long_type(uint8_t encoded, uint32_t version) {
    if (version != NEVERC_QUIC_VERSION_2)
        return (quic_packet_type_t)(encoded & 3U);
    static const quic_packet_type_t v2_types[4] = {
        QUIC_PKT_RETRY, QUIC_PKT_INITIAL, QUIC_PKT_0RTT,
        QUIC_PKT_HANDSHAKE};
    return v2_types[encoded & 3U];
}

static int qt_address_equal(const neverc_udp_addr_t *left,
                            const neverc_udp_addr_t *right) {
    if (!left || !right || left->_sa_len <= 0 ||
        left->_sa_len != right->_sa_len)
        return 0;
    return memcmp(left->_sa, right->_sa, (size_t)left->_sa_len) == 0;
}

static int qt_stream_is_uni(uint64_t stream_id) {
    return (stream_id & 2U) != 0;
}

static int qt_decode_varint_at(const uint8_t *buffer, size_t length,
                               size_t *position, uint64_t *value) {
    size_t consumed;
    if (!buffer || !position || !value || *position > length ||
        neverc_quic_varint_decode(buffer + *position, length - *position,
                                  value, &consumed) != 0)
        return -1;
    *position += consumed;
    return 0;
}

int neverc_quic_packet_number_offset(const uint8_t *packet, size_t length,
                                     uint8_t short_dcid_len,
                                     size_t *packet_number_offset) {
    if (!packet || !packet_number_offset || length < 1 ||
        (packet[0] & 0x40U) == 0)
        return -1;
    if ((packet[0] & 0x80U) == 0) {
        if (short_dcid_len > QUIC_MAX_CID_LEN ||
            length < 1U + short_dcid_len)
            return -1;
        *packet_number_offset = 1U + short_dcid_len;
        return 0;
    }
    if (length < 7) return -1;
    uint32_t version = ((uint32_t)packet[1] << 24) |
                       ((uint32_t)packet[2] << 16) |
                       ((uint32_t)packet[3] << 8) | packet[4];
    quic_packet_type_t type = qt_long_type((packet[0] >> 4) & 3U,
                                           version);
    size_t position = 5;
    size_t dcid_len = packet[position++];
    if (dcid_len > QUIC_MAX_CID_LEN || dcid_len > length - position)
        return -1;
    position += dcid_len;
    if (position == length) return -1;
    size_t scid_len = packet[position++];
    if (scid_len > QUIC_MAX_CID_LEN || scid_len > length - position)
        return -1;
    position += scid_len;
    if (type == QUIC_PKT_RETRY) return -1;
    if (type == QUIC_PKT_INITIAL) {
        uint64_t token_len;
        if (qt_decode_varint_at(packet, length, &position, &token_len) != 0 ||
            token_len > SIZE_MAX || (size_t)token_len > length - position)
            return -1;
        position += (size_t)token_len;
    }
    uint64_t payload_len;
    if (qt_decode_varint_at(packet, length, &position, &payload_len) != 0 ||
        payload_len < 1 || payload_len > length - position)
        return -1;
    *packet_number_offset = position;
    return 0;
}

static int qt_packet_space(quic_packet_type_t type) {
    if (type == QUIC_PKT_INITIAL) return QUIC_PNS_INITIAL;
    if (type == QUIC_PKT_HANDSHAKE) return QUIC_PNS_HANDSHAKE;
    return QUIC_PNS_APPLICATION;
}

static quic_enc_level_t qt_packet_level(quic_packet_type_t type) {
    if (type == QUIC_PKT_INITIAL) return QUIC_ENC_INITIAL;
    if (type == QUIC_PKT_HANDSHAKE) return QUIC_ENC_HANDSHAKE;
    if (type == QUIC_PKT_0RTT) return QUIC_ENC_EARLY_DATA;
    return QUIC_ENC_APPLICATION;
}

static int qt_mark_received(quic_pn_state_t *state, uint64_t packet_number) {
    if (!state) return -1;
    if (!state->has_recv) {
        state->has_recv = 1;
        state->largest_recv = packet_number;
        state->received_bitmap = 1;
        return 1;
    }
    if (packet_number > state->largest_recv) {
        uint64_t shift = packet_number - state->largest_recv;
        state->received_bitmap = shift >= 64 ? 1 :
            (state->received_bitmap << (unsigned)shift) | 1U;
        state->largest_recv = packet_number;
        return 1;
    }
    uint64_t distance = state->largest_recv - packet_number;
    if (distance >= 64) return 0;
    uint64_t mask = UINT64_C(1) << (unsigned)distance;
    if (state->received_bitmap & mask) return 0;
    state->received_bitmap |= mask;
    return 1;
}

static int qt_write_ack(quic_pn_state_t *state, uint8_t *output,
                        size_t capacity, size_t *written) {
    if (!state || !state->has_recv || !output || !written) return -1;
    quic_ack_range_t ranges[64];
    int range_count = 0;
    unsigned bit = 0;
    while (bit < 64 && bit <= state->largest_recv) {
        while (bit < 64 && bit <= state->largest_recv &&
               (state->received_bitmap & (UINT64_C(1) << bit)) == 0)
            bit++;
        if (bit >= 64 || bit > state->largest_recv) break;
        unsigned first = bit;
        while (bit < 64 && bit <= state->largest_recv &&
               (state->received_bitmap & (UINT64_C(1) << bit)) != 0)
            bit++;
        unsigned last = bit - 1U;
        ranges[range_count].start = state->largest_recv - last;
        ranges[range_count].end = state->largest_recv - first + 1U;
        range_count++;
    }
    if (range_count == 0) return -1;
    quic_frame_ack_t ack;
    memset(&ack, 0, sizeof(ack));
    ack.largest_acked = state->largest_recv;
    ack.ranges = ranges;
    ack.nranges = range_count;
    return neverc_quic_write_ack_frame(output, capacity, &ack, written);
}

static int qt_ack_contains(const quic_frame_ack_t *ack,
                           uint64_t packet_number) {
    if (!ack) return 0;
    for (int i = 0; i < ack->nranges; i++) {
        if (packet_number >= ack->ranges[i].start &&
            packet_number < ack->ranges[i].end)
            return 1;
    }
    return 0;
}

static int qt_already_received(const quic_pn_state_t *state,
                               uint64_t packet_number) {
    if (!state || !state->has_recv) return 0;
    if (packet_number > state->largest_recv) return 0;
    uint64_t distance = state->largest_recv - packet_number;
    if (distance >= 64) return 1;
    return (state->received_bitmap &
            (UINT64_C(1) << (unsigned)distance)) != 0;
}

static int qt_process_ack(struct neverc_quic_conn *conn, int space,
                          quic_frame_ack_t *ack, uint64_t now_ms) {
    if (!conn || !ack || space < 0 || space >= QUIC_PNS_COUNT) return -1;
    if (ack->largest_acked >= conn->pn[space].next_pn) return -1;
    uint64_t encoded_delay = ack->ack_delay;
    /* RFC 9000 §19.3: Initial/Handshake ACK Delay uses exponent 3. */
    uint64_t exponent = space == QUIC_PNS_APPLICATION ?
        conn->peer_params.ack_delay_exponent : 3;
    uint64_t delay_us = exponent >= 63 ||
                        encoded_delay > (UINT64_MAX >> exponent) ?
                            UINT64_MAX : encoded_delay << exponent;
    uint64_t delay_ms = delay_us == UINT64_MAX ? UINT64_MAX : delay_us / 1000U;
    /* RFC 9002 §5.3: do not subtract ACK delay until handshake confirmed. */
    if (space != QUIC_PNS_APPLICATION || !conn->handshake_confirmed)
        delay_ms = 0;
    int acknowledged_sent_packet = 0;
    for (size_t i = 0; i < QUIC_TX_RECORD_CAPACITY; i++) {
        quic_tx_record_t *record = &conn->tx_records[i];
        if (record->used && record->space == space &&
            qt_ack_contains(ack, record->packet_number)) {
            acknowledged_sent_packet = 1;
            neverc_quic_loss_mark_acked(&conn->loss, space,
                                        record->packet_number, now_ms);
            neverc_quic_conn_on_packet_acked(conn, space,
                                              record->packet_number);
        }
    }
    neverc_quic_loss_on_ack(&conn->loss, space, ack->largest_acked,
                            delay_ms, now_ms);
    if (conn->side == QUIC_SIDE_CLIENT && space == QUIC_PNS_HANDSHAKE &&
        acknowledged_sent_packet)
        conn->peer_completed_address_validation = 1;
    if (!conn->pn[space].largest_acked ||
        ack->largest_acked > conn->pn[space].largest_acked)
        conn->pn[space].largest_acked = ack->largest_acked;
    quic_sent_packet_t *sent = conn->loss.spaces[space].sent_packets;
    while (sent) {
        if (sent->lost)
            neverc_quic_conn_on_packet_lost(conn, space, sent->pkt_number);
        sent = sent->next;
    }
    neverc_quic_loss_cleanup(&conn->loss, space);
    return 0;
}

static int qt_queue_received_datagram(struct neverc_quic_conn *conn,
                                      const uint8_t *data, size_t length) {
    if (!conn || (!data && length != 0) ||
        conn->local_params.max_datagram_frame_size == 0 ||
        length > conn->local_params.max_datagram_frame_size ||
        conn->recv_datagram_count == QUIC_DATAGRAM_QUEUE_CAPACITY)
        return -1;
    uint8_t *copy = NULL;
    if (length) {
        copy = (uint8_t *)malloc(length);
        if (!copy) return -1;
        memcpy(copy, data, length);
    }
    quic_datagram_entry_t *entry =
        &conn->recv_datagrams[conn->recv_datagram_tail];
    entry->data = copy;
    entry->len = length;
    conn->recv_datagram_tail =
        (conn->recv_datagram_tail + 1U) % QUIC_DATAGRAM_QUEUE_CAPACITY;
    conn->recv_datagram_count++;
    nc_cond_broadcast(&conn->datagram_cond);
    return 0;
}

static int qt_handle_stop_sending(struct neverc_quic_conn *conn,
                                  const quic_frame_stop_sending_t *frame) {
    return neverc_quic_stream_apply_stop_sending_locked(
        conn, frame->stream_id, frame->error_code);
}

static int qt_handle_frames(struct neverc_quic_conn *conn,
                            quic_enc_level_t level, int space,
                            const uint8_t *payload, size_t payload_len,
                            const neverc_udp_addr_t *source,
                            int *ack_eliciting) {
    size_t position = 0;
    while (position < payload_len) {
        uint64_t frame_type;
        size_t type_len;
        if (neverc_quic_varint_decode(payload + position,
                                      payload_len - position,
                                      &frame_type, &type_len) != 0)
            return -1;
        if (!neverc_quic_frame_allowed(frame_type, level))
            return -1;
        size_t consumed = type_len;
        if (frame_type == QUIC_FRAME_PADDING) {
            position++;
            continue;
        }
        if (frame_type == QUIC_FRAME_PING) {
            *ack_eliciting = 1;
        } else if (frame_type == QUIC_FRAME_ACK ||
                   frame_type == QUIC_FRAME_ACK_ECN) {
            quic_frame_ack_t ack;
            memset(&ack, 0, sizeof(ack));
            if (neverc_quic_parse_ack_frame(payload + position,
                                            payload_len - position,
                                            &ack, &consumed) != 0)
                return -1;
            if (qt_process_ack(conn, space, &ack,
                               nc_monotonic_ms()) != 0) {
                free(ack.ranges);
                return -1;
            }
            free(ack.ranges);
        } else if (frame_type == QUIC_FRAME_CRYPTO) {
            quic_frame_crypto_t crypto;
            if (neverc_quic_parse_crypto_frame(payload + position,
                                               payload_len - position,
                                               &crypto, &consumed) != 0 ||
                neverc_quic_tls_receive_crypto(conn->tls, level,
                                               crypto.offset, crypto.data,
                                               crypto.data_len) != 0 ||
                neverc_quic_tls_process(conn->tls) != 0)
                return -1;
            *ack_eliciting = 1;
        } else if (frame_type >= QUIC_FRAME_STREAM_BASE &&
                   frame_type <= QUIC_FRAME_STREAM_BASE + 7U) {
            quic_frame_stream_t stream;
            if (neverc_quic_parse_stream_frame(payload + position,
                                               payload_len - position,
                                               &stream, &consumed) != 0 ||
                neverc_quic_stream_receive_locked(conn, &stream) != 0)
                return -1;
            *ack_eliciting = 1;
        } else if (frame_type == QUIC_FRAME_RESET_STREAM) {
            quic_frame_reset_stream_t reset;
            if (neverc_quic_parse_reset_stream(payload + position,
                                               payload_len - position,
                                               &reset, &consumed) != 0 ||
                neverc_quic_stream_receive_reset_locked(conn, &reset) != 0)
                return -1;
            *ack_eliciting = 1;
        } else if (frame_type == QUIC_FRAME_STOP_SENDING) {
            quic_frame_stop_sending_t stop;
            if (neverc_quic_parse_stop_sending(payload + position,
                                               payload_len - position,
                                               &stop, &consumed) != 0 ||
                qt_handle_stop_sending(conn, &stop) != 0)
                return -1;
            *ack_eliciting = 1;
        } else if (frame_type == QUIC_FRAME_MAX_DATA) {
            size_t cursor = position + type_len;
            uint64_t maximum;
            if (qt_decode_varint_at(payload, payload_len, &cursor,
                                    &maximum) != 0)
                return -1;
            if (neverc_quic_conn_apply_max_data_locked(conn, maximum) != 0)
                return -1;
            consumed = cursor - position;
            *ack_eliciting = 1;
        } else if (frame_type == QUIC_FRAME_MAX_STREAM_DATA) {
            size_t cursor = position + type_len;
            uint64_t stream_id;
            uint64_t maximum;
            if (qt_decode_varint_at(payload, payload_len, &cursor,
                                    &stream_id) != 0 ||
                qt_decode_varint_at(payload, payload_len, &cursor,
                                    &maximum) != 0)
                return -1;
            if (neverc_quic_stream_apply_max_stream_data_locked(
                    conn, stream_id, maximum) != 0)
                return -1;
            consumed = cursor - position;
            *ack_eliciting = 1;
        } else if (frame_type == QUIC_FRAME_MAX_STREAMS_BIDI ||
                   frame_type == QUIC_FRAME_MAX_STREAMS_UNI) {
            size_t cursor = position + type_len;
            uint64_t maximum;
            if (qt_decode_varint_at(payload, payload_len, &cursor,
                                    &maximum) != 0 || maximum > (UINT64_C(1) << 60))
                return -1;
            uint64_t *current = frame_type == QUIC_FRAME_MAX_STREAMS_BIDI ?
                &conn->peer_max_streams_bidi : &conn->peer_max_streams_uni;
            if (maximum > *current) *current = maximum;
            consumed = cursor - position;
            *ack_eliciting = 1;
        } else if (frame_type == QUIC_FRAME_NEW_CONNECTION_ID) {
            quic_frame_new_conn_id_t new_cid;
            if (neverc_quic_parse_new_conn_id(payload + position,
                                              payload_len - position,
                                              &new_cid, &consumed) != 0 ||
                neverc_quic_conn_add_peer_cid(conn, &new_cid) != 0)
                return -1;
            *ack_eliciting = 1;
        } else if (frame_type == QUIC_FRAME_RETIRE_CONNECTION_ID) {
            size_t cursor = position + type_len;
            uint64_t sequence;
            if (qt_decode_varint_at(payload, payload_len, &cursor,
                                    &sequence) != 0)
                return -1;
            if (neverc_quic_conn_retire_local_cid_locked(conn, sequence) != 0) {
                (void)neverc_quic_conn_close_locked(
                    conn, QUIC_ERR_PROTOCOL_VIOLATION,
                    "RETIRE_CONNECTION_ID for unused sequence", 0);
                return -1;
            }
            consumed = cursor - position;
            *ack_eliciting = 1;
        } else if (frame_type == QUIC_FRAME_PATH_CHALLENGE) {
            if (payload_len - position < type_len + 8U) return -1;
            memcpy(conn->path_response, payload + position + type_len, 8);
            conn->path_response_addr = *source;
            conn->path_response_pending = 1;
            consumed = type_len + 8U;
            *ack_eliciting = 1;
        } else if (frame_type == QUIC_FRAME_PATH_RESPONSE) {
            if (payload_len - position < type_len + 8U) return -1;
            if (conn->path_validation_pending &&
                qt_address_equal(source, &conn->candidate_addr) &&
                memcmp(payload + position + type_len,
                       conn->path_challenge, 8) == 0) {
                conn->peer_addr = conn->candidate_addr;
                memcpy(conn->remote_addr, conn->peer_addr.addr,
                       sizeof(conn->remote_addr));
                conn->remote_addr[sizeof(conn->remote_addr) - 1] = '\0';
                conn->path_validation_pending = 0;
                conn->path_challenge_pending = 0;
            }
            consumed = type_len + 8U;
            *ack_eliciting = 1;
        } else if (frame_type == QUIC_FRAME_CONNECTION_CLOSE ||
                   frame_type == QUIC_FRAME_CONNECTION_CLOSE_APP) {
            quic_frame_connection_close_t close_frame;
            if (neverc_quic_parse_connection_close(payload + position,
                                                    payload_len - position,
                                                    &close_frame,
                                                    &consumed) != 0)
                return -1;
            conn->close_error_code = close_frame.error_code;
            conn->close_is_app = close_frame.is_app;
            size_t reason_len = close_frame.reason_len;
            if (reason_len >= sizeof(conn->close_reason))
                reason_len = sizeof(conn->close_reason) - 1;
            memcpy(conn->close_reason, close_frame.reason, reason_len);
            conn->close_reason[reason_len] = '\0';
            conn->state = QUIC_CONN_DRAINING;
            conn->draining_started_ms = nc_monotonic_ms();
            conn->close_pending = 0;
            for (int i = 0; i < conn->n_streams; i++)
                quic_stream_mark_connection_closing(conn->streams[i]);
            nc_cond_broadcast(&conn->state_cond);
            nc_cond_broadcast(&conn->stream_avail_cond);
            nc_cond_broadcast(&conn->datagram_cond);
        } else if (frame_type == QUIC_FRAME_HANDSHAKE_DONE) {
            if (conn->side != QUIC_SIDE_CLIENT)
                return -1;
            conn->handshake_confirmed = 1;
            conn->peer_completed_address_validation = 1;
            consumed = type_len;
            *ack_eliciting = 1;
        } else if (frame_type == QUIC_FRAME_DATAGRAM ||
                   frame_type == QUIC_FRAME_DATAGRAM_LEN) {
            size_t cursor = position + type_len;
            size_t datagram_len;
            if (frame_type == QUIC_FRAME_DATAGRAM_LEN) {
                uint64_t encoded_len;
                if (qt_decode_varint_at(payload, payload_len, &cursor,
                                        &encoded_len) != 0 ||
                    encoded_len > payload_len - cursor)
                    return -1;
                datagram_len = (size_t)encoded_len;
            } else {
                datagram_len = payload_len - cursor;
            }
            if (qt_queue_received_datagram(conn, payload + cursor,
                                           datagram_len) != 0)
                return -1;
            cursor += datagram_len;
            consumed = cursor - position;
            *ack_eliciting = 1;
        } else if (frame_type == QUIC_FRAME_NEW_TOKEN) {
            /* RFC 9000 §19.7: clients receive NEW_TOKEN; servers MUST NOT. */
            if (conn->side != QUIC_SIDE_CLIENT)
                return -1;
            size_t cursor = position + type_len;
            uint64_t token_len;
            if (qt_decode_varint_at(payload, payload_len, &cursor,
                                    &token_len) != 0 ||
                token_len > payload_len - cursor)
                return -1;
            cursor += (size_t)token_len;
            consumed = cursor - position;
            *ack_eliciting = 1;
        } else if (frame_type == QUIC_FRAME_DATA_BLOCKED) {
            size_t cursor = position + type_len;
            uint64_t maximum;
            if (qt_decode_varint_at(payload, payload_len, &cursor,
                                    &maximum) != 0)
                return -1;
            consumed = cursor - position;
            *ack_eliciting = 1;
        } else if (frame_type == QUIC_FRAME_STREAM_DATA_BLOCKED) {
            size_t cursor = position + type_len;
            uint64_t stream_id, maximum;
            if (qt_decode_varint_at(payload, payload_len, &cursor,
                                    &stream_id) != 0 ||
                qt_decode_varint_at(payload, payload_len, &cursor,
                                    &maximum) != 0)
                return -1;
            consumed = cursor - position;
            *ack_eliciting = 1;
        } else if (frame_type == QUIC_FRAME_STREAMS_BLOCKED_BIDI ||
                   frame_type == QUIC_FRAME_STREAMS_BLOCKED_UNI) {
            size_t cursor = position + type_len;
            uint64_t maximum;
            if (qt_decode_varint_at(payload, payload_len, &cursor,
                                    &maximum) != 0)
                return -1;
            consumed = cursor - position;
            *ack_eliciting = 1;
        } else {
            return -1;
        }
        if (consumed == 0 || consumed > payload_len - position) return -1;
        position += consumed;
    }
    return 0;
}

static void qt_start_path_validation(struct neverc_quic_conn *conn,
                                     const neverc_udp_addr_t *source) {
    if (!conn || !source || conn->peer_disable_migration ||
        conn->side != QUIC_SIDE_SERVER ||
        qt_address_equal(source, &conn->peer_addr) ||
        (conn->path_validation_pending &&
         qt_address_equal(source, &conn->candidate_addr)))
        return;
    conn->candidate_addr = *source;
    if (neverc_crypto_rand_read(conn->path_challenge, 8) != 0) {
        (void)neverc_quic_conn_close_locked(conn, QUIC_ERR_NO_VIABLE_PATH,
                                            "path challenge RNG failed", 0);
        return;
    }
    conn->path_validation_pending = 1;
    conn->path_challenge_pending = 1;
}

static int qt_handle_version_negotiation(struct neverc_quic_conn *conn,
                                         const uint8_t *packet, size_t length,
                                         size_t *consumed) {
    *consumed = length;
    /* Servers ignore Version Negotiation; clients abandon unless the
     * packet lists the version already in use (RFC 9000 §6.2). */
    if (!conn || conn->side != QUIC_SIDE_CLIENT) return 0;
    if (neverc_quic_version_negotiation_supports(packet, length,
                                                 conn->version))
        return 0;
    conn->state = QUIC_CONN_CLOSED;
    conn->io_running = 0;
    conn->close_pending = 0;
    conn->close_is_app = 0;
    conn->close_error_code = QUIC_ERR_CONNECTION_REFUSED;
    memcpy(conn->close_reason, "peer rejected QUIC version", 27);
    conn->close_reason[26] = '\0';
    quic_conn_finalize_streams(conn);
    nc_cond_broadcast(&conn->state_cond);
    nc_cond_broadcast(&conn->stream_avail_cond);
    nc_cond_broadcast(&conn->datagram_cond);
    return 0;
}

static int qt_process_one_packet(struct neverc_quic_conn *conn,
                                 const uint8_t *packet, size_t length,
                                 const neverc_udp_addr_t *source,
                                 size_t *consumed) {
    if (!conn || !packet || !source || !consumed || length < 1) return -1;
    int is_long = (packet[0] & 0x80U) != 0;
    quic_packet_type_t protected_type = QUIC_PKT_1RTT;
    if (is_long) {
        if (length < 5) return -1;
        uint32_t version = ((uint32_t)packet[1] << 24) |
                           ((uint32_t)packet[2] << 16) |
                           ((uint32_t)packet[3] << 8) | packet[4];
        if (version == 0)
            return qt_handle_version_negotiation(conn, packet, length,
                                                 consumed);
        if (version != conn->version) return -1;
        protected_type = qt_long_type((packet[0] >> 4) & 3U, version);
        if (protected_type == QUIC_PKT_RETRY) return -1;
    }
    quic_enc_level_t level = qt_packet_level(protected_type);
    const quic_keys_t *keys = neverc_quic_tls_get_read_keys(conn->tls, level);
    if (!keys) return -1;
    size_t pn_offset;
    uint8_t dcid_len = conn->local_cids[0].len;
    if (neverc_quic_packet_number_offset(packet, length, dcid_len,
                                         &pn_offset) != 0)
        return -1;
    quic_packet_header_t header;
    uint8_t *copy = NULL;
    uint8_t *plaintext = NULL;
    size_t packet_len = 0;
    int space = 0;
    uint64_t packet_number = 0;
    quic_keys_t next_read_keys;
    int trying_next_keys = 0;

decrypt_attempt:
    free(copy);
    free(plaintext);
    copy = (uint8_t *)malloc(length);
    plaintext = NULL;
    if (!copy) goto decrypt_failed;
    memcpy(copy, packet, length);
    if (neverc_quic_remove_header_protection(keys->hp, copy, length,
                                             pn_offset) != 0)
        goto decrypt_retry;
    if (neverc_quic_parse_packet_header(copy, length, &header,
                                        dcid_len) != 0)
        goto decrypt_retry;
    if (header.type != protected_type || header.header_len > length ||
        header.payload_len > length - header.header_len)
        goto decrypt_retry;
    if (!is_long && header.key_phase !=
            (uint8_t)(neverc_quic_tls_get_read_key_phase(conn->tls) ^
                      trying_next_keys))
        goto decrypt_retry;
    packet_len = is_long ? header.header_len + header.payload_len : length;
    if (packet_len > length || header.payload_len < 16)
        goto decrypt_retry;
    space = qt_packet_space(header.type);
    uint64_t largest = conn->pn[space].has_recv ?
        conn->pn[space].largest_recv : 0;
    packet_number = neverc_quic_decode_packet_number(
        largest, header.pkt_number, (unsigned)header.pkt_number_len * 8U);
    if (packet_number > QUIC_VARINT_MAX)
        goto decrypt_failed;
    plaintext = (uint8_t *)malloc(header.payload_len - 16U + 1U);
    if (!plaintext) goto decrypt_failed;
    if (neverc_quic_decrypt_payload(keys, packet_number, copy,
                                    header.header_len,
                                    copy + header.header_len,
                                    header.payload_len, plaintext) != 0)
        goto decrypt_retry;
    if (trying_next_keys &&
        neverc_quic_tls_commit_read_key_update(conn->tls,
                                                &next_read_keys) != 0)
        goto decrypt_failed;
    neverc_platform_secure_zero(&next_read_keys, sizeof(next_read_keys));
    if ((is_long && (copy[0] & 0x0cU) != 0) ||
        (!is_long && (copy[0] & 0x18U) != 0)) {
        free(plaintext);
        free(copy);
        (void)neverc_quic_conn_close_locked(
            conn, QUIC_ERR_PROTOCOL_VIOLATION,
            "reserved bits must be zero", 0);
        return -1;
    }
    if (conn->side == QUIC_SIDE_CLIENT && is_long &&
        header.type == QUIC_PKT_INITIAL) {
        quic_conn_id_entry_t *peer = &conn->peer_cids[0];
        int still_initial_dcid =
            peer->len == conn->initial_dcid.len &&
            (peer->len == 0 ||
             memcmp(peer->id, conn->initial_dcid.data, peer->len) == 0);
        if (still_initial_dcid) {
            memset(peer, 0, sizeof(*peer));
            if (header.scid.len)
                memcpy(peer->id, header.scid.data, header.scid.len);
            peer->len = header.scid.len;
        } else if (peer->len != header.scid.len ||
                   (peer->len != 0 &&
                    memcmp(peer->id, header.scid.data, peer->len) != 0)) {
            goto decrypt_failed;
        }
    }
    goto decrypt_complete;

decrypt_retry:
    if (!is_long && !trying_next_keys &&
        neverc_quic_tls_is_established(conn->tls) &&
        neverc_quic_tls_prepare_read_key_update(conn->tls,
                                                 &next_read_keys) == 0) {
        trying_next_keys = 1;
        keys = &next_read_keys;
        goto decrypt_attempt;
    }
decrypt_failed:
    neverc_quic_tls_discard_read_key_update(conn->tls);
    neverc_platform_secure_zero(&next_read_keys, sizeof(next_read_keys));
    free(plaintext);
    free(copy);
    return -1;

decrypt_complete:
    if (conn->side == QUIC_SIDE_SERVER &&
        header.type == QUIC_PKT_HANDSHAKE)
        conn->address_validated = 1;
    if (!is_long) qt_start_path_validation(conn, source);
    int duplicate = qt_already_received(&conn->pn[space], packet_number);
    if (!duplicate) {
        int ack_eliciting = 0;
        size_t plaintext_len = header.payload_len - 16U;
        if (plaintext_len == 0) {
            free(plaintext);
            free(copy);
            if (conn->state != QUIC_CONN_DRAINING &&
                conn->state != QUIC_CONN_CLOSED)
                (void)neverc_quic_conn_close_locked(
                    conn, QUIC_ERR_PROTOCOL_VIOLATION,
                    "packet contained no frames", 0);
            return -1;
        }
        if (qt_handle_frames(conn, level, space, plaintext,
                             plaintext_len, source,
                             &ack_eliciting) != 0) {
            free(plaintext);
            free(copy);
            if (conn->state != QUIC_CONN_DRAINING &&
                conn->state != QUIC_CONN_CLOSED)
                (void)neverc_quic_conn_close_locked(
                    conn, QUIC_ERR_FRAME_ENCODING_ERROR,
                    "invalid QUIC frame", 0);
            return -1;
        }
        if (qt_mark_received(&conn->pn[space], packet_number) < 0) {
            free(plaintext);
            free(copy);
            return -1;
        }
        if (ack_eliciting) conn->pn[space].ack_pending = 1;
    }
    /* RFC 9001 §4.9.1: drop Initial keys after a Handshake packet. */
    if (header.type == QUIC_PKT_HANDSHAKE) {
        neverc_quic_tls_discard_keys(conn->tls, QUIC_ENC_INITIAL);
        conn->pn[QUIC_PNS_INITIAL].ack_pending = 0;
    }
    conn->last_activity_ms = nc_monotonic_ms();
    if (neverc_quic_tls_is_established(conn->tls) &&
        conn->state == QUIC_CONN_HANDSHAKING) {
        quic_conn_id_entry_t *peer = &conn->peer_cids[0];
        int transport_ids_valid =
            conn->peer_params.initial_scid_len == peer->len &&
            (peer->len == 0 ||
             memcmp(conn->peer_params.initial_scid, peer->id,
                    peer->len) == 0);
        if (conn->side == QUIC_SIDE_CLIENT) {
            transport_ids_valid = transport_ids_valid &&
                conn->peer_params.original_dcid_len ==
                    conn->initial_dcid.len &&
                memcmp(conn->peer_params.original_dcid,
                       conn->initial_dcid.data,
                       conn->initial_dcid.len) == 0;
        }
        if (!transport_ids_valid) {
            free(plaintext);
            free(copy);
            (void)neverc_quic_conn_close_locked(
                conn, QUIC_ERR_TRANSPORT_PARAMETER_ERROR,
                "peer connection IDs do not match transport parameters", 0);
            return -1;
        }
        if (conn->state != QUIC_CONN_HANDSHAKING) {
            free(plaintext);
            free(copy);
            *consumed = packet_len;
            return 0;
        }
        conn->state = QUIC_CONN_ESTABLISHED;
        conn->flow.max_data_peer = conn->peer_params.initial_max_data;
        conn->peer_max_streams_bidi =
            conn->peer_params.initial_max_streams_bidi;
        conn->peer_max_streams_uni =
            conn->peer_params.initial_max_streams_uni;
        conn->idle_timeout_ms = neverc_quic_effective_idle_timeout_ms(
            conn->idle_timeout_ms, conn->peer_params.max_idle_timeout);
        conn->peer_disable_migration =
            conn->peer_params.disable_active_migration;
        const char *alpn = neverc_quic_tls_alpn(conn->tls);
        if (alpn) {
            size_t alpn_len = strlen(alpn);
            if (alpn_len >= sizeof(conn->alpn)) alpn_len = sizeof(conn->alpn) - 1;
            memcpy(conn->alpn, alpn, alpn_len);
            conn->alpn[alpn_len] = '\0';
        }
        if (conn->side == QUIC_SIDE_SERVER) {
            conn->handshake_confirmed = 1;
            conn->handshake_done_pending = 1;
        }
        nc_cond_broadcast(&conn->state_cond);
    }
    free(plaintext);
    free(copy);
    *consumed = packet_len;
    return 0;
}

int neverc_quic_conn_process_datagram(struct neverc_quic_conn *conn,
                                      const uint8_t *packet, size_t length,
                                      const neverc_udp_addr_t *source) {
    if (!conn || !packet || length == 0 || !source)
        return -1;
    nc_mutex_lock(&conn->lock);
    if (conn->state == QUIC_CONN_CLOSED) {
        nc_mutex_unlock(&conn->lock);
        return -1;
    }
    if (conn->side == QUIC_SIDE_SERVER && !conn->address_validated) {
        if (conn->bytes_received_before_validation <= UINT64_MAX - length)
            conn->bytes_received_before_validation += length;
        else
            conn->bytes_received_before_validation = UINT64_MAX;
    }
    size_t position = 0;
    while (position < length) {
        size_t consumed;
        if (qt_process_one_packet(conn, packet + position, length - position,
                                  source, &consumed) != 0 || consumed == 0) {
            int send_close = conn->close_pending &&
                             conn->state != QUIC_CONN_CLOSED;
            nc_mutex_unlock(&conn->lock);
            if (send_close)
                (void)neverc_quic_conn_flush(conn);
            return -1;
        }
        position += consumed;
        if ((packet[position - consumed] & 0x80U) == 0) break;
    }
    int closed = conn->state == QUIC_CONN_CLOSED;
    nc_mutex_unlock(&conn->lock);
    if (closed) return 0;
    return neverc_quic_conn_flush(conn);
}

static int qt_write_varint(uint8_t *output, size_t capacity, size_t *position,
                           uint64_t value) {
    size_t written;
    if (!output || !position || *position > capacity ||
        neverc_quic_varint_encode(value, output + *position,
                                  capacity - *position, &written) != 0)
        return -1;
    *position += written;
    return 0;
}

static int qt_write_new_cid(struct neverc_quic_conn *conn, uint8_t *output,
                            size_t capacity, int cid_index,
                            size_t *written) {
    if (!conn || !output || !written ||
        cid_index < 0 || cid_index > conn->n_local_cids ||
        cid_index >= QUIC_MAX_LOCAL_CONN_IDS)
        return -1;
    quic_conn_id_entry_t *entry = &conn->local_cids[cid_index];
    if (cid_index == conn->n_local_cids && entry->len == 0) {
        memset(entry, 0, sizeof(*entry));
        entry->len = 8;
        entry->sequence = conn->next_local_cid_seq;
        if (neverc_crypto_rand_read(entry->id, entry->len) != 0 ||
            neverc_crypto_rand_read(entry->stateless_reset_token, 16) != 0) {
            memset(entry, 0, sizeof(*entry));
            return -1;
        }
    }
    size_t position = 0;
    if (qt_write_varint(output, capacity, &position,
                        QUIC_FRAME_NEW_CONNECTION_ID) != 0 ||
        qt_write_varint(output, capacity, &position, entry->sequence) != 0 ||
        qt_write_varint(output, capacity, &position, 0) != 0 ||
        capacity - position < 1U + entry->len + 16U)
        return -1;
    output[position++] = entry->len;
    memcpy(output + position, entry->id, entry->len);
    position += entry->len;
    memcpy(output + position, entry->stateless_reset_token, 16);
    position += 16;
    *written = position;
    return 0;
}

static quic_enc_level_t qt_close_level(struct neverc_quic_conn *conn) {
    if (neverc_quic_tls_get_write_keys(conn->tls, QUIC_ENC_APPLICATION))
        return QUIC_ENC_APPLICATION;
    if (neverc_quic_tls_get_write_keys(conn->tls, QUIC_ENC_HANDSHAKE))
        return QUIC_ENC_HANDSHAKE;
    return QUIC_ENC_INITIAL;
}

static int qt_build_control(struct neverc_quic_conn *conn, uint8_t *output,
                            size_t capacity, quic_send_meta_t *meta,
                            size_t *written) {
    size_t position = 0;
    if (conn->close_pending) {
        quic_frame_connection_close_t close_frame;
        memset(&close_frame, 0, sizeof(close_frame));
        int have_app = neverc_quic_tls_get_write_keys(
                           conn->tls, QUIC_ENC_APPLICATION) != NULL;
        /* RFC 9000 §12.4: 0x1d is 0-RTT/1-RTT only. Handshake closes use
         * 0x1c; an application close without 1-RTT keys is APPLICATION_ERROR. */
        close_frame.is_app = conn->close_is_app && have_app;
        close_frame.error_code = close_frame.is_app ? conn->close_error_code :
            (conn->close_is_app ? QUIC_ERR_APPLICATION_ERROR :
                                  conn->close_error_code);
        close_frame.reason = conn->close_reason;
        close_frame.reason_len = strlen(conn->close_reason);
        if (neverc_quic_write_connection_close(output, capacity,
                                               &close_frame,
                                               &position) != 0)
            return -1;
        meta->control_type = QUIC_FRAME_CONNECTION_CLOSE;
        meta->level = qt_close_level(conn);
    } else if (conn->path_response_pending) {
        if (capacity < 9) return -1;
        output[position++] = QUIC_FRAME_PATH_RESPONSE;
        memcpy(output + position, conn->path_response, 8);
        position += 8;
        meta->destination = &conn->path_response_addr;
        meta->control_type = QUIC_FRAME_PATH_RESPONSE;
    } else if (conn->path_challenge_pending) {
        if (capacity < 9) return -1;
        output[position++] = QUIC_FRAME_PATH_CHALLENGE;
        memcpy(output + position, conn->path_challenge, 8);
        position += 8;
        meta->destination = &conn->candidate_addr;
        meta->control_type = QUIC_FRAME_PATH_CHALLENGE;
    } else if (conn->handshake_done_pending) {
        if (neverc_quic_write_handshake_done(output, capacity,
                                             &position) != 0)
            return -1;
        meta->control_type = QUIC_FRAME_HANDSHAKE_DONE;
    } else if (conn->max_data_pending) {
        if (neverc_quic_write_max_data(output, capacity,
                                       conn->flow.max_data_local,
                                       &position) != 0)
            return -1;
        meta->control_type = QUIC_FRAME_MAX_DATA;
    } else if (conn->max_stream_data_pending) {
        quic_stream_t *stream = conn->max_stream_data_pending;
        if (neverc_quic_write_max_stream_data(output, capacity, stream->id,
                                              stream->recv_max_data,
                                              &position) != 0)
            return -1;
        meta->stream_id = stream->id;
        meta->control_type = QUIC_FRAME_MAX_STREAM_DATA;
    } else if (conn->new_cid_pending) {
        if (conn->new_cid_retransmit_index < 0)
            conn->new_cid_retransmit_index = conn->n_local_cids;
        if (qt_write_new_cid(conn, output, capacity,
                             conn->new_cid_retransmit_index,
                             &position) != 0)
            return -1;
        meta->stream_id = (uint64_t)conn->new_cid_retransmit_index;
        meta->control_type = QUIC_FRAME_NEW_CONNECTION_ID;
    } else {
        for (int i = 0; i < conn->n_peer_cids; i++) {
            if (!conn->peer_cids[i].retire_unsent) continue;
            if (neverc_quic_write_retire_conn_id(
                    output, capacity, conn->peer_cids[i].sequence,
                    &position) != 0)
                return -1;
            meta->stream_id = conn->peer_cids[i].sequence;
            meta->control_type = QUIC_FRAME_RETIRE_CONNECTION_ID;
            break;
        }
    }
    if (position == 0) {
        for (int i = 0; i < conn->n_streams; i++) {
            quic_stream_t *stream = conn->streams[i];
            if (!stream) continue;
            nc_mutex_lock(&stream->lock);
            if (stream->reset_pending) {
                int result = neverc_quic_write_reset_stream(
                    output, capacity, stream->id,
                    stream->reset_error_code,
                    stream->send_offset + stream->send_len, &position);
                nc_mutex_unlock(&stream->lock);
                if (result != 0) return -1;
                meta->stream_id = stream->id;
                meta->control_type = QUIC_FRAME_RESET_STREAM;
                break;
            }
            if (stream->stop_sending_pending) {
                if (qt_write_varint(output, capacity, &position,
                                    QUIC_FRAME_STOP_SENDING) != 0 ||
                    qt_write_varint(output, capacity, &position,
                                    stream->id) != 0 ||
                    qt_write_varint(output, capacity, &position,
                                    stream->stop_sending_error_code) != 0) {
                    nc_mutex_unlock(&stream->lock);
                    return -1;
                }
                nc_mutex_unlock(&stream->lock);
                meta->stream_id = stream->id;
                meta->control_type = QUIC_FRAME_STOP_SENDING;
                break;
            }
            nc_mutex_unlock(&stream->lock);
        }
    }
    if (position == 0) return 0;
    meta->kind = QUIC_TX_CONTROL;
    if (meta->control_type != QUIC_FRAME_CONNECTION_CLOSE)
        meta->level = QUIC_ENC_APPLICATION;
    meta->length = position;
    *written = position;
    return 1;
}

static int qt_build_crypto(struct neverc_quic_conn *conn,
                           quic_enc_level_t level, uint8_t *output,
                           size_t capacity, quic_send_meta_t *meta,
                           size_t *written) {
    uint64_t offset;
    const uint8_t *data;
    size_t length;
    if (!neverc_quic_tls_get_write_keys(conn->tls, level) ||
        neverc_quic_tls_get_crypto_data(conn->tls, level, &offset,
                                        &data, &length) != 0 || length == 0)
        return 0;
    size_t overhead = 1U + neverc_quic_varint_len(offset) + 8U;
    if (capacity <= overhead) return -1;
    if (length > capacity - overhead) length = capacity - overhead;
    size_t frame_len;
    if (neverc_quic_write_crypto_frame(output, capacity, offset, data,
                                       length, &frame_len) != 0)
        return -1;
    meta->kind = QUIC_TX_CRYPTO;
    meta->level = level;
    meta->offset = offset;
    meta->length = length;
    *written = frame_len;
    return 1;
}

static int qt_build_stream(struct neverc_quic_conn *conn, uint8_t *output,
                           size_t capacity, quic_send_meta_t *meta,
                           size_t *written) {
    for (int i = 0; i < conn->n_streams; i++) {
        quic_stream_t *stream = conn->streams[i];
        if (!stream) continue;
        nc_mutex_lock(&stream->lock);
        if (stream->send_inflight ||
            (stream->send_len == 0 &&
             (!stream->send_fin || stream->send_fin_sent))) {
            nc_mutex_unlock(&stream->lock);
            continue;
        }
        size_t overhead = 1U + neverc_quic_varint_len(stream->id) +
                          neverc_quic_varint_len(stream->send_offset) + 8U;
        if (capacity <= overhead) {
            nc_mutex_unlock(&stream->lock);
            return -1;
        }
        size_t length = stream->send_len;
        if (length > capacity - overhead) length = capacity - overhead;
        int is_new_data = stream->send_offset == stream->send_highest;
        if (is_new_data) {
            uint64_t available = conn->flow.max_data_peer > conn->flow.data_sent ?
                conn->flow.max_data_peer - conn->flow.data_sent : 0;
            if (length > available) length = (size_t)available;
        }
        int fin = stream->send_fin && length == stream->send_len;
        if (length == 0 && !fin) {
            nc_mutex_unlock(&stream->lock);
            continue;
        }
        quic_frame_stream_t frame;
        memset(&frame, 0, sizeof(frame));
        frame.stream_id = stream->id;
        frame.offset = stream->send_offset;
        frame.data = stream->send_buf;
        frame.data_len = length;
        frame.fin = fin;
        size_t frame_len;
        if (neverc_quic_write_stream_frame(output, capacity, &frame,
                                           &frame_len) != 0) {
            nc_mutex_unlock(&stream->lock);
            return -1;
        }
        meta->kind = QUIC_TX_STREAM;
        meta->level = QUIC_ENC_APPLICATION;
        meta->offset = stream->send_offset;
        meta->stream_id = stream->id;
        meta->length = length;
        meta->fin = fin;
        *written = frame_len;
        nc_mutex_unlock(&stream->lock);
        return 1;
    }
    return 0;
}

static int qt_build_datagram(struct neverc_quic_conn *conn, uint8_t *output,
                             size_t capacity, quic_send_meta_t *meta,
                             size_t *written) {
    if (conn->send_datagram_count == 0) return 0;
    quic_datagram_entry_t *entry =
        &conn->send_datagrams[conn->send_datagram_head];
    size_t position = 0;
    if (qt_write_varint(output, capacity, &position,
                        QUIC_FRAME_DATAGRAM_LEN) != 0 ||
        qt_write_varint(output, capacity, &position, entry->len) != 0 ||
        entry->len > capacity - position)
        return -1;
    memcpy(output + position, entry->data, entry->len);
    position += entry->len;
    meta->kind = QUIC_TX_CONTROL;
    meta->level = QUIC_ENC_APPLICATION;
    meta->control_type = QUIC_FRAME_DATAGRAM_LEN;
    meta->length = position;
    *written = position;
    return 1;
}

static int qt_build_pto_probe(struct neverc_quic_conn *conn,
                              uint8_t *output, size_t capacity,
                              quic_send_meta_t *meta, size_t *written) {
    if (conn->pto_probe_pending <= 0) return 0;
    if (!neverc_quic_tls_get_write_keys(conn->tls,
                                        conn->pto_probe_level)) {
        conn->pto_probe_pending = 0;
        return 0;
    }
    if (neverc_quic_write_ping(output, capacity, written) != 0)
        return -1;
    meta->kind = QUIC_TX_CONTROL;
    meta->level = conn->pto_probe_level;
    meta->control_type = QUIC_FRAME_PING;
    meta->length = *written;
    return 1;
}

static int qt_build_item(struct neverc_quic_conn *conn, uint8_t *output,
                         size_t capacity, quic_send_meta_t *meta,
                         size_t *written) {
    memset(meta, 0, sizeof(*meta));
    meta->destination = &conn->peer_addr;
    if (conn->state == QUIC_CONN_DRAINING && !conn->close_pending)
        return 0;
    if (conn->close_pending)
        return qt_build_control(conn, output, capacity, meta, written);
    int result = qt_build_pto_probe(conn, output, capacity, meta, written);
    if (result != 0) return result;
    result = qt_build_crypto(conn, QUIC_ENC_INITIAL, output, capacity,
                             meta, written);
    if (result != 0) return result;
    result = qt_build_crypto(conn, QUIC_ENC_HANDSHAKE, output, capacity,
                             meta, written);
    if (result != 0) return result;
    if (neverc_quic_tls_get_write_keys(conn->tls, QUIC_ENC_APPLICATION)) {
        result = qt_build_control(conn, output, capacity, meta, written);
        if (result != 0) return result;
        result = qt_build_datagram(conn, output, capacity, meta, written);
        if (result != 0) return result;
        result = qt_build_stream(conn, output, capacity, meta, written);
        if (result != 0) return result;
    }
    for (int level = QUIC_ENC_INITIAL; level <= QUIC_ENC_APPLICATION; level++) {
        int space = level == QUIC_ENC_INITIAL ? QUIC_PNS_INITIAL :
                    level == QUIC_ENC_HANDSHAKE ? QUIC_PNS_HANDSHAKE :
                                                  QUIC_PNS_APPLICATION;
        if (conn->pn[space].ack_pending &&
            neverc_quic_tls_get_write_keys(conn->tls,
                                            (quic_enc_level_t)level)) {
            if (qt_write_ack(&conn->pn[space], output, capacity,
                             written) != 0)
                return -1;
            meta->level = (quic_enc_level_t)level;
            meta->ack_only = 1;
            meta->destination = &conn->peer_addr;
            return 1;
        }
    }
    return 0;
}

static int qt_prepend_ack(struct neverc_quic_conn *conn,
                          quic_send_meta_t *meta, uint8_t *payload,
                          size_t *payload_len, size_t capacity,
                          int *included_ack) {
    int space = meta->level == QUIC_ENC_INITIAL ? QUIC_PNS_INITIAL :
                meta->level == QUIC_ENC_HANDSHAKE ? QUIC_PNS_HANDSHAKE :
                                                    QUIC_PNS_APPLICATION;
    *included_ack = 0;
    if (meta->ack_only) {
        *included_ack = 1;
        return 0;
    }
    if (!conn->pn[space].ack_pending) return 0;
    uint8_t ack_buffer[1024];
    size_t ack_len;
    if (qt_write_ack(&conn->pn[space], ack_buffer, sizeof(ack_buffer),
                     &ack_len) != 0 || ack_len > capacity - *payload_len)
        return 0;
    memmove(payload + ack_len, payload, *payload_len);
    memcpy(payload, ack_buffer, ack_len);
    *payload_len += ack_len;
    *included_ack = 1;
    return 0;
}

static quic_tx_record_t *qt_alloc_tx_record(
    struct neverc_quic_conn *conn) {
    for (size_t i = 0; i < QUIC_TX_RECORD_CAPACITY; i++) {
        if (!conn->tx_records[i].used) return &conn->tx_records[i];
    }
    return NULL;
}

static void qt_commit_control(struct neverc_quic_conn *conn,
                              const quic_send_meta_t *meta) {
    switch (meta->control_type) {
    case QUIC_FRAME_CONNECTION_CLOSE:
        conn->close_pending = 0;
        break;
    case QUIC_FRAME_PATH_RESPONSE:
        conn->path_response_pending = 0;
        break;
    case QUIC_FRAME_PATH_CHALLENGE:
        conn->path_challenge_pending = 0;
        break;
    case QUIC_FRAME_HANDSHAKE_DONE:
        conn->handshake_done_pending = 0;
        break;
    case QUIC_FRAME_MAX_DATA:
        conn->max_data_pending = 0;
        break;
    case QUIC_FRAME_MAX_STREAM_DATA:
        if (conn->max_stream_data_pending &&
            conn->max_stream_data_pending->id == meta->stream_id)
            conn->max_stream_data_pending = NULL;
        break;
    case QUIC_FRAME_NEW_CONNECTION_ID:
        if (meta->stream_id == (uint64_t)conn->n_local_cids &&
            conn->n_local_cids < QUIC_MAX_LOCAL_CONN_IDS) {
            conn->n_local_cids++;
            conn->next_local_cid_seq++;
        }
        conn->new_cid_pending = 0;
        conn->new_cid_retransmit_index = -1;
        break;
    case QUIC_FRAME_PING:
        if (conn->pto_probe_pending > 0) conn->pto_probe_pending--;
        break;
    case QUIC_FRAME_RESET_STREAM: {
        quic_stream_t *stream = neverc_quic_conn_find_stream(
            conn, meta->stream_id);
        if (stream) {
            nc_mutex_lock(&stream->lock);
            stream->reset_pending = 0;
            nc_mutex_unlock(&stream->lock);
        }
        break;
    }
    case QUIC_FRAME_STOP_SENDING: {
        quic_stream_t *stream = neverc_quic_conn_find_stream(
            conn, meta->stream_id);
        if (stream) {
            nc_mutex_lock(&stream->lock);
            stream->stop_sending_pending = 0;
            nc_mutex_unlock(&stream->lock);
        }
        break;
    }
    case QUIC_FRAME_RETIRE_CONNECTION_ID:
        for (int i = 0; i < conn->n_peer_cids; i++) {
            if (conn->peer_cids[i].sequence == meta->stream_id)
                conn->peer_cids[i].retire_unsent = 0;
        }
        break;
    case QUIC_FRAME_DATAGRAM_LEN: {
        if (conn->send_datagram_count) {
            quic_datagram_entry_t *entry =
                &conn->send_datagrams[conn->send_datagram_head];
            free(entry->data);
            entry->data = NULL;
            entry->len = 0;
            conn->send_datagram_head = (conn->send_datagram_head + 1U) %
                                       QUIC_DATAGRAM_QUEUE_CAPACITY;
            conn->send_datagram_count--;
        }
        break;
    }
    default:
        break;
    }
}

static int qt_write_short_header(struct neverc_quic_conn *conn,
                                 uint8_t *packet, size_t capacity,
                                 uint64_t packet_number,
                                 uint8_t packet_number_len,
                                 size_t *header_len) {
    quic_conn_id_entry_t *dcid =
        &conn->peer_cids[conn->active_peer_cid_idx];
    size_t needed = 1U + dcid->len + packet_number_len;
    if (capacity < needed) return -1;
    size_t position = 0;
    packet[position++] = 0x40U |
        ((uint8_t)neverc_quic_tls_get_key_phase(conn->tls) << 2) |
        (packet_number_len - 1U);
    memcpy(packet + position, dcid->id, dcid->len);
    position += dcid->len;
    for (uint8_t i = 0; i < packet_number_len; i++)
        packet[position + i] = (uint8_t)(packet_number >>
            (8U * (packet_number_len - i - 1U)));
    position += packet_number_len;
    *header_len = position;
    return 0;
}

static int qt_send_item(struct neverc_quic_conn *conn,
                        uint8_t *payload, size_t payload_len,
                        quic_send_meta_t *meta) {
    int space = meta->level == QUIC_ENC_INITIAL ? QUIC_PNS_INITIAL :
                meta->level == QUIC_ENC_HANDSHAKE ? QUIC_PNS_HANDSHAKE :
                                                    QUIC_PNS_APPLICATION;
    uint64_t packet_number = conn->pn[space].next_pn;
    if (packet_number > QUIC_VARINT_MAX) return -1;
    uint8_t pn_len = 4;
    size_t maximum = (size_t)conn->local_params.max_udp_payload_size;
    if (conn->peer_params.max_udp_payload_size >= QUIC_MIN_INITIAL_SIZE &&
        conn->peer_params.max_udp_payload_size < maximum)
        maximum = (size_t)conn->peer_params.max_udp_payload_size;
    if (maximum < QUIC_MIN_INITIAL_SIZE) maximum = QUIC_MIN_INITIAL_SIZE;
    if (maximum > QUIC_MAX_PACKET_SIZE) maximum = QUIC_MAX_PACKET_SIZE;
    uint8_t *packet = (uint8_t *)malloc(maximum);
    if (!packet) return -1;
    int included_ack;
    (void)qt_prepend_ack(conn, meta, payload, &payload_len,
                         maximum - 64U, &included_ack);
    size_t header_len;
    if (meta->level == QUIC_ENC_APPLICATION) {
        if (qt_write_short_header(conn, packet, maximum, packet_number,
                                  pn_len, &header_len) != 0) {
            free(packet);
            return -1;
        }
    } else {
        quic_packet_header_t header;
        memset(&header, 0, sizeof(header));
        header.type = meta->level == QUIC_ENC_INITIAL ?
            QUIC_PKT_INITIAL : QUIC_PKT_HANDSHAKE;
        header.version = conn->version;
        quic_conn_id_entry_t *dcid =
            &conn->peer_cids[conn->active_peer_cid_idx];
        memcpy(header.dcid.data, dcid->id, dcid->len);
        header.dcid.len = dcid->len;
        memcpy(header.scid.data, conn->local_cids[0].id,
               conn->local_cids[0].len);
        header.scid.len = conn->local_cids[0].len;
        header.pkt_number = packet_number;
        header.pkt_number_len = pn_len;
        header.payload_len = payload_len + 16U;
        if (neverc_quic_write_long_header(packet, maximum, &header,
                                          &header_len) != 0) {
            free(packet);
            return -1;
        }
        if (meta->level == QUIC_ENC_INITIAL &&
            header_len + payload_len + 16U < QUIC_MIN_INITIAL_SIZE) {
            size_t original_payload_len = payload_len;
            size_t padded_payload_len = payload_len;
            for (int iteration = 0; iteration < 4; iteration++) {
                header.payload_len = padded_payload_len + 16U;
                if (neverc_quic_write_long_header(
                        packet, maximum, &header, &header_len) != 0 ||
                    header_len + 16U > QUIC_MIN_INITIAL_SIZE) {
                    free(packet);
                    return -1;
                }
                size_t required_payload_len =
                    QUIC_MIN_INITIAL_SIZE - header_len - 16U;
                if (required_payload_len < original_payload_len ||
                    required_payload_len > maximum) {
                    free(packet);
                    return -1;
                }
                if (required_payload_len == padded_payload_len) break;
                padded_payload_len = required_payload_len;
            }
            memset(payload + original_payload_len, 0,
                   padded_payload_len - original_payload_len);
            payload_len = padded_payload_len;
            header.payload_len = payload_len + 16U;
            if (neverc_quic_write_long_header(packet, maximum, &header,
                                              &header_len) != 0 ||
                header_len + payload_len + 16U != QUIC_MIN_INITIAL_SIZE) {
                free(packet);
                return -1;
            }
        }
    }
    if (header_len > maximum - 16U ||
        payload_len > maximum - header_len - 16U) {
        free(packet);
        return -1;
    }
    const quic_keys_t *keys = neverc_quic_tls_get_write_keys(conn->tls,
                                                              meta->level);
    if (!keys) {
        free(packet);
        return -1;
    }
    if (neverc_quic_encrypt_payload(keys, packet_number,
                                    packet, header_len,
                                    payload, payload_len,
                                    packet + header_len) != 0) {
        free(packet);
        return -1;
    }
    size_t packet_len = header_len + payload_len + 16U;
    size_t pn_offset = header_len - pn_len;
    if (neverc_quic_apply_header_protection(keys->hp, packet,
                                            packet_len, pn_offset) != 0) {
        free(packet);
        return -1;
    }
    if (conn->side == QUIC_SIDE_SERVER && !conn->address_validated) {
        uint64_t limit = conn->bytes_received_before_validation > UINT64_MAX / 3U ?
            UINT64_MAX : conn->bytes_received_before_validation * 3U;
        if (conn->bytes_sent_before_validation > limit ||
            packet_len > limit - conn->bytes_sent_before_validation) {
            free(packet);
            return 1;
        }
    }
    quic_tx_record_t *record = NULL;
    int ack_eliciting = !meta->ack_only;
    if (ack_eliciting && !(record = qt_alloc_tx_record(conn))) {
        free(packet);
        return -1;
    }
    neverc_net_result_t result = neverc_udp_try_write(
        conn->udp, packet, packet_len,
        conn->endpoint ? meta->destination : NULL);
    free(packet);
    if (result.status != NEVERC_NET_OK || result.transferred != packet_len)
        return result.status == NEVERC_NET_WOULD_BLOCK ? 1 : -1;
    if (conn->side == QUIC_SIDE_SERVER && !conn->address_validated)
        conn->bytes_sent_before_validation += packet_len;
    conn->pn[space].next_pn++;
    if (included_ack) conn->pn[space].ack_pending = 0;
    if (ack_eliciting) {
        memset(record, 0, sizeof(*record));
        record->used = 1;
        record->space = space;
        record->packet_number = packet_number;
        record->sent_at_ms = nc_monotonic_ms();
        record->packet_bytes = packet_len;
        record->kind = meta->kind;
        record->level = meta->level;
        record->offset = meta->kind == QUIC_TX_CONTROL ?
            (uint64_t)meta->control_type : meta->offset;
        record->length = meta->length;
        record->stream_id = meta->stream_id;
        record->fin = meta->fin;
        neverc_quic_loss_on_sent(&conn->loss, space, packet_number,
                                 record->sent_at_ms, packet_len, 1);
        if (meta->kind == QUIC_TX_CRYPTO)
            neverc_quic_tls_crypto_data_sent(conn->tls, meta->level,
                                             meta->length);
        else if (meta->kind == QUIC_TX_STREAM) {
            quic_stream_t *stream = neverc_quic_conn_find_stream(
                conn, meta->stream_id);
            if (stream) {
                nc_mutex_lock(&stream->lock);
                if (stream->send_offset == stream->send_highest) {
                    conn->flow.data_sent += meta->length;
                    stream->send_highest += meta->length;
                }
                stream->send_inflight = meta->length;
                stream->send_inflight_pn = packet_number;
                if (meta->fin) stream->send_fin_sent = 1;
                nc_mutex_unlock(&stream->lock);
            }
        } else if (meta->kind == QUIC_TX_CONTROL) {
            qt_commit_control(conn, meta);
        }
    }
    conn->last_activity_ms = nc_monotonic_ms();
    return 0;
}

int neverc_quic_conn_flush(struct neverc_quic_conn *conn) {
    if (!conn || !conn->udp || !conn->tls ||
        conn->state == QUIC_CONN_CLOSED)
        return -1;
    nc_mutex_lock(&conn->lock);
    int result = 0;
    for (int attempts = 0; attempts < 32; attempts++) {
        size_t maximum = (size_t)conn->local_params.max_udp_payload_size;
        if (maximum < QUIC_MIN_INITIAL_SIZE) maximum = QUIC_MIN_INITIAL_SIZE;
        if (maximum > QUIC_MAX_PACKET_SIZE) maximum = QUIC_MAX_PACKET_SIZE;
        uint8_t *payload = (uint8_t *)malloc(maximum);
        if (!payload) {
            result = -1;
            break;
        }
        quic_send_meta_t meta;
        size_t payload_len = 0;
        int congestion_blocked =
            conn->loss.cc.bytes_in_flight >= conn->loss.cc.congestion_window;
        int built;
        if (conn->state == QUIC_CONN_DRAINING && !conn->close_pending) {
            free(payload);
            break;
        }
        if (conn->close_pending) {
            memset(&meta, 0, sizeof(meta));
            meta.destination = &conn->peer_addr;
            built = qt_build_control(conn, payload,
                                     maximum - QUIC_PACKET_OVERHEAD_RESERVE,
                                     &meta, &payload_len);
        } else if (congestion_blocked && conn->pto_probe_pending > 0) {
            memset(&meta, 0, sizeof(meta));
            meta.destination = &conn->peer_addr;
            built = qt_build_pto_probe(conn, payload,
                                       maximum - QUIC_PACKET_OVERHEAD_RESERVE,
                                       &meta, &payload_len);
        } else if (congestion_blocked) {
            built = 0;
            for (int level = QUIC_ENC_INITIAL;
                 level <= QUIC_ENC_APPLICATION; level++) {
                int space = level == QUIC_ENC_INITIAL ? QUIC_PNS_INITIAL :
                    level == QUIC_ENC_HANDSHAKE ? QUIC_PNS_HANDSHAKE :
                                                  QUIC_PNS_APPLICATION;
                if (conn->pn[space].ack_pending &&
                    neverc_quic_tls_get_write_keys(
                        conn->tls, (quic_enc_level_t)level)) {
                    memset(&meta, 0, sizeof(meta));
                    meta.level = (quic_enc_level_t)level;
                    meta.ack_only = 1;
                    meta.destination = &conn->peer_addr;
                    built = qt_write_ack(
                        &conn->pn[space], payload,
                        maximum - QUIC_PACKET_OVERHEAD_RESERVE,
                                         &payload_len) == 0 ? 1 : -1;
                    break;
                }
            }
        } else {
            built = qt_build_item(conn, payload,
                                  maximum - QUIC_PACKET_OVERHEAD_RESERVE,
                                  &meta, &payload_len);
        }
        if (built <= 0) {
            free(payload);
            if (built < 0) result = -1;
            break;
        }
        int sent = qt_send_item(conn, payload, payload_len, &meta);
        free(payload);
        if (sent != 0) {
            result = sent < 0 ? -1 : 0;
            break;
        }
    }
    if (result < 0 && conn->state != QUIC_CONN_DRAINING) {
        size_t length = strlen("QUIC packet send failed");
        memcpy(conn->error, "QUIC packet send failed", length + 1U);
    }
    nc_mutex_unlock(&conn->lock);
    return result;
}

void neverc_quic_conn_tick(struct neverc_quic_conn *conn, uint64_t now_ms) {
    if (!conn) return;
    nc_mutex_lock(&conn->lock);
    if (conn->state == QUIC_CONN_CLOSED) {
        nc_mutex_unlock(&conn->lock);
        return;
    }
    if (conn->state == QUIC_CONN_DRAINING) {
        int send_close = conn->close_pending;
        uint64_t pto = conn->loss.rtt.has_sample ?
            conn->loss.rtt.smoothed_rtt + 4U * conn->loss.rtt.rttvar +
                conn->loss.rtt.max_ack_delay : 1000U;
        if (pto < 100U) pto = 100U;
        if (now_ms >= conn->draining_started_ms &&
            now_ms - conn->draining_started_ms >= 3U * pto) {
            conn->state = QUIC_CONN_CLOSED;
            conn->io_running = 0;
            send_close = 0;
            quic_conn_finalize_streams(conn);
            nc_cond_broadcast(&conn->state_cond);
        }
        nc_mutex_unlock(&conn->lock);
        if (send_close)
            (void)neverc_quic_conn_flush(conn);
        return;
    }
    if (conn->idle_timeout_ms != 0 &&
        now_ms >= conn->last_activity_ms &&
        now_ms - conn->last_activity_ms >= conn->idle_timeout_ms) {
        int changed = neverc_quic_conn_close_locked(
            conn, 0, "idle timeout", 0);
        nc_mutex_unlock(&conn->lock);
        if (changed) (void)neverc_quic_conn_flush(conn);
        return;
    }
    uint64_t timeout = neverc_quic_conn_loss_timeout(conn, now_ms);
    if (timeout && now_ms >= timeout) {
        int detected_loss = neverc_quic_loss_detect(&conn->loss, now_ms);
        if (detected_loss) {
            for (int space = 0; space < QUIC_PN_SPACE_COUNT; space++) {
                quic_sent_packet_t *sent =
                    conn->loss.spaces[space].sent_packets;
                while (sent) {
                    if (sent->lost)
                        neverc_quic_conn_on_packet_lost(
                            conn, space, sent->pkt_number);
                    sent = sent->next;
                }
                neverc_quic_loss_cleanup(&conn->loss, space);
            }
        } else {
            conn->validation_pto_deadline_ms = 0;
            conn->loss.pto_count++;
            if (conn->loss.pto_count > 8) {
                int changed = neverc_quic_conn_close_locked(
                    conn, QUIC_ERR_NO_VIABLE_PATH,
                    "PTO limit exceeded", 0);
                nc_mutex_unlock(&conn->lock);
                if (changed) (void)neverc_quic_conn_flush(conn);
                return;
            }
            conn->pto_probe_pending = 2;
            conn->pto_probe_level = QUIC_ENC_INITIAL;
            if (!neverc_quic_conn_has_ack_eliciting_in_flight(conn) &&
                conn->side == QUIC_SIDE_CLIENT &&
                !conn->peer_completed_address_validation) {
                if (neverc_quic_tls_get_write_keys(
                        conn->tls, QUIC_ENC_HANDSHAKE))
                    conn->pto_probe_level = QUIC_ENC_HANDSHAKE;
                else if (!neverc_quic_tls_get_write_keys(
                             conn->tls, QUIC_ENC_INITIAL))
                    conn->pto_probe_pending = 0;
            } else {
                conn->pto_probe_pending = 0;
                for (int space = QUIC_PNS_APPLICATION;
                     space >= QUIC_PNS_INITIAL; space--) {
                    if (!conn->loss.spaces[space].sent_packets) continue;
                    quic_enc_level_t level =
                        space == QUIC_PNS_APPLICATION ?
                            QUIC_ENC_APPLICATION :
                        space == QUIC_PNS_HANDSHAKE ?
                            QUIC_ENC_HANDSHAKE : QUIC_ENC_INITIAL;
                    if (!neverc_quic_tls_get_write_keys(conn->tls, level))
                        continue;
                    conn->pto_probe_level = level;
                    conn->pto_probe_pending = 2;
                    break;
                }
            }
        }
    }
    nc_mutex_unlock(&conn->lock);
    (void)neverc_quic_conn_flush(conn);
}
