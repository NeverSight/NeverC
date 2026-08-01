#ifndef NEVERC_NET_QUIC_INTERNAL_H
#define NEVERC_NET_QUIC_INTERNAL_H

#include "neverc/std/net/quic.h"

#include <stddef.h>
#include <stdint.h>

#define QUIC_MAX_CID_LEN 20
#define QUIC_PN_SPACE_COUNT 3

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

typedef enum {
    QUIC_ENC_INITIAL = 0,
    QUIC_ENC_HANDSHAKE = 1,
    QUIC_ENC_APPLICATION = 2,
    QUIC_ENC_EARLY_DATA = 3,
    QUIC_ENC_LEVEL_COUNT = 4,
} quic_enc_level_t;

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
void neverc_quic_loss_on_sent(quic_loss_detector_t *detector, int space,
                              uint64_t packet_number, uint64_t sent_time,
                              size_t sent_bytes, int ack_eliciting);
void neverc_quic_loss_mark_acked(quic_loss_detector_t *detector, int space,
                                 uint64_t packet_number, uint64_t now);
void neverc_quic_loss_on_ack(quic_loss_detector_t *detector, int space,
                             uint64_t largest_acked,
                             uint64_t ack_delay_ms, uint64_t now);
uint64_t neverc_quic_loss_get_timeout(
    const quic_loss_detector_t *detector);
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
int neverc_quic_tls_get_crypto_data(quic_tls_t *tls,
                                    quic_enc_level_t level,
                                    const uint8_t **data, size_t *len);
void neverc_quic_tls_crypto_data_sent(quic_tls_t *tls,
                                      quic_enc_level_t level,
                                      size_t bytes_sent);
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
const quic_keys_t *neverc_quic_tls_get_read_keys(
    const quic_tls_t *tls, quic_enc_level_t level);
const quic_keys_t *neverc_quic_tls_get_write_keys(
    const quic_tls_t *tls, quic_enc_level_t level);

#endif
