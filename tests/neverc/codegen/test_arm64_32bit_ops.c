// test_arm64_32bit_ops.c
// Validates that ARM64 32-bit sub-register operations (W registers)
// work correctly after removing ARM32 code from the MC layer.
//
// Removed code:
//   - ARMUnwindEmitter (ARM32 Windows unwind, 1083 lines)
//   - ARM32 unwind opcodes (UOP_AllocHuge, UOP_WideAlloc*, etc.)
//   - ThumbFunc tracking (MCAssembler, MCExpr, MCSymbolMachO)
//   - ARM32 Mach-O subtypes (CPUSubTypeARM, CPU_SUBTYPE_ARM64_32)
//   - ARM32 COFF relocation handling (IMAGE_FILE_MACHINE_ARMNT)
//
// W0-W30 are the low 32 bits of X0-X30 and are fully legal in AArch64.
//
// RUN: %neverc -O2 %s -o %t && %t && echo "PASS: arm64_32bit_ops"
// Note: this test runs on both x86_64 and AArch64; the C code is portable.
// On AArch64, it exercises W register codegen directly.
// On x86_64, it exercises equivalent 32-bit sub-register codegen (EAX etc).

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
        abort(); \
    } \
} while (0)

// --- 1. 32-bit ops that map to W register instructions on AArch64 ---

__attribute__((noinline))
static uint32_t add32(uint32_t a, uint32_t b) { return a + b; }

__attribute__((noinline))
static uint32_t sub32(uint32_t a, uint32_t b) { return a - b; }

__attribute__((noinline))
static uint32_t mul32(uint32_t a, uint32_t b) { return a * b; }

__attribute__((noinline))
static uint32_t shl32(uint32_t a, int n) { return a << n; }

__attribute__((noinline))
static uint32_t shr32(uint32_t a, int n) { return a >> n; }

__attribute__((noinline))
static int32_t asr32(int32_t a, int n) { return a >> n; }

static void test_w_register_alu(void) {
    CHECK(add32(0xFFFFFFFF, 1) == 0, "w add overflow");
    CHECK(sub32(0, 1) == 0xFFFFFFFF, "w sub underflow");
    CHECK(mul32(0x10001, 0x10001) == 0x20001, "w mul truncation");
    CHECK(shl32(1, 31) == 0x80000000U, "w shl");
    CHECK(shr32(0x80000000U, 31) == 1, "w lsr");
    CHECK(asr32(-1, 31) == -1, "w asr sign-extend");
    CHECK(asr32(-128, 3) == -16, "w asr negative");
}

// --- 2. Zero-extension from W to X (32->64 bit) ---

__attribute__((noinline))
static uint64_t zext32to64(uint32_t x) {
    return x;
}

__attribute__((noinline))
static int64_t sext32to64(int32_t x) {
    return x;
}

static void test_extension(void) {
    CHECK(zext32to64(0xDEADBEEF) == 0xDEADBEEFULL, "zext u32->u64");
    CHECK(zext32to64(0) == 0, "zext 0");
    CHECK(zext32to64(0xFFFFFFFF) == 0xFFFFFFFFULL, "zext max u32");

    CHECK(sext32to64(-1) == -1LL, "sext -1");
    CHECK(sext32to64(-2147483648) == -2147483648LL, "sext INT32_MIN");
    CHECK(sext32to64(2147483647) == 2147483647LL, "sext INT32_MAX");
    CHECK(sext32to64(0) == 0LL, "sext 0");
}

// --- 3. Truncation from X to W (64->32 bit) ---

__attribute__((noinline))
static uint32_t trunc64to32(uint64_t x) {
    return (uint32_t)x;
}

static void test_truncation(void) {
    CHECK(trunc64to32(0xDEADBEEFCAFEBABEULL) == 0xCAFEBABEU, "trunc lo");
    CHECK(trunc64to32(0x100000000ULL) == 0, "trunc hi only");
    CHECK(trunc64to32(0xFFFFFFFFULL) == 0xFFFFFFFFU, "trunc max32");
}

// --- 4. Bit manipulation (CLZ/CTZ map to CLZ/RBIT+CLZ on AArch64) ---

__attribute__((noinline))
static int clz32(uint32_t x) {
    return __builtin_clz(x);
}

__attribute__((noinline))
static int ctz32(uint32_t x) {
    return __builtin_ctz(x);
}

__attribute__((noinline))
static int clz64(uint64_t x) {
    return __builtin_clzll(x);
}

static void test_bit_ops(void) {
    CHECK(clz32(1) == 31, "clz32(1)");
    CHECK(clz32(0x80000000U) == 0, "clz32(MSB)");
    CHECK(ctz32(0x80000000U) == 31, "ctz32(MSB)");
    CHECK(ctz32(4) == 2, "ctz32(4)");
    CHECK(clz64(1) == 63, "clz64(1)");
    CHECK(clz64(0x8000000000000000ULL) == 0, "clz64(MSB)");
}

// --- 5. Conditional select (CSEL on AArch64, CMOVcc on x86_64) ---

__attribute__((noinline))
static uint32_t csel32(int cond, uint32_t a, uint32_t b) {
    return cond ? a : b;
}

__attribute__((noinline))
static uint64_t csel64(int cond, uint64_t a, uint64_t b) {
    return cond ? a : b;
}

static void test_csel(void) {
    CHECK(csel32(1, 0xAA, 0xBB) == 0xAA, "csel32 true");
    CHECK(csel32(0, 0xAA, 0xBB) == 0xBB, "csel32 false");
    CHECK(csel64(1, 0xDEADULL, 0xBEEFULL) == 0xDEADULL, "csel64 true");
    CHECK(csel64(0, 0xDEADULL, 0xBEEFULL) == 0xBEEFULL, "csel64 false");
}

// --- 6. Mixed 32/64-bit struct layout ---

struct mixed_layout {
    uint32_t a;
    uint64_t b;
    uint32_t c;
    uint16_t d;
    uint8_t  e;
};

__attribute__((noinline))
static uint64_t hash_struct(const struct mixed_layout *s) {
    return (uint64_t)s->a ^ s->b ^ (uint64_t)s->c ^
           (uint64_t)s->d ^ (uint64_t)s->e;
}

static void test_struct_layout(void) {
    struct mixed_layout m = { 0xDEADBEEF, 0xCAFEBABE12345678ULL,
                              0x42424242, 0xABCD, 0x77 };

    CHECK(m.a == 0xDEADBEEF, "struct field a");
    CHECK(m.b == 0xCAFEBABE12345678ULL, "struct field b");
    CHECK(m.c == 0x42424242, "struct field c");
    CHECK(m.d == 0xABCD, "struct field d");
    CHECK(m.e == 0x77, "struct field e");

    uint64_t h = hash_struct(&m);
    uint64_t expected = 0xDEADBEEFULL ^ 0xCAFEBABE12345678ULL ^
                        0x42424242ULL ^ 0xABCDULL ^ 0x77ULL;
    CHECK(h == expected, "struct hash");
}

int main(void) {
    test_w_register_alu();
    test_extension();
    test_truncation();
    test_bit_ops();
    test_csel();
    test_struct_layout();
    printf("PASS: arm64_32bit_ops - all tests passed\n");
    return 0;
}
