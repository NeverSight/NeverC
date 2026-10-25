//===- X86CompressExpandNative.h - Native compact memory ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_X86COMPRESSEXPANDNATIVE_H
#define LLVM_IR_X86COMPRESSEXPANDNATIVE_H

namespace llvm::X86 {
// Architectural opcode family, independent of the fixed i32 vector carrier.
enum class CompressExpandKind : unsigned { Byte, Word, Dword, Qword, PS, PD };
inline constexpr unsigned CompressExpandKindCount = 6;
inline constexpr unsigned compressExpandElementBytes(unsigned Kind) {
  constexpr unsigned Bytes[] = {1, 2, 4, 8, 4, 8};
  return Kind < CompressExpandKindCount ? Bytes[Kind] : 0;
}
} // namespace llvm::X86

#endif // LLVM_IR_X86COMPRESSEXPANDNATIVE_H
