//===- X86NativeDivision.h - Full-dividend native division ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_X86_X86NATIVEDIVISION_H
#define LLVM_LIB_TARGET_X86_X86NATIVEDIVISION_H

#include "X86.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/IR/IntrinsicsX86.h"
#include "llvm/Support/ErrorHandling.h"

namespace llvm::X86 {

inline SDValue lowerNativeDivision(SDValue Op, bool Is16Bit, bool Is64Bit,
                                   SelectionDAG &DAG) {
  const MVT VT = Op.getSimpleValueType();
  if (Is16Bit || (VT == MVT::i64 && !Is64Bit))
    report_fatal_error(
        "native division intrinsic does not match target code mode");
  unsigned Opcode, LowReg, HighReg;
  switch (Op.getConstantOperandVal(1)) {
#define DIVISION(Name, Inst, Low, High)                                        \
  case Intrinsic::x86_##Name:                                                  \
    Opcode = Inst;                                                             \
    LowReg = Low;                                                              \
    HighReg = High;                                                            \
    break;
    DIVISION(udivrem8, DIV8r, AX, AX)
    DIVISION(sdivrem8, IDIV8r, AX, AX)
    DIVISION(udivrem16, DIV16r, AX, DX)
    DIVISION(sdivrem16, IDIV16r, AX, DX)
    DIVISION(udivrem32, DIV32r, EAX, EDX)
    DIVISION(sdivrem32, IDIV32r, EAX, EDX)
    DIVISION(udivrem64, DIV64r, RAX, RDX)
    DIVISION(sdivrem64, IDIV64r, RAX, RDX)
#undef DIVISION
  default:
    llvm_unreachable("unexpected native division intrinsic");
  }
  const SDLoc DL(Op);
  SDValue Low = Op.getOperand(2), High = Op.getOperand(3);
  if (VT == MVT::i8) {
    Low = DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i16, Low);
    High = DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i16, High);
    High = DAG.getNode(ISD::SHL, DL, MVT::i16, High,
                       DAG.getConstant(8, DL, MVT::i8));
    Low = DAG.getNode(ISD::OR, DL, MVT::i16, Low, High);
  }
  SDValue Chain =
      DAG.getCopyToReg(Op.getOperand(0), DL, LowReg, Low, SDValue());
  if (VT != MVT::i8)
    Chain = DAG.getCopyToReg(Chain, DL, HighReg, High, Chain.getValue(1));
  auto *Divide =
      DAG.getMachineNode(Opcode, DL, DAG.getVTList(MVT::Other, MVT::Glue),
                         {Op.getOperand(4), Chain, Chain.getValue(1)});
  Low = DAG.getCopyFromReg(SDValue(Divide, 0), DL, LowReg,
                           VT == MVT::i8 ? MVT::i16 : VT, SDValue(Divide, 1));
  Chain = Low.getValue(1);
  if (VT == MVT::i8) {
    // Copy complete AX, avoiding an AH copy that register allocation could
    // combine with a REX-only destination. Both bytes belong to one result.
    High = DAG.getNode(ISD::SRL, DL, MVT::i16, Low,
                       DAG.getConstant(8, DL, MVT::i8));
    High = DAG.getNode(ISD::TRUNCATE, DL, VT, High);
    Low = DAG.getNode(ISD::TRUNCATE, DL, VT, Low);
  } else {
    High = DAG.getCopyFromReg(Chain, DL, HighReg, VT, Low.getValue(2));
    Chain = High.getValue(1);
  }
  return DAG.getMergeValues({Low, High, Chain}, DL);
}

} // namespace llvm::X86

#endif // LLVM_LIB_TARGET_X86_X86NATIVEDIVISION_H
