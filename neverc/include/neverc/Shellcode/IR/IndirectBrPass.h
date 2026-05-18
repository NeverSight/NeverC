#ifndef NEVERC_SHELLCODE_INDIRECTBRPASS_H
#define NEVERC_SHELLCODE_INDIRECTBRPASS_H

#include "llvm/IR/PassManager.h"

namespace neverc {
namespace shellcode {

struct IndirectBrPass : public llvm::PassInfoMixin<IndirectBrPass> {
  llvm::PreservedAnalyses run(llvm::Module &M,
                              llvm::ModuleAnalysisManager &MAM);
  static llvm::StringRef name() { return "IndirectBrPass"; }
};

}
}

#endif
