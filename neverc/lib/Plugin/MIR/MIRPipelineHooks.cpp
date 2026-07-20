#include "neverc/Plugin/Host/PluginCodeGenPipeline.h"
#include "../IR/IRModuleArtifact.h"
#include "MIRModuleArtifact.h"
#include "neverc/Plugin/Host/IRPluginBridge.h"
#include "neverc/Plugin/Host/MCEmissionPlan.h"
#include "neverc/Plugin/Host/MCPluginBridge.h"
#include "neverc/Plugin/Host/MIRPluginBridge.h"
#include "neverc/Plugin/Host/MIRToMCProvider.h"
#include "neverc/Plugin/Host/MachineEmissionBridge.h"
#include "neverc/Plugin/Host/PluginArtifactRegistry.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginPhaseExecutor.h"
#include "neverc/Plugin/Host/PluginPhaseGraph.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTargetRegistry.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "neverc/Plugin/Host/IRToMIRProvider.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/Errc.h"
#include "llvm/Target/TargetMachine.h"
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;

namespace neverc::plugin {
namespace {

NevercInterfaceID mirProviderInterfaceID() {
  return {NEVERC_INTERFACE_MIR_PROVIDER_HIGH,
          NEVERC_INTERFACE_MIR_PROVIDER_LOW};
}

NevercInterfaceID mcProviderInterfaceID() {
  return {NEVERC_INTERFACE_MC_PROVIDER_HIGH,
          NEVERC_INTERFACE_MC_PROVIDER_LOW};
}

NevercInterfaceID irToMIRPhaseID() {
  return {NEVERC_PHASE_CODEGEN_IR_TO_MIR_HIGH,
          NEVERC_PHASE_CODEGEN_IR_TO_MIR_LOW};
}

NevercInterfaceID mirToMCPhaseID() {
  return {NEVERC_PHASE_CODEGEN_MIR_TO_MC_HIGH,
          NEVERC_PHASE_CODEGEN_MIR_TO_MC_LOW};
}

NevercInterfaceID mcUnitArtifactID() {
  return {NEVERC_PHASE_CODEGEN_MIR_TO_MC_OUTPUT_HIGH,
          NEVERC_PHASE_CODEGEN_MIR_TO_MC_OUTPUT_LOW};
}

bool sameHandle(NevercHandle Left, NevercHandle Right) {
  return Left.Owner == Right.Owner && Left.Value == Right.Value;
}

bool sameID(NevercInterfaceID Left, NevercInterfaceID Right) {
  return Left.High == Right.High && Left.Low == Right.Low;
}

bool nonzero(NevercInterfaceID ID) {
  return ID.High != 0 || ID.Low != 0;
}

NevercStatus providerStatus(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

bool validBool(NevercBool Value) {
  return Value == NEVERC_FALSE || Value == NEVERC_TRUE;
}

StringRef view(NevercStringView Value) {
  return StringRef(Value.Data ? Value.Data : "",
                   static_cast<size_t>(Value.Length));
}

std::string joinedFeatures(const NevercStringArrayView &Features) {
  std::vector<std::string> Values;
  const auto *Data = reinterpret_cast<const uint8_t *>(Features.Data);
  for (uint64_t I = 0; Data && I != Features.Count; ++I) {
    const auto *Value = reinterpret_cast<const NevercStringView *>(
        Data + I * Features.ElementStride);
    Values.push_back(view(*Value).str());
  }
  return llvm::join(Values, ",");
}

template <typename T>
NevercStatus writeRecord(T *Output, const T &Value, uint16_t Major) {
  if (!Output || Output->Header.StructSize < sizeof(T) ||
      Output->Header.Major != Major)
    return providerStatus(NEVERC_STATUS_ABI_MISMATCH);
  *Output = Value;
  return neverc_status_ok();
}

Error pipelineError(const Twine &Message) {
  return createStringError(errc::invalid_argument, Message);
}

class IRToMIRPipelinePass final : public ModulePass {
public:
  static char ID;

  IRToMIRPipelinePass(
      std::shared_ptr<PluginCodeGenPipelineRuntime> RuntimeValue,
      LLVMTargetMachine &TargetMachineValue,
      MachineModuleInfoWrapperPass &MMIValue, bool VerifyValue)
      : ModulePass(ID), Runtime(std::move(RuntimeValue)),
        TargetMachine(TargetMachineValue), MMI(MMIValue),
        Verify(VerifyValue) {}

  StringRef getPassName() const override {
    return "NeverC plugin IR-to-MIR replacement";
  }

  void getAnalysisUsage(AnalysisUsage &Usage) const override {
    Usage.addPreserved<MachineModuleInfoWrapperPass>();
    Usage.setPreservesAll();
  }

  bool runOnModule(Module &ModuleValue) override {
    if (Error E =
            Runtime->runIRToMIR(ModuleValue, TargetMachine, MMI, Verify))
      ModuleValue.getContext().emitError(toString(std::move(E)));
    return false;
  }

private:
  std::shared_ptr<PluginCodeGenPipelineRuntime> Runtime;
  LLVMTargetMachine &TargetMachine;
  MachineModuleInfoWrapperPass &MMI;
  bool Verify;
};

char IRToMIRPipelinePass::ID = 0;

class MIRToMCPipelinePass final : public ModulePass {
public:
  static char ID;

  MIRToMCPipelinePass(
      std::shared_ptr<PluginCodeGenPipelineRuntime> RuntimeValue,
      LLVMTargetMachine &TargetMachineValue,
      MachineModuleInfoWrapperPass &MMIValue, bool VerifyValue)
      : ModulePass(ID), Runtime(std::move(RuntimeValue)),
        TargetMachine(TargetMachineValue), MMI(MMIValue),
        Verify(VerifyValue) {}

  StringRef getPassName() const override {
    return "NeverC plugin MIR-to-MC replacement";
  }

  void getAnalysisUsage(AnalysisUsage &Usage) const override {
    Usage.addPreserved<MachineModuleInfoWrapperPass>();
    Usage.setPreservesAll();
  }

  bool runOnModule(Module &ModuleValue) override {
    if (Error E =
            Runtime->runMIRToMC(ModuleValue, TargetMachine, MMI, Verify))
      ModuleValue.getContext().emitError(toString(std::move(E)));
    return false;
  }

private:
  std::shared_ptr<PluginCodeGenPipelineRuntime> Runtime;
  LLVMTargetMachine &TargetMachine;
  MachineModuleInfoWrapperPass &MMI;
  bool Verify;
};

char MIRToMCPipelinePass::ID = 0;

} // namespace

struct PluginCodeGenPipelineRuntime::Impl {
  class ProviderService final : public PluginHostService {
  public:
    ProviderService() {
      MIRAPI.Header = {sizeof(MIRAPI), NEVERC_MIR_PROVIDER_API_MAJOR,
                       NEVERC_MIR_PROVIDER_API_MINOR, 0};
      MIRAPI.Context = this;
      MIRAPI.GetIRToMIRInput = getIRToMIRInput;
      MIRAPI.GetOrCreateMachineFunction = getOrCreateMachineFunction;
      MIRAPI.PublishMIRModule = publishMIRModule;

      MCAPI.Header = {sizeof(MCAPI), NEVERC_MC_PROVIDER_API_MAJOR,
                      NEVERC_MC_PROVIDER_API_MINOR, 0};
      MCAPI.Context = this;
      MCAPI.GetMIRToMCInput = getMIRToMCInput;
      MCAPI.GetMachineFunction = getMachineFunction;
      MCAPI.GetMCBuilder = getMCBuilder;
      MCAPI.PublishMCUnit = publishMCUnit;
    }

