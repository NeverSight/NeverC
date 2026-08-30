#include "neverc/std/encoding/protobuf.h"

#include <string.h>

static size_t protobuf_scalar_size(neverc_protobuf_scalar_type_t type) {
    switch (type) {
    case NEVERC_PROTOBUF_TYPE_UINT32:
    case NEVERC_PROTOBUF_TYPE_INT32:
    case NEVERC_PROTOBUF_TYPE_SINT32:
    case NEVERC_PROTOBUF_TYPE_SFIXED32:
    case NEVERC_PROTOBUF_TYPE_ENUM:
        return 4;
    case NEVERC_PROTOBUF_TYPE_UINT64:
    case NEVERC_PROTOBUF_TYPE_INT64:
    case NEVERC_PROTOBUF_TYPE_SINT64:
    case NEVERC_PROTOBUF_TYPE_FIXED64:
    case NEVERC_PROTOBUF_TYPE_DOUBLE:
    case NEVERC_PROTOBUF_TYPE_SFIXED64:
        return 8;
    case NEVERC_PROTOBUF_TYPE_BOOL:
        return sizeof(int);
    case NEVERC_PROTOBUF_TYPE_FIXED32:
    case NEVERC_PROTOBUF_TYPE_FLOAT:
        return 4;
    case NEVERC_PROTOBUF_TYPE_BYTES:
    case NEVERC_PROTOBUF_TYPE_STRING:
        return sizeof(neverc_protobuf_bytes_t);
    default:
        return 0;
    }
}

static neverc_protobuf_wire_type_t protobuf_scalar_wire(
    neverc_protobuf_scalar_type_t type) {
    switch (type) {
    case NEVERC_PROTOBUF_TYPE_UINT32:
    case NEVERC_PROTOBUF_TYPE_UINT64:
    case NEVERC_PROTOBUF_TYPE_INT32:
    case NEVERC_PROTOBUF_TYPE_INT64:
    case NEVERC_PROTOBUF_TYPE_SINT32:
    case NEVERC_PROTOBUF_TYPE_SINT64:
    case NEVERC_PROTOBUF_TYPE_BOOL:
    case NEVERC_PROTOBUF_TYPE_ENUM:
        return NEVERC_PROTOBUF_WIRE_VARINT;
    case NEVERC_PROTOBUF_TYPE_FIXED64:
    case NEVERC_PROTOBUF_TYPE_SFIXED64:
    case NEVERC_PROTOBUF_TYPE_DOUBLE:
        return NEVERC_PROTOBUF_WIRE_FIXED64;
    case NEVERC_PROTOBUF_TYPE_BYTES:
    case NEVERC_PROTOBUF_TYPE_STRING:
        return NEVERC_PROTOBUF_WIRE_LENGTH_DELIMITED;
    case NEVERC_PROTOBUF_TYPE_FIXED32:
    case NEVERC_PROTOBUF_TYPE_SFIXED32:
    case NEVERC_PROTOBUF_TYPE_FLOAT:
        return NEVERC_PROTOBUF_WIRE_FIXED32;
    default:
        return (neverc_protobuf_wire_type_t)-1;
    }
}

static int protobuf_ranges_overlap(size_t a_offset, size_t a_size,
                                   size_t b_offset, size_t b_size) {
    return a_offset < b_offset + b_size && b_offset < a_offset + a_size;
}

