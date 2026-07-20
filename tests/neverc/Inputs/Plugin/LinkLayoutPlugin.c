#include "LinkLayoutPlugin.h"
#include <string.h>

static NevercStatus invalid_status(void) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = NEVERC_STATUS_INVALID_ARGUMENT;
  return Status;
}

NevercStatus NEVERC_CALL neverc_test_link_layout_interceptor(
    const NevercPhaseFrame *Frame,
    NevercPhaseContinuation *Continuation,
    NevercPhaseResult *OutResult, void *UserData) {
  NevercTestLinkLayoutTrace *Trace =
      (NevercTestLinkLayoutTrace *)UserData;
  NevercPhaseResult Downstream;
  NevercStatus Status;
  NevercLinkPhaseGraphInfo GraphInfo;
  NevercLinkProofInfo ProofInfo;
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
  memset(&GraphInfo, 0, sizeof(GraphInfo));
  GraphInfo.Header.StructSize = sizeof(GraphInfo);
  GraphInfo.Header.Major = NEVERC_LINK_PHASE_API_MAJOR;
  GraphInfo.Header.Minor = NEVERC_LINK_PHASE_API_MINOR;
  Status = Trace->PhaseAPI->GetGraph(
      Trace->PhaseAPI->Context, Frame, Downstream.Output, &GraphInfo);
  if (!neverc_status_is_ok(Status))
    return Status;
  if (neverc_handle_is_null(GraphInfo.Proof))
    return invalid_status();
  memset(&ProofInfo, 0, sizeof(ProofInfo));
  ProofInfo.Header.StructSize = sizeof(ProofInfo);
  ProofInfo.Header.Major = NEVERC_LINK_API_MAJOR;
  ProofInfo.Header.Minor = NEVERC_LINK_API_MINOR;
  Status = GraphInfo.Link->GetProofInfo(
      GraphInfo.Link->Context, Frame->Task, GraphInfo.Proof, &ProofInfo);
  if (!neverc_status_is_ok(Status))
    return Status;
  Trace->ProofSeen = NEVERC_TRUE;
  Trace->ObservedImageBase = ProofInfo.ImageBase;
  Trace->ObservedEntryAddress = ProofInfo.EntryAddress;

  if (Trace->Mutations == 0 &&
      (Trace->DesiredImageBase != 0 ||
       Trace->InvalidPageSize != NEVERC_FALSE)) {
    NevercLinkMutationHandle Mutation = {0, 0};
    NevercLinkConstraintHandle Constraint = {0, 0};
    NevercLinkConstraintInfo Descriptor;
    Status = GraphInfo.Link->BeginMutation(
        GraphInfo.Link->Context, Frame->Task, GraphInfo.Graph, &Mutation);
    if (!neverc_status_is_ok(Status))
      return Status;
    memset(&Descriptor, 0, sizeof(Descriptor));
    Descriptor.Header.StructSize = sizeof(Descriptor);
    Descriptor.Header.Major = NEVERC_LINK_API_MAJOR;
    Descriptor.Header.Minor = NEVERC_LINK_API_MINOR;
    if (Trace->InvalidPageSize != NEVERC_FALSE) {
      Descriptor.Kind = (NevercStringView){"page-size", 9};
      Descriptor.Value = 3;
    } else {
      Descriptor.Kind = (NevercStringView){"image-base", 10};
      Descriptor.Value = Trace->DesiredImageBase;
    }
    Descriptor.Required = NEVERC_TRUE;
    Status = GraphInfo.Link->CreateConstraint(
        GraphInfo.Link->Context, Frame->Task, Mutation, &Descriptor,
        &Constraint);
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
