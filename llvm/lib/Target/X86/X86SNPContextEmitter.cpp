//===- X86SNPContextEmitter.cpp - Native SNP register transport ---------===//
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
#include "llvm/IR/X86SnpState.h"
#include "llvm/MC/MCInstBuilder.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

bool X86AsmPrinter::emitSNPContext(const MachineInstr *MI) {
  if (MI->getOpcode() != X86::SNP_CONTEXT64)
    return false;
  const auto Kind = static_cast<X86Snp::Operation>(MI->getOperand(0).getImm());
  const auto Control = MI->getOperand(1).getImm();
  if (!X86Snp::validControl(Kind, Control))
    report_fatal_error("invalid SNP context pseudo");
  // SNP-capable AMD processors provide long-mode LAHF/SAHF. Capturing flags
  // without PUSHF avoids touching a live red zone or creating an unwind gap.
  SMShadowTracker.emitShadowPadding(*OutStreamer, getSubtargetInfo());
  const bool AllowPadding = OutStreamer->getAllowAutoPadding();
  OutStreamer->setAllowAutoPadding(false);
  auto Memory = [&](unsigned Reg, unsigned Base, unsigned Offset, bool Store) {
    MCInstBuilder Inst(Store ? X86::MOV64mr : X86::MOV64rm);
    if (!Store)
      Inst.addReg(Reg);
    Inst.addReg(Base).addImm(1).addReg(0).addImm(Offset).addReg(0);
    if (Store)
      Inst.addReg(Reg);
    EmitAndCountInstruction(Inst);
  };
  const unsigned Registers[] = {X86::RAX, X86::RCX, X86::RDX};
  for (unsigned I = 0; I != 3; ++I)
    if (X86Snp::inputRegisters(Kind) & (1U << I))
      Memory(Registers[I], X86::R10, I * 8, false);
  unsigned Segment = 0;
  switch (Control >> X86Snp::SegmentShift) {
  case 0x26:
    Segment = X86::ES_PREFIX;
    break;
  case 0x2e:
    Segment = X86::CS_PREFIX;
    break;
  case 0x36:
    Segment = X86::SS_PREFIX;
    break;
  case 0x3e:
    Segment = X86::DS_PREFIX;
    break;
  case 0x64:
    Segment = X86::FS_PREFIX;
    break;
  case 0x65:
    Segment = X86::GS_PREFIX;
    break;
  }
  if (Segment)
    EmitAndCountInstruction(MCInstBuilder(Segment));
  if (Control & X86Snp::Address32)
    EmitAndCountInstruction(MCInstBuilder(X86::ADDR32_PREFIX));
  const unsigned Opcodes[] = {X86::PSMASH,    X86::PVALIDATE64, X86::RMPADJUST,
                              X86::RMPUPDATE, X86::RMPQUERY,    X86::RMPREAD,
                              X86::RMPOPT,    X86::RMPCHKD};
  EmitAndCountInstruction(MCInstBuilder(Opcodes[static_cast<unsigned>(Kind)]));
  for (unsigned I = 0; I != 3; ++I)
    if (X86Snp::outputRegisters(Kind) & (1U << I))
      Memory(Registers[I], X86::R10, I * 8, true);
  // MOV stores leave the result flags intact. Snapshot AH and OF before any
  // flag-changing instruction; all GPR results are already published.
  EmitAndCountInstruction(MCInstBuilder(X86::LAHF));
  EmitAndCountInstruction(
      MCInstBuilder(X86::SETCCr).addReg(X86::CL).addImm(X86::COND_O));
  EmitAndCountInstruction(
      MCInstBuilder(X86::MOVZX32rr8_NOREX).addReg(X86::EAX).addReg(X86::AH));
  EmitAndCountInstruction(
      MCInstBuilder(X86::MOVZX32rr8).addReg(X86::ECX).addReg(X86::CL));
  EmitAndCountInstruction(
      MCInstBuilder(X86::SHL32ri).addReg(X86::ECX).addReg(X86::ECX).addImm(11));
  EmitAndCountInstruction(MCInstBuilder(X86::OR32rr)
                              .addReg(X86::EAX)
                              .addReg(X86::EAX)
                              .addReg(X86::ECX));
  const unsigned Mask = X86Snp::outputFlags(Kind);
  EmitAndCountInstruction(MCInstBuilder(X86::AND32ri)
                              .addReg(X86::EAX)
                              .addReg(X86::EAX)
                              .addImm(Mask));
  Memory(X86::RDX, X86::R11, 0, false);
  EmitAndCountInstruction(MCInstBuilder(X86::AND64ri32)
                              .addReg(X86::RDX)
                              .addReg(X86::RDX)
                              .addImm(~int64_t(Mask)));
  EmitAndCountInstruction(MCInstBuilder(X86::OR64rr)
                              .addReg(X86::RDX)
                              .addReg(X86::RDX)
                              .addReg(X86::RAX));
  Memory(X86::RDX, X86::R11, 0, true);
  OutStreamer->setAllowAutoPadding(AllowPadding);
  return true;
}
