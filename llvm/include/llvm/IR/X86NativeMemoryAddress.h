//===- X86NativeMemoryAddress.h - Native addresses ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_X86NATIVEMEMORYADDRESS_H
#define LLVM_IR_X86NATIVEMEMORYADDRESS_H

#include <cstdint>

namespace llvm::X86 {

// The address is an unsigned offset before segment-base addition. A zero
// segment prefix selects the native DS default; other values are actual
// architectural override bytes, not LLVM pointer address-space numbers.
inline constexpr bool isNativeMemoryAddressWidth(bool Is64Bit, uint64_t Width) {
  return Is64Bit ? Width == 32 || Width == 64 : Width == 16 || Width == 32;
}

inline constexpr bool isNativeMemorySegmentPrefix(uint64_t Prefix) {
  switch (Prefix) {
  case 0:
  case 0x26: // ES
  case 0x2e: // CS
  case 0x36: // SS
  case 0x3e: // DS
  case 0x64: // FS
  case 0x65: // GS
    return true;
  default:
    return false;
  }
}

} // namespace llvm::X86

#endif // LLVM_IR_X86NATIVEMEMORYADDRESS_H
