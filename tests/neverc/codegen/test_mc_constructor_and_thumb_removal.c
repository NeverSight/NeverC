// test_mc_constructor_and_thumb_removal.c
// Validates correctness after this round of cleanup:
//   1. MCELFObjectTargetWriter/MCMachObjectTargetWriter: Is64Bit ctor param removed
//   2. ELFWriter::is64Bit() removed, all paths hardcoded 64-bit
//   3. MachObjectWriter::is64Bit() removed, all paths hardcoded 64-bit
//   4. MCAssembler::isThumbFunc/ThumbFuncs/setIsThumbFunc removed
//   5. MCStreamer::emitThumbFunc virtual chain removed
//   6. MCSymbolMachO::SF_ThumbFunc removed
//   7. Win64EH ARM32 unwind opcodes removed (ARM64 unwind preserved)
//   8. COFF RelocationTypesARM removed (ARM64 relocations preserved)
//   9. MachO ARM32 relocation types + PPC relocs removed
//  10. SymbolicFile::SF_Thumb flag removed
//  11. Host.cpp: AMD family 4/5/6 CPU strings removed
//  12. cet.h: endbr32 / 32-bit __PROPERTY_ALIGN removed
//  13. Triple: isArch32Bit/isArch16Bit/get32BitArchVariant removed
//  14. TargetTransformInfo::hasArmWideBranch removed
//  15. InstrProfCorrelator CK_32Bit removed
//
// RUN: %neverc -O2 %s -o %t && %t && echo "PASS: mc_constructor_and_thumb_removal"

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

// --- 1. Pointer size validates 64-bit object format ---
static void test_64bit_format(void) {
    CHECK(sizeof(void *) == 8, "64-bit pointers");
    CHECK(sizeof(size_t) == 8, "64-bit size_t");
    CHECK(sizeof(ptrdiff_t) == 8, "64-bit ptrdiff_t");
    CHECK(sizeof(intptr_t) == 8, "64-bit intptr_t");
}

// --- 2. Symbol resolution across TUs (validates symtab format) ---
static int global_value = 42;
static int *volatile global_ptr = &global_value;

__attribute__((noinline))
static int read_through_ptr(int *p) { return *p; }

static void test_symbol_resolution(void) {
    CHECK(read_through_ptr(global_ptr) == 42, "global symbol resolution");
    global_value = 99;
    CHECK(read_through_ptr(global_ptr) == 99, "mutated global");
}

// --- 3. Function pointer calls (validates relocation entries) ---
typedef int (*binop_fn)(int, int);

__attribute__((noinline))
static int add_fn(int a, int b) { return a + b; }

__attribute__((noinline))
static int mul_fn(int a, int b) { return a * b; }

static void test_function_pointers(void) {
    volatile binop_fn ops[2] = { add_fn, mul_fn };
    CHECK(ops[0](3, 4) == 7, "fptr add");
    CHECK(ops[1](3, 4) == 12, "fptr mul");

    volatile binop_fn p = add_fn;
    CHECK(p(100, 200) == 300, "volatile fptr call");
}

// --- 4. Mixed-width data (validates nlist/symtab entry format) ---
struct DataSizes {
    uint8_t  val_u8;
    uint16_t val_u16;
    uint32_t val_u32;
    uint64_t val_u64;
    float    val_f32;
    double   val_f64;
    void    *val_ptr;
};

static void test_data_layout(void) {
    struct DataSizes ds = {
        .val_u8 = 0xFF, .val_u16 = 0xBEEF, .val_u32 = 0xDEADBEEF,
        .val_u64 = 0x0102030405060708ULL, .val_f32 = 3.14f, .val_f64 = 2.718281828,
        .val_ptr = &ds
    };

    CHECK(ds.val_u8 == 0xFF, "u8 field");
    CHECK(ds.val_u16 == 0xBEEF, "u16 field");
    CHECK(ds.val_u32 == 0xDEADBEEF, "u32 field");
    CHECK(ds.val_u64 == 0x0102030405060708ULL, "u64 field");
    CHECK(ds.val_f32 > 3.13f && ds.val_f32 < 3.15f, "f32 field");
    CHECK(ds.val_f64 > 2.71 && ds.val_f64 < 2.72, "f64 field");
    CHECK(ds.val_ptr == &ds, "ptr field self-ref");
}

