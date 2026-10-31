//===- X86HyperVContext.h - Explicit Hyper-V page ABI -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_X86_X86HYPERVCONTEXT_H
#define LLVM_LIB_TARGET_X86_X86HYPERVCONTEXT_H

#include "X86.h"
#include "X86Subtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/Support/ErrorHandling.h"

namespace llvm::X86 {

inline SDValue lowerHyperVCall(SDValue Op, const X86Subtarget &Subtarget,
                               SelectionDAG &DAG) {
  if (!Subtarget.is64Bit())
    report_fatal_error("Hyper-V context requires 64-bit mode");
  const auto *Stride = dyn_cast<ConstantSDNode>(Op.getOperand(5));
  if (!Stride || (Stride->getZExtValue() != 16 &&
                  Stride->getZExtValue() != 32 && Stride->getZExtValue() != 64))
    report_fatal_error("Hyper-V XMM stride must be 16, 32 or 64");
  // The pseudo contains a real CALL/RET pair. Frame lowering must reserve an
  // aligned call frame and must not put live locals in the SysV red zone.
  auto &Frame = DAG.getMachineFunction().getFrameInfo();
  Frame.setHasCalls(true);
  Frame.setAdjustsStack(true);
  const SDLoc DL(Op);
  SDValue Chain = Op.getOperand(0), Glue;
  const unsigned Registers[] = {R9, R10, R11};
  for (unsigned I = 0; I != 3; ++I) {
    Chain =
        DAG.getCopyToReg(Chain, DL, Registers[I], Op.getOperand(I + 2), Glue);
    Glue = Chain.getValue(1);
  }
  return SDValue(DAG.getMachineNode(HYPERV_CALL64, DL, MVT::Other,
                                    {DAG.getTargetConstant(
                                         Stride->getZExtValue(), DL, MVT::i32),
                                     Chain, Glue}),
                 0);
}

} // namespace llvm::X86

#endif // LLVM_LIB_TARGET_X86_X86HYPERVCONTEXT_H
