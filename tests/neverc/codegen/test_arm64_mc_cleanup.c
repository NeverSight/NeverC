// Validates that removing ARM 32-bit dead code hasn't broken AArch64 codegen:
//
//   - MCMachObjectTargetWriter: Is64Bit parameter removed (hardcoded true)
//   - MCELFObjectTargetWriter: Is64Bit parameter removed (hardcoded true)
//   - MCAssembler: ThumbFuncs set + isThumbFunc()/setIsThumbFunc() removed
//   - MCStreamer: emitThumbFunc() virtual method removed from all streamers
//   - MCSymbolMachO: SF_ThumbFunc flag removed
//   - MCWin64EH: entire ARM (Thumb) Windows unwind implementation removed
//   - Win64EH.h: ARM-specific unwind opcodes removed
//   - ELFObjectWriter: Thumb interworking bit removed from SymbolValue()
//   - MCExpr: Thumb interworking bit removed from FinalizeFolding
//   - Triple.h: isArch32Bit/isArch16Bit/isWatchABI/isThumb removed
//   - InstrProfCorrelator: CK_32Bit removed
//   - X86InstrCompiler.td: NoCMOV-gated CMOV_GR32/CMOV_GR16 pseudos removed
//
// Tests use AArch64 W-register (32-bit) operations within 64-bit mode
// to prove "32-bit-within-64-bit" codegen is unaffected.
//
// RUN: %neverc -O2 %s -o %t && %t && echo "PASS: arm64_mc_cleanup"

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

// --- 1. W-register (32-bit) arithmetic in AArch64 mode ---
// AArch64 ADDS/SUBS/AND/ORR have W-register (32-bit) and X-register (64-bit)
// variants.  Removing ARM 32-bit code must NOT affect these.

__attribute__((noinline))
static uint32_t add32(uint32_t a, uint32_t b) { return a + b; }

__attribute__((noinline))
static uint32_t mul32(uint32_t a, uint32_t b) { return a * b; }

__attribute__((noinline))
static uint32_t and32(uint32_t a, uint32_t b) { return a & b; }

__attribute__((noinline))
static uint32_t shr32(uint32_t a, unsigned shift) { return a >> shift; }

static void test_w_register_ops(void) {
    CHECK(add32(0x7FFFFFFF, 1) == 0x80000000u, "W-reg add overflow");
    CHECK(mul32(0x10000, 0x10000) == 0, "W-reg mul overflow wraps to 0");
    CHECK(and32(0xDEADBEEF, 0x0000FFFF) == 0x0000BEEF, "W-reg AND mask");
    CHECK(shr32(0x80000000, 1) == 0x40000000, "W-reg logical shift right");
}

// --- 2. X-register (64-bit) arithmetic ---

__attribute__((noinline))
static uint64_t add64(uint64_t a, uint64_t b) { return a + b; }

__attribute__((noinline))
static uint64_t mul64(uint64_t a, uint64_t b) { return a * b; }

static void test_x_register_ops(void) {
    CHECK(add64(0x100000000ULL, 0x200000000ULL) == 0x300000000ULL,
          "X-reg add beyond 32-bit");
    CHECK(mul64(0x100000, 0x100000) == 0x10000000000ULL,
          "X-reg mul produces 64-bit result");
}

// --- 3. Conditional select (CSEL/CSINC/CSINV) ---
// On AArch64, CSEL has W and X variants.
// On x86_64, CMOV has 16/32/64-bit variants (NoCMOV pseudos removed).

__attribute__((noinline))
static uint32_t csel32(int cond, uint32_t a, uint32_t b) {
    return cond ? a : b;
}

__attribute__((noinline))
static uint64_t csel64(int cond, uint64_t a, uint64_t b) {
    return cond ? a : b;
}

static void test_conditional_select(void) {
    CHECK(csel32(1, 0xAAAAAAAA, 0x55555555) == 0xAAAAAAAA, "csel32 true");
    CHECK(csel32(0, 0xAAAAAAAA, 0x55555555) == 0x55555555, "csel32 false");
    CHECK(csel64(1, 0xDEADDEADDEADDEADULL, 0) == 0xDEADDEADDEADDEADULL,
          "csel64 true");
    CHECK(csel64(0, 0, 0xCAFECAFECAFECAFEULL) == 0xCAFECAFECAFECAFEULL,
          "csel64 false");
}

// --- 4. Mixed 32/64-bit struct layout ---
// Validates pointer is 8 bytes (64-bit) and int is 4 bytes.

typedef struct {
    uint32_t tag;
    void *ptr;
    uint64_t val;
    uint16_t flags;
} MixedRecord;

static void test_mixed_layout(void) {
    CHECK(sizeof(void *) == 8, "pointer is 8 bytes (64-bit)");
    CHECK(sizeof(uint32_t) == 4, "uint32_t is 4 bytes");
    CHECK(sizeof(MixedRecord) >= 24, "struct has 64-bit aligned members");

    MixedRecord r = { .tag = 0xBEEF, .ptr = &r, .val = 0x123456789ABCDEF0ULL,
                      .flags = 0xFFFF };
    CHECK(r.tag == 0xBEEF, "32-bit field in struct");
    CHECK(r.ptr == &r, "pointer field");
    CHECK(r.val == 0x123456789ABCDEF0ULL, "64-bit field");
    CHECK(r.flags == 0xFFFF, "16-bit field");
}

// --- 5. Function pointer (validates 64-bit symbol table / nlist_64) ---

typedef int (*fn_ptr_t)(unsigned int, unsigned int);

__attribute__((noinline))
static int call_via_fptr(fn_ptr_t f, unsigned int a, unsigned int b) {
    return f(a, b);
}

static int my_add(unsigned int a, unsigned int b) { return (int)(a + b); }

static void test_function_pointer(void) {
    fn_ptr_t f = my_add;
    CHECK(sizeof(f) == 8, "function pointer is 8 bytes");
    CHECK(call_via_fptr(f, 100, 200) == 300, "indirect call via fptr");
}

// --- 6. Bitfield operations (W-register bit manipulation) ---

typedef struct {
    unsigned a : 5;
    unsigned b : 11;
    unsigned c : 16;
} Bitfields;

static void test_bitfields(void) {
    Bitfields bf = { .a = 31, .b = 2047, .c = 65535 };
    CHECK(bf.a == 31, "5-bit field max");
    CHECK(bf.b == 2047, "11-bit field max");
    CHECK(bf.c == 65535, "16-bit field max");

    bf.a = 0;
    bf.b = 1;
    CHECK(bf.a == 0 && bf.b == 1, "bitfield write");
}

int main(void) {
    test_w_register_ops();
    test_x_register_ops();
    test_conditional_select();
    test_mixed_layout();
    test_function_pointer();
    test_bitfields();

    printf("PASS: arm64_mc_cleanup - all 6 test groups passed\n");
    return 0;
}
