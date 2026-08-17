#include "neverc/std/encoding/protobuf.h"

#include <limits.h>
#include <string.h>

#define PROTOBUF_WIRE_START_GROUP 3U
#define PROTOBUF_WIRE_END_GROUP 4U
#define PROTOBUF_MAX_GROUP_DEPTH 100

static int protobuf_field_number_valid(uint32_t number) {
    return number > 0 && number <= NEVERC_PROTOBUF_MAX_FIELD_NUMBER;
}

static int protobuf_read_varint(const uint8_t *data, size_t length,
                                size_t *consumed, uint64_t *value) {
    uint64_t result = 0;
    for (size_t i = 0; i < 10; i++) {
        if (i >= length) return -1;
        uint8_t byte = data[i];
        if (i == 9 && byte > 1) return -1;
        result |= (uint64_t)(byte & 0x7fU) << (unsigned)(i * 7);
        if ((byte & 0x80U) == 0) {
            *consumed = i + 1;
            *value = result;
            return 0;
        }
    }
    return -1;
}

static int protobuf_write_raw(neverc_protobuf_writer_t *writer,
                              const void *data, size_t length) {
    if (!writer || writer->length > writer->capacity ||
        (length > 0 && !data) ||
        length > writer->capacity - writer->length)
        return -1;
    if (length > 0)
        memcpy(writer->data + writer->length, data, length);
    writer->length += length;
    return 0;
}

static int protobuf_write_varint(neverc_protobuf_writer_t *writer,
                                 uint64_t value) {
    uint8_t encoded[10];
    size_t length = 0;
    do {
        uint8_t byte = (uint8_t)(value & 0x7fU);
        value >>= 7;
        encoded[length++] = value ? (uint8_t)(byte | 0x80U) : byte;
    } while (value);
    return protobuf_write_raw(writer, encoded, length);
}

static int protobuf_write_tag(neverc_protobuf_writer_t *writer,
                              uint32_t number,
                              neverc_protobuf_wire_type_t wire_type) {
    if (!protobuf_field_number_valid(number)) return -1;
    return protobuf_write_varint(writer,
                                 ((uint64_t)number << 3) | wire_type);
}

int neverc_protobuf_utf8_valid(const void *input, size_t length) {
    if (!input && length > 0) return 0;
    const uint8_t *data = (const uint8_t *)input;
    size_t i = 0;
    while (i < length) {
        uint8_t first = data[i++];
        if (first <= 0x7f) continue;
        unsigned continuation;
        uint32_t codepoint;
        if (first >= 0xc2 && first <= 0xdf) {
            continuation = 1;
            codepoint = first & 0x1fU;
        } else if (first >= 0xe0 && first <= 0xef) {
            continuation = 2;
            codepoint = first & 0x0fU;
        } else if (first >= 0xf0 && first <= 0xf4) {
            continuation = 3;
            codepoint = first & 0x07U;
        } else {
            return 0;
        }
        if (continuation > length - i) return 0;
        for (unsigned j = 0; j < continuation; j++) {
            uint8_t byte = data[i++];
            if ((byte & 0xc0U) != 0x80U) return 0;
            codepoint = (codepoint << 6) | (byte & 0x3fU);
        }
        if ((continuation == 2 && codepoint < 0x800) ||
            (continuation == 3 && codepoint < 0x10000) ||
            (codepoint >= 0xd800 && codepoint <= 0xdfff) ||
            codepoint > 0x10ffff)
            return 0;
    }
    return 1;
}

void neverc_protobuf_reader_init(neverc_protobuf_reader_t *reader,
                                  const void *data, size_t length,
                                  size_t max_field_size) {
    if (!reader) return;
    reader->data = (const uint8_t *)data;
    reader->length = data ? length : 0;
    reader->offset = 0;
    reader->max_field_size = max_field_size
        ? max_field_size : NEVERC_PROTOBUF_DEFAULT_MAX_FIELD_SIZE;
}

