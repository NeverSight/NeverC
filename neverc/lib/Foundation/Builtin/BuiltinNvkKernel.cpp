#include "neverc/Foundation/Builtin/BuiltinNvkKernel.h"

using namespace neverc;

#include "BuiltinNvkKernelBitcode.h"

unsigned BuiltinNvkKernel::getEmbeddedModuleCount() {
  return kNvkKernelBCEntryCount;
}

std::pair<llvm::StringRef, llvm::StringRef>
BuiltinNvkKernel::getEmbeddedModule(unsigned Idx) {
  if (Idx >= kNvkKernelBCEntryCount)
    return {};
  auto &E = kNvkKernelBCEntries[Idx];
  return {llvm::StringRef(E.name),
          llvm::StringRef(reinterpret_cast<const char *>(E.data), E.len)};
}
