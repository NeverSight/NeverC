//===- llvm/Support/COM.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// Provides a library for accessing COM functionality of the Host OS.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_COM_H
#define LLVM_SUPPORT_COM_H

#include "csupport/lcom.h"

namespace llvm {
namespace sys {

enum class COMThreadingMode {
  SingleThreaded = CSUPPORT_COM_SINGLE_THREADED,
  MultiThreaded = CSUPPORT_COM_MULTI_THREADED
};

class InitializeCOMRAII {
public:
  explicit InitializeCOMRAII(COMThreadingMode Threading,
                             bool SpeedOverMemory = false)
      : Initialized(csupport_com_initialize(
                        static_cast<csupport_com_threading_mode_t>(Threading),
                        SpeedOverMemory ? 1 : 0) != 0) {}
  ~InitializeCOMRAII() {
    if (Initialized)
      csupport_com_uninitialize();
  }

private:
  InitializeCOMRAII(const InitializeCOMRAII &) = delete;
  void operator=(const InitializeCOMRAII &) = delete;

  bool Initialized;
};
} // namespace sys
} // namespace llvm

#endif
