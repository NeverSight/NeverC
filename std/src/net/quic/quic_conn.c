/*
 * QUIC Connection State Machine (RFC 9000 §5)
 *
 * Manages the lifecycle of a QUIC connection:
 *   - Connection ID management
 *   - Packet number spaces (Initial, Handshake, Application Data)
 *   - Stream multiplexing
 *   - Flow control (connection-level)
 *   - Idle timeout
 *   - Graceful close (GOAWAY equivalent: CONNECTION_CLOSE + draining)
 *
 * Thread model: single connection loop handles I/O + timer events.
 * Streams are accessed from user threads via mutex-protected state.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "neverc/std/crypto/rand.h"

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  typedef int socklen_t;
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <unistd.h>
  #include <pthread.h>
  #include <errno.h>
  #include <fcntl.h>
#endif

extern int neverc_quic_varint_decode(const uint8_t *buf, size_t len,
                                      uint64_t *value, size_t *consumed);
extern int neverc_quic_varint_encode(uint64_t value, uint8_t *buf, size_t cap,
                                      size_t *written);
extern size_t neverc_quic_varint_len(uint64_t value);

/* ======================================================================
 * Connection State
 * ====================================================================== */

typedef enum {
    QUIC_CONN_IDLE = 0,
    QUIC_CONN_HANDSHAKING,
    QUIC_CONN_ESTABLISHED,
    QUIC_CONN_DRAINING,
    QUIC_CONN_CLOSED,
} quic_conn_state_t;

typedef enum {
    QUIC_SIDE_CLIENT = 0,
    QUIC_SIDE_SERVER = 1,
} quic_conn_side_t;

/* Packet number space */
typedef enum {
    QUIC_PNS_INITIAL = 0,
    QUIC_PNS_HANDSHAKE = 1,
    QUIC_PNS_APPLICATION = 2,
    QUIC_PNS_COUNT = 3,
} quic_pn_space_t;

#define QUIC_MAX_CONN_ID_LEN 20
#define QUIC_MAX_LOCAL_CONN_IDS 8
#define QUIC_MAX_PEER_CONN_IDS 8
#define QUIC_MAX_STREAMS 1024

/* Connection ID entry */
typedef struct {
    uint8_t  id[QUIC_MAX_CONN_ID_LEN];
    uint8_t  len;
    uint64_t sequence;
    uint8_t  stateless_reset_token[16];
    int      retired;
} quic_conn_id_entry_t;

/* Stream state */
typedef enum {
    QUIC_STREAM_IDLE = 0,
    QUIC_STREAM_OPEN,
    QUIC_STREAM_HALF_CLOSED_LOCAL,
    QUIC_STREAM_HALF_CLOSED_REMOTE,
    QUIC_STREAM_CLOSED,
    QUIC_STREAM_RESET,
} quic_stream_state_t;

typedef struct quic_stream {
    uint64_t           id;
    quic_stream_state_t state;

    /* Receive side */
    uint8_t           *recv_buf;
    size_t             recv_buf_cap;
    size_t             recv_len;
    uint64_t           recv_offset;     /* next expected offset */
    uint64_t           recv_max_data;   /* flow control window (MAX_STREAM_DATA sent) */
    int                recv_fin;

    /* Send side */
    uint8_t           *send_buf;
    size_t             send_buf_cap;
    size_t             send_len;
    uint64_t           send_offset;     /* next offset to send */
    uint64_t           send_max_data;   /* peer's MAX_STREAM_DATA for this stream */
    int                send_fin;
    int                send_fin_sent;

#ifndef _WIN32
    pthread_mutex_t    lock;
    pthread_cond_t     read_cond;
    pthread_cond_t     write_cond;
#endif
} quic_stream_t;

/* Packet number tracking per number space */
typedef struct {
    uint64_t next_pn;          /* next packet number to send */
    uint64_t largest_recv;     /* largest packet number received */
    uint64_t largest_acked;    /* largest packet number acknowledged by peer */
    int      has_recv;
} quic_pn_state_t;

/* Flow control */
typedef struct {
    uint64_t max_data;         /* connection-level max data (send limit) */
    uint64_t data_sent;        /* total bytes sent */
    uint64_t max_data_peer;    /* peer's connection-level limit (received via MAX_DATA) */
    uint64_t data_received;    /* total bytes received */
    uint64_t max_data_local;   /* our connection-level limit (sent via MAX_DATA) */
} quic_flow_control_t;

/* The connection */
struct neverc_quic_conn {
    quic_conn_state_t     state;
    quic_conn_side_t      side;

    /* Socket */
    int                   udp_fd;
    struct sockaddr_storage peer_addr;
    socklen_t             peer_addr_len;

