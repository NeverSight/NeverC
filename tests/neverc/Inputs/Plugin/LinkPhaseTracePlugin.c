#include "LinkPhaseTracePlugin.h"
#include <string.h>

static void record_event(NevercTestLinkPhaseTrace *Trace, char Event) {
  if (Trace && Trace->EventCount < sizeof(Trace->Events))
    Trace->Events[Trace->EventCount++] = Event;
}

static NevercStatus invalid_status(void) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = NEVERC_STATUS_INVALID_ARGUMENT;
  return Status;
}

NevercStatus NEVERC_CALL neverc_test_link_observer(
    const NevercPhaseFrame *Frame, NevercObserverPoint Point,
    void *UserData) {
  NevercTestLinkPhaseTrace *Trace =
      (NevercTestLinkPhaseTrace *)UserData;
  if (!Frame || !Trace)
    return invalid_status();
  record_event(Trace, Point == NEVERC_OBSERVER_BEFORE ? 'B' : 'A');
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL neverc_test_link_interceptor(
    const NevercPhaseFrame *Frame,
    NevercPhaseContinuation *Continuation,
    NevercPhaseResult *OutResult, void *UserData) {
  NevercTestLinkPhaseTrace *Trace =
      (NevercTestLinkPhaseTrace *)UserData;
  NevercPhaseResult Downstream;
  NevercStatus Status;
  if (!Frame || !Continuation || !OutResult || !Trace ||
      !Trace->PhaseAPI)
    return invalid_status();

  record_event(Trace, 'I');
  memset(&Downstream, 0, sizeof(Downstream));
  Downstream.Header.StructSize = sizeof(Downstream);
  Downstream.Header.Major = NEVERC_PLUGIN_ABI_MAJOR;
  Downstream.Header.Minor = NEVERC_PLUGIN_ABI_MINOR;
  Status = Continuation->InvokeNext(Continuation, Frame, &Downstream);
  if (!neverc_status_is_ok(Status))
    return Status;
  record_event(Trace, 'N');

  if (Trace->Mutations == 0) {
    NevercLinkPhaseGraphInfo GraphInfo;
    NevercLinkSymbolInfo Symbol;
    NevercLinkEntityPage Page;
    NevercLinkMutationHandle Mutation = {0, 0};
    memset(&GraphInfo, 0, sizeof(GraphInfo));
    GraphInfo.Header.StructSize = sizeof(GraphInfo);
    GraphInfo.Header.Major = NEVERC_LINK_PHASE_API_MAJOR;
    GraphInfo.Header.Minor = NEVERC_LINK_PHASE_API_MINOR;
    Status = Trace->PhaseAPI->GetGraph(
        Trace->PhaseAPI->Context, Frame, Downstream.Output, &GraphInfo);
    if (!neverc_status_is_ok(Status))
      return Status;

    memset(&Symbol, 0, sizeof(Symbol));
    Symbol.Header.StructSize = sizeof(Symbol);
    Symbol.Header.Major = NEVERC_LINK_API_MAJOR;
    Symbol.Header.Minor = NEVERC_LINK_API_MINOR;
    memset(&Page, 0, sizeof(Page));
    Page.Header.StructSize = sizeof(Page);
    Page.Header.Major = NEVERC_LINK_API_MAJOR;
    Page.Header.Minor = NEVERC_LINK_API_MINOR;
    Page.Data = &Symbol;
    Page.ElementCapacity = 1;
    Page.ElementStride = sizeof(Symbol);
    Status = GraphInfo.Link->GetSymbolPage(
        GraphInfo.Link->Context, Frame->Task, GraphInfo.Graph, 0, &Page);
    if (!neverc_status_is_ok(Status))
      return Status;
    if (Page.OutCount != 1)
      return invalid_status();

    Status = GraphInfo.Link->BeginMutation(
        GraphInfo.Link->Context, Frame->Task, GraphInfo.Graph, &Mutation);
    if (!neverc_status_is_ok(Status))
      return Status;
    Status = GraphInfo.Link->SetSymbolRoot(
        GraphInfo.Link->Context, Frame->Task, Mutation, Symbol.Symbol,
        NEVERC_FALSE);
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
    record_event(Trace, 'M');
  }

  memset(OutResult, 0, sizeof(*OutResult));
  OutResult->Header.StructSize = sizeof(*OutResult);
  OutResult->Header.Major = NEVERC_PLUGIN_ABI_MAJOR;
  OutResult->Header.Minor = NEVERC_PLUGIN_ABI_MINOR;
  OutResult->Action = NEVERC_PHASE_CONTINUE;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL neverc_test_link_provider(
    const NevercPhaseFrame *Frame, NevercPhaseResult *OutResult,
    void *UserData) {
  NevercTestLinkPhaseTrace *Trace =
      (NevercTestLinkPhaseTrace *)UserData;
  NevercLinkPhaseGraphInfo GraphInfo;
  NevercStatus Status;
  if (!Frame || !OutResult || !Trace || !Trace->PhaseAPI)
    return invalid_status();
  record_event(Trace, 'P');
  memset(&GraphInfo, 0, sizeof(GraphInfo));
  GraphInfo.Header.StructSize = sizeof(GraphInfo);
  GraphInfo.Header.Major = NEVERC_LINK_PHASE_API_MAJOR;
  GraphInfo.Header.Minor = NEVERC_LINK_PHASE_API_MINOR;
  Status = Trace->PhaseAPI->GetGraph(
      Trace->PhaseAPI->Context, Frame, Frame->Input, &GraphInfo);
  if (!neverc_status_is_ok(Status))
    return Status;
  memset(OutResult, 0, sizeof(*OutResult));
  OutResult->Header.StructSize = sizeof(*OutResult);
  OutResult->Header.Major = NEVERC_PLUGIN_ABI_MAJOR;
  OutResult->Header.Minor = NEVERC_PLUGIN_ABI_MINOR;
  Status = Trace->PhaseAPI->PublishGraph(
      Trace->PhaseAPI->Context, Frame, GraphInfo.Graph, &OutResult->Output);
  if (!neverc_status_is_ok(Status))
    return Status;
  OutResult->Action = NEVERC_PHASE_REPLACE;
  return neverc_status_ok();
}
