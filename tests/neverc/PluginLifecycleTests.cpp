#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "gtest/gtest.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include <array>
#include <cstdlib>
#include <string>
#include <vector>

using namespace llvm;
using namespace neverc::plugin;

namespace {

std::string takeErrorMessage(Error ErrorValue) {
  auto Message = toString(std::move(ErrorValue));
  return Message.str().str();
}

void setTracePath(StringRef Path) {
#if defined(_WIN32)
  _putenv_s("NEVERC_PLUGIN_TRACE_FILE", Path.str().c_str());
#else
  setenv("NEVERC_PLUGIN_TRACE_FILE", Path.str().c_str(), 1);
#endif
}

void clearTracePath() {
#if defined(_WIN32)
  _putenv_s("NEVERC_PLUGIN_TRACE_FILE", "");
#else
  unsetenv("NEVERC_PLUGIN_TRACE_FILE");
#endif
}

class TraceFile {
public:
  TraceFile() {
    EXPECT_FALSE(
        sys::fs::createTemporaryFile("neverc-plugin-lifecycle", "trace", Path));
    setTracePath(Path);
  }

  ~TraceFile() {
    clearTracePath();
    if (!Path.empty())
      consumeError(errorCodeToError(sys::fs::remove(Path)));
  }

  std::vector<std::string> lines() const {
    auto Buffer = MemoryBuffer::getFile(Path);
    if (!Buffer) {
      ADD_FAILURE() << Buffer.getError().message();
      return {};
    }
    SmallVector<StringRef, 16> Split;
    (*Buffer)->getBuffer().split(Split, '\n', -1, false);
    std::vector<std::string> Result;
    for (StringRef Line : Split)
      if (!Line.empty())
        Result.push_back(Line.str());
    return Result;
  }

private:
  SmallString<128> Path;
};

std::shared_ptr<const PluginModule> load(PluginProcessServices &Services,
                                         StringRef Path) {
  auto Result = Services.registry().load(Path);
  if (!Result) {
    ADD_FAILURE() << takeErrorMessage(Result.takeError());
    return nullptr;
  }
  return *Result;
}

TEST(PluginLifecycleTest, RunsDependencyOrderedCallbacksAndReverseDestroy) {
  TraceFile Trace;
  PluginProcessServices Services("neverc-plugin-lifecycle-tests",
                                 LLVM_VERSION_MAJOR);
  ASSERT_FALSE(Services.interfaces().freeze());
  ASSERT_NE(load(Services, NEVERC_TEST_LIFECYCLE_B_PLUGIN), nullptr);
  ASSERT_NE(load(Services, NEVERC_TEST_LIFECYCLE_A_PLUGIN), nullptr);

  {
    const std::array<StringRef, 2> Selected = {
        "org.neverc.test.lifecycle.b", "org.neverc.test.lifecycle.a"};
    auto Plan = makePluginActivationPlan(Services.registry(), Selected);
    ASSERT_TRUE(static_cast<bool>(Plan));
    EXPECT_FALSE(activatePluginPlan(Services, *Plan));
    EXPECT_FALSE(activatePluginPlan(Services, *Plan));
  }
  EXPECT_FALSE(Services.shutdown());

  EXPECT_EQ(
      Trace.lines(),
      (std::vector<std::string>{
          "org.neverc.test.lifecycle.b:entry",
          "org.neverc.test.lifecycle.a:entry",
          "org.neverc.test.lifecycle.a:process_begin",
          "org.neverc.test.lifecycle.b:process_begin",
          "org.neverc.test.lifecycle.a:register",
          "org.neverc.test.lifecycle.b:register",
          "org.neverc.test.lifecycle.b:destroy",
          "org.neverc.test.lifecycle.a:destroy",
      }));
}

TEST(PluginLifecycleTest, RollsBackSuccessfulProcessBeginPrefix) {
  TraceFile Trace;
  PluginProcessServices Services("neverc-plugin-lifecycle-tests",
                                 LLVM_VERSION_MAJOR);
  ASSERT_FALSE(Services.interfaces().freeze());
  ASSERT_NE(load(Services, NEVERC_TEST_LIFECYCLE_A_PLUGIN), nullptr);
  ASSERT_NE(load(Services, NEVERC_TEST_PROCESS_FAILURE_PLUGIN), nullptr);

  {
    const std::array<StringRef, 2> Selected = {
        "org.neverc.test.lifecycle.process-failure",
        "org.neverc.test.lifecycle.a"};
    auto Plan = makePluginActivationPlan(Services.registry(), Selected);
    ASSERT_TRUE(static_cast<bool>(Plan));
    Error Activation = activatePluginPlan(Services, *Plan);
    ASSERT_TRUE(static_cast<bool>(Activation));
    EXPECT_NE(takeErrorMessage(std::move(Activation)).find("ProcessBegin"),
              std::string::npos);
  }
  EXPECT_FALSE(Services.shutdown());

  EXPECT_EQ(
      Trace.lines(),
      (std::vector<std::string>{
          "org.neverc.test.lifecycle.a:entry",
          "org.neverc.test.lifecycle.process-failure:entry",
          "org.neverc.test.lifecycle.a:process_begin",
          "org.neverc.test.lifecycle.process-failure:process_begin",
          "org.neverc.test.lifecycle.a:destroy",
      }));
}

TEST(PluginLifecycleTest, RegistrationFailureRollsBackAllBegunPlugins) {
  TraceFile Trace;
  PluginProcessServices Services("neverc-plugin-lifecycle-tests",
                                 LLVM_VERSION_MAJOR);
  ASSERT_FALSE(Services.interfaces().freeze());
  ASSERT_NE(load(Services, NEVERC_TEST_LIFECYCLE_A_PLUGIN), nullptr);
  ASSERT_NE(load(Services, NEVERC_TEST_REGISTRATION_FAILURE_PLUGIN), nullptr);

  {
    const std::array<StringRef, 2> Selected = {
        "org.neverc.test.lifecycle.registration-failure",
        "org.neverc.test.lifecycle.a"};
    auto Plan = makePluginActivationPlan(Services.registry(), Selected);
    ASSERT_TRUE(static_cast<bool>(Plan));
    Error Activation = activatePluginPlan(Services, *Plan);
    ASSERT_TRUE(static_cast<bool>(Activation));
    EXPECT_NE(takeErrorMessage(std::move(Activation)).find("Register"),
              std::string::npos);
  }
  EXPECT_FALSE(Services.shutdown());

  EXPECT_EQ(
      Trace.lines(),
      (std::vector<std::string>{
          "org.neverc.test.lifecycle.a:entry",
          "org.neverc.test.lifecycle.registration-failure:entry",
          "org.neverc.test.lifecycle.a:process_begin",
          "org.neverc.test.lifecycle.registration-failure:process_begin",
          "org.neverc.test.lifecycle.a:register",
          "org.neverc.test.lifecycle.registration-failure:register",
          "org.neverc.test.lifecycle.registration-failure:destroy",
          "org.neverc.test.lifecycle.a:destroy",
      }));
}

TEST(PluginLifecycleTest, DestroysRegisteredUserdataInReverseOrder) {
  TraceFile Trace;
  PluginProcessServices Services("neverc-plugin-lifecycle-tests",
                                 LLVM_VERSION_MAJOR);
  ASSERT_FALSE(Services.interfaces().freeze());
  ASSERT_NE(load(Services, NEVERC_TEST_REGISTRATION_USERDATA_PLUGIN), nullptr);

  {
    const std::array<StringRef, 1> Selected = {
        "org.neverc.test.registration-userdata"};
    auto Plan = makePluginActivationPlan(Services.registry(), Selected);
    ASSERT_TRUE(static_cast<bool>(Plan));
    EXPECT_FALSE(activatePluginPlan(Services, *Plan));
  }
  EXPECT_FALSE(Services.shutdown());

  EXPECT_EQ(
      Trace.lines(),
      (std::vector<std::string>{
          "org.neverc.test.registration-userdata:entry",
          "org.neverc.test.registration-userdata:process_begin",
          "org.neverc.test.registration-userdata:register",
          "org.neverc.test.registration-userdata:userdata_second_destroy",
          "org.neverc.test.registration-userdata:userdata_first_destroy",
          "org.neverc.test.registration-userdata:destroy",
      }));
}

TEST(PluginLifecycleTest, RegistrationRollbackDestroysUserdataExactlyOnce) {
  TraceFile Trace;
  PluginProcessServices Services("neverc-plugin-lifecycle-tests",
                                 LLVM_VERSION_MAJOR);
  ASSERT_FALSE(Services.interfaces().freeze());
  ASSERT_NE(
      load(Services, NEVERC_TEST_REGISTRATION_USERDATA_FAILURE_PLUGIN),
      nullptr);

  {
    const std::array<StringRef, 1> Selected = {
        "org.neverc.test.registration-userdata-failure"};
    auto Plan = makePluginActivationPlan(Services.registry(), Selected);
    ASSERT_TRUE(static_cast<bool>(Plan));
    Error Activation = activatePluginPlan(Services, *Plan);
    ASSERT_TRUE(static_cast<bool>(Activation));
    consumeError(std::move(Activation));
  }
  EXPECT_FALSE(Services.shutdown());

  EXPECT_EQ(
      Trace.lines(),
      (std::vector<std::string>{
          "org.neverc.test.registration-userdata-failure:entry",
          "org.neverc.test.registration-userdata-failure:process_begin",
          "org.neverc.test.registration-userdata-failure:register",
          "org.neverc.test.registration-userdata-failure:"
          "userdata_second_destroy",
          "org.neverc.test.registration-userdata-failure:"
          "userdata_first_destroy",
          "org.neverc.test.registration-userdata-failure:destroy",
      }));
}

} // namespace
