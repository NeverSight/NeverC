//===- X86SmxIntrinsics.h - SMX intrinsic leaf contracts ---------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_X86SMXINTRINSICS_H
#define LLVM_IR_X86SMXINTRINSICS_H

#include <cstdint>

namespace llvm::X86 {

// CAPABILITIES, PARAMETERS, SMCTRL and WAKEUP return to the issuing processor.
// EAX selects the leaf independently of the upper natural-width rAX bits.
inline constexpr bool isBoundedGetsecLeaf(uint64_t Control) {
  const auto Leaf = static_cast<uint32_t>(Control);
  return Leaf == 0 || (Leaf >= 6 && Leaf <= 8);
}

} // namespace llvm::X86

#endif // LLVM_IR_X86SMXINTRINSICS_H
