// Validates ARM64 codegen after ARM32 infrastructure removal:
//   - ARM32 SEH unwind (1083 lines from MCWin64EH.cpp)
//   - ThumbFunc tracking removed from MCAssembler/MCExpr/MCSymbolMachO
//   - ARM32 Mach-O CPU subtypes and COFF relocation types removed
//   - MC is64Bit() hardcoded, ELF/MachO always write 64-bit format
//
// Tests verify W register ops (32-bit sub-registers of X) are preserved,
// since AArch64 heavily uses W0-W30 for 32-bit operations.
//
// RUN: %neverc -O2 %s -o %t && %t && echo "PASS: aarch64_cleanup_validation"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); \
        abort(); \
    } \
} while (0)

// ---- W register ALU ops (AArch64 32-bit sub-register operations) ----

__attribute__((noinline))
static uint32_t w_add(uint32_t a, uint32_t b) { return a + b; }

__attribute__((noinline))
static uint32_t w_sub(uint32_t a, uint32_t b) { return a - b; }

__attribute__((noinline))
static uint32_t w_mul(uint32_t a, uint32_t b) { return a * b; }

__attribute__((noinline))
static uint32_t w_and(uint32_t a, uint32_t b) { return a & b; }

__attribute__((noinline))
static uint32_t w_or(uint32_t a, uint32_t b) { return a | b; }

__attribute__((noinline))
static uint32_t w_xor(uint32_t a, uint32_t b) { return a ^ b; }

static void test_w_alu(void) {
    CHECK(w_add(0xFFFFFFFF, 1) == 0, "w add overflow");
    CHECK(w_add(123, 456) == 579, "w add simple");
    CHECK(w_sub(100, 30) == 70, "w sub");
    CHECK(w_sub(0, 1) == 0xFFFFFFFF, "w sub underflow");
    CHECK(w_mul(0x10000, 0x10000) == 0, "w mul overflow");
    CHECK(w_mul(12345, 6789) == 83810205U, "w mul");
    CHECK(w_and(0xFF00FF00, 0x0F0F0F0F) == 0x0F000F00, "w and");
    CHECK(w_or(0xFF000000, 0x00FF0000) == 0xFFFF0000, "w or");
    CHECK(w_xor(0xAAAAAAAA, 0x55555555) == 0xFFFFFFFF, "w xor");
}

// ---- W to X zero-extension ----

__attribute__((noinline))
static uint64_t zext_w_to_x(uint32_t w) {
    return w;
}

__attribute__((noinline))
static int64_t sext_w_to_x(int32_t w) {
    return w;
}

static void test_w_extension(void) {
    CHECK(zext_w_to_x(0xFFFFFFFF) == 0x00000000FFFFFFFFULL, "w zext max");
    CHECK(zext_w_to_x(0) == 0, "w zext zero");
    CHECK(zext_w_to_x(42) == 42, "w zext simple");

    CHECK(sext_w_to_x(-1) == -1LL, "w sext -1");
    CHECK((uint64_t)sext_w_to_x(-1) == 0xFFFFFFFFFFFFFFFFULL, "w sext -1 bits");
    CHECK(sext_w_to_x(INT32_MIN) == (int64_t)INT32_MIN, "w sext INT32_MIN");
    CHECK(sext_w_to_x(42) == 42LL, "w sext positive");
}

// ---- X to W truncation ----

__attribute__((noinline))
static uint32_t trunc_x_to_w(uint64_t x) {
    return (uint32_t)x;
}

static void test_trunc(void) {
    CHECK(trunc_x_to_w(0x123456789ABCDEF0ULL) == 0x9ABCDEF0U, "trunc low 32");
    CHECK(trunc_x_to_w(0x00000000FFFFFFFFULL) == 0xFFFFFFFF, "trunc max u32");
    CHECK(trunc_x_to_w(0) == 0, "trunc zero");
}

// ---- Conditional select (CSEL on AArch64, CMOV on x86_64) ----

__attribute__((noinline))
static int32_t csel32(int cond, int32_t a, int32_t b) {
    return cond ? a : b;
}

__attribute__((noinline))
static int64_t csel64(int cond, int64_t a, int64_t b) {
    return cond ? a : b;
}

