#include "neverc/std/encoding/protobuf.h"

#include <limits.h>
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

static void test_scalar_writers(void) {
    uint8_t encoded[256];
    neverc_protobuf_writer_t writer;
    neverc_protobuf_writer_init(&writer, encoded, sizeof(encoded));
    CHECK(neverc_protobuf_write_bool(&writer, 1U, 1) == 0);
    CHECK(neverc_protobuf_write_bool(&writer, 1U, 2) == -1);
    CHECK(neverc_protobuf_write_uint32(&writer, 2U, 7U) == 0);
    CHECK(neverc_protobuf_write_int32(&writer, 3U, -1) == 0);
    CHECK(neverc_protobuf_write_int64(&writer, 4U, -2) == 0);
    CHECK(neverc_protobuf_write_sint64(&writer, 5U, -3) == 0);
    CHECK(neverc_protobuf_write_fixed32(&writer, 6U, 1U) == 0);
    CHECK(neverc_protobuf_write_fixed64(&writer, 7U, 2U) == 0);
    CHECK(neverc_protobuf_write_sfixed32(&writer, 8U, -4) == 0);
    CHECK(neverc_protobuf_write_sfixed64(&writer, 9U, -5) == 0);
    CHECK(neverc_protobuf_write_enum(&writer, 10U, 3) == 0);
    CHECK(neverc_protobuf_write_float(&writer, 11U, 1.0f) == 0);
    CHECK(neverc_protobuf_write_double(&writer, 12U, 2.0) == 0);
    CHECK(neverc_protobuf_write_bytes(&writer, 13U, "hi", 2U) == 0);
    CHECK(neverc_protobuf_writer_length(&writer) > 0U);
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
    static const uint8_t invalid_wire[] = {0x0fU}; /* field 1, wire type 7 */
    neverc_protobuf_reader_init(&reader, invalid_wire, sizeof(invalid_wire),
                                64U);
    CHECK(neverc_protobuf_reader_next(&reader, &field) == -1);

    /* 10-byte varint whose 10th byte is > 1 overflows uint64. */
    static const uint8_t overflow_varint[] = {
        0x08U, 0x80U, 0x80U, 0x80U, 0x80U, 0x80U,
        0x80U, 0x80U, 0x80U, 0x80U, 0x02U};
    neverc_protobuf_reader_init(&reader, overflow_varint,
                                sizeof(overflow_varint), 64U);
    CHECK(neverc_protobuf_reader_next(&reader, &field) == -1);

    /* 10-byte encoding of UINT64_MAX is in range (10th byte == 1). */
    static const uint8_t max_varint[] = {
        0x08U, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU,
        0xffU, 0xffU, 0xffU, 0xffU, 0x01U};
    neverc_protobuf_reader_init(&reader, max_varint, sizeof(max_varint), 64U);
    CHECK(neverc_protobuf_reader_next(&reader, &field) == 1);
    CHECK(field.number == 1U && field.value.varint == UINT64_MAX);
    CHECK(neverc_protobuf_reader_next(&reader, &field) == 0);
}

