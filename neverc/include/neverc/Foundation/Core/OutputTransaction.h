#ifndef NEVERC_FOUNDATION_CORE_OUTPUTTRANSACTION_H
#define NEVERC_FOUNDATION_CORE_OUTPUTTRANSACTION_H

#include "neverc/Foundation/Core/OutputCoordinator.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace neverc {

enum class OutputDestinationKind : uint8_t {
  Memory,
  File,
  Stream,
};

enum class OutputTransactionState : uint8_t {
  Open,
  Finished,
  Committed,
  Aborted,
  FailedPartial,
};

enum class OutputTransactionResult : uint8_t {
  Success,
  InvalidState,
  InvalidArgument,
  ResourceExhausted,
  IOFailure,
  FailedPartial,
};

enum class OutputFileOperation : uint8_t {
  WriteStaging,
  SyncStaging,
  Publish,
  SyncDirectory,
  DiscardStaging,
};

enum OutputPublicationFlag : uint64_t {
  OutputPublished = UINT64_C(1),
  OutputDurable = UINT64_C(2),
  OutputMayBePartial = UINT64_C(4),
  OutputRecoveryRequired = UINT64_C(8),
  OutputDurabilityUnconfirmed = UINT64_C(16),
};

struct OutputTransactionSummary {
  OutputDestinationKind Kind = OutputDestinationKind::Memory;
  OutputTransactionState State = OutputTransactionState::Open;
  uint64_t Flags = 0;
  uint64_t Size = 0;
  uint64_t PublicationGeneration = 0;
  std::array<uint8_t, 32> Digest{};
};

struct MemoryOutputSnapshot {
  uint64_t Generation = 0;
  std::vector<uint8_t> Bytes;
};

class OutputTransaction {
public:
  using StreamCommitCallback =
      std::function<OutputTransactionResult(llvm::ArrayRef<uint8_t>)>;
  using FileFaultInjector =
      std::function<std::error_code(OutputFileOperation)>;

  static std::shared_ptr<OutputTransaction>
  createMemory(llvm::StringRef LogicalName, uint64_t SizeBudget);

  static llvm::Expected<std::shared_ptr<OutputTransaction>>
  createFile(OutputCoordinator &Coordinator, llvm::StringRef FinalPath,
             uint64_t SizeBudget,
             OutputCoordinator::CancellationCheck IsCancelled = {},
             FileFaultInjector InjectFault = {},
             OutputLeaseOwner LeaseOwner = {});

  static std::shared_ptr<OutputTransaction>
  createStream(llvm::StringRef LogicalName, uint64_t SizeBudget,
               StreamCommitCallback Commit);

  ~OutputTransaction();

  OutputTransaction(const OutputTransaction &) = delete;
  OutputTransaction &operator=(const OutputTransaction &) = delete;

  OutputTransactionResult write(llvm::ArrayRef<uint8_t> Bytes);
  OutputTransactionResult writeAt(uint64_t Offset,
                                  llvm::ArrayRef<uint8_t> Bytes);
  OutputTransactionResult tell(uint64_t &OutPosition) const;
  OutputTransactionResult truncate(uint64_t Size);
  OutputTransactionResult setMetadata(llvm::StringRef Key,
                                      llvm::StringRef Value);
  OutputTransactionResult finish();
  OutputTransactionResult abort();
  llvm::Expected<OutputTransactionSummary> commit();

  OutputTransactionSummary summary() const;
  std::optional<MemoryOutputSnapshot> memorySnapshot() const;
  OutputDestinationKind kind() const { return Kind; }
  llvm::StringRef destination() const { return Destination; }

private:
  OutputTransaction(OutputDestinationKind Kind, std::string Destination,
                    uint64_t SizeBudget);

  OutputTransactionResult writeFileStagingLocked();
  OutputTransactionResult abortLocked();
  OutputTransactionSummary summaryLocked() const;

  OutputDestinationKind Kind;
  std::string Destination;
  uint64_t SizeBudget = 0;
  mutable std::mutex Mutex;
  std::vector<uint8_t> Staging;
  std::vector<uint8_t> PublishedMemory;
  std::map<std::string, std::string> Metadata;
  std::array<uint8_t, 32> Digest{};
  OutputTransactionState State = OutputTransactionState::Open;
  uint64_t PublicationGeneration = 0;
  bool Durable = false;
  bool DurabilityUnconfirmed = false;
  std::optional<OutputPathLease> PathLease;
  std::optional<llvm::sys::fs::TempFile> TemporaryFile;
  FileFaultInjector InjectFileFault;
  StreamCommitCallback CommitStream;
};

} // namespace neverc

#endif
