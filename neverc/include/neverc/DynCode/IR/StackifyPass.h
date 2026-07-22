#ifndef NEVERC_DYNCODE_STACKIFYPASS_H
#define NEVERC_DYNCODE_STACKIFYPASS_H

#include "llvm/ADT/StringRef.h"
#include "llvm/IR/PassManager.h"
#include <string>

namespace neverc {
namespace dyncode {

// Volume 6 task 6: the late dyncode IR stage.  Re-normalises any functions or
// globals introduced by the intervening transforms (prepare), inlines the
// non-entry users of mutable globals, stackifies mutable globals into entry
// allocas and moves the entry to the front of the module.  Split out of the
// monolithic ZeroRelocPass; it no longer uses a named-metadata sentinel to
// decide whether it is the "second run".
struct StackifyPass : public llvm::PassInfoMixin<StackifyPass> {
  std::string EntrySymbol;
  bool InlineAll = false;

  StackifyPass() = default;
  explicit StackifyPass(llvm::StringRef Entry, bool InlineAll = false)
      : EntrySymbol(Entry.str()), InlineAll(InlineAll) {}

  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &MAM);
  static llvm::StringRef name() { return "DynCodeStackifyPass"; }
};

} // namespace dyncode
} // namespace neverc

#endif // NEVERC_DYNCODE_STACKIFYPASS_H
