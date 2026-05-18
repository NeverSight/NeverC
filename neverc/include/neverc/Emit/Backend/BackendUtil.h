#ifndef NEVERC_CODEGEN_BACKENDUTIL_H
#define NEVERC_CODEGEN_BACKENDUTIL_H

#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>

namespace llvm {
template <typename T> class Expected;
template <typename T> class IntrusiveRefCntPtr;
class Module;
namespace vfs {
class FileSystem;
} // namespace vfs
} // namespace llvm

namespace neverc {
class DiagnosticsEngine;
class HeaderIndexOptions;
class CodeGenOptions;
class TargetOptions;
class LangOptions;
class EmitterConsumer;

enum BackendAction {
  Backend_EmitAssembly, ///< Emit native assembly files
  Backend_EmitBC,       ///< Emit LLVM bitcode files
  Backend_EmitLL,       ///< Emit human-readable LLVM assembly
  Backend_EmitNothing,  ///< Don't emit anything (benchmarking mode)
  Backend_EmitMCNull,   ///< Run CodeGen, but don't emit anything
  Backend_EmitObj       ///< Emit native object files
};

void genBackendOutput(DiagnosticsEngine &Diags, const HeaderIndexOptions &,
                      const CodeGenOptions &CGOpts, const TargetOptions &TOpts,
                      const LangOptions &LOpts, llvm::StringRef TDesc,
                      llvm::Module *M, BackendAction Action,
                      llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS,
                      std::unique_ptr<llvm::raw_pwrite_stream> OS,
                      EmitterConsumer *BC = nullptr);

} // namespace neverc

#endif
