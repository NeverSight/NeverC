#ifndef NEVERC_TRANSFORMS_XORSTRCLEANUPPASS_H
#define NEVERC_TRANSFORMS_XORSTRCLEANUPPASS_H

#include "llvm/IR/PassManager.h"

namespace neverc {
namespace xorstr {

struct XorStrCleanupPass : public llvm::PassInfoMixin<XorStrCleanupPass> {
  llvm::PreservedAnalyses run(llvm::Function &F,
                              llvm::FunctionAnalysisManager &FAM);
  static llvm::StringRef name() { return "XorStrCleanupPass"; }
};

} // namespace xorstr
} // namespace neverc

#endif
