#ifndef NEVERC_LIB_PLUGIN_IO_PLUGINFILESYSTEM_H
#define NEVERC_LIB_PLUGIN_IO_PLUGINFILESYSTEM_H

#include "neverc/Foundation/Core/OutputCoordinator.h"
#include "neverc/Plugin/Host/PluginIOBridge.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/VirtualFileSystem.h"
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace neverc::plugin {

class PluginProcessServices;
class PluginOutputState;
class PluginSession;
class PluginTaskContext;

struct PluginVFSProviderBinding {
  std::string PluginID;
  std::string ProviderID;
  std::string RoutePrefix;
  NevercVFSProviderDescriptor Descriptor{};
};

class PluginIOProcessBridge;

class PluginVFSView
    : public std::enable_shared_from_this<PluginVFSView> {
public:
  PluginVFSView(
      PluginIOProcessBridge &Owner, NevercTaskHandle Task,
      llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> BaseFileSystem,
      std::vector<PluginVFSProviderBinding> Providers);

  llvm::ErrorOr<llvm::vfs::Status> status(llvm::StringRef Path);
  llvm::ErrorOr<std::unique_ptr<llvm::vfs::File>>
  openFileForRead(llvm::StringRef Path);
  llvm::vfs::directory_iterator dirBegin(llvm::StringRef Path,
                                          std::error_code &Error);
  llvm::ErrorOr<llvm::SmallString<256>> getCurrentWorkingDirectory() const;
  std::error_code setCurrentWorkingDirectory(llvm::StringRef Path);
  std::error_code getRealPath(llvm::StringRef Path,
                              llvm::SmallVectorImpl<char> &Output) const;
  std::error_code isLocal(llvm::StringRef Path, bool &Result);

  PluginTaskContext *task() const;
  NevercTaskHandle taskHandle() const { return Task; }
  llvm::vfs::FileSystem &baseFileSystem() const { return *BaseFileSystem; }

private:
  PluginIOProcessBridge &Owner;
  NevercTaskHandle Task{};
  llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> BaseFileSystem;
  std::vector<PluginVFSProviderBinding> Providers;
  mutable std::mutex LocalityMutex;
  std::map<std::string, bool> ProviderLocality;
};

class PluginFileSystem final : public llvm::vfs::FileSystem {
public:
  explicit PluginFileSystem(std::shared_ptr<PluginVFSView> View);

  llvm::ErrorOr<llvm::vfs::Status>
  status(const llvm::Twine &Path) override;
  llvm::ErrorOr<std::unique_ptr<llvm::vfs::File>>
  openFileForRead(const llvm::Twine &Path) override;
  llvm::vfs::directory_iterator
  dir_begin(const llvm::Twine &Dir, std::error_code &Error) override;
  llvm::ErrorOr<llvm::SmallString<256>>
  getCurrentWorkingDirectory() const override;
  std::error_code
  setCurrentWorkingDirectory(const llvm::Twine &Path) override;
  std::error_code
  getRealPath(const llvm::Twine &Path,
              llvm::SmallVectorImpl<char> &Output) const override;
  std::error_code isLocal(const llvm::Twine &Path, bool &Result) override;

private:
  std::shared_ptr<PluginVFSView> View;
};

class PluginIOProcessBridge
    : public PluginHostService,
      public std::enable_shared_from_this<PluginIOProcessBridge> {
public:
  explicit PluginIOProcessBridge(PluginProcessServices &Services);

  const NevercIOAPI &api() const { return API; }
  PluginProcessServices &services() const { return Services; }

  llvm::Expected<std::shared_ptr<PluginVFSView>>
  createView(PluginTaskContext &Task,
             llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> BaseFileSystem);
  std::shared_ptr<PluginVFSView> findView(NevercTaskHandle Task) const;

  NevercStatus addMemoryFile(NevercSessionHandle Session,
                             NevercStringView Path, NevercByteView Content,
                             int64_t ModificationTime);
  llvm::Expected<std::shared_ptr<PluginOutputState>>
  createMemoryOutput(PluginTaskContext &Task, llvm::StringRef LogicalName,
                     uint64_t SizeBudget);
  llvm::Expected<std::shared_ptr<PluginOutputState>>
  createFileOutput(PluginTaskContext &Task, llvm::StringRef FinalPath,
                   uint64_t SizeBudget);
  llvm::Expected<std::shared_ptr<PluginOutputState>>
  createStreamOutput(PluginTaskContext &Task, NevercOutputStream Stream,
                     uint64_t SizeBudget);
  llvm::Error bindOutputStream(PluginTaskContext &Task,
                               NevercOutputStream Stream,
                               llvm::raw_ostream &Output);
  llvm::Expected<std::string>
  canonicalizeOutputPath(llvm::StringRef Path) const;
  void recordDependency(PluginTaskContext &Task,
                        std::shared_ptr<PluginDependencySnapshot> Dependency);
  std::vector<PluginDependencySnapshot>
  dependencies(NevercTaskHandle Task) const;
  std::shared_ptr<PluginOutputState>
  findMemoryOutput(NevercTaskHandle Task,
                   llvm::StringRef LogicalName) const;
  llvm::Error
  taskScopeEnding(NevercTaskHandle Task) override;
  void taskScopeUnregistered(NevercTaskHandle Task) noexcept override;

private:
  struct MemoryFile {
    std::string Path;
    std::vector<char> Content;
    int64_t ModificationTime = 0;
  };

  llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem>
  buildBaseView(PluginSession &Session,
                llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> Base);
  std::vector<PluginVFSProviderBinding>
  collectProviders(PluginSession &Session) const;

  PluginProcessServices &Services;
  OutputCoordinator &Outputs;
  NevercIOAPI API{};
  mutable std::mutex Mutex;
  std::map<std::pair<uint64_t, uint64_t>, std::shared_ptr<PluginVFSView>>
      TaskViews;
  std::map<uint64_t, std::map<std::string, MemoryFile>> SessionMemoryFiles;
  std::map<std::pair<uint64_t, uint64_t>,
           std::map<std::string, std::shared_ptr<PluginOutputState>>>
      TaskMemoryOutputs;
  std::map<std::pair<uint64_t, uint64_t>,
           std::vector<std::shared_ptr<PluginOutputState>>>
      TaskOutputs;
  std::map<std::pair<uint64_t, uint64_t>,
           std::map<NevercOutputStream, llvm::raw_ostream *>>
      TaskOutputStreams;
  std::map<std::pair<uint64_t, uint64_t>,
           std::vector<std::shared_ptr<PluginDependencySnapshot>>>
      TaskDependencies;

  friend void initializePluginIOAPI(NevercIOAPI &API,
                                    PluginIOProcessBridge &Bridge);
};

std::shared_ptr<PluginIOProcessBridge>
findPluginIOProcessBridge(PluginProcessServices &Services);
void initializePluginIOAPI(NevercIOAPI &API,
                           PluginIOProcessBridge &Bridge);

} // namespace neverc::plugin

#endif
