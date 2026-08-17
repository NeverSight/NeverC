/* QUIC connection, stream, and flow-control state (RFC 9000). */

#include "_quic_internal.h"
#include "../_net_thread.h"

#include "neverc/std/crypto/rand.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define QUIC_STREAM_BUFFER_MIN 4096U

static void quic_set_error(struct neverc_quic_conn *conn,
                           const char *message) {
    if (!conn || !message) return;
    size_t length = strlen(message);
    if (length >= sizeof(conn->error)) length = sizeof(conn->error) - 1;
    memcpy(conn->error, message, length);
    conn->error[length] = '\0';
}

static int quic_add_u64(uint64_t left, uint64_t right, uint64_t *result) {
    if (!result || left > UINT64_MAX - right) return -1;
    *result = left + right;
    return 0;
}

static int quic_reserve(uint8_t **buffer, size_t *capacity, size_t needed,
                        size_t limit) {
    if (!buffer || !capacity || needed > limit) return -1;
    if (*capacity >= needed) return 0;
    size_t next = *capacity ? *capacity : QUIC_STREAM_BUFFER_MIN;
    while (next < needed) {
        if (next > limit / 2) {
            next = limit;
            break;
        }
        next *= 2;
    }
    uint8_t *grown = (uint8_t *)realloc(*buffer, next);
    if (!grown) return -1;
    *buffer = grown;
    *capacity = next;
    return 0;
}

static int quic_generate_cid_entry(quic_conn_id_entry_t *entry,
                                   uint64_t sequence) {
    if (!entry) return -1;
    memset(entry, 0, sizeof(*entry));
    entry->len = 8;
    entry->sequence = sequence;
    if (neverc_crypto_rand_read(entry->id, entry->len) != 0 ||
        neverc_crypto_rand_read(entry->stateless_reset_token,
                                sizeof(entry->stateless_reset_token)) != 0) {
        memset(entry, 0, sizeof(*entry));
        return -1;
    }
    return 0;
}

static int conn_add_local_cid(struct neverc_quic_conn *conn) {
    if (!conn || conn->n_local_cids >= QUIC_MAX_LOCAL_CONN_IDS) return -1;
    quic_conn_id_entry_t *entry = &conn->local_cids[conn->n_local_cids];
    if (quic_generate_cid_entry(entry, conn->next_local_cid_seq) != 0)
        return -1;
    conn->next_local_cid_seq++;
    conn->n_local_cids++;
    return 0;
}

static int stream_is_local(const struct neverc_quic_conn *conn,
                           uint64_t stream_id) {
    if (!conn) return 0;
    int initiator_is_server = (stream_id & 1U) != 0;
    return initiator_is_server == (conn->side == QUIC_SIDE_SERVER);
}

static int stream_is_uni(uint64_t stream_id) {
    return (stream_id & 2U) != 0;
}

static uint64_t stream_ordinal(uint64_t stream_id) {
    return stream_id / 4U + 1U;
}

static quic_stream_t *stream_create(struct neverc_quic_conn *conn,
                                    uint64_t id, int peer_initiated) {
    if (!conn) return NULL;
    quic_stream_t *stream = (quic_stream_t *)calloc(1, sizeof(*stream));
    if (!stream) return NULL;
    stream->conn = conn;
    stream->id = id;
    stream->state = QUIC_STREAM_OPEN;
    stream->peer_initiated = peer_initiated;

    uint64_t receive_limit;
    uint64_t send_limit;
    if (stream_is_uni(id)) {
        receive_limit = conn->local_params.initial_max_stream_data_uni;
        send_limit = conn->peer_params.initial_max_stream_data_uni;
    } else if (peer_initiated) {
        receive_limit =
            conn->local_params.initial_max_stream_data_bidi_remote;
        send_limit = conn->peer_params.initial_max_stream_data_bidi_local;
    } else {
        receive_limit = conn->local_params.initial_max_stream_data_bidi_local;
        send_limit = conn->peer_params.initial_max_stream_data_bidi_remote;
    }
    /* RFC 9000 §4.1: a missing or zero transport parameter is a zero
     * limit. Substituting a default would violate flow control. */
    stream->recv_max_data = receive_limit;
    stream->send_max_data = send_limit;

    nc_mutex_init(&stream->lock);
    nc_cond_init(&stream->read_cond);
    nc_cond_init(&stream->write_cond);
    return stream;
}

static void fragment_list_destroy(quic_fragment_t *fragment) {
    while (fragment) {
        quic_fragment_t *next = fragment->next;
        free(fragment->data);
        free(fragment);
        fragment = next;
    }
}

static void stream_destroy(quic_stream_t *stream) {
    if (!stream) return;
    nc_mutex_destroy(&stream->lock);
    nc_cond_destroy(&stream->read_cond);
    nc_cond_destroy(&stream->write_cond);
    fragment_list_destroy(stream->recv_fragments);
    if (stream->recv_buf) memset(stream->recv_buf, 0, stream->recv_buf_cap);
    if (stream->send_buf) memset(stream->send_buf, 0, stream->send_buf_cap);
    free(stream->recv_buf);
    free(stream->send_buf);
    free(stream);
}

struct neverc_quic_conn *neverc_quic_conn_create(quic_conn_side_t side,
                                                  int udp_fd) {
    struct neverc_quic_conn *conn =
        (struct neverc_quic_conn *)calloc(1, sizeof(*conn));
    if (!conn) return NULL;
    conn->state = QUIC_CONN_IDLE;
    conn->side = side;
    conn->peer_completed_address_validation = side == QUIC_SIDE_SERVER;
    conn->udp_fd = udp_fd;
    conn->version = NEVERC_QUIC_VERSION_1;
    conn->idle_timeout_ms = 30000;
    conn->last_activity_ms = nc_monotonic_ms();
    conn->handshake_start_ms = conn->last_activity_ms;
    conn->next_bidi_stream_id = side == QUIC_SIDE_CLIENT ? 0 : 1;
    conn->next_uni_stream_id = side == QUIC_SIDE_CLIENT ? 2 : 3;
    conn->peer_max_streams_bidi = 100;
    conn->peer_max_streams_uni = 100;
    conn->new_cid_retransmit_index = -1;
    conn->flow.max_data_local = 10U * 1024U * 1024U;
    conn->flow.max_data_peer = 10U * 1024U * 1024U;
    neverc_quic_transport_params_default(&conn->local_params);
    neverc_quic_transport_params_default(&conn->peer_params);
    neverc_quic_loss_init(&conn->loss);
    nc_mutex_init(&conn->lock);
    nc_cond_init(&conn->stream_avail_cond);
    nc_cond_init(&conn->datagram_cond);
    nc_cond_init(&conn->state_cond);
    conn->sync_initialized = 1;
    if (conn_add_local_cid(conn) != 0) {
        neverc_quic_conn_destroy(conn);
        return NULL;
    }
    return conn;
}

