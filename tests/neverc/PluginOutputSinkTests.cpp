#include "neverc/Plugin/Host/PluginIOBridge.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "neverc/Foundation/Core/OutputTransaction.h"
#include "gtest/gtest.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/Support/raw_ostream.h"
#include <atomic>
#include <array>
#include <chrono>
#include <cstring>
#include <future>
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

class PluginOutputSinkTest : public testing::Test {
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

    auto Query = Services.interfaces().query(
        ioPluginInterfaceID(), NEVERC_IO_API_MAJOR, NEVERC_IO_API_MINOR);
    ASSERT_TRUE(static_cast<bool>(Query))
        << takeErrorMessage(Query.takeError());
    API = static_cast<const NevercIOAPI *>(Query->Table);
    ASSERT_NE(API, nullptr);
  }

  void TearDown() override {
    if (Task && !Task->isEnded())
      EXPECT_FALSE(Task->end());
    Task.reset();
    if (Session)
      EXPECT_FALSE(Session->end());
    Session.reset();
    Plan.reset();
    EXPECT_FALSE(Services.shutdown());
  }

  PluginProcessServices Services{"neverc-plugin-output-sink-tests",
                                 LLVM_VERSION_MAJOR};
  std::optional<PluginActivationPlan> Plan;
  std::unique_ptr<PluginSession> Session;
  std::unique_ptr<PluginTaskContext> Task;
  const NevercIOAPI *API = nullptr;
};

