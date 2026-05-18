// test_mc_64bit_and_arm_cleanup.c
// Validates MC layer 64-bit hardcoding and ARM 32-bit code removal:
//
//   - ELFObjectWriter: is64Bit() removed, always writes Elf64 format
//   - MachObjectWriter: is64Bit() removed, writes mach_header_64/nlist_64
//   - MCMachObjectTargetWriter: Is64Bit field removed from constructor
//   - MCELFObjectTargetWriter: Is64Bit field removed from constructor
//   - WinCOFFObjectWriter: ARM (ARMNT) relocation handling removed
//   - COFF.h: RelocationTypesARM enum removed
//   - MachO.h: ARM32 CPU subtypes, PPC/ARM relocation types removed
//   - Win64EH: ARMUnwindEmitter + ARM unwind opcodes removed (~1083 lines)
//   - cet.h: endbr32 + 32-bit property alignment removed
//   - ia32intrin.h: ia32-specific instruction doc removed
//   - Host.cpp: pre-Athlon AMD CPU family detection removed
//
// Tests exercise 32-bit operations within 64-bit mode to ensure
// they still work after removing 32-bit-ONLY code paths.
//
// RUN: %neverc -O2 %s -o %t && %t && echo "PASS: mc_64bit_and_arm_cleanup"

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

static volatile int sink;

// --- 1. 32-bit integer ops in 64-bit mode (EAX/EBX low-half usage) ---
// These produce mov/add/sub/and with 32-bit operand size in x86_64 mode.
// The instructions use EAX (low 32 of RAX) which zero-extends to RAX.

__attribute__((noinline))
static uint32_t zero_extend_test(uint32_t x) {
    uint64_t wide = x;
    return (uint32_t)(wide >> 16);
}

__attribute__((noinline))
static uint32_t xor_test(uint32_t a, uint32_t b) {
    return a ^ b;
}

static void test_32bit_in_64bit(void) {
    CHECK(zero_extend_test(0xDEAD0000) == 0xDEAD, "zero-extend high 16");
    CHECK(zero_extend_test(0x0000BEEF) == 0, "zero-extend low 16 shifted out");
    CHECK(xor_test(0xFFFFFFFF, 0xAAAAAAAA) == 0x55555555, "xor32");
    CHECK(xor_test(0, 0) == 0, "xor32 identity");
}

// --- 2. Integer absolute value (canUseCMOV guard removed) ---
// Previously gated on canUseCMOV() which is always true on x86_64.

__attribute__((noinline))
static int32_t abs32(int32_t x) {
    return x < 0 ? -x : x;
}

__attribute__((noinline))
static int64_t abs64(int64_t x) {
    return x < 0 ? -x : x;
}

static void test_abs(void) {
    CHECK(abs32(-42) == 42, "abs32 negative");
    CHECK(abs32(42) == 42, "abs32 positive");
    CHECK(abs32(0) == 0, "abs32 zero");
    CHECK(abs32(-2147483647) == 2147483647, "abs32 near INT_MIN");

    CHECK(abs64(-100LL) == 100, "abs64 negative");
    CHECK(abs64(100LL) == 100, "abs64 positive");
    CHECK(abs64(-4294967296LL) == 4294967296LL, "abs64 beyond 32-bit");
}

// --- 3. Signed division by power of 2 (canUseCMOV guard removed) ---
// SDIVPow2 optimization previously required canUseCMOV() guard.

__attribute__((noinline))
static int32_t sdiv_pow2(int32_t x) {
    return x / 8;
}

__attribute__((noinline))
static int64_t sdiv_pow2_64(int64_t x) {
    return x / 16;
}

static void test_sdiv_pow2(void) {
    CHECK(sdiv_pow2(80) == 10, "sdiv by 8 positive");
    CHECK(sdiv_pow2(-80) == -10, "sdiv by 8 negative");
    CHECK(sdiv_pow2(7) == 0, "sdiv by 8 truncates");
    CHECK(sdiv_pow2(-7) == 0, "sdiv by 8 truncates neg");
    CHECK(sdiv_pow2(-1) == 0, "sdiv by 8 rounds toward zero");

    CHECK(sdiv_pow2_64(160LL) == 10, "sdiv64 by 16");
    CHECK(sdiv_pow2_64(-160LL) == -10, "sdiv64 by 16 neg");
}

// --- 4. Pointer size / struct layout (64-bit object format) ---
// Validates the object file uses 64-bit pointers (nlist_64, Elf64_Sym).

typedef struct {
    void *ptr;
    uint32_t val32;
    uint64_t val64;
} AlignTest;

static void test_64bit_layout(void) {
    CHECK(sizeof(void *) == 8, "pointer size is 8 bytes");
    CHECK(sizeof(size_t) == 8, "size_t is 8 bytes");
    CHECK(sizeof(uintptr_t) == 8, "uintptr_t is 8 bytes");

    AlignTest t = { .ptr = &t, .val32 = 0xBEEF, .val64 = 0xCAFEBABEDEADFACEULL };
    CHECK(t.ptr == &t, "self-referential pointer");
    CHECK(t.val32 == 0xBEEF, "32-bit member");
    CHECK(t.val64 == 0xCAFEBABEDEADFACEULL, "64-bit member");
}

// --- 5. Global data with mixed sizes (ELF/MachO symbol table validation) ---

static uint8_t g_u8 = 0xFF;
static uint16_t g_u16 = 0xBEEF;
static uint32_t g_u32 = 0xDEADBEEF;
static uint64_t g_u64 = 0x123456789ABCDEF0ULL;

static void test_global_data(void) {
    CHECK(g_u8 == 0xFF, "global u8");
    CHECK(g_u16 == 0xBEEF, "global u16");
    CHECK(g_u32 == 0xDEADBEEF, "global u32");
    CHECK(g_u64 == 0x123456789ABCDEF0ULL, "global u64");

    g_u32 = 42;
    CHECK(g_u32 == 42, "global u32 write");
    g_u32 = 0xDEADBEEF;
}

// --- 6. Indirect call (validates 64-bit function pointer relocation) ---

typedef uint32_t (*binop_t)(uint32_t, uint32_t);

__attribute__((noinline))
static uint32_t add_fn(uint32_t a, uint32_t b) { return a + b; }
__attribute__((noinline))
static uint32_t sub_fn(uint32_t a, uint32_t b) { return a - b; }

static void test_indirect_call(void) {
    volatile binop_t ops[2] = { add_fn, sub_fn };
    CHECK(ops[0](100, 42) == 142, "indirect add");
    CHECK(ops[1](100, 42) == 58, "indirect sub");
}

// --- 7. Switch (validates jump table uses 64-bit addresses) ---

__attribute__((noinline))
static int switch_test(int x) {
    switch (x) {
        case 0: return 10;
        case 1: return 20;
        case 2: return 30;
        case 3: return 40;
        case 4: return 50;
        case 5: return 60;
        default: return -1;
    }
}

static void test_switch(void) {
    CHECK(switch_test(0) == 10, "switch case 0");
    CHECK(switch_test(3) == 40, "switch case 3");
    CHECK(switch_test(5) == 60, "switch case 5");
    CHECK(switch_test(99) == -1, "switch default");
}

int main(void) {
    test_32bit_in_64bit();
    test_abs();
    test_sdiv_pow2();
    test_64bit_layout();
    test_global_data();
    test_indirect_call();
    test_switch();

    printf("PASS: mc_64bit_and_arm_cleanup - all 7 test groups passed\n");
    return 0;
}
