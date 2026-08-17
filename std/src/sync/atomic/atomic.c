#include "neverc/std/sync/atomic.h"
#include "neverc/std/_platform.h"
#include <string.h>

#if defined(_MSC_VER) && !defined(__clang__) && !defined(__neverc__)
#include <intrin.h>

int32_t neverc_atomic_load_int32(const volatile int32_t *addr) { return _InterlockedOr((volatile long*)addr, 0); }
int64_t neverc_atomic_load_int64(const volatile int64_t *addr) { return _InterlockedOr64((volatile long long*)addr, 0); }
uint32_t neverc_atomic_load_uint32(const volatile uint32_t *addr) { return (uint32_t)_InterlockedOr((volatile long*)addr, 0); }
uint64_t neverc_atomic_load_uint64(const volatile uint64_t *addr) { return (uint64_t)_InterlockedOr64((volatile long long*)addr, 0); }
void *neverc_atomic_load_pointer(void *const volatile *addr) {
    /* InterlockedOr64 is 64-bit-only; CompareExchangePointer is a no-op
     * load on both 32- and 64-bit Windows (NULL/NULL succeeds only when the
     * pointer is already NULL, otherwise it returns the current value). */
    return _InterlockedCompareExchangePointer((void *volatile *)addr, NULL, NULL);
}

void neverc_atomic_store_int32(volatile int32_t *addr, int32_t val) { _InterlockedExchange((volatile long*)addr, val); }
void neverc_atomic_store_int64(volatile int64_t *addr, int64_t val) { _InterlockedExchange64((volatile long long*)addr, val); }
void neverc_atomic_store_uint32(volatile uint32_t *addr, uint32_t val) { _InterlockedExchange((volatile long*)addr, (long)val); }
void neverc_atomic_store_uint64(volatile uint64_t *addr, uint64_t val) { _InterlockedExchange64((volatile long long*)addr, (long long)val); }
void neverc_atomic_store_pointer(void *volatile *addr, void *val) { _InterlockedExchangePointer(addr, val); }

int32_t neverc_atomic_add_int32(volatile int32_t *addr, int32_t d) { return _InterlockedExchangeAdd((volatile long*)addr, d) + d; }
int64_t neverc_atomic_add_int64(volatile int64_t *addr, int64_t d) { return _InterlockedExchangeAdd64((volatile long long*)addr, d) + d; }
uint32_t neverc_atomic_add_uint32(volatile uint32_t *addr, uint32_t d) { return (uint32_t)(_InterlockedExchangeAdd((volatile long*)addr, (long)d) + (long)d); }
uint64_t neverc_atomic_add_uint64(volatile uint64_t *addr, uint64_t d) { return (uint64_t)(_InterlockedExchangeAdd64((volatile long long*)addr, (long long)d) + (long long)d); }

int32_t neverc_atomic_swap_int32(volatile int32_t *addr, int32_t v) { return _InterlockedExchange((volatile long*)addr, v); }
int64_t neverc_atomic_swap_int64(volatile int64_t *addr, int64_t v) { return _InterlockedExchange64((volatile long long*)addr, v); }
uint32_t neverc_atomic_swap_uint32(volatile uint32_t *addr, uint32_t v) { return (uint32_t)_InterlockedExchange((volatile long*)addr, (long)v); }
uint64_t neverc_atomic_swap_uint64(volatile uint64_t *addr, uint64_t v) { return (uint64_t)_InterlockedExchange64((volatile long long*)addr, (long long)v); }
void *neverc_atomic_swap_pointer(void *volatile *addr, void *v) { return _InterlockedExchangePointer(addr, v); }

int neverc_atomic_cas_int32(volatile int32_t *addr, int32_t old_v, int32_t new_v) { return _InterlockedCompareExchange((volatile long*)addr, new_v, old_v) == old_v; }
int neverc_atomic_cas_int64(volatile int64_t *addr, int64_t old_v, int64_t new_v) { return _InterlockedCompareExchange64((volatile long long*)addr, new_v, old_v) == old_v; }
int neverc_atomic_cas_uint32(volatile uint32_t *addr, uint32_t old_v, uint32_t new_v) { return (uint32_t)_InterlockedCompareExchange((volatile long*)addr, (long)new_v, (long)old_v) == old_v; }
int neverc_atomic_cas_uint64(volatile uint64_t *addr, uint64_t old_v, uint64_t new_v) { return (uint64_t)_InterlockedCompareExchange64((volatile long long*)addr, (long long)new_v, (long long)old_v) == old_v; }
int neverc_atomic_cas_pointer(void *volatile *addr, void *old_v, void *new_v) { return _InterlockedCompareExchangePointer(addr, new_v, old_v) == old_v; }

