// NeverC test: Linux kernel C patterns (self-contained, no kernel headers)
// RUN: %neverc -std=gnu11 -Wall -Wextra -Werror -Wno-gnu -fsyntax-only %s
// RUN: %neverc -std=gnu11 -Wall -Wextra -Werror -Wno-gnu -O2 -c %s -o /dev/null
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>

// ============================================================
// Kernel-style type helpers
// ============================================================

// u8/u16/u32/u64 are built-in NeverC keywords (Rust-style fixed-width
// unsigned integer types).  Signed kernel-style aliases stay as typedefs.
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef int64_t  s64;

#define __user
#define __kernel
#define __force
#define __iomem
#define __rcu
#define asmlinkage
#ifdef __MACH__
#define __init
#define __exit
#define __initdata
#define __ro_after_init
#else
#define __init     __attribute__((section(".init.text")))
#define __exit     __attribute__((section(".exit.text")))
#define __initdata __attribute__((section(".init.data")))
#define __ro_after_init __attribute__((section(".data..ro_after_init")))
#endif
#define EXPORT_SYMBOL(sym)
#define MODULE_LICENSE(x)
#define MODULE_AUTHOR(x)
#define MODULE_DESCRIPTION(x)

// ============================================================
// likely/unlikely
// ============================================================
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

// ============================================================
// READ_ONCE / WRITE_ONCE (kernel volatile access pattern)
// ============================================================
#define READ_ONCE(x) ({                            \
    typeof(x) __val = *(const volatile typeof(x) *)&(x); \
    __val;                                         \
})
#define WRITE_ONCE(x, val) do {                    \
    *(volatile typeof(x) *)&(x) = (val);           \
} while (0)

// ============================================================
// ARRAY_SIZE
// ============================================================
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

// ============================================================
// BIT macros
// ============================================================
#define BIT(nr)           (1UL << (nr))
#define GENMASK(h, l)     (((~0UL) - (1UL << (l)) + 1) & (~0UL >> (BITS_PER_LONG - 1 - (h))))
#define BITS_PER_LONG     (sizeof(long) * 8)

// ============================================================
// container_of
// ============================================================
#define container_of(ptr, type, member) ({                     \
    const typeof(((type *)0)->member) *__mptr = (ptr);         \
    (type *)((char *)__mptr - offsetof(type, member));         \
})

// ============================================================
// list_head (minimal doubly linked list, kernel-style)
// ============================================================
struct list_head {
    struct list_head *next, *prev;
};

#define LIST_HEAD_INIT(name) { &(name), &(name) }
#define LIST_HEAD(name) struct list_head name = LIST_HEAD_INIT(name)

static inline void INIT_LIST_HEAD(struct list_head *list) {
    WRITE_ONCE(list->next, list);
    list->prev = list;
}

static inline void __list_add(struct list_head *new_node,
                              struct list_head *prev,
                              struct list_head *next) {
    next->prev = new_node;
    new_node->next = next;
    new_node->prev = prev;
    WRITE_ONCE(prev->next, new_node);
}

static inline void list_add(struct list_head *new_node, struct list_head *head) {
    __list_add(new_node, head, head->next);
}

static inline void list_add_tail(struct list_head *new_node, struct list_head *head) {
    __list_add(new_node, head->prev, head);
}

static inline int list_empty(const struct list_head *head) {
    return READ_ONCE(head->next) == head;
}

#define list_entry(ptr, type, member) container_of(ptr, type, member)

#define list_for_each(pos, head) \
    for (pos = (head)->next; pos != (head); pos = pos->next)

#define list_for_each_entry(pos, head, member)                         \
    for (pos = list_entry((head)->next, typeof(*pos), member);         \
         &pos->member != (head);                                       \
         pos = list_entry(pos->member.next, typeof(*pos), member))

// ============================================================
// RB-tree (minimal, kernel-style)
// ============================================================
struct rb_node {
    unsigned long __rb_parent_color;
    struct rb_node *rb_right;
    struct rb_node *rb_left;
};

struct rb_root { struct rb_node *rb_node; };

#define RB_ROOT (struct rb_root) { NULL }

static inline void rb_link_node(struct rb_node *node, struct rb_node *parent,
                                struct rb_node **rb_link) {
    node->__rb_parent_color = (unsigned long)parent;
    node->rb_left = node->rb_right = NULL;
    *rb_link = node;
}

// ============================================================
// Spinlock pattern (stub)
// ============================================================
typedef struct { volatile int lock; } spinlock_t;

#define SPIN_LOCK_INIT { .lock = 0 }

