#include "PluginLinkTestSupport.h"
#include "Inputs/Plugin/LinkPhaseTracePlugin.h"
#include "Link/LinkPhaseExecutor.h"
#include "neverc/Plugin/Schema/PluginPhaseSchema.inc"
#include "gtest/gtest.h"
#include <string>

using namespace llvm;
using namespace neverc::plugin;
using namespace neverc::plugin::test_support;

namespace {

NevercInterfaceID phaseID(uint64_t High, uint64_t Low) {
  return {High, Low};
}

std::shared_ptr<PluginLinkGraph> makeGraph() {
  auto Target = makeTargetKey();
  if (!Target) {
    ADD_FAILURE() << errorText(Target.takeError());
    return {};
  }
  auto Graph =
      std::make_shared<PluginLinkGraph>(std::move(*Target));
  populateValidGraph(*Graph);
  return Graph;
}

TEST(PluginLinkPhaseTest, RegistryMatchesGeneratedStableSchema) {
  auto Registry = LinkPhaseRegistry::create();
  ASSERT_TRUE(static_cast<bool>(Registry))
      << errorText(Registry.takeError());
  EXPECT_EQ(Registry->graph().size(), NEVERC_BUILTIN_LINK_PHASE_COUNT);
  EXPECT_EQ(Registry->transitions().size(), 13U);
  for (const LinkTransitionDefinition &Transition :
       Registry->transitions()) {
    const PluginPhaseDefinition *Phase =
        Registry->graph().find(Transition.Phase);
    ASSERT_NE(Phase, nullptr);
    EXPECT_EQ(
        Phase->Policy,
        NEVERC_PHASE_OBSERVABLE | NEVERC_PHASE_INTERCEPTABLE |
            NEVERC_PHASE_REPLACEABLE |
            NEVERC_PHASE_SKIPPABLE_WITH_PROOF);
  }
}

TEST(PluginLinkPhaseTest,
     NativeProjectionIsRequiredOnlyByLinkPhaseBindings) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Pipeline = LinkPhasePipeline::create(Scope.task());
  ASSERT_TRUE(static_cast<bool>(Pipeline))
      << errorText(Pipeline.takeError());
  EXPECT_FALSE((*Pipeline)->requiresNativeProjection());

  NevercTestLinkPhaseTrace Trace{};
  Trace.PhaseAPI = &Scope.phaseAPI();
  NevercObserverDescriptor Observer{};
  Observer.Header = {sizeof(Observer), NEVERC_PLUGIN_ABI_MAJOR,
                     NEVERC_PLUGIN_ABI_MINOR, 0};
  Observer.Phase = phaseID(NEVERC_PHASE_LINK_LAYOUT_HIGH,
                           NEVERC_PHASE_LINK_LAYOUT_LOW);
  Observer.Points = NEVERC_OBSERVER_BEFORE | NEVERC_OBSERVER_AFTER;
  Observer.Callback = neverc_test_link_observer;
  Observer.UserData = &Trace;
  ASSERT_FALSE(
      (*Pipeline)->addObserver(LinkTestPluginID, Observer));
  EXPECT_TRUE((*Pipeline)->requiresNativeProjection());
}

TEST(PluginLinkPhaseTest,
     CoarseLinkGateDoesNotRequireNativeGraphProjection) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Pipeline = LinkPhasePipeline::create(Scope.task());
  ASSERT_TRUE(static_cast<bool>(Pipeline))
      << errorText(Pipeline.takeError());

  NevercTestLinkPhaseTrace Trace{};
  Trace.PhaseAPI = &Scope.phaseAPI();
  NevercObserverDescriptor Observer{};
  Observer.Header = {sizeof(Observer), NEVERC_PLUGIN_ABI_MAJOR,
                     NEVERC_PLUGIN_ABI_MINOR, 0};
  Observer.Phase = phaseID(NEVERC_PHASE_LINK_FULL_HIGH,
                           NEVERC_PHASE_LINK_FULL_LOW);
  Observer.Points = NEVERC_OBSERVER_BEFORE | NEVERC_OBSERVER_AFTER;
  Observer.Callback = neverc_test_link_observer;
  Observer.UserData = &Trace;
  ASSERT_FALSE(
      (*Pipeline)->addObserver(LinkTestPluginID, Observer));
  EXPECT_FALSE((*Pipeline)->requiresNativeProjection());
}

