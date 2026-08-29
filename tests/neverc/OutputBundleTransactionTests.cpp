#include "Inputs/Plugin/PostEmitPlugin.h"
#include "Link/LinkOutputBundle.h"
#include "PluginBinaryImageTestSupport.h"
#include "neverc/Foundation/AndroidKernelReleasePublisher.h"
#include "neverc/Foundation/Core/OutputBundleTransaction.h"
#include "neverc/Foundation/Core/OutputCoordinator.h"
#include "neverc/Foundation/Core/OutputTransaction.h"
#include "neverc/Plugin/Schema/PluginPhaseSchema.inc"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#ifdef __APPLE__
#include <membership.h>
#include <sys/acl.h>
#include <unistd.h>
#endif
#ifdef _WIN32
#ifndef NOGDI
#define NOGDI
#endif
#include "llvm/Support/Windows/WindowsSupport.h"
#include <aclapi.h>
#ifndef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
#define SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE 0x2
#endif
#endif
#include "gtest/gtest.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <iterator>
#include <memory>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

using namespace llvm;
using namespace neverc::plugin;
using namespace neverc::plugin::test_support;

namespace {

#ifdef _WIN32
testing::AssertionResult hasProtectedOwnerOnlyDacl(StringRef Path) {
  SmallVector<wchar_t, 256> WidePath;
  if (std::error_code EC = sys::windows::widenPath(Path, WidePath))
    return testing::AssertionFailure() << EC.message();

  PSID Owner = nullptr;
  PACL Dacl = nullptr;
  PSECURITY_DESCRIPTOR SecurityDescriptor = nullptr;
  const DWORD SecurityError = ::GetNamedSecurityInfoW(
      WidePath.data(), SE_FILE_OBJECT,
      OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &Owner, nullptr,
      &Dacl, nullptr, &SecurityDescriptor);
  if (SecurityError != ERROR_SUCCESS)
    return testing::AssertionFailure()
           << "GetNamedSecurityInfoW failed with error " << SecurityError;
  auto ReleaseDescriptor =
      make_scope_exit([&] { ::LocalFree(SecurityDescriptor); });

  SECURITY_DESCRIPTOR_CONTROL Control = 0;
  DWORD Revision = 0;
  if (!::GetSecurityDescriptorControl(SecurityDescriptor, &Control, &Revision))
    return testing::AssertionFailure()
           << "GetSecurityDescriptorControl failed with error "
           << ::GetLastError();
  if ((Control & SE_DACL_PROTECTED) == 0)
    return testing::AssertionFailure() << "DACL inherits parent permissions";
  if (!Owner || !Dacl)
    return testing::AssertionFailure() << "owner or DACL is missing";

  HANDLE RawToken = nullptr;
  if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &RawToken))
    return testing::AssertionFailure()
           << "OpenProcessToken failed with error " << ::GetLastError();
  auto ReleaseToken = make_scope_exit([&] { ::CloseHandle(RawToken); });
  DWORD TokenSize = 0;
  if (::GetTokenInformation(RawToken, TokenUser, nullptr, 0, &TokenSize) ||
      ::GetLastError() != ERROR_INSUFFICIENT_BUFFER)
    return testing::AssertionFailure()
           << "could not size TokenUser information";
  std::vector<uint8_t> TokenInformation(TokenSize);
  if (!::GetTokenInformation(RawToken, TokenUser, TokenInformation.data(),
                             TokenSize, &TokenSize))
    return testing::AssertionFailure()
           << "GetTokenInformation failed with error " << ::GetLastError();
  PSID User = reinterpret_cast<TOKEN_USER *>(TokenInformation.data())->User.Sid;
  if (!::EqualSid(Owner, User))
    return testing::AssertionFailure() << "file owner is not the token user";

  ACL_SIZE_INFORMATION AclInfo{};
  if (!::GetAclInformation(Dacl, &AclInfo, sizeof(AclInfo), AclSizeInformation))
    return testing::AssertionFailure()
           << "GetAclInformation failed with error " << ::GetLastError();
  if (AclInfo.AceCount != 1)
    return testing::AssertionFailure()
           << "expected one owner ACE, found " << AclInfo.AceCount;

  void *RawAce = nullptr;
  if (!::GetAce(Dacl, 0, &RawAce))
    return testing::AssertionFailure()
           << "GetAce failed with error " << ::GetLastError();
  const auto *Header = static_cast<const ACE_HEADER *>(RawAce);
  if (Header->AceType != ACCESS_ALLOWED_ACE_TYPE)
    return testing::AssertionFailure() << "owner ACE does not grant access";
  const auto *Ace = static_cast<const ACCESS_ALLOWED_ACE *>(RawAce);
  PSID AceSid = const_cast<DWORD *>(&Ace->SidStart);
  if (!::EqualSid(Owner, AceSid))
    return testing::AssertionFailure() << "sole ACE does not name the owner";
  if ((Ace->Mask & FILE_ALL_ACCESS) != FILE_ALL_ACCESS)
    return testing::AssertionFailure() << "owner lacks full file access";
  return testing::AssertionSuccess();
}

std::error_code createWindowsFileSymlink(StringRef Target, StringRef Link) {
  SmallVector<wchar_t, 256> WideTarget;
  SmallVector<wchar_t, 256> WideLink;
  if (std::error_code EC = sys::windows::widenPath(Target, WideTarget))
    return EC;
  if (std::error_code EC = sys::windows::widenPath(Link, WideLink))
    return EC;

  if (::CreateSymbolicLinkW(WideLink.data(), WideTarget.data(),
                            SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE))
    return {};
  DWORD WindowsError = ::GetLastError();
  if (WindowsError == ERROR_INVALID_PARAMETER &&
      ::CreateSymbolicLinkW(WideLink.data(), WideTarget.data(), 0))
    return {};
  if (WindowsError == ERROR_INVALID_PARAMETER)
    WindowsError = ::GetLastError();
  if (WindowsError == ERROR_PRIVILEGE_NOT_HELD)
    return std::make_error_code(std::errc::permission_denied);
  return mapWindowsError(WindowsError);
}
#endif

TEST(PluginBinaryImageTest, MainPublishFailureRestoresTheOldBundle) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Image = makeImage(Scope);
  ASSERT_TRUE(static_cast<bool>(Image)) << errorText(Image.takeError());
  TemporaryDirectory Directory;
  const std::string Main = Directory.file("program.bin");
  const std::string Map = Directory.file("program.map");
  writeFile(Main, "old-main");
  writeFile(Map, "old-map");
  neverc::OutputCoordinator Coordinator;
  auto Pipeline = LinkOutputPipeline::create(Scope.task(), Coordinator);
  ASSERT_TRUE(static_cast<bool>(Pipeline)) << errorText(Pipeline.takeError());
  const std::vector<PluginLinkSideOutput> Sides = {
      {"map", Map, {'n', 'e', 'w', '-', 'm', 'a', 'p'}}};
  auto Output = (*Pipeline)->execute(
      *Image, Main, Sides,
      [](neverc::OutputBundleOperation Operation, StringRef) {
        return Operation == neverc::OutputBundleOperation::PublishMain
                   ? std::make_error_code(std::errc::io_error)
                   : std::error_code();
      });
  EXPECT_FALSE(static_cast<bool>(Output));
  const std::string Failure =
      Output ? std::string() : errorText(Output.takeError());
  EXPECT_NE(Failure.find(std::make_error_code(std::errc::io_error).message()),
            std::string::npos)
      << Failure;
  EXPECT_EQ(readFile(Main), "old-main");
  EXPECT_EQ(readFile(Map), "old-map");
}

