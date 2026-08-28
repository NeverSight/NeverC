//===- NevercInlinePolicy.h - Shared NeverC inliner policy ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TRANSFORMS_IPO_NEVERCINLINEPOLICY_H
#define LLVM_LIB_TRANSFORMS_IPO_NEVERCINLINEPOLICY_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/IR/Function.h"

namespace llvm {

/// Count back edges without constructing LoopInfo or a dominator tree. NeverC
/// uses this cheap proxy only to distinguish loop-dense callers from sparse
/// ones while inlining.
inline unsigned nevercCountFunctionLoops(const Function &F) {
  if (F.isDeclaration())
    return 0;
  SmallVector<std::pair<const BasicBlock *, const BasicBlock *>, 16> BackEdges;
  FindFunctionBackedges(F, BackEdges);
  return BackEdges.size();
}

} // namespace llvm

#endif // LLVM_LIB_TRANSFORMS_IPO_NEVERCINLINEPOLICY_H