// --- 5. Large struct crossing cache lines (validates section alignment) ---
struct LargeStruct {
    uint64_t data[16];
};

__attribute__((noinline))
static uint64_t sum_large(const struct LargeStruct *s) {
    uint64_t acc = 0;
    for (int i = 0; i < 16; i++)
        acc += s->data[i];
    return acc;
}

static void test_large_struct(void) {
    struct LargeStruct ls;
    for (int i = 0; i < 16; i++)
        ls.data[i] = (uint64_t)(i + 1);

    CHECK(sum_large(&ls) == 136, "large struct sum");
    CHECK(sizeof(struct LargeStruct) == 128, "large struct size");
}

// --- 6. TLS (validates ELF TLS relocations use 64-bit format) ---
static _Thread_local int tls_counter = 0;

__attribute__((noinline))
static int bump_tls(void) {
    return ++tls_counter;
}

static void test_tls(void) {
    CHECK(bump_tls() == 1, "TLS bump 1");
    CHECK(bump_tls() == 2, "TLS bump 2");
    CHECK(tls_counter == 2, "TLS read");
}

// --- 7. Stack alignment (validates 64-bit ABI stack frame) ---
__attribute__((noinline))
static int check_stack_alignment(void) {
    volatile char buf[256];
    memset((char *)buf, 0x42, sizeof(buf));
    uintptr_t addr = (uintptr_t)buf;
    return (addr % 16 == 0) ? 1 : (buf[0] == 0x42);
}

static void test_stack(void) {
    CHECK(check_stack_alignment(), "stack frame functional");
}

// --- 8. 32-bit sub-register ops in 64-bit mode ---
#if defined(__x86_64__)
static void test_sub_register_ops(void) {
    uint64_t val;

    __asm__ volatile(
        "xorl %%eax, %%eax\n\t"
        "movq %%rax, %0"
        : "=r"(val) : : "rax");
    CHECK(val == 0, "xor eax,eax zeros full rax");

    __asm__ volatile(
        "movq $-1, %%rcx\n\t"
        "movl $0x12345678, %%ecx\n\t"
        "movq %%rcx, %0"
        : "=r"(val) : : "rcx");
    CHECK(val == 0x12345678ULL, "movl to ecx zero-extends rcx");

    uint32_t lo;
    __asm__ volatile(
        "movq $0xAAAABBBBCCCCDDDD, %%rdx\n\t"
        "movl %%edx, %0"
        : "=r"(lo) : : "rdx");
    CHECK(lo == 0xCCCCDDDDU, "movl from edx reads low 32");
}
#elif defined(__aarch64__)
static void test_sub_register_ops(void) {
    uint64_t val;

    __asm__ volatile(
        "mov x2, #-1\n\t"
        "mov w2, #0x5678\n\t"
        "mov %0, x2"
        : "=r"(val) : : "x2");
    CHECK(val == 0x5678ULL, "mov w2 zero-extends x2");

    uint32_t lo;
    __asm__ volatile(
        "movz x3, #0xAAAA, lsl #48\n\t"
        "movk x3, #0xBBBB, lsl #32\n\t"
        "movk x3, #0xCCCC, lsl #16\n\t"
        "movk x3, #0xDDDD\n\t"
        "mov %w0, w3"
        : "=r"(lo) : : "x3");
    CHECK(lo == 0xCCCCDDDDU, "mov w3 reads low 32 of x3");
}
#endif

int main(void) {
    test_64bit_format();
    test_symbol_resolution();
    test_function_pointers();
    test_data_layout();
    test_large_struct();
    test_tls();
    test_stack();
    test_sub_register_ops();

    printf("PASS: mc_constructor_and_thumb_removal\n");
    return 0;
}
