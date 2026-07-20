#ifndef NEVERC_TEST_SYNTHETIC_LINK_PLUGIN_H
#define NEVERC_TEST_SYNTHETIC_LINK_PLUGIN_H

#include "neverc/Plugin/PluginLink.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NevercTestSyntheticLinkTrace {
  const NevercLinkPhaseAPI *PhaseAPI;
  uint32_t Mutations;
  NevercStatusCode MutationStatus;
  NevercBool MakeInvalid;
} NevercTestSyntheticLinkTrace;

NevercStatus NEVERC_CALL neverc_test_synthetic_link_interceptor(
    const NevercPhaseFrame *Frame,
    NevercPhaseContinuation *Continuation,
    NevercPhaseResult *OutResult, void *UserData);

#ifdef __cplusplus
}
#endif

#endif
