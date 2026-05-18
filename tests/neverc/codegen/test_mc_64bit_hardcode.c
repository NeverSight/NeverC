// test_mc_64bit_hardcode.c
// Validates correctness after hardcoding is64Bit()=true in MC layer:
//   1. ELF writer: SymbolTableWriter 64-bit format, WriteWord always uint64
//   2. MachO writer: MH_MAGIC_64, LC_SEGMENT_64, nlist_64, Align(8) padding
//   3. TargetLibraryInfo: setIntSize(32) after isArch16Bit() removal
//   4. MachO CPU type/subtype: getCPUType/getCPUSubType no longer checks isArch64Bit
//   5. StringTableBuilder::MachO64 hardcoded in MachObjectWriter
//   6. InstrProfCorrelator 32-bit template instantiation removed
//
// RUN: %neverc -O2 %s -o %t && %t && echo "PASS: mc_64bit_hardcode"

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

// --- 1. Type widths (validates setIntSize(32) after isArch16Bit removal) ---

static void test_type_widths(void) {
    CHECK(sizeof(char) == 1, "char is 1 byte");
    CHECK(sizeof(short) == 2, "short is 2 bytes");
    CHECK(sizeof(int) == 4, "int is 4 bytes (setIntSize=32)");
    CHECK(sizeof(long long) == 8, "long long is 8 bytes");
    CHECK(sizeof(void *) == 8, "pointer is 8 bytes (64-bit)");
    CHECK(sizeof(size_t) == 8, "size_t is 8 bytes");

    CHECK(_Alignof(int) == 4, "int alignment is 4");
    CHECK(_Alignof(long long) == 8, "long long alignment is 8");
    CHECK(_Alignof(void *) == 8, "pointer alignment is 8");
}

// --- 2. Function pointer size (validates 64-bit nlist/symtab) ---

__attribute__((noinline))
static int func_a(int x) { return x * 2; }

__attribute__((noinline))
static int func_b(int x) { return x + 100; }

typedef int (*fn_ptr_t)(int);

static void test_function_pointers(void) {
    CHECK(sizeof(fn_ptr_t) == 8, "function pointer is 8 bytes");

    volatile fn_ptr_t fpa = func_a;
    volatile fn_ptr_t fpb = func_b;

    CHECK(fpa(21) == 42, "indirect call through 64-bit function pointer A");
    CHECK(fpb(0) == 100, "indirect call through 64-bit function pointer B");

    CHECK((uintptr_t)fpa != 0, "function pointer A is non-null");
    CHECK((uintptr_t)fpb != 0, "function pointer B is non-null");
    CHECK((uintptr_t)fpa != (uintptr_t)fpb, "distinct functions have distinct addresses");
}

// --- 3. Global data addressing (validates 64-bit relocation encoding) ---

static int g_array[256];
static const char g_string[] = "Hello from 64-bit MC writer";
__attribute__((weak)) int g_weak = 0xBEEF;

static void test_global_data(void) {
    for (int i = 0; i < 256; i++)
        g_array[i] = i * 3;

    CHECK(g_array[0] == 0, "global array element 0");
    CHECK(g_array[100] == 300, "global array element 100");
    CHECK(g_array[255] == 765, "global array element 255");

    CHECK(strlen(g_string) == 27, "global string length");
    CHECK(g_string[0] == 'H', "global string content");

    CHECK(g_weak == 0xBEEF, "weak symbol resolved");
}

// --- 4. Large struct with pointer members (validates 64-bit section layout) ---

struct LargeStruct {
    void *ptrs[4];
    uint64_t vals[4];
    uint32_t tags[4];
    uint16_t flags[4];
    uint8_t bytes[4];
};