static void test_csel(void) {
    CHECK(csel32(1, 100, 200) == 100, "csel32 true");
    CHECK(csel32(0, 100, 200) == 200, "csel32 false");
    CHECK(csel64(1, 0x100000000LL, -1LL) == 0x100000000LL, "csel64 true");
    CHECK(csel64(0, 0x100000000LL, -1LL) == -1LL, "csel64 false");
}

// ---- CLZ/CTZ on 32 and 64-bit ----

static void test_clz_ctz(void) {
    CHECK(__builtin_clz(1) == 31, "clz(1) == 31");
    CHECK(__builtin_clz(0x80000000U) == 0, "clz(1<<31) == 0");
    CHECK(__builtin_ctz(0x80000000U) == 31, "ctz(1<<31) == 31");
    CHECK(__builtin_ctz(1) == 0, "ctz(1) == 0");

    CHECK(__builtin_clzll(1ULL) == 63, "clzll(1) == 63");
    CHECK(__builtin_clzll(0x8000000000000000ULL) == 0, "clzll(1<<63) == 0");
    CHECK(__builtin_ctzll(0x100000000ULL) == 32, "ctzll cross 32-bit boundary");
}

// ---- Mixed 32/64-bit struct layout ----

struct Mixed {
    uint32_t a;
    uint64_t b;
    uint32_t c;
    void *d;
};

static void test_struct_layout(void) {
    CHECK(sizeof(void *) == 8, "64-bit pointers");
    CHECK(sizeof(struct Mixed) == 32, "mixed struct size");

    struct Mixed m = { .a = 0xCAFE, .b = 0x0102030405060708ULL,
                       .c = 0xBEEF, .d = &m };
    CHECK(m.a == 0xCAFE, "struct 32-bit field a");
    CHECK(m.b == 0x0102030405060708ULL, "struct 64-bit field b");
    CHECK(m.c == 0xBEEF, "struct 32-bit field c");
    CHECK(m.d == &m, "struct ptr field d");
}

// ---- 32-bit atomic in 64-bit mode ----

static void test_atomic32(void) {
    volatile uint32_t atom = 0;
    __atomic_store_n(&atom, 0x1234, __ATOMIC_SEQ_CST);
    CHECK(__atomic_load_n(&atom, __ATOMIC_SEQ_CST) == 0x1234, "atomic32 store");

    uint32_t old = __atomic_fetch_add(&atom, 1, __ATOMIC_SEQ_CST);
    CHECK(old == 0x1234, "atomic32 fetch_add old");
    CHECK(__atomic_load_n(&atom, __ATOMIC_SEQ_CST) == 0x1235, "atomic32 new");
}

// ---- 64-bit atomic ----

static void test_atomic64(void) {
    volatile uint64_t atom = 0;
    __atomic_store_n(&atom, 0x100000000ULL, __ATOMIC_SEQ_CST);
    CHECK(__atomic_load_n(&atom, __ATOMIC_SEQ_CST) == 0x100000000ULL,
          "atomic64 store");

    uint64_t expected = 0x100000000ULL;
    int ok = __atomic_compare_exchange_n(&atom, &expected, 0xDEADULL, 0,
                                          __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    CHECK(ok, "atomic64 CAS ok");
    CHECK(__atomic_load_n(&atom, __ATOMIC_SEQ_CST) == 0xDEADULL, "atomic64 CAS val");
}

// ---- Inline asm: W register (AArch64) or EAX (x86_64) ----

#if defined(__aarch64__)
static void test_arch_asm(void) {
    uint64_t result;
    __asm__ __volatile__(
        "mov x0, #-1\n\t"
        "mov w0, #0x42\n\t"
        "mov %0, x0"
        : "=r"(result)
        :
        : "x0");
    CHECK(result == 0x42ULL, "mov w0 clears upper 32 bits of x0");
}
#elif defined(__x86_64__)
static void test_arch_asm(void) {
    uint64_t result;
    __asm__ __volatile__(
        "movq $-1, %%rax\n\t"
        "movl $0x42, %%eax\n\t"
        "movq %%rax, %0"
        : "=r"(result)
        :
        : "rax");
    CHECK(result == 0x42ULL, "movl eax clears upper 32 bits of rax");
}
#endif

int main(void) {
    test_w_alu();
    test_w_extension();
    test_trunc();
    test_csel();
    test_clz_ctz();
    test_struct_layout();
    test_atomic32();
    test_atomic64();
    test_arch_asm();

    printf("PASS: aarch64_cleanup_validation\n");
    return 0;
}
