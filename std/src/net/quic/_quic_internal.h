#ifndef NEVERC_NET_QUIC_INTERNAL_H
#define NEVERC_NET_QUIC_INTERNAL_H

#include "neverc/std/net/quic.h"
#include "neverc/std/net/udp.h"

#include "../_net_platform.h"

#include <stddef.h>
#include <stdint.h>

#define QUIC_MAX_CID_LEN 20
#define QUIC_PN_SPACE_COUNT 3
#define QUIC_MAX_LOCAL_CONN_IDS 8
#define QUIC_MAX_PEER_CONN_IDS 8
#define QUIC_MAX_STREAMS 1024
#define QUIC_MAX_CONNECTIONS 4096
#define QUIC_MAX_PENDING_ACCEPTS 1024
#define QUIC_DATAGRAM_QUEUE_CAPACITY 256
#define QUIC_TX_RECORD_CAPACITY 512
#define QUIC_STREAM_SEND_BUFFER_LIMIT (1024U * 1024U)
#define QUIC_MIN_INITIAL_SIZE 1200
#define QUIC_DEFAULT_MAX_PACKET_SIZE 1200
#define QUIC_MAX_PACKET_SIZE 65527
#define QUIC_VARINT_MAX ((UINT64_C(1) << 62) - 1)

#define QUIC_FRAME_PADDING              0x00
#define QUIC_FRAME_PING                 0x01
#define QUIC_FRAME_ACK                  0x02
#define QUIC_FRAME_ACK_ECN              0x03
#define QUIC_FRAME_RESET_STREAM         0x04
#define QUIC_FRAME_STOP_SENDING         0x05
#define QUIC_FRAME_CRYPTO               0x06
#define QUIC_FRAME_NEW_TOKEN            0x07
#define QUIC_FRAME_STREAM_BASE          0x08
#define QUIC_FRAME_MAX_DATA             0x10
#define QUIC_FRAME_MAX_STREAM_DATA      0x11
#define QUIC_FRAME_MAX_STREAMS_BIDI     0x12
#define QUIC_FRAME_MAX_STREAMS_UNI      0x13
#define QUIC_FRAME_DATA_BLOCKED         0x14
#define QUIC_FRAME_STREAM_DATA_BLOCKED  0x15
#define QUIC_FRAME_STREAMS_BLOCKED_BIDI 0x16
#define QUIC_FRAME_STREAMS_BLOCKED_UNI  0x17
#define QUIC_FRAME_NEW_CONNECTION_ID    0x18
#define QUIC_FRAME_RETIRE_CONNECTION_ID 0x19
#define QUIC_FRAME_PATH_CHALLENGE       0x1a
#define QUIC_FRAME_PATH_RESPONSE        0x1b
#define QUIC_FRAME_CONNECTION_CLOSE     0x1c
#define QUIC_FRAME_CONNECTION_CLOSE_APP 0x1d
#define QUIC_FRAME_HANDSHAKE_DONE       0x1e
#define QUIC_FRAME_DATAGRAM             0x30
#define QUIC_FRAME_DATAGRAM_LEN         0x31

#define QUIC_ERR_INTERNAL_ERROR              0x01U
#define QUIC_ERR_CONNECTION_REFUSED          0x02U
#define QUIC_ERR_FLOW_CONTROL_ERROR          0x03U
#define QUIC_ERR_STREAM_LIMIT_ERROR           0x04U
#define QUIC_ERR_STREAM_STATE_ERROR           0x05U
#define QUIC_ERR_FINAL_SIZE_ERROR             0x06U
#define QUIC_ERR_FRAME_ENCODING_ERROR         0x07U
#define QUIC_ERR_TRANSPORT_PARAMETER_ERROR    0x08U
#define QUIC_ERR_CONNECTION_ID_LIMIT_ERROR    0x09U
#define QUIC_ERR_PROTOCOL_VIOLATION           0x0aU
#define QUIC_ERR_NO_VIABLE_PATH               0x10U

