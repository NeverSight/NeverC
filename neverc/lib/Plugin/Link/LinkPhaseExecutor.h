#ifndef NEVERC_PLUGIN_LINK_LINKPHASEEXECUTOR_H
#define NEVERC_PLUGIN_LINK_LINKPHASEEXECUTOR_H

#include "LinkGraph.h"
#include "LinkPhaseRegistry.h"
#include "neverc/Plugin/Host/PluginPhaseExecutor.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <functional>
#include <memory>

namespace neverc::plugin {

class PluginTaskContext;

class LinkPhasePipeline {
public:
  using BuiltinGraphProvider = std::function<
      llvm::Expected<std::shared_ptr<PluginLinkGraph>>(
          const PluginLinkGraph &)>;

  static llvm::Expected<std::unique_ptr<LinkPhasePipeline>>
  create(PluginTaskContext &Task);
  ~LinkPhasePipeline();

  LinkPhasePipeline(const LinkPhasePipeline &) = delete;
  LinkPhasePipeline &operator=(const LinkPhasePipeline &) = delete;

  llvm::Error addObserver(llvm::StringRef PluginID,
                          const NevercObserverDescriptor &Descriptor);
  llvm::Error addInterceptor(
      llvm::StringRef PluginID,
      const NevercInterceptorDescriptor &Descriptor);
  llvm::Error addProvider(llvm::StringRef PluginID,
                          const NevercProviderDescriptor &Descriptor);
  llvm::Error selectProvider(NevercInterfaceID Phase,
                             llvm::StringRef PluginID);
  llvm::Error setBuiltinProvider(
      NevercInterfaceID Phase,
      PluginPhaseExecutor::BuiltinProvider Provider);
  llvm::Error setBuiltinGraphProvider(
      NevercInterfaceID Phase, BuiltinGraphProvider Provider);
  llvm::Error freeze();

  llvm::Expected<std::shared_ptr<PluginLinkGraph>>
  execute(std::shared_ptr<PluginLinkGraph> Input,
          NevercLinkState ThroughState =
              NEVERC_LINK_STATE_IMAGE_EMITTED);

  const LinkPhaseRegistry &registry() const;
  uint32_t rerunCount(NevercInterfaceID Phase) const;

private:
  struct Impl;
  explicit LinkPhasePipeline(std::unique_ptr<Impl> State);
  std::unique_ptr<Impl> State;
};

} // namespace neverc::plugin

#endif
