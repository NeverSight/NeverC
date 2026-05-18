//===-- llvm/Support/Signposts.h - Interval debug annotations ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file Some OS's provide profilers that allow applications to provide custom
/// annotations to the profiler. For example, on Xcode 10 and later 'signposts'
/// can be emitted by the application and these will be rendered to the Points
/// of Interest track on the instruments timeline.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_SIGNPOSTS_H
#define LLVM_SUPPORT_SIGNPOSTS_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Mutex.h"
#include <memory>

#if LLVM_SUPPORT_XCODE_SIGNPOSTS
#include <Availability.h>
#include <os/signpost.h>
#define SIGNPOSTS_AVAILABLE()                                                  \
  __builtin_available(macos 10.14, iOS 12, tvOS 12, watchOS 5, *)
#endif

namespace llvm {

#if LLVM_SUPPORT_XCODE_SIGNPOSTS
class SignpostEmitterImpl {
  using LogPtrTy = std::unique_ptr<os_log_t>;
  using LogTy = LogPtrTy::element_type;
  LogPtrTy SignpostLog;
  DenseMap<const void *, os_signpost_id_t> Signposts;
  sys::SmartMutex<true> Mutex;
  LogTy &getLogger() const { return *SignpostLog; }
  os_signpost_id_t getSignpostForObject(const void *O) {
    sys::SmartScopedLock<true> Lock(Mutex);
    const auto &I = Signposts.find(O);
    if (I != Signposts.end())
      return I->second;
    os_signpost_id_t ID = {};
    if (SIGNPOSTS_AVAILABLE())
      ID = os_signpost_id_make_with_pointer(getLogger(), O);
    const auto &Inserted = Signposts.insert({O, ID});
    return Inserted.first->second;
  }
  static os_log_t *LogCreator() {
    os_log_t *X = (os_log_t *)malloc(sizeof(os_log_t));
    *X = os_log_create("org.llvm.signposts", "toolchain");
    return X;
  }

public:
  SignpostEmitterImpl() : SignpostLog(LogCreator()) {}
  bool isEnabled() const {
    if (SIGNPOSTS_AVAILABLE())
      return os_signpost_enabled(*SignpostLog);
    return false;
  }
  void startInterval(const void *O, StringRef Name) {
    if (isEnabled()) {
      if (SIGNPOSTS_AVAILABLE())
        os_signpost_interval_begin(getLogger(), getSignpostForObject(O),
                                   "LLVM Timers", "%s", Name.data());
    }
  }
  void endInterval(const void *O, StringRef Name) {
    if (isEnabled()) {
      if (SIGNPOSTS_AVAILABLE())
        os_signpost_interval_end(getLogger(), getSignpostForObject(O),
                                 "LLVM Timers", "");
    }
  }
};
#else
class SignpostEmitterImpl {};
#endif

#if LLVM_SUPPORT_XCODE_SIGNPOSTS
#define HAVE_ANY_SIGNPOST_IMPL 1
#else
#define HAVE_ANY_SIGNPOST_IMPL 0
#endif

class StringRef;

/// Manages the emission of signposts into the recording method supported by
/// the OS.
class SignpostEmitter {
  std::unique_ptr<SignpostEmitterImpl> Impl;

public:
  SignpostEmitter();
  ~SignpostEmitter();

  bool isEnabled() const;

  /// Begin a signposted interval for a given object.
  void startInterval(const void *O, StringRef Name);
  /// End a signposted interval for a given object.
  void endInterval(const void *O, StringRef Name);
};

inline SignpostEmitter::SignpostEmitter() {
#if HAVE_ANY_SIGNPOST_IMPL
  Impl = std::unique_ptr<SignpostEmitterImpl>(new SignpostEmitterImpl());
#endif
}
inline SignpostEmitter::~SignpostEmitter() = default;
inline bool SignpostEmitter::isEnabled() const {
#if HAVE_ANY_SIGNPOST_IMPL
  return Impl->isEnabled();
#else
  return false;
#endif
}
inline void SignpostEmitter::startInterval(const void *O, StringRef Name) {
#if HAVE_ANY_SIGNPOST_IMPL
  if (Impl == 0)
    return;
  return Impl->startInterval(O, Name);
#endif
}
inline void SignpostEmitter::endInterval(const void *O, StringRef Name) {
#if HAVE_ANY_SIGNPOST_IMPL
  if (Impl == 0)
    return;
  Impl->endInterval(O, Name);
#endif
}

} // end namespace llvm

#endif // LLVM_SUPPORT_SIGNPOSTS_H