static void test_skip_groups(void) {
    neverc_protobuf_reader_t reader;
    neverc_protobuf_field_t field;

    /* field 1=1, empty group 2, field 3=7 */
    static const uint8_t empty_group[] = {
        0x08U, 0x01U, 0x13U, 0x14U, 0x18U, 0x07U};
    neverc_protobuf_reader_init(&reader, empty_group, sizeof(empty_group), 64U);
    CHECK(neverc_protobuf_reader_next(&reader, &field) == 1);
    CHECK(field.number == 1U && field.value.varint == 1U);
    CHECK(neverc_protobuf_reader_next(&reader, &field) == 1);
    CHECK(field.number == 3U && field.value.varint == 7U);
    CHECK(neverc_protobuf_reader_next(&reader, &field) == 0);

    /* start-group 2, nested varint, end-group 2, then field 1 */
    static const uint8_t nested[] = {
        0x13U, 0x08U, 0x01U, 0x14U, 0x08U, 0x2aU};
    neverc_protobuf_reader_init(&reader, nested, sizeof(nested), 64U);
    CHECK(neverc_protobuf_reader_next(&reader, &field) == 1);
    CHECK(field.number == 1U && field.value.varint == 42U);
    CHECK(neverc_protobuf_reader_next(&reader, &field) == 0);

    /* unmatched start-group */
    static const uint8_t open_group[] = {0x0bU};
    neverc_protobuf_reader_init(&reader, open_group, sizeof(open_group), 64U);
    CHECK(neverc_protobuf_reader_next(&reader, &field) == -1);

    /* 100 nested groups (the cap) then field 1=7. */
    {
        uint8_t deep[202];
        int i;
        for (i = 0; i < 100; i++) deep[i] = 0x0bU;
        for (i = 0; i < 100; i++) deep[100 + i] = 0x0cU;
        deep[200] = 0x08U;
        deep[201] = 0x07U;
        neverc_protobuf_reader_init(&reader, deep, sizeof(deep), 64U);
        CHECK(neverc_protobuf_reader_next(&reader, &field) == 1);
        CHECK(field.number == 1U && field.value.varint == 7U);
        CHECK(neverc_protobuf_reader_next(&reader, &field) == 0);
    }
    /* 101 nested groups exceed PROTOBUF_MAX_GROUP_DEPTH. */
    {
        uint8_t too_deep[202];
        int i;
        for (i = 0; i < 101; i++) too_deep[i] = 0x0bU;
        for (i = 0; i < 101; i++) too_deep[101 + i] = 0x0cU;
        neverc_protobuf_reader_init(&reader, too_deep, sizeof(too_deep), 64U);
        CHECK(neverc_protobuf_reader_next(&reader, &field) == -1);
    }

    /* end-group without start */
    static const uint8_t lone_end[] = {0x0cU};
    neverc_protobuf_reader_init(&reader, lone_end, sizeof(lone_end), 64U);
    CHECK(neverc_protobuf_reader_next(&reader, &field) == -1);

    /* start 2 / end 3 mismatch */
    static const uint8_t mismatch[] = {0x13U, 0x1cU};
    neverc_protobuf_reader_init(&reader, mismatch, sizeof(mismatch), 64U);
    CHECK(neverc_protobuf_reader_next(&reader, &field) == -1);

    /* Length-delimited payload inside a group must honor max_field_size. */
    {
        static const uint8_t group_oversize[] = {
            0x0bU, 0x12U, 0x05U, 1U, 2U, 3U, 4U, 5U, 0x0cU};
        neverc_protobuf_reader_init(&reader, group_oversize,
                                    sizeof(group_oversize), 4U);
        CHECK(neverc_protobuf_reader_next(&reader, &field) == -1);
    }
    /* Truncated fixed32 inside a group must not skip past the end-group. */
    {
        static const uint8_t group_trunc[] = {0x0bU, 0x0dU, 0x01U, 0x02U};
        neverc_protobuf_reader_init(&reader, group_trunc,
                                    sizeof(group_trunc), 64U);
        CHECK(neverc_protobuf_reader_next(&reader, &field) == -1);
        CHECK(neverc_protobuf_reader_next(&reader, &field) == -1);
    }
    /* Unknown wire type inside a group is malformed, not skipped. */
    {
        static const uint8_t group_bad_wire[] = {0x0bU, 0x0fU};
        neverc_protobuf_reader_init(&reader, group_bad_wire,
                                    sizeof(group_bad_wire), 64U);
        CHECK(neverc_protobuf_reader_next(&reader, &field) == -1);
    }
    /* Empty length-delimited field is valid. */
    {
        static const uint8_t empty_bytes[] = {0x0aU, 0x00U};
        neverc_protobuf_reader_init(&reader, empty_bytes,
                                    sizeof(empty_bytes), 64U);
        CHECK(neverc_protobuf_reader_next(&reader, &field) == 1);
        CHECK(field.number == 1U && field.value.bytes.length == 0U);
        CHECK(neverc_protobuf_reader_next(&reader, &field) == 0);
    }
}

