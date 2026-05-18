// test_rust_integers.c - Rust-style fixed-width integer type tests
// The Rust-style keywords are off by default; this suite opts in explicitly.
// RUN: %neverc -std=c23 -fneverc-types -c %s -o %t.o && echo "PASS: rust integers compile"
// RUN: %neverc -std=c23 -fneverc-types %s -o %t && %t && echo "PASS: rust integers run"

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

// ---- Size verification ----

static void test_sizes(void) {
    if (sizeof(i8) != 1) abort();
    if (sizeof(i16) != 2) abort();
    if (sizeof(i32) != 4) abort();
    if (sizeof(i64) != 8) abort();

    if (sizeof(u8) != 1) abort();
    if (sizeof(u16) != 2) abort();
    if (sizeof(u32) != 4) abort();
    if (sizeof(u64) != 8) abort();

    if (sizeof(isize) != sizeof(void *)) abort();
    if (sizeof(usize) != sizeof(void *)) abort();
}

// ---- Signedness verification ----

static void test_signedness(void) {
    i8 neg8 = -1;
    if (neg8 >= 0) abort();

    i16 neg16 = -1;
    if (neg16 >= 0) abort();

    i32 neg32 = -1;
    if (neg32 >= 0) abort();

    i64 neg64 = -1;
    if (neg64 >= 0) abort();

    // Unsigned types: (u8)(-1) wraps to max value
    u8 max8 = (u8)-1;
    if (max8 != 255) abort();

    u16 max16 = (u16)-1;
    if (max16 != 65535) abort();

    u32 max32 = (u32)-1;
    if (max32 != 4294967295U) abort();

    u64 max64 = (u64)-1;
    if (max64 != 18446744073709551615ULL) abort();
}

// ---- Range boundary tests ----

static void test_ranges(void) {
    i8 i8_min = -128;
    i8 i8_max = 127;
    if (i8_min != -128) abort();
    if (i8_max != 127) abort();

    i16 i16_min = -32768;
    i16 i16_max = 32767;
    if (i16_min != -32768) abort();
    if (i16_max != 32767) abort();

    i32 i32_min = (-2147483647 - 1);
    i32 i32_max = 2147483647;
    if (i32_min != (-2147483647 - 1)) abort();
    if (i32_max != 2147483647) abort();

    u8 u8_min = 0;
    u8 u8_max = 255;
    if (u8_min != 0) abort();
    if (u8_max != 255) abort();

    u16 u16_min = 0;
    u16 u16_max = 65535;
    if (u16_min != 0) abort();
    if (u16_max != 65535) abort();

    u32 u32_min = 0;
    u32 u32_max = 4294967295U;
    if (u32_min != 0) abort();
    if (u32_max != 4294967295U) abort();
}

// ---- Arithmetic operations ----

static void test_arithmetic(void) {
    i32 a = 100;
    i32 b = 200;
    i32 sum = a + b;
    if (sum != 300) abort();

    u64 x = 1000000000ULL;
    u64 y = 2000000000ULL;
    u64 product = x * 3;
    if (product != 3000000000ULL) abort();

    i8 small = 10;
    i8 smaller = 3;
    i8 remainder = small % smaller;
    if (remainder != 1) abort();
}

// ---- Pointer arithmetic with usize ----

static void test_pointer_arithmetic(void) {
    int arr[10];
    for (usize idx = 0; idx < 10; idx++) {
        arr[idx] = (i32)idx * 10;
    }
    if (arr[0] != 0) abort();
    if (arr[5] != 50) abort();
    if (arr[9] != 90) abort();

    usize len = sizeof(arr) / sizeof(arr[0]);
    if (len != 10) abort();
}

// ---- Type casting ----

static void test_casts(void) {
    i32 signed_val = -42;
    u32 unsigned_val = (u32)signed_val;
    i32 back = (i32)unsigned_val;
    if (back != -42) abort();

    u8 byte = 200;
    i8 sbyte = (i8)byte;
    u8 back_byte = (u8)sbyte;
    if (back_byte != 200) abort();

    i64 big = 100000LL;
    i16 truncated = (i16)big;
    if (truncated != (i16)100000LL) abort();
}

// ---- Struct with Rust-style types ----

struct RustPacket {
    u8 version;
    u16 length;
    u32 sequence;
    i64 timestamp;
    usize payload_size;
};

static void test_struct(void) {
    struct RustPacket pkt;
    pkt.version = 1;
    pkt.length = 1024;
    pkt.sequence = 42;
    pkt.timestamp = -1000;
    pkt.payload_size = 512;

    if (pkt.version != 1) abort();
    if (pkt.length != 1024) abort();
    if (pkt.sequence != 42) abort();
    if (pkt.timestamp != -1000) abort();
    if (pkt.payload_size != 512) abort();
}

// ---- Function parameters and return values ----

static u32 add_u32(u32 a, u32 b) {
    return a + b;
}

static i64 negate_i64(i64 val) {
    return -val;
}

static usize strlen_manual(const u8 *s) {
    usize len = 0;
    while (s[len] != 0) len++;
    return len;
}

static void test_functions(void) {
    u32 result = add_u32(100, 200);
    if (result != 300) abort();

    i64 neg = negate_i64(42);
    if (neg != -42) abort();

    const u8 *msg = (const u8 *)"hello";
    usize len = strlen_manual(msg);
    if (len != 5) abort();
}

// ---- u8 string literal coexistence ----
// Verify that u8"..." string literals still work alongside the u8 type keyword

static void test_u8_string_coexistence(void) {
    const char *utf8_str = u8"hello";
    if (utf8_str[0] != 'h') abort();

    u8 byte_val = 42;
    if (byte_val != 42) abort();

    u8 arr[] = {1, 2, 3, 4, 5};
    if (sizeof(arr) != 5) abort();
    if (arr[2] != 3) abort();
}

// ---- 128-bit types (if supported) ----

static void test_128bit(void) {
    i128 big_signed = 1;
    big_signed = big_signed << 100;
    if (big_signed == 0) abort();

    u128 big_unsigned = 1;
    big_unsigned = big_unsigned << 100;
    if (big_unsigned == 0) abort();

    i128 neg128 = -1;
    if (neg128 >= 0) abort();
}

// ---- Array of Rust-style types ----

static void test_arrays(void) {
    i32 ints[5] = {10, 20, 30, 40, 50};
    i32 total = 0;
    for (usize i = 0; i < 5; i++) {
        total += ints[i];
    }
    if (total != 150) abort();

    u8 bytes[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    if (bytes[0] != 0xDE) abort();
    if (bytes[3] != 0xEF) abort();
}

// ---- Typedef compatibility ----

typedef i32 MyInt;
typedef u64 MySize;

static void test_typedef(void) {
    MyInt x = -100;
    if (x != -100) abort();

    MySize sz = 1024 * 1024;
    if (sz != 1048576) abort();
}

int main(void) {
    test_sizes();
    test_signedness();
    test_ranges();
    test_arithmetic();
    test_pointer_arithmetic();
    test_casts();
    test_struct();
    test_functions();
    test_u8_string_coexistence();
    test_128bit();
    test_arrays();
    test_typedef();

    printf("test_rust_integers: ALL PASSED\n");
    return 0;
}