static int protobuf_descriptor_valid(
    const neverc_protobuf_message_descriptor_t *descriptor) {
    if (!descriptor || descriptor->struct_size == 0 ||
        (descriptor->field_count > 0 && !descriptor->fields))
        return 0;
    for (size_t i = 0; i < descriptor->field_count; i++) {
        const neverc_protobuf_field_descriptor_t *field =
            &descriptor->fields[i];
        size_t value_size = protobuf_scalar_size(field->type);
        if (field->number == 0 ||
            field->number > NEVERC_PROTOBUF_MAX_FIELD_NUMBER ||
            value_size == 0 || field->value_offset > descriptor->struct_size ||
            value_size > descriptor->struct_size - field->value_offset ||
            (field->presence_offset != SIZE_MAX &&
             (field->presence_offset > descriptor->struct_size ||
              sizeof(int) > descriptor->struct_size -
                                field->presence_offset)))
            return 0;
        if (field->presence_offset != SIZE_MAX &&
            protobuf_ranges_overlap(field->value_offset, value_size,
                                    field->presence_offset, sizeof(int)))
            return 0;
        for (size_t j = 0; j < i; j++) {
            const neverc_protobuf_field_descriptor_t *previous =
                &descriptor->fields[j];
            size_t previous_size = protobuf_scalar_size(previous->type);
            if (previous->number == field->number ||
                protobuf_ranges_overlap(field->value_offset, value_size,
                                        previous->value_offset,
                                        previous_size) ||
                (previous->presence_offset != SIZE_MAX &&
                 protobuf_ranges_overlap(field->value_offset, value_size,
                                         previous->presence_offset,
                                         sizeof(int))) ||
                (field->presence_offset != SIZE_MAX &&
                 (protobuf_ranges_overlap(field->presence_offset,
                                          sizeof(int),
                                          previous->value_offset,
                                          previous_size) ||
                  (previous->presence_offset != SIZE_MAX &&
                   protobuf_ranges_overlap(field->presence_offset,
                                           sizeof(int),
                                           previous->presence_offset,
                                           sizeof(int))))))
                return 0;
        }
    }
    return 1;
}

static int protobuf_value_present(
    const neverc_protobuf_field_descriptor_t *field, const uint8_t *message) {
    if (field->presence_offset != SIZE_MAX) {
        int present;
        memcpy(&present, message + field->presence_offset, sizeof(present));
        return present != 0;
    }
    const void *value = message + field->value_offset;
    switch (field->type) {
    case NEVERC_PROTOBUF_TYPE_UINT32:
    case NEVERC_PROTOBUF_TYPE_FIXED32:
    case NEVERC_PROTOBUF_TYPE_SFIXED32: {
        uint32_t scalar;
        memcpy(&scalar, value, sizeof(scalar));
        return scalar != 0;
    }
    case NEVERC_PROTOBUF_TYPE_UINT64:
    case NEVERC_PROTOBUF_TYPE_FIXED64: {
        uint64_t scalar;
        memcpy(&scalar, value, sizeof(scalar));
        return scalar != 0;
    }
    case NEVERC_PROTOBUF_TYPE_INT64:
    case NEVERC_PROTOBUF_TYPE_SINT64: {
        int64_t scalar;
        memcpy(&scalar, value, sizeof(scalar));
        return scalar != 0;
    }
    case NEVERC_PROTOBUF_TYPE_INT32:
    case NEVERC_PROTOBUF_TYPE_SINT32:
    case NEVERC_PROTOBUF_TYPE_ENUM: {
        int32_t scalar;
        memcpy(&scalar, value, sizeof(scalar));
        return scalar != 0;
    }
    case NEVERC_PROTOBUF_TYPE_BOOL: {
        int scalar;
        memcpy(&scalar, value, sizeof(scalar));
        return scalar != 0;
    }
    /* IEEE-754 makes -0.0 compare equal to 0.0, so a value comparison would
     * drop negative zero as if the field were unset. protobuf-go compensates
     * with math.Signbit and C++ compares the raw bits; do the latter. */
    case NEVERC_PROTOBUF_TYPE_FLOAT: {
        uint32_t bits;
        memcpy(&bits, value, sizeof(bits));
        return bits != 0;
    }
    case NEVERC_PROTOBUF_TYPE_DOUBLE: {
        uint64_t bits;
        memcpy(&bits, value, sizeof(bits));
        return bits != 0;
    }
    case NEVERC_PROTOBUF_TYPE_SFIXED64: {
        int64_t scalar;
        memcpy(&scalar, value, sizeof(scalar));
        return scalar != 0;
    }
    case NEVERC_PROTOBUF_TYPE_BYTES:
    case NEVERC_PROTOBUF_TYPE_STRING: {
        neverc_protobuf_bytes_t bytes;
        memcpy(&bytes, value, sizeof(bytes));
        return bytes.length != 0;
    }
    default:
        return 0;
    }
}

