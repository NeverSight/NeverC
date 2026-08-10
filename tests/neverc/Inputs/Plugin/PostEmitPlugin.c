#include "PostEmitPlugin.h"
#include <string.h>

static NevercStatus invalid_status(void) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = NEVERC_STATUS_INVALID_ARGUMENT;
  return Status;
}

static NevercStatus get_image(
    const NevercPhaseFrame *Frame, NevercArtifactHandle Artifact,
    NevercTestPostEmitTrace *Trace,
    NevercLinkPhaseImageInfo *OutPhase,
    NevercBinaryImageInfo *OutImage) {
  NevercStatus Status;
  memset(OutPhase, 0, sizeof(*OutPhase));
  OutPhase->Header.StructSize = sizeof(*OutPhase);
  OutPhase->Header.Major = NEVERC_LINK_PHASE_API_MAJOR;
  OutPhase->Header.Minor = NEVERC_LINK_PHASE_API_MINOR;
  Status = Trace->PhaseAPI->GetImage(
      Trace->PhaseAPI->Context, Frame, Artifact, OutPhase);
  if (!neverc_status_is_ok(Status))
    return Status;
  memset(OutImage, 0, sizeof(*OutImage));
  OutImage->Header.StructSize = sizeof(*OutImage);
  OutImage->Header.Major = NEVERC_LINK_API_MAJOR;
  OutImage->Header.Minor = NEVERC_LINK_API_MINOR;
  return OutPhase->Link->GetBinaryImageInfo(
      OutPhase->Link->Context, Frame->Task, OutPhase->Image, OutImage);
}

static void cache_image(const NevercPhaseFrame *Frame,
                        const NevercLinkPhaseImageInfo *Phase,
                        const NevercBinaryImageInfo *Image,
                        NevercTestPostEmitTrace *Trace) {
  Trace->CachedLink = Phase->Link;
  Trace->CachedBinary = Image->Binary;
  Trace->CachedTask = Frame->Task;
  Trace->CachedImage = Phase->Image;
  Trace->CachedBuilder = Image->Builder;
  Trace->CachedImageSize = Image->Size;
}

static void record_mutation_statuses(
    const NevercMutableBinaryAPI *Binary, NevercTaskHandle Task,
    NevercMutableBinaryBuilderHandle Builder, uint64_t Size,
    NevercStatusCode OutStatuses[6]) {
  NevercByteView Empty = {0, 0};
  OutStatuses[0] =
      Binary->Reserve(Binary->Context, Task, Builder, 0).Code;
  OutStatuses[1] =
      Binary->Write(Binary->Context, Task, Builder, Empty).Code;
  OutStatuses[2] =
      Binary->WriteAt(Binary->Context, Task, Builder, 0, Empty).Code;
  OutStatuses[3] =
      Binary->Insert(Binary->Context, Task, Builder, 0, Empty).Code;
  OutStatuses[4] =
      Binary->Append(Binary->Context, Task, Builder, Empty).Code;
  OutStatuses[5] = Binary->Resize(Binary->Context, Task, Builder, Size).Code;
}

