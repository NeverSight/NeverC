#include "neverc/Plugin/Host/PluginIOBridge.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "gtest/gtest.h"
#include "llvm/ADT/SmallSet.h"
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

std::string takeErrorMessage(Error ErrorValue) {
  return toString(std::move(ErrorValue)).str().str();
}

NevercStringView stringView(StringRef Value) {
  return {Value.data(), static_cast<uint64_t>(Value.size())};
}

class PluginVFSTest : public testing::Test {
protected:
  void SetUp() override {
    ASSERT_FALSE(registerPluginIOInterface(Services));
    ASSERT_FALSE(Services.interfaces().freeze());

    auto CreatedPlan =
        makePluginActivationPlan(Services.registry(), {});
    ASSERT_TRUE(static_cast<bool>(CreatedPlan))
        << takeErrorMessage(CreatedPlan.takeError());
    Plan.emplace(std::move(*CreatedPlan));
    auto CreatedSession = PluginSession::create(Services, *Plan);
    ASSERT_TRUE(static_cast<bool>(CreatedSession))
        << takeErrorMessage(CreatedSession.takeError());
    Session = std::move(*CreatedSession);
    auto CreatedTask =
        Session->createTask(NEVERC_TASK_TRANSLATION_UNIT);
    ASSERT_TRUE(static_cast<bool>(CreatedTask))
        << takeErrorMessage(CreatedTask.takeError());
    Task = std::move(*CreatedTask);

    Base = makeIntrusiveRefCnt<vfs::InMemoryFileSystem>();
    ASSERT_TRUE(Base->addFile(
        "/work/input.c", 7,
        MemoryBuffer::getMemBufferCopy("input", "/work/input.c")));
    ASSERT_TRUE(Base->addFile(
        "/work/sub/value.txt", 8,
        MemoryBuffer::getMemBufferCopy("value",
                                       "/work/sub/value.txt")));
    ASSERT_FALSE(Base->setCurrentWorkingDirectory("/work"));

    auto Query = Services.interfaces().query(
        ioPluginInterfaceID(), NEVERC_IO_API_MAJOR,
        NEVERC_IO_API_MINOR);
    ASSERT_TRUE(static_cast<bool>(Query))
        << takeErrorMessage(Query.takeError());
    API = static_cast<const NevercIOAPI *>(Query->Table);
    ASSERT_NE(API, nullptr);
  }

  void TearDown() override {
    FileSystem.reset();
    Base.reset();
    if (Task && !Task->isEnded())
      EXPECT_FALSE(Task->end());
    Task.reset();
    if (Session)
      EXPECT_FALSE(Session->end());
    Session.reset();
    Plan.reset();
    EXPECT_FALSE(Services.shutdown());
  }

  void installFileSystem() {
    auto Created = createPluginFileSystem(*Task, Base);
    ASSERT_TRUE(static_cast<bool>(Created))
        << takeErrorMessage(Created.takeError());
    FileSystem = std::move(*Created);
  }

  std::string bufferText(NevercBufferHandle Buffer) {
    NevercBufferView View{};
    View.Header.StructSize = sizeof(View);
    EXPECT_EQ(API->GetBufferView(API->Context, Task->handle(), Buffer, &View)
                  .Code,
              NEVERC_STATUS_OK);
    return std::string(reinterpret_cast<const char *>(View.Data),
                       static_cast<size_t>(View.Length));
  }

  PluginProcessServices Services{"neverc-plugin-vfs-tests",
                                 LLVM_VERSION_MAJOR};
  std::optional<PluginActivationPlan> Plan;
  std::unique_ptr<PluginSession> Session;
  std::unique_ptr<PluginTaskContext> Task;
  IntrusiveRefCntPtr<vfs::InMemoryFileSystem> Base;
  IntrusiveRefCntPtr<vfs::FileSystem> FileSystem;
  const NevercIOAPI *API = nullptr;
};