static int protobuf_encode_field(
    neverc_protobuf_writer_t *writer,
    const neverc_protobuf_field_descriptor_t *field,
    const uint8_t *message) {
    const void *value = message + field->value_offset;
    switch (field->type) {
    case NEVERC_PROTOBUF_TYPE_UINT32: {
        uint32_t scalar;
        memcpy(&scalar, value, sizeof(scalar));
        return neverc_protobuf_write_uint32(writer, field->number, scalar);
    }
    case NEVERC_PROTOBUF_TYPE_UINT64: {
        uint64_t scalar;
        memcpy(&scalar, value, sizeof(scalar));
        return neverc_protobuf_write_uint64(writer, field->number,
                                             scalar);
    }
    case NEVERC_PROTOBUF_TYPE_INT64: {
        int64_t scalar;
        memcpy(&scalar, value, sizeof(scalar));
        return neverc_protobuf_write_int64(writer, field->number,
                                            scalar);
    }
    case NEVERC_PROTOBUF_TYPE_INT32: {
        int32_t scalar;
        memcpy(&scalar, value, sizeof(scalar));
        return neverc_protobuf_write_int32(writer, field->number, scalar);
    }
    case NEVERC_PROTOBUF_TYPE_SINT32: {
        int32_t scalar;
        memcpy(&scalar, value, sizeof(scalar));
        return neverc_protobuf_write_sint32(writer, field->number, scalar);
    }
    case NEVERC_PROTOBUF_TYPE_SINT64: {
        int64_t scalar;
        memcpy(&scalar, value, sizeof(scalar));
        return neverc_protobuf_write_sint64(writer, field->number,
                                             scalar);
    }
    case NEVERC_PROTOBUF_TYPE_BOOL: {
        int scalar;
        memcpy(&scalar, value, sizeof(scalar));
        /* Wire format: any nonzero varint is true, and the decoder already
         * normalises that way. The low-level writer keeps its strict 0/1
         * contract for direct callers. */
        return neverc_protobuf_write_bool(writer, field->number,
                                           scalar != 0);
    }
    case NEVERC_PROTOBUF_TYPE_FIXED32: {
        uint32_t scalar;
        memcpy(&scalar, value, sizeof(scalar));
        return neverc_protobuf_write_fixed32(writer, field->number,
                                              scalar);
    }
    case NEVERC_PROTOBUF_TYPE_FIXED64: {
        uint64_t scalar;
        memcpy(&scalar, value, sizeof(scalar));
        return neverc_protobuf_write_fixed64(writer, field->number,
                                              scalar);
    }
    case NEVERC_PROTOBUF_TYPE_SFIXED32: {
        int32_t scalar;
        memcpy(&scalar, value, sizeof(scalar));
        return neverc_protobuf_write_sfixed32(writer, field->number, scalar);
    }
    case NEVERC_PROTOBUF_TYPE_SFIXED64: {
        int64_t scalar;
        memcpy(&scalar, value, sizeof(scalar));
        return neverc_protobuf_write_sfixed64(writer, field->number, scalar);
    }
    case NEVERC_PROTOBUF_TYPE_FLOAT: {
        float scalar;
        memcpy(&scalar, value, sizeof(scalar));
        return neverc_protobuf_write_float(writer, field->number,
                                            scalar);
    }
    case NEVERC_PROTOBUF_TYPE_DOUBLE: {
        double scalar;
        memcpy(&scalar, value, sizeof(scalar));
        return neverc_protobuf_write_double(writer, field->number,
                                             scalar);
    }
    case NEVERC_PROTOBUF_TYPE_BYTES: {
        neverc_protobuf_bytes_t bytes;
        memcpy(&bytes, value, sizeof(bytes));
        return neverc_protobuf_write_bytes(writer, field->number,
                                            bytes.data, bytes.length);
    }
    case NEVERC_PROTOBUF_TYPE_STRING: {
        neverc_protobuf_bytes_t string;
        memcpy(&string, value, sizeof(string));
        return neverc_protobuf_write_string(
            writer, field->number, (const char *)string.data,
            string.length);
    }
    case NEVERC_PROTOBUF_TYPE_ENUM: {
        int32_t scalar;
        memcpy(&scalar, value, sizeof(scalar));
        return neverc_protobuf_write_enum(writer, field->number, scalar);
    }
    default:
        return -1;
    }
}

