// Volume 6 task 5: dyncode phase registry, executor, proof and C callback
// bridge.  These tests drive the 34-phase dyncode chain over the generic
// PluginPhaseExecutor with a loaded test plugin, and check the full trace, a
// single-phase interceptor, sealed-gate Provider/Interceptor rejection, illegal
// SKIP rejection, and error/cancellation behaviour.

#include "DynCodePhaseCAPI.h"
#include "DynCodePhaseExecutor.h"
#include "DynCodePhaseRegistry.h"

#include "neverc/Plugin/Host/PluginIOBridge.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "neverc/Plugin/Schema/PluginPhaseSchema.inc"

#include "llvm/ADT/StringRef.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Error.h"
#include "gtest/gtest.h"

#include <array>
#include <cstring>
#include <memory>
#include <optional>
#include <string>

using namespace llvm;
using namespace neverc;
using namespace neverc::dyncode;
using neverc::plugin::PluginProcessServices;
using neverc::plugin::PluginSession;
using neverc::plugin::PluginTaskContext;
using neverc::plugin::samePluginInterfaceID;

namespace {

constexpr const char *DynCodeTestPluginID = "org.neverc.test.dyncode-phase";

std::string errorText(Error Value) {
  return toString(std::move(Value)).str().str();
}

NevercInterfaceID phaseID(uint64_t High, uint64_t Low) { return {High, Low}; }

NevercInterfaceID requestFreezeInput() {
  return phaseID(NEVERC_PHASE_DYNCODE_REQUEST_FREEZE_INPUT_HIGH,
                 NEVERC_PHASE_DYNCODE_REQUEST_FREEZE_INPUT_LOW);
}
NevercInterfaceID irPreparePhase() {
  return phaseID(NEVERC_PHASE_DYNCODE_IR_PREPARE_HIGH,
                 NEVERC_PHASE_DYNCODE_IR_PREPARE_LOW);
}
NevercInterfaceID verifyPhase() {
  return phaseID(NEVERC_PHASE_DYNCODE_VERIFY_HIGH,
                 NEVERC_PHASE_DYNCODE_VERIFY_LOW);
}

std::shared_ptr<DynCodePipelineValue> makeInitialValue() {
  return std::make_shared<DynCodePipelineValue>(requestFreezeInput(), 1);
}

struct DynCodeTrace {
  const NevercDynCodePhaseAPI *PhaseAPI = nullptr;
  char Events[256] = {};
  uint32_t EventCount = 0;
  uint64_t LastGeneration = 0;
  uint32_t Interceptions = 0;
};

void record(DynCodeTrace *Trace, char Event) {
  if (Trace && Trace->EventCount < sizeof(Trace->Events))
    Trace->Events[Trace->EventCount++] = Event;
}

NevercStatus statusCode(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

NevercStatus NEVERC_CALL traceObserver(const NevercPhaseFrame *Frame,
                                       NevercObserverPoint Point,
                                       void *UserData) {
  auto *Trace = static_cast<DynCodeTrace *>(UserData);
  if (!Frame || !Trace)
    return statusCode(NEVERC_STATUS_INVALID_ARGUMENT);
  record(Trace, Point == NEVERC_OBSERVER_BEFORE ? 'B' : 'A');
  if (Trace->PhaseAPI) {
    NevercDynCodePhaseInfo Info{};
    Info.Header = {sizeof(Info), NEVERC_DYNCODE_PHASE_API_MAJOR,
                   NEVERC_DYNCODE_PHASE_API_MINOR, 0};
    if (neverc_status_is_ok(Trace->PhaseAPI->GetPhaseInfo(
            Trace->PhaseAPI->Context, Frame, &Info)))
      Trace->LastGeneration = Info.Generation;
  }
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL traceInterceptor(const NevercPhaseFrame *Frame,
                                          NevercPhaseContinuation *Continuation,
                                          NevercPhaseResult *OutResult,
                                          void *UserData) {
  auto *Trace = static_cast<DynCodeTrace *>(UserData);
  NevercPhaseResult Downstream;
  NevercStatus Status;
  if (!Frame || !Continuation || !OutResult || !Trace)
    return statusCode(NEVERC_STATUS_INVALID_ARGUMENT);
  record(Trace, 'I');
  std::memset(&Downstream, 0, sizeof(Downstream));
  Downstream.Header = {sizeof(Downstream), NEVERC_PLUGIN_ABI_MAJOR,
                       NEVERC_PLUGIN_ABI_MINOR, 0};
  Status = Continuation->InvokeNext(Continuation, Frame, &Downstream);
  if (!neverc_status_is_ok(Status))
    return Status;
  record(Trace, 'N');
  ++Trace->Interceptions;
  std::memset(OutResult, 0, sizeof(*OutResult));
  OutResult->Header = {sizeof(*OutResult), NEVERC_PLUGIN_ABI_MAJOR,
                       NEVERC_PLUGIN_ABI_MINOR, 0};
  OutResult->Action = NEVERC_PHASE_CONTINUE;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL skipInterceptor(const NevercPhaseFrame *Frame,
                                         NevercPhaseContinuation *Continuation,
                                         NevercPhaseResult *OutResult,
                                         void *UserData) {
  (void)Continuation;
  auto *Trace = static_cast<DynCodeTrace *>(UserData);
  if (!Frame || !OutResult)
    return statusCode(NEVERC_STATUS_INVALID_ARGUMENT);
  record(Trace, 'S');
  std::memset(OutResult, 0, sizeof(*OutResult));
  OutResult->Header = {sizeof(*OutResult), NEVERC_PLUGIN_ABI_MAJOR,
                       NEVERC_PLUGIN_ABI_MINOR, 0};
  // SKIP is illegal on the 30 ordinary transitions; the executor must reject it.
  OutResult->Action = NEVERC_PHASE_SKIP;
  return neverc_status_ok();
}

NevercObserverDescriptor makeObserver(NevercInterfaceID Phase,
                                      DynCodeTrace &Trace) {
  NevercObserverDescriptor Observer{};
  Observer.Header = {sizeof(Observer), NEVERC_PLUGIN_ABI_MAJOR,
                     NEVERC_PLUGIN_ABI_MINOR, 0};
  Observer.Phase = Phase;
  Observer.Points = NEVERC_OBSERVER_BEFORE | NEVERC_OBSERVER_AFTER;
  Observer.Callback = traceObserver;
  Observer.UserData = &Trace;
  return Observer;
}

NevercStatus NEVERC_CALL noopProvider(const NevercPhaseFrame *Frame,
                                      NevercPhaseResult *OutResult,
                                      void *UserData) {
  (void)Frame;
  (void)UserData;
  if (!OutResult)
    return statusCode(NEVERC_STATUS_INVALID_ARGUMENT);
  std::memset(OutResult, 0, sizeof(*OutResult));
  OutResult->Header = {sizeof(*OutResult), NEVERC_PLUGIN_ABI_MAJOR,
                       NEVERC_PLUGIN_ABI_MINOR, 0};
  OutResult->Action = NEVERC_PHASE_CONTINUE;
  return neverc_status_ok();
}

NevercInterceptorDescriptor makeInterceptor(NevercInterfaceID Phase,
                                            DynCodeTrace &Trace,
                                            NevercPhaseInterceptorFn Callback) {
  NevercInterceptorDescriptor Interceptor{};
  Interceptor.Header = {sizeof(Interceptor), NEVERC_PLUGIN_ABI_MAJOR,
                        NEVERC_PLUGIN_ABI_MINOR, 0};
  Interceptor.Phase = Phase;
  Interceptor.Callback = Callback;
  Interceptor.UserData = &Trace;
  return Interceptor;
}

NevercProviderDescriptor makeProvider(NevercInterfaceID Phase) {
  NevercProviderDescriptor Provider{};
  Provider.Header = {sizeof(Provider), NEVERC_PLUGIN_ABI_MAJOR,
                     NEVERC_PLUGIN_ABI_MINOR, 0};
  Provider.Phase = Phase;
  Provider.ProviderID = {"dyncode-forbidden", 17};
  Provider.Route.Header = {sizeof(Provider.Route), NEVERC_PLUGIN_ABI_MAJOR,
                           NEVERC_PLUGIN_ABI_MINOR, 0};
  Provider.Callback = noopProvider;
  return Provider;
}

class DynCodeTaskScope {
public:
  DynCodeTaskScope()
      : Services("neverc-plugin-dyncode-phase-tests", LLVM_VERSION_MAJOR) {}

  bool initialize() {
    if (Error E = plugin::registerPluginIOInterface(Services)) {
      ADD_FAILURE() << errorText(std::move(E));
      return false;
    }
    if (Error E = registerPluginDynCodePhaseInterface(Services)) {
      ADD_FAILURE() << errorText(std::move(E));
      return false;
    }
    if (Error E = Services.interfaces().freeze()) {
      ADD_FAILURE() << errorText(std::move(E));
      return false;
    }
    auto Loaded = Services.registry().load(NEVERC_TEST_DYNCODE_PHASE_PLUGIN);
    if (!Loaded) {
      ADD_FAILURE() << errorText(Loaded.takeError());
      return false;
    }
    auto Query = Services.interfaces().query(
        {NEVERC_INTERFACE_DYNCODE_PHASE_HIGH,
         NEVERC_INTERFACE_DYNCODE_PHASE_LOW},
        NEVERC_DYNCODE_PHASE_API_MAJOR, NEVERC_DYNCODE_PHASE_API_MINOR);
    if (!Query) {
      ADD_FAILURE() << errorText(Query.takeError());
      return false;
    }
    PhaseAPI = static_cast<const NevercDynCodePhaseAPI *>(Query->Table);
    const std::array<StringRef, 1> Selected = {DynCodeTestPluginID};
    auto CreatedPlan =
        plugin::makePluginActivationPlan(Services.registry(), Selected);
    if (!CreatedPlan) {
      ADD_FAILURE() << errorText(CreatedPlan.takeError());
      return false;
    }
    Plan.emplace(std::move(*CreatedPlan));
    auto CreatedSession = PluginSession::create(Services, *Plan);
    if (!CreatedSession) {
      ADD_FAILURE() << errorText(CreatedSession.takeError());
      return false;
    }
    Session = std::move(*CreatedSession);
    auto CreatedTask = Session->createTask(NEVERC_TASK_DYNCODE);
    if (!CreatedTask) {
      ADD_FAILURE() << errorText(CreatedTask.takeError());
      return false;
    }
    Task = std::move(*CreatedTask);
    return true;
  }

  ~DynCodeTaskScope() {
    if (Task)
      EXPECT_FALSE(Task->end());
    if (Session)
      EXPECT_FALSE(Session->end());
    Plan.reset();
    EXPECT_FALSE(Services.shutdown());
  }

  PluginTaskContext &task() { return *Task; }
  PluginSession &session() { return *Session; }
  const NevercDynCodePhaseAPI &phaseAPI() const { return *PhaseAPI; }

private:
  PluginProcessServices Services;
  std::optional<plugin::PluginActivationPlan> Plan;
  std::unique_ptr<PluginSession> Session;
  std::unique_ptr<PluginTaskContext> Task;
  const NevercDynCodePhaseAPI *PhaseAPI = nullptr;
};

TEST(PluginDynCodePhaseTest, RegistryMatchesGeneratedStableSchema) {
  auto Registry = DynCodePhaseRegistry::create();
  ASSERT_TRUE(static_cast<bool>(Registry)) << errorText(Registry.takeError());
  EXPECT_EQ(Registry->graph().size(), NEVERC_BUILTIN_DYNCODE_PHASE_COUNT);
  EXPECT_EQ(Registry->phases().size(), 34U);
  unsigned Sealed = 0;
  for (const DynCodePhaseDefinition &Phase : Registry->phases())
    if (Phase.isSealedGate())
      ++Sealed;
  EXPECT_EQ(Sealed, 4U);
}

TEST(PluginDynCodePhaseTest, BuiltinPipelineRunsFullChain) {
  DynCodeTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Pipeline = DynCodePhasePipeline::create(Scope.task());
  ASSERT_TRUE(static_cast<bool>(Pipeline)) << errorText(Pipeline.takeError());
  auto Output = (*Pipeline)->execute(makeInitialValue());
  ASSERT_TRUE(static_cast<bool>(Output)) << errorText(Output.takeError());
  NevercInterfaceID Last =
      (*Pipeline)->registry().phases().back().OutputArtifact;
  EXPECT_TRUE(samePluginInterfaceID((*Output)->type(), Last));
  // Every one of the 34 phases republished the value once, so the generation
  // advanced from 1 to 35.
  EXPECT_EQ((*Output)->generation(), 35U);
}

TEST(PluginDynCodePhaseTest, ObserverTracesTransition) {
  DynCodeTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Pipeline = DynCodePhasePipeline::create(Scope.task());
  ASSERT_TRUE(static_cast<bool>(Pipeline)) << errorText(Pipeline.takeError());
  DynCodeTrace Trace{};
  Trace.PhaseAPI = &Scope.phaseAPI();
  NevercObserverDescriptor Observer = makeObserver(irPreparePhase(), Trace);
  ASSERT_FALSE((*Pipeline)->addObserver(DynCodeTestPluginID, Observer));
  auto Output = (*Pipeline)->execute(makeInitialValue(), irPreparePhase());
  ASSERT_TRUE(static_cast<bool>(Output)) << errorText(Output.takeError());
  EXPECT_EQ(std::string(Trace.Events, Trace.Events + Trace.EventCount), "BA");
  EXPECT_NE(Trace.LastGeneration, 0U);
}

TEST(PluginDynCodePhaseTest, InterceptorRunsAndContinues) {
  DynCodeTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Pipeline = DynCodePhasePipeline::create(Scope.task());
  ASSERT_TRUE(static_cast<bool>(Pipeline)) << errorText(Pipeline.takeError());
  DynCodeTrace Trace{};
  Trace.PhaseAPI = &Scope.phaseAPI();
  NevercInterceptorDescriptor Interceptor =
      makeInterceptor(irPreparePhase(), Trace, traceInterceptor);
  ASSERT_FALSE((*Pipeline)->addInterceptor(DynCodeTestPluginID, Interceptor));
  auto Output = (*Pipeline)->execute(makeInitialValue(), irPreparePhase());
  ASSERT_TRUE(static_cast<bool>(Output)) << errorText(Output.takeError());
  EXPECT_EQ(std::string(Trace.Events, Trace.Events + Trace.EventCount), "IN");
  EXPECT_EQ(Trace.Interceptions, 1U);
}

TEST(PluginDynCodePhaseTest, SealedGateRejectsProviderAndInterceptor) {
  DynCodeTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Pipeline = DynCodePhasePipeline::create(Scope.task());
  ASSERT_TRUE(static_cast<bool>(Pipeline)) << errorText(Pipeline.takeError());
  DynCodeTrace Trace{};

  NevercProviderDescriptor Provider = makeProvider(verifyPhase());
  Error ProviderRejected = (*Pipeline)->addProvider(DynCodeTestPluginID,
                                                    Provider);
  EXPECT_TRUE(static_cast<bool>(ProviderRejected));
  consumeError(std::move(ProviderRejected));

  NevercInterceptorDescriptor Interceptor =
      makeInterceptor(verifyPhase(), Trace, traceInterceptor);
  Error InterceptorRejected =
      (*Pipeline)->addInterceptor(DynCodeTestPluginID, Interceptor);
  EXPECT_TRUE(static_cast<bool>(InterceptorRejected));
  consumeError(std::move(InterceptorRejected));

  // An observer on a sealed gate is allowed (the gate is OBSERVABLE).
  NevercObserverDescriptor Observer = makeObserver(verifyPhase(), Trace);
  EXPECT_FALSE((*Pipeline)->addObserver(DynCodeTestPluginID, Observer));
}

TEST(PluginDynCodePhaseTest, IllegalSkipOnTransitionIsRejected) {
  DynCodeTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Pipeline = DynCodePhasePipeline::create(Scope.task());
  ASSERT_TRUE(static_cast<bool>(Pipeline)) << errorText(Pipeline.takeError());
  DynCodeTrace Trace{};
  NevercInterceptorDescriptor Interceptor =
      makeInterceptor(irPreparePhase(), Trace, skipInterceptor);
  ASSERT_FALSE((*Pipeline)->addInterceptor(DynCodeTestPluginID, Interceptor));
  auto Output = (*Pipeline)->execute(makeInitialValue(), irPreparePhase());
  EXPECT_FALSE(static_cast<bool>(Output));
  if (!Output)
    consumeError(Output.takeError());
}

TEST(PluginDynCodePhaseTest, ErrorAndCancellationDoNotComplete) {
  DynCodeTaskScope ErrorScope;
  ASSERT_TRUE(ErrorScope.initialize());
  auto ErrorPipeline = DynCodePhasePipeline::create(ErrorScope.task());
  ASSERT_TRUE(static_cast<bool>(ErrorPipeline))
      << errorText(ErrorPipeline.takeError());
  ASSERT_FALSE((*ErrorPipeline)
                   ->setBuiltinProvider(
                       irPreparePhase(),
                       [](const NevercPhaseFrame *, NevercPhaseResult *) {
                         return statusCode(NEVERC_STATUS_PLUGIN_FAILURE);
                       }));
  auto Failed =
      (*ErrorPipeline)->execute(makeInitialValue(), irPreparePhase());
  EXPECT_FALSE(static_cast<bool>(Failed));
  if (!Failed)
    consumeError(Failed.takeError());

  DynCodeTaskScope CancelledScope;
  ASSERT_TRUE(CancelledScope.initialize());
  auto CancelledPipeline =
      DynCodePhasePipeline::create(CancelledScope.task());
  ASSERT_TRUE(static_cast<bool>(CancelledPipeline))
      << errorText(CancelledPipeline.takeError());
  CancelledScope.session().cancel();
  auto Cancelled = (*CancelledPipeline)->execute(makeInitialValue());
  EXPECT_FALSE(static_cast<bool>(Cancelled));
  if (!Cancelled)
    consumeError(Cancelled.takeError());
}

} // namespace