#else /* GCC/Clang */

int32_t neverc_atomic_load_int32(const volatile int32_t *addr) {
    return __atomic_load_n(addr, __ATOMIC_SEQ_CST);
}
uint32_t neverc_atomic_load_uint32(const volatile uint32_t *addr) {
    return __atomic_load_n(addr, __ATOMIC_SEQ_CST);
}
void *neverc_atomic_load_pointer(void *const volatile *addr) {
    return __atomic_load_n(addr, __ATOMIC_SEQ_CST);
}

void neverc_atomic_store_int32(volatile int32_t *addr, int32_t val) {
    __atomic_store_n(addr, val, __ATOMIC_SEQ_CST);
}
void neverc_atomic_store_uint32(volatile uint32_t *addr, uint32_t val) {
    __atomic_store_n(addr, val, __ATOMIC_SEQ_CST);
}
void neverc_atomic_store_pointer(void *volatile *addr, void *val) {
    __atomic_store_n(addr, val, __ATOMIC_SEQ_CST);
}

int32_t neverc_atomic_add_int32(volatile int32_t *addr, int32_t delta) {
    return __atomic_fetch_add(addr, delta, __ATOMIC_SEQ_CST) + delta;
}
uint32_t neverc_atomic_add_uint32(volatile uint32_t *addr, uint32_t delta) {
    return __atomic_fetch_add(addr, delta, __ATOMIC_SEQ_CST) + delta;
}

int32_t neverc_atomic_swap_int32(volatile int32_t *addr, int32_t new_val) {
    return __atomic_exchange_n(addr, new_val, __ATOMIC_SEQ_CST);
}
uint32_t neverc_atomic_swap_uint32(volatile uint32_t *addr, uint32_t new_val) {
    return __atomic_exchange_n(addr, new_val, __ATOMIC_SEQ_CST);
}
void *neverc_atomic_swap_pointer(void *volatile *addr, void *new_val) {
    return __atomic_exchange_n(addr, new_val, __ATOMIC_SEQ_CST);
}

