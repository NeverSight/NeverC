/*===-- DomainConformancePlugin.c - per-domain phase conformance fixture --===*\
|*                                                                            *|
|* One pure-C fixture, built by the *system* C compiler against ONLY the      *|
|* public SDK single header, that registers observers and interceptors on the *|
|* standard phases of each domain. Which registrations happen is selected by  *|
|* -D toggles so a single source proves OBSERVABLE / INTERCEPTABLE /          *|
|* SEALED_HOST_GATE policy behaviour across driver, IR, MC, object, link and  *|
|* dyncode:                                                                    *|
|*                                                                            *|
|*   NCF_OBSERVE_DRIVER_ARGS   observer on neverc.driver.raw_arguments        *|
|*   NCF_INTERCEPT_DRIVER_JOB  interceptor on neverc.driver.execute_job       *|
|*   NCF_OBSERVE_IR_GENERATE   observer on neverc.ir.generate                 *|
|*   NCF_OBSERVE_MC_PRE_INSN   observer on neverc.mc.emission.pre_instruction *|
|*   NCF_INTERCEPT_OBJECT      interceptor on neverc.object.pre_write (adds a  *|
|*                             deterministic ".nvc_conf" section)             *|
|*   NCF_OBSERVE_LINK_FULL     observer on neverc.link.full                   *|
|*   NCF_OBSERVE_DYNCODE_REQ   observer on neverc.dyncode.request.freeze      *|
|*                                                                            *|
|* Sealed-gate negatives register an interceptor on a SEALED_HOST_GATE phase, *|
|* which the host must reject (registration or session freeze fails with a    *|
|* "sealed" diagnostic):                                                      *|
|*                                                                            *|
|*   NCF_SEAL_OBJECT_COMMIT    neverc.object.commit                           *|
|*   NCF_SEAL_IR_FINAL_VERIFY  neverc.ir.final_verify                        *|
|*   NCF_SEAL_LINK_COMMIT      neverc.link.commit                            *|
|*   NCF_SEAL_DYNCODE_VERIFY   neverc.dyncode.verify                         *|
|*                                                                            *|
|* Every firing appends a deterministic line to the file named by the         *|
|* NEVERC_CONFORMANCE_LOG environment variable, so tests observe behaviour    *|
|* through a stable on-disk record rather than diagnostic formatting.         *|
\*===----------------------------------------------------------------------===*/

#include "neverc/Plugin/NevercPluginAPI.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef NCF_ID
#define NCF_ID "com.neverc.conformance.domain"
#endif

#define NCF_SV(Text)                                                           \
  (NevercStringView) { (Text), (uint64_t)(sizeof(Text) - 1) }

