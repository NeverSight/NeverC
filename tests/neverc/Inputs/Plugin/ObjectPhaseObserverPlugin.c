#include "neverc/Plugin/PluginObject.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define STRING_VIEW(Value) {(Value), (uint64_t)(sizeof(Value) - 1)}

static const NevercObjectPhaseAPI *ObjectPhase;
static uint64_t GraphObserverCalls;
static uint64_t ImageObserverCalls;
static int ObserverHealthy = 1;

static NevercStatus status_code(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

static void expect_policy_violation(NevercStatus Status) {
  if (Status.Code != NEVERC_STATUS_POLICY_VIOLATION)
    ObserverHealthy = 0;
}

static NevercStatus NEVERC_CALL observe_graph(const NevercPhaseFrame *Frame,
                                              NevercObserverPoint Point,
                                              void *UserData) {
  NevercObjectPhaseGraphInfo Graph;
  NevercObjectMutationHandle Mutation = {0, 0};
  NevercStatus Status;
  (void)UserData;

  if (Frame == NULL || ObjectPhase == NULL || Point != NEVERC_OBSERVER_BEFORE)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  memset(&Graph, 0, sizeof(Graph));
  Graph.Header.StructSize = sizeof(Graph);
  Graph.Header.Major = NEVERC_OBJECT_PHASE_API_MAJOR;
  Graph.Header.Minor = NEVERC_OBJECT_PHASE_API_MINOR;
  Status =
      ObjectPhase->GetGraph(ObjectPhase->Context, Frame, Frame->Input, &Graph);
  if (Status.Code != NEVERC_STATUS_OK || Graph.Object == NULL)
    return Status.Code == NEVERC_STATUS_OK
               ? status_code(NEVERC_STATUS_INVALID_STATE)
               : Status;
  Status = Graph.Object->BeginMutation(Graph.Object->Context, Frame->Task,
                                       Graph.Graph, &Mutation);
  if (Status.Code != NEVERC_STATUS_POLICY_VIOLATION)
    ObserverHealthy = 0;
  ++GraphObserverCalls;
  return ObserverHealthy ? neverc_status_ok()
                         : status_code(NEVERC_STATUS_PLUGIN_FAILURE);
}

static NevercStatus NEVERC_CALL observe_image(const NevercPhaseFrame *Frame,
                                              NevercObserverPoint Point,
                                              void *UserData) {
  static const char ExpectedProvenance[] =
      "native:neverc.builtin.llvm-object:elf";
  NevercObjectImageInfo Image;
  NevercStatus Status;
  uint8_t Byte = 0;
  uint64_t Position = 0;
  (void)UserData;

  if (Frame == NULL || ObjectPhase == NULL || Point != NEVERC_OBSERVER_AFTER)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  memset(&Image, 0, sizeof(Image));
  Image.Header.StructSize = sizeof(Image);
  Image.Header.Major = NEVERC_OBJECT_PHASE_API_MAJOR;
  Image.Header.Minor = NEVERC_OBJECT_PHASE_API_MINOR;
  Status = ObjectPhase->GetImage(ObjectPhase->Context, Frame,
                                 Frame->CurrentOutput, &Image);
  if (Status.Code != NEVERC_STATUS_OK || Image.Binary == NULL ||
      neverc_handle_is_null(Image.Builder) || Image.Size == 0)
    return Status.Code == NEVERC_STATUS_OK
               ? status_code(NEVERC_STATUS_INVALID_STATE)
               : Status;
  if (Image.Provenance.Length != sizeof(ExpectedProvenance) - 1 ||
      memcmp(Image.Provenance.Data, ExpectedProvenance,
             sizeof(ExpectedProvenance) - 1) != 0)
    ObserverHealthy = 0;
  Status =
      Image.Binary->ReadAt(Image.Binary->Context, Frame->Task, Image.Builder, 0,
                           (NevercMutableByteView){&Byte, 1});
  if (Status.Code != NEVERC_STATUS_OK)
    ObserverHealthy = 0;
  Status = Image.Binary->Tell(Image.Binary->Context, Frame->Task, Image.Builder,
                              &Position);
  if (Status.Code != NEVERC_STATUS_OK || Position != Image.Size)
    ObserverHealthy = 0;
  Status = Image.Binary->Reserve(Image.Binary->Context, Frame->Task,
                                 Image.Builder, 1);
  expect_policy_violation(Status);
  Status = Image.Binary->Write(Image.Binary->Context, Frame->Task,
                               Image.Builder, (NevercByteView){&Byte, 1});
  expect_policy_violation(Status);
  Status = Image.Binary->WriteAt(Image.Binary->Context, Frame->Task,
                                 Image.Builder, 0, (NevercByteView){&Byte, 1});
  expect_policy_violation(Status);
  Status = Image.Binary->Insert(Image.Binary->Context, Frame->Task,
                                Image.Builder, 0, (NevercByteView){&Byte, 1});
  expect_policy_violation(Status);
  Status = Image.Binary->Append(Image.Binary->Context, Frame->Task,
                                Image.Builder, (NevercByteView){&Byte, 1});
  expect_policy_violation(Status);
  Status = Image.Binary->Resize(Image.Binary->Context, Frame->Task,
                                Image.Builder, Image.Size);
  expect_policy_violation(Status);
  ++ImageObserverCalls;
  return ObserverHealthy ? neverc_status_ok()
                         : status_code(NEVERC_STATUS_PLUGIN_FAILURE);
}

#ifdef NEVERC_TEST_WITH_INTERCEPTOR
static NevercStatus
    NEVERC_CALL intercept_image(const NevercPhaseFrame *Frame,
                                NevercPhaseContinuation *Continuation,
                                NevercPhaseResult *OutResult, void *UserData) {
  NevercPhaseResult Downstream;
  NevercStatus Status;
  (void)UserData;

  if (Frame == NULL || Continuation == NULL || OutResult == NULL)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  memset(&Downstream, 0, sizeof(Downstream));
  Downstream.Header.StructSize = sizeof(Downstream);
  Downstream.Header.Major = NEVERC_PLUGIN_ABI_MAJOR;
  Downstream.Header.Minor = NEVERC_PLUGIN_ABI_MINOR;
  Status = Continuation->InvokeNext(Continuation, Frame, &Downstream);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  memset(OutResult, 0, sizeof(*OutResult));
  OutResult->Header.StructSize = sizeof(*OutResult);
  OutResult->Header.Major = NEVERC_PLUGIN_ABI_MAJOR;
  OutResult->Header.Minor = NEVERC_PLUGIN_ABI_MINOR;
  OutResult->Action = NEVERC_PHASE_CONTINUE;
  return neverc_status_ok();
}
#endif

#ifdef NEVERC_TEST_WITH_PROVIDER
static NevercStatus NEVERC_CALL provide_graph(const NevercPhaseFrame *Frame,
                                              NevercPhaseResult *OutResult,
                                              void *UserData) {
  (void)Frame;
  (void)OutResult;
  (void)UserData;
  return status_code(NEVERC_STATUS_PLUGIN_FAILURE);
}
#endif

static NevercStatus
    NEVERC_CALL register_plugin(const NevercCoreAPI *Core,
                                const NevercRegistrarAPI *Registrar,
                                void *RegistrarContext, void *ProcessState) {
  const void *Table = NULL;
  uint16_t Minor = 0;
  uint64_t StructSize = 0;
  NevercObserverDescriptor Observer;
  NevercStatus Status;
  (void)ProcessState;

  if (Core == NULL || Registrar == NULL || Registrar->RegisterObserver == NULL)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  Status = Core->QueryInterface(
      Core->Context,
      (NevercInterfaceID){NEVERC_INTERFACE_OBJECT_PHASE_HIGH,
                          NEVERC_INTERFACE_OBJECT_PHASE_LOW},
      NEVERC_OBJECT_PHASE_API_MAJOR, NEVERC_OBJECT_PHASE_API_MINOR, &Table,
      &Minor, &StructSize);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Table == NULL || StructSize < sizeof(NevercObjectPhaseAPI))
    return status_code(NEVERC_STATUS_ABI_MISMATCH);
  ObjectPhase = (const NevercObjectPhaseAPI *)Table;
  GraphObserverCalls = 0;
  ImageObserverCalls = 0;
  ObserverHealthy = 1;

  memset(&Observer, 0, sizeof(Observer));
  Observer.Header.StructSize = sizeof(Observer);
  Observer.Header.Major = NEVERC_PLUGIN_ABI_MAJOR;
  Observer.Header.Minor = NEVERC_PLUGIN_ABI_MINOR;
  Observer.Phase.High = NEVERC_PHASE_OBJECT_PRE_WRITE_HIGH;
  Observer.Phase.Low = NEVERC_PHASE_OBJECT_PRE_WRITE_LOW;
  Observer.Points = NEVERC_OBSERVER_BEFORE;
  Observer.Callback = observe_graph;
  Status = Registrar->RegisterObserver(RegistrarContext, &Observer);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  Observer.Phase.High = NEVERC_PHASE_OBJECT_POST_WRITE_HIGH;
  Observer.Phase.Low = NEVERC_PHASE_OBJECT_POST_WRITE_LOW;
  Observer.Points = NEVERC_OBSERVER_AFTER;
  Observer.Callback = observe_image;
  Status = Registrar->RegisterObserver(RegistrarContext, &Observer);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

#ifdef NEVERC_TEST_WITH_INTERCEPTOR
  {
    NevercInterceptorDescriptor Interceptor;
    if (Registrar->RegisterInterceptor == NULL)
      return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
    memset(&Interceptor, 0, sizeof(Interceptor));
    Interceptor.Header.StructSize = sizeof(Interceptor);
    Interceptor.Header.Major = NEVERC_PLUGIN_ABI_MAJOR;
    Interceptor.Header.Minor = NEVERC_PLUGIN_ABI_MINOR;
    Interceptor.Phase.High = NEVERC_PHASE_OBJECT_POST_WRITE_HIGH;
    Interceptor.Phase.Low = NEVERC_PHASE_OBJECT_POST_WRITE_LOW;
    Interceptor.Callback = intercept_image;
    Status = Registrar->RegisterInterceptor(RegistrarContext, &Interceptor);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
  }
#endif
#ifdef NEVERC_TEST_WITH_PROVIDER
  {
    NevercProviderDescriptor Provider;
    if (Registrar->RegisterProvider == NULL)
      return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
    memset(&Provider, 0, sizeof(Provider));
    Provider.Header.StructSize = sizeof(Provider);
    Provider.Header.Major = NEVERC_PLUGIN_ABI_MAJOR;
    Provider.Header.Minor = NEVERC_PLUGIN_ABI_MINOR;
    Provider.Phase.High = NEVERC_PHASE_OBJECT_PRE_WRITE_HIGH;
    Provider.Phase.Low = NEVERC_PHASE_OBJECT_PRE_WRITE_LOW;
    Provider.ProviderID =
        (NevercStringView)STRING_VIEW("test.object.pre-write.provider");
    Provider.Route.Header.StructSize = sizeof(Provider.Route);
    Provider.Route.Header.Major = NEVERC_PLUGIN_ABI_MAJOR;
    Provider.Route.Header.Minor = NEVERC_PLUGIN_ABI_MINOR;
    Provider.Deterministic = NEVERC_TRUE;
    Provider.Callback = provide_graph;
    Status = Registrar->RegisterProvider(RegistrarContext, &Provider);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
  }
#endif
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL destroy_plugin(const NevercCoreAPI *Core,
                                               void *ProcessState) {
  (void)Core;
  (void)ProcessState;
#if defined(NEVERC_TEST_WITH_INTERCEPTOR) || defined(NEVERC_TEST_WITH_PROVIDER)
  return neverc_status_ok();
#else
  return ObserverHealthy && GraphObserverCalls != 0 && ImageObserverCalls != 0
             ? neverc_status_ok()
             : status_code(NEVERC_STATUS_PLUGIN_FAILURE);
#endif
}

NEVERC_EXPORT NevercStatus NEVERC_CALL neverc_plugin_entry(
    const NevercBootstrapAPI *Bootstrap, NevercPluginDescriptor *OutPlugin) {
  NevercPluginDescriptor Descriptor;
  uint32_t Capacity;
  size_t BytesToWrite;

  if (Bootstrap == NULL || OutPlugin == NULL ||
      OutPlugin->Header.StructSize < sizeof(uint32_t))
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  Capacity = OutPlugin->Header.StructSize;
  memset(&Descriptor, 0, sizeof(Descriptor));
  Descriptor.Header.StructSize = sizeof(Descriptor);
  Descriptor.Header.Major = NEVERC_PLUGIN_ABI_MAJOR;
  Descriptor.Header.Minor = NEVERC_PLUGIN_ABI_MINOR;
#ifdef NEVERC_TEST_WITH_INTERCEPTOR
  Descriptor.PluginID = (NevercStringView)STRING_VIEW(
      "org.neverc.test.object-observer-interceptor");
  Descriptor.DisplayName = (NevercStringView)STRING_VIEW(
      "NeverC Object Observer and Interceptor Test");
#elif defined(NEVERC_TEST_WITH_PROVIDER)
  Descriptor.PluginID =
      (NevercStringView)STRING_VIEW("org.neverc.test.object-observer-provider");
  Descriptor.DisplayName =
      (NevercStringView)STRING_VIEW("NeverC Object Observer and Provider Test");
#else
  Descriptor.PluginID =
      (NevercStringView)STRING_VIEW("org.neverc.test.object-observer");
  Descriptor.DisplayName =
      (NevercStringView)STRING_VIEW("NeverC Object Observer Test");
#endif
  Descriptor.Version.Major = 1;
  Descriptor.Concurrency = NEVERC_CONCURRENCY_PROCESS_SERIAL;
  Descriptor.Reentrancy = NEVERC_REENTRANCY_NONE;
  Descriptor.Register = register_plugin;
  Descriptor.Destroy = destroy_plugin;

  BytesToWrite = Capacity < sizeof(Descriptor) ? Capacity : sizeof(Descriptor);
  memcpy(OutPlugin, &Descriptor, BytesToWrite);
  return neverc_status_ok();
}
