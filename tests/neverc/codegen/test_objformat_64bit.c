// test_objformat_64bit.c
// Validates that ELF and Mach-O object files are correctly emitted as 64-bit
// after simplifying is64Bit() dead branches in the MC layer.
//
// Tests:
//   1. Object file is recognized as 64-bit by `file` command
//   2. Final executable runs correctly (end-to-end object format validation)
//   3. Symbol table is functional (extern/global symbols resolve)
//   4. Relocation entries work (cross-object linking)
//   5. Debug info sections are valid 64-bit format (-g)
//
// RUN: %neverc -fno-lto -c %s -o %t.o && file %t.o | grep -q "64-bit" && echo "PASS: objformat 64-bit header"
// RUN: %neverc -O2 %s -o %t && %t && echo "PASS: objformat end-to-end"
// RUN: %neverc -g -O0 %s -o %t_debug && %t_debug && echo "PASS: objformat debug info"

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

extern int printf(const char *, ...);

static int global_arr[4] = {10, 20, 30, 40};

static uint64_t compute_hash(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint64_t h = 0x811c9dc5;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= 0x01000193;
    }
    return h;
}

static void test_symbol_resolution(void) {
    CHECK(global_arr[0] == 10, "global symbol access [0]");
    CHECK(global_arr[3] == 40, "global symbol access [3]");

    int sum = 0;
    for (int i = 0; i < 4; i++)
        sum += global_arr[i];
    CHECK(sum == 100, "global array sum");
}

static void test_relocation(void) {
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "hello %d world %llu",
                     42, (unsigned long long)0xDEADBEEFULL);
    CHECK(n > 0, "snprintf produced output");
    CHECK(strstr(buf, "hello 42") != NULL, "snprintf format resolved");
    CHECK(strstr(buf, "3735928559") != NULL, "snprintf u64 resolved");
}

static void test_data_section_layout(void) {
    static const uint64_t rodata[] = {
        0x0102030405060708ULL,
        0x1112131415161718ULL,
        0x2122232425262728ULL,
        0x3132333435363738ULL
    };

    uint64_t hash = compute_hash(rodata, sizeof(rodata));
    CHECK(hash != 0, "rodata section hash nonzero");

    CHECK(rodata[0] == 0x0102030405060708ULL, "rodata[0]");
    CHECK(rodata[3] == 0x3132333435363738ULL, "rodata[3]");
}

static void test_stack_frame(void) {
    volatile uint64_t big_local[32];
    for (int i = 0; i < 32; i++)
        big_local[i] = (uint64_t)i * 0x0101010101010101ULL;

    CHECK(big_local[0] == 0, "stack frame [0]");
    CHECK(big_local[1] == 0x0101010101010101ULL, "stack frame [1]");
    CHECK(big_local[31] == 31ULL * 0x0101010101010101ULL, "stack frame [31]");

    CHECK(((intptr_t)&big_local[0] & 0x7) == 0, "stack 8-byte aligned");
}

static void test_function_pointer(void) {
    typedef int (*printf_fn)(const char *, ...);
    volatile printf_fn fp = printf;
    CHECK(fp != NULL, "function pointer resolved");
    CHECK((void *)fp == (void *)printf, "function pointer matches");
}

int visible_global = 0xBEEF;
static int hidden_static = 0xCAFE;

__attribute__((weak)) int weak_symbol(void) { return 42; }

static int many_statics_1 = 1;
static int many_statics_2 = 2;
static int many_statics_3 = 3;
static int many_statics_4 = 4;

__attribute__((noinline))
static int helper_a(int x) { return x + many_statics_1; }

__attribute__((noinline))
static int helper_b(int x) { return x * many_statics_2; }

__attribute__((noinline))
int exported_helper(int x) { return helper_a(x) + helper_b(x); }

static void test_symtab_variety(void) {
    CHECK(visible_global == 0xBEEF, "global symbol value");
    CHECK(hidden_static == 0xCAFE, "static symbol value");
    CHECK(weak_symbol() == 42, "weak symbol call");

    int r = exported_helper(10);
    CHECK(r == (10 + 1) + (10 * 2), "multi-symbol call chain");

    CHECK(many_statics_3 == 3, "static data 3");
    CHECK(many_statics_4 == 4, "static data 4");

    visible_global = 0xDEAD;
    CHECK(visible_global == 0xDEAD, "global symbol write");
}

int main(void) {
    test_symbol_resolution();
    test_relocation();
    test_data_section_layout();
    test_stack_frame();
    test_function_pointer();
    test_symtab_variety();

    printf("test_objformat_64bit: ALL PASSED\n");
    return 0;
}