int neverc_protobuf_message_encode(
    const neverc_protobuf_message_descriptor_t *descriptor,
    const void *message, void *output, size_t output_capacity,
    size_t *output_length) {
    if (output_length) *output_length = 0;
    if (!protobuf_descriptor_valid(descriptor) || !message || !output ||
        !output_length)
        return -1;
    neverc_protobuf_writer_t writer;
    neverc_protobuf_writer_init(&writer, output, output_capacity);
    for (size_t i = 0; i < descriptor->field_count; i++) {
        const neverc_protobuf_field_descriptor_t *field =
            &descriptor->fields[i];
        if (protobuf_value_present(field, (const uint8_t *)message) &&
            protobuf_encode_field(&writer, field,
                                  (const uint8_t *)message) != 0)
            return -1;
    }
    *output_length = writer.length;
    return 0;
}

static const neverc_protobuf_field_descriptor_t *protobuf_find_field(
    const neverc_protobuf_message_descriptor_t *descriptor,
    uint32_t number) {
    for (size_t i = 0; i < descriptor->field_count; i++)
        if (descriptor->fields[i].number == number)
            return &descriptor->fields[i];
    return NULL;
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

static int protobuf_type_packable(neverc_protobuf_scalar_type_t type) {
    return type != NEVERC_PROTOBUF_TYPE_BYTES &&
           type != NEVERC_PROTOBUF_TYPE_STRING;
}

static int protobuf_decode_field(
    const neverc_protobuf_field_descriptor_t *descriptor,
    const neverc_protobuf_field_t *field, uint8_t *message) {
    if (field->wire_type != protobuf_scalar_wire(descriptor->type)) return -1;
    void *value = message + descriptor->value_offset;
    switch (descriptor->type) {
    case NEVERC_PROTOBUF_TYPE_UINT32: {
        /* proto2/proto3 truncate oversized varints to 32 bits. */
        uint32_t uint32_value = (uint32_t)field->value.varint;
        memcpy(value, &uint32_value, sizeof(uint32_value));
        break;
    }
    case NEVERC_PROTOBUF_TYPE_UINT64:
        memcpy(value, &field->value.varint, sizeof(uint64_t));
        break;
    case NEVERC_PROTOBUF_TYPE_INT64: {
        int64_t scalar = (int64_t)field->value.varint;
        memcpy(value, &scalar, sizeof(scalar));
        break;
    }
    case NEVERC_PROTOBUF_TYPE_INT32:
    case NEVERC_PROTOBUF_TYPE_ENUM: {
        int32_t scalar = (int32_t)(uint32_t)field->value.varint;
        memcpy(value, &scalar, sizeof(scalar));
        break;
    }
    case NEVERC_PROTOBUF_TYPE_SINT32: {
        int32_t scalar = neverc_protobuf_zigzag_decode32(
            (uint32_t)field->value.varint);
        memcpy(value, &scalar, sizeof(scalar));
        break;
    }
    case NEVERC_PROTOBUF_TYPE_SINT64: {
        int64_t scalar = neverc_protobuf_zigzag_decode64(field->value.varint);
        memcpy(value, &scalar, sizeof(scalar));
        break;
    }
    case NEVERC_PROTOBUF_TYPE_BOOL: {
        /* Wire format: 0 is false; any other varint is true. */
        int boolean = field->value.varint != 0;
        memcpy(value, &boolean, sizeof(boolean));
        break;
    }
    case NEVERC_PROTOBUF_TYPE_FIXED32:
        memcpy(value, &field->value.fixed32, sizeof(uint32_t));
        break;
    case NEVERC_PROTOBUF_TYPE_FIXED64:
        memcpy(value, &field->value.fixed64, sizeof(uint64_t));
        break;
    case NEVERC_PROTOBUF_TYPE_SFIXED32: {
        int32_t scalar = (int32_t)field->value.fixed32;
        memcpy(value, &scalar, sizeof(scalar));
        break;
    }
    case NEVERC_PROTOBUF_TYPE_SFIXED64: {
        int64_t scalar = (int64_t)field->value.fixed64;
        memcpy(value, &scalar, sizeof(scalar));
        break;
    }
    case NEVERC_PROTOBUF_TYPE_FLOAT:
        memcpy(value, &field->value.fixed32, sizeof(float));
        break;
    case NEVERC_PROTOBUF_TYPE_DOUBLE:
        memcpy(value, &field->value.fixed64, sizeof(double));
        break;
    case NEVERC_PROTOBUF_TYPE_BYTES:
        memcpy(value, &field->value.bytes, sizeof(field->value.bytes));
        break;
    case NEVERC_PROTOBUF_TYPE_STRING:
        if (!neverc_protobuf_utf8_valid(field->value.bytes.data,
                                        field->value.bytes.length))
            return -1;
        memcpy(value, &field->value.bytes, sizeof(field->value.bytes));
        break;
    default:
        return -1;
    }
    if (descriptor->presence_offset != SIZE_MAX) {
        int present = 1;
        memcpy(message + descriptor->presence_offset, &present,
               sizeof(present));
    }
    return 0;
}

static int protobuf_decode_packed(
    const neverc_protobuf_field_descriptor_t *descriptor,
    const neverc_protobuf_bytes_t *bytes, uint8_t *message) {
    if (!bytes || (bytes->length > 0 && !bytes->data)) return -1;
    neverc_protobuf_wire_type_t elem_wire =
        protobuf_scalar_wire(descriptor->type);
    neverc_protobuf_field_t field;
    memset(&field, 0, sizeof(field));
    field.number = descriptor->number;
    field.wire_type = elem_wire;
    size_t offset = 0;
    if (elem_wire == NEVERC_PROTOBUF_WIRE_VARINT) {
        while (offset < bytes->length) {
            size_t consumed = 0;
            if (protobuf_read_varint(bytes->data + offset,
                                     bytes->length - offset, &consumed,
                                     &field.value.varint) != 0)
                return -1;
            offset += consumed;
            if (protobuf_decode_field(descriptor, &field, message) != 0)
                return -1;
        }
        return 0;
    }
    if (elem_wire == NEVERC_PROTOBUF_WIRE_FIXED32) {
        if ((bytes->length & 3U) != 0) return -1;
        while (offset < bytes->length) {
            field.value.fixed32 = 0;
            for (unsigned i = 0; i < 4; i++)
                field.value.fixed32 |=
                    (uint32_t)bytes->data[offset + i] << (i * 8);
            offset += 4;
            if (protobuf_decode_field(descriptor, &field, message) != 0)
                return -1;
        }
        return 0;
    }
    if (elem_wire == NEVERC_PROTOBUF_WIRE_FIXED64) {
        if ((bytes->length & 7U) != 0) return -1;
        while (offset < bytes->length) {
            field.value.fixed64 = 0;
            for (unsigned i = 0; i < 8; i++)
                field.value.fixed64 |=
                    (uint64_t)bytes->data[offset + i] << (i * 8);
            offset += 8;
            if (protobuf_decode_field(descriptor, &field, message) != 0)
                return -1;
        }
        return 0;
    }
    return -1;
}

int neverc_protobuf_message_decode(
    const neverc_protobuf_message_descriptor_t *descriptor,
    const void *input, size_t input_length, size_t max_field_size,
    void *message, size_t message_size) {
    if (!protobuf_descriptor_valid(descriptor) || !message ||
        message_size < descriptor->struct_size)
        return -1;
    memset(message, 0, descriptor->struct_size);
    if (!input && input_length > 0) return -1;
    neverc_protobuf_reader_t reader;
    neverc_protobuf_reader_init(&reader, input, input_length, max_field_size);
    for (;;) {
        neverc_protobuf_field_t field;
        int result = neverc_protobuf_reader_next(&reader, &field);
        if (result == 0) return 0;
        if (result < 0) goto fail;
        const neverc_protobuf_field_descriptor_t *known =
            protobuf_find_field(descriptor, field.number);
        if (!known) continue;
        neverc_protobuf_wire_type_t expected =
            protobuf_scalar_wire(known->type);
        if (field.wire_type == expected) {
            if (protobuf_decode_field(known, &field, (uint8_t *)message) != 0)
                goto fail;
        } else if (field.wire_type == NEVERC_PROTOBUF_WIRE_LENGTH_DELIMITED &&
                   protobuf_type_packable(known->type)) {
            if (protobuf_decode_packed(known, &field.value.bytes,
                                       (uint8_t *)message) != 0)
                goto fail;
        }
    }
fail:
    /* A later malformed field (or truncated packed payload) must not leave
     * earlier fields in the caller's struct. */
    memset(message, 0, descriptor->struct_size);
    return -1;
}
