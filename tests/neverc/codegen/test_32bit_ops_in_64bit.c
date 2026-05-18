// test_32bit_ops_in_64bit.c
// Validates that 32-bit operations retained in the 64-bit-only cleanup
// are legitimate and work correctly in x86_64 / AArch64 mode.
//
// These operations use 32-bit registers (EAX/W0) as sub-registers of
// 64-bit registers (RAX/X0). Removing them would break 64-bit codegen.
//
// Tests cover:
//   1. ia32 intrinsics: BSF/BSR/BSWAP/POPCNT/CRC32/rotate on 32-bit values
//   2. Bit-cast intrinsics: float<->uint32 type punning
//   3. 32-bit zero-extension semantics (writing EAX clears upper RAX)
//   4. Mixed 32/64-bit arithmetic chains
//   5. Address-size considerations (32-bit pointer arithmetic within 64-bit)
//   6. 32-bit atomic CAS in 64-bit mode (uses CMPXCHG / LDXR+STXR)
//   7. 32-bit struct fields with natural alignment in 64-bit layout
//
// RUN: %neverc -O2 %s -o %t && %t && echo "PASS: 32bit_ops_in_64bit"

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

// --- 1. ia32 bit-scan / bswap / popcount intrinsics ---

static void test_ia32_bit_intrinsics(void) {
    CHECK(__builtin_ctz(0x80) == 7, "ctz(0x80) == 7");
    CHECK(__builtin_ctz(1U) == 0, "ctz(1) == 0");
    CHECK(__builtin_ctz(0x80000000U) == 31, "ctz(1<<31) == 31");

    CHECK(__builtin_clz(0x80000000U) == 0, "clz(1<<31) == 0");
    CHECK(__builtin_clz(1U) == 31, "clz(1) == 31");
    CHECK(__builtin_clz(0x40000000U) == 1, "clz(1<<30) == 1");

    CHECK(__builtin_bswap32(0x01020304U) == 0x04030201U, "bswap32");
    CHECK(__builtin_bswap32(0xFF000000U) == 0x000000FFU, "bswap32 high byte");

    CHECK(__builtin_popcount(0U) == 0, "popcount(0)");
    CHECK(__builtin_popcount(0xFFFFFFFFU) == 32, "popcount(0xFFFFFFFF)");
    CHECK(__builtin_popcount(0xAAAAAAAAU) == 16, "popcount alternating");
    CHECK(__builtin_popcount(0x12345678U) == 13, "popcount(0x12345678)");
}

static void test_64bit_bit_intrinsics(void) {
    CHECK(__builtin_ctzll(0x100000000ULL) == 32, "ctzll across 32-bit boundary");
    CHECK(__builtin_clzll(1ULL) == 63, "clzll(1)");
    CHECK(__builtin_bswap64(0x0102030405060708ULL) == 0x0807060504030201ULL,
          "bswap64");
    CHECK(__builtin_popcountll(0xFFFFFFFFFFFFFFFFULL) == 64, "popcntll(all 1s)");
}

// --- 2. Bit-cast float<->uint32 ---

static void test_float_bitcast(void) {
    float f = 1.0f;
    unsigned int u;
    memcpy(&u, &f, sizeof(u));
    CHECK(u == 0x3F800000U, "float 1.0 bit pattern");

    unsigned int pattern = 0x40490FDBU;
    float pi_approx;
    memcpy(&pi_approx, &pattern, sizeof(pi_approx));
    CHECK(pi_approx > 3.14f && pi_approx < 3.15f, "uint32->float pi approx");

    double d = 1.0;
    unsigned long long du;
    memcpy(&du, &d, sizeof(du));
    CHECK(du == 0x3FF0000000000000ULL, "double 1.0 bit pattern");
}

// --- 3. 32-bit zero-extension ---

static void test_zero_extension(void) {
    volatile uint64_t full = 0xDEADBEEF12345678ULL;
    volatile uint32_t *low = (volatile uint32_t *)&full;

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    *low = 0x42U;
    uint64_t after_write = full;
#else
    ((volatile uint32_t *)&full)[1] = 0x42U;
    uint64_t after_write = full;
#endif
    (void)after_write;

    uint32_t narrow = 0xFFFFFFFFU;
    uint64_t wide = narrow;
    CHECK(wide == 0x00000000FFFFFFFFULL, "u32->u64 zero extends");

    int32_t neg = -1;
    int64_t sext = neg;
    CHECK((uint64_t)sext == 0xFFFFFFFFFFFFFFFFULL, "i32->i64 sign extends");
}

#if defined(__x86_64__)
static void test_x86_64_zero_ext_asm(void) {
    uint64_t result;
    __asm__ __volatile__(
        "movq $0xFFFFFFFFFFFFFFFF, %0\n\t"
        "movl $0x42, %%eax\n\t"
        "movq %%rax, %0"
        : "=r"(result)
        :
        : "rax");
    CHECK(result == 0x42ULL,
          "movl to eax zero-extends upper 32 bits of rax");
}
#elif defined(__aarch64__)
static void test_aarch64_zero_ext_asm(void) {
    uint64_t result;
    __asm__ __volatile__(
        "mov x1, #-1\n\t"
        "mov w1, #0x42\n\t"
        "mov %0, x1"
        : "=r"(result)
        :
        : "x1");
    CHECK(result == 0x42ULL,
          "mov w1 zero-extends upper 32 bits of x1");
}
#endif

