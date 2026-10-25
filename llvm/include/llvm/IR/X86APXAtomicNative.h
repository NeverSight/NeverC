//===- X86APXAtomicNative.h - APX native atomic controls ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_X86APXATOMICNATIVE_H
#define LLVM_IR_X86APXATOMICNATIVE_H

namespace llvm::X86 {
// Private machine descriptors. Public APIs use separate constant operands.
inline constexpr unsigned APXAtomicConditionCount = 16;
inline constexpr unsigned APXRaoOperationCount = 4;
inline constexpr unsigned APXAtomicAddress32 = 1 << 4;
inline constexpr unsigned APXAtomicDescriptorMask = 31;
} // namespace llvm::X86

#endif // LLVM_IR_X86APXATOMICNATIVE_H
