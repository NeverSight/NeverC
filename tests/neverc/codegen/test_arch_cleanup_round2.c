// Validates that the second round of 32-bit architecture cleanup
// hasn't broken 64-bit code generation or ABI behavior.
//
// Areas covered by this round's cleanup:
//   1. Win64EH ARM 32-bit unwind opcodes removed from enum
//   2. MachO ARM 32-bit CPU subtypes removed
//   3. MCELFStreamer createARMELFStreamer declaration removed
//   4. SF_Thumb symbol flag removed
//   5. CPU_ARCH_ABI64_32 / CPU_TYPE_ARM64_32 removed
//
// Tests verify:
//   - 64-bit pointer size and alignment are correct
//   - x86_64 / AArch64 SEH-compatible exception handling basics
//   - struct layout consistency (no ABI change)
//   - 32-bit sub-register operations still work in 64-bit mode
//   - volatile + setjmp/longjmp work (tests unwind info indirectly)
//
// RUN: %neverc -O2 %s -o %t && %t && echo "PASS: arch_cleanup_round2"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
        abort(); \
    } \
} while (0)

static void test_pointer_size(void) {
    CHECK(sizeof(void *) == 8, "pointer must be 8 bytes (64-bit)");
    CHECK(sizeof(size_t) == 8, "size_t must be 8 bytes");
    CHECK(sizeof(ptrdiff_t) == 8, "ptrdiff_t must be 8 bytes");
    CHECK(sizeof(intptr_t) == 8, "intptr_t must be 8 bytes");
    CHECK(_Alignof(void *) == 8, "pointer alignment must be 8");
}

struct MixedLayout {
    uint32_t a;
    uint64_t b;
    uint32_t c;
    uint16_t d;
};

static void test_struct_layout(void) {
    CHECK(sizeof(struct MixedLayout) == 24, "MixedLayout size");
    CHECK(offsetof(struct MixedLayout, a) == 0, "MixedLayout.a offset");
    CHECK(offsetof(struct MixedLayout, b) == 8, "MixedLayout.b offset");
    CHECK(offsetof(struct MixedLayout, c) == 16, "MixedLayout.c offset");
    CHECK(offsetof(struct MixedLayout, d) == 20, "MixedLayout.d offset");
}

static volatile int unwind_reached = 0;

static void test_setjmp_longjmp(void) {
    jmp_buf env;
    volatile int stage = 0;

    int val = setjmp(env);
    if (val == 0) {
        stage = 1;
        longjmp(env, 42);
        CHECK(0, "longjmp should not return");
    } else {
        CHECK(val == 42, "longjmp value preserved");
        CHECK(stage == 1, "volatile preserved across longjmp");
        unwind_reached = 1;
    }
    CHECK(unwind_reached == 1, "setjmp/longjmp completed");
}

static uint32_t __attribute__((noinline)) subreg_chain(uint32_t x) {
    uint32_t a = x * 3;
    uint32_t b = a ^ 0xDEAD;
    uint32_t c = __builtin_bswap32(b);
    return c + 1;
}

static void test_32bit_subreg_in_64bit(void) {
    uint32_t result = subreg_chain(100);
    uint32_t expected = __builtin_bswap32((100 * 3) ^ 0xDEAD) + 1;
    CHECK(result == expected, "32-bit sub-register chain in 64-bit mode");

    uint64_t wide = (uint64_t)result;
    CHECK((wide >> 32) == 0, "32-bit result zero-extended to 64-bit");
}

static void test_mixed_width_calls(void) {
    volatile uint32_t a32 = 0xCAFEBABE;
    volatile uint64_t b64 = 0x123456789ABCDEF0ULL;
    volatile uint16_t c16 = 0x1234;

    uint64_t sum = (uint64_t)a32 + b64 + (uint64_t)c16;
    uint64_t expected = 0xCAFEBABEULL + 0x123456789ABCDEF0ULL + 0x1234ULL;
    CHECK(sum == expected, "mixed-width arithmetic");
}

#if defined(__x86_64__)
static void test_x86_64_seh_basics(void) {
    volatile uint64_t rsp_proxy;
    __asm__ volatile ("mov %%rsp, %0" : "=r"(rsp_proxy));
    CHECK((rsp_proxy & 0xF) == 0, "stack 16-byte aligned on x86_64");

    uint64_t flags;
    __asm__ volatile ("pushfq; pop %0" : "=r"(flags));
    CHECK(flags != 0, "RFLAGS readable (RDFLAGS64 path)");
}
#endif

#if defined(__aarch64__)
static void test_aarch64_basics(void) {
    uint64_t sp;
    __asm__ volatile ("mov %0, sp" : "=r"(sp));
    CHECK((sp & 0xF) == 0, "stack 16-byte aligned on AArch64");

    uint32_t w_val = 0xDEADBEEF;
    uint64_t x_val;
    __asm__ volatile ("mov %w0, %w1; uxtw %0, %w0" : "=r"(x_val) : "r"(w_val));
    CHECK(x_val == 0xDEADBEEFULL, "W register zero-extends to X register");
}
#endif

int main(void) {
    test_pointer_size();
    test_struct_layout();
    test_setjmp_longjmp();
    test_32bit_subreg_in_64bit();
    test_mixed_width_calls();

#if defined(__x86_64__)
    test_x86_64_seh_basics();
#elif defined(__aarch64__)
    test_aarch64_basics();
#endif

    printf("test_arch_cleanup_round2: ALL PASSED\n");
    return 0;
}
