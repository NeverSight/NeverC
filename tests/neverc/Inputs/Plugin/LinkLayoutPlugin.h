#ifndef NEVERC_TEST_LINK_LAYOUT_PLUGIN_H
#define NEVERC_TEST_LINK_LAYOUT_PLUGIN_H

#include "neverc/Plugin/PluginLink.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NevercTestLinkLayoutTrace {
  const NevercLinkPhaseAPI *PhaseAPI;
  uint64_t DesiredImageBase;
  NevercBool InvalidPageSize;
  uint32_t Mutations;
  NevercStatusCode MutationStatus;
  NevercBool ProofSeen;
  uint64_t ObservedImageBase;
  uint64_t ObservedEntryAddress;
} NevercTestLinkLayoutTrace;

NevercStatus NEVERC_CALL neverc_test_link_layout_interceptor(
    const NevercPhaseFrame *Frame,
    NevercPhaseContinuation *Continuation,
    NevercPhaseResult *OutResult, void *UserData);

#ifdef __cplusplus
}
#endif

#endif
