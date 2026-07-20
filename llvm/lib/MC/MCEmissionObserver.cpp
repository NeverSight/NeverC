//===- MCEmissionObserver.cpp - Per-context MC emission hooks ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCEmissionObserver.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCFixup.h"

using namespace llvm;

MCEmissionObserver::~MCEmissionObserver() = default;

Error MCEmissionObserver::enterCallback() {
  if (InCallback)
    return createStringError(inconvertibleErrorCode(),
                             "recursive MC emission callback");
  InCallback = true;
  return Error::success();
}

void MCEmissionObserver::leaveCallback() { InCallback = false; }

Error MCEmissionObserver::notifyUnitBegin(MCStreamer &Streamer) {
  if (Error E = enterCallback())
    return E;
  auto Leave = make_scope_exit([&] { leaveCallback(); });
  return onUnitBegin(Streamer);
}

Error MCEmissionObserver::notifyUnitEnd(MCStreamer &Streamer) {
  if (Error E = enterCallback())
    return E;
  auto Leave = make_scope_exit([&] { leaveCallback(); });
  return onUnitEnd(Streamer);
}

Error MCEmissionObserver::notifySectionChange(MCStreamer &Streamer,
                                              const MCSection &Section,
                                              const MCExpr *Subsection) {
  if (Error E = enterCallback())
    return E;
  auto Leave = make_scope_exit([&] { leaveCallback(); });
  return onSectionChange(Streamer, Section, Subsection);
}

Expected<MCInst>
MCEmissionObserver::notifyPreInstruction(MCStreamer &Streamer,
                                         const MCInst &Instruction,
                                         const MCSubtargetInfo &STI) {
  if (Error E = enterCallback())
    return std::move(E);
  auto Leave = make_scope_exit([&] { leaveCallback(); });
  return onPreInstruction(Streamer, Instruction, STI);
}

Error MCEmissionObserver::notifyPostInstruction(
    MCStreamer &Streamer, const MCInst &Instruction,
    const MCSubtargetInfo &STI) {
  if (Error E = enterCallback())
    return E;
  auto Leave = make_scope_exit([&] { leaveCallback(); });
  return onPostInstruction(Streamer, Instruction, STI);
}

Error MCEmissionObserver::notifyPostEncode(MCContext &Context,
                                           const MCInst &Instruction,
                                           StringRef Bytes,
                                           ArrayRef<MCFixup> Fixups) {
  if (Error E = enterCallback())
    return E;
  auto Leave = make_scope_exit([&] { leaveCallback(); });
  return onPostEncode(Context, Instruction, Bytes, Fixups);
}

Error MCEmissionObserver::notifyFixup(
    MCAssembler &Assembler, const MCAsmLayout &Layout,
    const MCFragment &Fragment, const MCFixup &Fixup, uint64_t Value,
    bool IsResolved) {
  if (Error E = enterCallback())
    return E;
  auto Leave = make_scope_exit([&] { leaveCallback(); });
  return onFixup(Assembler, Layout, Fragment, Fixup, Value, IsResolved);
}

Error MCEmissionObserver::notifyRelaxationRound(
    MCAssembler &Assembler, const MCAsmLayout &Layout, unsigned Round,
    bool Changed) {
  if (Error E = enterCallback())
    return E;
  auto Leave = make_scope_exit([&] { leaveCallback(); });
  return onRelaxationRound(Assembler, Layout, Round, Changed);
}

Error MCEmissionObserver::notifyPreLayout(MCAssembler &Assembler) {
  if (Error E = enterCallback())
    return E;
  auto Leave = make_scope_exit([&] { leaveCallback(); });
  return onPreLayout(Assembler);
}

Error MCEmissionObserver::notifyPostLayout(MCAssembler &Assembler,
                                           const MCAsmLayout &Layout) {
  if (Error E = enterCallback())
    return E;
  auto Leave = make_scope_exit([&] { leaveCallback(); });
  return onPostLayout(Assembler, Layout);
}

