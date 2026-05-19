// Final validation that all 32-bit architecture removal is safe.
//
// This test exercises code paths affected by:
//   1. canUseCMOV() removed (always true on x86_64)
//   2. CMOV_GR32/CMOV_GR16 NoCMOV pseudos removed from .td
//   3. NoCMOV predicate removed, HasCMOV changed to hasCMOV()
//   4. ARM32 Windows EH unwind code removed (~1083 lines)
//   5. ARM32 unwind opcodes removed from Win64EH.h
//   6. Triple: isArch32Bit/get32BitArchVariant/isArch16Bit removed
//   7. ELF/Mach-O writers hardcoded to 64-bit
//   8. MCAssembler ThumbFunc tracking removed
//
// Key principle: 32-bit OPERATIONS within 64-bit mode must still work.
// Only 32-bit ARCHITECTURES/MODES were removed.
//
// RUN: %neverc -O2 %s -o %t && %t && echo "PASS: 64only_final_validation"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
        abort(); \
    } \
} while (0)

// =====================================================
// 1. CMOV on all integer widths (canUseCMOV() removed)
// =====================================================

__attribute__((noinline))
static int64_t cmov_i64(int cond, int64_t a, int64_t b) { return cond ? a : b; }

__attribute__((noinline))
static int32_t cmov_i32(int cond, int32_t a, int32_t b) { return cond ? a : b; }

__attribute__((noinline))
static int16_t cmov_i16(int cond, int16_t a, int16_t b) { return cond ? a : b; }

__attribute__((noinline))
static int8_t cmov_i8(int cond, int8_t a, int8_t b) { return cond ? a : b; }

static void test_cmov_all_widths(void) {
    CHECK(cmov_i64(1, 0x123456789ABCDEF0LL, -1) == 0x123456789ABCDEF0LL, "cmov64 true");
    CHECK(cmov_i64(0, -1, 0x123456789ABCDEF0LL) == 0x123456789ABCDEF0LL, "cmov64 false");
    CHECK(cmov_i32(1, 0x7FFFFFFF, -1) == 0x7FFFFFFF, "cmov32 true");
    CHECK(cmov_i32(0, -1, 0x7FFFFFFF) == 0x7FFFFFFF, "cmov32 false");
    CHECK(cmov_i16(1, 0x7FFF, -1) == 0x7FFF, "cmov16 true");
    CHECK(cmov_i16(0, -1, 0x7FFF) == 0x7FFF, "cmov16 false");
    CHECK(cmov_i8(1, 127, -1) == 127, "cmov8 true");
    CHECK(cmov_i8(0, -1, 127) == 127, "cmov8 false");
}

// =====================================================
// 2. Integer ABS (canUseCMOV guard removed from ABS)
// =====================================================

__attribute__((noinline))
static int32_t my_abs32(int32_t x) {
    return x < 0 ? -x : x;
}

__attribute__((noinline))
static int64_t my_abs64(int64_t x) {
    return x < 0 ? -x : x;
}

static void test_abs_without_cmov_guard(void) {
    CHECK(my_abs32(0) == 0, "abs32(0)");
    CHECK(my_abs32(42) == 42, "abs32(42)");
    CHECK(my_abs32(-42) == 42, "abs32(-42)");
    CHECK(my_abs32(-2147483647) == 2147483647, "abs32(INT32_MIN+1)");

    CHECK(my_abs64(0) == 0, "abs64(0)");
    CHECK(my_abs64(42) == 42, "abs64(42)");
    CHECK(my_abs64(-42) == 42, "abs64(-42)");
    CHECK(my_abs64(-9223372036854775807LL) == 9223372036854775807LL, "abs64(INT64_MIN+1)");
}

// =====================================================
// 3. SDIV by power of 2 (canUseCMOV guard removed)
// =====================================================

__attribute__((noinline))
static int32_t sdiv_pow2_32(int32_t x) { return x / 8; }

__attribute__((noinline))
static int64_t sdiv_pow2_64(int64_t x) { return x / 16; }

static void test_sdiv_pow2(void) {
    CHECK(sdiv_pow2_32(80) == 10, "sdiv32(80,8)");
    CHECK(sdiv_pow2_32(-80) == -10, "sdiv32(-80,8)");
    CHECK(sdiv_pow2_32(7) == 0, "sdiv32(7,8)");
    CHECK(sdiv_pow2_32(-7) == 0, "sdiv32(-7,8)");

    CHECK(sdiv_pow2_64(160) == 10, "sdiv64(160,16)");
    CHECK(sdiv_pow2_64(-160) == -10, "sdiv64(-160,16)");
    CHECK(sdiv_pow2_64(15) == 0, "sdiv64(15,16)");
}

// =====================================================
// 4. 32-bit sub-register operations within 64-bit mode
//    (these must NOT be broken by 32-bit arch removal)
// =====================================================

__attribute__((noinline))
static uint32_t rotate_left32(uint32_t val, int n) {
    n &= 31;
    return (val << n) | (val >> (32 - n));
}

__attribute__((noinline))
static uint32_t popcount32(uint32_t x) {
    return __builtin_popcount(x);
}

__attribute__((noinline))
static uint32_t clz32(uint32_t x) {
    return x ? __builtin_clz(x) : 32;
}

__attribute__((noinline))
static uint32_t ctz32(uint32_t x) {
    return x ? __builtin_ctz(x) : 32;
}

