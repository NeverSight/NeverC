#include "neverc/std/net/http/http2.h"
#include "neverc/std/net/http3.h"
#include "neverc/std/net/rpc.h"
#include "_quic_internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint64_t type;
    uint64_t length;
    size_t header_size;
} fuzz_h3_frame_header_t;

typedef struct {
    uint64_t qpack_max_table_capacity;
    uint64_t max_field_section_size;
    uint64_t qpack_blocked_streams;
} fuzz_h3_settings_t;

int neverc_h3_parse_frame_header(const uint8_t *input, size_t input_length,
                                 fuzz_h3_frame_header_t *header);
int neverc_h3_settings_decode(const uint8_t *input, size_t input_length,
                              fuzz_h3_settings_t *settings);

static void fuzz_h2(const uint8_t *input, size_t input_length,
                    uint8_t selector) {
    neverc_h2_frame_header_t frame_header;
    (void)neverc_h2_frame_header_read(input, input_length, &frame_header);

    uint8_t decoded[64 * 1024];
    size_t decoded_length = 0;
    (void)neverc_hpack_huffman_decode(input, input_length, decoded,
                                      sizeof(decoded), &decoded_length);

    neverc_hpack_decoder_t *decoder = neverc_hpack_decoder_create(
        selector & 1U ? 0U : NC_H2_DEFAULT_HEADER_TABLE_SIZE);
    if (!decoder) return;
    neverc_hpack_header_t headers[64];
    int header_count = 0;
    (void)neverc_hpack_decode(decoder, input, input_length, headers, 64,
                              &header_count);
    for (int i = 0; i < header_count; i++) {
        free(headers[i].name);
        free(headers[i].value);
    }
    neverc_hpack_decoder_destroy(decoder);
}

static void fuzz_quic(const uint8_t *input, size_t input_length,
                      uint8_t selector) {
    size_t consumed = 0;
    switch ((selector >> 3) % 9U) {
    case 0: {
        quic_packet_header_t header;
        (void)neverc_quic_parse_packet_header(input, input_length, &header,
                                               selector % 21U);
        break;
    }
    case 1: {
        quic_frame_crypto_t frame;
        (void)neverc_quic_parse_crypto_frame(input, input_length, &frame,
                                              &consumed);
        break;
    }
    case 2: {
        quic_frame_stream_t frame;
        (void)neverc_quic_parse_stream_frame(input, input_length, &frame,
                                              &consumed);
        break;
    }
    case 3: {
        quic_frame_ack_t frame;
        memset(&frame, 0, sizeof(frame));
        if (neverc_quic_parse_ack_frame(input, input_length, &frame,
                                        &consumed) == 0)
            free(frame.ranges);
        break;
    }
    case 4: {
        quic_frame_reset_stream_t frame;
        (void)neverc_quic_parse_reset_stream(input, input_length, &frame,
                                              &consumed);
        break;
    }
    case 5: {
        quic_frame_stop_sending_t frame;
        (void)neverc_quic_parse_stop_sending(input, input_length, &frame,
                                              &consumed);
        break;
    }
    case 6: {
        quic_frame_new_conn_id_t frame;
        (void)neverc_quic_parse_new_conn_id(input, input_length, &frame,
                                             &consumed);
        break;
    }
    case 7: {
        quic_frame_connection_close_t frame;
        (void)neverc_quic_parse_connection_close(input, input_length, &frame,
                                                  &consumed);
        break;
    }
    default: {
        quic_transport_params_t parameters;
        (void)neverc_quic_transport_params_decode(input, input_length,
                                                   &parameters);
        break;
    }
    }
}

static void fuzz_h3(const uint8_t *input, size_t input_length) {
    fuzz_h3_frame_header_t frame_header;
    (void)neverc_h3_parse_frame_header(input, input_length, &frame_header);
    fuzz_h3_settings_t settings;
    (void)neverc_h3_settings_decode(input, input_length, &settings);

    neverc_qpack_decoder_t *decoder = neverc_qpack_decoder_create(0);
    if (!decoder) return;
    neverc_qpack_header_t headers[64];
    int header_count = 0;
    (void)neverc_qpack_decode(decoder, input, input_length, headers, 64,
                              &header_count);
    for (int i = 0; i < header_count; i++) {
        free(headers[i].name);
        free(headers[i].value);
    }
    neverc_qpack_decoder_destroy(decoder);
}

static void fuzz_rpc(const uint8_t *input, size_t input_length) {
    neverc_rpc_frame_t frame;
    size_t consumed = 0;
    (void)neverc_rpc_frame_decode(input, input_length, 64 * 1024, &frame,
                                  &consumed);
    neverc_rpc_metadata_t metadata[NEVERC_RPC_DEFAULT_MAX_METADATA_COUNT];
    neverc_rpc_open_t open;
    memset(&open, 0, sizeof(open));
    open.metadata = metadata;
    open.metadata_capacity = NEVERC_RPC_DEFAULT_MAX_METADATA_COUNT;
    (void)neverc_rpc_open_decode(input, input_length, 64 * 1024, &open);
}

void neverc_network_test_fuzz_binary_protocols(uint8_t selector,
                                               const uint8_t *input,
                                               size_t input_length) {
    switch (selector % 6U) {
    case 2:
        fuzz_h2(input, input_length, selector);
        break;
    case 3:
        fuzz_quic(input, input_length, selector);
        break;
    case 4:
        fuzz_h3(input, input_length);
        break;
    case 5:
        fuzz_rpc(input, input_length);
        break;
    default:
        break;
    }
}
