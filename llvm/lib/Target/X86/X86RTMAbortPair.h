//===- X86RTMAbortPair.h - Indivisible native RTM abort ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_X86_X86RTMABORTPAIR_H
#define LLVM_LIB_TARGET_X86_X86RTMABORTPAIR_H

#include "X86.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/Support/ErrorHandling.h"

namespace llvm::X86 {

inline SDValue lowerRTMAbortPair(SDValue Op, bool Is16Bit, SelectionDAG &DAG) {
  if (Is16Bit)
    report_fatal_error("RTM abort pair requires 32-bit or 64-bit code mode");
  const auto *Code = dyn_cast<ConstantSDNode>(Op.getOperand(2));
  if (!Code || Code->getZExtValue() > 255)
    report_fatal_error("RTM abort pair requires an imm8 abort code");
  const SDLoc DL(Op);
  auto *Pair = DAG.getMachineNode(
      RTM_ABORT_PAIR, DL, DAG.getVTList(MVT::Other, MVT::Glue),
      {DAG.getTargetConstant(Code->getZExtValue(), DL, MVT::i32),
       Op.getOperand(0)});
  const auto Result =
      DAG.getCopyFromReg(SDValue(Pair, 0), DL, EAX, MVT::i32, SDValue(Pair, 1));
  return DAG.getMergeValues({Result, Result.getValue(1)}, DL);
}

} // namespace llvm::X86

#endif // LLVM_LIB_TARGET_X86_X86RTMABORTPAIR_H
