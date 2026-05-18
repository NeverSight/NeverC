// test_arch64_with_32bit_ops.c
// Validates that 32-bit sub-operations work correctly in 64-bit-only mode
// after all 32-bit architecture dead code has been removed.
//
// x86_64: EAX/EBX/etc. are the low 32 bits of RAX/RBX, and i32 operations
//   (MOV32, ADD32, XOR32, LEA with 32-bit operands, etc.) are fully legal.
// AArch64: W0-W30 are the low 32 bits of X0-X30, and 32-bit ALU ops are legal.
//
// RUN: %neverc -O2 %s -o %t && %t && echo "PASS: arch64_with_32bit_ops"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
        abort(); \
    } \
} while (0)

// ===== Section 1: 32-bit integer arithmetic in 64-bit mode =====

static uint32_t rotate_left_32(uint32_t x, int n) {
    return (x << n) | (x >> (32 - n));
}

static uint32_t rotate_right_32(uint32_t x, int n) {
    return (x >> n) | (x << (32 - n));
}

static void test_32bit_arithmetic(void) {
    uint32_t a = 0xDEADBEEF;
    uint32_t b = 0xCAFEBABE;

    ASSERT(a + b == 0xA9AC79AD, "u32 add");
    ASSERT(a - b == 0x13AF0431, "u32 sub");
    ASSERT(a * b == 0x88CF5B62, "u32 mul");
    ASSERT(a / 7 == 0x1FCFAD8F, "u32 div");
    ASSERT(a % 7 == 6, "u32 mod");

    ASSERT((a & b) == 0xCAACBAAE, "u32 and");
    ASSERT((a | b) == 0xDEFFBEFF, "u32 or");
    ASSERT((a ^ b) == 0x14530451, "u32 xor");
    ASSERT(~a == 0x21524110, "u32 not");

    ASSERT(rotate_left_32(a, 8) == 0xADBEEFDE, "u32 rotl");
    ASSERT(rotate_right_32(a, 8) == 0xEFDEADBE, "u32 rotr");

    int32_t sa = -42;
    int32_t sb = 17;
    ASSERT(sa + sb == -25, "i32 add signed");
    ASSERT(sa * sb == -714, "i32 mul signed");
    ASSERT(sa / sb == -2, "i32 div signed");
    ASSERT(sa >> 3 == -6, "i32 arith shift right");
}

// ===== Section 2: 64-bit operations that depend on 32-bit sub-regs =====

static void test_64bit_with_32bit_parts(void) {
    uint64_t val = 0x0000000100000002ULL;
    uint32_t lo = (uint32_t)val;
    uint32_t hi = (uint32_t)(val >> 32);

    ASSERT(lo == 2, "u64 low 32 extract");
    ASSERT(hi == 1, "u64 high 32 extract");

    uint64_t combined = ((uint64_t)hi << 32) | lo;
    ASSERT(combined == val, "u64 combine from u32 parts");

    // Zero-extension: writing to a 32-bit register clears upper 32 bits on x86_64
    uint64_t zext_test = 0xFFFFFFFFFFFFFFFFULL;
    uint32_t trunc = (uint32_t)zext_test;
    uint64_t rezext = trunc;
    ASSERT(rezext == 0xFFFFFFFF, "u32 zero-extension to u64");

    // Sign-extension
    int32_t neg = -1;
    int64_t sext = neg;
    ASSERT(sext == -1LL, "i32 sign-extension to i64");
    ASSERT((uint64_t)sext == 0xFFFFFFFFFFFFFFFFULL, "i32 sext bit pattern");
}

// ===== Section 3: 32-bit pointer arithmetic patterns =====

static void test_pointer_arithmetic(void) {
    int arr[16];
    for (int i = 0; i < 16; i++)
        arr[i] = i * i;

    // Array indexing with 32-bit index (compiler uses LEA/addressing modes)
    uint32_t idx = 7;
    ASSERT(arr[idx] == 49, "array index with u32");

    // Pointer difference (64-bit, but element count fits in 32 bits)
    int *p1 = &arr[3];
    int *p2 = &arr[11];
    long diff = p2 - p1;
    ASSERT(diff == 8, "pointer diff");
    ASSERT((uint32_t)diff == 8, "pointer diff truncated to u32");

    // Pointer with 32-bit offset
    int32_t offset = 5;
    int *p3 = arr + offset;
    ASSERT(*p3 == 25, "pointer + i32 offset");
}

// ===== Section 4: Struct packing with mixed 32/64-bit fields =====

struct Mixed {
    uint32_t a;
    uint64_t b;
    uint32_t c;
    uint16_t d;
    uint8_t  e;
};

struct __attribute__((packed)) Packed {
    uint32_t a;
    uint64_t b;
    uint32_t c;
};

