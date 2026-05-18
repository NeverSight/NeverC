// NeverC test: GNU C extensions (used heavily by Linux kernel)
// RUN: %neverc -std=gnu11 -Wall -Wextra -Werror -Wno-gnu -fsyntax-only %s
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// --- typeof (GNU) ---
#define max(a, b) ({           \
    typeof(a) _a = (a);        \
    typeof(b) _b = (b);        \
    _a > _b ? _a : _b;        \
})

#define min(a, b) ({           \
    typeof(a) _a = (a);        \
    typeof(b) _b = (b);        \
    _a < _b ? _a : _b;        \
})

// --- statement expressions ---
#define swap(a, b) do {        \
    typeof(a) _t = (a);        \
    (a) = (b);                 \
    (b) = _t;                  \
} while (0)

// --- __attribute__ ---
static void die(void) __attribute__((noreturn));
static void die(void) { __builtin_trap(); }

__attribute__((unused))
static int helper(void) { return 0; }

__attribute__((warn_unused_result))
static int must_check(void) { return 0; }

__attribute__((always_inline))
static inline int force_inlined(int x) { return x * 2; }

__attribute__((noinline))
static int not_inlined(int x) { return x + 1; }

struct __attribute__((packed)) PackedStruct {
    char a;
    int  b;
    char c;
};
_Static_assert(sizeof(struct PackedStruct) == 1 + 4 + 1, "");

__attribute__((aligned(64)))
static int aligned_var;

#ifdef __MACH__
static int section_var = 42;
#else
__attribute__((section(".mydata")))
static int section_var = 42;
#endif

__attribute__((cold))
static void cold_path(void) { (void)0; }

__attribute__((hot))
static void hot_path(void) { (void)0; }

__attribute__((format(printf, 1, 2)))
static int my_printf(const char *fmt, ...) { (void)fmt; return 0; }

__attribute__((constructor))
static void init_func(void) { (void)0; }

__attribute__((destructor))
static void fini_func(void) { (void)0; }

__attribute__((visibility("hidden")))
int hidden_func(void) { return 0; }

__attribute__((weak))
int weak_func(void) { return -1; }

// --- __builtin_* ---
void test_builtins(void) {
    int x = 123;
    (void)__builtin_expect(x > 0, 1);
    (void)__builtin_clz((unsigned)x);
    (void)__builtin_ctz(8u);
    (void)__builtin_popcount(0xFFu);
    (void)__builtin_bswap32(0x12345678u);
    (void)__builtin_bswap64(0x123456789ABCDEF0ULL);
    (void)__builtin_ffs(x);

    char buf[64];
    __builtin_memset(buf, 0, sizeof(buf));
    __builtin_memcpy(buf, "hello", 5);
    (void)__builtin_strlen("test");

    (void)__builtin_offsetof(struct PackedStruct, b);

    if (__builtin_constant_p(42)) {
        (void)0;
    }

    // __builtin_unreachable / __builtin_trap are tested via die() above

    _Static_assert(__builtin_types_compatible_p(int, int), "same");
    _Static_assert(!__builtin_types_compatible_p(int, long), "diff");

    int *p = __builtin_assume_aligned(buf, 8);
    (void)p;
}

// --- __extension__ ---
__extension__ typedef long long ext_ll;

// --- zero-length array (GCC extension) ---
struct ZeroTail {
    int len;
    char data[0];
};

// --- case ranges ---
int classify_char(int c) {
    switch (c) {
    case 'a' ... 'z': return 1;
    case 'A' ... 'Z': return 2;
    case '0' ... '9': return 3;
    default: return 0;
    }
}

// --- labels as values (computed goto) ---
void computed_goto(int idx) {
    static void *label_table[] = { &&label_a, &&label_b, &&label_end };
    if (idx < 0 || idx > 2) return;
    goto *label_table[idx];
label_a:
    return;
label_b:
    return;
label_end:
    return;
}

// --- __auto_type ---
void test_auto_type(void) {
    __auto_type a = 42;
    __auto_type b = 3.14;
    (void)a; (void)b;
}

// --- container_of pattern (kernel-style) ---
#define container_of(ptr, type, member) ({             \
    const typeof(((type *)0)->member) *__mptr = (ptr); \
    (type *)((char *)__mptr - offsetof(type, member)); \
})

struct ListNode { struct ListNode *next, *prev; };
struct MyObj {
    int data;
    struct ListNode node;
};

void test_container_of(void) {
    struct MyObj obj = {.data = 99, .node = {0}};
    struct ListNode *n = &obj.node;
    struct MyObj *back = container_of(n, struct MyObj, node);
    (void)back;
}

// --- __COUNTER__ ---
#define UNIQUE_NAME(prefix) PASTE2(prefix, __COUNTER__)
#define PASTE2(a, b) PASTE2_(a, b)
#define PASTE2_(a, b) a##b
int UNIQUE_NAME(uniq_) = 1;
int UNIQUE_NAME(uniq_) = 2;

int main(void) {
    (void)max(3, 5);
    (void)min(3, 5);
    int a = 1, b = 2;
    swap(a, b);
    (void)force_inlined(10);
    (void)not_inlined(10);
    (void)aligned_var;
    (void)section_var;
    cold_path();
    hot_path();
    my_printf("test %d", 42);
    test_builtins();
    (void)classify_char('x');
    test_auto_type();
    test_container_of();
    (void)(int)must_check();
    return 0;
}