static void datagram_queue_destroy(quic_datagram_entry_t *entries,
                                   size_t capacity) {
    for (size_t i = 0; i < capacity; i++) {
        free(entries[i].data);
        entries[i].data = NULL;
        entries[i].len = 0;
    }
}

void neverc_quic_conn_destroy(struct neverc_quic_conn *conn) {
    if (!conn) return;
    conn->io_running = 0;
    if (conn->io_thread_started) {
        nc_thread_join(conn->io_thread);
        conn->io_thread_started = 0;
    }
    for (int i = 0; i < conn->n_streams; i++)
        stream_destroy(conn->streams[i]);
    datagram_queue_destroy(conn->recv_datagrams,
                           QUIC_DATAGRAM_QUEUE_CAPACITY);
    datagram_queue_destroy(conn->send_datagrams,
                           QUIC_DATAGRAM_QUEUE_CAPACITY);
    neverc_quic_tls_destroy(conn->tls);
    neverc_quic_loss_destroy(&conn->loss);
    if (conn->owns_udp && conn->udp) neverc_udp_close(conn->udp);
    if (!conn->udp && conn->udp_fd >= 0) {
#ifdef _WIN32
        closesocket((SOCKET)(uintptr_t)conn->udp_fd);
#else
        close(conn->udp_fd);
#endif
    }
    if (conn->sync_initialized) {
        nc_mutex_destroy(&conn->lock);
        nc_cond_destroy(&conn->stream_avail_cond);
        nc_cond_destroy(&conn->datagram_cond);
        nc_cond_destroy(&conn->state_cond);
    }
    memset(conn, 0, sizeof(*conn));
    free(conn);
}

static int quic_copy_peer_cid(struct neverc_quic_conn *conn,
                              const quic_conn_id_t *peer_cid) {
    /* RFC 9000 §5.1: a peer Source CID may be zero-length. */
    if (!conn || !peer_cid || peer_cid->len > QUIC_MAX_CID_LEN)
        return -1;
    quic_conn_id_entry_t *entry = &conn->peer_cids[0];
    memset(entry, 0, sizeof(*entry));
    if (peer_cid->len)
        memcpy(entry->id, peer_cid->data, peer_cid->len);
    entry->len = peer_cid->len;
    entry->sequence = 0;
    conn->n_peer_cids = 1;
    conn->active_peer_cid_idx = 0;
    return 0;
}

int neverc_quic_conn_configure(struct neverc_quic_conn *conn,
                               const neverc_quic_config_t *config,
                               neverc_udp_conn_t *udp, int owns_udp,
                               const neverc_udp_addr_t *peer,
                               struct neverc_quic_endpoint *endpoint,
                               const quic_conn_id_t *initial_dcid,
                               const quic_conn_id_t *peer_cid,
                               const char *server_name) {
    if (!conn || !config || !udp || !peer || !initial_dcid ||
        initial_dcid->len == 0 || initial_dcid->len > QUIC_MAX_CID_LEN ||
        (conn->version != NEVERC_QUIC_VERSION_1 &&
         conn->version != NEVERC_QUIC_VERSION_2) ||
        quic_copy_peer_cid(conn, peer_cid) != 0)
        return -1;
    conn->udp = udp;
    conn->owns_udp = owns_udp;
    conn->udp_fd = -1;
    conn->endpoint = endpoint;
    conn->peer_addr = *peer;
    memcpy(conn->remote_addr, peer->addr, sizeof(conn->remote_addr));
    conn->remote_addr[sizeof(conn->remote_addr) - 1] = '\0';
    conn->initial_dcid = *initial_dcid;
    conn->idle_timeout_ms = config->max_idle_timeout_ms;
    if (conn->idle_timeout_ms == 0) conn->idle_timeout_ms = 30000;

    neverc_quic_transport_params_default(&conn->local_params);
    conn->local_params.max_idle_timeout = conn->idle_timeout_ms;
    conn->local_params.max_udp_payload_size = config->max_udp_payload_size;
    conn->local_params.initial_max_data = config->max_data;
    conn->local_params.initial_max_stream_data_bidi_local =
        config->max_stream_data_bidi_local;
    conn->local_params.initial_max_stream_data_bidi_remote =
        config->max_stream_data_bidi_remote;
    conn->local_params.initial_max_stream_data_uni =
        config->max_stream_data_uni;
    conn->local_params.initial_max_streams_bidi = config->max_streams_bidi;
    conn->local_params.initial_max_streams_uni = config->max_streams_uni;
    conn->local_params.max_datagram_frame_size =
        config->max_udp_payload_size;
    conn->local_params.disable_active_migration = config->disable_migration;
    memcpy(conn->local_params.initial_scid, conn->local_cids[0].id,
           conn->local_cids[0].len);
    conn->local_params.initial_scid_len = conn->local_cids[0].len;
    conn->local_params.has_initial_scid = 1;
    if (conn->side == QUIC_SIDE_SERVER) {
        memcpy(conn->local_params.original_dcid, initial_dcid->data,
               initial_dcid->len);
        conn->local_params.original_dcid_len = initial_dcid->len;
        conn->local_params.has_original_dcid = 1;
        memcpy(conn->local_params.stateless_reset_token,
               conn->local_cids[0].stateless_reset_token, 16);
        conn->local_params.has_stateless_reset_token = 1;
    }

    conn->flow.max_data_local = config->max_data;
    conn->flow.max_data_peer = 0;
    conn->peer_max_streams_bidi = 0;
    conn->peer_max_streams_uni = 0;
    conn->tls = neverc_quic_tls_create(conn->side == QUIC_SIDE_SERVER);
    if (!conn->tls ||
        neverc_quic_tls_set_initial_dcid(conn->tls, initial_dcid->data,
                                         initial_dcid->len,
                                         conn->version) != 0 ||
        neverc_quic_tls_configure(conn->tls, config, server_name,
                                  &conn->local_params,
                                  &conn->peer_params) != 0) {
        quic_set_error(conn, conn->tls ? neverc_quic_tls_error(conn->tls) :
                                        "failed to create QUIC TLS state");
        return -1;
    }
    conn->state = QUIC_CONN_HANDSHAKING;
    conn->last_activity_ms = nc_monotonic_ms();
    conn->handshake_start_ms = conn->last_activity_ms;
    return 0;
}

int neverc_quic_conn_start_client(struct neverc_quic_conn *conn) {
    if (!conn || conn->side != QUIC_SIDE_CLIENT ||
        conn->state != QUIC_CONN_HANDSHAKING)
        return -1;
    if (neverc_quic_tls_start(conn->tls) != 0) {
        quic_set_error(conn, neverc_quic_tls_error(conn->tls));
        return -1;
    }
    return neverc_quic_conn_flush(conn);
}

quic_stream_t *neverc_quic_conn_find_stream(
    struct neverc_quic_conn *conn, uint64_t stream_id) {
    if (!conn) return NULL;
    for (int i = 0; i < conn->n_streams; i++) {
        if (conn->streams[i] && conn->streams[i]->id == stream_id)
            return conn->streams[i];
    }
    return NULL;
}

