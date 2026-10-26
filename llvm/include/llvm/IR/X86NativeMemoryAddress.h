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

// The ordinary-SIB native address APIs accept NativeMemoryStackReference.
#define LLVM_X86_NATIVE_MEMORY_STACK_REFERENCE 1

namespace llvm::X86 {

// OR this bit with a segment-prefix byte to request a stack-default memory
// operand in 64-bit addressing. An SS prefix alone does not change a
// data-default operand's noncanonical fault. Stride/MIB address APIs do not
// accept this control; their index has instruction-specific semantics.
inline constexpr uint32_t NativeMemoryStackReference = 0x100;

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

inline constexpr bool isNativeMemoryAddressControl(bool Is64Bit, uint64_t Width,
                                                   uint64_t Control) {
  const uint64_t Prefix = Control & ~uint64_t(NativeMemoryStackReference);
  if (!isNativeMemoryAddressWidth(Is64Bit, Width) ||
      !isNativeMemorySegmentPrefix(Prefix))
    return false;
  return !(Control & NativeMemoryStackReference) ||
         (Is64Bit && Width == 64 && Prefix != 0x64 && Prefix != 0x65);
}

} // namespace llvm::X86

#endif // LLVM_IR_X86NATIVEMEMORYADDRESS_H
