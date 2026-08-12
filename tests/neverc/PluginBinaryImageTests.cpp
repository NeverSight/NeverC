#include "Inputs/Plugin/PostEmitPlugin.h"
#include "Link/LinkOutputBundle.h"
#include "PluginBinaryImageTestSupport.h"
#include "neverc/Foundation/Core/OutputCoordinator.h"
#include "neverc/Plugin/Schema/PluginPhaseSchema.inc"
#include "gtest/gtest.h"
#include <memory>
#include <vector>

using namespace llvm;
using namespace neverc::plugin;
using namespace neverc::plugin::test_support;

namespace {

TEST(PluginBinaryImageTest, EmitsQueryableSegmentsSectionsAndStableBytes) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Image = makeImage(Scope);
  ASSERT_TRUE(static_cast<bool>(Image)) << errorText(Image.takeError());
  EXPECT_EQ((*Image)->state(), NEVERC_BINARY_IMAGE_CANDIDATE);
  EXPECT_EQ((*Image)->bytes().size(), 8U);
  EXPECT_EQ((*Image)->entryAddress(), UINT64_C(0x10000));

  NevercBinaryImageInfo Info{};
  Info.Header = {sizeof(Info), NEVERC_LINK_API_MAJOR, NEVERC_LINK_API_MINOR, 0};
  ASSERT_EQ((*Image)
                ->linkAPI()
                .GetBinaryImageInfo((*Image)->linkAPI().Context,
                                    Scope.task().handle(), (*Image)->handle(),
                                    &Info)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Info.Size, 8U);
  EXPECT_EQ(Info.SegmentCount, 1U);
  EXPECT_EQ(Info.SectionCount, 1U);
  EXPECT_FALSE(neverc_handle_is_null(Info.Builder));

  NevercBinarySegmentInfo Segment{};
  NevercLinkEntityPage Page{};
  Page.Header = {sizeof(Page), NEVERC_LINK_API_MAJOR, NEVERC_LINK_API_MINOR, 0};
  Page.Data = &Segment;
  Page.ElementCapacity = 1;
  Page.ElementStride = sizeof(Segment);
  ASSERT_EQ((*Image)
                ->linkAPI()
                .GetBinarySegmentPage((*Image)->linkAPI().Context,
                                      Scope.task().handle(), (*Image)->handle(),
                                      0, &Page)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(Page.OutCount, 1U);
  EXPECT_EQ(Segment.FileOffset, 0U);
  EXPECT_EQ(Segment.FileSize, 8U);
  EXPECT_NE(Segment.Flags & NEVERC_BINARY_SEGMENT_EXECUTE, 0U);
}

TEST(PluginBinaryImageTest,
     PostEmitPatchPrecedesSealedVerifyAndCommitIsReadOnly) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Image = makeImage(Scope);
  ASSERT_TRUE(static_cast<bool>(Image)) << errorText(Image.takeError());
  TemporaryDirectory Directory;
  const std::string Main = Directory.file("program.bin");
  const std::string Map = Directory.file("program.map");
  neverc::OutputCoordinator Coordinator;
  auto Pipeline = LinkOutputPipeline::create(Scope.task(), Coordinator);
  ASSERT_TRUE(static_cast<bool>(Pipeline)) << errorText(Pipeline.takeError());