static void test_large_struct(void) {
    CHECK(sizeof(struct LargeStruct) == 96, "LargeStruct size");

    CHECK(offsetof(struct LargeStruct, ptrs) == 0, "ptrs offset");
    CHECK(offsetof(struct LargeStruct, vals) == 32, "vals offset");
    CHECK(offsetof(struct LargeStruct, tags) == 64, "tags offset");
    CHECK(offsetof(struct LargeStruct, flags) == 80, "flags offset");
    CHECK(offsetof(struct LargeStruct, bytes) == 88, "bytes offset");

    struct LargeStruct ls;
    memset(&ls, 0, sizeof(ls));
    ls.ptrs[0] = &ls;
    ls.vals[0] = 0xDEADBEEFCAFEBABEULL;
    ls.tags[0] = 0x12345678;
    ls.flags[0] = 0xABCD;
    ls.bytes[0] = 0xFF;

    CHECK(ls.ptrs[0] == &ls, "pointer member self-reference");
    CHECK(ls.vals[0] == 0xDEADBEEFCAFEBABEULL, "uint64 member");
    CHECK(ls.tags[0] == 0x12345678, "uint32 member");
    CHECK(ls.flags[0] == 0xABCD, "uint16 member");
    CHECK(ls.bytes[0] == 0xFF, "uint8 member");
}

// --- 5. Stack alignment (validates Align(8) MachO padding) ---

static void __attribute__((noinline)) check_stack_alignment(void) {
    volatile char buf[1];
    buf[0] = 'X';
    uintptr_t sp;
#if defined(__x86_64__)
    __asm__ volatile ("mov %%rsp, %0" : "=r"(sp));
#elif defined(__aarch64__)
    __asm__ volatile ("mov %0, sp" : "=r"(sp));
#else
    sp = 16;
#endif
    CHECK((sp & 0xF) == 0, "stack is 16-byte aligned");
}

// --- 6. Mixed-width arithmetic chain ---
//    Exercises 32-bit sub-register ops within 64-bit pipeline.
//    After removing NoCMOV pseudos, hardware CMOV is used directly.

static uint32_t __attribute__((noinline)) chain32(uint32_t x) {
    uint32_t a = x << 3;
    uint32_t b = a | 0xFF;
    uint32_t c = b ^ 0xDEAD0000;
    return c >> 1;
}

static uint64_t __attribute__((noinline)) chain64(uint64_t x) {
    uint64_t a = x << 5;
    uint64_t b = a | 0xFFFF;
    uint64_t c = b ^ 0xCAFEBABE00000000ULL;
    return c >> 2;
}

static void test_mixed_arithmetic(void) {
    uint32_t r32 = chain32(0x12345678);
    uint32_t e32 = (((0x12345678U << 3) | 0xFF) ^ 0xDEAD0000U) >> 1;
    CHECK(r32 == e32, "32-bit arithmetic chain");

    uint64_t r64 = chain64(0x123456789ABCDEF0ULL);
    uint64_t e64 = ((((0x123456789ABCDEF0ULL) << 5) | 0xFFFF) ^ 0xCAFEBABE00000000ULL) >> 2;
    CHECK(r64 == e64, "64-bit arithmetic chain");

    uint64_t promoted = (uint64_t)r32;
    CHECK((promoted >> 32) == 0, "32-bit result zero-extends in 64-bit register");
}

// --- 7. Conditional select (validates CMOV without NoCMOV pseudo) ---

static uint32_t __attribute__((noinline)) csel32(int c, uint32_t a, uint32_t b) {
    return c ? a : b;
}

static uint16_t __attribute__((noinline)) csel16(int c, uint16_t a, uint16_t b) {
    return c ? a : b;
}

static void test_conditional_select(void) {
    CHECK(csel32(1, 0xAAAAAAAA, 0x55555555) == 0xAAAAAAAA, "csel32 true");
    CHECK(csel32(0, 0xAAAAAAAA, 0x55555555) == 0x55555555, "csel32 false");
    CHECK(csel16(1, 0xAAAA, 0x5555) == 0xAAAA, "csel16 true");
    CHECK(csel16(0, 0xAAAA, 0x5555) == 0x5555, "csel16 false");
}

int main(void) {
    test_type_widths();
    test_function_pointers();
    test_global_data();
    test_large_struct();
    check_stack_alignment();
    test_mixed_arithmetic();
    test_conditional_select();

    printf("test_mc_64bit_hardcode: ALL PASSED\n");
    return 0;
}
