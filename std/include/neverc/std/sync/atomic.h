#ifndef NEVERC_SYNC_ATOMIC_H
#define NEVERC_SYNC_ATOMIC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int32_t  neverc_atomic_load_int32(const volatile int32_t *addr);
int64_t  neverc_atomic_load_int64(const volatile int64_t *addr);
uint32_t neverc_atomic_load_uint32(const volatile uint32_t *addr);
uint64_t neverc_atomic_load_uint64(const volatile uint64_t *addr);
void    *neverc_atomic_load_pointer(void *const volatile *addr);

void neverc_atomic_store_int32(volatile int32_t *addr, int32_t val);
void neverc_atomic_store_int64(volatile int64_t *addr, int64_t val);
void neverc_atomic_store_uint32(volatile uint32_t *addr, uint32_t val);
void neverc_atomic_store_uint64(volatile uint64_t *addr, uint64_t val);
void neverc_atomic_store_pointer(void *volatile *addr, void *val);

int32_t  neverc_atomic_add_int32(volatile int32_t *addr, int32_t delta);
int64_t  neverc_atomic_add_int64(volatile int64_t *addr, int64_t delta);
uint32_t neverc_atomic_add_uint32(volatile uint32_t *addr, uint32_t delta);
uint64_t neverc_atomic_add_uint64(volatile uint64_t *addr, uint64_t delta);

int32_t  neverc_atomic_swap_int32(volatile int32_t *addr, int32_t new_val);
int64_t  neverc_atomic_swap_int64(volatile int64_t *addr, int64_t new_val);
uint32_t neverc_atomic_swap_uint32(volatile uint32_t *addr, uint32_t new_val);
uint64_t neverc_atomic_swap_uint64(volatile uint64_t *addr, uint64_t new_val);
void    *neverc_atomic_swap_pointer(void *volatile *addr, void *new_val);

int neverc_atomic_cas_int32(volatile int32_t *addr, int32_t old_val, int32_t new_val);
int neverc_atomic_cas_int64(volatile int64_t *addr, int64_t old_val, int64_t new_val);
int neverc_atomic_cas_uint32(volatile uint32_t *addr, uint32_t old_val, uint32_t new_val);
int neverc_atomic_cas_uint64(volatile uint64_t *addr, uint64_t old_val, uint64_t new_val);
int neverc_atomic_cas_pointer(void *volatile *addr, void *old_val, void *new_val);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/sync.h>
#endif


#endif