  NevercTestPostEmitTrace Trace{};
  Trace.PhaseAPI = &Scope.phaseAPI();
  Trace.PatchOffset = 0;
  Trace.PatchValue = 0xcc;
  NevercInterceptorDescriptor Interceptor{};
  Interceptor.Header = {sizeof(Interceptor), NEVERC_PLUGIN_ABI_MAJOR,
                        NEVERC_PLUGIN_ABI_MINOR, 0};
  Interceptor.Phase = {NEVERC_PHASE_LINK_POST_EMIT_HIGH,
                       NEVERC_PHASE_LINK_POST_EMIT_LOW};
  Interceptor.Callback = neverc_test_post_emit_interceptor;
  Interceptor.UserData = &Trace;
  ASSERT_FALSE((*Pipeline)->addInterceptor(LinkTestPluginID, Interceptor));
  NevercObserverDescriptor Observer{};
  Observer.Header = {sizeof(Observer), NEVERC_PLUGIN_ABI_MAJOR,
                     NEVERC_PLUGIN_ABI_MINOR, 0};
  Observer.Phase = {NEVERC_PHASE_LINK_AFTER_COMMIT_HIGH,
                    NEVERC_PHASE_LINK_AFTER_COMMIT_LOW};
  Observer.Points = NEVERC_OBSERVER_AFTER;
  Observer.Callback = neverc_test_after_commit_observer;
  Observer.UserData = &Trace;
  ASSERT_FALSE((*Pipeline)->addObserver(LinkTestPluginID, Observer));

  const std::vector<PluginLinkSideOutput> Sides = {
      {"map", Map, {'m', 'a', 'p'}}};
  auto Output = (*Pipeline)->execute(*Image, Main, Sides);
  ASSERT_TRUE(static_cast<bool>(Output)) << errorText(Output.takeError());
  EXPECT_EQ(Output->Image->state(), NEVERC_BINARY_IMAGE_COMMITTED);
  EXPECT_EQ(Output->Summary.State, neverc::OutputBundleState::Committed);
  EXPECT_EQ(Trace.Calls, 1U);
  EXPECT_EQ(Trace.MutationStatus, NEVERC_STATUS_OK);
  EXPECT_EQ(Trace.AfterCommitCalls, 1U);
  EXPECT_EQ(Trace.AfterCommitWriteStatus, NEVERC_STATUS_POLICY_VIOLATION);
  const std::string MainBytes = readFile(Main);
  ASSERT_EQ(MainBytes.size(), 8U);
  EXPECT_EQ(static_cast<uint8_t>(MainBytes[0]), 0xcc);
  EXPECT_EQ(readFile(Map), "map");
}

TEST(PluginBinaryImageTest, PostEmitObserverCannotMutatePreSealBinaryImage) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  TemporaryDirectory Directory;
  const std::string BaselinePath = Directory.file("baseline.bin");
  const std::string ObservedPath = Directory.file("observed.bin");
  neverc::OutputCoordinator Coordinator;

  auto BaselineImage = makeImage(Scope);
  ASSERT_TRUE(static_cast<bool>(BaselineImage))
      << errorText(BaselineImage.takeError());
  auto BaselinePipeline = LinkOutputPipeline::create(Scope.task(), Coordinator);
  ASSERT_TRUE(static_cast<bool>(BaselinePipeline))
      << errorText(BaselinePipeline.takeError());
  auto Baseline = (*BaselinePipeline)->execute(*BaselineImage, BaselinePath);
  ASSERT_TRUE(static_cast<bool>(Baseline)) << errorText(Baseline.takeError());

  auto ObservedImage = makeImage(Scope);
  ASSERT_TRUE(static_cast<bool>(ObservedImage))
      << errorText(ObservedImage.takeError());
  auto ObservedPipeline = LinkOutputPipeline::create(Scope.task(), Coordinator);
  ASSERT_TRUE(static_cast<bool>(ObservedPipeline))
      << errorText(ObservedPipeline.takeError());
  NevercTestPostEmitTrace Trace{};
  Trace.PhaseAPI = &Scope.phaseAPI();
  NevercObserverDescriptor Observer{};
  Observer.Header = {sizeof(Observer), NEVERC_PLUGIN_ABI_MAJOR,
                     NEVERC_PLUGIN_ABI_MINOR, 0};
  Observer.Phase = {NEVERC_PHASE_LINK_POST_EMIT_HIGH,
                    NEVERC_PHASE_LINK_POST_EMIT_LOW};
  Observer.Points = NEVERC_OBSERVER_BEFORE;
  Observer.Callback = neverc_test_post_emit_observer;
  Observer.UserData = &Trace;
  ASSERT_FALSE((*ObservedPipeline)->addObserver(LinkTestPluginID, Observer));

  auto Observed = (*ObservedPipeline)->execute(*ObservedImage, ObservedPath);
  ASSERT_TRUE(static_cast<bool>(Observed)) << errorText(Observed.takeError());
  ASSERT_EQ(Trace.ObserverCalls, 1U);
  for (NevercStatusCode Status : Trace.ObserverMutationStatuses)
    EXPECT_EQ(Status, NEVERC_STATUS_POLICY_VIOLATION);
  EXPECT_EQ(readFile(ObservedPath), readFile(BaselinePath));
}

