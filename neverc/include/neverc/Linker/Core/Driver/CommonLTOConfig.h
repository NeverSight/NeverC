#ifndef LINKER_CORE_DRIVER_COMMONLTOCONFIG_H
#define LINKER_CORE_DRIVER_COMMONLTOCONFIG_H

#include "llvm/ADT/StringRef.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/LTO/Config.h"

namespace linker {

struct LinkerDriverConfig;

/// True when an -lto-basic-block-sections value names a function-list
/// file rather than one of the keyword modes (all/labels/none).  List
/// files are read from disk by createLTOConfig().
bool ltoBasicBlockSectionsIsListFile(llvm::StringRef bbs);

/// Build an lto::Config directly from LinkerDriverConfig.
/// Each backend only needs to supply its DiagHandler and EmitAddrsig
/// preference.
llvm::lto::Config createLTOConfig(const LinkerDriverConfig &Cfg,
                                  llvm::DiagnosticHandlerFunction DiagHandler,
                                  bool EmitAddrsig = true);

/// Parse Cfg.mllvmOpts into the global cl::opt registry.
/// Must be called before createLTOConfig().  When mllvmOpts is empty
/// (the common case), this is a near-zero-cost fast path.
void parseMllvmOptions(const LinkerDriverConfig &Cfg);

} // namespace linker

#endif // LINKER_CORE_DRIVER_COMMONLTOCONFIG_H