static quic_stream_t *conn_add_stream_locked(struct neverc_quic_conn *conn,
                                             uint64_t id,
                                             int peer_initiated) {
    if (!conn || conn->n_streams >= QUIC_MAX_STREAMS) return NULL;
    quic_stream_t *stream = stream_create(conn, id, peer_initiated);
    if (!stream) return NULL;
    conn->streams[conn->n_streams++] = stream;
    if (peer_initiated) nc_cond_broadcast(&conn->stream_avail_cond);
    return stream;
}

static quic_stream_t *conn_open_stream(struct neverc_quic_conn *conn,
                                       int unidirectional) {
    if (!conn) return NULL;
    nc_mutex_lock(&conn->lock);
    if (conn->state != QUIC_CONN_ESTABLISHED ||
        conn->n_streams >= QUIC_MAX_STREAMS) {
        nc_mutex_unlock(&conn->lock);
        return NULL;
    }
    uint64_t id = unidirectional ? conn->next_uni_stream_id :
                                   conn->next_bidi_stream_id;
    uint64_t limit = unidirectional ? conn->peer_max_streams_uni :
                                      conn->peer_max_streams_bidi;
    if (stream_ordinal(id) > limit) {
        nc_mutex_unlock(&conn->lock);
        return NULL;
    }
    if (unidirectional)
        conn->next_uni_stream_id += 4;
    else
        conn->next_bidi_stream_id += 4;
    quic_stream_t *stream = conn_add_stream_locked(conn, id, 0);
    nc_mutex_unlock(&conn->lock);
    return stream;
}

quic_stream_t *neverc_quic_conn_open_stream(struct neverc_quic_conn *conn) {
    return conn_open_stream(conn, 0);
}

quic_stream_t *neverc_quic_conn_open_uni_stream(
    struct neverc_quic_conn *conn) {
    return conn_open_stream(conn, 1);
}

int neverc_quic_stream_write_data(quic_stream_t *stream,
                                  const void *data, size_t length) {
    if (!stream || (!data && length != 0) || length > INT_MAX) return -1;
    if (length == 0) return 0;
    const uint8_t *bytes = (const uint8_t *)data;
    size_t completed = 0;
    while (completed < length) {
        nc_mutex_lock(&stream->lock);
        for (;;) {
            if (stream->state == QUIC_STREAM_CLOSED ||
                stream->state == QUIC_STREAM_HALF_CLOSED_LOCAL ||
                stream->state == QUIC_STREAM_RESET ||
                (stream_is_uni(stream->id) && stream->peer_initiated) ||
                !neverc_quic_conn_is_alive_check(stream->conn)) {
                nc_mutex_unlock(&stream->lock);
                return -1;
            }
            uint64_t buffered_end;
            if (quic_add_u64(stream->send_offset,
                             (uint64_t)stream->send_len,
                             &buffered_end) != 0) {
                nc_mutex_unlock(&stream->lock);
                return -1;
            }
            size_t queue_room = stream->send_len <
                    QUIC_STREAM_SEND_BUFFER_LIMIT ?
                QUIC_STREAM_SEND_BUFFER_LIMIT - stream->send_len : 0;
            uint64_t flow_room = buffered_end < stream->send_max_data ?
                stream->send_max_data - buffered_end : 0;
            if (queue_room && flow_room) {
                size_t chunk = length - completed;
                if (chunk > queue_room) chunk = queue_room;
                if ((uint64_t)chunk > flow_room) chunk = (size_t)flow_room;
                size_t needed = stream->send_len + chunk;
                if (quic_reserve(&stream->send_buf, &stream->send_buf_cap,
                                  needed,
                                  QUIC_STREAM_SEND_BUFFER_LIMIT) != 0) {
                    nc_mutex_unlock(&stream->lock);
                    return -1;
                }
                memcpy(stream->send_buf + stream->send_len,
                       bytes + completed, chunk);
                stream->send_len += chunk;
                completed += chunk;
                break;
            }
            nc_cond_wait(&stream->write_cond, &stream->lock);
        }
        nc_mutex_unlock(&stream->lock);
        if (neverc_quic_conn_flush(stream->conn) < 0) return -1;
    }
    return (int)length;
}

static int quic_conn_allows_stream_read(struct neverc_quic_conn *conn);

static int quic_stream_read_impl(quic_stream_t *stream, void *buffer,
                                 size_t capacity, int blocking) {
    if (!stream || !buffer || capacity == 0 || capacity > INT_MAX) return -1;
    if (stream_is_uni(stream->id) && !stream->peer_initiated) return -1;
    nc_mutex_lock(&stream->lock);
    while (stream->recv_len == 0 && !stream->recv_fin &&
           stream->state != QUIC_STREAM_CLOSED &&
           stream->state != QUIC_STREAM_RESET &&
           quic_conn_allows_stream_read(stream->conn)) {
        if (!blocking) {
            nc_mutex_unlock(&stream->lock);
            return -2;
        }
        nc_cond_wait(&stream->read_cond, &stream->lock);
    }
    if (stream->recv_len == 0) {
        int result = stream->state == QUIC_STREAM_RESET ? -1 :
                     (stream->recv_fin ? 0 : -1);
        nc_mutex_unlock(&stream->lock);
        return result;
    }
    size_t count = capacity < stream->recv_len ? capacity : stream->recv_len;
    memcpy(buffer, stream->recv_buf, count);
    stream->recv_len -= count;
    if (stream->recv_len)
        memmove(stream->recv_buf, stream->recv_buf + count,
                stream->recv_len);
    uint64_t consumed = (uint64_t)count;
    nc_mutex_unlock(&stream->lock);

    nc_mutex_lock(&stream->conn->lock);
    if (stream->conn->flow.data_consumed <= UINT64_MAX - consumed)
        stream->conn->flow.data_consumed += consumed;
    uint64_t new_stream_limit;
    if (quic_add_u64(stream->recv_max_data, consumed,
                     &new_stream_limit) == 0 &&
        new_stream_limit <= QUIC_VARINT_MAX) {
        stream->recv_max_data = new_stream_limit;
        stream->conn->max_stream_data_pending = stream;
    }
    uint64_t new_conn_limit;
    if (quic_add_u64(stream->conn->flow.max_data_local, consumed,
                     &new_conn_limit) == 0 &&
        new_conn_limit <= QUIC_VARINT_MAX) {
        stream->conn->flow.max_data_local = new_conn_limit;
        stream->conn->max_data_pending = 1;
    }
    nc_mutex_unlock(&stream->conn->lock);
    (void)neverc_quic_conn_flush(stream->conn);
    return (int)count;
}

int neverc_quic_stream_read_data(quic_stream_t *stream,
                                 void *buffer, size_t capacity) {
    return quic_stream_read_impl(stream, buffer, capacity, 1);
}

