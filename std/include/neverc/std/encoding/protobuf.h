#ifndef NEVERC_ENCODING_PROTOBUF_H
#define NEVERC_ENCODING_PROTOBUF_H

/* Bounded Protocol Buffers wire codec (proto2/proto3 scalar subset). */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_PROTOBUF_MAX_FIELD_NUMBER UINT32_C(536870911)
#define NEVERC_PROTOBUF_DEFAULT_MAX_FIELD_SIZE (16U * 1024U * 1024U)

typedef enum {
    NEVERC_PROTOBUF_WIRE_VARINT = 0,
    NEVERC_PROTOBUF_WIRE_FIXED64 = 1,
    NEVERC_PROTOBUF_WIRE_LENGTH_DELIMITED = 2,
    NEVERC_PROTOBUF_WIRE_FIXED32 = 5
} neverc_protobuf_wire_type_t;

typedef struct {
    const uint8_t *data;
    size_t length;
} neverc_protobuf_bytes_t;

typedef struct {
    const uint8_t *data;
    size_t length;
    size_t offset;
    size_t max_field_size;
} neverc_protobuf_reader_t;

typedef struct {
    uint8_t *data;
    size_t capacity;
    size_t length;
} neverc_protobuf_writer_t;

typedef struct {
    uint32_t number;
    neverc_protobuf_wire_type_t wire_type;
    union {
        uint64_t varint;
        uint64_t fixed64;
        uint32_t fixed32;
        neverc_protobuf_bytes_t bytes;
    } value;
} neverc_protobuf_field_t;

void neverc_protobuf_reader_init(neverc_protobuf_reader_t *reader,
                                  const void *data, size_t length,
                                  size_t max_field_size);

/* Return 1 for a field, 0 at message end, and -1 for malformed input. */
int neverc_protobuf_reader_next(neverc_protobuf_reader_t *reader,
                                 neverc_protobuf_field_t *field);

void neverc_protobuf_writer_init(neverc_protobuf_writer_t *writer,
                                  void *data, size_t capacity);
size_t neverc_protobuf_writer_length(const neverc_protobuf_writer_t *writer);

int neverc_protobuf_write_uint64(neverc_protobuf_writer_t *writer,
                                  uint32_t field_number, uint64_t value);
int neverc_protobuf_write_uint32(neverc_protobuf_writer_t *writer,
                                  uint32_t field_number, uint32_t value);
int neverc_protobuf_write_int64(neverc_protobuf_writer_t *writer,
                                 uint32_t field_number, int64_t value);
int neverc_protobuf_write_int32(neverc_protobuf_writer_t *writer,
                                 uint32_t field_number, int32_t value);
int neverc_protobuf_write_sint64(neverc_protobuf_writer_t *writer,
                                  uint32_t field_number, int64_t value);
int neverc_protobuf_write_sint32(neverc_protobuf_writer_t *writer,
                                  uint32_t field_number, int32_t value);
int neverc_protobuf_write_bool(neverc_protobuf_writer_t *writer,
                                uint32_t field_number, int value);
int neverc_protobuf_write_fixed32(neverc_protobuf_writer_t *writer,
                                   uint32_t field_number, uint32_t value);
int neverc_protobuf_write_fixed64(neverc_protobuf_writer_t *writer,
                                   uint32_t field_number, uint64_t value);
int neverc_protobuf_write_sfixed32(neverc_protobuf_writer_t *writer,
                                    uint32_t field_number, int32_t value);
int neverc_protobuf_write_sfixed64(neverc_protobuf_writer_t *writer,
                                    uint32_t field_number, int64_t value);
int neverc_protobuf_write_enum(neverc_protobuf_writer_t *writer,
                                uint32_t field_number, int32_t value);
int neverc_protobuf_write_float(neverc_protobuf_writer_t *writer,
                                 uint32_t field_number, float value);
int neverc_protobuf_write_double(neverc_protobuf_writer_t *writer,
                                  uint32_t field_number, double value);
int neverc_protobuf_write_bytes(neverc_protobuf_writer_t *writer,
                                 uint32_t field_number,
                                 const void *data, size_t length);
int neverc_protobuf_write_string(neverc_protobuf_writer_t *writer,
                                  uint32_t field_number,
                                  const char *value, size_t length);

uint64_t neverc_protobuf_zigzag_encode64(int64_t value);
int64_t neverc_protobuf_zigzag_decode64(uint64_t value);
uint32_t neverc_protobuf_zigzag_encode32(int32_t value);
int32_t neverc_protobuf_zigzag_decode32(uint32_t value);
int neverc_protobuf_utf8_valid(const void *data, size_t length);

/* Descriptor-driven scalar messages. Generated structs use uint64_t/int64_t,
 * uint32_t, float, double, int, or neverc_protobuf_bytes_t members. */
typedef enum {
    NEVERC_PROTOBUF_TYPE_UINT32,
    NEVERC_PROTOBUF_TYPE_UINT64,
    NEVERC_PROTOBUF_TYPE_INT32,
    NEVERC_PROTOBUF_TYPE_INT64,
    NEVERC_PROTOBUF_TYPE_SINT32,
    NEVERC_PROTOBUF_TYPE_SINT64,
    NEVERC_PROTOBUF_TYPE_BOOL,
    NEVERC_PROTOBUF_TYPE_FIXED32,
    NEVERC_PROTOBUF_TYPE_FIXED64,
    NEVERC_PROTOBUF_TYPE_SFIXED32,
    NEVERC_PROTOBUF_TYPE_SFIXED64,
    NEVERC_PROTOBUF_TYPE_FLOAT,
    NEVERC_PROTOBUF_TYPE_DOUBLE,
    NEVERC_PROTOBUF_TYPE_BYTES,
    NEVERC_PROTOBUF_TYPE_STRING,
    NEVERC_PROTOBUF_TYPE_ENUM
} neverc_protobuf_scalar_type_t;

typedef struct {
    uint32_t number;
    neverc_protobuf_scalar_type_t type;
    size_t value_offset;
    size_t presence_offset; /* SIZE_MAX means infer presence from nonzero value */
} neverc_protobuf_field_descriptor_t;

typedef struct {
    size_t struct_size;
    const neverc_protobuf_field_descriptor_t *fields;
    size_t field_count;
} neverc_protobuf_message_descriptor_t;

int neverc_protobuf_message_encode(
    const neverc_protobuf_message_descriptor_t *descriptor,
    const void *message, void *output, size_t output_capacity,
    size_t *output_length);

/* Decode zero-initializes output. bytes/string members are non-owning views
 * into input. Unknown fields are skipped. */
int neverc_protobuf_message_decode(
    const neverc_protobuf_message_descriptor_t *descriptor,
    const void *input, size_t input_length, size_t max_field_size,
    void *message, size_t message_size);

#ifdef __cplusplus
}
#endif

#ifdef __neverc__
#include <neverc/std/encoding.h>
#endif

#endif /* NEVERC_ENCODING_PROTOBUF_H */
