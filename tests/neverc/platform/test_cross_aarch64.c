// NeverC test: cross-compilation to aarch64-linux-gnu (syntax-only)
// RUN: %neverc --target=aarch64-linux-gnu -std=gnu11 -fneverc-types -Wall -Werror -Wno-tentative-definition-incomplete-type -fsyntax-only %s
// This test verifies that NeverC can parse C code targeting aarch64.
// Rust-style integer keywords are off by default to play nice with third-party
// headers, so this test opts in via -fneverc-types.
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// u8/u16/u32/u64 are built-in NeverC keywords (Rust-style fixed-width
// unsigned integer types) — no typedef needed.

struct page {
    unsigned long flags;
    u32 order;
    void *virtual_addr;
};

static inline u64 read_sysreg_stub(void) {
    return 0;
}

struct cpu_info {
    u32 cpu_id;
    u32 cluster_id;
    u64 mpidr;
    bool online;
};

static _Alignas(64) struct cpu_info per_cpu_info[8];

#define barrier() __asm__ __volatile__("" ::: "memory")
#define smp_mb()  __atomic_thread_fence(__ATOMIC_SEQ_CST)

static inline u32 atomic_load_u32(const volatile u32 *p) {
    u32 v;
    __atomic_load(p, &v, __ATOMIC_ACQUIRE);
    return v;
}

static inline void atomic_store_u32(volatile u32 *p, u32 v) {
    __atomic_store(p, &v, __ATOMIC_RELEASE);
}

struct spinlock {
    volatile u32 val;
};

static inline void spin_lock(struct spinlock *l) {
    while (__atomic_exchange_n(&l->val, 1, __ATOMIC_ACQUIRE)) { }
}

static inline void spin_unlock(struct spinlock *l) {
    __atomic_store_n(&l->val, 0, __ATOMIC_RELEASE);
}

static struct spinlock global_lock = {0};

void test(void) {
    spin_lock(&global_lock);

    per_cpu_info[0].cpu_id = 0;
    per_cpu_info[0].online = true;
    barrier();
    smp_mb();

    u32 id = atomic_load_u32(&per_cpu_info[0].cpu_id);
    (void)id;

    spin_unlock(&global_lock);
}

int main(void) {
    test();
    return 0;
}