typedef enum {
    QUIC_PKT_INITIAL = 0,
    QUIC_PKT_0RTT = 1,
    QUIC_PKT_HANDSHAKE = 2,
    QUIC_PKT_RETRY = 3,
    QUIC_PKT_1RTT = 4,
} quic_packet_type_t;

typedef struct {
    uint8_t data[QUIC_MAX_CID_LEN];
    uint8_t len;
} quic_conn_id_t;

typedef struct {
    quic_packet_type_t type;
    uint32_t version;
    quic_conn_id_t dcid;
    quic_conn_id_t scid;
    uint64_t pkt_number;
    uint8_t pkt_number_len;
    uint8_t key_phase;
    uint8_t spin_bit;
    const uint8_t *token;
    size_t token_len;
    size_t header_len;
    size_t payload_len;
} quic_packet_header_t;

typedef struct {
    uint8_t key[16];
    uint8_t iv[12];
    uint8_t hp[16];
} quic_keys_t;

typedef struct {
    quic_keys_t client;
    quic_keys_t server;
} quic_initial_keys_t;

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

typedef enum {
    QUIC_PNS_INITIAL = 0,
    QUIC_PNS_HANDSHAKE = 1,
    QUIC_PNS_APPLICATION = 2,
    QUIC_PNS_COUNT = 3,
} quic_pn_space_t;

typedef enum {
    QUIC_ENC_INITIAL = 0,
    QUIC_ENC_HANDSHAKE = 1,
    QUIC_ENC_APPLICATION = 2,
    QUIC_ENC_EARLY_DATA = 3,
    QUIC_ENC_LEVEL_COUNT = 4,
} quic_enc_level_t;

typedef enum {
    QUIC_STREAM_IDLE = 0,
    QUIC_STREAM_OPEN,
    QUIC_STREAM_HALF_CLOSED_LOCAL,
    QUIC_STREAM_HALF_CLOSED_REMOTE,
    QUIC_STREAM_CLOSED,
    QUIC_STREAM_RESET,
} quic_stream_state_t;

typedef struct {
    uint8_t id[QUIC_MAX_CID_LEN];
    uint8_t len;
    uint64_t sequence;
    uint8_t stateless_reset_token[16];
    int retired;
} quic_conn_id_entry_t;

typedef struct quic_fragment {
    uint64_t offset;
    size_t len;
    uint8_t *data;
    struct quic_fragment *next;
} quic_fragment_t;

typedef struct {
    uint64_t next_pn;
    uint64_t largest_recv;
    uint64_t largest_acked;
    uint64_t acked_floor;
    uint64_t received_bitmap;
    int has_recv;
    int ack_pending;
} quic_pn_state_t;

typedef struct {
    uint64_t max_data;
    uint64_t data_sent;
    uint64_t max_data_peer;
    uint64_t data_received;
    uint64_t max_data_local;
    uint64_t data_consumed;
} quic_flow_control_t;

struct neverc_quic_conn;

typedef struct neverc_quic_stream {
    struct neverc_quic_conn *conn;
    uint64_t id;
    quic_stream_state_t state;
    uint8_t *recv_buf;
    size_t recv_buf_cap;
    size_t recv_len;
    uint64_t recv_offset;
    uint64_t recv_highest;
    uint64_t recv_max_data;
    uint64_t recv_final_size;
    int recv_final_known;
    int recv_fin;
    quic_fragment_t *recv_fragments;
    uint8_t *send_buf;
    size_t send_buf_cap;
    size_t send_len;
    uint64_t send_offset;
    uint64_t send_highest;
    uint64_t send_max_data;
    size_t send_inflight;
    uint64_t send_inflight_pn;
    int send_fin;
    int send_fin_sent;
    int send_fin_acked;
    int reset_pending;
    uint64_t reset_error_code;
    int stop_sending_pending;
    uint64_t stop_sending_error_code;
    int peer_initiated;
    int application_accepted;
    int application_released;
    int accept_enqueued;
    int application_owned;
    nc_mutex_t lock;
    nc_cond_t read_cond;
    nc_cond_t write_cond;
} quic_stream_t;

typedef struct quic_datagram_entry {
    uint8_t *data;
    size_t len;
} quic_datagram_entry_t;

