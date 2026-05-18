#ifndef CSUPPORT_LR_LW_LMUTEX_H
#define CSUPPORT_LR_LW_LMUTEX_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/*
 * Pure C interface for RWMutexImpl.
 * The handle is an opaque pointer to platform-specific rwlock data
 * (e.g. pthread_rwlock_t on Unix).
 */

void *csupport_rwmutex_create(void);
void csupport_rwmutex_destroy(void *handle);
int csupport_rwmutex_lock_shared(void *handle);
int csupport_rwmutex_unlock_shared(void *handle);
int csupport_rwmutex_lock(void *handle);
int csupport_rwmutex_unlock(void *handle);

#ifdef __cplusplus
}
#endif
#endif
