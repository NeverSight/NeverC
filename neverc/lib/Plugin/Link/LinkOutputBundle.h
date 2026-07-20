#ifndef NEVERC_PLUGIN_LINK_LINKOUTPUTBUNDLE_H
#define NEVERC_PLUGIN_LINK_LINKOUTPUTBUNDLE_H

#include "BinaryImage.h"
#include "neverc/Foundation/Core/OutputBundleTransaction.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Error.h"
#include <memory>
#include <string>
#include <vector>

namespace neverc::plugin {

class PluginTaskContext;

struct PluginLinkSideOutput {
  std::string Name;
  std::string Path;
  std::vector<uint8_t> Bytes;
};

struct LinkOutputResult {
  std::shared_ptr<PluginBinaryImage> Image;
  std::shared_ptr<class LinkOutputBundle> Bundle;
  neverc::OutputBundleSummary Summary;
};

class LinkOutputBundle {
public:
  static llvm::Expected<std::shared_ptr<LinkOutputBundle>>
  create(PluginTaskContext &Task, neverc::OutputCoordinator &Coordinator,
         std::shared_ptr<PluginBinaryImage> Image,
         llvm::StringRef MainPath,
         llvm::ArrayRef<PluginLinkSideOutput> SideOutputs = {},
         neverc::OutputBundleTransaction::FaultInjector InjectFault = {});

  ~LinkOutputBundle();

  NevercOutputBundleHandle handle() const { return Handle; }
  llvm::Error verifyAndPrepare();
  llvm::Expected<neverc::OutputBundleSummary> commit();
  llvm::Error abort();
  neverc::OutputBundleSummary summary() const;

private:
  LinkOutputBundle(PluginTaskContext &Task,
                   std::shared_ptr<PluginBinaryImage> Image,
                   std::unique_ptr<neverc::OutputBundleTransaction>
                       Transaction);
  llvm::Error initializeHandle();

  PluginTaskContext &Task;
  std::shared_ptr<PluginBinaryImage> Image;
  std::unique_ptr<neverc::OutputBundleTransaction> Transaction;
  NevercOutputBundleHandle Handle{};
};

class LinkOutputPipeline {
public:
  static llvm::Expected<std::unique_ptr<LinkOutputPipeline>>
  create(PluginTaskContext &Task,
         neverc::OutputCoordinator &Coordinator);
  ~LinkOutputPipeline();

  LinkOutputPipeline(const LinkOutputPipeline &) = delete;
  LinkOutputPipeline &operator=(const LinkOutputPipeline &) = delete;

  llvm::Error addObserver(
      llvm::StringRef PluginID,
      const NevercObserverDescriptor &Descriptor);
  llvm::Error addInterceptor(
      llvm::StringRef PluginID,
      const NevercInterceptorDescriptor &Descriptor);
  llvm::Error freeze();

  llvm::Expected<LinkOutputResult>
  execute(std::shared_ptr<PluginBinaryImage> Image,
          llvm::StringRef MainPath,
          llvm::ArrayRef<PluginLinkSideOutput> SideOutputs = {},
          neverc::OutputBundleTransaction::FaultInjector InjectFault = {});

private:
  struct Impl;
  explicit LinkOutputPipeline(std::unique_ptr<Impl> State);
  std::unique_ptr<Impl> State;
};

} // namespace neverc::plugin

#endif
