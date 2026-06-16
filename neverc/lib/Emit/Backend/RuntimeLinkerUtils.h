#ifndef NEVERC_LIB_EMIT_BACKEND_RUNTIMELINKERUTILS_H
#define NEVERC_LIB_EMIT_BACKEND_RUNTIMELINKERUTILS_H

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/Linker/Linker.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MemoryBuffer.h"

namespace neverc {

// ===----------------------------------------------------------------------===//
// Target attribute stripping
// ===----------------------------------------------------------------------===//

inline void stripHostTargetAttributes(llvm::Module &Mod) {
  for (llvm::Function &F : Mod) {
    if (F.isDeclaration())
      continue;
    F.removeFnAttr("target-cpu");
    F.removeFnAttr("target-features");
    F.removeFnAttr("tune-cpu");
  }
}

// ===----------------------------------------------------------------------===//
// Bitcode parsing and linking
// ===----------------------------------------------------------------------===//

inline std::unique_ptr<llvm::Module>
parseBitcodeAndPrepare(llvm::StringRef Embedded, llvm::Module &M,
                       llvm::StringRef Label) {
  auto Buf = llvm::MemoryBuffer::getMemBuffer(
      Embedded, Label, /*RequiresNullTerminator=*/false);

  auto ExpectedMod =
      llvm::parseBitcodeFile(Buf->getMemBufferRef(), M.getContext());
  if (!ExpectedMod)
    llvm::report_fatal_error(llvm::Twine("Failed to parse ") + Label + ": " +
                             llvm::toString(ExpectedMod.takeError()));
  auto Mod = std::move(*ExpectedMod);

  stripHostTargetAttributes(*Mod);
  Mod->setDataLayout(M.getDataLayout());
  Mod->setTargetTriple(M.getTargetTriple());

  if (auto *Flags = Mod->getModuleFlagsMetadata())
    Flags->clearOperands();

  return Mod;
}

inline void linkModuleOrFail(llvm::Module &M,
                             std::unique_ptr<llvm::Module> Src,
                             llvm::StringRef Label) {
  if (llvm::Linker::linkModules(M, std::move(Src),
                                llvm::Linker::Flags::OverrideFromSrc))
    llvm::report_fatal_error(llvm::Twine("Failed to link ") + Label);
}

inline void captureDefinitionNames(const llvm::Module &Mod,
                                   llvm::StringSet<> &FnNames,
                                   llvm::StringSet<> &GlobalNames) {
  for (const llvm::Function &F : Mod)
    if (!F.isDeclaration())
      FnNames.insert(F.getName());
  for (const llvm::GlobalVariable &GV : Mod.globals())
    if (!GV.isDeclaration())
      GlobalNames.insert(GV.getName());
}

// ===----------------------------------------------------------------------===//
// GlobalValue reference walking
//
// Shared by all runtime linker passes for call-graph pruning and DCE.
// Correctly recurses into ConstantExpr / ConstantStruct / ConstantArray /
// ConstantVector to find nested GlobalValue references.
// ===----------------------------------------------------------------------===//

namespace detail {

template <typename VisitFn>
void visitGlobalRefs(llvm::Constant *C, VisitFn &Visit,
                     llvm::SmallPtrSetImpl<const llvm::Value *> &Visited) {
  if (!Visited.insert(C).second)
    return;
  if (auto *GV = llvm::dyn_cast<llvm::GlobalValue>(C)) {
    Visit(GV);
    return;
  }
  for (llvm::Use &Op : C->operands())
    if (auto *Inner = llvm::dyn_cast<llvm::Constant>(Op))
      visitGlobalRefs(Inner, Visit, Visited);
}

} // namespace detail

/// Visit every GlobalValue referenced by \p U's operands.
template <typename VisitFn>
void forEachGlobalOperand(llvm::User *U, VisitFn Visit) {
  llvm::SmallPtrSet<const llvm::Value *, 16> Visited;
  for (llvm::Use &Op : U->operands())
    if (auto *C = llvm::dyn_cast<llvm::Constant>(Op))
      detail::visitGlobalRefs(C, Visit, Visited);
}

/// Visit every GlobalValue referenced in \p C (including \p C itself
/// if it is a GlobalValue).  Use for global-variable initializer walking.
template <typename VisitFn>
void forEachGlobalInConstant(llvm::Constant *C, VisitFn Visit) {
  llvm::SmallPtrSet<const llvm::Value *, 16> Visited;
  detail::visitGlobalRefs(C, Visit, Visited);
}

/// Visit every GlobalValue referenced by any instruction in \p F.
/// Shares a single visited-set across all instructions for efficiency.
template <typename VisitFn>
void forEachGlobalReferencedBy(llvm::Function &F, VisitFn Visit) {
  llvm::SmallPtrSet<const llvm::Value *, 32> Visited;
  for (llvm::BasicBlock &BB : F)
    for (llvm::Instruction &I : BB)
      for (llvm::Use &Op : I.operands())
        if (auto *C = llvm::dyn_cast<llvm::Constant>(Op))
          detail::visitGlobalRefs(C, Visit, Visited);
}

// ===----------------------------------------------------------------------===//
// Global erasure
// ===----------------------------------------------------------------------===//

template <typename GlobalT>
void poisonAndErase(GlobalT &GV) {
  GV.replaceAllUsesWith(llvm::PoisonValue::get(GV.getType()));
  GV.eraseFromParent();
}

} // namespace neverc

#endif
