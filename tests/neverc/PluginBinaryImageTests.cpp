#include "Inputs/Plugin/PostEmitPlugin.h"
#include "Link/BinaryImage.h"
#include "Link/LinkOutputBundle.h"
#include "Link/LinkPhaseExecutor.h"
#include "PluginLinkTestSupport.h"
#include "neverc/Foundation/Core/OutputBundleTransaction.h"
#include "neverc/Foundation/Core/OutputCoordinator.h"
#include "neverc/Foundation/Core/OutputTransaction.h"
#include "neverc/Plugin/Host/PluginIOBridge.h"
#include "neverc/Plugin/Schema/PluginPhaseSchema.inc"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "gtest/gtest.h"
#include <atomic>
#include <fstream>

using namespace llvm;
using namespace neverc::plugin;
using namespace neverc::plugin::test_support;

namespace {

Expected<const NevercIOAPI *> ioAPI(LinkTaskScope &Scope) {
  auto Query = Scope.services().interfaces().query(
      ioPluginInterfaceID(), NEVERC_IO_API_MAJOR, NEVERC_IO_API_MINOR);
  if (!Query)
    return Query.takeError();
  return static_cast<const NevercIOAPI *>(Query->Table);
}

Expected<std::shared_ptr<PluginBinaryImage>>
makeImage(LinkTaskScope &Scope, uint64_t Budget = UINT64_C(8192)) {
  auto Target = makeTargetKey();
  if (!Target)
    return Target.takeError();
  auto Graph = std::make_shared<PluginLinkGraph>(
      std::move(*Target), NEVERC_LINK_STATE_THUNKS_RELAXED);
  GraphEntities Entities = populateValidGraph(*Graph);
  Graph->findAtom(Entities.AtomID)->Flags |= NEVERC_LINK_ATOM_LIVE;
  PluginLinkConstraint FileBase;
  FileBase.Kind = "file-base";
  FileBase.Value = 0;
  FileBase.Required = true;
  Graph->addConstraint(std::move(FileBase));
  auto LinkPipeline = LinkPhasePipeline::create(Scope.task());
  if (!LinkPipeline)
    return LinkPipeline.takeError();
  auto Relocated =
      (*LinkPipeline)->execute(Graph, NEVERC_LINK_STATE_RELOCATIONS_APPLIED);
  if (!Relocated)
    return Relocated.takeError();

  static std::atomic<uint64_t> NextOutput{1};
  std::string Name = "binary-image-" + std::to_string(NextOutput.fetch_add(1));
  NevercOutputSinkHandle Sink{};
  auto IO = ioAPI(Scope);
  if (!IO)
    return IO.takeError();
  NevercStatus Status =
      (*IO)->BeginMemoryOutput((*IO)->Context, Scope.task().handle(),
                               {Name.data(), Name.size()}, Budget, &Sink);
  if (!neverc_status_is_ok(Status))
    return createStringError(inconvertibleErrorCode(),
                             "could not create BinaryImage output sink");
  return PluginBinaryImage::emit(Scope.task(), **IO, Sink, **Relocated);
}

struct TemporaryDirectory {
  SmallString<128> Path;

  TemporaryDirectory() {
    EXPECT_FALSE(
        sys::fs::createUniqueDirectory("neverc-plugin-binary-image", Path));
  }
  ~TemporaryDirectory() { (void)sys::fs::remove_directories(Path); }

