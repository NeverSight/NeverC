#ifndef NEVERC_TRANSFORMS_ENCRYPTCALLSTRINGSPASS_H
#define NEVERC_TRANSFORMS_ENCRYPTCALLSTRINGSPASS_H

#include "llvm/IR/PassManager.h"

namespace neverc {
namespace xorstr {

struct EncryptCallStringsPass
    : public llvm::PassInfoMixin<EncryptCallStringsPass> {
  unsigned MaxLen;
  explicit EncryptCallStringsPass(unsigned MaxLen = 1024) : MaxLen(MaxLen) {}
  llvm::PreservedAnalyses run(llvm::Module &M,
                              llvm::ModuleAnalysisManager &MAM);
  static llvm::StringRef name() { return "EncryptCallStringsPass"; }
};

} // namespace xorstr
} // namespace neverc

#endif
