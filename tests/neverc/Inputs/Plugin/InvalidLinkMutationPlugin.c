#include "neverc/Plugin/PluginLink.h"

NevercStatus neverc_test_submit_invalid_link_mutation(
    const NevercLinkAPI *API, NevercTaskHandle Task,
    NevercLinkGraphHandle Graph, NevercLinkSymbolHandle Symbol) {
  NevercLinkMutationHandle Mutation = {0, 0};
  NevercLinkAtomHandle MissingAtom = {0, 0};
  NevercStatus Status =
      API->BeginMutation(API->Context, Task, Graph, &Mutation);
  if (!neverc_status_is_ok(Status))
    return Status;
  Status = API->RebindSymbol(API->Context, Task, Mutation, Symbol,
                             MissingAtom);
  if (!neverc_status_is_ok(Status)) {
    (void)API->AbandonMutation(API->Context, Task, Mutation);
    return Status;
  }
  return API->CommitMutation(API->Context, Task, Mutation);
}