    /* Connection IDs */
    quic_conn_id_entry_t  local_cids[QUIC_MAX_LOCAL_CONN_IDS];
    int                   n_local_cids;
    quic_conn_id_entry_t  peer_cids[QUIC_MAX_PEER_CONN_IDS];
    int                   n_peer_cids;
    int                   active_peer_cid_idx;
    uint64_t              next_local_cid_seq;

    /* Packet number spaces */
    quic_pn_state_t       pn[QUIC_PNS_COUNT];

    /* Streams */
    quic_stream_t        *streams[QUIC_MAX_STREAMS];
    int                   n_streams;
    uint64_t              next_bidi_stream_id;
    uint64_t              next_uni_stream_id;
    uint64_t              peer_max_streams_bidi;
    uint64_t              peer_max_streams_uni;

    /* Flow control */
    quic_flow_control_t   flow;

    /* Timing */
    uint64_t              idle_timeout_ms;
    uint64_t              last_activity_ms;
    uint64_t              handshake_start_ms;

    /* ALPN */
    char                  alpn[32];

    /* Close info */
    uint64_t              close_error_code;
    char                  close_reason[256];
    int                   close_is_app;

#ifndef _WIN32
    pthread_mutex_t       lock;
    pthread_cond_t        stream_avail_cond;
#endif
};

/* ======================================================================
 * Monotonic Time Helper
 * ====================================================================== */

static uint64_t quic_monotonic_ms(void) {
#ifdef _WIN32
    return GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
#endif
}

/* ======================================================================
 * Connection ID Management
 * ====================================================================== */

static int generate_conn_id(uint8_t *id, uint8_t len) {
    return neverc_crypto_rand_read(id, len);
}

static int conn_add_local_cid(struct neverc_quic_conn *conn) {
    if (conn->n_local_cids >= QUIC_MAX_LOCAL_CONN_IDS) return -1;

    quic_conn_id_entry_t *entry = &conn->local_cids[conn->n_local_cids];
    entry->len = 8;
    if (generate_conn_id(entry->id, entry->len) != 0) return -1;
    entry->sequence = conn->next_local_cid_seq++;
    entry->retired = 0;

    /* Generate stateless reset token */
    if (generate_conn_id(entry->stateless_reset_token, 16) != 0) {
        memset(entry, 0, sizeof(*entry));
        return -1;
    }

    conn->n_local_cids++;
    return 0;
}

/* ======================================================================
 * Stream Management
 * ====================================================================== */

static int stream_is_local(const struct neverc_quic_conn *conn,
                           uint64_t stream_id) {
    if (!conn) return 0;
    int initiator_is_server = (stream_id & 1U) != 0;
    return initiator_is_server == (conn->side == QUIC_SIDE_SERVER);
}

static quic_stream_t *stream_create(uint64_t id, size_t buf_size) {
    quic_stream_t *s = (quic_stream_t *)calloc(1, sizeof(*s));
    if (!s) return NULL;

    s->id = id;
    s->state = QUIC_STREAM_OPEN;

    s->recv_buf_cap = buf_size;
    s->recv_buf = (uint8_t *)malloc(buf_size);
    s->send_buf_cap = buf_size;
    s->send_buf = (uint8_t *)malloc(buf_size);

    if (!s->recv_buf || !s->send_buf) {
        free(s->recv_buf);
        free(s->send_buf);
        free(s);
        return NULL;
    }

    s->recv_max_data = buf_size;
    s->send_max_data = buf_size;

#ifndef _WIN32
    pthread_mutex_init(&s->lock, NULL);
    pthread_cond_init(&s->read_cond, NULL);
    pthread_cond_init(&s->write_cond, NULL);
#endif

    return s;
}

static void stream_destroy(quic_stream_t *s) {
    if (!s) return;
#ifndef _WIN32
    pthread_mutex_destroy(&s->lock);
    pthread_cond_destroy(&s->read_cond);
    pthread_cond_destroy(&s->write_cond);
#endif
    free(s->recv_buf);
    free(s->send_buf);
    free(s);
}

void neverc_quic_conn_destroy(struct neverc_quic_conn *conn);

/* ======================================================================
 * Connection Lifecycle
 * ====================================================================== */

