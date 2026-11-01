//===- X86SnpState.h - Explicit SNP register completion ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_X86SNPSTATE_H
#define LLVM_IR_X86SNPSTATE_H

#include <cstdint>

namespace llvm::X86Snp {

enum class Operation : uint8_t {
  Psmash,
  Pvalidate,
  Rmpadjust,
  Rmpupdate,
  Rmpquery,
  Rmpread,
  Rmpopt,
  Rmpchkd
};

// GPRs are three unaligned u64 slots in RAX/RCX/RDX order. Flags is a separate
// unaligned u64. Both objects must be disjoint from each other and from the
// instruction's translated memory, RMP state and native compiler frame.
// Only normal completion publishes outputs. Fault/restart state stays in the
// architectural registers; these objects are not an exception save area.
constexpr unsigned GprBytes = 24;
constexpr unsigned FlagsBytes = 8;
constexpr unsigned Address32 = 1;
constexpr unsigned SegmentShift = 8;
constexpr unsigned ArithmeticFlags = 0x8d5;

constexpr bool validControl(Operation Op, unsigned Control) {
  if (static_cast<unsigned>(Op) > static_cast<unsigned>(Operation::Rmpchkd) ||
      (Control & ~0xff01U) ||
      ((Control & Address32) && Op != Operation::Pvalidate))
    return false;
  switch (Control >> SegmentShift) {
  case 0:
  case 0x26:
  case 0x2e:
  case 0x36:
  case 0x3e:
  case 0x64:
  case 0x65:
    return true;
  default:
    return false;
  }
}

constexpr unsigned inputRegisters(Operation Op) {
  switch (Op) {
  case Operation::Psmash:
    return 1;
  case Operation::Pvalidate:
  case Operation::Rmpadjust:
    return 7;
  case Operation::Rmpquery:
    return 5;
  default:
    return 3;
  }
}

constexpr unsigned outputRegisters(Operation Op) {
  switch (Op) {
  case Operation::Rmpquery:
    return 7;
  case Operation::Rmpchkd:
    return 3;
  case Operation::Rmpopt:
    return 0;
  default:
    return 1;
  }
}

constexpr unsigned outputFlags(Operation Op) {
  switch (Op) {
  case Operation::Pvalidate:
  case Operation::Rmpchkd:
    return ArithmeticFlags;
  case Operation::Rmpopt:
    return 1;
  default:
    return ArithmeticFlags & ~1U;
  }
}

constexpr unsigned undefinedFlags(Operation Op) {
  return Op == Operation::Rmpchkd ? ArithmeticFlags & ~0x41U : 0;
}

} // namespace llvm::X86Snp

#endif // LLVM_IR_X86SNPSTATE_H
