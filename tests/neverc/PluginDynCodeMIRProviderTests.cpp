// The dyncode MIR stage is split into a replaceable transform
// (neverc.dyncode.mir.prepare, PreEmit hook) and a sealed final verifier
// (neverc.dyncode.mir.final_verify, Final hook before AsmPrinter).  These tests
// check the two passes are distinct and constructed from immutable options, the
// phase registry classifies mir.prepare as an ordinary (non-sealed) transition
// and mir.final_verify as a sealed gate, and -- driving the generic phase
// executor with a loaded test plugin -- that the transition accepts an
// interceptor while the sealed gate rejects Provider/Interceptor but still
// allows a read-only observer.

#include "neverc/DynCode/MIR/MIRPrepPass.h"

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
#include "llvm/Pass.h"
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

namespace {

constexpr const char *DynCodeTestPluginID = "org.neverc.test.dyncode-phase";

std::string errorText(Error Value) {
  return toString(std::move(Value)).str().str();
}

NevercInterfaceID mirPreparePhase() {
  return {NEVERC_PHASE_DYNCODE_MIR_PREPARE_HIGH,
          NEVERC_PHASE_DYNCODE_MIR_PREPARE_LOW};
}
NevercInterfaceID mirFinalVerifyPhase() {
  return {NEVERC_PHASE_DYNCODE_MIR_FINAL_VERIFY_HIGH,
          NEVERC_PHASE_DYNCODE_MIR_FINAL_VERIFY_LOW};
}

NevercStatus statusCode(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

NevercStatus NEVERC_CALL traceObserver(const NevercPhaseFrame *Frame,
                                       NevercObserverPoint Point,
                                       void *UserData) {
  (void)Point;
  (void)UserData;
  if (!Frame)
    return statusCode(NEVERC_STATUS_INVALID_ARGUMENT);
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL passthroughInterceptor(
    const NevercPhaseFrame *Frame, NevercPhaseContinuation *Continuation,
    NevercPhaseResult *OutResult, void *UserData) {
  (void)UserData;
  if (!Frame || !Continuation || !OutResult)
    return statusCode(NEVERC_STATUS_INVALID_ARGUMENT);
  NevercPhaseResult Downstream;
  std::memset(&Downstream, 0, sizeof(Downstream));
  Downstream.Header = {sizeof(Downstream), NEVERC_PLUGIN_ABI_MAJOR,
                       NEVERC_PLUGIN_ABI_MINOR, 0};
  NevercStatus Status =
      Continuation->InvokeNext(Continuation, Frame, &Downstream);
  if (!neverc_status_is_ok(Status))
    return Status;
  std::memset(OutResult, 0, sizeof(*OutResult));
  OutResult->Header = {sizeof(*OutResult), NEVERC_PLUGIN_ABI_MAJOR,
                       NEVERC_PLUGIN_ABI_MINOR, 0};
  OutResult->Action = NEVERC_PHASE_CONTINUE;
  return neverc_status_ok();
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

NevercObserverDescriptor makeObserver(NevercInterfaceID Phase) {
  NevercObserverDescriptor Observer{};
  Observer.Header = {sizeof(Observer), NEVERC_PLUGIN_ABI_MAJOR,
                     NEVERC_PLUGIN_ABI_MINOR, 0};
  Observer.Phase = Phase;
  Observer.Points = NEVERC_OBSERVER_BEFORE | NEVERC_OBSERVER_AFTER;
  Observer.Callback = traceObserver;
  Observer.UserData = nullptr;
  return Observer;
}

NevercInterceptorDescriptor makeInterceptor(NevercInterfaceID Phase) {
  NevercInterceptorDescriptor Interceptor{};
  Interceptor.Header = {sizeof(Interceptor), NEVERC_PLUGIN_ABI_MAJOR,
                        NEVERC_PLUGIN_ABI_MINOR, 0};
  Interceptor.Phase = Phase;
  Interceptor.Callback = passthroughInterceptor;
  Interceptor.UserData = nullptr;
  return Interceptor;
}

NevercProviderDescriptor makeProvider(NevercInterfaceID Phase) {
  NevercProviderDescriptor Provider{};
  Provider.Header = {sizeof(Provider), NEVERC_PLUGIN_ABI_MAJOR,
                     NEVERC_PLUGIN_ABI_MINOR, 0};
  Provider.Phase = Phase;
  Provider.ProviderID = {"dyncode-mir-replace", 19};
  Provider.Route.Header = {sizeof(Provider.Route), NEVERC_PLUGIN_ABI_MAJOR,
                           NEVERC_PLUGIN_ABI_MINOR, 0};
  Provider.Callback = noopProvider;
  return Provider;
}

class DynCodeTaskScope {
public:
  DynCodeTaskScope()
      : Services("neverc-plugin-dyncode-mir-tests", LLVM_VERSION_MAJOR) {}

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

private:
  PluginProcessServices Services;
  std::optional<plugin::PluginActivationPlan> Plan;
  std::unique_ptr<PluginSession> Session;
  std::unique_ptr<PluginTaskContext> Task;
};

TEST(PluginDynCodeMIRProviderTest, TransformAndVerifierAreDistinctPasses) {
  DynCodeOptions Opts;
  Opts.Enabled = true;
  std::unique_ptr<FunctionPass> Transform(createDynCodeMIRTransformPass(Opts));
  std::unique_ptr<FunctionPass> Verifier(createDynCodeMIRVerifierPass(Opts));
  ASSERT_NE(Transform, nullptr);
  ASSERT_NE(Verifier, nullptr);
  EXPECT_EQ(Transform->getPassName(), "NeverC DynCode MIR Transform");
  EXPECT_EQ(Verifier->getPassName(), "NeverC DynCode MIR Final Verify");
  EXPECT_NE(Transform->getPassName(), Verifier->getPassName());
}

TEST(PluginDynCodeMIRProviderTest, MIRPhasePoliciesMatchSchema) {
  auto Registry = DynCodePhaseRegistry::create();
  ASSERT_TRUE(static_cast<bool>(Registry)) << errorText(Registry.takeError());

  const DynCodePhaseDefinition *Prepare = Registry->find(mirPreparePhase());
  ASSERT_NE(Prepare, nullptr);
  EXPECT_FALSE(Prepare->isSealedGate());

  const DynCodePhaseDefinition *Final = Registry->find(mirFinalVerifyPhase());
  ASSERT_NE(Final, nullptr);
  EXPECT_TRUE(Final->isSealedGate());

  // The transform and its sealed verifier are two distinct phases.
  EXPECT_FALSE(Prepare->Phase.High == Final->Phase.High &&
               Prepare->Phase.Low == Final->Phase.Low);
}

TEST(PluginDynCodeMIRProviderTest, PrepareTransitionAcceptsInterceptor) {
  DynCodeTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Pipeline = DynCodePhasePipeline::create(Scope.task());
  ASSERT_TRUE(static_cast<bool>(Pipeline)) << errorText(Pipeline.takeError());
  // mir.prepare is OBSERVABLE | INTERCEPTABLE | REPLACEABLE.
  NevercInterceptorDescriptor Interceptor = makeInterceptor(mirPreparePhase());
  EXPECT_FALSE((*Pipeline)->addInterceptor(DynCodeTestPluginID, Interceptor));
  NevercProviderDescriptor Provider = makeProvider(mirPreparePhase());
  EXPECT_FALSE((*Pipeline)->addProvider(DynCodeTestPluginID, Provider));
}

TEST(PluginDynCodeMIRProviderTest, FinalVerifyIsSealed) {
  DynCodeTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Pipeline = DynCodePhasePipeline::create(Scope.task());
  ASSERT_TRUE(static_cast<bool>(Pipeline)) << errorText(Pipeline.takeError());

  NevercProviderDescriptor Provider = makeProvider(mirFinalVerifyPhase());
  Error ProviderRejected =
      (*Pipeline)->addProvider(DynCodeTestPluginID, Provider);
  EXPECT_TRUE(static_cast<bool>(ProviderRejected));
  consumeError(std::move(ProviderRejected));

  NevercInterceptorDescriptor Interceptor =
      makeInterceptor(mirFinalVerifyPhase());
  Error InterceptorRejected =
      (*Pipeline)->addInterceptor(DynCodeTestPluginID, Interceptor);
  EXPECT_TRUE(static_cast<bool>(InterceptorRejected));
  consumeError(std::move(InterceptorRejected));

  // A sealed gate is still OBSERVABLE.
  NevercObserverDescriptor Observer = makeObserver(mirFinalVerifyPhase());
  EXPECT_FALSE((*Pipeline)->addObserver(DynCodeTestPluginID, Observer));
}

} // namespace
