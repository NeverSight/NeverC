#include "neverc/Plugin/Host/PluginIOBridge.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "gtest/gtest.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/VirtualFileSystem.h"
#include <memory>
#include <optional>
#include <string>

using namespace llvm;
using namespace neverc::plugin;

namespace {

std::string providerErrorMessage(Error ErrorValue) {
  return toString(std::move(ErrorValue)).str().str();
}

class ProviderVFSHarness {
public:
  explicit ProviderVFSHarness(const char *PluginPath)
      : Services("neverc-provider-vfs-tests", LLVM_VERSION_MAJOR) {
    if (Error E = registerPluginIOInterface(Services))
      Failure = providerErrorMessage(std::move(E));
    if (Failure.empty())
      if (Error E = Services.interfaces().freeze())
        Failure = providerErrorMessage(std::move(E));
    if (!Failure.empty())
      return;

    auto Loaded = Services.registry().load(PluginPath);
    if (!Loaded) {
      Failure = providerErrorMessage(Loaded.takeError());
      return;
    }
    PluginID = (*Loaded)->descriptor().PluginID;
    const std::array<StringRef, 1> Selected = {PluginID};
    auto CreatedPlan =
        makePluginActivationPlan(Services.registry(), Selected);
    if (!CreatedPlan) {
      Failure = providerErrorMessage(CreatedPlan.takeError());
      return;
    }
    Plan.emplace(std::move(*CreatedPlan));
    if (Error E = activatePluginPlan(Services, *Plan)) {
      Failure = providerErrorMessage(std::move(E));
      return;
    }
    auto CreatedSession = PluginSession::create(Services, *Plan);
    if (!CreatedSession) {
      Failure = providerErrorMessage(CreatedSession.takeError());
      return;
    }
    Session = std::move(*CreatedSession);
    auto CreatedTask =
        Session->createTask(NEVERC_TASK_TRANSLATION_UNIT);
    if (!CreatedTask) {
      Failure = providerErrorMessage(CreatedTask.takeError());
      return;
    }
    Task = std::move(*CreatedTask);

    Base = makeIntrusiveRefCnt<vfs::InMemoryFileSystem>();
    Base->addFile(
        "/work/input.c", 0,
        MemoryBuffer::getMemBufferCopy("base", "/work/input.c"));
    Base->setCurrentWorkingDirectory("/work");
    auto CreatedFS = createPluginFileSystem(*Task, Base);
    if (!CreatedFS) {
      Failure = providerErrorMessage(CreatedFS.takeError());
      return;
    }
    FileSystem = std::move(*CreatedFS);
  }

  ~ProviderVFSHarness() {
    FileSystem.reset();
    Base.reset();
    if (Task && !Task->isEnded())
      consumeError(Task->end());
    Task.reset();
    if (Session)
      consumeError(Session->end());
    Session.reset();
    Plan.reset();
    consumeError(Services.shutdown());
  }

  bool valid() const { return Failure.empty(); }
  const std::string &failure() const { return Failure; }

