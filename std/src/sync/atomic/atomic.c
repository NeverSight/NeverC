#include "neverc/std/sync/atomic.h"
#include "neverc/std/_platform.h"

#if defined(_MSC_VER)
#include <intrin.h>

int32_t neverc_atomic_load_int32(const volatile int32_t *addr) { return _InterlockedOr((volatile long*)addr, 0); }
int64_t neverc_atomic_load_int64(const volatile int64_t *addr) { return _InterlockedOr64((volatile long long*)addr, 0); }
uint32_t neverc_atomic_load_uint32(const volatile uint32_t *addr) { return (uint32_t)_InterlockedOr((volatile long*)addr, 0); }
uint64_t neverc_atomic_load_uint64(const volatile uint64_t *addr) { return (uint64_t)_InterlockedOr64((volatile long long*)addr, 0); }
void *neverc_atomic_load_pointer(void *const volatile *addr) { return (void*)_InterlockedOr64((volatile long long*)addr, 0); }

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
int64_t neverc_atomic_load_int64(const volatile int64_t *addr) {
    return __atomic_load_n(addr, __ATOMIC_SEQ_CST);
}
uint32_t neverc_atomic_load_uint32(const volatile uint32_t *addr) {
    return __atomic_load_n(addr, __ATOMIC_SEQ_CST);
}
uint64_t neverc_atomic_load_uint64(const volatile uint64_t *addr) {
    return __atomic_load_n(addr, __ATOMIC_SEQ_CST);
}
void *neverc_atomic_load_pointer(void *const volatile *addr) {
    return __atomic_load_n(addr, __ATOMIC_SEQ_CST);
}

void neverc_atomic_store_int32(volatile int32_t *addr, int32_t val) {
    __atomic_store_n(addr, val, __ATOMIC_SEQ_CST);
}
void neverc_atomic_store_int64(volatile int64_t *addr, int64_t val) {
    __atomic_store_n(addr, val, __ATOMIC_SEQ_CST);
}
void neverc_atomic_store_uint32(volatile uint32_t *addr, uint32_t val) {
    __atomic_store_n(addr, val, __ATOMIC_SEQ_CST);
}
void neverc_atomic_store_uint64(volatile uint64_t *addr, uint64_t val) {
    __atomic_store_n(addr, val, __ATOMIC_SEQ_CST);
}
void neverc_atomic_store_pointer(void *volatile *addr, void *val) {
    __atomic_store_n(addr, val, __ATOMIC_SEQ_CST);
}

int32_t neverc_atomic_add_int32(volatile int32_t *addr, int32_t delta) {
    return __atomic_fetch_add(addr, delta, __ATOMIC_SEQ_CST) + delta;
}
int64_t neverc_atomic_add_int64(volatile int64_t *addr, int64_t delta) {
    return __atomic_fetch_add(addr, delta, __ATOMIC_SEQ_CST) + delta;
}
uint32_t neverc_atomic_add_uint32(volatile uint32_t *addr, uint32_t delta) {
    return __atomic_fetch_add(addr, delta, __ATOMIC_SEQ_CST) + delta;
}
uint64_t neverc_atomic_add_uint64(volatile uint64_t *addr, uint64_t delta) {
    return __atomic_fetch_add(addr, delta, __ATOMIC_SEQ_CST) + delta;
}

int32_t neverc_atomic_swap_int32(volatile int32_t *addr, int32_t new_val) {
    return __atomic_exchange_n(addr, new_val, __ATOMIC_SEQ_CST);
}
int64_t neverc_atomic_swap_int64(volatile int64_t *addr, int64_t new_val) {
    return __atomic_exchange_n(addr, new_val, __ATOMIC_SEQ_CST);
}
uint32_t neverc_atomic_swap_uint32(volatile uint32_t *addr, uint32_t new_val) {
    return __atomic_exchange_n(addr, new_val, __ATOMIC_SEQ_CST);
}
uint64_t neverc_atomic_swap_uint64(volatile uint64_t *addr, uint64_t new_val) {
    return __atomic_exchange_n(addr, new_val, __ATOMIC_SEQ_CST);
}
void *neverc_atomic_swap_pointer(void *volatile *addr, void *new_val) {
    return __atomic_exchange_n(addr, new_val, __ATOMIC_SEQ_CST);
}

int neverc_atomic_cas_int32(volatile int32_t *addr, int32_t old_val, int32_t new_val) {
    return __atomic_compare_exchange_n(addr, &old_val, new_val, 0,
                                       __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}
int neverc_atomic_cas_int64(volatile int64_t *addr, int64_t old_val, int64_t new_val) {
    return __atomic_compare_exchange_n(addr, &old_val, new_val, 0,
                                       __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}
int neverc_atomic_cas_uint32(volatile uint32_t *addr, uint32_t old_val, uint32_t new_val) {
    return __atomic_compare_exchange_n(addr, &old_val, new_val, 0,
                                       __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}
int neverc_atomic_cas_uint64(volatile uint64_t *addr, uint64_t old_val, uint64_t new_val) {
    return __atomic_compare_exchange_n(addr, &old_val, new_val, 0,
                                       __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}
int neverc_atomic_cas_pointer(void *volatile *addr, void *old_val, void *new_val) {
    return __atomic_compare_exchange_n(addr, &old_val, new_val, 0,
                                       __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

#endif /* _MSC_VER */
