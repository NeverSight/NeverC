// RUN: %neverc -fsyntax-only %s
/*
 * NeverC Compiler Validation - Advanced Kernel C Patterns (Part 2)
 *
 * Supplementary patterns extracted from Android 12 (5.10) kernel
 * that are NOT covered by test_kernel_advanced.c or other tests.
 *
 * Categories:
 *  1.  ERR_PTR / PTR_ERR / IS_ERR (error-in-pointer encoding)
 *  2.  Elvis operator (GNU ?: with omitted middle operand)
 *  3.  list_for_each_entry (typeof-based intrusive list iteration)
 *  4.  Bit-stealing in pointers (rb_parent_color pattern)
 *  5.  Range designated initializer [0 ... N] = value
 *  6.  __attribute__((constructor/destructor))
 *  7.  __attribute__((alias))
 *  8.  __attribute__((nonnull/returns_nonnull/sentinel/alloc_size))
 *  9.  Compound literal as function argument
 * 10.  Union type punning
 * 11.  Nested struct with complex designated init
 * 12.  Function pointer table (vtable pattern)
 * 13.  READ_ONCE / WRITE_ONCE runtime verification
 * 14.  Static variable in statement expression (DO_ONCE pattern)
 * 15.  Zero-length array + flexible array member interop
 * 16.  Pointer arithmetic with cast-through-uintptr_t
 * 17.  X-macro pattern
 * 18.  Comma operator in complex expressions
 * 19.  __attribute__((used/externally_visible))
 * 20.  Nested function-like macro with stringification
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stddef.h>

#define ASSERT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "ASSERT FAILED: %s @ %s:%d\n", #cond, __FILE__, __LINE__); \
        abort(); \
    } \
} while (0)

/* ================================================================
 * 1. ERR_PTR / PTR_ERR / IS_ERR (error-in-pointer encoding)
 * Source: include/linux/err.h
 * ================================================================ */

#define MAX_ERRNO 4095

#define IS_ERR_VALUE(x) ((unsigned long)(void *)(x) >= (unsigned long)-MAX_ERRNO)

static inline void *ERR_PTR(long error) {
    return (void *)error;
}

static inline long PTR_ERR(const void *ptr) {
    return (long)ptr;
}

static inline bool IS_ERR(const void *ptr) {
    return IS_ERR_VALUE((unsigned long)ptr);
}

static inline bool IS_ERR_OR_NULL(const void *ptr) {
    return !ptr || IS_ERR_VALUE((unsigned long)ptr);
}

static inline int PTR_ERR_OR_ZERO(const void *ptr) {
    if (IS_ERR(ptr))
        return (int)PTR_ERR(ptr);
    return 0;
}

static void test_err_ptr(void) {
    void *good = (void *)0x1000;
    void *bad = ERR_PTR(-12);  /* -ENOMEM */
    void *null = NULL;

    ASSERT(!IS_ERR(good));
    ASSERT(IS_ERR(bad));
    ASSERT(PTR_ERR(bad) == -12);
    ASSERT(!IS_ERR(null));

    ASSERT(!IS_ERR_OR_NULL(good));
    ASSERT(IS_ERR_OR_NULL(bad));
    ASSERT(IS_ERR_OR_NULL(null));

    ASSERT(PTR_ERR_OR_ZERO(good) == 0);
    ASSERT(PTR_ERR_OR_ZERO(bad) == -12);
}

/* ================================================================
 * 2. Elvis operator (GNU ?: with omitted middle operand)
 * Source: include/linux/list.h (list_prepare_entry)
 * ================================================================ */

static void test_elvis_operator(void) {
    int a = 42;
    int b = 0;
    ASSERT((a ?: 99) == 42);
    ASSERT((b ?: 99) == 99);

    char *s1 = "hello";
    char *s2 = NULL;
    ASSERT((s1 ?: "default") == s1);
    ASSERT(strcmp(s2 ?: "default", "default") == 0);

    int val = 0;
    ASSERT((val ?: -1) == -1);
    val = 5;
    ASSERT((val ?: -1) == 5);
}

