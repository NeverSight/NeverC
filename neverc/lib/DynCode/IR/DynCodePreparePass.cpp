#include "neverc/DynCode/IR/DynCodePreparePass.h"
#include "DynCodeIRStageSupport.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"

using namespace llvm;

namespace neverc {
namespace dyncode {

PreservedAnalyses DynCodePreparePass::run(Module &M, ModuleAnalysisManager &) {
  if (ir_stage::hadHardError(M))
    return PreservedAnalyses::all();
  if (Hosted)
    for (const GlobalVariable &G : M.globals())
      if (G.isThreadLocal() || (!G.isDeclaration() && !G.isConstant())) {
        ir_stage::reportError(
            M, "hosted DynCode does not admit mutable/TLS global state: " +
                   G.getName());
        return PreservedAnalyses::all();
      }

  Function *Entry = ir_stage::findEntry(M, EntrySymbol);
  bool Changed = ir_stage::prep(M, Entry, InlineAll);
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

} // namespace dyncode
} // namespace neverc
