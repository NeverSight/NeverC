#ifndef NEVERC_FOUNDATION_DARWINSDKINFO_H
#define NEVERC_FOUNDATION_DARWINSDKINFO_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/VersionTuple.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/TargetParser/Triple.h"
#include <optional>

namespace llvm {
namespace json {
class Object;
} // end namespace json
} // end namespace llvm

namespace neverc {

class DarwinSDKInfo {
public:
  struct OSEnvPair {
  public:
    using StorageType = uint64_t;

    constexpr OSEnvPair(llvm::Triple::OSType FromOS,
                        llvm::Triple::EnvironmentType FromEnv,
                        llvm::Triple::OSType ToOS,
                        llvm::Triple::EnvironmentType ToEnv)
        : Value(((StorageType(FromOS) * StorageType(llvm::Triple::LastOSType) +
                  StorageType(FromEnv))
                 << 32ull) |
                (StorageType(ToOS) * StorageType(llvm::Triple::LastOSType) +
                 StorageType(ToEnv))) {}

  private:
    StorageType Value;

    friend class DarwinSDKInfo;
  };

  class RelatedTargetVersionMapping {
  public:
    RelatedTargetVersionMapping(
        llvm::VersionTuple MinimumKeyVersion,
        llvm::VersionTuple MaximumKeyVersion, llvm::VersionTuple MinimumValue,
        llvm::VersionTuple MaximumValue,
        llvm::DenseMap<llvm::VersionTuple, llvm::VersionTuple> Mapping)
        : MinimumKeyVersion(MinimumKeyVersion),
          MaximumKeyVersion(MaximumKeyVersion), MinimumValue(MinimumValue),
          MaximumValue(MaximumValue), Mapping(Mapping) {
      assert(!this->Mapping.empty() && "unexpected empty mapping");
    }

    /// Returns the value with the lowest version in the mapping.
    const llvm::VersionTuple &getMinimumValue() const { return MinimumValue; }

    /// Returns the mapped key, or the appropriate Minimum / MaximumValue if
    /// they key is outside of the mapping bounds. If they key isn't mapped, but
    /// within the minimum and maximum bounds, std::nullopt is returned.
    std::optional<llvm::VersionTuple>
    map(const llvm::VersionTuple &Key, const llvm::VersionTuple &MinimumValue,
        std::optional<llvm::VersionTuple> MaximumValue) const;

    static std::optional<RelatedTargetVersionMapping>
    parseJSON(const llvm::json::Object &Obj,
              llvm::VersionTuple MaximumDeploymentTarget);

  private:
    llvm::VersionTuple MinimumKeyVersion;
    llvm::VersionTuple MaximumKeyVersion;
    llvm::VersionTuple MinimumValue;
    llvm::VersionTuple MaximumValue;
    llvm::DenseMap<llvm::VersionTuple, llvm::VersionTuple> Mapping;
  };

  DarwinSDKInfo(
      llvm::VersionTuple Version, llvm::VersionTuple MaximumDeploymentTarget,
      llvm::DenseMap<OSEnvPair::StorageType,
                     std::optional<RelatedTargetVersionMapping>>
          VersionMappings =
              llvm::DenseMap<OSEnvPair::StorageType,
                             std::optional<RelatedTargetVersionMapping>>())
      : Version(Version), MaximumDeploymentTarget(MaximumDeploymentTarget),
        VersionMappings(std::move(VersionMappings)) {}

  const llvm::VersionTuple &getVersion() const { return Version; }

  // Returns the optional, target-specific version mapping that maps from one
  // target to another target.
  //
  // This mapping is constructed from an appropriate mapping in the SDKSettings,
  // for instance, when building for Mac Catalyst, the mapping would contain the
  // "macOS_iOSMac" mapping as it maps the macOS versions to the Mac Catalyst
  // versions.
  //
  // This mapping does not exist when the target doesn't have an appropriate
  // related version mapping, or when there was an error reading the mapping
  // from the SDKSettings, or when it's missing in the SDKSettings.
  const RelatedTargetVersionMapping *getVersionMapping(OSEnvPair Kind) const {
    auto Mapping = VersionMappings.find(Kind.Value);
    if (Mapping == VersionMappings.end())
      return nullptr;
    return Mapping->getSecond() ? &*Mapping->getSecond() : nullptr;
  }

  static std::optional<DarwinSDKInfo>
  parseDarwinSDKSettingsJSON(const llvm::json::Object *Obj);

private:
  llvm::VersionTuple Version;
  llvm::VersionTuple MaximumDeploymentTarget;
  // Need to wrap the value in an optional here as the value has to be default
  // constructible, and std::unique_ptr doesn't like DarwinSDKInfo being
  // Optional as Optional is trying to copy it in emplace.
  llvm::DenseMap<OSEnvPair::StorageType,
                 std::optional<RelatedTargetVersionMapping>>
      VersionMappings;
};

llvm::Expected<std::optional<DarwinSDKInfo>>
parseDarwinSDKInfo(llvm::vfs::FileSystem &VFS, llvm::StringRef SDKRootPath);

} // end namespace neverc

#endif // NEVERC_FOUNDATION_DARWINSDKINFO_H
