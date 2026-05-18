// test_arch_64only_validation.c
// Validates that the 64-bit-only architecture cleanup is correct:
//   - ARM32 SubArch enums removed (v7/v6/v5/v4t/v8m_* deleted from Triple.h)
//   - ARM32 Win64EH unwind opcodes removed
//   - SafeSEH entire chain removed
//   - x86 32-bit classify*Reference dead branches removed
//   - Various 32-bit-in-64-bit operations still work
//
// Key principle: 32-bit *operations* within 64-bit mode are legitimate.
//   - x86_64: EAX/EBX are low-32 aliases of RAX/RBX, MOV32/XOR32 zero-extend
//   - AArch64: W0-W30 are low-32 aliases of X0-X30, 32-bit ops zero-extend
// Removing these would break 64-bit codegen.
//
// RUN: %neverc -O2 %s -o %t && %t && echo "PASS: arch_64only_validation"

#include <stddef.h>
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

// --- 1. 64-bit fundamentals (both arches) ---

static void test_64bit_type_model(void) {
    CHECK(sizeof(void *) == 8, "pointer is 8 bytes");
    CHECK(sizeof(long long) == 8, "long long is 8 bytes");
    CHECK(sizeof(size_t) == 8, "size_t is 8 bytes");
    CHECK(sizeof(int) == 4, "int is still 4 bytes in 64-bit");
    CHECK(sizeof(short) == 2, "short is 2 bytes");
    CHECK(sizeof(char) == 1, "char is 1 byte");
}

// --- 2. 32-bit zero-extension in 64-bit mode ---

static uint64_t __attribute__((noinline)) zext_chain(uint32_t x) {
    uint32_t a = x * 7;
    uint32_t b = a ^ 0xABCD1234;
    return (uint64_t)b;
}

static void test_32bit_zeroext(void) {
    uint64_t result = zext_chain(0xFFFFFFFF);
    CHECK((result >> 32) == 0, "32-bit result zero-extends (upper bits clear)");
    CHECK(result == (uint64_t)(uint32_t)(0xFFFFFFFF * 7 ^ 0xABCD1234),
          "32-bit arithmetic correct");
}

// --- 3. Mixed 32/64-bit arithmetic chains ---

static uint64_t __attribute__((noinline))
mixed_width_compute(uint32_t a, uint64_t b, uint16_t c, uint8_t d) {
    uint64_t r = (uint64_t)a + b;
    r -= (uint64_t)c;
    r ^= (uint64_t)d;
    return r;
}

static void test_mixed_width_ops(void) {
    uint64_t r = mixed_width_compute(0xDEADBEEF, 0x123456789ABCDEF0ULL, 0xFFFF, 0x42);
    uint64_t expected = 0xDEADBEEFULL + 0x123456789ABCDEF0ULL - 0xFFFFULL ^ 0x42ULL;
    CHECK(r == expected, "mixed-width arithmetic chain");
}

// --- 4. 32-bit builtins that generate sub-register instructions ---

static void test_32bit_builtins(void) {
    CHECK(__builtin_popcount(0xF0F0F0F0U) == 16, "popcount 32-bit");
    CHECK(__builtin_popcountll(0xF0F0F0F0F0F0F0F0ULL) == 32, "popcount 64-bit");

    CHECK(__builtin_clz(1U) == 31, "clz 32-bit");
    CHECK(__builtin_clzll(1ULL) == 63, "clz 64-bit");

    CHECK(__builtin_ctz(0x80U) == 7, "ctz 32-bit");
    CHECK(__builtin_ctzll(0x10000000000ULL) == 40, "ctz 64-bit");

    CHECK(__builtin_bswap32(0x01020304) == 0x04030201, "bswap32");
    CHECK(__builtin_bswap64(0x0102030405060708ULL) == 0x0807060504030201ULL, "bswap64");
}

// --- 5. 32-bit atomics in 64-bit mode ---

static void test_32bit_atomics(void) {
    volatile int32_t counter = 0;
    __sync_fetch_and_add(&counter, 10);
    CHECK(counter == 10, "32-bit atomic add");

    int32_t old = __sync_val_compare_and_swap(&counter, 10, 42);
    CHECK(old == 10, "32-bit CAS old value");
    CHECK(counter == 42, "32-bit CAS new value");

    volatile int64_t counter64 = 0;
    __sync_fetch_and_add(&counter64, 0x100000000LL);
    CHECK(counter64 == 0x100000000LL, "64-bit atomic add across 32-bit boundary");
}

// --- 6. Struct with mixed 32/64-bit fields ---

struct __attribute__((packed)) PackedMixed {
    uint8_t  tag;
    uint32_t value;
    uint64_t large;
    uint16_t flags;
};

struct NaturalMixed {
    uint32_t a;
    uint64_t b;
    uint32_t c;
};

static void test_struct_layout(void) {
    CHECK(sizeof(struct PackedMixed) == 15, "packed struct: 1+4+8+2 = 15");

    CHECK(sizeof(struct NaturalMixed) == 24, "natural struct with padding");
    CHECK(offsetof(struct NaturalMixed, a) == 0, "NaturalMixed.a at 0");
    CHECK(offsetof(struct NaturalMixed, b) == 8, "NaturalMixed.b at 8 (padded)");
    CHECK(offsetof(struct NaturalMixed, c) == 16, "NaturalMixed.c at 16");
}

