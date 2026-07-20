//===- MCEmissionObserver.h - Per-context MC emission hooks ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCEMISSIONOBSERVER_H
#define LLVM_MC_MCEMISSIONOBSERVER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/Error.h"
#include <memory>

namespace llvm {

class MCAsmLayout;
class MCAssembler;
class MCCodeEmitter;
class MCContext;
class MCExpr;
class MCFixup;
class MCFragment;
class MCSection;
class MCStreamer;
class MCSubtargetInfo;

enum class MCEmissionEventKind {
  UnitBegin,
  UnitEnd,
  SectionChange,
  PreInstruction,
  PostInstruction,
  PostEncode,
  Fixup,
  RelaxationRound,
  PreLayout,
  PostLayout,
  PreObjectWrite,
};

/// A non-owning, per-MCContext customization seam for observing MC emission
/// and replacing an instruction immediately before it reaches a streamer.
///
/// The notify methods enforce a callback gate: recursively emitting through
/// the same observer is rejected rather than recursively re-entering plugin
/// code. Implementations override the protected on* methods only.
class MCEmissionObserver {
public:
  MCEmissionObserver() = default;
  MCEmissionObserver(const MCEmissionObserver &) = delete;
  MCEmissionObserver &operator=(const MCEmissionObserver &) = delete;
  virtual ~MCEmissionObserver();

  Error notifyUnitBegin(MCStreamer &Streamer);
  Error notifyUnitEnd(MCStreamer &Streamer);
  Error notifySectionChange(MCStreamer &Streamer, const MCSection &Section,
                            const MCExpr *Subsection);
  Expected<MCInst> notifyPreInstruction(MCStreamer &Streamer,
                                        const MCInst &Instruction,
                                        const MCSubtargetInfo &STI);
  Error notifyPostInstruction(MCStreamer &Streamer,
                              const MCInst &Instruction,
                              const MCSubtargetInfo &STI);
  Error notifyPostEncode(MCContext &Context, const MCInst &Instruction,
                         StringRef Bytes, ArrayRef<MCFixup> Fixups);
  Error notifyFixup(MCAssembler &Assembler, const MCAsmLayout &Layout,
                    const MCFragment &Fragment, const MCFixup &Fixup,
                    uint64_t Value, bool IsResolved);
  Error notifyRelaxationRound(MCAssembler &Assembler,
                              const MCAsmLayout &Layout, unsigned Round,
                              bool Changed);
  Error notifyPreLayout(MCAssembler &Assembler);
  Error notifyPostLayout(MCAssembler &Assembler,
                         const MCAsmLayout &Layout);
  Error notifyPreObjectWrite(MCAssembler &Assembler,
                             const MCAsmLayout &Layout);

protected:
  virtual Error onUnitBegin(MCStreamer &Streamer);
  virtual Error onUnitEnd(MCStreamer &Streamer);
  virtual Error onSectionChange(MCStreamer &Streamer,
                                const MCSection &Section,
                                const MCExpr *Subsection);
  virtual Expected<MCInst>
  onPreInstruction(MCStreamer &Streamer, const MCInst &Instruction,
                   const MCSubtargetInfo &STI);
  virtual Error onPostInstruction(MCStreamer &Streamer,
                                  const MCInst &Instruction,
                                  const MCSubtargetInfo &STI);
  virtual Error onPostEncode(MCContext &Context, const MCInst &Instruction,
                             StringRef Bytes, ArrayRef<MCFixup> Fixups);
  virtual Error onFixup(MCAssembler &Assembler,
                        const MCAsmLayout &Layout,
                        const MCFragment &Fragment, const MCFixup &Fixup,
                        uint64_t Value, bool IsResolved);
  virtual Error onRelaxationRound(MCAssembler &Assembler,
                                  const MCAsmLayout &Layout, unsigned Round,
                                  bool Changed);
  virtual Error onPreLayout(MCAssembler &Assembler);
  virtual Error onPostLayout(MCAssembler &Assembler,
                             const MCAsmLayout &Layout);
  virtual Error onPreObjectWrite(MCAssembler &Assembler,
                                 const MCAsmLayout &Layout);

private:
  Error enterCallback();
  void leaveCallback();

  bool InCallback = false;
};

/// Wraps an emitter so successful encodes are reported to the observer stored
/// in \p Context. Returns \p Fallback unchanged when no observer is installed.
std::unique_ptr<MCCodeEmitter>
createMCEmissionCodeEmitter(MCContext &Context,
                            std::unique_ptr<MCCodeEmitter> Fallback);

} // end namespace llvm

#endif // LLVM_MC_MCEMISSIONOBSERVER_H
