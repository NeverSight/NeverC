#ifndef NEVERC_SHELLCODE_ZERORELOCPASS_H
#define NEVERC_SHELLCODE_ZERORELOCPASS_H

#include "llvm/ADT/StringRef.h"
#include "llvm/IR/PassManager.h"
#include <string>

namespace neverc {
namespace shellcode {

struct ZeroRelocPass : public llvm::PassInfoMixin<ZeroRelocPass> {
  std::string EntrySymbol;

  ZeroRelocPass() = default;
  explicit ZeroRelocPass(llvm::StringRef Entry) : EntrySymbol(Entry.str()) {}

  llvm::PreservedAnalyses run(llvm::Module &M,
                              llvm::ModuleAnalysisManager &MAM);
  static llvm::StringRef name() { return "ZeroRelocPass"; }
};

}
}

#endif
