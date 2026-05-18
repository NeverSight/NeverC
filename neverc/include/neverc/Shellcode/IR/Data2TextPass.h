#ifndef NEVERC_SHELLCODE_DATA2TEXTPASS_H
#define NEVERC_SHELLCODE_DATA2TEXTPASS_H

#include "llvm/IR/PassManager.h"

namespace neverc {
namespace shellcode {

struct Data2TextPass : public llvm::PassInfoMixin<Data2TextPass> {
  llvm::PreservedAnalyses run(llvm::Module &M,
                              llvm::ModuleAnalysisManager &MAM);
  static llvm::StringRef name() { return "Data2TextPass"; }
};

}
}

#endif
