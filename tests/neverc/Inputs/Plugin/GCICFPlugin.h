#ifndef NEVERC_TEST_GC_ICF_PLUGIN_H
#define NEVERC_TEST_GC_ICF_PLUGIN_H

#include "neverc/Plugin/PluginLink.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t NevercTestGCICFOperation;
#define NEVERC_TEST_GC_KEEP_DEAD UINT32_C(1)
#define NEVERC_TEST_GC_DROP_ROOT UINT32_C(2)
#define NEVERC_TEST_ICF_PREVENT_FOLD UINT32_C(3)
#define NEVERC_TEST_ICF_INVALID_FOLD UINT32_C(4)

typedef struct NevercTestGCICFTrace {
  const NevercLinkPhaseAPI *PhaseAPI;
  NevercTestGCICFOperation Operation;
  uint32_t Mutations;
  NevercStatusCode MutationStatus;
} NevercTestGCICFTrace;

NevercStatus NEVERC_CALL neverc_test_gc_icf_interceptor(
    const NevercPhaseFrame *Frame,
    NevercPhaseContinuation *Continuation,
    NevercPhaseResult *OutResult, void *UserData);

#ifdef __cplusplus
}
#endif

#endif
