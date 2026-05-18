// RUN: %neverc -fsyntax-only %s
/*
 * NeverC Compiler Validation - Real-World C Architecture Patterns
 *
 * Exercises production C patterns used in high-quality codebases
 * (kernel, SQLite, Redis, nginx, systemd, FFmpeg, etc.):
 *
 *  1.  State machine with function pointer dispatch
 *  2.  Arena/pool allocator
 *  3.  Intrusive doubly-linked list with type-safe macros
 *  4.  Hash map (open addressing)
 *  5.  Ring buffer (lock-free style)
 *  6.  Callback registry / observer pattern
 *  7.  String builder (dynamic buffer)
 *  8.  Bit rotation & extraction
 *  9.  X-macro code generation
 * 10.  Coroutine-like state machine (switch/case Duff's device)
 * 11.  Object system with vtable
 * 12.  Error propagation with cleanup goto
 * 13.  Memory-mapped I/O simulation (volatile struct)
 * 14.  Tagged union with visitor
 * 15.  Compile-time assertion tricks
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
 * 1. State machine with function pointer dispatch
 * ================================================================ */
typedef enum { ST_IDLE, ST_RUNNING, ST_PAUSED, ST_DONE, ST_COUNT } state_t;
typedef state_t (*state_fn)(int event);

static state_t st_idle(int ev)    { return ev == 1 ? ST_RUNNING : ST_IDLE; }
static state_t st_running(int ev) { return ev == 2 ? ST_PAUSED : ev == 3 ? ST_DONE : ST_RUNNING; }
static state_t st_paused(int ev)  { return ev == 1 ? ST_RUNNING : ev == 3 ? ST_DONE : ST_PAUSED; }
static state_t st_done(int ev)    { (void)ev; return ST_DONE; }

static state_fn state_table[ST_COUNT] = {
    [ST_IDLE]    = st_idle,
    [ST_RUNNING] = st_running,
    [ST_PAUSED]  = st_paused,
    [ST_DONE]    = st_done,
};

static int test_state_machine(void) {
    state_t s = ST_IDLE;
    s = state_table[s](1);  ASSERT(s == ST_RUNNING);
    s = state_table[s](2);  ASSERT(s == ST_PAUSED);
    s = state_table[s](1);  ASSERT(s == ST_RUNNING);
    s = state_table[s](3);  ASSERT(s == ST_DONE);
    s = state_table[s](1);  ASSERT(s == ST_DONE);
    return 0;
}

/* ================================================================
 * 2. Arena/pool allocator
 * ================================================================ */
#define ARENA_SIZE 4096

struct arena {
    char buf[ARENA_SIZE];
    size_t offset;
};

static void arena_init(struct arena *a) { a->offset = 0; }

static void *arena_alloc(struct arena *a, size_t sz) {
    sz = (sz + 7) & ~(size_t)7;
    if (a->offset + sz > ARENA_SIZE) return NULL;
    void *p = a->buf + a->offset;
    a->offset += sz;
    return p;
}

static void arena_reset(struct arena *a) { a->offset = 0; }

static int test_arena(void) {
    struct arena a;
    arena_init(&a);

    int *x = arena_alloc(&a, sizeof(int));
    ASSERT(x != NULL);
    *x = 42;

    char *s = arena_alloc(&a, 32);
    strcpy(s, "arena string");
    ASSERT(strcmp(s, "arena string") == 0);
    ASSERT(*x == 42);

    double *d = arena_alloc(&a, sizeof(double));
    *d = 3.14;
    ASSERT(*d == 3.14);

    arena_reset(&a);
    int *y = arena_alloc(&a, sizeof(int));
    ASSERT(y == (int *)(void *)a.buf);
    return 0;
}

/* ================================================================
 * 3. Intrusive doubly-linked list with type-safe macros
 * ================================================================ */
struct dlist {
    struct dlist *prev, *next;
};

#define DLIST_INIT(name) { &(name), &(name) }

static inline void dlist_init(struct dlist *h) { h->prev = h->next = h; }

static inline void dlist_insert(struct dlist *node, struct dlist *prev, struct dlist *next) {
    next->prev = node;
    node->next = next;
    node->prev = prev;
    prev->next = node;
}

static inline void dlist_push_back(struct dlist *head, struct dlist *node) {
    dlist_insert(node, head->prev, head);
}

/* Classic doubly-linked remove; clang -Wself-assign-field false-positives on prev/next chains. */
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wself-assign-field"
#endif
static inline void dlist_remove(struct dlist *node) {
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->prev = node->next = node;
}
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

