//===- Transforms/Instrumentation/PGOInstrumentation.h ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file provides the interface for IR based instrumentation passes.
///
/// NeverC keeps only the indirect-call-promotion pass; the historical
/// profile-instr-gen / profile-instr-use / memop-size / create-var classes
/// had no implementation in this build (the PGOInstrumentation.cpp source
/// is deleted) and no callers outside these stale declarations.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_PGOINSTRUMENTATION_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_PGOINSTRUMENTATION_H

#include "llvm/IR/PassManager.h"
#include "llvm/Support/CommandLine.h"

namespace llvm {

extern cl::opt<bool> DebugInfoCorrelate;

class Module;

/// The indirect function call promotion pass.
class PGOIndirectCallPromotion
    : public PassInfoMixin<PGOIndirectCallPromotion> {
public:
  PGOIndirectCallPromotion(bool IsInLTO = false, bool SamplePGO = false)
      : InLTO(IsInLTO), SamplePGO(SamplePGO) {}

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);

private:
  bool InLTO;
  bool SamplePGO;
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_INSTRUMENTATION_PGOINSTRUMENTATION_H