int neverc_quic_stream_try_read_data(quic_stream_t *stream,
                                     void *buffer, size_t capacity) {
    return quic_stream_read_impl(stream, buffer, capacity, 0);
}

int neverc_quic_stream_close_write_side(quic_stream_t *stream) {
    if (!stream) return -1;
    nc_mutex_lock(&stream->lock);
    if (stream->send_fin || stream->state == QUIC_STREAM_CLOSED ||
        stream->state == QUIC_STREAM_RESET ||
        (stream_is_uni(stream->id) && stream->peer_initiated)) {
        nc_mutex_unlock(&stream->lock);
        return -1;
    }
    stream->send_fin = 1;
    if (stream->state == QUIC_STREAM_OPEN)
        stream->state = QUIC_STREAM_HALF_CLOSED_LOCAL;
    else if (stream->state == QUIC_STREAM_HALF_CLOSED_REMOTE)
        stream->state = QUIC_STREAM_CLOSED;
    nc_mutex_unlock(&stream->lock);
    (void)neverc_quic_conn_flush(stream->conn);
    return 0;
}

static int stream_overlap_matches(quic_stream_t *stream, uint64_t offset,
                                  const uint8_t *data, size_t length) {
    uint64_t end;
    if (!stream || (!data && length != 0) ||
        quic_add_u64(offset, (uint64_t)length, &end) != 0)
        return -1;
    if (stream->recv_len > 0 && stream->recv_offset >= stream->recv_len) {
        uint64_t buf_start = stream->recv_offset - stream->recv_len;
        uint64_t start = offset > buf_start ? offset : buf_start;
        uint64_t stop = end < stream->recv_offset ? end : stream->recv_offset;
        if (start < stop) {
            if (memcmp(stream->recv_buf + (size_t)(start - buf_start),
                       data + (size_t)(start - offset),
                       (size_t)(stop - start)) != 0)
                return -1;
        }
    }
    for (quic_fragment_t *fragment = stream->recv_fragments; fragment;
         fragment = fragment->next) {
        uint64_t fragment_end;
        if (quic_add_u64(fragment->offset, (uint64_t)fragment->len,
                         &fragment_end) != 0)
            return -1;
        uint64_t start = offset > fragment->offset ? offset : fragment->offset;
        uint64_t stop = end < fragment_end ? end : fragment_end;
        if (start < stop) {
            if (memcmp(fragment->data + (size_t)(start - fragment->offset),
                       data + (size_t)(start - offset),
                       (size_t)(stop - start)) != 0)
                return -1;
        }
    }
    return 0;
}

static int fragment_insert(quic_stream_t *stream, uint64_t offset,
                           const uint8_t *data, size_t length) {
    if (length == 0) return 0;
    uint64_t end;
    if (quic_add_u64(offset, (uint64_t)length, &end) != 0) return -1;
    if (stream_overlap_matches(stream, offset, data, length) != 0)
        return -1;
    if (end <= stream->recv_offset) return 0;
    if (offset < stream->recv_offset) {
        size_t skip = (size_t)(stream->recv_offset - offset);
        data += skip;
        length -= skip;
        offset = stream->recv_offset;
    }
    quic_fragment_t **position = &stream->recv_fragments;
    while (*position && (*position)->offset < offset)
        position = &(*position)->next;
    if (*position && (*position)->offset == offset &&
        (*position)->len >= length)
        return 0;
    quic_fragment_t *fragment =
        (quic_fragment_t *)calloc(1, sizeof(*fragment));
    if (!fragment) return -1;
    fragment->data = (uint8_t *)malloc(length);
    if (!fragment->data) {
        free(fragment);
        return -1;
    }
    memcpy(fragment->data, data, length);
    fragment->offset = offset;
    fragment->len = length;
    fragment->next = *position;
    *position = fragment;
    return 0;
}

static int fragment_drain(quic_stream_t *stream) {
    for (;;) {
        quic_fragment_t **position = &stream->recv_fragments;
        while (*position) {
            quic_fragment_t *fragment = *position;
            uint64_t fragment_end;
            if (quic_add_u64(fragment->offset, fragment->len,
                             &fragment_end) != 0)
                return -1;
            if (fragment_end <= stream->recv_offset) {
                *position = fragment->next;
                free(fragment->data);
                free(fragment);
                continue;
            }
            if (fragment->offset > stream->recv_offset) break;
            size_t skip = (size_t)(stream->recv_offset - fragment->offset);
            size_t append = fragment->len - skip;
            uint64_t unread_limit = stream->recv_max_data;
            size_t limit = unread_limit > SIZE_MAX ? SIZE_MAX :
                                                   (size_t)unread_limit;
            if (append > SIZE_MAX - stream->recv_len ||
                quic_reserve(&stream->recv_buf, &stream->recv_buf_cap,
                             stream->recv_len + append, limit) != 0)
                return -1;
            memcpy(stream->recv_buf + stream->recv_len,
                   fragment->data + skip, append);
            stream->recv_len += append;
            stream->recv_offset += append;
            *position = fragment->next;
            free(fragment->data);
            free(fragment);
            break;
        }
        if (!*position || (*position)->offset > stream->recv_offset) break;
    }
    if (stream->recv_final_known &&
        stream->recv_offset == stream->recv_final_size &&
        stream->state != QUIC_STREAM_RESET) {
        stream->recv_fin = 1;
        if (stream->state == QUIC_STREAM_OPEN)
            stream->state = QUIC_STREAM_HALF_CLOSED_REMOTE;
        else if (stream->state == QUIC_STREAM_HALF_CLOSED_LOCAL)
            stream->state = QUIC_STREAM_CLOSED;
    }
    return 0;
}

static quic_stream_t *get_or_create_receive_stream_locked(
    struct neverc_quic_conn *conn, uint64_t stream_id) {
    quic_stream_t *stream = neverc_quic_conn_find_stream(conn, stream_id);
    if (stream) return stream;
    if (stream_is_local(conn, stream_id)) return NULL;
    uint64_t ordinal = stream_ordinal(stream_id);
    uint64_t limit = stream_is_uni(stream_id) ?
        conn->local_params.initial_max_streams_uni :
        conn->local_params.initial_max_streams_bidi;
    if (ordinal > limit) return NULL;

    /* RFC 9000 §2.1: a stream ID used out of order also opens every
     * lower-numbered stream of the same type. */
    uint64_t first_id = stream_id & 3U;
    size_t needed = 0;
    for (uint64_t id = first_id; id <= stream_id; id += 4U) {
        if (!neverc_quic_conn_find_stream(conn, id))
            needed++;
        if (id > UINT64_MAX - 4U) break;
    }
    if (needed > (size_t)(QUIC_MAX_STREAMS - conn->n_streams))
        return NULL;

    quic_stream_t *created = NULL;
    for (uint64_t id = first_id; id <= stream_id; id += 4U) {
        quic_stream_t *existing = neverc_quic_conn_find_stream(conn, id);
        if (!existing) {
            existing = conn_add_stream_locked(conn, id, 1);
            if (!existing) return NULL;
        }
        if (id == stream_id) created = existing;
        if (id > UINT64_MAX - 4U) break;
    }
    if (stream_is_uni(stream_id)) {
        if (ordinal > conn->opened_peer_streams_uni)
            conn->opened_peer_streams_uni = ordinal;
    } else if (ordinal > conn->opened_peer_streams_bidi) {
        conn->opened_peer_streams_bidi = ordinal;
    }
    return created;
}

