#ifndef NEVERC_DYNCODE_KERNELIMPORTPASS_H
#define NEVERC_DYNCODE_KERNELIMPORTPASS_H

#include "neverc/DynCode/Pipeline/DynCodeOptions.h"
#include "llvm/IR/PassManager.h"

namespace neverc {
namespace dyncode {

class KernelImportPass : public llvm::PassInfoMixin<KernelImportPass> {
public:
  explicit KernelImportPass(const DynCodeOptions &Opts)
      : Target(Opts.Target), EntrySymbol(Opts.EntrySymbol) {}
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &);

private:
  TargetDesc Target;
  std::string EntrySymbol;
};

}
}

#endif
