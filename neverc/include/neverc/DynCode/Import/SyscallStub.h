#ifndef NEVERC_DYNCODE_SYSCALLSTUB_H
#define NEVERC_DYNCODE_SYSCALLSTUB_H

#include "neverc/DynCode/Pipeline/DynCodeOptions.h"
#include "llvm/IR/PassManager.h"

namespace neverc {
namespace dyncode {

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
