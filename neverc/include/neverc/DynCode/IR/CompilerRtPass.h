#ifndef NEVERC_DYNCODE_COMPILERRTPASS_H
#define NEVERC_DYNCODE_COMPILERRTPASS_H

#include "neverc/DynCode/Pipeline/TargetDesc.h"
#include "llvm/IR/PassManager.h"

namespace neverc {
namespace dyncode {

// Volume 6 task 7: CompilerRtPass is a plain, idempotent transform.  It used to
// carry a CompilerRtStampAnalysis cache key to skip redundant re-runs, but the
// dyncode pipeline always runs the full optimizer between its CompilerRt phases
// (compiler_rt.pre / compiler_rt.post / compiler_rt.final), so the stamp was
// always invalidated and never actually skipped.  Each run now works purely from
// the current module and is idempotent on re-entry.
class CompilerRtPass : public llvm::PassInfoMixin<CompilerRtPass> {
public:
  CompilerRtPass() = default;
  explicit CompilerRtPass(const TargetDesc &Target) : Target(Target) {}
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &);

private:
  TargetDesc Target;
};

} // namespace dyncode
} // namespace neverc

#endif