TEST(PluginBinaryImageTest,
     CachedPostEmitInterceptorCapabilityExpiresBeforeImageVerify) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Image = makeImage(Scope);
  ASSERT_TRUE(static_cast<bool>(Image)) << errorText(Image.takeError());
  TemporaryDirectory Directory;
  const std::string MainPath = Directory.file("expired-capability.bin");
  neverc::OutputCoordinator Coordinator;
  auto Pipeline = LinkOutputPipeline::create(Scope.task(), Coordinator);
  ASSERT_TRUE(static_cast<bool>(Pipeline)) << errorText(Pipeline.takeError());
  NevercTestPostEmitTrace Trace{};
  Trace.PhaseAPI = &Scope.phaseAPI();
  Trace.PatchOffset = 0;
  Trace.PatchValue = 0xcc;

  NevercInterceptorDescriptor Interceptor{};
  Interceptor.Header = {sizeof(Interceptor), NEVERC_PLUGIN_ABI_MAJOR,
                        NEVERC_PLUGIN_ABI_MINOR, 0};
  Interceptor.Phase = {NEVERC_PHASE_LINK_POST_EMIT_HIGH,
                       NEVERC_PHASE_LINK_POST_EMIT_LOW};
  Interceptor.Callback = neverc_test_post_emit_interceptor;
  Interceptor.UserData = &Trace;
  ASSERT_FALSE((*Pipeline)->addInterceptor(LinkTestPluginID, Interceptor));

  NevercObserverDescriptor Observer{};
  Observer.Header = {sizeof(Observer), NEVERC_PLUGIN_ABI_MAJOR,
                     NEVERC_PLUGIN_ABI_MINOR, 0};
  Observer.Phase = {NEVERC_PHASE_LINK_IMAGE_VERIFY_HIGH,
                    NEVERC_PHASE_LINK_IMAGE_VERIFY_LOW};
  Observer.Points = NEVERC_OBSERVER_BEFORE;
  Observer.Callback = neverc_test_cached_post_emit_capability_observer;
  Observer.UserData = &Trace;
  ASSERT_FALSE((*Pipeline)->addObserver(LinkTestPluginID, Observer));

  auto Output = (*Pipeline)->execute(*Image, MainPath);
  ASSERT_TRUE(static_cast<bool>(Output)) << errorText(Output.takeError());
  EXPECT_EQ(Trace.MutationStatus, NEVERC_STATUS_OK);
  EXPECT_EQ(Trace.CachedCapabilityObserverCalls, 1U);
  EXPECT_EQ(Trace.CachedLinkReadStatus, NEVERC_STATUS_OK);
  for (NevercStatusCode Status : Trace.CachedCapabilityMutationStatuses)
    EXPECT_EQ(Status, NEVERC_STATUS_POLICY_VIOLATION);
  const std::string Bytes = readFile(MainPath);
  ASSERT_EQ(Bytes.size(), 8U);
  EXPECT_EQ(static_cast<uint8_t>(Bytes[0]), 0xcc);
}