static void test_mixed_struct(void) {
    struct Mixed m = {0xAABBCCDD, 0x1122334455667788ULL, 0x99887766, 0xFFEE, 0xAB};

    ASSERT(m.a == 0xAABBCCDD, "struct u32 field");
    ASSERT(m.b == 0x1122334455667788ULL, "struct u64 field");
    ASSERT(m.c == 0x99887766, "struct u32 field 2");
    ASSERT(m.d == 0xFFEE, "struct u16 field");
    ASSERT(m.e == 0xAB, "struct u8 field");

    ASSERT(sizeof(struct Mixed) >= 20, "struct Mixed size >= 20");

    struct Packed p = {0x11223344, 0xAABBCCDDEEFF0011ULL, 0x55667788};
    ASSERT(sizeof(struct Packed) == 16, "packed struct size == 16");
    ASSERT(p.a == 0x11223344, "packed u32 field");
    ASSERT(p.b == 0xAABBCCDDEEFF0011ULL, "packed u64 field");
    ASSERT(p.c == 0x55667788, "packed u32 field 2");
}

// ===== Section 5: Inline assembly with 32-bit sub-registers =====

#if defined(__x86_64__)
static void test_x86_64_32bit_subregs(void) {
    // Use EAX (32-bit) in 64-bit mode — must zero-extend to RAX
    uint32_t result;
    __asm__ __volatile__("movl $42, %%eax" : "=a"(result));
    ASSERT(result == 42, "x86_64: movl imm to eax");

    // XOR EAX,EAX — canonical zero-register idiom in x86_64
    uint64_t zeroed;
    __asm__ __volatile__("xorl %%eax, %%eax" : "=a"(zeroed));
    ASSERT(zeroed == 0, "x86_64: xor eax,eax zeros rax");

    // LEA with 32-bit operand size
    uint32_t a = 10, b = 20, sum;
    __asm__("leal (%1, %2), %0" : "=r"(sum) : "r"(a), "r"(b));
    ASSERT(sum == 30, "x86_64: leal with 32-bit regs");

    // BSR/BSF — bit scan on 32-bit register
    uint32_t val = 0x00800000;
    uint32_t bsr_result;
    __asm__("bsrl %1, %0" : "=r"(bsr_result) : "r"(val));
    ASSERT(bsr_result == 23, "x86_64: bsrl (bit scan reverse 32-bit)");

    // POPCNT if available (falls back to software count if not)
    uint32_t popcount_val = 0xFF00FF00;
    uint32_t popcount_expected = 16;
    uint32_t popcount_result = __builtin_popcount(popcount_val);
    ASSERT(popcount_result == popcount_expected, "x86_64: __builtin_popcount (32-bit)");

    // 64-bit specific: RDTSC returns 64-bit timestamp
    uint64_t tsc;
    __asm__ __volatile__("rdtsc; shlq $32, %%rdx; orq %%rdx, %%rax"
                         : "=a"(tsc) : : "rdx");
    ASSERT(tsc != 0, "x86_64: rdtsc produces nonzero timestamp");
}
#endif

#if defined(__aarch64__)
static void test_aarch64_32bit_subregs(void) {
    // Use W register (32-bit) in AArch64 mode
    uint32_t result;
    __asm__ __volatile__("mov %w0, #42" : "=r"(result));
    ASSERT(result == 42, "aarch64: mov w-reg immediate");

    // 32-bit add with W registers
    uint32_t a = 100, b = 200, sum;
    __asm__("add %w0, %w1, %w2" : "=r"(sum) : "r"(a), "r"(b));
    ASSERT(sum == 300, "aarch64: add w-regs");

    // 64-bit operation with X registers
    uint64_t x = 0x1234567890ABCDEFULL, y;
    __asm__("mov %0, %1" : "=r"(y) : "r"(x));
    ASSERT(y == x, "aarch64: mov x-reg 64-bit");

    // UXTH — zero-extend halfword (16-bit to 32-bit)
    uint32_t val16 = 0xFFFF1234;
    uint32_t extended;
    __asm__("uxth %w0, %w1" : "=r"(extended) : "r"(val16));
    ASSERT(extended == 0x1234, "aarch64: uxth zero-extend 16->32");

    // UXTB — zero-extend byte (8-bit to 32-bit)
    uint32_t val8 = 0xFFFFFFAB;
    uint32_t byteval;
    __asm__("uxtb %w0, %w1" : "=r"(byteval) : "r"(val8));
    ASSERT(byteval == 0xAB, "aarch64: uxtb zero-extend 8->32");

    // CLZ on 32-bit register
    uint32_t clz_val = 0x00010000;
    uint32_t clz_result;
    __asm__("clz %w0, %w1" : "=r"(clz_result) : "r"(clz_val));
    ASSERT(clz_result == 15, "aarch64: clz w-reg");

    // REV (byte-reverse) on 32-bit register
    uint32_t rev_val = 0x12345678;
    uint32_t rev_result;
    __asm__("rev %w0, %w1" : "=r"(rev_result) : "r"(rev_val));
    ASSERT(rev_result == 0x78563412, "aarch64: rev w-reg");

    // MRS — read system counter (64-bit specific)
    uint64_t cntvct;
    __asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(cntvct));
    ASSERT(cntvct != 0, "aarch64: mrs cntvct_el0 nonzero");
}
#endif