static int protobuf_skip_value(neverc_protobuf_reader_t *reader, uint8_t wire) {
    if (!reader || reader->offset > reader->length) return -1;
    size_t remaining = reader->length - reader->offset;
    size_t consumed = 0;
    uint64_t value = 0;
    switch (wire) {
    case NEVERC_PROTOBUF_WIRE_VARINT:
        if (protobuf_read_varint(reader->data + reader->offset, remaining,
                                 &consumed, &value) != 0)
            return -1;
        reader->offset += consumed;
        return 0;
    case NEVERC_PROTOBUF_WIRE_FIXED64:
        if (remaining < 8) return -1;
        reader->offset += 8;
        return 0;
    case NEVERC_PROTOBUF_WIRE_LENGTH_DELIMITED:
        if (protobuf_read_varint(reader->data + reader->offset, remaining,
                                 &consumed, &value) != 0)
            return -1;
        reader->offset += consumed;
        remaining = reader->length - reader->offset;
        if (value > remaining || value > reader->max_field_size ||
            value > (uint64_t)SIZE_MAX)
            return -1;
        reader->offset += (size_t)value;
        return 0;
    case NEVERC_PROTOBUF_WIRE_FIXED32:
        if (remaining < 4) return -1;
        reader->offset += 4;
        return 0;
    default:
        return -1;
    }
}

/* Iterative group skip: nested start-groups used to recurse and could blow
 * the C stack before PROTOBUF_MAX_GROUP_DEPTH was reached on small threads. */
static int protobuf_skip_group(neverc_protobuf_reader_t *reader,
                               uint32_t number) {
    uint32_t nest[PROTOBUF_MAX_GROUP_DEPTH];
    int top = 0;
    if (!reader) return -1;
    nest[top++] = number;
    while (top > 0) {
        size_t consumed = 0;
        uint64_t tag = 0;
        uint64_t nested;
        uint8_t wire;
        if (reader->offset >= reader->length) return -1;
        if (protobuf_read_varint(reader->data + reader->offset,
                                 reader->length - reader->offset,
                                 &consumed, &tag) != 0)
            return -1;
        reader->offset += consumed;
        nested = tag >> 3;
        wire = (uint8_t)(tag & 7U);
        if (nested == 0 || nested > NEVERC_PROTOBUF_MAX_FIELD_NUMBER)
            return -1;
        if (wire == PROTOBUF_WIRE_END_GROUP) {
            if (nested != nest[top - 1]) return -1;
            top--;
            continue;
        }
        if (wire == PROTOBUF_WIRE_START_GROUP) {
            if (top >= PROTOBUF_MAX_GROUP_DEPTH) return -1;
            nest[top++] = (uint32_t)nested;
            continue;
        }
        if (protobuf_skip_value(reader, wire) != 0)
            return -1;
    }
    return 0;
}

