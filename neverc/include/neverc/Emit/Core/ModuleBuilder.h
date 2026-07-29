#ifndef NEVERC_EMIT_CORE_MODULEBUILDER_H
#define NEVERC_EMIT_CORE_MODULEBUILDER_H

#include "neverc/Tree/Core/TreeConsumer.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/STLFunctionalExtras.h"

namespace llvm {
class Constant;
class LLVMContext;
class Module;
class StringRef;

namespace vfs {
class FileSystem;
}
} // namespace llvm

namespace neverc {
class CodeGenOptions;
class Decl;
class DiagnosticsEngine;
class GlobalDecl;
class HeaderIndexOptions;
class PrepOptions;
class TreeContext;

namespace Emit {
class ModuleEmitter;
class DebugEmitter;
} // namespace Emit

class IRGenerator : public TreeConsumer {
  virtual void anchor();

public:
  Emit::ModuleEmitter &ME();

  llvm::Module *getModule();

  llvm::Module *releaseModule();

  Emit::DebugEmitter *getDebugEmitter();

  const Decl *getDeclForMangledName(llvm::StringRef MangledName);

  llvm::StringRef getMangledName(GlobalDecl GD);

  llvm::Constant *addrOfGlobal(GlobalDecl decl, bool isForDefinition);

  llvm::Module *startModule(llvm::StringRef ModuleName, llvm::LLVMContext &C);

  /// Generates the whole unit at once, for a caller that held IRGen back
  /// instead of streaming declarations in as they were parsed.
  ///
  /// \p ReportUnit is called where Sema's end-of-unit reports land in the
  /// streaming path: after every declaration has been lowered, before the
  /// module is released.  A caller that buffered those reports -- because it
  /// could not know yet whether this generator would be the one to run --
  /// delivers them there, at the point their effect is the same as if they
  /// had arrived on time.
  llvm::Module *
  generateTranslationUnit(TreeContext &Context,
                          llvm::function_ref<void()> ReportUnit = {});
};

IRGenerator *
CreateIRGenerator(DiagnosticsEngine &Diags, llvm::StringRef ModuleName,
                  llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS,
                  const HeaderIndexOptions &HeaderIdxOpts,
                  const PrepOptions &PrepOpts, const CodeGenOptions &CGO,
                  llvm::LLVMContext &C);

} // end namespace neverc

#endif