TEST_F(PluginVFSTest, StatsOpensReadsAndReportsNotFound) {
  installFileSystem();

  NevercVFSStatus Status{};
  Status.Header.StructSize = sizeof(Status);
  ASSERT_EQ(API->Stat(API->Context, Task->handle(),
                      stringView("/work/input.c"), &Status)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Status.Type, NEVERC_VFS_FILE_REGULAR);
  EXPECT_EQ(Status.Size, 5U);
  EXPECT_EQ(Status.ModificationTime, 7);

  NevercFileHandle File{};
  ASSERT_EQ(API->OpenFileForRead(API->Context, Task->handle(),
                                 stringView("/work/input.c"), &File)
                .Code,
            NEVERC_STATUS_OK);
  NevercBufferHandle Buffer{};
  ASSERT_EQ(API->ReadFile(API->Context, Task->handle(), File, 1, 3,
                          &Buffer)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(bufferText(Buffer), "npu");
  NevercBufferView View{};
  View.Header.StructSize = sizeof(View);
  ASSERT_EQ(API->GetBufferView(API->Context, Task->handle(), Buffer, &View)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(View.NullTerminated, NEVERC_TRUE);
  EXPECT_EQ(View.Data[View.Length], 0);
  EXPECT_EQ(API->ReleaseBuffer(API->Context, Task->handle(), Buffer).Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(API->CloseFile(API->Context, Task->handle(), File).Code,
            NEVERC_STATUS_OK);

  NevercVFSStatus Missing{};
  Missing.Header.StructSize = sizeof(Missing);
  NevercStatus MissingStatus =
      API->Stat(API->Context, Task->handle(),
                stringView("/work/missing.h"), &Missing);
  EXPECT_EQ(MissingStatus.Code, NEVERC_STATUS_PLUGIN_FAILURE);
  EXPECT_EQ(MissingStatus.Detail, NEVERC_IO_ERROR_NOT_FOUND);
}

TEST_F(PluginVFSTest, CanonicalizesAndTraversesDirectories) {
  installFileSystem();

  NevercBufferHandle Current{};
  ASSERT_EQ(API->GetWorkingDirectory(
                API->Context, Task->handle(), &Current)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(bufferText(Current), "/work");
  EXPECT_EQ(API->ReleaseBuffer(API->Context, Task->handle(), Current).Code,
            NEVERC_STATUS_OK);

  ASSERT_EQ(API->SetWorkingDirectory(
                API->Context, Task->handle(),
                stringView("/work/sub"))
                .Code,
            NEVERC_STATUS_OK);
  NevercBufferHandle Canonical{};
  ASSERT_EQ(API->Canonicalize(API->Context, Task->handle(),
                              stringView("../input.c"), &Canonical)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(bufferText(Canonical), "/work/input.c");
  EXPECT_EQ(API->ReleaseBuffer(API->Context, Task->handle(), Canonical).Code,
            NEVERC_STATUS_OK);

  NevercDirectoryCursorHandle Cursor{};
  ASSERT_EQ(API->OpenDirectory(API->Context, Task->handle(),
                               stringView("/work"), &Cursor)
                .Code,
            NEVERC_STATUS_OK);
  SmallSet<std::string, 4> Paths;
  for (;;) {
    NevercVFSDirectoryEntry Entry{};
    Entry.Header.StructSize = sizeof(Entry);
    NevercBool HasEntry = NEVERC_FALSE;
    ASSERT_EQ(API->ReadDirectory(API->Context, Task->handle(), Cursor,
                                 &Entry, &HasEntry)
                  .Code,
              NEVERC_STATUS_OK);
    if (HasEntry == NEVERC_FALSE)
      break;
    Paths.insert(StringRef(Entry.Path.Data,
                           static_cast<size_t>(Entry.Path.Length))
                     .str());
  }
  EXPECT_TRUE(Paths.count("/work/input.c"));
  EXPECT_TRUE(Paths.count("/work/sub"));
  EXPECT_EQ(API->CloseDirectory(API->Context, Task->handle(), Cursor).Code,
            NEVERC_STATUS_OK);
}

TEST_F(PluginVFSTest, CopiesSessionMemoryFilesIntoFrozenTaskViews) {
  char Content[] = "virtual";
  ASSERT_EQ(API->AddMemoryFile(
                API->Context, Session->handle(),
                stringView("/work/virtual.h"),
                {reinterpret_cast<const uint8_t *>(Content), 7}, 11)
                .Code,
            NEVERC_STATUS_OK);
  Content[0] = 'X';
  installFileSystem();

  auto Buffer = FileSystem->getBufferForFile("/work/virtual.h");
  ASSERT_TRUE(static_cast<bool>(Buffer));
  EXPECT_EQ((*Buffer)->getBuffer(), "virtual");

  ASSERT_EQ(API->AddMemoryFile(
                API->Context, Session->handle(),
                stringView("/work/late.h"),
                {reinterpret_cast<const uint8_t *>("late"), 4}, 12)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_FALSE(FileSystem->status("/work/late.h"));

  auto CreatedSecond =
      Session->createTask(NEVERC_TASK_TRANSLATION_UNIT);
  ASSERT_TRUE(static_cast<bool>(CreatedSecond))
      << takeErrorMessage(CreatedSecond.takeError());
  std::unique_ptr<PluginTaskContext> Second =
      std::move(*CreatedSecond);
  auto SecondFileSystem = createPluginFileSystem(*Second, Base);
  ASSERT_TRUE(static_cast<bool>(SecondFileSystem))
      << takeErrorMessage(SecondFileSystem.takeError());
  auto Late = (*SecondFileSystem)->getBufferForFile("/work/late.h");
  ASSERT_TRUE(static_cast<bool>(Late));
  EXPECT_EQ((*Late)->getBuffer(), "late");
  SecondFileSystem->reset();
  EXPECT_FALSE(Second->end());
}

TEST_F(PluginVFSTest, ChildSessionsInheritReadOnlyAndWriteSeparateOverlays) {
  const uint8_t ParentContent[] = "parent";
  ASSERT_EQ(API->AddMemoryFile(
                API->Context, Session->handle(),
                stringView("/work/shared.h"),
                {ParentContent, sizeof(ParentContent) - 1}, 1)
                .Code,
            NEVERC_STATUS_OK);
  installFileSystem();

  auto CreatedChild = Session->createChild();
  ASSERT_TRUE(static_cast<bool>(CreatedChild))
      << takeErrorMessage(CreatedChild.takeError());
  std::unique_ptr<PluginSession> Child = std::move(*CreatedChild);
  const uint8_t ChildContent[] = "child";
  const uint8_t ChildOnlyContent[] = "child-only";
  ASSERT_EQ(API->AddMemoryFile(
                API->Context, Child->handle(),
                stringView("/work/shared.h"),
                {ChildContent, sizeof(ChildContent) - 1}, 2)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API->AddMemoryFile(
                API->Context, Child->handle(),
                stringView("/work/child-only.h"),
                {ChildOnlyContent, sizeof(ChildOnlyContent) - 1}, 3)
                .Code,
            NEVERC_STATUS_OK);

  auto CreatedChildTask =
      Child->createTask(NEVERC_TASK_TRANSLATION_UNIT);
  ASSERT_TRUE(static_cast<bool>(CreatedChildTask))
      << takeErrorMessage(CreatedChildTask.takeError());
  std::unique_ptr<PluginTaskContext> ChildTask =
      std::move(*CreatedChildTask);
  auto ChildFS = createPluginFileSystem(*ChildTask, Base);
  ASSERT_TRUE(static_cast<bool>(ChildFS))
      << takeErrorMessage(ChildFS.takeError());
  auto ChildShared = (*ChildFS)->getBufferForFile("/work/shared.h");
  auto ChildOnly = (*ChildFS)->getBufferForFile("/work/child-only.h");
  ASSERT_TRUE(static_cast<bool>(ChildShared));
  ASSERT_TRUE(static_cast<bool>(ChildOnly));
  EXPECT_EQ((*ChildShared)->getBuffer(), "child");
  EXPECT_EQ((*ChildOnly)->getBuffer(), "child-only");

  auto ParentShared = FileSystem->getBufferForFile("/work/shared.h");
  ASSERT_TRUE(static_cast<bool>(ParentShared));
  EXPECT_EQ((*ParentShared)->getBuffer(), "parent");
  EXPECT_FALSE(FileSystem->status("/work/child-only.h"));

  ChildFS->reset();
  EXPECT_FALSE(ChildTask->end());
  ChildTask.reset();
  EXPECT_FALSE(Child->end());
}

TEST_F(PluginVFSTest, TaskEndClosesLeakedFilesBuffersAndDirectoryCursors) {
  installFileSystem();
  NevercFileHandle File{};
  ASSERT_EQ(API->OpenFileForRead(API->Context, Task->handle(),
                                 stringView("/work/input.c"), &File)
                .Code,
            NEVERC_STATUS_OK);
  NevercBufferHandle Buffer{};
  ASSERT_EQ(API->ReadFile(API->Context, Task->handle(), File, 0, 5,
                          &Buffer)
                .Code,
            NEVERC_STATUS_OK);
  NevercDirectoryCursorHandle Cursor{};
  ASSERT_EQ(API->OpenDirectory(API->Context, Task->handle(),
                               stringView("/work"), &Cursor)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_GE(Task->handles().liveCount(), 3U);

  NevercTaskHandle EndedHandle = Task->handle();
  ASSERT_FALSE(Task->end());
  EXPECT_EQ(Task->handles().liveCount(), 0U);

  NevercVFSStatus Status{};
  Status.Header.StructSize = sizeof(Status);
  EXPECT_EQ(API->Stat(API->Context, EndedHandle,
                      stringView("/work/input.c"), &Status)
                .Code,
            NEVERC_STATUS_STALE_HANDLE);
}

} // namespace
