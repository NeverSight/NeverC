//===- X86NativeFrameReturn.h - Native frame returns ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_X86_X86NATIVEFRAMERETURN_H
#define LLVM_LIB_TARGET_X86_X86NATIVEFRAMERETURN_H

#include "MCTargetDesc/X86MCTargetDesc.h"
#include "llvm/Support/ErrorHandling.h"

namespace llvm::X86 {

enum class NativeFrameReturn : unsigned {
#define X86_FRAME_RETURN(Name, Opcode, LongOnly, Immediate, Uintr) Name,
#include "X86NativeFrameReturns.def"
#undef X86_FRAME_RETURN
  Count
};

struct NativeFrameReturnInfo {
  unsigned Opcode;
  bool LongOnly;
  bool Immediate;
  bool Uintr;
};

inline NativeFrameReturnInfo nativeFrameReturnInfo(unsigned Kind) {
  static constexpr NativeFrameReturnInfo Entries[] = {
#define X86_FRAME_RETURN(Name, Opcode, LongOnly, Immediate, Uintr)             \
  {X86::Opcode, LongOnly, Immediate, Uintr},
#include "X86NativeFrameReturns.def"
#undef X86_FRAME_RETURN
  };
  if (Kind >= unsigned(NativeFrameReturn::Count))
    report_fatal_error("invalid native frame-return operation");
  return Entries[Kind];
}

} // namespace llvm::X86

#endif