static inline bool dlist_empty(const struct dlist *h) { return h->next == h; }

#define dlist_entry(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

#define dlist_for_each(pos, head) \
    for (pos = (head)->next; pos != (head); pos = pos->next)

struct task {
    int id;
    int priority;
    struct dlist link;
};

static int test_intrusive_list(void) {
    struct dlist tasks = DLIST_INIT(tasks);
    struct task t1 = { .id = 1, .priority = 3 };
    struct task t2 = { .id = 2, .priority = 1 };
    struct task t3 = { .id = 3, .priority = 2 };

    dlist_init(&t1.link);
    dlist_init(&t2.link);
    dlist_init(&t3.link);

    dlist_push_back(&tasks, &t1.link);
    dlist_push_back(&tasks, &t2.link);
    dlist_push_back(&tasks, &t3.link);

    ASSERT(!dlist_empty(&tasks));

    int ids[3], idx = 0;
    struct dlist *pos;
    dlist_for_each(pos, &tasks) {
        struct task *t = dlist_entry(pos, struct task, link);
        ids[idx++] = t->id;
    }
    ASSERT(ids[0] == 1 && ids[1] == 2 && ids[2] == 3);

    dlist_remove(&t2.link);
    idx = 0;
    dlist_for_each(pos, &tasks) {
        struct task *t = dlist_entry(pos, struct task, link);
        ids[idx++] = t->id;
    }
    ASSERT(idx == 2 && ids[0] == 1 && ids[1] == 3);

    return 0;
}

/* ================================================================
 * 4. Hash map (open addressing, linear probing)
 * ================================================================ */
#define HM_CAPACITY 16

struct hm_entry { uint64_t key; int value; bool occupied; };
struct hashmap { struct hm_entry buckets[HM_CAPACITY]; };

static uint64_t hash64(uint64_t k) {
    k ^= k >> 33;
    k *= 0xff51afd7ed558ccdULL;
    k ^= k >> 33;
    return k;
}

static void hm_init(struct hashmap *m) { memset(m, 0, sizeof(*m)); }

static bool hm_put(struct hashmap *m, uint64_t key, int value) {
    uint64_t idx = hash64(key) % HM_CAPACITY;
    for (int i = 0; i < HM_CAPACITY; i++) {
        uint64_t slot = (idx + i) % HM_CAPACITY;
        if (!m->buckets[slot].occupied || m->buckets[slot].key == key) {
            m->buckets[slot] = (struct hm_entry){key, value, true};
            return true;
        }
    }
    return false;
}

static int *hm_get(struct hashmap *m, uint64_t key) {
    uint64_t idx = hash64(key) % HM_CAPACITY;
    for (int i = 0; i < HM_CAPACITY; i++) {
        uint64_t slot = (idx + i) % HM_CAPACITY;
        if (!m->buckets[slot].occupied) return NULL;
        if (m->buckets[slot].key == key) return &m->buckets[slot].value;
    }
    return NULL;
}

static int test_hashmap(void) {
    struct hashmap m;
    hm_init(&m);

    ASSERT(hm_put(&m, 100, 42));
    ASSERT(hm_put(&m, 200, 84));
    ASSERT(hm_put(&m, 300, 126));

    int *v = hm_get(&m, 100);
    ASSERT(v && *v == 42);
    v = hm_get(&m, 200);
    ASSERT(v && *v == 84);
    v = hm_get(&m, 999);
    ASSERT(v == NULL);

    hm_put(&m, 100, 999);
    v = hm_get(&m, 100);
    ASSERT(v && *v == 999);

    return 0;
}

/* ================================================================
 * 5. Ring buffer (power-of-two, mask-based)
 * ================================================================ */
#define RING_SIZE 8
#define RING_MASK (RING_SIZE - 1)

struct ring_buf {
    int data[RING_SIZE];
    unsigned head, tail;
};

static bool ring_push(struct ring_buf *r, int val) {
    if (((r->head + 1) & RING_MASK) == r->tail) return false;
    r->data[r->head] = val;
    r->head = (r->head + 1) & RING_MASK;
    return true;
}

static bool ring_pop(struct ring_buf *r, int *val) {
    if (r->head == r->tail) return false;
    *val = r->data[r->tail];
    r->tail = (r->tail + 1) & RING_MASK;
    return true;
}

static unsigned ring_count(const struct ring_buf *r) {
    return (r->head - r->tail) & RING_MASK;
}

