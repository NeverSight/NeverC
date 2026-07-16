#include "neverc/Foundation/Core/OutputTransaction.h"
#include "OutputPlatform.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/SHA256.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <limits>
#include <utility>

using namespace llvm;

namespace neverc {

OutputTransaction::OutputTransaction(OutputDestinationKind KindValue,
                                     std::string DestinationValue,
                                     uint64_t SizeBudgetValue)
    : Kind(KindValue), Destination(std::move(DestinationValue)),
      SizeBudget(SizeBudgetValue) {}

std::shared_ptr<OutputTransaction>
OutputTransaction::createMemory(StringRef LogicalName, uint64_t SizeBudget) {
  return std::shared_ptr<OutputTransaction>(new OutputTransaction(
      OutputDestinationKind::Memory, LogicalName.str(), SizeBudget));
}

Expected<std::shared_ptr<OutputTransaction>>
OutputTransaction::createFile(
    OutputCoordinator &Coordinator, StringRef FinalPath, uint64_t SizeBudget,
    OutputCoordinator::CancellationCheck IsCancelled,
    FileFaultInjector InjectFault, OutputLeaseOwner LeaseOwner) {
  auto Lease =
      Coordinator.acquire(FinalPath, std::move(IsCancelled), LeaseOwner);
  if (!Lease)
    return Lease.takeError();

  SmallString<256> Model(Lease->canonicalPath());
  Model += ".tmp-%%%%%%";
  auto Temporary = sys::fs::TempFile::create(Model);
  if (!Temporary)
    return Temporary.takeError();

  auto Result = std::shared_ptr<OutputTransaction>(new OutputTransaction(
      OutputDestinationKind::File, Lease->canonicalPath(), SizeBudget));
  Result->PathLease.emplace(std::move(*Lease));
  Result->TemporaryFile.emplace(std::move(*Temporary));
  Result->InjectFileFault = std::move(InjectFault);
  return Result;
}

std::shared_ptr<OutputTransaction>
OutputTransaction::createStream(StringRef LogicalName, uint64_t SizeBudget,
                                StreamCommitCallback Commit) {
  auto Result = std::shared_ptr<OutputTransaction>(new OutputTransaction(
      OutputDestinationKind::Stream, LogicalName.str(), SizeBudget));
  Result->CommitStream = std::move(Commit);
  return Result;
}

OutputTransaction::~OutputTransaction() {
  (void)abort();
}

OutputTransactionResult
OutputTransaction::write(ArrayRef<uint8_t> Bytes) {
  std::lock_guard<std::mutex> Lock(Mutex);
  if (State != OutputTransactionState::Open)
    return OutputTransactionResult::InvalidState;
  if (Bytes.size() > SizeBudget ||
      Staging.size() > SizeBudget - Bytes.size())
    return OutputTransactionResult::ResourceExhausted;
  Staging.insert(Staging.end(), Bytes.begin(), Bytes.end());
  return OutputTransactionResult::Success;
}

OutputTransactionResult
OutputTransaction::writeAt(uint64_t Offset, ArrayRef<uint8_t> Bytes) {
  std::lock_guard<std::mutex> Lock(Mutex);
  if (State != OutputTransactionState::Open)
    return OutputTransactionResult::InvalidState;
  if (Offset > SizeBudget || Bytes.size() > SizeBudget - Offset ||
      Offset > std::numeric_limits<size_t>::max())
    return OutputTransactionResult::ResourceExhausted;
  const size_t NativeOffset = static_cast<size_t>(Offset);
  const size_t End = NativeOffset + Bytes.size();
  if (End > Staging.size())
    Staging.resize(End, 0);
  std::copy(Bytes.begin(), Bytes.end(), Staging.begin() + NativeOffset);
  return OutputTransactionResult::Success;
}

OutputTransactionResult
OutputTransaction::tell(uint64_t &OutPosition) const {
  std::lock_guard<std::mutex> Lock(Mutex);
  if (State != OutputTransactionState::Open)
    return OutputTransactionResult::InvalidState;
  OutPosition = Staging.size();
  return OutputTransactionResult::Success;
}

OutputTransactionResult OutputTransaction::truncate(uint64_t Size) {
  std::lock_guard<std::mutex> Lock(Mutex);
  if (State != OutputTransactionState::Open)
    return OutputTransactionResult::InvalidState;
  if (Size > SizeBudget || Size > std::numeric_limits<size_t>::max())
    return OutputTransactionResult::ResourceExhausted;
  Staging.resize(static_cast<size_t>(Size), 0);
  return OutputTransactionResult::Success;
}

OutputTransactionResult
OutputTransaction::setMetadata(StringRef Key, StringRef Value) {
  std::lock_guard<std::mutex> Lock(Mutex);
  if (State != OutputTransactionState::Open)
    return OutputTransactionResult::InvalidState;
  if (Key.empty() || Key.contains('\0') || Value.contains('\0'))
    return OutputTransactionResult::InvalidArgument;
  Metadata.insert_or_assign(Key.str(), Value.str());
  return OutputTransactionResult::Success;
}

OutputTransactionResult OutputTransaction::writeFileStagingLocked() {
  if (!TemporaryFile)
    return OutputTransactionResult::InvalidState;
  raw_fd_ostream Stream(TemporaryFile->FD, false);
  Stream.write(reinterpret_cast<const char *>(Staging.data()),
               Staging.size());
  Stream.flush();
  if (!Stream.has_error())
    return OutputTransactionResult::Success;
  Stream.clear_error();
  return OutputTransactionResult::IOFailure;
}

OutputTransactionResult OutputTransaction::finish() {
  std::lock_guard<std::mutex> Lock(Mutex);
  if (State == OutputTransactionState::Finished ||
      State == OutputTransactionState::Committed)
    return OutputTransactionResult::Success;
  if (State != OutputTransactionState::Open)
    return OutputTransactionResult::InvalidState;
  if (Kind == OutputDestinationKind::File) {
    std::error_code WriteFault =
        InjectFileFault
            ? InjectFileFault(OutputFileOperation::WriteStaging)
            : std::error_code();
    OutputTransactionResult WriteResult =
        WriteFault ? OutputTransactionResult::IOFailure
                   : writeFileStagingLocked();
    if (WriteResult != OutputTransactionResult::Success) {
      OutputTransactionResult AbortResult = abortLocked();
      if (AbortResult != OutputTransactionResult::Success)
        return OutputTransactionResult::FailedPartial;
      return WriteResult;
    }
    std::error_code SyncError =
        InjectFileFault
            ? InjectFileFault(OutputFileOperation::SyncStaging)
            : std::error_code();
    if (!SyncError)
      SyncError =
          output_platform::syncFileDescriptor(TemporaryFile->FD);
    if (SyncError) {
      OutputTransactionResult AbortResult = abortLocked();
      if (AbortResult != OutputTransactionResult::Success)
        return OutputTransactionResult::FailedPartial;
      return OutputTransactionResult::IOFailure;
    }
  }
  Digest = SHA256::hash(Staging);
  State = OutputTransactionState::Finished;
  return OutputTransactionResult::Success;
}

OutputTransactionResult OutputTransaction::abortLocked() {
  if (State == OutputTransactionState::Aborted)
    return OutputTransactionResult::Success;
  if (State == OutputTransactionState::Committed)
    return OutputTransactionResult::InvalidState;
  if (State == OutputTransactionState::FailedPartial)
    return OutputTransactionResult::FailedPartial;

  if (TemporaryFile) {
    std::error_code DiscardFault =
        InjectFileFault
            ? InjectFileFault(OutputFileOperation::DiscardStaging)
            : std::error_code();
    Error DiscardError = TemporaryFile->discard();
    TemporaryFile.reset();
    if (DiscardFault || DiscardError) {
      consumeError(std::move(DiscardError));
      State = OutputTransactionState::FailedPartial;
      PathLease.reset();
      return OutputTransactionResult::FailedPartial;
    }
  }
  Staging.clear();
  State = OutputTransactionState::Aborted;
  PathLease.reset();
  return OutputTransactionResult::Success;
}

OutputTransactionResult OutputTransaction::abort() {
  std::lock_guard<std::mutex> Lock(Mutex);
  return abortLocked();
}

Expected<OutputTransactionSummary> OutputTransaction::commit() {
  std::lock_guard<std::mutex> Lock(Mutex);
  if (State == OutputTransactionState::Committed)
    return summaryLocked();
  if (State != OutputTransactionState::Finished)
    return createStringError(inconvertibleErrorCode(),
                             "only a finished output can be committed");

  if (Kind == OutputDestinationKind::Memory) {
    PublishedMemory = Staging;
  } else if (Kind == OutputDestinationKind::File) {
    if (!TemporaryFile)
      return createStringError(inconvertibleErrorCode(),
                               "file output has no temporary file");
    std::error_code PublishFault =
        InjectFileFault
            ? InjectFileFault(OutputFileOperation::Publish)
            : std::error_code();
    if (PublishFault) {
      Error PublishError = errorCodeToError(PublishFault);
      if (abortLocked() != OutputTransactionResult::Success)
        return joinErrors(
            std::move(PublishError),
            createStringError(inconvertibleErrorCode(),
                              "failed to discard unpublished output"));
      return std::move(PublishError);
    }
    Error KeepError = TemporaryFile->keep(Destination);
    const bool Published = TemporaryFile->TmpName.empty();
    TemporaryFile.reset();
    if (KeepError) {
      if (Published) {
        State = OutputTransactionState::Committed;
        PublicationGeneration = 1;
        DurabilityUnconfirmed = true;
      } else {
        State = OutputTransactionState::Aborted;
      }
      PathLease.reset();
      return std::move(KeepError);
    }
    PublicationGeneration = 1;
    State = OutputTransactionState::Committed;
    std::error_code DirectorySyncError =
        InjectFileFault
            ? InjectFileFault(OutputFileOperation::SyncDirectory)
            : std::error_code();
    if (!DirectorySyncError)
      DirectorySyncError =
          output_platform::syncParentDirectory(Destination);
    if (DirectorySyncError) {
      DurabilityUnconfirmed = true;
      PathLease.reset();
      if (DirectorySyncError ==
          std::make_error_code(std::errc::operation_not_supported))
        return summaryLocked();
      return errorCodeToError(DirectorySyncError);
    }
    Durable = true;
  } else {
    if (!CommitStream)
      return createStringError(inconvertibleErrorCode(),
                               "stream output has no commit callback");
    OutputTransactionResult StreamResult = CommitStream(Staging);
    if (StreamResult != OutputTransactionResult::Success) {
      State = OutputTransactionState::FailedPartial;
      return createStringError(inconvertibleErrorCode(),
                               "stream output commit failed");
    }
  }

  PublicationGeneration = 1;
  State = OutputTransactionState::Committed;
  PathLease.reset();
  return summaryLocked();
}

OutputTransactionSummary OutputTransaction::summaryLocked() const {
  OutputTransactionSummary Result;
  Result.Kind = Kind;
  Result.State = State;
  if (State == OutputTransactionState::Committed)
    Result.Flags |= OutputPublished;
  if (Durable)
    Result.Flags |= OutputDurable;
  if (DurabilityUnconfirmed)
    Result.Flags |= OutputDurabilityUnconfirmed;
  if (State == OutputTransactionState::FailedPartial)
    Result.Flags |= OutputMayBePartial | OutputRecoveryRequired;
  Result.Size =
      State == OutputTransactionState::Aborted ? 0 : Staging.size();
  Result.PublicationGeneration = PublicationGeneration;
  Result.Digest = Digest;
  return Result;
}

OutputTransactionSummary OutputTransaction::summary() const {
  std::lock_guard<std::mutex> Lock(Mutex);
  return summaryLocked();
}

std::optional<MemoryOutputSnapshot>
OutputTransaction::memorySnapshot() const {
  std::lock_guard<std::mutex> Lock(Mutex);
  if (Kind != OutputDestinationKind::Memory ||
      State != OutputTransactionState::Committed)
    return std::nullopt;
  return MemoryOutputSnapshot{PublicationGeneration, PublishedMemory};
}

} // namespace neverc
