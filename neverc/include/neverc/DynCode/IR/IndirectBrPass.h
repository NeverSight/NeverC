#ifndef NEVERC_DYNCODE_INDIRECTBRPASS_H
#define NEVERC_DYNCODE_INDIRECTBRPASS_H

#include "llvm/IR/PassManager.h"

namespace neverc {
namespace dyncode {

struct IndirectBrPass : public llvm::PassInfoMixin<IndirectBrPass> {
  llvm::PreservedAnalyses run(llvm::Module &M,
                              llvm::ModuleAnalysisManager &MAM);
  static llvm::StringRef name() { return "IndirectBrPass"; }
};

}
}

#endif
