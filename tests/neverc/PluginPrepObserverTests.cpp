#include "PluginFrontendTestSupport.h"
#include "neverc/Foundation/Core/FileManager.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/Diagnostic/DiagnosticIDs.h"
#include "neverc/Plugin/Host/FrontendPluginBridge.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "neverc/Scan/MacroRecord.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Scan/PrepObserver.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Error.h"
#include <algorithm>
#include <cstdint>
#include <vector>

using namespace llvm;
using namespace neverc;
using namespace neverc::plugin;
using namespace neverc::test;

namespace {

struct EventCapture {
  const NevercPrepAPI *API = nullptr;
  std::vector<NevercPrepEventKind> Kinds;
  NevercMacroArgumentHandle Arguments{};
  uint32_t ArgumentCount = 0;
};

NevercStatus NEVERC_CALL captureEvent(NevercTaskHandle Task,
                                      const NevercPrepEvent *Event,
                                      void *UserData) {
  if (!Event || !UserData || Event->Header.StructSize < sizeof(*Event) ||
      Event->Kind == 0 || Event->Kind > NEVERC_PREP_EVENT_COUNT) {
    NevercStatus Status = neverc_status_ok();
    Status.Code = NEVERC_STATUS_INVALID_ARGUMENT;
    return Status;
  }
  auto &Capture = *static_cast<EventCapture *>(UserData);
  Capture.Kinds.push_back(Event->Kind);
  if (Event->Kind == NEVERC_PREP_EVENT_MACRO_EXPANDS &&
      !neverc_handle_is_null(Event->Payload.Macro.Arguments)) {
    Capture.Arguments = Event->Payload.Macro.Arguments;
    NevercMacroArgumentInfo Info{};
    Info.Header.StructSize = sizeof(Info);
    NevercStatus Status = Capture.API->GetMacroArgumentInfo(
        Capture.API->Context, Task, Capture.Arguments, &Info);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    Capture.ArgumentCount = Info.ArgumentCount;
  }
  return neverc_status_ok();
}

class PluginPrepObserverTest : public PluginPrepTest {
protected:
  void registerCapture(EventCapture &Capture, NevercPrepEventMask Events) {
    Capture.API = &prepBridge().prepAPI();
    NevercPrepObserverDescriptor Descriptor{};
    Descriptor.Header = {sizeof(Descriptor), NEVERC_PREP_API_MAJOR,
                         NEVERC_PREP_API_MINOR, 0};
    Descriptor.Events = Events;
    Descriptor.Callback = captureEvent;
    Descriptor.UserData = &Capture;
    auto Registered = task().invokeCallback(
        "org.neverc.test.minimal", "RegisterPrepObserver",
        [&] {
          return Capture.API->RegisterEventObserver(
              Capture.API->Context, task().handle(), &Descriptor);
        },
        /*CheckCancellation=*/false);
    ASSERT_TRUE(static_cast<bool>(Registered))
        << toString(Registered.takeError()).str().str();
    ASSERT_EQ(Registered->Code, NEVERC_STATUS_OK);
  }
};

TEST_F(PluginPrepObserverTest,
       DispatchesReadOnlyEventsAndPromotesEventHandlesToTaskScope) {
  EventCapture Capture;
  registerCapture(Capture, NEVERC_PREP_EVENT_MASK_ALL);
  ASSERT_NE(prep().getObserver(), nullptr);

  SourceLocation Start =
      sourceManager().getLocForStartOfFile(sourceManager().getMainFileID());
  prep().getObserver()->FileChanged(Start, PrepObserver::EnterFile,
                                    SrcMgr::C_User, {});
  EXPECT_FALSE(prep().getObserver()->FileNotFound("missing.h"));

  (void)lexAll();
  prep().FiniMainInput();

  auto Saw = [&](NevercPrepEventKind Kind) {
    return llvm::is_contained(Capture.Kinds, Kind);
  };
  EXPECT_TRUE(Saw(NEVERC_PREP_EVENT_FILE_CHANGED));
  EXPECT_TRUE(Saw(NEVERC_PREP_EVENT_FILE_NOT_FOUND));
  EXPECT_TRUE(Saw(NEVERC_PREP_EVENT_MACRO_DEFINED));
  EXPECT_TRUE(Saw(NEVERC_PREP_EVENT_MACRO_EXPANDS));
  EXPECT_TRUE(Saw(NEVERC_PREP_EVENT_END_OF_MAIN_FILE));
  EXPECT_FALSE(neverc_handle_is_null(Capture.Arguments));
  EXPECT_GE(Capture.ArgumentCount, 2U);

  NevercMacroArgumentInfo ArgumentInfo{};
  ArgumentInfo.Header.StructSize = sizeof(ArgumentInfo);
  ASSERT_EQ(Capture.API
                ->GetMacroArgumentInfo(Capture.API->Context, task().handle(),
                                       Capture.Arguments, &ArgumentInfo)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_GE(ArgumentInfo.ArgumentCount, 1U);
  NevercTokenStreamHandle FirstArgument{};
  ASSERT_EQ(
      Capture.API
          ->GetMacroArgumentTokenStream(Capture.API->Context, task().handle(),
                                        Capture.Arguments, 0, &FirstArgument)
          .Code,
      NEVERC_STATUS_OK);
  NevercTokenViewList FirstArgumentView{};
  FirstArgumentView.Header.StructSize = sizeof(FirstArgumentView);
  EXPECT_EQ(Capture.API
                ->GetTokenStreamView(Capture.API->Context, task().handle(),
                                     FirstArgument, &FirstArgumentView)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_GT(FirstArgumentView.Count, 0U);
}

TEST_F(PluginPrepObserverTest, EnforcesRegistrationScopeAndEventMask) {
  EventCapture OutsideCapture;
  OutsideCapture.API = &prepBridge().prepAPI();
  NevercPrepObserverDescriptor Outside{};
  Outside.Header = {sizeof(Outside), NEVERC_PREP_API_MAJOR,
                    NEVERC_PREP_API_MINOR, 0};
  Outside.Events = NEVERC_PREP_EVENT_MASK(NEVERC_PREP_EVENT_END_OF_MAIN_FILE);
  Outside.Callback = captureEvent;
  Outside.UserData = &OutsideCapture;
  EXPECT_EQ(OutsideCapture.API
                ->RegisterEventObserver(OutsideCapture.API->Context,
                                        task().handle(), &Outside)
                .Code,
            NEVERC_STATUS_REENTRANCY_DENIED);

  EventCapture Filtered;
  registerCapture(Filtered,
                  NEVERC_PREP_EVENT_MASK(NEVERC_PREP_EVENT_END_OF_MAIN_FILE));
  ASSERT_NE(prep().getObserver(), nullptr);
  SourceLocation Start =
      sourceManager().getLocForStartOfFile(sourceManager().getMainFileID());
  prep().getObserver()->FileChanged(Start, PrepObserver::EnterFile,
                                    SrcMgr::C_User, {});
  prep().FiniMainInput();
  ASSERT_EQ(Filtered.Kinds.size(), 1U);
  EXPECT_EQ(Filtered.Kinds.front(), NEVERC_PREP_EVENT_END_OF_MAIN_FILE);
}

TEST_F(PluginPrepObserverTest,
       MapsEveryPrepObserverCallbackToDistinctStableEvent) {
  EventCapture Capture;
  registerCapture(Capture, NEVERC_PREP_EVENT_MASK_ALL);
  (void)lexAll();
  Capture.Kinds.clear();

  PrepObserver *Observer = prep().getObserver();
  ASSERT_NE(Observer, nullptr);
  SourceManager &SourceMgr = sourceManager();
  FileID MainFile = SourceMgr.getMainFileID();
  SourceLocation Start = SourceMgr.getLocForStartOfFile(MainFile);
  SourceRange Range(Start, Start.getLocWithOffset(1));
  CharSourceRange CharRange =
      CharSourceRange::getCharRange(Start, Start.getLocWithOffset(1));
  IdentifierInfo *Name = prep().getIdentifierInfo("SUM");
  MacroDefinition Definition = prep().getMacroDefinition(Name);
  ASSERT_TRUE(static_cast<bool>(Definition));
  ASSERT_NE(Definition.getLocalDirective(), nullptr);

  Token NameToken;
  NameToken.startToken();
  NameToken.setKind(tok::identifier);
  NameToken.setIdentifierInfo(Name);
  NameToken.setLocation(Start);
  NameToken.setLength(3);

  auto PhysicalFile =
      SourceMgr.getFileManager().getFileRef(__FILE__, /*OpenFile=*/true);
  ASSERT_TRUE(static_cast<bool>(PhysicalFile))
      << toString(PhysicalFile.takeError()).str().str();
  OptionalFileEntryRef OptionalPhysicalFile(*PhysicalFile);

  Observer->FileChanged(Start, PrepObserver::EnterFile, SrcMgr::C_User, {});
  Observer->LexedFileChanged(MainFile,
                             PrepObserver::LexedFileChangeReason::EnterFile,
                             SrcMgr::C_User, {}, Start);
  Observer->FileSkipped(*PhysicalFile, NameToken, SrcMgr::C_User);
  EXPECT_FALSE(Observer->FileNotFound("missing.h"));
  Observer->InclusionDirective(Start, NameToken, "fixture.h", false, CharRange,
                               OptionalPhysicalFile, "/tmp", "fixture.h",
                               SrcMgr::C_User);
  Observer->EndOfMainFile();
  Observer->Ident(Start, "ident");
  Observer->PragmaDirective(Start, PIK_HashPragma);
  Observer->PragmaComment(Start, Name, "comment");
  Observer->PragmaMark(Start, "mark");
  Observer->PragmaDetectMismatch(Start, "name", "value");
  Observer->PragmaDebug(Start, "debug");
  Observer->PragmaMessage(Start, "GCC", PrepObserver::PMK_Message, "message");
  Observer->PragmaDiagnosticPush(Start, "GCC");
  Observer->PragmaDiagnosticPop(Start, "GCC");
  Observer->PragmaDiagnostic(Start, "GCC", diag::Severity::Warning,
                             "-Wfixture");
  const int WarningIDs[] = {100, 200};
  Observer->PragmaWarning(Start, PrepObserver::PWS_Disable, WarningIDs);
  Observer->PragmaWarningPush(Start, 3);
  Observer->PragmaWarningPop(Start);
  Observer->PragmaExecCharsetPush(Start, "utf-8");
  Observer->PragmaExecCharsetPop(Start);
  Observer->PragmaAssumeNonNullBegin(Start);
  Observer->PragmaAssumeNonNullEnd(Start);
  Observer->MacroExpands(NameToken, Definition, Range, nullptr);
  Observer->MacroDefined(NameToken, Definition.getLocalDirective());
  Observer->MacroUndefined(NameToken, Definition,
                           Definition.getLocalDirective());
  Observer->Defined(NameToken, Definition, Range);
  Observer->HasInclude(Start, "fixture.h", false, OptionalPhysicalFile,
                       SrcMgr::C_User);
  Observer->SourceRangeSkipped(Range, Start);
  Observer->If(Start, Range, PrepObserver::CVK_True);
  Observer->Elif(Start, Range, PrepObserver::CVK_False, Start);
  Observer->Ifdef(Start, NameToken, Definition);
  Observer->Elifdef(Start, NameToken, Definition);
  Observer->Elifdef(Start, Range, Start);
  Observer->Ifndef(Start, NameToken, Definition);
  Observer->Elifndef(Start, NameToken, Definition);
  Observer->Elifndef(Start, Range, Start);
  Observer->Else(Start, Start);
  Observer->Endif(Start, Start);

  std::sort(Capture.Kinds.begin(), Capture.Kinds.end());
  Capture.Kinds.erase(std::unique(Capture.Kinds.begin(), Capture.Kinds.end()),
                      Capture.Kinds.end());
  ASSERT_EQ(Capture.Kinds.size(), NEVERC_PREP_EVENT_COUNT);
  for (uint32_t Index = 0; Index != NEVERC_PREP_EVENT_COUNT; ++Index)
    EXPECT_EQ(Capture.Kinds[Index], Index + 1);
}

} // namespace
