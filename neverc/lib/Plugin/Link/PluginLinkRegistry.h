#ifndef NEVERC_LIB_PLUGIN_LINK_PLUGINLINKREGISTRY_H
#define NEVERC_LIB_PLUGIN_LINK_PLUGINLINKREGISTRY_H

#include "LinkRequest.h"
#include "neverc/Plugin/Host/PluginRegistry.h"
#include "neverc/Plugin/PluginLTO.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <memory>
#include <string>
#include <vector>

namespace neverc::plugin {

class PluginModule;
class PluginProcessServices;

struct PluginLinkRegistrationView {
  llvm::StringRef PluginID;
  std::shared_ptr<const PluginModule> Owner;
  llvm::ArrayRef<NevercLinkerProviderDescriptor> LinkerProviders;
  llvm::ArrayRef<NevercObjectMergeProviderDescriptor> ObjectMergeProviders;
  llvm::ArrayRef<NevercBinaryImageVerifierDescriptor> ImageVerifiers;
  llvm::ArrayRef<NevercLTOProviderDescriptor> LTOProviders;
};

class PluginLinkSnapshot {
public:
  struct LinkerProviderRecord {
    std::string PluginID;
    std::shared_ptr<const PluginModule> Owner;
    std::string ProviderID;
    NevercTargetID TargetID{};
    NevercObjectFormatID InputFormat{};
    NevercObjectFormatID OutputFormat{};
    NevercLinkOutputKind OutputKind = 0;
    NevercLinkProviderFlags Flags = 0;
    std::string CompatibilityKey;
    NevercInterfaceID ProductID{};
    NevercLinkerProviderFn Link = nullptr;
    NevercBinaryImageVerifierFn VerifyImage = nullptr;
    void *UserData = nullptr;
    bool Builtin = false;
  };

  struct ObjectMergeProviderRecord {
    std::string PluginID;
    std::shared_ptr<const PluginModule> Owner;
    std::string ProviderID;
    NevercTargetID TargetID{};
    NevercObjectFormatID FormatID{};
    NevercLinkProviderFlags Flags = 0;
    std::string CompatibilityKey;
    NevercInterfaceID ProductID{};
    NevercObjectMergeProviderFn Merge = nullptr;
    void *UserData = nullptr;
    bool Builtin = false;
  };

  struct ImageVerifierRecord {
    std::string PluginID;
    std::shared_ptr<const PluginModule> Owner;
    std::string VerifierID;
    NevercTargetID TargetID{};
    NevercObjectFormatID FormatID{};
    NevercLinkOutputKind OutputKind = 0;
    NevercBinaryImageVerifierFn Verify = nullptr;
    void *UserData = nullptr;
  };

  struct LTOProviderRecord {
    std::string PluginID;
    std::shared_ptr<const PluginModule> Owner;
    std::string ProviderID;
    NevercTargetID TargetID{};
    NevercLTOProviderFlags Flags = 0;
    std::string CompatibilityKey;
    NevercInterfaceID ProductID{};
    NevercLTOBuildCacheKeyFn BuildCacheKey = nullptr;
    NevercLTOCodegenFn Codegen = nullptr;
    void *UserData = nullptr;
  };

  llvm::ArrayRef<LinkerProviderRecord> linkerProviders() const {
    return LinkerProviders;
  }
  llvm::ArrayRef<ObjectMergeProviderRecord> objectMergeProviders() const {
    return ObjectMergeProviders;
  }
  llvm::ArrayRef<ImageVerifierRecord> imageVerifiers() const {
    return ImageVerifiers;
  }
  llvm::ArrayRef<LTOProviderRecord> ltoProviders() const {
    return LTOProviders;
  }

private:
  std::vector<LinkerProviderRecord> LinkerProviders;
  std::vector<ObjectMergeProviderRecord> ObjectMergeProviders;
  std::vector<ImageVerifierRecord> ImageVerifiers;
  std::vector<LTOProviderRecord> LTOProviders;

  friend class PluginLinkRegistry;
  friend struct PluginLinkSnapshotAccess;
};

class PluginLinkRegistry {
public:
  static llvm::Expected<std::shared_ptr<const PluginLinkSnapshot>>
  freeze(llvm::ArrayRef<PluginLinkRegistrationView> Registrations);
  static llvm::Expected<std::shared_ptr<const PluginLinkSnapshot>>
  freeze(llvm::ArrayRef<std::shared_ptr<const PluginModule>> Modules);
};

struct LinkRouteRequest {
  NevercTargetID TargetID{};
  NevercObjectFormatID InputFormat{};
  NevercObjectFormatID OutputFormat{};
  NevercLinkOutputKind OutputKind = 0;
  std::string CompatibilityKey;
  std::string ForcedProvider;
};

class PlannedLinkRoute {
public:
  enum class Kind { FullLink, ObjectMerge };

  Kind kind() const { return RouteKind; }
  const PluginLinkSnapshot::LinkerProviderRecord *linkerProvider() const {
    return LinkerProvider;
  }
  const PluginLinkSnapshot::ObjectMergeProviderRecord *
  objectMergeProvider() const {
    return ObjectMergeProvider;
  }
  unsigned specificity() const { return Specificity; }

private:
  Kind RouteKind = Kind::FullLink;
  const PluginLinkSnapshot::LinkerProviderRecord *LinkerProvider = nullptr;
  const PluginLinkSnapshot::ObjectMergeProviderRecord *ObjectMergeProvider =
      nullptr;
  unsigned Specificity = 0;

  friend class LinkRoutePlanner;
};

class LinkRoutePlanner {
public:
  static llvm::Expected<PlannedLinkRoute>
  plan(llvm::ArrayRef<PluginLinkSnapshot::LinkerProviderRecord> Linkers,
       llvm::ArrayRef<PluginLinkSnapshot::ObjectMergeProviderRecord> Mergers,
       const LinkRouteRequest &Request);
  static llvm::Expected<PlannedLinkRoute>
  plan(const PluginLinkSnapshot &Snapshot,
       const LinkRouteRequest &Request);
};

} // namespace neverc::plugin

#endif
