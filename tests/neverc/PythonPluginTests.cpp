#include "../../neverc/lib/Plugin/Python/PythonPluginLoader.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "gtest/gtest.h"
#include <string>
#include <vector>

using namespace llvm;
using namespace neverc::plugin;

namespace {

std::string takeErrorMessage(Error ErrorValue) {
  return toString(std::move(ErrorValue)).str().str();
}

std::string
takeErrorMessage(Expected<std::shared_ptr<const PluginModule>> &Result) {
  return takeErrorMessage(Result.takeError());
}

std::vector<std::string> splitTraceLines(StringRef Buffer) {
  SmallVector<StringRef, 16> Split;
  Buffer.split(Split, '\n', -1, false);
  std::vector<std::string> Result;
  for (StringRef Line : Split) {
    Line.consume_back("\r");
    if (!Line.empty())
      Result.push_back(Line.str());
  }
  return Result;
}

class TraceFile {
public:
  TraceFile() {
#if defined(_WIN32)
    sys::path::system_temp_directory(false, Path);
#else
    Path = "/tmp";
#endif
    sys::path::append(Path, (Twine("neverc-python-plugin-") +
                             Twine(sys::Process::getProcessId()) + ".trace")
                                .str());
    consumeError(errorCodeToError(sys::fs::remove(Path)));
  }

  ~TraceFile() {
    if (!Path.empty())
      consumeError(errorCodeToError(sys::fs::remove(Path)));
  }

