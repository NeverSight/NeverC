//===- X86HleState.h - Native HLE operation contract ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_X86HLESTATE_H
#define LLVM_IR_X86HLESTATE_H

#include <cstdint>

namespace llvm::X86Hle {

enum class Operation : uint8_t {
  Add,
  Adc,
  And,
  Btc,
  Btr,
  Bts,
  Cmpxchg,
  Cmpxchg8b,
  Dec,
  Inc,
  Neg,
  Not,
  Or,
  Sbb,
  Sub,
  Xor,
  Xadd,
  Xchg,
  Mov
};

// GPRs: four unaligned u64 slots in RAX/RCX/RDX/RBX order. Flags: one
// unaligned u64. Memory, both objects and the compiler frame must be disjoint.
// RCX supplies the explicit source/index; CMPXCHG uses RAX and CMPXCHG8B
// uses all four implicit operands. Only actual architectural outputs change.
constexpr unsigned GprBytes = 32;
constexpr unsigned FlagsBytes = 8;
constexpr unsigned Acquire = 0x100;
// PAUSE before publishing outputs forces any HLE transaction to retry the
// original acquire without elision. This is a native abort, not a synthetic
// success or dropped hint. Requires entry outside RTM and any external HLE.
constexpr unsigned AbortBeforePublication = 0x200;
constexpr unsigned OperationMask = 0xff;
constexpr unsigned ArithmeticFlags = 0x8d5;

constexpr Operation operation(unsigned Control) {
  return static_cast<Operation>(Control & OperationMask);
}

constexpr bool validControl(unsigned Control, unsigned Bytes) {
  const auto Op = operation(Control);
  if ((Control & ~0x3ffU) ||
      static_cast<unsigned>(Op) > static_cast<unsigned>(Operation::Mov) ||
      (Bytes != 1 && Bytes != 2 && Bytes != 4 && Bytes != 8))
    return false;
  if (Op == Operation::Mov && (Control & Acquire))
    return false;
  if (Op == Operation::Cmpxchg8b)
    return Bytes == 8;
  return Bytes != 1 ||
         (Op != Operation::Btc && Op != Operation::Btr && Op != Operation::Bts);
}

constexpr unsigned inputRegisters(Operation Op) {
  switch (Op) {
  case Operation::Cmpxchg8b:
    return 15;
  case Operation::Cmpxchg:
    return 3;
  case Operation::Dec:
  case Operation::Inc:
  case Operation::Neg:
  case Operation::Not:
    return 0;
  default:
    return 2;
  }
}

constexpr unsigned outputRegisters(Operation Op) {
  switch (Op) {
  case Operation::Cmpxchg8b:
    return 5;
  case Operation::Cmpxchg:
    return 1;
  case Operation::Xadd:
  case Operation::Xchg:
    return 2;
  default:
    return 0;
  }
}

constexpr unsigned outputFlags(Operation Op) {
  switch (Op) {
  case Operation::Cmpxchg8b:
    return 0x40;
  case Operation::Dec:
  case Operation::Inc:
    return ArithmeticFlags & ~1U;
  case Operation::Not:
  case Operation::Xchg:
  case Operation::Mov:
    return 0;
  // Bit operations preserve ZF; OF/SF/AF/PF retain the actual CPU's undefined
  // values. Logical operations likewise publish the CPU's undefined AF.
  case Operation::Btc:
  case Operation::Btr:
  case Operation::Bts:
    return ArithmeticFlags & ~0x40U;
  default:
    return ArithmeticFlags;
  }
}

} // namespace llvm::X86Hle

#endif // LLVM_IR_X86HLESTATE_H