struct neverc_quic_conn *neverc_quic_conn_create(quic_conn_side_t side,
                                                   int udp_fd) {
    struct neverc_quic_conn *conn = (struct neverc_quic_conn *)calloc(1, sizeof(*conn));
    if (!conn) return NULL;

    conn->state = QUIC_CONN_IDLE;
    conn->side = side;
    conn->udp_fd = udp_fd;
    conn->idle_timeout_ms = 30000;
    conn->last_activity_ms = quic_monotonic_ms();
    conn->handshake_start_ms = conn->last_activity_ms;

    /* Initialize stream IDs based on side */
    if (side == QUIC_SIDE_CLIENT) {
        conn->next_bidi_stream_id = 0; /* client-initiated bidi: 0, 4, 8, ... */
        conn->next_uni_stream_id = 2;  /* client-initiated uni: 2, 6, 10, ... */
    } else {
        conn->next_bidi_stream_id = 1; /* server-initiated bidi: 1, 5, 9, ... */
        conn->next_uni_stream_id = 3;  /* server-initiated uni: 3, 7, 11, ... */
    }

    conn->peer_max_streams_bidi = 100;
    conn->peer_max_streams_uni = 100;

    /* Default flow control */
    conn->flow.max_data_local = 10 * 1024 * 1024;
    conn->flow.max_data_peer = 10 * 1024 * 1024;

#ifndef _WIN32
    pthread_mutex_init(&conn->lock, NULL);
    pthread_cond_init(&conn->stream_avail_cond, NULL);
#endif

    /* Generate initial connection ID and reset token from the OS CSPRNG. */
    if (conn_add_local_cid(conn) != 0) {
        neverc_quic_conn_destroy(conn);
        return NULL;
    }

    return conn;
}

void neverc_quic_conn_destroy(struct neverc_quic_conn *conn) {
    if (!conn) return;

    for (int i = 0; i < conn->n_streams; i++) {
        stream_destroy(conn->streams[i]);
    }

#ifndef _WIN32
    pthread_mutex_destroy(&conn->lock);
    pthread_cond_destroy(&conn->stream_avail_cond);
#endif

    if (conn->udp_fd >= 0) {
#ifdef _WIN32
        closesocket(conn->udp_fd);
#else
        close(conn->udp_fd);
#endif
    }

    free(conn);
}

/* ======================================================================
 * Stream Operations (User-facing API)
 * ====================================================================== */

quic_stream_t *neverc_quic_conn_open_stream(struct neverc_quic_conn *conn) {
    if (!conn || conn->state != QUIC_CONN_ESTABLISHED) return NULL;

#ifndef _WIN32
    pthread_mutex_lock(&conn->lock);
#endif

    if (conn->n_streams >= QUIC_MAX_STREAMS) {
#ifndef _WIN32
        pthread_mutex_unlock(&conn->lock);
#endif
        return NULL;
    }

    uint64_t id = conn->next_bidi_stream_id;
    conn->next_bidi_stream_id += 4;

    quic_stream_t *s = stream_create(id, 1024 * 1024);
    if (!s) {
#ifndef _WIN32
        pthread_mutex_unlock(&conn->lock);
#endif
        return NULL;
    }

    conn->streams[conn->n_streams++] = s;

#ifndef _WIN32
    pthread_mutex_unlock(&conn->lock);
#endif

    return s;
}

quic_stream_t *neverc_quic_conn_open_uni_stream(struct neverc_quic_conn *conn) {
    if (!conn || conn->state != QUIC_CONN_ESTABLISHED) return NULL;

#ifndef _WIN32
    pthread_mutex_lock(&conn->lock);
#endif

    if (conn->n_streams >= QUIC_MAX_STREAMS) {
#ifndef _WIN32
        pthread_mutex_unlock(&conn->lock);
#endif
        return NULL;
    }

    uint64_t id = conn->next_uni_stream_id;
    conn->next_uni_stream_id += 4;

    quic_stream_t *s = stream_create(id, 1024 * 1024);
    if (!s) {
#ifndef _WIN32
        pthread_mutex_unlock(&conn->lock);
#endif
        return NULL;
    }

    conn->streams[conn->n_streams++] = s;

#ifndef _WIN32
    pthread_mutex_unlock(&conn->lock);
#endif

    return s;
}

int neverc_quic_stream_write_data(quic_stream_t *s,
                                   const void *data, size_t len) {
    if (!s || !data || len == 0) return -1;
    if (s->state == QUIC_STREAM_CLOSED ||
        s->state == QUIC_STREAM_HALF_CLOSED_LOCAL ||
        s->state == QUIC_STREAM_RESET) return -1;

#ifndef _WIN32
    pthread_mutex_lock(&s->lock);
#endif

    /* Buffer the data for transmission */
    size_t avail = s->send_buf_cap - s->send_len;
    if (len > avail) {
        /* Grow buffer */
        size_t new_cap = s->send_buf_cap * 2;
        while (new_cap < s->send_len + len) new_cap *= 2;
        uint8_t *new_buf = (uint8_t *)realloc(s->send_buf, new_cap);
        if (!new_buf) {
#ifndef _WIN32
            pthread_mutex_unlock(&s->lock);
#endif
            return -1;
        }
        s->send_buf = new_buf;
        s->send_buf_cap = new_cap;
    }

    memcpy(s->send_buf + s->send_len, data, len);
    s->send_len += len;

#ifndef _WIN32
    pthread_mutex_unlock(&s->lock);
#endif

    return (int)len;
}

