#include "neverc/std/net/rpc.h"

#include <limits.h>
#include <string.h>

static uint16_t rpc_get_u16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t rpc_get_u32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint64_t rpc_get_u64(const uint8_t *p) {
    uint64_t value = 0;
    for (int i = 0; i < 8; i++) value = (value << 8) | p[i];
    return value;
}

static void rpc_put_u16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

static void rpc_put_u32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static void rpc_put_u64(uint8_t *p, uint64_t value) {
    for (int i = 7; i >= 0; i--) {
        p[i] = (uint8_t)value;
        value >>= 8;
    }
}

static int rpc_frame_type_valid(uint8_t type) {
    return type >= NEVERC_RPC_FRAME_OPEN && type <= NEVERC_RPC_FRAME_GOAWAY;
}

static int rpc_frame_header_valid(const neverc_rpc_frame_header_t *header) {
    if (!header || header->version != NEVERC_RPC_VERSION_1 ||
        !rpc_frame_type_valid(header->type) ||
        (header->flags & ~(NEVERC_RPC_FLAG_END_STREAM |
                           NEVERC_RPC_FLAG_RESPONSE |
                           NEVERC_RPC_FLAG_IDEMPOTENT)) != 0)
        return 0;
    uint16_t allowed_flags = 0;
    switch (header->type) {
    case NEVERC_RPC_FRAME_OPEN:
        allowed_flags = NEVERC_RPC_FLAG_END_STREAM |
                        NEVERC_RPC_FLAG_IDEMPOTENT;
        break;
    case NEVERC_RPC_FRAME_DATA:
        allowed_flags = NEVERC_RPC_FLAG_END_STREAM |
                        NEVERC_RPC_FLAG_RESPONSE;
        break;
    case NEVERC_RPC_FRAME_END:
        allowed_flags = NEVERC_RPC_FLAG_END_STREAM |
                        NEVERC_RPC_FLAG_RESPONSE;
        if ((header->flags & NEVERC_RPC_FLAG_END_STREAM) == 0) return 0;
        break;
    case NEVERC_RPC_FRAME_CANCEL:
        allowed_flags = NEVERC_RPC_FLAG_RESPONSE;
        if (header->code == NEVERC_RPC_STATUS_OK) return 0;
        break;
    case NEVERC_RPC_FRAME_PING:
    case NEVERC_RPC_FRAME_PONG:
    case NEVERC_RPC_FRAME_GOAWAY:
        break;
    default:
        return 0;
    }
    if ((header->flags & ~allowed_flags) != 0) return 0;
    if ((header->type == NEVERC_RPC_FRAME_OPEN ||
         header->type == NEVERC_RPC_FRAME_DATA ||
         header->type == NEVERC_RPC_FRAME_END ||
         header->type == NEVERC_RPC_FRAME_CANCEL) &&
        header->request_id == 0)
        return 0;
    if ((header->type == NEVERC_RPC_FRAME_PING ||
         header->type == NEVERC_RPC_FRAME_PONG ||
         header->type == NEVERC_RPC_FRAME_GOAWAY) &&
        header->request_id != 0)
        return 0;
    if ((header->type == NEVERC_RPC_FRAME_END ||
         header->type == NEVERC_RPC_FRAME_CANCEL ||
         header->type == NEVERC_RPC_FRAME_GOAWAY) &&
        !neverc_rpc_status_code_valid(header->code))
        return 0;
    if (header->type != NEVERC_RPC_FRAME_END &&
        header->type != NEVERC_RPC_FRAME_CANCEL &&
        header->type != NEVERC_RPC_FRAME_GOAWAY && header->code != 0)
        return 0;
    if ((header->type == NEVERC_RPC_FRAME_PING ||
         header->type == NEVERC_RPC_FRAME_PONG) &&
        header->payload_length > 125)
        return 0;
    return 1;
}

