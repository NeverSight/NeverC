//===- X86SMMContextEmitter.cpp - Native RSM context transport -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/X86MCTargetDesc.h"
#include "X86AsmPrinter.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/MC/MCInstBuilder.h"
#include "llvm/MC/MCStreamer.h"

using namespace llvm;

bool X86AsmPrinter::emitSMMContext(const MachineInstr *MI) {
  if (MI->getOpcode() != X86::RSM_CONTEXT64)
    return false;
  SMShadowTracker.emitShadowPadding(*OutStreamer, getSubtargetInfo());
  const bool AllowPadding = OutStreamer->getAllowAutoPadding();
  OutStreamer->setAllowAutoPadding(false);
  EmitAndCountInstruction(MCInstBuilder(X86::MOV64rr)
                              .addReg(X86::R10)
                              .addReg(MI->getOperand(0).getReg()));
  auto Load = [&](unsigned To, unsigned Offset, bool Wide = true) {
    EmitAndCountInstruction(MCInstBuilder(Wide ? X86::MOV64rm : X86::MOV32rm)
                                .addReg(To)
                                .addReg(X86::R10)
                                .addImm(1)
                                .addReg(0)
                                .addImm(Offset)
                                .addReg(0));
  };
  // Restore these before XRSTOR, so no compiler or context-memory access
  // follows restoration of PKRU or any other enabled user xstate component.
  Load(X86::RAX, 16);
  EmitAndCountInstruction(
      MCInstBuilder(X86::MOV64cr).addReg(X86::CR2).addReg(X86::RAX));
  Load(X86::RAX, 24);
  EmitAndCountInstruction(
      MCInstBuilder(X86::MOV64cr).addReg(X86::CR8).addReg(X86::RAX));
  Load(X86::EAX, 32, false);
  Load(X86::EDX, 36, false);
  EmitAndCountInstruction(
      MCInstBuilder(X86::MOV32ri).addReg(X86::ECX).addImm(0xc0000102));
  EmitAndCountInstruction(MCInstBuilder(X86::WRMSR));
  Load(X86::R11, 0);
  Load(X86::EAX, 8, false);
  Load(X86::EDX, 12, false);
  EmitAndCountInstruction(MCInstBuilder(X86::XRSTOR64)
                              .addReg(X86::R11)
                              .addImm(1)
                              .addReg(0)
                              .addImm(0)
                              .addReg(0));
  EmitAndCountInstruction(MCInstBuilder(X86::RSM));
  OutStreamer->setAllowAutoPadding(AllowPadding);
  return true;
}
