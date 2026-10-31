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
  const bool Wide =
      ID == Intrinsic::x86_vmcall_kvm || ID == Intrinsic::x86_vmmcall_kvm;
  const bool AMD =
      ID == Intrinsic::x86_vmmcall_kvm || ID == Intrinsic::x86_vmmcall_kvm32;
  // NeverC's pruned native backend admits only 64-bit X86 code generation.
  if (!Subtarget.is64Bit() || !Wide)
    report_fatal_error(
        "KVM hypercall intrinsic does not match target code mode");
  const unsigned Regs64[] = {RAX, RBX, RCX, RDX, RSI};
  const unsigned Regs32[] = {EAX, EBX, ECX, EDX, ESI};
  const auto *Regs = Wide ? Regs64 : Regs32;
  const SDLoc DL(Op);
  SDValue Chain = Op.getOperand(0), Glue;
  for (unsigned I = 0; I != 5; ++I) {
    Chain = DAG.getCopyToReg(Chain, DL, Regs[I], Op.getOperand(I + 2), Glue);
    Glue = Chain.getValue(1);
  }
  const unsigned Opcode = AMD ? (Wide ? VMMCALL_KVM64 : VMMCALL_KVM32)
                              : (Wide ? VMCALL_KVM64 : VMCALL_KVM32);
  auto *Call = DAG.getMachineNode(
      Opcode, DL, DAG.getVTList(MVT::Other, MVT::Glue), {Chain, Glue});
  const auto Result =
      DAG.getCopyFromReg(SDValue(Call, 0), DL, Regs[0],
                         Wide ? MVT::i64 : MVT::i32, SDValue(Call, 1));
  return DAG.getMergeValues({Result, Result.getValue(1)}, DL);
}

} // namespace llvm::X86

#endif // LLVM_LIB_TARGET_X86_X86KVMHYPERCALL_H
