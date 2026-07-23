#include "neverc/DynCode/IR/ZeroRelocPass.h"
#include "neverc/DynCode/IR/DynCodeIRVerifier.h"
#include "neverc/DynCode/IR/DynCodePreparePass.h"
#include "neverc/DynCode/IR/StackifyPass.h"
#include "llvm/IR/Module.h"

using namespace llvm;

namespace neverc {
namespace dyncode {

// ZeroRelocPass is now a thin compatibility wrapper.  The
// dyncode pipeline schedules DynCodePreparePass early and StackifyPass followed
// by the sealed DynCodeIRVerifier gate late (see Pipeline.cpp).  A single
// ZeroRelocPass run performs prepare, stackify and final IR verification in
// order for any caller that still wants the whole IR stage at once.  It no
// longer relies on a named-metadata "which run is this" sentinel.
PreservedAnalyses ZeroRelocPass::run(Module &M, ModuleAnalysisManager &MAM) {
  bool Changed = false;
  Changed |= !DynCodePreparePass(EntrySymbol, InlineAll)
                  .run(M, MAM)
                  .areAllPreserved();
  Changed |=
      !StackifyPass(EntrySymbol, InlineAll).run(M, MAM).areAllPreserved();
  DynCodeIRVerifier().run(M, MAM);
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

} // namespace dyncode
} // namespace neverc
