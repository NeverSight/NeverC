#include "neverc/Plugin/Host/ObjectImage.h"
#include "neverc/Plugin/Host/MutableBinaryBuilder.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/Support/Errc.h"
#include <algorithm>
#include <cstring>

using namespace llvm;

namespace neverc::plugin {

PluginObjectImage::PluginObjectImage(
    PluginTaskContext &TaskValue, NevercObjectFormatID FormatIDValue,
    NevercTargetID TargetIDValue, uint64_t GraphGenerationValue,
    NevercOutputSeal SealValue, StringRef ProvenanceValue,
    std::optional<NevercObjectLayoutProofInfo> LayoutReportValue)
    : Task(TaskValue), FormatID(FormatIDValue), TargetID(TargetIDValue),
      GraphGeneration(GraphGenerationValue), Seal(SealValue),
      Provenance(ProvenanceValue.str()),
      LayoutReport(std::move(LayoutReportValue)) {}

PluginObjectImage::PluginObjectImage(
    PluginTaskContext &TaskValue, NevercObjectFormatID FormatIDValue,
    NevercTargetID TargetIDValue, uint64_t GraphGenerationValue,
    std::unique_ptr<MutableBinaryBuilder> BuilderValue,
    StringRef ProvenanceValue,
    std::optional<NevercObjectLayoutProofInfo> LayoutReportValue)
    : Task(TaskValue), FormatID(FormatIDValue), TargetID(TargetIDValue),
      GraphGeneration(GraphGenerationValue),
      Provenance(ProvenanceValue.str()),
      LayoutReport(std::move(LayoutReportValue)),
      Builder(std::move(BuilderValue)) {}

Expected<std::unique_ptr<PluginObjectImage>>
PluginObjectImage::create(PluginTaskContext &Task,
                          NevercObjectFormatID FormatID,
                          NevercTargetID TargetID,
                          uint64_t GraphGeneration,
                          NevercOutputSeal Seal,
                          StringRef Provenance,
                          std::optional<NevercObjectLayoutProofInfo>
                              LayoutReport) {
  if (neverc_handle_is_null(Seal.Handle))
    return createStringError(errc::invalid_argument,
                             "object image has no output seal");
  auto Result = std::unique_ptr<PluginObjectImage>(
      new PluginObjectImage(Task, FormatID, TargetID, GraphGeneration,
                            Seal, Provenance, std::move(LayoutReport)));
  auto Handle = Task.handles().create(
      PluginObjectImageHandleKind, Result.get());
  if (!Handle)
    return Handle.takeError();
  Result->Handle = *Handle;
  return Result;
}

Expected<std::unique_ptr<PluginObjectImage>>
PluginObjectImage::createPending(
    PluginTaskContext &Task, NevercObjectFormatID FormatID,
    NevercTargetID TargetID, uint64_t GraphGeneration,
    std::unique_ptr<MutableBinaryBuilder> Builder,
    StringRef Provenance,
    std::optional<NevercObjectLayoutProofInfo> LayoutReport) {
  if (!Builder)
    return createStringError(errc::invalid_argument,
                             "pending object image has no binary builder");
  auto Result = std::unique_ptr<PluginObjectImage>(
      new PluginObjectImage(Task, FormatID, TargetID, GraphGeneration,
                            std::move(Builder), Provenance,
                            std::move(LayoutReport)));
  auto Handle = Task.handles().create(
      PluginObjectImageHandleKind, Result.get());
  if (!Handle)
    return Handle.takeError();
  Result->Handle = *Handle;
  return Result;
}

PluginObjectImage::~PluginObjectImage() {
  if (State == PluginObjectImageState::Candidate ||
      State == PluginObjectImageState::Verified)
    consumeError(abort());
  if (!neverc_handle_is_null(Handle))
    (void)Task.handles().release(Handle, PluginObjectImageHandleKind);
}

const NevercMutableBinaryAPI *PluginObjectImage::binaryAPI() const {
  return Builder ? &Builder->api() : nullptr;
}

NevercMutableBinaryBuilderHandle
PluginObjectImage::binaryBuilder() const {
  return Builder ? Builder->handle()
                 : NevercMutableBinaryBuilderHandle{};
}

Expected<NevercOutputSummary>
PluginObjectImage::outputSummary() const {
  if (Builder)
    return Builder->summary();
  if (neverc_handle_is_null(Seal.Handle))
    return createStringError(errc::invalid_argument,
                             "object image has no output");
  auto Snapshot = inspectPluginOutputSeal(Task, Seal.Handle);
  if (!Snapshot)
    return Snapshot.takeError();
  NevercOutputSummary Summary{};
  Summary.Header = {sizeof(Summary), NEVERC_IO_API_MAJOR,
                    NEVERC_IO_API_MINOR, 0};
  Summary.State = Snapshot->State;
  Summary.Kind = Snapshot->Kind;
  Summary.Flags = Snapshot->Flags;
  Summary.Size = Snapshot->Size;
  Summary.PublicationGeneration =
      Snapshot->PublicationGeneration;
  std::copy(Snapshot->Digest.begin(), Snapshot->Digest.end(),
            Summary.Digest);
  return Summary;
}

Expected<ArrayRef<uint8_t>> PluginObjectImage::pendingBytes() const {
  if (State != PluginObjectImageState::Candidate || !Builder)
    return createStringError(
        errc::invalid_argument,
        "object image has no mutable bytes pending final verification");
  return Builder->bytes();
}

Error PluginObjectImage::finish() {
  if (!Builder)
    return neverc_handle_is_null(Seal.Handle)
               ? createStringError(errc::invalid_argument,
                                   "object image has no pending output")
               : Error::success();
  if (State != PluginObjectImageState::Candidate)
    return createStringError(errc::invalid_argument,
                             "only a candidate object image can finish");
  auto Finished = Builder->finish();
  if (!Finished)
    return Finished.takeError();
  Seal = *Finished;
  Builder.reset();
  return Error::success();
}

Error PluginObjectImage::verify() {
  if (State == PluginObjectImageState::Verified ||
      State == PluginObjectImageState::Committed)
    return Error::success();
  if (State != PluginObjectImageState::Candidate)
    return createStringError(errc::invalid_argument,
                             "object image is not a candidate");

  if (Builder || neverc_handle_is_null(Seal.Handle))
    return createStringError(
        errc::invalid_argument,
        "object image must finish before final verification");
  auto Snapshot = inspectPluginOutputSeal(Task, Seal.Handle);
  if (!Snapshot)
    return Snapshot.takeError();
  if (Snapshot->State == NEVERC_OUTPUT_FAILED_PARTIAL) {
    State = PluginObjectImageState::FailedPartial;
    return createStringError(errc::io_error,
                             "object image output is partially written");
  }
  if (Snapshot->State != NEVERC_OUTPUT_FINISHED)
    return createStringError(
        errc::invalid_argument,
        "object image must be finished but unpublished before verification");
  if (Snapshot->Kind != Seal.Kind || Snapshot->Size != Seal.Size ||
      !std::equal(Snapshot->Digest.begin(), Snapshot->Digest.end(),
                  std::begin(Seal.Digest)))
    return createStringError(errc::invalid_argument,
                             "object image seal metadata changed");
  if (Seal.Size == 0)
    return createStringError(errc::invalid_argument,
                             "object image cannot be empty");
  if (LayoutReport &&
      (LayoutReport->Header.StructSize <
           sizeof(NevercObjectLayoutProofInfo) ||
       LayoutReport->GraphGeneration != GraphGeneration ||
       LayoutReport->TargetID.High != TargetID.High ||
       LayoutReport->TargetID.Low != TargetID.Low ||
       LayoutReport->FormatID.High != FormatID.High ||
       LayoutReport->FormatID.Low != FormatID.Low))
    return createStringError(
        errc::invalid_argument,
        "object image layout report does not match its provenance");

  State = PluginObjectImageState::Verified;
  return Error::success();
}

Expected<NevercOutputSummary> PluginObjectImage::commit() {
  if (State == PluginObjectImageState::Candidate)
    return createStringError(
        errc::operation_not_permitted,
        "object image must pass final verification before commit");
  if (State == PluginObjectImageState::Aborted)
    return createStringError(errc::operation_not_permitted,
                             "object image was aborted");
  if (State == PluginObjectImageState::FailedPartial)
    return createStringError(errc::invalid_argument,
                             "partial object image cannot be committed");

  auto Result = hostCommitPluginOutput(Task, Seal.Handle);
  if (!Result) {
    Error CommitError = Result.takeError();
    auto Snapshot = inspectPluginOutputSeal(Task, Seal.Handle);
    if (Snapshot) {
      if (Snapshot->State == NEVERC_OUTPUT_COMMITTED)
        State = PluginObjectImageState::Committed;
      else if (Snapshot->State == NEVERC_OUTPUT_FAILED_PARTIAL)
        State = PluginObjectImageState::FailedPartial;
    } else {
      consumeError(Snapshot.takeError());
    }
    return std::move(CommitError);
  }
  if (Result->State != NEVERC_OUTPUT_COMMITTED)
    return createStringError(errc::io_error,
                             "object image commit did not publish output");
  State = PluginObjectImageState::Committed;
  return *Result;
}

Error PluginObjectImage::abort() {
  if (State == PluginObjectImageState::Aborted)
    return Error::success();
  if (State == PluginObjectImageState::Committed)
    return createStringError(errc::operation_not_permitted,
                             "committed object image cannot be aborted");
  if (State == PluginObjectImageState::FailedPartial)
    return createStringError(errc::invalid_argument,
                             "partial object image requires recovery");

  if (Builder) {
    NevercStatus Status = Builder->abort();
    Builder.reset();
    if (!neverc_status_is_ok(Status))
      return createStringError(
          errc::io_error,
          "pending object image output could not be aborted");
    State = PluginObjectImageState::Aborted;
    return Error::success();
  }
  if (neverc_handle_is_null(Seal.Handle)) {
    State = PluginObjectImageState::Aborted;
    return Error::success();
  }
  auto Result = hostAbortPluginOutput(Task, Seal.Handle);
  if (!Result)
    return Result.takeError();
  if (Result->State == NEVERC_OUTPUT_FAILED_PARTIAL) {
    State = PluginObjectImageState::FailedPartial;
    return createStringError(errc::io_error,
                             "object image abort left partial output");
  }
  if (Result->State != NEVERC_OUTPUT_ABORTED)
    return createStringError(errc::invalid_argument,
                             "object image output was not aborted");
  State = PluginObjectImageState::Aborted;
  return Error::success();
}

} // namespace neverc::plugin
