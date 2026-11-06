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
  // The pruned backend emits code64 only. Linux i386 source takes the checked
  // full-LLVM route, which also owns the immediate far-transfer MC records.
  if (!Subtarget.is64Bit())
    report_fatal_error("legacy far transfers require the full LLVM backend");
  unsigned Opcode, Bits;
  bool Immediate, Call;
  switch (Op.getConstantOperandVal(1)) {
  default:
    llvm_unreachable("unexpected native far-transfer intrinsic");
#define X86_FAR_OPCODE_false(Native) Opcode = X86::Native
#define X86_FAR_OPCODE_true(Native)                                            \
  report_fatal_error("immediate far transfers require a legacy target and "    \
                     "the full LLVM backend")
#define X86_FAR_TRANSFER(Name, Native, Width, Imm, Returns)                    \
  case Intrinsic::x86_##Name:                                                  \
    X86_FAR_OPCODE_##Imm(Native);                                              \
    Bits = Width;                                                              \
    Immediate = Imm;                                                           \
    Call = Returns;                                                            \
    break;
#include "X86NativeFarTransfers.def"
#undef X86_FAR_TRANSFER
#undef X86_FAR_OPCODE_true
#undef X86_FAR_OPCODE_false
  }
  const bool Wide = Subtarget.is64Bit();
  if ((Bits == 64 && !Wide) || (Immediate && Wide))
    report_fatal_error(
        "native far-transfer intrinsic does not match the target code mode");
  const SDLoc DL(Op);
  // Native memory operands use the data-layout address class (GR32 for x32).
  // Terminal carriers instead use architectural-width addresses after widening.
  const MVT PtrVT = Call && !Immediate ? Op.getOperand(2).getSimpleValueType()
                    : Wide             ? MVT::i64
                                       : MVT::i32;
  SmallVector<SDValue, 8> Operands;
  if (Immediate) {
    for (unsigned Index : {2u, 3u}) {
      const auto *Value = dyn_cast<ConstantSDNode>(Op.getOperand(Index));
      if (!Value)
        report_fatal_error(
            "far-transfer offset and selector must be immediate");
      Operands.push_back(DAG.getTargetConstant(
          Value->getZExtValue(), DL, Index == 2 ? MVT::i32 : MVT::i16));
    }
  } else {
    Operands.push_back(DAG.getZExtOrTrunc(Op.getOperand(2), DL, PtrVT));
  }
  if (!Call) {
    const SDValue Stack =
        DAG.getZExtOrTrunc(Op.getOperand(Immediate ? 4 : 3), DL, PtrVT);
    Operands.push_back(Stack);
    Operands.push_back(DAG.getTargetConstant(Bits, DL, MVT::i32));
    Operands.push_back(Op.getOperand(0));
    const unsigned Pseudo = Immediate ? X86::NATIVE_FAR_JUMP_IMM32
                            : Wide    ? X86::NATIVE_FAR_JUMP64
                                      : X86::NATIVE_FAR_JUMP32;
    return SDValue(DAG.getMachineNode(Pseudo, DL, MVT::Other, Operands), 0);
  }

  // A zero-argument far entry obeys the platform C register-preservation
  // contract and returns AX/EAX/RAX with RETF, restoring the incoming CS/SS/SP.
  // Register masks, call sequences and shadow space must match a real call;
  // merely emitting an effectful instruction leaves live values unprotected.
  MachineFunction &MF = DAG.getMachineFunction();
  const unsigned ShadowBytes =
      Subtarget.isCallingConvWin64(CallingConv::C) ? 32 : 0;
  SDValue Chain = DAG.getCALLSEQ_START(Op.getOperand(0), ShadowBytes, 0, DL);
  if (!Immediate) {
    Operands.append(
        {DAG.getTargetConstant(1, DL, MVT::i8), DAG.getRegister(0, PtrVT),
         DAG.getTargetConstant(0, DL, MVT::i32), DAG.getRegister(0, MVT::i16)});
  }
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
