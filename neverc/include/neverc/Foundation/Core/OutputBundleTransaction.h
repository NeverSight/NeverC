#ifndef NEVERC_FOUNDATION_CORE_OUTPUTBUNDLETRANSACTION_H
#define NEVERC_FOUNDATION_CORE_OUTPUTBUNDLETRANSACTION_H

#include "neverc/Foundation/Core/OutputCoordinator.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace neverc {

enum class OutputBundleState : uint8_t {
  Open,
  Prepared,
  Committed,
  Aborted,
  FailedPartial,
};

enum class OutputBundleOperation : uint8_t {
  WriteStaging,
  SyncStaging,
  CreateJournal,
  SyncJournal,
  BackupExisting,
  PublishSide,
  PublishMain,
  RestoreBackup,
  RemovePublished,
  SyncDirectory,
  CompleteJournal,
  CleanupJournal,
  DiscardStaging,
  SyncRecoveryState,
  SyncCompletedJournal,
};

enum class OutputBundleFileAction : uint8_t {
  Publish,
  Remove,
};

struct OutputBundleFile {
  std::string Name;
  std::string Path;
  std::vector<uint8_t> Bytes;
  bool Main = false;
  bool Executable = false;
  OutputBundleFileAction Action = OutputBundleFileAction::Publish;
  /// Restrict published files to their owner (0600 or an owner-only ACL).
  bool OwnerOnly = false;
};

struct OutputBundleSummary {
  OutputBundleState State = OutputBundleState::Open;
  uint64_t Flags = 0;
  uint64_t OutputCount = 0;
  uint64_t PublicationGeneration = 0;
  std::array<uint8_t, 32> MainDigest{};
  std::string TransactionID;
  std::string JournalPath;
};

class OutputBundleTransaction {
public:
  using FaultInjector =
      std::function<std::error_code(OutputBundleOperation, llvm::StringRef)>;

  static llvm::Expected<std::unique_ptr<OutputBundleTransaction>>
  create(OutputCoordinator &Coordinator,
         llvm::ArrayRef<OutputBundleFile> Outputs,
         OutputCoordinator::CancellationCheck IsCancelled = {},
         FaultInjector InjectFault = {}, OutputLeaseOwner LeaseOwner = {});

  ~OutputBundleTransaction();

  OutputBundleTransaction(const OutputBundleTransaction &) = delete;
  OutputBundleTransaction &operator=(const OutputBundleTransaction &) = delete;

  llvm::Error prepare();
  llvm::Expected<OutputBundleSummary> commit();
  llvm::Error abort();
  OutputBundleSummary summary() const;

private:
  struct Entry;
  struct PublicationLock;

  OutputBundleTransaction(OutputCoordinator &Coordinator,
                          std::vector<Entry> Entries,
                          std::vector<OutputPathLease> Leases,
                          OutputCoordinator::CancellationCheck IsCancelled,
                          FaultInjector InjectFault);

  std::error_code fault(OutputBundleOperation Operation,
                        llvm::StringRef Path) const;
  llvm::Error acquirePublicationLocks();
  llvm::Error discardStaging(Entry &EntryValue);
  llvm::Error createJournal();
  llvm::Error appendAndSyncJournal(llvm::StringRef Text,
                                   OutputBundleOperation SyncOperation);
  llvm::Error rollback();
  void releaseLeases();

  OutputCoordinator &Coordinator;
  std::vector<Entry> Entries;
  std::vector<OutputPathLease> Leases;
  OutputCoordinator::CancellationCheck IsCancelled;
  std::vector<std::string> PublicationLockPaths;
  std::vector<OutputPathLease> PublicationLeases;
  std::vector<std::unique_ptr<PublicationLock>> PublicationLocks;
  FaultInjector InjectFault;
  OutputBundleState State = OutputBundleState::Open;
  uint64_t Flags = 0;
  uint64_t PublicationGeneration = 0;
  size_t MainIndex = 0;
  std::array<uint8_t, 32> MainDigest{};
  std::string TransactionID;
  std::string JournalPath;
};

} // namespace neverc

#endif
