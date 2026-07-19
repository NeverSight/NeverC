#include "neverc/Plugin/Host/IRPassPlugin.h"
#include "neverc/Plugin/Host/IRPluginBridge.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginInterfaceRegistry.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/LazyCallGraph.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar/LoopPassManager.h"
#include <algorithm>
#include <cstddef>
#include <functional>
#include <limits>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;

namespace neverc::plugin {
namespace {

bool sameInterface(NevercInterfaceID Left, NevercInterfaceID Right) {
  return Left.High == Right.High && Left.Low == Right.Low;
}

bool validPreservedAnalyses(const NevercIRPreservedAnalyses &Preserved) {
  constexpr uint64_t Required =
      offsetof(NevercIRPreservedAnalyses, Flags) +
      sizeof(NevercIRPreservedAnalyses::Flags);
  constexpr NevercIRPreservedAnalysisFlags Known =
      NEVERC_IR_PRESERVE_CFG | NEVERC_IR_PRESERVE_ALL;
  if (Preserved.Header.StructSize < Required ||
      Preserved.Header.Major != NEVERC_IR_PASS_API_MAJOR ||
      Preserved.Header.Minor > NEVERC_IR_PASS_API_MINOR ||
      Preserved.Header.Flags != 0 || (Preserved.Flags & ~Known) != 0 ||
      Preserved.Reserved[0] != 0 || Preserved.Reserved[1] != 0 ||
      Preserved.CustomAnalysisCount > 1024 ||
      (Preserved.CustomAnalysisCount != 0 && !Preserved.CustomAnalyses) ||
      ((Preserved.Flags & NEVERC_IR_PRESERVE_ALL) != 0 &&
       Preserved.Flags != NEVERC_IR_PRESERVE_ALL))
    return false;
  for (uint64_t I = 0; I != Preserved.CustomAnalysisCount; ++I) {
    NevercInterfaceID Analysis = Preserved.CustomAnalyses[I];
    if ((Analysis.High == 0 && Analysis.Low == 0))
      return false;
    for (uint64_t J = 0; J != I; ++J)
      if (sameInterface(Analysis, Preserved.CustomAnalyses[J]))
        return false;
  }
  return true;
}

NevercStatus NEVERC_CALL
registerPass(void *, void *RegistrarContext,
             const NevercIRPassDescriptor *Descriptor) {
  return registerPluginIRPass(RegistrarContext, Descriptor);
}

NevercStatus NEVERC_CALL
registerAnalysis(void *, void *RegistrarContext,
                 const NevercIRAnalysisDescriptor *Descriptor) {
  return registerPluginIRAnalysis(RegistrarContext, Descriptor);
}

const NevercIRPassAPI PassAPI = {
    {sizeof(NevercIRPassAPI), NEVERC_IR_PASS_API_MAJOR,
     NEVERC_IR_PASS_API_MINOR, 0},
    nullptr,
    registerPass,
};

NevercStatus analysisStatus(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

struct AnalysisBinding {
  std::string PluginID;
  std::string Name;
  NevercIRAnalysisDescriptor Descriptor{};
  std::vector<NevercInterfaceID> Dependencies;
};

struct CustomAnalysisScope {
  NevercIRPassLevel Level = NEVERC_IR_PASS_LEVEL_MODULE;
  Module *M = nullptr;
  Function *F = nullptr;
  BasicBlock *LoopHeader = nullptr;
  std::vector<Function *> SCCFunctions;

  bool operator==(const CustomAnalysisScope &Other) const {
    return Level == Other.Level && M == Other.M && F == Other.F &&
           LoopHeader == Other.LoopHeader &&
           SCCFunctions == Other.SCCFunctions;
  }
};

struct CustomAnalysisCacheEntry {
  NevercInterfaceID Analysis{};
  CustomAnalysisScope Scope;
  const AnalysisBinding *Binding = nullptr;
  void *Result = nullptr;
};

struct CustomAnalysisState {
  explicit CustomAnalysisState(PluginTaskContext &TaskValue) : Task(TaskValue) {}

  ~CustomAnalysisState() {
    std::lock_guard<std::recursive_mutex> Lock(Mutex);
    (void)discardEntries(
        [](const CustomAnalysisCacheEntry &) { return true; },
        NEVERC_IR_ANALYSIS_INVALIDATED_BY_PLAN_DESTROY);
  }

  const AnalysisBinding *findBinding(NevercInterfaceID Analysis) const {
    for (const AnalysisBinding &Binding : Bindings)
      if (sameInterface(Binding.Descriptor.AnalysisID, Analysis))
        return &Binding;
    return nullptr;
  }

  std::shared_ptr<CustomAnalysisCacheEntry>
  findEntry(NevercInterfaceID Analysis, const CustomAnalysisScope &Scope) {
    for (const auto &Entry : Cache)
      if (sameInterface(Entry->Analysis, Analysis) && Entry->Scope == Scope)
        return Entry;
    return {};
  }

  NevercStatus invalidate(const NevercIRPreservedAnalyses &Preserved) {
    std::lock_guard<std::recursive_mutex> Lock(Mutex);
    if ((Preserved.Flags & NEVERC_IR_PRESERVE_ALL) != 0)
      return neverc_status_ok();
    const auto IsPreserved = [&](NevercInterfaceID Analysis) {
      for (uint64_t I = 0; I != Preserved.CustomAnalysisCount; ++I)
        if (sameInterface(Analysis, Preserved.CustomAnalyses[I]))
          return true;
      return false;
    };
    std::function<bool(NevercInterfaceID)> CanPreserve =
        [&](NevercInterfaceID Analysis) {
          if (!IsPreserved(Analysis))
            return false;
          const AnalysisBinding *Binding = findBinding(Analysis);
          return Binding &&
                 llvm::all_of(Binding->Dependencies,
                              [&](NevercInterfaceID Dependency) {
                                return CanPreserve(Dependency);
                              });
        };
    return discardEntries(
        [&](const CustomAnalysisCacheEntry &Entry) {
          return !CanPreserve(Entry.Analysis);
        },
        NEVERC_IR_ANALYSIS_INVALIDATED_BY_PASS);
  }

  std::vector<AnalysisBinding> Bindings;
  std::vector<std::shared_ptr<CustomAnalysisCacheEntry>> Cache;
  std::recursive_mutex Mutex;

private:
  template <typename Predicate>
  NevercStatus
  discardEntries(Predicate ShouldDiscard,
                 NevercIRAnalysisInvalidationReason Reason) {
    NevercStatus FirstFailure = neverc_status_ok();
    std::vector<uint8_t> Remove(Cache.size(), 0);
    for (size_t I = 0; I != Cache.size(); ++I)
      Remove[I] = ShouldDiscard(*Cache[I]) ? 1 : 0;

    for (size_t I = Cache.size(); I != 0; --I) {
      if (!Remove[I - 1])
        continue;
      CustomAnalysisCacheEntry &Entry = *Cache[I - 1];
      const AnalysisBinding &Binding = *Entry.Binding;
      auto InvalidateStatus = Task.invokeCallback(
          Binding.PluginID, "ir-analysis-invalidate/" + Binding.Name,
          [&] {
            return Binding.Descriptor.Invalidate(
                Entry.Result, Reason, Binding.Descriptor.UserData);
          },
          false);
      if (!InvalidateStatus) {
        consumeError(InvalidateStatus.takeError());
        if (FirstFailure.Code == NEVERC_STATUS_OK)
          FirstFailure = analysisStatus(NEVERC_STATUS_PLUGIN_EXCEPTION);
      } else if (InvalidateStatus->Code != NEVERC_STATUS_OK &&
                 FirstFailure.Code == NEVERC_STATUS_OK) {
        FirstFailure = *InvalidateStatus;
      }

      auto DestroyStatus = Task.invokeCallback(
          Binding.PluginID, "ir-analysis-destroy/" + Binding.Name,
          [&] {
            Binding.Descriptor.Destroy(Entry.Result,
                                       Binding.Descriptor.UserData);
            return neverc_status_ok();
          },
          false);
      if (!DestroyStatus) {
        consumeError(DestroyStatus.takeError());
        if (FirstFailure.Code == NEVERC_STATUS_OK)
          FirstFailure = analysisStatus(NEVERC_STATUS_PLUGIN_EXCEPTION);
      } else if (DestroyStatus->Code != NEVERC_STATUS_OK &&
                 FirstFailure.Code == NEVERC_STATUS_OK) {
        FirstFailure = *DestroyStatus;
      }
      Entry.Result = nullptr;
    }

    size_t Index = 0;
    Cache.erase(std::remove_if(Cache.begin(), Cache.end(),
                               [&](const auto &) {
                                 return Remove[Index++] != 0;
                               }),
                Cache.end());
    return FirstFailure;
  }

  PluginTaskContext &Task;
};

struct AnalysisAccess {
  ModuleAnalysisManager *ModuleAnalyses = nullptr;
  FunctionAnalysisManager *FunctionAnalyses = nullptr;
  LazyCallGraph *CallGraph = nullptr;
  LoopStandardAnalysisResults *LoopAnalyses = nullptr;
  Function *FunctionScope = nullptr;
};

class AnalysisInvocationBridge {
public:
  AnalysisInvocationBridge(PluginTaskContext &TaskValue,
                           IRPluginBridge &IRValue, AnalysisAccess AccessValue,
                           CustomAnalysisState &CustomValue,
                           CustomAnalysisScope ScopeValue)
      : Task(TaskValue), IR(IRValue), Access(AccessValue),
        Custom(CustomValue), Scope(std::move(ScopeValue)),
        InitialMutationGeneration(IRValue.mutationGeneration()) {
    API.Header = {sizeof(API), NEVERC_IR_ANALYSIS_API_MAJOR,
                  NEVERC_IR_ANALYSIS_API_MINOR, 0};
    API.Context = this;
    API.QueryBuiltin = queryBuiltin;
    API.DominatorTreeDominates = dominatorTreeDominates;
    API.GetLoopCount = getLoopCount;
    API.GetLoopHeader = getLoopHeader;
    API.GetLoopForBlock = getLoopForBlock;
    API.GetScalarEvolutionConstantTripCount =
        getScalarEvolutionConstantTripCount;
    API.GetMemoryAccessKind = getMemoryAccessKind;
    API.GetDirectCalleeCount = getDirectCalleeCount;
    API.GetDirectCallee = getDirectCallee;
    API.Alias = alias;
    API.RegisterAnalysis = registerAnalysis;
    API.QueryCustom = queryCustom;
    API.GetCustomResultData = getCustomResultData;
  }

  ~AnalysisInvocationBridge() {
    for (const auto &Result : Results)
      (void)Task.handles().release(Result->Handle,
                                   PluginIRAnalysisResultHandleKind);
  }

  const NevercIRAnalysisAPI &api() const { return API; }

  NevercStatus requireCustomAnalysis(NevercInterfaceID Analysis) {
    NevercIRAnalysisResultHandle Result{};
    return queryCustomAnalysis(Analysis, &Result);
  }

private:
  struct ResultState {
    AnalysisInvocationBridge *Owner = nullptr;
    NevercIRBuiltinAnalysis Kind = 0;
    Function *Scope = nullptr;
    void *Payload = nullptr;
    void *SecondaryPayload = nullptr;
    NevercInterfaceID CustomAnalysis{};
    std::shared_ptr<CustomAnalysisCacheEntry> CustomEntry;
    NevercIRAnalysisResultHandle Handle{};
  };

  static AnalysisInvocationBridge *get(void *Context, NevercTaskHandle Task,
                                       NevercStatus &Status) {
    if (!Context) {
      Status = analysisStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
      return nullptr;
    }
    auto *Bridge = static_cast<AnalysisInvocationBridge *>(Context);
    NevercTaskHandle Expected = Bridge->Task.handle();
    if (Expected.Owner != Task.Owner || Expected.Value != Task.Value) {
      Status = analysisStatus(NEVERC_STATUS_WRONG_SCOPE);
      return nullptr;
    }
    if (Bridge->IR.mutationGeneration() !=
        Bridge->InitialMutationGeneration) {
      Status = analysisStatus(NEVERC_STATUS_STALE_HANDLE);
      return nullptr;
    }
    Status = neverc_status_ok();
    return Bridge;
  }

  NevercStatus resolveResult(NevercIRAnalysisResultHandle Handle,
                             NevercIRBuiltinAnalysis ExpectedKind,
                             ResultState **OutResult) {
    if (!OutResult)
      return analysisStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *OutResult = nullptr;
    void *Payload = nullptr;
    NevercStatus Status = Task.handles().resolve(
        Handle, PluginIRAnalysisResultHandleKind, &Payload);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    auto *Result = static_cast<ResultState *>(Payload);
    if (!Result || Result->Owner != this)
      return analysisStatus(NEVERC_STATUS_WRONG_SCOPE);
    if (Result->Kind != ExpectedKind)
      return analysisStatus(NEVERC_STATUS_WRONG_TYPE);
    *OutResult = Result;
    return neverc_status_ok();
  }

  NevercStatus resolveCustomResult(NevercIRAnalysisResultHandle Handle,
                                   ResultState **OutResult) {
    if (!OutResult)
      return analysisStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *OutResult = nullptr;
    void *Payload = nullptr;
    NevercStatus Status = Task.handles().resolve(
        Handle, PluginIRAnalysisResultHandleKind, &Payload);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    auto *Result = static_cast<ResultState *>(Payload);
    if (!Result || Result->Owner != this)
      return analysisStatus(NEVERC_STATUS_WRONG_SCOPE);
    if (!Result->CustomEntry || !Result->CustomEntry->Result)
      return analysisStatus(NEVERC_STATUS_WRONG_TYPE);
    *OutResult = Result;
    return neverc_status_ok();
  }

  NevercStatus resolveFunction(NevercIRValueHandle Handle,
                               Function **OutFunction) {
    if (!OutFunction)
      return analysisStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *OutFunction = nullptr;
    Value *Resolved = nullptr;
    NevercStatus Status = IR.resolveValue(Handle, &Resolved);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    auto *F = dyn_cast<Function>(Resolved);
    if (!F)
      return analysisStatus(NEVERC_STATUS_WRONG_TYPE);
    if (F->getParent() != &IR.module())
      return analysisStatus(NEVERC_STATUS_WRONG_SCOPE);
    *OutFunction = F;
    return neverc_status_ok();
  }

  NevercStatus query(NevercIRBuiltinAnalysis Analysis,
                     NevercIRValueHandle Scope,
                     NevercIRAnalysisResultHandle *OutResult) {
    if (!OutResult)
      return analysisStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *OutResult = {};

    Function *F = nullptr;
    if (Analysis != NEVERC_IR_ANALYSIS_CALL_GRAPH) {
      NevercStatus Status = resolveFunction(Scope, &F);
      if (Status.Code != NEVERC_STATUS_OK)
        return Status;
    }
    for (const auto &Existing : Results) {
      if (Existing->Kind == Analysis && Existing->Scope == F) {
        *OutResult = Existing->Handle;
        return neverc_status_ok();
      }
    }

    void *Payload = nullptr;
    void *SecondaryPayload = nullptr;
    switch (Analysis) {
    case NEVERC_IR_ANALYSIS_DOMINATOR_TREE:
      if (Access.FunctionAnalyses)
        Payload = &Access.FunctionAnalyses
                       ->getResult<DominatorTreeAnalysis>(*F);
      else if (Access.LoopAnalyses && Access.FunctionScope == F)
        Payload = &Access.LoopAnalyses->DT;
      break;
    case NEVERC_IR_ANALYSIS_POST_DOMINATOR_TREE:
      if (Access.FunctionAnalyses)
        Payload = &Access.FunctionAnalyses
                       ->getResult<PostDominatorTreeAnalysis>(*F);
      else {
        if (!OwnedPostDominatorTree)
          OwnedPostDominatorTree = std::make_unique<PostDominatorTree>(*F);
        Payload = OwnedPostDominatorTree.get();
      }
      break;
    case NEVERC_IR_ANALYSIS_LOOP_INFO:
      if (Access.FunctionAnalyses)
        Payload = &Access.FunctionAnalyses->getResult<LoopAnalysis>(*F);
      else if (Access.LoopAnalyses && Access.FunctionScope == F)
        Payload = &Access.LoopAnalyses->LI;
      break;
    case NEVERC_IR_ANALYSIS_SCALAR_EVOLUTION:
      if (Access.FunctionAnalyses) {
        Payload =
            &Access.FunctionAnalyses->getResult<ScalarEvolutionAnalysis>(*F);
        SecondaryPayload =
            &Access.FunctionAnalyses->getResult<LoopAnalysis>(*F);
      } else if (Access.LoopAnalyses && Access.FunctionScope == F) {
        Payload = &Access.LoopAnalyses->SE;
        SecondaryPayload = &Access.LoopAnalyses->LI;
      }
      break;
    case NEVERC_IR_ANALYSIS_MEMORY_SSA:
      if (Access.FunctionAnalyses)
        Payload = &Access.FunctionAnalyses
                       ->getResult<MemorySSAAnalysis>(*F)
                       .getMSSA();
      else if (Access.LoopAnalyses && Access.FunctionScope == F)
        Payload = Access.LoopAnalyses->MSSA;
      break;
    case NEVERC_IR_ANALYSIS_CALL_GRAPH:
      if (Access.CallGraph)
        Payload = Access.CallGraph;
      else if (Access.ModuleAnalyses)
        Payload =
            &Access.ModuleAnalyses->getResult<LazyCallGraphAnalysis>(
                IR.module());
      break;
    case NEVERC_IR_ANALYSIS_ALIAS:
      if (Access.FunctionAnalyses)
        Payload = &Access.FunctionAnalyses->getResult<AAManager>(*F);
      else if (Access.LoopAnalyses && Access.FunctionScope == F)
        Payload = &Access.LoopAnalyses->AA;
      break;
    default:
      return analysisStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    }
    if (!Payload)
      return analysisStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);

    auto State = std::make_unique<ResultState>();
    State->Owner = this;
    State->Kind = Analysis;
    State->Scope = F;
    State->Payload = Payload;
    State->SecondaryPayload = SecondaryPayload;
    auto Handle = Task.handles().create(
        PluginIRAnalysisResultHandleKind, State.get());
    if (!Handle) {
      consumeError(Handle.takeError());
      return analysisStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    State->Handle = *Handle;
    *OutResult = *Handle;
    Results.push_back(std::move(State));
    return neverc_status_ok();
  }

  NevercStatus queryCustomAnalysis(
      NevercInterfaceID Analysis,
      NevercIRAnalysisResultHandle *OutResult) {
    if (!OutResult)
      return analysisStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *OutResult = {};
    std::lock_guard<std::recursive_mutex> Lock(Custom.Mutex);
    const AnalysisBinding *Binding = Custom.findBinding(Analysis);
    if (!Binding)
      return analysisStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);

    CustomAnalysisScope RequestedScope = Scope;
    RequestedScope.Level = Binding->Descriptor.Level;
    switch (Binding->Descriptor.Level) {
    case NEVERC_IR_PASS_LEVEL_MODULE:
      RequestedScope.F = nullptr;
      RequestedScope.LoopHeader = nullptr;
      RequestedScope.SCCFunctions.clear();
      break;
    case NEVERC_IR_PASS_LEVEL_CGSCC:
      if (Scope.Level != NEVERC_IR_PASS_LEVEL_CGSCC ||
          Scope.SCCFunctions.empty())
        return analysisStatus(NEVERC_STATUS_WRONG_SCOPE);
      RequestedScope.F = nullptr;
      RequestedScope.LoopHeader = nullptr;
      break;
    case NEVERC_IR_PASS_LEVEL_FUNCTION:
      if (!Scope.F)
        return analysisStatus(NEVERC_STATUS_WRONG_SCOPE);
      RequestedScope.LoopHeader = nullptr;
      RequestedScope.SCCFunctions.clear();
      break;
    case NEVERC_IR_PASS_LEVEL_LOOP:
      if (!Scope.F || !Scope.LoopHeader)
        return analysisStatus(NEVERC_STATUS_WRONG_SCOPE);
      RequestedScope.SCCFunctions.clear();
      break;
    default:
      return analysisStatus(NEVERC_STATUS_INVALID_DESCRIPTOR);
    }

    std::shared_ptr<CustomAnalysisCacheEntry> Entry =
        Custom.findEntry(Analysis, RequestedScope);
    if (!Entry) {
      if (llvm::any_of(Computing, [&](NevercInterfaceID Current) {
            return sameInterface(Current, Analysis);
          }))
        return analysisStatus(NEVERC_STATUS_DEPENDENCY_CYCLE);

      Computing.push_back(Analysis);
      auto RemoveComputing = make_scope_exit([&] { Computing.pop_back(); });
      for (NevercInterfaceID Dependency : Binding->Dependencies) {
        NevercIRAnalysisResultHandle DependencyResult{};
        NevercStatus Status =
            queryCustomAnalysis(Dependency, &DependencyResult);
        if (Status.Code != NEVERC_STATUS_OK)
          return Status;
      }

      NevercIRPassInvocation Invocation{};
      Invocation.Header = {sizeof(Invocation), NEVERC_IR_PASS_API_MAJOR,
                           NEVERC_IR_PASS_API_MINOR, 0};
      Invocation.Task = Task.handle();
      Invocation.Level = RequestedScope.Level;
      Invocation.Module = IR.moduleHandle();
      Invocation.Core = &IR.coreAPI();
      Invocation.Builder = nullptr;
      Invocation.Analyses = &API;

      auto WrapScopeValue = [&](Value *V,
                                NevercIRValueHandle &Out) -> NevercStatus {
        if (!V)
          return neverc_status_ok();
        auto Wrapped = IR.wrapValue(*V);
        if (!Wrapped) {
          consumeError(Wrapped.takeError());
          return analysisStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
        }
        Out = *Wrapped;
        return neverc_status_ok();
      };
      NevercStatus Status =
          WrapScopeValue(RequestedScope.F, Invocation.Function);
      if (Status.Code == NEVERC_STATUS_OK)
        Status =
            WrapScopeValue(RequestedScope.LoopHeader, Invocation.LoopHeader);
      if (Status.Code != NEVERC_STATUS_OK)
        return Status;

      std::vector<NevercIRValueHandle> SCCHandles;
      SCCHandles.reserve(RequestedScope.SCCFunctions.size());
      for (Function *F : RequestedScope.SCCFunctions) {
        NevercIRValueHandle Handle{};
        Status = WrapScopeValue(F, Handle);
        if (Status.Code != NEVERC_STATUS_OK)
          return Status;
        SCCHandles.push_back(Handle);
      }
      Invocation.SCCFunctions = SCCHandles.data();
      Invocation.SCCFunctionCount = SCCHandles.size();

      void *ComputedResult = nullptr;
      NevercStatus ComputeStatus{};
      {
        IR.enterReadOnly();
        auto ReadOnlyGuard = make_scope_exit([&] { IR.leaveReadOnly(); });
        if (Task.session().currentCallbackPluginID() == Binding->PluginID) {
          ComputeStatus = Binding->Descriptor.Compute(
              &Invocation, &ComputedResult, Binding->Descriptor.UserData);
        } else {
          auto CallbackStatus = Task.invokeCallback(
              Binding->PluginID, "ir-analysis/" + Binding->Name, [&] {
                return Binding->Descriptor.Compute(
                    &Invocation, &ComputedResult,
                    Binding->Descriptor.UserData);
              });
          if (!CallbackStatus) {
            consumeError(CallbackStatus.takeError());
            return analysisStatus(NEVERC_STATUS_PLUGIN_EXCEPTION);
          }
          ComputeStatus = *CallbackStatus;
        }
      }
      if (ComputeStatus.Code != NEVERC_STATUS_OK)
        return ComputeStatus;
      if (!ComputedResult)
        return analysisStatus(NEVERC_STATUS_INVALID_DESCRIPTOR);

      auto NewEntry = std::make_shared<CustomAnalysisCacheEntry>();
      NewEntry->Analysis = Analysis;
      NewEntry->Scope = std::move(RequestedScope);
      NewEntry->Binding = Binding;
      NewEntry->Result = ComputedResult;
      Entry = NewEntry;
      Custom.Cache.push_back(std::move(NewEntry));
    }
    for (const auto &Existing : Results) {
      if (Existing->CustomEntry == Entry) {
        *OutResult = Existing->Handle;
        return neverc_status_ok();
      }
    }
    auto State = std::make_unique<ResultState>();
    State->Owner = this;
    State->CustomAnalysis = Analysis;
    State->CustomEntry = Entry;
    auto Handle = Task.handles().create(
        PluginIRAnalysisResultHandleKind, State.get());
    if (!Handle) {
      consumeError(Handle.takeError());
      return analysisStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    State->Handle = *Handle;
    *OutResult = *Handle;
    Results.push_back(std::move(State));
    return neverc_status_ok();
  }

public:
  static NevercStatus NEVERC_CALL
  queryBuiltin(void *Context, NevercTaskHandle Task,
               NevercIRBuiltinAnalysis Analysis, NevercIRValueHandle Scope,
               NevercIRAnalysisResultHandle *OutResult) {
    NevercStatus Status{};
    AnalysisInvocationBridge *Bridge = get(Context, Task, Status);
    if (!Bridge)
      return Status;
    return Bridge->query(Analysis, Scope, OutResult);
  }

  static NevercStatus NEVERC_CALL
  queryCustom(void *Context, NevercTaskHandle Task,
              NevercInterfaceID Analysis,
              NevercIRAnalysisResultHandle *OutResult) {
    NevercStatus Status{};
    AnalysisInvocationBridge *Bridge = get(Context, Task, Status);
    if (!Bridge)
      return Status;
    return Bridge->queryCustomAnalysis(Analysis, OutResult);
  }

  static NevercStatus NEVERC_CALL getCustomResultData(
      void *Context, NevercTaskHandle Task,
      NevercIRAnalysisResultHandle Result, NevercByteView *OutData) {
    if (!OutData)
      return analysisStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *OutData = {};
    NevercStatus Status{};
    AnalysisInvocationBridge *Bridge = get(Context, Task, Status);
    if (!Bridge)
      return Status;
    std::lock_guard<std::recursive_mutex> Lock(Bridge->Custom.Mutex);
    ResultState *State = nullptr;
    Status = Bridge->resolveCustomResult(Result, &State);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    const AnalysisBinding &Binding = *State->CustomEntry->Binding;
    NevercStatus QueryStatus{};
    if (Bridge->Task.session().currentCallbackPluginID() ==
        Binding.PluginID) {
      QueryStatus = Binding.Descriptor.Query(State->CustomEntry->Result,
                                             OutData,
                                             Binding.Descriptor.UserData);
    } else {
      auto CallbackStatus = Bridge->Task.invokeCallback(
          Binding.PluginID, "ir-analysis-query/" + Binding.Name, [&] {
            return Binding.Descriptor.Query(State->CustomEntry->Result,
                                            OutData,
                                            Binding.Descriptor.UserData);
          });
      if (!CallbackStatus) {
        consumeError(CallbackStatus.takeError());
        return analysisStatus(NEVERC_STATUS_PLUGIN_EXCEPTION);
      }
      QueryStatus = *CallbackStatus;
    }
    if (QueryStatus.Code != NEVERC_STATUS_OK)
      return QueryStatus;
    if ((OutData->Length != 0 && !OutData->Data) ||
        OutData->Length >
            static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
      *OutData = {};
      return analysisStatus(NEVERC_STATUS_INVALID_DESCRIPTOR);
    }
    return neverc_status_ok();
  }

  static NevercStatus NEVERC_CALL dominatorTreeDominates(
      void *Context, NevercTaskHandle Task,
      NevercIRAnalysisResultHandle Result, NevercIRValueHandle Dominator,
      NevercIRValueHandle Dominated, NevercBool *OutDominates) {
    if (!OutDominates)
      return analysisStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *OutDominates = NEVERC_FALSE;
    NevercStatus Status{};
    AnalysisInvocationBridge *Bridge = get(Context, Task, Status);
    if (!Bridge)
      return Status;
    ResultState *State = nullptr;
    Status = Bridge->resolveResult(
        Result, NEVERC_IR_ANALYSIS_DOMINATOR_TREE, &State);
    if (Status.Code == NEVERC_STATUS_WRONG_TYPE)
      Status = Bridge->resolveResult(
          Result, NEVERC_IR_ANALYSIS_POST_DOMINATOR_TREE, &State);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    Value *Left = nullptr;
    Value *Right = nullptr;
    Status = Bridge->IR.resolveValue(Dominator, &Left);
    if (Status.Code == NEVERC_STATUS_OK)
      Status = Bridge->IR.resolveValue(Dominated, &Right);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    auto *LeftBlock = dyn_cast<BasicBlock>(Left);
    auto *RightBlock = dyn_cast<BasicBlock>(Right);
    if (!LeftBlock || !RightBlock)
      return analysisStatus(NEVERC_STATUS_WRONG_TYPE);
    bool Dominates =
        State->Kind == NEVERC_IR_ANALYSIS_POST_DOMINATOR_TREE
            ? static_cast<PostDominatorTree *>(State->Payload)
                  ->dominates(LeftBlock, RightBlock)
            : static_cast<DominatorTree *>(State->Payload)
                  ->dominates(LeftBlock, RightBlock);
    *OutDominates = Dominates ? NEVERC_TRUE : NEVERC_FALSE;
    return neverc_status_ok();
  }

  static void collectLoops(Loop &Current, std::vector<Loop *> &Loops) {
    Loops.push_back(&Current);
    for (Loop *Child : Current.getSubLoops())
      collectLoops(*Child, Loops);
  }

  static std::vector<Loop *> loops(LoopInfo &Info) {
    std::vector<Loop *> Result;
    for (Loop *TopLevel : Info)
      collectLoops(*TopLevel, Result);
    return Result;
  }

  static NevercStatus NEVERC_CALL
  getLoopCount(void *Context, NevercTaskHandle Task,
               NevercIRAnalysisResultHandle Result, uint64_t *OutCount) {
    if (!OutCount)
      return analysisStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *OutCount = 0;
    NevercStatus Status{};
    AnalysisInvocationBridge *Bridge = get(Context, Task, Status);
    if (!Bridge)
      return Status;
    ResultState *State = nullptr;
    Status =
        Bridge->resolveResult(Result, NEVERC_IR_ANALYSIS_LOOP_INFO, &State);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    *OutCount = loops(*static_cast<LoopInfo *>(State->Payload)).size();
    return neverc_status_ok();
  }

  static NevercStatus NEVERC_CALL
  getLoopHeader(void *Context, NevercTaskHandle Task,
                NevercIRAnalysisResultHandle Result, uint64_t Index,
                NevercIRValueHandle *OutHeader) {
    if (!OutHeader)
      return analysisStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *OutHeader = {};
    NevercStatus Status{};
    AnalysisInvocationBridge *Bridge = get(Context, Task, Status);
    if (!Bridge)
      return Status;
    ResultState *State = nullptr;
    Status =
        Bridge->resolveResult(Result, NEVERC_IR_ANALYSIS_LOOP_INFO, &State);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    std::vector<Loop *> All =
        loops(*static_cast<LoopInfo *>(State->Payload));
    if (Index >= All.size())
      return analysisStatus(NEVERC_STATUS_NOT_FOUND);
    auto Wrapped = Bridge->IR.wrapValue(*All[Index]->getHeader());
    if (!Wrapped) {
      consumeError(Wrapped.takeError());
      return analysisStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    *OutHeader = *Wrapped;
    return neverc_status_ok();
  }

  static NevercStatus NEVERC_CALL
  getLoopForBlock(void *Context, NevercTaskHandle Task,
                  NevercIRAnalysisResultHandle Result,
                  NevercIRValueHandle Block, NevercIRValueHandle *OutHeader,
                  uint32_t *OutDepth) {
    if (!OutHeader || !OutDepth)
      return analysisStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *OutHeader = {};
    *OutDepth = 0;
    NevercStatus Status{};
    AnalysisInvocationBridge *Bridge = get(Context, Task, Status);
    if (!Bridge)
      return Status;
    ResultState *State = nullptr;
    Status =
        Bridge->resolveResult(Result, NEVERC_IR_ANALYSIS_LOOP_INFO, &State);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    Value *Resolved = nullptr;
    Status = Bridge->IR.resolveValue(Block, &Resolved);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    auto *BB = dyn_cast<BasicBlock>(Resolved);
    if (!BB)
      return analysisStatus(NEVERC_STATUS_WRONG_TYPE);
    Loop *Containing =
        static_cast<LoopInfo *>(State->Payload)->getLoopFor(BB);
    if (!Containing)
      return neverc_status_ok();
    auto Wrapped = Bridge->IR.wrapValue(*Containing->getHeader());
    if (!Wrapped) {
      consumeError(Wrapped.takeError());
      return analysisStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    *OutHeader = *Wrapped;
    *OutDepth = Containing->getLoopDepth();
    return neverc_status_ok();
  }

  static NevercStatus NEVERC_CALL getScalarEvolutionConstantTripCount(
      void *Context, NevercTaskHandle Task,
      NevercIRAnalysisResultHandle Result, NevercIRValueHandle LoopHeader,
      NevercBool *OutKnown, uint64_t *OutTripCount) {
    if (!OutKnown || !OutTripCount)
      return analysisStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *OutKnown = NEVERC_FALSE;
    *OutTripCount = 0;
    NevercStatus Status{};
    AnalysisInvocationBridge *Bridge = get(Context, Task, Status);
    if (!Bridge)
      return Status;
    ResultState *State = nullptr;
    Status = Bridge->resolveResult(
        Result, NEVERC_IR_ANALYSIS_SCALAR_EVOLUTION, &State);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    Value *Resolved = nullptr;
    Status = Bridge->IR.resolveValue(LoopHeader, &Resolved);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    auto *Header = dyn_cast<BasicBlock>(Resolved);
    auto *Info = static_cast<LoopInfo *>(State->SecondaryPayload);
    if (!Header || !Info)
      return analysisStatus(NEVERC_STATUS_WRONG_TYPE);
    Loop *L = Info->getLoopFor(Header);
    if (!L || L->getHeader() != Header)
      return analysisStatus(NEVERC_STATUS_NOT_FOUND);
    auto *SE = static_cast<ScalarEvolution *>(State->Payload);
    const SCEV *BackedgeCount = SE->getBackedgeTakenCount(L);
    auto *Constant = dyn_cast<SCEVConstant>(BackedgeCount);
    if (!Constant)
      return neverc_status_ok();
    uint64_t Count = Constant->getAPInt().getLimitedValue();
    if (Count == UINT64_MAX)
      return neverc_status_ok();
    *OutKnown = NEVERC_TRUE;
    *OutTripCount = Count + 1;
    return neverc_status_ok();
  }

  static NevercStatus NEVERC_CALL getMemoryAccessKind(
      void *Context, NevercTaskHandle Task,
      NevercIRAnalysisResultHandle Result, NevercIRValueHandle Instruction,
      NevercIRMemoryAccessKind *OutKind) {
    if (!OutKind)
      return analysisStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *OutKind = NEVERC_IR_MEMORY_ACCESS_NONE;
    NevercStatus Status{};
    AnalysisInvocationBridge *Bridge = get(Context, Task, Status);
    if (!Bridge)
      return Status;
    ResultState *State = nullptr;
    Status =
        Bridge->resolveResult(Result, NEVERC_IR_ANALYSIS_MEMORY_SSA, &State);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    Value *Resolved = nullptr;
    Status = Bridge->IR.resolveValue(Instruction, &Resolved);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    auto *I = dyn_cast<llvm::Instruction>(Resolved);
    if (!I)
      return analysisStatus(NEVERC_STATUS_WRONG_TYPE);
    auto *MSSA = static_cast<MemorySSA *>(State->Payload);
    MemoryAccess *Access = MSSA->getMemoryAccess(I);
    if (!Access)
      return neverc_status_ok();
    if (MSSA->isLiveOnEntryDef(Access))
      *OutKind = NEVERC_IR_MEMORY_ACCESS_LIVE_ON_ENTRY;
    else if (isa<MemoryUse>(Access))
      *OutKind = NEVERC_IR_MEMORY_ACCESS_USE;
    else if (isa<MemoryDef>(Access))
      *OutKind = NEVERC_IR_MEMORY_ACCESS_DEF;
    else if (isa<MemoryPhi>(Access))
      *OutKind = NEVERC_IR_MEMORY_ACCESS_PHI;
    return neverc_status_ok();
  }

  static NevercStatus resolveCallGraphFunction(
      AnalysisInvocationBridge &Bridge, ResultState &State,
      NevercIRValueHandle FunctionHandle, LazyCallGraph::Node **OutNode) {
    Function *F = nullptr;
    NevercStatus Status = Bridge.resolveFunction(FunctionHandle, &F);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    auto *Graph = static_cast<LazyCallGraph *>(State.Payload);
    *OutNode = Graph->lookup(*F);
    if (!*OutNode)
      *OutNode = &Graph->get(*F);
    return neverc_status_ok();
  }

  static NevercStatus NEVERC_CALL getDirectCalleeCount(
      void *Context, NevercTaskHandle Task,
      NevercIRAnalysisResultHandle Result, NevercIRValueHandle FunctionHandle,
      uint64_t *OutCount) {
    if (!OutCount)
      return analysisStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *OutCount = 0;
    NevercStatus Status{};
    AnalysisInvocationBridge *Bridge = get(Context, Task, Status);
    if (!Bridge)
      return Status;
    ResultState *State = nullptr;
    Status =
        Bridge->resolveResult(Result, NEVERC_IR_ANALYSIS_CALL_GRAPH, &State);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    LazyCallGraph::Node *Node = nullptr;
    Status =
        resolveCallGraphFunction(*Bridge, *State, FunctionHandle, &Node);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    for (LazyCallGraph::Edge &Edge : Node->populate().calls())
      (void)Edge, ++*OutCount;
    return neverc_status_ok();
  }

  static NevercStatus NEVERC_CALL getDirectCallee(
      void *Context, NevercTaskHandle Task,
      NevercIRAnalysisResultHandle Result, NevercIRValueHandle FunctionHandle,
      uint64_t Index, NevercIRValueHandle *OutCallee) {
    if (!OutCallee)
      return analysisStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *OutCallee = {};
    NevercStatus Status{};
    AnalysisInvocationBridge *Bridge = get(Context, Task, Status);
    if (!Bridge)
      return Status;
    ResultState *State = nullptr;
    Status =
        Bridge->resolveResult(Result, NEVERC_IR_ANALYSIS_CALL_GRAPH, &State);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    LazyCallGraph::Node *Node = nullptr;
    Status =
        resolveCallGraphFunction(*Bridge, *State, FunctionHandle, &Node);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    uint64_t Current = 0;
    for (LazyCallGraph::Edge &Edge : Node->populate().calls()) {
      if (Current++ != Index)
        continue;
      auto Wrapped = Bridge->IR.wrapValue(Edge.getFunction());
      if (!Wrapped) {
        consumeError(Wrapped.takeError());
        return analysisStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
      }
      *OutCallee = *Wrapped;
      return neverc_status_ok();
    }
    return analysisStatus(NEVERC_STATUS_NOT_FOUND);
  }

  static NevercStatus NEVERC_CALL
  alias(void *Context, NevercTaskHandle Task,
        NevercIRAnalysisResultHandle Result, NevercIRValueHandle Left,
        uint64_t LeftBytes, NevercIRValueHandle Right, uint64_t RightBytes,
        NevercIRAliasResult *OutAlias) {
    if (!OutAlias)
      return analysisStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *OutAlias = NEVERC_IR_ALIAS_MAY;
    NevercStatus Status{};
    AnalysisInvocationBridge *Bridge = get(Context, Task, Status);
    if (!Bridge)
      return Status;
    ResultState *State = nullptr;
    Status =
        Bridge->resolveResult(Result, NEVERC_IR_ANALYSIS_ALIAS, &State);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    Value *LeftValue = nullptr;
    Value *RightValue = nullptr;
    Status = Bridge->IR.resolveValue(Left, &LeftValue);
    if (Status.Code == NEVERC_STATUS_OK)
      Status = Bridge->IR.resolveValue(Right, &RightValue);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    if (!LeftValue->getType()->isPointerTy() ||
        !RightValue->getType()->isPointerTy())
      return analysisStatus(NEVERC_STATUS_WRONG_TYPE);
    AliasResult ResultValue =
        static_cast<AAResults *>(State->Payload)
            ->alias(MemoryLocation(LeftValue, LocationSize::precise(LeftBytes)),
                    MemoryLocation(RightValue,
                                   LocationSize::precise(RightBytes)));
    switch (static_cast<AliasResult::Kind>(ResultValue)) {
    case AliasResult::NoAlias:
      *OutAlias = NEVERC_IR_ALIAS_NO;
      break;
    case AliasResult::MayAlias:
      *OutAlias = NEVERC_IR_ALIAS_MAY;
      break;
    case AliasResult::PartialAlias:
      *OutAlias = NEVERC_IR_ALIAS_PARTIAL;
      break;
    case AliasResult::MustAlias:
      *OutAlias = NEVERC_IR_ALIAS_MUST;
      break;
    }
    return neverc_status_ok();
  }

private:
  PluginTaskContext &Task;
  IRPluginBridge &IR;
  AnalysisAccess Access;
  CustomAnalysisState &Custom;
  CustomAnalysisScope Scope;
  uint64_t InitialMutationGeneration;
  NevercIRAnalysisAPI API{};
  std::vector<std::unique_ptr<ResultState>> Results;
  std::vector<NevercInterfaceID> Computing;
  std::unique_ptr<PostDominatorTree> OwnedPostDominatorTree;
};

const NevercIRAnalysisAPI AnalysisAPI = {
    {sizeof(NevercIRAnalysisAPI), NEVERC_IR_ANALYSIS_API_MAJOR,
     NEVERC_IR_ANALYSIS_API_MINOR, 0},
    nullptr,
    AnalysisInvocationBridge::queryBuiltin,
    AnalysisInvocationBridge::dominatorTreeDominates,
    AnalysisInvocationBridge::getLoopCount,
    AnalysisInvocationBridge::getLoopHeader,
    AnalysisInvocationBridge::getLoopForBlock,
    AnalysisInvocationBridge::getScalarEvolutionConstantTripCount,
    AnalysisInvocationBridge::getMemoryAccessKind,
    AnalysisInvocationBridge::getDirectCalleeCount,
    AnalysisInvocationBridge::getDirectCallee,
    AnalysisInvocationBridge::alias,
    registerAnalysis,
    AnalysisInvocationBridge::queryCustom,
    AnalysisInvocationBridge::getCustomResultData,
};

struct PassBinding {
  std::string PluginID;
  std::string PassID;
  NevercIRPassDescriptor Descriptor{};
  std::vector<NevercInterfaceID> RequiredAnalyses;
  std::vector<uint8_t> ExternalDependencyDigest;
};

PreservedAnalyses preservedAnalyses(NevercIRPreservedAnalysisFlags Flags) {
  if ((Flags & NEVERC_IR_PRESERVE_ALL) != 0)
    return PreservedAnalyses::all();
  PreservedAnalyses Result = PreservedAnalyses::none();
  if ((Flags & NEVERC_IR_PRESERVE_CFG) != 0)
    Result.preserveSet<CFGAnalyses>();
  return Result;
}

} // namespace

struct IRPassPlan::Impl {
  explicit Impl(PluginTaskContext &TaskValue)
      : Task(TaskValue), CustomAnalyses(TaskValue) {}

  PreservedAnalyses run(const PassBinding &Binding, Module &M,
                        Function *CurrentFunction,
                        BasicBlock *CurrentLoopHeader,
                        ArrayRef<Function *> SCCFunctions,
                        AnalysisAccess AnalysisAccessValue,
                        NevercIROptimizationLevel OptimizationLevel) const {
    auto BridgeOrError = IRPluginBridge::borrow(Task, M);
    if (!BridgeOrError) {
      M.getContext().emitError(
          ("NeverC plugin pass '" + Binding.PassID +
           "' could not create an IR bridge: " +
           toString(BridgeOrError.takeError())));
      return PreservedAnalyses::none();
    }
    std::unique_ptr<IRPluginBridge> Bridge = std::move(*BridgeOrError);
    uint64_t InitialGeneration = Bridge->mutationGeneration();
    CustomAnalysisScope AnalysisScope;
    AnalysisScope.Level = Binding.Descriptor.Level;
    AnalysisScope.M = &M;
    AnalysisScope.F = CurrentFunction;
    AnalysisScope.LoopHeader = CurrentLoopHeader;
    AnalysisScope.SCCFunctions.assign(SCCFunctions.begin(), SCCFunctions.end());
    AnalysisInvocationBridge Analyses(Task, *Bridge, AnalysisAccessValue,
                                      CustomAnalyses,
                                      std::move(AnalysisScope));
    bool PreservationApplied = false;
    auto InvalidateOnFailure = make_scope_exit([&] {
      if (!PreservationApplied)
        (void)CustomAnalyses.invalidate({});
    });
    for (NevercInterfaceID Required : Binding.RequiredAnalyses) {
      NevercStatus Status = Analyses.requireCustomAnalysis(Required);
      if (Status.Code != NEVERC_STATUS_OK) {
        M.getContext().emitError(
            ("NeverC plugin pass '" + Binding.PassID +
             "' could not acquire required analysis (status " +
             std::to_string(Status.Code) + ")"));
        return PreservedAnalyses::none();
      }
    }

    NevercIRValueHandle FunctionHandle{};
    if (CurrentFunction) {
      auto Wrapped = Bridge->wrapValue(*CurrentFunction);
      if (!Wrapped) {
        M.getContext().emitError(
            ("NeverC plugin pass '" + Binding.PassID +
             "' could not wrap its function: " +
             toString(Wrapped.takeError())));
        return PreservedAnalyses::none();
      }
      FunctionHandle = *Wrapped;
    }

    NevercIRValueHandle LoopHeaderHandle{};
    if (CurrentLoopHeader) {
      auto Wrapped = Bridge->wrapValue(*CurrentLoopHeader);
      if (!Wrapped) {
        M.getContext().emitError(
            ("NeverC plugin pass '" + Binding.PassID +
             "' could not wrap its loop header: " +
             toString(Wrapped.takeError())));
        return PreservedAnalyses::none();
      }
      LoopHeaderHandle = *Wrapped;
    }

    std::vector<NevercIRValueHandle> SCCFunctionHandles;
    SCCFunctionHandles.reserve(SCCFunctions.size());
    for (Function *SCCFunction : SCCFunctions) {
      auto Wrapped = Bridge->wrapValue(*SCCFunction);
      if (!Wrapped) {
        M.getContext().emitError(
            ("NeverC plugin pass '" + Binding.PassID +
             "' could not wrap an SCC function: " +
             toString(Wrapped.takeError())));
        return PreservedAnalyses::none();
      }
      SCCFunctionHandles.push_back(*Wrapped);
    }

    NevercIRPassInvocation Invocation{};
    Invocation.Header = {sizeof(Invocation), NEVERC_IR_PASS_API_MAJOR,
                         NEVERC_IR_PASS_API_MINOR, 0};
    Invocation.Task = Task.handle();
    Invocation.Phase = Binding.Descriptor.Phase;
    Invocation.PassID = {Binding.PassID.data(), Binding.PassID.size()};
    Invocation.Level = Binding.Descriptor.Level;
    Invocation.OptimizationLevel = OptimizationLevel;
    Invocation.Module = Bridge->moduleHandle();
    Invocation.Function = FunctionHandle;
    Invocation.LoopHeader = LoopHeaderHandle;
    Invocation.SCCFunctions = SCCFunctionHandles.data();
    Invocation.SCCFunctionCount = SCCFunctionHandles.size();
    Invocation.Core = &Bridge->coreAPI();
    Invocation.Builder = &Bridge->builderAPI();
    Invocation.Analyses = &Analyses.api();

    NevercIRPreservedAnalyses Preserved{};
    Preserved.Header = {sizeof(Preserved), NEVERC_IR_PASS_API_MAJOR,
                        NEVERC_IR_PASS_API_MINOR, 0};
    auto CallbackStatus = Task.invokeCallback(
        Binding.PluginID, "ir-pass/" + Binding.PassID,
        [&] {
          return Binding.Descriptor.Run(&Invocation, &Preserved,
                                        Binding.Descriptor.UserData);
        });
    if (!CallbackStatus) {
      M.getContext().emitError(
          ("NeverC plugin pass '" + Binding.PassID + "' failed: " +
           toString(CallbackStatus.takeError())));
      return PreservedAnalyses::none();
    }
    if (CallbackStatus->Code != NEVERC_STATUS_OK) {
      M.getContext().emitError(
          ("NeverC plugin pass '" + Binding.PassID +
           "' returned status " + std::to_string(CallbackStatus->Code)));
      return PreservedAnalyses::none();
    }
    if (!validPreservedAnalyses(Preserved)) {
      M.getContext().emitError(
          ("NeverC plugin pass '" + Binding.PassID +
           "' returned an invalid preserved-analysis descriptor"));
      return PreservedAnalyses::none();
    }
    bool Changed = Bridge->mutationGeneration() != InitialGeneration;
    if (Changed &&
        (Preserved.Flags & NEVERC_IR_PRESERVE_ALL) != 0) {
      M.getContext().emitError(
          ("NeverC plugin pass '" + Binding.PassID +
           "' mutated IR while preserving all analyses"));
      return PreservedAnalyses::none();
    }
    if (Changed && verifyModule(M, &errs())) {
      M.getContext().emitError(
          ("NeverC plugin pass '" + Binding.PassID +
           "' produced an invalid LLVM module"));
      return PreservedAnalyses::none();
    }
    NevercStatus InvalidateStatus = CustomAnalyses.invalidate(Preserved);
    PreservationApplied = true;
    if (InvalidateStatus.Code != NEVERC_STATUS_OK) {
      M.getContext().emitError(
          ("NeverC plugin pass '" + Binding.PassID +
           "' failed to invalidate custom analyses (status " +
           std::to_string(InvalidateStatus.Code) + ")"));
      return PreservedAnalyses::none();
    }
    return preservedAnalyses(Preserved.Flags);
  }

  PluginTaskContext &Task;
  std::vector<PassBinding> Bindings;
  mutable CustomAnalysisState CustomAnalyses;
};

namespace {

class PluginModulePass : public PassInfoMixin<PluginModulePass> {
public:
  PluginModulePass(const IRPassPlan::Impl &PlanValue,
                   const PassBinding &BindingValue,
                   NevercIROptimizationLevel OptimizationLevelValue)
      : Plan(PlanValue), Binding(BindingValue),
        OptimizationLevel(OptimizationLevelValue) {}

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &Analyses) {
    return Plan.run(Binding, M, nullptr, nullptr, {},
                    {&Analyses, nullptr, nullptr, nullptr, nullptr},
                    OptimizationLevel);
  }

  static bool isRequired() { return true; }

private:
  const IRPassPlan::Impl &Plan;
  const PassBinding &Binding;
  NevercIROptimizationLevel OptimizationLevel;
};

class PluginCGSCCPass : public PassInfoMixin<PluginCGSCCPass> {
public:
  PluginCGSCCPass(const IRPassPlan::Impl &PlanValue,
                  const PassBinding &BindingValue,
                  NevercIROptimizationLevel OptimizationLevelValue)
      : Plan(PlanValue), Binding(BindingValue),
        OptimizationLevel(OptimizationLevelValue) {}

  PreservedAnalyses run(LazyCallGraph::SCC &C, CGSCCAnalysisManager &,
                        LazyCallGraph &Graph, CGSCCUpdateResult &) {
    std::vector<Function *> Functions;
    Functions.reserve(C.size());
    for (LazyCallGraph::Node &Node : C)
      Functions.push_back(&Node.getFunction());
    if (Functions.empty())
      return PreservedAnalyses::all();
    return Plan.run(
        Binding, *Functions.front()->getParent(), nullptr, nullptr, Functions,
        {nullptr, nullptr, &Graph, nullptr, nullptr}, OptimizationLevel);
  }

  static bool isRequired() { return true; }

private:
  const IRPassPlan::Impl &Plan;
  const PassBinding &Binding;
  NevercIROptimizationLevel OptimizationLevel;
};

class PluginFunctionPass : public PassInfoMixin<PluginFunctionPass> {
public:
  PluginFunctionPass(const IRPassPlan::Impl &PlanValue,
                     const PassBinding &BindingValue,
                     NevercIROptimizationLevel OptimizationLevelValue)
      : Plan(PlanValue), Binding(BindingValue),
        OptimizationLevel(OptimizationLevelValue) {}

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &Analyses) {
    return Plan.run(
        Binding, *F.getParent(), &F, nullptr, {},
        {nullptr, &Analyses, nullptr, nullptr, &F}, OptimizationLevel);
  }

  static bool isRequired() { return true; }

private:
  const IRPassPlan::Impl &Plan;
  const PassBinding &Binding;
  NevercIROptimizationLevel OptimizationLevel;
};

class PluginLoopPass : public PassInfoMixin<PluginLoopPass> {
public:
  PluginLoopPass(const IRPassPlan::Impl &PlanValue,
                 const PassBinding &BindingValue,
                 NevercIROptimizationLevel OptimizationLevelValue)
      : Plan(PlanValue), Binding(BindingValue),
        OptimizationLevel(OptimizationLevelValue) {}

  PreservedAnalyses run(Loop &L, LoopAnalysisManager &,
                        LoopStandardAnalysisResults &Analyses, LPMUpdater &) {
    BasicBlock *Header = L.getHeader();
    Function *F = Header ? Header->getParent() : nullptr;
    if (!F)
      return PreservedAnalyses::all();
    return Plan.run(
        Binding, *F->getParent(), F, Header, {},
        {nullptr, nullptr, nullptr, &Analyses, F}, OptimizationLevel);
  }

  static bool isRequired() { return true; }

private:
  const IRPassPlan::Impl &Plan;
  const PassBinding &Binding;
  NevercIROptimizationLevel OptimizationLevel;
};

} // namespace

Expected<std::unique_ptr<IRPassPlan>>
IRPassPlan::create(PluginTaskContext &Task) {
  auto State = std::make_unique<Impl>(Task);
  for (const auto &Module : Task.session().plugins()) {
    const PluginPublishedRegistration *Registration = Module->registration();
    if (!Registration)
      continue;
    for (const PluginRegistrationRecord &Record : Registration->records()) {
      if (Record.Kind != PluginRegistrationKind::IRAnalysis)
        continue;
      if (State->CustomAnalyses.findBinding(Record.Interface))
        return createStringError(
            inconvertibleErrorCode(),
            "duplicate custom IR analysis ID in frozen session plan");
      AnalysisBinding Binding;
      Binding.PluginID = Module->descriptor().PluginID;
      Binding.Name = Record.AnalysisName;
      Binding.Descriptor = Record.IRAnalysis;
      Binding.Descriptor.Name = {};
      Binding.Descriptor.Dependencies = nullptr;
      Binding.Descriptor.DependencyCount = 0;
      Binding.Dependencies = Record.RequiredAnalyses;
      State->CustomAnalyses.Bindings.push_back(std::move(Binding));
    }
  }

  const auto DependencyScopeAllowed = [](NevercIRPassLevel Analysis,
                                         NevercIRPassLevel Dependency) {
    if (Dependency == NEVERC_IR_PASS_LEVEL_MODULE)
      return true;
    if (Analysis == NEVERC_IR_PASS_LEVEL_CGSCC)
      return Dependency == NEVERC_IR_PASS_LEVEL_CGSCC;
    if (Analysis == NEVERC_IR_PASS_LEVEL_FUNCTION)
      return Dependency == NEVERC_IR_PASS_LEVEL_FUNCTION;
    if (Analysis == NEVERC_IR_PASS_LEVEL_LOOP)
      return Dependency == NEVERC_IR_PASS_LEVEL_FUNCTION ||
             Dependency == NEVERC_IR_PASS_LEVEL_LOOP;
    return false;
  };
  for (const AnalysisBinding &Binding : State->CustomAnalyses.Bindings) {
    for (NevercInterfaceID Dependency : Binding.Dependencies) {
      const AnalysisBinding *Required =
          State->CustomAnalyses.findBinding(Dependency);
      if (!Required)
        return createStringError(
            inconvertibleErrorCode(),
            "custom IR analysis dependency is missing from frozen session "
            "plan");
      if (!DependencyScopeAllowed(Binding.Descriptor.Level,
                                  Required->Descriptor.Level))
        return createStringError(
            inconvertibleErrorCode(),
            "custom IR analysis dependency has an incompatible scope");
    }
  }

  std::vector<uint8_t> Visit(State->CustomAnalyses.Bindings.size(), 0);
  std::function<Error(size_t)> VisitAnalysis = [&](size_t Index) -> Error {
    if (Visit[Index] == 1)
      return createStringError(inconvertibleErrorCode(),
                               "custom IR analysis dependency cycle");
    if (Visit[Index] == 2)
      return Error::success();
    Visit[Index] = 1;
    for (NevercInterfaceID Dependency :
         State->CustomAnalyses.Bindings[Index].Dependencies) {
      size_t DependencyIndex = 0;
      while (DependencyIndex != State->CustomAnalyses.Bindings.size() &&
             !sameInterface(
                 State->CustomAnalyses.Bindings[DependencyIndex]
                     .Descriptor.AnalysisID,
                 Dependency))
        ++DependencyIndex;
      if (DependencyIndex == State->CustomAnalyses.Bindings.size())
        return createStringError(inconvertibleErrorCode(),
                                 "custom IR analysis dependency disappeared");
      if (Error E = VisitAnalysis(DependencyIndex))
        return E;
    }
    Visit[Index] = 2;
    return Error::success();
  };
  for (size_t I = 0; I != State->CustomAnalyses.Bindings.size(); ++I)
    if (Error E = VisitAnalysis(I))
      return std::move(E);

  for (const auto &Module : Task.session().plugins()) {
    const PluginPublishedRegistration *Registration = Module->registration();
    if (!Registration)
      continue;
    for (const PluginRegistrationRecord &Record : Registration->records()) {
      if (Record.Kind != PluginRegistrationKind::IRPass)
        continue;
      PassBinding Binding;
      Binding.PluginID = Module->descriptor().PluginID;
      Binding.PassID = Record.PassID;
      Binding.Descriptor = Record.IRPass;
      Binding.Descriptor.PassID = {};
      Binding.RequiredAnalyses = Record.RequiredAnalyses;
      Binding.ExternalDependencyDigest =
          Record.IRExternalDependencyDigest;
      State->Bindings.push_back(std::move(Binding));
    }
  }
  return std::unique_ptr<IRPassPlan>(new IRPassPlan(std::move(State)));
}

IRPassPlan::IRPassPlan(std::unique_ptr<Impl> StateValue)
    : State(std::move(StateValue)) {}

IRPassPlan::~IRPassPlan() = default;

bool IRPassPlan::empty() const { return State->Bindings.empty(); }

void IRPassPlan::addPasses(ModulePassManager &Manager, NevercInterfaceID Phase,
                           NevercIROptimizationLevel OptimizationLevel) const {
  for (const PassBinding &Binding : State->Bindings) {
    if (!sameInterface(Binding.Descriptor.Phase, Phase))
      continue;
    switch (Binding.Descriptor.Level) {
    case NEVERC_IR_PASS_LEVEL_MODULE:
      Manager.addPass(
          PluginModulePass(*State, Binding, OptimizationLevel));
      break;
    case NEVERC_IR_PASS_LEVEL_CGSCC: {
      CGSCCPassManager CGSCC;
      CGSCC.addPass(PluginCGSCCPass(*State, Binding, OptimizationLevel));
      Manager.addPass(
          createModuleToPostOrderCGSCCPassAdaptor(std::move(CGSCC)));
      break;
    }
    case NEVERC_IR_PASS_LEVEL_FUNCTION: {
      FunctionPassManager Functions;
      Functions.addPass(
          PluginFunctionPass(*State, Binding, OptimizationLevel));
      Manager.addPass(
          createModuleToFunctionPassAdaptor(std::move(Functions)));
      break;
    }
    case NEVERC_IR_PASS_LEVEL_LOOP: {
      LoopPassManager Loops;
      Loops.addPass(PluginLoopPass(*State, Binding, OptimizationLevel));
      FunctionPassManager Functions;
      Functions.addPass(
          createFunctionToLoopPassAdaptor(std::move(Loops)));
      Manager.addPass(
          createModuleToFunctionPassAdaptor(std::move(Functions)));
      break;
    }
    }
  }
}

Error registerPluginIRPassInterface(PluginProcessServices &Services) {
  NevercInterfaceID AnalysisInterface{NEVERC_INTERFACE_IR_ANALYSIS_HIGH,
                                      NEVERC_INTERFACE_IR_ANALYSIS_LOW};
  if (Error E = Services.interfaces().registerInterface(
          AnalysisInterface, NEVERC_IR_ANALYSIS_INTERFACE_STABILITY,
          &AnalysisAPI, {}))
    return E;
  NevercInterfaceID Interface{NEVERC_INTERFACE_IR_PASS_HIGH,
                              NEVERC_INTERFACE_IR_PASS_LOW};
  return Services.interfaces().registerInterface(
      Interface, NEVERC_IR_PASS_INTERFACE_STABILITY, &PassAPI, {});
}

} // namespace neverc::plugin
