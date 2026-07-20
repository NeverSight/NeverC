#ifndef NEVERC_PLUGIN_LTO_LTOINPUTSET_H
#define NEVERC_PLUGIN_LTO_LTOINPUTSET_H

#include "../Link/LinkInputReader.h"
#include "../Link/LinkRequest.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/PluginLTO.h"
#include "llvm/Support/MemoryBuffer.h"
#include <array>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace llvm {
class ModuleSummaryIndex;
}

namespace neverc::plugin {

struct LTOInputSetOptions {
  NevercLTOMode Mode = NEVERC_LTO_FULL;
  NevercLTOCacheScope CacheScope = NEVERC_LTO_CACHE_DISABLED;
  NevercLTOOptionFlags Flags = NEVERC_LTO_OPTION_DETERMINISTIC;
  uint32_t OptimizationLevel = 2;
  uint32_t CodeGenOptimizationLevel = 2;
  uint32_t ThreadBudget = 0;
  uint32_t ThinBackendPartitions = 0;
  std::string CPU;
  std::string Features;
  std::string CacheNamespace;
};

struct LTOInputModuleRecord {
  NevercLTOInputModuleHandle Handle{};
  NevercLTOSummaryHandle SummaryHandle{};
  PluginLinkBitcodeModule *Module = nullptr;
  std::shared_ptr<llvm::ModuleSummaryIndex> Summary;
};

struct LTOSymbolResolutionRecord {
  NevercLTOResolutionHandle Handle{};
  NevercLTOInputModuleHandle Module{};
  NevercLinkSymbolHandle LinkSymbol{};
  std::string SymbolName;
  std::string ComdatName;
  std::string Version;
  NevercLTOSymbolResolutionFlags Flags = 0;
  bool Undefined = false;
};

class LTOInputSet;

class LTOProcessService final : public PluginHostService {
public:
  LTOProcessService();

  const NevercLTOAPI &api() const { return API; }
  llvm::Error attach(LTOInputSet &Runtime);
  void detach(NevercTaskHandle Task);
  void taskScopeUnregistered(NevercTaskHandle Task) noexcept override;

private:
  static NevercStatus NEVERC_CALL
  getRequest(void *Context, NevercTaskHandle Task,
             NevercLTORequestHandle Request,
             NevercLTORequest *OutRequest);
  static NevercStatus NEVERC_CALL
  getModulePage(void *Context, NevercTaskHandle Task,
                NevercLTORequestHandle Request, uint64_t Cursor,
                NevercLinkEntityPage *InOutPage);
  static NevercStatus NEVERC_CALL
  getResolutionPage(void *Context, NevercTaskHandle Task,
                    NevercLTORequestHandle Request, uint64_t Cursor,
                    NevercLinkEntityPage *InOutPage);
  static NevercStatus NEVERC_CALL
  getModuleInfo(void *Context, NevercTaskHandle Task,
                NevercLTOInputModuleHandle Module,
                NevercLTOInputModuleInfo *OutInfo);
  static NevercStatus NEVERC_CALL
  getResolutionInfo(void *Context, NevercTaskHandle Task,
                    NevercLTOResolutionHandle Resolution,
                    NevercLTOSymbolResolution *OutInfo);

  LTOInputSet *find(NevercTaskHandle Task);

  NevercLTOAPI API{};
  std::mutex Mutex;
  std::map<std::pair<uint64_t, uint64_t>, LTOInputSet *> Active;
};

class LTOInputSet {
public:
  static llvm::Expected<std::unique_ptr<LTOInputSet>>
  create(PluginTaskContext &Task,
         std::shared_ptr<const LinkRequest> Request,
         std::unique_ptr<LinkInputSet> Inputs,
         LTOInputSetOptions Options = {});
  ~LTOInputSet();

  LTOInputSet(const LTOInputSet &) = delete;
  LTOInputSet &operator=(const LTOInputSet &) = delete;

  NevercTaskHandle taskHandle() const;
  NevercLTORequestHandle requestHandle() const {
    return RequestHandle;
  }
  const LinkRequest &request() const { return *Request; }
  LinkInputSet &inputs() const { return *Inputs; }
  llvm::ArrayRef<LTOInputModuleRecord> modules() const {
    return Modules;
  }
  llvm::ArrayRef<LTOSymbolResolutionRecord> resolutions() const {
    return Resolutions;
  }
  llvm::ArrayRef<uint8_t> resolutionDigest() const {
    return ResolutionDigest;
  }

  llvm::Error verify() const;
  llvm::Error setResolutionFlags(
      NevercLTOResolutionHandle Resolution,
      NevercLTOSymbolResolutionFlags Flags);

private:
  LTOInputSet(PluginTaskContext &Task,
              std::shared_ptr<const LinkRequest> Request,
              std::unique_ptr<LinkInputSet> Inputs,
              LTOInputSetOptions Options);

  llvm::Error initialize();
  llvm::Error inspectModules();
  llvm::Error buildResolutions();
  void rebuildResolutionDigest();
  NevercStatus fillRequest(NevercLTORequestHandle RequestHandle,
                           NevercLTORequest *OutRequest);
  NevercStatus fillModulePage(NevercLTORequestHandle RequestHandle,
                              uint64_t Cursor,
                              NevercLinkEntityPage *Page);
  NevercStatus fillResolutionPage(
      NevercLTORequestHandle RequestHandle, uint64_t Cursor,
      NevercLinkEntityPage *Page);
  NevercStatus fillModuleInfo(NevercLTOInputModuleHandle Module,
                              NevercLTOInputModuleInfo *OutInfo);
  NevercStatus fillResolutionInfo(
      NevercLTOResolutionHandle Resolution,
      NevercLTOSymbolResolution *OutInfo);

  PluginTaskContext &Task;
  std::shared_ptr<const LinkRequest> Request;
  std::unique_ptr<LinkInputSet> Inputs;
  LTOInputSetOptions Options;
  std::unique_ptr<LinkGraphPluginBridge> GraphBridge;
  std::shared_ptr<LTOProcessService> Service;
  NevercLTORequestHandle RequestHandle{};
  NevercLinkGraphHandle GraphHandle{};
  std::vector<LTOInputModuleRecord> Modules;
  std::vector<LTOSymbolResolutionRecord> Resolutions;
  std::array<uint8_t, 32> ResolutionDigest{};
  std::vector<NevercLTOInputModuleInfo> ModuleViews;
  std::vector<NevercLTOSymbolResolution> ResolutionViews;
  bool Attached = false;

  friend class LTOProcessService;
};

llvm::Error inspectPluginBitcodeModule(
    PluginLinkBitcodeModule &Module, llvm::MemoryBufferRef Buffer,
    NevercTargetKey Target);

std::shared_ptr<LTOProcessService>
findLTOProcessService(PluginProcessServices &Services);

llvm::Error registerPluginLTOInterface(
    PluginProcessServices &Services);

} // namespace neverc::plugin

#endif
