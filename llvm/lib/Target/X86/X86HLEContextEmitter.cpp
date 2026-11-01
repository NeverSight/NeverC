//===- X86HLEContextEmitter.cpp - Native HLE operation transport --------===//
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
#include "llvm/IR/X86HleState.h"
#include "llvm/MC/MCInstBuilder.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

bool X86AsmPrinter::emitHLEContext(const MachineInstr *MI) {
  if (MI->getOpcode() != X86::HLE_CONTEXT64)
    return false;
  const unsigned Control = MI->getOperand(0).getImm();
  const unsigned Bytes = MI->getOperand(1).getImm();
  if (!X86Hle::validControl(Control, Bytes))
    report_fatal_error("invalid HLE context pseudo");
  const auto Kind = X86Hle::operation(Control);
  using Operation = X86Hle::Operation;
  const unsigned Width = Bytes == 1 ? 0 : Bytes == 2 ? 1 : Bytes == 4 ? 2 : 3;
  unsigned Opcode = 0;
  bool Unary = false;
  bool Exchange = false;
  switch (Kind) {
#define BINARY(Op, Stem, Suffix)                                               \
  case Operation::Op: {                                                        \
    const unsigned Opcodes[] = {X86::Stem##8##Suffix, X86::Stem##16##Suffix,   \
                                X86::Stem##32##Suffix, X86::Stem##64##Suffix}; \
    Opcode = Opcodes[Width];                                                   \
    break;                                                                     \
  }
    BINARY(Add, ADD, mr)
    BINARY(Adc, ADC, mr)
    BINARY(And, AND, mr)
    BINARY(Cmpxchg, CMPXCHG, rm)
    BINARY(Or, OR, mr)
    BINARY(Sbb, SBB, mr)
    BINARY(Sub, SUB, mr)
    BINARY(Xor, XOR, mr)
    BINARY(Xadd, XADD, rm)
    BINARY(Xchg, XCHG, rm)
    BINARY(Mov, MOV, mr)
#undef BINARY
#define UNARY(Op, Stem)                                                        \
  case Operation::Op: {                                                        \
    const unsigned Opcodes[] = {X86::Stem##8m, X86::Stem##16m, X86::Stem##32m, \
                                X86::Stem##64m};                               \
    Opcode = Opcodes[Width];                                                   \
    Unary = true;                                                              \
    break;                                                                     \
  }
    UNARY(Dec, DEC)
    UNARY(Inc, INC)
    UNARY(Neg, NEG)
    UNARY(Not, NOT)
#undef UNARY
#define BIT(Op, Stem)                                                          \
  case Operation::Op: {                                                        \
    const unsigned Opcodes[] = {0, X86::Stem##16mr, X86::Stem##32mr,           \
                                X86::Stem##64mr};                              \
    Opcode = Opcodes[Width];                                                   \
    break;                                                                     \
  }
    BIT(Btc, BTC)
    BIT(Btr, BTR)
    BIT(Bts, BTS)
#undef BIT
  case Operation::Cmpxchg8b:
    Opcode = X86::CMPXCHG8B;
    Unary = true;
    break;
  }
  Exchange = Kind == Operation::Xadd || Kind == Operation::Xchg;
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
  const unsigned Registers[] = {X86::RAX, X86::RCX, X86::RDX, X86::RBX};
  for (unsigned I = 0; I != 4; ++I)
    if (X86Hle::inputRegisters(Kind) & (1U << I))
      Memory(Registers[I], X86::R10, I * 8, false);
  if (Kind == Operation::Adc || Kind == Operation::Sbb) {
    Memory(X86::RDX, X86::R11, 0, false);
    EmitAndCountInstruction(
        MCInstBuilder(X86::BT64ri8).addReg(X86::RDX).addImm(0));
  }
  EmitAndCountInstruction(MCInstBuilder(
      Control & X86Hle::Acquire ? X86::XACQUIRE_PREFIX : X86::XRELEASE_PREFIX));
  if (Kind != Operation::Mov)
    EmitAndCountInstruction(MCInstBuilder(X86::LOCK_PREFIX));
  const unsigned SourceRegisters[] = {X86::CL, X86::CX, X86::ECX, X86::RCX};
  MCInstBuilder Inst(Opcode);
  if (Exchange)
    Inst.addReg(SourceRegisters[Width]).addReg(SourceRegisters[Width]);
  Inst.addReg(X86::R9).addImm(1).addReg(0).addImm(0).addReg(0);
  if (!Unary && !Exchange)
    Inst.addReg(SourceRegisters[Width]);
  EmitAndCountInstruction(Inst);
  // Intel SDM Vol. 1 §17.3.9.1 guarantees PAUSE aborts an active transaction.
  // On an HLE abort the CPU restarts the acquire itself without elision. The
  // checkpoint precedes every publication below, including GPR outputs.
  if (Control & X86Hle::AbortBeforePublication)
    EmitAndCountInstruction(MCInstBuilder(X86::PAUSE));
  for (unsigned I = 0; I != 4; ++I)
    if (X86Hle::outputRegisters(Kind) & (1U << I))
      Memory(Registers[I], X86::R10, I * 8, true);
  const unsigned Mask = X86Hle::outputFlags(Kind);
  if (Mask) {
    EmitAndCountInstruction(MCInstBuilder(X86::LAHF));
    EmitAndCountInstruction(
        MCInstBuilder(X86::SETCCr).addReg(X86::CL).addImm(X86::COND_O));
    EmitAndCountInstruction(
        MCInstBuilder(X86::MOVZX32rr8_NOREX).addReg(X86::EAX).addReg(X86::AH));
    EmitAndCountInstruction(
        MCInstBuilder(X86::MOVZX32rr8).addReg(X86::ECX).addReg(X86::CL));
    EmitAndCountInstruction(MCInstBuilder(X86::SHL32ri)
                                .addReg(X86::ECX)
                                .addReg(X86::ECX)
                                .addImm(11));
    EmitAndCountInstruction(MCInstBuilder(X86::OR32rr)
                                .addReg(X86::EAX)
                                .addReg(X86::EAX)
                                .addReg(X86::ECX));
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
  }
  OutStreamer->setAllowAutoPadding(AllowPadding);
  return true;
}
