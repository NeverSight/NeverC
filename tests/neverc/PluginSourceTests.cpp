#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "neverc/Plugin/Host/FrontendPluginBridge.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "gtest/gtest.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include <array>
#include <memory>
#include <optional>
#include <string>

using namespace llvm;
using namespace neverc;
using namespace neverc::plugin;

namespace {

std::string takeErrorMessage(Error ErrorValue) {
  return toString(std::move(ErrorValue)).str().str();
}

class PluginSourceTest : public testing::Test {
protected:
  void SetUp() override {
    ASSERT_FALSE(Services.interfaces().freeze());
    auto Loaded = Services.registry().load(NEVERC_TEST_MINIMAL_PLUGIN);
    ASSERT_TRUE(static_cast<bool>(Loaded))
        << takeErrorMessage(Loaded.takeError());
    const std::array<StringRef, 1> Selected = {"org.neverc.test.minimal"};
    auto CreatedPlan =
        makePluginActivationPlan(Services.registry(), Selected);
    ASSERT_TRUE(static_cast<bool>(CreatedPlan))
        << takeErrorMessage(CreatedPlan.takeError());
    Plan.emplace(std::move(*CreatedPlan));
    auto CreatedSession = PluginSession::create(Services, *Plan);
    ASSERT_TRUE(static_cast<bool>(CreatedSession))
        << takeErrorMessage(CreatedSession.takeError());
    Session = std::move(*CreatedSession);
    auto CreatedFirstTask =
        Session->createTask(NEVERC_TASK_TRANSLATION_UNIT);
    ASSERT_TRUE(static_cast<bool>(CreatedFirstTask))
        << takeErrorMessage(CreatedFirstTask.takeError());
    FirstTask = std::move(*CreatedFirstTask);
    auto CreatedSecondTask =
        Session->createTask(NEVERC_TASK_TRANSLATION_UNIT);
    ASSERT_TRUE(static_cast<bool>(CreatedSecondTask))
        << takeErrorMessage(CreatedSecondTask.takeError());
    SecondTask = std::move(*CreatedSecondTask);
  }

  void TearDown() override {
    if (SecondTask && !SecondTask->isEnded())
      EXPECT_FALSE(SecondTask->end());
    SecondTask.reset();
    if (FirstTask && !FirstTask->isEnded())
      EXPECT_FALSE(FirstTask->end());
    FirstTask.reset();
    if (Session)
      EXPECT_FALSE(Session->end());
    Session.reset();
    Plan.reset();
    EXPECT_FALSE(Services.shutdown());
  }