TEST(PluginBinaryImageTest,
     BundleFaultPointsPreservePublicationAndRollbackPhaseOrder) {
  using Operation = neverc::OutputBundleOperation;

  TemporaryDirectory Directory;
  const std::string Main = Directory.file("module.ko");
  const std::string Map = Directory.file("module.ko.symbols.json");
  writeFile(Main, "old-main");
  writeFile(Map, "old-map");
  neverc::OutputCoordinator Coordinator;
  std::vector<neverc::OutputBundleFile> Outputs = {
      {"main", Main, {'n', 'e', 'w', '-', 'm', 'a', 'i', 'n'}, true},
      {"map", Map, {'n', 'e', 'w', '-', 'm', 'a', 'p'}},
  };

  std::vector<Operation> SuccessfulOperations;
  auto Bundle = neverc::OutputBundleTransaction::create(
      Coordinator, Outputs,
      /*IsCancelled=*/{}, [&](Operation Current, StringRef) {
        SuccessfulOperations.push_back(Current);
        return std::error_code();
      });
  ASSERT_TRUE(static_cast<bool>(Bundle)) << errorText(Bundle.takeError());
  auto Committed = (*Bundle)->commit();
  ASSERT_TRUE(static_cast<bool>(Committed)) << errorText(Committed.takeError());

  const auto IndexOf = [](ArrayRef<Operation> Operations, Operation Expected) {
    const auto It = llvm::find(Operations, Expected);
    EXPECT_NE(It, Operations.end()) << static_cast<unsigned>(Expected);
    return static_cast<size_t>(std::distance(Operations.begin(), It));
  };
  const std::array OrderedSuccess = {
      Operation::WriteStaging,    Operation::SyncStaging,
      Operation::CreateJournal,   Operation::BackupExisting,
      Operation::PublishSide,     Operation::PublishMain,
      Operation::CompleteJournal, Operation::SyncCompletedJournal,
      Operation::CleanupJournal,
  };
  for (size_t Index = 1; Index != OrderedSuccess.size(); ++Index)
    EXPECT_LT(IndexOf(SuccessfulOperations, OrderedSuccess[Index - 1]),
              IndexOf(SuccessfulOperations, OrderedSuccess[Index]));
  for (Operation Expected :
       {Operation::SyncJournal, Operation::SyncRecoveryState,
        Operation::SyncDirectory})
    EXPECT_NE(llvm::find(SuccessfulOperations, Expected),
              SuccessfulOperations.end())
        << static_cast<unsigned>(Expected);

  writeFile(Main, "rollback-main");
  writeFile(Map, "rollback-map");
  std::vector<Operation> RollbackOperations;
  auto RollbackBundle = neverc::OutputBundleTransaction::create(
      Coordinator, Outputs,
      /*IsCancelled=*/{}, [&](Operation Current, StringRef) {
        RollbackOperations.push_back(Current);
        return Current == Operation::PublishMain
                   ? std::make_error_code(std::errc::io_error)
                   : std::error_code();
      });
  ASSERT_TRUE(static_cast<bool>(RollbackBundle))
      << errorText(RollbackBundle.takeError());
  auto Failed = (*RollbackBundle)->commit();
  ASSERT_FALSE(static_cast<bool>(Failed));
  consumeError(Failed.takeError());
  for (Operation Expected :
       {Operation::PublishMain, Operation::RemovePublished,
        Operation::RestoreBackup, Operation::DiscardStaging})
    EXPECT_NE(llvm::find(RollbackOperations, Expected),
              RollbackOperations.end())
        << static_cast<unsigned>(Expected);
  EXPECT_EQ(readFile(Main), "rollback-main");
  EXPECT_EQ(readFile(Map), "rollback-map");
}

TEST(PluginBinaryImageTest,
     ExistingBundleRemainsVisibleUntilAtomicReplacementsBegin) {
  TemporaryDirectory Directory;
  const std::string Main = Directory.file("module.ko");
  const std::string Map = Directory.file("module.ko.symbols.json");
  writeFile(Main, "old-main");
  writeFile(Map, "old-map");
  neverc::OutputCoordinator Coordinator;
  std::vector<neverc::OutputBundleFile> Outputs = {
      {"main", Main, {'n', 'e', 'w', '-', 'm', 'a', 'i', 'n'}, true},
      {"map", Map, {'n', 'e', 'w', '-', 'm', 'a', 'p'}},
  };
  bool ReachedSidePublish = false;
  bool SawOldMain = false;
  bool SawOldMap = false;
  auto Bundle = neverc::OutputBundleTransaction::create(
      Coordinator, Outputs,
      /*IsCancelled=*/{},
      [&](neverc::OutputBundleOperation Operation, StringRef) {
        if (Operation != neverc::OutputBundleOperation::PublishSide)
          return std::error_code();
        ReachedSidePublish = true;
        SawOldMain = readFile(Main) == "old-main";
        SawOldMap = readFile(Map) == "old-map";
        return std::make_error_code(std::errc::io_error);
      });
  ASSERT_TRUE(static_cast<bool>(Bundle)) << errorText(Bundle.takeError());
  auto Committed = (*Bundle)->commit();
  ASSERT_FALSE(static_cast<bool>(Committed));
  consumeError(Committed.takeError());
  EXPECT_TRUE(ReachedSidePublish);
  EXPECT_TRUE(SawOldMain);
  EXPECT_TRUE(SawOldMap);
  EXPECT_EQ(readFile(Main), "old-main");
  EXPECT_EQ(readFile(Map), "old-map");
}

TEST(PluginBinaryImageTest,
     BundleRejectsMissingCaseAliasedOutputPathsBeforePublication) {
  TemporaryDirectory Directory;
  const std::string Main = Directory.file("MODULE.KO");
  const std::string Map = Directory.file("module.ko");
  writeFile(Main, "old-output");

  bool Equivalent = false;
  if (std::error_code EC = sys::fs::equivalent(Main, Map, Equivalent))
    GTEST_SKIP() << "cannot query case aliasing: " << EC.message();
  if (!Equivalent)
    GTEST_SKIP() << "test filesystem is case-sensitive";
  ASSERT_FALSE(sys::fs::remove(Main));
  ASSERT_FALSE(sys::fs::exists(Main));
  ASSERT_FALSE(sys::fs::exists(Map));

  neverc::OutputCoordinator Coordinator;
  std::vector<neverc::OutputBundleFile> Outputs = {
      {"main", Main, {'n', 'e', 'w', '-', 'm', 'a', 'i', 'n'}, true},
      {"map", Map, {'n', 'e', 'w', '-', 'm', 'a', 'p'}},
  };
  auto Bundle = neverc::OutputBundleTransaction::create(Coordinator, Outputs);
  ASSERT_FALSE(static_cast<bool>(Bundle));
  const std::string Failure = errorText(Bundle.takeError());
  EXPECT_NE(Failure.find("paths must be unique"), std::string::npos) << Failure;
  EXPECT_FALSE(sys::fs::exists(Main));
  EXPECT_FALSE(sys::fs::exists(Map));
}

#if defined(_WIN32)
TEST(PluginBinaryImageTest, BundleRejectsWindowsTrailingDotOrSpaceOutputs) {
  TemporaryDirectory Directory;
  for (StringRef Name : {"module.ko.", "module.ko ", ".neverc-output.lock."}) {
    SCOPED_TRACE(Name.str());
    neverc::OutputCoordinator Coordinator;
    std::vector<neverc::OutputBundleFile> Outputs = {
        {"main", Directory.file(Name), {'n', 'e', 'w'}, true},
    };
    auto Bundle = neverc::OutputBundleTransaction::create(Coordinator, Outputs);
    ASSERT_FALSE(static_cast<bool>(Bundle));
    const std::string Failure = errorText(Bundle.takeError());
    EXPECT_NE(Failure.find("trailing dot or space"), std::string::npos)
        << Failure;
  }
}
#endif

#if defined(__APPLE__)
TEST(PluginBinaryImageTest,
     BundleRejectsMissingUnicodeNormalizationAliasesBeforePublication) {
  TemporaryDirectory Directory;
  const std::string Main = Directory.file("caf\xc3\xa9.ko");
  const std::string Map = Directory.file("cafe\xcc\x81.ko");
  writeFile(Main, "probe");

  bool Equivalent = false;
  if (std::error_code EC = sys::fs::equivalent(Main, Map, Equivalent))
    GTEST_SKIP() << "cannot query Unicode path aliasing: " << EC.message();
  if (!Equivalent)
    GTEST_SKIP() << "test filesystem preserves Unicode normalization";
  ASSERT_FALSE(sys::fs::remove(Main));
  ASSERT_FALSE(sys::fs::exists(Main));
  ASSERT_FALSE(sys::fs::exists(Map));

  neverc::OutputCoordinator Coordinator;
  std::vector<neverc::OutputBundleFile> Outputs = {
      {"main", Main, {'n', 'e', 'w', '-', 'm', 'a', 'i', 'n'}, true},
      {"map", Map, {'n', 'e', 'w', '-', 'm', 'a', 'p'}},
  };
  auto Bundle = neverc::OutputBundleTransaction::create(Coordinator, Outputs);
  ASSERT_FALSE(static_cast<bool>(Bundle));
  const std::string Failure = errorText(Bundle.takeError());
  EXPECT_NE(Failure.find("paths must be unique"), std::string::npos) << Failure;
  EXPECT_FALSE(sys::fs::exists(Main));
  EXPECT_FALSE(sys::fs::exists(Map));
}
#endif

TEST(PluginBinaryImageTest, SideDirectoryIsSyncedBeforeMainPublication) {
  TemporaryDirectory Directory;
  const std::string Main = Directory.file("module.ko");
  const std::string Map = Directory.file("module.ko.symbols.json");
  writeFile(Main, "old-main");
  writeFile(Map, "old-map");
  neverc::OutputCoordinator Coordinator;
  std::vector<neverc::OutputBundleFile> Outputs = {
      {"main", Main, {'n', 'e', 'w', '-', 'm', 'a', 'i', 'n'}, true},
      {"map", Map, {'n', 'e', 'w', '-', 'm', 'a', 'p'}},
  };
  bool SyncedSideDirectory = false;
  bool MainObservedSideSync = false;
  auto CanonicalMap = Coordinator.canonicalize(Map);
  ASSERT_TRUE(static_cast<bool>(CanonicalMap))
      << errorText(CanonicalMap.takeError());
  const std::string CanonicalMapPath = *CanonicalMap;
  auto Bundle = neverc::OutputBundleTransaction::create(
      Coordinator, Outputs,
      /*IsCancelled=*/{},
      [&](neverc::OutputBundleOperation Operation, StringRef Path) {
        if (Operation == neverc::OutputBundleOperation::SyncDirectory &&
            Path == CanonicalMapPath)
          SyncedSideDirectory = true;
        if (Operation != neverc::OutputBundleOperation::PublishMain)
          return std::error_code();
        MainObservedSideSync = SyncedSideDirectory;
        return std::make_error_code(std::errc::io_error);
      });
  ASSERT_TRUE(static_cast<bool>(Bundle)) << errorText(Bundle.takeError());
  auto Committed = (*Bundle)->commit();
  ASSERT_FALSE(static_cast<bool>(Committed));
  consumeError(Committed.takeError());

  EXPECT_TRUE(MainObservedSideSync);
  EXPECT_EQ(readFile(Main), "old-main");
  EXPECT_EQ(readFile(Map), "old-map");
}