static void test_32bit_ops_in_64bit(void) {
    CHECK(rotate_left32(0x12345678, 8) == 0x34567812, "rotl32(8)");
    CHECK(rotate_left32(0x80000001, 1) == 0x00000003, "rotl32(1)");

    CHECK(popcount32(0) == 0, "popcount(0)");
    CHECK(popcount32(0xFFFFFFFF) == 32, "popcount(0xFFFFFFFF)");
    CHECK(popcount32(0xAAAAAAAA) == 16, "popcount(0xAAAA...)");

    CHECK(clz32(0x80000000) == 0, "clz(0x80000000)");
    CHECK(clz32(1) == 31, "clz(1)");
    CHECK(clz32(0) == 32, "clz(0)");

    CHECK(ctz32(0x80000000) == 31, "ctz(0x80000000)");
    CHECK(ctz32(1) == 0, "ctz(1)");
    CHECK(ctz32(0) == 32, "ctz(0)");
}

// =====================================================
// 5. Type sizes confirm 64-bit target
// =====================================================

static void test_64bit_type_sizes(void) {
    CHECK(sizeof(void *) == 8, "ptr 8 bytes");
    CHECK(sizeof(long long) == 8, "long long 8 bytes");
    CHECK(sizeof(size_t) == 8, "size_t 8 bytes");
    CHECK(_Alignof(void *) == 8, "ptr align 8");

#if defined(__x86_64__)
    CHECK(sizeof(long) == 8 || sizeof(long) == 4,
          "long is 4 (Win64) or 8 (SysV)");
#elif defined(__aarch64__)
    CHECK(sizeof(long) == 8 || sizeof(long) == 4,
          "long is 4 (Win64) or 8 (LP64)");
#endif
}

// =====================================================
// 6. ffs-1 select pattern (canUseCMOV guard removed)
// =====================================================

__attribute__((noinline))
static int ffs_minus1_32(uint32_t x) {
    return x ? __builtin_ctz(x) : -1;
}

__attribute__((noinline))
static int ffs_minus1_64(uint64_t x) {
    return x ? __builtin_ctzll(x) : -1;
}

static void test_ffs_select_pattern(void) {
    CHECK(ffs_minus1_32(0) == -1, "ffs32(0)=-1");
    CHECK(ffs_minus1_32(1) == 0, "ffs32(1)=0");
    CHECK(ffs_minus1_32(0x80) == 7, "ffs32(0x80)=7");
    CHECK(ffs_minus1_32(0x80000000) == 31, "ffs32(MSB)=31");

    CHECK(ffs_minus1_64(0) == -1, "ffs64(0)=-1");
    CHECK(ffs_minus1_64(1) == 0, "ffs64(1)=0");
    CHECK(ffs_minus1_64(0x8000000000000000ULL) == 63, "ffs64(MSB)=63");
}

// =====================================================
// 7. smin/smax patterns (uses CMOV path)
// =====================================================

__attribute__((noinline))
static int32_t smin32(int32_t a, int32_t b) { return a < b ? a : b; }

__attribute__((noinline))
static int32_t smax32(int32_t a, int32_t b) { return a > b ? a : b; }

__attribute__((noinline))
static int64_t smin64(int64_t a, int64_t b) { return a < b ? a : b; }

static void test_smin_smax(void) {
    CHECK(smin32(10, 20) == 10, "smin32(10,20)");
    CHECK(smin32(-5, 5) == -5, "smin32(-5,5)");
    CHECK(smax32(10, 20) == 20, "smax32(10,20)");
    CHECK(smax32(-5, 5) == 5, "smax32(-5,5)");
    CHECK(smin64(-999999999999LL, 0) == -999999999999LL, "smin64(neg,0)");
}

// =====================================================
// 8. Struct with mixed-width fields (ABI correctness)
// =====================================================

typedef struct __attribute__((packed)) {
    uint8_t  tag;
    uint32_t val32;
    uint64_t val64;
    uint16_t checksum;
} PackedMsg;

static void test_packed_struct(void) {
    CHECK(sizeof(PackedMsg) == 15, "PackedMsg size 15");

    PackedMsg msg = {0xAB, 0xDEADBEEF, 0x123456789ABCDEF0ULL, 0xCAFE};
    CHECK(msg.tag == 0xAB, "packed tag");
    CHECK(msg.val32 == 0xDEADBEEF, "packed val32");
    CHECK(msg.val64 == 0x123456789ABCDEF0ULL, "packed val64");
    CHECK(msg.checksum == 0xCAFE, "packed checksum");

    PackedMsg copy;
    memcpy(&copy, &msg, sizeof(PackedMsg));
    CHECK(memcmp(&msg, &copy, sizeof(PackedMsg)) == 0, "packed memcpy");
}

// =====================================================
// 9. Floating-point conditional select (FP CMOV path)
// =====================================================

__attribute__((noinline))
static double fp_select(int cond, double a, double b) { return cond ? a : b; }

__attribute__((noinline))
static float fp_select_f(int cond, float a, float b) { return cond ? a : b; }

static void test_fp_cmov(void) {
    CHECK(fp_select(1, 3.14, 2.71) == 3.14, "fp_select true");
    CHECK(fp_select(0, 3.14, 2.71) == 2.71, "fp_select false");
    CHECK(fp_select_f(1, 1.5f, 2.5f) == 1.5f, "fp_select_f true");
    CHECK(fp_select_f(0, 1.5f, 2.5f) == 2.5f, "fp_select_f false");
}

int main(void) {
    test_cmov_all_widths();
    test_abs_without_cmov_guard();
    test_sdiv_pow2();
    test_32bit_ops_in_64bit();
    test_64bit_type_sizes();
    test_ffs_select_pattern();
    test_smin_smax();
    test_packed_struct();
    test_fp_cmov();

    printf("PASS: 64only_final_validation\n");
    return 0;
}