static void test_truncated_does_not_desync(void) {
    neverc_protobuf_reader_t reader;
    neverc_protobuf_field_t field;
    /* Field 1 fixed64, only 2 payload bytes. After -1 the leftover 08 01
     * must not be reparsed as a varint field. */
    static const uint8_t truncated_fixed64[] = {0x09U, 0x08U, 0x01U};
    neverc_protobuf_reader_init(&reader, truncated_fixed64,
                                sizeof(truncated_fixed64), 64U);
    CHECK(neverc_protobuf_reader_next(&reader, &field) == -1);
    CHECK(neverc_protobuf_reader_next(&reader, &field) == -1);
}

static void test_zigzag(void) {
    CHECK(neverc_protobuf_zigzag_encode32(0) == 0U);
    CHECK(neverc_protobuf_zigzag_encode32(-1) == 1U);
    CHECK(neverc_protobuf_zigzag_encode32(1) == 2U);
    CHECK(neverc_protobuf_zigzag_encode32(-2) == 3U);
    CHECK(neverc_protobuf_zigzag_decode32(1U) == -1);
    CHECK(neverc_protobuf_zigzag_decode32(3U) == -2);
    CHECK(neverc_protobuf_zigzag_encode32(INT32_MIN) == UINT32_MAX);
    CHECK(neverc_protobuf_zigzag_decode32(UINT32_MAX) == INT32_MIN);
    CHECK(neverc_protobuf_zigzag_encode64(INT64_MIN) == UINT64_MAX);
    CHECK(neverc_protobuf_zigzag_decode64(UINT64_MAX) == INT64_MIN);
}

static void test_packed_and_wire_compat(void) {
    typedef struct {
        int32_t n;
        int32_t delta;
    } packed_msg_t;
    static const neverc_protobuf_field_descriptor_t fields[] = {
        {1U, NEVERC_PROTOBUF_TYPE_INT32,
         offsetof(packed_msg_t, n), SIZE_MAX},
        {2U, NEVERC_PROTOBUF_TYPE_SINT32,
         offsetof(packed_msg_t, delta), SIZE_MAX},
    };
    static const neverc_protobuf_message_descriptor_t desc = {
        sizeof(packed_msg_t), fields, 2U};
    packed_msg_t msg;

    /* Packed int32 [42, 43] — last value wins. */
    static const uint8_t packed_int32[] = {0x0aU, 0x02U, 0x2aU, 0x2bU};
    CHECK(neverc_protobuf_message_decode(&desc, packed_int32,
                                         sizeof(packed_int32), 64U, &msg,
                                         sizeof(msg)) == 0);
    CHECK(msg.n == 43);

    /* Packed sint32 zigzag(-2) = 3. */
    static const uint8_t packed_sint32[] = {0x12U, 0x01U, 0x03U};
    CHECK(neverc_protobuf_message_decode(&desc, packed_sint32,
                                         sizeof(packed_sint32), 64U, &msg,
                                         sizeof(msg)) == 0);
    CHECK(msg.delta == -2);

    /* Truncated packed varint is malformed. */
    static const uint8_t truncated_packed[] = {0x0aU, 0x02U, 0x80U};
    CHECK(neverc_protobuf_message_decode(&desc, truncated_packed,
                                         sizeof(truncated_packed), 64U, &msg,
                                         sizeof(msg)) == -1);

    /* A valid field must not remain if a later field is truncated. */
    memset(&msg, 0xff, sizeof(msg));
    static const uint8_t good_then_truncated[] = {
        0x08U, 0x2aU, 0x12U, 0x02U, 0x80U};
    CHECK(neverc_protobuf_message_decode(&desc, good_then_truncated,
                                         sizeof(good_then_truncated), 64U,
                                         &msg, sizeof(msg)) == -1);
    CHECK(msg.n == 0 && msg.delta == 0);

    /* Unexpected fixed64 on int32 is skipped; later varint is kept. */
    static const uint8_t skip_wire[] = {
        0x09U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x08U, 0x2aU};
    CHECK(neverc_protobuf_message_decode(&desc, skip_wire, sizeof(skip_wire),
                                         64U, &msg, sizeof(msg)) == 0);
    CHECK(msg.n == 42);

    /* Unknown group is skipped; following int32 is kept. */
    static const uint8_t with_group[] = {
        0x13U, 0x08U, 0x01U, 0x14U, 0x08U, 0x09U};
    CHECK(neverc_protobuf_message_decode(&desc, with_group, sizeof(with_group),
                                         64U, &msg, sizeof(msg)) == 0);
    CHECK(msg.n == 9);

    /* Unknown field 15 (varint 99) is skipped; field 1 is kept. */
    static const uint8_t unknown_field[] = {
        0x78U, 0x63U, 0x08U, 0x2aU};
    CHECK(neverc_protobuf_message_decode(&desc, unknown_field,
                                         sizeof(unknown_field), 64U, &msg,
                                         sizeof(msg)) == 0);
    CHECK(msg.n == 42);
}

