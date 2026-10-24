//===- X86RepStringState.h - Explicit REP machine state ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_X86REPSTRINGSTATE_H
#define LLVM_IR_X86REPSTRINGSTATE_H

#include "llvm/IR/X86NativeMemoryAddress.h"

namespace llvm::X86 {
enum class RepStringKind {
  Move,
  Store,
  Load,
  Compare,
  Scan,
  Input,
  Output,
  Count
};
inline constexpr unsigned RepStringElementShift = 3;
inline constexpr unsigned RepStringNotEqual = 1 << 5;
inline constexpr unsigned RepStringAddressOverride = 1 << 6;
inline constexpr unsigned RepStringSegmentShift = 8;
inline constexpr unsigned RepStringDescriptorMask = 0xff7f;
// LAHF's five arithmetic bits in AH, OF in bit zero. Reserved bits are zero.
inline constexpr unsigned RepStringFlagsMask = 0xd501;
inline constexpr bool repStringCompares(RepStringKind Kind) {
  return Kind == RepStringKind::Compare || Kind == RepStringKind::Scan;
}
inline constexpr bool repStringHasSource(RepStringKind Kind) {
  return Kind == RepStringKind::Move || Kind == RepStringKind::Load ||
         Kind == RepStringKind::Compare || Kind == RepStringKind::Output;
}
inline constexpr bool repStringHasDestination(RepStringKind Kind) {
  return Kind == RepStringKind::Move || Kind == RepStringKind::Store ||
         Kind == RepStringKind::Compare || Kind == RepStringKind::Scan ||
         Kind == RepStringKind::Input;
}
inline constexpr bool repStringHasPort(RepStringKind Kind) {
  return Kind == RepStringKind::Input || Kind == RepStringKind::Output;
}
inline constexpr bool isRepStringControl(RepStringKind Kind, bool Is64,
                                         uint64_t Element, uint64_t Address,
                                         uint64_t Segment, uint64_t NotEqual) {
  return Kind < RepStringKind::Count &&
         (Element == 1 || Element == 2 || Element == 4 ||
          (Element == 8 && Is64 && !repStringHasPort(Kind))) &&
         isNativeMemoryAddressWidth(Is64, Address) &&
         isNativeMemorySegmentPrefix(Segment) &&
         (repStringHasSource(Kind) || Segment == 0) && NotEqual <= 1 &&
         (repStringCompares(Kind) || NotEqual == 0);
}
} // namespace llvm::X86

#endif // LLVM_IR_X86REPSTRINGSTATE_H
