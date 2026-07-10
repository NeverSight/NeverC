//===- IndVarSimplify.h - Induction Variable Simplification -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides the interface for the Induction Variable
// Simplification pass.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_INDVARSIMPLIFY_H
#define LLVM_TRANSFORMS_SCALAR_INDVARSIMPLIFY_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

class Function;
class Loop;
class LPMUpdater;

class IndVarSimplifyPass : public PassInfoMixin<IndVarSimplifyPass> {
  /// Perform IV widening during the pass.
  bool WidenIndVars;

  /// Disable IV widening when a function initially contains more loops than
  /// this limit. Zero keeps upstream's unlimited behavior.
  unsigned WidenMaxFunctionLoops;

  /// Pin the loop-density decision for the lifetime of this pass instance.
  /// Earlier loops may be deleted or unrolled before later loops are visited.
  DenseMap<const Function *, bool> FunctionWideningDecisions;

public:
  IndVarSimplifyPass(bool WidenIndVars = true,
                     unsigned WidenMaxFunctionLoops = 0)
      : WidenIndVars(WidenIndVars),
        WidenMaxFunctionLoops(WidenMaxFunctionLoops) {}
  PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                        LoopStandardAnalysisResults &AR, LPMUpdater &U);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_INDVARSIMPLIFY_H
