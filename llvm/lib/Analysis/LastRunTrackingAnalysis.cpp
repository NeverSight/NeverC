//===- LastRunTrackingAnalysis.cpp - Avoid running redundant passes
//--------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/LastRunTrackingAnalysis.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/CommandLine.h"

#define DEBUG_TYPE "last-run-tracking"

STATISTIC(NumSkipped, "Number of passes skipped by LastRunTracking");
STATISTIC(NumQueries, "Number of LastRunTracking queries");

using namespace llvm;

static cl::opt<bool> DisableLastRunTracking(
    "disable-last-run-tracking", cl::Hidden, cl::init(false),
    cl::desc("Disable last-run tracking to skip redundant passes"));

AnalysisKey LastRunTrackingAnalysis::Key;

bool LastRunTrackingInfo::shouldSkipImpl(PassID ID, OptionPtr Ptr) const {
  ++NumQueries;
  if (DisableLastRunTracking)
    return false;
  if (!Ptr) {
    if (SimpleTracked.contains(ID)) {
      ++NumSkipped;
      return true;
    }
    return false;
  }
  auto It = OptionsTracked.find(ID);
  if (It == OptionsTracked.end())
    return false;
  bool Skip = It->second(Ptr);
  if (Skip)
    ++NumSkipped;
  return Skip;
}

void LastRunTrackingInfo::updateImpl(PassID ID, bool Changed,
                                     CompatibilityCheckFn CheckFn) {
  if (DisableLastRunTracking)
    return;
  if (Changed) {
    SimpleTracked.clear();
    OptionsTracked.clear();
  }
  OptionsTracked[ID] = std::move(CheckFn);
}

void LastRunTrackingInfo::updateImpl(PassID ID, bool Changed) {
  if (DisableLastRunTracking)
    return;
  if (Changed) {
    SimpleTracked.clear();
    OptionsTracked.clear();
  }
  SimpleTracked.insert(ID);
}
