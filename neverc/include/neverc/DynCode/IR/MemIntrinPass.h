#ifndef NEVERC_DYNCODE_MEMINTRINPASS_H
#define NEVERC_DYNCODE_MEMINTRINPASS_H

#include "llvm/IR/PassManager.h"

namespace neverc {
namespace dyncode {

class MemIntrinPass : public llvm::PassInfoMixin<MemIntrinPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &);
};

}
}

#endif