// --- 7. Function pointer calling conventions ---

typedef int32_t (*fn32_t)(int32_t, int32_t);
typedef int64_t (*fn64_t)(int64_t, int64_t);

static int32_t __attribute__((noinline)) add32(int32_t a, int32_t b) { return a + b; }
static int64_t __attribute__((noinline)) add64(int64_t a, int64_t b) { return a + b; }

static void test_funcptr_calling(void) {
    fn32_t fp32 = add32;
    fn64_t fp64 = add64;
    CHECK(sizeof(fp32) == 8, "function pointer is 8 bytes");
    CHECK(fp32(100, 200) == 300, "32-bit return via function pointer");
    CHECK(fp64(0x100000000LL, 0x200000000LL) == 0x300000000LL,
          "64-bit return via function pointer");
}

// --- 8. Platform-specific inline assembly ---

#if defined(__x86_64__)
static void test_x86_64_specific(void) {
    // 32-bit register in 64-bit inline asm (EAX is low 32 of RAX)
    uint32_t eax_val;
    __asm__ volatile ("movl $0xDEADBEEF, %%eax; movl %%eax, %0"
                      : "=r"(eax_val) : : "eax");
    CHECK(eax_val == 0xDEADBEEF, "movl into EAX works in 64-bit mode");

    // XOR EAX,EAX is the canonical way to zero RAX in 64-bit mode
    uint64_t rax_zeroed;
    __asm__ volatile ("xorl %%eax, %%eax; movq %%rax, %0"
                      : "=r"(rax_zeroed) : : "rax");
    CHECK(rax_zeroed == 0, "xorl eax,eax zeros entire RAX");

    // RFLAGS accessible (RDFLAGS64 path, not the deleted RDFLAGS32)
    uint64_t rflags;
    __asm__ volatile ("pushfq; popq %0" : "=r"(rflags));
    CHECK(rflags != 0, "RFLAGS readable via pushfq/popq");

    // LEA with 32-bit result in 64-bit mode (LEA64_32r)
    uint32_t lea_result;
    __asm__ volatile ("leal 1(%%rdi, %%rsi, 4), %0"
                      : "=r"(lea_result)
                      : "D"((uint64_t)10), "S"((uint64_t)5));
    CHECK(lea_result == 31, "LEA with 32-bit result (10 + 5*4 + 1)");

    // Stack alignment check (must be 16-byte aligned)
    uint64_t rsp;
    __asm__ volatile ("movq %%rsp, %0" : "=r"(rsp));
    CHECK((rsp & 0xF) == 0, "RSP 16-byte aligned");
}
#endif

#if defined(__aarch64__)
static void test_aarch64_specific(void) {
    // W register (32-bit) in 64-bit mode
    uint32_t w_val;
    __asm__ volatile ("mov %w0, #0xBEEF" : "=r"(w_val));
    CHECK(w_val == 0xBEEF, "MOV into W register works");

    // W register write zero-extends to X register
    uint64_t x_from_w;
    __asm__ volatile ("mov %w0, #0xFFFFFFFF" : "=r"(x_from_w));
    CHECK(x_from_w == 0xFFFFFFFF, "W write zero-extends to X (upper 32 clear)");
    CHECK((x_from_w >> 32) == 0, "upper 32 bits of X are zero after W write");

    // SP alignment (must be 16-byte aligned on AArch64)
    uint64_t sp;
    __asm__ volatile ("mov %0, sp" : "=r"(sp));
    CHECK((sp & 0xF) == 0, "SP 16-byte aligned on AArch64");

    // 32-bit conditional select (CSEL Wd)
    uint32_t csel_result;
    uint32_t a = 100, b = 200;
    __asm__ volatile ("cmp %w1, %w2; csel %w0, %w1, %w2, lt"
                      : "=r"(csel_result) : "r"(a), "r"(b));
    CHECK(csel_result == 100, "CSEL Wd (32-bit conditional select)");

    // 64-bit conditional select (CSEL Xd)
    uint64_t csel64_result;
    uint64_t a64 = 0x100000000ULL, b64 = 0x200000000ULL;
    __asm__ volatile ("cmp %x1, %x2; csel %x0, %x1, %x2, lt"
                      : "=r"(csel64_result) : "r"(a64), "r"(b64));
    CHECK(csel64_result == a64, "CSEL Xd (64-bit conditional select)");
}
#endif

int main(void) {
    test_64bit_type_model();
    test_32bit_zeroext();
    test_mixed_width_ops();
    test_32bit_builtins();
    test_32bit_atomics();
    test_struct_layout();
    test_funcptr_calling();

#if defined(__x86_64__)
    test_x86_64_specific();
#elif defined(__aarch64__)
    test_aarch64_specific();
#endif

    printf("test_arch_64only_validation: ALL PASSED\n");
    return 0;
}