int neverc_atomic_cas_int32(volatile int32_t *addr, int32_t old_val, int32_t new_val) {
    return __atomic_compare_exchange_n(addr, &old_val, new_val, 0,
                                       __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}
int neverc_atomic_cas_uint32(volatile uint32_t *addr, uint32_t old_val, uint32_t new_val) {
    return __atomic_compare_exchange_n(addr, &old_val, new_val, 0,
                                       __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}
int neverc_atomic_cas_pointer(void *volatile *addr, void *old_val, void *new_val) {
    return __atomic_compare_exchange_n(addr, &old_val, new_val, 0,
                                       __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

#if UINTPTR_MAX <= 0xffffffffu
/*
 * 32-bit hosts may lack a lock-free 64-bit __atomic and can tear unaligned
 * 64-bit accesses. A hashed spinlock table in this TU covers all 64-bit ops.
 */
static volatile int32_t neverc_a64_gates[16];

static unsigned neverc_a64_index(const volatile void *addr) {
    return (unsigned)(((uintptr_t)addr >> 3) & 15u);
}

static void neverc_a64_lock(const volatile void *addr) {
    volatile int32_t *gate = &neverc_a64_gates[neverc_a64_index(addr)];
    int32_t expected = 0;
    while (!__atomic_compare_exchange_n(gate, &expected, 1, 0,
                                        __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
        expected = 0;
}

static void neverc_a64_unlock(const volatile void *addr) {
    __atomic_store_n(&neverc_a64_gates[neverc_a64_index(addr)], 0,
                     __ATOMIC_RELEASE);
}

static int64_t neverc_a64_read(const volatile int64_t *addr) {
    int64_t value;
    memcpy(&value, (const void *)addr, sizeof(value));
    return value;
}

static void neverc_a64_write(volatile int64_t *addr, int64_t value) {
    memcpy((void *)addr, &value, sizeof(value));
}

int64_t neverc_atomic_load_int64(const volatile int64_t *addr) {
    neverc_a64_lock(addr);
    int64_t value = neverc_a64_read(addr);
    neverc_a64_unlock(addr);
    return value;
}
uint64_t neverc_atomic_load_uint64(const volatile uint64_t *addr) {
    return (uint64_t)neverc_atomic_load_int64((const volatile int64_t *)addr);
}

void neverc_atomic_store_int64(volatile int64_t *addr, int64_t val) {
    neverc_a64_lock(addr);
    neverc_a64_write(addr, val);
    neverc_a64_unlock(addr);
}
void neverc_atomic_store_uint64(volatile uint64_t *addr, uint64_t val) {
    neverc_atomic_store_int64((volatile int64_t *)addr, (int64_t)val);
}

int64_t neverc_atomic_add_int64(volatile int64_t *addr, int64_t delta) {
    neverc_a64_lock(addr);
    int64_t value = neverc_a64_read(addr) + delta;
    neverc_a64_write(addr, value);
    neverc_a64_unlock(addr);
    return value;
}
uint64_t neverc_atomic_add_uint64(volatile uint64_t *addr, uint64_t delta) {
    return (uint64_t)neverc_atomic_add_int64((volatile int64_t *)addr,
                                             (int64_t)delta);
}

int64_t neverc_atomic_swap_int64(volatile int64_t *addr, int64_t new_val) {
    neverc_a64_lock(addr);
    int64_t old = neverc_a64_read(addr);
    neverc_a64_write(addr, new_val);
    neverc_a64_unlock(addr);
    return old;
}
uint64_t neverc_atomic_swap_uint64(volatile uint64_t *addr, uint64_t new_val) {
    return (uint64_t)neverc_atomic_swap_int64((volatile int64_t *)addr,
                                              (int64_t)new_val);
}

int neverc_atomic_cas_int64(volatile int64_t *addr, int64_t old_val,
                            int64_t new_val) {
    neverc_a64_lock(addr);
    int64_t current = neverc_a64_read(addr);
    int ok = current == old_val;
    if (ok)
        neverc_a64_write(addr, new_val);
    neverc_a64_unlock(addr);
    return ok;
}
int neverc_atomic_cas_uint64(volatile uint64_t *addr, uint64_t old_val,
                             uint64_t new_val) {
    return neverc_atomic_cas_int64((volatile int64_t *)addr, (int64_t)old_val,
                                   (int64_t)new_val);
}

#else /* 64-bit: hardware 64-bit atomics */

int64_t neverc_atomic_load_int64(const volatile int64_t *addr) {
    return __atomic_load_n(addr, __ATOMIC_SEQ_CST);
}
uint64_t neverc_atomic_load_uint64(const volatile uint64_t *addr) {
    return __atomic_load_n(addr, __ATOMIC_SEQ_CST);
}

void neverc_atomic_store_int64(volatile int64_t *addr, int64_t val) {
    __atomic_store_n(addr, val, __ATOMIC_SEQ_CST);
}
void neverc_atomic_store_uint64(volatile uint64_t *addr, uint64_t val) {
    __atomic_store_n(addr, val, __ATOMIC_SEQ_CST);
}

int64_t neverc_atomic_add_int64(volatile int64_t *addr, int64_t delta) {
    return __atomic_fetch_add(addr, delta, __ATOMIC_SEQ_CST) + delta;
}
uint64_t neverc_atomic_add_uint64(volatile uint64_t *addr, uint64_t delta) {
    return __atomic_fetch_add(addr, delta, __ATOMIC_SEQ_CST) + delta;
}

int64_t neverc_atomic_swap_int64(volatile int64_t *addr, int64_t new_val) {
    return __atomic_exchange_n(addr, new_val, __ATOMIC_SEQ_CST);
}
uint64_t neverc_atomic_swap_uint64(volatile uint64_t *addr, uint64_t new_val) {
    return __atomic_exchange_n(addr, new_val, __ATOMIC_SEQ_CST);
}

int neverc_atomic_cas_int64(volatile int64_t *addr, int64_t old_val, int64_t new_val) {
    return __atomic_compare_exchange_n(addr, &old_val, new_val, 0,
                                       __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}
int neverc_atomic_cas_uint64(volatile uint64_t *addr, uint64_t old_val, uint64_t new_val) {
    return __atomic_compare_exchange_n(addr, &old_val, new_val, 0,
                                       __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

#endif /* UINTPTR_MAX */

#endif /* _MSC_VER */