    const NevercMIRProviderAPI &mirAPI() const { return MIRAPI; }
    const NevercMCProviderAPI &mcAPI() const { return MCAPI; }

    Error attach(Impl &Runtime) {
      std::lock_guard<std::mutex> Lock(Mutex);
      const auto Key =
          std::make_pair(Runtime.Task.handle().Owner,
                         Runtime.Task.handle().Value);
      if (Active.count(Key) != 0)
        return pipelineError(
            "codegen provider runtime is already active for this task");
      Active[Key] = &Runtime;
      return Error::success();
    }

    void detach(NevercTaskHandle Task) {
      std::lock_guard<std::mutex> Lock(Mutex);
      Active.erase(std::make_pair(Task.Owner, Task.Value));
    }

    void taskScopeUnregistered(NevercTaskHandle Task) noexcept override {
      detach(Task);
    }

  private:
    Impl *find(NevercTaskHandle Task) {
      std::lock_guard<std::mutex> Lock(Mutex);
      auto It = Active.find(std::make_pair(Task.Owner, Task.Value));
      return It == Active.end() ? nullptr : It->second;
    }

    static ProviderService *service(void *Context) {
      return static_cast<ProviderService *>(Context);
    }

    static NevercStatus NEVERC_CALL getIRToMIRInput(
        void *Context, const NevercPhaseFrame *Frame,
        NevercArtifactHandle Input, NevercIRToMIRInputInfo *OutInfo) {
      if (!Context || !Frame)
        return providerStatus(NEVERC_STATUS_INVALID_ARGUMENT);
      Impl *Runtime = service(Context)->find(Frame->Task);
      return Runtime
                 ? Runtime->getIRToMIRInput(Frame, Input, OutInfo)
                 : providerStatus(NEVERC_STATUS_STALE_HANDLE);
    }

