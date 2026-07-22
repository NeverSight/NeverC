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

  Function *Entry = ir_stage::findEntry(M, EntrySymbol);
  bool Changed = ir_stage::prep(M, Entry, InlineAll);
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

} // namespace dyncode
} // namespace neverc
