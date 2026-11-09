//===- X86KVMHypercall.h - Explicit KVM register ABI ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_X86_X86KVMHYPERCALL_H
#define LLVM_LIB_TARGET_X86_X86KVMHYPERCALL_H

#include "X86.h"
#include "X86Subtarget.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/IR/IntrinsicsX86.h"
#include "llvm/Support/ErrorHandling.h"

namespace llvm::X86 {

// This is the KVM ABI, not an inferred ABI for arbitrary VMCALL/VMMCALL.
// The environment must preserve registers other than the rAX result and
// return normally. Calls that change other state need a different contract.
inline SDValue lowerKVMHypercall(SDValue Op, const X86Subtarget &Subtarget,
                                 SelectionDAG &DAG) {
  const unsigned ID = Op.getConstantOperandVal(1);
  const bool AMD = ID == Intrinsic::x86_vmmcall_kvm;
  if (!Subtarget.is64Bit())
    report_fatal_error("KVM hypercall intrinsic requires 64-bit mode");
  const unsigned Regs[] = {RAX, RBX, RCX, RDX, RSI};
  const SDLoc DL(Op);
  SDValue Chain = Op.getOperand(0), Glue;
  for (unsigned I = 0; I != 5; ++I) {
    Chain = DAG.getCopyToReg(Chain, DL, Regs[I], Op.getOperand(I + 2), Glue);
    Glue = Chain.getValue(1);
  }
  const unsigned Opcode = AMD ? VMMCALL_KVM64 : VMCALL_KVM64;
  auto *Call = DAG.getMachineNode(
      Opcode, DL, DAG.getVTList(MVT::Other, MVT::Glue), {Chain, Glue});
  const auto Result = DAG.getCopyFromReg(SDValue(Call, 0), DL, Regs[0],
                                         MVT::i64, SDValue(Call, 1));
  return DAG.getMergeValues({Result, Result.getValue(1)}, DL);
}

} // namespace llvm::X86

#endif // LLVM_LIB_TARGET_X86_X86KVMHYPERCALL_H
