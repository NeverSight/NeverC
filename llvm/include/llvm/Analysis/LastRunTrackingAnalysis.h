//===- LastRunTrackingAnalysis.h - Avoid running redundant pass -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Tracks which passes have already been run on each function, allowing
// idempotent passes (P(P(x)) == P(x)) to be skipped when nothing changed.
//
// Transition rules per function:
//   1. Pass P makes changes   -> S = {P}
//   2. Pass P makes no changes -> S = S + {P}
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_LASTRUNTRACKINGANALYSIS_H
#define LLVM_ANALYSIS_LASTRUNTRACKINGANALYSIS_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/IR/PassManager.h"
#include <functional>

namespace llvm {

class LastRunTrackingInfo {
public:
  using PassID = const void *;
  using OptionPtr = const void *;
  using CompatibilityCheckFn = std::function<bool(OptionPtr)>;

  template <typename OptionT>
  bool shouldSkip(PassID ID, const OptionT &Opt) const {
    return shouldSkipImpl(ID, &Opt);
  }
  bool shouldSkip(PassID ID) const { return shouldSkipImpl(ID, nullptr); }

  template <typename OptionT>
  void update(PassID ID, bool Changed, const OptionT &Opt) {
    updateImpl(ID, Changed, [Opt](OptionPtr Ptr) {
      return static_cast<const OptionT *>(Ptr)->isCompatibleWith(Opt);
    });
  }
  void update(PassID ID, bool Changed) { updateImpl(ID, Changed); }

private:
  bool shouldSkipImpl(PassID ID, OptionPtr Ptr) const;
  void updateImpl(PassID ID, bool Changed, CompatibilityCheckFn CheckFn);
  void updateImpl(PassID ID, bool Changed);

  SmallDenseSet<PassID, 16> SimpleTracked;
  SmallDenseMap<PassID, CompatibilityCheckFn, 4> OptionsTracked;
};

class LastRunTrackingAnalysis final
    : public AnalysisInfoMixin<LastRunTrackingAnalysis> {
  friend AnalysisInfoMixin<LastRunTrackingAnalysis>;
  static AnalysisKey Key;

public:
  using Result = LastRunTrackingInfo;
  LastRunTrackingInfo run(Function &F, FunctionAnalysisManager &) {
    return LastRunTrackingInfo();
  }
  LastRunTrackingInfo run(Module &M, ModuleAnalysisManager &) {
    return LastRunTrackingInfo();
  }
};

} // namespace llvm

#endif // LLVM_ANALYSIS_LASTRUNTRACKINGANALYSIS_H
