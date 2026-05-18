/*===- RWMutex.c - Reader/Writer Lock (pure C) ------------------*- C -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions. See https://llvm.org/LICENSE.txt for license information.       *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                   *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/
#include "include/csupport/lr_lw_lmutex.h"
#include "llvm/Config/config.h"
#include "llvm/Config/llvm-config.h"

#if !defined(LLVM_ENABLE_THREADS) || LLVM_ENABLE_THREADS == 0

void *csupport_rwmutex_create(void) { return NULL; }
void csupport_rwmutex_destroy(void *handle) { (void)handle; }
int csupport_rwmutex_lock_shared(void *handle) { (void)handle; return 1; }
int csupport_rwmutex_unlock_shared(void *handle) { (void)handle; return 1; }
int csupport_rwmutex_lock(void *handle) { (void)handle; return 1; }
int csupport_rwmutex_unlock(void *handle) { (void)handle; return 1; }

#elif defined(HAVE_PTHREAD_H) && defined(HAVE_PTHREAD_RWLOCK_INIT)

#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

void *csupport_rwmutex_create(void) {
  pthread_rwlock_t *rwlock = (pthread_rwlock_t *)malloc(sizeof(pthread_rwlock_t));
  if (!rwlock)
    return NULL;
#ifdef __APPLE__
  memset(rwlock, 0, sizeof(pthread_rwlock_t));
#endif
  int rc = pthread_rwlock_init(rwlock, NULL);
  assert(rc == 0);
  (void)rc;
  return rwlock;
}

void csupport_rwmutex_destroy(void *handle) {
  pthread_rwlock_t *rwlock = (pthread_rwlock_t *)handle;
  assert(rwlock != NULL);
  pthread_rwlock_destroy(rwlock);
  free(rwlock);
}

int csupport_rwmutex_lock_shared(void *handle) {
  pthread_rwlock_t *rwlock = (pthread_rwlock_t *)handle;
  assert(rwlock != NULL);
  return pthread_rwlock_rdlock(rwlock) == 0;
}

int csupport_rwmutex_unlock_shared(void *handle) {
  pthread_rwlock_t *rwlock = (pthread_rwlock_t *)handle;
  assert(rwlock != NULL);
  return pthread_rwlock_unlock(rwlock) == 0;
}

int csupport_rwmutex_lock(void *handle) {
  pthread_rwlock_t *rwlock = (pthread_rwlock_t *)handle;
  assert(rwlock != NULL);
  return pthread_rwlock_wrlock(rwlock) == 0;
}

int csupport_rwmutex_unlock(void *handle) {
  pthread_rwlock_t *rwlock = (pthread_rwlock_t *)handle;
  assert(rwlock != NULL);
  return pthread_rwlock_unlock(rwlock) == 0;
}

#else

void *csupport_rwmutex_create(void) { return NULL; }
void csupport_rwmutex_destroy(void *handle) { (void)handle; }
int csupport_rwmutex_lock_shared(void *handle) { (void)handle; return 1; }
int csupport_rwmutex_unlock_shared(void *handle) { (void)handle; return 1; }
int csupport_rwmutex_lock(void *handle) { (void)handle; return 1; }
int csupport_rwmutex_unlock(void *handle) { (void)handle; return 1; }

#endif
