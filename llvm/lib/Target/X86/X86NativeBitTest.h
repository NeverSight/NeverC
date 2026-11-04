//===- X86NativeBitTest.h - Native memory bit strings ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_X86_X86NATIVEBITTEST_H
#define LLVM_LIB_TARGET_X86_X86NATIVEBITTEST_H

#include "MCTargetDesc/X86BaseInfo.h"
#include "X86.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/IR/IntrinsicsX86.h"
#include "llvm/Support/ErrorHandling.h"

namespace llvm::X86 {

inline SDValue lowerNativeBitTest(SDValue Op, bool Is64Bit, SelectionDAG &DAG) {
  const MVT IndexVT = Op.getOperand(3).getSimpleValueType();
  if (IndexVT == MVT::i64 && !Is64Bit)
    report_fatal_error(
        "native bit test intrinsic does not match target code mode");
  unsigned Opcode;
  switch (Op.getConstantOperandVal(1)) {
#define BIT_TEST(Name, Inst)                                                   \
  case Intrinsic::x86_##Name:                                                  \
    Opcode = Inst;                                                             \
    break;
    BIT_TEST(bt_mem16, BT16mr)
    BIT_TEST(bt_mem32, BT32mr)
    BIT_TEST(bt_mem64, BT64mr)
    BIT_TEST(bts_mem16, BTS16mr)
    BIT_TEST(bts_mem32, BTS32mr)
    BIT_TEST(bts_mem64, BTS64mr)
    BIT_TEST(btr_mem16, BTR16mr)
    BIT_TEST(btr_mem32, BTR32mr)
    BIT_TEST(btr_mem64, BTR64mr)
    BIT_TEST(btc_mem16, BTC16mr)
    BIT_TEST(btc_mem32, BTC32mr)
    BIT_TEST(btc_mem64, BTC64mr)
#undef BIT_TEST
  default:
    llvm_unreachable("unexpected native bit test intrinsic");
  }
  const bool Write = Opcode != BT16mr && Opcode != BT32mr && Opcode != BT64mr;
  const SDLoc DL(Op);
  const MVT PtrVT = Op.getOperand(2).getSimpleValueType();
  auto *Access = DAG.getMachineNode(
      Opcode, DL, DAG.getVTList(MVT::Other, MVT::Glue),
      {Op.getOperand(2), DAG.getTargetConstant(1, DL, MVT::i8),
       DAG.getRegister(0, PtrVT), DAG.getTargetConstant(0, DL, MVT::i32),
       DAG.getRegister(0, MVT::i16), Op.getOperand(3), Op.getOperand(0)});
  // A signed register index can access words before or after the bit base.
  // Do not give alias analysis a fixed-width access at the pointer itself.
  auto *MMO = DAG.getMachineFunction().getMachineMemOperand(
      MachinePointerInfo(),
      MachineMemOperand::MOVolatile | MachineMemOperand::MOLoad |
          (Write ? MachineMemOperand::MOStore : MachineMemOperand::MONone),
      LLT(),
      Align(1)); // This pruned LLVM represents unknown size by invalid LLT.
  DAG.setNodeMemRefs(Access, {MMO});
  auto *Carry = DAG.getMachineNode(
      SETCCr, DL, MVT::i8,
      {DAG.getTargetConstant(COND_B, DL, MVT::i8), SDValue(Access, 1)});
  return DAG.getMergeValues({SDValue(Carry, 0), SDValue(Access, 0)}, DL);
}

} // namespace llvm::X86

#endif // LLVM_LIB_TARGET_X86_X86NATIVEBITTEST_H