static void test_int32_range(void) {
    typedef struct { int32_t n; } int32_msg_t;
    static const neverc_protobuf_field_descriptor_t fields[] = {
        {1U, NEVERC_PROTOBUF_TYPE_INT32,
         offsetof(int32_msg_t, n), SIZE_MAX},
    };
    static const neverc_protobuf_message_descriptor_t desc = {
        sizeof(int32_msg_t), fields, 1U};
    int32_msg_t msg;

    /* 5-byte 0xFFFFFFFF is a valid int32 encoding of -1 (low 32 bits). */
    static const uint8_t truncated_neg[] = {
        0x08U, 0xffU, 0xffU, 0xffU, 0xffU, 0x0fU};
    CHECK(neverc_protobuf_message_decode(&desc, truncated_neg,
                                         sizeof(truncated_neg), 64U, &msg,
                                         sizeof(msg)) == 0);
    CHECK(msg.n == -1);

    /* 2^32 truncates to 0, matching proto2/proto3 wire compatibility. */
    static const uint8_t two_pow_32[] = {
        0x08U, 0x80U, 0x80U, 0x80U, 0x80U, 0x10U};
    CHECK(neverc_protobuf_message_decode(&desc, two_pow_32,
                                         sizeof(two_pow_32), 64U, &msg,
                                         sizeof(msg)) == 0);
    CHECK(msg.n == 0);

    /* Sign-extended 10-byte int32(-1) still round-trips. */
    uint8_t encoded[16];
    neverc_protobuf_writer_t writer;
    neverc_protobuf_writer_init(&writer, encoded, sizeof(encoded));
    CHECK(neverc_protobuf_write_int32(&writer, 1U, -1) == 0);
    CHECK(neverc_protobuf_message_decode(&desc, encoded,
                                         neverc_protobuf_writer_length(&writer),
                                         64U, &msg, sizeof(msg)) == 0);
    CHECK(msg.n == -1);

    static const uint8_t ok[] = {0x08U, 0x2aU}; /* 42 */
    CHECK(neverc_protobuf_message_decode(&desc, ok, sizeof(ok),
                                         64U, &msg, sizeof(msg)) == 0);
    CHECK(msg.n == 42);
}

static void test_sint32_truncates_then_zigzag(void) {
    typedef struct { int32_t n; } sint32_msg_t;
    static const neverc_protobuf_field_descriptor_t fields[] = {
        {1U, NEVERC_PROTOBUF_TYPE_SINT32,
         offsetof(sint32_msg_t, n), SIZE_MAX},
    };
    static const neverc_protobuf_message_descriptor_t desc = {
        sizeof(sint32_msg_t), fields, 1U};
    sint32_msg_t msg;

    /* 2^32+2 truncated to 32 bits is 2; zigzag32(2) is 1. */
    static const uint8_t wide[] = {
        0x08U, 0x82U, 0x80U, 0x80U, 0x80U, 0x10U};
    CHECK(neverc_protobuf_message_decode(&desc, wide, sizeof(wide),
                                         64U, &msg, sizeof(msg)) == 0);
    CHECK(msg.n == 1);
}