TEST_F(PluginOutputSinkTest,
       FinishDoesNotPublishAndHostCommitIsExactlyOnce) {
  NevercOutputSinkHandle Sink{};
  ASSERT_EQ(API->BeginMemoryOutput(
                API->Context, Task->handle(), stringView("object"),
                UINT64_C(1024), &Sink)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_FALSE(neverc_handle_is_null(Sink));

  const uint8_t Bytes[] = {'o', 'b', 'j'};
  ASSERT_EQ(API->OutputWrite(API->Context, Task->handle(), Sink,
                             {Bytes, sizeof(Bytes)})
                .Code,
            NEVERC_STATUS_OK);

  NevercOutputSeal Seal{};
  Seal.Header.StructSize = sizeof(Seal);
  ASSERT_EQ(API->OutputFinish(API->Context, Task->handle(), Sink, &Seal).Code,
            NEVERC_STATUS_OK);
  EXPECT_FALSE(neverc_handle_is_null(Seal.Handle));
  EXPECT_EQ(Seal.Size, sizeof(Bytes));

  NevercOutputSummary BeforeCommit{};
  BeforeCommit.Header.StructSize = sizeof(BeforeCommit);
  ASSERT_EQ(API->OutputGetSummary(API->Context, Task->handle(), Sink,
                                  &BeforeCommit)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(BeforeCommit.State, NEVERC_OUTPUT_FINISHED);
  EXPECT_EQ(BeforeCommit.PublicationGeneration, 0U);
  EXPECT_FALSE(findPluginMemoryOutput(*Task, "object").has_value());

  auto FirstCommit = hostCommitPluginOutput(*Task, Seal.Handle);
  ASSERT_TRUE(static_cast<bool>(FirstCommit))
      << takeErrorMessage(FirstCommit.takeError());
  EXPECT_EQ(FirstCommit->State, NEVERC_OUTPUT_COMMITTED);
  EXPECT_EQ(FirstCommit->PublicationGeneration, 1U);

  auto Published = findPluginMemoryOutput(*Task, "object");
  ASSERT_TRUE(Published.has_value());
  EXPECT_EQ(Published->Generation, 1U);
  EXPECT_EQ(StringRef(reinterpret_cast<const char *>(Published->Bytes.data()),
                      Published->Bytes.size()),
            "obj");

  auto SecondCommit = hostCommitPluginOutput(*Task, Seal.Handle);
  ASSERT_TRUE(static_cast<bool>(SecondCommit))
      << takeErrorMessage(SecondCommit.takeError());
  EXPECT_EQ(SecondCommit->PublicationGeneration, 1U);
  EXPECT_EQ(API->OutputWrite(API->Context, Task->handle(), Sink,
                             {Bytes, sizeof(Bytes)})
                .Code,
            NEVERC_STATUS_INVALID_STATE);
  EXPECT_EQ(API->OutputAbort(API->Context, Task->handle(), Sink).Code,
            NEVERC_STATUS_INVALID_STATE);
}

TEST_F(PluginOutputSinkTest,
       SupportsRandomAccessTruncateAndAbortStateMachine) {
  NevercOutputSinkHandle Sink{};
  ASSERT_EQ(API->BeginMemoryOutput(
                API->Context, Task->handle(), stringView("assembly"),
                UINT64_C(64), &Sink)
                .Code,
            NEVERC_STATUS_OK);
  const uint8_t Initial[] = {'a', 'b', 'c', 'd', 'e', 'f'};
  ASSERT_EQ(API->OutputWrite(API->Context, Task->handle(), Sink,
                             {Initial, sizeof(Initial)})
                .Code,
            NEVERC_STATUS_OK);

  uint64_t Position = 0;
  ASSERT_EQ(API->OutputTell(API->Context, Task->handle(), Sink, &Position)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Position, sizeof(Initial));

  const uint8_t Patch[] = {'X', 'Y'};
  ASSERT_EQ(API->OutputWriteAt(API->Context, Task->handle(), Sink, 2,
                               {Patch, sizeof(Patch)})
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API->OutputTruncate(API->Context, Task->handle(), Sink, 4).Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API->OutputMetadataSet(
                API->Context, Task->handle(), Sink,
                stringView("content-type"),
                stringView("application/octet-stream"))
                .Code,
            NEVERC_STATUS_OK);

  NevercOutputSeal FirstSeal{};
  FirstSeal.Header.StructSize = sizeof(FirstSeal);
  ASSERT_EQ(API->OutputFinish(API->Context, Task->handle(), Sink, &FirstSeal)
                .Code,
            NEVERC_STATUS_OK);
  NevercOutputSeal SecondSeal{};
  SecondSeal.Header.StructSize = sizeof(SecondSeal);
  ASSERT_EQ(API->OutputFinish(API->Context, Task->handle(), Sink, &SecondSeal)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(FirstSeal.Handle.Owner, SecondSeal.Handle.Owner);
  EXPECT_EQ(FirstSeal.Handle.Value, SecondSeal.Handle.Value);
  EXPECT_EQ(FirstSeal.Size, 4U);
  EXPECT_EQ(std::memcmp(FirstSeal.Digest, SecondSeal.Digest,
                        sizeof(FirstSeal.Digest)),
            0);

  ASSERT_EQ(API->OutputAbort(API->Context, Task->handle(), Sink).Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(API->OutputAbort(API->Context, Task->handle(), Sink).Code,
            NEVERC_STATUS_OK);
  NevercOutputSummary Summary{};
  Summary.Header.StructSize = sizeof(Summary);
  ASSERT_EQ(API->OutputGetSummary(API->Context, Task->handle(), Sink, &Summary)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Summary.State, NEVERC_OUTPUT_ABORTED);
  EXPECT_EQ(Summary.Size, 0U);
  EXPECT_EQ(API->OutputFinish(API->Context, Task->handle(), Sink, &SecondSeal)
                .Code,
            NEVERC_STATUS_INVALID_STATE);
  EXPECT_FALSE(findPluginMemoryOutput(*Task, "assembly").has_value());
}

TEST_F(PluginOutputSinkTest,
       HostCanAbortASealedCandidateWithoutPublishingIt) {
  NevercOutputSinkHandle Sink{};
  ASSERT_EQ(API->BeginMemoryOutput(
                API->Context, Task->handle(), stringView("sealed-abort"),
                UINT64_C(64), &Sink)
                .Code,
            NEVERC_STATUS_OK);
  const uint8_t Bytes[] = {'o', 'l', 'd'};
  ASSERT_EQ(API->OutputWrite(API->Context, Task->handle(), Sink,
                             {Bytes, sizeof(Bytes)})
                .Code,
            NEVERC_STATUS_OK);
  NevercOutputSeal Seal{};
  Seal.Header.StructSize = sizeof(Seal);
  ASSERT_EQ(API->OutputFinish(API->Context, Task->handle(), Sink, &Seal).Code,
            NEVERC_STATUS_OK);

  auto FirstAbort = hostAbortPluginOutput(*Task, Seal.Handle);
  ASSERT_TRUE(static_cast<bool>(FirstAbort))
      << takeErrorMessage(FirstAbort.takeError());
  EXPECT_EQ(FirstAbort->State, NEVERC_OUTPUT_ABORTED);
  EXPECT_EQ(FirstAbort->Flags, NEVERC_OUTPUT_FLAG_NONE);
  EXPECT_FALSE(findPluginMemoryOutput(*Task, "sealed-abort").has_value());

  auto SecondAbort = hostAbortPluginOutput(*Task, Seal.Handle);
  ASSERT_TRUE(static_cast<bool>(SecondAbort))
      << takeErrorMessage(SecondAbort.takeError());
  EXPECT_EQ(SecondAbort->State, NEVERC_OUTPUT_ABORTED);
  auto Commit = hostCommitPluginOutput(*Task, Seal.Handle);
  EXPECT_FALSE(static_cast<bool>(Commit));
  consumeError(Commit.takeError());
}

TEST_F(PluginOutputSinkTest,
       FileOutputPublishesAtomicallyOnlyAfterHostCommit) {
  SmallString<128> Directory;
  ASSERT_FALSE(sys::fs::createUniqueDirectory(
      "neverc-plugin-output", Directory));
  auto RemoveDirectory = make_scope_exit(
      [&] { (void)sys::fs::remove_directories(Directory); });
  SmallString<160> FinalPath(Directory);
  sys::path::append(FinalPath, "artifact.o");

  NevercOutputSinkHandle Sink{};
  ASSERT_EQ(API->BeginFileOutput(
                API->Context, Task->handle(), stringView(FinalPath),
                UINT64_C(1024), &Sink)
                .Code,
            NEVERC_STATUS_OK);
  const uint8_t Bytes[] = {'n', 'e', 'w'};
  ASSERT_EQ(API->OutputWrite(API->Context, Task->handle(), Sink,
                             {Bytes, sizeof(Bytes)})
                .Code,
            NEVERC_STATUS_OK);
  NevercOutputSeal Seal{};
  Seal.Header.StructSize = sizeof(Seal);
  ASSERT_EQ(API->OutputFinish(API->Context, Task->handle(), Sink, &Seal).Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Seal.Kind, NEVERC_OUTPUT_FILE);
  EXPECT_FALSE(sys::fs::exists(FinalPath));

  auto Committed = hostCommitPluginOutput(*Task, Seal.Handle);
  ASSERT_TRUE(static_cast<bool>(Committed))
      << takeErrorMessage(Committed.takeError());
  EXPECT_EQ(Committed->State, NEVERC_OUTPUT_COMMITTED);
  EXPECT_EQ(Committed->Flags & NEVERC_OUTPUT_FLAG_PUBLISHED,
            NEVERC_OUTPUT_FLAG_PUBLISHED);
#if defined(_WIN32)
  EXPECT_EQ(Committed->Flags & NEVERC_OUTPUT_FLAG_DURABILITY_UNCONFIRMED,
            NEVERC_OUTPUT_FLAG_DURABILITY_UNCONFIRMED);
#else
  EXPECT_EQ(Committed->Flags & NEVERC_OUTPUT_FLAG_DURABLE,
            NEVERC_OUTPUT_FLAG_DURABLE);
#endif
  auto Contents = MemoryBuffer::getFile(FinalPath);
  ASSERT_TRUE(static_cast<bool>(Contents));
  EXPECT_EQ((*Contents)->getBuffer(), "new");

  SmallString<160> AbortedPath(Directory);
  sys::path::append(AbortedPath, "aborted.o");
  NevercOutputSinkHandle AbortedSink{};
  ASSERT_EQ(API->BeginFileOutput(
                API->Context, Task->handle(), stringView(AbortedPath),
                UINT64_C(1024), &AbortedSink)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API->OutputWrite(API->Context, Task->handle(), AbortedSink,
                             {Bytes, sizeof(Bytes)})
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(
      API->OutputAbort(API->Context, Task->handle(), AbortedSink).Code,
      NEVERC_STATUS_OK);
  EXPECT_FALSE(sys::fs::exists(AbortedPath));
}

TEST_F(PluginOutputSinkTest,
       SameTaskFileAliasIsRejectedWithoutWaiting) {
  SmallString<128> Directory;
  ASSERT_FALSE(sys::fs::createUniqueDirectory(
      "neverc-plugin-output-task-alias", Directory));
  auto RemoveDirectory = make_scope_exit(
      [&] { (void)sys::fs::remove_directories(Directory); });
  SmallString<160> FinalPath(Directory);
  sys::path::append(FinalPath, "artifact.o");
  SmallString<160> AliasPath(Directory);
  sys::path::append(AliasPath, ".", "artifact.o");

  NevercOutputSinkHandle First{};
  ASSERT_EQ(API->BeginFileOutput(
                API->Context, Task->handle(), stringView(FinalPath),
                UINT64_C(1024), &First)
                .Code,
            NEVERC_STATUS_OK);
  NevercOutputSinkHandle Duplicate{};
  EXPECT_EQ(API->BeginFileOutput(
                API->Context, Task->handle(), stringView(AliasPath),
                UINT64_C(1024), &Duplicate)
                .Code,
            NEVERC_STATUS_DUPLICATE_ID);
  EXPECT_TRUE(neverc_handle_is_null(Duplicate));
  EXPECT_EQ(API->OutputAbort(API->Context, Task->handle(), First).Code,
            NEVERC_STATUS_OK);
}

TEST_F(PluginOutputSinkTest,
       FileLeaseIsSharedAcrossSessionsAndWaitReportsCancellation) {
  SmallString<128> Directory;
  ASSERT_FALSE(sys::fs::createUniqueDirectory(
      "neverc-plugin-output-session-lease", Directory));
  auto RemoveDirectory = make_scope_exit(
      [&] { (void)sys::fs::remove_directories(Directory); });
  SmallString<160> FinalPath(Directory);
  sys::path::append(FinalPath, "artifact.o");
  SmallString<160> AliasPath(Directory);
  sys::path::append(AliasPath, ".", "artifact.o");

  NevercOutputSinkHandle First{};
  ASSERT_EQ(API->BeginFileOutput(
                API->Context, Task->handle(), stringView(FinalPath),
                UINT64_C(1024), &First)
                .Code,
            NEVERC_STATUS_OK);

  auto CreatedSession = PluginSession::create(Services, *Plan);
  ASSERT_TRUE(static_cast<bool>(CreatedSession))
      << takeErrorMessage(CreatedSession.takeError());
  std::unique_ptr<PluginSession> OtherSession = std::move(*CreatedSession);
  auto CreatedTask =
      OtherSession->createTask(NEVERC_TASK_TRANSLATION_UNIT);
  ASSERT_TRUE(static_cast<bool>(CreatedTask))
      << takeErrorMessage(CreatedTask.takeError());
  std::unique_ptr<PluginTaskContext> OtherTask = std::move(*CreatedTask);

  NevercOutputSinkHandle WaitingSink{};
  auto Waiting = std::async(std::launch::async, [&] {
    return API->BeginFileOutput(
        API->Context, OtherTask->handle(), stringView(AliasPath),
        UINT64_C(1024), &WaitingSink);
  });
  EXPECT_EQ(Waiting.wait_for(std::chrono::milliseconds(40)),
            std::future_status::timeout);
  OtherSession->cancel();
  NevercStatus WaitingStatus = Waiting.get();
  EXPECT_EQ(WaitingStatus.Code, NEVERC_STATUS_CANCELLED);
  if (WaitingStatus.Code == NEVERC_STATUS_OK)
    EXPECT_EQ(API->OutputAbort(API->Context, OtherTask->handle(),
                               WaitingSink)
                  .Code,
              NEVERC_STATUS_OK);
  EXPECT_TRUE(neverc_handle_is_null(WaitingSink));

  EXPECT_EQ(API->OutputAbort(API->Context, Task->handle(), First).Code,
            NEVERC_STATUS_OK);
  EXPECT_FALSE(OtherTask->end());
  OtherTask.reset();
  EXPECT_FALSE(OtherSession->end());
}

TEST_F(PluginOutputSinkTest,
       FileSyncFailureAbortsAndPreservesExistingPublication) {
  SmallString<128> Directory;
  ASSERT_FALSE(sys::fs::createUniqueDirectory(
      "neverc-plugin-output-sync-failure", Directory));
  auto RemoveDirectory = make_scope_exit(
      [&] { (void)sys::fs::remove_directories(Directory); });
  SmallString<160> FinalPath(Directory);
  sys::path::append(FinalPath, "artifact.o");
  {
    std::error_code Error;
    raw_fd_ostream Existing(FinalPath, Error);
    ASSERT_FALSE(Error);
    Existing << "existing";
  }

  neverc::OutputCoordinator Coordinator;
  auto Transaction = neverc::OutputTransaction::createFile(
      Coordinator, FinalPath, UINT64_C(1024), {},
      [](neverc::OutputFileOperation Operation) {
        return Operation == neverc::OutputFileOperation::SyncStaging
                   ? std::make_error_code(std::errc::io_error)
                   : std::error_code();
      });
  ASSERT_TRUE(static_cast<bool>(Transaction))
      << takeErrorMessage(Transaction.takeError());
  const uint8_t Replacement[] = {'n', 'e', 'w'};
  ASSERT_EQ((*Transaction)->write(Replacement),
            neverc::OutputTransactionResult::Success);
  EXPECT_EQ((*Transaction)->finish(),
            neverc::OutputTransactionResult::IOFailure);
  const neverc::OutputTransactionSummary Summary =
      (*Transaction)->summary();
  EXPECT_EQ(Summary.State, neverc::OutputTransactionState::Aborted);
  EXPECT_EQ(Summary.Flags, 0U);
  auto Contents = MemoryBuffer::getFile(FinalPath);
  ASSERT_TRUE(static_cast<bool>(Contents));
  EXPECT_EQ((*Contents)->getBuffer(), "existing");
}

TEST_F(PluginOutputSinkTest,
       FileStagingWriteFailureAbortsBeforePublication) {
  SmallString<128> Directory;
  ASSERT_FALSE(sys::fs::createUniqueDirectory(
      "neverc-plugin-output-write-failure", Directory));
  auto RemoveDirectory = make_scope_exit(
      [&] { (void)sys::fs::remove_directories(Directory); });
  SmallString<160> FinalPath(Directory);
  sys::path::append(FinalPath, "artifact.o");

  neverc::OutputCoordinator Coordinator;
  auto Transaction = neverc::OutputTransaction::createFile(
      Coordinator, FinalPath, UINT64_C(1024), {},
      [](neverc::OutputFileOperation Operation) {
        return Operation == neverc::OutputFileOperation::WriteStaging
                   ? std::make_error_code(std::errc::no_space_on_device)
                   : std::error_code();
      });
  ASSERT_TRUE(static_cast<bool>(Transaction))
      << takeErrorMessage(Transaction.takeError());
  const uint8_t Replacement[] = {'n', 'e', 'w'};
  ASSERT_EQ((*Transaction)->write(Replacement),
            neverc::OutputTransactionResult::Success);
  EXPECT_EQ((*Transaction)->finish(),
            neverc::OutputTransactionResult::IOFailure);
  EXPECT_EQ((*Transaction)->summary().State,
            neverc::OutputTransactionState::Aborted);
  EXPECT_FALSE(sys::fs::exists(FinalPath));
}

TEST_F(PluginOutputSinkTest,
       FailedStagingCleanupEntersNonRetryablePartialState) {
  SmallString<128> Directory;
  ASSERT_FALSE(sys::fs::createUniqueDirectory(
      "neverc-plugin-output-cleanup-failure", Directory));
  auto RemoveDirectory = make_scope_exit(
      [&] { (void)sys::fs::remove_directories(Directory); });
  SmallString<160> FinalPath(Directory);
  sys::path::append(FinalPath, "artifact.o");

  neverc::OutputCoordinator Coordinator;
  auto Transaction = neverc::OutputTransaction::createFile(
      Coordinator, FinalPath, UINT64_C(1024), {},
      [](neverc::OutputFileOperation Operation) {
        if (Operation == neverc::OutputFileOperation::SyncStaging ||
            Operation == neverc::OutputFileOperation::DiscardStaging)
          return std::make_error_code(std::errc::io_error);
        return std::error_code();
      });
  ASSERT_TRUE(static_cast<bool>(Transaction))
      << takeErrorMessage(Transaction.takeError());
  const uint8_t Replacement[] = {'n', 'e', 'w'};
  ASSERT_EQ((*Transaction)->write(Replacement),
            neverc::OutputTransactionResult::Success);
  EXPECT_EQ((*Transaction)->finish(),
            neverc::OutputTransactionResult::FailedPartial);

  const neverc::OutputTransactionSummary Summary =
      (*Transaction)->summary();
  EXPECT_EQ(Summary.State, neverc::OutputTransactionState::FailedPartial);
  EXPECT_EQ(Summary.Flags & neverc::OutputMayBePartial,
            neverc::OutputMayBePartial);
  EXPECT_EQ(Summary.Flags & neverc::OutputRecoveryRequired,
            neverc::OutputRecoveryRequired);
  EXPECT_FALSE(sys::fs::exists(FinalPath));
  EXPECT_EQ((*Transaction)->abort(),
            neverc::OutputTransactionResult::FailedPartial);
}

TEST_F(PluginOutputSinkTest,
       FilePublishFailureAbortsAndPreservesExistingPublication) {
  SmallString<128> Directory;
  ASSERT_FALSE(sys::fs::createUniqueDirectory(
      "neverc-plugin-output-publish-failure", Directory));
  auto RemoveDirectory = make_scope_exit(
      [&] { (void)sys::fs::remove_directories(Directory); });
  SmallString<160> FinalPath(Directory);
  sys::path::append(FinalPath, "artifact.o");
  {
    std::error_code Error;
    raw_fd_ostream Existing(FinalPath, Error);
    ASSERT_FALSE(Error);
    Existing << "existing";
  }

  neverc::OutputCoordinator Coordinator;
  auto Transaction = neverc::OutputTransaction::createFile(
      Coordinator, FinalPath, UINT64_C(1024), {},
      [](neverc::OutputFileOperation Operation) {
        return Operation == neverc::OutputFileOperation::Publish
                   ? std::make_error_code(std::errc::permission_denied)
                   : std::error_code();
      });
  ASSERT_TRUE(static_cast<bool>(Transaction))
      << takeErrorMessage(Transaction.takeError());
  const uint8_t Replacement[] = {'n', 'e', 'w'};
  ASSERT_EQ((*Transaction)->write(Replacement),
            neverc::OutputTransactionResult::Success);
  ASSERT_EQ((*Transaction)->finish(),
            neverc::OutputTransactionResult::Success);
  auto Commit = (*Transaction)->commit();
  EXPECT_FALSE(static_cast<bool>(Commit));
  consumeError(Commit.takeError());
  const neverc::OutputTransactionSummary Summary =
      (*Transaction)->summary();
  EXPECT_EQ(Summary.State, neverc::OutputTransactionState::Aborted);
  EXPECT_EQ(Summary.Flags, 0U);
  auto Contents = MemoryBuffer::getFile(FinalPath);
  ASSERT_TRUE(static_cast<bool>(Contents));
  EXPECT_EQ((*Contents)->getBuffer(), "existing");
}

TEST_F(PluginOutputSinkTest,
       DirectorySyncFailureKeepsPublishedOutputAndMarksDurabilityUnknown) {
  SmallString<128> Directory;
  ASSERT_FALSE(sys::fs::createUniqueDirectory(
      "neverc-plugin-output-directory-sync-failure", Directory));
  auto RemoveDirectory = make_scope_exit(
      [&] { (void)sys::fs::remove_directories(Directory); });
  SmallString<160> FinalPath(Directory);
  sys::path::append(FinalPath, "artifact.o");

  neverc::OutputCoordinator Coordinator;
  auto Transaction = neverc::OutputTransaction::createFile(
      Coordinator, FinalPath, UINT64_C(1024), {},
      [](neverc::OutputFileOperation Operation) {
        return Operation == neverc::OutputFileOperation::SyncDirectory
                   ? std::make_error_code(std::errc::io_error)
                   : std::error_code();
      });
  ASSERT_TRUE(static_cast<bool>(Transaction))
      << takeErrorMessage(Transaction.takeError());
  const uint8_t Replacement[] = {'n', 'e', 'w'};
  ASSERT_EQ((*Transaction)->write(Replacement),
            neverc::OutputTransactionResult::Success);
  ASSERT_EQ((*Transaction)->finish(),
            neverc::OutputTransactionResult::Success);
  auto Commit = (*Transaction)->commit();
  EXPECT_FALSE(static_cast<bool>(Commit));
  consumeError(Commit.takeError());

  const neverc::OutputTransactionSummary Summary =
      (*Transaction)->summary();
  EXPECT_EQ(Summary.State, neverc::OutputTransactionState::Committed);
  EXPECT_EQ(Summary.Flags & neverc::OutputPublished,
            neverc::OutputPublished);
  EXPECT_EQ(Summary.Flags & neverc::OutputDurabilityUnconfirmed,
            neverc::OutputDurabilityUnconfirmed);
  EXPECT_EQ(Summary.Flags & neverc::OutputDurable, 0U);
  auto Contents = MemoryBuffer::getFile(FinalPath);
  ASSERT_TRUE(static_cast<bool>(Contents));
  EXPECT_EQ((*Contents)->getBuffer(), "new");
}

TEST_F(PluginOutputSinkTest,
       OutputCoordinatorRejectsCanonicalAliasReentryBySameOwner) {
  SmallString<128> Directory;
  ASSERT_FALSE(sys::fs::createUniqueDirectory(
      "neverc-plugin-output-coordinator-owner", Directory));
  auto RemoveDirectory = make_scope_exit(
      [&] { (void)sys::fs::remove_directories(Directory); });
  SmallString<160> FinalPath(Directory);
  sys::path::append(FinalPath, "artifact.o");
  SmallString<160> AliasPath(Directory);
  sys::path::append(AliasPath, ".", "artifact.o");

  neverc::OutputCoordinator Coordinator;
  const neverc::OutputLeaseOwner Owner{UINT64_C(17), UINT64_C(41)};
  auto First = Coordinator.acquire(FinalPath, {}, Owner);
  ASSERT_TRUE(static_cast<bool>(First))
      << takeErrorMessage(First.takeError());
  auto Duplicate = Coordinator.acquire(AliasPath, {}, Owner);
  ASSERT_FALSE(static_cast<bool>(Duplicate));
  EXPECT_EQ(errorToErrorCode(Duplicate.takeError()),
            std::make_error_code(std::errc::file_exists));
}

TEST_F(PluginOutputSinkTest,
       OutputCoordinatorWaitIsCancellableAndLeaseIsReusable) {
  SmallString<128> Directory;
  ASSERT_FALSE(sys::fs::createUniqueDirectory(
      "neverc-plugin-output-coordinator-cancel", Directory));
  auto RemoveDirectory = make_scope_exit(
      [&] { (void)sys::fs::remove_directories(Directory); });
  SmallString<160> FinalPath(Directory);
  sys::path::append(FinalPath, "artifact.o");
  SmallString<160> AliasPath(Directory);
  sys::path::append(AliasPath, ".", "artifact.o");

  neverc::OutputCoordinator Coordinator;
  auto First = Coordinator.acquire(
      FinalPath, {}, neverc::OutputLeaseOwner{UINT64_C(17), UINT64_C(41)});
  ASSERT_TRUE(static_cast<bool>(First))
      << takeErrorMessage(First.takeError());

  std::atomic<bool> Cancelled{false};
  auto Waiting = std::async(std::launch::async, [&] {
    return Coordinator.acquire(
        AliasPath,
        [&] { return Cancelled.load(std::memory_order_acquire); },
        neverc::OutputLeaseOwner{UINT64_C(17), UINT64_C(42)});
  });
  EXPECT_EQ(Waiting.wait_for(std::chrono::milliseconds(40)),
            std::future_status::timeout);
  Cancelled.store(true, std::memory_order_release);
  auto CancelledLease = Waiting.get();
  ASSERT_FALSE(static_cast<bool>(CancelledLease));
  EXPECT_EQ(errorToErrorCode(CancelledLease.takeError()),
            std::make_error_code(std::errc::operation_canceled));

  First->release();
  auto Reused = Coordinator.acquire(
      AliasPath, {},
      neverc::OutputLeaseOwner{UINT64_C(17), UINT64_C(42)});
  ASSERT_TRUE(static_cast<bool>(Reused))
      << takeErrorMessage(Reused.takeError());
}

TEST_F(PluginOutputSinkTest,
       StreamOutputStagesUntilCommitAndDoesNotFlushTwice) {
  std::string Captured;
  raw_string_ostream Stream(Captured);
  ASSERT_FALSE(bindPluginOutputStream(
      *Task, NEVERC_OUTPUT_STREAM_STDOUT, Stream));

  NevercOutputSinkHandle Sink{};
  ASSERT_EQ(API->BeginStreamOutput(
                API->Context, Task->handle(),
                NEVERC_OUTPUT_STREAM_STDOUT, UINT64_C(1024), &Sink)
                .Code,
            NEVERC_STATUS_OK);
  const uint8_t Bytes[] = {'t', 'r', 'a', 'c', 'e'};
  ASSERT_EQ(API->OutputWrite(API->Context, Task->handle(), Sink,
                             {Bytes, sizeof(Bytes)})
                .Code,
            NEVERC_STATUS_OK);
  NevercOutputSeal Seal{};
  Seal.Header.StructSize = sizeof(Seal);
  ASSERT_EQ(API->OutputFinish(API->Context, Task->handle(), Sink, &Seal).Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Seal.Kind, NEVERC_OUTPUT_STREAM);
  EXPECT_TRUE(Captured.empty());

  auto FirstCommit = hostCommitPluginOutput(*Task, Seal.Handle);
  ASSERT_TRUE(static_cast<bool>(FirstCommit))
      << takeErrorMessage(FirstCommit.takeError());
  Stream.flush();
  EXPECT_EQ(Captured, "trace");

  auto SecondCommit = hostCommitPluginOutput(*Task, Seal.Handle);
  ASSERT_TRUE(static_cast<bool>(SecondCommit))
      << takeErrorMessage(SecondCommit.takeError());
  Stream.flush();
  EXPECT_EQ(Captured, "trace");
}

TEST_F(PluginOutputSinkTest,
       DependencyRecordsOwnCanonicalMetadataAndDigest) {
  std::string Path = "/virtual/include/config.h";
  std::string Provider = "org.neverc.test.vfs";
  std::array<uint8_t, 32> Digest{};
  for (size_t Index = 0; Index != Digest.size(); ++Index)
    Digest[Index] = static_cast<uint8_t>(Index);

  NevercDependencyDescriptor Descriptor{};
  Descriptor.Header = {sizeof(Descriptor), NEVERC_IO_API_MAJOR,
                       NEVERC_IO_API_MINOR, 0};
  Descriptor.CanonicalPath = stringView(Path);
  Descriptor.ContentDigest = {Digest.data(), Digest.size()};
  Descriptor.Kind = NEVERC_INPUT_DEPENDENCY_INCLUDE;
  Descriptor.System = NEVERC_FALSE;
  Descriptor.ProviderID = stringView(Provider);
  NevercDependencyHandle Dependency{};
  ASSERT_EQ(API->RecordDependency(API->Context, Task->handle(), &Descriptor,
                                  &Dependency)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_FALSE(neverc_handle_is_null(Dependency));

  Path.assign(Path.size(), 'x');
  Provider.assign(Provider.size(), 'y');
  Digest.fill(0xff);
  auto Records = getPluginDependencies(*Task);
  ASSERT_EQ(Records.size(), 1U);
  EXPECT_EQ(Records[0].CanonicalPath, "/virtual/include/config.h");
  EXPECT_EQ(Records[0].ProviderID, "org.neverc.test.vfs");
  EXPECT_EQ(Records[0].Kind, NEVERC_INPUT_DEPENDENCY_INCLUDE);
  EXPECT_FALSE(Records[0].System);
  for (size_t Index = 0; Index != Records[0].ContentDigest.size(); ++Index)
    EXPECT_EQ(Records[0].ContentDigest[Index], Index);
}

} // namespace
