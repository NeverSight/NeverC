//===- Transforms/Instrumentation.h - Instrumentation passes ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines shared helpers for PGO indirect call promotion.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_H

#include <cassert>
#include <cstdint>
#include <limits>

namespace llvm {

class CallBase;
class Function;
class OptimizationRemarkEmitter;

namespace pgo {

// Helper function that transforms CB (either an indirect-call instruction, or
// an invoke instruction), into a conditional call to F. Used by PGO-driven
// indirect call promotion and sample profile passes.
//
// TotalCount is the profile count value that the instruction executes; Count
// is the profile count value that F is the target function. These two values
// are used to update the branch weight. When AttachProfToDirectCall is true a
// !prof metadata is attached to the new direct call to contain Count.
CallBase &promoteIndirectCall(CallBase &CB, Function *F, uint64_t Count,
                              uint64_t TotalCount, bool AttachProfToDirectCall,
                              OptimizationRemarkEmitter *ORE);

} // namespace pgo

/// Calculate what to divide by to scale counts.
///
/// Given the maximum count, calculate a divisor that will scale all the
/// weights to strictly less than std::numeric_limits<uint32_t>::max().
static inline uint64_t calculateCountScale(uint64_t MaxCount) {
  return MaxCount < std::numeric_limits<uint32_t>::max()
             ? 1
             : MaxCount / std::numeric_limits<uint32_t>::max() + 1;
}

/// Scale an individual branch count.
///
/// Scale a 64-bit weight down to 32-bits using \c Scale.
static inline uint32_t scaleBranchCount(uint64_t Count, uint64_t Scale) {
  uint64_t Scaled = Count / Scale;
  assert(Scaled <= std::numeric_limits<uint32_t>::max() && "overflow 32-bits");
  return Scaled;
}

} // end namespace llvm

#endif // LLVM_TRANSFORMS_INSTRUMENTATION_H