  std::string file(StringRef Name) const {
    SmallString<160> Result(Path);
    sys::path::append(Result, Name);
    return Result.str().str();
  }
};

std::string readFile(StringRef Path) {
  auto Buffer = MemoryBuffer::getFile(Path);
  if (!Buffer)
    return {};
  return (*Buffer)->getBuffer().str();
}

void writeFile(StringRef Path, StringRef Contents) {
  std::ofstream Stream(Path.str(), std::ios::binary);
  Stream.write(Contents.data(), Contents.size());
}

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

TEST(PluginBinaryImageTest, MainPublishFailureRestoresTheOldBundle) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Image = makeImage(Scope);
  ASSERT_TRUE(static_cast<bool>(Image)) << errorText(Image.takeError());
  TemporaryDirectory Directory;
  const std::string Main = Directory.file("program.bin");
  const std::string Map = Directory.file("program.map");
  writeFile(Main, "old-main");
  writeFile(Map, "old-map");
  neverc::OutputCoordinator Coordinator;
  auto Pipeline = LinkOutputPipeline::create(Scope.task(), Coordinator);
  ASSERT_TRUE(static_cast<bool>(Pipeline)) << errorText(Pipeline.takeError());
  const std::vector<PluginLinkSideOutput> Sides = {
      {"map", Map, {'n', 'e', 'w', '-', 'm', 'a', 'p'}}};
  auto Output = (*Pipeline)->execute(
      *Image, Main, Sides,
      [](neverc::OutputBundleOperation Operation, StringRef) {
        return Operation == neverc::OutputBundleOperation::PublishMain
                   ? std::make_error_code(std::errc::io_error)
                   : std::error_code();
      });
  EXPECT_FALSE(static_cast<bool>(Output));
  if (!Output)
    consumeError(Output.takeError());
  EXPECT_EQ(readFile(Main), "old-main");
  EXPECT_EQ(readFile(Map), "old-map");
}

TEST(PluginBinaryImageTest,
     ExistingBundleRemainsVisibleUntilAtomicReplacementsBegin) {
  TemporaryDirectory Directory;
  const std::string Main = Directory.file("module.ko");
  const std::string Map = Directory.file("module.ko.symbols.json");
  writeFile(Main, "old-main");
  writeFile(Map, "old-map");
  neverc::OutputCoordinator Coordinator;
  std::vector<neverc::OutputBundleFile> Outputs = {
      {"main", Main, {'n', 'e', 'w', '-', 'm', 'a', 'i', 'n'}, true},
      {"map", Map, {'n', 'e', 'w', '-', 'm', 'a', 'p'}},
  };
  bool ReachedSidePublish = false;
  bool SawOldMain = false;
  bool SawOldMap = false;
  auto Bundle = neverc::OutputBundleTransaction::create(
      Coordinator, Outputs,
      /*IsCancelled=*/{},
      [&](neverc::OutputBundleOperation Operation, StringRef) {
        if (Operation != neverc::OutputBundleOperation::PublishSide)
          return std::error_code();
        ReachedSidePublish = true;
        SawOldMain = readFile(Main) == "old-main";
        SawOldMap = readFile(Map) == "old-map";
        return std::make_error_code(std::errc::io_error);
      });
  ASSERT_TRUE(static_cast<bool>(Bundle)) << errorText(Bundle.takeError());
  auto Committed = (*Bundle)->commit();
  ASSERT_FALSE(static_cast<bool>(Committed));
  consumeError(Committed.takeError());
  EXPECT_TRUE(ReachedSidePublish);
  EXPECT_TRUE(SawOldMain);
  EXPECT_TRUE(SawOldMap);
  EXPECT_EQ(readFile(Main), "old-main");
  EXPECT_EQ(readFile(Map), "old-map");
}

TEST(PluginBinaryImageTest,
     BackupMaterializationFailureAbortsWithoutRecoveryState) {
  TemporaryDirectory Directory;
  const std::string Main = Directory.file("occupied-output");
  ASSERT_FALSE(sys::fs::create_directory(Main));
  neverc::OutputCoordinator Coordinator;
  std::vector<neverc::OutputBundleFile> Outputs = {
      {"main", Main, {'n', 'e', 'w'}, true},
  };
  auto Bundle = neverc::OutputBundleTransaction::create(Coordinator, Outputs);
  ASSERT_TRUE(static_cast<bool>(Bundle)) << errorText(Bundle.takeError());
  auto Committed = (*Bundle)->commit();
  ASSERT_FALSE(static_cast<bool>(Committed));
  consumeError(Committed.takeError());
  const neverc::OutputBundleSummary Summary = (*Bundle)->summary();
  EXPECT_EQ(Summary.State, neverc::OutputBundleState::Aborted);
  EXPECT_EQ(Summary.Flags & neverc::OutputRecoveryRequired, 0U);
  sys::fs::file_status Status;
  ASSERT_FALSE(sys::fs::status(Main, Status, /*follow=*/false));
  EXPECT_EQ(Status.type(), sys::fs::file_type::directory_file);
}

