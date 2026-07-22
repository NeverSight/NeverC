#include "neverc/DynCode/IR/DynCodeIRVerifier.h"
#include "DynCodeIRStageSupport.h"
#include "neverc/DynCode/Pipeline/DynCodeIRHelperNames.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Module.h"

using namespace llvm;

namespace neverc {
namespace dyncode {

namespace {

void classifyInitializer(const Constant *C, bool &HasGlobalRef,
                         bool &HasBlockAddress) {
  if (!C || (HasGlobalRef && HasBlockAddress))
    return;
  if (isa<GlobalValue>(C))
    HasGlobalRef = true;
  if (isa<BlockAddress>(C))
    HasBlockAddress = true;
  if (HasGlobalRef && HasBlockAddress)
    return;
  if (auto *CE = dyn_cast<ConstantExpr>(C)) {
    for (const Use &U : CE->operands())
      if (auto *OpC = dyn_cast<Constant>(U.get()))
        classifyInitializer(OpC, HasGlobalRef, HasBlockAddress);
    return;
  }
  if (isa<ConstantAggregate>(C)) {
    for (const Use &U : C->operands())
      if (auto *OpC = dyn_cast<Constant>(U.get()))
        classifyInitializer(OpC, HasGlobalRef, HasBlockAddress);
  }
}

bool validate(Module &M) {
  for (GlobalVariable &GV : M.globals()) {
    if (GV.getName().starts_with(ir::kLlvmDotPrefix))
      continue;
    Constant *Init = GV.hasInitializer() ? GV.getInitializer() : nullptr;
    Twine Name = GV.getName();

    bool HasGlobalRef = false, HasBlockAddress = false;
    if (Init)
      classifyInitializer(Init, HasGlobalRef, HasBlockAddress);

    if (HasBlockAddress) {
      ir_stage::reportError(
          M, "'" + Name +
                 "' contains a BlockAddress (`&&label` from GCC's "
                 "computed-goto extension). DynCode cannot carry "
                 "the load-time relocations the backend needs to "
                 "materialise a basic-block address; rewrite the "
                 "`goto *labels[...]` dispatch as a plain `switch` "
                 "- the compiler lowers it to a compare-branch "
                 "chain that needs no data section.");
      return false;
    }
    if (GV.isConstant() && HasGlobalRef) {
      ir_stage::reportError(
          M, "constant '" + Name +
                 "' contains pointers to other globals or string "
                 "literals; dyncode cannot stackify such an "
                 "initializer because the pointers would need the "
                 "runtime load address. Rewrite it so the strings / "
                 "targets live inside the function body, or build "
                 "the table at runtime in the entry function.");
      return false;
    }
    if (!GV.isConstant() && HasGlobalRef) {
      ir_stage::reportError(
          M, "mutable global '" + Name +
                 "' is initialised with a pointer to another global; "
                 "dyncode has no loader to relocate that pointer. "
                 "Initialise the field at runtime in the entry "
                 "function instead.");
      return false;
    }
    ir_stage::reportError(M, "internal: leftover global variable '" + Name +
                                 "' after dyncode pipeline");
    return false;
  }
  return true;
}

} // namespace

PreservedAnalyses DynCodeIRVerifier::run(Module &M, ModuleAnalysisManager &) {
  // The sealed IR final-verify gate runs even when every upstream transform was
  // replaced.  It only reports (never rewrites) and then clears the internal
  // hard-error bookkeeping so it does not leak into later dyncode phases.
  if (!ir_stage::hadHardError(M))
    (void)validate(M);
  ir_stage::clearHardError(M);
  return PreservedAnalyses::all();
}

} // namespace dyncode
} // namespace neverc
