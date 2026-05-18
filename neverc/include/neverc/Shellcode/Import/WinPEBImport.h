#ifndef NEVERC_SHELLCODE_WINPEBIMPORT_H
#define NEVERC_SHELLCODE_WINPEBIMPORT_H

#include "neverc/Shellcode/Pipeline/ShellcodeOptions.h"
#include "llvm/IR/PassManager.h"

namespace neverc {
namespace shellcode {

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