    static NevercStatus NEVERC_CALL getOrCreateMachineFunction(
        void *Context, const NevercPhaseFrame *Frame,
        NevercIRValueHandle Function, const NevercMIRAPI **OutMIR,
        NevercMachineFunctionHandle *OutFunction) {
      if (!Context || !Frame)
        return providerStatus(NEVERC_STATUS_INVALID_ARGUMENT);
      Impl *Runtime = service(Context)->find(Frame->Task);
      return Runtime
                 ? Runtime->getOrCreateMachineFunction(
                       Frame, Function, OutMIR, OutFunction)
                 : providerStatus(NEVERC_STATUS_STALE_HANDLE);
    }

    static NevercStatus NEVERC_CALL publishMIRModule(
        void *Context, const NevercPhaseFrame *Frame,
        const NevercMIRModuleCoverageDescriptor *Coverage,
        NevercArtifactHandle *OutModule) {
      if (!Context || !Frame)
        return providerStatus(NEVERC_STATUS_INVALID_ARGUMENT);
      Impl *Runtime = service(Context)->find(Frame->Task);
      return Runtime
                 ? Runtime->publishMIRModule(Frame, Coverage, OutModule)
                 : providerStatus(NEVERC_STATUS_STALE_HANDLE);
    }

    static NevercStatus NEVERC_CALL getMIRToMCInput(
        void *Context, const NevercPhaseFrame *Frame,
        NevercArtifactHandle Input, NevercMIRToMCInputInfo *OutInfo) {
      if (!Context || !Frame)
        return providerStatus(NEVERC_STATUS_INVALID_ARGUMENT);
      Impl *Runtime = service(Context)->find(Frame->Task);
      return Runtime
                 ? Runtime->getMIRToMCInput(Frame, Input, OutInfo)
                 : providerStatus(NEVERC_STATUS_STALE_HANDLE);
    }

    static NevercStatus NEVERC_CALL getMachineFunction(
        void *Context, const NevercPhaseFrame *Frame, uint64_t Index,
        const NevercMIRAPI **OutMIR,
        NevercMachineFunctionHandle *OutFunction) {
      if (!Context || !Frame)
        return providerStatus(NEVERC_STATUS_INVALID_ARGUMENT);
      Impl *Runtime = service(Context)->find(Frame->Task);
      return Runtime
                 ? Runtime->getMachineFunction(
                       Frame, Index, OutMIR, OutFunction)
                 : providerStatus(NEVERC_STATUS_STALE_HANDLE);
    }

    static NevercStatus NEVERC_CALL getMCBuilder(
        void *Context, const NevercPhaseFrame *Frame,
        const NevercMCAPI **OutMC, NevercMCUnitHandle *OutUnit) {
      if (!Context || !Frame)
        return providerStatus(NEVERC_STATUS_INVALID_ARGUMENT);
      Impl *Runtime = service(Context)->find(Frame->Task);
      return Runtime ? Runtime->getMCBuilder(Frame, OutMC, OutUnit)
                     : providerStatus(NEVERC_STATUS_STALE_HANDLE);
    }

    static NevercStatus NEVERC_CALL publishMCUnit(
        void *Context, const NevercPhaseFrame *Frame,
        NevercArtifactHandle *OutUnit) {
      if (!Context || !Frame)
        return providerStatus(NEVERC_STATUS_INVALID_ARGUMENT);
      Impl *Runtime = service(Context)->find(Frame->Task);
      return Runtime ? Runtime->publishMCUnit(Frame, OutUnit)
                     : providerStatus(NEVERC_STATUS_STALE_HANDLE);
    }

    NevercMIRProviderAPI MIRAPI{};
    NevercMCProviderAPI MCAPI{};
    std::mutex Mutex;
    std::map<std::pair<uint64_t, uint64_t>, Impl *> Active;
  };

  Impl(PluginTaskContext &TaskValue,
       std::shared_ptr<const PluginTargetSnapshot> SnapshotValue,
       PluginPhaseGraph GraphValue,
       std::shared_ptr<ProviderService> ServiceValue)
      : Task(TaskValue), Snapshot(std::move(SnapshotValue)),
        Graph(std::move(GraphValue)), Service(std::move(ServiceValue)) {}

