//===- X86NativeArithmeticFlags.h - Explicit status transport -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_X86NATIVEARITHMETICFLAGS_H
#define LLVM_IR_X86NATIVEARITHMETICFLAGS_H

namespace llvm::X86 {
// LAHF's CF/PF/AF/ZF/SF in AH and OF in bit zero. Reserved bits are zero.
inline constexpr unsigned NativeArithmeticFlagsMask = 0xd501;
} // namespace llvm::X86

#endif // LLVM_IR_X86NATIVEARITHMETICFLAGS_H
