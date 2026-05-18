// RUN: %neverc -fsyntax-only %s
/*
 * NeverC Compiler Validation - Advanced GNU C Extensions
 *
 * Tests GNU extensions commonly used in kernel/systems code:
 *  1.  __int128 type
 *  2.  Computed goto (&&label, goto *ptr)
 *  3.  __attribute__((cleanup)) auto-cleanup
 *  4.  __attribute__((vector_size)) SIMD vectors
 *  5.  __attribute__((format(printf,...))) format checking
 *  6.  __attribute__((weak)) weak symbols
 *  7.  __attribute__((visibility)) ELF visibility
 *  8.  __attribute__((transparent_union))
 *  9.  __attribute__((packed)) dense layout
 * 10.  __attribute__((fallthrough)) / [[fallthrough]]
 * 11.  __auto_type (GNU auto)
 * 12.  Inline asm with goto labels
 * 13.  Bit manipulation builtins
 * 14.  Case ranges (case 1 ... 5:)
 * 15.  __builtin_assume / __builtin_assume_aligned
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define ASSERT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "ASSERT FAILED: %s @ %s:%d\n", #cond, __FILE__, __LINE__); \
        abort(); \
    } \
} while (0)

/* ================================================================
 * 1. __int128 type
 * ================================================================ */
static int test_int128(void) {
    __int128 big = (__int128)1 << 100;
    ASSERT(big != 0);
    ASSERT(big > 0);

    unsigned __int128 ubig = (unsigned __int128)0xFFFFFFFFFFFFFFFFULL;
    ubig = ubig * ubig;
    ASSERT(ubig != 0);

    __int128 a = 100;
    __int128 b = 200;
    __int128 c = a * b;
    ASSERT((long long)c == 20000LL);

    unsigned __int128 x = 1;
    x <<= 64;
    x |= 0xDEADBEEF;
    ASSERT((uint64_t)(x & 0xFFFFFFFF) == 0xDEADBEEF);
    ASSERT((uint64_t)(x >> 64) == 1);

    return 0;
}

/* ================================================================
 * 2. Computed goto (&&label, goto *ptr)
 * Used in kernel interpreters (BPF, etc.)
 * ================================================================ */
enum opcode { OP_ADD = 0, OP_SUB, OP_MUL, OP_HALT, OP_COUNT };

static int test_computed_goto(void) {
    static void *dispatch_table[] = {
        &&op_add, &&op_sub, &&op_mul, &&op_halt
    };

    enum opcode program[] = { OP_ADD, OP_MUL, OP_SUB, OP_ADD, OP_HALT };
    int acc = 10;
    int pc = 0;

    goto *dispatch_table[program[pc]];

op_add:
    acc += 5;
    pc++;
    goto *dispatch_table[program[pc]];

op_sub:
    acc -= 3;
    pc++;
    goto *dispatch_table[program[pc]];

op_mul:
    acc *= 2;
    pc++;
    goto *dispatch_table[program[pc]];

op_halt:
    ASSERT(acc == ((10 + 5) * 2 - 3 + 5));
    return 0;
}

/* ================================================================
 * 3. __attribute__((cleanup)) auto-cleanup pattern
 * Common in systemd, GLib, kernel mutex guards
 * ================================================================ */
static void cleanup_free(void *p) {
    free(*(void **)p);
}

#define _cleanup_free_ __attribute__((cleanup(cleanup_free)))

static int cleanup_counter = 0;

static void cleanup_int(int *p) {
    (void)p;
    cleanup_counter++;
}

static int test_cleanup_attribute(void) {
    cleanup_counter = 0;

    {
        __attribute__((cleanup(cleanup_int))) int a = 1;
        __attribute__((cleanup(cleanup_int))) int b = 2;
        (void)a; (void)b;
    }
    ASSERT(cleanup_counter == 2);

    {
        _cleanup_free_ char *buf = malloc(64);
        ASSERT(buf != NULL);
        strcpy(buf, "auto-freed");
        ASSERT(strcmp(buf, "auto-freed") == 0);
    }

    return 0;
}

/* ================================================================
 * 4. __attribute__((vector_size)) SIMD vectors
 * ================================================================ */
typedef int v4si __attribute__((vector_size(16)));
typedef float v4sf __attribute__((vector_size(16)));

static int test_vector_extensions(void) {
    v4si a = { 1, 2, 3, 4 };
    v4si b = { 10, 20, 30, 40 };
    v4si c = a + b;

    ASSERT(c[0] == 11 && c[1] == 22 && c[2] == 33 && c[3] == 44);

    v4si d = a * b;
    ASSERT(d[0] == 10 && d[1] == 40 && d[2] == 90 && d[3] == 160);

    v4sf fa = { 1.0f, 2.0f, 3.0f, 4.0f };
    v4sf fb = { 0.5f, 0.5f, 0.5f, 0.5f };
    v4sf fc = fa * fb;
    ASSERT(fc[0] == 0.5f && fc[3] == 2.0f);

    v4si mask = a > (v4si){2, 2, 2, 2};
    ASSERT(mask[0] == 0);
    ASSERT(mask[2] != 0);

    return 0;
}