int neverc_rpc_frame_encode(const neverc_rpc_frame_t *frame,
                            void *output, size_t output_capacity,
                            size_t *output_length) {
    if (output_length) *output_length = 0;
    if (!frame || !output || !output_length ||
        !rpc_frame_header_valid(&frame->header) ||
        (frame->header.payload_length > 0 && !frame->payload))
        return -1;
#if SIZE_MAX <= UINT32_MAX
    if (frame->header.payload_length >
        SIZE_MAX - NEVERC_RPC_FRAME_HEADER_SIZE)
        return -1;
#endif
    size_t total = NEVERC_RPC_FRAME_HEADER_SIZE +
                   (size_t)frame->header.payload_length;
    if (output_capacity < total) return -1;

    uint8_t *bytes = (uint8_t *)output;
    rpc_put_u32(bytes, NEVERC_RPC_MAGIC);
    bytes[4] = frame->header.version;
    bytes[5] = frame->header.type;
    rpc_put_u16(bytes + 6, frame->header.flags);
    rpc_put_u32(bytes + 8, frame->header.payload_length);
    rpc_put_u64(bytes + 12, frame->header.request_id);
    rpc_put_u32(bytes + 20, frame->header.code);
    if (frame->header.payload_length > 0)
        memcpy(bytes + NEVERC_RPC_FRAME_HEADER_SIZE, frame->payload,
               frame->header.payload_length);
    *output_length = total;
    return 0;
}

int neverc_rpc_frame_decode(const void *input, size_t input_length,
                            size_t max_payload_size,
                            neverc_rpc_frame_t *frame, size_t *consumed) {
    if (consumed) *consumed = 0;
    if (!input || !frame || !consumed || max_payload_size == 0) return -1;
    if (input_length < NEVERC_RPC_FRAME_HEADER_SIZE) return 0;
    const uint8_t *bytes = (const uint8_t *)input;
    if (rpc_get_u32(bytes) != NEVERC_RPC_MAGIC) return -1;

    neverc_rpc_frame_header_t header;
    header.version = bytes[4];
    header.type = bytes[5];
    header.flags = rpc_get_u16(bytes + 6);
    header.payload_length = rpc_get_u32(bytes + 8);
    header.request_id = rpc_get_u64(bytes + 12);
    header.code = rpc_get_u32(bytes + 20);
    if (!rpc_frame_header_valid(&header) ||
        header.payload_length > max_payload_size)
        return -1;
    size_t total = NEVERC_RPC_FRAME_HEADER_SIZE +
                   (size_t)header.payload_length;
    if (input_length < total) return 0;
    frame->header = header;
    frame->payload = bytes + NEVERC_RPC_FRAME_HEADER_SIZE;
    *consumed = total;
    return 1;
}

static int rpc_method_valid(const char *method, size_t length) {
    if (!method || length == 0 || length > UINT16_MAX ||
        method[0] == '/' || method[length - 1] == '/')
        return 0;
    int segment_has_char = 0;
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)method[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.') {
            segment_has_char = 1;
            continue;
        }
        if (c != '/' || !segment_has_char || i + 1 == length) return 0;
        segment_has_char = 0;
    }
    return segment_has_char;
}

static int rpc_metadata_key_valid(const char *key, size_t length) {
    if (!key || length == 0 || length > UINT16_MAX) return 0;
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)key[i];
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.')
            continue;
        return 0;
    }
    return 1;
}

static int rpc_codec_valid(neverc_rpc_codec_t codec) {
    return codec == NEVERC_RPC_CODEC_RAW ||
           codec == NEVERC_RPC_CODEC_JSON ||
           codec == NEVERC_RPC_CODEC_PROTOBUF;
}

int neverc_rpc_open_encode(const neverc_rpc_open_t *open,
                           void *output, size_t output_capacity,
                           size_t *output_length) {
    if (output_length) *output_length = 0;
    if (!open || !output || !output_length || open->deadline_ms < 0 ||
        !rpc_codec_valid(open->codec) ||
        !rpc_method_valid(open->method, open->method_length) ||
        open->metadata_count > NEVERC_RPC_DEFAULT_MAX_METADATA_COUNT ||
        (open->metadata_count > 0 && !open->metadata))
        return -1;
    size_t required = NEVERC_RPC_OPEN_HEADER_SIZE + open->method_length;
    for (size_t i = 0; i < open->metadata_count; i++) {
        const neverc_rpc_metadata_t *item = &open->metadata[i];
        if (!rpc_metadata_key_valid(item->key, item->key_length) ||
            item->value_length > UINT32_MAX ||
            (item->value_length > 0 && !item->value) ||
            required > SIZE_MAX - 6 - item->key_length ||
            required + 6 + item->key_length > SIZE_MAX - item->value_length)
            return -1;
        required += 6 + item->key_length + item->value_length;
        if (required - NEVERC_RPC_OPEN_HEADER_SIZE - open->method_length >
            NEVERC_RPC_DEFAULT_MAX_METADATA_SIZE)
            return -1;
    }
    if (required > output_capacity || required > UINT32_MAX) return -1;

    uint8_t *bytes = (uint8_t *)output;
    rpc_put_u64(bytes, (uint64_t)open->deadline_ms);
    rpc_put_u16(bytes + 8, (uint16_t)open->method_length);
    rpc_put_u16(bytes + 10, (uint16_t)open->metadata_count);
    bytes[12] = (uint8_t)open->codec;
    bytes[13] = 0;
    bytes[14] = 0;
    bytes[15] = 0;
    size_t offset = NEVERC_RPC_OPEN_HEADER_SIZE;
    memcpy(bytes + offset, open->method, open->method_length);
    offset += open->method_length;
    for (size_t i = 0; i < open->metadata_count; i++) {
        const neverc_rpc_metadata_t *item = &open->metadata[i];
        rpc_put_u16(bytes + offset, (uint16_t)item->key_length);
        rpc_put_u32(bytes + offset + 2, (uint32_t)item->value_length);
        offset += 6;
        memcpy(bytes + offset, item->key, item->key_length);
        offset += item->key_length;
        if (item->value_length > 0) {
            memcpy(bytes + offset, item->value, item->value_length);
            offset += item->value_length;
        }
    }
    *output_length = offset;
    return 0;
}

