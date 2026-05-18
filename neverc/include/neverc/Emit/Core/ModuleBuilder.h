#ifndef NEVERC_CODEGEN_MODULEBUILDER_H
#define NEVERC_CODEGEN_MODULEBUILDER_H

#include "neverc/Tree/Core/TreeConsumer.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"

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
};

IRGenerator *
CreateIRGenerator(DiagnosticsEngine &Diags, llvm::StringRef ModuleName,
                  llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS,
                  const HeaderIndexOptions &HeaderIdxOpts,
                  const PrepOptions &PrepOpts, const CodeGenOptions &CGO,
                  llvm::LLVMContext &C);

} // end namespace neverc

#endif
