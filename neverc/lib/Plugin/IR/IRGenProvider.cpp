#include "neverc/Plugin/Host/IRGenProvider.h"
#include "IRModuleArtifact.h"
#include "../Frontend/SemanticUnitArtifact.h"
#include "neverc/Plugin/Host/IROptimizationProvider.h"
#include "neverc/Plugin/Host/FrontendPluginBridge.h"
#include "neverc/Plugin/Host/IRPassPlugin.h"
#include "neverc/Plugin/Host/IRPluginBridge.h"
#include "neverc/Plugin/Host/MIRPluginBridge.h"
#include "neverc/Plugin/Host/PluginArtifactRegistry.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginPhaseExecutor.h"
#include "neverc/Plugin/Host/PluginPhaseGraph.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/JSON.h"
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

NevercInterfaceID irGenInterfaceID() {
  return {NEVERC_INTERFACE_IR_GEN_HIGH, NEVERC_INTERFACE_IR_GEN_LOW};
}

NevercStatus irGenStatus(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

bool sameHandle(NevercHandle Left, NevercHandle Right) {
  return Left.Owner == Right.Owner && Left.Value == Right.Value;
}

bool nonnull(NevercInterfaceID ID) { return ID.High != 0 || ID.Low != 0; }

bool validHeader(const NevercABITableHeader &Header, size_t Size) {
  return Header.StructSize >= Size && Header.Major == NEVERC_IR_GEN_API_MAJOR &&
         Header.Minor <= NEVERC_IR_GEN_API_MINOR && Header.Flags == 0;
}

bool validBytes(NevercByteView Bytes) {
  return (Bytes.Length == 0 || Bytes.Data) &&
         Bytes.Length <= static_cast<uint64_t>(
                             std::numeric_limits<size_t>::max());
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
    return irGenStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  const uint32_t Capacity = OutValue->Header.StructSize;
  if (Capacity < sizeof(NevercABITableHeader))
    return irGenStatus(NEVERC_STATUS_ABI_MISMATCH);
  const size_t Writable = std::min<size_t>(Capacity, sizeof(Value));
  std::memset(OutValue, 0, Writable);
  std::memcpy(OutValue, &Value, Writable);
  return Capacity < sizeof(Value)
             ? irGenStatus(NEVERC_STATUS_ABI_MISMATCH)
             : neverc_status_ok();
}

NevercPhaseRoute defaultRoute() {
  NevercPhaseRoute Route{};
  Route.Header = {sizeof(Route), NEVERC_PLUGIN_ABI_MAJOR,
                  NEVERC_PLUGIN_ABI_MINOR, 0};
  return Route;
}

class IRGenProcessBridge;

std::shared_ptr<IRGenProcessBridge>
findIRGenProcessBridge(PluginProcessServices &Services);

} // namespace

struct PluginIRGenProviderRuntime::Impl {
  Impl(PluginTaskContext &TaskValue, LLVMContext &ContextValue,
       StringRef TargetTripleValue, StringRef DataLayoutValue,
       BuiltinGenerator GenerateBuiltinValue, PluginPhaseGraph GraphValue)
      : Task(TaskValue), Context(ContextValue),
        TargetTriple(TargetTripleValue.str()),
        DataLayout(DataLayoutValue.str()),
        GenerateBuiltin(std::move(GenerateBuiltinValue)),
        Graph(std::move(GraphValue)) {}

  NevercStatus getGeneratePhaseInput(const NevercPhaseFrame *Frame,
                                     NevercArtifactHandle Input,
                                     NevercIRGeneratePhaseInput *OutInput);
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
  NevercStatus builtinProvider(const NevercPhaseFrame *Frame,
                               NevercPhaseResult *Result);
  bool validFrame(const NevercPhaseFrame *Frame) const;
  IRModuleArtifact *publishedArtifact() const;

