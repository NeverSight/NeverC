#include "neverc/Plugin/PluginObject.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <thread>

#define STRING_VIEW(Value) {(Value), (uint64_t)(sizeof(Value) - 1)}

namespace {

const NevercObjectPhaseAPI *ObjectPhase = nullptr;
const NevercObjectAPI *CachedObject = nullptr;
NevercObjectGraphHandle CachedGraph{};
NevercObjectMutationHandle CachedMutation{};
const NevercMutableBinaryAPI *CachedBinary = nullptr;
NevercMutableBinaryBuilderHandle CachedBuilder{};
uint64_t CachedImageSize = 0;
std::atomic<bool> Healthy{true};
std::atomic<uint64_t> ExpectationCalls{0};
std::atomic<uint64_t> FirstFailure{0};
std::atomic<uint64_t> GraphInterceptorCalls{0};
std::atomic<uint64_t> GraphObserverCalls{0};
std::atomic<uint64_t> ImageInterceptorCalls{0};
std::atomic<uint64_t> ImageObserverCalls{0};

NevercStatus statusCode(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

void expectCode(NevercStatus Status, NevercStatusCode Expected) {
  const uint64_t Index =
      ExpectationCalls.fetch_add(1, std::memory_order_relaxed) + 1;
  if (Status.Code != Expected) {
    Healthy.store(false, std::memory_order_relaxed);
    const uint64_t Detail =
        (Index << 32) | (static_cast<uint64_t>(Expected) << 16) | Status.Code;
    uint64_t Empty = 0;
    FirstFailure.compare_exchange_strong(Empty, Detail,
                                         std::memory_order_relaxed);
  }
}

NevercStatus failureStatus() {
  NevercStatus Status = statusCode(NEVERC_STATUS_PLUGIN_FAILURE);
  Status.Detail = FirstFailure.load(std::memory_order_relaxed);
  return Status;
}

NevercStatus continuePhase(const NevercPhaseFrame *Frame,
                           NevercPhaseContinuation *Continuation,
                           NevercPhaseResult *OutResult) {
  NevercPhaseResult Downstream{};
  Downstream.Header = {sizeof(Downstream), NEVERC_PLUGIN_ABI_MAJOR,
                       NEVERC_PLUGIN_ABI_MINOR, 0};
  NevercStatus Status =
      Continuation->InvokeNext(Continuation, Frame, &Downstream);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  std::memset(OutResult, 0, sizeof(*OutResult));
  OutResult->Header = {sizeof(*OutResult), NEVERC_PLUGIN_ABI_MAJOR,
                       NEVERC_PLUGIN_ABI_MINOR, 0};
  OutResult->Action = NEVERC_PHASE_CONTINUE;
  return neverc_status_ok();
}

void exerciseGraphRead(const NevercObjectAPI *Object, NevercTaskHandle Task,
                       NevercObjectGraphHandle Graph) {
  NevercObjectGraphInfo Info{};
  Info.Header = {sizeof(Info), NEVERC_OBJECT_API_MAJOR, NEVERC_OBJECT_API_MINOR,
                 0};
  expectCode(Object->GetGraphInfo(Object->Context, Task, Graph, &Info),
             NEVERC_STATUS_OK);
}

void exerciseGraphMutation(const NevercObjectAPI *Object, NevercTaskHandle Task,
                           NevercObjectGraphHandle Graph,
                           NevercStatusCode Expected) {
  NevercObjectMutationHandle Mutation{};
  NevercStatus Status =
      Object->BeginMutation(Object->Context, Task, Graph, &Mutation);
  expectCode(Status, Expected);
  if (Status.Code == NEVERC_STATUS_OK)
    expectCode(Object->AbandonMutation(Object->Context, Task, Mutation),
               NEVERC_STATUS_OK);
}

NevercObjectSectionDescriptor testSectionDescriptor() {
  static constexpr char Name[] = ".capability-cache";
  NevercObjectSectionDescriptor Descriptor{};
  Descriptor.Header = {sizeof(Descriptor), NEVERC_OBJECT_API_MAJOR,
                       NEVERC_OBJECT_API_MINOR, 0};
  Descriptor.Name = NevercStringView{Name, sizeof(Name) - 1};
  Descriptor.Kind = NEVERC_OBJECT_SECTION_KIND_DATA;
  Descriptor.Alignment = 1;
  return Descriptor;
}

void exerciseAllGraphMutations(const NevercObjectAPI *Object,
                               NevercTaskHandle Task,
                               NevercObjectGraphHandle Graph,
                               NevercObjectMutationHandle Mutation,
                               NevercStatusCode Expected) {
  NevercObjectMutationHandle NewMutation{};
  NevercObjectSectionHandle Section{};
  NevercObjectSymbolHandle Symbol{};
  NevercObjectRelocationHandle Relocation{};
  NevercObjectComdatHandle Comdat{};
  NevercObjectSectionDescriptor SectionDescriptor = testSectionDescriptor();
  NevercObjectSymbolDescriptor SymbolDescriptor{};
  SymbolDescriptor.Header = {sizeof(SymbolDescriptor), NEVERC_OBJECT_API_MAJOR,
                             NEVERC_OBJECT_API_MINOR, 0};
  NevercObjectRelocationDescriptor RelocationDescriptor{};
  RelocationDescriptor.Header = {sizeof(RelocationDescriptor),
                                 NEVERC_OBJECT_API_MAJOR,
                                 NEVERC_OBJECT_API_MINOR, 0};
  NevercObjectComdatDescriptor ComdatDescriptor{};
  ComdatDescriptor.Header = {sizeof(ComdatDescriptor), NEVERC_OBJECT_API_MAJOR,
                             NEVERC_OBJECT_API_MINOR, 0};

  expectCode(Object->BeginMutation(Object->Context, Task, Graph, &NewMutation),
             Expected);
  expectCode(Object->CreateSection(Object->Context, Task, Mutation,
                                   &SectionDescriptor, &Section),
             Expected);
  expectCode(Object->ReplaceSection(Object->Context, Task, Mutation, Section,
                                    &SectionDescriptor),
             Expected);
  expectCode(
      Object->MoveSectionBefore(Object->Context, Task, Mutation, Section, {}),
      Expected);
  expectCode(Object->EraseSection(Object->Context, Task, Mutation, Section),
             Expected);
  expectCode(Object->CreateSymbol(Object->Context, Task, Mutation,
                                  &SymbolDescriptor, &Symbol),
             Expected);
  expectCode(Object->ReplaceSymbol(Object->Context, Task, Mutation, Symbol,
                                   &SymbolDescriptor),
             Expected);
  expectCode(
      Object->MoveSymbolBefore(Object->Context, Task, Mutation, Symbol, {}),
      Expected);
  expectCode(Object->EraseSymbol(Object->Context, Task, Mutation, Symbol),
             Expected);
  expectCode(Object->CreateRelocation(Object->Context, Task, Mutation,
                                      &RelocationDescriptor, &Relocation),
             Expected);
  expectCode(Object->ReplaceRelocation(Object->Context, Task, Mutation,
                                       Relocation, &RelocationDescriptor),
             Expected);
  expectCode(Object->MoveRelocationBefore(Object->Context, Task, Mutation,
                                          Relocation, {}),
             Expected);
  expectCode(
      Object->EraseRelocation(Object->Context, Task, Mutation, Relocation),
      Expected);
  expectCode(Object->CreateComdat(Object->Context, Task, Mutation,
                                  &ComdatDescriptor, &Comdat),
             Expected);
  expectCode(Object->ReplaceComdat(Object->Context, Task, Mutation, Comdat,
                                   &ComdatDescriptor),
             Expected);
  expectCode(
      Object->MoveComdatBefore(Object->Context, Task, Mutation, Comdat, {}),
      Expected);
  expectCode(Object->EraseComdat(Object->Context, Task, Mutation, Comdat),
             Expected);
  expectCode(Object->CommitMutation(Object->Context, Task, Mutation), Expected);
  expectCode(Object->AbandonMutation(Object->Context, Task, Mutation),
             Expected);
}

void exerciseBinaryReads(const NevercMutableBinaryAPI *Binary,
                         NevercTaskHandle Task,
                         NevercMutableBinaryBuilderHandle Builder,
                         uint64_t ExpectedSize) {
  uint8_t Byte = 0;
  uint64_t Position = 0;
  expectCode(Binary->ReadAt(Binary->Context, Task, Builder, 0,
                            NevercMutableByteView{&Byte, 1}),
             NEVERC_STATUS_OK);
  expectCode(Binary->Tell(Binary->Context, Task, Builder, &Position),
             NEVERC_STATUS_OK);
  if (Position != ExpectedSize)
    Healthy.store(false, std::memory_order_relaxed);
}

void exerciseAllBinaryMutations(const NevercMutableBinaryAPI *Binary,
                                NevercTaskHandle Task,
                                NevercMutableBinaryBuilderHandle Builder,
                                uint64_t Size, NevercStatusCode Expected) {
  const NevercByteView Empty{nullptr, 0};
  expectCode(Binary->Reserve(Binary->Context, Task, Builder, 0), Expected);
  expectCode(Binary->Write(Binary->Context, Task, Builder, Empty), Expected);
  expectCode(Binary->WriteAt(Binary->Context, Task, Builder, 0, Empty),
             Expected);
  expectCode(Binary->Insert(Binary->Context, Task, Builder, 0, Empty),
             Expected);
  expectCode(Binary->Append(Binary->Context, Task, Builder, Empty), Expected);
  expectCode(Binary->Resize(Binary->Context, Task, Builder, Size), Expected);
}

NevercStatus NEVERC_CALL interceptGraph(const NevercPhaseFrame *Frame,
                                        NevercPhaseContinuation *Continuation,
                                        NevercPhaseResult *OutResult, void *) {
  if (!Frame || !Continuation || !OutResult || !ObjectPhase)
    return statusCode(NEVERC_STATUS_INVALID_ARGUMENT);
  NevercObjectPhaseGraphInfo Info{};
  Info.Header = {sizeof(Info), NEVERC_OBJECT_PHASE_API_MAJOR,
                 NEVERC_OBJECT_PHASE_API_MINOR, 0};
  NevercStatus Status =
      ObjectPhase->GetGraph(ObjectPhase->Context, Frame, Frame->Input, &Info);
  if (Status.Code != NEVERC_STATUS_OK || !Info.Object ||
      neverc_handle_is_null(Info.Graph))
    return Status.Code == NEVERC_STATUS_OK
               ? statusCode(NEVERC_STATUS_INVALID_STATE)
               : Status;
  CachedObject = Info.Object;
  CachedGraph = Info.Graph;
  exerciseGraphRead(CachedObject, Frame->Task, CachedGraph);
  exerciseGraphMutation(CachedObject, Frame->Task, CachedGraph,
                        NEVERC_STATUS_OK);
  // Ending a mutation intentionally invalidates entity and graph handles.
  // Reacquire the graph while the same callback capability is still active;
  // this is the capability/handle pair cached for the adversarial call.
  Info = {};
  Info.Header = {sizeof(Info), NEVERC_OBJECT_PHASE_API_MAJOR,
                 NEVERC_OBJECT_PHASE_API_MINOR, 0};
  Status =
      ObjectPhase->GetGraph(ObjectPhase->Context, Frame, Frame->Input, &Info);
  if (Status.Code != NEVERC_STATUS_OK || !Info.Object ||
      neverc_handle_is_null(Info.Graph))
    return Status.Code == NEVERC_STATUS_OK
               ? statusCode(NEVERC_STATUS_INVALID_STATE)
               : Status;
  CachedObject = Info.Object;
  CachedGraph = Info.Graph;
  CachedMutation = {};
  Status = CachedObject->BeginMutation(CachedObject->Context, Frame->Task,
                                       CachedGraph, &CachedMutation);
  if (Status.Code != NEVERC_STATUS_OK || neverc_handle_is_null(CachedMutation))
    return Status.Code == NEVERC_STATUS_OK
               ? statusCode(NEVERC_STATUS_INVALID_STATE)
               : Status;
  NevercObjectSectionDescriptor SectionDescriptor = testSectionDescriptor();
  NevercObjectSectionHandle Section{};
  expectCode(CachedObject->CreateSection(CachedObject->Context, Frame->Task,
                                         CachedMutation, &SectionDescriptor,
                                         &Section),
             NEVERC_STATUS_OK);
#ifdef NEVERC_TEST_CROSS_THREAD
  std::thread Worker([Task = Frame->Task] {
    exerciseAllGraphMutations(CachedObject, Task, CachedGraph, CachedMutation,
                              NEVERC_STATUS_POLICY_VIOLATION);
  });
  Worker.join();
#endif
  GraphInterceptorCalls.fetch_add(1, std::memory_order_relaxed);
  return continuePhase(Frame, Continuation, OutResult);
}

NevercStatus NEVERC_CALL observeGraph(const NevercPhaseFrame *Frame,
                                      NevercObserverPoint Point, void *) {
  if (!Frame || Point != NEVERC_OBSERVER_AFTER || !CachedObject ||
      neverc_handle_is_null(CachedGraph))
    return statusCode(NEVERC_STATUS_INVALID_STATE);
  NevercObjectPhaseGraphInfo ObserverView{};
  ObserverView.Header = {sizeof(ObserverView), NEVERC_OBJECT_PHASE_API_MAJOR,
                         NEVERC_OBJECT_PHASE_API_MINOR, 0};
  NevercStatus Status = ObjectPhase->GetGraph(
      ObjectPhase->Context, Frame, Frame->CurrentOutput, &ObserverView);
  if (Status.Code != NEVERC_STATUS_OK || !ObserverView.Object ||
      neverc_handle_is_null(ObserverView.Graph))
    return Status.Code == NEVERC_STATUS_OK
               ? statusCode(NEVERC_STATUS_INVALID_STATE)
               : Status;
  exerciseGraphRead(ObserverView.Object, Frame->Task, ObserverView.Graph);
  exerciseGraphMutation(ObserverView.Object, Frame->Task, ObserverView.Graph,
                        NEVERC_STATUS_POLICY_VIOLATION);
  // Acquiring the Observer facade must not destroy the capability facade
  // cached by the preceding Interceptor.
  exerciseGraphRead(CachedObject, Frame->Task, CachedGraph);
#ifndef NEVERC_TEST_CROSS_THREAD
  exerciseAllGraphMutations(CachedObject, Frame->Task, CachedGraph,
                            CachedMutation, NEVERC_STATUS_POLICY_VIOLATION);
#else
  std::thread Worker([Task = Frame->Task] {
    exerciseAllGraphMutations(CachedObject, Task, CachedGraph, CachedMutation,
                              NEVERC_STATUS_POLICY_VIOLATION);
  });
  Worker.join();
#endif
  GraphObserverCalls.fetch_add(1, std::memory_order_relaxed);
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL interceptImage(const NevercPhaseFrame *Frame,
                                        NevercPhaseContinuation *Continuation,
                                        NevercPhaseResult *OutResult, void *) {
  if (!Frame || !Continuation || !OutResult || !ObjectPhase)
    return statusCode(NEVERC_STATUS_INVALID_ARGUMENT);
  NevercObjectImageInfo Info{};
  Info.Header = {sizeof(Info), NEVERC_OBJECT_PHASE_API_MAJOR,
                 NEVERC_OBJECT_PHASE_API_MINOR, 0};
  NevercStatus Status =
      ObjectPhase->GetImage(ObjectPhase->Context, Frame, Frame->Input, &Info);
  if (Status.Code != NEVERC_STATUS_OK || !Info.Binary ||
      neverc_handle_is_null(Info.Builder) || Info.Size == 0)
    return Status.Code == NEVERC_STATUS_OK
               ? statusCode(NEVERC_STATUS_INVALID_STATE)
               : Status;
  CachedBinary = Info.Binary;
  CachedBuilder = Info.Builder;
  CachedImageSize = Info.Size;
  exerciseBinaryReads(CachedBinary, Frame->Task, CachedBuilder,
                      CachedImageSize);
  exerciseAllBinaryMutations(CachedBinary, Frame->Task, CachedBuilder,
                             CachedImageSize, NEVERC_STATUS_OK);
#ifdef NEVERC_TEST_CROSS_THREAD
  std::thread Worker([Task = Frame->Task] {
    exerciseAllBinaryMutations(CachedBinary, Task, CachedBuilder,
                               CachedImageSize, NEVERC_STATUS_POLICY_VIOLATION);
  });
  Worker.join();
#endif
  ImageInterceptorCalls.fetch_add(1, std::memory_order_relaxed);
  return continuePhase(Frame, Continuation, OutResult);
}

NevercStatus NEVERC_CALL observeImage(const NevercPhaseFrame *Frame,
                                      NevercObserverPoint Point, void *) {
  if (!Frame || Point != NEVERC_OBSERVER_AFTER || !CachedBinary ||
      neverc_handle_is_null(CachedBuilder))
    return statusCode(NEVERC_STATUS_INVALID_STATE);
  exerciseBinaryReads(CachedBinary, Frame->Task, CachedBuilder,
                      CachedImageSize);
#ifndef NEVERC_TEST_CROSS_THREAD
  exerciseAllBinaryMutations(CachedBinary, Frame->Task, CachedBuilder,
                             CachedImageSize, NEVERC_STATUS_POLICY_VIOLATION);
#else
  std::thread Worker([Task = Frame->Task] {
    exerciseAllBinaryMutations(CachedBinary, Task, CachedBuilder,
                               CachedImageSize, NEVERC_STATUS_POLICY_VIOLATION);
  });
  Worker.join();
#endif
  ImageObserverCalls.fetch_add(1, std::memory_order_relaxed);
  const bool AllCallbacksRan =
      GraphInterceptorCalls.load(std::memory_order_relaxed) != 0 &&
      GraphObserverCalls.load(std::memory_order_relaxed) != 0 &&
      ImageInterceptorCalls.load(std::memory_order_relaxed) != 0 &&
      ImageObserverCalls.load(std::memory_order_relaxed) != 0;
  return Healthy.load(std::memory_order_relaxed) && AllCallbacksRan
             ? neverc_status_ok()
             : failureStatus();
}

NevercStatus NEVERC_CALL observeCachedGraphInLaterPhase(
    const NevercPhaseFrame *Frame, NevercObserverPoint Point, void *) {
  if (!Frame || Point != NEVERC_OBSERVER_BEFORE || !CachedObject ||
      neverc_handle_is_null(CachedGraph) ||
      neverc_handle_is_null(CachedMutation))
    return statusCode(NEVERC_STATUS_INVALID_STATE);
  exerciseAllGraphMutations(CachedObject, Frame->Task, CachedGraph,
                            CachedMutation, NEVERC_STATUS_POLICY_VIOLATION);
  return Healthy.load(std::memory_order_relaxed) ? neverc_status_ok()
                                                 : failureStatus();
}

NevercStatus NEVERC_CALL observeCachedBinaryAfterFinish(
    const NevercPhaseFrame *Frame, NevercObserverPoint Point, void *) {
  if (!Frame || Point != NEVERC_OBSERVER_BEFORE || !CachedBinary ||
      neverc_handle_is_null(CachedBuilder))
    return statusCode(NEVERC_STATUS_INVALID_STATE);
  exerciseAllBinaryMutations(CachedBinary, Frame->Task, CachedBuilder,
                             CachedImageSize, NEVERC_STATUS_POLICY_VIOLATION);
  return Healthy.load(std::memory_order_relaxed) ? neverc_status_ok()
                                                 : failureStatus();
}

NevercStatus NEVERC_CALL registerPlugin(const NevercCoreAPI *Core,
                                        const NevercRegistrarAPI *Registrar,
                                        void *RegistrarContext, void *) {
  if (!Core || !Registrar || !Registrar->RegisterInterceptor ||
      !Registrar->RegisterObserver)
    return statusCode(NEVERC_STATUS_INVALID_ARGUMENT);
  const void *Table = nullptr;
  uint16_t Minor = 0;
  uint64_t StructSize = 0;
  NevercStatus Status = Core->QueryInterface(
      Core->Context,
      NevercInterfaceID{NEVERC_INTERFACE_OBJECT_PHASE_HIGH,
                        NEVERC_INTERFACE_OBJECT_PHASE_LOW},
      NEVERC_OBJECT_PHASE_API_MAJOR, NEVERC_OBJECT_PHASE_API_MINOR, &Table,
      &Minor, &StructSize);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!Table || StructSize < sizeof(NevercObjectPhaseAPI))
    return statusCode(NEVERC_STATUS_ABI_MISMATCH);
  ObjectPhase = static_cast<const NevercObjectPhaseAPI *>(Table);
  CachedObject = nullptr;
  CachedGraph = {};
  CachedMutation = {};
  CachedBinary = nullptr;
  CachedBuilder = {};
  CachedImageSize = 0;
  Healthy.store(true, std::memory_order_relaxed);
  ExpectationCalls.store(0, std::memory_order_relaxed);
  FirstFailure.store(0, std::memory_order_relaxed);
  GraphInterceptorCalls.store(0, std::memory_order_relaxed);
  GraphObserverCalls.store(0, std::memory_order_relaxed);
  ImageInterceptorCalls.store(0, std::memory_order_relaxed);
  ImageObserverCalls.store(0, std::memory_order_relaxed);

  NevercInterceptorDescriptor Interceptor{};
  Interceptor.Header = {sizeof(Interceptor), NEVERC_PLUGIN_ABI_MAJOR,
                        NEVERC_PLUGIN_ABI_MINOR, 0};
#if defined(NEVERC_TEST_GRAPH_CROSS_PHASE)
  Interceptor.Phase = {NEVERC_PHASE_OBJECT_PRE_WRITE_HIGH,
                       NEVERC_PHASE_OBJECT_PRE_WRITE_LOW};
  Interceptor.Callback = interceptGraph;
  Status = Registrar->RegisterInterceptor(RegistrarContext, &Interceptor);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  NevercObserverDescriptor Observer{};
  Observer.Header = {sizeof(Observer), NEVERC_PLUGIN_ABI_MAJOR,
                     NEVERC_PLUGIN_ABI_MINOR, 0};
  Observer.Phase = {NEVERC_PHASE_OBJECT_POST_LAYOUT_HIGH,
                    NEVERC_PHASE_OBJECT_POST_LAYOUT_LOW};
  Observer.Points = NEVERC_OBSERVER_BEFORE;
  Observer.Callback = observeCachedGraphInLaterPhase;
  return Registrar->RegisterObserver(RegistrarContext, &Observer);
#elif defined(NEVERC_TEST_BINARY_CROSS_PHASE)
  Interceptor.Phase = {NEVERC_PHASE_OBJECT_POST_WRITE_HIGH,
                       NEVERC_PHASE_OBJECT_POST_WRITE_LOW};
  Interceptor.Callback = interceptImage;
  Status = Registrar->RegisterInterceptor(RegistrarContext, &Interceptor);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  NevercObserverDescriptor Observer{};
  Observer.Header = {sizeof(Observer), NEVERC_PLUGIN_ABI_MAJOR,
                     NEVERC_PLUGIN_ABI_MINOR, 0};
  Observer.Phase = {NEVERC_PHASE_OBJECT_FINAL_VERIFY_HIGH,
                    NEVERC_PHASE_OBJECT_FINAL_VERIFY_LOW};
  Observer.Points = NEVERC_OBSERVER_BEFORE;
  Observer.Callback = observeCachedBinaryAfterFinish;
  return Registrar->RegisterObserver(RegistrarContext, &Observer);
#else
  Interceptor.Phase = {NEVERC_PHASE_OBJECT_PRE_WRITE_HIGH,
                       NEVERC_PHASE_OBJECT_PRE_WRITE_LOW};
  Interceptor.Callback = interceptGraph;
  Status = Registrar->RegisterInterceptor(RegistrarContext, &Interceptor);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Interceptor.Phase = {NEVERC_PHASE_OBJECT_POST_WRITE_HIGH,
                       NEVERC_PHASE_OBJECT_POST_WRITE_LOW};
  Interceptor.Callback = interceptImage;
  Status = Registrar->RegisterInterceptor(RegistrarContext, &Interceptor);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  NevercObserverDescriptor Observer{};
  Observer.Header = {sizeof(Observer), NEVERC_PLUGIN_ABI_MAJOR,
                     NEVERC_PLUGIN_ABI_MINOR, 0};
  Observer.Phase = {NEVERC_PHASE_OBJECT_PRE_WRITE_HIGH,
                    NEVERC_PHASE_OBJECT_PRE_WRITE_LOW};
  Observer.Points = NEVERC_OBSERVER_AFTER;
  Observer.Callback = observeGraph;
  Status = Registrar->RegisterObserver(RegistrarContext, &Observer);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Observer.Phase = {NEVERC_PHASE_OBJECT_POST_WRITE_HIGH,
                    NEVERC_PHASE_OBJECT_POST_WRITE_LOW};
  Observer.Points = NEVERC_OBSERVER_AFTER;
  Observer.Callback = observeImage;
  Status = Registrar->RegisterObserver(RegistrarContext, &Observer);
  return Status;
#endif
}

NevercStatus NEVERC_CALL destroyPlugin(const NevercCoreAPI *, void *) {
  return neverc_status_ok();
}

} // namespace

extern "C" NEVERC_EXPORT NevercStatus NEVERC_CALL neverc_plugin_entry(
    const NevercBootstrapAPI *Bootstrap, NevercPluginDescriptor *OutPlugin) {
  if (!Bootstrap || !OutPlugin ||
      OutPlugin->Header.StructSize < sizeof(uint32_t))
    return statusCode(NEVERC_STATUS_INVALID_ARGUMENT);
  const uint32_t Capacity = OutPlugin->Header.StructSize;
  NevercPluginDescriptor Descriptor{};
  Descriptor.Header = {sizeof(Descriptor), NEVERC_PLUGIN_ABI_MAJOR,
                       NEVERC_PLUGIN_ABI_MINOR, 0};
#if defined(NEVERC_TEST_GRAPH_CROSS_PHASE)
  Descriptor.PluginID = NevercStringView STRING_VIEW(
      "org.neverc.test.object-capability-graph-cross-phase");
  Descriptor.DisplayName = NevercStringView STRING_VIEW(
      "NeverC Object Graph Cross-Phase Capability Cache Test");
#elif defined(NEVERC_TEST_BINARY_CROSS_PHASE)
  Descriptor.PluginID = NevercStringView STRING_VIEW(
      "org.neverc.test.object-capability-binary-cross-phase");
  Descriptor.DisplayName = NevercStringView STRING_VIEW(
      "NeverC Object Binary Cross-Phase Capability Cache Test");
#elif defined(NEVERC_TEST_CROSS_THREAD)
  Descriptor.PluginID =
      NevercStringView STRING_VIEW("org.neverc.test.object-capability-thread");
  Descriptor.DisplayName = NevercStringView STRING_VIEW(
      "NeverC Object Cross-Thread Capability Cache Test");
#else
  Descriptor.PluginID = NevercStringView STRING_VIEW(
      "org.neverc.test.object-capability-after-observer");
  Descriptor.DisplayName = NevercStringView STRING_VIEW(
      "NeverC Object AFTER Observer Capability Cache Test");
#endif
  Descriptor.Version.Major = 1;
  Descriptor.Concurrency = NEVERC_CONCURRENCY_PROCESS_SERIAL;
  Descriptor.Reentrancy = NEVERC_REENTRANCY_NONE;
  Descriptor.Register = registerPlugin;
  Descriptor.Destroy = destroyPlugin;
  const size_t BytesToWrite =
      Capacity < sizeof(Descriptor) ? Capacity : sizeof(Descriptor);
  std::memcpy(OutPlugin, &Descriptor, BytesToWrite);
  return neverc_status_ok();
}