int neverc_quic_stream_receive_locked(struct neverc_quic_conn *conn,
                                      const quic_frame_stream_t *frame) {
    if (!conn || !frame || frame->stream_id > QUIC_VARINT_MAX ||
        frame->offset > QUIC_VARINT_MAX ||
        frame->data_len > QUIC_VARINT_MAX)
        return -1;
    uint64_t end;
    if (quic_add_u64(frame->offset, (uint64_t)frame->data_len, &end) != 0 ||
        end > QUIC_VARINT_MAX)
        return -1;
    if (!neverc_quic_conn_find_stream(conn, frame->stream_id)) {
        if (stream_is_local(conn, frame->stream_id)) {
            neverc_quic_conn_close_locked(conn, QUIC_ERR_STREAM_STATE_ERROR,
                                          "STREAM on unopened local stream",
                                          0);
            return -1;
        }
        uint64_t ordinal = stream_ordinal(frame->stream_id);
        uint64_t limit = stream_is_uni(frame->stream_id) ?
            conn->local_params.initial_max_streams_uni :
            conn->local_params.initial_max_streams_bidi;
        if (ordinal > limit) {
            neverc_quic_conn_close_locked(conn, QUIC_ERR_STREAM_LIMIT_ERROR,
                                          "stream limit exceeded", 0);
            return -1;
        }
    }
    quic_stream_t *stream =
        get_or_create_receive_stream_locked(conn, frame->stream_id);
    if (!stream || (stream_is_uni(stream->id) &&
                    stream_is_local(conn, stream->id))) {
        neverc_quic_conn_close_locked(conn, QUIC_ERR_STREAM_STATE_ERROR,
                                      "STREAM not permitted on this stream",
                                      0);
        return -1;
    }
    nc_mutex_lock(&stream->lock);
    uint64_t previous_highest = stream->recv_highest;
    if (end > stream->recv_max_data) {
        nc_mutex_unlock(&stream->lock);
        neverc_quic_conn_close_locked(conn, QUIC_ERR_FLOW_CONTROL_ERROR,
                                      "stream flow control exceeded", 0);
        return -1;
    }
    if ((stream->recv_final_known && end > stream->recv_final_size) ||
        (frame->fin && stream->recv_final_known &&
         end != stream->recv_final_size) ||
        (frame->fin && end < stream->recv_highest)) {
        nc_mutex_unlock(&stream->lock);
        neverc_quic_conn_close_locked(conn, QUIC_ERR_FINAL_SIZE_ERROR,
                                      "STREAM final size mismatch", 0);
        return -1;
    }
    if (frame->fin) {
        stream->recv_final_known = 1;
        stream->recv_final_size = end;
    }
    if (end > stream->recv_highest) stream->recv_highest = end;
    uint64_t delta = stream->recv_highest - previous_highest;
    if (delta > conn->flow.max_data_local -
                    (conn->flow.data_received > conn->flow.max_data_local ?
                         conn->flow.max_data_local :
                         conn->flow.data_received)) {
        stream->recv_highest = previous_highest;
        nc_mutex_unlock(&stream->lock);
        neverc_quic_conn_close_locked(conn, QUIC_ERR_FLOW_CONTROL_ERROR,
                                      "connection flow control exceeded", 0);
        return -1;
    }
    conn->flow.data_received += delta;
    /* RFC 9000 §3.2: STREAM after RESET_STREAM MAY be ignored. Delivering
     * the bytes would let fragment_drain turn a reset into a clean FIN. */
    if (stream->state == QUIC_STREAM_RESET) {
        nc_mutex_unlock(&stream->lock);
        return 0;
    }
    int result = fragment_insert(stream, frame->offset, frame->data,
                                 frame->data_len);
    if (result == 0) result = fragment_drain(stream);
    if (result == 0) nc_cond_broadcast(&stream->read_cond);
    nc_mutex_unlock(&stream->lock);
    if (result != 0)
        neverc_quic_conn_close_locked(conn, QUIC_ERR_PROTOCOL_VIOLATION,
                                      "conflicting STREAM data", 0);
    return result;
}

int neverc_quic_stream_receive(struct neverc_quic_conn *conn,
                               const quic_frame_stream_t *frame) {
    if (!conn) return -1;
    nc_mutex_lock(&conn->lock);
    int result = neverc_quic_stream_receive_locked(conn, frame);
    nc_mutex_unlock(&conn->lock);
    return result;
}

int neverc_quic_stream_receive_reset_locked(
    struct neverc_quic_conn *conn,
    const quic_frame_reset_stream_t *frame) {
    if (!conn || !frame || frame->stream_id > QUIC_VARINT_MAX ||
        frame->error_code > QUIC_VARINT_MAX ||
        frame->final_size > QUIC_VARINT_MAX)
        return -1;
    if (!neverc_quic_conn_find_stream(conn, frame->stream_id)) {
        if (stream_is_local(conn, frame->stream_id)) {
            neverc_quic_conn_close_locked(conn, QUIC_ERR_STREAM_STATE_ERROR,
                                          "RESET_STREAM on unopened local stream",
                                          0);
            return -1;
        }
        uint64_t ordinal = stream_ordinal(frame->stream_id);
        uint64_t limit = stream_is_uni(frame->stream_id) ?
            conn->local_params.initial_max_streams_uni :
            conn->local_params.initial_max_streams_bidi;
        if (ordinal > limit) {
            neverc_quic_conn_close_locked(conn, QUIC_ERR_STREAM_LIMIT_ERROR,
                                          "RESET_STREAM stream limit exceeded",
                                          0);
            return -1;
        }
    }
    quic_stream_t *stream =
        get_or_create_receive_stream_locked(conn, frame->stream_id);
    if (!stream || (stream_is_uni(stream->id) &&
                    stream_is_local(conn, stream->id))) {
        neverc_quic_conn_close_locked(conn, QUIC_ERR_STREAM_STATE_ERROR,
                                      "RESET_STREAM not permitted", 0);
        return -1;
    }
    nc_mutex_lock(&stream->lock);
    if ((stream->recv_final_known &&
         stream->recv_final_size != frame->final_size) ||
        frame->final_size < stream->recv_highest) {
        nc_mutex_unlock(&stream->lock);
        neverc_quic_conn_close_locked(conn, QUIC_ERR_FINAL_SIZE_ERROR,
                                      "RESET_STREAM final size mismatch", 0);
        return -1;
    }
    if (frame->final_size > stream->recv_max_data) {
        nc_mutex_unlock(&stream->lock);
        neverc_quic_conn_close_locked(conn, QUIC_ERR_FLOW_CONTROL_ERROR,
                                      "RESET_STREAM exceeds stream limit", 0);
        return -1;
    }
    uint64_t delta = frame->final_size - stream->recv_highest;
    uint64_t connection_remaining = conn->flow.data_received <=
            conn->flow.max_data_local
        ? conn->flow.max_data_local - conn->flow.data_received : 0;
    if (delta > connection_remaining) {
        nc_mutex_unlock(&stream->lock);
        neverc_quic_conn_close_locked(conn, QUIC_ERR_FLOW_CONTROL_ERROR,
                                      "RESET_STREAM exceeds connection limit",
                                      0);
        return -1;
    }
    conn->flow.data_received += delta;
    stream->recv_highest = frame->final_size;
    stream->recv_final_known = 1;
    stream->recv_final_size = frame->final_size;
    stream->reset_error_code = frame->error_code;
    stream->state = QUIC_STREAM_RESET;
    nc_cond_broadcast(&stream->read_cond);
    nc_mutex_unlock(&stream->lock);
    return 0;
}

