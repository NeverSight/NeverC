#ifndef LINKER_CORE_DRIVER_COMMONLTOCONFIG_H
#define LINKER_CORE_DRIVER_COMMONLTOCONFIG_H

#include "llvm/ADT/StringRef.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/LTO/Config.h"

namespace neverc {
class ResourceSessionView;
}

namespace linker {

struct LinkerDriverConfig;

/// True when an -lto-basic-block-sections value names a function-list
/// file rather than one of the keyword modes (all/labels/none).  List
/// files are read from disk by createLTOConfig().
bool ltoBasicBlockSectionsIsListFile(llvm::StringRef bbs);

/// Build an lto::Config directly from LinkerDriverConfig. The returned config
/// owns a snapshot-backed process-global LLVM option profile for its complete
/// lifetime, so it and the lto::LTO that consumes it must be destroyed on the
/// creating thread. Concurrent profiles are serialized; same-thread nesting
/// must unwind in strict LIFO order.
/// Each backend only needs to supply its DiagHandler and EmitAddrsig
/// preference.
llvm::lto::Config createLTOConfig(const LinkerDriverConfig &Cfg,
                                  llvm::DiagnosticHandlerFunction DiagHandler,
                                  bool EmitAddrsig = true);

/// Explicit-parent variant used by linker executions. The legacy overload
/// above remains a top-level ABI entry point.
llvm::lto::Config createLTOConfig(
    const LinkerDriverConfig &Cfg,
    llvm::DiagnosticHandlerFunction DiagHandler, bool EmitAddrsig,
    neverc::ResourceSessionView ParentSession);

/// Legacy compatibility entry point. New callers should use createLTOConfig(),
/// which owns and restores the parsed LLVM option profile for the complete LTO
/// lifetime. This function intentionally preserves the old process-global
/// parse behavior for existing lockstep C++ callers.
void parseMllvmOptions(const LinkerDriverConfig &Cfg);

} // namespace linker

#endif // LINKER_CORE_DRIVER_COMMONLTOCONFIG_H
