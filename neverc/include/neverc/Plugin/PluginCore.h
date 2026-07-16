/*===-- PluginCore.h - NeverC plugin core C ABI -------------------*- C -*-===*\
|*                                                                            *|
|* This header defines the first public NeverC plugin ABI.                    *|
|* It is valid C11/C23 and C++17 and does not expose C++ implementation       *|
|* types. Domain interfaces are negotiated independently through interface    *|
|* identifiers.                                                               *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef NEVERC_PLUGIN_PLUGINCORE_H
#define NEVERC_PLUGIN_PLUGINCORE_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#ifndef NEVERC_EXPORT
#define NEVERC_EXPORT __declspec(dllexport)
#endif
#ifndef NEVERC_CALL
#define NEVERC_CALL __cdecl
#endif
#elif defined(__GNUC__) || defined(__clang__)
#ifndef NEVERC_EXPORT
#define NEVERC_EXPORT __attribute__((visibility("default")))
#endif
#ifndef NEVERC_CALL
#define NEVERC_CALL
#endif
#else
#ifndef NEVERC_EXPORT
#define NEVERC_EXPORT
#endif
#ifndef NEVERC_CALL
#define NEVERC_CALL
#endif
#endif

#if defined(_MSC_VER)
#define NEVERC_ABI_PACK_BEGIN __pragma(pack(push, 8))
#define NEVERC_ABI_PACK_END __pragma(pack(pop))
#else
#define NEVERC_ABI_DO_PRAGMA_(value) _Pragma(#value)
#define NEVERC_ABI_PACK_BEGIN NEVERC_ABI_DO_PRAGMA_(pack(push, 8))
#define NEVERC_ABI_PACK_END NEVERC_ABI_DO_PRAGMA_(pack(pop))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_PLUGIN_ABI_MAJOR UINT16_C(1)
#define NEVERC_PLUGIN_ABI_MINOR UINT16_C(0)
#define NEVERC_PLUGIN_ENTRY_POINT "neverc_plugin_entry"

#define NEVERC_CORE_API_MAJOR UINT16_C(1)
#define NEVERC_CORE_API_MINOR UINT16_C(0)
#define NEVERC_INTERFACE_CORE_HIGH UINT64_C(0x4e6576657243436f)
#define NEVERC_INTERFACE_CORE_LOW UINT64_C(0x7265415049000001)

typedef uint32_t NevercBool;
#define NEVERC_FALSE UINT32_C(0)
#define NEVERC_TRUE UINT32_C(1)

typedef int32_t NevercStatusCode;
#define NEVERC_STATUS_OK INT32_C(0)
#define NEVERC_STATUS_INVALID_ARGUMENT INT32_C(1)
#define NEVERC_STATUS_ABI_MISMATCH INT32_C(2)
#define NEVERC_STATUS_MISSING_INTERFACE INT32_C(3)
#define NEVERC_STATUS_VERSION_MISMATCH INT32_C(4)
#define NEVERC_STATUS_INVALID_DESCRIPTOR INT32_C(5)
#define NEVERC_STATUS_DUPLICATE_ID INT32_C(6)
#define NEVERC_STATUS_DEPENDENCY_MISSING INT32_C(7)
#define NEVERC_STATUS_DEPENDENCY_CYCLE INT32_C(8)
#define NEVERC_STATUS_BUSY INT32_C(9)
#define NEVERC_STATUS_CANCELLED INT32_C(10)
#define NEVERC_STATUS_RESOURCE_EXHAUSTED INT32_C(11)
#define NEVERC_STATUS_STALE_HANDLE INT32_C(12)
#define NEVERC_STATUS_WRONG_SESSION INT32_C(13)
#define NEVERC_STATUS_WRONG_SCOPE INT32_C(14)
#define NEVERC_STATUS_WRONG_TYPE INT32_C(15)
#define NEVERC_STATUS_INVALID_STATE INT32_C(16)
#define NEVERC_STATUS_POLICY_VIOLATION INT32_C(17)
#define NEVERC_STATUS_VERIFICATION_FAILED INT32_C(18)
#define NEVERC_STATUS_CAPABILITY_UNAVAILABLE INT32_C(19)
#define NEVERC_STATUS_PLUGIN_FAILURE INT32_C(20)
#define NEVERC_STATUS_PLUGIN_EXCEPTION INT32_C(21)
#define NEVERC_STATUS_OUTPUT_PARTIAL INT32_C(22)
#define NEVERC_STATUS_REENTRANCY_DENIED INT32_C(23)

typedef uint32_t NevercStatusFlags;
#define NEVERC_STATUS_FLAG_NONE UINT32_C(0)
#define NEVERC_STATUS_FLAG_RECOVERABLE UINT32_C(1)
#define NEVERC_STATUS_FLAG_OUTPUT_ALREADY_COMMITTED UINT32_C(2)
#define NEVERC_STATUS_FLAG_OUTPUT_MAY_BE_PARTIAL UINT32_C(4)
#define NEVERC_STATUS_FLAG_OUTPUT_RECOVERY_REQUIRED UINT32_C(8)
#define NEVERC_STATUS_FLAG_DURABILITY_UNCONFIRMED UINT32_C(16)

typedef uint32_t NevercDiagnosticSeverity;
#define NEVERC_DIAGNOSTIC_NOTE UINT32_C(0)
#define NEVERC_DIAGNOSTIC_REMARK UINT32_C(1)
#define NEVERC_DIAGNOSTIC_WARNING UINT32_C(2)
#define NEVERC_DIAGNOSTIC_ERROR UINT32_C(3)
#define NEVERC_DIAGNOSTIC_FATAL UINT32_C(4)

typedef uint32_t NevercInterfaceStability;
#define NEVERC_INTERFACE_STABLE UINT32_C(0)
#define NEVERC_INTERFACE_LOCKSTEP UINT32_C(1)

typedef uint32_t NevercConcurrencyModel;
#define NEVERC_CONCURRENCY_SESSION_SERIAL UINT32_C(0)
#define NEVERC_CONCURRENCY_THREAD_SAFE UINT32_C(1)
#define NEVERC_CONCURRENCY_PROCESS_SERIAL UINT32_C(2)

typedef uint32_t NevercReentrancyModel;
#define NEVERC_REENTRANCY_NONE UINT32_C(0)
#define NEVERC_REENTRANCY_ALLOWED UINT32_C(1)

typedef uint32_t NevercTaskKind;
#define NEVERC_TASK_INVOCATION UINT32_C(1)
#define NEVERC_TASK_TRANSLATION_UNIT UINT32_C(2)
#define NEVERC_TASK_LTO UINT32_C(3)
#define NEVERC_TASK_LINK UINT32_C(4)
#define NEVERC_TASK_CODEGEN UINT32_C(5)
#define NEVERC_TASK_OBJECT UINT32_C(6)
#define NEVERC_TASK_DYNCODE UINT32_C(7)

typedef uint32_t NevercDependencyKind;
#define NEVERC_DEPENDENCY_REQUIRED UINT32_C(0)
#define NEVERC_DEPENDENCY_BEFORE UINT32_C(1)
#define NEVERC_DEPENDENCY_AFTER UINT32_C(2)

typedef uint64_t NevercPhasePolicy;
#define NEVERC_PHASE_OBSERVABLE UINT64_C(1)
#define NEVERC_PHASE_INTERCEPTABLE UINT64_C(2)
#define NEVERC_PHASE_REPLACEABLE UINT64_C(4)
#define NEVERC_PHASE_SKIPPABLE_WITH_PROOF UINT64_C(8)
#define NEVERC_PHASE_SEALED_HOST_GATE UINT64_C(16)

typedef uint32_t NevercPhaseAction;
#define NEVERC_PHASE_CONTINUE UINT32_C(0)
#define NEVERC_PHASE_REPLACE UINT32_C(1)
#define NEVERC_PHASE_SKIP UINT32_C(2)

typedef uint32_t NevercObserverPoint;
#define NEVERC_OBSERVER_BEFORE UINT32_C(1)
#define NEVERC_OBSERVER_AFTER UINT32_C(2)
#define NEVERC_OBSERVER_AFTER_COMMIT UINT32_C(4)

NEVERC_ABI_PACK_BEGIN

typedef struct NevercABITableHeader {
  uint32_t StructSize;
  uint16_t Major;
  uint16_t Minor;
  uint64_t Flags;
} NevercABITableHeader;

typedef struct NevercStringView {
  const char *Data;
  uint64_t Length;
} NevercStringView;

typedef struct NevercByteView {
  const uint8_t *Data;
  uint64_t Length;
} NevercByteView;

typedef struct NevercStructArrayView {
  const void *Data;
  uint64_t Count;
  uint64_t ElementStride;
} NevercStructArrayView;

typedef struct NevercInterfaceID {
  uint64_t High;
  uint64_t Low;
} NevercInterfaceID;

typedef struct NevercHandle {
  uint64_t Owner;
  uint64_t Value;
} NevercHandle;

typedef NevercHandle NevercSessionHandle;
typedef NevercHandle NevercTaskHandle;
typedef NevercHandle NevercArtifactHandle;
typedef NevercHandle NevercProofHandle;
typedef NevercHandle NevercDiagnosticHandle;
typedef NevercHandle NevercSourceLocationHandle;
typedef NevercHandle NevercSourceRangeHandle;
typedef NevercHandle NevercFixItHandle;

typedef struct NevercStatus {
  NevercStatusCode Code;
  NevercStatusFlags Flags;
  uint64_t Detail;
} NevercStatus;

typedef struct NevercSemanticVersion {
  uint32_t Major;
  uint32_t Minor;
  uint32_t Patch;
  uint32_t Reserved;
  NevercStringView Prerelease;
  NevercStringView BuildMetadata;
} NevercSemanticVersion;

typedef struct NevercVersionRange {
  NevercSemanticVersion MinimumInclusive;
  NevercSemanticVersion MaximumExclusive;
  NevercBool HasMaximum;
  NevercBool AllowPrerelease;
  uint64_t Reserved;
} NevercVersionRange;

typedef struct NevercCompatibilityKey {
  NevercABITableHeader Header;
  NevercStringView ProducerBuildID;
  NevercStringView TargetABIKey;
  uint32_t LLVMMajor;
  uint32_t Reserved;
} NevercCompatibilityKey;

typedef struct NevercInterfaceRequirement {
  NevercABITableHeader Header;
  NevercInterfaceID Interface;
  uint16_t Major;
  uint16_t MinimumMinor;
  NevercBool Required;
  NevercInterfaceStability Stability;
  NevercCompatibilityKey Compatibility;
} NevercInterfaceRequirement;

typedef struct NevercPluginDependency {
  NevercABITableHeader Header;
  NevercStringView PluginID;
  NevercVersionRange Version;
  NevercDependencyKind Kind;
  uint32_t Reserved;
} NevercPluginDependency;

typedef struct NevercDiagnosticNote {
  NevercABITableHeader Header;
  NevercStringView Message;
} NevercDiagnosticNote;

typedef struct NevercDiagnosticDescriptor {
  NevercABITableHeader Header;
  NevercDiagnosticSeverity Severity;
  uint32_t Code;
  NevercStringView PluginID;
  NevercStringView PhaseID;
  NevercStringView Message;
  NevercStructArrayView Notes;
  NevercSourceLocationHandle Location;
  NevercStructArrayView Ranges;
  NevercStructArrayView FixIts;
} NevercDiagnosticDescriptor;

typedef struct NevercPhaseResult {
  NevercABITableHeader Header;
  NevercPhaseAction Action;
  uint32_t Reserved;
  NevercArtifactHandle Output;
  NevercProofHandle Proof;
} NevercPhaseResult;

typedef struct NevercPhaseRoute {
  NevercABITableHeader Header;
  NevercStringView TargetTriple;
  NevercStringView CPU;
  NevercStringView Features;
  NevercStringView ObjectFormat;
  uint32_t ExecutionLevel;
  uint32_t Reserved;
} NevercPhaseRoute;

typedef struct NevercPhaseFrame {
  NevercABITableHeader Header;
  NevercSessionHandle Session;
  NevercTaskHandle Task;
  NevercInterfaceID Phase;
  NevercPhaseRoute Route;
  NevercArtifactHandle Input;
  NevercArtifactHandle CurrentOutput;
  NevercHandle Cancellation;
} NevercPhaseFrame;

struct NevercBootstrapAPI;
struct NevercCoreAPI;
struct NevercRegistrarAPI;
struct NevercPhaseContinuation;
struct NevercPluginDescriptor;
struct NevercOptionDescriptor;

typedef NevercStatus(NEVERC_CALL *NevercQueryInterfaceFn)(
    void *Context, NevercInterfaceID Interface, uint16_t Major,
    uint16_t MinimumMinor, const void **OutTable, uint16_t *OutMinor,
    uint64_t *OutStructSize);

typedef NevercStatus(NEVERC_CALL *NevercAllocateFn)(
    void *Context, uint64_t Size, uint64_t Alignment, void **OutPointer);
typedef NevercStatus(NEVERC_CALL *NevercReallocateFn)(
    void *Context, void *Pointer, uint64_t OldSize, uint64_t NewSize,
    uint64_t Alignment, void **OutPointer);
typedef NevercStatus(NEVERC_CALL *NevercDeallocateFn)(
    void *Context, void *Pointer, uint64_t Size, uint64_t Alignment);
typedef NevercStatus(NEVERC_CALL *NevercEmitDiagnosticFn)(
    void *Context, const NevercDiagnosticDescriptor *Diagnostic,
    NevercDiagnosticHandle *OutDiagnostic);
typedef NevercStatus(NEVERC_CALL *NevercCheckCancelledFn)(
    void *Context, NevercTaskHandle Task);
typedef NevercStatus(NEVERC_CALL *NevercGetSessionStateFn)(
    void *Context, NevercSessionHandle Session, NevercStringView PluginID,
    void **OutState);
typedef NevercStatus(NEVERC_CALL *NevercGetTaskStateFn)(
    void *Context, NevercTaskHandle Task, NevercStringView PluginID,
    void **OutState);

typedef struct NevercBootstrapAPI {
  NevercABITableHeader Header;
  void *Context;
  NevercQueryInterfaceFn QueryInterface;
  NevercStringView HostBuildID;
  uint32_t LLVMMajor;
  uint32_t Reserved;
} NevercBootstrapAPI;

typedef struct NevercCoreAPI {
  NevercABITableHeader Header;
  void *Context;
  NevercAllocateFn Allocate;
  NevercReallocateFn Reallocate;
  NevercDeallocateFn Deallocate;
  NevercEmitDiagnosticFn EmitDiagnostic;
  NevercQueryInterfaceFn QueryInterface;
  NevercCheckCancelledFn CheckCancelled;
  NevercGetSessionStateFn GetSessionState;
  NevercGetTaskStateFn GetTaskState;
} NevercCoreAPI;

typedef NevercStatus(NEVERC_CALL *NevercInvokeNextFn)(
    struct NevercPhaseContinuation *Continuation,
    const NevercPhaseFrame *Frame, NevercPhaseResult *OutResult);

typedef struct NevercPhaseContinuation {
  NevercABITableHeader Header;
  NevercInvokeNextFn InvokeNext;
  void *Context;
  uint64_t Generation;
} NevercPhaseContinuation;

typedef NevercStatus(NEVERC_CALL *NevercPhaseObserverFn)(
    const NevercPhaseFrame *Frame, NevercObserverPoint Point, void *UserData);
typedef NevercStatus(NEVERC_CALL *NevercPhaseInterceptorFn)(
    const NevercPhaseFrame *Frame, NevercPhaseContinuation *Continuation,
    NevercPhaseResult *OutResult, void *UserData);
typedef NevercStatus(NEVERC_CALL *NevercPhaseProviderFn)(
    const NevercPhaseFrame *Frame, NevercPhaseResult *OutResult,
    void *UserData);
typedef void(NEVERC_CALL *NevercDestroyUserDataFn)(void *UserData);

typedef struct NevercPhaseDescriptor {
  NevercABITableHeader Header;
  NevercInterfaceID Phase;
  NevercStringView CanonicalName;
  NevercInterfaceID InputArtifact;
  NevercInterfaceID OutputArtifact;
  NevercPhasePolicy Policy;
  NevercObserverPoint ObserverPoints;
  uint32_t Reserved;
} NevercPhaseDescriptor;

typedef struct NevercObserverDescriptor {
  NevercABITableHeader Header;
  NevercInterfaceID Phase;
  NevercObserverPoint Points;
  uint32_t Reserved;
  NevercPhaseObserverFn Callback;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercObserverDescriptor;

typedef struct NevercInterceptorDescriptor {
  NevercABITableHeader Header;
  NevercInterfaceID Phase;
  NevercPhaseInterceptorFn Callback;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercInterceptorDescriptor;

typedef struct NevercProviderDescriptor {
  NevercABITableHeader Header;
  NevercInterfaceID Phase;
  NevercStringView ProviderID;
  NevercPhaseRoute Route;
  NevercBool Deterministic;
  NevercBool Cacheable;
  NevercBool FallbackSafe;
  uint32_t Reserved;
  NevercPhaseProviderFn Callback;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercProviderDescriptor;

typedef NevercStatus(NEVERC_CALL *NevercRegisterInterfaceFn)(
    void *Registrar, NevercInterfaceID Interface,
    NevercInterfaceStability Stability, const void *Table,
    const NevercCompatibilityKey *Compatibility);
typedef NevercStatus(NEVERC_CALL *NevercRegisterPhaseFn)(
    void *Registrar, const NevercPhaseDescriptor *Descriptor);
typedef NevercStatus(NEVERC_CALL *NevercRegisterObserverFn)(
    void *Registrar, const NevercObserverDescriptor *Descriptor);
typedef NevercStatus(NEVERC_CALL *NevercRegisterInterceptorFn)(
    void *Registrar, const NevercInterceptorDescriptor *Descriptor);
typedef NevercStatus(NEVERC_CALL *NevercRegisterProviderFn)(
    void *Registrar, const NevercProviderDescriptor *Descriptor);
typedef NevercStatus(NEVERC_CALL *NevercRegisterOptionFn)(
    void *Registrar, const struct NevercOptionDescriptor *Descriptor);

typedef struct NevercRegistrarAPI {
  NevercABITableHeader Header;
  NevercRegisterInterfaceFn RegisterInterface;
  NevercRegisterPhaseFn RegisterPhase;
  NevercRegisterObserverFn RegisterObserver;
  NevercRegisterInterceptorFn RegisterInterceptor;
  NevercRegisterProviderFn RegisterProvider;
  NevercRegisterOptionFn RegisterOption;
} NevercRegistrarAPI;

typedef NevercStatus(NEVERC_CALL *NevercProcessBeginFn)(
    const NevercCoreAPI *Core, void **OutProcessState);
typedef NevercStatus(NEVERC_CALL *NevercRegisterPluginFn)(
    const NevercCoreAPI *Core, const NevercRegistrarAPI *Registrar,
    void *RegistrarContext, void *ProcessState);
typedef NevercStatus(NEVERC_CALL *NevercSessionBeginFn)(
    const NevercCoreAPI *Core, NevercSessionHandle Session, void *ProcessState,
    void **OutSessionState);
typedef NevercStatus(NEVERC_CALL *NevercSessionEndFn)(
    const NevercCoreAPI *Core, NevercSessionHandle Session, void *ProcessState,
    void *SessionState);
typedef NevercStatus(NEVERC_CALL *NevercTaskBeginFn)(
    const NevercCoreAPI *Core, NevercTaskHandle Task, NevercTaskKind Kind,
    void *ProcessState, void *SessionState, void **OutTaskState);
typedef NevercStatus(NEVERC_CALL *NevercTaskEndFn)(
    const NevercCoreAPI *Core, NevercTaskHandle Task, NevercTaskKind Kind,
    void *ProcessState, void *SessionState, void *TaskState);
typedef NevercStatus(NEVERC_CALL *NevercPluginDestroyFn)(
    const NevercCoreAPI *Core, void *ProcessState);

typedef struct NevercPluginDescriptor {
  NevercABITableHeader Header;
  NevercStringView PluginID;
  NevercStringView DisplayName;
  NevercSemanticVersion Version;
  NevercConcurrencyModel Concurrency;
  NevercReentrancyModel Reentrancy;
  NevercStructArrayView RequiredInterfaces;
  NevercStructArrayView OptionalInterfaces;
  NevercStructArrayView Dependencies;
  NevercProcessBeginFn ProcessBegin;
  NevercRegisterPluginFn Register;
  NevercSessionBeginFn SessionBegin;
  NevercSessionEndFn SessionEnd;
  NevercTaskBeginFn TaskBegin;
  NevercTaskEndFn TaskEnd;
  NevercPluginDestroyFn Destroy;
} NevercPluginDescriptor;

typedef NevercStatus(NEVERC_CALL *NevercPluginEntryFn)(
    const NevercBootstrapAPI *Bootstrap, NevercPluginDescriptor *OutPlugin);

NEVERC_ABI_PACK_END

#define NEVERC_ABI_FIELD_AVAILABLE(header_pointer, structure_type, field)      \
  ((header_pointer) != NULL &&                                                 \
   (header_pointer)->StructSize >=                                             \
       (uint32_t)(offsetof(structure_type, field) +                            \
                  sizeof(((structure_type *)0)->field)))

static inline NevercStatus neverc_status_ok(void) {
  NevercStatus Result;
  Result.Code = NEVERC_STATUS_OK;
  Result.Flags = NEVERC_STATUS_FLAG_NONE;
  Result.Detail = UINT64_C(0);
  return Result;
}

static inline NevercBool neverc_status_is_ok(NevercStatus Status) {
  return Status.Code == NEVERC_STATUS_OK ? NEVERC_TRUE : NEVERC_FALSE;
}

static inline NevercBool neverc_handle_is_null(NevercHandle Handle) {
  return (Handle.Owner == UINT64_C(0) && Handle.Value == UINT64_C(0))
             ? NEVERC_TRUE
             : NEVERC_FALSE;
}

/*
 * A plugin exports exactly this function:
 *
 * NEVERC_EXPORT NevercStatus NEVERC_CALL
 * neverc_plugin_entry(const NevercBootstrapAPI *Bootstrap,
 *                     NevercPluginDescriptor *OutPlugin);
 *
 * OutPlugin is a caller-owned buffer. On entry Header.StructSize contains the
 * writable capacity. The plugin writes at most that capacity and returns its
 * complete produced size in Header.StructSize.
 */
NEVERC_EXPORT NevercStatus NEVERC_CALL
neverc_plugin_entry(const NevercBootstrapAPI *Bootstrap,
                    NevercPluginDescriptor *OutPlugin);

#ifdef __cplusplus
}
#endif

#endif /* NEVERC_PLUGIN_PLUGINCORE_H */
