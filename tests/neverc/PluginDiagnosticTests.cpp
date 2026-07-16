#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "gtest/gtest.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Error.h"
#include <array>
#include <atomic>
#include <cstring>
#include <memory>
#include <string>
#include <thread>

using namespace llvm;
using namespace neverc::plugin;

namespace {

NevercStringView view(const char *Text) {
  return {Text, static_cast<uint64_t>(std::strlen(Text))};
}

std::string takeErrorMessage(Error ErrorValue) {
  return toString(std::move(ErrorValue)).str().str();
}

std::unique_ptr<PluginSession>
createSession(PluginProcessServices &Services, StringRef PluginPath,
              StringRef PluginID) {
  if (Error ErrorValue = Services.interfaces().freeze()) {
    ADD_FAILURE() << takeErrorMessage(std::move(ErrorValue));
    return nullptr;
  }
  auto Loaded = Services.registry().load(PluginPath);
  if (!Loaded) {
    ADD_FAILURE() << takeErrorMessage(Loaded.takeError());
    return nullptr;
  }
  const std::array<StringRef, 1> Selected = {PluginID};
  auto Plan = makePluginActivationPlan(Services.registry(), Selected);
  if (!Plan) {
    ADD_FAILURE() << takeErrorMessage(Plan.takeError());
    return nullptr;
  }
  auto Session = PluginSession::create(Services, *Plan);
  if (!Session) {
    ADD_FAILURE() << takeErrorMessage(Session.takeError());
    return nullptr;
  }
  return std::move(*Session);
}

NevercDiagnosticDescriptor
descriptor(NevercDiagnosticSeverity Severity, uint32_t Code,
           const char *PluginID, const char *PhaseID,
           const char *Message) {
  NevercDiagnosticDescriptor Result{};
  Result.Header = {sizeof(Result), NEVERC_CORE_API_MAJOR,
                   NEVERC_CORE_API_MINOR, 0};
  Result.Severity = Severity;
  Result.Code = Code;
  Result.PluginID = view(PluginID);
  Result.PhaseID = view(PhaseID);
  Result.Message = view(Message);
  return Result;
}

TEST(PluginDiagnosticTest, PreservesStructuredWarningAndNotes) {
  constexpr const char *PluginID = "org.neverc.test.scope.session";
  PluginProcessServices Services("neverc-plugin-diagnostic-tests",
                                 LLVM_VERSION_MAJOR);
  auto Session = createSession(
      Services, NEVERC_TEST_SCOPE_SESSION_PLUGIN, PluginID);
  ASSERT_NE(Session, nullptr);
  auto Task = Session->createTask(NEVERC_TASK_INVOCATION);
  ASSERT_TRUE(static_cast<bool>(Task))
      << takeErrorMessage(Task.takeError());

  char Message[] = "structured warning";
  char FirstNote[] = "first note";
  char SecondNote[] = "second note";
  NevercDiagnosticNote Notes[2]{};
  Notes[0].Header = {sizeof(NevercDiagnosticNote),
                     NEVERC_CORE_API_MAJOR, NEVERC_CORE_API_MINOR, 0};
  Notes[0].Message = view(FirstNote);
  Notes[1].Header = {sizeof(NevercDiagnosticNote),
                     NEVERC_CORE_API_MAJOR, NEVERC_CORE_API_MINOR, 0};
  Notes[1].Message = view(SecondNote);
  NevercDiagnosticDescriptor Diagnostic = descriptor(
      NEVERC_DIAGNOSTIC_WARNING, 1201, PluginID,
      "neverc.driver.raw_arguments", Message);
  Diagnostic.Notes = {Notes, 2, sizeof(NevercDiagnosticNote)};
  NevercDiagnosticHandle Handle{};
  auto Result = (*Task)->invokeCallback(
      PluginID, "neverc.driver.raw_arguments/provider", [&] {
        return Services.coreAPI().EmitDiagnostic(
            Services.coreAPI().Context, &Diagnostic, &Handle);
      });
  ASSERT_TRUE(static_cast<bool>(Result));
  EXPECT_EQ(Result->Code, NEVERC_STATUS_OK);
  EXPECT_EQ(Handle.Owner, Session->handle().Owner);
  EXPECT_NE(Handle.Value, 0U);
  Message[0] = 'X';
  FirstNote[0] = 'X';
  SecondNote[0] = 'X';

  auto Records = Session->diagnostics().takeSorted();
  ASSERT_EQ(Records.size(), 1U);
  EXPECT_EQ(Records[0].Severity, NEVERC_DIAGNOSTIC_WARNING);
  EXPECT_EQ(Records[0].Code, 1201U);
  EXPECT_EQ(Records[0].PluginID, PluginID);
  EXPECT_EQ(Records[0].PhaseID, "neverc.driver.raw_arguments");
  EXPECT_EQ(Records[0].Message, "structured warning");
  EXPECT_EQ(Records[0].Notes,
            (std::vector<std::string>{"first note", "second note"}));
  EXPECT_FALSE(Records[0].Implicit);
  EXPECT_FALSE(Session->isCancelled());

  EXPECT_FALSE((*Task)->end());
  EXPECT_FALSE(Session->end());
  EXPECT_FALSE(Services.shutdown());
}

TEST(PluginDiagnosticTest, RejectsDiagnosticForAnotherPlugin) {
  constexpr const char *PluginID = "org.neverc.test.scope.session";
  PluginProcessServices Services("neverc-plugin-diagnostic-tests",
                                 LLVM_VERSION_MAJOR);
  auto Session = createSession(
      Services, NEVERC_TEST_SCOPE_SESSION_PLUGIN, PluginID);
  ASSERT_NE(Session, nullptr);

  NevercDiagnosticDescriptor Diagnostic = descriptor(
      NEVERC_DIAGNOSTIC_ERROR, 1202, "org.neverc.test.other",
      "neverc.driver.job_graph", "spoofed diagnostic");
  NevercDiagnosticHandle Handle{};
  auto Result = Session->invokeCallback(
      PluginID, "neverc.driver.job_graph/provider", [&] {
        NevercStatus Status = Services.coreAPI().EmitDiagnostic(
            Services.coreAPI().Context, &Diagnostic, &Handle);
        EXPECT_EQ(Status.Code, NEVERC_STATUS_WRONG_SCOPE);
        return neverc_status_ok();
      });
  ASSERT_TRUE(static_cast<bool>(Result));
  EXPECT_EQ(Result->Code, NEVERC_STATUS_OK);
  EXPECT_TRUE(Session->diagnostics().takeSorted().empty());

  EXPECT_FALSE(Session->end());
  EXPECT_FALSE(Services.shutdown());
}

TEST(PluginDiagnosticTest, SynthesizesImplicitErrorAndCancelsSession) {
  constexpr const char *PluginID = "org.neverc.test.scope.session";
  PluginProcessServices Services("neverc-plugin-diagnostic-tests",
                                 LLVM_VERSION_MAJOR);
  auto Session = createSession(
      Services, NEVERC_TEST_SCOPE_SESSION_PLUGIN, PluginID);
  ASSERT_NE(Session, nullptr);

  auto Result = Session->invokeCallback(
      PluginID, "neverc.driver.action_graph/interceptor", [] {
        NevercStatus Status = neverc_status_ok();
        Status.Code = NEVERC_STATUS_PLUGIN_FAILURE;
        return Status;
      });
  ASSERT_TRUE(static_cast<bool>(Result));
  EXPECT_EQ(Result->Code, NEVERC_STATUS_PLUGIN_FAILURE);
  EXPECT_TRUE(Session->isCancelled());

  auto Records = Session->diagnostics().takeSorted();
  ASSERT_EQ(Records.size(), 1U);
  EXPECT_TRUE(Records[0].Implicit);
  EXPECT_EQ(Records[0].Severity, NEVERC_DIAGNOSTIC_ERROR);
  EXPECT_EQ(Records[0].PluginID, PluginID);
  EXPECT_EQ(Records[0].PhaseID,
            "neverc.driver.action_graph/interceptor");
  EXPECT_NE(Records[0].Message.find("without a structured diagnostic"),
            std::string::npos);

  EXPECT_FALSE(Session->end());
  EXPECT_FALSE(Services.shutdown());
}

TEST(PluginDiagnosticTest, RejectsDetailTokenFromEarlierCallback) {
  constexpr const char *PluginID = "org.neverc.test.scope.session";
  PluginProcessServices Services("neverc-plugin-diagnostic-tests",
                                 LLVM_VERSION_MAJOR);
  auto Session = createSession(
      Services, NEVERC_TEST_SCOPE_SESSION_PLUGIN, PluginID);
  ASSERT_NE(Session, nullptr);

  NevercDiagnosticDescriptor Diagnostic = descriptor(
      NEVERC_DIAGNOSTIC_ERROR, 1205, PluginID,
      "neverc.driver.action_graph", "earlier callback error");
  NevercDiagnosticHandle Earlier{};
  auto Emitted = Session->invokeCallback(
      PluginID, "neverc.driver.action_graph/observer", [&] {
        return Services.coreAPI().EmitDiagnostic(
            Services.coreAPI().Context, &Diagnostic, &Earlier);
      });
  ASSERT_TRUE(static_cast<bool>(Emitted));
  ASSERT_EQ(Emitted->Code, NEVERC_STATUS_OK);

  auto Failed = Session->invokeCallback(
      PluginID, "neverc.driver.action_graph/interceptor", [&] {
        NevercStatus Status = neverc_status_ok();
        Status.Code = NEVERC_STATUS_PLUGIN_FAILURE;
        Status.Detail = Earlier.Value;
        return Status;
      });
  ASSERT_TRUE(static_cast<bool>(Failed));
  EXPECT_EQ(Failed->Code, NEVERC_STATUS_PLUGIN_FAILURE);
  EXPECT_TRUE(Session->isCancelled());

  auto Records = Session->diagnostics().takeSorted();
  ASSERT_EQ(Records.size(), 2U);
  EXPECT_FALSE(Records[0].Implicit);
  EXPECT_TRUE(Records[1].Implicit);

  EXPECT_FALSE(Session->end());
  EXPECT_FALSE(Services.shutdown());
}

TEST(PluginDiagnosticTest, AcceptsCurrentCallbackErrorDetail) {
  constexpr const char *PluginID = "org.neverc.test.scope.session";
  PluginProcessServices Services("neverc-plugin-diagnostic-tests",
                                 LLVM_VERSION_MAJOR);
  auto Session = createSession(
      Services, NEVERC_TEST_SCOPE_SESSION_PLUGIN, PluginID);
  ASSERT_NE(Session, nullptr);

  NevercDiagnosticDescriptor Diagnostic = descriptor(
      NEVERC_DIAGNOSTIC_ERROR, 1206, PluginID,
      "neverc.driver.job_graph", "reported callback error");
  auto Failed = Session->invokeCallback(
      PluginID, "neverc.driver.job_graph/interceptor", [&] {
        NevercDiagnosticHandle Handle{};
        NevercStatus Status = Services.coreAPI().EmitDiagnostic(
            Services.coreAPI().Context, &Diagnostic, &Handle);
        if (Status.Code == NEVERC_STATUS_OK) {
          Status.Code = NEVERC_STATUS_PLUGIN_FAILURE;
          Status.Detail = Handle.Value;
        }
        return Status;
      });
  ASSERT_TRUE(static_cast<bool>(Failed));
  EXPECT_EQ(Failed->Code, NEVERC_STATUS_PLUGIN_FAILURE);
  EXPECT_TRUE(Session->isCancelled());

  auto Records = Session->diagnostics().takeSorted();
  ASSERT_EQ(Records.size(), 1U);
  EXPECT_FALSE(Records[0].Implicit);
  EXPECT_EQ(Records[0].Message, "reported callback error");

  EXPECT_FALSE(Session->end());
  EXPECT_FALSE(Services.shutdown());
}

TEST(PluginDiagnosticTest, DiscardsDiagnosticsFromRecoverableFallback) {
  constexpr const char *PluginID = "org.neverc.test.scope.session";
  PluginProcessServices Services("neverc-plugin-diagnostic-tests",
                                 LLVM_VERSION_MAJOR);
  auto Session = createSession(
      Services, NEVERC_TEST_SCOPE_SESSION_PLUGIN, PluginID);
  ASSERT_NE(Session, nullptr);

  NevercDiagnosticDescriptor Diagnostic = descriptor(
      NEVERC_DIAGNOSTIC_WARNING, 1204, PluginID,
      "neverc.driver.select_toolchain", "discarded fallback warning");
  NevercDiagnosticHandle Handle{};
  uint64_t DiagnosticTransactionID = 0;
  auto Result = Session->invokeCallback(
      PluginID, "neverc.driver.select_toolchain/provider", [&] {
        NevercStatus Status = Services.coreAPI().EmitDiagnostic(
            Services.coreAPI().Context, &Diagnostic, &Handle);
        if (Status.Code != NEVERC_STATUS_OK)
          return Status;
        Status.Code = NEVERC_STATUS_CAPABILITY_UNAVAILABLE;
        Status.Flags = NEVERC_STATUS_FLAG_RECOVERABLE;
        return Status;
      },
      true, nullptr, &DiagnosticTransactionID, true);
  ASSERT_TRUE(static_cast<bool>(Result));
  EXPECT_EQ(Result->Code, NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
  EXPECT_EQ(Session->diagnostics().messageForDetail(Handle.Value),
            "discarded fallback warning");
  Session->diagnostics().discardTransaction(
      DiagnosticTransactionID);
  EXPECT_TRUE(Session->diagnostics().takeSorted().empty());
  EXPECT_FALSE(Session->isCancelled());

  EXPECT_FALSE(Session->end());
  EXPECT_FALSE(Services.shutdown());
}

TEST(PluginDiagnosticTest, SortsConcurrentRecordsByTaskAndPhase) {
  constexpr const char *PluginID =
      "org.neverc.test.scope.thread-safe";
  PluginProcessServices Services("neverc-plugin-diagnostic-tests",
                                 LLVM_VERSION_MAJOR);
  auto Session = createSession(
      Services, NEVERC_TEST_SCOPE_THREAD_SAFE_PLUGIN, PluginID);
  ASSERT_NE(Session, nullptr);
  auto FirstTask = Session->createTask(NEVERC_TASK_TRANSLATION_UNIT);
  auto SecondTask = Session->createTask(NEVERC_TASK_TRANSLATION_UNIT);
  ASSERT_TRUE(static_cast<bool>(FirstTask));
  ASSERT_TRUE(static_cast<bool>(SecondTask));

  NevercDiagnosticDescriptor FirstLate = descriptor(
      NEVERC_DIAGNOSTIC_WARNING, 1, PluginID,
      "neverc.driver.job_graph", "first task later phase");
  NevercDiagnosticDescriptor FirstEarly = descriptor(
      NEVERC_DIAGNOSTIC_WARNING, 2, PluginID,
      "neverc.driver.raw_arguments", "first task earlier phase");
  NevercDiagnosticDescriptor Second = descriptor(
      NEVERC_DIAGNOSTIC_WARNING, 3, PluginID,
      "neverc.driver.raw_arguments", "second task");
  std::atomic<bool> FirstEntered{false};
  std::atomic<bool> SecondEmitted{false};
  NevercStatus FirstStatus = neverc_status_ok();
  NevercStatus SecondStatus = neverc_status_ok();

  std::thread FirstThread([&] {
    auto Result = (*FirstTask)->invokeCallback(
        PluginID, "concurrent-first", [&] {
          FirstEntered.store(true, std::memory_order_release);
          while (!SecondEmitted.load(std::memory_order_acquire))
            std::this_thread::yield();
          NevercDiagnosticHandle Handle{};
          NevercStatus Status = Services.coreAPI().EmitDiagnostic(
              Services.coreAPI().Context, &FirstLate, &Handle);
          if (Status.Code != NEVERC_STATUS_OK)
            return Status;
          return Services.coreAPI().EmitDiagnostic(
              Services.coreAPI().Context, &FirstEarly, &Handle);
        });
    if (Result)
      FirstStatus = *Result;
    else
      FirstStatus.Code = NEVERC_STATUS_PLUGIN_FAILURE;
  });
  while (!FirstEntered.load(std::memory_order_acquire))
    std::this_thread::yield();
  std::thread SecondThread([&] {
    auto Result = (*SecondTask)->invokeCallback(
        PluginID, "concurrent-second", [&] {
          NevercDiagnosticHandle Handle{};
          NevercStatus Status = Services.coreAPI().EmitDiagnostic(
              Services.coreAPI().Context, &Second, &Handle);
          SecondEmitted.store(true, std::memory_order_release);
          return Status;
        });
    if (Result)
      SecondStatus = *Result;
    else
      SecondStatus.Code = NEVERC_STATUS_PLUGIN_FAILURE;
  });
  FirstThread.join();
  SecondThread.join();
  ASSERT_EQ(FirstStatus.Code, NEVERC_STATUS_OK);
  ASSERT_EQ(SecondStatus.Code, NEVERC_STATUS_OK);

  auto Records = Session->diagnostics().takeSorted();
  ASSERT_EQ(Records.size(), 3U);
  EXPECT_EQ(Records[0].Message, "first task earlier phase");
  EXPECT_EQ(Records[1].Message, "first task later phase");
  EXPECT_EQ(Records[2].Message, "second task");
  EXPECT_LT(Records[0].TaskOrder, Records[2].TaskOrder);
  EXPECT_LT(Records[0].PhaseOrder, Records[1].PhaseOrder);

  EXPECT_FALSE((*SecondTask)->end());
  EXPECT_FALSE((*FirstTask)->end());
  EXPECT_FALSE(Session->end());
  EXPECT_FALSE(Services.shutdown());
}

} // namespace