int neverc_quic_stream_read_data(quic_stream_t *s, void *buf, size_t len) {
    if (!s || !buf || len == 0) return -1;
    if (s->state == QUIC_STREAM_CLOSED ||
        s->state == QUIC_STREAM_RESET) return -1;

#ifndef _WIN32
    pthread_mutex_lock(&s->lock);

    /* Wait for data to be available */
    while (s->recv_len == 0 && !s->recv_fin &&
           s->state == QUIC_STREAM_OPEN) {
        pthread_cond_wait(&s->read_cond, &s->lock);
    }
#endif

    if (s->recv_len == 0) {
#ifndef _WIN32
        pthread_mutex_unlock(&s->lock);
#endif
        return s->recv_fin ? 0 : -1;  /* 0 = FIN (EOF), -1 = error */
    }

    size_t to_read = len < s->recv_len ? len : s->recv_len;
    memcpy(buf, s->recv_buf, to_read);

    /* Shift remaining data */
    s->recv_len -= to_read;
    if (s->recv_len > 0) {
        memmove(s->recv_buf, s->recv_buf + to_read, s->recv_len);
    }
    s->recv_offset += to_read;

#ifndef _WIN32
    pthread_mutex_unlock(&s->lock);
#endif

    return (int)to_read;
}

int neverc_quic_stream_close_write_side(quic_stream_t *s) {
    if (!s) return -1;

#ifndef _WIN32
    pthread_mutex_lock(&s->lock);
#endif

    s->send_fin = 1;
    if (s->state == QUIC_STREAM_OPEN)
        s->state = QUIC_STREAM_HALF_CLOSED_LOCAL;
    else if (s->state == QUIC_STREAM_HALF_CLOSED_REMOTE)
        s->state = QUIC_STREAM_CLOSED;

#ifndef _WIN32
    pthread_mutex_unlock(&s->lock);
#endif

    return 0;
}

/* ======================================================================
 * Connection Close
 * ====================================================================== */

void neverc_quic_conn_close_internal(struct neverc_quic_conn *conn,
                                      uint64_t error_code,
                                      const char *reason,
                                      int is_app) {
    if (!conn) return;

#ifndef _WIN32
    pthread_mutex_lock(&conn->lock);
#endif

    if (conn->state == QUIC_CONN_CLOSED || conn->state == QUIC_CONN_DRAINING) {
#ifndef _WIN32
        pthread_mutex_unlock(&conn->lock);
#endif
        return;
    }

    conn->state = QUIC_CONN_DRAINING;
    conn->close_error_code = error_code;
    conn->close_is_app = is_app;
    if (reason) {
        size_t rlen = strlen(reason);
        if (rlen >= sizeof(conn->close_reason))
            rlen = sizeof(conn->close_reason) - 1;
        memcpy(conn->close_reason, reason, rlen);
        conn->close_reason[rlen] = '\0';
    }

    /* Wake all waiting streams */
    for (int i = 0; i < conn->n_streams; i++) {
        quic_stream_t *s = conn->streams[i];
        if (s) {
#ifndef _WIN32
            pthread_mutex_lock(&s->lock);
            s->state = QUIC_STREAM_CLOSED;
            pthread_cond_broadcast(&s->read_cond);
            pthread_cond_broadcast(&s->write_cond);
            pthread_mutex_unlock(&s->lock);
#else
            s->state = QUIC_STREAM_CLOSED;
#endif
        }
    }

#ifndef _WIN32
    pthread_cond_broadcast(&conn->stream_avail_cond);
    pthread_mutex_unlock(&conn->lock);
#endif
}

/* ======================================================================
 * Connection Queries
 * ====================================================================== */

int neverc_quic_conn_is_alive_check(struct neverc_quic_conn *conn) {
    if (!conn) return 0;
    return conn->state == QUIC_CONN_ESTABLISHED ||
           conn->state == QUIC_CONN_HANDSHAKING;
}

const char *neverc_quic_conn_get_alpn(struct neverc_quic_conn *conn) {
    if (!conn || conn->alpn[0] == '\0') return NULL;
    return conn->alpn;
}

uint64_t neverc_quic_stream_get_id(quic_stream_t *s) {
    return s ? s->id : UINT64_MAX;
}
