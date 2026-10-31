//===- X86SVMContextEmitter.cpp - Native VMRUN context transport --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/X86MCTargetDesc.h"
#include "X86AsmPrinter.h"
#include "X86SVMContext.h"
#include "X86Subtarget.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/MC/MCInstBuilder.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

bool X86AsmPrinter::emitSVMContext(const MachineInstr *MI) {
  if (MI->getOpcode() != X86::VMRUN_CONTEXT64)
    return false;
  if (MI->getNumExplicitOperands() != 7 ||
      MI->getOperand(1).getReg() != X86::RSP ||
      MI->getOperand(2).getImm() != 1 || MI->getOperand(3).getReg() ||
      !MI->getOperand(4).isImm() || MI->getOperand(5).getReg() ||
      (MI->getOperand(6).getImm() != 32 && MI->getOperand(6).getImm() != 64))
    report_fatal_error("invalid VMRUN context frame");
  SMShadowTracker.emitShadowPadding(*OutStreamer, getSubtargetInfo());
  const bool AllowPadding = OutStreamer->getAllowAutoPadding();
  OutStreamer->setAllowAutoPadding(false);
  auto Move = [&](unsigned To, unsigned From) {
    EmitAndCountInstruction(
        MCInstBuilder(X86::MOV64rr).addReg(To).addReg(From));
  };
  auto Load = [&](unsigned To, unsigned Base, int64_t Offset,
                  bool Wide = true) {
    EmitAndCountInstruction(MCInstBuilder(Wide ? X86::MOV64rm : X86::MOV32rm)
                                .addReg(To)
                                .addReg(Base)
                                .addImm(1)
                                .addReg(0)
                                .addImm(Offset)
                                .addReg(0));
  };
  auto Store = [&](unsigned Base, int64_t Offset, unsigned From) {
    EmitAndCountInstruction(MCInstBuilder(X86::MOV64mr)
                                .addReg(Base)
                                .addImm(1)
                                .addReg(0)
                                .addImm(Offset)
                                .addReg(0)
                                .addReg(From));
  };
  auto XState = [&](unsigned Opcode, unsigned PointerSlot) {
    Load(X86::R8, X86::RCX, PointerSlot);
    Load(X86::EAX, X86::RCX, X86::SVMXStateMask, false);
    Load(X86::EDX, X86::RCX, X86::SVMXStateMask + 4, false);
    EmitAndCountInstruction(MCInstBuilder(Opcode)
                                .addReg(X86::R8)
                                .addImm(1)
                                .addReg(0)
                                .addImm(0)
                                .addReg(0));
  };
  const unsigned Registers[] = {
      X86::RBX, X86::RCX, X86::RDX, X86::RSI, X86::RDI, X86::RBP, X86::R8,
      X86::R9,  X86::R10, X86::R11, X86::R12, X86::R13, X86::R14, X86::R15};
  const int64_t Spill = MI->getOperand(4).getImm();
  Move(X86::RAX, MI->getOperand(0).getReg());
  Store(X86::RSP, Spill, X86::RAX);
  Move(X86::RCX, X86::RAX);
  XState(X86::XSAVE64, X86::SVMHostXState);
  XState(X86::XRSTOR64, X86::SVMGuestXState);
  Move(X86::RAX, X86::RCX);
  for (unsigned I = 0; I != std::size(Registers); ++I)
    Load(Registers[I], X86::RAX, X86::SVMGPRs + I * 8);
  Load(X86::RAX, X86::RAX, X86::SVMVMCB);
  if (MI->getOperand(6).getImm() == 32)
    EmitAndCountInstruction(MCInstBuilder(X86::ADDR32_PREFIX));
  EmitAndCountInstruction(MCInstBuilder(X86::VMRUN64));
  // RSP is hardware-restored by VMEXIT. Every other software-managed GPR
  // still belongs to the guest, including RBP and the former context carrier.
  Load(X86::RAX, X86::RSP, Spill);
  for (unsigned I = 0; I != std::size(Registers); ++I)
    Store(X86::RAX, X86::SVMGPRs + I * 8, Registers[I]);
  Move(X86::RCX, X86::RAX);
  XState(X86::XSAVE64, X86::SVMGuestXState);
  XState(X86::XRSTOR64, X86::SVMHostXState);
  OutStreamer->setAllowAutoPadding(AllowPadding);
  return true;
}
