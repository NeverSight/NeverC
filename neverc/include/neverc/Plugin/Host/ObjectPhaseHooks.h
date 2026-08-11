#ifndef NEVERC_PLUGIN_HOST_OBJECTPHASEHOOKS_H
#define NEVERC_PLUGIN_HOST_OBJECTPHASEHOOKS_H

#include "neverc/Plugin/Host/ObjectWriterProvider.h"
#include "neverc/Plugin/PluginCore.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <functional>
#include <memory>

namespace neverc::plugin {

class PluginProcessServices;
class PluginTaskContext;

using ObjectImageSemanticValidator =
    std::function<llvm::Error(llvm::ArrayRef<uint8_t>)>;
using ObjectImageSemanticValidatorFactory =
    std::function<llvm::Expected<ObjectImageSemanticValidator>(
        llvm::ArrayRef<uint8_t>)>;

/// Optional host-owned invariants that plugins may preserve but never weaken.
/// Graph validation runs after every mutable graph phase. A pre/post-write
/// factory binds one immutable baseline after the complete write phase and its
/// returned validator runs after the complete post-write phase. Final image
/// validation then runs before the output is sealed.
struct ObjectPhaseSemanticValidators {
  std::function<llvm::Error(const PluginObjectGraph &)> Graph;
  ObjectImageSemanticValidatorFactory BindPrePostWriteImage;
  ObjectImageSemanticValidator Image;
};

class ObjectPhasePipeline {
public:
  static llvm::Expected<std::unique_ptr<ObjectPhasePipeline>>
  create(PluginTaskContext &Task,
         std::shared_ptr<const PluginTargetSnapshot> Snapshot);

  ~ObjectPhasePipeline();

  ObjectPhasePipeline(const ObjectPhasePipeline &) = delete;
  ObjectPhasePipeline &operator=(const ObjectPhasePipeline &) = delete;

  llvm::Error addObserver(llvm::StringRef PluginID,
                          const NevercObserverDescriptor &Descriptor);
  llvm::Error addInterceptor(llvm::StringRef PluginID,
                             const NevercInterceptorDescriptor &Descriptor);
  bool hasPluginBindings() const;
  bool hasInterceptors() const;
  /// Interceptors are phase-wide; providers are filtered by the actual route.
  bool mayReplaceArtifact(NevercTargetKey Target,
                          NevercObjectFormatID FormatID) const;
  /// Interceptors are phase-wide; providers are filtered by the actual route.
  bool mayReplaceWriteArtifact(NevercTargetKey Target,
                               NevercObjectFormatID FormatID) const;
  bool hasPluginOwnedGraphWriter(NevercObjectFormatID FormatID) const;
  llvm::Error freeze();

  llvm::Expected<std::shared_ptr<PluginObjectImage>>
  execute(const PluginObjectGraph &Graph,
          const ObjectOutputDestination &Destination);
  llvm::Expected<std::shared_ptr<PluginObjectImage>>
  executeNative(const PluginObjectGraph &Graph,
                llvm::ArrayRef<uint8_t> NativeImage,
                const ObjectOutputDestination &Destination);
  llvm::Expected<std::shared_ptr<PluginObjectImage>>
  executeNative(const PluginObjectGraph &Graph,
                llvm::ArrayRef<uint8_t> NativeImage,
                const ObjectOutputDestination &Destination,
                ObjectPhaseSemanticValidators Validators);
  llvm::Expected<std::shared_ptr<PluginObjectImage>>
  verifyAndCommitFinished(NevercTargetKey Target,
                          std::shared_ptr<PluginObjectImage> Image);

private:
  struct Impl;
  explicit ObjectPhasePipeline(std::unique_ptr<Impl> State);
  std::unique_ptr<Impl> State;
};

llvm::Error registerPluginObjectPhaseInterface(PluginProcessServices &Services);

} // namespace neverc::plugin

#endif
