#ifndef NEVERC_PLUGIN_HOST_PLUGINIOBRIDGE_H
#define NEVERC_PLUGIN_HOST_PLUGINIOBRIDGE_H

#include "neverc/Plugin/PluginSource.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace neverc::plugin {

class PluginProcessServices;
class PluginTaskContext;

struct PluginMemoryOutputSnapshot {
  uint64_t Generation = 0;
  std::vector<uint8_t> Bytes;
};

struct PluginOutputSealSnapshot {
  NevercOutputSealHandle Handle{};
  NevercOutputKind Kind = 0;
  NevercOutputState State = 0;
  NevercOutputFlags Flags = NEVERC_OUTPUT_FLAG_NONE;
  uint64_t Size = 0;
  uint64_t PublicationGeneration = 0;
  std::array<uint8_t, 32> Digest{};
  std::string Destination;
};

struct PluginDependencySnapshot {
  std::string CanonicalPath;
  std::array<uint8_t, 32> ContentDigest{};
  NevercInputDependencyKind Kind = NEVERC_INPUT_DEPENDENCY_SOURCE;
  bool System = false;
  std::string ProviderID;
};

NevercInterfaceID ioPluginInterfaceID();

llvm::Error registerPluginIOInterface(PluginProcessServices &Services);

llvm::Expected<llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem>>
createPluginFileSystem(
    PluginTaskContext &Task,
    llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> BaseFileSystem);

llvm::Error bindPluginOutputStream(PluginTaskContext &Task,
                                   NevercOutputStream Stream,
                                   llvm::raw_ostream &Output);

llvm::Expected<NevercOutputSummary>
hostCommitPluginOutput(PluginTaskContext &Task,
                       NevercOutputSealHandle Seal);

llvm::Expected<NevercOutputSummary>
hostAbortPluginOutput(PluginTaskContext &Task,
                      NevercOutputSealHandle Seal);

llvm::Expected<PluginOutputSealSnapshot>
inspectPluginOutputSeal(PluginTaskContext &Task,
                        NevercOutputSealHandle Seal);

llvm::Expected<std::string>
canonicalizePluginOutputPath(PluginTaskContext &Task,
                             llvm::StringRef Path);

std::optional<PluginMemoryOutputSnapshot>
findPluginMemoryOutput(PluginTaskContext &Task,
                       llvm::StringRef LogicalName);

std::vector<PluginDependencySnapshot>
getPluginDependencies(PluginTaskContext &Task);

} // namespace neverc::plugin

#endif