// ===== Section 6: Compiler builtins that use 32-bit sub-operations =====

static void test_builtins_32bit(void) {
    ASSERT(__builtin_clz(1) == 31, "clz(1) == 31");
    ASSERT(__builtin_clz(0x80000000U) == 0, "clz(0x80000000) == 0");
    ASSERT(__builtin_ctz(0x80000000U) == 31, "ctz(0x80000000) == 31");
    ASSERT(__builtin_ctz(1) == 0, "ctz(1) == 0");
    ASSERT(__builtin_popcount(0xAAAAAAAA) == 16, "popcount(0xAAAAAAAA) == 16");
    ASSERT(__builtin_popcount(0) == 0, "popcount(0) == 0");
    ASSERT(__builtin_parity(0xFF) == 0, "parity(0xFF) == 0");
    ASSERT(__builtin_parity(0x7F) == 1, "parity(0x7F) == 1");

    // 64-bit builtins
    ASSERT(__builtin_clzll(1ULL) == 63, "clzll(1) == 63");
    ASSERT(__builtin_ctzll(0x8000000000000000ULL) == 63, "ctzll(1<<63) == 63");
    ASSERT(__builtin_popcountll(0xAAAAAAAAAAAAAAAAULL) == 32, "popcountll == 32");

    // bswap
    ASSERT(__builtin_bswap32(0x12345678) == 0x78563412, "bswap32");
    ASSERT(__builtin_bswap64(0x0102030405060708ULL) == 0x0807060504030201ULL, "bswap64");
}

// ===== Section 7: Volatile 32-bit memory access =====

static volatile uint32_t g_vol32 = 0;
static volatile uint64_t g_vol64 = 0;

static void test_volatile_access(void) {
    g_vol32 = 0xBAADF00D;
    uint32_t v32 = g_vol32;
    ASSERT(v32 == 0xBAADF00D, "volatile u32 store/load");

    g_vol64 = 0xDEADCAFEBEEF1234ULL;
    uint64_t v64 = g_vol64;
    ASSERT(v64 == 0xDEADCAFEBEEF1234ULL, "volatile u64 store/load");

    g_vol32 = 0;
    for (int i = 0; i < 32; i++)
        g_vol32 |= (1U << i);
    ASSERT(g_vol32 == 0xFFFFFFFF, "volatile u32 bit-by-bit set");
}

// ===== Section 8: Atomic 32-bit operations =====

static void test_atomics_32bit(void) {
    volatile uint32_t a32 = 0;
    __atomic_store_n(&a32, 42, __ATOMIC_SEQ_CST);
    ASSERT(__atomic_load_n(&a32, __ATOMIC_SEQ_CST) == 42, "atomic u32 store/load");

    uint32_t old = __atomic_exchange_n(&a32, 100, __ATOMIC_SEQ_CST);
    ASSERT(old == 42, "atomic u32 exchange old");
    ASSERT(__atomic_load_n(&a32, __ATOMIC_SEQ_CST) == 100, "atomic u32 exchange new");

    uint32_t expected = 100;
    int ok = __atomic_compare_exchange_n(&a32, &expected, 200,
                                         0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    ASSERT(ok, "atomic u32 CAS success");
    ASSERT(__atomic_load_n(&a32, __ATOMIC_SEQ_CST) == 200, "atomic u32 CAS result");

    __atomic_fetch_add(&a32, 55, __ATOMIC_SEQ_CST);
    ASSERT(__atomic_load_n(&a32, __ATOMIC_SEQ_CST) == 255, "atomic u32 fetch_add");

    // 64-bit atomics
    volatile uint64_t a64 = 0;
    __atomic_store_n(&a64, 0xDEADBEEFCAFEBABEULL, __ATOMIC_SEQ_CST);
    ASSERT(__atomic_load_n(&a64, __ATOMIC_SEQ_CST) == 0xDEADBEEFCAFEBABEULL,
           "atomic u64 store/load");
}

// ===== Section 9: Cross-compile syntax check (AArch64 from x86_64 host) =====
// This section compiles on both architectures; the inline asm sections
// above are guarded by __x86_64__ / __aarch64__.

int main(void) {
    test_32bit_arithmetic();
    test_64bit_with_32bit_parts();
    test_pointer_arithmetic();
    test_mixed_struct();
    test_builtins_32bit();
    test_volatile_access();
    test_atomics_32bit();

#if defined(__x86_64__)
    test_x86_64_32bit_subregs();
#elif defined(__aarch64__)
    test_aarch64_32bit_subregs();
#endif

    printf("test_arch64_with_32bit_ops: ALL PASSED\n");
    return 0;
}
