#include "DynCodeIRStageSupport.h"

#include "Extractor/ExtractorCommon.h"
#include "neverc/DynCode/IR/ZeroRelocABI.h"
#include "neverc/DynCode/Pipeline/DynCodeIRHelperNames.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace neverc {
namespace dyncode {
namespace ir_stage {

bool hadHardError(Module &M) {
  return M.getNamedMetadata(ZeroRelocABI::HardErrorSentinel) != nullptr;
}

void reportError(Module &M, const Twine &Msg) {
  if (M.begin() != M.end())
    M.getContext().diagnose(
        DiagnosticInfoUnsupported(*M.begin(), "dyncode: " + Msg));
  else
    errs() << "error: dyncode: " << Msg << "\n";
  M.getOrInsertNamedMetadata(ZeroRelocABI::HardErrorSentinel);
}

void clearHardError(Module &M) {
  if (auto *N = M.getNamedMetadata(ZeroRelocABI::HardErrorSentinel))
    N->eraseFromParent();
}

Function *findEntry(Module &M, StringRef UserEntry) {
  Function *First = nullptr;
  for (Function &F : M) {
    if (F.isDeclaration())
      continue;
    if (isDynCodeEntryCandidate(F.getName(), UserEntry))
      return &F;
    if (!First)
      First = &F;
  }
  return First;
}

bool prep(Module &M, Function *Entry, bool InlineAll) {
  bool Changed = false;

  for (const char *Name : {"llvm.global_ctors", "llvm.global_dtors"}) {
    auto *GV = M.getNamedGlobal(Name);
    if (!GV)
      continue;
    if (auto *Init = GV->getInitializer())
      if (auto *Arr = dyn_cast<ConstantArray>(Init))
        if (Arr->getNumOperands() > 0) {
          reportError(M, StringRef(Name) == "llvm.global_ctors"
                             ? "global constructors are not allowed; move the "
                               "initialization into the entry function"
                             : "global destructors are not allowed");
          return false;
        }
  }

  for (GlobalVariable &GV : M.globals()) {
    StringRef Name = GV.getName();
    if (Name.starts_with(ir::kLlvmDotPrefix))
      continue;
    if (GV.isThreadLocal()) {
      GV.setThreadLocalMode(GlobalValue::NotThreadLocal);
      Changed = true;
    }
    if (GV.hasExternalWeakLinkage()) {
      reportError(M, "external_weak global '" + Name + "' is not allowed");
      return false;
    }
  }

  for (const char *Name : {"llvm.used", "llvm.compiler.used"}) {
    if (auto *GV = M.getNamedGlobal(Name)) {
      GV->eraseFromParent();
      Changed = true;
    }
  }

  for (Function &F : M) {
    if (F.isDeclaration())
      continue;
    if (&F == Entry) {
      F.setLinkage(GlobalValue::ExternalLinkage);
      F.setDSOLocal(true);
      F.removeFnAttr(Attribute::OptimizeNone);
      continue;
    }
    if (!F.hasLocalLinkage()) {
      F.setLinkage(GlobalValue::InternalLinkage);
      Changed = true;
    }
    if (InlineAll && !F.hasFnAttribute(Attribute::AlwaysInline) &&
        !F.hasFnAttribute(Attribute::NoInline)) {
      F.addFnAttr(Attribute::AlwaysInline);
      F.removeFnAttr(Attribute::OptimizeNone);
      Changed = true;
    }
  }

  return Changed;
}

} // namespace ir_stage
} // namespace dyncode
} // namespace neverc