int neverc_rpc_open_decode(const void *input, size_t input_length,
                           size_t max_metadata_size,
                           neverc_rpc_open_t *open) {
    if (!input || !open || input_length < NEVERC_RPC_OPEN_HEADER_SIZE ||
        max_metadata_size == 0 ||
        !open->metadata ||
        open->metadata_capacity == 0)
        return -1;
    const uint8_t *bytes = (const uint8_t *)input;
    uint64_t deadline = rpc_get_u64(bytes);
    if (deadline > INT64_MAX) return -1;
    uint16_t method_length = rpc_get_u16(bytes + 8);
    uint16_t metadata_count = rpc_get_u16(bytes + 10);
    neverc_rpc_codec_t codec = (neverc_rpc_codec_t)bytes[12];
    if (metadata_count > NEVERC_RPC_DEFAULT_MAX_METADATA_COUNT ||
        metadata_count > open->metadata_capacity ||
        !rpc_codec_valid(codec) || bytes[13] != 0 || bytes[14] != 0 ||
        bytes[15] != 0 || method_length == 0 ||
        (size_t)method_length >
            input_length - NEVERC_RPC_OPEN_HEADER_SIZE)
        return -1;
    size_t offset = NEVERC_RPC_OPEN_HEADER_SIZE;
    const char *method = (const char *)(bytes + offset);
    if (!rpc_method_valid(method, method_length)) return -1;
    offset += method_length;
    size_t metadata_start = offset;
    for (size_t i = 0; i < metadata_count; i++) {
        if (input_length - offset < 6) return -1;
        uint16_t key_length = rpc_get_u16(bytes + offset);
        uint32_t value_length = rpc_get_u32(bytes + offset + 2);
        offset += 6;
        if ((size_t)key_length > input_length - offset ||
            !rpc_metadata_key_valid((const char *)(bytes + offset),
                                    key_length))
            return -1;
        open->metadata[i].key = (const char *)(bytes + offset);
        open->metadata[i].key_length = key_length;
        offset += key_length;
        if ((size_t)value_length > input_length - offset) return -1;
        open->metadata[i].value = bytes + offset;
        open->metadata[i].value_length = value_length;
        offset += value_length;
        if (offset - metadata_start > max_metadata_size) return -1;
    }
    if (offset != input_length) return -1;
    open->method = method;
    open->method_length = method_length;
    open->deadline_ms = (int64_t)deadline;
    open->codec = codec;
    open->metadata_count = metadata_count;
    return 0;
}

int neverc_rpc_status_code_valid(uint32_t code) {
    return code <= NEVERC_RPC_STATUS_UNAUTHENTICATED;
}

const char *neverc_rpc_status_name(uint32_t code) {
    static const char *names[] = {
        "OK", "CANCELLED", "UNKNOWN", "INVALID_ARGUMENT",
        "DEADLINE_EXCEEDED", "NOT_FOUND", "ALREADY_EXISTS",
        "PERMISSION_DENIED", "RESOURCE_EXHAUSTED", "FAILED_PRECONDITION",
        "ABORTED", "OUT_OF_RANGE", "UNIMPLEMENTED", "INTERNAL",
        "UNAVAILABLE", "DATA_LOSS", "UNAUTHENTICATED"
    };
    return neverc_rpc_status_code_valid(code) ? names[code] : "INVALID_STATUS";
}
