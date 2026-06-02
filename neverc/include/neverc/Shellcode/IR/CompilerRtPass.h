#ifndef NEVERC_SHELLCODE_COMPILERRTPASS_H
#define NEVERC_SHELLCODE_COMPILERRTPASS_H

#include "neverc/Shellcode/Pipeline/TargetDesc.h"
#include "llvm/IR/PassManager.h"

namespace neverc {
namespace shellcode {

/// Trivial analysis whose sole purpose is to act as a "CompilerRtPass already
/// ran" cache key.  When any subsequent pass returns PreservedAnalyses::none(),
/// the analysis manager automatically invalidates this result, signalling that
/// the next CompilerRtPass invocation must do real work.
class CompilerRtStampAnalysis
    : public llvm::AnalysisInfoMixin<CompilerRtStampAnalysis> {
  friend llvm::AnalysisInfoMixin<CompilerRtStampAnalysis>;
  static llvm::AnalysisKey Key;

public:
  struct Result {
    bool invalidate(llvm::Module &, const llvm::PreservedAnalyses &PA,
                    llvm::ModuleAnalysisManager::Invalidator &) {
      return !PA.getChecker<CompilerRtStampAnalysis>().preserved();
    }
  };
  Result run(llvm::Module &, llvm::ModuleAnalysisManager &) { return {}; }
};

class CompilerRtPass : public llvm::PassInfoMixin<CompilerRtPass> {
public:
  CompilerRtPass() = default;
  explicit CompilerRtPass(const TargetDesc &Target) : Target(Target) {}
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &);

private:
  TargetDesc Target;
};

} // namespace shellcode
} // namespace neverc

#endif
