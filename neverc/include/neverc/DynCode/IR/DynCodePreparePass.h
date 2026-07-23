#ifndef NEVERC_DYNCODE_DYNCODEPREPAREPASS_H
#define NEVERC_DYNCODE_DYNCODEPREPAREPASS_H

#include "llvm/ADT/StringRef.h"
#include "llvm/IR/PassManager.h"
#include <string>

namespace neverc {
namespace dyncode {

// The early dyncode IR stage.  Chooses the single entry,
// normalises linkage/attributes, rejects global ctors/dtors and external-weak
// globals, demotes thread-local storage and strips llvm.used.  Split out of the
// monolithic ZeroRelocPass so the run-count named-metadata sentinel is gone;
// prepare runs once at the start of the dyncode IR pipeline.
struct DynCodePreparePass : public llvm::PassInfoMixin<DynCodePreparePass> {
  std::string EntrySymbol;
  bool InlineAll = false;

  DynCodePreparePass() = default;
  explicit DynCodePreparePass(llvm::StringRef Entry, bool InlineAll = false)
      : EntrySymbol(Entry.str()), InlineAll(InlineAll) {}

  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &MAM);
  static llvm::StringRef name() { return "DynCodePreparePass"; }
};

} // namespace dyncode
} // namespace neverc

#endif // NEVERC_DYNCODE_DYNCODEPREPAREPASS_H
