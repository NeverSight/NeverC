#include "neverc/Foundation/Builtin/BuiltinNvkKernel.h"

using namespace neverc;

#include "BuiltinNvkKernelBitcode.h"

llvm::StringRef BuiltinNvkKernel::getEmbeddedBitcode() {
  if (kNvkKernelBCLen == 0)
    return {};
  return llvm::StringRef(reinterpret_cast<const char *>(kNvkKernelBC),
                         kNvkKernelBCLen);
}
