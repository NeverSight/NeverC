//===-- WinException.h - Windows Exception Handling ----------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains support for writing windows exception info into asm files.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_CODEGEN_ASMPRINTER_WIN64EXCEPTION_H
#define LLVM_LIB_CODEGEN_ASMPRINTER_WIN64EXCEPTION_H

#include "EHStreamer.h"
#include <vector>

namespace llvm {
class GlobalValue;
class MachineFunction;
class MCExpr;
class MCSection;
struct WinEHFuncInfo;

class LLVM_LIBRARY_VISIBILITY WinException : public EHStreamer {
  /// Per-function flag to indicate if personality info should be emitted.
  bool shouldEmitPersonality = false;

  /// Per-function flag to indicate if the LSDA should be emitted.
  bool shouldEmitLSDA = false;

  /// Per-function flag to indicate if frame moves info should be emitted.
  bool shouldEmitMoves = false;

  /// True if this is a 64-bit target and we should use image relative offsets.
  bool useImageRel32 = false;

  bool isAArch64 = false;
  bool isX86 = false;

  /// Pointer to the current funclet entry BB.
  const MachineBasicBlock *CurrentFuncletEntry = nullptr;

  /// The section of the last funclet start.
  MCSection *CurrentFuncletTextSection = nullptr;

  /// The list of symbols to add to the ehcont section
  std::vector<const MCSymbol *> EHContTargets;

  void emitCSpecificHandlerTable(const MachineFunction *MF);

  void emitSEHActionsForRange(const WinEHFuncInfo &FuncInfo,
                              const MCSymbol *BeginLabel,
                              const MCSymbol *EndLabel, int State);

  const MCExpr *create32bitRef(const MCSymbol *Value);
  const MCExpr *create32bitRef(const GlobalValue *GV);
  const MCExpr *getLabel(const MCSymbol *Label);
  const MCExpr *getLabelPlusOne(const MCSymbol *Label);
  const MCExpr *getOffset(const MCSymbol *OffsetOf, const MCSymbol *OffsetFrom);
  const MCExpr *getOffsetPlusOne(const MCSymbol *OffsetOf,
                                 const MCSymbol *OffsetFrom);

  void endFuncletImpl();

public:
  //===--------------------------------------------------------------------===//
  // Main entry points.
  //
  WinException(AsmPrinter *A);
  ~WinException() override;

  /// Emit all exception information that should come after the content.
  void endModule() override;

  /// Gather pre-function exception information.  Assumes being emitted
  /// immediately after the function entry point.
  void beginFunction(const MachineFunction *MF) override;

  void markFunctionEnd() override;

  /// Gather and emit post-function exception information.
  void endFunction(const MachineFunction *) override;

  /// Emit target-specific EH funclet machinery.
  void beginFunclet(const MachineBasicBlock &MBB, MCSymbol *Sym) override;
  void endFunclet() override;
};
} // namespace llvm

#endif
