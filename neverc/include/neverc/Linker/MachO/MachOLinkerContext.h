#ifndef LINKER_MACHO_MACHOLINKERCONTEXT_H
#define LINKER_MACHO_MACHOLINKERCONTEXT_H

#include "Linker/Core/Runtime/Session.h"
#include "llvm/ADT/CachedHashString.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringRef.h"
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace linker::macho {

class ArchiveFile;
class DylibFile;

class MachOLinkerContext final : public CommonLinkerContext {
public:
  struct Impl;

  MachOLinkerContext();
  MachOLinkerContext(const MachOLinkerContext &) = delete;
  MachOLinkerContext &operator=(const MachOLinkerContext &) = delete;
  ~MachOLinkerContext() override;

  Impl &state() { return *State; }

private:
  std::unique_ptr<Impl> State;
};

struct ArchiveFileInfo {
  ArchiveFile *file = nullptr;
  bool isCommandLineLoad = false;
};

MachOLinkerContext &machoContext();

llvm::DenseMap<llvm::CachedHashStringRef, llvm::StringRef> &
machoResolvedLibraries();
llvm::DenseMap<llvm::CachedHashStringRef, llvm::StringRef> &
machoResolvedFrameworks();
llvm::DenseMap<llvm::StringRef, ArchiveFileInfo> &machoLoadedArchives();
std::vector<llvm::StringRef> &machoMissingAutolinkWarnings();
llvm::DenseSet<llvm::StringRef> &machoLoadedObjectFrameworks();
llvm::DenseMap<llvm::CachedHashStringRef, DylibFile *> &machoLoadedDylibs();
std::mutex &machoCachedReadsMutex();
uint32_t &machoLCDylibCount();

} // namespace linker::macho

#endif
