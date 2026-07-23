#ifndef NEVERC_DYNCODE_DATA2TEXTPASS_H
#define NEVERC_DYNCODE_DATA2TEXTPASS_H

#include "llvm/IR/PassManager.h"

namespace neverc {
namespace dyncode {

// Data2TextPass runs at two distinct dyncode phases
// (data_to_text.pre and data_to_text.post).  The phase is now an explicit
// constructor argument instead of a named-metadata "which run" sentinel: the
// pre run only inlines constant operands and eliminates constant globals; the
// post run additionally devectorizes constant stores.
struct Data2TextPass : public llvm::PassInfoMixin<Data2TextPass> {
  bool IsLate = false;

  Data2TextPass() = default;
  explicit Data2TextPass(bool IsLate) : IsLate(IsLate) {}

  llvm::PreservedAnalyses run(llvm::Module &M,
                              llvm::ModuleAnalysisManager &MAM);
  static llvm::StringRef name() { return "Data2TextPass"; }
};

}
}

#endif