  Error initialize() {
    Target = Snapshot ? Snapshot->selectedTarget() : nullptr;
    if (!Target)
      return pipelineError(
          "plugin codegen pipeline has no selected target");
    if (Target->Machine.SchemaDigest.empty())
      return pipelineError(
          "plugin codegen pipeline target has no schema digest");

    CompatibilityKey =
        Target->CanonicalName + ":" + Target->Machine.SchemaDigest;
    for (const auto &Edge : Snapshot->codeGenEdges()) {
      if (!sameID(Edge.TargetID, Target->ID) ||
          Edge.CompatibilityKey.empty())
        continue;
      const bool StandardEdge =
          (Edge.InputKind == NEVERC_CODEGEN_PRODUCT_IR &&
           Edge.OutputKind == NEVERC_CODEGEN_PRODUCT_MIR) ||
          (Edge.InputKind == NEVERC_CODEGEN_PRODUCT_MIR &&
           Edge.OutputKind == NEVERC_CODEGEN_PRODUCT_MC);
      if (!StandardEdge)
        continue;
      if (CompatibilityFromEdge &&
          CompatibilityKey != Edge.CompatibilityKey)
        return pipelineError(
            "standard codegen edges use incompatible compatibility keys");
      CompatibilityKey = Edge.CompatibilityKey;
      CompatibilityFromEdge = true;
    }

    if (const OwnedTargetKey *Key = Snapshot->targetKey()) {
      NevercTargetKey Value = Key->view();
      TargetTriple = view(Value.RawTriple).str();
      CPU = view(Value.CPU).str();
      Features = joinedFeatures(Value.Features);
      if (nonzero(Value.ObjectFormatID))
        if (const auto *Format =
                Snapshot->findObjectFormat(Value.ObjectFormatID))
          ObjectFormat = Format->CanonicalName;
    } else {
      TargetTriple = Target->Machine.RawTriple;
      CPU = Target->Machine.DefaultCPU;
    }

    if (Error E = registerIRModuleArtifactType(Artifacts))
      return E;
    if (Error E = registerMIRModuleArtifactType(Artifacts))
      return E;
    auto MCType = Artifacts.registerType(
        {mcUnitArtifactID(), "mc.unit", PluginArtifactOwnership::Borrowed,
         {}, {},
         [](const void *Payload) -> Error {
           if (!Payload)
             return pipelineError("MC unit artifact payload is null");
           return Error::success();
         }});
    if (!MCType)
      return MCType.takeError();
    if (Error E = Artifacts.freeze())
      return E;

    Executor = std::make_unique<PluginPhaseExecutor>(Graph, Artifacts);
    if (Error E = Executor->importSessionRegistrations(Task.session()))
      return E;
    ReplaceIRToMIR = Executor->hasProvider(irToMIRPhaseID());
    ReplaceMIRToMC = Executor->hasProvider(mirToMCPhaseID());
    return Executor->freeze();
  }

  NevercPhaseRoute route() const {
    NevercPhaseRoute Route{};
    Route.Header = {sizeof(Route), NEVERC_PLUGIN_ABI_MAJOR,
                    NEVERC_PLUGIN_ABI_MINOR, 0};
    Route.TargetTriple = {TargetTriple.data(), TargetTriple.size()};
    Route.CPU = {CPU.data(), CPU.size()};
    Route.Features = {Features.data(), Features.size()};
    Route.ObjectFormat = {ObjectFormat.data(), ObjectFormat.size()};
    return Route;
  }

  bool validFrame(const NevercPhaseFrame *Frame,
                  NevercInterfaceID Phase) const {
    return Frame && sameHandle(Frame->Task, Task.handle()) &&
           sameID(Frame->Phase, Phase);
  }

  Error beginActiveScope() {
    if (Error E = Service->attach(*this))
      return E;
    return Error::success();
  }

  void endActiveScope() {
    Service->detach(Task.handle());
    FunctionBridges.clear();
    DefinedFunctions.clear();
    IRBridge.reset();
    ActiveMIR = nullptr;
    ActiveEmission = nullptr;
    ActiveInput = {};
    MIRRootHandle = {};
    Published = false;
  }

  Error executeIRPhase(MIRModuleArtifact &Artifact) {
    ActiveMIR = &Artifact;
    Published = false;
    IRInput = {};
    IRInput.BorrowedModule = &Artifact.module();
    IRInput.Product = standardIRModuleProductID();
    IRInput.TargetTriple = Artifact.module().getTargetTriple();
    IRInput.DataLayout = Artifact.module().getDataLayoutStr();
    IRInput.Generation = Artifact.generation();
    IRInput.HasDependencyDigest = true;

    auto CreatedIRBridge = IRPluginBridge::borrow(Task, Artifact.module());
    if (!CreatedIRBridge)
      return CreatedIRBridge.takeError();
    IRBridge = std::shared_ptr<IRPluginBridge>(
        std::move(*CreatedIRBridge));

    auto Root = Task.handles().create(PluginMIRModuleHandleKind, &Artifact);
    if (!Root)
      return Root.takeError();
    MIRRootHandle = *Root;
    const NevercMIRModuleHandle RootHandle = MIRRootHandle;
    auto ReleaseRoot = make_scope_exit([&] {
      (void)Task.handles().release(
          RootHandle, PluginMIRModuleHandleKind);
      MIRRootHandle = {};
    });

    auto Input = Executor->createArtifactView(
        Task, irModuleArtifactID(), &IRInput, IRInput.Generation);
    if (!Input)
      return Input.takeError();
    ActiveInput = *Input;
    const NevercArtifactHandle InputHandle = ActiveInput;
    auto ReleaseInput = make_scope_exit([&] {
      (void)Task.handles().release(
          InputHandle, PluginArtifactHandleKind);
      ActiveInput = {};
    });
    PluginArtifactSlot Output(Artifacts.find(mirModuleArtifactID()));
    if (Error E = beginActiveScope())
      return E;
    auto Detach = make_scope_exit([&] { endActiveScope(); });
    if (Error E = Executor->execute(
            Task.session(), Task, irToMIRPhaseID(), route(), *Input,
            Output))
      return E;
    if (!Published || Output.payload() != &Artifact)
      return pipelineError(
          "IR-to-MIR provider published a foreign MIR product");
    return Error::success();
  }

