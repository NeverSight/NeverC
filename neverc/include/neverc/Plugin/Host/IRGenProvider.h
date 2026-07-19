#ifndef NEVERC_PLUGIN_HOST_IRGENPROVIDER_H
#define NEVERC_PLUGIN_HOST_IRGENPROVIDER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <functional>
#include <memory>

namespace llvm {
class LLVMContext;
class Module;
}

namespace neverc::plugin {

class PluginProcessServices;
class PluginSourcePhaseRuntime;
class PluginTaskContext;

class PluginIRGenProviderRuntime {
public:
  struct Impl;
  using BuiltinGenerator =
      std::function<llvm::Expected<llvm::Module *>()>;

  static llvm::Expected<std::unique_ptr<PluginIRGenProviderRuntime>>
  create(PluginTaskContext &Task, llvm::LLVMContext &Context,
         llvm::StringRef TargetTriple, llvm::StringRef DataLayout,
         BuiltinGenerator GenerateBuiltin);
  ~PluginIRGenProviderRuntime();

  PluginIRGenProviderRuntime(const PluginIRGenProviderRuntime &) = delete;
  PluginIRGenProviderRuntime &
  operator=(const PluginIRGenProviderRuntime &) = delete;

  llvm::Error execute(const PluginSourcePhaseRuntime &SourcePhases);
  llvm::Module *module() const;
  bool ownsModule() const;
  std::unique_ptr<llvm::Module> releaseOwnedModule();

private:
  explicit PluginIRGenProviderRuntime(std::unique_ptr<Impl> State);

  std::unique_ptr<Impl> State;
};

llvm::Error registerPluginIRInterface(PluginProcessServices &Services);

} // namespace neverc::plugin

#endif
