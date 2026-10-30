//===- X86AMXStackMemory.h - Stack-default AMX transfers ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Tile memory consumes the SIB index as a stride. A small, compiler-owned
// function can use BP for the complete base while its ordinary prologue and
// RSP-based unwind information preserve the caller's frame. In particular,
// never borrow the caller's live frame pointer across a faulting instruction.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_X86_X86AMXSTACKMEMORY_H
#define LLVM_LIB_TARGET_X86_X86AMXSTACKMEMORY_H

#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/IntrinsicsX86.h"

namespace llvm::X86 {

inline constexpr StringLiteral AMXStackMemoryAttribute = "x86-amx-stack-memory";

inline bool isAMXTileMemoryIntrinsic(Intrinsic::ID ID) {
  switch (ID) {
  case Intrinsic::x86_tileloadd64_address:
  case Intrinsic::x86_tileloaddt164_address:
  case Intrinsic::x86_tileloaddrs64_address:
  case Intrinsic::x86_tileloaddrst164_address:
  case Intrinsic::x86_tilestored64_address:
    return true;
  default:
    return false;
  }
}

// The attribute is private code-generation state, not permission for arbitrary
// IR to overwrite BP. Validate the complete leaf before relying on its frame.
inline bool isAMXStackMemoryHelper(const Function &F) {
  if (!F.hasFnAttribute(AMXStackMemoryAttribute) || !F.hasLocalLinkage() ||
      F.getCallingConv() != CallingConv::PreserveAll || F.arg_size() != 2 ||
      !F.getReturnType()->isVoidTy() ||
      !F.getArg(0)->getType()->isIntegerTy(64) ||
      !F.getArg(1)->getType()->isIntegerTy(64) || F.size() != 1 ||
      F.front().size() != 2 ||
      F.getFnAttribute("frame-pointer").getValueAsString() != "none")
    return false;
  const auto *I = dyn_cast<IntrinsicInst>(&F.front().front());
  return I && isAMXTileMemoryIntrinsic(I->getIntrinsicID()) &&
         I->getArgOperand(1) == F.getArg(0) &&
         I->getArgOperand(2) == F.getArg(1) &&
         isa<ReturnInst>(F.front().back());
}

} // namespace llvm::X86

#endif // LLVM_LIB_TARGET_X86_X86AMXSTACKMEMORY_H