TEST(PluginBinaryImageTest, BundleCanRemoveAStaleSideOutput) {
  TemporaryDirectory Directory;
  const std::string Main = Directory.file("module.ko");
  const std::string Map = Directory.file("module.ko.symbols.json");
  writeFile(Main, "old-main");
  writeFile(Map, "old-map");
  neverc::OutputCoordinator Coordinator;
  std::vector<neverc::OutputBundleFile> Outputs = {
      {"main", Main, {'n', 'e', 'w'}, true},
      {"map", Map, {}, false, false,
       neverc::OutputBundleFileAction::Remove},
  };
  auto Bundle = neverc::OutputBundleTransaction::create(Coordinator, Outputs);
  ASSERT_TRUE(static_cast<bool>(Bundle)) << errorText(Bundle.takeError());
  auto Committed = (*Bundle)->commit();
  ASSERT_TRUE(static_cast<bool>(Committed))
      << errorText(Committed.takeError());
  EXPECT_EQ(readFile(Main), "new");
  EXPECT_FALSE(sys::fs::exists(Map));
}

TEST(PluginBinaryImageTest, BundleRemovesADanglingSideOutputSymlink) {
  TemporaryDirectory Directory;
  const std::string Main = Directory.file("module.ko");
  const std::string Map = Directory.file("module.ko.symbols.json");
  const std::string MissingTarget = Directory.file("missing-symbol-map");
  writeFile(Main, "old-main");
  ASSERT_FALSE(sys::fs::create_link(MissingTarget, Map));
  sys::fs::file_status Before;
  ASSERT_FALSE(sys::fs::status(Map, Before, /*follow=*/false));
  ASSERT_EQ(Before.type(), sys::fs::file_type::symlink_file);

  neverc::OutputCoordinator Coordinator;
  std::vector<neverc::OutputBundleFile> Outputs = {
      {"main", Main, {'n', 'e', 'w'}, true},
      {"map", Map, {}, false, false,
       neverc::OutputBundleFileAction::Remove},
  };
  auto Bundle = neverc::OutputBundleTransaction::create(Coordinator, Outputs);
  ASSERT_TRUE(static_cast<bool>(Bundle)) << errorText(Bundle.takeError());
  auto Committed = (*Bundle)->commit();
  ASSERT_TRUE(static_cast<bool>(Committed))
      << errorText(Committed.takeError());
  EXPECT_EQ(readFile(Main), "new");

  sys::fs::file_status After;
  EXPECT_EQ(sys::fs::status(Map, After, /*follow=*/false),
            std::make_error_code(std::errc::no_such_file_or_directory));
}

TEST(PluginBinaryImageTest,
     UnsupportedDirectorySyncDoesNotClaimDurablePublication) {
  TemporaryDirectory Directory;
  const std::string Main = Directory.file("module.ko");
  neverc::OutputCoordinator Coordinator;
  std::vector<neverc::OutputBundleFile> Outputs = {
      {"main", Main, {'n', 'e', 'w'}, true},
  };
  auto Bundle = neverc::OutputBundleTransaction::create(
      Coordinator, Outputs,
      /*IsCancelled=*/{},
      [](neverc::OutputBundleOperation Operation, StringRef) {
        return Operation == neverc::OutputBundleOperation::SyncDirectory
                   ? std::make_error_code(
                         std::errc::operation_not_supported)
                   : std::error_code();
      });
  ASSERT_TRUE(static_cast<bool>(Bundle)) << errorText(Bundle.takeError());
  auto Committed = (*Bundle)->commit();
  ASSERT_TRUE(static_cast<bool>(Committed))
      << errorText(Committed.takeError());
  EXPECT_EQ(Committed->State, neverc::OutputBundleState::Committed);
  EXPECT_EQ(Committed->Flags & neverc::OutputDurable, 0U);
  EXPECT_EQ(Committed->Flags & neverc::OutputDurabilityUnconfirmed,
            neverc::OutputDurabilityUnconfirmed);
}