/* ================================================================
 * 3. list_for_each_entry (typeof-based intrusive list iteration)
 * Source: include/linux/list.h
 * ================================================================ */

#define container_of(ptr, type, member) ({ \
    const typeof(((type *)0)->member) *__mptr = (ptr); \
    (type *)((char *)__mptr - offsetof(type, member)); })

#define READ_ONCE(x) (*(const volatile typeof(x) *)&(x))
#define WRITE_ONCE(x, val) (*(volatile typeof(x) *)&(x) = (val))

struct list_head {
    struct list_head *next, *prev;
};

#define LIST_HEAD_INIT(name) { &(name), &(name) }
#define LIST_HEAD(name) struct list_head name = LIST_HEAD_INIT(name)

static inline void INIT_LIST_HEAD(struct list_head *list) {
    WRITE_ONCE(list->next, list);
    list->prev = list;
}

static inline void __list_add(struct list_head *new,
                              struct list_head *prev,
                              struct list_head *next) {
    next->prev = new;
    new->next = next;
    new->prev = prev;
    WRITE_ONCE(prev->next, new);
}

static inline void list_add_tail(struct list_head *new, struct list_head *head) {
    __list_add(new, head->prev, head);
}

static inline int list_empty(const struct list_head *head) {
    return READ_ONCE(head->next) == head;
}

static inline void __list_del(struct list_head *prev, struct list_head *next) {
    next->prev = prev;
    WRITE_ONCE(prev->next, next);
}

static inline void list_del(struct list_head *entry) {
    __list_del(entry->prev, entry->next);
    entry->next = NULL;
    entry->prev = NULL;
}

#define list_entry(ptr, type, member) container_of(ptr, type, member)
#define list_first_entry(ptr, type, member) \
    list_entry((ptr)->next, type, member)
#define list_next_entry(pos, member) \
    list_entry((pos)->member.next, typeof(*(pos)), member)
#define list_entry_is_head(pos, head, member) \
    (&pos->member == (head))
#define list_for_each_entry(pos, head, member) \
    for (pos = list_first_entry(head, typeof(*pos), member); \
         !list_entry_is_head(pos, head, member); \
         pos = list_next_entry(pos, member))

#define list_first_entry_or_null(ptr, type, member) ({ \
    struct list_head *head__ = (ptr); \
    struct list_head *pos__ = READ_ONCE(head__->next); \
    pos__ != head__ ? list_entry(pos__, type, member) : NULL; \
})

#define list_prepare_entry(pos, head, member) \
    ((pos) ?: list_entry(head, typeof(*pos), member))

struct task {
    int pid;
    char name[16];
    struct list_head list;
};

static void test_list_for_each_entry(void) {
    LIST_HEAD(tasks);

    struct task t1 = { .pid = 1, .name = "init" };
    struct task t2 = { .pid = 2, .name = "kthread" };
    struct task t3 = { .pid = 3, .name = "bash" };
    INIT_LIST_HEAD(&t1.list);
    INIT_LIST_HEAD(&t2.list);
    INIT_LIST_HEAD(&t3.list);

    list_add_tail(&t1.list, &tasks);
    list_add_tail(&t2.list, &tasks);
    list_add_tail(&t3.list, &tasks);

    int count = 0;
    int pids[3];
    struct task *pos;
    list_for_each_entry(pos, &tasks, list) {
        pids[count++] = pos->pid;
    }
    ASSERT(count == 3);
    ASSERT(pids[0] == 1 && pids[1] == 2 && pids[2] == 3);

    struct task *first = list_first_entry(&tasks, struct task, list);
    ASSERT(first->pid == 1);

    struct task *first_or_null = list_first_entry_or_null(&tasks, struct task, list);
    ASSERT(first_or_null != NULL && first_or_null->pid == 1);

    LIST_HEAD(empty);
    struct task *null_entry = list_first_entry_or_null(&empty, struct task, list);
    ASSERT(null_entry == NULL);

    list_del(&t2.list);
    count = 0;
    list_for_each_entry(pos, &tasks, list) {
        count++;
    }
    ASSERT(count == 2);
}