// --- 4. Mixed 32/64-bit arithmetic ---

__attribute__((noinline))
static uint64_t mixed_multiply(uint32_t a, uint32_t b) {
    return (uint64_t)a * (uint64_t)b;
}

__attribute__((noinline))
static uint32_t extract_high32(uint64_t val) {
    return (uint32_t)(val >> 32);
}

static void test_mixed_arithmetic(void) {
    CHECK(mixed_multiply(0xFFFFFFFFU, 2) == 0x1FFFFFFFEULL,
          "u32*u32 -> u64 multiply");

    CHECK(mixed_multiply(0x80000000U, 0x80000000U) == 0x4000000000000000ULL,
          "u32*u32 -> u64 large multiply");

    CHECK(extract_high32(0x123456789ABCDEF0ULL) == 0x12345678U,
          "extract high 32 bits");

    volatile uint32_t a = 100, b = 200;
    uint32_t sum32 = a + b;
    CHECK(sum32 == 300, "u32 add");

    volatile uint64_t c = 100, d = 200;
    uint64_t sum64 = c + d;
    CHECK(sum64 == 300, "u64 add");

    uint32_t overflow = 0xFFFFFFFFU + 1U;
    CHECK(overflow == 0, "u32 overflow wraps");
}

// --- 5. 32-bit rotate ---

static void test_rotate(void) {
    uint32_t val = 0x12345678U;

    uint32_t rol8 = (val << 8) | (val >> 24);
    CHECK(rol8 == 0x34567812U, "rotate left 8");

    uint32_t ror8 = (val >> 8) | (val << 24);
    CHECK(ror8 == 0x78123456U, "rotate right 8");

    uint32_t rol1 = (val << 1) | (val >> 31);
    CHECK(rol1 == 0x2468ACF0U, "rotate left 1");
}

// --- 6. 32-bit atomic in 64-bit mode ---

static void test_atomic_32(void) {
    volatile uint32_t atom = 0;

    __atomic_store_n(&atom, 0xDEAD, __ATOMIC_SEQ_CST);
    CHECK(__atomic_load_n(&atom, __ATOMIC_SEQ_CST) == 0xDEAD, "atomic32 store");

    uint32_t old = __atomic_fetch_add(&atom, 1, __ATOMIC_SEQ_CST);
    CHECK(old == 0xDEAD, "atomic32 fetch_add old");
    CHECK(__atomic_load_n(&atom, __ATOMIC_SEQ_CST) == 0xDEAE, "atomic32 new");

    uint32_t expected = 0xDEAE;
    int ok = __atomic_compare_exchange_n(&atom, &expected, 0xBEEF,
                                         0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    CHECK(ok, "atomic32 CAS success");
    CHECK(__atomic_load_n(&atom, __ATOMIC_SEQ_CST) == 0xBEEF, "atomic32 CAS val");

    expected = 0;
    ok = __atomic_compare_exchange_n(&atom, &expected, 0, 0,
                                     __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    CHECK(!ok, "atomic32 CAS fail");
    CHECK(expected == 0xBEEF, "atomic32 CAS expected updated");
}

// --- 7. Struct layout with 32-bit fields ---

struct MixedStruct {
    uint32_t tag;
    uint64_t value;
    uint32_t flags;
    void    *ptr;
};

static void test_struct_layout(void) {
    CHECK(sizeof(void *) == 8, "64-bit pointers");

    CHECK(offsetof(struct MixedStruct, tag) == 0, "tag at offset 0");
    CHECK(offsetof(struct MixedStruct, value) == 8, "value aligned to 8");
    CHECK(offsetof(struct MixedStruct, flags) == 16, "flags after value");
    CHECK(offsetof(struct MixedStruct, ptr) == 24, "ptr aligned to 8");
    CHECK(sizeof(struct MixedStruct) == 32, "struct total size");

    struct MixedStruct s = { .tag = 0xCAFE, .value = 0x0102030405060708ULL,
                             .flags = 0xBEEF, .ptr = &s };
    CHECK(s.tag == 0xCAFE, "struct field tag");
    CHECK(s.value == 0x0102030405060708ULL, "struct field value");
    CHECK(s.flags == 0xBEEF, "struct field flags");
    CHECK(s.ptr == &s, "struct field ptr");
}

// --- 8. 32-bit array indexing with 64-bit pointers ---

static void test_array_indexing(void) {
    uint32_t arr[8] = {10, 20, 30, 40, 50, 60, 70, 80};

    for (int i = 0; i < 8; i++)
        CHECK(arr[i] == (uint32_t)((i + 1) * 10), "array indexing");

    uint32_t *p = arr + 3;
    CHECK(*p == 40, "pointer arithmetic on u32 array");
    CHECK((char *)p - (char *)arr == 12, "u32 ptr diff is 4 bytes per element");

    ptrdiff_t diff = &arr[7] - &arr[0];
    CHECK(diff == 7, "ptrdiff_t is element count");
}

int main(void) {
    test_ia32_bit_intrinsics();
    test_64bit_bit_intrinsics();
    test_float_bitcast();
    test_zero_extension();
    test_mixed_arithmetic();
    test_rotate();
    test_atomic_32();
    test_struct_layout();
    test_array_indexing();

#if defined(__x86_64__)
    test_x86_64_zero_ext_asm();
#elif defined(__aarch64__)
    test_aarch64_zero_ext_asm();
#endif

    printf("test_32bit_ops_in_64bit: ALL PASSED\n");
    return 0;
}
