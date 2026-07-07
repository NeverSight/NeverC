#ifndef NEVERC_DYNCODE_STRINGRUNTIMEPASS_H
#define NEVERC_DYNCODE_STRINGRUNTIMEPASS_H

#include "neverc/DynCode/IR/StringRuntimeABI.h"
#include "neverc/DynCode/Pipeline/TargetDesc.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/PassManager.h"
#include <cstdint>

namespace neverc {
namespace dyncode {

struct StringRuntimePass : public llvm::PassInfoMixin<StringRuntimePass> {
  static constexpr uint64_t UserArenaSize = StringRuntimeABI::UserArenaSize;
  static constexpr uint64_t KernelArenaSize = StringRuntimeABI::KernelArenaSize;

  static constexpr uint64_t arenaSizeFor(ExecutionLevel Level) {
    return Level == ExecutionLevel::Kernel ? KernelArenaSize : UserArenaSize;
  }

  explicit StringRuntimePass(uint64_t ArenaSize = UserArenaSize,
                             bool DynamicArena = true,
                             DynCodeOS OS = DynCodeOS::Linux)
      : ArenaSize(ArenaSize), DynamicArena(DynamicArena), OS(OS) {}

  llvm::PreservedAnalyses run(llvm::Module &M,
                              llvm::ModuleAnalysisManager &MAM);
  static llvm::StringRef name() { return "StringRuntimePass"; }

  /// Ensure the arena global, bump-alloc function, and free-list function
  /// exist in @p M.  Safe to call multiple times — idempotent when the
  /// infrastructure already has full definitions.
  static void ensureArenaInfrastructure(llvm::Module &M, uint64_t ArenaSize,
                                        bool DynamicArena = true,
                                        DynCodeOS OS = DynCodeOS::Linux);

private:
  uint64_t ArenaSize;
  bool DynamicArena;
  DynCodeOS OS;
};

struct StringRuntimeInlineFinalizePass
    : public llvm::PassInfoMixin<StringRuntimeInlineFinalizePass> {
  llvm::PreservedAnalyses run(llvm::Module &M,
                              llvm::ModuleAnalysisManager &MAM);
  static llvm::StringRef name() { return "StringRuntimeInlineFinalizePass"; }
};

}
}

#endif
