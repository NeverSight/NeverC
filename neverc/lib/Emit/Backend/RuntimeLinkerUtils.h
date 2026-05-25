#ifndef NEVERC_LIB_EMIT_BACKEND_RUNTIMELINKERUTILS_H
#define NEVERC_LIB_EMIT_BACKEND_RUNTIMELINKERUTILS_H

#include "llvm/ADT/StringSet.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/Linker/Linker.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MemoryBuffer.h"

namespace neverc {

/// Strip host-specific target-cpu / target-features / tune-cpu attributes
/// from every definition in \p Mod.
///
/// Precompiled bitcode bakes the build host's CPU and feature set into
/// per-function attributes.  When cross-compiling to a different arch,
/// Linker::linkModules preserves these even after we reset the module
/// triple.  The mismatched backend then rejects unknown features.
/// Stripping lets merged functions inherit the user module's defaults.
inline void stripHostTargetAttributes(llvm::Module &Mod) {
  for (llvm::Function &F : Mod) {
    if (F.isDeclaration())
      continue;
    F.removeFnAttr("target-cpu");
    F.removeFnAttr("target-features");
    F.removeFnAttr("tune-cpu");
  }
}

/// Parse embedded bitcode, strip host attributes, and align metadata
/// (data layout, triple, module flags) with the user module.
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

/// Link the source module into M with OverrideFromSrc, or fatal error.
inline void linkModuleOrFail(llvm::Module &M,
                             std::unique_ptr<llvm::Module> Src,
                             llvm::StringRef Label) {
  if (llvm::Linker::linkModules(M, std::move(Src),
                                llvm::Linker::Flags::OverrideFromSrc))
    llvm::report_fatal_error(llvm::Twine("Failed to link ") + Label);
}

/// Capture all definition names from a module into string sets.
/// Must be called before linkModules destroys the source module.
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

} // namespace neverc

#endif