TEST(PluginBinaryImageTest,
     IndependentCoordinatorsSerializeOverlappingBundlePublication) {
  TemporaryDirectory Directory;
  const std::string Main = Directory.file("module.ko");
  const std::string Map = Directory.file("module.ko.symbols.json");
  writeFile(Main, "old-main");
  writeFile(Map, "old-map");

  neverc::OutputCoordinator FirstCoordinator;
  neverc::OutputCoordinator SecondCoordinator;
  std::vector<neverc::OutputBundleFile> FirstOutputs = {
      {"main", Main, {'f', 'i', 'r', 's', 't', '-', 'm', 'a', 'i', 'n'}, true},
      {"map", Map, {'f', 'i', 'r', 's', 't', '-', 'm', 'a', 'p'}},
  };
  std::vector<neverc::OutputBundleFile> SecondOutputs = {
      {"main",
       Main,
       {'s', 'e', 'c', 'o', 'n', 'd', '-', 'm', 'a', 'i', 'n'},
       true},
      {"map", Map, {'s', 'e', 'c', 'o', 'n', 'd', '-', 'm', 'a', 'p'}},
  };

  std::atomic<bool> ConcurrentCommitFinished = false;
  std::string ConcurrentFailure;
  std::thread ConcurrentCommit;
  std::unique_ptr<neverc::OutputBundleTransaction> SecondTransaction;
  auto FirstTransaction = neverc::OutputBundleTransaction::create(
      FirstCoordinator, FirstOutputs,
      /*IsCancelled=*/{},
      [&](neverc::OutputBundleOperation Operation, StringRef) {
        if (Operation != neverc::OutputBundleOperation::PublishMain)
          return std::error_code();
        ConcurrentCommit = std::thread([&] {
          auto Committed = SecondTransaction->commit();
          if (!Committed)
            ConcurrentFailure = errorText(Committed.takeError());
          ConcurrentCommitFinished = true;
        });
        const auto Deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
        while (!ConcurrentCommitFinished &&
               std::chrono::steady_clock::now() < Deadline)
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        EXPECT_FALSE(ConcurrentCommitFinished)
            << "overlapping publication was not serialized";
        return std::error_code();
      });
  ASSERT_TRUE(static_cast<bool>(FirstTransaction))
      << errorText(FirstTransaction.takeError());
  auto Second =
      neverc::OutputBundleTransaction::create(SecondCoordinator, SecondOutputs);
  ASSERT_TRUE(static_cast<bool>(Second)) << errorText(Second.takeError());
  SecondTransaction = std::move(*Second);
  if (Error E = FirstTransaction->get()->prepare())
    FAIL() << errorText(std::move(E));
  if (Error E = SecondTransaction->prepare())
    FAIL() << errorText(std::move(E));

  auto FirstCommitted = FirstTransaction->get()->commit();
  EXPECT_TRUE(static_cast<bool>(FirstCommitted))
      << errorText(FirstCommitted.takeError());
  ASSERT_TRUE(ConcurrentCommit.joinable());
  ConcurrentCommit.join();

  EXPECT_TRUE(ConcurrentCommitFinished);
  EXPECT_TRUE(ConcurrentFailure.empty()) << ConcurrentFailure;
  EXPECT_EQ(readFile(Main), "second-main");
  EXPECT_EQ(readFile(Map), "second-map");
}

TEST(PluginBinaryImageTest,
     RemovalOnlyBundleSerializesWithOverlappingPublication) {
  TemporaryDirectory Directory;
  const std::string Main = Directory.file("module.ko");
  const std::string Map = Directory.file("module.ko.symbols.json");
  writeFile(Main, "old-main");
  writeFile(Map, "old-map");

  neverc::OutputCoordinator PublisherCoordinator;
  neverc::OutputCoordinator CleanerCoordinator;
  std::vector<neverc::OutputBundleFile> PublishOutputs = {
      {"main", Main, {'n', 'e', 'w', '-', 'm', 'a', 'i', 'n'}, true},
      {"map", Map, {'n', 'e', 'w', '-', 'm', 'a', 'p'}},
  };
  std::vector<neverc::OutputBundleFile> RemoveOutputs = {
      {"main", Main, {}, true, false, neverc::OutputBundleFileAction::Remove},
      {"map", Map, {}, false, false, neverc::OutputBundleFileAction::Remove},
  };

  std::atomic<bool> CleanerFinished = false;
  std::string CleanerFailure;
  std::thread CleanerThread;
  std::unique_ptr<neverc::OutputBundleTransaction> Cleaner;
  auto Publisher = neverc::OutputBundleTransaction::create(
      PublisherCoordinator, PublishOutputs,
      /*IsCancelled=*/{},
      [&](neverc::OutputBundleOperation Operation, StringRef) {
        if (Operation != neverc::OutputBundleOperation::PublishMain)
          return std::error_code();
        CleanerThread = std::thread([&] {
          auto Cleaned = Cleaner->commit();
          if (!Cleaned)
            CleanerFailure = errorText(Cleaned.takeError());
          CleanerFinished = true;
        });
        const auto Deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
        while (!CleanerFinished && std::chrono::steady_clock::now() < Deadline)
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        EXPECT_FALSE(CleanerFinished)
            << "cleanup bypassed the active publication lock";
        return std::error_code();
      });
  ASSERT_TRUE(static_cast<bool>(Publisher)) << errorText(Publisher.takeError());
  auto CleanerOrErr = neverc::OutputBundleTransaction::create(
      CleanerCoordinator, RemoveOutputs);
  ASSERT_TRUE(static_cast<bool>(CleanerOrErr))
      << errorText(CleanerOrErr.takeError());
  Cleaner = std::move(*CleanerOrErr);
  if (Error E = (*Publisher)->prepare())
    FAIL() << errorText(std::move(E));
  if (Error E = Cleaner->prepare())
    FAIL() << errorText(std::move(E));

  auto Published = (*Publisher)->commit();
  EXPECT_TRUE(static_cast<bool>(Published)) << errorText(Published.takeError());
  ASSERT_TRUE(CleanerThread.joinable());
  CleanerThread.join();

  EXPECT_TRUE(CleanerFinished);
  EXPECT_TRUE(CleanerFailure.empty()) << CleanerFailure;
  EXPECT_FALSE(sys::fs::exists(Main));
  EXPECT_FALSE(sys::fs::exists(Map));
}

TEST(PluginBinaryImageTest, PublicationLockWaitHonorsCancellation) {
  TemporaryDirectory Directory;
  const std::string Main = Directory.file("module.ko");
  writeFile(Main, "old-main");

  neverc::OutputCoordinator FirstCoordinator;
  neverc::OutputCoordinator SecondCoordinator;
  std::vector<neverc::OutputBundleFile> FirstOutputs = {
      {"main", Main, {'f', 'i', 'r', 's', 't'}, true},
  };
  std::vector<neverc::OutputBundleFile> SecondOutputs = {
      {"main", Main, {'s', 'e', 'c', 'o', 'n', 'd'}, true},
  };

  std::atomic<bool> CancelSecond = false;
  std::atomic<bool> SawCancellationPoll = false;
  std::string ConcurrentFailure;
  std::thread ConcurrentCommit;
  std::unique_ptr<neverc::OutputBundleTransaction> SecondTransaction;
  auto FirstTransaction = neverc::OutputBundleTransaction::create(
      FirstCoordinator, FirstOutputs,
      /*IsCancelled=*/{},
      [&](neverc::OutputBundleOperation Operation, StringRef) {
        if (Operation != neverc::OutputBundleOperation::PublishMain)
          return std::error_code();
        ConcurrentCommit = std::thread([&] {
          auto Committed = SecondTransaction->commit();
          if (!Committed)
            ConcurrentFailure = errorText(Committed.takeError());
        });
        const auto Deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(1);
        while (!SawCancellationPoll &&
               std::chrono::steady_clock::now() < Deadline)
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        EXPECT_TRUE(SawCancellationPoll)
            << "the competing publisher never entered the lock wait loop";
        CancelSecond = true;
        return std::error_code();
      });
  ASSERT_TRUE(static_cast<bool>(FirstTransaction))
      << errorText(FirstTransaction.takeError());
  auto Second = neverc::OutputBundleTransaction::create(
      SecondCoordinator, SecondOutputs, [&] {
        SawCancellationPoll = true;
        return CancelSecond.load();
      });
  ASSERT_TRUE(static_cast<bool>(Second)) << errorText(Second.takeError());
  SecondTransaction = std::move(*Second);
  if (Error E = FirstTransaction->get()->prepare())
    FAIL() << errorText(std::move(E));
  if (Error E = SecondTransaction->prepare())
    FAIL() << errorText(std::move(E));

  auto FirstCommitted = FirstTransaction->get()->commit();
  EXPECT_TRUE(static_cast<bool>(FirstCommitted))
      << errorText(FirstCommitted.takeError());
  ASSERT_TRUE(ConcurrentCommit.joinable());
  ConcurrentCommit.join();
  EXPECT_NE(ConcurrentFailure.find(
                std::make_error_code(std::errc::operation_canceled).message()),
            std::string::npos)
      << ConcurrentFailure;
  EXPECT_FALSE(ConcurrentFailure.empty());
  EXPECT_EQ(SecondTransaction->summary().State,
            neverc::OutputBundleState::Prepared);
  if (Error E = SecondTransaction->abort())
    ADD_FAILURE() << errorText(std::move(E));
  EXPECT_EQ(readFile(Main), "first");
}

