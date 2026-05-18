#ifndef NEVERC_SHELLCODE_SYSCALLSTUB_H
#define NEVERC_SHELLCODE_SYSCALLSTUB_H

#include "neverc/Shellcode/Pipeline/ShellcodeOptions.h"
#include "llvm/IR/PassManager.h"

namespace neverc {
namespace shellcode {

class SyscallStubPass : public llvm::PassInfoMixin<SyscallStubPass> {
public:
  explicit SyscallStubPass(TargetDesc T) : Target(T) {}
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &);

private:
  TargetDesc Target;
};

}
}

#endif
