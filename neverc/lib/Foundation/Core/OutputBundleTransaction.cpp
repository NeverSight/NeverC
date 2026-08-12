#include "neverc/Foundation/Core/OutputBundleTransaction.h"
#include "OutputPlatform.h"
#include "neverc/Foundation/Core/OutputDigest.h"
#include "neverc/Foundation/Core/OutputPublicationFlags.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SHA256.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <chrono>
#include <set>
#include <utility>

using namespace llvm;

namespace neverc {
namespace {

Error bundleError(const Twine &Message) {
  return createStringError(inconvertibleErrorCode(),
                           "output bundle: " + Message);
}

OutputCoordinator &publicationLockCoordinator() {
  static OutputCoordinator Coordinator;
  return Coordinator;
}

} // namespace

struct OutputBundleTransaction::Entry {
  OutputBundleFile Output;
  std::string CanonicalPath;
  std::optional<sys::fs::TempFile> Staging;
  std::string BackupPath;
  bool HadExisting = false;
  bool ExistingLinkLike = false;
  bool Published = false;
};

struct OutputBundleTransaction::PublicationLock {
  explicit PublicationLock(int FileDescriptorValue)
      : FileDescriptor(FileDescriptorValue) {}

  ~PublicationLock() {
    if (Locked) [[maybe_unused]]
      const std::error_code UnlockError = sys::fs::unlockFile(FileDescriptor);
    if (FileDescriptor != -1) [[maybe_unused]]
      const std::error_code CloseError =
          output_platform::closeFileDescriptor(FileDescriptor);
  }

