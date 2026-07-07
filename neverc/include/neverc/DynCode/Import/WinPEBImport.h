#ifndef NEVERC_DYNCODE_WINPEBIMPORT_H
#define NEVERC_DYNCODE_WINPEBIMPORT_H

#include "neverc/DynCode/Pipeline/DynCodeOptions.h"
#include "llvm/IR/PassManager.h"

namespace neverc {
namespace dyncode {

class WinPEBImportPass : public llvm::PassInfoMixin<WinPEBImportPass> {
public:
  explicit WinPEBImportPass(TargetDesc T) : Target(T) {}
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &);

private:
  TargetDesc Target;
};

}
}

#endif