static int test_ring_buffer(void) {
    struct ring_buf rb = { .head = 0, .tail = 0 };

    for (int i = 0; i < 7; i++)
        ASSERT(ring_push(&rb, i * 10));
    ASSERT(!ring_push(&rb, 99));
    ASSERT(ring_count(&rb) == 7);

    int val;
    ASSERT(ring_pop(&rb, &val) && val == 0);
    ASSERT(ring_pop(&rb, &val) && val == 10);
    ASSERT(ring_count(&rb) == 5);

    ASSERT(ring_push(&rb, 100));
    ASSERT(ring_push(&rb, 200));
    ASSERT(ring_count(&rb) == 7);

    return 0;
}

/* ================================================================
 * 6. Callback registry / observer pattern
 * ================================================================ */
#define MAX_CALLBACKS 8

typedef void (*event_cb)(int event, void *ctx);

struct event_bus {
    struct { event_cb fn; void *ctx; } slots[MAX_CALLBACKS];
    int count;
};

static void bus_init(struct event_bus *b) { b->count = 0; }

static bool bus_subscribe(struct event_bus *b, event_cb fn, void *ctx) {
    if (b->count >= MAX_CALLBACKS) return false;
    b->slots[b->count++] = (typeof(b->slots[0])){fn, ctx};
    return true;
}

static void bus_emit(struct event_bus *b, int event) {
    for (int i = 0; i < b->count; i++)
        b->slots[i].fn(event, b->slots[i].ctx);
}

static void counter_cb(int event, void *ctx) {
    *(int *)ctx += event;
}

static int test_callback_registry(void) {
    struct event_bus bus;
    bus_init(&bus);

    int counter1 = 0, counter2 = 0;
    bus_subscribe(&bus, counter_cb, &counter1);
    bus_subscribe(&bus, counter_cb, &counter2);

    bus_emit(&bus, 10);
    bus_emit(&bus, 20);
    ASSERT(counter1 == 30 && counter2 == 30);

    return 0;
}

/* ================================================================
 * 7. String builder (dynamic buffer)
 * ================================================================ */
struct strbuf {
    char *data;
    size_t len, cap;
};

static void sb_init(struct strbuf *sb) {
    sb->cap = 16;
    sb->data = malloc(sb->cap);
    sb->data[0] = '\0';
    sb->len = 0;
}

static void sb_append(struct strbuf *sb, const char *s) {
    size_t slen = strlen(s);
    while (sb->len + slen + 1 > sb->cap) {
        sb->cap *= 2;
        sb->data = realloc(sb->data, sb->cap);
    }
    memcpy(sb->data + sb->len, s, slen + 1);
    sb->len += slen;
}

static void sb_free(struct strbuf *sb) { free(sb->data); }

static int test_string_builder(void) {
    struct strbuf sb;
    sb_init(&sb);

    sb_append(&sb, "Hello");
    sb_append(&sb, ", ");
    sb_append(&sb, "World");
    sb_append(&sb, "!");
    ASSERT(strcmp(sb.data, "Hello, World!") == 0);
    ASSERT(sb.len == 13);

    for (int i = 0; i < 50; i++)
        sb_append(&sb, "x");
    ASSERT(sb.len == 63);
    ASSERT(sb.cap >= 64);

    sb_free(&sb);
    return 0;
}

/* ================================================================
 * 8. Bit rotation & extraction
 * ================================================================ */
static inline uint32_t rotl32(uint32_t x, int n) {
    return (x << n) | (x >> (32 - n));
}

static inline uint32_t rotr32(uint32_t x, int n) {
    return (x >> n) | (x << (32 - n));
}

#define EXTRACT_BITS(val, hi, lo) \
    (((val) >> (lo)) & ((1U << ((hi) - (lo) + 1)) - 1))

static int test_bit_ops(void) {
    ASSERT(rotl32(0x80000001, 1) == 0x00000003);
    ASSERT(rotr32(0x80000001, 1) == 0xC0000000);
    ASSERT(rotl32(rotr32(0xDEADBEEF, 13), 13) == 0xDEADBEEF);
    ASSERT(EXTRACT_BITS(0xABCD1234, 15, 8) == 0x12);
    ASSERT(EXTRACT_BITS(0xFF, 7, 0) == 0xFF);
    ASSERT(EXTRACT_BITS(0xFF, 3, 0) == 0x0F);
    return 0;
}

/* ================================================================
 * 9. X-macro code generation
 * ================================================================ */