TEST(PluginBinaryImageTest, OwnerOnlyBundleOutputRestrictsPublishedFile) {
  TemporaryDirectory Directory;
  const std::string Main = Directory.file("private-output");
  neverc::OutputBundleFile Output{
      "main", Main, {'p', 'r', 'i', 'v', 'a', 't', 'e'}, true};
  Output.OwnerOnly = true;

  neverc::OutputCoordinator Coordinator;
  std::vector<neverc::OutputBundleFile> Outputs = {std::move(Output)};
  auto Bundle = neverc::OutputBundleTransaction::create(Coordinator, Outputs);
  ASSERT_TRUE(static_cast<bool>(Bundle)) << errorText(Bundle.takeError());
  auto Committed = (*Bundle)->commit();
  ASSERT_TRUE(static_cast<bool>(Committed)) << errorText(Committed.takeError());

#ifdef _WIN32
  EXPECT_TRUE(hasProtectedOwnerOnlyDacl(Main));
#else
  sys::fs::file_status Status;
  ASSERT_FALSE(sys::fs::status(Main, Status));
  EXPECT_EQ(Status.permissions() & sys::fs::owner_all,
            sys::fs::owner_read | sys::fs::owner_write);
  EXPECT_EQ(Status.permissions() & (sys::fs::group_all | sys::fs::others_all),
            sys::fs::no_perms);
#endif
  EXPECT_EQ(readFile(Main), "private");
}

TEST(PluginBinaryImageTest,
     RecoveryJournalAndPublicationLockAreRestrictedToOwner) {
  TemporaryDirectory Directory;
  const std::string Main = Directory.file("module.ko");
  neverc::OutputCoordinator Coordinator;
  std::vector<neverc::OutputBundleFile> Outputs = {
      {"main", Main, {'n', 'e', 'w'}, true},
  };
  auto Bundle = neverc::OutputBundleTransaction::create(Coordinator, Outputs);
  ASSERT_TRUE(static_cast<bool>(Bundle)) << errorText(Bundle.takeError());
  if (Error E = (*Bundle)->prepare())
    FAIL() << errorText(std::move(E));

  const std::string Journal = (*Bundle)->summary().JournalPath;
  ASSERT_FALSE(Journal.empty());
  ASSERT_TRUE(sys::fs::exists(Journal));

  auto ExpectOwnerOnly = [](StringRef Path) {
#ifdef _WIN32
    EXPECT_TRUE(hasProtectedOwnerOnlyDacl(Path));
#else
    sys::fs::file_status Status;
    ASSERT_FALSE(sys::fs::status(Path, Status));
    EXPECT_EQ(Status.permissions() & sys::fs::owner_all,
              sys::fs::owner_read | sys::fs::owner_write);
    EXPECT_EQ(Status.permissions() & (sys::fs::group_all | sys::fs::others_all),
              sys::fs::no_perms);
#endif
  };
  ExpectOwnerOnly(Journal);

  auto Committed = (*Bundle)->commit();
  ASSERT_TRUE(static_cast<bool>(Committed)) << errorText(Committed.takeError());
  const std::string Lock = Directory.file(".neverc-output.lock");
  ASSERT_TRUE(sys::fs::exists(Lock));
  ExpectOwnerOnly(Lock);
}

#ifdef __APPLE__
TEST(PluginBinaryImageTest, PrivateCreationDoesNotInheritDarwinAcl) {
  TemporaryDirectory Directory;
  acl_t ParentAcl = ::acl_init(1);
  ASSERT_NE(ParentAcl, nullptr);
  auto ReleaseParentAcl = make_scope_exit([&] { ::acl_free(ParentAcl); });

  acl_entry_t Entry = nullptr;
  ASSERT_EQ(::acl_create_entry(&ParentAcl, &Entry), 0);
  ASSERT_EQ(::acl_set_tag_type(Entry, ACL_EXTENDED_ALLOW), 0);
  uuid_t User;
  ASSERT_EQ(::mbr_uid_to_uuid(::getuid(), User), 0);
  ASSERT_EQ(::acl_set_qualifier(Entry, User), 0);
  acl_permset_t Permissions = nullptr;
  ASSERT_EQ(::acl_get_permset(Entry, &Permissions), 0);
  ASSERT_EQ(::acl_clear_perms(Permissions), 0);
  ASSERT_EQ(::acl_add_perm(Permissions, ACL_READ_DATA), 0);
  acl_flagset_t Flags = nullptr;
  ASSERT_EQ(::acl_get_flagset_np(Entry, &Flags), 0);
  ASSERT_EQ(::acl_clear_flags_np(Flags), 0);
  ASSERT_EQ(::acl_add_flag_np(Flags, ACL_ENTRY_FILE_INHERIT), 0);
  ASSERT_EQ(
      ::acl_set_file(Directory.Path.c_str(), ACL_TYPE_EXTENDED, ParentAcl), 0);

  const std::string Control = Directory.file("inherited-control");
  writeFile(Control, "");
  acl_t ControlAcl = ::acl_get_file(Control.c_str(), ACL_TYPE_EXTENDED);
  ASSERT_NE(ControlAcl, nullptr);
  auto ReleaseControlAcl = make_scope_exit([&] { ::acl_free(ControlAcl); });
  acl_entry_t ControlEntry = nullptr;
  ASSERT_EQ(::acl_get_entry(ControlAcl, ACL_FIRST_ENTRY, &ControlEntry), 0);

  SmallString<160> Model(Directory.file("private-%%%%%%%%"));
  SmallString<160> PrivatePath;
  int PrivateFD = -1;
  ASSERT_FALSE(sys::fs::createUniqueFile(Model, PrivateFD, PrivatePath,
                                         sys::fs::OF_NoInherit, 0600));
  ASSERT_EQ(::close(PrivateFD), 0);
  ASSERT_TRUE(sys::fs::exists(PrivatePath));
  errno = 0;
  acl_t PrivateAcl = ::acl_get_file(PrivatePath.c_str(), ACL_TYPE_EXTENDED);
  if (!PrivateAcl) {
    EXPECT_EQ(errno, ENOENT);
    return;
  }
  auto ReleasePrivateAcl = make_scope_exit([&] { ::acl_free(PrivateAcl); });
  acl_entry_t PrivateEntry = nullptr;
  EXPECT_EQ(::acl_get_entry(PrivateAcl, ACL_FIRST_ENTRY, &PrivateEntry), -1);
}
#endif

TEST(PluginBinaryImageTest,
     PublishRejectsSymlinkOutputsWithoutChangingExistingBundle) {
#ifdef _WIN32
  TemporaryDirectory Directory;
  const std::string MainTarget = Directory.file("old-module.ko");
  const std::string Main = Directory.file("module.ko");
  const std::string Map = Directory.file("module.ko.symbols.json");
  writeFile(MainTarget, "old-main");
  writeFile(Map, "old-map");
  if (std::error_code EC = createWindowsFileSymlink(MainTarget, Main)) {
    if (EC == std::make_error_code(std::errc::permission_denied))
      GTEST_SKIP() << "Windows symbolic-link creation is unavailable";
    FAIL() << "CreateSymbolicLinkW failed: " << EC.message();
  }
  SmallVector<wchar_t, 256> WideMain;
  ASSERT_FALSE(sys::windows::widenPath(Main, WideMain));
#else
  TemporaryDirectory Directory;
  const std::string MainTarget = Directory.file("old-module.ko");
  const std::string Main = Directory.file("module.ko");
  const std::string Map = Directory.file("module.ko.symbols.json");
  writeFile(MainTarget, "old-main");
  writeFile(Map, "old-map");
  ASSERT_FALSE(sys::fs::create_link(MainTarget, Main));
#endif

  neverc::OutputCoordinator Coordinator;
  std::vector<neverc::OutputBundleFile> Outputs = {
      {"main", Main, {'n', 'e', 'w', '-', 'm', 'a', 'i', 'n'}, true},
      {"map", Map, {'n', 'e', 'w', '-', 'm', 'a', 'p'}},
  };
  auto Bundle = neverc::OutputBundleTransaction::create(Coordinator, Outputs);
  ASSERT_TRUE(static_cast<bool>(Bundle)) << errorText(Bundle.takeError());
  auto Committed = (*Bundle)->commit();
  ASSERT_FALSE(static_cast<bool>(Committed));
  const std::string Failure = errorText(Committed.takeError());
  EXPECT_NE(Failure.find("output paths are unsupported"), std::string::npos)
      << Failure;

  const neverc::OutputBundleSummary Summary = (*Bundle)->summary();
  EXPECT_EQ(Summary.State, neverc::OutputBundleState::Aborted);
  EXPECT_EQ(Summary.Flags & neverc::OutputRecoveryRequired, 0U);
#ifdef _WIN32
  const DWORD Attributes = ::GetFileAttributesW(WideMain.data());
  ASSERT_NE(Attributes, INVALID_FILE_ATTRIBUTES);
  EXPECT_NE(Attributes & FILE_ATTRIBUTE_REPARSE_POINT, 0U);
#else
  sys::fs::file_status MainStatus;
  ASSERT_FALSE(sys::fs::status(Main, MainStatus, /*follow=*/false));
  EXPECT_EQ(MainStatus.type(), sys::fs::file_type::symlink_file);
#endif
  EXPECT_EQ(readFile(Main), "old-main");
  EXPECT_EQ(readFile(Map), "old-map");
}