  PluginTaskContext &Task;
  LLVMContext &Context;
  std::string TargetTriple;
  std::string DataLayout;
  BuiltinGenerator GenerateBuiltin;
  PluginPhaseGraph Graph;
  PluginArtifactRegistry Artifacts;
  std::unique_ptr<PluginPhaseExecutor> Executor;
  std::shared_ptr<IRGenProcessBridge> ProcessBridge;
  const SemanticUnitArtifact *ActiveSemantic = nullptr;
  std::shared_ptr<IRPluginBridge> BuildingBridge;
  std::unique_ptr<PluginArtifactSlot> Output;
  uint64_t BuiltinGeneration = 0;
  std::string FailureMessage;
};

namespace {

class IRGenProcessBridge final
    : public PluginHostService,
      public std::enable_shared_from_this<IRGenProcessBridge> {
public:
  explicit IRGenProcessBridge(PluginProcessServices &ServicesValue)
      : Services(ServicesValue) {
    API.Header = {sizeof(API), NEVERC_IR_GEN_API_MAJOR,
                  NEVERC_IR_GEN_API_MINOR, 0};
    API.Context = this;
    API.GetGeneratePhaseInput = getGeneratePhaseInput;
    API.CreateModule = createModule;
    API.ImportModule = importModule;
    API.PublishModule = publishModule;
    API.GetModuleArtifactInfo = getModuleArtifactInfo;
  }

  const NevercIRGenAPI &api() const { return API; }

  void attach(PluginTaskContext &Task,
              PluginIRGenProviderRuntime::Impl &Runtime) {
    std::lock_guard<std::mutex> Lock(Mutex);
    Tasks[std::make_pair(Task.handle().Owner, Task.handle().Value)] = &Runtime;
  }

  void detach(NevercTaskHandle Task) {
    std::lock_guard<std::mutex> Lock(Mutex);
    Tasks.erase(std::make_pair(Task.Owner, Task.Value));
  }

  void taskScopeUnregistered(NevercTaskHandle Task) noexcept override {
    detach(Task);
  }

private:
  PluginIRGenProviderRuntime::Impl *find(NevercTaskHandle Task) {
    std::lock_guard<std::mutex> Lock(Mutex);
    auto It = Tasks.find(std::make_pair(Task.Owner, Task.Value));
    return It == Tasks.end() ? nullptr : It->second;
  }

  static IRGenProcessBridge *bridge(void *Context) {
    return static_cast<IRGenProcessBridge *>(Context);
  }

  static NevercStatus NEVERC_CALL getGeneratePhaseInput(
      void *Context, const NevercPhaseFrame *Frame,
      NevercArtifactHandle Input, NevercIRGeneratePhaseInput *OutInput) {
    if (!Context || !Frame)
      return irGenStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    auto *Runtime = bridge(Context)->find(Frame->Task);
    return Runtime
               ? Runtime->getGeneratePhaseInput(Frame, Input, OutInput)
               : irGenStatus(NEVERC_STATUS_STALE_HANDLE);
  }

  static NevercStatus NEVERC_CALL createModule(
      void *Context, const NevercPhaseFrame *Frame,
      NevercStringView ModuleIdentifier,
      const NevercIRCoreAPI **OutCoreAPI,
      const NevercIRBuilderAPI **OutBuilderAPI) {
    if (!Context || !Frame)
      return irGenStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    auto *Runtime = bridge(Context)->find(Frame->Task);
    return Runtime ? Runtime->createModule(Frame, ModuleIdentifier, OutCoreAPI,
                                           OutBuilderAPI)
                   : irGenStatus(NEVERC_STATUS_STALE_HANDLE);
  }

  static NevercStatus NEVERC_CALL importModule(
      void *Context, const NevercPhaseFrame *Frame,
      NevercIRSerializationFormat Format, NevercByteView Bytes,
      const NevercIRCoreAPI **OutCoreAPI,
      const NevercIRBuilderAPI **OutBuilderAPI) {
    if (!Context || !Frame)
      return irGenStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    auto *Runtime = bridge(Context)->find(Frame->Task);
    return Runtime ? Runtime->importModule(Frame, Format, Bytes, OutCoreAPI,
                                           OutBuilderAPI)
                   : irGenStatus(NEVERC_STATUS_STALE_HANDLE);
  }

  static NevercStatus NEVERC_CALL publishModule(
      void *Context, const NevercPhaseFrame *Frame,
      const NevercIRModuleArtifactDescriptor *Descriptor,
      NevercArtifactHandle *OutOutput) {
    if (!Context || !Frame)
      return irGenStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    auto *Runtime = bridge(Context)->find(Frame->Task);
    return Runtime ? Runtime->publishModule(Frame, Descriptor, OutOutput)
                   : irGenStatus(NEVERC_STATUS_STALE_HANDLE);
  }

  static NevercStatus NEVERC_CALL getModuleArtifactInfo(
      void *Context, const NevercPhaseFrame *Frame,
      NevercArtifactHandle Module, NevercIRModuleArtifactInfo *OutInfo) {
    if (!Context || !Frame)
      return irGenStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    auto *Runtime = bridge(Context)->find(Frame->Task);
    return Runtime
               ? Runtime->getModuleArtifactInfo(Frame, Module, OutInfo)
               : irGenStatus(NEVERC_STATUS_STALE_HANDLE);
  }

  PluginProcessServices &Services;
  NevercIRGenAPI API{};
  std::mutex Mutex;
  std::map<std::pair<uint64_t, uint64_t>,
           PluginIRGenProviderRuntime::Impl *>
      Tasks;
};

std::shared_ptr<IRGenProcessBridge>
findIRGenProcessBridge(PluginProcessServices &Services) {
  return std::static_pointer_cast<IRGenProcessBridge>(
      Services.findHostService(irGenInterfaceID()));
}

} // namespace

