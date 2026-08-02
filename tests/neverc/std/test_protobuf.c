#include "neverc/std/encoding/protobuf.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int tests_run;
static int tests_failed;

#define CHECK(condition)                                                     \
    do {                                                                     \
        tests_run++;                                                         \
        if (!(condition)) {                                                  \
            tests_failed++;                                                  \
            printf("  FAIL %s:%d: %s\n", __func__, __LINE__, #condition);  \
        }                                                                    \
    } while (0)

typedef struct {
    uint64_t sequence;
    int has_delta;
    int32_t delta;
    neverc_protobuf_bytes_t player;
    int active;
} protobuf_test_message_t;

static const neverc_protobuf_field_descriptor_t protobuf_test_fields[] = {
    {1U, NEVERC_PROTOBUF_TYPE_UINT64,
     offsetof(protobuf_test_message_t, sequence), SIZE_MAX},
    {2U, NEVERC_PROTOBUF_TYPE_SINT32,
     offsetof(protobuf_test_message_t, delta),
     offsetof(protobuf_test_message_t, has_delta)},
    {3U, NEVERC_PROTOBUF_TYPE_STRING,
     offsetof(protobuf_test_message_t, player), SIZE_MAX},
    {4U, NEVERC_PROTOBUF_TYPE_BOOL,
     offsetof(protobuf_test_message_t, active), SIZE_MAX},
};

static const neverc_protobuf_message_descriptor_t protobuf_test_descriptor = {
    sizeof(protobuf_test_message_t), protobuf_test_fields,
    sizeof(protobuf_test_fields) / sizeof(protobuf_test_fields[0])};

static void test_wire_golden(void) {
    uint8_t encoded[128];
    neverc_protobuf_writer_t writer;
    neverc_protobuf_writer_init(&writer, encoded, sizeof(encoded));
    CHECK(neverc_protobuf_write_uint64(&writer, 1U, 150U) == 0);
    CHECK(neverc_protobuf_write_sint32(&writer, 2U, -2) == 0);
    CHECK(neverc_protobuf_write_string(&writer, 3U, "abc", 3U) == 0);
    static const uint8_t expected[] = {
        0x08U, 0x96U, 0x01U, 0x10U, 0x03U,
        0x1aU, 0x03U, 'a', 'b', 'c'};
    CHECK(neverc_protobuf_writer_length(&writer) == sizeof(expected));
    CHECK(memcmp(encoded, expected, sizeof(expected)) == 0);

    neverc_protobuf_reader_t reader;
    neverc_protobuf_reader_init(&reader, encoded, sizeof(expected), 64U);
    neverc_protobuf_field_t field;
    CHECK(neverc_protobuf_reader_next(&reader, &field) == 1);
    CHECK(field.number == 1U && field.value.varint == 150U);
    CHECK(neverc_protobuf_reader_next(&reader, &field) == 1);
    CHECK(field.number == 2U &&
          neverc_protobuf_zigzag_decode32((uint32_t)field.value.varint) == -2);
    CHECK(neverc_protobuf_reader_next(&reader, &field) == 1);
    CHECK(field.number == 3U && field.value.bytes.length == 3U &&
          memcmp(field.value.bytes.data, "abc", 3U) == 0);
    CHECK(neverc_protobuf_reader_next(&reader, &field) == 0);
}

static void test_descriptor_roundtrip(void) {
    protobuf_test_message_t input;
    memset(&input, 0, sizeof(input));
    input.sequence = UINT64_C(9007199254740991);
    input.has_delta = 1;
    input.delta = 0;
    input.player.data = (const uint8_t *)"player-1";
    input.player.length = 8U;
    input.active = 1;
    uint8_t encoded[256];
    size_t encoded_length = 0;
    CHECK(neverc_protobuf_message_encode(
              &protobuf_test_descriptor, &input, encoded, sizeof(encoded),
              &encoded_length) == 0);
    CHECK(encoded_length > 0);
    protobuf_test_message_t output;
    memset(&output, 0xff, sizeof(output));
    CHECK(neverc_protobuf_message_decode(
              &protobuf_test_descriptor, encoded, encoded_length, 64U,
              &output, sizeof(output)) == 0);
    CHECK(output.sequence == input.sequence);
    CHECK(output.has_delta == 1 && output.delta == 0);
    CHECK(output.player.length == 8U &&
          memcmp(output.player.data, "player-1", 8U) == 0);
    CHECK(output.active == 1);
}

static void test_malformed_inputs(void) {
    neverc_protobuf_reader_t reader;
    neverc_protobuf_field_t field;
    static const uint8_t truncated_varint[] = {
        0x08U, 0x80U, 0x80U, 0x80U, 0x80U, 0x80U};
    neverc_protobuf_reader_init(&reader, truncated_varint,
                                sizeof(truncated_varint), 64U);
    CHECK(neverc_protobuf_reader_next(&reader, &field) == -1);
    static const uint8_t zero_field[] = {0x00U};
    neverc_protobuf_reader_init(&reader, zero_field, sizeof(zero_field), 64U);
    CHECK(neverc_protobuf_reader_next(&reader, &field) == -1);
    static const uint8_t oversized[] = {0x0aU, 0x05U, 1U, 2U, 3U, 4U, 5U};
    neverc_protobuf_reader_init(&reader, oversized, sizeof(oversized), 4U);
    CHECK(neverc_protobuf_reader_next(&reader, &field) == -1);
    static const uint8_t invalid_wire[] = {0x0bU};
    neverc_protobuf_reader_init(&reader, invalid_wire, sizeof(invalid_wire),
                                64U);
    CHECK(neverc_protobuf_reader_next(&reader, &field) == -1);
}

static void test_utf8_and_bounds(void) {
    static const uint8_t valid[] = {'h', 0xc3U, 0xa9U};
    static const uint8_t invalid[] = {0xc0U, 0xafU};
    CHECK(neverc_protobuf_utf8_valid(valid, sizeof(valid)) == 1);
    CHECK(neverc_protobuf_utf8_valid(invalid, sizeof(invalid)) == 0);
    uint8_t one_byte[1];
    neverc_protobuf_writer_t writer;
    neverc_protobuf_writer_init(&writer, one_byte, sizeof(one_byte));
    CHECK(neverc_protobuf_write_string(&writer, 1U, "too-long", 8U) == -1);
    CHECK(neverc_protobuf_writer_length(&writer) == 0U);
}

int main(void) {
    printf("Protobuf test suite:\n");
    test_wire_golden();
    test_descriptor_roundtrip();
    test_malformed_inputs();
    test_utf8_and_bounds();
    printf("protobuf: %d checks, %d failed\n", tests_run, tests_failed);
    if (tests_failed == 0) puts("passed");
    return tests_failed == 0 ? 0 : 1;
}
