// Validates that 32-bit architecture cleanup hasn't broken 64-bit code generation.
// Tests cover:
//   1. Mixed 32/64-bit integer operations (EAX/W0 sub-register usage)
//   2. Function calls with mixed-width arguments
//   3. Struct layout with mixed-width fields
//   4. Atomic operations on 32-bit values (uses CMPXCHG on x86_64, LDXR/STXR on AArch64)
//   5. Bitwise operations that lower to 32-bit instructions
//   6. LEA-generating patterns (shift+add, validates classifyLEAReg fix)
//   7. 64-bit pointer width and addressing
//   8. Stack alignment (validates 16-byte default after 32-bit Solaris path removed)
//
// RUN: %neverc -O2 %s -o %t && %t && echo "PASS: 64bit_only_cleanup"

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

static volatile int prevent_opt = 0;

static uint32_t mix_widths(uint32_t a32, uint64_t b64) {
    uint64_t wide = (uint64_t)a32 * b64;
    return (uint32_t)(wide >> 32);
}

static void test_mixed_width_arithmetic(void) {
    CHECK(mix_widths(0xFFFFFFFF, 0x100000001ULL) == 0xFFFFFFFF, "mix_widths high");
    CHECK(mix_widths(2, 0x80000000ULL) == 1, "mix_widths 2*2G high");

    uint32_t narrow = 0xDEADBEEF;
    uint64_t wide = narrow;
    CHECK(wide == 0xDEADBEEFULL, "u32->u64 zero-extend");

    int32_t sneg = -42;
    int64_t swide = sneg;
    CHECK(swide == -42LL, "i32->i64 sign-extend");
    CHECK((uint64_t)swide == 0xFFFFFFFFFFFFFFD6ULL, "i32 sext bit pattern");
}

static void test_function_call_abi(void) {
    volatile uint32_t a = 0x12345678;
    volatile uint64_t b = 0xABCDEF0123456789ULL;
    volatile uint32_t c = 0x9ABCDEF0;

    uint32_t r = mix_widths(a, b);
    CHECK(r != 0 || prevent_opt, "cross-width call produced result");

    char buf[32];
    int n = snprintf(buf, sizeof(buf), "%u", (unsigned)a);
    CHECK(n == 9, "snprintf u32");
    CHECK(strcmp(buf, "305419896") == 0, "snprintf u32 value");
    (void)c;
}

struct Packed64 {
    uint32_t tag;
    uint64_t payload;
    uint32_t checksum;
} __attribute__((packed));

static void test_packed_struct_access(void) {
    struct Packed64 p;
    memset(&p, 0, sizeof(p));
    p.tag = 0xCAFE;
    p.payload = 0x0102030405060708ULL;
    p.checksum = 0xBEEF;

    CHECK(sizeof(struct Packed64) == 16, "packed struct size");
    CHECK(p.tag == 0xCAFE, "packed tag");
    CHECK(p.payload == 0x0102030405060708ULL, "packed payload");
    CHECK(p.checksum == 0xBEEF, "packed checksum");
}