/* ================================================================
 * 4. Bit-stealing in pointers (rb_parent_color pattern)
 * Source: include/linux/rbtree.h
 * ================================================================ */

struct rb_node {
    unsigned long __rb_parent_color;
    struct rb_node *rb_right;
    struct rb_node *rb_left;
} __attribute__((aligned(sizeof(long))));

struct rb_root { struct rb_node *rb_node; };

#define rb_parent(r) ((struct rb_node *)((r)->__rb_parent_color & ~3UL))
#define RB_RED   0
#define RB_BLACK 1
#define rb_color(r) ((int)((r)->__rb_parent_color & 1))
#define rb_set_parent_color(rb, p, color) \
    ((rb)->__rb_parent_color = (unsigned long)(p) | (color))
#define RB_EMPTY_NODE(node) ((node)->__rb_parent_color == (unsigned long)(node))
#define RB_CLEAR_NODE(node) ((node)->__rb_parent_color = (unsigned long)(node))

static void test_rb_bit_stealing(void) {
    struct rb_node parent __attribute__((aligned(4)));
    struct rb_node child;
    memset(&parent, 0, sizeof(parent));
    memset(&child, 0, sizeof(child));

    rb_set_parent_color(&child, &parent, RB_RED);
    ASSERT(rb_parent(&child) == &parent);
    ASSERT(rb_color(&child) == RB_RED);

    rb_set_parent_color(&child, &parent, RB_BLACK);
    ASSERT(rb_parent(&child) == &parent);
    ASSERT(rb_color(&child) == RB_BLACK);

    RB_CLEAR_NODE(&child);
    ASSERT(RB_EMPTY_NODE(&child));
}

/* ================================================================
 * 5. Range designated initializer [0 ... N] = value
 * Source: include/linux/hashtable.h (DEFINE_HASHTABLE)
 * ================================================================ */

static void test_range_designated_init(void) {
    int table[16] = { [0 ... 15] = -1 };
    for (int i = 0; i < 16; i++)
        ASSERT(table[i] == -1);

    int mixed[10] = { [0 ... 4] = 1, [5 ... 9] = 2 };
    ASSERT(mixed[0] == 1 && mixed[4] == 1);
    ASSERT(mixed[5] == 2 && mixed[9] == 2);

    char lookup[256] = {
        ['0' ... '9'] = 1,
        ['a' ... 'f'] = 2,
        ['A' ... 'F'] = 2,
    };
    ASSERT(lookup['5'] == 1);
    ASSERT(lookup['c'] == 2);
    ASSERT(lookup['D'] == 2);
    ASSERT(lookup['z'] == 0);
}

/* ================================================================
 * 6. __attribute__((constructor/destructor))
 * Source: kernel module init patterns
 * ================================================================ */

static int constructor_called = 0;
static int destructor_order = 0;

__attribute__((constructor))
static void my_constructor(void) {
    constructor_called = 1;
}

__attribute__((constructor(200)))
static void my_constructor_priority(void) {
    if (constructor_called)
        constructor_called = 2;
}

__attribute__((destructor))
static void my_destructor(void) {
    destructor_order++;
}

static void test_constructor_destructor(void) {
    ASSERT(constructor_called >= 1);
}

/* ================================================================
 * 7. __attribute__((nonnull/sentinel/const/pure/malloc))
 * Source: include/linux/compiler_attributes.h
 * ================================================================ */

__attribute__((nonnull(1)))
static int str_len(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

__attribute__((const))
static int pure_square(int x) {
    return x * x;
}

__attribute__((pure))
static int pure_sum(const int *arr, int n) {
    int s = 0;
    for (int i = 0; i < n; i++) s += arr[i];
    return s;
}

__attribute__((sentinel))
static int count_args(const char *first, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, first);
    int count = 1;
    while (__builtin_va_arg(ap, const char *) != NULL) count++;
    __builtin_va_end(ap);
    return count;
}