TEST(PluginBinaryImageTest,
     BackupMaterializationFailureAbortsWithoutRecoveryState) {
  TemporaryDirectory Directory;
  const std::string Main = Directory.file("occupied-output");
  ASSERT_FALSE(sys::fs::create_directory(Main));
  neverc::OutputCoordinator Coordinator;
  std::vector<neverc::OutputBundleFile> Outputs = {
      {"main", Main, {'n', 'e', 'w'}, true},
  };
  auto Bundle = neverc::OutputBundleTransaction::create(Coordinator, Outputs);
  ASSERT_TRUE(static_cast<bool>(Bundle)) << errorText(Bundle.takeError());
  auto Committed = (*Bundle)->commit();
  ASSERT_FALSE(static_cast<bool>(Committed));
  consumeError(Committed.takeError());
  const neverc::OutputBundleSummary Summary = (*Bundle)->summary();
  EXPECT_EQ(Summary.State, neverc::OutputBundleState::Aborted);
  EXPECT_EQ(Summary.Flags & neverc::OutputRecoveryRequired, 0U);
  sys::fs::file_status Status;
  ASSERT_FALSE(sys::fs::status(Main, Status, /*follow=*/false));
  EXPECT_EQ(Status.type(), sys::fs::file_type::directory_file);
}

TEST(PluginBinaryImageTest, AbortReportsStagingCleanupFailure) {
  TemporaryDirectory Directory;
  const std::string Main = Directory.file("module.ko");
  neverc::OutputCoordinator Coordinator;
  std::vector<neverc::OutputBundleFile> Outputs = {
      {"main", Main, {'n', 'e', 'w'}, true},
  };
  std::string OrphanedStaging;
  auto Bundle = neverc::OutputBundleTransaction::create(
      Coordinator, Outputs,
      /*IsCancelled=*/{},
      [&](neverc::OutputBundleOperation Operation, StringRef Path) {
        if (Operation != neverc::OutputBundleOperation::DiscardStaging)
          return std::error_code();
        OrphanedStaging = Path.str();
        return std::make_error_code(std::errc::io_error);
      });
  ASSERT_TRUE(static_cast<bool>(Bundle)) << errorText(Bundle.takeError());
  if (Error E = (*Bundle)->prepare())
    FAIL() << errorText(std::move(E));

  Error Aborted = (*Bundle)->abort();
  ASSERT_TRUE(static_cast<bool>(Aborted));
  const std::string Failure = errorText(std::move(Aborted));
  EXPECT_NE(Failure.find("staged output"), std::string::npos) << Failure;
  const neverc::OutputBundleSummary Summary = (*Bundle)->summary();
  EXPECT_EQ(Summary.State, neverc::OutputBundleState::Aborted);
  EXPECT_EQ(Summary.Flags & neverc::OutputRecoveryRequired,
            neverc::OutputRecoveryRequired);
  ASSERT_FALSE(OrphanedStaging.empty());
  EXPECT_TRUE(sys::fs::exists(OrphanedStaging));
  EXPECT_FALSE(sys::fs::remove(OrphanedStaging));
}

TEST(PluginBinaryImageTest, PrepareFailureDiscardsEveryStagedOutput) {
  TemporaryDirectory Directory;
  const std::string Main = Directory.file("module.ko");
  const std::string Map = Directory.file("module.ko.symbols.json");
  neverc::OutputCoordinator Coordinator;
  std::vector<neverc::OutputBundleFile> Outputs = {
      {"main", Main, {'n', 'e', 'w'}, true},
      {"map", Map, {'m', 'a', 'p'}},
  };
  std::vector<std::string> DiscardedStaging;
  unsigned SyncedStaging = 0;
  auto Bundle = neverc::OutputBundleTransaction::create(
      Coordinator, Outputs,
      /*IsCancelled=*/{},
      [&](neverc::OutputBundleOperation Operation, StringRef Path) {
        if (Operation == neverc::OutputBundleOperation::DiscardStaging) {
          DiscardedStaging.push_back(Path.str());
          return std::error_code();
        }
        return Operation == neverc::OutputBundleOperation::SyncStaging &&
                       ++SyncedStaging == 2
                   ? std::make_error_code(std::errc::io_error)
                   : std::error_code();
      });
  ASSERT_TRUE(static_cast<bool>(Bundle)) << errorText(Bundle.takeError());
  Error Prepared = (*Bundle)->prepare();
  ASSERT_TRUE(static_cast<bool>(Prepared));
  consumeError(std::move(Prepared));

  const neverc::OutputBundleSummary Summary = (*Bundle)->summary();
  EXPECT_EQ(Summary.State, neverc::OutputBundleState::Aborted);
  EXPECT_EQ(Summary.Flags & neverc::OutputRecoveryRequired, 0U);
  ASSERT_EQ(DiscardedStaging.size(), 2U);
  for (const std::string &Path : DiscardedStaging)
    EXPECT_FALSE(sys::fs::exists(Path));
  EXPECT_FALSE(sys::fs::exists(Main));
  EXPECT_FALSE(sys::fs::exists(Map));
}

TEST(PluginBinaryImageTest, BundleCanRemoveAStaleSideOutput) {
  TemporaryDirectory Directory;
  const std::string Main = Directory.file("module.ko");
  const std::string Map = Directory.file("module.ko.symbols.json");
  writeFile(Main, "old-main");
  writeFile(Map, "old-map");
  neverc::OutputCoordinator Coordinator;
  std::vector<neverc::OutputBundleFile> Outputs = {
      {"main", Main, {'n', 'e', 'w'}, true},
      {"map", Map, {}, false, false, neverc::OutputBundleFileAction::Remove},
  };
  auto Bundle = neverc::OutputBundleTransaction::create(Coordinator, Outputs);
  ASSERT_TRUE(static_cast<bool>(Bundle)) << errorText(Bundle.takeError());
  auto Committed = (*Bundle)->commit();
  ASSERT_TRUE(static_cast<bool>(Committed)) << errorText(Committed.takeError());
  EXPECT_EQ(readFile(Main), "new");
  EXPECT_FALSE(sys::fs::exists(Map));
}

TEST(PluginBinaryImageTest, RemovalOnlyBundleRemovesEveryOutput) {
  TemporaryDirectory Directory;
  const std::string Main = Directory.file("module.ko");
  const std::string Map = Directory.file("module.ko.symbols.json");
  const std::string State = Directory.file(".nvk-build-flags");
  writeFile(Main, "old-main");
  writeFile(Map, "old-map");
  writeFile(State, "old-state");

  neverc::OutputCoordinator Coordinator;
  std::vector<neverc::OutputBundleFile> Outputs = {
      {"main", Main, {}, true, false, neverc::OutputBundleFileAction::Remove},
      {"map", Map, {}, false, false, neverc::OutputBundleFileAction::Remove},
      {"state",
       State,
       {},
       false,
       false,
       neverc::OutputBundleFileAction::Remove},
  };
  auto Bundle = neverc::OutputBundleTransaction::create(Coordinator, Outputs);
  ASSERT_TRUE(static_cast<bool>(Bundle)) << errorText(Bundle.takeError());
  auto Committed = (*Bundle)->commit();
  ASSERT_TRUE(static_cast<bool>(Committed)) << errorText(Committed.takeError());
  EXPECT_FALSE(sys::fs::exists(Main));
  EXPECT_FALSE(sys::fs::exists(Map));
  EXPECT_FALSE(sys::fs::exists(State));
}

TEST(PluginBinaryImageTest, RemovedMainRejectsPublishedSideOutputs) {
  TemporaryDirectory Directory;
  const std::string Main = Directory.file("module.ko");
  const std::string Map = Directory.file("module.ko.symbols.json");
  neverc::OutputCoordinator Coordinator;
  std::vector<neverc::OutputBundleFile> Outputs = {
      {"main", Main, {}, true, false, neverc::OutputBundleFileAction::Remove},
      {"map", Map, {'n', 'e', 'w'}},
  };
  auto Bundle = neverc::OutputBundleTransaction::create(Coordinator, Outputs);
  ASSERT_FALSE(static_cast<bool>(Bundle));
  const std::string Failure = errorText(Bundle.takeError());
  EXPECT_NE(Failure.find("removal-only"), std::string::npos) << Failure;
}

