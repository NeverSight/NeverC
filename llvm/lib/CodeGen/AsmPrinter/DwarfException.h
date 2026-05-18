//===-- DwarfException.h - Dwarf Exception Framework -----------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains support for writing dwarf exception info into asm files.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_CODEGEN_ASMPRINTER_DWARFEXCEPTION_H
#define LLVM_LIB_CODEGEN_ASMPRINTER_DWARFEXCEPTION_H

#include "EHStreamer.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/MC/MCDwarf.h"

namespace llvm {
class MachineFunction;

class LLVM_LIBRARY_VISIBILITY DwarfCFIException : public EHStreamer {
  /// Per-function flag to indicate if .cfi_personality should be emitted.
  bool shouldEmitPersonality = false;

  /// Per-function flag to indicate if .cfi_personality must be emitted.
  bool forceEmitPersonality = false;

  /// Per-function flag to indicate if .cfi_lsda should be emitted.
  bool shouldEmitLSDA = false;

  /// Per-function flag to indicate if frame CFI info should be emitted.
  bool shouldEmitCFI = false;

  /// Per-module flag to indicate if .cfi_section has beeen emitted.
  bool hasEmittedCFISections = false;

  /// Vector of all personality functions seen so far in the module.
  std::vector<const GlobalValue *> Personalities;

  void addPersonality(const GlobalValue *Personality);

public:
  //===--------------------------------------------------------------------===//
  // Main entry points.
  //
  DwarfCFIException(AsmPrinter *A);
  ~DwarfCFIException() override;

  /// Emit all exception information that should come after the content.
  void endModule() override;

  /// Gather pre-function exception information.  Assumes being emitted
  /// immediately after the function entry point.
  void beginFunction(const MachineFunction *MF) override;

  /// Gather and emit post-function exception information.
  void endFunction(const MachineFunction *) override;

  void beginBasicBlockSection(const MachineBasicBlock &MBB) override;
  void endBasicBlockSection(const MachineBasicBlock &MBB) override;
};

} // End of namespace llvm

#endif
