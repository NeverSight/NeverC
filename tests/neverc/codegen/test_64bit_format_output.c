// Validates that MC layer changes to hardcode 64-bit object format
// produce correct output. After removing is64Bit() branching from:
//   - ELFObjectWriter (ELFCLASS64, Elf64_Ehdr, Elf64_Shdr, Elf64_Sym)
//   - MachObjectWriter (MH_MAGIC_64, segment_command_64, section_64, nlist_64)
//   - MCELFObjectTargetWriter (removed Is64Bit parameter)
//   - MCMachObjectTargetWriter (removed Is64Bit parameter)
//
// This test compiles a non-trivial program with both data and code
// sections, external calls, global variables, and runs it to verify
// the linker successfully consumes the 64-bit object output.
//
// RUN: %neverc -O2 %s -o %t && %t && echo "PASS: 64bit_format_output"

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

static const char rodata_string[] = "Hello from .rodata";

static int32_t bss_array[16];

static int32_t data_value = 0x12345678;

static volatile uint64_t side_effect;

__attribute__((noinline))
static uint64_t compute_pointer_diff(const void *a, const void *b) {
    return (uint64_t)((const char *)a - (const char *)b);
}

__attribute__((noinline))
static void fill_array(int32_t *arr, int n, int32_t val) {
    for (int i = 0; i < n; i++)
        arr[i] = val + i;
}

__attribute__((noinline))
static int64_t sum_array(const int32_t *arr, int n) {
    int64_t s = 0;
    for (int i = 0; i < n; i++)
        s += arr[i];
    return s;
}

static void test_sections(void) {
    CHECK(strlen(rodata_string) == 18, "rodata string length");
    CHECK(strcmp(rodata_string, "Hello from .rodata") == 0, "rodata content");

    CHECK(bss_array[0] == 0, "bss zero-init");
    CHECK(bss_array[15] == 0, "bss zero-init end");

    CHECK(data_value == 0x12345678, "data section value");
}

static void test_pointer_arithmetic(void) {
    uint64_t diff = compute_pointer_diff(&bss_array[15], &bss_array[0]);
    CHECK(diff == 15 * sizeof(int32_t), "pointer diff in 64-bit space");

    uintptr_t addr = (uintptr_t)&data_value;
    CHECK(addr > 0x1000, "data address in valid range");
    CHECK((addr & 0x3) == 0, "data address aligned");
}

static void test_large_values(void) {
    uint64_t big = 0xDEADBEEFCAFEBABEULL;
    side_effect = big;
    CHECK(side_effect == 0xDEADBEEFCAFEBABEULL, "64-bit volatile store/load");

    int64_t product = (int64_t)0x7FFFFFFF * (int64_t)0x7FFFFFFF;
    CHECK(product == 0x3FFFFFFF00000001LL, "64-bit multiply");
}

static void test_array_computation(void) {
    fill_array(bss_array, 16, 100);
    int64_t s = sum_array(bss_array, 16);
    // sum = 100+101+102+...+115 = 16*100 + (0+1+...+15) = 1600 + 120 = 1720
    CHECK(s == 1720, "array sum");

    CHECK(bss_array[0] == 100, "array[0]");
    CHECK(bss_array[15] == 115, "array[15]");
}

static void test_function_pointers(void) {
    typedef int64_t (*sum_fn)(const int32_t *, int);
    volatile sum_fn fn = sum_array;
    int64_t r = fn(bss_array, 16);
    CHECK(r == 1720, "indirect call via function pointer");
}

int main(void) {
    test_sections();
    test_pointer_arithmetic();
    test_large_values();
    test_array_computation();
    test_function_pointers();
    printf("PASS: 64bit_format_output - all tests passed\n");
    return 0;
}
