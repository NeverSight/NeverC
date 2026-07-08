#ifndef NEVERC_TRANSFORMS_STRHASHFOLDPASS_H
#define NEVERC_TRANSFORMS_STRHASHFOLDPASS_H

#include "llvm/IR/PassManager.h"

namespace neverc {
namespace strhash {

struct StrHashFoldPass : public llvm::PassInfoMixin<StrHashFoldPass> {
  llvm::PreservedAnalyses run(llvm::Module &M,
                              llvm::ModuleAnalysisManager &MAM);
  static llvm::StringRef name() { return "StrHashFoldPass"; }
};

} // namespace strhash
} // namespace neverc

#endif