static void test_function_attributes(void) {
    ASSERT(str_len("hello") == 5);
    ASSERT(pure_square(7) == 49);
    int arr[] = {1, 2, 3, 4, 5};
    ASSERT(pure_sum(arr, 5) == 15);
    ASSERT(count_args("a", "b", "c", NULL) == 3);
}

/* ================================================================
 * 8. Compound literal as function argument
 * Source: various kernel initialization patterns
 * ================================================================ */

struct point { int x; int y; };

static int point_distance_sq(const struct point *a, const struct point *b) {
    int dx = a->x - b->x;
    int dy = a->y - b->y;
    return dx * dx + dy * dy;
}

static int sum_array(const int *arr, int n) {
    int s = 0;
    for (int i = 0; i < n; i++) s += arr[i];
    return s;
}

static void test_compound_literal_arg(void) {
    int d = point_distance_sq(
        &(struct point){3, 4},
        &(struct point){0, 0}
    );
    ASSERT(d == 25);

    ASSERT(sum_array((int[]){10, 20, 30}, 3) == 60);

    struct point *p = &(struct point){ .x = 7, .y = 8 };
    ASSERT(p->x == 7 && p->y == 8);
}

/* ================================================================
 * 9. Union type punning
 * Source: various kernel low-level code
 * ================================================================ */

union float_bits {
    float f;
    uint32_t u;
};

union ptr_or_val {
    void *ptr;
    unsigned long val;
};

static void test_union_type_punning(void) {
    union float_bits fb;
    fb.f = 1.0f;
    ASSERT(fb.u == 0x3F800000);

    union ptr_or_val pv;
    int x = 42;
    pv.ptr = &x;
    ASSERT(pv.val == (unsigned long)&x);

    pv.val = 0;
    ASSERT(pv.ptr == NULL);

    union {
        uint8_t bytes[4];
        uint32_t word;
    } endian_check;
    endian_check.word = 0x01020304;
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    ASSERT(endian_check.bytes[0] == 0x04);
    ASSERT(endian_check.bytes[3] == 0x01);
#else
    ASSERT(endian_check.bytes[0] == 0x01);
    ASSERT(endian_check.bytes[3] == 0x04);
#endif
}

/* ================================================================
 * 10. Nested struct with complex designated init
 * Source: various kernel driver registration structs
 * ================================================================ */

struct file_operations {
    int (*open)(const char *, int);
    int (*read)(int, char *, int);
    int (*write)(int, const char *, int);
    int (*close)(int);
};

struct device_driver {
    const char *name;
    int id;
    struct {
        int major;
        int minor;
        int patch;
    } version;
    struct file_operations fops;
    const char *params[4];
};

static int my_open(const char *path, int flags) { (void)path; (void)flags; return 0; }
static int my_read(int fd, char *buf, int n) { (void)fd; (void)buf; (void)n; return 0; }

static void test_nested_designated_init(void) {
    struct device_driver drv = {
        .name = "test_driver",
        .id = 42,
        .version = { .major = 1, .minor = 2, .patch = 3 },
        .fops = {
            .open = my_open,
            .read = my_read,
            .write = NULL,
            .close = NULL,
        },
        .params = { "opt1", "opt2", NULL },
    };
    ASSERT(strcmp(drv.name, "test_driver") == 0);
    ASSERT(drv.version.major == 1);
    ASSERT(drv.fops.open == my_open);
    ASSERT(drv.fops.write == NULL);
    ASSERT(drv.params[2] == NULL);

    struct device_driver drv2 = {
        .name = "drv2",
        .fops.open = my_open,
        .fops.read = my_read,
    };
    ASSERT(drv2.fops.open == my_open);
    ASSERT(drv2.fops.close == NULL);
}

/* ================================================================
 * 11. Function pointer table (vtable pattern)
 * Source: kernel subsystem ops structs (file_operations, etc.)
 * ================================================================ */

struct ops_vtable {
    const char *name;
    int (*init)(void *ctx);
    void (*cleanup)(void *ctx);
    int (*process)(void *ctx, int cmd);
};

struct backend_ctx { int value; };

