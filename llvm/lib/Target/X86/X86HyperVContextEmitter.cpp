//===- X86HyperVContextEmitter.cpp - Native Hyper-V register transport
//------===//
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
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

bool X86AsmPrinter::emitHyperVContext(const MachineInstr *MI) {
  if (MI->getOpcode() != X86::HYPERV_CALL64)
    return false;
  const auto Stride = MI->getOperand(0).getImm();
  if (Stride != 16 && Stride != 32 && Stride != 64)
    report_fatal_error("invalid Hyper-V XMM stride");
  SMShadowTracker.emitShadowPadding(*OutStreamer, getSubtargetInfo());
  const bool AllowPadding = OutStreamer->getAllowAutoPadding();
  OutStreamer->setAllowAutoPadding(false);
  auto Memory = [&](unsigned Opcode, unsigned Reg, unsigned Base,
                    unsigned Offset, bool Store) {
    MCInstBuilder Inst(Opcode);
    if (!Store)
      Inst.addReg(Reg);
    Inst.addReg(Base).addImm(1).addReg(0).addImm(Offset).addReg(0);
    if (Store)
      Inst.addReg(Reg);
    EmitAndCountInstruction(Inst);
  };
  const unsigned GPRs[] = {X86::RAX, X86::RCX, X86::RDX, X86::R8};
  const unsigned Offsets[] = {0, 8, 16, 64};
  const unsigned Vectors[] = {X86::XMM0, X86::XMM1, X86::XMM2,
                              X86::XMM3, X86::XMM4, X86::XMM5};
  for (unsigned I = 1; I != 4; ++I)
    Memory(X86::MOV64rm, GPRs[I], X86::R10, Offsets[I], false);
  for (unsigned I = 0; I != 6; ++I)
    Memory(X86::MOVUPSrm, Vectors[I], X86::R11, I * Stride, false);
  // Use the registered hypercall page, including its continuation mechanism.
  // The bound returning TLFS ABI preserves R9/R10/R11 and the stack. Services
  // which replace caller state (e.g. SetVpRegisters on self) are not admitted.
  EmitAndCountInstruction(MCInstBuilder(X86::CALL64r).addReg(X86::R9));
  for (unsigned I = 0; I != 4; ++I)
    Memory(X86::MOV64mr, GPRs[I], X86::R10, Offsets[I], true);
  for (unsigned I = 0; I != 6; ++I)
    Memory(X86::MOVUPSmr, Vectors[I], X86::R11, I * Stride, true);
  OutStreamer->setAllowAutoPadding(AllowPadding);
  return true;
}