TEST(PluginLinkPhaseTest, BuiltinPipelineExecutesAllTypedTransitions) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Pipeline = LinkPhasePipeline::create(Scope.task());
  ASSERT_TRUE(static_cast<bool>(Pipeline))
      << errorText(Pipeline.takeError());
  auto Output = (*Pipeline)->execute(makeGraph());
  ASSERT_TRUE(static_cast<bool>(Output))
      << errorText(Output.takeError());
  EXPECT_EQ((*Output)->state(), NEVERC_LINK_STATE_IMAGE_EMITTED);
}

TEST(PluginLinkPhaseTest,
     PostInterceptorMutationInvalidatesAndRerunsFromEarliestPhase) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Pipeline = LinkPhasePipeline::create(Scope.task());
  ASSERT_TRUE(static_cast<bool>(Pipeline))
      << errorText(Pipeline.takeError());

  NevercTestLinkPhaseTrace Trace{};
  Trace.PhaseAPI = &Scope.phaseAPI();
  const NevercInterfaceID Layout =
      phaseID(NEVERC_PHASE_LINK_LAYOUT_HIGH,
              NEVERC_PHASE_LINK_LAYOUT_LOW);
  NevercObserverDescriptor Observer{};
  Observer.Header = {sizeof(Observer), NEVERC_PLUGIN_ABI_MAJOR,
                     NEVERC_PLUGIN_ABI_MINOR, 0};
  Observer.Phase = Layout;
  Observer.Points = NEVERC_OBSERVER_BEFORE | NEVERC_OBSERVER_AFTER;
  Observer.Callback = neverc_test_link_observer;
  Observer.UserData = &Trace;
  ASSERT_FALSE((*Pipeline)->addObserver(LinkTestPluginID, Observer));

  NevercInterceptorDescriptor Interceptor{};
  Interceptor.Header = {sizeof(Interceptor), NEVERC_PLUGIN_ABI_MAJOR,
                        NEVERC_PLUGIN_ABI_MINOR, 0};
  Interceptor.Phase = Layout;
  Interceptor.Callback = neverc_test_link_interceptor;
  Interceptor.UserData = &Trace;
  ASSERT_FALSE((*Pipeline)->addInterceptor(LinkTestPluginID, Interceptor));

  auto Output = (*Pipeline)->execute(
      makeGraph(), NEVERC_LINK_STATE_LAYOUT_COMPLETE);
  ASSERT_TRUE(static_cast<bool>(Output))
      << errorText(Output.takeError());
  EXPECT_EQ((*Output)->state(), NEVERC_LINK_STATE_LAYOUT_COMPLETE);
  EXPECT_EQ(Trace.Mutations, 1U);
  EXPECT_EQ(Trace.MutationStatus, NEVERC_STATUS_OK);
  EXPECT_EQ(std::string(Trace.Events,
                        Trace.Events + Trace.EventCount),
            "BINMABINA");
  EXPECT_EQ((*Pipeline)->rerunCount(Layout), 1U);
  EXPECT_EQ((*Pipeline)->rerunCount(
                phaseID(NEVERC_PHASE_LINK_GC_HIGH,
                        NEVERC_PHASE_LINK_GC_LOW)),
            1U);
}

TEST(PluginLinkPhaseTest, SelectedCProviderPublishesTypedGraph) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Pipeline = LinkPhasePipeline::create(Scope.task());
  ASSERT_TRUE(static_cast<bool>(Pipeline))
      << errorText(Pipeline.takeError());
  NevercTestLinkPhaseTrace Trace{};
  Trace.PhaseAPI = &Scope.phaseAPI();
  const NevercInterfaceID InputProbe =
      phaseID(NEVERC_PHASE_LINK_INPUT_PROBE_HIGH,
              NEVERC_PHASE_LINK_INPUT_PROBE_LOW);
  NevercProviderDescriptor Provider{};
  Provider.Header = {sizeof(Provider), NEVERC_PLUGIN_ABI_MAJOR,
                     NEVERC_PLUGIN_ABI_MINOR, 0};
  Provider.Phase = InputProbe;
  Provider.ProviderID = {"trace-provider", 14};
  Provider.Route.Header = {sizeof(Provider.Route),
                           NEVERC_PLUGIN_ABI_MAJOR,
                           NEVERC_PLUGIN_ABI_MINOR, 0};
  Provider.Deterministic = NEVERC_TRUE;
  Provider.Callback = neverc_test_link_provider;
  Provider.UserData = &Trace;
  ASSERT_FALSE((*Pipeline)->addProvider(LinkTestPluginID, Provider));
  ASSERT_FALSE(
      (*Pipeline)->selectProvider(InputProbe, LinkTestPluginID));

  auto Output = (*Pipeline)->execute(
      makeGraph(), NEVERC_LINK_STATE_INPUT_PROBED);
  ASSERT_TRUE(static_cast<bool>(Output))
      << errorText(Output.takeError());
  EXPECT_EQ((*Output)->state(), NEVERC_LINK_STATE_INPUT_PROBED);
  EXPECT_EQ(std::string(Trace.Events,
                        Trace.Events + Trace.EventCount),
            "P");
}