bool PluginIRGenProviderRuntime::Impl::validFrame(
    const NevercPhaseFrame *Frame) const {
  return Frame && ActiveSemantic && sameHandle(Frame->Task, Task.handle()) &&
         samePluginInterfaceID(Frame->Phase, irGeneratePhaseID());
}

NevercStatus PluginIRGenProviderRuntime::Impl::getGeneratePhaseInput(
    const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
    NevercIRGeneratePhaseInput *OutInput) {
  if (!validFrame(Frame) || !OutInput || !sameHandle(Input, Frame->Input))
    return irGenStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  const void *Payload = nullptr;
  NevercStatus Status = Executor->resolveArtifactPayload(
      Task, Input, semanticUnitArtifactID(), &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  const auto &Semantic =
      *static_cast<const SemanticUnitArtifact *>(Payload);

  NevercIRGeneratePhaseInput Value{};
  Value.Header = {sizeof(Value), NEVERC_IR_GEN_API_MAJOR,
                  NEVERC_IR_GEN_API_MINOR, 0};
  Value.SemanticUnit = Input;
  Value.SemanticProduct = Semantic.Product;
  Value.TargetTriple = stringView(TargetTriple);
  Value.DataLayout = stringView(DataLayout);
  Value.SourceIdentity = stringView(Semantic.SourceIdentity);
  Value.SourceDigest = {
      Semantic.SourceDigest.data(),
      Semantic.HasSourceDigest ? Semantic.SourceDigest.size() : 0};
  return writeCallerRecord(OutInput, Value);
}

NevercStatus PluginIRGenProviderRuntime::Impl::createModule(
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
    return irGenStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  if (BuildingBridge)
    return irGenStatus(NEVERC_STATUS_BUSY);

  auto Created = IRPluginBridge::createInContext(Task, Context, Identifier);
  if (!Created) {
    consumeError(Created.takeError());
    return irGenStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  BuildingBridge = std::shared_ptr<IRPluginBridge>(std::move(*Created));
  BuildingBridge->module().setTargetTriple(TargetTriple);
  BuildingBridge->module().setDataLayout(DataLayout);
  *OutCoreAPI = &BuildingBridge->coreAPI();
  *OutBuilderAPI = &BuildingBridge->builderAPI();
  return neverc_status_ok();
}

NevercStatus PluginIRGenProviderRuntime::Impl::importModule(
    const NevercPhaseFrame *Frame, NevercIRSerializationFormat Format,
    NevercByteView Bytes, const NevercIRCoreAPI **OutCoreAPI,
    const NevercIRBuilderAPI **OutBuilderAPI) {
  if (!validBytes(Bytes))
    return irGenStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  static constexpr char ImportedName[] = "plugin-imported-module";
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

NevercStatus PluginIRGenProviderRuntime::Impl::publishModule(
    const NevercPhaseFrame *Frame,
    const NevercIRModuleArtifactDescriptor *Descriptor,
    NevercArtifactHandle *OutOutput) {
  if (OutOutput)
    *OutOutput = {};
  if (!validFrame(Frame) || !Descriptor || !OutOutput || !BuildingBridge ||
      !validHeader(Descriptor->Header, sizeof(*Descriptor)) ||
      !nonnull(Descriptor->Product) ||
      !validBytes(Descriptor->DependencyDigest) ||
      Descriptor->DependencyDigest.Length != 32 ||
      Descriptor->Reserved[0] != 0 || Descriptor->Reserved[1] != 0)
    return irGenStatus(NEVERC_STATUS_INVALID_ARGUMENT);

  auto *Candidate = new (std::nothrow) IRModuleArtifact;
  if (!Candidate)
    return irGenStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  Candidate->Bridge = BuildingBridge;
  Candidate->Product = Descriptor->Product;
  Candidate->TargetTriple = TargetTriple;
  Candidate->DataLayout = DataLayout;
  Candidate->Generation = BuildingBridge->mutationGeneration();
  std::copy_n(Descriptor->DependencyDigest.Data,
              Candidate->DependencyDigest.size(),
              Candidate->DependencyDigest.begin());
  Candidate->HasDependencyDigest = true;
  auto Handle =
      Executor->createCandidate(Task, irModuleArtifactID(), Candidate);
  if (!Handle) {
    delete Candidate;
    consumeError(Handle.takeError());
    return irGenStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  BuildingBridge.reset();
  *OutOutput = *Handle;
  return neverc_status_ok();
}

NevercStatus PluginIRGenProviderRuntime::Impl::getModuleArtifactInfo(
    const NevercPhaseFrame *Frame, NevercArtifactHandle ModuleHandle,
    NevercIRModuleArtifactInfo *OutInfo) {
  if (!validFrame(Frame) || !OutInfo)
    return irGenStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  const void *Payload = nullptr;
  NevercStatus Status = Executor->resolveArtifactPayload(
      Task, ModuleHandle, irModuleArtifactID(), &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  const auto &Artifact = *static_cast<const IRModuleArtifact *>(Payload);
  NevercIRModuleArtifactInfo Info{};
  Info.Header = {sizeof(Info), NEVERC_IR_GEN_API_MAJOR,
                 NEVERC_IR_GEN_API_MINOR, 0};
  Info.Product = Artifact.Product;
  Info.TargetTriple = stringView(Artifact.TargetTriple);
  Info.DataLayout = stringView(Artifact.DataLayout);
  Info.Generation = Artifact.Generation;
  Info.DependencyDigest = {
      Artifact.DependencyDigest.data(),
      Artifact.HasDependencyDigest ? Artifact.DependencyDigest.size() : 0};
  return writeCallerRecord(OutInfo, Info);
}

NevercStatus PluginIRGenProviderRuntime::Impl::builtinProvider(
    const NevercPhaseFrame *Frame, NevercPhaseResult *Result) {
  FailureMessage.clear();
  if (!validFrame(Frame) || !Result || !GenerateBuiltin)
    return irGenStatus(NEVERC_STATUS_INVALID_STATE);
  if (!samePluginInterfaceID(ActiveSemantic->Product,
                             standardSemanticProductID())) {
    FailureMessage =
        "semantic product has no matching downstream IR provider";
    return irGenStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
  }
  auto Generated = GenerateBuiltin();
  if (!Generated) {
    FailureMessage = toString(Generated.takeError()).str().str();
    return irGenStatus(NEVERC_STATUS_PLUGIN_FAILURE);
  }
  Module *GeneratedModule = *Generated;
  if (!GeneratedModule) {
    FailureMessage = "builtin IRGen produced no module";
    return irGenStatus(NEVERC_STATUS_VERIFICATION_FAILED);
  }

  auto *Candidate = new (std::nothrow) IRModuleArtifact;
  if (!Candidate)
    return irGenStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  Candidate->BorrowedModule = GeneratedModule;
  Candidate->Product = standardIRModuleProductID();
  Candidate->TargetTriple = TargetTriple;
  Candidate->DataLayout = DataLayout;
  Candidate->Generation = ++BuiltinGeneration;
  Candidate->DependencyDigest = ActiveSemantic->SourceDigest;
  Candidate->HasDependencyDigest = ActiveSemantic->HasSourceDigest;
  auto Handle =
      Executor->createCandidate(Task, irModuleArtifactID(), Candidate);
  if (!Handle) {
    delete Candidate;
    consumeError(Handle.takeError());
    return irGenStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }

  *Result = {};
  Result->Header = {sizeof(*Result), NEVERC_PLUGIN_ABI_MAJOR,
                    NEVERC_PLUGIN_ABI_MINOR, 0};
  Result->Action = NEVERC_PHASE_REPLACE;
  Result->Output = *Handle;
  return neverc_status_ok();
}

IRModuleArtifact *
PluginIRGenProviderRuntime::Impl::publishedArtifact() const {
  if (!Output)
    return nullptr;
  PluginArtifactSlot::Snapshot Snapshot = Output->snapshot();
  return const_cast<IRModuleArtifact *>(
      static_cast<const IRModuleArtifact *>(Snapshot.Payload));
}

PluginIRGenProviderRuntime::PluginIRGenProviderRuntime(
    std::unique_ptr<Impl> StateValue)
    : State(std::move(StateValue)) {}

PluginIRGenProviderRuntime::~PluginIRGenProviderRuntime() {
  if (State && State->ProcessBridge)
    State->ProcessBridge->detach(State->Task.handle());
}

Expected<std::unique_ptr<PluginIRGenProviderRuntime>>
PluginIRGenProviderRuntime::create(
    PluginTaskContext &Task, LLVMContext &Context, StringRef TargetTriple,
    StringRef DataLayout, BuiltinGenerator GenerateBuiltin) {
  if (TargetTriple.empty() || DataLayout.empty() || !GenerateBuiltin)
    return createStringError(inconvertibleErrorCode(),
                             "IRGen runtime requires a target and provider");
  auto ProcessBridge = findIRGenProcessBridge(Task.processServices());
  if (!ProcessBridge)
    return createStringError(inconvertibleErrorCode(),
                             "plugin IRGen interface is not registered");
  auto Graph = PluginPhaseGraph::createBuiltinIRGraph();
  if (!Graph)
    return Graph.takeError();
  auto State = std::make_unique<Impl>(
      Task, Context, TargetTriple, DataLayout, std::move(GenerateBuiltin),
      std::move(*Graph));
  State->ProcessBridge = std::move(ProcessBridge);
  if (Error E = registerSemanticUnitArtifactType(State->Artifacts))
    return std::move(E);
  if (Error E = registerIRModuleArtifactType(State->Artifacts))
    return std::move(E);
  if (Error E = State->Artifacts.freeze())
    return std::move(E);
  State->Executor =
      std::make_unique<PluginPhaseExecutor>(State->Graph, State->Artifacts);
  if (Error E = State->Executor->importSessionRegistrations(Task.session()))
    return std::move(E);
  Impl *Raw = State.get();
  if (Error E = State->Executor->setBuiltinProvider(
          irGeneratePhaseID(),
          [Raw](const NevercPhaseFrame *Frame, NevercPhaseResult *Result) {
            return Raw->builtinProvider(Frame, Result);
          }))
    return std::move(E);
  if (Error E = State->Executor->freeze())
    return std::move(E);
  State->ProcessBridge->attach(Task, *State);
  return std::unique_ptr<PluginIRGenProviderRuntime>(
      new PluginIRGenProviderRuntime(std::move(State)));
}

Error PluginIRGenProviderRuntime::execute(
    const PluginSourcePhaseRuntime &SourcePhases) {
  if (!State)
    return createStringError(inconvertibleErrorCode(),
                             "IRGen runtime is not initialized");
  const void *Payload = SourcePhases.semanticUnitPayload();
  uint64_t Generation = SourcePhases.semanticUnitGeneration();
  if (!Payload || Generation == 0)
    return createStringError(inconvertibleErrorCode(),
                             "IRGen has no semantic-unit input");
  State->ActiveSemantic =
      static_cast<const SemanticUnitArtifact *>(Payload);
  State->BuildingBridge.reset();
  auto ResetActive = make_scope_exit([&] {
    State->ActiveSemantic = nullptr;
    State->BuildingBridge.reset();
  });
  auto Input = State->Executor->createArtifactView(
      State->Task, semanticUnitArtifactID(), Payload, Generation);
  if (!Input)
    return Input.takeError();
  auto ReleaseInput = make_scope_exit([&] {
    (void)State->Task.handles().release(*Input, PluginArtifactHandleKind);
  });
  State->Output = std::make_unique<PluginArtifactSlot>(
      State->Artifacts.find(irModuleArtifactID()));
  NevercPhaseRoute Route = defaultRoute();
  if (Error E = State->Executor->execute(
          State->Task.session(), State->Task, irGeneratePhaseID(), Route,
          *Input, *State->Output)) {
    if (!State->FailureMessage.empty())
      return joinErrors(
          std::move(E),
          createStringError(inconvertibleErrorCode(), State->FailureMessage));
    return E;
  }
  if (!State->publishedArtifact())
    return createStringError(inconvertibleErrorCode(),
                             "IRGen phase published no module");
  return Error::success();
}

Module *PluginIRGenProviderRuntime::module() const {
  if (!State)
    return nullptr;
  IRModuleArtifact *Artifact = State->publishedArtifact();
  return Artifact ? getIRModule(*Artifact) : nullptr;
}

bool PluginIRGenProviderRuntime::ownsModule() const {
  if (!State)
    return false;
  IRModuleArtifact *Artifact = State->publishedArtifact();
  return Artifact && static_cast<bool>(Artifact->Bridge);
}

std::unique_ptr<Module>
PluginIRGenProviderRuntime::releaseOwnedModule() {
  if (!State)
    return nullptr;
  IRModuleArtifact *Artifact = State->publishedArtifact();
  if (!Artifact || !Artifact->Bridge)
    return nullptr;
  std::unique_ptr<Module> Result = Artifact->Bridge->releaseModule();
  Artifact->BorrowedModule = Result.get();
  Artifact->Bridge.reset();
  return Result;
}

Error registerPluginIRInterface(PluginProcessServices &Services) {
  if (Services.interfaces().isFrozen())
    return createStringError(
        inconvertibleErrorCode(),
        "cannot register plugin IR interface after interface freeze");
  auto Bridge = std::make_shared<IRGenProcessBridge>(Services);
  if (Error E =
          Services.registerHostService(irGenInterfaceID(), Bridge))
    return E;
  if (Error E = Services.interfaces().registerInterface(
          irGenInterfaceID(), NEVERC_IR_GEN_INTERFACE_STABILITY,
          &Bridge->api(), {}))
    return E;
  if (Error E = registerPluginIROptimizationInterface(Services))
    return E;
  if (Error E = registerPluginIRPassInterface(Services))
    return E;
  return registerPluginMIRInterface(Services);
}

} // namespace neverc::plugin