static int backend_init(void *ctx) {
    ((struct backend_ctx *)ctx)->value = 100;
    return 0;
}
static void backend_cleanup(void *ctx) {
    ((struct backend_ctx *)ctx)->value = 0;
}
static int backend_process(void *ctx, int cmd) {
    return ((struct backend_ctx *)ctx)->value + cmd;
}

static const struct ops_vtable backend_ops = {
    .name = "backend",
    .init = backend_init,
    .cleanup = backend_cleanup,
    .process = backend_process,
};

static void test_vtable(void) {
    struct backend_ctx ctx = {0};
    ASSERT(backend_ops.init(&ctx) == 0);
    ASSERT(ctx.value == 100);
    ASSERT(backend_ops.process(&ctx, 5) == 105);
    backend_ops.cleanup(&ctx);
    ASSERT(ctx.value == 0);

    const struct ops_vtable *ops = &backend_ops;
    ASSERT(strcmp(ops->name, "backend") == 0);

    typedef int (*process_fn)(void *, int);
    process_fn fn = ops->process;
    ctx.value = 50;
    ASSERT(fn(&ctx, 10) == 60);
}

/* ================================================================
 * 12. Static variable in statement expression (DO_ONCE pattern)
 * Source: include/linux/once.h
 * ================================================================ */

#define DO_ONCE_LITE(func, ...) ({ \
    static bool ___done = false; \
    bool ___ret = false; \
    if (!___done) { \
        func(__VA_ARGS__); \
        ___done = true; \
        ___ret = true; \
    } \
    ___ret; \
})

static int init_counter = 0;
static void increment_counter(void) { init_counter++; }

static bool call_do_once(void) {
    return DO_ONCE_LITE(increment_counter);
}

static void test_do_once(void) {
    ASSERT(init_counter == 0);

    bool first = call_do_once();
    ASSERT(first);
    ASSERT(init_counter == 1);

    bool second = call_do_once();
    ASSERT(!second);
    ASSERT(init_counter == 1);
}

/* ================================================================
 * 13. Zero-length array (legacy kernel pattern)
 * Source: various old kernel structs
 * ================================================================ */

struct old_msg_header {
    uint32_t type;
    uint32_t len;
    char data[0];
};

struct new_msg_header {
    uint32_t type;
    uint32_t len;
    char data[];
};

_Static_assert(sizeof(struct old_msg_header) == sizeof(struct new_msg_header),
               "zero-length and flexible arrays have same struct size");
_Static_assert(sizeof(struct old_msg_header) == 8, "header is 8 bytes");

static void test_zero_length_array(void) {
    char buf[sizeof(struct old_msg_header) + 16];
    struct old_msg_header *msg = (struct old_msg_header *)buf;
    msg->type = 1;
    msg->len = 12;
    memcpy(msg->data, "hello world", 12);
    ASSERT(msg->data[0] == 'h');
    ASSERT(msg->data[10] == 'd');

    ASSERT(offsetof(struct old_msg_header, data) == 8);
    ASSERT(offsetof(struct new_msg_header, data) == 8);
}

/* ================================================================
 * 14. Pointer arithmetic with cast-through-uintptr_t
 * Source: various kernel pointer manipulation
 * ================================================================ */

static void test_pointer_arithmetic(void) {
    int arr[10] = {0, 10, 20, 30, 40, 50, 60, 70, 80, 90};
    int *p = arr;

    uintptr_t addr = (uintptr_t)p;
    addr += 3 * sizeof(int);
    int *q = (int *)addr;
    ASSERT(*q == 30);

    ASSERT(q - p == 3);
    ASSERT(p + 5 == &arr[5]);
    ASSERT(*(p + 9) == 90);

    char *cp = (char *)arr;
    int *ip = (int *)(cp + sizeof(int));
    ASSERT(*ip == 10);
}

/* ================================================================
 * 15. X-macro pattern
 * Source: kernel error code tables, register definitions
 * ================================================================ */

#define ERRNO_LIST(X) \
    X(EPERM,  1, "Operation not permitted") \
    X(ENOENT, 2, "No such file or directory") \
    X(ESRCH,  3, "No such process") \
    X(EINTR,  4, "Interrupted system call") \
    X(EIO,    5, "I/O error")