TEST(PluginLinkPhaseTest, SealedGateRejectsReplacementProvider) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Pipeline = LinkPhasePipeline::create(Scope.task());
  ASSERT_TRUE(static_cast<bool>(Pipeline))
      << errorText(Pipeline.takeError());
  NevercTestLinkPhaseTrace Trace{};
  Trace.PhaseAPI = &Scope.phaseAPI();
  NevercProviderDescriptor Provider{};
  Provider.Header = {sizeof(Provider), NEVERC_PLUGIN_ABI_MAJOR,
                     NEVERC_PLUGIN_ABI_MINOR, 0};
  Provider.Phase =
      phaseID(NEVERC_PHASE_LINK_IMAGE_VERIFY_HIGH,
              NEVERC_PHASE_LINK_IMAGE_VERIFY_LOW);
  Provider.ProviderID = {"forbidden", 9};
  Provider.Route.Header = {sizeof(Provider.Route),
                           NEVERC_PLUGIN_ABI_MAJOR,
                           NEVERC_PLUGIN_ABI_MINOR, 0};
  Provider.Callback = neverc_test_link_provider;
  Provider.UserData = &Trace;
  llvm::Error Rejected = (*Pipeline)->addProvider(
      LinkTestPluginID, Provider);
  EXPECT_TRUE(static_cast<bool>(Rejected));
  consumeError(std::move(Rejected));
}

TEST(PluginLinkPhaseTest, ErrorAndCancellationDoNotPublishOutput) {
  LinkTaskScope ErrorScope;
  ASSERT_TRUE(ErrorScope.initialize());
  auto ErrorPipeline = LinkPhasePipeline::create(ErrorScope.task());
  ASSERT_TRUE(static_cast<bool>(ErrorPipeline))
      << errorText(ErrorPipeline.takeError());
  const NevercInterfaceID InputProbe =
      phaseID(NEVERC_PHASE_LINK_INPUT_PROBE_HIGH,
              NEVERC_PHASE_LINK_INPUT_PROBE_LOW);
  ASSERT_FALSE((*ErrorPipeline)
                   ->setBuiltinProvider(
                       InputProbe,
                       [](const NevercPhaseFrame *, NevercPhaseResult *) {
                         NevercStatus Status = neverc_status_ok();
                         Status.Code = NEVERC_STATUS_PLUGIN_FAILURE;
                         return Status;
                       }));
  auto Failed = (*ErrorPipeline)->execute(
      makeGraph(), NEVERC_LINK_STATE_INPUT_PROBED);
  EXPECT_FALSE(static_cast<bool>(Failed));
  if (!Failed)
    consumeError(Failed.takeError());

  LinkTaskScope CancelledScope;
  ASSERT_TRUE(CancelledScope.initialize());
  auto CancelledPipeline =
      LinkPhasePipeline::create(CancelledScope.task());
  ASSERT_TRUE(static_cast<bool>(CancelledPipeline))
      << errorText(CancelledPipeline.takeError());
  CancelledScope.session().cancel();
  auto Cancelled = (*CancelledPipeline)->execute(
      makeGraph(), NEVERC_LINK_STATE_INPUT_PROBED);
  EXPECT_FALSE(static_cast<bool>(Cancelled));
  if (!Cancelled)
    consumeError(Cancelled.takeError());
}

} // namespace
