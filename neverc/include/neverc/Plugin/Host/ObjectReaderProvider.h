#ifndef NEVERC_PLUGIN_HOST_OBJECTREADERPROVIDER_H
#define NEVERC_PLUGIN_HOST_OBJECTREADERPROVIDER_H

#include "neverc/Plugin/Host/ObjectGraph.h"
#include "neverc/Plugin/Host/PluginTargetRegistry.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <memory>
#include <optional>
#include <vector>

namespace neverc::plugin {

class PluginTaskContext;

struct ObjectProbeMatch {
  const PluginTargetSnapshot::ObjectFormatRecord *Format = nullptr;
  uint32_t Confidence = 0;
  NevercObjectArtifactKind ArtifactKind = NEVERC_OBJECT_ARTIFACT_UNKNOWN;
  uint64_t ConsumedMinimum = 0;
};

class ObjectFormatRegistry {
public:
  static llvm::Expected<std::unique_ptr<ObjectFormatRegistry>>
  create(std::shared_ptr<const PluginTargetSnapshot> Snapshot);

  const PluginTargetSnapshot::ObjectFormatRecord *
  find(NevercObjectFormatID ID) const;

  llvm::Expected<ObjectProbeMatch>
  probe(PluginTaskContext &Task, llvm::ArrayRef<uint8_t> Input,
        llvm::StringRef LogicalPath, NevercTargetKey Target,
        std::optional<NevercObjectFormatID> RequiredFormat =
            std::nullopt) const;

  llvm::ArrayRef<PluginTargetSnapshot::ObjectFormatRecord> formats() const {
    return Formats;
  }

private:
  explicit ObjectFormatRegistry(
      std::shared_ptr<const PluginTargetSnapshot> SnapshotValue)
      : Snapshot(std::move(SnapshotValue)) {}

  std::shared_ptr<const PluginTargetSnapshot> Snapshot;
  std::vector<PluginTargetSnapshot::ObjectFormatRecord> Formats;
};

class ObjectReaderProvider {
public:
  static llvm::Expected<std::unique_ptr<ObjectReaderProvider>>
  create(std::shared_ptr<const PluginTargetSnapshot> Snapshot);

  llvm::Expected<std::unique_ptr<PluginObjectGraph>>
  read(PluginTaskContext &Task, llvm::ArrayRef<uint8_t> Input,
       llvm::StringRef LogicalPath, const OwnedTargetKey &Target,
       std::optional<NevercObjectFormatID> RequiredFormat =
           std::nullopt) const;

  const ObjectFormatRegistry &registry() const { return *Registry; }

private:
  explicit ObjectReaderProvider(
      std::unique_ptr<ObjectFormatRegistry> RegistryValue)
      : Registry(std::move(RegistryValue)) {}

  std::unique_ptr<ObjectFormatRegistry> Registry;
};

void appendBuiltinLLVMObjectFormats(
    std::vector<PluginTargetSnapshot::ObjectFormatRecord> &Formats);

} // namespace neverc::plugin

#endif
