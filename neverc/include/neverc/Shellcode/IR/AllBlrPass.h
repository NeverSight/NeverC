#ifndef NEVERC_SHELLCODE_ALLBLRPASS_H
#define NEVERC_SHELLCODE_ALLBLRPASS_H

#include "llvm/IR/PassManager.h"

namespace neverc {
namespace shellcode {

struct AllBlrPass : public llvm::PassInfoMixin<AllBlrPass> {
  llvm::PreservedAnalyses run(llvm::Module &M,
                              llvm::ModuleAnalysisManager &MAM);
  static llvm::StringRef name() { return "AllBlrPass"; }
};

}
}

#endif