enum my_errno {
#define DEFINE_ERRNO(name, val, desc) MY_##name = val,
    ERRNO_LIST(DEFINE_ERRNO)
#undef DEFINE_ERRNO
};

static const char *errno_to_str(int err) {
#define CASE_ERRNO(name, val, desc) case val: return desc;
    switch (err) {
        ERRNO_LIST(CASE_ERRNO)
        default: return "Unknown error";
    }
#undef CASE_ERRNO
}

static void test_x_macro(void) {
    ASSERT(MY_EPERM == 1);
    ASSERT(MY_EIO == 5);
    ASSERT(strcmp(errno_to_str(1), "Operation not permitted") == 0);
    ASSERT(strcmp(errno_to_str(5), "I/O error") == 0);
    ASSERT(strcmp(errno_to_str(99), "Unknown error") == 0);

    static const struct { int val; const char *name; } errno_table[] = {
#define TABLE_ENTRY(name, val, desc) { val, #name },
        ERRNO_LIST(TABLE_ENTRY)
#undef TABLE_ENTRY
    };
    ASSERT(errno_table[0].val == 1 && strcmp(errno_table[0].name, "EPERM") == 0);
    ASSERT(errno_table[4].val == 5 && strcmp(errno_table[4].name, "EIO") == 0);
}

/* ================================================================
 * 16. Comma operator in complex expressions
 * Source: kernel macro patterns
 * ================================================================ */

static void test_comma_operator(void) {
    int a = (1, 2, 3);
    ASSERT(a == 3);

    int x = 0, y = 0;
    int result = (x = 10, y = 20, x + y);
    ASSERT(result == 30);
    ASSERT(x == 10 && y == 20);

    for (int i = 0, j = 10; i < 5; i++, j--) {
        ASSERT(i + j == 10);
    }
}

/* ================================================================
 * 17. __attribute__((used)) / __attribute__((section))
 * Source: include/linux/compiler.h (__ADDRESSABLE)
 * ================================================================ */

__attribute__((used))
static int used_var = 12345;

#ifdef __MACH__
__attribute__((used, section("__DATA,__mydata")))
static int section_data = 99;
#else
__attribute__((used, section(".mydata")))
static int section_data = 99;
#endif

#define EXPORT_SYMBOL_FAKE(sym) \
    __attribute__((used)) static void *__ksym_##sym = (void *)&sym

static int exported_func(void) { return 42; }
EXPORT_SYMBOL_FAKE(exported_func);

static void test_used_section(void) {
    ASSERT(used_var == 12345);
    ASSERT(section_data == 99);
    ASSERT(exported_func() == 42);
}

/* ================================================================
 * 18. Nested macro expansion with token pasting + stringification
 * Source: kernel logging, tracepoint macros
 * ================================================================ */

#define ___PASTE(a, b) a##b
#define __PASTE(a, b) ___PASTE(a, b)
#define __stringify_1(x...) #x
#define __stringify(x...) __stringify_1(x)

#define DEFINE_COUNTER(name) \
    static int __PASTE(counter_, name) = 0; \
    static const char *__PASTE(counter_name_, name) = __stringify(name)

#define INC_COUNTER(name) (__PASTE(counter_, name)++)
#define GET_COUNTER(name) (__PASTE(counter_, name))
#define COUNTER_NAME(name) (__PASTE(counter_name_, name))

DEFINE_COUNTER(reads);
DEFINE_COUNTER(writes);

static void test_nested_macro_expansion(void) {
    ASSERT(GET_COUNTER(reads) == 0);
    INC_COUNTER(reads);
    INC_COUNTER(reads);
    ASSERT(GET_COUNTER(reads) == 2);

    INC_COUNTER(writes);
    ASSERT(GET_COUNTER(writes) == 1);

    ASSERT(strcmp(COUNTER_NAME(reads), "reads") == 0);
    ASSERT(strcmp(COUNTER_NAME(writes), "writes") == 0);
}