TEST(PluginBinaryImageTest, AndroidKernelPublisherUsesExplicitBuildState) {
  TemporaryDirectory Directory;
  const std::string Main = Directory.file("module.ko");
  neverc::AndroidKernelBuildState BuildState;
  BuildState.BuildID = "KERNEL=510 PROFILE=release NEVERC=/tool/neverc";
  BuildState.BuildExtra = "-fexample-one -fexample-two";

  neverc::OutputCoordinator Coordinator;
  const std::vector<uint8_t> Image = {'i', 'm', 'a', 'g', 'e'};
  auto Published = neverc::publishAndroidKernelReleaseOutput(
      Coordinator, Main, Image, /*Map=*/nullptr, BuildState);
  ASSERT_TRUE(static_cast<bool>(Published)) << errorText(Published.takeError());

  EXPECT_EQ(readFile(Main), "image");
  EXPECT_EQ(readFile(Directory.file(".nvk-build-flags")),
            *BuildState.BuildID + "\n");
  EXPECT_EQ(readFile(Directory.file(".nvk-build-extra")),
            *BuildState.BuildExtra + "\n");
  const std::string Integrity =
      readFile(Directory.file(".nvk-build-integrity"));
  EXPECT_NE(Integrity.find("IMAGE_SHA256="), std::string::npos);
  EXPECT_NE(Integrity.find(" BUILD_ID_SHA256="), std::string::npos);
  EXPECT_NE(Integrity.find(" BUILD_EXTRA_SHA256="), std::string::npos);
  EXPECT_FALSE(sys::fs::exists(Main + ".symbols.json"));
}

TEST(PluginBinaryImageTest,
     AndroidKernelCleanRemovesOnlyTheTransactionalOutputBundle) {
  TemporaryDirectory Directory;
  const std::string Main = Directory.file("module.ko");
  const std::vector<std::string> BundlePaths = {
      Main,
      Main + ".symbols.json",
      Directory.file(".nvk-build-flags"),
      Directory.file(".nvk-build-extra"),
      Directory.file(".nvk-build-integrity"),
      Directory.file(".nvk-release-bundle"),
  };
  for (const std::string &Path : BundlePaths)
    writeFile(Path, "old");
  const std::string Unrelated = Directory.file("unrelated.o");
  writeFile(Unrelated, "keep");

  neverc::OutputCoordinator Coordinator;
  auto Cleaned = neverc::cleanAndroidKernelReleaseOutput(Coordinator, Main);
  ASSERT_TRUE(static_cast<bool>(Cleaned)) << errorText(Cleaned.takeError());
  for (const std::string &Path : BundlePaths)
    EXPECT_FALSE(sys::fs::exists(Path)) << Path;
  EXPECT_EQ(readFile(Unrelated), "keep");
}

TEST(PluginBinaryImageTest,
     RemovedSideOutputRemainsVisibleUntilMainPublication) {
  TemporaryDirectory Directory;
  const std::string Main = Directory.file("module.ko");
  const std::string Map = Directory.file("module.ko.symbols.json");
  writeFile(Main, "old-main");
  writeFile(Map, "old-map");
  neverc::OutputCoordinator Coordinator;
  std::vector<neverc::OutputBundleFile> Outputs = {
      {"main", Main, {'n', 'e', 'w'}, true},
      {"map", Map, {}, false, false, neverc::OutputBundleFileAction::Remove},
  };
  bool MapWasStillVisible = false;
  auto Bundle = neverc::OutputBundleTransaction::create(
      Coordinator, Outputs,
      /*IsCancelled=*/{},
      [&](neverc::OutputBundleOperation Operation, StringRef) {
        if (Operation != neverc::OutputBundleOperation::PublishMain)
          return std::error_code();
        MapWasStillVisible = sys::fs::exists(Map);
        return std::make_error_code(std::errc::io_error);
      });
  ASSERT_TRUE(static_cast<bool>(Bundle)) << errorText(Bundle.takeError());
  auto Committed = (*Bundle)->commit();
  ASSERT_FALSE(static_cast<bool>(Committed));
  consumeError(Committed.takeError());

  EXPECT_TRUE(MapWasStillVisible);
  EXPECT_EQ(readFile(Main), "old-main");
  EXPECT_EQ(readFile(Map), "old-map");
}

TEST(PluginBinaryImageTest,
     RemovedSideOutputWaitsForMainDirectorySynchronization) {
  TemporaryDirectory Directory;
  const std::string Main = Directory.file("module.ko");
  const std::string Map = Directory.file("module.ko.symbols.json");
  writeFile(Main, "old-main");
  writeFile(Map, "old-map");
  neverc::OutputCoordinator Coordinator;
  std::vector<neverc::OutputBundleFile> Outputs = {
      {"main", Main, {'n', 'e', 'w'}, true},
      {"map", Map, {}, false, false, neverc::OutputBundleFileAction::Remove},
  };
  auto CanonicalMain = Coordinator.canonicalize(Main);
  ASSERT_TRUE(static_cast<bool>(CanonicalMain))
      << errorText(CanonicalMain.takeError());
  auto CanonicalMap = Coordinator.canonicalize(Map);
  ASSERT_TRUE(static_cast<bool>(CanonicalMap))
      << errorText(CanonicalMap.takeError());
  bool MainDirectorySynced = false;
  bool RemovalObservedMainSync = false;
  auto Bundle = neverc::OutputBundleTransaction::create(
      Coordinator, Outputs,
      /*IsCancelled=*/{},
      [&](neverc::OutputBundleOperation Operation, StringRef Path) {
        if (Operation == neverc::OutputBundleOperation::SyncDirectory &&
            Path == *CanonicalMain)
          MainDirectorySynced = true;
        if (Operation == neverc::OutputBundleOperation::PublishSide &&
            Path == *CanonicalMap)
          RemovalObservedMainSync = MainDirectorySynced;
        return std::error_code();
      });
  ASSERT_TRUE(static_cast<bool>(Bundle)) << errorText(Bundle.takeError());
  auto Committed = (*Bundle)->commit();
  ASSERT_TRUE(static_cast<bool>(Committed)) << errorText(Committed.takeError());

  EXPECT_TRUE(RemovalObservedMainSync);
  EXPECT_EQ(readFile(Main), "new");
  EXPECT_FALSE(sys::fs::exists(Map));
}

TEST(PluginBinaryImageTest, BundleRemovesADanglingSideOutputSymlink) {
  TemporaryDirectory Directory;
  const std::string Main = Directory.file("module.ko");
  const std::string Map = Directory.file("module.ko.symbols.json");
  const std::string MissingTarget = Directory.file("missing-symbol-map");
  writeFile(Main, "old-main");
#ifdef _WIN32
  if (std::error_code EC = createWindowsFileSymlink(MissingTarget, Map)) {
    if (EC == std::make_error_code(std::errc::permission_denied))
      GTEST_SKIP() << "Windows symbolic-link creation is unavailable";
    FAIL() << "CreateSymbolicLinkW failed: " << EC.message();
  }
  SmallVector<wchar_t, 256> WideMap;
  ASSERT_FALSE(sys::windows::widenPath(Map, WideMap));
  const DWORD BeforeAttributes = ::GetFileAttributesW(WideMap.data());
  ASSERT_NE(BeforeAttributes, INVALID_FILE_ATTRIBUTES);
  ASSERT_NE(BeforeAttributes & FILE_ATTRIBUTE_REPARSE_POINT, 0U);
#else
  ASSERT_FALSE(sys::fs::create_link(MissingTarget, Map));
  sys::fs::file_status Before;
  ASSERT_FALSE(sys::fs::status(Map, Before, /*follow=*/false));
  ASSERT_EQ(Before.type(), sys::fs::file_type::symlink_file);
#endif

  neverc::OutputCoordinator Coordinator;
  std::vector<neverc::OutputBundleFile> Outputs = {
      {"main", Main, {'n', 'e', 'w'}, true},
      {"map", Map, {}, false, false, neverc::OutputBundleFileAction::Remove},
  };
  auto Bundle = neverc::OutputBundleTransaction::create(Coordinator, Outputs);
  ASSERT_TRUE(static_cast<bool>(Bundle)) << errorText(Bundle.takeError());
  auto Committed = (*Bundle)->commit();
  ASSERT_TRUE(static_cast<bool>(Committed)) << errorText(Committed.takeError());
  EXPECT_EQ(readFile(Main), "new");

#ifdef _WIN32
  const DWORD AfterAttributes = ::GetFileAttributesW(WideMap.data());
  const DWORD AfterError = AfterAttributes == INVALID_FILE_ATTRIBUTES
                               ? ::GetLastError()
                               : ERROR_SUCCESS;
  EXPECT_EQ(AfterAttributes, INVALID_FILE_ATTRIBUTES);
  EXPECT_TRUE(AfterError == ERROR_FILE_NOT_FOUND ||
              AfterError == ERROR_PATH_NOT_FOUND);
#else
  sys::fs::file_status After;
  EXPECT_EQ(sys::fs::status(Map, After, /*follow=*/false),
            std::make_error_code(std::errc::no_such_file_or_directory));
#endif
}

