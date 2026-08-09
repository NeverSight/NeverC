#ifndef NEVERC_TRANSFORMS_ENCRYPTCALLSTRINGSPASS_H
#define NEVERC_TRANSFORMS_ENCRYPTCALLSTRINGSPASS_H

#include "llvm/IR/PassManager.h"
#include <cstdint>

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

struct FinalizeXorStrPass : public llvm::PassInfoMixin<FinalizeXorStrPass> {
  std::uint64_t KeySeed;
  explicit FinalizeXorStrPass(std::uint64_t KeySeed = 0) : KeySeed(KeySeed) {}
  llvm::PreservedAnalyses run(llvm::Module &M,
                              llvm::ModuleAnalysisManager &MAM);
  static llvm::StringRef name() { return "FinalizeXorStrPass"; }
};

} // namespace xorstr
} // namespace neverc

#endif
