#include "neverc/Foundation/Core/OutputBundleTransaction.h"
#include "OutputPlatform.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SHA256.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <set>
#include <utility>

using namespace llvm;

namespace neverc {
namespace {

Error bundleError(const Twine &Message) {
  return createStringError(inconvertibleErrorCode(),
                           "output bundle: " + Message);
}

std::string digestText(ArrayRef<uint8_t> Digest) {
  static constexpr char Hex[] = "0123456789abcdef";
  std::string Result;
  Result.reserve(Digest.size() * 2);
  for (uint8_t Byte : Digest) {
    Result.push_back(Hex[Byte >> 4]);
    Result.push_back(Hex[Byte & 0xf]);
  }
  return Result;
}

} // namespace

struct OutputBundleTransaction::Entry {
  OutputBundleFile Output;
  std::string CanonicalPath;
  std::optional<sys::fs::TempFile> Staging;
  std::string BackupPath;
  bool HadExisting = false;
  bool Published = false;
};

OutputBundleTransaction::OutputBundleTransaction(
    OutputCoordinator &CoordinatorValue, std::vector<Entry> EntriesValue,
    std::vector<OutputPathLease> LeasesValue,
    FaultInjector InjectFaultValue)
    : Coordinator(CoordinatorValue), Entries(std::move(EntriesValue)),
      Leases(std::move(LeasesValue)),
      InjectFault(std::move(InjectFaultValue)) {
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
    OutputCoordinator::CancellationCheck IsCancelled,
    FaultInjector InjectFault, OutputLeaseOwner LeaseOwner) {
  if (Outputs.empty())
    return bundleError("at least one output is required");
  size_t MainCount = 0;
  std::set<std::string> Names;
  std::vector<Entry> Entries;
  std::vector<StringRef> Paths;
  Entries.reserve(Outputs.size());
  Paths.reserve(Outputs.size());
  for (const OutputBundleFile &Output : Outputs) {
    if (Output.Name.empty() || Output.Path.empty() ||
        !Names.insert(Output.Name).second)
      return bundleError("output names and paths must be unique");
    MainCount += Output.Main ? 1 : 0;
    auto Canonical = Coordinator.canonicalize(Output.Path);
    if (!Canonical)
      return Canonical.takeError();
    Entries.push_back({Output, std::move(*Canonical), std::nullopt, {},
                       false, false});
  }
  if (MainCount != 1)
    return bundleError("exactly one main output is required");
  for (const Entry &EntryValue : Entries)
    Paths.push_back(EntryValue.CanonicalPath);
  auto Leases = Coordinator.acquireAll(
      Paths, std::move(IsCancelled), LeaseOwner);
  if (!Leases)
    return Leases.takeError();
  return std::unique_ptr<OutputBundleTransaction>(
      new OutputBundleTransaction(
          Coordinator, std::move(Entries), std::move(*Leases),
          std::move(InjectFault)));
}

OutputBundleTransaction::~OutputBundleTransaction() {
  if (State == OutputBundleState::Open ||
      State == OutputBundleState::Prepared)
    consumeError(abort());
}

std::error_code
OutputBundleTransaction::fault(OutputBundleOperation Operation,
                               StringRef Path) const {
  return InjectFault ? InjectFault(Operation, Path) : std::error_code();
}

Error OutputBundleTransaction::createJournal() {
  if (std::error_code Injected =
          fault(OutputBundleOperation::CreateJournal,
                Entries[MainIndex].CanonicalPath))
    return errorCodeToError(Injected);
  SmallString<256> Model(Entries[MainIndex].CanonicalPath);
  Model += ".neverc-journal-%%%%%%%%";
  int FD = -1;
  SmallString<256> Path;
  if (std::error_code EC =
          sys::fs::createUniqueFile(Model, FD, Path))
    return errorCodeToError(EC);
  JournalPath = Path.str().str();
  TransactionID = sys::path::filename(Path).str();
  raw_fd_ostream Stream(FD, true);
  Stream << "NEVERC_OUTPUT_BUNDLE_V1\n";
  Stream << "transaction " << TransactionID << "\n";
  for (const Entry &EntryValue : Entries) {
    const auto Digest = SHA256::hash(EntryValue.Output.Bytes);
    Stream << (EntryValue.Output.Main ? "main " : "side ")
           << EntryValue.Output.Name << " "
           << digestText(Digest) << " "
           << EntryValue.CanonicalPath << "\n";
  }
  Stream.flush();
  if (Stream.has_error())
    return bundleError("could not write recovery journal");
  if (std::error_code Injected =
          fault(OutputBundleOperation::SyncJournal, JournalPath))
    return errorCodeToError(Injected);
  if (std::error_code EC = output_platform::syncFileDescriptor(FD))
    return errorCodeToError(EC);
  return Error::success();
}

Error OutputBundleTransaction::prepare() {
  if (State == OutputBundleState::Prepared)
    return Error::success();
  if (State != OutputBundleState::Open)
    return bundleError("only an open bundle can be prepared");
  for (Entry &EntryValue : Entries) {
    if (std::error_code Injected =
            fault(OutputBundleOperation::WriteStaging,
                  EntryValue.CanonicalPath))
      return errorCodeToError(Injected);
    SmallString<256> Model(EntryValue.CanonicalPath);
    Model += ".neverc-stage-%%%%%%%%";
    auto Temporary = sys::fs::TempFile::create(Model);
    if (!Temporary)
      return Temporary.takeError();
    raw_fd_ostream Stream(Temporary->FD, false);
    Stream.write(
        reinterpret_cast<const char *>(EntryValue.Output.Bytes.data()),
        EntryValue.Output.Bytes.size());
    Stream.flush();
    if (Stream.has_error()) {
      consumeError(Temporary->discard());
      return bundleError("could not write staged output");
    }
    if (std::error_code Injected =
            fault(OutputBundleOperation::SyncStaging,
                  EntryValue.CanonicalPath)) {
      consumeError(Temporary->discard());
      return errorCodeToError(Injected);
    }
    if (std::error_code EC =
            output_platform::syncFileDescriptor(Temporary->FD)) {
      consumeError(Temporary->discard());
      return errorCodeToError(EC);
    }
    EntryValue.Staging.emplace(std::move(*Temporary));
  }
  if (Error E = createJournal())
    return E;
  State = OutputBundleState::Prepared;
  return Error::success();
}

Error OutputBundleTransaction::rollback() {
  Error Failures = Error::success();
  for (Entry &EntryValue : Entries) {
    if (EntryValue.Published) {
      std::error_code EC =
          fault(OutputBundleOperation::RemovePublished,
                EntryValue.CanonicalPath);
      if (!EC)
        EC = sys::fs::remove(EntryValue.CanonicalPath);
      if (EC)
        Failures =
            joinErrors(std::move(Failures), errorCodeToError(EC));
      else
        EntryValue.Published = false;
    }
  }
  for (auto It = Entries.rbegin(); It != Entries.rend(); ++It) {
    if (It->BackupPath.empty())
      continue;
    std::error_code EC =
        fault(OutputBundleOperation::RestoreBackup,
              It->CanonicalPath);
    if (!EC)
      EC = sys::fs::rename(It->BackupPath, It->CanonicalPath);
    if (EC)
      Failures =
          joinErrors(std::move(Failures), errorCodeToError(EC));
    else
      It->BackupPath.clear();
  }
  for (Entry &EntryValue : Entries)
    if (EntryValue.Staging) {
      Error Discard = EntryValue.Staging->discard();
      EntryValue.Staging.reset();
      if (Discard)
        Failures =
            joinErrors(std::move(Failures), std::move(Discard));
    }
  if (Failures) {
    State = OutputBundleState::FailedPartial;
    Flags |= OutputMayBePartial | OutputRecoveryRequired;
    releaseLeases();
    return Failures;
  }
  State = OutputBundleState::Aborted;
  if (!JournalPath.empty()) {
    (void)sys::fs::remove(JournalPath);
    JournalPath.clear();
  }
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

  for (Entry &EntryValue : Entries) {
    if (!sys::fs::exists(EntryValue.CanonicalPath))
      continue;
    EntryValue.HadExisting = true;
    if (std::error_code Injected =
            fault(OutputBundleOperation::BackupExisting,
                  EntryValue.CanonicalPath)) {
      Error Original = errorCodeToError(Injected);
      if (Error Rollback = rollback())
        return joinErrors(std::move(Original), std::move(Rollback));
      return std::move(Original);
    }
    SmallString<256> Model(EntryValue.CanonicalPath);
    Model += ".neverc-backup-%%%%%%%%";
    SmallString<256> Backup;
    if (std::error_code EC =
            sys::fs::createUniqueFile(Model, Backup)) {
      Error Original = errorCodeToError(EC);
      if (Error Rollback = rollback())
        return joinErrors(std::move(Original), std::move(Rollback));
      return std::move(Original);
    }
    EntryValue.BackupPath = Backup.str().str();
    if (std::error_code EC = sys::fs::rename(
            EntryValue.CanonicalPath, EntryValue.BackupPath)) {
      Error Original = errorCodeToError(EC);
      if (Error Rollback = rollback())
        return joinErrors(std::move(Original), std::move(Rollback));
      return std::move(Original);
    }
  }

  auto Publish = [&](Entry &EntryValue,
                     OutputBundleOperation Operation) -> Error {
    if (std::error_code Injected =
            fault(Operation, EntryValue.CanonicalPath))
      return errorCodeToError(Injected);
    if (!EntryValue.Staging)
      return bundleError("staged output disappeared before publication");
    if (Error E =
            EntryValue.Staging->keep(EntryValue.CanonicalPath))
      return E;
    EntryValue.Staging.reset();
    EntryValue.Published = true;
    return Error::success();
  };

  for (size_t Index = 0; Index != Entries.size(); ++Index) {
    if (Index == MainIndex)
      continue;
    if (Error E =
            Publish(Entries[Index],
                    OutputBundleOperation::PublishSide)) {
      if (Error Rollback = rollback())
        return joinErrors(std::move(E), std::move(Rollback));
      return std::move(E);
    }
  }
  if (Error E =
          Publish(Entries[MainIndex],
                  OutputBundleOperation::PublishMain)) {
    if (Error Rollback = rollback())
      return joinErrors(std::move(E), std::move(Rollback));
    return std::move(E);
  }

  State = OutputBundleState::Committed;
  PublicationGeneration = 1;
  Flags |= OutputPublished;
  for (const Entry &EntryValue : Entries) {
    std::error_code EC =
        fault(OutputBundleOperation::SyncDirectory,
              EntryValue.CanonicalPath);
    if (!EC)
      EC = output_platform::syncParentDirectory(
          EntryValue.CanonicalPath);
    if (EC &&
        EC != std::make_error_code(
                  std::errc::operation_not_supported)) {
      Flags |= OutputDurabilityUnconfirmed |
               OutputRecoveryRequired;
      releaseLeases();
      return errorCodeToError(EC);
    }
  }
  Flags |= OutputDurable;

  if (std::error_code Injected =
          fault(OutputBundleOperation::CompleteJournal,
                JournalPath)) {
    Flags |= OutputDurabilityUnconfirmed |
             OutputRecoveryRequired;
    releaseLeases();
    return errorCodeToError(Injected);
  }
  {
    std::error_code EC;
    raw_fd_ostream Journal(JournalPath, EC, sys::fs::OF_Append);
    if (EC) {
      Flags |= OutputDurabilityUnconfirmed |
               OutputRecoveryRequired;
      releaseLeases();
      return errorCodeToError(EC);
    }
    Journal << "completed\n";
    Journal.flush();
  }
  for (Entry &EntryValue : Entries)
    if (!EntryValue.BackupPath.empty()) {
      (void)sys::fs::remove(EntryValue.BackupPath);
      EntryValue.BackupPath.clear();
    }
  if (std::error_code Injected =
          fault(OutputBundleOperation::CleanupJournal,
                JournalPath)) {
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
  return summary();
}

Error OutputBundleTransaction::abort() {
  if (State == OutputBundleState::Aborted)
    return Error::success();
  if (State == OutputBundleState::Committed)
    return bundleError("a committed bundle cannot be aborted");
  if (State == OutputBundleState::FailedPartial)
    return bundleError("a partially failed bundle requires recovery");
  for (Entry &EntryValue : Entries)
    if (EntryValue.Staging) {
      consumeError(EntryValue.Staging->discard());
      EntryValue.Staging.reset();
    }
  State = OutputBundleState::Aborted;
  if (!JournalPath.empty()) {
    (void)sys::fs::remove(JournalPath);
    JournalPath.clear();
  }
  releaseLeases();
  return Error::success();
}

void OutputBundleTransaction::releaseLeases() {
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