  Error executeMCPhase(MachineEmissionBridge &Bridge) {
    ActiveMIR = &Bridge.mir();
    ActiveEmission = &Bridge;
    Published = false;
    DefinedFunctions.clear();
    for (Function &FunctionValue : ActiveMIR->module())
      if (!FunctionValue.isDeclaration())
        DefinedFunctions.push_back(&FunctionValue);

    auto Root =
        Task.handles().create(PluginMIRModuleHandleKind, ActiveMIR);
    if (!Root)
      return Root.takeError();
    MIRRootHandle = *Root;
    const NevercMIRModuleHandle RootHandle = MIRRootHandle;
    auto ReleaseRoot = make_scope_exit([&] {
      (void)Task.handles().release(
          RootHandle, PluginMIRModuleHandleKind);
      MIRRootHandle = {};
    });

    auto Input = Executor->createArtifactView(
        Task, mirModuleArtifactID(), ActiveMIR,
        ActiveMIR->generation());
    if (!Input)
      return Input.takeError();
    ActiveInput = *Input;
    const NevercArtifactHandle InputHandle = ActiveInput;
    auto ReleaseInput = make_scope_exit([&] {
      (void)Task.handles().release(
          InputHandle, PluginArtifactHandleKind);
      ActiveInput = {};
    });
    PluginArtifactSlot Output(Artifacts.find(mcUnitArtifactID()));
    if (Error E = beginActiveScope())
      return E;
    auto Detach = make_scope_exit([&] { endActiveScope(); });
    if (Error E = Executor->execute(
            Task.session(), Task, mirToMCPhaseID(), route(), *Input,
            Output))
      return E;
    if (!Published || Output.payload() != &Bridge.unit())
      return pipelineError(
          "MIR-to-MC provider published a foreign MC product");
    return Error::success();
  }

  MIRPluginBridge *bridgeFor(Function &FunctionValue,
                             bool CreateMachineFunction) {
    for (auto &Entry : FunctionBridges)
      if (Entry.first == &FunctionValue)
        return Entry.second.get();
    MachineFunction *MF =
        ActiveMIR->getMachineFunction(FunctionValue);
    if (!MF && CreateMachineFunction)
      MF = &ActiveMIR->getOrCreateMachineFunction(FunctionValue);
    if (!MF)
      return nullptr;
    auto Bridge = std::make_unique<MIRPluginBridge>(
        Task, *MF, ActiveMIR->generation(), ActiveMIR->schemaDigest(),
        ActiveMIR->schemaDigest());
    MIRPluginBridge *Result = Bridge.get();
    FunctionBridges.push_back(
        {&FunctionValue, std::move(Bridge)});
    return Result;
  }

  NevercStatus getIRToMIRInput(
      const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
      NevercIRToMIRInputInfo *OutInfo) {
    if (!validFrame(Frame, irToMIRPhaseID()) || !ActiveMIR ||
        !IRBridge || !sameHandle(Input, Frame->Input) ||
        !sameHandle(Input, ActiveInput) || !OutInfo)
      return providerStatus(NEVERC_STATUS_WRONG_SCOPE);
    const void *Payload = nullptr;
    NevercStatus Status = Executor->resolveArtifactPayload(
        Task, Input, irModuleArtifactID(), &Payload);
    if (Status.Code != NEVERC_STATUS_OK || Payload != &IRInput)
      return providerStatus(NEVERC_STATUS_WRONG_SCOPE);

    uint64_t DefinedCount = 0;
    for (const Function &FunctionValue : ActiveMIR->module())
      DefinedCount += !FunctionValue.isDeclaration();
    NevercIRToMIRInputInfo Value{};
    Value.Header = {sizeof(Value), NEVERC_MIR_PROVIDER_API_MAJOR,
                    NEVERC_MIR_PROVIDER_API_MINOR, 0};
    Value.Module = IRBridge->moduleHandle();
    Value.IR = &IRBridge->coreAPI();
    Value.TargetID = ActiveMIR->targetID();
    Value.CompatibilityKey = {
        ActiveMIR->compatibilityKey().data(),
        ActiveMIR->compatibilityKey().size()};
    Value.TargetSchemaDigest = {
        ActiveMIR->schemaDigest().data(),
        ActiveMIR->schemaDigest().size()};
    Value.DefinedFunctionCount = DefinedCount;
    return writeRecord(OutInfo, Value, NEVERC_MIR_PROVIDER_API_MAJOR);
  }