#define COLORS(X) \
    X(COLOR_RED,   "red",   0xFF0000) \
    X(COLOR_GREEN, "green", 0x00FF00) \
    X(COLOR_BLUE,  "blue",  0x0000FF) \
    X(COLOR_WHITE, "white", 0xFFFFFF) \
    X(COLOR_BLACK, "black", 0x000000)

#define ENUM_ENTRY(id, name, hex) id,
enum color_id { COLORS(ENUM_ENTRY) COLOR_COUNT };
#undef ENUM_ENTRY

#define NAME_ENTRY(id, name, hex) [id] = name,
static const char *color_names[] = { COLORS(NAME_ENTRY) };
#undef NAME_ENTRY

#define HEX_ENTRY(id, name, hex) [id] = hex,
static const uint32_t color_values[] = { COLORS(HEX_ENTRY) };
#undef HEX_ENTRY

static int test_xmacro(void) {
    ASSERT(COLOR_COUNT == 5);
    ASSERT(strcmp(color_names[COLOR_RED], "red") == 0);
    ASSERT(strcmp(color_names[COLOR_BLUE], "blue") == 0);
    ASSERT(color_values[COLOR_WHITE] == 0xFFFFFF);
    ASSERT(color_values[COLOR_BLACK] == 0x000000);
    return 0;
}

/* ================================================================
 * 10. Coroutine-like state machine (protothreads / switch-case)
 * ================================================================ */
struct coro {
    int state;
    int counter;
    int result;
};

#define CR_BEGIN(c) switch((c)->state) { case 0:
#define CR_YIELD(c, val) do { (c)->state = __LINE__; (c)->result = val; return; case __LINE__:; } while(0)
#define CR_END }

static void counting_coro(struct coro *c) {
    CR_BEGIN(c);
    for (c->counter = 0; c->counter < 5; c->counter++) {
        CR_YIELD(c, c->counter);
    }
    c->result = -1;
    CR_END;
}

static int test_coroutine(void) {
    struct coro c = {0};
    for (int i = 0; i < 5; i++) {
        counting_coro(&c);
        ASSERT(c.result == i);
    }
    counting_coro(&c);
    ASSERT(c.result == -1);
    return 0;
}

/* ================================================================
 * 11. Object system with vtable
 * ================================================================ */
struct shape;
struct shape_vtable {
    double (*area)(const struct shape *);
    double (*perimeter)(const struct shape *);
    const char *(*name)(const struct shape *);
};

struct shape { const struct shape_vtable *vt; };
struct circle { struct shape base; double radius; };
struct rectangle { struct shape base; double w, h; };

static double circle_area(const struct shape *s) {
    return 3.14159 * ((struct circle *)s)->radius * ((struct circle *)s)->radius;
}
static double circle_peri(const struct shape *s) {
    return 2.0 * 3.14159 * ((struct circle *)s)->radius;
}
static const char *circle_name(const struct shape *s) { (void)s; return "circle"; }

static const struct shape_vtable circle_vt = { circle_area, circle_peri, circle_name };

static double rect_area(const struct shape *s) {
    const struct rectangle *r = (const struct rectangle *)s;
    return r->w * r->h;
}
static double rect_peri(const struct shape *s) {
    const struct rectangle *r = (const struct rectangle *)s;
    return 2.0 * (r->w + r->h);
}
static const char *rect_name(const struct shape *s) { (void)s; return "rect"; }

static const struct shape_vtable rect_vt = { rect_area, rect_peri, rect_name };

static int test_vtable_oop(void) {
    struct circle c = { .base.vt = &circle_vt, .radius = 5.0 };
    struct rectangle r = { .base.vt = &rect_vt, .w = 3.0, .h = 4.0 };

    struct shape *shapes[] = { &c.base, &r.base };
    ASSERT(strcmp(shapes[0]->vt->name(shapes[0]), "circle") == 0);
    ASSERT(shapes[1]->vt->area(shapes[1]) == 12.0);
    ASSERT(shapes[1]->vt->perimeter(shapes[1]) == 14.0);
    ASSERT(shapes[0]->vt->area(shapes[0]) > 78.0);
    return 0;
}

/* ================================================================
 * 12. Error propagation with cleanup goto
 * ================================================================ */
static int complex_init(void) {
    int rc = -1;
    char *buf1 = NULL, *buf2 = NULL, *buf3 = NULL;

    buf1 = malloc(64);
    if (!buf1) goto fail;
    strcpy(buf1, "step1");

    buf2 = malloc(128);
    if (!buf2) goto fail;
    strcpy(buf2, "step2");

    buf3 = malloc(256);
    if (!buf3) goto fail;
    strcpy(buf3, "step3");

    ASSERT(strcmp(buf1, "step1") == 0);
    ASSERT(strcmp(buf2, "step2") == 0);
    ASSERT(strcmp(buf3, "step3") == 0);
    rc = 0;

fail:
    free(buf3);
    free(buf2);
    free(buf1);
    return rc;
}