int neverc_quic_stream_receive_reset(
    struct neverc_quic_conn *conn,
    const quic_frame_reset_stream_t *frame) {
    if (!conn) return -1;
    nc_mutex_lock(&conn->lock);
    int result = neverc_quic_stream_receive_reset_locked(conn, frame);
    nc_mutex_unlock(&conn->lock);
    return result;
}

int neverc_quic_stream_apply_max_stream_data_locked(
    struct neverc_quic_conn *conn, uint64_t stream_id, uint64_t maximum) {
    if (!conn || stream_id > QUIC_VARINT_MAX || maximum > QUIC_VARINT_MAX)
        return -1;
    /* Receive-only streams have no send side (RFC 9000 §19.10). */
    if (stream_is_uni(stream_id) && !stream_is_local(conn, stream_id))
        return -1;
    quic_stream_t *stream = neverc_quic_conn_find_stream(conn, stream_id);
    if (!stream) {
        if (stream_is_local(conn, stream_id)) return -1;
        stream = get_or_create_receive_stream_locked(conn, stream_id);
    }
    if (!stream) return -1;
    nc_mutex_lock(&stream->lock);
    if (maximum > stream->send_max_data) stream->send_max_data = maximum;
    nc_cond_broadcast(&stream->write_cond);
    nc_mutex_unlock(&stream->lock);
    return 0;
}

int neverc_quic_stream_apply_max_stream_data(
    struct neverc_quic_conn *conn, uint64_t stream_id, uint64_t maximum) {
    if (!conn) return -1;
    nc_mutex_lock(&conn->lock);
    int result = neverc_quic_stream_apply_max_stream_data_locked(
        conn, stream_id, maximum);
    nc_mutex_unlock(&conn->lock);
    return result;
}

int neverc_quic_stream_apply_stop_sending_locked(
    struct neverc_quic_conn *conn, uint64_t stream_id, uint64_t error_code) {
    if (!conn || stream_id > QUIC_VARINT_MAX || error_code > QUIC_VARINT_MAX)
        return -1;
    if (stream_is_uni(stream_id) && !stream_is_local(conn, stream_id))
        return -1;
    quic_stream_t *stream = neverc_quic_conn_find_stream(conn, stream_id);
    if (!stream) {
        if (stream_is_local(conn, stream_id)) return -1;
        stream = get_or_create_receive_stream_locked(conn, stream_id);
    }
    if (!stream) return -1;
    nc_mutex_lock(&stream->lock);
    stream->reset_pending = 1;
    stream->reset_error_code = error_code;
    nc_mutex_unlock(&stream->lock);
    return 0;
}

int neverc_quic_stream_apply_stop_sending(
    struct neverc_quic_conn *conn, uint64_t stream_id, uint64_t error_code) {
    if (!conn) return -1;
    nc_mutex_lock(&conn->lock);
    int result = neverc_quic_stream_apply_stop_sending_locked(
        conn, stream_id, error_code);
    nc_mutex_unlock(&conn->lock);
    return result;
}

int neverc_quic_conn_apply_max_data_locked(struct neverc_quic_conn *conn,
                                           uint64_t maximum) {
    if (!conn || maximum > QUIC_VARINT_MAX) return -1;
    if (maximum > conn->flow.max_data_peer)
        conn->flow.max_data_peer = maximum;
    for (int i = 0; i < conn->n_streams; i++) {
        quic_stream_t *stream = conn->streams[i];
        if (!stream) continue;
        nc_mutex_lock(&stream->lock);
        nc_cond_broadcast(&stream->write_cond);
        nc_mutex_unlock(&stream->lock);
    }
    return 0;
}

int neverc_quic_conn_add_peer_cid(struct neverc_quic_conn *conn,
                                  const quic_frame_new_conn_id_t *frame) {
    if (!conn || !frame || frame->conn_id_len == 0 ||
        frame->conn_id_len > QUIC_MAX_CID_LEN ||
        frame->retire_prior_to > frame->sequence)
        return -1;
    for (int i = 0; i < conn->n_peer_cids; i++) {
        quic_conn_id_entry_t *entry = &conn->peer_cids[i];
        if (entry->sequence == frame->sequence) {
            if (entry->len != frame->conn_id_len ||
                memcmp(entry->id, frame->conn_id, entry->len) != 0 ||
                memcmp(entry->stateless_reset_token,
                       frame->stateless_reset_token, 16) != 0)
                return -1;
            goto apply_retire;
        }
    }
    /* RFC 9000 §5.1.1 / §19.15: the limit is on active (unretired) IDs
     * after Retire Prior To is applied, not on the raw slot count. */
    int active = 1;
    for (int i = 0; i < conn->n_peer_cids; i++) {
        if (!conn->peer_cids[i].retired &&
            conn->peer_cids[i].sequence >= frame->retire_prior_to)
            active++;
    }
    if (active > QUIC_MAX_PEER_CONN_IDS ||
        (uint64_t)active > conn->local_params.active_connection_id_limit)
        return -1;
    quic_conn_id_entry_t *entry = NULL;
    if (conn->n_peer_cids < QUIC_MAX_PEER_CONN_IDS) {
        entry = &conn->peer_cids[conn->n_peer_cids++];
    } else {
        for (int i = 0; i < conn->n_peer_cids; i++) {
            if (conn->peer_cids[i].retired &&
                conn->peer_cids[i].sequence < frame->retire_prior_to) {
                entry = &conn->peer_cids[i];
                break;
            }
        }
        if (!entry) return -1;
    }
    memset(entry, 0, sizeof(*entry));
    memcpy(entry->id, frame->conn_id, frame->conn_id_len);
    entry->len = frame->conn_id_len;
    entry->sequence = frame->sequence;
    memcpy(entry->stateless_reset_token, frame->stateless_reset_token, 16);
apply_retire:
    for (int i = 0; i < conn->n_peer_cids; i++) {
        if (conn->peer_cids[i].sequence < frame->retire_prior_to &&
            !conn->peer_cids[i].retired) {
            conn->peer_cids[i].retired = 1;
            conn->peer_cids[i].retire_unsent = 1;
        }
    }
    if (conn->n_peer_cids > 0 &&
        conn->active_peer_cid_idx >= 0 &&
        conn->active_peer_cid_idx < conn->n_peer_cids &&
        conn->peer_cids[conn->active_peer_cid_idx].retired) {
        for (int i = 0; i < conn->n_peer_cids; i++) {
            if (!conn->peer_cids[i].retired) {
                conn->active_peer_cid_idx = i;
                break;
            }
        }
    }
    return 0;
}

