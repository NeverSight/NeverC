#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginPhaseExecutor.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Error.h"
#include "gtest/gtest.h"
#include <array>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace llvm;
using namespace neverc::plugin;

namespace neverc::plugin {

class PluginProcessServicesTestPeer {
public:
  static void setCallbackScopeSetupHook(PluginProcessServices &Services,
                                        void (*Hook)(void *),
                                        void *Context = nullptr) {
    Services.CallbackScopeSetupHookForTesting = Hook;
    Services.CallbackScopeSetupHookContextForTesting = Context;
  }
};

} // namespace neverc::plugin

namespace {

constexpr NevercInterfaceID TestPhaseID{0xabcddcba, 0x101};
constexpr NevercInterfaceID NestedReadOnlyPhaseID{0xabcddcba, 0x102};
constexpr NevercInterfaceID TestArtifactID{0xabcddcba, 0x202};
constexpr const char *TestPluginID = "org.neverc.test.scope.session";
constexpr const char *SecondPluginID = "org.neverc.test.other";
constexpr const char *ThirdPluginID = "org.neverc.test.minimal";

std::string takeErrorMessage(Error ErrorValue) {
  auto Message = toString(std::move(ErrorValue));
  return Message.str().str();
}

NevercStringView view(const char *Text) {
  return {Text, static_cast<uint64_t>(std::strlen(Text))};
}

NevercStatus failedStatus() {
  NevercStatus Status = neverc_status_ok();
  Status.Code = NEVERC_STATUS_PLUGIN_FAILURE;
  return Status;
}

PluginPhaseGraph
makeGraph(NevercPhasePolicy Policy = NEVERC_PHASE_OBSERVABLE |
                                     NEVERC_PHASE_INTERCEPTABLE |
                                     NEVERC_PHASE_REPLACEABLE) {
  PluginPhaseGraph Graph;
  PluginPhaseDefinition Phase;
  Phase.ID = TestPhaseID;
  Phase.CanonicalName = "test.phase.execute";
  Phase.Domain = "test";
  Phase.Verifier = "test.verify";
  Phase.InputArtifact = TestArtifactID;
  Phase.OutputArtifact = TestArtifactID;
  Phase.Policy = Policy;
  Phase.ObserverPoints = NEVERC_OBSERVER_BEFORE | NEVERC_OBSERVER_AFTER;
  Phase.HasBuiltinFallback = true;
  EXPECT_FALSE(Graph.addPhase(std::move(Phase)));
  EXPECT_FALSE(Graph.finalize());
  return Graph;
}

std::shared_ptr<const PluginArtifactType>
registerArtifact(PluginArtifactRegistry &Registry, int &Destroyed) {
  PluginArtifactTypeDescriptor Type;
  Type.ID = TestArtifactID;
  Type.Name = "test.phase.artifact";
  Type.Ownership = PluginArtifactOwnership::Owned;
  Type.Destroy = [&](void *) { ++Destroyed; };
  Type.Verify = [](const void *Payload) -> Error {
    if (!Payload || *static_cast<const int *>(Payload) < 0)
      return createStringError(inconvertibleErrorCode(),
                               "invalid test artifact");
    return Error::success();
  };
  auto Registered = Registry.registerType(std::move(Type));
  if (!Registered) {
    ADD_FAILURE() << takeErrorMessage(Registered.takeError());
    return nullptr;
  }
  if (Error E = Registry.freeze())
    ADD_FAILURE() << takeErrorMessage(std::move(E));
  return *Registered;
}

struct ActiveScopes {
  explicit ActiveScopes(uint64_t FirstArtifactMutationToken = 1)
      : Services("neverc-plugin-phase-tests", LLVM_VERSION_MAJOR, {},
                 FirstArtifactMutationToken) {
    initialize();
  }

  PluginProcessServices Services;
  std::unique_ptr<PluginSession> Session;
  std::unique_ptr<PluginTaskContext> Task;

  void initialize() {
    if (Error E = Services.interfaces().freeze())
      ADD_FAILURE() << takeErrorMessage(std::move(E));
    auto Loaded = Services.registry().load(NEVERC_TEST_SCOPE_SESSION_PLUGIN);
    if (!Loaded) {
      ADD_FAILURE() << takeErrorMessage(Loaded.takeError());
      return;
    }
    auto LoadedSecond = Services.registry().load(NEVERC_TEST_OTHER_PLUGIN);
    if (!LoadedSecond) {
      ADD_FAILURE() << takeErrorMessage(LoadedSecond.takeError());
      return;
    }
    auto LoadedThird = Services.registry().load(NEVERC_TEST_MINIMAL_PLUGIN);
    if (!LoadedThird) {
      ADD_FAILURE() << takeErrorMessage(LoadedThird.takeError());
      return;
    }
    const std::array<StringRef, 3> Selected = {TestPluginID, SecondPluginID,
                                               ThirdPluginID};
    auto Plan = makePluginActivationPlan(Services.registry(), Selected);
    if (!Plan) {
      ADD_FAILURE() << takeErrorMessage(Plan.takeError());
      return;
    }
    auto Created = PluginSession::create(Services, *Plan);
    if (!Created) {
      ADD_FAILURE() << takeErrorMessage(Created.takeError());
      return;
    }
    Session = std::move(*Created);
    auto CreatedTask = Session->createTask(NEVERC_TASK_INVOCATION);
    if (!CreatedTask) {
      ADD_FAILURE() << takeErrorMessage(CreatedTask.takeError());
      return;
    }
    Task = std::move(*CreatedTask);
  }