TEST(PluginBinaryImageTest,
     CachedPostEmitObserverFacadesTombstoneAfterImageRetires) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  TemporaryDirectory Directory;
  const std::string MainPath = Directory.file("cached-facade.bin");
  neverc::OutputCoordinator Coordinator;
  NevercTestPostEmitTrace Trace{};
  Trace.PhaseAPI = &Scope.phaseAPI();
  std::weak_ptr<PluginBinaryImage> WeakImage;

  {
    auto Image = makeImage(Scope);
    ASSERT_TRUE(static_cast<bool>(Image)) << errorText(Image.takeError());
    WeakImage = *Image;
    auto Pipeline = LinkOutputPipeline::create(Scope.task(), Coordinator);
    ASSERT_TRUE(static_cast<bool>(Pipeline)) << errorText(Pipeline.takeError());
    NevercObserverDescriptor Observer{};
    Observer.Header = {sizeof(Observer), NEVERC_PLUGIN_ABI_MAJOR,
                       NEVERC_PLUGIN_ABI_MINOR, 0};
    Observer.Phase = {NEVERC_PHASE_LINK_POST_EMIT_HIGH,
                      NEVERC_PHASE_LINK_POST_EMIT_LOW};
    Observer.Points = NEVERC_OBSERVER_BEFORE;
    Observer.Callback = neverc_test_post_emit_observer;
    Observer.UserData = &Trace;
    ASSERT_FALSE((*Pipeline)->addObserver(LinkTestPluginID, Observer));
    auto Output = (*Pipeline)->execute(*Image, MainPath);
    ASSERT_TRUE(static_cast<bool>(Output)) << errorText(Output.takeError());
  }

  ASSERT_TRUE(WeakImage.expired());
  ASSERT_NE(Trace.CachedLink, nullptr);
  ASSERT_NE(Trace.CachedBinary, nullptr);
  NevercBinaryImageInfo Info{};
  Info.Header = {sizeof(Info), NEVERC_LINK_API_MAJOR, NEVERC_LINK_API_MINOR, 0};
  EXPECT_EQ(Trace.CachedLink
                ->GetBinaryImageInfo(Trace.CachedLink->Context,
                                     Trace.CachedTask, Trace.CachedImage, &Info)
                .Code,
            NEVERC_STATUS_STALE_HANDLE);
  uint64_t Position = 0;
  EXPECT_EQ(Trace.CachedBinary
                ->Tell(Trace.CachedBinary->Context, Trace.CachedTask,
                       Trace.CachedBuilder, &Position)
                .Code,
            NEVERC_STATUS_STALE_HANDLE);
  NevercByteView Empty{};
  EXPECT_EQ(Trace.CachedBinary
                ->Reserve(Trace.CachedBinary->Context, Trace.CachedTask,
                          Trace.CachedBuilder, 0)
                .Code,
            NEVERC_STATUS_POLICY_VIOLATION);
  EXPECT_EQ(Trace.CachedBinary
                ->Write(Trace.CachedBinary->Context, Trace.CachedTask,
                        Trace.CachedBuilder, Empty)
                .Code,
            NEVERC_STATUS_POLICY_VIOLATION);
  EXPECT_EQ(Trace.CachedBinary
                ->WriteAt(Trace.CachedBinary->Context, Trace.CachedTask,
                          Trace.CachedBuilder, 0, Empty)
                .Code,
            NEVERC_STATUS_POLICY_VIOLATION);
  EXPECT_EQ(Trace.CachedBinary
                ->Insert(Trace.CachedBinary->Context, Trace.CachedTask,
                         Trace.CachedBuilder, 0, Empty)
                .Code,
            NEVERC_STATUS_POLICY_VIOLATION);
  EXPECT_EQ(Trace.CachedBinary
                ->Append(Trace.CachedBinary->Context, Trace.CachedTask,
                         Trace.CachedBuilder, Empty)
                .Code,
            NEVERC_STATUS_POLICY_VIOLATION);
  EXPECT_EQ(Trace.CachedBinary
                ->Resize(Trace.CachedBinary->Context, Trace.CachedTask,
                         Trace.CachedBuilder, Trace.CachedImageSize)
                .Code,
            NEVERC_STATUS_POLICY_VIOLATION);
}