int neverc_quic_conn_retire_local_cid_locked(struct neverc_quic_conn *conn,
                                             uint64_t sequence) {
    if (!conn) return -1;
    for (int i = 0; i < conn->n_local_cids; i++) {
        if (conn->local_cids[i].sequence != sequence) continue;
        if (!conn->local_cids[i].retired) {
            conn->local_cids[i].retired = 1;
            conn->new_cid_pending = 1;
        }
        return 0;
    }
    return -1;
}

void neverc_quic_conn_on_packet_acked(struct neverc_quic_conn *conn,
                                      int space, uint64_t packet_number) {
    if (!conn || space < 0 || space >= QUIC_PNS_COUNT) return;
    for (size_t i = 0; i < QUIC_TX_RECORD_CAPACITY; i++) {
        quic_tx_record_t *record = &conn->tx_records[i];
        if (!record->used || record->space != space ||
            record->packet_number != packet_number)
            continue;
        record->acked = 1;
        if (record->kind == QUIC_TX_CRYPTO) {
            neverc_quic_tls_crypto_data_acked(conn->tls, record->level,
                                              record->offset,
                                              record->length);
        } else if (record->kind == QUIC_TX_STREAM) {
            quic_stream_t *stream =
                neverc_quic_conn_find_stream(conn, record->stream_id);
            if (stream) {
                nc_mutex_lock(&stream->lock);
                if (stream->send_inflight_pn == packet_number &&
                    stream->send_inflight == record->length &&
                    stream->send_offset == record->offset) {
                    if (record->length < stream->send_len)
                        memmove(stream->send_buf,
                                stream->send_buf + record->length,
                                stream->send_len - record->length);
                    stream->send_len -= record->length;
                    stream->send_offset += record->length;
                    stream->send_inflight = 0;
                    if (record->fin) {
                        stream->send_fin_acked = 1;
                        stream->send_fin_sent = 1;
                    }
                    nc_cond_broadcast(&stream->write_cond);
                }
                nc_mutex_unlock(&stream->lock);
            }
        }
        record->used = 0;
    }
}

void neverc_quic_conn_on_packet_lost(struct neverc_quic_conn *conn,
                                     int space, uint64_t packet_number) {
    if (!conn || space < 0 || space >= QUIC_PNS_COUNT) return;
    for (size_t i = 0; i < QUIC_TX_RECORD_CAPACITY; i++) {
        quic_tx_record_t *record = &conn->tx_records[i];
        if (!record->used || record->space != space ||
            record->packet_number != packet_number)
            continue;
        if (record->kind == QUIC_TX_CRYPTO) {
            neverc_quic_tls_crypto_data_lost(conn->tls, record->level,
                                             record->offset);
        } else if (record->kind == QUIC_TX_STREAM) {
            quic_stream_t *stream =
                neverc_quic_conn_find_stream(conn, record->stream_id);
            if (stream) {
                nc_mutex_lock(&stream->lock);
                if (stream->send_inflight_pn == packet_number)
                    stream->send_inflight = 0;
                nc_mutex_unlock(&stream->lock);
            }
        } else if (record->kind == QUIC_TX_CONTROL) {
            switch ((int)record->offset) {
            case QUIC_FRAME_CONNECTION_CLOSE:
                conn->close_pending = 1;
                break;
            case QUIC_FRAME_PATH_RESPONSE:
                conn->path_response_pending = 1;
                break;
            case QUIC_FRAME_PATH_CHALLENGE:
                conn->path_challenge_pending = 1;
                break;
            case QUIC_FRAME_HANDSHAKE_DONE:
                conn->handshake_done_pending = 1;
                break;
            case QUIC_FRAME_MAX_DATA:
                conn->max_data_pending = 1;
                break;
            case QUIC_FRAME_MAX_STREAM_DATA:
                conn->max_stream_data_pending =
                    neverc_quic_conn_find_stream(conn, record->stream_id);
                break;
            case QUIC_FRAME_NEW_CONNECTION_ID:
                conn->new_cid_pending = 1;
                conn->new_cid_retransmit_index = (int)record->stream_id;
                break;
            case QUIC_FRAME_RESET_STREAM: {
                quic_stream_t *stream = neverc_quic_conn_find_stream(
                    conn, record->stream_id);
                if (stream) {
                    nc_mutex_lock(&stream->lock);
                    stream->reset_pending = 1;
                    nc_mutex_unlock(&stream->lock);
                }
                break;
            }
            case QUIC_FRAME_STOP_SENDING: {
                quic_stream_t *stream = neverc_quic_conn_find_stream(
                    conn, record->stream_id);
                if (stream) {
                    nc_mutex_lock(&stream->lock);
                    stream->stop_sending_pending = 1;
                    nc_mutex_unlock(&stream->lock);
                }
                break;
            }
            case QUIC_FRAME_RETIRE_CONNECTION_ID:
                for (int i = 0; i < conn->n_peer_cids; i++) {
                    if (conn->peer_cids[i].sequence == record->stream_id &&
                        conn->peer_cids[i].retired)
                        conn->peer_cids[i].retire_unsent = 1;
                }
                break;
            default:
                break;
            }
        }
        record->used = 0;
    }
}

void quic_stream_mark_connection_closing(quic_stream_t *stream) {
    if (!stream) return;
    nc_mutex_lock(&stream->lock);
    if (stream->recv_fin || stream->state == QUIC_STREAM_RESET)
        stream->state = QUIC_STREAM_CLOSED;
    else if (stream->state == QUIC_STREAM_OPEN ||
             stream->state == QUIC_STREAM_HALF_CLOSED_LOCAL)
        stream->state = QUIC_STREAM_HALF_CLOSED_REMOTE;
    nc_cond_broadcast(&stream->read_cond);
    nc_cond_broadcast(&stream->write_cond);
    nc_mutex_unlock(&stream->lock);
}