typedef enum {
    QUIC_TX_NONE = 0,
    QUIC_TX_CRYPTO,
    QUIC_TX_STREAM,
    QUIC_TX_CONTROL,
} quic_tx_kind_t;

typedef struct {
    int used;
    int acked;
    int space;
    uint64_t packet_number;
    uint64_t sent_at_ms;
    size_t packet_bytes;
    quic_tx_kind_t kind;
    quic_enc_level_t level;
    uint64_t offset;
    size_t length;
    uint64_t stream_id;
    int fin;
} quic_tx_record_t;

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
    size_t data_len;
} quic_frame_crypto_t;

typedef struct {
    uint64_t stream_id;
    uint64_t offset;
    const uint8_t *data;
    size_t data_len;
    int fin;
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
    int is_bidi;
} quic_frame_max_streams_t;

typedef struct {
    uint64_t sequence;
    uint64_t retire_prior_to;
    uint8_t conn_id_len;
    uint8_t conn_id[QUIC_MAX_CID_LEN];
    uint8_t stateless_reset_token[16];
} quic_frame_new_conn_id_t;

typedef struct {
    uint64_t sequence;
} quic_frame_retire_conn_id_t;

typedef struct {
    uint8_t data[8];
} quic_frame_path_challenge_t;

typedef struct {
    uint64_t error_code;
    uint64_t frame_type;
    const char *reason;
    size_t reason_len;
    int is_app;
} quic_frame_connection_close_t;

typedef struct {
    uint64_t start;
    uint64_t end;
} quic_ack_range_t;

typedef struct {
    uint64_t largest_acked;
    uint64_t ack_delay;
    quic_ack_range_t *ranges;
    int nranges;
    uint64_t ect0;
    uint64_t ect1;
    uint64_t ecn_ce;
} quic_frame_ack_t;

typedef struct {
    uint8_t original_dcid[QUIC_MAX_CID_LEN];
    uint8_t original_dcid_len;
    uint8_t initial_scid[QUIC_MAX_CID_LEN];
    uint8_t initial_scid_len;
    uint8_t retry_scid[QUIC_MAX_CID_LEN];
    uint8_t retry_scid_len;
    int has_retry_scid;
    uint64_t max_idle_timeout;
    uint64_t max_udp_payload_size;
    uint64_t initial_max_data;
    uint64_t initial_max_stream_data_bidi_local;
    uint64_t initial_max_stream_data_bidi_remote;
    uint64_t initial_max_stream_data_uni;
    uint64_t initial_max_streams_bidi;
    uint64_t initial_max_streams_uni;
    uint64_t ack_delay_exponent;
    uint64_t max_ack_delay;
    uint64_t active_connection_id_limit;
    uint64_t max_datagram_frame_size;
    int disable_active_migration;
    uint8_t stateless_reset_token[16];
    int has_stateless_reset_token;
} quic_transport_params_t;

typedef struct {
    uint64_t min_rtt;
    uint64_t smoothed_rtt;
    uint64_t rttvar;
    uint64_t latest_rtt;
    int has_sample;
    uint64_t max_ack_delay;
} quic_rtt_t;

typedef struct quic_sent_packet {
    uint64_t pkt_number;
    uint64_t sent_time_ms;
    size_t sent_bytes;
    int ack_eliciting;
    int in_flight;
    int lost;
    int acked;
    struct quic_sent_packet *next;
} quic_sent_packet_t;

typedef struct {
    quic_sent_packet_t *sent_packets;
    uint64_t largest_acked_packet;
    uint64_t loss_time;
    uint64_t time_of_last_ack_eliciting;
    int has_largest_acked;
} quic_loss_space_t;

typedef struct {
    uint64_t congestion_window;
    uint64_t bytes_in_flight;
    uint64_t ssthresh;
    uint64_t recovery_start_time;
    int in_recovery;
    uint64_t max_datagram_size;
} quic_congestion_t;

typedef struct {
    quic_rtt_t rtt;
    quic_loss_space_t spaces[QUIC_PN_SPACE_COUNT];
    quic_congestion_t cc;
    uint64_t pto_count;
} quic_loss_detector_t;