static NevercStatus ncf_fail(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

static void ncf_log(const char *Event) {
  const char *Path = getenv("NEVERC_CONFORMANCE_LOG");
  FILE *File;
  if (Path == NULL || Path[0] == '\0')
    return;
  File = fopen(Path, "ab");
  if (File == NULL)
    return;
  fputs(Event, File);
  fputc('\n', File);
  fclose(File);
}

static NevercInterfaceID ncf_phase(uint64_t High, uint64_t Low) {
  NevercInterfaceID Result;
  Result.High = High;
  Result.Low = Low;
  return Result;
}

/*--- Observer callbacks: read-only, log on the BEFORE point ---------------*/

#define NCF_DEFINE_OBSERVER(Fn, Marker)                                        \
  static NevercStatus NEVERC_CALL Fn(const NevercPhaseFrame *Frame,            \
                                     NevercObserverPoint Point,                \
                                     void *UserData) {                         \
    (void)UserData;                                                            \
    if (Frame == NULL)                                                         \
      return ncf_fail(NEVERC_STATUS_INVALID_ARGUMENT);                         \
    if (Point == NEVERC_OBSERVER_BEFORE)                                       \
      ncf_log(Marker);                                                         \
    return neverc_status_ok();                                                 \
  }

NCF_DEFINE_OBSERVER(observe_driver_args, "observe:driver_args")
NCF_DEFINE_OBSERVER(observe_ir_generate, "observe:ir_generate")
NCF_DEFINE_OBSERVER(observe_ir_optimize, "observe:ir_optimize")
NCF_DEFINE_OBSERVER(observe_mir_final, "observe:mir_final")
NCF_DEFINE_OBSERVER(observe_mc_pre_insn, "observe:mc_pre_instruction")
NCF_DEFINE_OBSERVER(observe_link_full, "observe:link_full")
NCF_DEFINE_OBSERVER(observe_dyncode_req, "observe:dyncode_request")

/*--- Pass-through interceptor: call next once, then CONTINUE --------------*/

static NevercStatus continue_after_next(const NevercPhaseFrame *Frame,
                                        NevercPhaseContinuation *Continuation,
                                        NevercPhaseResult *OutResult,
                                        const char *Marker) {
  NevercPhaseResult Downstream;
  NevercStatus Status;
  if (Frame == NULL || Continuation == NULL || OutResult == NULL)
    return ncf_fail(NEVERC_STATUS_INVALID_ARGUMENT);
  ncf_log(Marker);
  memset(&Downstream, 0, sizeof(Downstream));
  Downstream.Header = (NevercABITableHeader){
      sizeof(Downstream), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Status = Continuation->InvokeNext(Continuation, Frame, &Downstream);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  memset(OutResult, 0, sizeof(*OutResult));
  OutResult->Header = (NevercABITableHeader){
      sizeof(*OutResult), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  OutResult->Action = NEVERC_PHASE_CONTINUE;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL intercept_driver_job(
    const NevercPhaseFrame *Frame, NevercPhaseContinuation *Continuation,
    NevercPhaseResult *OutResult, void *UserData) {
  (void)UserData;
#if defined(NCF_FAIL_IN_INTERCEPT)
  (void)Continuation;
  (void)OutResult;
  if (Frame == NULL)
    return ncf_fail(NEVERC_STATUS_INVALID_ARGUMENT);
  ncf_log("intercept:driver_job_fail");
  return ncf_fail(NEVERC_STATUS_PLUGIN_FAILURE);
#else
  return continue_after_next(Frame, Continuation, OutResult,
                             "intercept:driver_job");
#endif
}

/* Sealed-gate probe: never actually invoked because the host rejects the
 * registration, but a valid callback keeps the descriptor well-formed. */
static NevercStatus NEVERC_CALL sealed_probe(
    const NevercPhaseFrame *Frame, NevercPhaseContinuation *Continuation,
    NevercPhaseResult *OutResult, void *UserData) {
  (void)UserData;
  return continue_after_next(Frame, Continuation, OutResult, "intercept:sealed");
}

/*--- Object pre-write interceptor: add a deterministic section ------------*/

#if defined(NCF_INTERCEPT_OBJECT)
static NevercStatus NEVERC_CALL intercept_object_pre_write(
    const NevercPhaseFrame *Frame, NevercPhaseContinuation *Continuation,
    NevercPhaseResult *OutResult, void *UserData) {
  const NevercObjectPhaseAPI *Phase = (const NevercObjectPhaseAPI *)UserData;
  static const uint8_t Marker[] = "neverc conformance section";
  NevercObjectPhaseGraphInfo Graph;
  NevercObjectSectionDescriptor Section;
  NevercObjectSectionHandle Created = {0, 0};
  NevercObjectMutationHandle Mutation = {0, 0};
  NevercPhaseResult Downstream;
  NevercStatus Status;

  if (Frame == NULL || Continuation == NULL || OutResult == NULL ||
      Phase == NULL)
    return ncf_fail(NEVERC_STATUS_INVALID_ARGUMENT);

  memset(&Graph, 0, sizeof(Graph));
  Graph.Header = (NevercABITableHeader){sizeof(Graph),
                                        NEVERC_OBJECT_PHASE_API_MAJOR,
                                        NEVERC_OBJECT_PHASE_API_MINOR, 0};
  Status = Phase->GetGraph(Phase->Context, Frame, Frame->Input, &Graph);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = Graph.Object->BeginMutation(Graph.Object->Context, Frame->Task,
                                       Graph.Graph, &Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  memset(&Section, 0, sizeof(Section));
  Section.Header = (NevercABITableHeader){
      sizeof(Section), NEVERC_OBJECT_API_MAJOR, NEVERC_OBJECT_API_MINOR, 0};
  Section.Name = NCF_SV(".nvc_conf");
  Section.Kind = NEVERC_OBJECT_SECTION_KIND_DATA;
  Section.Flags = NEVERC_OBJECT_SECTION_ALLOCATED;
  Section.Alignment = 1;
  Section.Data = (NevercByteView){Marker, (uint64_t)(sizeof(Marker) - 1)};
  Status = Graph.Object->CreateSection(Graph.Object->Context, Frame->Task,
                                       Mutation, &Section, &Created);
  if (Status.Code != NEVERC_STATUS_OK) {
    (void)Graph.Object->AbandonMutation(Graph.Object->Context, Frame->Task,
                                        Mutation);
    return Status;
  }
  Status = Graph.Object->CommitMutation(Graph.Object->Context, Frame->Task,
                                        Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  ncf_log("intercept:object_pre_write");
  memset(&Downstream, 0, sizeof(Downstream));
  Downstream.Header = (NevercABITableHeader){
      sizeof(Downstream), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Status = Continuation->InvokeNext(Continuation, Frame, &Downstream);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  memset(OutResult, 0, sizeof(*OutResult));
  OutResult->Header = (NevercABITableHeader){
      sizeof(*OutResult), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  OutResult->Action = NEVERC_PHASE_CONTINUE;
  return neverc_status_ok();
}
#endif

/*--- Registration helpers ------------------------------------------------*/

static NevercStatus register_observer(const NevercRegistrarAPI *Registrar,
                                      void *Ctx, NevercInterfaceID Phase,
                                      NevercPhaseObserverFn Callback) {
  NevercObserverDescriptor Observer;
  memset(&Observer, 0, sizeof(Observer));
  Observer.Header = (NevercABITableHeader){
      sizeof(Observer), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Observer.Phase = Phase;
  Observer.Points = NEVERC_OBSERVER_BEFORE | NEVERC_OBSERVER_AFTER;
  Observer.Callback = Callback;
  Observer.UserData = NULL;
  return Registrar->RegisterObserver(Ctx, &Observer);
}

static NevercStatus register_interceptor(const NevercRegistrarAPI *Registrar,
                                         void *Ctx, NevercInterfaceID Phase,
                                         NevercPhaseInterceptorFn Callback,
                                         void *UserData) {
  NevercInterceptorDescriptor Interceptor;
  memset(&Interceptor, 0, sizeof(Interceptor));
  Interceptor.Header = (NevercABITableHeader){
      sizeof(Interceptor), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Interceptor.Phase = Phase;
  Interceptor.Callback = Callback;
  Interceptor.UserData = UserData;
  return Registrar->RegisterInterceptor(Ctx, &Interceptor);
}

static NevercStatus NEVERC_CALL
register_plugin(const NevercCoreAPI *Core, const NevercRegistrarAPI *Registrar,
                void *RegistrarContext, void *ProcessState) {
  NevercStatus Status;
  (void)Core;
  (void)ProcessState;
  if (Registrar == NULL)
    return ncf_fail(NEVERC_STATUS_INVALID_ARGUMENT);

#if defined(NCF_BAD_OBSERVER_SIZE)
  {
    /* A deliberately malformed descriptor: the Header claims a size far below
     * the required prefix. The host must reject it rather than read out of
     * bounds. Returning the (non-OK) status fails registration and the
     * compile. */
    NevercObserverDescriptor Bad;
    memset(&Bad, 0, sizeof(Bad));
    Bad.Header = (NevercABITableHeader){(uint32_t)sizeof(uint32_t),
                                        NEVERC_PLUGIN_ABI_MAJOR,
                                        NEVERC_PLUGIN_ABI_MINOR, 0};
    Bad.Phase = ncf_phase(NEVERC_PHASE_DRIVER_RAW_ARGUMENTS_HIGH,
                          NEVERC_PHASE_DRIVER_RAW_ARGUMENTS_LOW);
    Bad.Points = NEVERC_OBSERVER_BEFORE;
    Bad.Callback = observe_driver_args;
    return Registrar->RegisterObserver(RegistrarContext, &Bad);
  }
#endif
#if defined(NCF_REGISTER_FAILURE)
  return ncf_fail(NEVERC_STATUS_PLUGIN_FAILURE);
#endif

#if defined(NCF_OBSERVE_DRIVER_ARGS)
  Status = register_observer(
      Registrar, RegistrarContext,
      ncf_phase(NEVERC_PHASE_DRIVER_RAW_ARGUMENTS_HIGH,
                NEVERC_PHASE_DRIVER_RAW_ARGUMENTS_LOW),
      observe_driver_args);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
#endif
#if defined(NCF_INTERCEPT_DRIVER_JOB)
  Status = register_interceptor(
      Registrar, RegistrarContext,
      ncf_phase(NEVERC_PHASE_DRIVER_EXECUTE_JOB_HIGH,
                NEVERC_PHASE_DRIVER_EXECUTE_JOB_LOW),
      intercept_driver_job, NULL);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
#endif
#if defined(NCF_OBSERVE_IR_GENERATE)
  Status = register_observer(
      Registrar, RegistrarContext,
      ncf_phase(NEVERC_PHASE_IR_GENERATE_HIGH, NEVERC_PHASE_IR_GENERATE_LOW),
      observe_ir_generate);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
#endif
#if defined(NCF_OBSERVE_IR_OPTIMIZE)
  Status = register_observer(
      Registrar, RegistrarContext,
      ncf_phase(NEVERC_PHASE_IR_OPTIMIZE_HIGH, NEVERC_PHASE_IR_OPTIMIZE_LOW),
      observe_ir_optimize);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
#endif
#if defined(NCF_OBSERVE_MIR_FINAL)
  Status = register_observer(
      Registrar, RegistrarContext,
      ncf_phase(NEVERC_PHASE_MIR_PASS_FINAL_HIGH,
                NEVERC_PHASE_MIR_PASS_FINAL_LOW),
      observe_mir_final);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
#endif
#if defined(NCF_OBSERVE_MC_PRE_INSN)
  Status = register_observer(
      Registrar, RegistrarContext,
      ncf_phase(NEVERC_PHASE_MC_EMISSION_PRE_INSTRUCTION_HIGH,
                NEVERC_PHASE_MC_EMISSION_PRE_INSTRUCTION_LOW),
      observe_mc_pre_insn);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
#endif
#if defined(NCF_OBSERVE_LINK_FULL)
  Status = register_observer(
      Registrar, RegistrarContext,
      ncf_phase(NEVERC_PHASE_LINK_FULL_HIGH, NEVERC_PHASE_LINK_FULL_LOW),
      observe_link_full);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
#endif
#if defined(NCF_OBSERVE_DYNCODE_REQ)
  Status = register_observer(
      Registrar, RegistrarContext,
      ncf_phase(NEVERC_PHASE_DYNCODE_REQUEST_FREEZE_HIGH,
                NEVERC_PHASE_DYNCODE_REQUEST_FREEZE_LOW),
      observe_dyncode_req);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
#endif
#if defined(NCF_INTERCEPT_OBJECT)
  {
    const void *Table = NULL;
    uint16_t Minor = 0;
    uint64_t StructSize = 0;
    Status = Core->QueryInterface(
        Core->Context,
        (NevercInterfaceID){NEVERC_INTERFACE_OBJECT_PHASE_HIGH,
                            NEVERC_INTERFACE_OBJECT_PHASE_LOW},
        NEVERC_OBJECT_PHASE_API_MAJOR, NEVERC_OBJECT_PHASE_API_MINOR, &Table,
        &Minor, &StructSize);
    if (Status.Code != NEVERC_STATUS_OK || Table == NULL ||
        StructSize < sizeof(NevercObjectPhaseAPI))
      return ncf_fail(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
    Status = register_interceptor(
        Registrar, RegistrarContext,
        ncf_phase(NEVERC_PHASE_OBJECT_PRE_WRITE_HIGH,
                  NEVERC_PHASE_OBJECT_PRE_WRITE_LOW),
        intercept_object_pre_write, (void *)Table);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
  }
#endif

  /* Sealed-gate negatives: registering an interceptor here must be rejected. */
#if defined(NCF_SEAL_OBJECT_COMMIT)
  return register_interceptor(
      Registrar, RegistrarContext,
      ncf_phase(NEVERC_PHASE_OBJECT_COMMIT_HIGH, NEVERC_PHASE_OBJECT_COMMIT_LOW),
      sealed_probe, NULL);
#elif defined(NCF_SEAL_IR_FINAL_VERIFY)
  return register_interceptor(
      Registrar, RegistrarContext,
      ncf_phase(NEVERC_PHASE_IR_FINAL_VERIFY_HIGH,
                NEVERC_PHASE_IR_FINAL_VERIFY_LOW),
      sealed_probe, NULL);
#elif defined(NCF_SEAL_LINK_COMMIT)
  return register_interceptor(
      Registrar, RegistrarContext,
      ncf_phase(NEVERC_PHASE_LINK_COMMIT_HIGH, NEVERC_PHASE_LINK_COMMIT_LOW),
      sealed_probe, NULL);
#elif defined(NCF_SEAL_DYNCODE_VERIFY)
  return register_interceptor(
      Registrar, RegistrarContext,
      ncf_phase(NEVERC_PHASE_DYNCODE_VERIFY_HIGH,
                NEVERC_PHASE_DYNCODE_VERIFY_LOW),
      sealed_probe, NULL);
#else
  (void)sealed_probe;
  return neverc_status_ok();
#endif
}

NEVERC_EXPORT NevercStatus NEVERC_CALL
neverc_plugin_entry(const NevercBootstrapAPI *Bootstrap,
                    NevercPluginDescriptor *OutPlugin) {
  NevercPluginDescriptor Descriptor;
  uint32_t Capacity;
  size_t Writable;
  if (Bootstrap == NULL || OutPlugin == NULL ||
      OutPlugin->Header.StructSize < sizeof(uint32_t))
    return ncf_fail(NEVERC_STATUS_INVALID_ARGUMENT);
  Capacity = OutPlugin->Header.StructSize;
  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header = (NevercABITableHeader){
      sizeof(Descriptor), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Descriptor.PluginID = (NevercStringView){NCF_ID, (uint64_t)strlen(NCF_ID)};
  Descriptor.DisplayName = NCF_SV("Conformance Domain Plugin");
  Descriptor.Version = (NevercSemanticVersion){1, 0, 0, 0, {0, 0}, {0, 0}};
  Descriptor.Concurrency = NEVERC_CONCURRENCY_SESSION_SERIAL;
  Descriptor.Reentrancy = NEVERC_REENTRANCY_ALLOWED;
  Descriptor.Register = register_plugin;
  Writable = Capacity < sizeof(Descriptor) ? Capacity : sizeof(Descriptor);
  memcpy(OutPlugin, &Descriptor, Writable);
  OutPlugin->Header.StructSize = (uint32_t)sizeof(Descriptor);
  return neverc_status_ok();
}