NevercStatus NEVERC_CALL neverc_test_post_emit_interceptor(
    const NevercPhaseFrame *Frame,
    NevercPhaseContinuation *Continuation,
    NevercPhaseResult *OutResult, void *UserData) {
  NevercTestPostEmitTrace *Trace =
      (NevercTestPostEmitTrace *)UserData;
  NevercLinkPhaseImageInfo Phase;
  NevercBinaryImageInfo Image;
  NevercPhaseResult Downstream;
  NevercStatus Status;
  NevercByteView Patch;
  if (!Frame || !Continuation || !OutResult || !Trace ||
      !Trace->PhaseAPI)
    return invalid_status();
  Status = get_image(Frame, Frame->Input, Trace, &Phase, &Image);
  if (!neverc_status_is_ok(Status))
    return Status;
  cache_image(Frame, &Phase, &Image, Trace);
  ++Trace->Calls;
  Trace->ObservedSize = Image.Size;
  Patch.Data = &Trace->PatchValue;
  Patch.Length = 1;
  if (Trace->AppendByte)
    Status = Image.Binary->Append(
        Image.Binary->Context, Frame->Task, Image.Builder, Patch);
  else
    Status = Image.Binary->WriteAt(
        Image.Binary->Context, Frame->Task, Image.Builder,
        Trace->PatchOffset, Patch);
  Trace->MutationStatus = Status.Code;
  if (!neverc_status_is_ok(Status))
    return Status;

  memset(&Downstream, 0, sizeof(Downstream));
  Downstream.Header.StructSize = sizeof(Downstream);
  Downstream.Header.Major = NEVERC_PLUGIN_ABI_MAJOR;
  Downstream.Header.Minor = NEVERC_PLUGIN_ABI_MINOR;
  Status = Continuation->InvokeNext(
      Continuation, Frame, &Downstream);
  if (!neverc_status_is_ok(Status))
    return Status;
  memset(OutResult, 0, sizeof(*OutResult));
  OutResult->Header.StructSize = sizeof(*OutResult);
  OutResult->Header.Major = NEVERC_PLUGIN_ABI_MAJOR;
  OutResult->Header.Minor = NEVERC_PLUGIN_ABI_MINOR;
  OutResult->Action = NEVERC_PHASE_CONTINUE;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL neverc_test_after_commit_observer(
    const NevercPhaseFrame *Frame, NevercObserverPoint Point,
    void *UserData) {
  NevercTestPostEmitTrace *Trace =
      (NevercTestPostEmitTrace *)UserData;
  NevercLinkPhaseImageInfo Phase;
  NevercBinaryImageInfo Image;
  NevercStatus Status;
  NevercByteView Patch;
  if (!Frame || !Trace || !Trace->PhaseAPI ||
      Point != NEVERC_OBSERVER_AFTER)
    return invalid_status();
  Status = get_image(Frame, Frame->Input, Trace, &Phase, &Image);
  if (!neverc_status_is_ok(Status))
    return Status;
  ++Trace->AfterCommitCalls;
  Patch.Data = &Trace->PatchValue;
  Patch.Length = 1;
  Status = Image.Binary->WriteAt(
      Image.Binary->Context, Frame->Task, Image.Builder, 0, Patch);
  Trace->AfterCommitWriteStatus = Status.Code;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL neverc_test_post_emit_observer(
    const NevercPhaseFrame *Frame, NevercObserverPoint Point,
    void *UserData) {
  NevercTestPostEmitTrace *Trace =
      (NevercTestPostEmitTrace *)UserData;
  NevercLinkPhaseImageInfo Phase;
  NevercBinaryImageInfo Image;
  NevercStatus Status;
  if (!Frame || !Trace || !Trace->PhaseAPI ||
      Point != NEVERC_OBSERVER_BEFORE)
    return invalid_status();
  Status = get_image(Frame, Frame->Input, Trace, &Phase, &Image);
  if (!neverc_status_is_ok(Status))
    return Status;
  ++Trace->ObserverCalls;
  cache_image(Frame, &Phase, &Image, Trace);
  record_mutation_statuses(Image.Binary, Frame->Task, Image.Builder,
                           Image.Size, Trace->ObserverMutationStatuses);
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL neverc_test_cached_post_emit_capability_observer(
    const NevercPhaseFrame *Frame, NevercObserverPoint Point,
    void *UserData) {
  NevercTestPostEmitTrace *Trace =
      (NevercTestPostEmitTrace *)UserData;
  NevercBinaryImageInfo Image;
  NevercStatus Status;
  if (!Frame || !Trace || !Trace->CachedLink ||
      Point != NEVERC_OBSERVER_BEFORE)
    return invalid_status();
  memset(&Image, 0, sizeof(Image));
  Image.Header.StructSize = sizeof(Image);
  Image.Header.Major = NEVERC_LINK_API_MAJOR;
  Image.Header.Minor = NEVERC_LINK_API_MINOR;
  Status = Trace->CachedLink->GetBinaryImageInfo(
      Trace->CachedLink->Context, Frame->Task, Trace->CachedImage, &Image);
  Trace->CachedLinkReadStatus = Status.Code;
  if (!neverc_status_is_ok(Status))
    return Status;
  ++Trace->CachedCapabilityObserverCalls;
  record_mutation_statuses(
      Image.Binary, Frame->Task, Image.Builder, Image.Size,
      Trace->CachedCapabilityMutationStatuses);
  return neverc_status_ok();
}