typedef struct quic_tls quic_tls_t;

struct neverc_quic_endpoint {
    neverc_udp_conn_t *udp;
    neverc_quic_config_t config;
    char *cert_file;
    char *key_file;
    char **alpn;
    size_t alpn_count;
    struct neverc_quic_conn *connections[QUIC_MAX_CONNECTIONS];
    size_t connection_count;
    struct neverc_quic_conn *accept_queue[QUIC_MAX_PENDING_ACCEPTS];
    size_t accept_head;
    size_t accept_tail;
    size_t accept_count;
    nc_mutex_t lock;
    nc_cond_t accept_cond;
    nc_thread_t io_thread;
    int io_thread_started;
    volatile int running;
    volatile int ref_count;
    char error[256];
};

struct neverc_quic_conn {
    _Atomic quic_conn_state_t state;
    quic_conn_side_t side;
    neverc_udp_conn_t *udp;
    int owns_udp;
    int udp_fd;
    struct neverc_quic_endpoint *endpoint;
    neverc_udp_addr_t peer_addr;
    char remote_addr[64];
    quic_conn_id_entry_t local_cids[QUIC_MAX_LOCAL_CONN_IDS];
    int n_local_cids;
    quic_conn_id_entry_t peer_cids[QUIC_MAX_PEER_CONN_IDS];
    int n_peer_cids;
    int active_peer_cid_idx;
    uint64_t next_local_cid_seq;
    quic_conn_id_t initial_dcid;
    uint32_t version;
    quic_pn_state_t pn[QUIC_PNS_COUNT];
    quic_stream_t *streams[QUIC_MAX_STREAMS];
    int n_streams;
    uint64_t next_bidi_stream_id;
    uint64_t next_uni_stream_id;
    uint64_t peer_max_streams_bidi;
    uint64_t peer_max_streams_uni;
    uint64_t opened_peer_streams_bidi;
    uint64_t opened_peer_streams_uni;
    quic_flow_control_t flow;
    quic_transport_params_t local_params;
    quic_transport_params_t peer_params;
    quic_tls_t *tls;
    quic_loss_detector_t loss;
    quic_tx_record_t tx_records[QUIC_TX_RECORD_CAPACITY];
    uint64_t idle_timeout_ms;
    uint64_t last_activity_ms;
    uint64_t handshake_start_ms;
    int handshake_confirmed;
    int peer_completed_address_validation;
    uint64_t validation_pto_deadline_ms;
    uint64_t draining_started_ms;
    uint64_t bytes_received_before_validation;
    uint64_t bytes_sent_before_validation;
    int address_validated;
    char alpn[32];
    uint64_t close_error_code;
    char close_reason[256];
    int close_is_app;
    int close_pending;
    int handshake_done_pending;
    int new_cid_pending;
    int new_cid_retransmit_index;
    int max_data_pending;
    quic_stream_t *max_stream_data_pending;
    int pto_probe_pending;
    quic_enc_level_t pto_probe_level;
    quic_datagram_entry_t recv_datagrams[QUIC_DATAGRAM_QUEUE_CAPACITY];
    size_t recv_datagram_head;
    size_t recv_datagram_tail;
    size_t recv_datagram_count;
    quic_datagram_entry_t send_datagrams[QUIC_DATAGRAM_QUEUE_CAPACITY];
    size_t send_datagram_head;
    size_t send_datagram_tail;
    size_t send_datagram_count;
    neverc_udp_addr_t candidate_addr;
    neverc_udp_addr_t path_response_addr;
    uint8_t path_challenge[8];
    uint8_t path_response[8];
    int path_challenge_pending;
    int path_validation_pending;
    int path_response_pending;
    int peer_disable_migration;
    nc_mutex_t lock;
    nc_cond_t stream_avail_cond;
    nc_cond_t datagram_cond;
    nc_cond_t state_cond;
    nc_thread_t io_thread;
    int io_thread_started;
    _Atomic int io_running;
    int sync_initialized;
    int accept_enqueued;
    int application_owned;
    int application_released;
    char error[256];
};

int neverc_quic_varint_decode(const uint8_t *buf, size_t len,
                              uint64_t *value, size_t *consumed);
