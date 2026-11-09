//===- X86NativeFarTransfer.h - Native far transfers ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_X86_X86NATIVEFARTRANSFER_H
#define LLVM_LIB_TARGET_X86_X86NATIVEFARTRANSFER_H

#include "X86Subtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/IR/IntrinsicsX86.h"

namespace llvm::X86 {

inline SDValue lowerNativeFarTransfer(SDValue Op, const X86Subtarget &Subtarget,
                                      SelectionDAG &DAG) {
  if (!Subtarget.is64Bit())
    report_fatal_error("native far transfers require a 64-bit x86 target");
  unsigned Opcode, Bits;
  bool Call;
  switch (Op.getConstantOperandVal(1)) {
  default:
    llvm_unreachable("unexpected native far-transfer intrinsic");
#define X86_FAR_TRANSFER(Name, Native, Width, Imm, Returns)                    \
  case Intrinsic::x86_##Name:                                                  \
    Opcode = X86::Native;                                                      \
    Bits = Width;                                                              \
    Call = Returns;                                                            \
    break;
#include "X86NativeFarTransfers.def"
#undef X86_FAR_TRANSFER
  }
  const SDLoc DL(Op);
  const MVT PtrVT = MVT::i64;
  SmallVector<SDValue, 8> Operands;
  Operands.push_back(DAG.getZExtOrTrunc(Op.getOperand(2), DL, PtrVT));
  if (!Call) {
    Operands.push_back(DAG.getZExtOrTrunc(Op.getOperand(3), DL, PtrVT));
    Operands.push_back(DAG.getTargetConstant(Bits, DL, MVT::i32));
    Operands.push_back(Op.getOperand(0));
    return SDValue(
        DAG.getMachineNode(X86::NATIVE_FAR_JUMP64, DL, MVT::Other, Operands),
        0);
  }

  // A zero-argument far entry obeys the platform C register-preservation
  // contract and returns AX/EAX/RAX with RETF, restoring the incoming CS/SS/SP.
  // Register masks, call sequences and shadow space must match a real call;
  // merely emitting an effectful instruction leaves live values unprotected.
  MachineFunction &MF = DAG.getMachineFunction();
  const unsigned ShadowBytes =
      Subtarget.isCallingConvWin64(CallingConv::C) ? 32 : 0;
  SDValue Chain = DAG.getCALLSEQ_START(Op.getOperand(0), ShadowBytes, 0, DL);
  Operands.append(
      {DAG.getTargetConstant(1, DL, MVT::i8), DAG.getRegister(0, PtrVT),
       DAG.getTargetConstant(0, DL, MVT::i32), DAG.getRegister(0, MVT::i16)});
  Operands.push_back(DAG.getRegisterMask(
      Subtarget.getRegisterInfo()->getCallPreservedMask(MF, CallingConv::C)));
  Operands.push_back(Chain);
  Chain =
      SDValue(DAG.getMachineNode(
                  Opcode, DL, DAG.getVTList(MVT::Other, MVT::Glue), Operands),
              0);
  Chain = DAG.getCALLSEQ_END(Chain, ShadowBytes, 0, Chain.getValue(1), DL);
  MF.getFrameInfo().setHasCalls(true);
  const unsigned ResultReg = Bits == 16   ? X86::AX
                             : Bits == 32 ? X86::EAX
                                          : X86::RAX;
  SDValue Result = DAG.getCopyFromReg(Chain, DL, ResultReg, Op.getValueType(),
                                      Chain.getValue(1));
  return DAG.getMergeValues({Result, Result.getValue(1)}, DL);
}

} // namespace llvm::X86

#endif
