#ifndef NEVERC_TEST_LINKPHASETRACEPLUGIN_H
#define NEVERC_TEST_LINKPHASETRACEPLUGIN_H

#include "neverc/Plugin/PluginLink.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NevercTestLinkPhaseTrace {
  const NevercLinkPhaseAPI *PhaseAPI;
  char Events[128];
  uint32_t EventCount;
  uint32_t Mutations;
  NevercStatusCode MutationStatus;
} NevercTestLinkPhaseTrace;

NevercStatus NEVERC_CALL neverc_test_link_observer(
    const NevercPhaseFrame *Frame, NevercObserverPoint Point,
    void *UserData);
NevercStatus NEVERC_CALL neverc_test_link_interceptor(
    const NevercPhaseFrame *Frame,
    NevercPhaseContinuation *Continuation,
    NevercPhaseResult *OutResult, void *UserData);
NevercStatus NEVERC_CALL neverc_test_link_provider(
    const NevercPhaseFrame *Frame, NevercPhaseResult *OutResult,
    void *UserData);

#ifdef __cplusplus
}
#endif

#endif