int neverc_quic_varint_encode(uint64_t value, uint8_t *buf, size_t cap,
                              size_t *written);
size_t neverc_quic_varint_len(uint64_t value);

int neverc_quic_parse_packet_header(const uint8_t *buf, size_t len,
                                    quic_packet_header_t *header,
                                    uint8_t expected_dcid_len);
int neverc_quic_write_long_header(uint8_t *buf, size_t cap,
                                  const quic_packet_header_t *header,
                                  size_t *written);
uint64_t neverc_quic_decode_packet_number(uint64_t largest_received,
                                          uint64_t truncated,
                                          unsigned packet_number_bits);

int neverc_quic_derive_initial_keys(const uint8_t *dcid, size_t dcid_len,
                                    uint32_t version,
                                    quic_initial_keys_t *keys);
int neverc_quic_encrypt_payload(const quic_keys_t *keys,
                                uint64_t packet_number,
                                const uint8_t *header, size_t header_len,
                                const uint8_t *plaintext,
                                size_t plaintext_len, uint8_t *output);
int neverc_quic_decrypt_payload(const quic_keys_t *keys,
                                uint64_t packet_number,
                                const uint8_t *header, size_t header_len,
                                const uint8_t *ciphertext,
                                size_t ciphertext_len, uint8_t *output);
int neverc_quic_apply_header_protection(const uint8_t *hp_key,
                                        uint8_t *packet, size_t packet_len,
                                        size_t packet_number_offset);
int neverc_quic_remove_header_protection(const uint8_t *hp_key,
                                         uint8_t *packet, size_t packet_len,
                                         size_t packet_number_offset);

int neverc_quic_parse_crypto_frame(const uint8_t *buf, size_t len,
                                   quic_frame_crypto_t *output,
                                   size_t *consumed);
int neverc_quic_parse_stream_frame(const uint8_t *buf, size_t len,
                                   quic_frame_stream_t *output,
                                   size_t *consumed);
int neverc_quic_parse_ack_frame(const uint8_t *buf, size_t len,
                                quic_frame_ack_t *output,
                                size_t *consumed);
int neverc_quic_parse_reset_stream(const uint8_t *buf, size_t len,
                                   quic_frame_reset_stream_t *output,
                                   size_t *consumed);
int neverc_quic_parse_stop_sending(const uint8_t *buf, size_t len,
                                   quic_frame_stop_sending_t *output,
                                   size_t *consumed);
int neverc_quic_parse_new_conn_id(const uint8_t *buf, size_t len,
                                  quic_frame_new_conn_id_t *output,
                                  size_t *consumed);
int neverc_quic_parse_connection_close(
    const uint8_t *buf, size_t len,
    quic_frame_connection_close_t *output, size_t *consumed);
int neverc_quic_write_crypto_frame(uint8_t *buf, size_t cap,
                                   uint64_t offset, const uint8_t *data,
                                   size_t data_len, size_t *written);
int neverc_quic_write_stream_frame(uint8_t *buf, size_t cap,
                                   const quic_frame_stream_t *frame,
                                   size_t *written);
int neverc_quic_write_ack_frame(uint8_t *buf, size_t cap,
                                const quic_frame_ack_t *ack,
                                size_t *written);
int neverc_quic_write_connection_close(
    uint8_t *buf, size_t cap,
    const quic_frame_connection_close_t *close_frame, size_t *written);
int neverc_quic_write_max_data(uint8_t *buf, size_t cap,
                               uint64_t max_data, size_t *written);
int neverc_quic_write_max_stream_data(uint8_t *buf, size_t cap,
                                      uint64_t stream_id,
                                      uint64_t max_data, size_t *written);
int neverc_quic_write_reset_stream(uint8_t *buf, size_t cap,
                                   uint64_t stream_id,
                                   uint64_t error_code,
                                   uint64_t final_size, size_t *written);
int neverc_quic_write_ping(uint8_t *buf, size_t cap, size_t *written);
int neverc_quic_write_handshake_done(uint8_t *buf, size_t cap,
                                     size_t *written);