static void test_bool_and_uint32_compat(void) {
    typedef struct {
        int flag;
        uint32_t u;
    } compat_msg_t;
    static const neverc_protobuf_field_descriptor_t fields[] = {
        {1U, NEVERC_PROTOBUF_TYPE_BOOL,
         offsetof(compat_msg_t, flag), SIZE_MAX},
        {2U, NEVERC_PROTOBUF_TYPE_UINT32,
         offsetof(compat_msg_t, u), SIZE_MAX},
    };
    static const neverc_protobuf_message_descriptor_t desc = {
        sizeof(compat_msg_t), fields, 2U};
    compat_msg_t msg;

    /* Nonzero bool varint is true. */
    static const uint8_t bool_two[] = {0x08U, 0x02U};
    CHECK(neverc_protobuf_message_decode(&desc, bool_two, sizeof(bool_two),
                                         64U, &msg, sizeof(msg)) == 0);
    CHECK(msg.flag == 1);

    /* uint32 2^32+1 truncates to 1. */
    static const uint8_t u32[] = {
        0x10U, 0x81U, 0x80U, 0x80U, 0x80U, 0x10U};
    CHECK(neverc_protobuf_message_decode(&desc, u32, sizeof(u32),
                                         64U, &msg, sizeof(msg)) == 0);
    CHECK(msg.u == 1U);

    /* Packed bool last-wins, including a nonzero-not-one value. */
    static const uint8_t packed_bool[] = {0x0aU, 0x02U, 0x00U, 0x07U};
    CHECK(neverc_protobuf_message_decode(&desc, packed_bool,
                                         sizeof(packed_bool), 64U, &msg,
                                         sizeof(msg)) == 0);
    CHECK(msg.flag == 1);
}

static void test_utf8_and_bounds(void) {
    static const uint8_t valid[] = {'h', 0xc3U, 0xa9U};
    static const uint8_t invalid[] = {0xc0U, 0xafU};
    static const uint8_t overlong3[] = {0xe0U, 0x80U, 0x80U};
    static const uint8_t surrogate[] = {0xedU, 0xa0U, 0x80U};
    CHECK(neverc_protobuf_utf8_valid(valid, sizeof(valid)) == 1);
    CHECK(neverc_protobuf_utf8_valid(invalid, sizeof(invalid)) == 0);
    CHECK(neverc_protobuf_utf8_valid(overlong3, sizeof(overlong3)) == 0);
    CHECK(neverc_protobuf_utf8_valid(surrogate, sizeof(surrogate)) == 0);
    static const uint8_t too_large[] = {0xf4U, 0x90U, 0x80U, 0x80U};
    CHECK(neverc_protobuf_utf8_valid(too_large, sizeof(too_large)) == 0);
    uint8_t one_byte[1];
    neverc_protobuf_writer_t writer;
    neverc_protobuf_writer_init(&writer, one_byte, sizeof(one_byte));
    CHECK(neverc_protobuf_write_string(&writer, 1U, "too-long", 8U) == -1);
    CHECK(neverc_protobuf_writer_length(&writer) == 0U);

    typedef struct { neverc_protobuf_bytes_t s; } str_msg_t;
    static const neverc_protobuf_field_descriptor_t str_fields[] = {
        {1U, NEVERC_PROTOBUF_TYPE_STRING,
         offsetof(str_msg_t, s), SIZE_MAX},
    };
    static const neverc_protobuf_message_descriptor_t str_desc = {
        sizeof(str_msg_t), str_fields, 1U};
    str_msg_t str_msg;
    static const uint8_t bad_str[] = {0x0aU, 0x02U, 0xc0U, 0xafU};
    CHECK(neverc_protobuf_message_decode(&str_desc, bad_str, sizeof(bad_str),
                                         64U, &str_msg, sizeof(str_msg)) == -1);
}

int main(void) {
    printf("Protobuf test suite:\n");
    test_wire_golden();
    test_scalar_writers();
    test_descriptor_roundtrip();
    test_malformed_inputs();
    test_skip_groups();
    test_truncated_does_not_desync();
    test_zigzag();
    test_packed_and_wire_compat();
    test_int32_range();
    test_sint32_truncates_then_zigzag();
    test_bool_and_uint32_compat();
    test_utf8_and_bounds();
    printf("protobuf: %d checks, %d failed\n", tests_run, tests_failed);
    if (tests_failed == 0) puts("passed");
    return tests_failed == 0 ? 0 : 1;
}