TEST(PluginBinaryImageTest, InvalidPostEmitGrowthPublishesNothing) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Image = makeImage(Scope);
  ASSERT_TRUE(static_cast<bool>(Image)) << errorText(Image.takeError());
  TemporaryDirectory Directory;
  const std::string Main = Directory.file("program.bin");
  neverc::OutputCoordinator Coordinator;
  auto Pipeline = LinkOutputPipeline::create(Scope.task(), Coordinator);
  ASSERT_TRUE(static_cast<bool>(Pipeline)) << errorText(Pipeline.takeError());
  NevercTestPostEmitTrace Trace{};
  Trace.PhaseAPI = &Scope.phaseAPI();
  Trace.PatchValue = 0xaa;
  Trace.AppendByte = NEVERC_TRUE;
  NevercInterceptorDescriptor Interceptor{};
  Interceptor.Header = {sizeof(Interceptor), NEVERC_PLUGIN_ABI_MAJOR,
                        NEVERC_PLUGIN_ABI_MINOR, 0};
  Interceptor.Phase = {NEVERC_PHASE_LINK_POST_EMIT_HIGH,
                       NEVERC_PHASE_LINK_POST_EMIT_LOW};
  Interceptor.Callback = neverc_test_post_emit_interceptor;
  Interceptor.UserData = &Trace;
  ASSERT_FALSE((*Pipeline)->addInterceptor(LinkTestPluginID, Interceptor));
  auto Output = (*Pipeline)->execute(*Image, Main);
  EXPECT_FALSE(static_cast<bool>(Output));
  if (!Output)
    consumeError(Output.takeError());
  EXPECT_EQ(Trace.MutationStatus, NEVERC_STATUS_OK);
  EXPECT_FALSE(sys::fs::exists(Main));
}

TEST(PluginBinaryImageTest, PostEmitBudgetOverflowPublishesNothing) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Image = makeImage(Scope, 8);
  ASSERT_TRUE(static_cast<bool>(Image)) << errorText(Image.takeError());
  TemporaryDirectory Directory;
  const std::string Main = Directory.file("program.bin");
  neverc::OutputCoordinator Coordinator;
  auto Pipeline = LinkOutputPipeline::create(Scope.task(), Coordinator);
  ASSERT_TRUE(static_cast<bool>(Pipeline)) << errorText(Pipeline.takeError());
  NevercTestPostEmitTrace Trace{};
  Trace.PhaseAPI = &Scope.phaseAPI();
  Trace.PatchValue = 0xaa;
  Trace.AppendByte = NEVERC_TRUE;
  NevercInterceptorDescriptor Interceptor{};
  Interceptor.Header = {sizeof(Interceptor), NEVERC_PLUGIN_ABI_MAJOR,
                        NEVERC_PLUGIN_ABI_MINOR, 0};
  Interceptor.Phase = {NEVERC_PHASE_LINK_POST_EMIT_HIGH,
                       NEVERC_PHASE_LINK_POST_EMIT_LOW};
  Interceptor.Callback = neverc_test_post_emit_interceptor;
  Interceptor.UserData = &Trace;
  ASSERT_FALSE((*Pipeline)->addInterceptor(LinkTestPluginID, Interceptor));
  auto Output = (*Pipeline)->execute(*Image, Main);
  EXPECT_FALSE(static_cast<bool>(Output));
  if (!Output)
    consumeError(Output.takeError());
  EXPECT_EQ(Trace.MutationStatus, NEVERC_STATUS_RESOURCE_EXHAUSTED);
  EXPECT_FALSE(sys::fs::exists(Main));
}

} // namespace