void neverc_quic_transport_params_default(quic_transport_params_t *params);
int neverc_quic_transport_params_decode(const uint8_t *buf, size_t len,
                                        quic_transport_params_t *params);
int neverc_quic_transport_params_encode(
    const quic_transport_params_t *params, uint8_t *buf, size_t cap,
    size_t *written);

void neverc_quic_loss_init(quic_loss_detector_t *detector);
uint64_t neverc_quic_pto(const quic_rtt_t *rtt,
                         int include_max_ack_delay);
void neverc_quic_loss_on_sent(quic_loss_detector_t *detector, int space,
                              uint64_t packet_number, uint64_t sent_time,
                              size_t sent_bytes, int ack_eliciting);
void neverc_quic_loss_mark_acked(quic_loss_detector_t *detector, int space,
                                 uint64_t packet_number, uint64_t now);
void neverc_quic_loss_on_ack(quic_loss_detector_t *detector, int space,
                             uint64_t largest_acked,
                             uint64_t ack_delay_ms, uint64_t now);
int neverc_quic_loss_detect(quic_loss_detector_t *detector, uint64_t now);
uint64_t neverc_quic_loss_get_timeout(
    const quic_loss_detector_t *detector, int handshake_confirmed);
void neverc_quic_loss_cleanup(quic_loss_detector_t *detector, int space);
void neverc_quic_loss_destroy(quic_loss_detector_t *detector);

quic_tls_t *neverc_quic_tls_create(int is_server);
void neverc_quic_tls_destroy(quic_tls_t *tls);
int neverc_quic_tls_set_initial_dcid(quic_tls_t *tls,
                                     const uint8_t *dcid,
                                     size_t dcid_len, uint32_t version);
int neverc_quic_tls_receive_crypto(quic_tls_t *tls, quic_enc_level_t level,
                                   uint64_t offset, const uint8_t *data,
                                   size_t len);
int neverc_quic_tls_configure(quic_tls_t *tls,
                              const neverc_quic_config_t *config,
                              const char *server_name,
                              const quic_transport_params_t *local_params,
                              quic_transport_params_t *peer_params);
int neverc_quic_tls_start(quic_tls_t *tls);
int neverc_quic_tls_process(quic_tls_t *tls);
int neverc_quic_tls_get_crypto_data(quic_tls_t *tls,
                                    quic_enc_level_t level,
                                    uint64_t *offset,
                                    const uint8_t **data, size_t *len);
void neverc_quic_tls_crypto_data_sent(quic_tls_t *tls,
                                      quic_enc_level_t level,
                                      size_t bytes_sent);
void neverc_quic_tls_crypto_data_acked(quic_tls_t *tls,
                                       quic_enc_level_t level,
                                       uint64_t offset, size_t length);
void neverc_quic_tls_crypto_data_lost(quic_tls_t *tls,
                                      quic_enc_level_t level,
                                      uint64_t offset);
int neverc_quic_tls_install_keys(quic_tls_t *tls, quic_enc_level_t level,
                                 const quic_keys_t *read_key,
                                 const quic_keys_t *write_key);
int neverc_quic_tls_key_update(quic_tls_t *tls);
int neverc_quic_tls_handshake_complete(
    quic_tls_t *tls, const uint8_t *client_app_secret,
    const uint8_t *server_app_secret, const char *negotiated_alpn);
int neverc_quic_tls_is_established(const quic_tls_t *tls);
const char *neverc_quic_tls_alpn(const quic_tls_t *tls);
int neverc_quic_tls_get_key_phase(const quic_tls_t *tls);
int neverc_quic_tls_get_read_key_phase(const quic_tls_t *tls);
int neverc_quic_tls_prepare_read_key_update(quic_tls_t *tls,
                                             quic_keys_t *next_keys);
int neverc_quic_tls_commit_read_key_update(quic_tls_t *tls,
                                            const quic_keys_t *next_keys);
void neverc_quic_tls_discard_read_key_update(quic_tls_t *tls);
const quic_keys_t *neverc_quic_tls_get_read_keys(
    const quic_tls_t *tls, quic_enc_level_t level);