int neverc_protobuf_reader_next(neverc_protobuf_reader_t *reader,
                                 neverc_protobuf_field_t *field) {
    if (!reader || !field || (!reader->data && reader->length > 0) ||
        reader->offset > reader->length)
        return -1;
    if (reader->offset == reader->length) return 0;
    size_t start = reader->offset;
    for (;;) {
        if (reader->offset == reader->length) return 0;
        size_t consumed = 0;
        uint64_t tag = 0;
        if (protobuf_read_varint(reader->data + reader->offset,
                                 reader->length - reader->offset,
                                 &consumed, &tag) != 0)
            goto fail;
        reader->offset += consumed;
        uint64_t number = tag >> 3;
        uint8_t wire = (uint8_t)(tag & 7U);
        if (number == 0 || number > NEVERC_PROTOBUF_MAX_FIELD_NUMBER)
            goto fail;
        if (wire == PROTOBUF_WIRE_START_GROUP) {
            if (protobuf_skip_group(reader, (uint32_t)number) != 0)
                goto fail;
            continue;
        }
        if (wire == PROTOBUF_WIRE_END_GROUP ||
            (wire != NEVERC_PROTOBUF_WIRE_VARINT &&
             wire != NEVERC_PROTOBUF_WIRE_FIXED64 &&
             wire != NEVERC_PROTOBUF_WIRE_LENGTH_DELIMITED &&
             wire != NEVERC_PROTOBUF_WIRE_FIXED32))
            goto fail;
        memset(field, 0, sizeof(*field));
        field->number = (uint32_t)number;
        field->wire_type = (neverc_protobuf_wire_type_t)wire;
        size_t remaining = reader->length - reader->offset;
        switch (wire) {
        case NEVERC_PROTOBUF_WIRE_VARINT:
            if (protobuf_read_varint(reader->data + reader->offset, remaining,
                                     &consumed, &field->value.varint) != 0)
                goto fail;
            reader->offset += consumed;
            return 1;
        case NEVERC_PROTOBUF_WIRE_FIXED64:
            if (remaining < 8) goto fail;
            for (unsigned i = 0; i < 8; i++)
                field->value.fixed64 |=
                    (uint64_t)reader->data[reader->offset + i] << (i * 8);
            reader->offset += 8;
            return 1;
        case NEVERC_PROTOBUF_WIRE_LENGTH_DELIMITED: {
            uint64_t encoded_length = 0;
            if (protobuf_read_varint(reader->data + reader->offset, remaining,
                                     &consumed, &encoded_length) != 0)
                goto fail;
            reader->offset += consumed;
            remaining = reader->length - reader->offset;
            if (encoded_length > SIZE_MAX || encoded_length > remaining ||
                encoded_length > reader->max_field_size)
                goto fail;
            field->value.bytes.data = reader->data + reader->offset;
            field->value.bytes.length = (size_t)encoded_length;
            reader->offset += (size_t)encoded_length;
            return 1;
        }
        case NEVERC_PROTOBUF_WIRE_FIXED32:
            if (remaining < 4) goto fail;
            for (unsigned i = 0; i < 4; i++)
                field->value.fixed32 |=
                    (uint32_t)reader->data[reader->offset + i] << (i * 8);
            reader->offset += 4;
            return 1;
        default:
            goto fail;
        }
    }
fail:
    reader->offset = start;
    return -1;
}

void neverc_protobuf_writer_init(neverc_protobuf_writer_t *writer,
                                  void *data, size_t capacity) {
    if (!writer) return;
    writer->data = (uint8_t *)data;
    writer->capacity = data ? capacity : 0;
    writer->length = 0;
}

size_t neverc_protobuf_writer_length(const neverc_protobuf_writer_t *writer) {
    return writer ? writer->length : 0;
}

int neverc_protobuf_write_uint64(neverc_protobuf_writer_t *writer,
                                  uint32_t field_number, uint64_t value) {
    if (!writer) return -1;
    size_t start = writer->length;
    if (protobuf_write_tag(writer, field_number,
                           NEVERC_PROTOBUF_WIRE_VARINT) == 0 &&
        protobuf_write_varint(writer, value) == 0)
        return 0;
    writer->length = start;
    return -1;
}

int neverc_protobuf_write_int64(neverc_protobuf_writer_t *writer,
                                 uint32_t field_number, int64_t value) {
    return neverc_protobuf_write_uint64(writer, field_number,
                                        (uint64_t)value);
}

int neverc_protobuf_write_uint32(neverc_protobuf_writer_t *writer,
                                  uint32_t field_number, uint32_t value) {
    return neverc_protobuf_write_uint64(writer, field_number, value);
}

int neverc_protobuf_write_int32(neverc_protobuf_writer_t *writer,
                                 uint32_t field_number, int32_t value) {
    return neverc_protobuf_write_uint64(writer, field_number,
                                        (uint64_t)(int64_t)value);
}

uint64_t neverc_protobuf_zigzag_encode64(int64_t value) {
    return ((uint64_t)value << 1) ^ (uint64_t)-(value < 0);
}

int64_t neverc_protobuf_zigzag_decode64(uint64_t value) {
    return (int64_t)((value >> 1) ^ (uint64_t)-(int64_t)(value & 1));
}

uint32_t neverc_protobuf_zigzag_encode32(int32_t value) {
    return ((uint32_t)value << 1) ^ (uint32_t)-(value < 0);
}

int32_t neverc_protobuf_zigzag_decode32(uint32_t value) {
    return (int32_t)((value >> 1) ^ (uint32_t)-(int32_t)(value & 1));
}