  PluginTaskContext &task() { return *Task; }
  vfs::FileSystem &fs() { return *FileSystem; }

private:
  PluginProcessServices Services;
  std::string PluginID;
  std::string Failure;
  std::optional<PluginActivationPlan> Plan;
  std::unique_ptr<PluginSession> Session;
  std::unique_ptr<PluginTaskContext> Task;
  IntrusiveRefCntPtr<vfs::InMemoryFileSystem> Base;
  IntrusiveRefCntPtr<vfs::FileSystem> FileSystem;
};

TEST(PluginProviderVFSTest, ServesCopiedFilesDirectoriesAndCanonicalPaths) {
  ProviderVFSHarness Harness(NEVERC_TEST_VFS_PLUGIN);
  ASSERT_TRUE(Harness.valid()) << Harness.failure();

  auto First = Harness.fs().openFileForRead("/plugin/virtual.h");
  ASSERT_TRUE(static_cast<bool>(First));
  auto Second = Harness.fs().openFileForRead("/plugin/virtual.h");
  ASSERT_TRUE(static_cast<bool>(Second));
  auto FirstBuffer =
      (*First)->getBuffer("/plugin/virtual.h", -1, true, false);
  auto SecondBuffer =
      (*Second)->getBuffer("/plugin/virtual.h", -1, true, false);
  ASSERT_TRUE(static_cast<bool>(FirstBuffer));
  ASSERT_TRUE(static_cast<bool>(SecondBuffer));
  EXPECT_EQ((*FirstBuffer)->getBuffer(), "int one;\n");
  EXPECT_EQ((*SecondBuffer)->getBuffer(), "int two;\n");

  auto BaseBuffer = Harness.fs().getBufferForFile("/work/input.c");
  ASSERT_TRUE(static_cast<bool>(BaseBuffer));
  EXPECT_EQ((*BaseBuffer)->getBuffer(), "base");

  SmallString<64> Canonical;
  ASSERT_FALSE(Harness.fs().getRealPath("/plugin/", Canonical));
  EXPECT_EQ(Canonical, "/plugin");

  std::error_code Error;
  vfs::directory_iterator Iterator =
      Harness.fs().dir_begin("/plugin", Error);
  ASSERT_FALSE(Error);
  ASSERT_NE(Iterator, vfs::directory_iterator());
  EXPECT_EQ(Iterator->path(), "/plugin/virtual.h");
  Iterator.increment(Error);
  EXPECT_FALSE(Error);
  EXPECT_EQ(Iterator, vfs::directory_iterator());
}

TEST(PluginProviderVFSTest, RejectsInconsistentOpenSize) {
  ProviderVFSHarness Harness(NEVERC_TEST_VFS_BAD_SIZE_PLUGIN);
  ASSERT_TRUE(Harness.valid()) << Harness.failure();
  auto Opened = Harness.fs().openFileForRead("/plugin/virtual.h");
  ASSERT_FALSE(static_cast<bool>(Opened));
  EXPECT_EQ(Opened.getError(), errc::io_error);
}

TEST(PluginProviderVFSTest, DoesNotFallbackAfterProviderError) {
  ProviderVFSHarness Harness(NEVERC_TEST_VFS_ERROR_FALLBACK_PLUGIN);
  ASSERT_TRUE(Harness.valid()) << Harness.failure();
  auto Status = Harness.fs().status("/work/input.c");
  ASSERT_FALSE(static_cast<bool>(Status));
  EXPECT_EQ(Status.getError(), errc::permission_denied);
}

TEST(PluginProviderVFSTest, RejectsHalfFilledNotHandledResults) {
  ProviderVFSHarness Harness(NEVERC_TEST_VFS_HALF_RESULT_PLUGIN);
  ASSERT_TRUE(Harness.valid()) << Harness.failure();
  auto Status = Harness.fs().status("/plugin/virtual.h");
  ASSERT_FALSE(static_cast<bool>(Status));
  EXPECT_EQ(Status.getError(), errc::io_error);
}

TEST(PluginProviderVFSTest, RejectsUnknownResultDiscriminants) {
  ProviderVFSHarness Harness(
      NEVERC_TEST_VFS_INVALID_DISPOSITION_PLUGIN);
  ASSERT_TRUE(Harness.valid()) << Harness.failure();
  auto Status = Harness.fs().status("/plugin/virtual.h");
  ASSERT_FALSE(static_cast<bool>(Status));
  EXPECT_EQ(Status.getError(), errc::io_error);
}

TEST(PluginProviderVFSTest, DeniesSamePathRecursiveRouting) {
  ProviderVFSHarness Harness(NEVERC_TEST_VFS_RECURSIVE_PLUGIN);
  ASSERT_TRUE(Harness.valid()) << Harness.failure();
  auto Status = Harness.fs().status("/plugin/virtual.h");
  ASSERT_FALSE(static_cast<bool>(Status));
  EXPECT_EQ(Status.getError(), errc::resource_deadlock_would_occur);
}

TEST(PluginProviderVFSTest, StopsCallbacksAfterTaskEnd) {
  ProviderVFSHarness Harness(NEVERC_TEST_VFS_PLUGIN);
  ASSERT_TRUE(Harness.valid()) << Harness.failure();
  ASSERT_FALSE(Harness.task().end());
  auto Status = Harness.fs().status("/plugin/virtual.h");
  ASSERT_FALSE(static_cast<bool>(Status));
  EXPECT_EQ(Status.getError(),
            std::make_error_code(std::errc::operation_canceled));
}

} // namespace
