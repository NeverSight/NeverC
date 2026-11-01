//===- X86SGXTransferContextEmitter.cpp - Native SGX transfer context
//-------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/X86BaseInfo.h"
#include "MCTargetDesc/X86MCTargetDesc.h"
#include "X86AsmPrinter.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInstBuilder.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Support/Alignment.h"

using namespace llvm;

// Code64 specialization of the full fork; NeverC prunes legacy opcodes.
bool X86AsmPrinter::emitSGXTransferContext(const MachineInstr *MI) {
  const bool At = MI->getOpcode() == X86::ENCLU_TRANSFER_AT_CONTEXT64;
  if (!At && MI->getOpcode() != X86::ENCLU_TRANSFER_CONTEXT64)
    return false;
  SMShadowTracker.emitShadowPadding(*OutStreamer, getSubtargetInfo());
  const bool AllowPadding = OutStreamer->getAllowAutoPadding();
  OutStreamer->setAllowAutoPadding(false);
  const unsigned AX = X86::RAX;
  const unsigned SI = X86::RSI;
  const unsigned DI = X86::RDI;
  const unsigned Word = 8, Count = 16;
  auto Move = [&](unsigned To, unsigned From) {
    EmitAndCountInstruction(
        MCInstBuilder(X86::MOV64rr).addReg(To).addReg(From));
  };
  auto Memory = [&](unsigned Opcode, unsigned Base, unsigned Offset) {
    return MCInstBuilder(Opcode)
        .addReg(Base)
        .addImm(1)
        .addReg(0)
        .addImm(Offset)
        .addReg(0);
  };
  auto Load = [&](unsigned To, unsigned Base, unsigned Offset,
                  bool Full = true) {
    EmitAndCountInstruction(MCInstBuilder(Full ? X86::MOV64rm : X86::MOV32rm)
                                .addReg(To)
                                .addReg(Base)
                                .addImm(1)
                                .addReg(0)
                                .addImm(Offset)
                                .addReg(0));
  };
  Move(DI, MI->getOperand(0).getReg());
  Load(X86::EAX, DI, 0, false);
  // Returning leaves must never execute under this terminal contract. Check
  // the natural input EAX before changing xstate or borrowing the target SP.
  EmitAndCountInstruction(
      MCInstBuilder(X86::SUB32ri8).addReg(X86::EAX).addReg(X86::EAX).addImm(2));
  EmitAndCountInstruction(
      MCInstBuilder(X86::CMP32ri8).addReg(X86::EAX).addImm(2));
  auto *Valid = OutContext.createTempSymbol();
  EmitAndCountInstruction(
      MCInstBuilder(X86::JCC_1)
          .addExpr(MCSymbolRefExpr::create(Valid, OutContext))
          .addImm(X86::COND_BE));
  EmitAndCountInstruction(MCInstBuilder(X86::TRAP));
  OutStreamer->emitLabel(Valid);

  Load(SI, DI, (Count + 1) * Word);
  EmitAndCountInstruction(
      MCInstBuilder(X86::MOV32ri).addReg(X86::EAX).addImm(7));
  EmitAndCountInstruction(
      MCInstBuilder(X86::MOV32ri).addReg(X86::EDX).addImm(0));
  EmitAndCountInstruction(Memory(X86::XRSTOR64, SI, 0));
  Move(AX, DI);
  EmitAndCountInstruction(Memory(X86::PUSH64rmm, AX, Count * Word));
  EmitAndCountInstruction(MCInstBuilder(X86::POPF64));
  const unsigned Registers64[] = {X86::RAX, X86::RCX, X86::RDX, X86::RBX,
                                  X86::RSP, X86::RBP, X86::RSI, X86::RDI,
                                  X86::R8,  X86::R9,  X86::R10, X86::R11,
                                  X86::R12, X86::R13, X86::R14, X86::R15};
  const auto *Registers = Registers64;
  for (unsigned I = 1; I != Count; ++I)
    if (I != 4)
      Load(Registers[I], AX, I * Word);
  Load(Registers[4], AX, 4 * Word);
  // Preserve RAX's upper half too: EEXIT does not redefine that register.
  Load(AX, AX, 0);
  if (At) {
    // A code-local literal keeps every restored GPR and flag intact. Executing
    // the immutable original ENCLU also preserves its next-PC output in RCX.
    auto *Target = OutContext.createTempSymbol();
    EmitAndCountInstruction(
        MCInstBuilder(X86::JMP64m)
            .addReg(X86::RIP)
            .addImm(1)
            .addReg(0)
            .addExpr(MCSymbolRefExpr::create(Target, OutContext))
            .addReg(0));
    // AC may already be set at CPL3. The indirect jump reads a natural word,
    // so the literal must remain aligned when CR0.AM enables #AC checks.
    OutStreamer->emitValueToAlignment(Align(Word));
    OutStreamer->emitLabel(Target);
    OutStreamer->emitIntValue(MI->getOperand(1).getImm(), Word);
  } else {
    EmitAndCountInstruction(MCInstBuilder(X86::ENCLU));
    // There is no C continuation. A native EENTER return address is observable
    // in RCX; deliberately trap if a caller's enclave protocol returns here.
    EmitAndCountInstruction(MCInstBuilder(X86::TRAP));
  }
  OutStreamer->setAllowAutoPadding(AllowPadding);
  return true;
}
