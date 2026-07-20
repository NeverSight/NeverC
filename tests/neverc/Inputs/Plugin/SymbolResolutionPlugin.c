#include "SymbolResolutionPlugin.h"
#include <string.h>

static NevercStatus invalid_status(void) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = NEVERC_STATUS_INVALID_ARGUMENT;
  return Status;
}

static int same_handle(NevercHandle Left, NevercHandle Right) {
  return Left.Owner == Right.Owner && Left.Value == Right.Value;
}

NevercStatus NEVERC_CALL neverc_test_symbol_resolution_interceptor(
    const NevercPhaseFrame *Frame,
    NevercPhaseContinuation *Continuation,
    NevercPhaseResult *OutResult, void *UserData) {
  NevercTestSymbolResolutionTrace *Trace =
      (NevercTestSymbolResolutionTrace *)UserData;
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
    NevercLinkSymbolInfo Symbols[8];
    NevercLinkEntityPage Page;
    NevercLinkSymbolHandle Weak = {0, 0};
    NevercLinkSymbolHandle Strong = {0, 0};
    NevercLinkMutationHandle Mutation = {0, 0};
    uint64_t Index;

    memset(&GraphInfo, 0, sizeof(GraphInfo));
    GraphInfo.Header.StructSize = sizeof(GraphInfo);
    GraphInfo.Header.Major = NEVERC_LINK_PHASE_API_MAJOR;
    GraphInfo.Header.Minor = NEVERC_LINK_PHASE_API_MINOR;
    Status = Trace->PhaseAPI->GetGraph(
        Trace->PhaseAPI->Context, Frame, Downstream.Output, &GraphInfo);
    if (!neverc_status_is_ok(Status))
      return Status;

    memset(Symbols, 0, sizeof(Symbols));
    for (Index = 0; Index != 8; ++Index) {
      Symbols[Index].Header.StructSize = sizeof(Symbols[Index]);
      Symbols[Index].Header.Major = NEVERC_LINK_API_MAJOR;
      Symbols[Index].Header.Minor = NEVERC_LINK_API_MINOR;
    }
    memset(&Page, 0, sizeof(Page));
    Page.Header.StructSize = sizeof(Page);
    Page.Header.Major = NEVERC_LINK_API_MAJOR;
    Page.Header.Minor = NEVERC_LINK_API_MINOR;
    Page.Data = Symbols;
    Page.ElementCapacity = 8;
    Page.ElementStride = sizeof(Symbols[0]);
    Status = GraphInfo.Link->GetSymbolPage(
        GraphInfo.Link->Context, Frame->Task, GraphInfo.Graph, 0, &Page);
    if (!neverc_status_is_ok(Status))
      return Status;
    for (Index = 0; Index != Page.OutCount; ++Index) {
      if (Symbols[Index].Binding == NEVERC_LINK_SYMBOL_BINDING_WEAK)
        Weak = Symbols[Index].Symbol;
      else if (Symbols[Index].Binding ==
               NEVERC_LINK_SYMBOL_BINDING_GLOBAL)
        Strong = Symbols[Index].Symbol;
    }
    if (neverc_handle_is_null(Weak) || neverc_handle_is_null(Strong))
      return invalid_status();

    Status = GraphInfo.Link->BeginMutation(
        GraphInfo.Link->Context, Frame->Task, GraphInfo.Graph, &Mutation);
    if (!neverc_status_is_ok(Status))
      return Status;
    for (Index = 0; Index != Page.OutCount; ++Index) {
      NevercLinkSymbolResolutionUpdate Update;
      memset(&Update, 0, sizeof(Update));
      Update.Header.StructSize = sizeof(Update);
      Update.Header.Major = NEVERC_LINK_API_MAJOR;
      Update.Header.Minor = NEVERC_LINK_API_MINOR;
      Update.Binding = Symbols[Index].Binding;
      Update.Visibility = Symbols[Index].Visibility;
      Update.Definition = Symbols[Index].Definition;
      Update.IsPrevailing =
          Trace->MakeInvalid != NEVERC_FALSE ||
                  same_handle(Symbols[Index].Symbol, Weak)
              ? NEVERC_TRUE
              : NEVERC_FALSE;
      Update.IsExported = Symbols[Index].IsExported;
      Status = GraphInfo.Link->SetSymbolResolution(
          GraphInfo.Link->Context, Frame->Task, Mutation,
          Symbols[Index].Symbol, &Update);
      if (!neverc_status_is_ok(Status))
        goto abandon;
    }

    Status = GraphInfo.Link->CommitMutation(
        GraphInfo.Link->Context, Frame->Task, Mutation);
    Trace->MutationStatus = Status.Code;
    if (!neverc_status_is_ok(Status))
      return Status;
    ++Trace->Mutations;
    goto mutation_done;

  abandon:
    (void)GraphInfo.Link->AbandonMutation(
        GraphInfo.Link->Context, Frame->Task, Mutation);
    Trace->MutationStatus = Status.Code;
    return Status;

  mutation_done:
    ;
  }

  memset(OutResult, 0, sizeof(*OutResult));
  OutResult->Header.StructSize = sizeof(*OutResult);
  OutResult->Header.Major = NEVERC_PLUGIN_ABI_MAJOR;
  OutResult->Header.Minor = NEVERC_PLUGIN_ABI_MINOR;
  OutResult->Action = NEVERC_PHASE_CONTINUE;
  return neverc_status_ok();
}
