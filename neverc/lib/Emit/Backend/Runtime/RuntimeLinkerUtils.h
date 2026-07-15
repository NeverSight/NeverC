#ifndef NEVERC_LIB_EMIT_BACKEND_RUNTIME_RUNTIMELINKERUTILS_H
#define NEVERC_LIB_EMIT_BACKEND_RUNTIME_RUNTIMELINKERUTILS_H

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/IR/Comdat.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalObject.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/Linker/Linker.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/TargetParser/Triple.h"

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
  const llvm::Triple EmbeddedTT(Mod->getTargetTriple());
  const llvm::Triple ConsumerTT(M.getTargetTriple());
  const auto EmbeddedFormat = EmbeddedTT.getObjectFormat();
  const bool LinkerMetadataIsCompatible =
      EmbeddedFormat != llvm::Triple::UnknownObjectFormat &&
      EmbeddedFormat == ConsumerTT.getObjectFormat() &&
      EmbeddedTT.getOS() == ConsumerTT.getOS() &&
      EmbeddedTT.getEnvironment() == ConsumerTT.getEnvironment();

  stripHostTargetAttributes(*Mod);
  Mod->setDataLayout(M.getDataLayout());
  Mod->setTargetTriple(M.getTargetTriple());

  if (auto *Flags = Mod->getModuleFlagsMetadata())
    Flags->clearOperands();

  // Embedded runtimes may have been bootstrapped for another object format
  // (e.g. a Windows-hosted string runtime retargeted to Linux). COFF encodes
  // llvm.linker.options as one "/DEFAULTLIB:..." string, while ELF requires
  // option/value pairs and otherwise fatals with "invalid llvm.linker.options".
  // Preserve directives only when object format, OS, and ABI environment
  // match: they can carry real target dependencies, but ELF alone is not
  // enough to make (for example) GNU/Linux and Android libraries compatible.
  // Mimalloc still adds advapi32 below if its source metadata did not contain
  // it.
  if (!LinkerMetadataIsCompatible)
    for (llvm::StringRef MDName :
         {"llvm.linker.options", "llvm.dependent-libraries"})
      if (auto *NMD = Mod->getNamedMetadata(MDName))
        Mod->eraseNamedMetadata(NMD);

  // Do not attribute embedded runtime implementation units to user objects.
  if (auto *NMD = Mod->getNamedMetadata("llvm.ident"))
    Mod->eraseNamedMetadata(NMD);

  return Mod;
}

inline void linkModuleOrFail(llvm::Module &M,
                             std::unique_ptr<llvm::Module> Src,
                             llvm::StringRef Label) {
  if (llvm::Linker::linkModules(M, std::move(Src),
                                llvm::Linker::Flags::OverrideFromSrc))
    llvm::report_fatal_error(llvm::Twine("Failed to link ") + Label);
}

// ===----------------------------------------------------------------------===//
// Runtime-definition provenance
// ===----------------------------------------------------------------------===//

inline void tagRuntimeDefinitions(llvm::Module &Mod,
                                  llvm::StringRef MetadataKind) {
  llvm::MDNode *Tag = llvm::MDNode::get(Mod.getContext(), {});
  for (llvm::Function &F : Mod)
    if (!F.isDeclaration())
      F.setMetadata(MetadataKind, Tag);
  for (llvm::GlobalVariable &GV : Mod.globals())
    if (!GV.isDeclaration())
      GV.setMetadata(MetadataKind, Tag);
}

inline bool hasRuntimeDefinitionTag(const llvm::GlobalObject &GO,
                                    llvm::StringRef MetadataKind) {
  return GO.getMetadata(MetadataKind) != nullptr;
}

inline void clearRuntimeDefinitionTags(llvm::Module &Mod,
                                       llvm::StringRef MetadataKind) {
  for (llvm::Function &F : Mod)
    if (hasRuntimeDefinitionTag(F, MetadataKind))
      F.setMetadata(MetadataKind, nullptr);
  for (llvm::GlobalVariable &GV : Mod.globals())
    if (hasRuntimeDefinitionTag(GV, MetadataKind))
      GV.setMetadata(MetadataKind, nullptr);
}

/// Attach a COMDAT group on COFF so linkonce_odr / weak_odr survive a native
/// multi-TU link.  ELF/Mach-O emit weak/coalesced symbols from the linkage
/// alone; COFF requires an explicit selection kind or the symbols become
/// ordinary strong definitions and the link reports duplicates.
inline void ensureCoalescibleComdat(llvm::GlobalObject &GO) {
  if (GO.hasComdat())
    return;
  llvm::Module *M = GO.getParent();
  if (!M || !llvm::Triple(M->getTargetTriple()).isOSBinFormatCOFF())
    return;
  llvm::Comdat *C = M->getOrInsertComdat(GO.getName());
  C->setSelectionKind(llvm::Comdat::Any);
  GO.setComdat(C);
}

inline void makeLinkOnceODR(llvm::GlobalObject &GO) {
  GO.setLinkage(llvm::GlobalValue::LinkOnceODRLinkage);
  GO.setVisibility(llvm::GlobalValue::HiddenVisibility);
  ensureCoalescibleComdat(GO);
}

inline void makeWeakODR(llvm::GlobalObject &GO,
                        llvm::GlobalValue::VisibilityTypes Vis =
                            llvm::GlobalValue::DefaultVisibility) {
  GO.setLinkage(llvm::GlobalValue::WeakODRLinkage);
  GO.setVisibility(Vis);
  ensureCoalescibleComdat(GO);
}

/// Give runtime-private definitions stable names before linking them into a
/// consumer module. Without this step, a colliding user-local symbol can make
/// LLVM append a TU-specific suffix, preventing runtime copies from coalescing
/// consistently at the later LTO or native link.
inline void namespaceRuntimeLocals(llvm::Module &Mod, llvm::StringRef Prefix) {
  for (llvm::Function &F : Mod)
    if (!F.isDeclaration() && F.hasLocalLinkage())
      F.setName((llvm::Twine(Prefix) + F.getName()).str());
  for (llvm::GlobalVariable &GV : Mod.globals())
    if (!GV.isDeclaration() && GV.hasLocalLinkage())
      GV.setName((llvm::Twine(Prefix) + GV.getName()).str());
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

#endif // NEVERC_LIB_EMIT_BACKEND_RUNTIME_RUNTIMELINKERUTILS_H
