#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Error.h"
#include "gtest/gtest.h"
#include <array>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

using namespace llvm;
using namespace neverc::plugin;

namespace {

std::string takeErrorMessage(Error ErrorValue) {
  auto Message = toString(std::move(ErrorValue));
  return Message.str().str();
}

NevercStringView view(const char *Text) {
  return {Text, static_cast<uint64_t>(std::strlen(Text))};
}

void loadPlugin(PluginProcessServices &Services, StringRef Path) {
  auto Loaded = Services.registry().load(Path);
  if (!Loaded)
    ADD_FAILURE() << takeErrorMessage(Loaded.takeError());
}

class FailingTaskEndHostService final : public PluginHostService {
public:
  Error taskScopeEnding(NevercTaskHandle) override {
    ++Calls;
    return createStringError(inconvertibleErrorCode(),
                             "injected task cleanup failure");
  }

  unsigned Calls = 0;
};

TEST(PluginSessionTest, IsolatesTopLevelChildAndTaskState) {
  PluginProcessServices Services("neverc-plugin-session-tests",
                                 LLVM_VERSION_MAJOR);
  ASSERT_FALSE(Services.interfaces().freeze());
  loadPlugin(Services, NEVERC_TEST_SCOPE_SESSION_PLUGIN);

  {
    const std::array<StringRef, 1> Selected = {"org.neverc.test.scope.session"};
    auto Plan = makePluginActivationPlan(Services.registry(), Selected);
    ASSERT_TRUE(static_cast<bool>(Plan)) << takeErrorMessage(Plan.takeError());

    auto First = PluginSession::create(Services, *Plan);
    ASSERT_TRUE(static_cast<bool>(First))
        << takeErrorMessage(First.takeError());
    auto Second = PluginSession::create(Services, *Plan);
    ASSERT_TRUE(static_cast<bool>(Second))
        << takeErrorMessage(Second.takeError());
    auto Child = (*First)->createChild();
    ASSERT_TRUE(static_cast<bool>(Child))
        << takeErrorMessage(Child.takeError());

    EXPECT_NE((*First)->handle().Owner, (*Second)->handle().Owner);
    EXPECT_NE((*First)->handle().Owner, (*Child)->handle().Owner);
    EXPECT_EQ((*First)->registryGeneration(), (*Child)->registryGeneration());
    EXPECT_EQ(Services.registry().activeSessions(), 3U);

    void *FirstState = nullptr;
    void *SecondState = nullptr;
    void *ChildState = nullptr;
    ASSERT_EQ(
        (*First)->queryState("org.neverc.test.scope.session", &FirstState).Code,
        NEVERC_STATUS_OK);
    ASSERT_EQ((*Second)
                  ->queryState("org.neverc.test.scope.session", &SecondState)
                  .Code,
              NEVERC_STATUS_OK);
    ASSERT_EQ(
        (*Child)->queryState("org.neverc.test.scope.session", &ChildState).Code,
        NEVERC_STATUS_OK);
    EXPECT_NE(FirstState, nullptr);
    EXPECT_NE(SecondState, nullptr);
    EXPECT_NE(ChildState, nullptr);
    EXPECT_NE(FirstState, SecondState);
    EXPECT_NE(FirstState, ChildState);

    auto FirstTask = (*First)->createTask(NEVERC_TASK_TRANSLATION_UNIT);
    ASSERT_TRUE(static_cast<bool>(FirstTask))
        << takeErrorMessage(FirstTask.takeError());
    auto SecondTask = (*Second)->createTask(NEVERC_TASK_TRANSLATION_UNIT);
    ASSERT_TRUE(static_cast<bool>(SecondTask))
        << takeErrorMessage(SecondTask.takeError());

    void *FirstTaskState = nullptr;
    void *SecondTaskState = nullptr;
    ASSERT_EQ((*FirstTask)
                  ->queryState("org.neverc.test.scope.session", &FirstTaskState)
                  .Code,
              NEVERC_STATUS_OK);
    ASSERT_EQ(
        (*SecondTask)
            ->queryState("org.neverc.test.scope.session", &SecondTaskState)
            .Code,
        NEVERC_STATUS_OK);
    EXPECT_NE(FirstTaskState, nullptr);
    EXPECT_NE(SecondTaskState, nullptr);
    EXPECT_NE(FirstTaskState, SecondTaskState);

    void *OutsideState = nullptr;
    EXPECT_EQ(
        Services.coreAPI()
            .GetTaskState(Services.coreAPI().Context, (*FirstTask)->handle(),
                          view("org.neverc.test.scope.session"), &OutsideState)
            .Code,
        NEVERC_STATUS_INVALID_STATE);
    NevercStatusCode SessionLookup = NEVERC_STATUS_INVALID_STATE;
    NevercStatusCode TaskLookup = NEVERC_STATUS_INVALID_STATE;
    NevercStatusCode CancelLookup = NEVERC_STATUS_INVALID_STATE;
    NevercStatusCode WrongSessionLookup = NEVERC_STATUS_OK;
    NevercStatusCode WrongTaskLookup = NEVERC_STATUS_OK;
    auto Lookup =
        (*FirstTask)
            ->invokeCallback(
                "org.neverc.test.scope.session", "state-lookup", [&] {
                  void *CallbackSessionState = nullptr;
                  void *CallbackTaskState = nullptr;
                  void *WrongState = nullptr;
                  SessionLookup =
                      Services.coreAPI()
                          .GetSessionState(
                              Services.coreAPI().Context, (*First)->handle(),
                              view("org.neverc.test.scope.session"),
                              &CallbackSessionState)
                          .Code;
                  TaskLookup =
                      Services.coreAPI()
                          .GetTaskState(Services.coreAPI().Context,
                                        (*FirstTask)->handle(),
                                        view("org.neverc.test.scope.session"),
                                        &CallbackTaskState)
                          .Code;
                  CancelLookup = Services.coreAPI()
                                     .CheckCancelled(Services.coreAPI().Context,
                                                     (*FirstTask)->handle())
                                     .Code;
                  WrongSessionLookup =
                      Services.coreAPI()
                          .GetSessionState(
                              Services.coreAPI().Context, (*Second)->handle(),
                              view("org.neverc.test.scope.session"),
                              &WrongState)
                          .Code;
                  WrongTaskLookup =
                      Services.coreAPI()
                          .GetTaskState(Services.coreAPI().Context,
                                        (*SecondTask)->handle(),
                                        view("org.neverc.test.scope.session"),
                                        &WrongState)
                          .Code;
                  EXPECT_EQ(CallbackSessionState, FirstState);
                  EXPECT_EQ(CallbackTaskState, FirstTaskState);
                  return neverc_status_ok();
                });
    ASSERT_TRUE(static_cast<bool>(Lookup));
    EXPECT_EQ(Lookup->Code, NEVERC_STATUS_OK);
    EXPECT_EQ(SessionLookup, NEVERC_STATUS_OK);
    EXPECT_EQ(TaskLookup, NEVERC_STATUS_OK);
    EXPECT_EQ(CancelLookup, NEVERC_STATUS_OK);
    EXPECT_EQ(WrongSessionLookup, NEVERC_STATUS_WRONG_SESSION);
    EXPECT_EQ(WrongTaskLookup, NEVERC_STATUS_WRONG_SCOPE);

    (*First)->cancel();
    EXPECT_TRUE((*First)->isCancelled());
    EXPECT_FALSE((*Child)->isCancelled());

    NevercTaskHandle EndedTask = (*FirstTask)->handle();
    EXPECT_FALSE((*FirstTask)->end());
    EXPECT_EQ(Services.coreAPI()
                  .CheckCancelled(Services.coreAPI().Context, EndedTask)
                  .Code,
              NEVERC_STATUS_STALE_HANDLE);
    EXPECT_FALSE((*SecondTask)->end());
    EXPECT_FALSE((*Child)->end());
    EXPECT_FALSE((*Second)->end());
    NevercSessionHandle EndedSession = (*First)->handle();
    EXPECT_FALSE((*First)->end());
    void *StaleState = nullptr;
    EXPECT_EQ(Services.coreAPI()
                  .GetSessionState(Services.coreAPI().Context, EndedSession,
                                   view("org.neverc.test.scope.session"),
                                   &StaleState)
                  .Code,
              NEVERC_STATUS_STALE_HANDLE);
    EXPECT_EQ(Services.registry().activeSessions(), 0U);
  }

  EXPECT_FALSE(Services.shutdown());
}

TEST(PluginSessionTest, RefusesSessionEndWhileTasksAreActive) {
  PluginProcessServices Services("neverc-plugin-session-tests",
                                 LLVM_VERSION_MAJOR);
  ASSERT_FALSE(Services.interfaces().freeze());
  loadPlugin(Services, NEVERC_TEST_SCOPE_SESSION_PLUGIN);

  {
    const std::array<StringRef, 1> Selected = {"org.neverc.test.scope.session"};
    auto Plan = makePluginActivationPlan(Services.registry(), Selected);
    ASSERT_TRUE(static_cast<bool>(Plan));
    auto Session = PluginSession::create(Services, *Plan);
    ASSERT_TRUE(static_cast<bool>(Session));
    auto Task = (*Session)->createTask(NEVERC_TASK_INVOCATION);
    ASSERT_TRUE(static_cast<bool>(Task));
    auto ChildTask =
        (*Session)->createTask(NEVERC_TASK_TRANSLATION_UNIT, Task->get());
    ASSERT_TRUE(static_cast<bool>(ChildTask));
    EXPECT_EQ((*Task)->activeChildCount(), 1U);

    Error Busy = (*Session)->end();
    ASSERT_TRUE(static_cast<bool>(Busy));
    EXPECT_NE(takeErrorMessage(std::move(Busy)).find("active task"),
              std::string::npos);
    Error ParentBusy = (*Task)->end();
    ASSERT_TRUE(static_cast<bool>(ParentBusy));
    EXPECT_NE(takeErrorMessage(std::move(ParentBusy)).find("child task"),
              std::string::npos);
    EXPECT_FALSE((*ChildTask)->end());
    EXPECT_FALSE((*Task)->end());
    EXPECT_FALSE((*Session)->end());
  }

  EXPECT_FALSE(Services.shutdown());
}

TEST(PluginSessionTest, SessionDestructorFailsFastWithLiveTask) {
  EXPECT_DEATH_IF_SUPPORTED(
      ([] {
        PluginProcessServices Services("neverc-plugin-session-death-test",
                                       LLVM_VERSION_MAJOR);
        cantFail(Services.interfaces().freeze());
        auto Loaded = cantFail(
            Services.registry().load(NEVERC_TEST_SCOPE_SESSION_PLUGIN));
        (void)Loaded;
        const std::array<StringRef, 1> Selected = {
            "org.neverc.test.scope.session"};
        auto Plan =
            cantFail(makePluginActivationPlan(Services.registry(), Selected));
        auto Session = cantFail(PluginSession::create(Services, Plan));
        auto Task = cantFail(Session->createTask(NEVERC_TASK_INVOCATION));
        (void)Task;
        Session.reset();
        std::_Exit(0);
      }()),
      "PluginSession.*active task");
}

TEST(PluginSessionTest, ParentTaskDestructorFailsFastWithLiveChild) {
  EXPECT_DEATH_IF_SUPPORTED(
      ([] {
        PluginProcessServices Services("neverc-plugin-task-death-test",
                                       LLVM_VERSION_MAJOR);
        cantFail(Services.interfaces().freeze());
        auto Loaded = cantFail(
            Services.registry().load(NEVERC_TEST_SCOPE_SESSION_PLUGIN));
        (void)Loaded;
        const std::array<StringRef, 1> Selected = {
            "org.neverc.test.scope.session"};
        auto Plan =
            cantFail(makePluginActivationPlan(Services.registry(), Selected));
        auto Session = cantFail(PluginSession::create(Services, Plan));
        auto Parent = cantFail(Session->createTask(NEVERC_TASK_INVOCATION));
        auto Child = cantFail(
            Session->createTask(NEVERC_TASK_TRANSLATION_UNIT, Parent.get()));
        (void)Child;
        Parent.reset();
        std::_Exit(0);
      }()),
      "PluginTaskContext.*child task");
}

TEST(PluginSessionTest, TaskDestructorFailsFastInsideActiveCallback) {
  EXPECT_DEATH_IF_SUPPORTED(
      ([] {
        PluginProcessServices Services("neverc-plugin-callback-death-test",
                                       LLVM_VERSION_MAJOR);
        cantFail(Services.interfaces().freeze());
        auto Loaded = cantFail(
            Services.registry().load(NEVERC_TEST_SCOPE_SESSION_PLUGIN));
        (void)Loaded;
        const std::array<StringRef, 1> Selected = {
            "org.neverc.test.scope.session"};
        auto Plan =
            cantFail(makePluginActivationPlan(Services.registry(), Selected));
        auto Session = cantFail(PluginSession::create(Services, Plan));
        auto Task = cantFail(Session->createTask(NEVERC_TASK_INVOCATION));
        auto Result = Task->invokeCallback("org.neverc.test.scope.session",
                                           "destroy-active-task", [&] {
                                             Task.reset();
                                             std::_Exit(0);
                                             return neverc_status_ok();
                                           });
        if (!Result)
          consumeError(Result.takeError());
        std::_Exit(0);
      }()),
      "PluginTaskContext.*callback");
}

TEST(PluginSessionTest, TaskDestructorConsumesCleanupErrorAfterCompletingEnd) {
  PluginProcessServices Services("neverc-plugin-task-cleanup-test",
                                 LLVM_VERSION_MAJOR);
  ASSERT_FALSE(Services.interfaces().freeze());
  loadPlugin(Services, NEVERC_TEST_SCOPE_SESSION_PLUGIN);
  auto FailingEnd = std::make_shared<FailingTaskEndHostService>();
  ASSERT_FALSE(Services.registerHostService({0xdecafbad, 0x51}, FailingEnd));

  {
    const std::array<StringRef, 1> Selected = {"org.neverc.test.scope.session"};
    auto Plan = makePluginActivationPlan(Services.registry(), Selected);
    ASSERT_TRUE(static_cast<bool>(Plan));
    auto Session = PluginSession::create(Services, *Plan);
    ASSERT_TRUE(static_cast<bool>(Session));
    {
      auto Task = (*Session)->createTask(NEVERC_TASK_INVOCATION);
      ASSERT_TRUE(static_cast<bool>(Task));
    }

    EXPECT_EQ(FailingEnd->Calls, 1U);
    EXPECT_EQ((*Session)->activeTaskCount(), 0U);
    EXPECT_FALSE((*Session)->end());
  }
  EXPECT_FALSE(Services.shutdown());
}

TEST(PluginSessionTest, RollsBackSuccessfulSessionAndTaskBeginPrefixes) {
  PluginProcessServices Services("neverc-plugin-session-tests",
                                 LLVM_VERSION_MAJOR);
  ASSERT_FALSE(Services.interfaces().freeze());
  loadPlugin(Services, NEVERC_TEST_SCOPE_SESSION_PLUGIN);
  loadPlugin(Services, NEVERC_TEST_SESSION_FAILURE_PLUGIN);

  {
    const std::array<StringRef, 2> Selected = {
        "org.neverc.test.scope.session-failure",
        "org.neverc.test.scope.session"};
    auto Plan = makePluginActivationPlan(Services.registry(), Selected);
    ASSERT_TRUE(static_cast<bool>(Plan));
    auto Session = PluginSession::create(Services, *Plan);
    ASSERT_FALSE(static_cast<bool>(Session));
    EXPECT_NE(takeErrorMessage(Session.takeError()).find("SessionBegin"),
              std::string::npos);
    EXPECT_EQ(Services.registry().activeSessions(), 0U);
  }

  EXPECT_FALSE(Services.shutdown());

  PluginProcessServices TaskServices("neverc-plugin-task-tests",
                                     LLVM_VERSION_MAJOR);
  ASSERT_FALSE(TaskServices.interfaces().freeze());
  loadPlugin(TaskServices, NEVERC_TEST_SCOPE_SESSION_PLUGIN);
  loadPlugin(TaskServices, NEVERC_TEST_TASK_FAILURE_PLUGIN);

  {
    const std::array<StringRef, 2> Selected = {
        "org.neverc.test.scope.task-failure", "org.neverc.test.scope.session"};
    auto Plan = makePluginActivationPlan(TaskServices.registry(), Selected);
    ASSERT_TRUE(static_cast<bool>(Plan));
    auto Session = PluginSession::create(TaskServices, *Plan);
    ASSERT_TRUE(static_cast<bool>(Session));
    auto Task = (*Session)->createTask(NEVERC_TASK_CODEGEN);
    ASSERT_FALSE(static_cast<bool>(Task));
    EXPECT_NE(takeErrorMessage(Task.takeError()).find("TaskBegin"),
              std::string::npos);
    EXPECT_EQ((*Session)->activeTaskCount(), 0U);
    EXPECT_FALSE((*Session)->end());
  }

  EXPECT_FALSE(TaskServices.shutdown());
}

} // namespace
