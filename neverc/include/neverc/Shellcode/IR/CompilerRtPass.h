#ifndef NEVERC_SHELLCODE_COMPILERRTPASS_H
#define NEVERC_SHELLCODE_COMPILERRTPASS_H

#include "llvm/IR/PassManager.h"

namespace neverc {
namespace shellcode {

class CompilerRtPass : public llvm::PassInfoMixin<CompilerRtPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &);
};

}
}

#endif
