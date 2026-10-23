//===- X86SgxIntrinsics.h - SGX intrinsic leaf contracts ---------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_X86SGXINTRINSICS_H
#define LLVM_IR_X86SGXINTRINSICS_H

#include <cstdint>

namespace llvm::X86 {

// EAX selects the leaf, independently of the upper natural-width rAX bits.
// These leaves fall through without the entry/exit/resume continuation ABI.
inline constexpr bool isBoundedEncluLeaf(uint64_t Control) {
  const auto Leaf = static_cast<uint32_t>(Control);
  return Leaf == 0 || Leaf == 1 || (Leaf >= 5 && Leaf <= 11);
}

} // namespace llvm::X86

#endif // LLVM_IR_X86SGXINTRINSICS_H