Error MCEmissionObserver::notifyPreObjectWrite(
    MCAssembler &Assembler, const MCAsmLayout &Layout) {
  if (Error E = enterCallback())
    return E;
  auto Leave = make_scope_exit([&] { leaveCallback(); });
  return onPreObjectWrite(Assembler, Layout);
}

Error MCEmissionObserver::onUnitBegin(MCStreamer &) {
  return Error::success();
}

Error MCEmissionObserver::onUnitEnd(MCStreamer &) {
  return Error::success();
}

Error MCEmissionObserver::onSectionChange(MCStreamer &, const MCSection &,
                                          const MCExpr *) {
  return Error::success();
}

Expected<MCInst>
MCEmissionObserver::onPreInstruction(MCStreamer &, const MCInst &Instruction,
                                     const MCSubtargetInfo &) {
  return Instruction;
}

Error MCEmissionObserver::onPostInstruction(MCStreamer &, const MCInst &,
                                            const MCSubtargetInfo &) {
  return Error::success();
}

Error MCEmissionObserver::onPostEncode(MCContext &, const MCInst &, StringRef,
                                       ArrayRef<MCFixup>) {
  return Error::success();
}

Error MCEmissionObserver::onFixup(MCAssembler &, const MCAsmLayout &,
                                  const MCFragment &, const MCFixup &,
                                  uint64_t, bool) {
  return Error::success();
}

Error MCEmissionObserver::onRelaxationRound(MCAssembler &,
                                            const MCAsmLayout &, unsigned,
                                            bool) {
  return Error::success();
}

Error MCEmissionObserver::onPreLayout(MCAssembler &) {
  return Error::success();
}

Error MCEmissionObserver::onPostLayout(MCAssembler &, const MCAsmLayout &) {
  return Error::success();
}

Error MCEmissionObserver::onPreObjectWrite(MCAssembler &,
                                           const MCAsmLayout &) {
  return Error::success();
}

namespace {

class ObservingCodeEmitter final : public MCCodeEmitter {
public:
  ObservingCodeEmitter(MCContext &ContextValue,
                       std::unique_ptr<MCCodeEmitter> FallbackValue)
      : Context(ContextValue), Fallback(std::move(FallbackValue)) {}

  void reset() override { Fallback->reset(); }

  void emitPrefix(const MCInst &Instruction, SmallVectorImpl<char> &Buffer,
                  const MCSubtargetInfo &STI) const override {
    Fallback->emitPrefix(Instruction, Buffer, STI);
  }

  void encodeInstruction(const MCInst &Instruction,
                         SmallVectorImpl<char> &Buffer,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const override {
    const size_t ByteStart = Buffer.size();
    const size_t FixupStart = Fixups.size();
    Fallback->encodeInstruction(Instruction, Buffer, Fixups, STI);
    MCEmissionObserver *Observer = Context.getEmissionObserver();
    if (!Observer)
      return;
    StringRef Bytes(Buffer.data() + ByteStart, Buffer.size() - ByteStart);
    ArrayRef<MCFixup> AddedFixups(Fixups.data() + FixupStart,
                                  Fixups.size() - FixupStart);
    if (Error E = Observer->notifyPostEncode(
            Context, Instruction, Bytes, AddedFixups))
      Context.reportError(Instruction.getLoc(), toString(std::move(E)));
  }

private:
  MCContext &Context;
  std::unique_ptr<MCCodeEmitter> Fallback;
};

} // namespace

std::unique_ptr<MCCodeEmitter>
llvm::createMCEmissionCodeEmitter(
    MCContext &Context, std::unique_ptr<MCCodeEmitter> Fallback) {
  if (!Fallback || !Context.getEmissionObserver())
    return Fallback;
  return std::make_unique<ObservingCodeEmitter>(Context,
                                                std::move(Fallback));
}