void quic_conn_finalize_streams(struct neverc_quic_conn *conn) {
    if (!conn) return;
    for (int i = 0; i < conn->n_streams; i++) {
        quic_stream_t *stream = conn->streams[i];
        if (!stream) continue;
        nc_mutex_lock(&stream->lock);
        if (stream->state != QUIC_STREAM_RESET)
            stream->state = QUIC_STREAM_CLOSED;
        nc_cond_broadcast(&stream->read_cond);
        nc_cond_broadcast(&stream->write_cond);
        nc_mutex_unlock(&stream->lock);
    }
}

int neverc_quic_conn_close_locked(struct neverc_quic_conn *conn,
                                  uint64_t error_code,
                                  const char *reason, int is_app) {
    if (!conn) return 0;
    if (conn->state == QUIC_CONN_CLOSED ||
        conn->state == QUIC_CONN_DRAINING)
        return 0;
    conn->state = QUIC_CONN_DRAINING;
    conn->draining_started_ms = nc_monotonic_ms();
    conn->close_error_code = error_code;
    conn->close_is_app = is_app != 0;
    conn->close_pending = 1;
    if (reason) {
        size_t length = strlen(reason);
        if (length >= sizeof(conn->close_reason))
            length = sizeof(conn->close_reason) - 1;
        memcpy(conn->close_reason, reason, length);
        conn->close_reason[length] = '\0';
    }
    for (int i = 0; i < conn->n_streams; i++)
        quic_stream_mark_connection_closing(conn->streams[i]);
    nc_cond_broadcast(&conn->stream_avail_cond);
    nc_cond_broadcast(&conn->datagram_cond);
    nc_cond_broadcast(&conn->state_cond);
    return 1;
}

void neverc_quic_conn_close_internal(struct neverc_quic_conn *conn,
                                     uint64_t error_code,
                                     const char *reason, int is_app) {
    if (!conn) return;
    nc_mutex_lock(&conn->lock);
    int changed = neverc_quic_conn_close_locked(conn, error_code, reason,
                                                is_app);
    nc_mutex_unlock(&conn->lock);
    if (changed) (void)neverc_quic_conn_flush(conn);
}

int neverc_quic_conn_send_drained(struct neverc_quic_conn *conn) {
    if (!conn) return 1;
    int drained = 1;
    nc_mutex_lock(&conn->lock);
    for (size_t i = 0; i < QUIC_TX_RECORD_CAPACITY; i++) {
        if (conn->tx_records[i].used) {
            drained = 0;
            break;
        }
    }
    for (int i = 0; drained && i < conn->n_streams; i++) {
        quic_stream_t *stream = conn->streams[i];
        if (!stream) continue;
        nc_mutex_lock(&stream->lock);
        if (stream->send_len != 0 || stream->send_inflight != 0 ||
            (stream->send_fin && !stream->send_fin_acked))
            drained = 0;
        nc_mutex_unlock(&stream->lock);
    }
    nc_mutex_unlock(&conn->lock);
    return drained;
}

uint64_t neverc_quic_conn_loss_timeout(struct neverc_quic_conn *conn,
                                       uint64_t now_ms) {
    if (!conn) return 0;
    uint64_t earliest_loss = 0;
    for (int space = 0; space < QUIC_PN_SPACE_COUNT; space++) {
        uint64_t loss_time = conn->loss.spaces[space].loss_time;
        if (loss_time && (!earliest_loss || loss_time < earliest_loss))
            earliest_loss = loss_time;
    }
    if (earliest_loss) {
        conn->validation_pto_deadline_ms = 0;
        return earliest_loss;
    }

    if (conn->side == QUIC_SIDE_SERVER && !conn->address_validated) {
        uint64_t received = conn->bytes_received_before_validation;
        uint64_t limit = received > UINT64_MAX / 3U ?
            UINT64_MAX : received * 3U;
        if (conn->bytes_sent_before_validation >= limit) {
            conn->validation_pto_deadline_ms = 0;
            return 0;
        }
    }

    if (neverc_quic_conn_has_ack_eliciting_in_flight(conn)) {
        conn->validation_pto_deadline_ms = 0;
        return neverc_quic_loss_get_timeout(
            &conn->loss, conn->handshake_confirmed);
    }
    if (conn->peer_completed_address_validation ||
        conn->side != QUIC_SIDE_CLIENT) {
        conn->validation_pto_deadline_ms = 0;
        return 0;
    }
    if (!conn->validation_pto_deadline_ms) {
        uint64_t duration = neverc_quic_pto(&conn->loss.rtt, 0);
        if (conn->loss.pto_count >= 63 ||
            duration > (UINT64_MAX >> conn->loss.pto_count))
            duration = UINT64_MAX;
        else
            duration <<= conn->loss.pto_count;
        conn->validation_pto_deadline_ms =
            now_ms > UINT64_MAX - duration ? UINT64_MAX : now_ms + duration;
    }
    return conn->validation_pto_deadline_ms;
}

int neverc_quic_conn_has_ack_eliciting_in_flight(
    const struct neverc_quic_conn *conn) {
    if (!conn) return 0;
    for (int space = 0; space < QUIC_PN_SPACE_COUNT; space++) {
        for (const quic_sent_packet_t *packet =
                 conn->loss.spaces[space].sent_packets;
             packet; packet = packet->next) {
            if (packet->ack_eliciting && packet->in_flight &&
                !packet->acked && !packet->lost)
                return 1;
        }
    }
    return 0;
}

int neverc_quic_conn_is_alive_check(struct neverc_quic_conn *conn) {
    if (!conn) return 0;
    return conn->state == QUIC_CONN_HANDSHAKING ||
           conn->state == QUIC_CONN_ESTABLISHED;
}

static int quic_conn_allows_stream_read(struct neverc_quic_conn *conn) {
    /* Draining permits buffered reads, but no new application work. */
    if (!conn) return 0;
    return conn->state == QUIC_CONN_HANDSHAKING ||
           conn->state == QUIC_CONN_ESTABLISHED ||
           conn->state == QUIC_CONN_DRAINING;
}

const char *neverc_quic_conn_get_alpn(struct neverc_quic_conn *conn) {
    return conn && conn->alpn[0] ? conn->alpn : NULL;
}

uint64_t neverc_quic_stream_get_id(quic_stream_t *stream) {
    return stream ? stream->id : UINT64_MAX;
}

int neverc_quic_conn_id_matches(const struct neverc_quic_conn *conn,
                                const uint8_t *cid, size_t cid_len) {
    if (!conn || !cid || cid_len == 0) return 0;
    if (conn->side == QUIC_SIDE_SERVER &&
        conn->state == QUIC_CONN_HANDSHAKING &&
        conn->initial_dcid.len == cid_len &&
        memcmp(conn->initial_dcid.data, cid, cid_len) == 0)
        return 1;
    /* RFC 9000 §5.1.2: packets using a retired CID can still arrive. */
    for (int i = 0; i < conn->n_local_cids; i++) {
        const quic_conn_id_entry_t *entry = &conn->local_cids[i];
        if (entry->len == cid_len &&
            memcmp(entry->id, cid, cid_len) == 0)
            return 1;
    }
    return 0;
}