  ~ActiveScopes() {
    if (Task)
      consumeError(Task->end());
    if (Session)
      consumeError(Session->end());
    consumeError(Services.shutdown());
  }
};

NevercPhaseRoute route(const char *Triple = nullptr,
                       uint32_t ExecutionLevel = 0) {
  NevercPhaseRoute Route{};
  Route.Header = {sizeof(Route), NEVERC_PLUGIN_ABI_MAJOR,
                  NEVERC_PLUGIN_ABI_MINOR, 0};
  if (Triple)
    Route.TargetTriple = view(Triple);
  Route.ExecutionLevel = ExecutionLevel;
  return Route;
}

void setContinue(NevercPhaseResult *Result) {
  *Result = {};
  Result->Header = {sizeof(*Result), NEVERC_PLUGIN_ABI_MAJOR,
                    NEVERC_PLUGIN_ABI_MINOR, 0};
  Result->Action = NEVERC_PHASE_CONTINUE;
}

struct ObserverData {
  std::vector<std::string> *Trace = nullptr;
  const char *Name = nullptr;
  bool FailAfter = false;
  PluginTaskContext *Task = nullptr;
  bool CheckCandidate = false;
  NevercStatusCode CandidateLookup = NEVERC_STATUS_INVALID_STATE;
  NevercObserverPoint FailPoint = 0;
};

NevercStatus NEVERC_CALL observer(const NevercPhaseFrame *Frame,
                                  NevercObserverPoint Point, void *UserData) {
  auto *Data = static_cast<ObserverData *>(UserData);
  Data->Trace->push_back(
      std::string(Data->Name) +
      (Point == NEVERC_OBSERVER_BEFORE ? ":before" : ":after"));
  if (Point == NEVERC_OBSERVER_AFTER && Data->CheckCandidate) {
    void *Candidate = nullptr;
    Data->CandidateLookup =
        Data->Task->handles()
            .resolve(Frame->CurrentOutput, PluginArtifactHandleKind, &Candidate)
            .Code;
  }
  if (Data->FailAfter && Point == NEVERC_OBSERVER_AFTER)
    return failedStatus();
  if (Data->FailPoint == Point)
    return failedStatus();
  return neverc_status_ok();
}

struct InterceptorData {
  std::vector<std::string> *Trace = nullptr;
  const char *Name = nullptr;
  PluginPhaseExecutor *Executor = nullptr;
  PluginTaskContext *Task = nullptr;
  int *Replacement = nullptr;
  bool Replace = false;
  bool Skip = false;
  bool SkipNext = false;
  bool CallTwice = false;
  NevercProofHandle Proof{};
  NevercPhaseContinuation Saved{};
};

NevercStatus NEVERC_CALL interceptor(const NevercPhaseFrame *Frame,
                                     NevercPhaseContinuation *Continuation,
                                     NevercPhaseResult *OutResult,
                                     void *UserData) {
  auto *Data = static_cast<InterceptorData *>(UserData);
  if (Data->Trace)
    Data->Trace->push_back(Data->Name);
  Data->Saved = *Continuation;
  if (Data->Replace) {
    auto Candidate = Data->Executor->createCandidate(
        *Data->Task, TestArtifactID, Data->Replacement);
    if (!Candidate) {
      consumeError(Candidate.takeError());
      return failedStatus();
    }
    OutResult->Action = NEVERC_PHASE_REPLACE;
    OutResult->Output = *Candidate;
    return neverc_status_ok();
  }
  if (Data->Skip) {
    OutResult->Action = NEVERC_PHASE_SKIP;
    OutResult->Output = Frame->CurrentOutput;
    OutResult->Proof = Data->Proof;
    return neverc_status_ok();
  }
  if (Data->SkipNext)
    return neverc_status_ok();
  NevercStatus First = Continuation->InvokeNext(Continuation, Frame, OutResult);
  if (First.Code != NEVERC_STATUS_OK)
    return First;
  if (!Data->CallTwice) {
    setContinue(OutResult);
    return First;
  }
  return Continuation->InvokeNext(Continuation, Frame, OutResult);
}

struct ProviderData {
  PluginPhaseExecutor *Executor = nullptr;
  PluginTaskContext *Task = nullptr;
  std::vector<std::string> *Trace = nullptr;
  int *Payload = nullptr;
  int *Calls = nullptr;
  bool ReturnContinue = false;
};

enum class InvalidInterceptorMode {
  ContinueWithOutput,
  ReplaceWithProof,
  SkipWithoutOutput,
  ReplaceAfterNext,
  CallNextFromAnotherThread,
};

struct InvalidInterceptorData {
  InvalidInterceptorMode Mode = InvalidInterceptorMode::ContinueWithOutput;
  PluginPhaseExecutor *Executor = nullptr;
  PluginTaskContext *Task = nullptr;
  int *Payload = nullptr;
  NevercStatus ThreadStatus = neverc_status_ok();
};

NevercStatus NEVERC_CALL invalidInterceptor(
    const NevercPhaseFrame *Frame, NevercPhaseContinuation *Continuation,
    NevercPhaseResult *OutResult, void *UserData) {
  auto *Data = static_cast<InvalidInterceptorData *>(UserData);
  if (Data->Mode == InvalidInterceptorMode::CallNextFromAnotherThread) {
    std::thread Worker([&] {
      NevercPhaseResult ThreadResult{};
      ThreadResult.Header = {sizeof(ThreadResult), NEVERC_PLUGIN_ABI_MAJOR,
                             NEVERC_PLUGIN_ABI_MINOR, 0};
      Data->ThreadStatus =
          Continuation->InvokeNext(Continuation, Frame, &ThreadResult);
    });
    Worker.join();
    return Data->ThreadStatus;
  }

  if (Data->Mode == InvalidInterceptorMode::SkipWithoutOutput) {
    OutResult->Action = NEVERC_PHASE_SKIP;
    OutResult->Proof = {UINT64_C(1), UINT64_C(1)};
    return neverc_status_ok();
  }

  if (Data->Mode == InvalidInterceptorMode::ReplaceAfterNext)
    return Continuation->InvokeNext(Continuation, Frame, OutResult);

  auto Candidate = Data->Executor->createCandidate(*Data->Task, TestArtifactID,
                                                   Data->Payload);
  if (!Candidate) {
    consumeError(Candidate.takeError());
    return failedStatus();
  }
  OutResult->Output = *Candidate;
  if (Data->Mode == InvalidInterceptorMode::ReplaceWithProof) {
    OutResult->Action = NEVERC_PHASE_REPLACE;
    OutResult->Proof = {UINT64_C(1), UINT64_C(1)};
  } else {
    OutResult->Action = NEVERC_PHASE_CONTINUE;
  }
  return neverc_status_ok();
}

NevercInterceptorDescriptor
invalidInterceptorDescriptor(InvalidInterceptorData &Data) {
  NevercInterceptorDescriptor Descriptor{};
  Descriptor.Header = {sizeof(Descriptor), NEVERC_PLUGIN_ABI_MAJOR,
                       NEVERC_PLUGIN_ABI_MINOR, 0};
  Descriptor.Phase = TestPhaseID;
  Descriptor.Callback = invalidInterceptor;
  Descriptor.UserData = &Data;
  return Descriptor;
}

NevercStatus NEVERC_CALL provider(const NevercPhaseFrame *,
                                  NevercPhaseResult *OutResult,
                                  void *UserData) {
  auto *Data = static_cast<ProviderData *>(UserData);
  if (Data->Trace)
    Data->Trace->push_back("provider");
  if (Data->Calls)
    ++*Data->Calls;
  auto Candidate = Data->Executor->createCandidate(*Data->Task, TestArtifactID,
                                                   Data->Payload);
  if (!Candidate) {
    consumeError(Candidate.takeError());
    return failedStatus();
  }
  OutResult->Action =
      Data->ReturnContinue ? NEVERC_PHASE_CONTINUE : NEVERC_PHASE_REPLACE;
  OutResult->Output = *Candidate;
  return neverc_status_ok();
}

struct RecoverableProviderData {
  PluginPhaseExecutor *Executor = nullptr;
  PluginTaskContext *Task = nullptr;
  int *CandidatePayload = nullptr;
  int *Calls = nullptr;
  NevercStatusFlags Flags = NEVERC_STATUS_FLAG_RECOVERABLE;
  const NevercCoreAPI *Core = nullptr;
  const char *DiagnosticMessage = nullptr;
};

NevercStatus NEVERC_CALL recoverableProvider(const NevercPhaseFrame *,
                                             NevercPhaseResult *OutResult,
                                             void *UserData) {
  auto *Data = static_cast<RecoverableProviderData *>(UserData);
  if (Data->Calls)
    ++*Data->Calls;
  if (Data->CandidatePayload) {
    auto Candidate = Data->Executor->createCandidate(
        *Data->Task, TestArtifactID, Data->CandidatePayload);
    if (!Candidate) {
      consumeError(Candidate.takeError());
      return failedStatus();
    }
    OutResult->Action = NEVERC_PHASE_REPLACE;
    OutResult->Output = *Candidate;
  }
  if (Data->Core && Data->DiagnosticMessage) {
    NevercDiagnosticDescriptor Diagnostic{};
    Diagnostic.Header = {sizeof(Diagnostic), NEVERC_CORE_API_MAJOR,
                         NEVERC_CORE_API_MINOR, 0};
    Diagnostic.Severity = NEVERC_DIAGNOSTIC_WARNING;
    Diagnostic.Code = 9001;
    Diagnostic.PluginID = view(TestPluginID);
    Diagnostic.PhaseID = view("test.phase.execute");
    Diagnostic.Message = view(Data->DiagnosticMessage);
    NevercDiagnosticHandle Handle{};
    NevercStatus Emitted =
        Data->Core->EmitDiagnostic(Data->Core->Context, &Diagnostic, &Handle);
    if (Emitted.Code != NEVERC_STATUS_OK)
      return Emitted;
  }
  NevercStatus Status = failedStatus();
  Status.Flags = Data->Flags;
  return Status;
}

NevercProviderDescriptor
recoverableProviderDescriptor(RecoverableProviderData &Data,
                              NevercBool FallbackSafe) {
  NevercProviderDescriptor Descriptor{};
  Descriptor.Header = {sizeof(Descriptor), NEVERC_PLUGIN_ABI_MAJOR,
                       NEVERC_PLUGIN_ABI_MINOR, 0};
  Descriptor.Phase = TestPhaseID;
  Descriptor.ProviderID = view("recoverable-provider");
  Descriptor.Route.Header = {sizeof(Descriptor.Route), NEVERC_PLUGIN_ABI_MAJOR,
                             NEVERC_PLUGIN_ABI_MINOR, 0};
  Descriptor.Deterministic = NEVERC_TRUE;
  Descriptor.FallbackSafe = FallbackSafe;
  Descriptor.Callback = recoverableProvider;
  Descriptor.UserData = &Data;
  return Descriptor;
}

NevercObserverDescriptor observerDescriptor(ObserverData &Data) {
  NevercObserverDescriptor Descriptor{};
  Descriptor.Header = {sizeof(Descriptor), NEVERC_PLUGIN_ABI_MAJOR,
                       NEVERC_PLUGIN_ABI_MINOR, 0};
  Descriptor.Phase = TestPhaseID;
  Descriptor.Points = NEVERC_OBSERVER_BEFORE | NEVERC_OBSERVER_AFTER;
  Descriptor.Callback = observer;
  Descriptor.UserData = &Data;
  return Descriptor;
}

NevercInterceptorDescriptor interceptorDescriptor(InterceptorData &Data) {
  NevercInterceptorDescriptor Descriptor{};
  Descriptor.Header = {sizeof(Descriptor), NEVERC_PLUGIN_ABI_MAJOR,
                       NEVERC_PLUGIN_ABI_MINOR, 0};
  Descriptor.Phase = TestPhaseID;
  Descriptor.Callback = interceptor;
  Descriptor.UserData = &Data;
  return Descriptor;
}

NevercProviderDescriptor providerDescriptor(const char *ID,
                                            ProviderData &Data) {
  NevercProviderDescriptor Descriptor{};
  Descriptor.Header = {sizeof(Descriptor), NEVERC_PLUGIN_ABI_MAJOR,
                       NEVERC_PLUGIN_ABI_MINOR, 0};
  Descriptor.Phase = TestPhaseID;
  Descriptor.ProviderID = view(ID);
  Descriptor.Route.Header = {sizeof(Descriptor.Route), NEVERC_PLUGIN_ABI_MAJOR,
                             NEVERC_PLUGIN_ABI_MINOR, 0};
  Descriptor.Deterministic = NEVERC_TRUE;
  Descriptor.Callback = provider;
  Descriptor.UserData = &Data;
  return Descriptor;
}

struct NestedCapabilityObserverData {
  PluginPhaseExecutor *OuterExecutor = nullptr;
  PluginTaskContext *Task = nullptr;
  uint64_t OuterCapability = 0;
  bool OuterCapabilityAccepted = false;
};

NevercStatus NEVERC_CALL nestedCapabilityObserver(const NevercPhaseFrame *,
                                                  NevercObserverPoint,
                                                  void *UserData) {
  auto *Data = static_cast<NestedCapabilityObserverData *>(UserData);
  Data->OuterCapabilityAccepted =
      Data->OuterExecutor->validatesArtifactMutationCapability(
          *Data->Task, Data->OuterCapability);
  return neverc_status_ok();
}

struct NestedCapabilityInterceptorData {
  PluginPhaseExecutor *OuterExecutor = nullptr;
  PluginPhaseExecutor *InnerExecutor = nullptr;
  PluginSession *Session = nullptr;
  PluginTaskContext *Task = nullptr;
  NevercArtifactHandle Artifact{};
  NestedCapabilityObserverData *Observer = nullptr;
  bool AcceptedBeforeNestedCallback = false;
  bool AcceptedAfterNestedCallback = false;
  std::string NestedFailure;
};

NevercStatus NEVERC_CALL nestedCapabilityInterceptor(
    const NevercPhaseFrame *Frame, NevercPhaseContinuation *Continuation,
    NevercPhaseResult *OutResult, void *UserData) {
  auto *Data = static_cast<NestedCapabilityInterceptorData *>(UserData);
  auto Capability =
      Data->OuterExecutor->currentArtifactMutationCapability(*Data->Task);
  if (!Capability)
    return failedStatus();
  Data->Observer->OuterCapability = *Capability;
  Data->AcceptedBeforeNestedCallback =
      Data->OuterExecutor->validatesArtifactMutationCapability(*Data->Task,
                                                               *Capability);
  Error Nested = Data->InnerExecutor->notify(*Data->Session, *Data->Task,
                                             NestedReadOnlyPhaseID, route(),
                                             Data->Artifact);
  if (Nested) {
    Data->NestedFailure = takeErrorMessage(std::move(Nested));
    return failedStatus();
  }
  Data->AcceptedAfterNestedCallback =
      Data->OuterExecutor->validatesArtifactMutationCapability(*Data->Task,
                                                               *Capability);
  NevercStatus Status =
      Continuation->InvokeNext(Continuation, Frame, OutResult);
  if (Status.Code == NEVERC_STATUS_OK)
    setContinue(OutResult);
  return Status;
}

TEST(PluginPhaseExecutionTest,
     NestedReadOnlyCallbackShadowsOuterArtifactMutationCapability) {
  ActiveScopes Scopes;
  ASSERT_NE(Scopes.Task, nullptr);

  PluginPhaseGraph OuterGraph = makeGraph();
  PluginPhaseGraph InnerGraph;
  PluginPhaseDefinition InnerPhase;
  InnerPhase.ID = NestedReadOnlyPhaseID;
  InnerPhase.CanonicalName = "test.phase.nested-read-only";
  InnerPhase.Domain = "test";
  InnerPhase.Verifier = "test.verify";
  InnerPhase.InputArtifact = TestArtifactID;
  InnerPhase.OutputArtifact = TestArtifactID;
  InnerPhase.Policy = NEVERC_PHASE_OBSERVABLE;
  InnerPhase.ObserverPoints = NEVERC_OBSERVER_BEFORE;
  ASSERT_FALSE(InnerGraph.addPhase(std::move(InnerPhase)));
  ASSERT_FALSE(InnerGraph.finalize());

  PluginArtifactRegistry Artifacts;
  int Destroyed = 0;
  auto Type = registerArtifact(Artifacts, Destroyed);
  ASSERT_NE(Type, nullptr);
  PluginPhaseExecutor OuterExecutor(OuterGraph, Artifacts);
  PluginPhaseExecutor InnerExecutor(InnerGraph, Artifacts);

  int InputPayload = 1;
  auto Input = OuterExecutor.createArtifactView(*Scopes.Task, TestArtifactID,
                                                &InputPayload, 1);
  ASSERT_TRUE(static_cast<bool>(Input));

  NestedCapabilityObserverData ObserverData{&OuterExecutor, Scopes.Task.get()};
  NevercObserverDescriptor Observer{};
  Observer.Header = {sizeof(Observer), NEVERC_PLUGIN_ABI_MAJOR,
                     NEVERC_PLUGIN_ABI_MINOR, 0};
  Observer.Phase = NestedReadOnlyPhaseID;
  Observer.Points = NEVERC_OBSERVER_BEFORE;
  Observer.Callback = nestedCapabilityObserver;
  Observer.UserData = &ObserverData;
  ASSERT_FALSE(InnerExecutor.addObserver(SecondPluginID, Observer));

  NestedCapabilityInterceptorData InterceptorData{
      &OuterExecutor,    &InnerExecutor, Scopes.Session.get(),
      Scopes.Task.get(), *Input,         &ObserverData};
  NevercInterceptorDescriptor Interceptor{};
  Interceptor.Header = {sizeof(Interceptor), NEVERC_PLUGIN_ABI_MAJOR,
                        NEVERC_PLUGIN_ABI_MINOR, 0};
  Interceptor.Phase = TestPhaseID;
  Interceptor.Callback = nestedCapabilityInterceptor;
  Interceptor.UserData = &InterceptorData;
  ASSERT_FALSE(OuterExecutor.addInterceptor(TestPluginID, Interceptor));

  int OutputPayload = 2;
  ASSERT_FALSE(OuterExecutor.setBuiltinProvider(
      TestPhaseID, [&](const NevercPhaseFrame *, NevercPhaseResult *Result) {
        auto Candidate = OuterExecutor.createCandidate(
            *Scopes.Task, TestArtifactID, &OutputPayload);
        if (!Candidate) {
          consumeError(Candidate.takeError());
          return failedStatus();
        }
        Result->Action = NEVERC_PHASE_REPLACE;
        Result->Output = *Candidate;
        return neverc_status_ok();
      }));
  PluginArtifactSlot Slot(Type);

  EXPECT_FALSE(OuterExecutor.execute(*Scopes.Session, *Scopes.Task, TestPhaseID,
                                     route(), *Input, Slot));
  EXPECT_TRUE(InterceptorData.NestedFailure.empty());
  EXPECT_TRUE(InterceptorData.AcceptedBeforeNestedCallback);
  EXPECT_FALSE(ObserverData.OuterCapabilityAccepted);
  EXPECT_TRUE(InterceptorData.AcceptedAfterNestedCallback);
}

TEST(PluginPhaseExecutionTest,
     CapabilityTokenExhaustionIsProcessLocalAndRestoresOuterCallback) {
  constexpr uint64_t LastToken = std::numeric_limits<uint64_t>::max();
  int OuterDomain = 0;
  int NestedDomain = 0;

  {
    ActiveScopes Scopes(LastToken);
    ASSERT_NE(Scopes.Task, nullptr);
    bool NestedBodyRan = false;
    bool OuterRestored = false;
    auto Outer = Scopes.Task->invokeCallback(
        TestPluginID, "capability-exhaustion/outer",
        [&] {
          auto Token =
              Scopes.Task->currentArtifactMutationCapability(&OuterDomain);
          EXPECT_EQ(Token, std::optional<uint64_t>(LastToken));
          auto Nested = Scopes.Task->invokeCallback(
              SecondPluginID, "capability-exhaustion/nested",
              [&] {
                NestedBodyRan = true;
                return neverc_status_ok();
              },
              true, nullptr, false, &NestedDomain);
          EXPECT_TRUE(static_cast<bool>(Nested));
          if (Nested)
            EXPECT_EQ(Nested->Code, NEVERC_STATUS_RESOURCE_EXHAUSTED);
          OuterRestored = Scopes.Task->validatesArtifactMutationCapability(
              &OuterDomain, LastToken);
          return neverc_status_ok();
        },
        true, nullptr, false, &OuterDomain);
    ASSERT_TRUE(static_cast<bool>(Outer))
        << takeErrorMessage(Outer.takeError());
    EXPECT_EQ(Outer->Code, NEVERC_STATUS_OK);
    EXPECT_FALSE(NestedBodyRan);
    EXPECT_TRUE(OuterRestored);

    bool PlainBodyRan = false;
    auto Plain = Scopes.Task->invokeCallback(
        SecondPluginID, "capability-exhaustion/plain", [&] {
          PlainBodyRan = true;
          EXPECT_FALSE(
              Scopes.Task->currentArtifactMutationCapability(&OuterDomain));
          return neverc_status_ok();
        });
    ASSERT_TRUE(static_cast<bool>(Plain))
        << takeErrorMessage(Plain.takeError());
    EXPECT_EQ(Plain->Code, NEVERC_STATUS_OK);
    EXPECT_TRUE(PlainBodyRan);
  }

  // Capability generations belong to one PluginProcessServices instance. An
  // exhausted compiler/service must not consume another instance's namespace.
  {
    ActiveScopes IndependentScopes(LastToken);
    ASSERT_NE(IndependentScopes.Task, nullptr);
    uint64_t SeenToken = 0;
    auto Independent = IndependentScopes.Task->invokeCallback(
        TestPluginID, "capability-exhaustion/independent-process-services",
        [&] {
          auto Token =
              IndependentScopes.Task->currentArtifactMutationCapability(
                  &OuterDomain);
          if (Token)
            SeenToken = *Token;
          return neverc_status_ok();
        },
        true, nullptr, false, &OuterDomain);
    ASSERT_TRUE(static_cast<bool>(Independent))
        << takeErrorMessage(Independent.takeError());
    EXPECT_EQ(Independent->Code, NEVERC_STATUS_OK);
    EXPECT_EQ(SeenToken, LastToken);
  }
}

TEST(PluginPhaseExecutionTest,
     NestedProcessServicesCallbackShadowsAuthorityAndRejectsForgery) {
  ActiveScopes OuterScopes;
  ActiveScopes InnerScopes;
  ASSERT_NE(OuterScopes.Task, nullptr);
  ASSERT_NE(InnerScopes.Task, nullptr);
  auto OtherInnerTask =
      InnerScopes.Session->createTask(NEVERC_TASK_TRANSLATION_UNIT);
  ASSERT_TRUE(static_cast<bool>(OtherInnerTask))
      << takeErrorMessage(OtherInnerTask.takeError());

  int OuterDomain = 0;
  int InnerDomain = 0;
  int WrongDomain = 0;
  bool OuterShadowed = false;
  bool OuterRestored = false;
  bool ExactAccepted = false;
  bool WrongTokenRejected = false;
  bool WrongDomainRejected = false;
  bool WrongTaskRejected = false;
  NevercStatusCode ShadowedOuterCoreState = NEVERC_STATUS_OK;

  auto Outer = OuterScopes.Task->invokeCallback(
      TestPluginID, "cross-services/outer",
      [&] {
        auto OuterToken =
            OuterScopes.Task->currentArtifactMutationCapability(&OuterDomain);
        EXPECT_TRUE(OuterToken.has_value());
        auto Inner = InnerScopes.Task->invokeCallback(
            SecondPluginID, "cross-services/inner",
            [&] {
              auto InnerToken =
                  InnerScopes.Task->currentArtifactMutationCapability(
                      &InnerDomain);
              EXPECT_TRUE(InnerToken.has_value());
              OuterShadowed =
                  OuterToken &&
                  !OuterScopes.Task->validatesArtifactMutationCapability(
                      &OuterDomain, *OuterToken);
              ShadowedOuterCoreState =
                  OuterScopes.Services.coreAPI()
                      .CheckCancelled(OuterScopes.Services.coreAPI().Context,
                                      OuterScopes.Task->handle())
                      .Code;
              if (InnerToken) {
                ExactAccepted =
                    InnerScopes.Task->validatesArtifactMutationCapability(
                        &InnerDomain, *InnerToken);
                WrongTokenRejected =
                    !InnerScopes.Task->validatesArtifactMutationCapability(
                        &InnerDomain, *InnerToken - 1) &&
                    !InnerScopes.Task->validatesArtifactMutationCapability(
                        &InnerDomain, *InnerToken + 1);
                WrongDomainRejected =
                    !InnerScopes.Task->validatesArtifactMutationCapability(
                        &WrongDomain, *InnerToken);
                WrongTaskRejected = !(*OtherInnerTask)
                                         ->validatesArtifactMutationCapability(
                                             &InnerDomain, *InnerToken);
              }
              return neverc_status_ok();
            },
            true, nullptr, false, &InnerDomain);
        EXPECT_TRUE(static_cast<bool>(Inner));
        if (Inner)
          EXPECT_EQ(Inner->Code, NEVERC_STATUS_OK);
        OuterRestored =
            OuterToken && OuterScopes.Task->validatesArtifactMutationCapability(
                              &OuterDomain, *OuterToken);
        return neverc_status_ok();
      },
      true, nullptr, false, &OuterDomain);

  ASSERT_TRUE(static_cast<bool>(Outer)) << takeErrorMessage(Outer.takeError());
  EXPECT_EQ(Outer->Code, NEVERC_STATUS_OK);
  EXPECT_TRUE(OuterShadowed);
  EXPECT_TRUE(OuterRestored);
  EXPECT_TRUE(ExactAccepted);
  EXPECT_TRUE(WrongTokenRejected);
  EXPECT_TRUE(WrongDomainRejected);
  EXPECT_TRUE(WrongTaskRejected);
  EXPECT_EQ(ShadowedOuterCoreState, NEVERC_STATUS_INVALID_STATE);
  EXPECT_FALSE((*OtherInnerTask)->end());
}

TEST(PluginPhaseExecutionTest,
     CallbackSetupExceptionRollsBackNestedAuthorityAndPluginFrame) {
  ActiveScopes Scopes;
  ASSERT_NE(Scopes.Task, nullptr);
  int OuterDomain = 0;
  int NestedDomain = 0;
  bool NestedBodyRan = false;
  bool OuterAuthorityRestored = false;

  auto Outer = Scopes.Task->invokeCallback(
      TestPluginID, "setup-exception/outer",
      [&] {
        auto OuterToken =
            Scopes.Task->currentArtifactMutationCapability(&OuterDomain);
        EXPECT_TRUE(OuterToken.has_value());

        PluginProcessServicesTestPeer::setCallbackScopeSetupHook(
            Scopes.Services, +[](void *) { throw std::bad_alloc(); });
        auto Nested = Scopes.Task->invokeCallback(
            SecondPluginID, "setup-exception/nested",
            [&] {
              NestedBodyRan = true;
              return neverc_status_ok();
            },
            true, nullptr, false, &NestedDomain);
        PluginProcessServicesTestPeer::setCallbackScopeSetupHook(
            Scopes.Services, nullptr);

        EXPECT_TRUE(static_cast<bool>(Nested));
        if (Nested)
          EXPECT_EQ(Nested->Code, NEVERC_STATUS_RESOURCE_EXHAUSTED);
        OuterAuthorityRestored =
            OuterToken && Scopes.Task->validatesArtifactMutationCapability(
                              &OuterDomain, *OuterToken);
        return neverc_status_ok();
      },
      true, nullptr, false, &OuterDomain);

  ASSERT_TRUE(static_cast<bool>(Outer)) << takeErrorMessage(Outer.takeError());
  EXPECT_EQ(Outer->Code, NEVERC_STATUS_OK);
  EXPECT_FALSE(NestedBodyRan);
  EXPECT_TRUE(OuterAuthorityRestored);

  bool LaterCallbackRan = false;
  auto Later =
      Scopes.Task->invokeCallback(SecondPluginID, "setup-exception/later", [&] {
        LaterCallbackRan = true;
        EXPECT_TRUE(
            Scopes.Session->currentCallbackPluginID().equals(SecondPluginID));
        EXPECT_FALSE(
            Scopes.Task->currentArtifactMutationCapability(&OuterDomain));
        return neverc_status_ok();
      });
  ASSERT_TRUE(static_cast<bool>(Later)) << takeErrorMessage(Later.takeError());
  EXPECT_EQ(Later->Code, NEVERC_STATUS_OK);
  EXPECT_TRUE(LaterCallbackRan);
}

TEST(PluginPhaseExecutionTest,
     PluginExceptionRollsBackNestedAuthorityAndPluginFrame) {
  ActiveScopes Scopes;
  ASSERT_NE(Scopes.Task, nullptr);
  int OuterDomain = 0;
  int NestedDomain = 0;
  bool OuterAuthorityRestored = false;

  auto Outer = Scopes.Task->invokeCallback(
      TestPluginID, "plugin-exception/outer",
      [&] {
        auto OuterToken =
            Scopes.Task->currentArtifactMutationCapability(&OuterDomain);
        EXPECT_TRUE(OuterToken.has_value());
        auto Nested = Scopes.Task->invokeCallback(
            SecondPluginID, "plugin-exception/nested",
            []() -> NevercStatus {
              throw std::runtime_error("injected plugin callback failure");
            },
            true, nullptr, false, &NestedDomain);
        EXPECT_TRUE(static_cast<bool>(Nested));
        if (Nested)
          EXPECT_EQ(Nested->Code, NEVERC_STATUS_PLUGIN_EXCEPTION);
        OuterAuthorityRestored =
            OuterToken &&
            Scopes.Task->validatesArtifactMutationCapability(&OuterDomain,
                                                             *OuterToken) &&
            Scopes.Session->currentCallbackPluginID().equals(TestPluginID);
        return neverc_status_ok();
      },
      true, nullptr, false, &OuterDomain);

  ASSERT_TRUE(static_cast<bool>(Outer)) << takeErrorMessage(Outer.takeError());
  EXPECT_EQ(Outer->Code, NEVERC_STATUS_OK);
  EXPECT_TRUE(OuterAuthorityRestored);

  bool LaterCallbackRan = false;
  auto Later = Scopes.Task->invokeCallback(
      SecondPluginID, "plugin-exception/later",
      [&] {
        LaterCallbackRan = true;
        EXPECT_TRUE(
            Scopes.Session->currentCallbackPluginID().equals(SecondPluginID));
        EXPECT_FALSE(
            Scopes.Task->currentArtifactMutationCapability(&OuterDomain));
        return neverc_status_ok();
      },
      /*CheckCancellation=*/false);
  ASSERT_TRUE(static_cast<bool>(Later)) << takeErrorMessage(Later.takeError());
  EXPECT_EQ(Later->Code, NEVERC_STATUS_OK);
  EXPECT_TRUE(LaterCallbackRan);
}

TEST(PluginPhaseExecutionTest,
     RunsObserversInterceptorsProviderAndAtomicPublishInOrder) {
  ActiveScopes Scopes;
  ASSERT_NE(Scopes.Task, nullptr);
  PluginPhaseGraph Graph = makeGraph();
  PluginArtifactRegistry Artifacts;
  int Destroyed = 0;
  auto Type = registerArtifact(Artifacts, Destroyed);
  ASSERT_NE(Type, nullptr);
  PluginArtifactSlot Slot(Type);
  PluginPhaseExecutor Executor(Graph, Artifacts);

  std::vector<std::string> Trace;
  ObserverData FirstObserver{&Trace, "observer-one", false};
  ObserverData SecondObserver{&Trace, "observer-two", false, Scopes.Task.get(),
                              true};
  InterceptorData FirstInterceptor{&Trace, "interceptor-one"};
  InterceptorData SecondInterceptor{&Trace, "interceptor-two"};
  int Payload = 42;
  int ProviderCalls = 0;
  ProviderData Provider{&Executor, Scopes.Task.get(), &Trace, &Payload,
                        &ProviderCalls};
  ASSERT_FALSE(
      Executor.addObserver(TestPluginID, observerDescriptor(FirstObserver)));
  ASSERT_FALSE(
      Executor.addObserver(TestPluginID, observerDescriptor(SecondObserver)));
  ASSERT_FALSE(Executor.addInterceptor(
      TestPluginID, interceptorDescriptor(FirstInterceptor)));
  ASSERT_FALSE(Executor.addInterceptor(
      SecondPluginID, interceptorDescriptor(SecondInterceptor)));
  ASSERT_FALSE(Executor.addProvider(
      ThirdPluginID, providerDescriptor("test-provider", Provider)));
  int InputPayload = 0;
  auto Input = Executor.createArtifactView(*Scopes.Task, TestArtifactID,
                                           &InputPayload, 1);
  ASSERT_TRUE(static_cast<bool>(Input));

  EXPECT_FALSE(Executor.execute(*Scopes.Session, *Scopes.Task, TestPhaseID,
                                route(), *Input, Slot));
  EXPECT_EQ(ProviderCalls, 1);
  EXPECT_EQ(Slot.payload(), &Payload);
  EXPECT_EQ(Trace, (std::vector<std::string>{
                       "observer-one:before", "observer-two:before",
                       "interceptor-one", "interceptor-two", "provider",
                       "observer-two:after", "observer-one:after"}));
  EXPECT_EQ(Destroyed, 0);
  EXPECT_EQ(SecondObserver.CandidateLookup, NEVERC_STATUS_OK);
}

TEST(PluginPhaseExecutionTest,
     ReplacementSkipsBuiltinAndInvalidContinuationUseIsRejected) {
  ActiveScopes Scopes;
  ASSERT_NE(Scopes.Task, nullptr);
  PluginPhaseGraph Graph = makeGraph();
  PluginArtifactRegistry Artifacts;
  int Destroyed = 0;
  auto Type = registerArtifact(Artifacts, Destroyed);
  ASSERT_NE(Type, nullptr);
  PluginArtifactSlot Slot(Type);

  int Replacement = 7;
  int BuiltinPayload = 9;
  int BuiltinCalls = 0;
  PluginPhaseExecutor Executor(Graph, Artifacts);
  InterceptorData Replacer;
  Replacer.Executor = &Executor;
  Replacer.Task = Scopes.Task.get();
  Replacer.Replacement = &Replacement;
  Replacer.Replace = true;
  ASSERT_FALSE(
      Executor.addInterceptor(TestPluginID, interceptorDescriptor(Replacer)));
  ASSERT_FALSE(Executor.setBuiltinProvider(
      TestPhaseID, [&](const NevercPhaseFrame *, NevercPhaseResult *Result) {
        ++BuiltinCalls;
        auto Candidate = Executor.createCandidate(*Scopes.Task, TestArtifactID,
                                                  &BuiltinPayload);
        if (!Candidate) {
          consumeError(Candidate.takeError());
          return failedStatus();
        }
        Result->Action = NEVERC_PHASE_REPLACE;
        Result->Output = *Candidate;
        return neverc_status_ok();
      }));
  int InputPayload = 0;
  auto Input = Executor.createArtifactView(*Scopes.Task, TestArtifactID,
                                           &InputPayload, 1);
  ASSERT_TRUE(static_cast<bool>(Input));
  EXPECT_FALSE(Executor.execute(*Scopes.Session, *Scopes.Task, TestPhaseID,
                                route(), *Input, Slot));
  EXPECT_EQ(BuiltinCalls, 0);
  EXPECT_EQ(Slot.payload(), &Replacement);

  NevercPhaseFrame DummyFrame{};
  DummyFrame.Header = {sizeof(DummyFrame), NEVERC_PLUGIN_ABI_MAJOR,
                       NEVERC_PLUGIN_ABI_MINOR, 0};
  NevercPhaseResult DummyResult{};
  DummyResult.Header = {sizeof(DummyResult), NEVERC_PLUGIN_ABI_MAJOR,
                        NEVERC_PLUGIN_ABI_MINOR, 0};
  NevercStatus Escaped =
      Replacer.Saved.InvokeNext(&Replacer.Saved, &DummyFrame, &DummyResult);
  EXPECT_EQ(Escaped.Code, NEVERC_STATUS_INVALID_STATE);
}

TEST(PluginPhaseExecutionTest, RejectsMissingAndRepeatedNextWithoutPublishing) {
  ActiveScopes Scopes;
  ASSERT_NE(Scopes.Task, nullptr);
  PluginPhaseGraph Graph = makeGraph();
  PluginArtifactRegistry Artifacts;
  int Destroyed = 0;
  auto Type = registerArtifact(Artifacts, Destroyed);
  ASSERT_NE(Type, nullptr);
  PluginArtifactSlot Slot(Type);
  int Payload = 5;

  {
    PluginPhaseExecutor Executor(Graph, Artifacts);
    InterceptorData Missing;
    Missing.SkipNext = true;
    ASSERT_FALSE(
        Executor.addInterceptor(TestPluginID, interceptorDescriptor(Missing)));
    ASSERT_FALSE(Executor.setBuiltinProvider(
        TestPhaseID, [&](const NevercPhaseFrame *, NevercPhaseResult *Result) {
          auto Candidate =
              Executor.createCandidate(*Scopes.Task, TestArtifactID, &Payload);
          if (!Candidate) {
            consumeError(Candidate.takeError());
            return failedStatus();
          }
          Result->Action = NEVERC_PHASE_REPLACE;
          Result->Output = *Candidate;
          return neverc_status_ok();
        }));
    int InputPayload = 0;
    auto Input = Executor.createArtifactView(*Scopes.Task, TestArtifactID,
                                             &InputPayload, 1);
    ASSERT_TRUE(static_cast<bool>(Input));
    Error MissingNext = Executor.execute(*Scopes.Session, *Scopes.Task,
                                         TestPhaseID, route(), *Input, Slot);
    ASSERT_TRUE(static_cast<bool>(MissingNext));
    EXPECT_NE(takeErrorMessage(std::move(MissingNext)).find("InvokeNext"),
              std::string::npos);
    EXPECT_EQ(Slot.payload(), nullptr);
  }

  {
    ActiveScopes RepeatedScopes;
    ASSERT_NE(RepeatedScopes.Task, nullptr);
    PluginArtifactSlot RepeatedSlot(Type);
    PluginPhaseExecutor Executor(Graph, Artifacts);
    InterceptorData Repeated;
    Repeated.CallTwice = true;
    ASSERT_FALSE(
        Executor.addInterceptor(TestPluginID, interceptorDescriptor(Repeated)));
    ASSERT_FALSE(Executor.setBuiltinProvider(
        TestPhaseID, [&](const NevercPhaseFrame *, NevercPhaseResult *Result) {
          auto Candidate = Executor.createCandidate(*RepeatedScopes.Task,
                                                    TestArtifactID, &Payload);
          if (!Candidate) {
            consumeError(Candidate.takeError());
            return failedStatus();
          }
          Result->Action = NEVERC_PHASE_REPLACE;
          Result->Output = *Candidate;
          return neverc_status_ok();
        }));
    int InputPayload = 0;
    auto Input = Executor.createArtifactView(*RepeatedScopes.Task,
                                             TestArtifactID, &InputPayload, 1);
    ASSERT_TRUE(static_cast<bool>(Input));
    Error RepeatedNext =
        Executor.execute(*RepeatedScopes.Session, *RepeatedScopes.Task,
                         TestPhaseID, route(), *Input, RepeatedSlot);
    ASSERT_TRUE(static_cast<bool>(RepeatedNext));
    EXPECT_NE(takeErrorMessage(std::move(RepeatedNext)).find("InvokeNext"),
              std::string::npos);
    EXPECT_EQ(RepeatedSlot.payload(), nullptr);
  }
}

TEST(PluginPhaseExecutionTest,
     AfterObserverFailureRollsBackCandidateAndProviderConflictIsExplicit) {
  ActiveScopes Scopes;
  ASSERT_NE(Scopes.Task, nullptr);
  PluginPhaseGraph Graph = makeGraph();
  PluginArtifactRegistry Artifacts;
  int Destroyed = 0;
  auto Type = registerArtifact(Artifacts, Destroyed);
  ASSERT_NE(Type, nullptr);
  PluginArtifactSlot Slot(Type);
  int Published = 1;
  auto Initial =
      PluginArtifactTransaction::create(Artifacts, TestArtifactID, &Published);
  ASSERT_TRUE(static_cast<bool>(Initial));
  ASSERT_FALSE((*Initial)->commit(Slot));

  int CandidatePayload = 2;
  PluginPhaseExecutor Executor(Graph, Artifacts);
  std::vector<std::string> Trace;
  ObserverData Failing{&Trace, "failing", true};
  ProviderData Candidate{&Executor, Scopes.Task.get(), nullptr,
                         &CandidatePayload, nullptr};
  ASSERT_FALSE(Executor.addObserver(TestPluginID, observerDescriptor(Failing)));
  ASSERT_FALSE(Executor.addProvider(
      TestPluginID, providerDescriptor("provider-one", Candidate)));
  int InputPayload = 0;
  auto Input = Executor.createArtifactView(*Scopes.Task, TestArtifactID,
                                           &InputPayload, 1);
  ASSERT_TRUE(static_cast<bool>(Input));
  Error FailedAfter = Executor.execute(*Scopes.Session, *Scopes.Task,
                                       TestPhaseID, route(), *Input, Slot);
  ASSERT_TRUE(static_cast<bool>(FailedAfter));
  EXPECT_NE(takeErrorMessage(std::move(FailedAfter)).find("Observer"),
            std::string::npos);
  EXPECT_EQ(Slot.payload(), &Published);
  EXPECT_EQ(Slot.generation(), 1U);
  EXPECT_EQ(Destroyed, 1);

  ActiveScopes ConflictScopes;
  ASSERT_NE(ConflictScopes.Task, nullptr);
  PluginPhaseExecutor Conflict(Graph, Artifacts);
  ProviderData First{&Conflict, ConflictScopes.Task.get(), nullptr,
                     &CandidatePayload, nullptr};
  ProviderData Second = First;
  ASSERT_FALSE(Conflict.addProvider(TestPluginID,
                                    providerDescriptor("provider-one", First)));
  ASSERT_FALSE(Conflict.addProvider(
      TestPluginID, providerDescriptor("provider-two", Second)));
  int ConflictInputPayload = 0;
  auto ConflictInput = Conflict.createArtifactView(
      *ConflictScopes.Task, TestArtifactID, &ConflictInputPayload, 1);
  ASSERT_TRUE(static_cast<bool>(ConflictInput));
  Error Ambiguous =
      Conflict.execute(*ConflictScopes.Session, *ConflictScopes.Task,
                       TestPhaseID, route(), *ConflictInput, Slot);
  ASSERT_TRUE(static_cast<bool>(Ambiguous));
  EXPECT_NE(takeErrorMessage(std::move(Ambiguous)).find("multiple"),
            std::string::npos);
  EXPECT_EQ(Slot.payload(), &Published);
}

TEST(PluginPhaseExecutionTest,
     RejectsMalformedResultsAndCrossThreadContinuationCalls) {
  PluginArtifactRegistry Artifacts;
  int Destroyed = 0;
  auto Type = registerArtifact(Artifacts, Destroyed);
  ASSERT_NE(Type, nullptr);
  struct Case {
    InvalidInterceptorMode Mode;
    NevercPhasePolicy Policy;
    const char *Message;
    bool CreatesCandidate;
  };
  const Case Cases[] = {
      {InvalidInterceptorMode::ContinueWithOutput,
       NEVERC_PHASE_OBSERVABLE | NEVERC_PHASE_INTERCEPTABLE |
           NEVERC_PHASE_REPLACEABLE,
       "CONTINUE", true},
      {InvalidInterceptorMode::ReplaceWithProof,
       NEVERC_PHASE_OBSERVABLE | NEVERC_PHASE_INTERCEPTABLE |
           NEVERC_PHASE_REPLACEABLE,
       "REPLACE", true},
      {InvalidInterceptorMode::SkipWithoutOutput,
       NEVERC_PHASE_OBSERVABLE | NEVERC_PHASE_INTERCEPTABLE |
           NEVERC_PHASE_SKIPPABLE_WITH_PROOF,
       "SKIP", false},
      {InvalidInterceptorMode::ReplaceAfterNext,
       NEVERC_PHASE_OBSERVABLE | NEVERC_PHASE_INTERCEPTABLE |
           NEVERC_PHASE_REPLACEABLE,
       "CONTINUE", true},
      {InvalidInterceptorMode::CallNextFromAnotherThread,
       NEVERC_PHASE_OBSERVABLE | NEVERC_PHASE_INTERCEPTABLE |
           NEVERC_PHASE_REPLACEABLE,
       "thread", false},
  };

  for (const Case &Current : Cases) {
    SCOPED_TRACE(static_cast<int>(Current.Mode));
    ActiveScopes Scopes;
    ASSERT_NE(Scopes.Task, nullptr);
    PluginPhaseGraph Graph = makeGraph(Current.Policy);
    PluginArtifactSlot Slot(Type);
    PluginPhaseExecutor Executor(Graph, Artifacts);
    int CandidatePayload = 61;
    InvalidInterceptorData Data;
    Data.Mode = Current.Mode;
    Data.Executor = &Executor;
    Data.Task = Scopes.Task.get();
    Data.Payload = &CandidatePayload;
    ASSERT_FALSE(Executor.addInterceptor(TestPluginID,
                                         invalidInterceptorDescriptor(Data)));
    ASSERT_FALSE(Executor.setBuiltinProvider(
        TestPhaseID, [&](const NevercPhaseFrame *, NevercPhaseResult *Result) {
          auto Candidate = Executor.createCandidate(
              *Scopes.Task, TestArtifactID, &CandidatePayload);
          if (!Candidate) {
            consumeError(Candidate.takeError());
            return failedStatus();
          }
          Result->Action = NEVERC_PHASE_REPLACE;
          Result->Output = *Candidate;
          return neverc_status_ok();
        }));
    int InputPayload = 1;
    auto Input = Executor.createArtifactView(*Scopes.Task, TestArtifactID,
                                             &InputPayload, 1);
    ASSERT_TRUE(static_cast<bool>(Input));
    int DestroyedBefore = Destroyed;

    Error Rejected = Executor.execute(*Scopes.Session, *Scopes.Task,
                                      TestPhaseID, route(), *Input, Slot);
    ASSERT_TRUE(static_cast<bool>(Rejected));
    EXPECT_NE(takeErrorMessage(std::move(Rejected)).find(Current.Message),
              std::string::npos);
    EXPECT_EQ(Slot.payload(), nullptr);
    EXPECT_EQ(Scopes.Task->handles().liveCount(), 1U);
    EXPECT_EQ(Destroyed, DestroyedBefore + (Current.CreatesCandidate ? 1 : 0));
    EXPECT_TRUE(Scopes.Session->isCancelled());
    if (Current.Mode == InvalidInterceptorMode::CallNextFromAnotherThread)
      EXPECT_EQ(Data.ThreadStatus.Code, NEVERC_STATUS_WRONG_SCOPE);
  }

  {
    ActiveScopes Scopes;
    ASSERT_NE(Scopes.Task, nullptr);
    PluginPhaseGraph Graph = makeGraph();
    PluginArtifactSlot Slot(Type);
    PluginPhaseExecutor Executor(Graph, Artifacts);
    int CandidatePayload = 62;
    ProviderData Data{&Executor,         Scopes.Task.get(), nullptr,
                      &CandidatePayload, nullptr,           true};
    ASSERT_FALSE(Executor.addProvider(
        TestPluginID, providerDescriptor("bad-provider", Data)));
    int InputPayload = 2;
    auto Input = Executor.createArtifactView(*Scopes.Task, TestArtifactID,
                                             &InputPayload, 1);
    ASSERT_TRUE(static_cast<bool>(Input));
    int DestroyedBefore = Destroyed;

    Error Rejected = Executor.execute(*Scopes.Session, *Scopes.Task,
                                      TestPhaseID, route(), *Input, Slot);
    ASSERT_TRUE(static_cast<bool>(Rejected));
    EXPECT_NE(takeErrorMessage(std::move(Rejected)).find("Provider"),
              std::string::npos);
    EXPECT_EQ(Scopes.Task->handles().liveCount(), 1U);
    EXPECT_EQ(Destroyed, DestroyedBefore + 1);
  }
}

TEST(PluginPhaseExecutionTest,
     ValidatesLiveStaleAndCrossTaskEquivalenceProofs) {
  PluginPhaseGraph Graph =
      makeGraph(NEVERC_PHASE_OBSERVABLE | NEVERC_PHASE_INTERCEPTABLE |
                NEVERC_PHASE_SKIPPABLE_WITH_PROOF);
  PluginArtifactRegistry Artifacts;
  int Destroyed = 0;
  auto Type = registerArtifact(Artifacts, Destroyed);
  ASSERT_NE(Type, nullptr);

  {
    ActiveScopes Scopes;
    ASSERT_NE(Scopes.Task, nullptr);
    PluginArtifactSlot Slot(Type);
    int Published = 10;
    auto Initial = PluginArtifactTransaction::create(Artifacts, TestArtifactID,
                                                     &Published);
    ASSERT_TRUE(static_cast<bool>(Initial));
    ASSERT_FALSE((*Initial)->commit(Slot));

    PluginPhaseExecutor Executor(Graph, Artifacts);
    int InputPayload = 1;
    auto Input = Executor.createArtifactView(*Scopes.Task, TestArtifactID,
                                             &InputPayload, 7);
    ASSERT_TRUE(static_cast<bool>(Input));
    auto Proof = Executor.createEquivalenceProof(*Scopes.Task, TestPhaseID,
                                                 *Input, Slot, route());
    ASSERT_TRUE(static_cast<bool>(Proof));
    InterceptorData Skipper;
    Skipper.Skip = true;
    Skipper.Proof = *Proof;
    ASSERT_FALSE(
        Executor.addInterceptor(TestPluginID, interceptorDescriptor(Skipper)));

    EXPECT_FALSE(Executor.execute(*Scopes.Session, *Scopes.Task, TestPhaseID,
                                  route(), *Input, Slot));
    EXPECT_EQ(Slot.payload(), &Published);
    EXPECT_EQ(Slot.generation(), 1U);
    EXPECT_FALSE(Scopes.Session->isCancelled());
  }

  {
    ActiveScopes Scopes;
    ASSERT_NE(Scopes.Task, nullptr);
    PluginArtifactSlot Slot(Type);
    int OldOutput = 20;
    auto Initial = PluginArtifactTransaction::create(Artifacts, TestArtifactID,
                                                     &OldOutput);
    ASSERT_TRUE(static_cast<bool>(Initial));
    ASSERT_FALSE((*Initial)->commit(Slot));

    PluginPhaseExecutor Executor(Graph, Artifacts);
    int InputPayload = 2;
    auto Input = Executor.createArtifactView(*Scopes.Task, TestArtifactID,
                                             &InputPayload, 8);
    ASSERT_TRUE(static_cast<bool>(Input));
    auto Proof = Executor.createEquivalenceProof(*Scopes.Task, TestPhaseID,
                                                 *Input, Slot, route());
    ASSERT_TRUE(static_cast<bool>(Proof));

    int NewOutput = 21;
    auto Replacement = PluginArtifactTransaction::create(
        Artifacts, TestArtifactID, &NewOutput);
    ASSERT_TRUE(static_cast<bool>(Replacement));
    ASSERT_FALSE((*Replacement)->commit(Slot));
    InterceptorData Skipper;
    Skipper.Skip = true;
    Skipper.Proof = *Proof;
    ASSERT_FALSE(
        Executor.addInterceptor(TestPluginID, interceptorDescriptor(Skipper)));

    Error Stale = Executor.execute(*Scopes.Session, *Scopes.Task, TestPhaseID,
                                   route(), *Input, Slot);
    ASSERT_TRUE(static_cast<bool>(Stale));
    EXPECT_NE(takeErrorMessage(std::move(Stale)).find("proof"),
              std::string::npos);
    EXPECT_EQ(Slot.payload(), &NewOutput);
    EXPECT_EQ(Slot.generation(), 2U);
    EXPECT_TRUE(Scopes.Session->isCancelled());
  }

  {
    ActiveScopes ProofScopes;
    ActiveScopes ExecutionScopes;
    ASSERT_NE(ProofScopes.Task, nullptr);
    ASSERT_NE(ExecutionScopes.Task, nullptr);
    PluginArtifactSlot Slot(Type);
    int Published = 30;
    auto Initial = PluginArtifactTransaction::create(Artifacts, TestArtifactID,
                                                     &Published);
    ASSERT_TRUE(static_cast<bool>(Initial));
    ASSERT_FALSE((*Initial)->commit(Slot));

    PluginPhaseExecutor Executor(Graph, Artifacts);
    int ProofInputPayload = 3;
    auto ProofInput = Executor.createArtifactView(
        *ProofScopes.Task, TestArtifactID, &ProofInputPayload, 9);
    ASSERT_TRUE(static_cast<bool>(ProofInput));
    auto ForeignProof = Executor.createEquivalenceProof(
        *ProofScopes.Task, TestPhaseID, *ProofInput, Slot, route());
    ASSERT_TRUE(static_cast<bool>(ForeignProof));
    int ExecutionInputPayload = 3;
    auto ExecutionInput = Executor.createArtifactView(
        *ExecutionScopes.Task, TestArtifactID, &ExecutionInputPayload, 9);
    ASSERT_TRUE(static_cast<bool>(ExecutionInput));
    InterceptorData Skipper;
    Skipper.Skip = true;
    Skipper.Proof = *ForeignProof;
    ASSERT_FALSE(
        Executor.addInterceptor(TestPluginID, interceptorDescriptor(Skipper)));

    Error CrossTask =
        Executor.execute(*ExecutionScopes.Session, *ExecutionScopes.Task,
                         TestPhaseID, route(), *ExecutionInput, Slot);
    ASSERT_TRUE(static_cast<bool>(CrossTask));
    EXPECT_NE(takeErrorMessage(std::move(CrossTask)).find("proof"),
              std::string::npos);
    EXPECT_EQ(Slot.payload(), &Published);
    EXPECT_TRUE(ExecutionScopes.Session->isCancelled());
  }
}

TEST(PluginPhaseExecutionTest,
     SelectsProvidersByPluginAndUsesBuiltinForUnmatchedRoute) {
  PluginPhaseGraph Graph = makeGraph();
  PluginArtifactRegistry Artifacts;
  int Destroyed = 0;
  auto Type = registerArtifact(Artifacts, Destroyed);
  ASSERT_NE(Type, nullptr);

  {
    ActiveScopes Scopes;
    ASSERT_NE(Scopes.Task, nullptr);
    PluginArtifactSlot Slot(Type);
    PluginPhaseExecutor Executor(Graph, Artifacts);
    int FirstPayload = 41;
    int SecondPayload = 42;
    int FirstCalls = 0;
    int SecondCalls = 0;
    ProviderData First{&Executor, Scopes.Task.get(), nullptr, &FirstPayload,
                       &FirstCalls};
    ProviderData Second{&Executor, Scopes.Task.get(), nullptr, &SecondPayload,
                        &SecondCalls};
    NevercProviderDescriptor FirstDescriptor =
        providerDescriptor("first", First);
    FirstDescriptor.Route.TargetTriple = view("aarch64-test-none");
    FirstDescriptor.Route.ExecutionLevel = 2;
    NevercProviderDescriptor SecondDescriptor =
        providerDescriptor("second", Second);
    SecondDescriptor.Route.TargetTriple = view("aarch64-test-none");
    SecondDescriptor.Route.ExecutionLevel = 2;
    ASSERT_FALSE(Executor.addProvider(TestPluginID, FirstDescriptor));
    ASSERT_FALSE(Executor.addProvider(SecondPluginID, SecondDescriptor));
    ASSERT_FALSE(Executor.selectProvider(TestPhaseID, SecondPluginID));
    int InputPayload = 1;
    auto Input = Executor.createArtifactView(*Scopes.Task, TestArtifactID,
                                             &InputPayload, 1);
    ASSERT_TRUE(static_cast<bool>(Input));

    EXPECT_FALSE(Executor.execute(*Scopes.Session, *Scopes.Task, TestPhaseID,
                                  route("aarch64-test-none", 2), *Input, Slot));
    EXPECT_EQ(FirstCalls, 0);
    EXPECT_EQ(SecondCalls, 1);
    EXPECT_EQ(Slot.payload(), &SecondPayload);
    EXPECT_TRUE(Executor.isFrozen());
    Error LateRegistration =
        Executor.addProvider(TestPluginID, FirstDescriptor);
    ASSERT_TRUE(static_cast<bool>(LateRegistration));
    EXPECT_NE(takeErrorMessage(std::move(LateRegistration)).find("freeze"),
              std::string::npos);
  }

  {
    ActiveScopes Scopes;
    ASSERT_NE(Scopes.Task, nullptr);
    PluginArtifactSlot Slot(Type);
    PluginPhaseExecutor Executor(Graph, Artifacts);
    int PluginPayload = 51;
    int BuiltinPayload = 52;
    int PluginCalls = 0;
    int BuiltinCalls = 0;
    ProviderData Plugin{&Executor, Scopes.Task.get(), nullptr, &PluginPayload,
                        &PluginCalls};
    NevercProviderDescriptor Descriptor =
        providerDescriptor("route-specific", Plugin);
    Descriptor.Route.TargetTriple = view("x86_64-other-none");
    ASSERT_FALSE(Executor.addProvider(TestPluginID, Descriptor));
    ASSERT_FALSE(Executor.setBuiltinProvider(
        TestPhaseID, [&](const NevercPhaseFrame *, NevercPhaseResult *Result) {
          ++BuiltinCalls;
          auto Candidate = Executor.createCandidate(
              *Scopes.Task, TestArtifactID, &BuiltinPayload);
          if (!Candidate) {
            consumeError(Candidate.takeError());
            return failedStatus();
          }
          Result->Action = NEVERC_PHASE_REPLACE;
          Result->Output = *Candidate;
          return neverc_status_ok();
        }));
    int InputPayload = 2;
    auto Input = Executor.createArtifactView(*Scopes.Task, TestArtifactID,
                                             &InputPayload, 1);
    ASSERT_TRUE(static_cast<bool>(Input));

    EXPECT_FALSE(Executor.execute(*Scopes.Session, *Scopes.Task, TestPhaseID,
                                  route("aarch64-test-none", 2), *Input, Slot));
    EXPECT_EQ(PluginCalls, 0);
    EXPECT_EQ(BuiltinCalls, 1);
    EXPECT_EQ(Slot.payload(), &BuiltinPayload);
  }
}

TEST(PluginPhaseExecutionTest,
     RecoverableFallbackRequiresExplicitSafeDiscardableRoute) {
  PluginPhaseGraph Graph = makeGraph();
  PluginArtifactRegistry Artifacts;
  int Destroyed = 0;
  auto Type = registerArtifact(Artifacts, Destroyed);
  ASSERT_NE(Type, nullptr);

  {
    ActiveScopes Scopes;
    ASSERT_NE(Scopes.Task, nullptr);
    PluginArtifactSlot Slot(Type);
    PluginPhaseExecutor Executor(Graph, Artifacts);
    int FailedCandidate = 81;
    int BuiltinPayload = 82;
    int PluginCalls = 0;
    int BuiltinCalls = 0;
    RecoverableProviderData Data{&Executor, Scopes.Task.get(), &FailedCandidate,
                                 &PluginCalls};
    Data.Core = &Scopes.Services.coreAPI();
    Data.DiagnosticMessage = "discarded fallback diagnostic";
    ASSERT_FALSE(Executor.addProvider(
        TestPluginID, recoverableProviderDescriptor(Data, NEVERC_TRUE)));
    ASSERT_FALSE(Executor.setBuiltinProvider(
        TestPhaseID, [&](const NevercPhaseFrame *, NevercPhaseResult *Result) {
          ++BuiltinCalls;
          auto Candidate = Executor.createCandidate(
              *Scopes.Task, TestArtifactID, &BuiltinPayload);
          if (!Candidate) {
            consumeError(Candidate.takeError());
            return failedStatus();
          }
          Result->Action = NEVERC_PHASE_REPLACE;
          Result->Output = *Candidate;
          return neverc_status_ok();
        }));
    ASSERT_FALSE(Executor.enableRecoverableBuiltinFallback(TestPhaseID));
    int InputPayload = 1;
    auto Input = Executor.createArtifactView(*Scopes.Task, TestArtifactID,
                                             &InputPayload, 1);
    ASSERT_TRUE(static_cast<bool>(Input));
    int DestroyedBefore = Destroyed;

    EXPECT_FALSE(Executor.execute(*Scopes.Session, *Scopes.Task, TestPhaseID,
                                  route(), *Input, Slot));
    EXPECT_EQ(PluginCalls, 1);
    EXPECT_EQ(BuiltinCalls, 1);
    EXPECT_EQ(Slot.payload(), &BuiltinPayload);
    EXPECT_EQ(Destroyed, DestroyedBefore + 1);
    EXPECT_FALSE(Scopes.Session->isCancelled());
    EXPECT_TRUE(Scopes.Session->diagnostics().takeSorted().empty());
    auto Provenance = Executor.fallbackProvenance();
    ASSERT_EQ(Provenance.size(), 1U);
    EXPECT_NE(Provenance.front().find(TestPluginID), std::string::npos);
  }

  {
    ActiveScopes Scopes;
    ASSERT_NE(Scopes.Task, nullptr);
    PluginArtifactSlot Slot(Type);
    PluginPhaseExecutor Executor(Graph, Artifacts);
    int BuiltinPayload = 83;
    int PluginCalls = 0;
    int BuiltinCalls = 0;
    RecoverableProviderData Data{&Executor, Scopes.Task.get(), nullptr,
                                 &PluginCalls};
    Data.Core = &Scopes.Services.coreAPI();
    Data.DiagnosticMessage = "retained terminal diagnostic";
    ASSERT_FALSE(Executor.addProvider(
        TestPluginID, recoverableProviderDescriptor(Data, NEVERC_TRUE)));
    ASSERT_FALSE(Executor.setBuiltinProvider(
        TestPhaseID, [&](const NevercPhaseFrame *, NevercPhaseResult *Result) {
          ++BuiltinCalls;
          auto Candidate = Executor.createCandidate(
              *Scopes.Task, TestArtifactID, &BuiltinPayload);
          if (!Candidate) {
            consumeError(Candidate.takeError());
            return failedStatus();
          }
          Result->Action = NEVERC_PHASE_REPLACE;
          Result->Output = *Candidate;
          return neverc_status_ok();
        }));
    int InputPayload = 2;
    auto Input = Executor.createArtifactView(*Scopes.Task, TestArtifactID,
                                             &InputPayload, 1);
    ASSERT_TRUE(static_cast<bool>(Input));

    Error NoOptIn = Executor.execute(*Scopes.Session, *Scopes.Task, TestPhaseID,
                                     route(), *Input, Slot);
    ASSERT_TRUE(static_cast<bool>(NoOptIn));
    consumeError(std::move(NoOptIn));
    EXPECT_EQ(PluginCalls, 1);
    EXPECT_EQ(BuiltinCalls, 0);
    EXPECT_EQ(Slot.payload(), nullptr);
    EXPECT_TRUE(Scopes.Session->isCancelled());
    auto Diagnostics = Scopes.Session->diagnostics().takeSorted();
    ASSERT_EQ(Diagnostics.size(), 1U);
    EXPECT_EQ(Diagnostics[0].Message, "retained terminal diagnostic");
    EXPECT_TRUE(Executor.fallbackProvenance().empty());
  }

  {
    ActiveScopes Scopes;
    ASSERT_NE(Scopes.Task, nullptr);
    PluginArtifactSlot Slot(Type);
    PluginPhaseExecutor Executor(Graph, Artifacts);
    int BuiltinCalls = 0;
    RecoverableProviderData Data;
    Data.Executor = &Executor;
    Data.Task = Scopes.Task.get();
    Data.Flags = NEVERC_STATUS_FLAG_RECOVERABLE |
                 NEVERC_STATUS_FLAG_OUTPUT_ALREADY_COMMITTED;
    ASSERT_FALSE(Executor.addProvider(
        TestPluginID, recoverableProviderDescriptor(Data, NEVERC_TRUE)));
    ASSERT_FALSE(Executor.setBuiltinProvider(
        TestPhaseID, [&](const NevercPhaseFrame *, NevercPhaseResult *) {
          ++BuiltinCalls;
          return failedStatus();
        }));
    ASSERT_FALSE(Executor.enableRecoverableBuiltinFallback(TestPhaseID));
    int InputPayload = 3;
    auto Input = Executor.createArtifactView(*Scopes.Task, TestArtifactID,
                                             &InputPayload, 1);
    ASSERT_TRUE(static_cast<bool>(Input));

    Error CommittedEffect = Executor.execute(
        *Scopes.Session, *Scopes.Task, TestPhaseID, route(), *Input, Slot);
    ASSERT_TRUE(static_cast<bool>(CommittedEffect));
    consumeError(std::move(CommittedEffect));
    EXPECT_EQ(BuiltinCalls, 0);
    EXPECT_TRUE(Scopes.Session->isCancelled());
    EXPECT_TRUE(Executor.fallbackProvenance().empty());
  }
}

TEST(PluginPhaseExecutionTest,
     RejectsRecursivePhaseInvocationAndCancelsSession) {
  ActiveScopes Scopes;
  ASSERT_NE(Scopes.Task, nullptr);
  PluginPhaseGraph Graph = makeGraph();
  PluginArtifactRegistry Artifacts;
  int Destroyed = 0;
  auto Type = registerArtifact(Artifacts, Destroyed);
  ASSERT_NE(Type, nullptr);
  PluginArtifactSlot Slot(Type);
  PluginPhaseExecutor Executor(Graph, Artifacts);
  int InputPayload = 1;
  auto Input = Executor.createArtifactView(*Scopes.Task, TestArtifactID,
                                           &InputPayload, 1);
  ASSERT_TRUE(static_cast<bool>(Input));
  std::string RecursiveFailure;
  ASSERT_FALSE(Executor.setBuiltinProvider(
      TestPhaseID, [&](const NevercPhaseFrame *, NevercPhaseResult *) {
        Error Recursive = Executor.execute(*Scopes.Session, *Scopes.Task,
                                           TestPhaseID, route(), *Input, Slot);
        if (Recursive)
          RecursiveFailure = takeErrorMessage(std::move(Recursive));
        return failedStatus();
      }));

  Error Failed = Executor.execute(*Scopes.Session, *Scopes.Task, TestPhaseID,
                                  route(), *Input, Slot);
  ASSERT_TRUE(static_cast<bool>(Failed));
  consumeError(std::move(Failed));
  EXPECT_NE(RecursiveFailure.find("recursively"), std::string::npos);
  EXPECT_EQ(Slot.payload(), nullptr);
  EXPECT_TRUE(Scopes.Session->isCancelled());
}

TEST(PluginPhaseExecutionTest, SealedCommitPublishesBeforeAfterCommitFailure) {
  ActiveScopes Scopes;
  ASSERT_NE(Scopes.Task, nullptr);
  PluginPhaseGraph Graph;
  PluginPhaseDefinition Phase;
  Phase.ID = TestPhaseID;
  Phase.CanonicalName = "test.phase.sealed-commit";
  Phase.Domain = "test";
  Phase.Verifier = "test.verify";
  Phase.InputArtifact = TestArtifactID;
  Phase.OutputArtifact = TestArtifactID;
  Phase.Policy = NEVERC_PHASE_OBSERVABLE | NEVERC_PHASE_SEALED_HOST_GATE;
  Phase.ObserverPoints = NEVERC_OBSERVER_BEFORE | NEVERC_OBSERVER_AFTER |
                         NEVERC_OBSERVER_AFTER_COMMIT;
  Phase.Gate = PluginPhaseGateKind::SealedCommit;
  Phase.HasBuiltinFallback = true;
  ASSERT_FALSE(Graph.addPhase(std::move(Phase)));
  ASSERT_FALSE(Graph.finalize());
  PluginArtifactRegistry Artifacts;
  int Destroyed = 0;
  auto Type = registerArtifact(Artifacts, Destroyed);
  ASSERT_NE(Type, nullptr);
  PluginArtifactSlot Slot(Type);
  PluginPhaseExecutor Executor(Graph, Artifacts);
  std::vector<std::string> Trace;
  ObserverData LateObserver;
  LateObserver.Trace = &Trace;
  LateObserver.Name = "late";
  LateObserver.FailPoint = NEVERC_OBSERVER_AFTER_COMMIT;
  NevercObserverDescriptor Descriptor = observerDescriptor(LateObserver);
  Descriptor.Points = NEVERC_OBSERVER_AFTER_COMMIT;
  ASSERT_FALSE(Executor.addObserver(TestPluginID, Descriptor));
  int Published = 71;
  ASSERT_FALSE(Executor.setBuiltinProvider(
      TestPhaseID, [&](const NevercPhaseFrame *, NevercPhaseResult *Result) {
        auto Candidate =
            Executor.createCandidate(*Scopes.Task, TestArtifactID, &Published);
        if (!Candidate) {
          consumeError(Candidate.takeError());
          return failedStatus();
        }
        Result->Action = NEVERC_PHASE_REPLACE;
        Result->Output = *Candidate;
        return neverc_status_ok();
      }));
  int InputPayload = 1;
  auto Input = Executor.createArtifactView(*Scopes.Task, TestArtifactID,
                                           &InputPayload, 1);
  ASSERT_TRUE(static_cast<bool>(Input));

  Error LateFailure = Executor.execute(*Scopes.Session, *Scopes.Task,
                                       TestPhaseID, route(), *Input, Slot);
  ASSERT_TRUE(static_cast<bool>(LateFailure));
  EXPECT_NE(takeErrorMessage(std::move(LateFailure)).find("Observer"),
            std::string::npos);
  EXPECT_EQ(Slot.payload(), &Published);
  EXPECT_EQ(Slot.generation(), 1U);
  EXPECT_EQ(Trace, (std::vector<std::string>{"late:after"}));
  EXPECT_TRUE(Scopes.Session->isCancelled());
}

TEST(PluginPhaseExecutionTest, SealedGateRejectsInterceptorRegistration) {
  PluginPhaseGraph Graph;
  PluginPhaseDefinition Phase;
  Phase.ID = TestPhaseID;
  Phase.CanonicalName = "test.phase.sealed";
  Phase.Domain = "test";
  Phase.Verifier = "test.verify";
  Phase.InputArtifact = TestArtifactID;
  Phase.OutputArtifact = TestArtifactID;
  Phase.Policy = NEVERC_PHASE_OBSERVABLE | NEVERC_PHASE_SEALED_HOST_GATE;
  Phase.ObserverPoints = NEVERC_OBSERVER_BEFORE | NEVERC_OBSERVER_AFTER;
  Phase.Gate = PluginPhaseGateKind::SealedVerifier;
  Phase.HasBuiltinFallback = true;
  ASSERT_FALSE(Graph.addPhase(std::move(Phase)));
  ASSERT_FALSE(Graph.finalize());
  PluginArtifactRegistry Artifacts;
  int Destroyed = 0;
  ASSERT_NE(registerArtifact(Artifacts, Destroyed), nullptr);
  PluginPhaseExecutor Executor(Graph, Artifacts);
  InterceptorData Data;
  Error Rejected =
      Executor.addInterceptor(TestPluginID, interceptorDescriptor(Data));
  ASSERT_TRUE(static_cast<bool>(Rejected));
  EXPECT_NE(takeErrorMessage(std::move(Rejected)).find("sealed"),
            std::string::npos);
}

} // namespace