static void test_atomic_32bit_in_64bit_mode(void) {
    volatile uint32_t atom = 0;
    __atomic_store_n(&atom, 0x42, __ATOMIC_SEQ_CST);
    CHECK(__atomic_load_n(&atom, __ATOMIC_SEQ_CST) == 0x42, "atomic32 store/load");

    uint32_t old = __atomic_fetch_add(&atom, 0x10, __ATOMIC_SEQ_CST);
    CHECK(old == 0x42, "atomic32 fetch_add old");
    CHECK(__atomic_load_n(&atom, __ATOMIC_SEQ_CST) == 0x52, "atomic32 fetch_add new");

    uint32_t expected = 0x52;
    int ok = __atomic_compare_exchange_n(&atom, &expected, 0xFF,
                                          0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    CHECK(ok, "atomic32 CAS success");
    CHECK(__atomic_load_n(&atom, __ATOMIC_SEQ_CST) == 0xFF, "atomic32 CAS result");
}

static void test_bitwise_32bit(void) {
    volatile uint32_t x = 0xAAAA5555;
    CHECK(__builtin_popcount(x) == 16, "popcount32");
    CHECK(__builtin_clz(x) == 0, "clz32 high bit set");
    CHECK(__builtin_ctz(1U) == 0, "ctz32(1)");
    CHECK(__builtin_bswap32(0x01020304) == 0x04030201, "bswap32");
    (void)x;
}

static void test_lea_patterns(void) {
    volatile int32_t base = 10;
    int32_t v = base;

    int32_t shl1 = v * 2;
    int32_t shl2 = v * 4;
    int32_t shl3 = v * 8;
    CHECK(shl1 == 20, "lea: v*2");
    CHECK(shl2 == 40, "lea: v*4");
    CHECK(shl3 == 80, "lea: v*8");

    int32_t add_shl = v + v * 4;
    CHECK(add_shl == 50, "lea: v+v*4");

    int32_t add_shl_disp = v * 2 + 7;
    CHECK(add_shl_disp == 27, "lea: v*2+7");

    int32_t inc = v + 1;
    int32_t dec = v - 1;
    CHECK(inc == 11, "lea: inc32");
    CHECK(dec == 9, "lea: dec32");
}

static void test_pointer_64bit(void) {
    CHECK(sizeof(void *) == 8, "pointer is 64-bit");
    CHECK(sizeof(size_t) == 8, "size_t is 64-bit");
    CHECK(sizeof(intptr_t) == 8, "intptr_t is 64-bit");

    int local;
    intptr_t addr = (intptr_t)&local;
    CHECK(addr != 0, "stack address nonzero");

    int arr[4] = {10, 20, 30, 40};
    int *p = arr;
    CHECK(*(p + 3) == 40, "ptr arith on 64-bit");
    CHECK((char *)(p + 1) - (char *)p == (ptrdiff_t)sizeof(int), "ptr diff");
}

static void test_stack_alignment(void) {
    volatile char buf[1];
    buf[0] = 0;
    intptr_t sp_approx = (intptr_t)buf;
    (void)sp_approx;

    typedef struct { char c; } __attribute__((aligned(16))) Aligned16;
    Aligned16 a;
    a.c = 42;
    CHECK(((intptr_t)&a & 0xF) == 0, "16-byte aligned local");
}

#if defined(__x86_64__)
static void test_x86_64_specific(void) {
    uint32_t eax_val;
    __asm__ __volatile__("movl $0xDEAD, %%eax" : "=a"(eax_val));
    CHECK(eax_val == 0xDEAD, "x86_64: movl to eax");

    uint64_t rax_val;
    __asm__ __volatile__("xorl %%eax, %%eax" : "=a"(rax_val));
    CHECK(rax_val == 0, "x86_64: xor eax clears rax");

    uint64_t rdx_hi;
    __asm__ __volatile__(
        "movq $0xFFFFFFFF00000000, %%rdx\n\t"
        "movl $0x1, %%edx"
        : "=d"(rdx_hi));
    CHECK(rdx_hi == 1, "x86_64: movl to edx zero-extends to rdx");
}
#elif defined(__aarch64__)
static void test_aarch64_specific(void) {
    uint32_t w_val;
    __asm__ __volatile__("mov %w0, #0xBEEF" : "=r"(w_val));
    CHECK(w_val == 0xBEEF, "aarch64: mov w-reg");

    uint32_t a = 100, b = 200, sum;
    __asm__("add %w0, %w1, %w2" : "=r"(sum) : "r"(a), "r"(b));
    CHECK(sum == 300, "aarch64: add w-regs");

    uint64_t x_val;
    __asm__ __volatile__(
        "movz %x0, #0xFFFF, lsl #48\n\t"
        "mov %w0, #1"
        : "=r"(x_val));
    CHECK(x_val == 1, "aarch64: mov w0 zero-extends to x0");
}
#endif

int main(void) {
    test_mixed_width_arithmetic();
    test_function_call_abi();
    test_packed_struct_access();
    test_atomic_32bit_in_64bit_mode();
    test_bitwise_32bit();
    test_lea_patterns();
    test_pointer_64bit();
    test_stack_alignment();

#if defined(__x86_64__)
    test_x86_64_specific();
#elif defined(__aarch64__)
    test_aarch64_specific();
#endif

    printf("test_64bit_only_cleanup: ALL PASSED\n");
    return 0;
}
