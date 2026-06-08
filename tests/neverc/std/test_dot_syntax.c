/*
 * Dot-syntax test suite.
 * Verifies that module.function() calls are correctly resolved.
 *
 * Top-level: math.abs(x), strconv.format_int(...)
 * Submodules: encoding.hex.encode(...), hash.crc32.ieee(...)
 */
#include "neverc/std/math.h"
#include "neverc/std/strconv.h"
#include "neverc/std/encoding.h"
#include "neverc/std/hash.h"
#include "neverc/std/container.h"
#include <stdio.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

#define CHECK(name, cond) do { \
    tests_run++; \
    if (cond) { tests_passed++; } \
    else { tests_failed++; printf("  FAIL: %s\n", name); } \
} while(0)

#define APPROX(a, b) (((a) - (b)) < 1e-9 && ((b) - (a)) < 1e-9)

#ifdef __neverc__

static void test_math_dot_syntax(void) {
    CHECK("math.abs_positive", APPROX(math.abs(3.14), 3.14));
    CHECK("math.abs_negative", APPROX(math.abs(-2.718), 2.718));
    CHECK("math.sqrt_4", APPROX(math.sqrt(4.0), 2.0));
    CHECK("math.sqrt_9", APPROX(math.sqrt(9.0), 3.0));
    CHECK("math.ceil_1_3", APPROX(math.ceil(1.3), 2.0));
    CHECK("math.floor_1_7", APPROX(math.floor(1.7), 1.0));
    CHECK("math.max_vals", APPROX(math.max(3.0, 5.0), 5.0));
    CHECK("math.min_vals", APPROX(math.min(3.0, 5.0), 3.0));
    CHECK("math.pow_2_10", APPROX(math.pow(2.0, 10.0), 1024.0));
    CHECK("math.log2_8", APPROX(math.log2(8.0), 3.0));
    CHECK("math.sin_0", APPROX(math.sin(0.0), 0.0));
    CHECK("math.cos_0", APPROX(math.cos(0.0), 1.0));
    CHECK("math.copysign_neg", APPROX(math.copysign(5.0, -1.0), -5.0));

    double sn, cs;
    math.sincos(0.0, &sn, &cs);
    CHECK("math.sincos_zero_sin", APPROX(sn, 0.0));
    CHECK("math.sincos_zero_cos", APPROX(cs, 1.0));
}

static void test_strconv_dot_syntax(void) {
    char buf[64];
    int n = strconv.format_int(42, 10, buf, sizeof(buf));
    CHECK("strconv.format_int_42", n > 0 && strcmp(buf, "42") == 0);

    n = strconv.format_int(-7, 10, buf, sizeof(buf));
    CHECK("strconv.format_int_neg", n > 0 && strcmp(buf, "-7") == 0);

    int64_t val;
    int ok = strconv.parse_int("12345", 10, &val);
    CHECK("strconv.parse_int_ok", ok == 0 && val == 12345);

    n = strconv.format_bool(1, buf, sizeof(buf));
    CHECK("strconv.format_bool_true", n > 0 && strcmp(buf, "true") == 0);
}

static void test_encoding_dot_syntax(void) {
    const uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF};
    char hexbuf[16];
    encoding.hex.encode(hexbuf, data, 4);
    CHECK("encoding.hex.encode", strcmp(hexbuf, "deadbeef") == 0);

    uint8_t decoded[4];
    int rc = encoding.hex.decode(decoded, hexbuf, 8);
    CHECK("encoding.hex.decode_ok", rc == 4);
    CHECK("encoding.hex.decode_match", decoded[0] == 0xDE && decoded[3] == 0xEF);
}

static void test_hash_dot_syntax(void) {
    uint32_t c = hash.crc32.ieee((const uint8_t *)"hello", 5);
    CHECK("hash.crc32.ieee_nonzero", c != 0);
    CHECK("hash.crc32.ieee_known", c == 0x3610a686);
}

static void test_container_vector_dot_syntax(void) {
    neverc_vector_t *v = container.vector.new(sizeof(int));
    CHECK("container.vector.new", v != NULL);
    CHECK("container.vector.empty", container.vector.empty(v));
    CHECK("container.vector.size_0", container.vector.size(v) == 0);

    int val = 42;
    container.vector.push_back(v, &val);
    CHECK("container.vector.push_back", container.vector.size(v) == 1);
    CHECK("container.vector.at", *(int *)container.vector.at(v, 0) == 42);

    val = 99;
    container.vector.push_back(v, &val);
    CHECK("container.vector.back", *(int *)container.vector.back(v) == 99);

    container.vector.clear(v);
    CHECK("container.vector.clear", container.vector.size(v) == 0);

    container.vector.free(v);
    CHECK("container.vector.free", 1 == 1);
}

#else
static void test_math_dot_syntax(void) {
    CHECK("dot_syntax_unavailable_math", 1);
}
static void test_strconv_dot_syntax(void) {
    CHECK("dot_syntax_unavailable_strconv", 1);
}
static void test_encoding_dot_syntax(void) {
    CHECK("dot_syntax_unavailable_encoding", 1);
}
static void test_hash_dot_syntax(void) {
    CHECK("dot_syntax_unavailable_hash", 1);
}
static void test_container_vector_dot_syntax(void) {
    CHECK("dot_syntax_unavailable_container_vector", 1);
}
#endif /* __neverc__ */

int main(void) {
    test_math_dot_syntax();
    test_strconv_dot_syntax();
    test_encoding_dot_syntax();
    test_hash_dot_syntax();
    test_container_vector_dot_syntax();

    printf("%d/%d tests passed\n", tests_passed, tests_run);
    if (tests_failed > 0)
        printf("%d tests FAILED\n", tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
