#ifndef NEVERC_PLUGIN_HOST_OBJECTWRITERPROVIDER_H
#define NEVERC_PLUGIN_HOST_OBJECTWRITERPROVIDER_H

#include "neverc/Plugin/Host/ObjectImage.h"
#include "neverc/Plugin/Host/ObjectReaderProvider.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <memory>
#include <string>

namespace neverc::plugin {

class PluginTaskContext;

enum class ObjectOutputDestinationKind : uint8_t {
  Memory,
  File,
};

enum class ObjectWritePolicy : uint8_t {
  Default,
  // Preserve graph symbols while canonicalizing ELF table ownership/indices.
  CanonicalELFTables,
  // Finalize serialized Android ELF symbols and replay structural names.
  AndroidKernelRelease,
};

struct ObjectOutputDestination {
  ObjectOutputDestinationKind Kind = ObjectOutputDestinationKind::Memory;
  std::string Name;
  uint64_t SizeBudget = 0;
  // Graph-writer policy only. beginImage stages authoritative native bytes
  // verbatim; semantic validators remain responsible for auditing that image.
  ObjectWritePolicy WritePolicy = ObjectWritePolicy::Default;
  bool DropDebugInfo = false;

  static ObjectOutputDestination memory(llvm::StringRef LogicalName,
                                        uint64_t SizeBudget);
  static ObjectOutputDestination file(llvm::StringRef FinalPath,
                                      uint64_t SizeBudget);
};

class ObjectWriterProvider {
public:
  static llvm::Expected<std::unique_ptr<ObjectWriterProvider>>
  create(std::shared_ptr<const PluginTargetSnapshot> Snapshot);

  llvm::Expected<std::unique_ptr<PluginObjectImage>>
  beginWrite(PluginTaskContext &Task, PluginObjectGraph &Graph,
             const ObjectOutputDestination &Destination) const;
  llvm::Expected<std::unique_ptr<PluginObjectImage>>
  beginImage(PluginTaskContext &Task, NevercObjectFormatID FormatID,
             NevercTargetID TargetID, uint64_t GraphGeneration,
             llvm::ArrayRef<uint8_t> Bytes,
             const ObjectOutputDestination &Destination) const;
  llvm::Expected<std::unique_ptr<PluginObjectImage>>
  write(PluginTaskContext &Task, PluginObjectGraph &Graph,
        const ObjectOutputDestination &Destination) const;

  /// True when graph serialization for this format would execute a plugin
  /// callback rather than the host-owned built-in writer.
  bool hasPluginOwnedGraphWriter(NevercObjectFormatID FormatID) const;

  const ObjectFormatRegistry &registry() const { return *Registry; }

private:
  explicit ObjectWriterProvider(
      std::unique_ptr<ObjectFormatRegistry> RegistryValue)
      : Registry(std::move(RegistryValue)) {}

  std::unique_ptr<ObjectFormatRegistry> Registry;
};

} // namespace neverc::plugin

#endif
