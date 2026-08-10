#ifndef NEVERC_TESTS_INPUTS_PLUGIN_POSTEMITPLUGIN_H
#define NEVERC_TESTS_INPUTS_PLUGIN_POSTEMITPLUGIN_H

#include "neverc/Plugin/PluginLink.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NevercTestPostEmitTrace {
  const NevercLinkPhaseAPI *PhaseAPI;
  uint64_t PatchOffset;
  uint8_t PatchValue;
  NevercBool AppendByte;
  uint32_t Calls;
  NevercStatusCode MutationStatus;
  uint64_t ObservedSize;
  uint32_t AfterCommitCalls;
  NevercStatusCode AfterCommitWriteStatus;
  uint32_t ObserverCalls;
  NevercStatusCode ObserverMutationStatuses[6];
  const NevercLinkAPI *CachedLink;
  const NevercMutableBinaryAPI *CachedBinary;
  NevercTaskHandle CachedTask;
  NevercBinaryImageHandle CachedImage;
  NevercMutableBinaryBuilderHandle CachedBuilder;
  uint64_t CachedImageSize;
  uint32_t CachedCapabilityObserverCalls;
  NevercStatusCode CachedLinkReadStatus;
  NevercStatusCode CachedCapabilityMutationStatuses[6];
} NevercTestPostEmitTrace;

NevercStatus NEVERC_CALL neverc_test_post_emit_interceptor(
    const NevercPhaseFrame *Frame,
    NevercPhaseContinuation *Continuation,
    NevercPhaseResult *OutResult, void *UserData);

NevercStatus NEVERC_CALL neverc_test_after_commit_observer(
    const NevercPhaseFrame *Frame, NevercObserverPoint Point,
    void *UserData);

NevercStatus NEVERC_CALL neverc_test_post_emit_observer(
    const NevercPhaseFrame *Frame, NevercObserverPoint Point,
    void *UserData);

NevercStatus NEVERC_CALL neverc_test_cached_post_emit_capability_observer(
    const NevercPhaseFrame *Frame, NevercObserverPoint Point,
    void *UserData);

#ifdef __cplusplus
}
#endif

#endif