  std::vector<std::string> lines() const {
    auto Buffer = MemoryBuffer::getFile(Path);
    if (!Buffer) {
      ADD_FAILURE() << Buffer.getError().message();
      return {};
    }
    return splitTraceLines((*Buffer)->getBuffer());
  }

private:
  SmallString<128> Path;
};

TEST(PythonPluginTest, TraceLineParsingNormalizesCRLF) {
  EXPECT_EQ(splitTraceLines("first\r\nsecond\n\r\nthird\r\n"),
            (std::vector<std::string>{"first", "second", "third"}));
}

TEST(PythonPluginTest, FindsOnlyAnExistingAdjacentPythonHome) {
  SmallString<128> Temporary;
  ASSERT_FALSE(sys::fs::createUniqueDirectory("neverc-python-home", Temporary));
  SmallString<128> Bin(Temporary);
  sys::path::append(Bin, "bin");
  ASSERT_FALSE(sys::fs::create_directories(Bin));
  SmallString<128> Executable(Bin);
  sys::path::append(Executable, "neverc");

  EXPECT_TRUE(findAdjacentPythonHome(Executable).empty());

  SmallString<128> PythonHome(Temporary);
  sys::path::append(PythonHome, "python");
  ASSERT_FALSE(sys::fs::create_directories(PythonHome));
  SmallString<128> Canonical;
  ASSERT_FALSE(sys::fs::real_path(PythonHome, Canonical));
  EXPECT_EQ(findAdjacentPythonHome(Executable), Canonical.str());

  EXPECT_FALSE(sys::fs::remove_directories(Temporary));
}

TEST(PythonPluginTest, LoadsMetadataAndDeduplicatesTheScriptFile) {
  PluginRegistry Registry("neverc-python-plugin-tests", LLVM_VERSION_MAJOR);
  auto First = Registry.load(NEVERC_TEST_PYTHON_MINIMAL_PLUGIN);
  if (!First)
    FAIL() << takeErrorMessage(First);
  const PluginDescriptorRecord &Descriptor = (*First)->descriptor();
  EXPECT_EQ(Descriptor.PluginID, "org.neverc.test.python-minimal");
  EXPECT_EQ(Descriptor.DisplayName, "NeverC Minimal Python Plugin");
  EXPECT_EQ(Descriptor.Version.Major, 1u);
  EXPECT_EQ(Descriptor.Version.Minor, 2u);
  EXPECT_EQ(Descriptor.Version.Patch, 3u);
  EXPECT_EQ(Descriptor.VersionPrerelease, "beta.1");
  EXPECT_EQ(Descriptor.VersionBuildMetadata, "test");
  EXPECT_EQ(Descriptor.Concurrency, NEVERC_CONCURRENCY_SESSION_SERIAL);
  EXPECT_EQ(Descriptor.Reentrancy, NEVERC_REENTRANCY_NONE);

  auto Again = Registry.load(NEVERC_TEST_PYTHON_MINIMAL_PLUGIN);
  if (!Again)
    FAIL() << takeErrorMessage(Again);
  EXPECT_EQ((*Again).get(), (*First).get());
  EXPECT_EQ(Registry.moduleCount(), 1u);
}

TEST(PythonPluginTest, LoadsIndependentScriptsAndRejectsDuplicateIDs) {
  PluginRegistry Registry("neverc-python-plugin-tests", LLVM_VERSION_MAJOR);
  auto Minimal = Registry.load(NEVERC_TEST_PYTHON_MINIMAL_PLUGIN);
  if (!Minimal)
    FAIL() << takeErrorMessage(Minimal);
  auto Other = Registry.load(NEVERC_TEST_PYTHON_OTHER_PLUGIN);
  if (!Other)
    FAIL() << takeErrorMessage(Other);
  EXPECT_NE((*Minimal).get(), (*Other).get());
  EXPECT_EQ(Registry.moduleCount(), 2u);

  auto Duplicate = Registry.load(NEVERC_TEST_PYTHON_DUPLICATE_PLUGIN);
  ASSERT_FALSE(static_cast<bool>(Duplicate));
  EXPECT_NE(takeErrorMessage(Duplicate).find("duplicate plugin ID"),
            std::string::npos);
  EXPECT_EQ(Registry.moduleCount(), 2u);
}

TEST(PythonPluginTest, LoadsCompleteNativeDescriptorMetadata) {
  PluginRegistry Registry("neverc-python-plugin-tests", LLVM_VERSION_MAJOR);
  auto Loaded = Registry.load(NEVERC_TEST_PYTHON_METADATA_PLUGIN);
  if (!Loaded)
    FAIL() << takeErrorMessage(Loaded);
  const PluginDescriptorRecord &Descriptor = (*Loaded)->descriptor();
  EXPECT_EQ(Descriptor.ABIFlags, 0u);
  EXPECT_EQ(Descriptor.Concurrency, NEVERC_CONCURRENCY_THREAD_SAFE);
  EXPECT_EQ(Descriptor.Reentrancy, NEVERC_REENTRANCY_ALLOWED);
  ASSERT_EQ(Descriptor.RequiredInterfaces.size(), 1u);
  EXPECT_TRUE(Descriptor.RequiredInterfaces[0].Required);
  EXPECT_EQ(Descriptor.RequiredInterfaces[0].Major, 1u);
  EXPECT_EQ(Descriptor.RequiredInterfaces[0].Stability,
            NEVERC_INTERFACE_STABLE);
  ASSERT_EQ(Descriptor.OptionalInterfaces.size(), 1u);
  EXPECT_FALSE(Descriptor.OptionalInterfaces[0].Required);
  ASSERT_EQ(Descriptor.Dependencies.size(), 1u);
  const OwnedPluginDependency &Dependency = Descriptor.Dependencies[0];
  EXPECT_EQ(Dependency.PluginID, "org.neverc.test.python-minimal");
  EXPECT_EQ(Dependency.Kind, NEVERC_DEPENDENCY_AFTER);
  EXPECT_EQ(Dependency.Version.MinimumInclusive.Major, 1u);
  EXPECT_EQ(Dependency.Version.MinimumInclusive.Minor, 2u);
  EXPECT_EQ(Dependency.MinimumPrerelease, "beta.1");
  EXPECT_EQ(Dependency.Version.MaximumExclusive.Major, 2u);
  EXPECT_EQ(Dependency.Version.HasMaximum, NEVERC_TRUE);
  EXPECT_EQ(Dependency.Version.AllowPrerelease, NEVERC_TRUE);
}

TEST(PythonPluginTest, ImportFailuresContainThePythonTraceback) {
  PluginRegistry Registry("neverc-python-plugin-tests", LLVM_VERSION_MAJOR);
  auto Loaded = Registry.load(NEVERC_TEST_PYTHON_INVALID_PLUGIN);
  ASSERT_FALSE(static_cast<bool>(Loaded));
  std::string Message = takeErrorMessage(Loaded);
  EXPECT_NE(Message.find("Traceback"), std::string::npos) << Message;
  EXPECT_NE(Message.find("intentional Python plugin import failure"),
            std::string::npos)
      << Message;
  EXPECT_NE(Message.find("InvalidPlugin.py"), std::string::npos) << Message;
}

TEST(PythonPluginTest, ProcessHookFailuresContainThePythonTraceback) {
  PluginProcessServices Services("neverc-python-plugin-tests",
                                 LLVM_VERSION_MAJOR);
  ASSERT_FALSE(Services.interfaces().freeze());
  {
    auto Loaded =
        Services.registry().load(NEVERC_TEST_PYTHON_PROCESS_EXCEPTION_PLUGIN);
    if (!Loaded)
      FAIL() << takeErrorMessage(Loaded);

    const StringRef Selected[] = {"org.neverc.test.python-process-exception"};
    auto Plan = makePluginActivationPlan(Services.registry(), Selected);
    if (!Plan)
      FAIL() << takeErrorMessage(Plan.takeError());
    auto Session = PluginSession::create(Services, *Plan);
    ASSERT_FALSE(static_cast<bool>(Session));
    std::string Message = takeErrorMessage(Session.takeError());
    EXPECT_NE(Message.find("Traceback"), std::string::npos) << Message;
    EXPECT_NE(Message.find("intentional Python process hook explosion"),
              std::string::npos)
        << Message;
    EXPECT_NE(Message.find("ProcessExceptionPlugin.py"), std::string::npos)
        << Message;
  }
  EXPECT_FALSE(Services.shutdown());
}

TEST(PythonPluginTest, RunsProcessSessionTaskAndDestroyLifecycle) {
  TraceFile Trace;
  PluginProcessServices Services("neverc-python-plugin-tests",
                                 LLVM_VERSION_MAJOR);
  ASSERT_FALSE(Services.interfaces().freeze());
  auto Loaded = Services.registry().load(NEVERC_TEST_PYTHON_LIFECYCLE_PLUGIN);
  if (!Loaded)
    FAIL() << takeErrorMessage(Loaded);

  {
    const StringRef Selected[] = {"org.neverc.test.python-lifecycle"};
    auto Plan = makePluginActivationPlan(Services.registry(), Selected);
    if (!Plan)
      FAIL() << takeErrorMessage(Plan.takeError());
    auto Session = PluginSession::create(Services, *Plan);
    if (!Session)
      FAIL() << takeErrorMessage(Session.takeError());
    auto Task = (*Session)->createTask(NEVERC_TASK_TRANSLATION_UNIT);
    if (!Task)
      FAIL() << takeErrorMessage(Task.takeError());
    Task->reset();
    EXPECT_FALSE((*Session)->end());
    Session->reset();
  }
  Loaded->reset();
  EXPECT_FALSE(Services.shutdown());

  EXPECT_EQ(Trace.lines(),
            (std::vector<std::string>{
                "process_begin", "register", "session_begin",
                "task_begin:translation_unit", "task_end:translation_unit",
                "session_end", "destroy"}));
}

TEST(PythonPluginTest, BindsEveryGeneratedFFICallbackSlot) {
  PluginProcessServices Services("neverc-python-plugin-tests",
                                 LLVM_VERSION_MAJOR);
  ASSERT_FALSE(Services.interfaces().freeze());
  auto Loaded =
      Services.registry().load(NEVERC_TEST_PYTHON_ALL_FFI_CALLBACKS_PLUGIN);
  if (!Loaded)
    FAIL() << takeErrorMessage(Loaded);

  {
    const StringRef Selected[] = {"org.neverc.test.python-all-ffi-callbacks"};
    auto Plan = makePluginActivationPlan(Services.registry(), Selected);
    if (!Plan)
      FAIL() << takeErrorMessage(Plan.takeError());
    auto Session = PluginSession::create(Services, *Plan);
    if (!Session)
      FAIL() << takeErrorMessage(Session.takeError());
    EXPECT_FALSE((*Session)->end());
    Session->reset();
  }
  Loaded->reset();
  EXPECT_FALSE(Services.shutdown());
}

TEST(PythonPluginTest, UnloadAndReloadCreateFreshRuntimeInstances) {
  PluginRegistry Registry("neverc-python-plugin-tests", LLVM_VERSION_MAJOR);
  std::weak_ptr<const PluginModule> Old;
  {
    auto First = Registry.load(NEVERC_TEST_PYTHON_MINIMAL_PLUGIN);
    if (!First)
      FAIL() << takeErrorMessage(First);
    Old = *First;
    EXPECT_FALSE(Registry.unload("org.neverc.test.python-minimal"));
  }
  EXPECT_TRUE(Old.expired());

  auto Reloaded = Registry.load(NEVERC_TEST_PYTHON_MINIMAL_PLUGIN);
  if (!Reloaded)
    FAIL() << takeErrorMessage(Reloaded);
  EXPECT_EQ((*Reloaded)->descriptor().PluginID,
            "org.neverc.test.python-minimal");
}

} // namespace
