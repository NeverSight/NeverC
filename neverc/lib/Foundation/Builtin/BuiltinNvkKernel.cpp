#include "neverc/Foundation/Builtin/BuiltinNvkKernel.h"

#include <algorithm>

using namespace neverc;

#include "BuiltinNvkKernelBitcode.h"

llvm::StringRef BuiltinNvkKernel::getEmbeddedBitcode() {
  if (kNvkKernelBCLen == 0)
    return {};
  return llvm::StringRef(reinterpret_cast<const char *>(kNvkKernelBC),
                         kNvkKernelBCLen);
}

bool BuiltinNvkKernel::hasEmbeddedSymbol(llvm::StringRef Name) {
  const char *const *Begin = kNvkKernelSymbols;
  const char *const *End = Begin + kNvkKernelSymbolCount;
  const auto It = std::lower_bound(
      Begin, End, Name, [](const char *Left, llvm::StringRef Right) {
        return llvm::StringRef(Left).compare(Right) < 0;
      });
  return It != End && Name == *It;
}
