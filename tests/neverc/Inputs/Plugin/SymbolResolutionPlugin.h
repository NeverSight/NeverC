#ifndef NEVERC_TEST_SYMBOL_RESOLUTION_PLUGIN_H
#define NEVERC_TEST_SYMBOL_RESOLUTION_PLUGIN_H

#include "neverc/Plugin/PluginLink.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NevercTestSymbolResolutionTrace {
  const NevercLinkPhaseAPI *PhaseAPI;
  uint32_t Mutations;
  NevercStatusCode MutationStatus;
  NevercBool MakeInvalid;
} NevercTestSymbolResolutionTrace;

NevercStatus NEVERC_CALL neverc_test_symbol_resolution_interceptor(
    const NevercPhaseFrame *Frame,
    NevercPhaseContinuation *Continuation,
    NevercPhaseResult *OutResult, void *UserData);

#ifdef __cplusplus
}
#endif

#endif
