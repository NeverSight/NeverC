#ifndef NEVERC_LIB_INVOKE_PLUGIN_PLUGINBOOTSTRAP_H
#define NEVERC_LIB_INVOKE_PLUGIN_PLUGINBOOTSTRAP_H

#include "neverc/Plugin/Host/PluginOptionRegistry.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace llvm::opt {
class OptTable;
}

namespace neverc::plugin {
class PluginActivationPlan;
class PluginProcessServices;
class PluginSession;
} // namespace neverc::plugin

namespace neverc::driver {

enum class PluginArgumentOrigin : uint8_t {
  Configuration,
  CommandLine,
};

struct PluginBootstrapToken {
  std::string Value;
  PluginArgumentOrigin Origin = PluginArgumentOrigin::CommandLine;
  std::string Source;
  uint64_t Position = 0;
};

class PluginBootstrap {
public:
  using InterfaceInitializer =
      std::function<llvm::Error(plugin::PluginProcessServices &)>;

  PluginBootstrap(std::string HostBuildID, uint32_t LLVMMajor,
                  std::vector<std::string> StaticOptionSpellings);
  ~PluginBootstrap();

  PluginBootstrap(const PluginBootstrap &) = delete;
  PluginBootstrap &operator=(const PluginBootstrap &) = delete;

  llvm::Error discoverAndActivate(
      llvm::ArrayRef<PluginBootstrapToken> Tokens,
      InterfaceInitializer InitializeInterfaces = {});
  llvm::Expected<plugin::PluginOptionParseResult>
  parsePluginOptions(llvm::ArrayRef<llvm::StringRef> Arguments,
                     llvm::StringRef TargetTriple = "") const;
  llvm::Expected<std::unique_ptr<plugin::PluginSession>>
  createSession(plugin::PluginOptionParseResult Options);
  llvm::Error shutdown();

  bool isActive() const { return Services != nullptr; }
  plugin::PluginProcessServices *services() { return Services.get(); }
  const plugin::PluginProcessServices *services() const {
    return Services.get();
  }
  llvm::ArrayRef<std::string> pluginIDs() const {
    return LoadedPluginIDs;
  }

  static bool isReservedBootstrapToken(llvm::StringRef Token);
  static std::vector<std::string>
  collectStaticOptionSpellings(const llvm::opt::OptTable &Table);

private:
  std::string HostBuildID;
  uint32_t LLVMMajor = 0;
  std::vector<std::string> StaticOptionSpellings;
  std::unique_ptr<plugin::PluginProcessServices> Services;
  std::unique_ptr<plugin::PluginActivationPlan> Plan;
  std::vector<std::string> LoadedPluginIDs;
  bool Activated = false;
};

} // namespace neverc::driver

#endif
