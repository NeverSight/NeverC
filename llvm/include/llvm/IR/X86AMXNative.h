//===- X86AMXNative.h - Physical AMX encoding controls --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_X86AMXNATIVE_H
#define LLVM_IR_X86AMXNATIVE_H

namespace llvm::X86 {
// Private machine descriptors. Public APIs use separate constant operands.
inline constexpr unsigned AMXNativeTileCount = 8;
inline constexpr unsigned AMXNativeMemoryCount = 7;
inline constexpr unsigned AMXNativeRowCount = 6;
inline constexpr unsigned AMXNativeTileShift = 3;
inline constexpr unsigned AMXNativeAddress32 = 1 << 6;
inline constexpr unsigned AMXNativeEVEX = 1 << 7;
inline constexpr unsigned AMXNativeMemoryMask = 255;
inline constexpr unsigned AMXNativeRowMask = 63;
} // namespace llvm::X86

#endif // LLVM_IR_X86AMXNATIVE_H