  PluginProcessServices Services{"neverc-plugin-source-tests",
                                 LLVM_VERSION_MAJOR};
  std::optional<PluginActivationPlan> Plan;
  std::unique_ptr<PluginSession> Session;
  std::unique_ptr<PluginTaskContext> FirstTask;
  std::unique_ptr<PluginTaskContext> SecondTask;
};

TEST_F(PluginSourceTest, MapsFileLocationsAndRejectsCrossTaskHandles) {
  SourceManagerForFile Source("main.c", "int value;\n");
  SourceManager &SM = Source.get();
  SourceLocation Native =
      SM.getLocForStartOfFile(SM.getMainFileID()).getLocWithOffset(4);
  FrontendPluginBridge FirstBridge(*FirstTask, SM);
  FrontendPluginBridge SecondBridge(*SecondTask, SM);

  auto PublicLocation = FirstBridge.createLocation(Native);
  ASSERT_TRUE(static_cast<bool>(PublicLocation))
      << takeErrorMessage(PublicLocation.takeError());

  NevercSourceLocationInfo Info{};
  Info.Header.StructSize = sizeof(Info);
  const NevercSourceLocationAPI &API = FirstBridge.sourceLocationAPI();
  NevercStatus Status = API.GetLocationInfo(
      API.Context, FirstTask->handle(), *PublicLocation, &Info);
  ASSERT_EQ(Status.Code, NEVERC_STATUS_OK);
  EXPECT_EQ(Info.Header.StructSize, sizeof(Info));
  EXPECT_EQ(Info.Header.Major, NEVERC_SOURCE_LOCATION_API_MAJOR);
  EXPECT_EQ(Info.Kind, NEVERC_SOURCE_LOCATION_FILE);
  EXPECT_EQ(Info.FileOffset, 4U);
  EXPECT_EQ(Info.Line, 1U);
  EXPECT_EQ(Info.Column, 5U);

  NevercSourceLocationInfo WrongScope{};
  WrongScope.Header.StructSize = sizeof(WrongScope);
  Status = SecondBridge.sourceLocationAPI().GetLocationInfo(
      SecondBridge.sourceLocationAPI().Context, SecondTask->handle(),
      *PublicLocation, &WrongScope);
  EXPECT_EQ(Status.Code, NEVERC_STATUS_WRONG_SCOPE);
}

TEST_F(PluginSourceTest, MapsMacroSpellingExpansionAndFileLocations) {
  SourceManagerForFile Source("macro.c", "name use\n");
  SourceManager &SM = Source.get();
  SourceLocation Start = SM.getLocForStartOfFile(SM.getMainFileID());
  SourceLocation Spelling = Start.getLocWithOffset(0);
  SourceLocation ExpansionStart = Start.getLocWithOffset(5);
  SourceLocation ExpansionEnd = Start.getLocWithOffset(7);
  SourceLocation Macro =
      SM.createExpansionLoc(Spelling, ExpansionStart, ExpansionEnd, 4);
  FrontendPluginBridge Bridge(*FirstTask, SM);

  auto PublicMacro = Bridge.createLocation(Macro);
  ASSERT_TRUE(static_cast<bool>(PublicMacro))
      << takeErrorMessage(PublicMacro.takeError());
  const NevercSourceLocationAPI &API = Bridge.sourceLocationAPI();

  NevercSourceLocation PublicSpelling{};
  ASSERT_EQ(API.GetSpellingLocation(API.Context, FirstTask->handle(),
                                    *PublicMacro, &PublicSpelling)
                .Code,
            NEVERC_STATUS_OK);
  NevercSourceLocationInfo SpellingInfo{};
  SpellingInfo.Header.StructSize = sizeof(SpellingInfo);
  ASSERT_EQ(API.GetLocationInfo(API.Context, FirstTask->handle(),
                                PublicSpelling, &SpellingInfo)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(SpellingInfo.Kind, NEVERC_SOURCE_LOCATION_FILE);
  EXPECT_EQ(SpellingInfo.FileOffset, 0U);

  NevercSourceLocation PublicExpansion{};
  ASSERT_EQ(API.GetExpansionLocation(API.Context, FirstTask->handle(),
                                     *PublicMacro, &PublicExpansion)
                .Code,
            NEVERC_STATUS_OK);
  NevercSourceLocationInfo ExpansionInfo{};
  ExpansionInfo.Header.StructSize = sizeof(ExpansionInfo);
  ASSERT_EQ(API.GetLocationInfo(API.Context, FirstTask->handle(),
                                PublicExpansion, &ExpansionInfo)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(ExpansionInfo.Kind, NEVERC_SOURCE_LOCATION_FILE);
  EXPECT_EQ(ExpansionInfo.FileOffset, 5U);

  NevercSourceLocation PublicFile{};
  ASSERT_EQ(API.GetFileLocation(API.Context, FirstTask->handle(), *PublicMacro,
                                &PublicFile)
                .Code,
            NEVERC_STATUS_OK);
  NevercSourceLocationInfo FileInfo{};
  FileInfo.Header.StructSize = sizeof(FileInfo);
  ASSERT_EQ(API.GetLocationInfo(API.Context, FirstTask->handle(), PublicFile,
                                &FileInfo)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(FileInfo.Kind, NEVERC_SOURCE_LOCATION_FILE);
  EXPECT_EQ(FileInfo.FileOffset, 5U);
}

TEST_F(PluginSourceTest, ExposesCharacterRangesAndBorrowedSourceText) {
  SourceManagerForFile Source("range.c", "int value;\n");
  SourceManager &SM = Source.get();
  LangOptions LangOpts;
  FrontendPluginBridge Bridge(*FirstTask, SM, LangOpts);
  SourceLocation Start = SM.getLocForStartOfFile(SM.getMainFileID());
  CharSourceRange NativeRange = CharSourceRange::getCharRange(
      Start.getLocWithOffset(4), Start.getLocWithOffset(9));

  auto PublicRange = Bridge.createRange(NativeRange);
  ASSERT_TRUE(static_cast<bool>(PublicRange))
      << takeErrorMessage(PublicRange.takeError());
  const NevercSourceLocationAPI &API = Bridge.sourceLocationAPI();

  NevercSourceRangeInfo RangeInfo{};
  RangeInfo.Header.StructSize = sizeof(RangeInfo);
  ASSERT_EQ(API.GetRangeInfo(API.Context, FirstTask->handle(), *PublicRange,
                             &RangeInfo)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(RangeInfo.Kind, NEVERC_SOURCE_RANGE_CHARACTER);

  NevercSourceLocationInfo BeginInfo{};
  BeginInfo.Header.StructSize = sizeof(BeginInfo);
  ASSERT_EQ(API.GetLocationInfo(API.Context, FirstTask->handle(),
                                RangeInfo.Begin, &BeginInfo)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(BeginInfo.FileOffset, 4U);

  NevercSourceLocationInfo EndInfo{};
  EndInfo.Header.StructSize = sizeof(EndInfo);
  ASSERT_EQ(API.GetLocationInfo(API.Context, FirstTask->handle(), RangeInfo.End,
                                &EndInfo)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(EndInfo.FileOffset, 9U);

  NevercBufferView Text{};
  Text.Header.StructSize = sizeof(Text);
  ASSERT_EQ(API.GetSourceText(API.Context, FirstTask->handle(), *PublicRange,
                              &Text)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_NE(Text.Data, nullptr);
  EXPECT_EQ(StringRef(reinterpret_cast<const char *>(Text.Data),
                      static_cast<size_t>(Text.Length)),
            "value");
  EXPECT_EQ(Text.NullTerminated, NEVERC_FALSE);
}

TEST_F(PluginSourceTest, ExposesPresumedAndFileMetadata) {
  SourceManagerForFile Source("metadata.c", "int value;\n");
  SourceManager &SM = Source.get();
  FrontendPluginBridge Bridge(*FirstTask, SM);
  SourceLocation Native =
      SM.getLocForStartOfFile(SM.getMainFileID()).getLocWithOffset(4);
  auto PublicLocation = Bridge.createLocation(Native);
  ASSERT_TRUE(static_cast<bool>(PublicLocation))
      << takeErrorMessage(PublicLocation.takeError());
  const NevercSourceLocationAPI &API = Bridge.sourceLocationAPI();

  NevercPresumedLocation Presumed{};
  Presumed.Header.StructSize = sizeof(Presumed);
  ASSERT_EQ(API.GetPresumedLocation(API.Context, FirstTask->handle(),
                                    *PublicLocation, &Presumed)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(StringRef(Presumed.Filename.Data,
                      static_cast<size_t>(Presumed.Filename.Length)),
            "metadata.c");
  EXPECT_EQ(Presumed.Line, 1U);
  EXPECT_EQ(Presumed.Column, 5U);
  EXPECT_TRUE(neverc_handle_is_null(Presumed.IncludeLocation));

  NevercFileHandle File{};
  ASSERT_EQ(API.GetLocationFile(API.Context, FirstTask->handle(),
                                *PublicLocation, &File)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_FALSE(neverc_handle_is_null(File));

  NevercFileInfo FileInfo{};
  FileInfo.Header.StructSize = sizeof(FileInfo);
  ASSERT_EQ(API.GetFileInfo(API.Context, FirstTask->handle(), File, &FileInfo)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(StringRef(FileInfo.Path.Data,
                      static_cast<size_t>(FileInfo.Path.Length)),
            "metadata.c");
  EXPECT_FALSE(StringRef(FileInfo.CanonicalPath.Data,
                         static_cast<size_t>(FileInfo.CanonicalPath.Length))
                   .empty());
  EXPECT_EQ(FileInfo.Size, 11U);
  EXPECT_EQ(FileInfo.ModificationTime, 0);
  EXPECT_EQ(FileInfo.Characteristic, NEVERC_FILE_CHARACTERISTIC_USER);
  EXPECT_EQ(FileInfo.NamedPipe, NEVERC_FALSE);
}

TEST_F(PluginSourceTest, ExposesCharacterDataTokenEndAndBatchInfo) {
  SourceManagerForFile Source("batch.c", "int value;\n");
  SourceManager &SM = Source.get();
  LangOptions LangOpts;
  FrontendPluginBridge Bridge(*FirstTask, SM, LangOpts);
  SourceLocation Start = SM.getLocForStartOfFile(SM.getMainFileID());

  std::array<NevercSourceLocation, 3> Locations{};
  for (size_t Index = 0; Index != Locations.size(); ++Index) {
    const std::array<unsigned, 3> Offsets = {0, 4, 9};
    auto Created = Bridge.createLocation(
        Start.getLocWithOffset(Offsets[Index]));
    ASSERT_TRUE(static_cast<bool>(Created))
        << takeErrorMessage(Created.takeError());
    Locations[Index] = *Created;
  }
  const NevercSourceLocationAPI &API = Bridge.sourceLocationAPI();

  NevercBufferView CharacterData{};
  CharacterData.Header.StructSize = sizeof(CharacterData);
  ASSERT_EQ(API.GetCharacterData(API.Context, FirstTask->handle(),
                                 Locations[1], &CharacterData)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(StringRef(reinterpret_cast<const char *>(CharacterData.Data),
                      static_cast<size_t>(CharacterData.Length)),
            "value;\n");
  EXPECT_EQ(CharacterData.NullTerminated, NEVERC_TRUE);

  NevercSourceLocation TokenEnd{};
  ASSERT_EQ(API.GetTokenEnd(API.Context, FirstTask->handle(), Locations[1],
                            &TokenEnd)
                .Code,
            NEVERC_STATUS_OK);
  NevercSourceLocationInfo TokenEndInfo{};
  TokenEndInfo.Header.StructSize = sizeof(TokenEndInfo);
  ASSERT_EQ(API.GetLocationInfo(API.Context, FirstTask->handle(), TokenEnd,
                                &TokenEndInfo)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(TokenEndInfo.FileOffset, 9U);

  std::array<NevercSourceLocationInfo, 3> Infos{};
  for (NevercSourceLocationInfo &Info : Infos)
    Info.Header.StructSize = sizeof(Info);
  ASSERT_EQ(API.GetLocationInfoBatch(
                API.Context, FirstTask->handle(), Locations.data(),
                Locations.size(), Infos.data(), Infos.size())
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Infos[0].FileOffset, 0U);
  EXPECT_EQ(Infos[1].FileOffset, 4U);
  EXPECT_EQ(Infos[2].FileOffset, 9U);

  EXPECT_EQ(API.GetLocationInfoBatch(
                API.Context, FirstTask->handle(), Locations.data(),
                Locations.size(), Infos.data(), Infos.size() - 1)
                .Code,
            NEVERC_STATUS_INVALID_ARGUMENT);
}

TEST_F(PluginSourceTest, RejectsCrossFileSourceTextRanges) {
  SourceManagerForFile Source("first.c", "first\n");
  SourceManager &SM = Source.get();
  LangOptions LangOpts;
  FrontendPluginBridge Bridge(*FirstTask, SM, LangOpts);
  FileID SecondFile = SM.createFileID(
      MemoryBuffer::getMemBufferCopy("second\n", "second.c"));
  CharSourceRange NativeRange = CharSourceRange::getCharRange(
      SM.getLocForStartOfFile(SM.getMainFileID()),
      SM.getLocForStartOfFile(SecondFile).getLocWithOffset(6));
  auto PublicRange = Bridge.createRange(NativeRange);
  ASSERT_TRUE(static_cast<bool>(PublicRange))
      << takeErrorMessage(PublicRange.takeError());

  NevercBufferView Text{};
  Text.Header.StructSize = sizeof(Text);
  const NevercSourceLocationAPI &API = Bridge.sourceLocationAPI();
  EXPECT_EQ(API.GetSourceText(API.Context, FirstTask->handle(), *PublicRange,
                              &Text)
                .Code,
            NEVERC_STATUS_VERIFICATION_FAILED);
}

TEST_F(PluginSourceTest, ExposesTokenRangesAndIncludeLocations) {
  SourceManagerForFile Source("including.c", "#include \"included.h\"\n");
  SourceManager &SM = Source.get();
  LangOptions LangOpts;
  FrontendPluginBridge Bridge(*FirstTask, SM, LangOpts);
  SourceLocation IncludeLocation =
      SM.getLocForStartOfFile(SM.getMainFileID()).getLocWithOffset(10);
  FileID IncludedFile = SM.createFileID(
      MemoryBuffer::getMemBufferCopy("value\n", "included.h"),
      SrcMgr::C_User, 0, 0, IncludeLocation);
  SourceLocation TokenLocation = SM.getLocForStartOfFile(IncludedFile);

  auto PublicLocation = Bridge.createLocation(TokenLocation);
  ASSERT_TRUE(static_cast<bool>(PublicLocation))
      << takeErrorMessage(PublicLocation.takeError());
  auto PublicRange = Bridge.createRange(
      CharSourceRange::getTokenRange(TokenLocation, TokenLocation));
  ASSERT_TRUE(static_cast<bool>(PublicRange))
      << takeErrorMessage(PublicRange.takeError());
  const NevercSourceLocationAPI &API = Bridge.sourceLocationAPI();

  NevercSourceRangeInfo RangeInfo{};
  RangeInfo.Header.StructSize = sizeof(RangeInfo);
  ASSERT_EQ(API.GetRangeInfo(API.Context, FirstTask->handle(), *PublicRange,
                             &RangeInfo)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(RangeInfo.Kind, NEVERC_SOURCE_RANGE_TOKEN);

  NevercBufferView Text{};
  Text.Header.StructSize = sizeof(Text);
  ASSERT_EQ(API.GetSourceText(API.Context, FirstTask->handle(), *PublicRange,
                              &Text)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(StringRef(reinterpret_cast<const char *>(Text.Data),
                      static_cast<size_t>(Text.Length)),
            "value");

  NevercPresumedLocation Presumed{};
  Presumed.Header.StructSize = sizeof(Presumed);
  ASSERT_EQ(API.GetPresumedLocation(API.Context, FirstTask->handle(),
                                    *PublicLocation, &Presumed)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_FALSE(neverc_handle_is_null(Presumed.IncludeLocation));

  NevercSourceLocation PublicInclude{};
  ASSERT_EQ(API.GetIncludeLocation(API.Context, FirstTask->handle(),
                                   *PublicLocation, &PublicInclude)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_FALSE(neverc_handle_is_null(PublicInclude));
  NevercSourceLocationInfo IncludeInfo{};
  IncludeInfo.Header.StructSize = sizeof(IncludeInfo);
  ASSERT_EQ(API.GetLocationInfo(API.Context, FirstTask->handle(),
                                PublicInclude, &IncludeInfo)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(IncludeInfo.FileOffset, 10U);
}

} // namespace
