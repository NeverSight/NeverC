#ifndef NEVERC_FOUNDATION_STACK_H
#define NEVERC_FOUNDATION_STACK_H

#include <cstddef>

#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Compiler.h"

namespace neverc {
constexpr size_t DesiredStackSize = 8 << 20;

void noteBottomOfStack();

bool isStackNearlyExhausted();

void runWithSufficientStackSpaceSlow(llvm::function_ref<void()> Diag,
                                     llvm::function_ref<void()> Fn);

inline void runWithSufficientStackSpace(llvm::function_ref<void()> Diag,
                                        llvm::function_ref<void()> Fn) {
#if LLVM_ENABLE_THREADS
  if (LLVM_UNLIKELY(isStackNearlyExhausted()))
    runWithSufficientStackSpaceSlow(Diag, Fn);
  else
    Fn();
#else
  if (LLVM_UNLIKELY(isStackNearlyExhausted()))
    Diag();
  Fn();
#endif
}
} // end namespace neverc

#endif // NEVERC_FOUNDATION_STACK_H
