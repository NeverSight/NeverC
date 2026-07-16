#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "gtest/gtest.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Error.h"
#include <array>
#include <cstring>
#include <memory>

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
createSession(PluginProcessServices &Services, StringRef PluginID) {
  if (Error ErrorValue = Services.interfaces().freeze()) {
    ADD_FAILURE() << takeErrorMessage(std::move(ErrorValue));
    return nullptr;
  }
  auto Loaded =
      Services.registry().load(NEVERC_TEST_SCOPE_SESSION_PLUGIN);
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

TEST(PluginCancellationTest,
     FirstFatalCancelsTasksAndBlocksUnstartedCallbacks) {
  constexpr const char *PluginID = "org.neverc.test.scope.session";
  PluginProcessServices Services("neverc-plugin-cancellation-tests",
                                 LLVM_VERSION_MAJOR);
  auto Session = createSession(Services, PluginID);
  ASSERT_NE(Session, nullptr);
  auto Task = Session->createTask(NEVERC_TASK_INVOCATION);
  ASSERT_TRUE(static_cast<bool>(Task));

  NevercDiagnosticDescriptor Diagnostic{};
  Diagnostic.Header = {sizeof(Diagnostic), NEVERC_CORE_API_MAJOR,
                       NEVERC_CORE_API_MINOR, 0};
  Diagnostic.Severity = NEVERC_DIAGNOSTIC_FATAL;
  Diagnostic.Code = 1203;
  Diagnostic.PluginID = view(PluginID);
  Diagnostic.PhaseID = view("neverc.driver.execute_job");
  Diagnostic.Message = view("fatal plugin error");
  NevercDiagnosticHandle Handle{};
  NevercStatusCode Cancellation = NEVERC_STATUS_OK;
  auto Result = (*Task)->invokeCallback(
      PluginID, "neverc.driver.execute_job/provider", [&] {
        NevercStatus Status = Services.coreAPI().EmitDiagnostic(
            Services.coreAPI().Context, &Diagnostic, &Handle);
        if (Status.Code != NEVERC_STATUS_OK)
          return Status;
        Cancellation =
            Services.coreAPI()
                .CheckCancelled(Services.coreAPI().Context,
                                (*Task)->handle())
                .Code;
        return neverc_status_ok();
      });
  ASSERT_TRUE(static_cast<bool>(Result));
  EXPECT_EQ(Result->Code, NEVERC_STATUS_OK);
  EXPECT_EQ(Cancellation, NEVERC_STATUS_CANCELLED);
  EXPECT_TRUE(Session->isCancelled());

  bool EnteredAfterCancellation = false;
  auto Skipped = (*Task)->invokeCallback(
      PluginID, "neverc.driver.execute_job/observer", [&] {
        EnteredAfterCancellation = true;
        return neverc_status_ok();
      });
  ASSERT_TRUE(static_cast<bool>(Skipped));
  EXPECT_EQ(Skipped->Code, NEVERC_STATUS_CANCELLED);
  EXPECT_FALSE(EnteredAfterCancellation);
  auto NewTask =
      Session->createTask(NEVERC_TASK_TRANSLATION_UNIT);
  EXPECT_FALSE(static_cast<bool>(NewTask));
  if (!NewTask)
    consumeError(NewTask.takeError());

  auto Records = Session->diagnostics().takeSorted();
  ASSERT_EQ(Records.size(), 1U);
  EXPECT_EQ(Records[0].Severity, NEVERC_DIAGNOSTIC_FATAL);
  EXPECT_FALSE(Records[0].Implicit);

  EXPECT_FALSE((*Task)->end());
  EXPECT_FALSE(Session->end());
  EXPECT_FALSE(Services.shutdown());
}

} // namespace