  NevercStatus getOrCreateMachineFunction(
      const NevercPhaseFrame *Frame, NevercIRValueHandle FunctionHandle,
      const NevercMIRAPI **OutMIR,
      NevercMachineFunctionHandle *OutFunction) {
    if (OutMIR)
      *OutMIR = nullptr;
    if (OutFunction)
      *OutFunction = {};
    if (!validFrame(Frame, irToMIRPhaseID()) || !ActiveMIR ||
        !IRBridge || !OutMIR || !OutFunction)
      return providerStatus(NEVERC_STATUS_WRONG_SCOPE);
    Value *IRValue = nullptr;
    NevercStatus Status =
        IRBridge->resolveValue(FunctionHandle, &IRValue);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    auto *FunctionValue = dyn_cast_or_null<Function>(IRValue);
    if (!FunctionValue || FunctionValue->getParent() != &ActiveMIR->module() ||
        FunctionValue->isDeclaration())
      return providerStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    MIRPluginBridge *Bridge = bridgeFor(*FunctionValue, true);
    if (!Bridge)
      return providerStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    auto Wrapped = Bridge->machineFunction();
    if (!Wrapped) {
      consumeError(Wrapped.takeError());
      return providerStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    *OutMIR = &Bridge->api();
    *OutFunction = *Wrapped;
    return neverc_status_ok();
  }

  NevercStatus publishMIRModule(
      const NevercPhaseFrame *Frame,
      const NevercMIRModuleCoverageDescriptor *Coverage,
      NevercArtifactHandle *OutModule) {
    if (OutModule)
      *OutModule = {};
    if (!validFrame(Frame, irToMIRPhaseID()) || !ActiveMIR ||
        !Coverage || !OutModule || Published ||
        Coverage->Header.StructSize < sizeof(*Coverage) ||
        Coverage->Header.Major != NEVERC_MIR_PROVIDER_API_MAJOR ||
        Coverage->Header.Minor > NEVERC_MIR_PROVIDER_API_MINOR ||
        Coverage->Header.Flags != 0 || Coverage->Reserved != 0 ||
        !validBool(Coverage->HandlesGlobals) ||
        !validBool(Coverage->HandlesConstructors) ||
        !validBool(Coverage->HandlesDebugInfo) ||
        !validBool(Coverage->HandlesUnwind))
      return providerStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    ActiveMIR->setCoveragePolicy(
        {Coverage->HandlesGlobals == NEVERC_TRUE,
         Coverage->HandlesConstructors == NEVERC_TRUE,
         Coverage->HandlesDebugInfo == NEVERC_TRUE,
         Coverage->HandlesUnwind == NEVERC_TRUE});
    auto Candidate = Executor->createCandidate(
        Task, mirModuleArtifactID(), ActiveMIR);
    if (!Candidate) {
      consumeError(Candidate.takeError());
      return providerStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    *OutModule = *Candidate;
    Published = true;
    return neverc_status_ok();
  }

  NevercStatus getMIRToMCInput(
      const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
      NevercMIRToMCInputInfo *OutInfo) {
    if (!validFrame(Frame, mirToMCPhaseID()) || !ActiveMIR ||
        !sameHandle(Input, Frame->Input) ||
        !sameHandle(Input, ActiveInput) || !OutInfo)
      return providerStatus(NEVERC_STATUS_WRONG_SCOPE);
    const void *Payload = nullptr;
    NevercStatus Status = Executor->resolveArtifactPayload(
        Task, Input, mirModuleArtifactID(), &Payload);
    if (Status.Code != NEVERC_STATUS_OK || Payload != ActiveMIR)
      return providerStatus(NEVERC_STATUS_WRONG_SCOPE);
    NevercMIRToMCInputInfo Value{};
    Value.Header = {sizeof(Value), NEVERC_MC_PROVIDER_API_MAJOR,
                    NEVERC_MC_PROVIDER_API_MINOR, 0};
    Value.Module = MIRRootHandle;
    Value.TargetID = ActiveMIR->targetID();
    Value.CompatibilityKey = {
        ActiveMIR->compatibilityKey().data(),
        ActiveMIR->compatibilityKey().size()};
    Value.TargetSchemaDigest = {
        ActiveMIR->schemaDigest().data(),
        ActiveMIR->schemaDigest().size()};
    Value.DefinedFunctionCount = DefinedFunctions.size();
    return writeRecord(OutInfo, Value, NEVERC_MC_PROVIDER_API_MAJOR);
  }

  NevercStatus getMachineFunction(
      const NevercPhaseFrame *Frame, uint64_t Index,
      const NevercMIRAPI **OutMIR,
      NevercMachineFunctionHandle *OutFunction) {
    if (OutMIR)
      *OutMIR = nullptr;
    if (OutFunction)
      *OutFunction = {};
    if (!validFrame(Frame, mirToMCPhaseID()) || !ActiveMIR ||
        !OutMIR || !OutFunction || Index >= DefinedFunctions.size())
      return providerStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    MIRPluginBridge *Bridge =
        bridgeFor(*DefinedFunctions[static_cast<size_t>(Index)], false);
    if (!Bridge)
      return providerStatus(NEVERC_STATUS_INVALID_STATE);
    auto Wrapped = Bridge->machineFunction();
    if (!Wrapped) {
      consumeError(Wrapped.takeError());
      return providerStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    *OutMIR = &Bridge->api();
    *OutFunction = *Wrapped;
    return neverc_status_ok();
  }

  NevercStatus getMCBuilder(
      const NevercPhaseFrame *Frame, const NevercMCAPI **OutMC,
      NevercMCUnitHandle *OutUnit) {
    if (OutMC)
      *OutMC = nullptr;
    if (OutUnit)
      *OutUnit = {};
    if (!validFrame(Frame, mirToMCPhaseID()) || !ActiveEmission ||
        !OutMC || !OutUnit)
      return providerStatus(NEVERC_STATUS_WRONG_SCOPE);
    auto Unit = ActiveEmission->unitHandle();
    if (!Unit) {
      consumeError(Unit.takeError());
      return providerStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    *OutMC = &ActiveEmission->api();
    *OutUnit = *Unit;
    return neverc_status_ok();
  }

  NevercStatus publishMCUnit(const NevercPhaseFrame *Frame,
                             NevercArtifactHandle *OutUnit) {
    if (OutUnit)
      *OutUnit = {};
    if (!validFrame(Frame, mirToMCPhaseID()) || !ActiveEmission ||
        !OutUnit || Published)
      return providerStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    auto Candidate = Executor->createCandidate(
        Task, mcUnitArtifactID(), &ActiveEmission->unit());
    if (!Candidate) {
      consumeError(Candidate.takeError());
      return providerStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    *OutUnit = *Candidate;
    Published = true;
    return neverc_status_ok();
  }

  PluginTaskContext &Task;
  std::shared_ptr<const PluginTargetSnapshot> Snapshot;
  const PluginTargetSnapshot::TargetRecord *Target = nullptr;
  PluginPhaseGraph Graph;
  PluginArtifactRegistry Artifacts;
  std::unique_ptr<PluginPhaseExecutor> Executor;
  std::shared_ptr<ProviderService> Service;
  std::string CompatibilityKey;
  bool CompatibilityFromEdge = false;
  std::string TargetTriple;
  std::string CPU;
  std::string Features;
  std::string ObjectFormat;
  bool ReplaceIRToMIR = false;
  bool ReplaceMIRToMC = false;
  IRModuleArtifact IRInput;
  std::shared_ptr<IRPluginBridge> IRBridge;
  std::unique_ptr<MIRModuleArtifact> MIRProduct;
  std::unique_ptr<PluginMCUnit> MCProduct;
  MIRModuleArtifact *ActiveMIR = nullptr;
  MachineEmissionBridge *ActiveEmission = nullptr;
  NevercArtifactHandle ActiveInput{};
  NevercMIRModuleHandle MIRRootHandle{};
  bool Published = false;
  std::vector<Function *> DefinedFunctions;
  std::vector<
      std::pair<Function *, std::unique_ptr<MIRPluginBridge>>>
      FunctionBridges;
};

PluginCodeGenPipelineRuntime::PluginCodeGenPipelineRuntime(
    std::unique_ptr<Impl> StateValue)
    : State(std::move(StateValue)) {}

PluginCodeGenPipelineRuntime::~PluginCodeGenPipelineRuntime() = default;

Expected<std::shared_ptr<PluginCodeGenPipelineRuntime>>
PluginCodeGenPipelineRuntime::create(
    PluginTaskContext &Task,
    std::shared_ptr<const PluginTargetSnapshot> Snapshot) {
  if (!Snapshot)
    return pipelineError("plugin codegen pipeline has no target snapshot");
  auto Graph = PluginPhaseGraph::createBuiltinCodeGenGraph();
  if (!Graph)
    return Graph.takeError();
  auto Service = std::static_pointer_cast<Impl::ProviderService>(
      Task.processServices().findHostService(mirProviderInterfaceID()));
  if (!Service)
    return pipelineError(
        "plugin codegen provider interfaces are not registered");
  auto State = std::make_unique<Impl>(
      Task, std::move(Snapshot), std::move(*Graph), std::move(Service));
  if (Error E = State->initialize())
    return std::move(E);
  return std::shared_ptr<PluginCodeGenPipelineRuntime>(
      new PluginCodeGenPipelineRuntime(std::move(State)));
}

bool PluginCodeGenPipelineRuntime::replacesIRToMIR() const {
  return State->ReplaceIRToMIR;
}

bool PluginCodeGenPipelineRuntime::replacesMIRToMC() const {
  return State->ReplaceMIRToMC;
}

void PluginCodeGenPipelineRuntime::install(
    LLVMTargetMachine &TargetMachine, bool RunMachineVerifier) {
  std::shared_ptr<PluginCodeGenPipelineRuntime> Self = shared_from_this();
  if (replacesIRToMIR())
    TargetMachine.setMachinePipelineFactory(
        [Self, RunMachineVerifier](
            LLVMTargetMachine &TM, PassManagerBase &PM,
            MachineModuleInfoWrapperPass &MMI, bool DisableVerify) {
          PM.add(new IRToMIRPipelinePass(
              Self, TM, MMI,
              RunMachineVerifier && !DisableVerify));
          return false;
        });
  if (replacesMIRToMC())
    TargetMachine.setMachineEmissionFactory(
        [Self, RunMachineVerifier](
            LLVMTargetMachine &TM, PassManagerBase &PM,
            raw_pwrite_stream &, raw_pwrite_stream *, CodeGenFileType,
            MachineModuleInfoWrapperPass &MMI) {
          PM.add(new MIRToMCPipelinePass(
              Self, TM, MMI, RunMachineVerifier));
          return false;
        });
}

Error PluginCodeGenPipelineRuntime::runIRToMIR(
    Module &ModuleValue, LLVMTargetMachine &TargetMachine,
    MachineModuleInfoWrapperPass &MMI, bool RunMachineVerifier) {
  MIRModuleCoveragePolicy Coverage;
  IRToMIRExecutionRequest Request;
  Request.Module = &ModuleValue;
  Request.TargetMachine = &TargetMachine;
  Request.PipelineMMI = &MMI;
  Request.TargetID = State->Target->ID;
  Request.CompatibilityKey = State->CompatibilityKey;
  Request.SchemaDigest = State->Target->Machine.SchemaDigest;
  Request.Coverage = &Coverage;
  Request.HasFinalIRProof = true;
  Request.RunMachineVerifier = RunMachineVerifier;
  auto Product = IRToMIRProviderRuntime::execute(
      Request,
      [&](MIRModuleArtifact &Artifact) {
        return State->executeIRPhase(Artifact);
      },
      {});
  if (!Product)
    return Product.takeError();
  State->MIRProduct = std::move(*Product);
  return Error::success();
}

Error PluginCodeGenPipelineRuntime::runMIRToMC(
    Module &ModuleValue, LLVMTargetMachine &TargetMachine,
    MachineModuleInfoWrapperPass &MMI, bool RunMachineVerifier) {
  if (!State->MIRProduct) {
    State->MIRProduct = MIRModuleArtifact::borrow(
        ModuleValue, MMI, State->Target->ID, State->CompatibilityKey,
        State->Target->Machine.SchemaDigest);
    State->MIRProduct->setCoveragePolicy(
        {/*HandlesGlobals=*/true, /*HandlesConstructors=*/true,
         /*HandlesDebugInfo=*/true, /*HandlesUnwind=*/true});
  }
  if (&State->MIRProduct->module() != &ModuleValue)
    return pipelineError(
        "MIR-to-MC pipeline received a foreign LLVM module");

  MIRToMCExecutionRequest Request;
  Request.Task = &State->Task;
  Request.MIR = State->MIRProduct.get();
  Request.Snapshot = State->Snapshot.get();
  Request.HasFinalMIRProof = true;
  Request.RunMachineVerifier = RunMachineVerifier;
  auto Product = MIRToMCProviderRuntime::execute(
      Request,
      [&](MachineEmissionBridge &Bridge) {
        return State->executeMCPhase(Bridge);
      },
      {});
  if (!Product)
    return Product.takeError();
  State->MCProduct = std::move(*Product);
  return Error::success();
}

Error registerPluginCodeGenProviderInterfaces(
    PluginProcessServices &Services) {
  if (Services.interfaces().isFrozen())
    return pipelineError(
        "cannot register codegen provider interfaces after interface freeze");
  auto Service =
      std::make_shared<PluginCodeGenPipelineRuntime::Impl::ProviderService>();
  if (Error E =
          Services.registerHostService(mirProviderInterfaceID(), Service))
    return E;
  if (Error E =
          Services.registerHostService(mcProviderInterfaceID(), Service))
    return E;
  if (Error E = Services.interfaces().registerInterface(
          mirProviderInterfaceID(),
          NEVERC_MIR_PROVIDER_INTERFACE_STABILITY, &Service->mirAPI(), {}))
    return E;
  if (Error E = Services.interfaces().registerInterface(
          mcProviderInterfaceID(),
          NEVERC_MC_PROVIDER_INTERFACE_STABILITY, &Service->mcAPI(), {}))
    return E;
  return registerPluginMCEmissionInterface(Services);
}

} // namespace neverc::plugin