/* ================================================================
 * 5. __attribute__((format(printf,...)))
 * ================================================================ */
__attribute__((format(printf, 2, 3)))
static int my_log(int level, const char *fmt, ...) {
    (void)level; (void)fmt;
    return 0;
}

__attribute__((format(printf, 1, 2)))
static char *my_asprintf(const char *fmt, ...) {
    (void)fmt;
    return NULL;
}

static int test_format_attribute(void) {
    my_log(0, "test %d %s", 42, "hello");
    my_log(1, "value=%lu", 100UL);
    (void)my_asprintf("x=%d", 42);
    return 0;
}

/* ================================================================
 * 6. __attribute__((weak)) weak symbols
 * ================================================================ */
__attribute__((weak)) int weak_global = 42;

__attribute__((weak))
int weak_function(int x) {
    return x + 1;
}

static int test_weak_symbols(void) {
    ASSERT(weak_global == 42);
    ASSERT(weak_function(10) == 11);
    return 0;
}

/* ================================================================
 * 7. __attribute__((visibility))
 * ================================================================ */
__attribute__((visibility("default")))
int visible_function(void) { return 1; }

__attribute__((visibility("hidden")))
int hidden_function(void) { return 2; }

static int test_visibility(void) {
    ASSERT(visible_function() == 1);
    ASSERT(hidden_function() == 2);
    return 0;
}

/* ================================================================
 * 8. __attribute__((transparent_union))
 * ================================================================ */
typedef union {
    int *ip;
    long *lp;
    void *vp;
} __attribute__((transparent_union)) multi_ptr;

static int read_via_multi(multi_ptr p) {
    return *(int *)p.vp;
}

static int test_transparent_union(void) {
    int x = 42;
    int result = read_via_multi(&x);
    ASSERT(result == 42);
    return 0;
}

/* ================================================================
 * 9. __attribute__((packed)) dense layout
 * ================================================================ */
struct __attribute__((packed)) packet_header {
    uint8_t  version;
    uint16_t length;
    uint32_t sequence;
    uint8_t  flags;
};

struct mixed_packed {
    uint8_t a;
    uint32_t b __attribute__((packed));
    uint8_t c;
};

static int test_packed(void) {
    ASSERT(sizeof(struct packet_header) == 8);
    ASSERT(__builtin_offsetof(struct packet_header, version) == 0);
    ASSERT(__builtin_offsetof(struct packet_header, length) == 1);
    ASSERT(__builtin_offsetof(struct packet_header, sequence) == 3);
    ASSERT(__builtin_offsetof(struct packet_header, flags) == 7);

    struct packet_header hdr = {
        .version = 1,
        .length = 100,
        .sequence = 12345,
        .flags = 0xFF,
    };
    ASSERT(hdr.version == 1);
    ASSERT(hdr.length == 100);
    ASSERT(hdr.sequence == 12345);

    return 0;
}

/* ================================================================
 * 10. [[fallthrough]] / __attribute__((fallthrough))
 * ================================================================ */
static int classify_char(char c) {
    int result = 0;
    switch (c) {
    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
        result = 1;
        break;
    case 'a' ... 'f':
    case 'A' ... 'F':
        result = 2;
        __attribute__((fallthrough));
    case 'g' ... 'z':
    case 'G' ... 'Z':
        result += 10;
        break;
    default:
        result = -1;
        break;
    }
    return result;
}

static int test_fallthrough(void) {
    ASSERT(classify_char('5') == 1);
    ASSERT(classify_char('a') == 12);
    ASSERT(classify_char('z') == 10);
    ASSERT(classify_char('!') == -1);
    return 0;
}

/* ================================================================
 * 11. __auto_type (GNU auto)
 * ================================================================ */
#define MAX_GNU(a, b) ({ \
    __auto_type _a = (a); \
    __auto_type _b = (b); \
    _a > _b ? _a : _b; \
})

#define SWAP_GNU(a, b) do { \
    __auto_type _tmp = (a); \
    (a) = (b); \
    (b) = _tmp; \
} while (0)

static int test_auto_type(void) {
    ASSERT(MAX_GNU(3, 7) == 7);
    ASSERT(MAX_GNU(3.14, 2.71) == 3.14);
    ASSERT(MAX_GNU(-1, -5) == -1);

    int x = 10, y = 20;
    SWAP_GNU(x, y);
    ASSERT(x == 20 && y == 10);

    double a = 1.5, b = 2.5;
    SWAP_GNU(a, b);
    ASSERT(a == 2.5 && b == 1.5);

    return 0;
}

