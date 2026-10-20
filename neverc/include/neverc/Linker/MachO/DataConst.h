#ifndef LINKER_MACHO_DATACONST_H
#define LINKER_MACHO_DATACONST_H

#include "llvm/BinaryFormat/MachO.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/VersionTuple.h"
#include "llvm/TextAPI/Platform.h"

namespace linker::macho {

inline bool defaultDataConst(llvm::MachO::HeaderFileType OutputType, bool IsPic,
                             llvm::MachO::PlatformType Platform,
                             const llvm::VersionTuple &MinDeployment) {
  if (Platform == llvm::MachO::PLATFORM_MACOS &&
      MinDeployment < llvm::VersionTuple(10, 15))
    return false;
  switch (OutputType) {
  case llvm::MachO::MH_EXECUTE:
    return IsPic;
  case llvm::MachO::MH_DYLIB:
  case llvm::MachO::MH_BUNDLE:
    return true;
  case llvm::MachO::MH_OBJECT:
  case llvm::MachO::MH_DYLINKER:
  case llvm::MachO::MH_PRELOAD:
  case llvm::MachO::MH_KEXT_BUNDLE:
    // These images do not use the ordinary userspace dyld binding window.
    // Keep data writable unless the caller explicitly selects -data_const.
    return false;
  default:
    llvm_unreachable("unsupported output type for determining data-const default");
  }
}

} // namespace linker::macho

#endif
