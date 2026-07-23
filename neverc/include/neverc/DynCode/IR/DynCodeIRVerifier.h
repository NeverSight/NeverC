#ifndef NEVERC_DYNCODE_DYNCODEIRVERIFIER_H
#define NEVERC_DYNCODE_DYNCODEIRVERIFIER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/IR/PassManager.h"

namespace neverc {
namespace dyncode {

// The sealed dyncode IR final-verify gate (phase
// dyncode.ir.final_verify).  Checks that no data-carrying global survived
// stackify, that no BlockAddress / global-pointer initializer remains, and
// clears the internal hard-error bookkeeping.  In the dyncode phase graph this
// phase is a SEALED_HOST_GATE: plugins cannot provide/intercept/skip it, and it
// still runs even when every upstream IR transform was fully replaced.
struct DynCodeIRVerifier : public llvm::PassInfoMixin<DynCodeIRVerifier> {
  DynCodeIRVerifier() = default;

  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &MAM);
  static llvm::StringRef name() { return "DynCodeIRVerifier"; }
};

} // namespace dyncode
} // namespace neverc

#endif // NEVERC_DYNCODE_DYNCODEIRVERIFIER_H
