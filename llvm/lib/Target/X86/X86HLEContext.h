//===- X86HLEContext.h - HLE operation lowering -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_X86_X86HLECONTEXT_H
#define LLVM_LIB_TARGET_X86_X86HLECONTEXT_H

#include "X86.h"
#include "X86Subtarget.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/IR/X86HleState.h"
#include "llvm/Support/ErrorHandling.h"

namespace llvm::X86 {

inline SDValue lowerHLEContext(SDValue Op, const X86Subtarget &Subtarget,
                               SelectionDAG &DAG) {
  if (!Subtarget.is64Bit())
    report_fatal_error("HLE context requires 64-bit mode");
  const auto *Control = dyn_cast<ConstantSDNode>(Op.getOperand(5));
  const auto *Bytes = dyn_cast<ConstantSDNode>(Op.getOperand(6));
  if (!Control || !Bytes ||
      !X86Hle::validControl(Control->getZExtValue(), Bytes->getZExtValue()))
    report_fatal_error("invalid HLE operation or width");
  const SDLoc DL(Op);
  SDValue Chain =
      DAG.getCopyToReg(Op.getOperand(0), DL, R9, Op.getOperand(2), SDValue());
  Chain = DAG.getCopyToReg(Chain, DL, R10, Op.getOperand(3), Chain.getValue(1));
  Chain = DAG.getCopyToReg(Chain, DL, R11, Op.getOperand(4), Chain.getValue(1));
  return SDValue(
      DAG.getMachineNode(
          HLE_CONTEXT64, DL, MVT::Other,
          {DAG.getTargetConstant(Control->getZExtValue(), DL, MVT::i32),
           DAG.getTargetConstant(Bytes->getZExtValue(), DL, MVT::i32), Chain,
           Chain.getValue(1)}),
      0);
}

} // namespace llvm::X86

#endif // LLVM_LIB_TARGET_X86_X86HLECONTEXT_H