TEST(PluginBinaryImageTest,
     RemovedLinkLikeOutputRemainsVisibleUntilPublication) {
  TemporaryDirectory Directory;
  const std::string Main = Directory.file("module.ko");
  const std::string Map = Directory.file("module.ko.symbols.json");
  const std::string MapTarget = Directory.file("private-symbol-map");
  writeFile(Main, "old-main");
  writeFile(MapTarget, "old-map");
#ifdef _WIN32
  if (std::error_code EC = createWindowsFileSymlink(MapTarget, Map)) {
    if (EC == std::make_error_code(std::errc::permission_denied))
      GTEST_SKIP() << "Windows symbolic-link creation is unavailable";
    FAIL() << "CreateSymbolicLinkW failed: " << EC.message();
  }
  SmallVector<wchar_t, 256> WideMap;
  ASSERT_FALSE(sys::windows::widenPath(Map, WideMap));
#else
  ASSERT_FALSE(sys::fs::create_link(MapTarget, Map));
#endif

  neverc::OutputCoordinator Coordinator;
  std::vector<neverc::OutputBundleFile> Outputs = {
      {"main", Main, {'n', 'e', 'w'}, true},
      {"map", Map, {}, false, false, neverc::OutputBundleFileAction::Remove},
  };
  bool SawLinkLikeOutput = false;
  auto Bundle = neverc::OutputBundleTransaction::create(
      Coordinator, Outputs,
      /*IsCancelled=*/{},
      [&](neverc::OutputBundleOperation Operation, StringRef) {
        if (Operation != neverc::OutputBundleOperation::PublishSide)
          return std::error_code();
#ifdef _WIN32
        const DWORD Attributes = ::GetFileAttributesW(WideMap.data());
        SawLinkLikeOutput = Attributes != INVALID_FILE_ATTRIBUTES &&
                            (Attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
        sys::fs::file_status Status;
        SawLinkLikeOutput = !sys::fs::status(Map, Status, /*follow=*/false) &&
                            Status.type() == sys::fs::file_type::symlink_file;
#endif
        return std::make_error_code(std::errc::io_error);
      });
  ASSERT_TRUE(static_cast<bool>(Bundle)) << errorText(Bundle.takeError());
  auto Committed = (*Bundle)->commit();
  ASSERT_FALSE(static_cast<bool>(Committed));
  consumeError(Committed.takeError());

  EXPECT_TRUE(SawLinkLikeOutput);
  EXPECT_EQ(readFile(Main), "old-main");
  EXPECT_EQ(readFile(Map), "old-map");
}

TEST(PluginBinaryImageTest,
     MainPublishFailureRestoresRemovedLinkLikeSideOutput) {
  TemporaryDirectory Directory;
  const std::string Main = Directory.file("module.ko");
  const std::string Map = Directory.file("module.ko.symbols.json");
  const std::string MapTarget = Directory.file("private-symbol-map");
  writeFile(Main, "old-main");
  writeFile(MapTarget, "old-map");
#ifdef _WIN32
  if (std::error_code EC = createWindowsFileSymlink(MapTarget, Map)) {
    if (EC == std::make_error_code(std::errc::permission_denied))
      GTEST_SKIP() << "Windows symbolic-link creation is unavailable";
    FAIL() << "CreateSymbolicLinkW failed: " << EC.message();
  }
  SmallVector<wchar_t, 256> WideMap;
  ASSERT_FALSE(sys::windows::widenPath(Map, WideMap));
#else
  ASSERT_FALSE(sys::fs::create_link(MapTarget, Map));
#endif

  neverc::OutputCoordinator Coordinator;
  std::vector<neverc::OutputBundleFile> Outputs = {
      {"main", Main, {'n', 'e', 'w'}, true},
      {"map", Map, {}, false, false, neverc::OutputBundleFileAction::Remove},
  };
  auto Bundle = neverc::OutputBundleTransaction::create(
      Coordinator, Outputs,
      /*IsCancelled=*/{},
      [](neverc::OutputBundleOperation Operation, StringRef) {
        return Operation == neverc::OutputBundleOperation::PublishMain
                   ? std::make_error_code(std::errc::io_error)
                   : std::error_code();
      });
  ASSERT_TRUE(static_cast<bool>(Bundle)) << errorText(Bundle.takeError());
  auto Committed = (*Bundle)->commit();
  ASSERT_FALSE(static_cast<bool>(Committed));
  consumeError(Committed.takeError());

  EXPECT_EQ(readFile(Main), "old-main");
  EXPECT_EQ(readFile(Map), "old-map");
  EXPECT_EQ(readFile(MapTarget), "old-map");
#ifdef _WIN32
  const DWORD Attributes = ::GetFileAttributesW(WideMap.data());
  ASSERT_NE(Attributes, INVALID_FILE_ATTRIBUTES);
  EXPECT_NE(Attributes & FILE_ATTRIBUTE_REPARSE_POINT, 0U);
#else
  sys::fs::file_status MapStatus;
  ASSERT_FALSE(sys::fs::status(Map, MapStatus, /*follow=*/false));
  EXPECT_EQ(MapStatus.type(), sys::fs::file_type::symlink_file);
#endif
}

TEST(PluginBinaryImageTest,
     UnsupportedDirectorySyncDoesNotClaimDurablePublication) {
  TemporaryDirectory Directory;
  const std::string Main = Directory.file("module.ko");
  neverc::OutputCoordinator Coordinator;
  std::vector<neverc::OutputBundleFile> Outputs = {
      {"main", Main, {'n', 'e', 'w'}, true},
  };
  auto Bundle = neverc::OutputBundleTransaction::create(
      Coordinator, Outputs,
      /*IsCancelled=*/{},
      [](neverc::OutputBundleOperation Operation, StringRef) {
        return Operation == neverc::OutputBundleOperation::SyncDirectory
                   ? std::make_error_code(std::errc::operation_not_supported)
                   : std::error_code();
      });
  ASSERT_TRUE(static_cast<bool>(Bundle)) << errorText(Bundle.takeError());
  auto Committed = (*Bundle)->commit();
  ASSERT_TRUE(static_cast<bool>(Committed)) << errorText(Committed.takeError());
  EXPECT_EQ(Committed->State, neverc::OutputBundleState::Committed);
  EXPECT_EQ(Committed->Flags & neverc::OutputDurable, 0U);
  EXPECT_EQ(Committed->Flags & neverc::OutputDurabilityUnconfirmed,
            neverc::OutputDurabilityUnconfirmed);
}

TEST(PluginBinaryImageTest,
     CompletedJournalSyncFailureRetainsRecoveryArtifacts) {
  TemporaryDirectory Directory;
  const std::string Main = Directory.file("module.ko");
  writeFile(Main, "old-main");
  neverc::OutputCoordinator Coordinator;
  std::vector<neverc::OutputBundleFile> Outputs = {
      {"main", Main, {'n', 'e', 'w'}, true},
  };
  auto Bundle = neverc::OutputBundleTransaction::create(
      Coordinator, Outputs,
      /*IsCancelled=*/{},
      [](neverc::OutputBundleOperation Operation, StringRef) {
        return Operation == neverc::OutputBundleOperation::SyncCompletedJournal
                   ? std::make_error_code(std::errc::io_error)
                   : std::error_code();
      });
  ASSERT_TRUE(static_cast<bool>(Bundle)) << errorText(Bundle.takeError());
  auto Committed = (*Bundle)->commit();
  ASSERT_FALSE(static_cast<bool>(Committed));
  consumeError(Committed.takeError());

  const neverc::OutputBundleSummary Summary = (*Bundle)->summary();
  EXPECT_EQ(Summary.State, neverc::OutputBundleState::Committed);
  EXPECT_EQ(Summary.Flags & neverc::OutputRecoveryRequired,
            neverc::OutputRecoveryRequired);
  EXPECT_EQ(Summary.Flags & neverc::OutputDurable, 0U);
  EXPECT_EQ(Summary.Flags & neverc::OutputDurabilityUnconfirmed,
            neverc::OutputDurabilityUnconfirmed);
  ASSERT_FALSE(Summary.JournalPath.empty());
  ASSERT_TRUE(sys::fs::exists(Summary.JournalPath));
  const std::string Journal = readFile(Summary.JournalPath);
  const StringRef BackupPrefix = "backup main ";
  const size_t BackupStart = Journal.find(BackupPrefix.str());
  ASSERT_NE(BackupStart, std::string::npos);
  const size_t PathStart = BackupStart + BackupPrefix.size();
  const size_t PathEnd = Journal.find('\n', PathStart);
  ASSERT_NE(PathEnd, std::string::npos);
  const std::string Backup = Journal.substr(PathStart, PathEnd - PathStart);
  EXPECT_TRUE(sys::fs::exists(Backup));
  EXPECT_EQ(readFile(Backup), "old-main");
  EXPECT_EQ(readFile(Main), "new");
}

TEST(PluginBinaryImageTest, MissingJournalIsNotSilentlyRecreated) {
  TemporaryDirectory Directory;
  const std::string Main = Directory.file("module.ko");
  writeFile(Main, "old-main");
  neverc::OutputCoordinator Coordinator;
  std::vector<neverc::OutputBundleFile> Outputs = {
      {"main", Main, {'n', 'e', 'w'}, true},
  };
  std::string Journal;
  bool RemovedJournal = false;
  auto Bundle = neverc::OutputBundleTransaction::create(
      Coordinator, Outputs,
      /*IsCancelled=*/{},
      [&](neverc::OutputBundleOperation Operation, StringRef) {
        if (Operation != neverc::OutputBundleOperation::BackupExisting ||
            RemovedJournal)
          return std::error_code();
        const std::error_code EC = sys::fs::remove(Journal);
        RemovedJournal = !EC;
        return EC;
      });
  ASSERT_TRUE(static_cast<bool>(Bundle)) << errorText(Bundle.takeError());
  if (Error E = (*Bundle)->prepare())
    FAIL() << errorText(std::move(E));
  Journal = (*Bundle)->summary().JournalPath;
  ASSERT_FALSE(Journal.empty());

  auto Committed = (*Bundle)->commit();
  ASSERT_FALSE(static_cast<bool>(Committed));
  consumeError(Committed.takeError());
  EXPECT_TRUE(RemovedJournal);
  EXPECT_EQ(readFile(Main), "old-main");
  EXPECT_FALSE(sys::fs::exists(Journal));
  EXPECT_EQ((*Bundle)->summary().State, neverc::OutputBundleState::Aborted);
  EXPECT_EQ((*Bundle)->summary().Flags & neverc::OutputRecoveryRequired, 0U);
}

TEST(PluginBinaryImageTest, RollbackBeforePublicationClearsDurabilityWarning) {
  TemporaryDirectory Directory;
  const std::string Main = Directory.file("module.ko");
  const std::string Map = Directory.file("module.ko.symbols.json");
  neverc::OutputCoordinator Coordinator;
  std::vector<neverc::OutputBundleFile> Outputs = {
      {"main", Main, {'n', 'e', 'w'}, true},
      {"map", Map, {'m', 'a', 'p'}},
  };
  auto Bundle = neverc::OutputBundleTransaction::create(
      Coordinator, Outputs,
      /*IsCancelled=*/{},
      [](neverc::OutputBundleOperation Operation, StringRef) {
        if (Operation == neverc::OutputBundleOperation::SyncRecoveryState)
          return std::make_error_code(std::errc::operation_not_supported);
        if (Operation == neverc::OutputBundleOperation::PublishSide)
          return std::make_error_code(std::errc::io_error);
        return std::error_code();
      });
  ASSERT_TRUE(static_cast<bool>(Bundle)) << errorText(Bundle.takeError());
  auto Committed = (*Bundle)->commit();
  ASSERT_FALSE(static_cast<bool>(Committed));
  consumeError(Committed.takeError());

  const neverc::OutputBundleSummary Summary = (*Bundle)->summary();
  EXPECT_EQ(Summary.State, neverc::OutputBundleState::Aborted);
  EXPECT_EQ(Summary.Flags & neverc::OutputDurabilityUnconfirmed, 0U);
  EXPECT_FALSE(sys::fs::exists(Main));
  EXPECT_FALSE(sys::fs::exists(Map));
}

TEST(PluginBinaryImageTest,
     BundleRemovalRollbackRestoresTheOldMainAndSideOutput) {
  TemporaryDirectory Directory;
  const std::string Main = Directory.file("module.ko");
  const std::string Map = Directory.file("module.ko.symbols.json");
  writeFile(Main, "old-main");
  writeFile(Map, "old-map");
  neverc::OutputCoordinator Coordinator;
  std::vector<neverc::OutputBundleFile> Outputs = {
      {"main", Main, {'n', 'e', 'w'}, true},
      {"map", Map, {}, false, false, neverc::OutputBundleFileAction::Remove},
  };
  auto Bundle = neverc::OutputBundleTransaction::create(
      Coordinator, Outputs,
      /*IsCancelled=*/{},
      [](neverc::OutputBundleOperation Operation, StringRef) {
        return Operation == neverc::OutputBundleOperation::PublishMain
                   ? std::make_error_code(std::errc::io_error)
                   : std::error_code();
      });
  ASSERT_TRUE(static_cast<bool>(Bundle)) << errorText(Bundle.takeError());
  auto Committed = (*Bundle)->commit();
  EXPECT_FALSE(static_cast<bool>(Committed));
  if (!Committed)
    consumeError(Committed.takeError());
  EXPECT_EQ(readFile(Main), "old-main");
  EXPECT_EQ(readFile(Map), "old-map");
}

TEST(PluginBinaryImageTest,
     LateJournalFailureKeepsNewBundleAndFiresAfterCommit) {
  LinkTaskScope Scope;
  ASSERT_TRUE(Scope.initialize());
  auto Image = makeImage(Scope);
  ASSERT_TRUE(static_cast<bool>(Image)) << errorText(Image.takeError());
  TemporaryDirectory Directory;
  const std::string Main = Directory.file("program.bin");
  neverc::OutputCoordinator Coordinator;
  auto Pipeline = LinkOutputPipeline::create(Scope.task(), Coordinator);
  ASSERT_TRUE(static_cast<bool>(Pipeline)) << errorText(Pipeline.takeError());
  NevercTestPostEmitTrace Trace{};
  Trace.PhaseAPI = &Scope.phaseAPI();
  Trace.PatchValue = 0xcc;
  NevercObserverDescriptor Observer{};
  Observer.Header = {sizeof(Observer), NEVERC_PLUGIN_ABI_MAJOR,
                     NEVERC_PLUGIN_ABI_MINOR, 0};
  Observer.Phase = {NEVERC_PHASE_LINK_AFTER_COMMIT_HIGH,
                    NEVERC_PHASE_LINK_AFTER_COMMIT_LOW};
  Observer.Points = NEVERC_OBSERVER_AFTER;
  Observer.Callback = neverc_test_after_commit_observer;
  Observer.UserData = &Trace;
  if (Error E = (*Pipeline)->addObserver(LinkTestPluginID, Observer))
    FAIL() << errorText(std::move(E));
  auto Output = (*Pipeline)->execute(
      *Image, Main, {}, [](neverc::OutputBundleOperation Operation, StringRef) {
        return Operation == neverc::OutputBundleOperation::CleanupJournal
                   ? std::make_error_code(std::errc::io_error)
                   : std::error_code();
      });
  EXPECT_FALSE(static_cast<bool>(Output));
  if (!Output)
    consumeError(Output.takeError());
  EXPECT_EQ(readFile(Main).size(), 8U);
  EXPECT_EQ((*Image)->state(), NEVERC_BINARY_IMAGE_COMMITTED);
  EXPECT_EQ(Trace.AfterCommitCalls, 1U);

  std::vector<neverc::OutputBundleFile> RetryOutputs = {
      {"main", Main, {'r', 'e', 't', 'r', 'y'}, true},
  };
  auto Retry =
      neverc::OutputBundleTransaction::create(Coordinator, RetryOutputs);
  ASSERT_TRUE(static_cast<bool>(Retry)) << errorText(Retry.takeError());
  auto Retried = (*Retry)->commit();
  ASSERT_TRUE(static_cast<bool>(Retried)) << errorText(Retried.takeError());
  EXPECT_EQ(readFile(Main), "retry");
}

TEST(PluginBinaryImageTest,
     BundleRejectsCanonicalPathAliasesBeforePublication) {
  TemporaryDirectory Directory;
  const std::string Main = Directory.file("program.bin");
  SmallString<160> Alias(Directory.Path);
  sys::path::append(Alias, ".", "program.bin");
  neverc::OutputCoordinator Coordinator;
  std::vector<neverc::OutputBundleFile> Outputs = {
      {"main", Main, {'m'}, true}, {"alias", Alias.str().str(), {'s'}, false}};
  auto Bundle = neverc::OutputBundleTransaction::create(Coordinator, Outputs);
  EXPECT_FALSE(static_cast<bool>(Bundle));
  if (!Bundle)
    consumeError(Bundle.takeError());
  EXPECT_FALSE(sys::fs::exists(Main));
}

TEST(PluginBinaryImageTest, BundleRejectsPublicationLockAsAnOutput) {
  TemporaryDirectory Directory;
  neverc::OutputCoordinator Coordinator;
  for (StringRef Name :
       {StringRef(".neverc-output.lock"), StringRef(".NEVERC-OUTPUT.LOCK")}) {
    const std::string Lock = Directory.file(Name);
    std::vector<neverc::OutputBundleFile> Outputs = {
        {"main", Lock, {'n', 'e', 'w'}, true},
    };
    auto Bundle = neverc::OutputBundleTransaction::create(Coordinator, Outputs);
    EXPECT_FALSE(static_cast<bool>(Bundle)) << Name.str();
    if (!Bundle)
      consumeError(Bundle.takeError());
    EXPECT_FALSE(sys::fs::exists(Lock)) << Name.str();
  }
}
} // namespace