  int FileDescriptor = -1;
  bool Locked = false;
};

OutputBundleTransaction::OutputBundleTransaction(
    OutputCoordinator &CoordinatorValue, std::vector<Entry> EntriesValue,
    std::vector<OutputPathLease> LeasesValue,
    OutputCoordinator::CancellationCheck IsCancelledValue,
    FaultInjector InjectFaultValue)
    : Coordinator(CoordinatorValue), Entries(std::move(EntriesValue)),
      Leases(std::move(LeasesValue)), IsCancelled(std::move(IsCancelledValue)),
      InjectFault(std::move(InjectFaultValue)) {
  std::set<std::string> LockPaths;
  for (const Entry &EntryValue : Entries) {
    SmallString<256> LockPath(sys::path::parent_path(EntryValue.CanonicalPath));
    sys::path::append(LockPath, ".neverc-output.lock");
    LockPaths.insert(LockPath.str().str());
  }
  PublicationLockPaths.assign(LockPaths.begin(), LockPaths.end());
  for (size_t Index = 0; Index != Entries.size(); ++Index)
    if (Entries[Index].Output.Main) {
      MainIndex = Index;
      break;
    }
  MainDigest = SHA256::hash(Entries[MainIndex].Output.Bytes);
}

Expected<std::unique_ptr<OutputBundleTransaction>>
OutputBundleTransaction::create(
    OutputCoordinator &Coordinator, ArrayRef<OutputBundleFile> Outputs,
    OutputCoordinator::CancellationCheck IsCancelled, FaultInjector InjectFault,
    OutputLeaseOwner LeaseOwner) {
  if (Outputs.empty())
    return bundleError("at least one output is required");
  size_t MainCount = 0;
  bool MainIsRemoval = false;
  bool HasPublishedOutput = false;
  std::set<std::string> Names;
  std::vector<Entry> Entries;
  std::vector<StringRef> Paths;
  Entries.reserve(Outputs.size());
  Paths.reserve(Outputs.size());
  for (const OutputBundleFile &Output : Outputs) {
    if (Output.Name.empty() || Output.Path.empty() ||
        !Names.insert(Output.Name).second)
      return bundleError("output names and paths must be unique");
    if (Output.Action == OutputBundleFileAction::Remove &&
        (!Output.Bytes.empty() || Output.Executable || Output.OwnerOnly))
      return bundleError("removed outputs cannot contain bytes or permissions");
    if (Output.Main)
      MainIsRemoval = Output.Action == OutputBundleFileAction::Remove;
    HasPublishedOutput |= Output.Action == OutputBundleFileAction::Publish;
    MainCount += Output.Main ? 1 : 0;
    auto Canonical = Coordinator.canonicalize(Output.Path);
    if (!Canonical)
      return Canonical.takeError();
#if defined(_WIN32)
    const StringRef Filename = sys::path::filename(*Canonical);
    if (Filename.ends_with(".") || Filename.ends_with(" "))
      return bundleError(
          "Windows output paths cannot have a trailing dot or space");
#endif
    for (const Entry &Existing : Entries) {
      bool SameLocation = false;
      if (std::error_code EC = output_platform::pathsReferToSameLocation(
              Existing.CanonicalPath, *Canonical, SameLocation))
        return joinErrors(
            bundleError("could not compare output paths for aliasing"),
            errorCodeToError(EC));
      if (SameLocation)
        return bundleError("output names and paths must be unique");
    }
    if (sys::path::filename(*Canonical)
            .equals_insensitive(".neverc-output.lock"))
      return bundleError(Twine("output path is reserved for publication "
                               "coordination: ") +
                         *Canonical);
    Entries.push_back(
        {Output, std::move(*Canonical), std::nullopt, {}, false, false, false});
  }
  if (MainCount != 1)
    return bundleError("exactly one main output is required");
  if (MainIsRemoval && HasPublishedOutput)
    return bundleError(
        "a removed main output requires a removal-only transaction");
  for (const Entry &EntryValue : Entries)
    Paths.push_back(EntryValue.CanonicalPath);
  auto Leases = Coordinator.acquireAll(Paths, IsCancelled, LeaseOwner);
  if (!Leases)
    return Leases.takeError();
  return std::unique_ptr<OutputBundleTransaction>(new OutputBundleTransaction(
      Coordinator, std::move(Entries), std::move(*Leases),
      std::move(IsCancelled), std::move(InjectFault)));
}

OutputBundleTransaction::~OutputBundleTransaction() {
  if (State == OutputBundleState::Open || State == OutputBundleState::Prepared)
    consumeError(abort());
}

std::error_code OutputBundleTransaction::fault(OutputBundleOperation Operation,
                                               StringRef Path) const {
  return InjectFault ? InjectFault(Operation, Path) : std::error_code();
}

Error OutputBundleTransaction::acquirePublicationLocks() {
  if (!PublicationLocks.empty())
    return Error::success();

  SmallVector<StringRef, 4> Paths;
  Paths.reserve(PublicationLockPaths.size());
  for (const std::string &Path : PublicationLockPaths)
    Paths.push_back(Path);
  auto ProcessLeases =
      publicationLockCoordinator().acquireAll(Paths, IsCancelled);
  if (!ProcessLeases)
    return ProcessLeases.takeError();

  std::vector<std::unique_ptr<PublicationLock>> FileLocks;
  FileLocks.reserve(PublicationLockPaths.size());
  for (const std::string &Path : PublicationLockPaths) {
    int FileDescriptor = -1;
    if (std::error_code EC = sys::fs::openFileForReadWrite(
            Path, FileDescriptor, sys::fs::CD_OpenAlways, sys::fs::OF_None,
            static_cast<unsigned>(sys::fs::owner_read | sys::fs::owner_write)))
      return joinErrors(
          bundleError(Twine("could not open publication lock '") + Path + "'"),
          errorCodeToError(EC));

    auto Lock = std::make_unique<PublicationLock>(FileDescriptor);
    bool DeleteDispositionActive = false;
    if (std::error_code EC = output_platform::restrictFileToOwner(
            Path, FileDescriptor, DeleteDispositionActive))
      return joinErrors(
          bundleError(Twine("could not restrict publication lock '") + Path +
                      "' to its owner"),
          errorCodeToError(EC));
    for (;;) {
      const std::error_code EC =
          sys::fs::tryLockFile(FileDescriptor, std::chrono::milliseconds(50));
      if (!EC) {
        Lock->Locked = true;
        break;
      }
      if (EC != std::errc::no_lock_available)
        return joinErrors(
            bundleError(Twine("could not acquire publication lock '") + Path +
                        "'"),
            errorCodeToError(EC));
      if (IsCancelled && IsCancelled())
        return errorCodeToError(
            std::make_error_code(std::errc::operation_canceled));
    }
    FileLocks.push_back(std::move(Lock));
  }

  PublicationLeases = std::move(*ProcessLeases);
  PublicationLocks = std::move(FileLocks);
  return Error::success();
}

Error OutputBundleTransaction::discardStaging(Entry &EntryValue) {
  if (!EntryValue.Staging)
    return Error::success();

  const std::string Path = EntryValue.Staging->TmpName.str().str();
  Error Failure = Error::success();
  if (std::error_code Injected =
          fault(OutputBundleOperation::DiscardStaging, Path)) {
    Failure = errorCodeToError(Injected);
    Error Preserve = EntryValue.Staging->keep();
    if (Preserve) {
      Error Discard = EntryValue.Staging->discard();
      Preserve = joinErrors(std::move(Preserve), std::move(Discard));
    }
    Failure = joinErrors(std::move(Failure), std::move(Preserve));
  } else {
    Failure = EntryValue.Staging->discard();
  }
  EntryValue.Staging.reset();
  if (!Failure)
    return Error::success();

  Flags |= OutputRecoveryRequired;
  return joinErrors(bundleError(Twine("could not discard staged output '") +
                                Path + "'; recovery may be required"),
                    std::move(Failure));
}

Error OutputBundleTransaction::createJournal() {
  if (std::error_code Injected = fault(OutputBundleOperation::CreateJournal,
                                       Entries[MainIndex].CanonicalPath))
    return errorCodeToError(Injected);
  SmallString<256> Model(Entries[MainIndex].CanonicalPath);
  Model += ".neverc-journal-%%%%%%%%";
  int FD = -1;
  SmallString<256> Path;
  if (std::error_code EC = sys::fs::createUniqueFile(
          Model, FD, Path,
          static_cast<sys::fs::OpenFlags>(sys::fs::OF_Exclusive |
                                          sys::fs::OF_NoInherit |
                                          sys::fs::OF_AccessControl),
          static_cast<unsigned>(sys::fs::owner_read | sys::fs::owner_write)))
    return errorCodeToError(EC);
  JournalPath = Path.str().str();
  TransactionID = sys::path::filename(Path).str();
  bool DeleteDispositionActive = false;
  if (std::error_code EC = output_platform::restrictFileToOwner(
          JournalPath, FD, DeleteDispositionActive)) {
    Error Failure = errorCodeToError(EC);
    if (std::error_code CloseError = output_platform::closeFileDescriptor(FD))
      Failure = joinErrors(std::move(Failure), errorCodeToError(CloseError));
    if (std::error_code RemoveError = sys::fs::remove(JournalPath))
      Failure = joinErrors(std::move(Failure), errorCodeToError(RemoveError));
    JournalPath.clear();
    TransactionID.clear();
    return joinErrors(
        bundleError("could not restrict recovery journal to its owner"),
        std::move(Failure));
  }
  std::error_code WriteError;
  {
    raw_fd_ostream Stream(FD, false);
    Stream << "NEVERC_OUTPUT_BUNDLE_V1\n";
    Stream << "transaction " << TransactionID << "\n";
    for (const Entry &EntryValue : Entries) {
      if (EntryValue.Output.Action == OutputBundleFileAction::Remove) {
        Stream << "remove " << EntryValue.Output.Name << " - "
               << EntryValue.CanonicalPath << "\n";
        continue;
      }
      const auto Digest = SHA256::hash(EntryValue.Output.Bytes);
      Stream << (EntryValue.Output.Main ? "main " : "side ")
             << EntryValue.Output.Name << " " << outputDigestText(Digest) << " "
             << EntryValue.CanonicalPath << "\n";
    }
    Stream.flush();
    if (Stream.has_error()) {
      WriteError = Stream.error();
      Stream.clear_error();
    }
  }
  std::error_code SyncError;
  if (!WriteError)
    SyncError = fault(OutputBundleOperation::SyncJournal, JournalPath);
  if (!WriteError && !SyncError)
    SyncError = output_platform::syncFileDescriptor(FD);
  const std::error_code CloseError = output_platform::closeFileDescriptor(FD);
  if (WriteError)
    return joinErrors(bundleError("could not write recovery journal"),
                      errorCodeToError(WriteError));
  if (SyncError)
    return errorCodeToError(SyncError);
  if (CloseError)
    return errorCodeToError(CloseError);

  std::error_code DirectoryError =
      fault(OutputBundleOperation::SyncRecoveryState, JournalPath);
  if (!DirectoryError)
    DirectoryError = output_platform::syncParentDirectory(JournalPath);
  if (DirectoryError ==
      std::make_error_code(std::errc::operation_not_supported)) {
    Flags |= OutputDurabilityUnconfirmed;
  } else if (DirectoryError) {
    return errorCodeToError(DirectoryError);
  }
  return Error::success();
}

Error OutputBundleTransaction::appendAndSyncJournal(
    StringRef Text, OutputBundleOperation SyncOperation) {
  int FD = -1;
  if (std::error_code EC = sys::fs::openFileForWrite(
          JournalPath, FD, sys::fs::CD_OpenExisting, sys::fs::OF_None))
    return errorCodeToError(EC);
  if (std::error_code EC = output_platform::seekFileToEnd(FD)) {
    const std::error_code CloseError = output_platform::closeFileDescriptor(FD);
    return joinErrors(errorCodeToError(EC), errorCodeToError(CloseError));
  }

  std::error_code WriteError;
  {
    raw_fd_ostream Stream(FD, false);
    Stream << Text;
    Stream.flush();
    if (Stream.has_error()) {
      WriteError = Stream.error();
      Stream.clear_error();
    }
  }
  std::error_code SyncError;
  if (!WriteError)
    SyncError = fault(SyncOperation, JournalPath);
  if (!WriteError && !SyncError)
    SyncError = output_platform::syncFileDescriptor(FD);
  const std::error_code CloseError = output_platform::closeFileDescriptor(FD);
  if (WriteError)
    return joinErrors(bundleError("could not update recovery journal"),
                      errorCodeToError(WriteError));
  if (SyncError)
    return errorCodeToError(SyncError);
  if (CloseError)
    return errorCodeToError(CloseError);
  return Error::success();
}

Error OutputBundleTransaction::prepare() {
  if (State == OutputBundleState::Prepared)
    return Error::success();
  if (State != OutputBundleState::Open)
    return bundleError("only an open bundle can be prepared");
  auto Fail = [this](Error Failure) -> Error {
    if (Error Cleanup = abort())
      return joinErrors(std::move(Failure), std::move(Cleanup));
    return Failure;
  };
  for (Entry &EntryValue : Entries) {
    if (EntryValue.Output.Action == OutputBundleFileAction::Remove)
      continue;
    if (std::error_code Injected = fault(OutputBundleOperation::WriteStaging,
                                         EntryValue.CanonicalPath))
      return Fail(errorCodeToError(Injected));
    SmallString<256> Model(EntryValue.CanonicalPath);
    Model += ".neverc-stage-%%%%%%%%";
    const unsigned CreateMode =
        EntryValue.Output.OwnerOnly
            ? static_cast<unsigned>(sys::fs::owner_read | sys::fs::owner_write)
            : static_cast<unsigned>(sys::fs::all_read | sys::fs::all_write);
    const sys::fs::OpenFlags CreateFlags =
        EntryValue.Output.OwnerOnly
            ? static_cast<sys::fs::OpenFlags>(sys::fs::OF_Exclusive |
                                              sys::fs::OF_NoInherit |
                                              sys::fs::OF_AccessControl)
            : sys::fs::OF_None;
    auto Temporary = sys::fs::TempFile::create(Model, CreateMode, CreateFlags);
    if (!Temporary)
      return Fail(Temporary.takeError());
    EntryValue.Staging.emplace(std::move(*Temporary));
    if (EntryValue.Output.OwnerOnly) {
      bool DeleteDispositionActive = false;
#if defined(_WIN32)
      DeleteDispositionActive = !EntryValue.Staging->RemoveOnClose;
#endif
      const std::error_code EC = output_platform::restrictFileToOwner(
          EntryValue.Staging->TmpName, EntryValue.Staging->FD,
          DeleteDispositionActive);
#if defined(_WIN32)
      EntryValue.Staging->RemoveOnClose = !DeleteDispositionActive;
#endif
      if (EC)
        return Fail(errorCodeToError(EC));
    }
    Error WriteFailure = Error::success();
    {
      raw_fd_ostream Stream(EntryValue.Staging->FD, false);
      Stream.write(
          reinterpret_cast<const char *>(EntryValue.Output.Bytes.data()),
          EntryValue.Output.Bytes.size());
      Stream.flush();
      if (Stream.has_error()) {
        WriteFailure = joinErrors(bundleError("could not write staged output"),
                                  errorCodeToError(Stream.error()));
        Stream.clear_error();
      }
    }
    if (WriteFailure)
      return Fail(std::move(WriteFailure));
    if (EntryValue.Output.Executable || EntryValue.Output.OwnerOnly) {
      const sys::fs::perms Permissions =
          EntryValue.Output.OwnerOnly
              ? (EntryValue.Output.Executable
                     ? sys::fs::owner_all
                     : sys::fs::owner_read | sys::fs::owner_write)
              : sys::fs::all_read | sys::fs::owner_write | sys::fs::all_exe;
      if (std::error_code EC =
              sys::fs::setPermissions(EntryValue.Staging->FD, Permissions))
        return Fail(errorCodeToError(EC));
    }
    if (std::error_code Injected =
            fault(OutputBundleOperation::SyncStaging, EntryValue.CanonicalPath))
      return Fail(errorCodeToError(Injected));
    if (std::error_code EC =
            output_platform::syncFileDescriptor(EntryValue.Staging->FD))
      return Fail(errorCodeToError(EC));
  }
  if (Error E = createJournal())
    return Fail(std::move(E));
  State = OutputBundleState::Prepared;
  return Error::success();
}

Error OutputBundleTransaction::rollback() {
  Error Failures = Error::success();
  bool FilesystemChanged = false;
  for (Entry &EntryValue : Entries) {
    if (EntryValue.Published) {
      FilesystemChanged = true;
      std::error_code EC = fault(OutputBundleOperation::RemovePublished,
                                 EntryValue.CanonicalPath);
      if (!EC)
        EC = sys::fs::remove(EntryValue.CanonicalPath);
      if (EC == std::make_error_code(std::errc::no_such_file_or_directory))
        EC.clear();
      if (EC)
        Failures = joinErrors(std::move(Failures), errorCodeToError(EC));
    }
  }
  for (auto It = Entries.rbegin(); It != Entries.rend(); ++It) {
    if (It->BackupPath.empty())
      continue;
    if (!It->Published) {
      std::error_code EC = sys::fs::remove(It->BackupPath);
      if (EC == std::make_error_code(std::errc::no_such_file_or_directory)) {
        It->BackupPath.clear();
        continue;
      }
      if (EC)
        Failures = joinErrors(std::move(Failures), errorCodeToError(EC));
      else
        It->BackupPath.clear();
      continue;
    }
    std::error_code EC =
        fault(OutputBundleOperation::RestoreBackup, It->CanonicalPath);
    if (!EC) {
      EC = It->ExistingLinkLike
               ? output_platform::renameLinkLikePath(It->BackupPath,
                                                     It->CanonicalPath)
               : sys::fs::rename(It->BackupPath, It->CanonicalPath);
    }
    if (EC)
      Failures = joinErrors(std::move(Failures), errorCodeToError(EC));
    else {
      It->BackupPath.clear();
      It->Published = false;
    }
  }
  for (Entry &EntryValue : Entries)
    if (EntryValue.BackupPath.empty())
      EntryValue.Published = false;
  for (Entry &EntryValue : Entries)
    if (Error Discard = discardStaging(EntryValue))
      Failures = joinErrors(std::move(Failures), std::move(Discard));
  if (FilesystemChanged) {
    for (const Entry &EntryValue : Entries) {
      std::error_code EC = fault(OutputBundleOperation::SyncRecoveryState,
                                 EntryValue.CanonicalPath);
      if (!EC)
        EC = output_platform::syncParentDirectory(EntryValue.CanonicalPath);
      if (EC == std::make_error_code(std::errc::operation_not_supported)) {
        Flags |= OutputDurabilityUnconfirmed;
        continue;
      }
      if (EC)
        Failures = joinErrors(std::move(Failures), errorCodeToError(EC));
    }
  }
  if (!Failures && !JournalPath.empty()) {
    std::error_code EC = sys::fs::remove(JournalPath);
    if (EC == std::make_error_code(std::errc::no_such_file_or_directory))
      EC.clear();
    if (EC)
      Failures = joinErrors(std::move(Failures), errorCodeToError(EC));
    else
      JournalPath.clear();
  }
  if (Failures) {
    State = OutputBundleState::FailedPartial;
    Flags |= OutputMayBePartial | OutputRecoveryRequired;
    releaseLeases();
    return Failures;
  }
  Flags &= ~(OutputPublished | OutputDurable | OutputMayBePartial |
             OutputRecoveryRequired);
  if (!FilesystemChanged)
    Flags &= ~OutputDurabilityUnconfirmed;
  PublicationGeneration = 0;
  State = OutputBundleState::Aborted;
  releaseLeases();
  return Error::success();
}

Error OutputBundleTransaction::rollbackAfter(Error Failure) {
  if (Error Rollback = rollback())
    return joinErrors(std::move(Failure), std::move(Rollback));
  return Failure;
}

Error OutputBundleTransaction::backupExistingOutputs() {
  for (Entry &EntryValue : Entries) {
    const std::error_code MissingPath =
        std::make_error_code(std::errc::no_such_file_or_directory);
    const std::error_code LinkError = output_platform::isLinkLikePath(
        EntryValue.CanonicalPath, EntryValue.ExistingLinkLike);
    if (LinkError == MissingPath)
      continue;
    if (LinkError)
      return errorCodeToError(LinkError);

    sys::fs::file_status ExistingStatus;
    if (!EntryValue.ExistingLinkLike) {
      const std::error_code StatusError = sys::fs::status(
          EntryValue.CanonicalPath, ExistingStatus, /*follow=*/false);
      if (StatusError == MissingPath)
        continue;
      if (StatusError)
        return errorCodeToError(StatusError);
      if (!sys::fs::exists(ExistingStatus))
        continue;
    }
    EntryValue.HadExisting = true;
    if (EntryValue.ExistingLinkLike &&
        EntryValue.Output.Action == OutputBundleFileAction::Publish)
      return bundleError(
          Twine(
              "symbolic-link or reparse-point output paths are unsupported: ") +
          EntryValue.CanonicalPath);

    if (std::error_code Injected = fault(OutputBundleOperation::BackupExisting,
                                         EntryValue.CanonicalPath))
      return errorCodeToError(Injected);

    SmallString<256> Model(EntryValue.CanonicalPath);
    Model += ".neverc-backup-%%%%%%%%";
    SmallString<256> Backup;
    if (std::error_code EC = sys::fs::createUniqueFile(Model, Backup))
      return errorCodeToError(EC);
    EntryValue.BackupPath = Backup.str().str();
    std::error_code EC = sys::fs::remove(EntryValue.BackupPath);
    if (!EC && !EntryValue.ExistingLinkLike) {
      EC = sys::fs::create_hard_link(EntryValue.CanonicalPath,
                                     EntryValue.BackupPath);
      if (EC) {
        int BackupFD = -1;
        SmallString<256> CopiedBackup;
        EC = sys::fs::createUniqueFile(
            Model, BackupFD, CopiedBackup,
            static_cast<sys::fs::OpenFlags>(sys::fs::OF_Exclusive |
                                            sys::fs::OF_NoInherit |
                                            sys::fs::OF_AccessControl),
            static_cast<unsigned>(sys::fs::owner_read | sys::fs::owner_write));
        if (!EC) {
          EntryValue.BackupPath = CopiedBackup.str().str();
#if defined(_WIN32)
          EC = output_platform::applyBackupFilePermissions(
              EntryValue.CanonicalPath, EntryValue.BackupPath, BackupFD,
              static_cast<unsigned>(ExistingStatus.permissions()));
#else
          bool DeleteDispositionActive = false;
          EC = output_platform::restrictFileToOwner(
              EntryValue.BackupPath, BackupFD, DeleteDispositionActive);
#endif
          if (!EC)
            EC = sys::fs::copy_file(EntryValue.CanonicalPath, BackupFD);
#if !defined(_WIN32)
          if (!EC)
            EC = output_platform::applyBackupFilePermissions(
                EntryValue.CanonicalPath, EntryValue.BackupPath, BackupFD,
                static_cast<unsigned>(ExistingStatus.permissions()));
#endif
          if (!EC)
            EC = output_platform::syncFileDescriptor(BackupFD);
          const std::error_code Close =
              output_platform::closeFileDescriptor(BackupFD);
          if (!EC)
            EC = Close;
        }
      }
    }
    if (EC)
      return errorCodeToError(EC);
  }
  return Error::success();
}

Error OutputBundleTransaction::recordBackupRecoveryEvidence(
    bool &AllDirectoriesSynced) {
  SmallString<512> RecoveryRecords;
  raw_svector_ostream RecoveryStream(RecoveryRecords);
  for (const Entry &EntryValue : Entries)
    if (!EntryValue.BackupPath.empty())
      RecoveryStream << "backup " << EntryValue.Output.Name << " "
                     << EntryValue.BackupPath << "\n";
  if (!RecoveryRecords.empty())
    if (Error E = appendAndSyncJournal(RecoveryRecords,
                                       OutputBundleOperation::SyncJournal))
      return E;

  for (const Entry &EntryValue : Entries) {
    if (EntryValue.BackupPath.empty() ||
        (EntryValue.ExistingLinkLike &&
         EntryValue.Output.Action == OutputBundleFileAction::Remove))
      continue;
    std::error_code EC =
        fault(OutputBundleOperation::SyncRecoveryState, EntryValue.BackupPath);
    if (!EC)
      EC = output_platform::syncParentDirectory(EntryValue.BackupPath);
    if (EC == std::make_error_code(std::errc::operation_not_supported)) {
      AllDirectoriesSynced = false;
      continue;
    }
    if (EC)
      return errorCodeToError(EC);
  }
  return Error::success();
}

Error OutputBundleTransaction::publishStagedOutput(
    Entry &EntryValue, OutputBundleOperation Operation) {
  if (std::error_code Injected = fault(Operation, EntryValue.CanonicalPath))
    return errorCodeToError(Injected);
  if (!EntryValue.Staging)
    return bundleError("staged output disappeared before publication");

  bool DeleteDispositionActive = false;
#if defined(_WIN32)
  DeleteDispositionActive = !EntryValue.Staging->RemoveOnClose;
#endif
  const std::error_code RenameError = output_platform::renameStagingFile(
      EntryValue.Staging->TmpName, EntryValue.Staging->FD,
      DeleteDispositionActive, EntryValue.CanonicalPath);
#if defined(_WIN32)
  EntryValue.Staging->RemoveOnClose = !DeleteDispositionActive;
#endif
  if (RenameError)
    return errorCodeToError(RenameError);

  EntryValue.Published = true;
  Error Finalize = EntryValue.Staging->keep();
  if (Finalize && EntryValue.Staging->FD != -1) {
    const std::error_code Close =
        output_platform::closeFileDescriptor(EntryValue.Staging->FD);
    EntryValue.Staging->FD = -1;
    if (Close)
      Finalize = joinErrors(std::move(Finalize), errorCodeToError(Close));
  }
  if (Finalize && !EntryValue.Staging->TmpName.empty()) {
    sys::DontRemoveFileOnSignal(EntryValue.Staging->TmpName);
    EntryValue.Staging->TmpName.clear();
#if defined(_WIN32)
    EntryValue.Staging->RemoveOnClose = false;
#endif
  }
  EntryValue.Staging.reset();
  return Finalize;
}

Error OutputBundleTransaction::publishSideOutputs(bool &AllDirectoriesSynced) {
  // Publish replacement side outputs before the main output. A removed side
  // output follows the main instead: after a crash, a stale digest-mismatched
  // map is safer than losing the map while the old release image is still
  // durable. Consumers must still validate side-output digests after an
  // unclean or durability-unconfirmed shutdown.
  for (size_t Index = 0; Index != Entries.size(); ++Index) {
    if (Index == MainIndex ||
        Entries[Index].Output.Action == OutputBundleFileAction::Remove)
      continue;
    if (Error E = publishStagedOutput(Entries[Index],
                                      OutputBundleOperation::PublishSide))
      return E;
  }

  // Make every side-output directory update durable before publishing the
  // main output that identifies the bundle generation.
  for (size_t Index = 0; Index != Entries.size(); ++Index) {
    if (Index == MainIndex || !Entries[Index].Published)
      continue;
    std::error_code EC = fault(OutputBundleOperation::SyncDirectory,
                               Entries[Index].CanonicalPath);
    if (!EC)
      EC = output_platform::syncParentDirectory(Entries[Index].CanonicalPath);
    if (EC == std::make_error_code(std::errc::operation_not_supported)) {
      AllDirectoriesSynced = false;
      continue;
    }
    if (EC)
      return errorCodeToError(EC);
  }
  return Error::success();
}

Error OutputBundleTransaction::publishMainAndRemovals(
    bool &AllDirectoriesSynced) {
  if (Entries[MainIndex].Output.Action == OutputBundleFileAction::Publish)
    if (Error E = publishStagedOutput(Entries[MainIndex],
                                      OutputBundleOperation::PublishMain))
      return E;

  const bool HasRemovedOutput =
      std::any_of(Entries.begin(), Entries.end(), [](const Entry &EntryValue) {
        return EntryValue.Output.Action == OutputBundleFileAction::Remove &&
               EntryValue.HadExisting;
      });
  if (Entries[MainIndex].Output.Action == OutputBundleFileAction::Publish &&
      HasRemovedOutput) {
    std::error_code EC = fault(OutputBundleOperation::SyncDirectory,
                               Entries[MainIndex].CanonicalPath);
    if (!EC)
      EC = output_platform::syncParentDirectory(
          Entries[MainIndex].CanonicalPath);
    if (EC == std::make_error_code(std::errc::operation_not_supported)) {
      AllDirectoriesSynced = false;
    } else if (EC) {
      return errorCodeToError(EC);
    }
  }

  for (size_t Index = 0; Index != Entries.size(); ++Index) {
    if (Entries[Index].Output.Action != OutputBundleFileAction::Remove ||
        !Entries[Index].HadExisting)
      continue;
    if (std::error_code Injected = fault(OutputBundleOperation::PublishSide,
                                         Entries[Index].CanonicalPath))
      return errorCodeToError(Injected);
    std::error_code EC =
        Entries[Index].ExistingLinkLike
            ? output_platform::renameLinkLikePath(Entries[Index].CanonicalPath,
                                                  Entries[Index].BackupPath)
            : sys::fs::remove(Entries[Index].CanonicalPath);
    if (EC == std::make_error_code(std::errc::no_such_file_or_directory))
      EC.clear();
    if (EC)
      return errorCodeToError(EC);
    Entries[Index].Published = true;
  }
  return Error::success();
}

Error OutputBundleTransaction::finalizePublication(bool &AllDirectoriesSynced) {
  State = OutputBundleState::Committed;
  PublicationGeneration = 1;
  Flags |= OutputPublished;
  for (const Entry &EntryValue : Entries) {
    std::error_code EC =
        fault(OutputBundleOperation::SyncDirectory, EntryValue.CanonicalPath);
    if (!EC)
      EC = output_platform::syncParentDirectory(EntryValue.CanonicalPath);
    if (EC == std::make_error_code(std::errc::operation_not_supported)) {
      AllDirectoriesSynced = false;
      continue;
    }
    if (EC) {
      Flags |= OutputDurabilityUnconfirmed | OutputRecoveryRequired;
      releaseLeases();
      return errorCodeToError(EC);
    }
  }
  Flags |= AllDirectoriesSynced ? OutputDurable : OutputDurabilityUnconfirmed;

  if (std::error_code Injected =
          fault(OutputBundleOperation::CompleteJournal, JournalPath)) {
    Flags &= ~OutputDurable;
    Flags |= OutputDurabilityUnconfirmed | OutputRecoveryRequired;
    releaseLeases();
    return errorCodeToError(Injected);
  }
  if (Error E = appendAndSyncJournal(
          "completed\n", OutputBundleOperation::SyncCompletedJournal)) {
    Flags &= ~OutputDurable;
    Flags |= OutputDurabilityUnconfirmed | OutputRecoveryRequired;
    releaseLeases();
    return E;
  }

  Error BackupCleanupFailures = Error::success();
  for (Entry &EntryValue : Entries)
    if (!EntryValue.BackupPath.empty()) {
      if (std::error_code EC = sys::fs::remove(EntryValue.BackupPath))
        BackupCleanupFailures =
            joinErrors(std::move(BackupCleanupFailures), errorCodeToError(EC));
      else
        EntryValue.BackupPath.clear();
    }
  if (BackupCleanupFailures) {
    Flags |= OutputRecoveryRequired;
    releaseLeases();
    return BackupCleanupFailures;
  }
  if (std::error_code Injected =
          fault(OutputBundleOperation::CleanupJournal, JournalPath)) {
    Flags |= OutputRecoveryRequired;
    releaseLeases();
    return errorCodeToError(Injected);
  }
  if (std::error_code EC = sys::fs::remove(JournalPath)) {
    Flags |= OutputRecoveryRequired;
    releaseLeases();
    return errorCodeToError(EC);
  }
  JournalPath.clear();
  releaseLeases();
  return Error::success();
}

Expected<OutputBundleSummary> OutputBundleTransaction::commit() {
  if (State == OutputBundleState::Committed)
    return summary();
  if (State == OutputBundleState::Open)
    if (Error E = prepare())
      return std::move(E);
  if (State != OutputBundleState::Prepared)
    return bundleError("only a prepared bundle can be committed");
  if (Error E = acquirePublicationLocks())
    return std::move(E);

  bool AllDirectoriesSynced = (Flags & OutputDurabilityUnconfirmed) == 0;
  if (Error E = backupExistingOutputs())
    return rollbackAfter(std::move(E));
  if (Error E = recordBackupRecoveryEvidence(AllDirectoriesSynced))
    return rollbackAfter(std::move(E));
  if (Error E = publishSideOutputs(AllDirectoriesSynced))
    return rollbackAfter(std::move(E));
  if (Error E = publishMainAndRemovals(AllDirectoriesSynced))
    return rollbackAfter(std::move(E));
  if (Error E = finalizePublication(AllDirectoriesSynced))
    return std::move(E);
  return summary();
}

Error OutputBundleTransaction::abort() {
  if (State == OutputBundleState::Aborted && JournalPath.empty())
    return Error::success();
  if (State == OutputBundleState::Committed)
    return bundleError("a committed bundle cannot be aborted");
  if (State == OutputBundleState::FailedPartial)
    return bundleError("a partially failed bundle requires recovery");
  Error Failures = Error::success();
  for (Entry &EntryValue : Entries)
    if (Error Discard = discardStaging(EntryValue))
      Failures = joinErrors(std::move(Failures), std::move(Discard));
  State = OutputBundleState::Aborted;
  if (!JournalPath.empty()) {
    std::error_code EC = sys::fs::remove(JournalPath);
    if (EC == std::make_error_code(std::errc::no_such_file_or_directory))
      EC.clear();
    if (EC) {
      Flags |= OutputRecoveryRequired;
      Failures = joinErrors(std::move(Failures), errorCodeToError(EC));
    } else
      JournalPath.clear();
  }
  releaseLeases();
  return Failures;
}

void OutputBundleTransaction::releaseLeases() {
  PublicationLocks.clear();
  for (OutputPathLease &Lease : PublicationLeases)
    Lease.release();
  PublicationLeases.clear();
  for (OutputPathLease &Lease : Leases)
    Lease.release();
  Leases.clear();
}

OutputBundleSummary OutputBundleTransaction::summary() const {
  OutputBundleSummary Result;
  Result.State = State;
  Result.Flags = Flags;
  Result.OutputCount = Entries.size();
  Result.PublicationGeneration = PublicationGeneration;
  Result.MainDigest = MainDigest;
  Result.TransactionID = TransactionID;
  Result.JournalPath = JournalPath;
  return Result;
}

} // namespace neverc
