//===- RWMutex.h - Reader/Writer Mutual Exclusion Lock ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the llvm::sys::RWMutex class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_RWMUTEX_H
#define LLVM_SUPPORT_RWMUTEX_H

#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Threading.h"
#include <assert.h>
#include <mutex>
#include <shared_mutex>

#if defined(__APPLE__)
#define LLVM_USE_RW_MUTEX_IMPL
#endif

namespace llvm {
namespace sys {

#if defined(LLVM_USE_RW_MUTEX_IMPL)

#if defined(LLVM_ENABLE_THREADS) && LLVM_ENABLE_THREADS != 0
extern "C" void *csupport_rwmutex_create(void);
extern "C" void csupport_rwmutex_destroy(void *handle);
extern "C" int csupport_rwmutex_lock_shared(void *handle);
extern "C" int csupport_rwmutex_unlock_shared(void *handle);
extern "C" int csupport_rwmutex_lock(void *handle);
extern "C" int csupport_rwmutex_unlock(void *handle);
#endif

/// Platform agnostic RWMutex class.
class RWMutexImpl {
public:
#if !defined(LLVM_ENABLE_THREADS) || LLVM_ENABLE_THREADS == 0
  explicit RWMutexImpl() = default;
  ~RWMutexImpl() = default;
  bool lock_shared() { return true; }
  bool unlock_shared() { return true; }
  bool lock() { return true; }
  bool unlock() { return true; }
#else
  explicit RWMutexImpl() { data_ = csupport_rwmutex_create(); }
  ~RWMutexImpl() { csupport_rwmutex_destroy(data_); }
  bool lock_shared() { return csupport_rwmutex_lock_shared(data_); }
  bool unlock_shared() { return csupport_rwmutex_unlock_shared(data_); }
  bool lock() { return csupport_rwmutex_lock(data_); }
  bool unlock() { return csupport_rwmutex_unlock(data_); }
#endif

  RWMutexImpl(const RWMutexImpl &original) = delete;
  RWMutexImpl &operator=(const RWMutexImpl &) = delete;

private:
#if defined(LLVM_ENABLE_THREADS) && LLVM_ENABLE_THREADS != 0
  void *data_ = nullptr;
#endif
};
#endif

/// SmartMutex - An R/W mutex with a compile time constant parameter that
/// indicates whether this mutex should become a no-op when we're not
/// running in multithreaded mode.
template <bool mt_only> class SmartRWMutex {
#if !defined(LLVM_USE_RW_MUTEX_IMPL)
  std::shared_mutex impl;
#else
  RWMutexImpl impl;
#endif
  unsigned readers = 0;
  unsigned writers = 0;

public:
  bool lock_shared() {
    if (!mt_only || llvm_is_multithreaded()) {
      impl.lock_shared();
      return true;
    }

    // Single-threaded debugging code.  This would be racy in multithreaded
    // mode, but provides not basic checks in single threaded mode.
    ++readers;
    return true;
  }

  bool unlock_shared() {
    if (!mt_only || llvm_is_multithreaded()) {
      impl.unlock_shared();
      return true;
    }

    // Single-threaded debugging code.  This would be racy in multithreaded
    // mode, but provides not basic checks in single threaded mode.
    assert(readers > 0 && "Reader lock not acquired before release!");
    --readers;
    return true;
  }

  bool lock() {
    if (!mt_only || llvm_is_multithreaded()) {
      impl.lock();
      return true;
    }

    // Single-threaded debugging code.  This would be racy in multithreaded
    // mode, but provides not basic checks in single threaded mode.
    assert(writers == 0 && "Writer lock already acquired!");
    ++writers;
    return true;
  }

  bool unlock() {
    if (!mt_only || llvm_is_multithreaded()) {
      impl.unlock();
      return true;
    }

    // Single-threaded debugging code.  This would be racy in multithreaded
    // mode, but provides not basic checks in single threaded mode.
    assert(writers == 1 && "Writer lock not acquired before release!");
    --writers;
    return true;
  }
};

typedef SmartRWMutex<false> RWMutex;

/// ScopedReader - RAII acquisition of a reader lock
#if !defined(LLVM_USE_RW_MUTEX_IMPL)
template <bool mt_only>
using SmartScopedReader = const std::shared_lock<SmartRWMutex<mt_only>>;
#else
template <bool mt_only> struct SmartScopedReader {
  SmartRWMutex<mt_only> &mutex;

  explicit SmartScopedReader(SmartRWMutex<mt_only> &m) : mutex(m) {
    mutex.lock_shared();
  }

  ~SmartScopedReader() { mutex.unlock_shared(); }
};
#endif
typedef SmartScopedReader<false> ScopedReader;

/// ScopedWriter - RAII acquisition of a writer lock
#if !defined(LLVM_USE_RW_MUTEX_IMPL)
template <bool mt_only>
using SmartScopedWriter = std::lock_guard<SmartRWMutex<mt_only>>;
#else
template <bool mt_only> struct SmartScopedWriter {
  SmartRWMutex<mt_only> &mutex;

  explicit SmartScopedWriter(SmartRWMutex<mt_only> &m) : mutex(m) {
    mutex.lock();
  }

  ~SmartScopedWriter() { mutex.unlock(); }
};
#endif
typedef SmartScopedWriter<false> ScopedWriter;

} // end namespace sys
} // end namespace llvm

#endif // LLVM_SUPPORT_RWMUTEX_H
