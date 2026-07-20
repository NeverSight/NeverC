#include "SyntheticLinkPlugin.h"
#include <string.h>

static NevercStatus invalid_status(void) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = NEVERC_STATUS_INVALID_ARGUMENT;
  return Status;
}

NevercStatus NEVERC_CALL neverc_test_synthetic_link_interceptor(
    const NevercPhaseFrame *Frame,
    NevercPhaseContinuation *Continuation,
    NevercPhaseResult *OutResult, void *UserData) {
  NevercTestSyntheticLinkTrace *Trace =
      (NevercTestSyntheticLinkTrace *)UserData;
  NevercPhaseResult Downstream;
  NevercStatus Status;
  if (!Frame || !Continuation || !OutResult || !Trace ||
      !Trace->PhaseAPI)
    return invalid_status();
  memset(&Downstream, 0, sizeof(Downstream));
  Downstream.Header.StructSize = sizeof(Downstream);
  Downstream.Header.Major = NEVERC_PLUGIN_ABI_MAJOR;
  Downstream.Header.Minor = NEVERC_PLUGIN_ABI_MINOR;
  Status = Continuation->InvokeNext(Continuation, Frame, &Downstream);
  if (!neverc_status_is_ok(Status))
    return Status;

  if (Trace->Mutations == 0) {
    NevercLinkPhaseGraphInfo GraphInfo;
    NevercLinkSectionInfo Section;
    NevercLinkAtomInfo Atom;
    NevercLinkEntityPage Page;
    NevercLinkMutationHandle Mutation = {0, 0};
    NevercLinkSyntheticHandle Synthetic = {0, 0};
    NevercLinkSyntheticInfo Descriptor;

    memset(&GraphInfo, 0, sizeof(GraphInfo));
    GraphInfo.Header.StructSize = sizeof(GraphInfo);
    GraphInfo.Header.Major = NEVERC_LINK_PHASE_API_MAJOR;
    GraphInfo.Header.Minor = NEVERC_LINK_PHASE_API_MINOR;
    Status = Trace->PhaseAPI->GetGraph(
        Trace->PhaseAPI->Context, Frame, Downstream.Output, &GraphInfo);
    if (!neverc_status_is_ok(Status))
      return Status;

    memset(&Section, 0, sizeof(Section));
    Section.Header.StructSize = sizeof(Section);
    Section.Header.Major = NEVERC_LINK_API_MAJOR;
    Section.Header.Minor = NEVERC_LINK_API_MINOR;
    memset(&Page, 0, sizeof(Page));
    Page.Header.StructSize = sizeof(Page);
    Page.Header.Major = NEVERC_LINK_API_MAJOR;
    Page.Header.Minor = NEVERC_LINK_API_MINOR;
    Page.Data = &Section;
    Page.ElementCapacity = 1;
    Page.ElementStride = sizeof(Section);
    Status = GraphInfo.Link->GetSectionPage(
        GraphInfo.Link->Context, Frame->Task, GraphInfo.Graph, 0, &Page);
    if (!neverc_status_is_ok(Status) || Page.OutCount != 1)
      return neverc_status_is_ok(Status) ? invalid_status() : Status;

    memset(&Atom, 0, sizeof(Atom));
    Atom.Header.StructSize = sizeof(Atom);
    Atom.Header.Major = NEVERC_LINK_API_MAJOR;
    Atom.Header.Minor = NEVERC_LINK_API_MINOR;
    memset(&Page, 0, sizeof(Page));
    Page.Header.StructSize = sizeof(Page);
    Page.Header.Major = NEVERC_LINK_API_MAJOR;
    Page.Header.Minor = NEVERC_LINK_API_MINOR;
    Page.Data = &Atom;
    Page.ElementCapacity = 1;
    Page.ElementStride = sizeof(Atom);
    Status = GraphInfo.Link->GetAtomPage(
        GraphInfo.Link->Context, Frame->Task, GraphInfo.Graph, 0, &Page);
    if (!neverc_status_is_ok(Status) || Page.OutCount != 1)
      return neverc_status_is_ok(Status) ? invalid_status() : Status;

    Status = GraphInfo.Link->BeginMutation(
        GraphInfo.Link->Context, Frame->Task, GraphInfo.Graph, &Mutation);
    if (!neverc_status_is_ok(Status))
      return Status;
    memset(&Descriptor, 0, sizeof(Descriptor));
    Descriptor.Header.StructSize = sizeof(Descriptor);
    Descriptor.Header.Major = NEVERC_LINK_API_MAJOR;
    Descriptor.Header.Minor = NEVERC_LINK_API_MINOR;
    if (Trace->MakeInvalid == NEVERC_FALSE)
      Descriptor.Role = (NevercStringView){"plugin-metadata", 15};
    Descriptor.Section = Section.Section;
    Descriptor.Atom = Atom.Atom;
    Status = GraphInfo.Link->CreateSynthetic(
        GraphInfo.Link->Context, Frame->Task, Mutation, &Descriptor,
        &Synthetic);
    if (!neverc_status_is_ok(Status)) {
      (void)GraphInfo.Link->AbandonMutation(
          GraphInfo.Link->Context, Frame->Task, Mutation);
      return Status;
    }
    Status = GraphInfo.Link->CommitMutation(
        GraphInfo.Link->Context, Frame->Task, Mutation);
    Trace->MutationStatus = Status.Code;
    if (!neverc_status_is_ok(Status))
      return Status;
    ++Trace->Mutations;
  }

  memset(OutResult, 0, sizeof(*OutResult));
  OutResult->Header.StructSize = sizeof(*OutResult);
  OutResult->Header.Major = NEVERC_PLUGIN_ABI_MAJOR;
  OutResult->Header.Minor = NEVERC_PLUGIN_ABI_MINOR;
  OutResult->Action = NEVERC_PHASE_CONTINUE;
  return neverc_status_ok();
}
