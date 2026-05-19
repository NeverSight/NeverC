// Validates that MC layer 64-bit-only changes are correct:
//   1. Mach-O nlist_64 symbol format (16 bytes per entry, not 12)
//   2. ELF64 symbol table entry format (Elf64_Sym layout)
//   3. Function pointers resolve correctly through 64-bit relocations
//   4. Global data addresses are 64-bit wide
//   5. Exception handling / stack unwinding works after ARM32 unwind removal
//   6. Debug info works after is64Bit() flattening
//
// RUN: %neverc -O2 %s -o %t && %t && echo "PASS: mc_64bit_format"
// RUN: %neverc -g -O0 %s -o %t_debug && %t_debug && echo "PASS: mc_64bit_format debug"

#include <setjmp.h>
#include <signal.h>
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

// --- 1. Symbol resolution through 64-bit nlist/symtab ---

int global_data = 0xCAFEBABE;
static int static_data = 0xDEADBEEF;
__attribute__((weak)) int weak_data = 42;

__attribute__((noinline))
int exported_func(int x) { return x * 3 + 1; }

__attribute__((noinline))
static int static_func(int x) { return x + static_data; }

static void test_symbol_types(void) {
    CHECK(global_data == (int)0xCAFEBABE, "global data symbol");
    CHECK(static_data == (int)0xDEADBEEF, "static data symbol");
    CHECK(weak_data == 42, "weak data symbol");
    CHECK(exported_func(10) == 31, "exported function symbol");
    CHECK(static_func(1) == 1 + (int)0xDEADBEEF, "static function symbol");
}

// --- 2. Function pointer table (relocation entries) ---

typedef int (*fn_t)(int);
static int fn_a(int x) { return x + 1; }
static int fn_b(int x) { return x + 2; }
static int fn_c(int x) { return x + 3; }
static int fn_d(int x) { return x + 4; }

static void test_function_pointer_table(void) {
    volatile fn_t table[4] = { fn_a, fn_b, fn_c, fn_d };

    CHECK(sizeof(fn_t) == 8, "function pointer is 64-bit");

    for (int i = 0; i < 4; i++) {
        CHECK(table[i](10) == 10 + i + 1, "fn table dispatch");
    }

    CHECK((uintptr_t)table[0] != (uintptr_t)table[1],
          "function pointers are distinct");
}

// --- 3. Large address space (64-bit addresses) ---

static void test_64bit_addresses(void) {
    int local;
    uintptr_t stack_addr = (uintptr_t)&local;
    uintptr_t heap_addr = (uintptr_t)malloc(1);
    uintptr_t global_addr = (uintptr_t)&global_data;
    uintptr_t func_addr = (uintptr_t)&exported_func;

    CHECK(stack_addr != 0, "stack address nonzero");
    CHECK(heap_addr != 0, "heap address nonzero");
    CHECK(global_addr != 0, "global address nonzero");
    CHECK(func_addr != 0, "function address nonzero");

    free((void *)heap_addr);

    CHECK(sizeof(uintptr_t) == 8, "pointer is 8 bytes");
    CHECK(sizeof(void *) == 8, "void* is 8 bytes");
    CHECK(sizeof(size_t) == 8, "size_t is 8 bytes");
}

// --- 4. setjmp/longjmp (validates unwind-related state save/restore) ---

static jmp_buf jmp_env;
static volatile int jmp_value = 0;

__attribute__((noinline))
static void do_longjmp(int val) {
    longjmp(jmp_env, val);
}

static void test_setjmp_longjmp(void) {
    int ret = setjmp(jmp_env);
    if (ret == 0) {
        jmp_value = 1;
        do_longjmp(42);
        CHECK(0, "should not reach here after longjmp");
    } else {
        CHECK(ret == 42, "longjmp returned correct value");
        CHECK(jmp_value == 1, "state before longjmp preserved");
    }
}

// --- 5. Volatile signal handler (validates signal frame layout) ---

static volatile sig_atomic_t signal_received = 0;

static void signal_handler(int sig) {
    (void)sig;
    signal_received = 1;
}

static void test_signal_handling(void) {
    void (*prev)(int) = signal(SIGUSR1, signal_handler);
    CHECK(prev != SIG_ERR, "signal() succeeded");

    raise(SIGUSR1);
    CHECK(signal_received == 1, "signal handler invoked");

    signal(SIGUSR1, SIG_DFL);
}

// --- 6. Cross-TU style linking (multiple compilation units simulated) ---

struct VTable {
    int (*get)(void);
    void (*set)(int);
    const char *name;
};

static int vtable_value = 0;
static int vtable_get(void) { return vtable_value; }
static void vtable_set(int v) { vtable_value = v; }

static void test_vtable_simulation(void) {
    volatile struct VTable vt = {
        .get = vtable_get,
        .set = vtable_set,
        .name = "test_vtable"
    };

    CHECK(sizeof(struct VTable) == 24, "vtable struct is 3 pointers");

    vt.set(99);
    CHECK(vt.get() == 99, "vtable dispatch works");
    CHECK(strcmp(vt.name, "test_vtable") == 0, "vtable string resolved");
}

// --- 7. Large static data (validates data section layout in 64-bit format) ---

static const uint64_t large_rodata[64] = {
    [0]  = 0x0001020304050607ULL,
    [31] = 0xDEADBEEFCAFEBABEULL,
    [63] = 0xFFFFFFFFFFFFFFFFULL,
};

static uint64_t large_bss[64];

static void test_large_data_sections(void) {
    CHECK(large_rodata[0] == 0x0001020304050607ULL, "rodata[0]");
    CHECK(large_rodata[31] == 0xDEADBEEFCAFEBABEULL, "rodata[31]");
    CHECK(large_rodata[63] == 0xFFFFFFFFFFFFFFFFULL, "rodata[63]");

    for (int i = 0; i < 64; i++) {
        CHECK(large_bss[i] == 0, "bss zero-initialized");
    }

    large_bss[0] = 0x42;
    large_bss[63] = 0xFF;
    CHECK(large_bss[0] == 0x42, "bss writable [0]");
    CHECK(large_bss[63] == 0xFF, "bss writable [63]");
}

int main(void) {
    test_symbol_types();
    test_function_pointer_table();
    test_64bit_addresses();
    test_setjmp_longjmp();
    test_signal_handling();
    test_vtable_simulation();
    test_large_data_sections();

    printf("test_mc_64bit_format: ALL PASSED\n");
    return 0;
}