TEST(PluginBinaryImageTest,
     BundleRemovalRollbackRestoresTheOldMainAndSideOutput) {
  TemporaryDirectory Directory;
  const std::string Main = Directory.file("module.ko");
  const std::string Map = Directory.file("module.ko.symbols.json");
  writeFile(Main, "old-main");
  writeFile(Map, "old-map");
  neverc::OutputCoordinator Coordinator;
  std::vector<neverc::OutputBundleFile> Outputs = {
      {"main", Main, {'n', 'e', 'w'}, true},
      {"map", Map, {}, false, false,
       neverc::OutputBundleFileAction::Remove},
  };
  auto Bundle = neverc::OutputBundleTransaction::create(
      Coordinator, Outputs,
      /*IsCancelled=*/{},
      [](neverc::OutputBundleOperation Operation, StringRef) {
        return Operation == neverc::OutputBundleOperation::PublishMain
                   ? std::make_error_code(std::errc::io_error)
                   : std::error_code();
      });
  ASSERT_TRUE(static_cast<bool>(Bundle)) << errorText(Bundle.takeError());
  auto Committed = (*Bundle)->commit();
  EXPECT_FALSE(static_cast<bool>(Committed));
  if (!Committed)
    consumeError(Committed.takeError());
  EXPECT_EQ(readFile(Main), "old-main");
  EXPECT_EQ(readFile(Map), "old-map");
}

TEST(PluginBinaryImageTest,
     LateJournalFailureKeepsNewBundleAndFiresAfterCommit) {
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
  Trace.PatchValue = 0xcc;
  NevercObserverDescriptor Observer{};
  Observer.Header = {sizeof(Observer), NEVERC_PLUGIN_ABI_MAJOR,
                     NEVERC_PLUGIN_ABI_MINOR, 0};
  Observer.Phase = {NEVERC_PHASE_LINK_AFTER_COMMIT_HIGH,
                    NEVERC_PHASE_LINK_AFTER_COMMIT_LOW};
  Observer.Points = NEVERC_OBSERVER_AFTER;
  Observer.Callback = neverc_test_after_commit_observer;
  Observer.UserData = &Trace;
  ASSERT_FALSE((*Pipeline)->addObserver(LinkTestPluginID, Observer));
  auto Output = (*Pipeline)->execute(
      *Image, Main, {}, [](neverc::OutputBundleOperation Operation, StringRef) {
        return Operation == neverc::OutputBundleOperation::CleanupJournal
                   ? std::make_error_code(std::errc::io_error)
                   : std::error_code();
      });
  EXPECT_FALSE(static_cast<bool>(Output));
  if (!Output)
    consumeError(Output.takeError());
  EXPECT_EQ(readFile(Main).size(), 8U);
  EXPECT_EQ((*Image)->state(), NEVERC_BINARY_IMAGE_COMMITTED);
  EXPECT_EQ(Trace.AfterCommitCalls, 1U);
}

TEST(PluginBinaryImageTest,
     BundleRejectsCanonicalPathAliasesBeforePublication) {
  TemporaryDirectory Directory;
  const std::string Main = Directory.file("program.bin");
  SmallString<160> Alias(Directory.Path);
  sys::path::append(Alias, ".", "program.bin");
  neverc::OutputCoordinator Coordinator;
  std::vector<neverc::OutputBundleFile> Outputs = {
      {"main", Main, {'m'}, true}, {"alias", Alias.str().str(), {'s'}, false}};
  auto Bundle = neverc::OutputBundleTransaction::create(Coordinator, Outputs);
  EXPECT_FALSE(static_cast<bool>(Bundle));
  if (!Bundle)
    consumeError(Bundle.takeError());
  EXPECT_FALSE(sys::fs::exists(Main));
}

} // namespace
