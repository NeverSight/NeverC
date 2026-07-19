#include "neverc/Plugin/Host/IROptimizationProvider.h"
#include "IRModuleArtifact.h"
#include "neverc/Plugin/Host/IRPluginBridge.h"
#include "neverc/Plugin/Host/PluginArtifactRegistry.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginPhaseExecutor.h"
#include "neverc/Plugin/Host/PluginPhaseGraph.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "neverc/Plugin/PluginIR.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/SHA256.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>
#include <new>
#include <utility>

using namespace llvm;

namespace neverc::plugin {
namespace {

NevercInterfaceID optimizationInterfaceID() {
  return {NEVERC_INTERFACE_IR_OPTIMIZATION_HIGH,
          NEVERC_INTERFACE_IR_OPTIMIZATION_LOW};
}

NevercStatus optimizationStatus(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

bool sameHandle(NevercHandle Left, NevercHandle Right) {
  return Left.Owner == Right.Owner && Left.Value == Right.Value;
}

bool validHeader(const NevercABITableHeader &Header, size_t Size) {
  return Header.StructSize >= Size &&
         Header.Major == NEVERC_IR_OPTIMIZATION_API_MAJOR &&
         Header.Minor <= NEVERC_IR_OPTIMIZATION_API_MINOR &&
         Header.Flags == 0;
}

bool validBytes(NevercByteView Bytes) {
  return (Bytes.Length == 0 || Bytes.Data) &&
         Bytes.Length <=
             static_cast<uint64_t>(std::numeric_limits<size_t>::max());
}

bool stringRef(NevercStringView View, StringRef *Out) {
  if (!Out || (View.Length != 0 && !View.Data) ||
      View.Length >
          static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
    return false;
  *Out = StringRef(View.Data ? View.Data : "",
                   static_cast<size_t>(View.Length));
  return !Out->contains('\0') && json::isUTF8(*Out);
}

NevercStringView stringView(StringRef Text) {
  return {Text.data(), static_cast<uint64_t>(Text.size())};
}

template <typename T>
NevercStatus writeCallerRecord(T *OutValue, const T &Value) {
  if (!OutValue)
    return optimizationStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  const uint32_t Capacity = OutValue->Header.StructSize;
  if (Capacity < sizeof(NevercABITableHeader))
    return optimizationStatus(NEVERC_STATUS_ABI_MISMATCH);
  const size_t Writable = std::min<size_t>(Capacity, sizeof(Value));
  std::memset(OutValue, 0, Writable);
  std::memcpy(OutValue, &Value, Writable);
  return Capacity < sizeof(Value)
             ? optimizationStatus(NEVERC_STATUS_ABI_MISMATCH)
             : neverc_status_ok();
}

NevercPhaseRoute defaultRoute() {
  NevercPhaseRoute Route{};
  Route.Header = {sizeof(Route), NEVERC_PLUGIN_ABI_MAJOR,
                  NEVERC_PLUGIN_ABI_MINOR, 0};
  return Route;
}

std::array<uint8_t, 32> inputDigest(const Module &M,
                                    uint32_t OptimizationLevel,
                                    bool DisableLLVMPasses) {
  SmallString<256> Bytes;
  raw_svector_ostream Stream(Bytes);
  Stream << M.getModuleIdentifier() << '\0' << M.getTargetTriple() << '\0'
         << M.getDataLayoutStr() << '\0' << OptimizationLevel << '\0'
         << DisableLLVMPasses;
  return SHA256::hash(ArrayRef<uint8_t>(
      reinterpret_cast<const uint8_t *>(Bytes.data()), Bytes.size()));
}

class IROptimizationProcessBridge;

std::shared_ptr<IROptimizationProcessBridge>
findOptimizationProcessBridge(PluginProcessServices &Services);

} // namespace

struct PluginIROptimizationProviderRuntime::Impl {
  Impl(PluginTaskContext &TaskValue, Module &ModuleValue,
       uint32_t OptimizationLevelValue, bool DisableLLVMPassesValue,
       BuiltinOptimizer OptimizeBuiltinValue, PluginPhaseGraph GraphValue)
      : Task(TaskValue), InputModule(ModuleValue),
        TargetTriple(ModuleValue.getTargetTriple()),
        DataLayout(ModuleValue.getDataLayoutStr()),
        OptimizationLevel(OptimizationLevelValue),
        DisableLLVMPasses(DisableLLVMPassesValue),
        OptimizeBuiltin(std::move(OptimizeBuiltinValue)),
        Graph(std::move(GraphValue)) {}

  bool validFrame(const NevercPhaseFrame *Frame) const;
  NevercStatus getOptimizationPhaseInput(
      const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
      NevercIROptimizationPhaseInput *OutInput);
  NevercStatus getInputModule(const NevercPhaseFrame *Frame,
                              NevercArtifactHandle Input,
                              const NevercIRCoreAPI **OutCoreAPI,
                              const NevercIRBuilderAPI **OutBuilderAPI);
  NevercStatus createModule(const NevercPhaseFrame *Frame,
                            NevercStringView ModuleIdentifier,
                            const NevercIRCoreAPI **OutCoreAPI,
                            const NevercIRBuilderAPI **OutBuilderAPI);
  NevercStatus importModule(const NevercPhaseFrame *Frame,
                            NevercIRSerializationFormat Format,
                            NevercByteView Bytes,
                            const NevercIRCoreAPI **OutCoreAPI,
                            const NevercIRBuilderAPI **OutBuilderAPI);
  NevercStatus publishModule(
      const NevercPhaseFrame *Frame,
      const NevercIRModuleArtifactDescriptor *Descriptor,
      NevercArtifactHandle *OutOutput);
  NevercStatus getModuleArtifactInfo(const NevercPhaseFrame *Frame,
                                     NevercArtifactHandle Module,
                                     NevercIRModuleArtifactInfo *OutInfo);
  NevercStatus runBuiltinPipeline(const NevercPhaseFrame *Frame,
                                  NevercArtifactHandle *OutOutput);
  NevercStatus builtinProvider(const NevercPhaseFrame *Frame,
                               NevercPhaseResult *Result);
  NevercStatus finalVerifier(const NevercPhaseFrame *Frame,
                             NevercPhaseResult *Result);
  IRModuleArtifact *publishedArtifact() const;

  PluginTaskContext &Task;
  Module &InputModule;
  std::string TargetTriple;
  std::string DataLayout;
  uint32_t OptimizationLevel;
  bool DisableLLVMPasses;
  BuiltinOptimizer OptimizeBuiltin;
  PluginPhaseGraph Graph;
  PluginArtifactRegistry Artifacts;
  std::unique_ptr<PluginPhaseExecutor> Executor;
  std::shared_ptr<IROptimizationProcessBridge> ProcessBridge;
  std::shared_ptr<IRPluginBridge> InputBridge;
  std::shared_ptr<IRPluginBridge> BuildingBridge;
  std::unique_ptr<IRModuleArtifact> ActiveInput;
  std::unique_ptr<PluginArtifactSlot> Output;
  std::array<uint8_t, 32> InputDigest{};
  bool BuiltinRan = false;
  std::string FailureMessage;
};

namespace {

class IROptimizationProcessBridge final
    : public PluginHostService,
      public std::enable_shared_from_this<IROptimizationProcessBridge> {
public:
  explicit IROptimizationProcessBridge(PluginProcessServices &ServicesValue)
      : Services(ServicesValue) {
    API.Header = {sizeof(API), NEVERC_IR_OPTIMIZATION_API_MAJOR,
                  NEVERC_IR_OPTIMIZATION_API_MINOR, 0};
    API.Context = this;
    API.GetOptimizationPhaseInput = getOptimizationPhaseInput;
    API.GetInputModule = getInputModule;
    API.CreateModule = createModule;
    API.ImportModule = importModule;
    API.PublishModule = publishModule;
    API.GetModuleArtifactInfo = getModuleArtifactInfo;
    API.RunBuiltinPipeline = runBuiltinPipeline;
  }

  const NevercIROptimizationAPI &api() const { return API; }

  void attach(PluginTaskContext &Task,
              PluginIROptimizationProviderRuntime::Impl &Runtime) {
    std::lock_guard<std::mutex> Lock(Mutex);
    Tasks[{Task.handle().Owner, Task.handle().Value}] = &Runtime;
  }

  void detach(NevercTaskHandle Task) {
    std::lock_guard<std::mutex> Lock(Mutex);
    Tasks.erase({Task.Owner, Task.Value});
  }

  void taskScopeUnregistered(NevercTaskHandle Task) noexcept override {
    detach(Task);
  }

private:
  PluginIROptimizationProviderRuntime::Impl *find(NevercTaskHandle Task) {
    std::lock_guard<std::mutex> Lock(Mutex);
    auto It = Tasks.find({Task.Owner, Task.Value});
    return It == Tasks.end() ? nullptr : It->second;
  }

  static IROptimizationProcessBridge *bridge(void *Context) {
    return static_cast<IROptimizationProcessBridge *>(Context);
  }

  static NevercStatus NEVERC_CALL getOptimizationPhaseInput(
      void *Context, const NevercPhaseFrame *Frame,
      NevercArtifactHandle Input, NevercIROptimizationPhaseInput *OutInput) {
    if (!Context || !Frame)
      return optimizationStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    auto *Runtime = bridge(Context)->find(Frame->Task);
    return Runtime
               ? Runtime->getOptimizationPhaseInput(Frame, Input, OutInput)
               : optimizationStatus(NEVERC_STATUS_STALE_HANDLE);
  }

  static NevercStatus NEVERC_CALL getInputModule(
      void *Context, const NevercPhaseFrame *Frame,
      NevercArtifactHandle Input, const NevercIRCoreAPI **OutCoreAPI,
      const NevercIRBuilderAPI **OutBuilderAPI) {
    if (!Context || !Frame)
      return optimizationStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    auto *Runtime = bridge(Context)->find(Frame->Task);
    return Runtime
               ? Runtime->getInputModule(Frame, Input, OutCoreAPI,
                                         OutBuilderAPI)
               : optimizationStatus(NEVERC_STATUS_STALE_HANDLE);
  }

  static NevercStatus NEVERC_CALL createModule(
      void *Context, const NevercPhaseFrame *Frame,
      NevercStringView ModuleIdentifier,
      const NevercIRCoreAPI **OutCoreAPI,
      const NevercIRBuilderAPI **OutBuilderAPI) {
    if (!Context || !Frame)
      return optimizationStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    auto *Runtime = bridge(Context)->find(Frame->Task);
    return Runtime
               ? Runtime->createModule(Frame, ModuleIdentifier, OutCoreAPI,
                                       OutBuilderAPI)
               : optimizationStatus(NEVERC_STATUS_STALE_HANDLE);
  }

  static NevercStatus NEVERC_CALL importModule(
      void *Context, const NevercPhaseFrame *Frame,
      NevercIRSerializationFormat Format, NevercByteView Bytes,
      const NevercIRCoreAPI **OutCoreAPI,
      const NevercIRBuilderAPI **OutBuilderAPI) {
    if (!Context || !Frame)
      return optimizationStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    auto *Runtime = bridge(Context)->find(Frame->Task);
    return Runtime ? Runtime->importModule(Frame, Format, Bytes, OutCoreAPI,
                                           OutBuilderAPI)
                   : optimizationStatus(NEVERC_STATUS_STALE_HANDLE);
  }

  static NevercStatus NEVERC_CALL publishModule(
      void *Context, const NevercPhaseFrame *Frame,
      const NevercIRModuleArtifactDescriptor *Descriptor,
      NevercArtifactHandle *OutOutput) {
    if (!Context || !Frame)
      return optimizationStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    auto *Runtime = bridge(Context)->find(Frame->Task);
    return Runtime ? Runtime->publishModule(Frame, Descriptor, OutOutput)
                   : optimizationStatus(NEVERC_STATUS_STALE_HANDLE);
  }

  static NevercStatus NEVERC_CALL getModuleArtifactInfo(
      void *Context, const NevercPhaseFrame *Frame,
      NevercArtifactHandle Module, NevercIRModuleArtifactInfo *OutInfo) {
    if (!Context || !Frame)
      return optimizationStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    auto *Runtime = bridge(Context)->find(Frame->Task);
    return Runtime ? Runtime->getModuleArtifactInfo(Frame, Module, OutInfo)
                   : optimizationStatus(NEVERC_STATUS_STALE_HANDLE);
  }

  static NevercStatus NEVERC_CALL runBuiltinPipeline(
      void *Context, const NevercPhaseFrame *Frame,
      NevercArtifactHandle *OutOutput) {
    if (!Context || !Frame)
      return optimizationStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    auto *Runtime = bridge(Context)->find(Frame->Task);
    return Runtime ? Runtime->runBuiltinPipeline(Frame, OutOutput)
                   : optimizationStatus(NEVERC_STATUS_STALE_HANDLE);
  }

  PluginProcessServices &Services;
  NevercIROptimizationAPI API{};
  std::mutex Mutex;
  std::map<std::pair<uint64_t, uint64_t>,
           PluginIROptimizationProviderRuntime::Impl *>
      Tasks;
};

std::shared_ptr<IROptimizationProcessBridge>
findOptimizationProcessBridge(PluginProcessServices &Services) {
  return std::static_pointer_cast<IROptimizationProcessBridge>(
      Services.findHostService(optimizationInterfaceID()));
}

} // namespace

bool PluginIROptimizationProviderRuntime::Impl::validFrame(
    const NevercPhaseFrame *Frame) const {
  return Frame && ActiveInput && sameHandle(Frame->Task, Task.handle()) &&
         samePluginInterfaceID(Frame->Phase, irOptimizePhaseID());
}

NevercStatus
PluginIROptimizationProviderRuntime::Impl::getOptimizationPhaseInput(
    const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
    NevercIROptimizationPhaseInput *OutInput) {
  if (!validFrame(Frame) || !OutInput || !sameHandle(Input, Frame->Input))
    return optimizationStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  NevercIROptimizationPhaseInput Value{};
  Value.Header = {sizeof(Value), NEVERC_IR_OPTIMIZATION_API_MAJOR,
                  NEVERC_IR_OPTIMIZATION_API_MINOR, 0};
  Value.Module = Input;
  Value.Product = ActiveInput->Product;
  Value.TargetTriple = stringView(TargetTriple);
  Value.DataLayout = stringView(DataLayout);
  Value.OptimizationLevel = OptimizationLevel;
  Value.DisableLLVMPasses =
      DisableLLVMPasses ? NEVERC_TRUE : NEVERC_FALSE;
  Value.InputDigest = {InputDigest.data(), InputDigest.size()};
  return writeCallerRecord(OutInput, Value);
}

NevercStatus PluginIROptimizationProviderRuntime::Impl::getInputModule(
    const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
    const NevercIRCoreAPI **OutCoreAPI,
    const NevercIRBuilderAPI **OutBuilderAPI) {
  if (OutCoreAPI)
    *OutCoreAPI = nullptr;
  if (OutBuilderAPI)
    *OutBuilderAPI = nullptr;
  if (!validFrame(Frame) || !sameHandle(Input, Frame->Input) || !OutCoreAPI ||
      !OutBuilderAPI || !InputBridge)
    return optimizationStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutCoreAPI = &InputBridge->coreAPI();
  *OutBuilderAPI = &InputBridge->builderAPI();
  return neverc_status_ok();
}

NevercStatus PluginIROptimizationProviderRuntime::Impl::createModule(
    const NevercPhaseFrame *Frame, NevercStringView ModuleIdentifier,
    const NevercIRCoreAPI **OutCoreAPI,
    const NevercIRBuilderAPI **OutBuilderAPI) {
  if (OutCoreAPI)
    *OutCoreAPI = nullptr;
  if (OutBuilderAPI)
    *OutBuilderAPI = nullptr;
  StringRef Identifier;
  if (!validFrame(Frame) || !OutCoreAPI || !OutBuilderAPI ||
      !stringRef(ModuleIdentifier, &Identifier) || Identifier.empty())
    return optimizationStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  if (BuildingBridge || BuiltinRan)
    return optimizationStatus(NEVERC_STATUS_BUSY);
  auto Created = IRPluginBridge::createInContext(
      Task, InputModule.getContext(), Identifier);
  if (!Created) {
    consumeError(Created.takeError());
    return optimizationStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  BuildingBridge = std::shared_ptr<IRPluginBridge>(std::move(*Created));
  BuildingBridge->module().setTargetTriple(TargetTriple);
  BuildingBridge->module().setDataLayout(DataLayout);
  *OutCoreAPI = &BuildingBridge->coreAPI();
  *OutBuilderAPI = &BuildingBridge->builderAPI();
  return neverc_status_ok();
}

NevercStatus PluginIROptimizationProviderRuntime::Impl::importModule(
    const NevercPhaseFrame *Frame, NevercIRSerializationFormat Format,
    NevercByteView Bytes, const NevercIRCoreAPI **OutCoreAPI,
    const NevercIRBuilderAPI **OutBuilderAPI) {
  if (!validBytes(Bytes))
    return optimizationStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  static constexpr char ImportedName[] = "plugin-optimized-module";
  NevercStatus Status = createModule(
      Frame, {ImportedName, sizeof(ImportedName) - 1}, OutCoreAPI,
      OutBuilderAPI);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = BuildingBridge->importModule(Format, Bytes);
  if (Status.Code != NEVERC_STATUS_OK) {
    BuildingBridge.reset();
    if (OutCoreAPI)
      *OutCoreAPI = nullptr;
    if (OutBuilderAPI)
      *OutBuilderAPI = nullptr;
  }
  return Status;
}

NevercStatus PluginIROptimizationProviderRuntime::Impl::publishModule(
    const NevercPhaseFrame *Frame,
    const NevercIRModuleArtifactDescriptor *Descriptor,
    NevercArtifactHandle *OutOutput) {
  if (OutOutput)
    *OutOutput = {};
  std::shared_ptr<IRPluginBridge> Source =
      BuildingBridge ? BuildingBridge : InputBridge;
  if (!validFrame(Frame) || !Descriptor || !OutOutput || !Source ||
      BuiltinRan ||
      !validHeader(Descriptor->Header, sizeof(*Descriptor)) ||
      !samePluginInterfaceID(Descriptor->Product,
                             optimizedIRModuleArtifactID()) ||
      !validBytes(Descriptor->DependencyDigest) ||
      Descriptor->DependencyDigest.Length != InputDigest.size() ||
      !std::equal(InputDigest.begin(), InputDigest.end(),
                  Descriptor->DependencyDigest.Data) ||
      Descriptor->Reserved[0] != 0 || Descriptor->Reserved[1] != 0)
    return optimizationStatus(NEVERC_STATUS_INVALID_ARGUMENT);

  auto *Candidate = new (std::nothrow) IRModuleArtifact;
  if (!Candidate)
    return optimizationStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  Candidate->Bridge = Source;
  Candidate->Product = optimizedIRModuleArtifactID();
  Candidate->TargetTriple = TargetTriple;
  Candidate->DataLayout = DataLayout;
  Candidate->Generation = Source->mutationGeneration();
  Candidate->DependencyDigest = InputDigest;
  Candidate->HasDependencyDigest = true;
  auto Handle = Executor->createCandidate(
      Task, optimizedIRModuleArtifactID(), Candidate);
  if (!Handle) {
    delete Candidate;
    consumeError(Handle.takeError());
    return optimizationStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  BuildingBridge.reset();
  *OutOutput = *Handle;
  return neverc_status_ok();
}

NevercStatus
PluginIROptimizationProviderRuntime::Impl::getModuleArtifactInfo(
    const NevercPhaseFrame *Frame, NevercArtifactHandle ModuleHandle,
    NevercIRModuleArtifactInfo *OutInfo) {
  if (!validFrame(Frame) || !OutInfo)
    return optimizationStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  NevercInterfaceID Type = sameHandle(ModuleHandle, Frame->Input)
                               ? irModuleArtifactID()
                               : optimizedIRModuleArtifactID();
  const void *Payload = nullptr;
  NevercStatus Status =
      Executor->resolveArtifactPayload(Task, ModuleHandle, Type, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  const auto &Artifact = *static_cast<const IRModuleArtifact *>(Payload);
  NevercIRModuleArtifactInfo Info{};
  Info.Header = {sizeof(Info), NEVERC_IR_OPTIMIZATION_API_MAJOR,
                 NEVERC_IR_OPTIMIZATION_API_MINOR, 0};
  Info.Product = Artifact.Product;
  Info.TargetTriple = stringView(Artifact.TargetTriple);
  Info.DataLayout = stringView(Artifact.DataLayout);
  Info.Generation = Artifact.Generation;
  Info.DependencyDigest = {
      Artifact.DependencyDigest.data(),
      Artifact.HasDependencyDigest ? Artifact.DependencyDigest.size() : 0};
  return writeCallerRecord(OutInfo, Info);
}

NevercStatus PluginIROptimizationProviderRuntime::Impl::runBuiltinPipeline(
    const NevercPhaseFrame *Frame, NevercArtifactHandle *OutOutput) {
  if (OutOutput)
    *OutOutput = {};
  if (!validFrame(Frame) || !OutOutput || !OptimizeBuiltin)
    return optimizationStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  if (BuiltinRan || BuildingBridge)
    return optimizationStatus(NEVERC_STATUS_BUSY);
  BuiltinRan = true;
  if (Error E = OptimizeBuiltin(InputBridge->module())) {
    FailureMessage = toString(std::move(E)).str();
    return optimizationStatus(NEVERC_STATUS_PLUGIN_FAILURE);
  }
  InputBridge->noteExternalMutation();

  auto *Candidate = new (std::nothrow) IRModuleArtifact;
  if (!Candidate)
    return optimizationStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  Candidate->Bridge = InputBridge;
  Candidate->Product = optimizedIRModuleArtifactID();
  Candidate->TargetTriple = TargetTriple;
  Candidate->DataLayout = DataLayout;
  Candidate->Generation = InputBridge->mutationGeneration();
  Candidate->DependencyDigest = InputDigest;
  Candidate->HasDependencyDigest = true;
  auto Handle = Executor->createCandidate(
      Task, optimizedIRModuleArtifactID(), Candidate);
  if (!Handle) {
    delete Candidate;
    consumeError(Handle.takeError());
    return optimizationStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutOutput = *Handle;
  return neverc_status_ok();
}

NevercStatus PluginIROptimizationProviderRuntime::Impl::builtinProvider(
    const NevercPhaseFrame *Frame, NevercPhaseResult *Result) {
  FailureMessage.clear();
  if (!validFrame(Frame) || !Result)
    return optimizationStatus(NEVERC_STATUS_INVALID_STATE);
  NevercArtifactHandle OutputHandle{};
  NevercStatus Status = runBuiltinPipeline(Frame, &OutputHandle);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  *Result = {};
  Result->Header = {sizeof(*Result), NEVERC_PLUGIN_ABI_MAJOR,
                    NEVERC_PLUGIN_ABI_MINOR, 0};
  Result->Action = NEVERC_PHASE_REPLACE;
  Result->Output = OutputHandle;
  return neverc_status_ok();
}

NevercStatus PluginIROptimizationProviderRuntime::Impl::finalVerifier(
    const NevercPhaseFrame *Frame, NevercPhaseResult *Result) {
  if (!Frame || !Result || !sameHandle(Frame->Task, Task.handle()) ||
      !samePluginInterfaceID(
          Frame->Phase,
          {NEVERC_PHASE_IR_FINAL_VERIFY_HIGH,
           NEVERC_PHASE_IR_FINAL_VERIFY_LOW}))
    return optimizationStatus(NEVERC_STATUS_INVALID_STATE);
  const void *Payload = nullptr;
  NevercStatus Status = Executor->resolveArtifactPayload(
      Task, Frame->Input, optimizedIRModuleArtifactID(), &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *Candidate = new (std::nothrow)
      IRModuleArtifact(*static_cast<const IRModuleArtifact *>(Payload));
  if (!Candidate)
    return optimizationStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  auto Handle = Executor->createCandidate(
      Task, optimizedIRModuleArtifactID(), Candidate);
  if (!Handle) {
    delete Candidate;
    consumeError(Handle.takeError());
    return optimizationStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *Result = {};
  Result->Header = {sizeof(*Result), NEVERC_PLUGIN_ABI_MAJOR,
                    NEVERC_PLUGIN_ABI_MINOR, 0};
  Result->Action = NEVERC_PHASE_REPLACE;
  Result->Output = *Handle;
  return neverc_status_ok();
}

IRModuleArtifact *
PluginIROptimizationProviderRuntime::Impl::publishedArtifact() const {
  if (!Output)
    return nullptr;
  PluginArtifactSlot::Snapshot Snapshot = Output->snapshot();
  return const_cast<IRModuleArtifact *>(
      static_cast<const IRModuleArtifact *>(Snapshot.Payload));
}

PluginIROptimizationProviderRuntime::PluginIROptimizationProviderRuntime(
    std::unique_ptr<Impl> StateValue)
    : State(std::move(StateValue)) {}

PluginIROptimizationProviderRuntime::~PluginIROptimizationProviderRuntime() {
  if (State && State->ProcessBridge)
    State->ProcessBridge->detach(State->Task.handle());
}

Expected<std::unique_ptr<PluginIROptimizationProviderRuntime>>
PluginIROptimizationProviderRuntime::create(
    PluginTaskContext &Task, Module &ModuleValue,
    uint32_t OptimizationLevel, bool DisableLLVMPasses,
    BuiltinOptimizer OptimizeBuiltin) {
  if (ModuleValue.getTargetTriple().empty() ||
      ModuleValue.getDataLayoutStr().empty() || !OptimizeBuiltin)
    return createStringError(
        inconvertibleErrorCode(),
        "IR optimization runtime requires a target and provider");
  auto ProcessBridge =
      findOptimizationProcessBridge(Task.processServices());
  if (!ProcessBridge)
    return createStringError(
        inconvertibleErrorCode(),
        "plugin IR optimization interface is not registered");
  auto Graph = PluginPhaseGraph::createBuiltinIRGraph();
  if (!Graph)
    return Graph.takeError();
  auto State = std::make_unique<Impl>(
      Task, ModuleValue, OptimizationLevel, DisableLLVMPasses,
      std::move(OptimizeBuiltin), std::move(*Graph));
  State->ProcessBridge = std::move(ProcessBridge);
  State->InputDigest =
      inputDigest(ModuleValue, OptimizationLevel, DisableLLVMPasses);
  auto Borrowed = IRPluginBridge::borrow(Task, ModuleValue);
  if (!Borrowed)
    return Borrowed.takeError();
  State->InputBridge =
      std::shared_ptr<IRPluginBridge>(std::move(*Borrowed));
  if (Error E = registerIRModuleArtifactType(State->Artifacts))
    return std::move(E);
  if (Error E = registerOptimizedIRModuleArtifactType(State->Artifacts))
    return std::move(E);
  if (Error E = State->Artifacts.freeze())
    return std::move(E);
  State->Executor =
      std::make_unique<PluginPhaseExecutor>(State->Graph, State->Artifacts);
  if (Error E =
          State->Executor->importSessionRegistrations(Task.session()))
    return std::move(E);
  Impl *Raw = State.get();
  if (Error E = State->Executor->setBuiltinProvider(
          irOptimizePhaseID(),
          [Raw](const NevercPhaseFrame *Frame, NevercPhaseResult *Result) {
            return Raw->builtinProvider(Frame, Result);
          }))
    return std::move(E);
  if (Error E = State->Executor->setBuiltinProvider(
          {NEVERC_PHASE_IR_FINAL_VERIFY_HIGH,
           NEVERC_PHASE_IR_FINAL_VERIFY_LOW},
          [Raw](const NevercPhaseFrame *Frame, NevercPhaseResult *Result) {
            return Raw->finalVerifier(Frame, Result);
          }))
    return std::move(E);
  if (Error E = State->Executor->freeze())
    return std::move(E);
  State->ProcessBridge->attach(Task, *State);
  return std::unique_ptr<PluginIROptimizationProviderRuntime>(
      new PluginIROptimizationProviderRuntime(std::move(State)));
}

Error PluginIROptimizationProviderRuntime::execute() {
  if (!State)
    return createStringError(inconvertibleErrorCode(),
                             "IR optimization runtime is not initialized");
  State->BuildingBridge.reset();
  State->BuiltinRan = false;
  State->FailureMessage.clear();
  State->ActiveInput = std::make_unique<IRModuleArtifact>();
  State->ActiveInput->Bridge = State->InputBridge;
  State->ActiveInput->Product = standardIRModuleProductID();
  State->ActiveInput->TargetTriple = State->TargetTriple;
  State->ActiveInput->DataLayout = State->DataLayout;
  State->ActiveInput->Generation =
      State->InputBridge->mutationGeneration();
  State->ActiveInput->DependencyDigest = State->InputDigest;
  State->ActiveInput->HasDependencyDigest = true;
  auto ResetActive = make_scope_exit([&] {
    State->ActiveInput.reset();
    State->BuildingBridge.reset();
  });
  auto Input = State->Executor->createArtifactView(
      State->Task, irModuleArtifactID(), State->ActiveInput.get(),
      State->ActiveInput->Generation);
  if (!Input)
    return Input.takeError();
  auto ReleaseInput = make_scope_exit([&] {
    (void)State->Task.handles().release(*Input, PluginArtifactHandleKind);
  });
  State->Output = std::make_unique<PluginArtifactSlot>(
      State->Artifacts.find(optimizedIRModuleArtifactID()));
  NevercPhaseRoute Route = defaultRoute();
  if (Error E = State->Executor->execute(
          State->Task.session(), State->Task, irOptimizePhaseID(), Route,
          *Input, *State->Output)) {
    if (!State->FailureMessage.empty())
      return joinErrors(
          std::move(E),
          createStringError(inconvertibleErrorCode(),
                            State->FailureMessage));
    return E;
  }
  IRModuleArtifact *Artifact = State->publishedArtifact();
  if (!Artifact)
    return createStringError(inconvertibleErrorCode(),
                             "IR optimization phase published no module");
  std::unique_ptr<PluginArtifactSlot> OptimizedOutput =
      std::move(State->Output);
  auto FinalInput = State->Executor->createArtifactView(
      State->Task, optimizedIRModuleArtifactID(), Artifact,
      Artifact->Generation);
  if (!FinalInput)
    return FinalInput.takeError();
  auto ReleaseFinalInput = make_scope_exit([&] {
    (void)State->Task.handles().release(*FinalInput,
                                       PluginArtifactHandleKind);
  });
  State->Output = std::make_unique<PluginArtifactSlot>(
      State->Artifacts.find(optimizedIRModuleArtifactID()));
  if (Error E = State->Executor->execute(
          State->Task.session(), State->Task,
          {NEVERC_PHASE_IR_FINAL_VERIFY_HIGH,
           NEVERC_PHASE_IR_FINAL_VERIFY_LOW},
          Route, *FinalInput, *State->Output))
    return E;
  Artifact = State->publishedArtifact();
  if (!Artifact)
    return createStringError(inconvertibleErrorCode(),
                             "final IR verifier published no module");
  Module *Optimized = getIRModule(*Artifact);
  if (!Optimized || Optimized->getTargetTriple() != State->TargetTriple ||
      Optimized->getDataLayoutStr() != State->DataLayout ||
      verifyModule(*Optimized))
    return createStringError(
        inconvertibleErrorCode(),
        "sealed final IR verification rejected optimized module");
  return Error::success();
}

bool PluginIROptimizationProviderRuntime::ranBuiltinPipeline() const {
  return State && State->BuiltinRan;
}

Module *PluginIROptimizationProviderRuntime::module() const {
  if (!State)
    return nullptr;
  IRModuleArtifact *Artifact = State->publishedArtifact();
  return Artifact ? getIRModule(*Artifact) : nullptr;
}

bool PluginIROptimizationProviderRuntime::ownsModule() const {
  if (!State)
    return false;
  IRModuleArtifact *Artifact = State->publishedArtifact();
  return Artifact && Artifact->Bridge && Artifact->Bridge->ownsModule();
}

std::unique_ptr<Module>
PluginIROptimizationProviderRuntime::releaseOwnedModule() {
  if (!State)
    return nullptr;
  IRModuleArtifact *Artifact = State->publishedArtifact();
  if (!Artifact || !Artifact->Bridge || !Artifact->Bridge->ownsModule())
    return nullptr;
  std::unique_ptr<Module> Result = Artifact->Bridge->releaseModule();
  Artifact->BorrowedModule = Result.get();
  Artifact->Bridge.reset();
  return Result;
}

Error
registerPluginIROptimizationInterface(PluginProcessServices &Services) {
  if (Services.interfaces().isFrozen())
    return createStringError(
        inconvertibleErrorCode(),
        "cannot register plugin IR optimization interface after freeze");
  auto Bridge =
      std::make_shared<IROptimizationProcessBridge>(Services);
  if (Error E =
          Services.registerHostService(optimizationInterfaceID(), Bridge))
    return E;
  return Services.interfaces().registerInterface(
      optimizationInterfaceID(), NEVERC_IR_OPTIMIZATION_INTERFACE_STABILITY,
      &Bridge->api(), {});
}

} // namespace neverc::plugin
