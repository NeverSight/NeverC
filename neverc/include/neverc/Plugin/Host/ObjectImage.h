#ifndef NEVERC_PLUGIN_HOST_OBJECTIMAGE_H
#define NEVERC_PLUGIN_HOST_OBJECTIMAGE_H

#include "neverc/Plugin/Host/PluginIOBridge.h"
#include "neverc/Plugin/PluginObject.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace neverc::plugin {

class MutableBinaryBuilder;
class PluginPhaseExecutor;
class PluginTaskContext;

enum class PluginObjectImageState : uint8_t {
  Candidate,
  Verified,
  Committed,
  Aborted,
  FailedPartial,
};

/// A custom committer must report the terminal output state even when a late
/// durability or journal operation fails after publication. Failure is
/// returned to the caller, while Summary keeps the object-image state aligned
/// with the files that are actually visible.
struct PluginObjectImageCommitResult {
  NevercOutputSummary Summary{};
  llvm::Error Failure = llvm::Error::success();
};

using PluginObjectImageCommitter =
    std::function<PluginObjectImageCommitResult(llvm::ArrayRef<uint8_t>)>;

class PluginObjectImage {
public:
  static llvm::Expected<std::unique_ptr<PluginObjectImage>>
  create(PluginTaskContext &Task, NevercObjectFormatID FormatID,
         NevercTargetID TargetID, uint64_t GraphGeneration,
         NevercOutputSeal Seal, llvm::StringRef Provenance = {},
         std::optional<NevercObjectLayoutProofInfo> LayoutReport =
             std::nullopt);
  static llvm::Expected<std::unique_ptr<PluginObjectImage>>
  createPending(PluginTaskContext &Task,
                NevercObjectFormatID FormatID,
                NevercTargetID TargetID,
                uint64_t GraphGeneration,
                std::unique_ptr<MutableBinaryBuilder> Builder,
                llvm::StringRef Provenance = {},
                std::optional<NevercObjectLayoutProofInfo> LayoutReport =
                    std::nullopt);

  ~PluginObjectImage();

  PluginObjectImage(const PluginObjectImage &) = delete;
  PluginObjectImage &operator=(const PluginObjectImage &) = delete;

  NevercObjectImageHandle handle() const { return Handle; }
  PluginObjectImageState state() const { return State; }
  NevercObjectFormatID formatID() const { return FormatID; }
  NevercTargetID targetID() const { return TargetID; }
  uint64_t graphGeneration() const { return GraphGeneration; }
  const NevercOutputSeal &seal() const { return Seal; }
  llvm::StringRef provenance() const { return Provenance; }
  const std::optional<NevercObjectLayoutProofInfo> &layoutReport() const {
    return LayoutReport;
  }
  const NevercMutableBinaryAPI *readOnlyBinaryAPI() const;
  const NevercMutableBinaryAPI *
  capabilityBinaryAPI(const PluginPhaseExecutor &Executor, uint64_t Token);
  NevercMutableBinaryBuilderHandle binaryBuilder() const;

  llvm::Expected<NevercOutputSummary> outputSummary() const;
  llvm::Expected<llvm::ArrayRef<uint8_t>> pendingBytes() const;
  llvm::Error setCommitter(PluginObjectImageCommitter Committer);
  llvm::Error finish();
  llvm::Error verify();
  llvm::Expected<NevercOutputSummary> commit();
  llvm::Error abort();

private:
  PluginObjectImage(PluginTaskContext &Task,
                    NevercObjectFormatID FormatID,
                    NevercTargetID TargetID, uint64_t GraphGeneration,
                    NevercOutputSeal Seal, llvm::StringRef Provenance,
                    std::optional<NevercObjectLayoutProofInfo> LayoutReport);
  PluginObjectImage(PluginTaskContext &Task,
                    NevercObjectFormatID FormatID,
                    NevercTargetID TargetID,
                    uint64_t GraphGeneration,
                    std::unique_ptr<MutableBinaryBuilder> Builder,
                    llvm::StringRef Provenance,
                    std::optional<NevercObjectLayoutProofInfo> LayoutReport);

  PluginTaskContext &Task;
  NevercObjectFormatID FormatID{};
  NevercTargetID TargetID{};
  uint64_t GraphGeneration = 0;
  NevercOutputSeal Seal{};
  std::string Provenance;
  std::optional<NevercObjectLayoutProofInfo> LayoutReport;
  std::unique_ptr<MutableBinaryBuilder> Builder;
  PluginObjectImageCommitter Committer;
  std::vector<uint8_t> CommitBytes;
  std::optional<NevercOutputSummary> CommittedSummary;
  NevercObjectImageHandle Handle{};
  PluginObjectImageState State = PluginObjectImageState::Candidate;
};

} // namespace neverc::plugin

#endif
