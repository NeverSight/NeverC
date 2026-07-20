#include "GCICFPlugin.h"
#include <string.h>

static NevercStatus invalid_status(void) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = NEVERC_STATUS_INVALID_ARGUMENT;
  return Status;
}

static int name_is(NevercStringView Name, const char *Expected) {
  size_t Length = strlen(Expected);
  return Name.Length == Length && Name.Data &&
         memcmp(Name.Data, Expected, Length) == 0;
}

NevercStatus NEVERC_CALL neverc_test_gc_icf_interceptor(
    const NevercPhaseFrame *Frame,
    NevercPhaseContinuation *Continuation,
    NevercPhaseResult *OutResult, void *UserData) {
  NevercTestGCICFTrace *Trace = (NevercTestGCICFTrace *)UserData;
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
    NevercLinkAtomInfo Atoms[16];
    NevercLinkEntityPage Page;
    NevercLinkAtomHandle Subject = {0, 0};
    NevercLinkAtomHandle Leader = {0, 0};
    NevercLinkMutationHandle Mutation = {0, 0};
    uint64_t Index;
    const char *SubjectName = 0;

    if (Trace->Operation == NEVERC_TEST_GC_KEEP_DEAD)
      SubjectName = "dead";
    else if (Trace->Operation == NEVERC_TEST_GC_DROP_ROOT)
      SubjectName = "root";
    else if (Trace->Operation == NEVERC_TEST_ICF_PREVENT_FOLD)
      SubjectName = "candidate";
    else if (Trace->Operation == NEVERC_TEST_ICF_INVALID_FOLD)
      SubjectName = "address-significant";
    else
      return invalid_status();

    memset(&GraphInfo, 0, sizeof(GraphInfo));
    GraphInfo.Header.StructSize = sizeof(GraphInfo);
    GraphInfo.Header.Major = NEVERC_LINK_PHASE_API_MAJOR;
    GraphInfo.Header.Minor = NEVERC_LINK_PHASE_API_MINOR;
    Status = Trace->PhaseAPI->GetGraph(
        Trace->PhaseAPI->Context, Frame, Downstream.Output, &GraphInfo);
    if (!neverc_status_is_ok(Status))
      return Status;

    memset(Atoms, 0, sizeof(Atoms));
    for (Index = 0; Index != 16; ++Index) {
      Atoms[Index].Header.StructSize = sizeof(Atoms[Index]);
      Atoms[Index].Header.Major = NEVERC_LINK_API_MAJOR;
      Atoms[Index].Header.Minor = NEVERC_LINK_API_MINOR;
    }
    memset(&Page, 0, sizeof(Page));
    Page.Header.StructSize = sizeof(Page);
    Page.Header.Major = NEVERC_LINK_API_MAJOR;
    Page.Header.Minor = NEVERC_LINK_API_MINOR;
    Page.Data = Atoms;
    Page.ElementCapacity = 16;
    Page.ElementStride = sizeof(Atoms[0]);
    Status = GraphInfo.Link->GetAtomPage(
        GraphInfo.Link->Context, Frame->Task, GraphInfo.Graph, 0, &Page);
    if (!neverc_status_is_ok(Status))
      return Status;
    for (Index = 0; Index != Page.OutCount; ++Index) {
      if (name_is(Atoms[Index].Name, SubjectName))
        Subject = Atoms[Index].Atom;
      if (name_is(Atoms[Index].Name, "leader"))
        Leader = Atoms[Index].Atom;
    }
    if (neverc_handle_is_null(Subject))
      return invalid_status();

    Status = GraphInfo.Link->BeginMutation(
        GraphInfo.Link->Context, Frame->Task, GraphInfo.Graph, &Mutation);
    if (!neverc_status_is_ok(Status))
      return Status;
    if (Trace->Operation == NEVERC_TEST_GC_KEEP_DEAD ||
        Trace->Operation == NEVERC_TEST_GC_DROP_ROOT) {
      Status = GraphInfo.Link->SetAtomLive(
          GraphInfo.Link->Context, Frame->Task, Mutation, Subject,
          Trace->Operation == NEVERC_TEST_GC_KEEP_DEAD
              ? NEVERC_TRUE
              : NEVERC_FALSE);
    } else {
      if (Trace->Operation == NEVERC_TEST_ICF_INVALID_FOLD &&
          neverc_handle_is_null(Leader)) {
        (void)GraphInfo.Link->AbandonMutation(
            GraphInfo.Link->Context, Frame->Task, Mutation);
        return invalid_status();
      }
      Status = GraphInfo.Link->SetFoldLeader(
          GraphInfo.Link->Context, Frame->Task, Mutation, Subject,
          Trace->Operation == NEVERC_TEST_ICF_INVALID_FOLD
              ? Leader
              : (NevercLinkAtomHandle){0, 0});
    }
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