const quic_keys_t *neverc_quic_tls_get_write_keys(
    const quic_tls_t *tls, quic_enc_level_t level);
const char *neverc_quic_tls_error(const quic_tls_t *tls);

struct neverc_quic_conn *neverc_quic_conn_create(quic_conn_side_t side,
                                                  int udp_fd);
void neverc_quic_conn_destroy(struct neverc_quic_conn *conn);
int neverc_quic_conn_configure(struct neverc_quic_conn *conn,
                               const neverc_quic_config_t *config,
                               neverc_udp_conn_t *udp, int owns_udp,
                               const neverc_udp_addr_t *peer,
                               struct neverc_quic_endpoint *endpoint,
                               const quic_conn_id_t *initial_dcid,
                               const quic_conn_id_t *peer_cid,
                               const char *server_name);
int neverc_quic_conn_start_client(struct neverc_quic_conn *conn);
int neverc_quic_conn_process_datagram(struct neverc_quic_conn *conn,
                                      const uint8_t *packet, size_t length,
                                      const neverc_udp_addr_t *source);
int neverc_quic_conn_flush(struct neverc_quic_conn *conn);
void neverc_quic_conn_tick(struct neverc_quic_conn *conn, uint64_t now_ms);
quic_stream_t *neverc_quic_conn_open_stream(struct neverc_quic_conn *conn);
quic_stream_t *neverc_quic_conn_open_uni_stream(
    struct neverc_quic_conn *conn);
int neverc_quic_stream_write_data(quic_stream_t *stream,
                                  const void *data, size_t length);
int neverc_quic_stream_read_data(quic_stream_t *stream,
                                 void *buffer, size_t capacity);
int neverc_quic_stream_try_read_data(quic_stream_t *stream,
                                     void *buffer, size_t capacity);
int neverc_quic_stream_close_write_side(quic_stream_t *stream);
void neverc_quic_conn_close_internal(struct neverc_quic_conn *conn,
                                     uint64_t error_code,
                                     const char *reason, int is_app);
void quic_stream_mark_connection_closing(quic_stream_t *stream);
void quic_conn_finalize_streams(struct neverc_quic_conn *conn);
int neverc_quic_conn_close_locked(struct neverc_quic_conn *conn,
                                  uint64_t error_code,
                                  const char *reason, int is_app);
int neverc_quic_conn_send_drained(struct neverc_quic_conn *conn);
int neverc_quic_conn_has_ack_eliciting_in_flight(
    const struct neverc_quic_conn *conn);
uint64_t neverc_quic_conn_loss_timeout(struct neverc_quic_conn *conn,
                                       uint64_t now_ms);
int neverc_quic_conn_is_alive_check(struct neverc_quic_conn *conn);
const char *neverc_quic_conn_get_alpn(struct neverc_quic_conn *conn);
uint64_t neverc_quic_stream_get_id(quic_stream_t *stream);
quic_stream_t *neverc_quic_conn_find_stream(
    struct neverc_quic_conn *conn, uint64_t stream_id);
int neverc_quic_stream_receive(struct neverc_quic_conn *conn,
                               const quic_frame_stream_t *frame);
int neverc_quic_stream_receive_locked(struct neverc_quic_conn *conn,
                                      const quic_frame_stream_t *frame);
int neverc_quic_stream_receive_reset(
    struct neverc_quic_conn *conn,
    const quic_frame_reset_stream_t *frame);
int neverc_quic_stream_receive_reset_locked(
    struct neverc_quic_conn *conn,
    const quic_frame_reset_stream_t *frame);
void neverc_quic_conn_on_packet_acked(struct neverc_quic_conn *conn,
                                      int space, uint64_t packet_number);
void neverc_quic_conn_on_packet_lost(struct neverc_quic_conn *conn,
                                     int space, uint64_t packet_number);
int neverc_quic_conn_id_matches(const struct neverc_quic_conn *conn,
                                const uint8_t *cid, size_t cid_len);
int neverc_quic_packet_number_offset(const uint8_t *packet, size_t length,
                                     uint8_t short_dcid_len,
                                     size_t *packet_number_offset);

#endif