/* ================================================================
 * 19. hlist (hash list) with container_of iteration
 * Source: include/linux/list.h (hlist section)
 * ================================================================ */

struct hlist_head { struct hlist_node *first; };
struct hlist_node { struct hlist_node *next, **pprev; };

#define HLIST_HEAD_INIT { .first = NULL }
#define INIT_HLIST_NODE(ptr) do { (ptr)->next = NULL; (ptr)->pprev = NULL; } while (0)

static inline void hlist_add_head(struct hlist_node *n, struct hlist_head *h) {
    struct hlist_node *first = h->first;
    n->next = first;
    if (first)
        first->pprev = &n->next;
    h->first = n;
    n->pprev = &h->first;
}

static inline void hlist_del(struct hlist_node *n) {
    struct hlist_node *next = n->next;
    struct hlist_node **pprev = n->pprev;
    *pprev = next;
    if (next)
        next->pprev = pprev;
}

static inline int hlist_empty(const struct hlist_head *h) {
    return !h->first;
}

#define hlist_entry(ptr, type, member) container_of(ptr, type, member)
#define hlist_for_each_entry(pos, head, member) \
    for (pos = (head)->first ? hlist_entry((head)->first, typeof(*pos), member) : NULL; \
         pos; \
         pos = pos->member.next ? hlist_entry(pos->member.next, typeof(*pos), member) : NULL)

struct hash_item {
    int key;
    int value;
    struct hlist_node node;
};

static void test_hlist(void) {
    struct hlist_head bucket = HLIST_HEAD_INIT;
    ASSERT(hlist_empty(&bucket));

    struct hash_item items[3] = {
        { .key = 1, .value = 100 },
        { .key = 2, .value = 200 },
        { .key = 3, .value = 300 },
    };
    INIT_HLIST_NODE(&items[0].node);
    INIT_HLIST_NODE(&items[1].node);
    INIT_HLIST_NODE(&items[2].node);

    hlist_add_head(&items[0].node, &bucket);
    hlist_add_head(&items[1].node, &bucket);
    hlist_add_head(&items[2].node, &bucket);
    ASSERT(!hlist_empty(&bucket));

    int sum = 0;
    struct hash_item *entry;
    hlist_for_each_entry(entry, &bucket, node) {
        sum += entry->value;
    }
    ASSERT(sum == 600);

    hlist_del(&items[1].node);
    sum = 0;
    hlist_for_each_entry(entry, &bucket, node) {
        sum += entry->value;
    }
    ASSERT(sum == 400);
}

/* ================================================================
 * 20. Labeled statement at end of compound stmt + case fallthrough
 * Source: kernel switch patterns with __attribute__((fallthrough))
 * ================================================================ */

static int classify_priority(int level) {
    int result = 0;
    switch (level) {
    case 5:
        result += 100;
        __attribute__((fallthrough));
    case 4:
        result += 50;
        __attribute__((fallthrough));
    case 3:
        result += 20;
        break;
    case 2:
    case 1:
        result = 1;
        break;
    default:
        result = -1;
    }
    return result;
}

static void test_fallthrough(void) {
    ASSERT(classify_priority(5) == 170);
    ASSERT(classify_priority(4) == 70);
    ASSERT(classify_priority(3) == 20);
    ASSERT(classify_priority(2) == 1);
    ASSERT(classify_priority(0) == -1);
}

/* ================================================================
 * Main
 * ================================================================ */

int main(void) {
    test_err_ptr();
    test_elvis_operator();
    test_list_for_each_entry();
    test_rb_bit_stealing();
    test_range_designated_init();
    test_constructor_destructor();
    test_function_attributes();
    test_compound_literal_arg();
    test_union_type_punning();
    test_nested_designated_init();
    test_vtable();
    test_do_once();
    test_zero_length_array();
    test_pointer_arithmetic();
    test_x_macro();
    test_comma_operator();
    test_used_section();
    test_nested_macro_expansion();
    test_hlist();
    test_fallthrough();

    printf("test_kernel_advanced2: ALL PASSED\n");
    return 0;
}