/* ================================================================
 * 12. Inline asm (basic; no asm-goto on all targets but basic asm works)
 * ================================================================ */
static int test_inline_asm(void) {
    int result = 0;
#if defined(__aarch64__)
    int a = 5, b = 3;
    __asm__ volatile(
        "add %w0, %w1, %w2"
        : "=r"(result)
        : "r"(a), "r"(b)
    );
    ASSERT(result == 8);
#elif defined(__x86_64__)
    int a = 5, b = 3;
    __asm__ volatile(
        "addl %2, %0"
        : "=r"(result)
        : "0"(a), "r"(b)
    );
    ASSERT(result == 8);
#else
    result = 8;
#endif
    __asm__ volatile("" ::: "memory");
    return 0;
}

/* ================================================================
 * 13. Bit manipulation builtins
 * ================================================================ */
static int test_bit_builtins(void) {
    ASSERT(__builtin_clz(1) == 31);
    ASSERT(__builtin_clz(0x80000000U) == 0);
    ASSERT(__builtin_ctz(8) == 3);
    ASSERT(__builtin_ctz(1) == 0);
    ASSERT(__builtin_popcount(0xFF) == 8);
    ASSERT(__builtin_popcount(0) == 0);
    ASSERT(__builtin_popcount(0x55555555) == 16);
    ASSERT(__builtin_ffs(0) == 0);
    ASSERT(__builtin_ffs(1) == 1);
    ASSERT(__builtin_ffs(8) == 4);
    ASSERT(__builtin_parity(0xFF) == 0);
    ASSERT(__builtin_parity(0x7F) == 1);

    ASSERT(__builtin_clzll(1ULL) == 63);
    ASSERT(__builtin_ctzll(1ULL << 40) == 40);
    ASSERT(__builtin_popcountll(0xFFFFFFFFFFFFFFFFULL) == 64);

    ASSERT(__builtin_bswap16(0x1234) == 0x3412);
    ASSERT(__builtin_bswap32(0x12345678U) == 0x78563412U);
    ASSERT(__builtin_bswap64(0x0102030405060708ULL) == 0x0807060504030201ULL);

    return 0;
}

/* ================================================================
 * 14. Case ranges (GNU extension: case 1 ... 5:)
 * ================================================================ */
static const char *temp_range(int celsius) {
    switch (celsius) {
    case -40 ... -1: return "freezing";
    case 0 ... 10:   return "cold";
    case 11 ... 20:  return "cool";
    case 21 ... 30:  return "warm";
    case 31 ... 50:  return "hot";
    default:         return "extreme";
    }
}

static int test_case_ranges(void) {
    ASSERT(strcmp(temp_range(-10), "freezing") == 0);
    ASSERT(strcmp(temp_range(5), "cold") == 0);
    ASSERT(strcmp(temp_range(15), "cool") == 0);
    ASSERT(strcmp(temp_range(25), "warm") == 0);
    ASSERT(strcmp(temp_range(35), "hot") == 0);
    ASSERT(strcmp(temp_range(100), "extreme") == 0);
    return 0;
}

/* ================================================================
 * 15. __builtin_assume_aligned
 * ================================================================ */
static int sum_aligned(const int *data, int n) {
    const int *p = __builtin_assume_aligned(data, 16);
    int total = 0;
    for (int i = 0; i < n; i++)
        total += p[i];
    return total;
}

static int test_assume_aligned(void) {
    _Alignas(16) int data[4] = {1, 2, 3, 4};
    ASSERT(sum_aligned(data, 4) == 10);
    return 0;
}

/* ================================================================
 * Main
 * ================================================================ */
int main(void) {
    int failures = 0;
#define RUN(fn) do { \
    if (fn() != 0) { fprintf(stderr, "FAIL: " #fn "\n"); failures++; } \
} while (0)

    RUN(test_int128);
    RUN(test_computed_goto);
    RUN(test_cleanup_attribute);
    RUN(test_vector_extensions);
    RUN(test_format_attribute);
    RUN(test_weak_symbols);
    RUN(test_visibility);
    RUN(test_transparent_union);
    RUN(test_packed);
    RUN(test_fallthrough);
    RUN(test_auto_type);
    RUN(test_inline_asm);
    RUN(test_bit_builtins);
    RUN(test_case_ranges);
    RUN(test_assume_aligned);

#undef RUN
    if (failures == 0)
        printf("test_gnu_advanced: ALL PASSED\n");
    else
        printf("test_gnu_advanced: %d FAILED\n", failures);
    return failures;
}