static int test_cleanup_goto(void) {
    ASSERT(complex_init() == 0);
    return 0;
}

/* ================================================================
 * 13. Memory-mapped I/O simulation (volatile struct pointer)
 * ================================================================ */
struct mmio_regs {
    volatile uint32_t control;
    volatile uint32_t status;
    volatile uint32_t data;
    volatile uint32_t reserved;
};

static int test_mmio(void) {
    struct mmio_regs regs = {0};
    volatile struct mmio_regs *hw = &regs;

    hw->control = 0x01;
    ASSERT(hw->control == 0x01);

    hw->data = 0xDEADBEEF;
    ASSERT(hw->data == 0xDEADBEEF);

    hw->status = 0;
    hw->control |= 0x80;
    ASSERT((hw->control & 0x80) != 0);

    return 0;
}

/* ================================================================
 * 14. Tagged union with visitor
 * ================================================================ */
enum expr_type { EXPR_INT, EXPR_ADD, EXPR_MUL };

struct expr {
    enum expr_type type;
    union {
        int int_val;
        struct { struct expr *left, *right; } bin;
    };
};

static int eval(const struct expr *e) {
    switch (e->type) {
    case EXPR_INT: return e->int_val;
    case EXPR_ADD: return eval(e->bin.left) + eval(e->bin.right);
    case EXPR_MUL: return eval(e->bin.left) * eval(e->bin.right);
    }
    __builtin_unreachable();
}

static int test_tagged_union(void) {
    struct expr two = { EXPR_INT, .int_val = 2 };
    struct expr three = { EXPR_INT, .int_val = 3 };
    struct expr five = { EXPR_INT, .int_val = 5 };

    struct expr add = { EXPR_ADD, .bin = { &two, &three } };
    struct expr mul = { EXPR_MUL, .bin = { &add, &five } };

    ASSERT(eval(&two) == 2);
    ASSERT(eval(&add) == 5);
    ASSERT(eval(&mul) == 25);
    return 0;
}

/* ================================================================
 * 15. Compile-time assertion tricks
 * ================================================================ */
#define SAME_TYPE(a, b) __builtin_types_compatible_p(typeof(a), typeof(b))

_Static_assert(sizeof(int) == 4, "need 32-bit int");
_Static_assert(__alignof__(double) >= 4, "alignment");
_Static_assert(sizeof(struct { char c; }) == 1, "struct min size");
_Static_assert(sizeof(void *) == sizeof(size_t), "ptr == size_t");

static int test_compile_time(void) {
    int arr[10];
    _Static_assert(sizeof(arr) / sizeof(arr[0]) == 10, "array count");
    _Static_assert(sizeof(int) == 4, "need 32-bit int");
    _Static_assert(__alignof__(double) >= 4, "alignment");
    _Static_assert(sizeof(struct { char c; }) == 1, "struct min size");
    _Static_assert(sizeof(void *) == sizeof(size_t), "ptr == size_t");

    int choose_result = __builtin_choose_expr(1, 42, "string");
    ASSERT(choose_result == 42);

    const char *choose_str = __builtin_choose_expr(0, 42, "chosen");
    ASSERT(strcmp(choose_str, "chosen") == 0);

    ASSERT(__builtin_constant_p(42));
    ASSERT(__builtin_constant_p(sizeof(int)));
    ASSERT(__builtin_constant_p(3 + 4));

    ASSERT(__builtin_offsetof(struct task, priority) > 0);
    ASSERT(__builtin_offsetof(struct task, id) == 0);

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

    RUN(test_state_machine);
    RUN(test_arena);
    RUN(test_intrusive_list);
    RUN(test_hashmap);
    RUN(test_ring_buffer);
    RUN(test_callback_registry);
    RUN(test_string_builder);
    RUN(test_bit_ops);
    RUN(test_xmacro);
    RUN(test_coroutine);
    RUN(test_vtable_oop);
    RUN(test_cleanup_goto);
    RUN(test_mmio);
    RUN(test_tagged_union);
    RUN(test_compile_time);

#undef RUN
    if (failures == 0)
        printf("test_c_real_world: ALL PASSED\n");
    else
        printf("test_c_real_world: %d FAILED\n", failures);
    return failures;
}
