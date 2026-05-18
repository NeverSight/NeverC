#ifndef NEVERC_SHELLCODE_KERNELIMPORTPASS_H
#define NEVERC_SHELLCODE_KERNELIMPORTPASS_H

#include "neverc/Shellcode/Pipeline/ShellcodeOptions.h"
#include "llvm/IR/PassManager.h"

namespace neverc {
namespace shellcode {

class KernelImportPass : public llvm::PassInfoMixin<KernelImportPass> {
public:
  explicit KernelImportPass(const ShellcodeOptions &Opts)
      : Target(Opts.Target), EntrySymbol(Opts.EntrySymbol) {}
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &);

private:
  TargetDesc Target;
  std::string EntrySymbol;
};

}
}

#endif
