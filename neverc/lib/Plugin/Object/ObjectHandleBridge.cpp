#include "neverc/Plugin/Host/ObjectPluginBridge.h"
#include "neverc/Plugin/Host/PluginPhaseExecutor.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Error.h"
#include <cstring>
#include <new>

using namespace llvm;

namespace neverc::plugin {
namespace {

NevercStatus objectStatus(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

bool sameHandle(NevercHandle Left, NevercHandle Right) {
  return Left.Owner == Right.Owner && Left.Value == Right.Value;
}

PluginHandleKind handleKind(ObjectPluginBridge::EntityKind Kind) {
  switch (Kind) {
  case ObjectPluginBridge::EntityKind::Section:
    return PluginObjectSectionHandleKind;
  case ObjectPluginBridge::EntityKind::Symbol:
    return PluginObjectSymbolHandleKind;
  case ObjectPluginBridge::EntityKind::Relocation:
    return PluginObjectRelocationHandleKind;
  case ObjectPluginBridge::EntityKind::Comdat:
    return PluginObjectComdatHandleKind;
  }
  return 0;
}

ObjectPluginBridge::OwnerLease bridge(void *Context, NevercTaskHandle Task,
                                      NevercStatus &Status) {
  return ObjectPluginBridge::acquire(Context, Task, false, Status);
}

template <typename T> bool validRecord(const T *Value) {
  return Value && Value->Header.StructSize >= sizeof(T) &&
         Value->Header.Major == NEVERC_OBJECT_API_MAJOR &&
         Value->Header.Minor <= NEVERC_OBJECT_API_MINOR;
}

template <typename T> NevercStatus writeHandle(Expected<T> Handle, T *Output) {
  if (!Output) {
    if (!Handle)
      consumeError(Handle.takeError());
    return objectStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  }
  *Output = {};
  if (!Handle) {
    consumeError(Handle.takeError());
    return objectStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *Output = *Handle;
  return neverc_status_ok();
}

template <typename Storage, typename Wrap>
NevercStatus firstEntity(Storage &Values, Wrap WrapValue,
                         NevercHandle *Output) {
  if (!Output)
    return objectStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *Output = {};
  if (Values.empty())
    return objectStatus(NEVERC_STATUS_NOT_FOUND);
  return writeHandle(WrapValue(Values.front()), Output);
}

NevercStatus NEVERC_CALL GetGraphInfo(
    void *Context, NevercTaskHandle Task, NevercObjectGraphHandle Graph,
    NevercObjectGraphInfo *OutInfo) {
  NevercStatus Status;
  auto Bridge = bridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  if (!validRecord(OutInfo))
    return objectStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  PluginObjectGraph *Resolved = nullptr;
  Status = Bridge->resolveGraph(Graph, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  OutInfo->Target = Resolved->targetKey();
  OutInfo->ObjectSchemaDigest = {
      NEVERC_OBJECT_SCHEMA_DIGEST,
      std::strlen(NEVERC_OBJECT_SCHEMA_DIGEST)};
  OutInfo->Generation = Resolved->generation();
  OutInfo->SectionCount = Resolved->sectionCount();
  OutInfo->SymbolCount = Resolved->symbolCount();
  OutInfo->RelocationCount = Resolved->relocationCount();
  OutInfo->ComdatCount = Resolved->comdatCount();
  OutInfo->HasLayoutProof =
      Resolved->hasLayoutProof() ? NEVERC_TRUE : NEVERC_FALSE;
  std::memset(OutInfo->Reserved8, 0, sizeof(OutInfo->Reserved8));
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL GetFirstSection(
    void *Context, NevercTaskHandle Task, NevercObjectGraphHandle Graph,
    NevercObjectSectionHandle *OutSection) {
  NevercStatus Status;
  auto Bridge = bridge(Context, Task, Status);
  if (!Bridge || !OutSection)
    return Bridge ? objectStatus(NEVERC_STATUS_INVALID_ARGUMENT) : Status;
  PluginObjectGraph *Resolved = nullptr;
  Status = Bridge->resolveGraph(Graph, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  return firstEntity(
      Bridge->activeGraph().sections(),
      [Owner = &*Bridge](PluginObjectSection &Value) {
        return Owner->wrapSection(Value);
      },
      OutSection);
}

NevercStatus NEVERC_CALL GetNextSection(
    void *Context, NevercTaskHandle Task,
    NevercObjectSectionHandle Section,
    NevercObjectSectionHandle *OutSection) {
  NevercStatus Status;
  auto Bridge = bridge(Context, Task, Status);
  if (!Bridge || !OutSection)
    return Bridge ? objectStatus(NEVERC_STATUS_INVALID_ARGUMENT) : Status;
  *OutSection = {};
  PluginObjectSection *Resolved = nullptr;
  Status = Bridge->resolveSection(Section, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto &Values = Bridge->activeGraph().sections();
  auto It = llvm::find_if(
      Values, [Resolved](const auto &Value) { return &Value == Resolved; });
  if (It == Values.end() || ++It == Values.end())
    return objectStatus(NEVERC_STATUS_NOT_FOUND);
  return writeHandle(Bridge->wrapSection(*It), OutSection);
}

NevercStatus NEVERC_CALL GetSectionInfo(
    void *Context, NevercTaskHandle Task,
    NevercObjectSectionHandle Section,
    NevercObjectSectionInfo *OutInfo) {
  NevercStatus Status;
  auto Bridge = bridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  if (!validRecord(OutInfo))
    return objectStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  PluginObjectSection *Resolved = nullptr;
  Status = Bridge->resolveSection(Section, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  OutInfo->Name = {Resolved->Name.data(), Resolved->Name.size()};
  OutInfo->Kind = Resolved->Kind;
  OutInfo->Reserved = 0;
  OutInfo->Flags = Resolved->Flags;
  OutInfo->Alignment = Resolved->Alignment;
  OutInfo->Data = {Resolved->Data.data(), Resolved->Data.size()};
  OutInfo->ZeroFillSize = Resolved->ZeroFillSize;
  OutInfo->Comdat = {};
  if (Resolved->ComdatID != 0) {
    PluginObjectComdat *Comdat =
        Bridge->activeGraph().findComdat(Resolved->ComdatID);
    if (!Comdat)
      return objectStatus(NEVERC_STATUS_VERIFICATION_FAILED);
    auto Handle = Bridge->wrapComdat(*Comdat);
    if (!Handle) {
      consumeError(Handle.takeError());
      return objectStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    OutInfo->Comdat = *Handle;
  }
  OutInfo->ExtensionOwner = Resolved->Extension.Owner;
  OutInfo->ExtensionVersion = Resolved->Extension.Version;
  OutInfo->ReservedExtension = 0;
  OutInfo->Extension = {Resolved->Extension.Bytes.data(),
                        Resolved->Extension.Bytes.size()};
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL GetFirstSymbol(
    void *Context, NevercTaskHandle Task, NevercObjectGraphHandle Graph,
    NevercObjectSymbolHandle *OutSymbol) {
  NevercStatus Status;
  auto Bridge = bridge(Context, Task, Status);
  if (!Bridge || !OutSymbol)
    return Bridge ? objectStatus(NEVERC_STATUS_INVALID_ARGUMENT) : Status;
  PluginObjectGraph *Resolved = nullptr;
  Status = Bridge->resolveGraph(Graph, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  return firstEntity(
      Bridge->activeGraph().symbols(),
      [Owner = &*Bridge](PluginObjectSymbol &Value) {
        return Owner->wrapSymbol(Value);
      },
      OutSymbol);
}

NevercStatus NEVERC_CALL GetNextSymbol(
    void *Context, NevercTaskHandle Task, NevercObjectSymbolHandle Symbol,
    NevercObjectSymbolHandle *OutSymbol) {
  NevercStatus Status;
  auto Bridge = bridge(Context, Task, Status);
  if (!Bridge || !OutSymbol)
    return Bridge ? objectStatus(NEVERC_STATUS_INVALID_ARGUMENT) : Status;
  *OutSymbol = {};
  PluginObjectSymbol *Resolved = nullptr;
  Status = Bridge->resolveSymbol(Symbol, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto &Values = Bridge->activeGraph().symbols();
  auto It = llvm::find_if(
      Values, [Resolved](const auto &Value) { return &Value == Resolved; });
  if (It == Values.end() || ++It == Values.end())
    return objectStatus(NEVERC_STATUS_NOT_FOUND);
  return writeHandle(Bridge->wrapSymbol(*It), OutSymbol);
}

NevercStatus NEVERC_CALL GetSymbolInfo(
    void *Context, NevercTaskHandle Task, NevercObjectSymbolHandle Symbol,
    NevercObjectSymbolInfo *OutInfo) {
  NevercStatus Status;
  auto Bridge = bridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  if (!validRecord(OutInfo))
    return objectStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  PluginObjectSymbol *Resolved = nullptr;
  Status = Bridge->resolveSymbol(Symbol, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  OutInfo->Name = {Resolved->Name.data(), Resolved->Name.size()};
  OutInfo->Binding = Resolved->Binding;
  OutInfo->Visibility = Resolved->Visibility;
  OutInfo->Type = Resolved->Type;
  OutInfo->Definition = Resolved->Definition;
  OutInfo->Section = {};
  if (Resolved->SectionID != 0) {
    PluginObjectSection *Section =
        Bridge->activeGraph().findSection(Resolved->SectionID);
    if (!Section)
      return objectStatus(NEVERC_STATUS_VERIFICATION_FAILED);
    auto Handle = Bridge->wrapSection(*Section);
    if (!Handle) {
      consumeError(Handle.takeError());
      return objectStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    OutInfo->Section = *Handle;
  }
  OutInfo->Value = Resolved->Value;
  OutInfo->Size = Resolved->Size;
  OutInfo->Alignment = Resolved->Alignment;
  OutInfo->Comdat = {};
  if (Resolved->ComdatID != 0) {
    PluginObjectComdat *Comdat =
        Bridge->activeGraph().findComdat(Resolved->ComdatID);
    if (!Comdat)
      return objectStatus(NEVERC_STATUS_VERIFICATION_FAILED);
    auto Handle = Bridge->wrapComdat(*Comdat);
    if (!Handle) {
      consumeError(Handle.takeError());
      return objectStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    OutInfo->Comdat = *Handle;
  }
  OutInfo->Flags = Resolved->Flags;
  OutInfo->ExtensionOwner = Resolved->Extension.Owner;
  OutInfo->ExtensionVersion = Resolved->Extension.Version;
  OutInfo->Reserved = 0;
  OutInfo->Extension = {Resolved->Extension.Bytes.data(),
                        Resolved->Extension.Bytes.size()};
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL GetFirstRelocation(
    void *Context, NevercTaskHandle Task, NevercObjectGraphHandle Graph,
    NevercObjectRelocationHandle *OutRelocation) {
  NevercStatus Status;
  auto Bridge = bridge(Context, Task, Status);
  if (!Bridge || !OutRelocation)
    return Bridge ? objectStatus(NEVERC_STATUS_INVALID_ARGUMENT) : Status;
  PluginObjectGraph *Resolved = nullptr;
  Status = Bridge->resolveGraph(Graph, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  return firstEntity(
      Bridge->activeGraph().relocations(),
      [Owner = &*Bridge](PluginObjectRelocation &Value) {
        return Owner->wrapRelocation(Value);
      },
      OutRelocation);
}

NevercStatus NEVERC_CALL GetNextRelocation(
    void *Context, NevercTaskHandle Task,
    NevercObjectRelocationHandle Relocation,
    NevercObjectRelocationHandle *OutRelocation) {
  NevercStatus Status;
  auto Bridge = bridge(Context, Task, Status);
  if (!Bridge || !OutRelocation)
    return Bridge ? objectStatus(NEVERC_STATUS_INVALID_ARGUMENT) : Status;
  *OutRelocation = {};
  PluginObjectRelocation *Resolved = nullptr;
  Status = Bridge->resolveRelocation(Relocation, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto &Values = Bridge->activeGraph().relocations();
  auto It = llvm::find_if(
      Values, [Resolved](const auto &Value) { return &Value == Resolved; });
  if (It == Values.end() || ++It == Values.end())
    return objectStatus(NEVERC_STATUS_NOT_FOUND);
  return writeHandle(Bridge->wrapRelocation(*It), OutRelocation);
}

NevercStatus NEVERC_CALL GetRelocationInfo(
    void *Context, NevercTaskHandle Task,
    NevercObjectRelocationHandle Relocation,
    NevercObjectRelocationInfo *OutInfo) {
  NevercStatus Status;
  auto Bridge = bridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  if (!validRecord(OutInfo))
    return objectStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  PluginObjectRelocation *Resolved = nullptr;
  Status = Bridge->resolveRelocation(Relocation, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  PluginObjectSection *Section =
      Bridge->activeGraph().findSection(Resolved->SectionID);
  if (!Section)
    return objectStatus(NEVERC_STATUS_VERIFICATION_FAILED);
  auto SectionHandle = Bridge->wrapSection(*Section);
  if (!SectionHandle) {
    consumeError(SectionHandle.takeError());
    return objectStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  OutInfo->Section = *SectionHandle;
  OutInfo->Offset = Resolved->Offset;
  OutInfo->Kind = Resolved->Kind;
  OutInfo->TargetKind = Resolved->TargetKind;
  OutInfo->Width = Resolved->Width;
  OutInfo->IsPCRelative =
      Resolved->IsPCRelative ? NEVERC_TRUE : NEVERC_FALSE;
  OutInfo->IsSigned = Resolved->IsSigned ? NEVERC_TRUE : NEVERC_FALSE;
  std::memset(OutInfo->Reserved8, 0, sizeof(OutInfo->Reserved8));
  OutInfo->Addend = Resolved->Addend;
  OutInfo->TargetSymbol = {};
  if (Resolved->TargetSymbolID != 0) {
    PluginObjectSymbol *Symbol =
        Bridge->activeGraph().findSymbol(Resolved->TargetSymbolID);
    if (!Symbol)
      return objectStatus(NEVERC_STATUS_VERIFICATION_FAILED);
    auto Handle = Bridge->wrapSymbol(*Symbol);
    if (!Handle) {
      consumeError(Handle.takeError());
      return objectStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    OutInfo->TargetSymbol = *Handle;
  }
  OutInfo->TargetSection = {};
  if (Resolved->TargetSectionID != 0) {
    PluginObjectSection *Target =
        Bridge->activeGraph().findSection(Resolved->TargetSectionID);
    if (!Target)
      return objectStatus(NEVERC_STATUS_VERIFICATION_FAILED);
    auto Handle = Bridge->wrapSection(*Target);
    if (!Handle) {
      consumeError(Handle.takeError());
      return objectStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    OutInfo->TargetSection = *Handle;
  }
  OutInfo->TargetValue = Resolved->TargetValue;
  OutInfo->TargetExtensionKind = Resolved->TargetExtensionKind;
  OutInfo->Reserved = 0;
  OutInfo->ExtensionOwner = Resolved->Extension.Owner;
  OutInfo->ExtensionVersion = Resolved->Extension.Version;
  OutInfo->ReservedExtension = 0;
  OutInfo->Extension = {Resolved->Extension.Bytes.data(),
                        Resolved->Extension.Bytes.size()};
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL GetFirstComdat(
    void *Context, NevercTaskHandle Task, NevercObjectGraphHandle Graph,
    NevercObjectComdatHandle *OutComdat) {
  NevercStatus Status;
  auto Bridge = bridge(Context, Task, Status);
  if (!Bridge || !OutComdat)
    return Bridge ? objectStatus(NEVERC_STATUS_INVALID_ARGUMENT) : Status;
  PluginObjectGraph *Resolved = nullptr;
  Status = Bridge->resolveGraph(Graph, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  return firstEntity(
      Bridge->activeGraph().comdats(),
      [Owner = &*Bridge](PluginObjectComdat &Value) {
        return Owner->wrapComdat(Value);
      },
      OutComdat);
}

NevercStatus NEVERC_CALL GetNextComdat(
    void *Context, NevercTaskHandle Task, NevercObjectComdatHandle Comdat,
    NevercObjectComdatHandle *OutComdat) {
  NevercStatus Status;
  auto Bridge = bridge(Context, Task, Status);
  if (!Bridge || !OutComdat)
    return Bridge ? objectStatus(NEVERC_STATUS_INVALID_ARGUMENT) : Status;
  *OutComdat = {};
  PluginObjectComdat *Resolved = nullptr;
  Status = Bridge->resolveComdat(Comdat, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto &Values = Bridge->activeGraph().comdats();
  auto It = llvm::find_if(
      Values, [Resolved](const auto &Value) { return &Value == Resolved; });
  if (It == Values.end() || ++It == Values.end())
    return objectStatus(NEVERC_STATUS_NOT_FOUND);
  return writeHandle(Bridge->wrapComdat(*It), OutComdat);
}

NevercStatus NEVERC_CALL GetComdatInfo(
    void *Context, NevercTaskHandle Task, NevercObjectComdatHandle Comdat,
    NevercObjectComdatInfo *OutInfo) {
  NevercStatus Status;
  auto Bridge = bridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  if (!validRecord(OutInfo))
    return objectStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  PluginObjectComdat *Resolved = nullptr;
  Status = Bridge->resolveComdat(Comdat, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  OutInfo->Name = {Resolved->Name.data(), Resolved->Name.size()};
  OutInfo->Selection = Resolved->Selection;
  OutInfo->Reserved = 0;
  OutInfo->AssociatedComdat = {};
  if (Resolved->AssociatedComdatID != 0) {
    PluginObjectComdat *Associated =
        Bridge->activeGraph().findComdat(Resolved->AssociatedComdatID);
    if (!Associated)
      return objectStatus(NEVERC_STATUS_VERIFICATION_FAILED);
    auto Handle = Bridge->wrapComdat(*Associated);
    if (!Handle) {
      consumeError(Handle.takeError());
      return objectStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    OutInfo->AssociatedComdat = *Handle;
  }
  OutInfo->ExtensionOwner = Resolved->Extension.Owner;
  OutInfo->ExtensionVersion = Resolved->Extension.Version;
  OutInfo->ReservedExtension = 0;
  OutInfo->Extension = {Resolved->Extension.Bytes.data(),
                        Resolved->Extension.Bytes.size()};
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL GetLayoutProof(
    void *Context, NevercTaskHandle Task, NevercObjectGraphHandle Graph,
    NevercObjectLayoutProofHandle *OutProof) {
  NevercStatus Status;
  auto Bridge = bridge(Context, Task, Status);
  if (!Bridge || !OutProof)
    return Bridge ? objectStatus(NEVERC_STATUS_INVALID_ARGUMENT) : Status;
  PluginObjectGraph *Resolved = nullptr;
  Status = Bridge->resolveGraph(Graph, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!Resolved->hasLayoutProof()) {
    *OutProof = {};
    return objectStatus(NEVERC_STATUS_NOT_FOUND);
  }
  return writeHandle(Bridge->layoutProof(), OutProof);
}

NevercStatus NEVERC_CALL GetLayoutProofInfo(
    void *Context, NevercTaskHandle Task,
    NevercObjectLayoutProofHandle Proof,
    NevercObjectLayoutProofInfo *OutInfo) {
  NevercStatus Status;
  auto Bridge = bridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  if (!validRecord(OutInfo))
    return objectStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  const PluginObjectLayoutProof *Resolved = nullptr;
  Status = Bridge->resolveLayoutProof(Proof, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  OutInfo->GraphGeneration = Resolved->GraphGeneration;
  OutInfo->TargetID = Resolved->TargetID;
  OutInfo->FormatID = Resolved->FormatID;
  return neverc_status_ok();
}

} // namespace

ObjectPluginBridge::ObjectPluginBridge(PluginTaskContext &TaskValue,
                                       PluginObjectGraph &GraphValue,
                                       bool AllowMutation)
    : Task(TaskValue), Graph(GraphValue), MutationAllowed(AllowMutation) {
  Control = std::make_shared<OwnerControl>();
  Control->Owner = this;
  Facade = createFacade(AllowMutation);
  ReadOnlyFacade = AllowMutation ? createFacade(false) : Facade;
}

std::shared_ptr<ObjectPluginBridge::APIFacade>
ObjectPluginBridge::createFacade(bool AllowMutation, const void *Domain,
                                 uint64_t Token) {
  auto Result = std::make_shared<APIFacade>();
  Result->Task = &Task;
  Result->TaskHandle = Task.handle();
  Result->Control = Control;
  Result->MutationDomain = Domain;
  Result->Token = Token;
  Result->MutationAllowed = AllowMutation;
  Result->API.Header = {sizeof(Result->API), NEVERC_OBJECT_API_MAJOR,
                        NEVERC_OBJECT_API_MINOR, 0};
  initializeObjectQueryAPI(Result->API, *this);
  initializeObjectMutationAPI(Result->API, *this);
  initializeObjectBuilderAPI(Result->API, *this);
  Result->API.Context = Result.get();
  Task.retainCallbackContext(Result);
  return Result;
}

ObjectPluginBridge::ObjectPluginBridge(PluginTaskContext &TaskValue,
                                       PluginObjectGraph &GraphValue,
                                       const PluginPhaseExecutor &Executor,
                                       uint64_t Token)
    : ObjectPluginBridge(TaskValue, GraphValue, &Executor, Token) {}

ObjectPluginBridge::ObjectPluginBridge(PluginTaskContext &TaskValue,
                                       PluginObjectGraph &GraphValue,
                                       const void *Domain, uint64_t Token)
    : Task(TaskValue), Graph(GraphValue), MutationAllowed(Domain && Token != 0),
      MutationDomain(Domain), MutationCapabilityToken(Token) {
  Control = std::make_shared<OwnerControl>();
  Control->Owner = this;
  ReadOnlyFacade = createFacade(false);
  Facade = MutationAllowed ? createFacade(true, Domain, Token) : ReadOnlyFacade;
}

const NevercObjectAPI &ObjectPluginBridge::capabilityAPI(const void *Domain,
                                                         uint64_t Token) {
  if (!Domain || Token == 0)
    return ReadOnlyFacade->API;
  std::lock_guard<std::recursive_mutex> Lock(Control->Mutex);
  auto Existing = llvm::find_if(
      CapabilityFacades, [&](const std::shared_ptr<APIFacade> &Candidate) {
        return Candidate->MutationDomain == Domain && Candidate->Token == Token;
      });
  if (Existing != CapabilityFacades.end())
    return (*Existing)->API;
  auto Result = createFacade(true, Domain, Token);
  const NevercObjectAPI &API = Result->API;
  CapabilityFacades.push_back(std::move(Result));
  return API;
}

bool ObjectPluginBridge::mutationAllowed() const {
  if (!MutationAllowed)
    return false;
  return !MutationDomain || Task.validatesArtifactMutationCapability(
                                MutationDomain, MutationCapabilityToken);
}

ObjectPluginBridge::OwnerLease
ObjectPluginBridge::acquire(void *Context, NevercTaskHandle TaskHandle,
                            bool RequireMutation, NevercStatus &Status) {
  if (!Context) {
    Status = objectStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    return {};
  }
  auto &Facade = *static_cast<APIFacade *>(Context);
  if (!sameHandle(Facade.TaskHandle, TaskHandle)) {
    Status = objectStatus(NEVERC_STATUS_WRONG_SCOPE);
    return {};
  }
  if (RequireMutation && (!Facade.MutationAllowed ||
                          (Facade.MutationDomain &&
                           !Facade.Task->validatesArtifactMutationCapability(
                               Facade.MutationDomain, Facade.Token)))) {
    Status = objectStatus(NEVERC_STATUS_POLICY_VIOLATION);
    return {};
  }
  auto Control = Facade.Control;
  std::unique_lock<std::recursive_mutex> Lock(Control->Mutex);
  ObjectPluginBridge *Owner = Control->Owner;
  if (!Owner) {
    Status = objectStatus(NEVERC_STATUS_STALE_HANDLE);
    return {};
  }
  Status = neverc_status_ok();
  return OwnerLease(std::move(Control), std::move(Lock), Owner);
}

ObjectPluginBridge::~ObjectPluginBridge() {
  std::unique_lock<std::recursive_mutex> Lock(Control->Mutex);
  Working.reset();
  if (!neverc_handle_is_null(MutationHandle))
    (void)Task.handles().release(MutationHandle,
                                 PluginObjectMutationHandleKind);
  finishHandles();
  if (!neverc_handle_is_null(GraphHandle))
    (void)Task.handles().release(GraphHandle, PluginObjectGraphHandleKind);
  Control->Owner = nullptr;
}

PluginObjectGraph &ObjectPluginBridge::activeGraph() const {
  return Working ? *Working : Graph;
}

Expected<NevercObjectGraphHandle> ObjectPluginBridge::graph() {
  if (!neverc_handle_is_null(GraphHandle))
    return GraphHandle;
  auto Handle =
      Task.handles().create(PluginObjectGraphHandleKind, &Graph);
  if (!Handle)
    return Handle.takeError();
  GraphHandle = *Handle;
  return GraphHandle;
}

Expected<NevercObjectLayoutProofHandle>
ObjectPluginBridge::layoutProof() {
  if (!Graph.hasLayoutProof())
    return createStringError(inconvertibleErrorCode(),
                             "ObjectGraph has no layout proof");
  if (!neverc_handle_is_null(LayoutProofHandle))
    return LayoutProofHandle;
  auto Handle =
      Task.handles().create(PluginObjectLayoutProofHandleKind, this);
  if (!Handle)
    return Handle.takeError();
  LayoutProofHandle = *Handle;
  return LayoutProofHandle;
}

NevercStatus ObjectPluginBridge::resolveGraph(
    NevercObjectGraphHandle Handle, PluginObjectGraph **OutGraph) const {
  if (!OutGraph)
    return objectStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutGraph = nullptr;
  void *Payload = nullptr;
  NevercStatus Status = Task.handles().resolve(
      Handle, PluginObjectGraphHandleKind, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Payload != &Graph || !sameHandle(Handle, GraphHandle))
    return objectStatus(NEVERC_STATUS_WRONG_SCOPE);
  *OutGraph = &Graph;
  return neverc_status_ok();
}

NevercStatus ObjectPluginBridge::resolveLayoutProof(
    NevercObjectLayoutProofHandle Handle,
    const PluginObjectLayoutProof **OutProof) const {
  if (!OutProof)
    return objectStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutProof = nullptr;
  void *Payload = nullptr;
  NevercStatus Status = Task.handles().resolve(
      Handle, PluginObjectLayoutProofHandleKind, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Payload != this || !sameHandle(Handle, LayoutProofHandle))
    return objectStatus(NEVERC_STATUS_WRONG_SCOPE);
  const PluginObjectLayoutProof *Proof = Graph.layoutProof();
  if (!Proof)
    return objectStatus(NEVERC_STATUS_STALE_HANDLE);
  *OutProof = Proof;
  return neverc_status_ok();
}

Expected<NevercHandle>
ObjectPluginBridge::wrapEntity(EntityKind Kind, uint64_t ID) {
  const PluginHandleKind HandleKind = handleKind(Kind);
  for (const auto &Entry : EntityHandles) {
    if (Entry.second != HandleKind)
      continue;
    void *Payload = nullptr;
    if (Task.handles().resolve(Entry.first, HandleKind, &Payload).Code !=
        NEVERC_STATUS_OK)
      continue;
    const auto *Reference = static_cast<EntityReference *>(Payload);
    if (Reference->Bridge == this && Reference->Kind == Kind &&
        Reference->ID == ID &&
        Reference->Generation == BridgeGeneration)
      return Entry.first;
  }
  auto *Reference = new (std::nothrow)
      EntityReference{this, Kind, ID, BridgeGeneration};
  if (!Reference)
    return createStringError(inconvertibleErrorCode(),
                             "failed to allocate ObjectGraph reference");
  auto Handle = Task.handles().create(
      HandleKind, Reference,
      [](void *Value) { delete static_cast<EntityReference *>(Value); });
  if (!Handle) {
    delete Reference;
    return Handle.takeError();
  }
  EntityHandles.push_back({*Handle, HandleKind});
  return *Handle;
}

Expected<NevercObjectSectionHandle>
ObjectPluginBridge::wrapSection(PluginObjectSection &Section) {
  if (activeGraph().findSection(Section.ID) != &Section)
    return createStringError(inconvertibleErrorCode(),
                             "section does not belong to ObjectGraph");
  return wrapEntity(EntityKind::Section, Section.ID);
}

Expected<NevercObjectSymbolHandle>
ObjectPluginBridge::wrapSymbol(PluginObjectSymbol &Symbol) {
  if (activeGraph().findSymbol(Symbol.ID) != &Symbol)
    return createStringError(inconvertibleErrorCode(),
                             "symbol does not belong to ObjectGraph");
  return wrapEntity(EntityKind::Symbol, Symbol.ID);
}

Expected<NevercObjectRelocationHandle>
ObjectPluginBridge::wrapRelocation(
    PluginObjectRelocation &Relocation) {
  if (activeGraph().findRelocation(Relocation.ID) != &Relocation)
    return createStringError(inconvertibleErrorCode(),
                             "relocation does not belong to ObjectGraph");
  return wrapEntity(EntityKind::Relocation, Relocation.ID);
}

Expected<NevercObjectComdatHandle>
ObjectPluginBridge::wrapComdat(PluginObjectComdat &Comdat) {
  if (activeGraph().findComdat(Comdat.ID) != &Comdat)
    return createStringError(inconvertibleErrorCode(),
                             "COMDAT does not belong to ObjectGraph");
  return wrapEntity(EntityKind::Comdat, Comdat.ID);
}

NevercStatus ObjectPluginBridge::resolveEntity(
    NevercHandle Handle, EntityKind Kind, uint64_t *OutID) const {
  if (!OutID)
    return objectStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutID = 0;
  void *Payload = nullptr;
  const PluginHandleKind HandleKind = handleKind(Kind);
  NevercStatus Status =
      Task.handles().resolve(Handle, HandleKind, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  const auto *Reference = static_cast<EntityReference *>(Payload);
  if (Reference->Bridge != this || Reference->Kind != Kind)
    return objectStatus(NEVERC_STATUS_WRONG_SCOPE);
  if (Reference->Generation != BridgeGeneration)
    return objectStatus(NEVERC_STATUS_STALE_HANDLE);
  *OutID = Reference->ID;
  return neverc_status_ok();
}

NevercStatus ObjectPluginBridge::resolveSection(
    NevercObjectSectionHandle Handle,
    PluginObjectSection **OutSection) const {
  if (!OutSection)
    return objectStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutSection = nullptr;
  uint64_t ID = 0;
  NevercStatus Status =
      resolveEntity(Handle, EntityKind::Section, &ID);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  *OutSection = activeGraph().findSection(ID);
  return *OutSection ? neverc_status_ok()
                     : objectStatus(NEVERC_STATUS_STALE_HANDLE);
}

NevercStatus ObjectPluginBridge::resolveSymbol(
    NevercObjectSymbolHandle Handle,
    PluginObjectSymbol **OutSymbol) const {
  if (!OutSymbol)
    return objectStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutSymbol = nullptr;
  uint64_t ID = 0;
  NevercStatus Status = resolveEntity(Handle, EntityKind::Symbol, &ID);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  *OutSymbol = activeGraph().findSymbol(ID);
  return *OutSymbol ? neverc_status_ok()
                    : objectStatus(NEVERC_STATUS_STALE_HANDLE);
}

NevercStatus ObjectPluginBridge::resolveRelocation(
    NevercObjectRelocationHandle Handle,
    PluginObjectRelocation **OutRelocation) const {
  if (!OutRelocation)
    return objectStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutRelocation = nullptr;
  uint64_t ID = 0;
  NevercStatus Status =
      resolveEntity(Handle, EntityKind::Relocation, &ID);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  *OutRelocation = activeGraph().findRelocation(ID);
  return *OutRelocation ? neverc_status_ok()
                        : objectStatus(NEVERC_STATUS_STALE_HANDLE);
}

NevercStatus ObjectPluginBridge::resolveComdat(
    NevercObjectComdatHandle Handle,
    PluginObjectComdat **OutComdat) const {
  if (!OutComdat)
    return objectStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutComdat = nullptr;
  uint64_t ID = 0;
  NevercStatus Status = resolveEntity(Handle, EntityKind::Comdat, &ID);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  *OutComdat = activeGraph().findComdat(ID);
  return *OutComdat ? neverc_status_ok()
                    : objectStatus(NEVERC_STATUS_STALE_HANDLE);
}

void ObjectPluginBridge::finishHandles() {
  for (const auto &Entry : EntityHandles)
    (void)Task.handles().release(Entry.first, Entry.second);
  EntityHandles.clear();
  if (!neverc_handle_is_null(LayoutProofHandle)) {
    (void)Task.handles().release(LayoutProofHandle,
                                 PluginObjectLayoutProofHandleKind);
    LayoutProofHandle = {};
  }
}

void initializeObjectQueryAPI(NevercObjectAPI &API,
                              ObjectPluginBridge &Bridge) {
  API.Context = &Bridge;
  API.GetGraphInfo = GetGraphInfo;
  API.GetFirstSection = GetFirstSection;
  API.GetNextSection = GetNextSection;
  API.GetSectionInfo = GetSectionInfo;
  API.GetFirstSymbol = GetFirstSymbol;
  API.GetNextSymbol = GetNextSymbol;
  API.GetSymbolInfo = GetSymbolInfo;
  API.GetFirstRelocation = GetFirstRelocation;
  API.GetNextRelocation = GetNextRelocation;
  API.GetRelocationInfo = GetRelocationInfo;
  API.GetFirstComdat = GetFirstComdat;
  API.GetNextComdat = GetNextComdat;
  API.GetComdatInfo = GetComdatInfo;
  API.GetLayoutProof = GetLayoutProof;
  API.GetLayoutProofInfo = GetLayoutProofInfo;
}

} // namespace neverc::plugin
