//===- X86MpxState.h - Architectural MPX bank operations ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_X86MPXSTATE_H
#define LLVM_IR_X86MPXSTATE_H

namespace llvm::X86 {

// Model an original control transfer independently of compiler-created CFG.
// Initialize has the native conditional effect: MPX must be enabled and
// BNDPRESERVE must be clear. Preserve keeps the bank live through generated
// branches even when a function contains no explicit bounds instruction.
enum class MpxBranchKind : unsigned { Initialize, Preserve, Count };

// Private machine-pseudo control, never an SSA representation of a bound.
// The actual instruction owns BND0..3, BNDCFG, BNDSTATUS, enable checks and
// partial completion when bounds-table accesses fault.
enum class MpxOperation : unsigned {
  Make,
  Move,
  Load,
  Store,
  StoreLocked,
  CheckLowerRegister,
  CheckUpperRegister,
  CheckUpperRawRegister,
  CheckLowerAddress,
  CheckUpperAddress,
  CheckUpperRawAddress,
  LoadTable,
  StoreTable,
  Count
};

inline constexpr unsigned MpxRegisterCount = 4;
inline constexpr unsigned MpxAddress32 = 1u << 8;
inline constexpr unsigned MpxSourceMask = 3u << 6;
inline constexpr unsigned MpxDescriptorMask = 0x00ff01ff;

inline constexpr bool mpxIsCheckRegister(MpxOperation Op) {
  return Op >= MpxOperation::CheckLowerRegister &&
         Op <= MpxOperation::CheckUpperRawRegister;
}

inline constexpr bool mpxHasIndex(MpxOperation Op) {
  return Op == MpxOperation::Make || Op == MpxOperation::LoadTable ||
         Op == MpxOperation::StoreTable;
}

inline constexpr unsigned mpxDescriptor(MpxOperation Op, unsigned Bound,
                                        unsigned Source, bool Address32,
                                        unsigned SegmentPrefix) {
  return unsigned(Op) | (Bound << 4) | (Source << 6) |
         (Address32 ? MpxAddress32 : 0) | (SegmentPrefix << 16);
}

} // namespace llvm::X86

#endif
