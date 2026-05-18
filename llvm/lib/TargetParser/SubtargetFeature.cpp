//===- SubtargetFeature.cpp - CPU characteristics Implementation ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file Implements the SubtargetFeature interface.
//
//===----------------------------------------------------------------------===//

#include "llvm/TargetParser/SubtargetFeature.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <string>
#include <vector>

using namespace llvm;

/// Splits a string of comma separated items in to a vector of strings.
void SubtargetFeatures::Split(std::vector<std::string> &V, StringRef S) {
  SmallVector<StringRef, 3> Tmp;
  S.split(Tmp, ',', -1, false /* KeepEmpty */);
  V.reserve(Tmp.size());
  for (StringRef T : Tmp)
    V.push_back(std::string(T));
}

void SubtargetFeatures::AddFeature(StringRef String, bool Enable) {
  if (!String.empty()) {
    auto Low = String.lower();
    std::string LowStr(Low.data(), Low.size());
    Features.push_back(hasFlag(String) ? LowStr
                                       : (Enable ? "+" : "-") + LowStr);
  }
}

void SubtargetFeatures::addFeaturesVector(
    const ArrayRef<std::string> OtherFeatures) {
  Features.insert(Features.cend(), OtherFeatures.begin(), OtherFeatures.end());
}

SubtargetFeatures::SubtargetFeatures(StringRef Initial) {
  // Break up string into separate features
  Split(Features, Initial);
}

std::string SubtargetFeatures::getString() const {
  return join(Features.begin(), Features.end(), ",");
}

void SubtargetFeatures::print(raw_ostream &OS) const {
  for (const auto &F : Features)
    OS << F << " ";
  OS << "\n";
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void SubtargetFeatures::dump() const { print(dbgs()); }
#endif

void SubtargetFeatures::getDefaultSubtargetFeatures(const Triple &Triple) {}