static inline void spin_lock(spinlock_t *l) {
    while (__atomic_exchange_n(&l->lock, 1, __ATOMIC_ACQUIRE)) { }
}

static inline void spin_unlock(spinlock_t *l) {
    __atomic_store_n(&l->lock, 0, __ATOMIC_RELEASE);
}

// ============================================================
// Atomic operations
// ============================================================
typedef struct { int counter; } atomic_t;

#define ATOMIC_INIT(i) { (i) }

static inline int atomic_read(const atomic_t *v) {
    return READ_ONCE(v->counter);
}

static inline void atomic_set(atomic_t *v, int i) {
    WRITE_ONCE(v->counter, i);
}

static inline void atomic_inc(atomic_t *v) {
    __atomic_add_fetch(&v->counter, 1, __ATOMIC_RELAXED);
}

static inline int atomic_dec_and_test(atomic_t *v) {
    return __atomic_sub_fetch(&v->counter, 1, __ATOMIC_ACQ_REL) == 0;
}

// ============================================================
// Refcount pattern
// ============================================================
struct kref {
    atomic_t refcount;
};

static inline void kref_init(struct kref *k) {
    atomic_set(&k->refcount, 1);
}

static inline void kref_get(struct kref *k) {
    atomic_inc(&k->refcount);
}

// ============================================================
// WARN / BUG macros
// ============================================================
#define BUG() __builtin_trap()
#define BUG_ON(cond) do { if (unlikely(cond)) BUG(); } while (0)
#define WARN_ON(cond) ({ int __ret = !!(cond); __ret; })

// ============================================================
// bitmap operations
// ============================================================
static inline void set_bit(unsigned int nr, unsigned long *addr) {
    addr[nr / BITS_PER_LONG] |= BIT(nr % BITS_PER_LONG);
}

static inline void clear_bit(unsigned int nr, unsigned long *addr) {
    addr[nr / BITS_PER_LONG] &= ~BIT(nr % BITS_PER_LONG);
}

static inline int test_bit(unsigned int nr, const unsigned long *addr) {
    return !!(addr[nr / BITS_PER_LONG] & BIT(nr % BITS_PER_LONG));
}

// ============================================================
// hash table bucket (kernel-style)
// ============================================================
struct hlist_node {
    struct hlist_node *next, **pprev;
};

struct hlist_head {
    struct hlist_node *first;
};

#define HLIST_HEAD_INIT { .first = NULL }

static inline void hlist_add_head(struct hlist_node *n, struct hlist_head *h) {
    struct hlist_node *first = h->first;
    n->next = first;
    if (first) first->pprev = &n->next;
    WRITE_ONCE(h->first, n);
    n->pprev = &h->first;
}

// ============================================================
// Test: actually exercise all the above
// ============================================================
struct my_device {
    int id;
    const char *name;
    struct list_head list;
    struct rb_node rb;
    struct kref ref;
    atomic_t users;
    spinlock_t lock;
};

static LIST_HEAD(device_list);

__init static int test_init(void) {
    struct my_device devs[3] = {
        { .id = 0, .name = "dev0", .ref = {ATOMIC_INIT(1)}, .users = ATOMIC_INIT(0), .lock = SPIN_LOCK_INIT },
        { .id = 1, .name = "dev1", .ref = {ATOMIC_INIT(1)}, .users = ATOMIC_INIT(0), .lock = SPIN_LOCK_INIT },
        { .id = 2, .name = "dev2", .ref = {ATOMIC_INIT(1)}, .users = ATOMIC_INIT(0), .lock = SPIN_LOCK_INIT },
    };

    for (int i = 0; i < (int)ARRAY_SIZE(devs); i++) {
        INIT_LIST_HEAD(&devs[i].list);
        kref_init(&devs[i].ref);
        list_add_tail(&devs[i].list, &device_list);
    }

    struct my_device *pos;
    int count = 0;
    list_for_each_entry(pos, &device_list, list) {
        kref_get(&pos->ref);
        atomic_inc(&pos->users);
        spin_lock(&pos->lock);
        spin_unlock(&pos->lock);
        count++;
    }

    BUG_ON(count != 3);
    WARN_ON(list_empty(&device_list));

    unsigned long bitmap[2] = {0};
    set_bit(0, bitmap);
    set_bit(63, bitmap);
    BUG_ON(!test_bit(0, bitmap));
    BUG_ON(!test_bit(63, bitmap));
    clear_bit(0, bitmap);
    BUG_ON(test_bit(0, bitmap));

    BUG_ON(BIT(0) != 1UL);
    BUG_ON(BIT(3) != 8UL);

    return 0;
}

int main(void) {
    return test_init();
}

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("NeverC kernel pattern test");
