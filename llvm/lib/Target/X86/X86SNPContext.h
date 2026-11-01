//===- X86SNPContext.h - SNP intrinsic lowering -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_X86_X86SNPCONTEXT_H
#define LLVM_LIB_TARGET_X86_X86SNPCONTEXT_H

#include "X86.h"
#include "X86Subtarget.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/IR/IntrinsicsX86.h"
#include "llvm/IR/X86SnpState.h"
#include "llvm/Support/ErrorHandling.h"

namespace llvm::X86 {

inline SDValue lowerSNPContext(SDValue Op, const X86Subtarget &Subtarget,
                               SelectionDAG &DAG) {
  if (!Subtarget.is64Bit())
    report_fatal_error("SNP context requires 64-bit mode");
  X86Snp::Operation Kind;
  switch (Op.getConstantOperandVal(1)) {
  case Intrinsic::x86_psmash_context:
    Kind = X86Snp::Operation::Psmash;
    break;
  case Intrinsic::x86_pvalidate_context:
    Kind = X86Snp::Operation::Pvalidate;
    break;
  case Intrinsic::x86_rmpadjust_context:
    Kind = X86Snp::Operation::Rmpadjust;
    break;
  case Intrinsic::x86_rmpupdate_context:
    Kind = X86Snp::Operation::Rmpupdate;
    break;
  case Intrinsic::x86_rmpquery_context:
    Kind = X86Snp::Operation::Rmpquery;
    break;
  case Intrinsic::x86_rmpread_context:
    Kind = X86Snp::Operation::Rmpread;
    break;
  case Intrinsic::x86_rmpopt_context:
    Kind = X86Snp::Operation::Rmpopt;
    break;
  case Intrinsic::x86_rmpchkd_context:
    Kind = X86Snp::Operation::Rmpchkd;
    break;
  default:
    llvm_unreachable("unexpected SNP context intrinsic");
  }
  const auto *Control = dyn_cast<ConstantSDNode>(Op.getOperand(4));
  if (!Control || !X86Snp::validControl(Kind, Control->getZExtValue()))
    report_fatal_error("invalid SNP address/segment control");
  const SDLoc DL(Op);
  SDValue Chain =
      DAG.getCopyToReg(Op.getOperand(0), DL, R10, Op.getOperand(2), SDValue());
  Chain = DAG.getCopyToReg(Chain, DL, R11, Op.getOperand(3), Chain.getValue(1));
  return SDValue(
      DAG.getMachineNode(
          SNP_CONTEXT64, DL, MVT::Other,
          {DAG.getTargetConstant(static_cast<unsigned>(Kind), DL, MVT::i32),
           DAG.getTargetConstant(Control->getZExtValue(), DL, MVT::i32), Chain,
           Chain.getValue(1)}),
      0);
}

inline SDValue lowerSEVGuestExit(SDValue Op, const X86Subtarget &Subtarget,
                                 SelectionDAG &DAG) {
  if (!Subtarget.is64Bit())
    report_fatal_error("SEV-ES VMGEXIT intrinsic requires 64-bit mode");
  const SDLoc DL(Op);
  auto Chain =
      DAG.getCopyToReg(Op.getOperand(0), DL, RAX, Op.getOperand(2), SDValue());
  return SDValue(
      DAG.getMachineNode(VMGEXIT, DL, MVT::Other, {Chain, Chain.getValue(1)}),
      0);
}

} // namespace llvm::X86

#endif // LLVM_LIB_TARGET_X86_X86SNPCONTEXT_H