int neverc_protobuf_write_sint64(neverc_protobuf_writer_t *writer,
                                  uint32_t field_number, int64_t value) {
    return neverc_protobuf_write_uint64(
        writer, field_number, neverc_protobuf_zigzag_encode64(value));
}

int neverc_protobuf_write_sint32(neverc_protobuf_writer_t *writer,
                                  uint32_t field_number, int32_t value) {
    return neverc_protobuf_write_uint64(
        writer, field_number, neverc_protobuf_zigzag_encode32(value));
}

int neverc_protobuf_write_bool(neverc_protobuf_writer_t *writer,
                                uint32_t field_number, int value) {
    if (value != 0 && value != 1) return -1;
    return neverc_protobuf_write_uint64(writer, field_number,
                                        (uint64_t)value);
}

int neverc_protobuf_write_fixed32(neverc_protobuf_writer_t *writer,
                                   uint32_t field_number, uint32_t value) {
    uint8_t bytes[4];
    for (unsigned i = 0; i < 4; i++) bytes[i] = (uint8_t)(value >> (i * 8));
    if (!writer) return -1;
    size_t start = writer->length;
    if (protobuf_write_tag(writer, field_number,
                           NEVERC_PROTOBUF_WIRE_FIXED32) == 0 &&
        protobuf_write_raw(writer, bytes, sizeof(bytes)) == 0)
        return 0;
    writer->length = start;
    return -1;
}

int neverc_protobuf_write_fixed64(neverc_protobuf_writer_t *writer,
                                   uint32_t field_number, uint64_t value) {
    uint8_t bytes[8];
    for (unsigned i = 0; i < 8; i++) bytes[i] = (uint8_t)(value >> (i * 8));
    if (!writer) return -1;
    size_t start = writer->length;
    if (protobuf_write_tag(writer, field_number,
                           NEVERC_PROTOBUF_WIRE_FIXED64) == 0 &&
        protobuf_write_raw(writer, bytes, sizeof(bytes)) == 0)
        return 0;
    writer->length = start;
    return -1;
}

int neverc_protobuf_write_sfixed32(neverc_protobuf_writer_t *writer,
                                    uint32_t field_number, int32_t value) {
    return neverc_protobuf_write_fixed32(writer, field_number,
                                          (uint32_t)value);
}

int neverc_protobuf_write_sfixed64(neverc_protobuf_writer_t *writer,
                                    uint32_t field_number, int64_t value) {
    return neverc_protobuf_write_fixed64(writer, field_number,
                                          (uint64_t)value);
}

int neverc_protobuf_write_enum(neverc_protobuf_writer_t *writer,
                                uint32_t field_number, int32_t value) {
    return neverc_protobuf_write_int32(writer, field_number, value);
}

int neverc_protobuf_write_float(neverc_protobuf_writer_t *writer,
                                 uint32_t field_number, float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return neverc_protobuf_write_fixed32(writer, field_number, bits);
}

int neverc_protobuf_write_double(neverc_protobuf_writer_t *writer,
                                  uint32_t field_number, double value) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return neverc_protobuf_write_fixed64(writer, field_number, bits);
}

int neverc_protobuf_write_bytes(neverc_protobuf_writer_t *writer,
                                 uint32_t field_number,
                                 const void *data, size_t length) {
    if (!writer || (length > 0 && !data)) return -1;
    size_t start = writer->length;
    if (protobuf_write_tag(writer, field_number,
                           NEVERC_PROTOBUF_WIRE_LENGTH_DELIMITED) != 0 ||
        protobuf_write_varint(writer, (uint64_t)length) != 0 ||
        protobuf_write_raw(writer, data, length) != 0) {
        writer->length = start;
        return -1;
    }
    return 0;
}

int neverc_protobuf_write_string(neverc_protobuf_writer_t *writer,
                                  uint32_t field_number,
                                  const char *value, size_t length) {
    if ((length > 0 && !value) ||
        !neverc_protobuf_utf8_valid(value, length))
        return -1;
    return neverc_protobuf_write_bytes(writer, field_number, value, length);
}
