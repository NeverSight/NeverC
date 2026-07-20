#include "LinkMutation.h"
#include "LivenessVerifier.h"
#include "ResolutionVerifier.h"
#include "SynthesisVerifier.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include <algorithm>
#include <cstring>
#include <utility>

using namespace llvm;

namespace neverc::plugin {
namespace {

template <typename Storage, typename Value>
Error replaceByID(Storage &Values, uint64_t ID, Value Replacement,
                  const char *Kind) {
  auto It = std::find_if(Values.begin(), Values.end(),
                         [ID](const auto &Entry) {
                           return Entry.ID == ID;
                         });
  if (It == Values.end())
    return createStringError(inconvertibleErrorCode(),
                             "cannot replace missing LinkGraph %s", Kind);
  Replacement.ID = ID;
  *It = std::move(Replacement);
  return Error::success();
}

template <typename Storage>
Error eraseByID(Storage &Values, uint64_t ID, const char *Kind) {
  auto It = std::find_if(Values.begin(), Values.end(),
                         [ID](const auto &Entry) {
                           return Entry.ID == ID;
                         });
  if (It == Values.end())
    return createStringError(inconvertibleErrorCode(),
                             "cannot erase missing LinkGraph %s", Kind);
  Values.erase(It);
  return Error::success();
}

} // namespace

LinkMutation::LinkMutation(const PluginLinkGraph &Graph,
                           std::string CapabilityValue)
    : Working(std::make_unique<PluginLinkGraph>(Graph)),
      Capability(std::move(CapabilityValue)) {}

void LinkMutation::mark(LinkMutationKind Kind) {
  Changed = true;
  EarliestInvalidatedState =
      std::min(EarliestInvalidatedState,
               earliestInvalidatedLinkState(Kind));
}

PluginLinkSection &LinkMutation::addSection(PluginLinkSection Value) {
  mark(LinkMutationKind::InputStructure);
  return Working->addSection(std::move(Value));
}

Error LinkMutation::replaceSection(uint64_t ID, PluginLinkSection Value) {
  if (Error E = replaceByID(Working->sections(), ID, std::move(Value),
                            "section"))
    return E;
  mark(LinkMutationKind::InputStructure);
  return Error::success();
}

Error LinkMutation::eraseSection(uint64_t ID) {
  if (Error E = eraseByID(Working->sections(), ID, "section"))
    return E;
  mark(LinkMutationKind::InputStructure);
  return Error::success();
}

PluginLinkAtom &LinkMutation::addAtom(PluginLinkAtom Value) {
  mark(LinkMutationKind::InputStructure);
  return Working->addAtom(std::move(Value));
}

Error LinkMutation::replaceAtom(uint64_t ID, PluginLinkAtom Value) {
  if (Error E =
          replaceByID(Working->atoms(), ID, std::move(Value), "atom"))
    return E;
  mark(LinkMutationKind::InputStructure);
  return Error::success();
}

Error LinkMutation::eraseAtom(uint64_t ID) {
  if (Error E = eraseByID(Working->atoms(), ID, "atom"))
    return E;
  mark(LinkMutationKind::InputStructure);
  return Error::success();
}

PluginLinkSymbol &LinkMutation::addSymbol(PluginLinkSymbol Value) {
  mark(LinkMutationKind::SymbolResolution);
  return Working->addSymbol(std::move(Value));
}

Error LinkMutation::replaceSymbol(uint64_t ID, PluginLinkSymbol Value) {
  if (Error E = replaceByID(Working->symbols(), ID, std::move(Value),
                            "symbol"))
    return E;
  mark(LinkMutationKind::SymbolResolution);
  return Error::success();
}

Error LinkMutation::eraseSymbol(uint64_t ID) {
  if (Error E = eraseByID(Working->symbols(), ID, "symbol"))
    return E;
  mark(LinkMutationKind::SymbolResolution);
  return Error::success();
}

PluginLinkEdge &LinkMutation::addEdge(PluginLinkEdge Value) {
  mark(LinkMutationKind::SymbolResolution);
  return Working->addEdge(std::move(Value));
}

Error LinkMutation::replaceEdge(uint64_t ID, PluginLinkEdge Value) {
  if (Error E =
          replaceByID(Working->edges(), ID, std::move(Value), "edge"))
    return E;
  mark(LinkMutationKind::SymbolResolution);
  return Error::success();
}

Error LinkMutation::eraseEdge(uint64_t ID) {
  if (Error E = eraseByID(Working->edges(), ID, "edge"))
    return E;
  mark(LinkMutationKind::SymbolResolution);
  return Error::success();
}

PluginLinkSynthetic &
LinkMutation::addSynthetic(PluginLinkSynthetic Value) {
  mark(LinkMutationKind::Synthetic);
  return Working->addSynthetic(std::move(Value));
}

Error LinkMutation::replaceSynthetic(uint64_t ID,
                                     PluginLinkSynthetic Value) {
  if (Error E = replaceByID(Working->synthetics(), ID, std::move(Value),
                            "synthetic"))
    return E;
  mark(LinkMutationKind::Synthetic);
  return Error::success();
}

Error LinkMutation::eraseSynthetic(uint64_t ID) {
  if (Error E = eraseByID(Working->synthetics(), ID, "synthetic"))
    return E;
  mark(LinkMutationKind::Synthetic);
  return Error::success();
}

PluginLinkConstraint &
LinkMutation::addConstraint(PluginLinkConstraint Value) {
  mark(LinkMutationKind::LayoutConstraint);
  return Working->addConstraint(std::move(Value));
}

Error LinkMutation::replaceConstraint(uint64_t ID,
                                      PluginLinkConstraint Value) {
  if (Error E = replaceByID(Working->constraints(), ID, std::move(Value),
                            "constraint"))
    return E;
  mark(LinkMutationKind::LayoutConstraint);
  return Error::success();
}

Error LinkMutation::eraseConstraint(uint64_t ID) {
  if (Error E = eraseByID(Working->constraints(), ID, "constraint"))
    return E;
  mark(LinkMutationKind::LayoutConstraint);
  return Error::success();
}

Error LinkMutation::rebindSymbol(uint64_t SymbolID, uint64_t AtomID) {
  PluginLinkSymbol *Symbol = Working->findSymbol(SymbolID);
  if (!Symbol)
    return createStringError(inconvertibleErrorCode(),
                             "cannot rebind missing LinkGraph symbol");
  Symbol->AtomID = AtomID;
  mark(LinkMutationKind::SymbolResolution);
  return Error::success();
}

Error LinkMutation::setSymbolResolution(
    uint64_t SymbolID, NevercLinkSymbolBinding Binding,
    NevercLinkSymbolVisibility Visibility,
    NevercLinkSymbolDefinition Definition, bool Prevailing,
    bool Exported) {
  PluginLinkSymbol *Symbol = Working->findSymbol(SymbolID);
  if (!Symbol)
    return createStringError(
        inconvertibleErrorCode(),
        "cannot resolve missing LinkGraph symbol");
  Symbol->Binding = Binding;
  Symbol->Visibility = Visibility;
  Symbol->Definition = Definition;
  Symbol->IsPrevailing = Prevailing;
  Symbol->IsExported = Exported;
  if (Prevailing) {
    for (PluginLinkEdge &Edge : Working->edges()) {
      PluginLinkSymbol *Target =
          Working->findSymbol(Edge.TargetSymbolID);
      if (!Target)
        continue;
      const bool SameCandidateSet =
          Binding != NEVERC_LINK_SYMBOL_BINDING_LOCAL &&
          Target->Binding != NEVERC_LINK_SYMBOL_BINDING_LOCAL &&
          Target->Name == Symbol->Name &&
          Target->Version == Symbol->Version;
      if (Target->ID == SymbolID || SameCandidateSet) {
        Edge.TargetSymbolID = SymbolID;
        Edge.TargetAtomID = 0;
      }
    }
  }
  mark(LinkMutationKind::ResolutionOutcome);
  return Error::success();
}

Error LinkMutation::retargetEdge(uint64_t EdgeID, uint64_t SymbolID,
                                 uint64_t AtomID) {
  PluginLinkEdge *Edge = Working->findEdge(EdgeID);
  if (!Edge)
    return createStringError(inconvertibleErrorCode(),
                             "cannot retarget missing LinkGraph edge");
  Edge->TargetSymbolID = SymbolID;
  Edge->TargetAtomID = AtomID;
  mark(LinkMutationKind::SymbolResolution);
  return Error::success();
}

Error LinkMutation::setSymbolRoot(uint64_t SymbolID, bool Root) {
  PluginLinkSymbol *Symbol = Working->findSymbol(SymbolID);
  if (!Symbol)
    return createStringError(inconvertibleErrorCode(),
                             "cannot change missing LinkGraph symbol root");
  Symbol->IsRoot = Root;
  mark(LinkMutationKind::Liveness);
  return Error::success();
}

Error LinkMutation::setAtomLive(uint64_t AtomID, bool Live) {
  PluginLinkAtom *Atom = Working->findAtom(AtomID);
  if (!Atom)
    return createStringError(inconvertibleErrorCode(),
                             "cannot change missing LinkGraph atom liveness");
  if (Live)
    Atom->Flags |= NEVERC_LINK_ATOM_LIVE;
  else
    Atom->Flags &= ~NEVERC_LINK_ATOM_LIVE;
  mark(LinkMutationKind::LivenessOutcome);
  return Error::success();
}

Error LinkMutation::setFoldLeader(uint64_t AtomID, uint64_t LeaderID) {
  PluginLinkAtom *Atom = Working->findAtom(AtomID);
  if (!Atom)
    return createStringError(inconvertibleErrorCode(),
                             "cannot fold missing LinkGraph atom");
  Atom->FoldLeaderID = LeaderID;
  if (LeaderID == 0)
    Atom->Flags &= ~NEVERC_LINK_ATOM_FOLDED;
  else
    Atom->Flags |= NEVERC_LINK_ATOM_FOLDED;
  mark(LinkMutationKind::Folding);
  return Error::success();
}

Error LinkMutation::replaceAtomContent(uint64_t AtomID,
                                       std::vector<uint8_t> Content,
                                       uint64_t ZeroFillSize) {
  PluginLinkAtom *Atom = Working->findAtom(AtomID);
  if (!Atom)
    return createStringError(inconvertibleErrorCode(),
                             "cannot replace missing LinkGraph atom bytes");
  Atom->Content = std::move(Content);
  Atom->ZeroFillSize = ZeroFillSize;
  mark(LinkMutationKind::AtomContent);
  return Error::success();
}

PluginLinkGraph &LinkGraphPluginBridge::activeGraph() const {
  return Mutation ? Mutation->graph() : Graph;
}

bool LinkGraphPluginBridge::hasActiveMutation() const {
  return Mutation && !neverc_handle_is_null(MutationHandle);
}

Expected<NevercLinkMutationHandle>
LinkGraphPluginBridge::beginMutation(std::string Capability) {
  if (!MutationAllowed)
    return createStringError(inconvertibleErrorCode(),
                             "LinkGraph mutation is not allowed");
  if (hasActiveMutation())
    return createStringError(inconvertibleErrorCode(),
                             "LinkGraph mutation is already active");
  Mutation = std::make_unique<LinkMutation>(Graph, std::move(Capability));
  auto Handle =
      Task.handles().create(PluginLinkMutationHandleKind, this);
  if (!Handle) {
    Mutation.reset();
    return Handle.takeError();
  }
  MutationHandle = *Handle;
  return MutationHandle;
}

NevercStatus LinkGraphPluginBridge::checkMutation(
    NevercLinkMutationHandle Handle) const {
  NevercStatus Status = neverc_status_ok();
  if (!hasActiveMutation()) {
    Status.Code = NEVERC_STATUS_INVALID_STATE;
    return Status;
  }
  void *Payload = nullptr;
  Status =
      Task.handles().resolve(Handle, PluginLinkMutationHandleKind, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Payload != this || Handle.Owner != MutationHandle.Owner ||
      Handle.Value != MutationHandle.Value)
    Status.Code = NEVERC_STATUS_WRONG_SCOPE;
  return Status;
}

LinkMutation *LinkGraphPluginBridge::mutationValue(
    NevercLinkMutationHandle Handle, NevercStatus &Status) const {
  Status = checkMutation(Handle);
  return Status.Code == NEVERC_STATUS_OK ? Mutation.get() : nullptr;
}

NevercStatus LinkGraphPluginBridge::commitMutation(
    NevercLinkMutationHandle Handle) {
  NevercStatus Status = checkMutation(Handle);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Error E = verifyPluginLinkGraph(Mutation->graph())) {
    consumeError(std::move(E));
    finishMutation();
    Status.Code = NEVERC_STATUS_VERIFICATION_FAILED;
    return Status;
  }
  if (Mutation->graph().state() ==
      NEVERC_LINK_STATE_SYMBOLS_RESOLVED) {
    if (Error E =
            verifyLinkSymbolResolution(Mutation->graph())) {
      consumeError(std::move(E));
      finishMutation();
      Status.Code = NEVERC_STATUS_VERIFICATION_FAILED;
      return Status;
    }
  }
  if (Mutation->graph().state() ==
          NEVERC_LINK_STATE_COMDAT_SELECTED) {
    if (Error E =
            verifyLinkSymbolResolution(Mutation->graph())) {
      consumeError(std::move(E));
      finishMutation();
      Status.Code = NEVERC_STATUS_VERIFICATION_FAILED;
      return Status;
    }
    if (Error E = verifyLinkComdatSelection(Mutation->graph())) {
      consumeError(std::move(E));
      finishMutation();
      Status.Code = NEVERC_STATUS_VERIFICATION_FAILED;
      return Status;
    }
  }
  if (Mutation->graph().state() == NEVERC_LINK_STATE_GC_COMPLETE) {
    if (Error E = verifyLinkLiveness(Mutation->graph())) {
      consumeError(std::move(E));
      finishMutation();
      Status.Code = NEVERC_STATUS_VERIFICATION_FAILED;
      return Status;
    }
  }
  if (Mutation->graph().state() == NEVERC_LINK_STATE_ICF_COMPLETE) {
    if (Error E = verifyLinkFolding(Mutation->graph())) {
      consumeError(std::move(E));
      finishMutation();
      Status.Code = NEVERC_STATUS_VERIFICATION_FAILED;
      return Status;
    }
  }
  if (Mutation->graph().state() ==
      NEVERC_LINK_STATE_SYNTHETICS_READY) {
    if (Error E = verifyLinkSynthetics(Mutation->graph())) {
      consumeError(std::move(E));
      finishMutation();
      Status.Code = NEVERC_STATUS_VERIFICATION_FAILED;
      return Status;
    }
  }
  if (Mutation->graph().state() ==
      NEVERC_LINK_STATE_THUNKS_RELAXED) {
    if (Error E = verifyLinkRelaxation(Mutation->graph())) {
      consumeError(std::move(E));
      finishMutation();
      Status.Code = NEVERC_STATUS_VERIFICATION_FAILED;
      return Status;
    }
  }
  if (Mutation->changed()) {
    LastInvalidatedState = Mutation->earliestInvalidatedState();
    Mutation->graph().setState(std::min(
        Graph.state(), predecessorLinkState(LastInvalidatedState)));
    Mutation->graph().advanceGeneration();
    Graph = std::move(Mutation->graph());
  }
  finishMutation();
  return neverc_status_ok();
}

NevercStatus LinkGraphPluginBridge::abandonMutation(
    NevercLinkMutationHandle Handle) {
  NevercStatus Status = checkMutation(Handle);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  finishMutation();
  return neverc_status_ok();
}

void LinkGraphPluginBridge::finishMutation() {
  if (!neverc_handle_is_null(MutationHandle)) {
    (void)Task.handles().release(MutationHandle,
                                 PluginLinkMutationHandleKind);
    MutationHandle = {};
  }
  Mutation.reset();
  invalidateEntityHandles();
}

namespace {

NevercStatus mutationStatus(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

bool sameHandle(NevercHandle Left, NevercHandle Right) {
  return Left.Owner == Right.Owner && Left.Value == Right.Value;
}

LinkGraphPluginBridge *mutationBridge(void *Context,
                                      NevercTaskHandle Task,
                                      NevercStatus &Status) {
  if (!Context) {
    Status = mutationStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    return nullptr;
  }
  auto *Bridge = static_cast<LinkGraphPluginBridge *>(Context);
  if (!sameHandle(Bridge->taskHandle(), Task)) {
    Status = mutationStatus(NEVERC_STATUS_WRONG_SCOPE);
    return nullptr;
  }
  Status = neverc_status_ok();
  return Bridge;
}

template <typename T> bool validDescriptor(const T *Value) {
  return Value && Value->Header.StructSize >= sizeof(T) &&
         Value->Header.Major == NEVERC_LINK_API_MAJOR &&
         Value->Header.Minor <= NEVERC_LINK_API_MINOR;
}

NevercStatus mutationError(Error E) {
  consumeError(std::move(E));
  return mutationStatus(NEVERC_STATUS_INVALID_ARGUMENT);
}

NevercStatus resolveOptionalEntity(
    LinkGraphPluginBridge &Bridge, NevercHandle Handle,
    LinkGraphPluginBridge::EntityKind Kind, uint64_t &ID) {
  ID = 0;
  if (neverc_handle_is_null(Handle))
    return neverc_status_ok();
  return Bridge.resolveEntity(Handle, Kind, &ID);
}

NevercStatus decodeOrigin(LinkGraphPluginBridge &Bridge,
                          const NevercLinkOrigin &Source,
                          PluginLinkOriginData &Out) {
  NevercStatus Status = resolveOptionalEntity(
      Bridge, Source.Input, LinkGraphPluginBridge::EntityKind::Input,
      Out.InputID);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = resolveOptionalEntity(
      Bridge, Source.ArchiveMember,
      LinkGraphPluginBridge::EntityKind::ArchiveMember,
      Out.ArchiveMemberID);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if ((Source.CreatedByProvider.Length != 0 &&
       !Source.CreatedByProvider.Data) ||
      (Source.LastMutationPlugin.Length != 0 &&
       !Source.LastMutationPlugin.Data))
    return mutationStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  Out.ObjectGraph = Source.ObjectGraph;
  Out.ObjectEntityID = Source.ObjectEntityID;
  Out.CreatedByPhase = Source.CreatedByPhase;
  Out.CreatedByProvider.assign(Source.CreatedByProvider.Data
                                   ? Source.CreatedByProvider.Data
                                   : "",
                               Source.CreatedByProvider.Length);
  Out.LastMutationPhase = Source.LastMutationPhase;
  Out.LastMutationPlugin.assign(Source.LastMutationPlugin.Data
                                    ? Source.LastMutationPlugin.Data
                                    : "",
                                Source.LastMutationPlugin.Length);
  return neverc_status_ok();
}

NevercStatus decodeExtensions(const NevercStructArrayView &Source,
                              PluginLinkExtensionSet &Out) {
  if (Source.Count == 0)
    return neverc_status_ok();
  if (!Source.Data ||
      Source.ElementStride < sizeof(NevercLinkExtension))
    return mutationStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  const auto *Bytes = static_cast<const uint8_t *>(Source.Data);
  for (uint64_t Index = 0; Index < Source.Count; ++Index) {
    const auto *Extension = reinterpret_cast<const NevercLinkExtension *>(
        Bytes + Index * Source.ElementStride);
    if (!validDescriptor(Extension) ||
        (Extension->Payload.Length != 0 && !Extension->Payload.Data) ||
        (Extension->Digest.Length != 0 && !Extension->Digest.Data))
      return mutationStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    PluginLinkExtensionData Value;
    Value.NamespaceID = Extension->NamespaceID;
    Value.Version = Extension->Version;
    Value.Required = Extension->Required != NEVERC_FALSE;
    if (Extension->Payload.Length != 0)
      Value.Payload.assign(
          Extension->Payload.Data,
          Extension->Payload.Data + Extension->Payload.Length);
    Value.Digest.assign(Extension->Digest.Data ? Extension->Digest.Data : "",
                        Extension->Digest.Length);
    Out.values().push_back(std::move(Value));
  }
  return neverc_status_ok();
}

NevercStatus decodeSynthetic(LinkGraphPluginBridge &Bridge,
                             const NevercLinkSyntheticInfo *Descriptor,
                             PluginLinkSynthetic &Out) {
  if (!validDescriptor(Descriptor) ||
      (Descriptor->Role.Length != 0 && !Descriptor->Role.Data))
    return mutationStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  Out.Role.assign(Descriptor->Role.Data ? Descriptor->Role.Data : "",
                  Descriptor->Role.Length);
  NevercStatus Status = resolveOptionalEntity(
      Bridge, Descriptor->Section,
      LinkGraphPluginBridge::EntityKind::Section, Out.SectionID);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = resolveOptionalEntity(
      Bridge, Descriptor->Atom, LinkGraphPluginBridge::EntityKind::Atom,
      Out.AtomID);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = decodeOrigin(Bridge, Descriptor->Origin, Out.Origin);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  return decodeExtensions(Descriptor->Extensions, Out.Extensions);
}

NevercStatus decodeConstraint(
    LinkGraphPluginBridge &Bridge,
    const NevercLinkConstraintInfo *Descriptor,
    PluginLinkConstraint &Out) {
  if (!validDescriptor(Descriptor) ||
      (Descriptor->Kind.Length != 0 && !Descriptor->Kind.Data))
    return mutationStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  Out.Kind.assign(Descriptor->Kind.Data ? Descriptor->Kind.Data : "",
                  Descriptor->Kind.Length);
  Out.SubjectID = Descriptor->SubjectID;
  Out.Value = Descriptor->Value;
  Out.Required = Descriptor->Required != NEVERC_FALSE;
  NevercStatus Status =
      decodeOrigin(Bridge, Descriptor->Origin, Out.Origin);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  return decodeExtensions(Descriptor->Extensions, Out.Extensions);
}

NevercStatus NEVERC_CALL BeginMutation(
    void *Context, NevercTaskHandle Task, NevercLinkGraphHandle Graph,
    NevercLinkMutationHandle *OutMutation) {
  NevercStatus Status;
  LinkGraphPluginBridge *Bridge = mutationBridge(Context, Task, Status);
  if (!Bridge || !OutMutation)
    return Bridge ? mutationStatus(NEVERC_STATUS_INVALID_ARGUMENT) : Status;
  *OutMutation = {};
  PluginLinkGraph *Resolved = nullptr;
  Status = Bridge->resolveGraph(Graph, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Resolved != &Bridge->activeGraph())
    return mutationStatus(NEVERC_STATUS_WRONG_SCOPE);
  auto Mutation = Bridge->beginMutation();
  if (!Mutation) {
    consumeError(Mutation.takeError());
    return mutationStatus(Bridge->mutationAllowed()
                              ? NEVERC_STATUS_INVALID_STATE
                              : NEVERC_STATUS_POLICY_VIOLATION);
  }
  *OutMutation = *Mutation;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL CommitMutation(
    void *Context, NevercTaskHandle Task,
    NevercLinkMutationHandle MutationHandle) {
  NevercStatus Status;
  LinkGraphPluginBridge *Bridge = mutationBridge(Context, Task, Status);
  return Bridge ? Bridge->commitMutation(MutationHandle) : Status;
}

NevercStatus NEVERC_CALL AbandonMutation(
    void *Context, NevercTaskHandle Task,
    NevercLinkMutationHandle MutationHandle) {
  NevercStatus Status;
  LinkGraphPluginBridge *Bridge = mutationBridge(Context, Task, Status);
  return Bridge ? Bridge->abandonMutation(MutationHandle) : Status;
}

NevercStatus NEVERC_CALL RebindSymbol(
    void *Context, NevercTaskHandle Task,
    NevercLinkMutationHandle MutationHandle,
    NevercLinkSymbolHandle Symbol, NevercLinkAtomHandle Atom) {
  NevercStatus Status;
  LinkGraphPluginBridge *Bridge = mutationBridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  LinkMutation *Mutation =
      Bridge->mutationValue(MutationHandle, Status);
  if (!Mutation)
    return Status;
  uint64_t SymbolID = 0;
  uint64_t AtomID = 0;
  Status = Bridge->resolveEntity(
      Symbol, LinkGraphPluginBridge::EntityKind::Symbol, &SymbolID);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = resolveOptionalEntity(
      *Bridge, Atom, LinkGraphPluginBridge::EntityKind::Atom, AtomID);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Error E = Mutation->rebindSymbol(SymbolID, AtomID))
    return mutationError(std::move(E));
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL SetSymbolResolution(
    void *Context, NevercTaskHandle Task,
    NevercLinkMutationHandle MutationHandle,
    NevercLinkSymbolHandle Symbol,
    const NevercLinkSymbolResolutionUpdate *Update) {
  NevercStatus Status;
  LinkGraphPluginBridge *Bridge = mutationBridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  if (!validDescriptor(Update))
    return mutationStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  LinkMutation *Mutation =
      Bridge->mutationValue(MutationHandle, Status);
  if (!Mutation)
    return Status;
  uint64_t SymbolID = 0;
  Status = Bridge->resolveEntity(
      Symbol, LinkGraphPluginBridge::EntityKind::Symbol, &SymbolID);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Error E = Mutation->setSymbolResolution(
          SymbolID, Update->Binding, Update->Visibility,
          Update->Definition,
          Update->IsPrevailing != NEVERC_FALSE,
          Update->IsExported != NEVERC_FALSE))
    return mutationError(std::move(E));
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL RetargetEdge(
    void *Context, NevercTaskHandle Task,
    NevercLinkMutationHandle MutationHandle, NevercLinkEdgeHandle Edge,
    NevercLinkSymbolHandle TargetSymbol, NevercLinkAtomHandle TargetAtom) {
  NevercStatus Status;
  LinkGraphPluginBridge *Bridge = mutationBridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  LinkMutation *Mutation =
      Bridge->mutationValue(MutationHandle, Status);
  if (!Mutation)
    return Status;
  uint64_t EdgeID = 0;
  uint64_t SymbolID = 0;
  uint64_t AtomID = 0;
  Status = Bridge->resolveEntity(
      Edge, LinkGraphPluginBridge::EntityKind::Edge, &EdgeID);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = resolveOptionalEntity(
      *Bridge, TargetSymbol, LinkGraphPluginBridge::EntityKind::Symbol,
      SymbolID);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = resolveOptionalEntity(
      *Bridge, TargetAtom, LinkGraphPluginBridge::EntityKind::Atom,
      AtomID);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Error E = Mutation->retargetEdge(EdgeID, SymbolID, AtomID))
    return mutationError(std::move(E));
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL SetSymbolRoot(
    void *Context, NevercTaskHandle Task,
    NevercLinkMutationHandle MutationHandle,
    NevercLinkSymbolHandle Symbol, NevercBool Root) {
  NevercStatus Status;
  LinkGraphPluginBridge *Bridge = mutationBridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  LinkMutation *Mutation =
      Bridge->mutationValue(MutationHandle, Status);
  if (!Mutation)
    return Status;
  uint64_t SymbolID = 0;
  Status = Bridge->resolveEntity(
      Symbol, LinkGraphPluginBridge::EntityKind::Symbol, &SymbolID);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Error E = Mutation->setSymbolRoot(SymbolID, Root != NEVERC_FALSE))
    return mutationError(std::move(E));
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL SetAtomLive(
    void *Context, NevercTaskHandle Task,
    NevercLinkMutationHandle MutationHandle, NevercLinkAtomHandle Atom,
    NevercBool Live) {
  NevercStatus Status;
  LinkGraphPluginBridge *Bridge = mutationBridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  LinkMutation *Mutation =
      Bridge->mutationValue(MutationHandle, Status);
  if (!Mutation)
    return Status;
  uint64_t AtomID = 0;
  Status = Bridge->resolveEntity(
      Atom, LinkGraphPluginBridge::EntityKind::Atom, &AtomID);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Error E = Mutation->setAtomLive(AtomID, Live != NEVERC_FALSE))
    return mutationError(std::move(E));
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL SetFoldLeader(
    void *Context, NevercTaskHandle Task,
    NevercLinkMutationHandle MutationHandle, NevercLinkAtomHandle Atom,
    NevercLinkAtomHandle Leader) {
  NevercStatus Status;
  LinkGraphPluginBridge *Bridge = mutationBridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  LinkMutation *Mutation =
      Bridge->mutationValue(MutationHandle, Status);
  if (!Mutation)
    return Status;
  uint64_t AtomID = 0;
  uint64_t LeaderID = 0;
  Status = Bridge->resolveEntity(
      Atom, LinkGraphPluginBridge::EntityKind::Atom, &AtomID);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = resolveOptionalEntity(
      *Bridge, Leader, LinkGraphPluginBridge::EntityKind::Atom, LeaderID);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Error E = Mutation->setFoldLeader(AtomID, LeaderID))
    return mutationError(std::move(E));
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL ReplaceAtomContent(
    void *Context, NevercTaskHandle Task,
    NevercLinkMutationHandle MutationHandle, NevercLinkAtomHandle Atom,
    NevercByteView Content, uint64_t ZeroFillSize) {
  NevercStatus Status;
  LinkGraphPluginBridge *Bridge = mutationBridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  if (Content.Length != 0 && !Content.Data)
    return mutationStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  LinkMutation *Mutation =
      Bridge->mutationValue(MutationHandle, Status);
  if (!Mutation)
    return Status;
  uint64_t AtomID = 0;
  Status = Bridge->resolveEntity(
      Atom, LinkGraphPluginBridge::EntityKind::Atom, &AtomID);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  std::vector<uint8_t> Bytes;
  if (Content.Length != 0)
    Bytes.assign(Content.Data, Content.Data + Content.Length);
  if (Error E = Mutation->replaceAtomContent(
          AtomID, std::move(Bytes), ZeroFillSize))
    return mutationError(std::move(E));
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL CreateSynthetic(
    void *Context, NevercTaskHandle Task,
    NevercLinkMutationHandle MutationHandle,
    const NevercLinkSyntheticInfo *Descriptor,
    NevercLinkSyntheticHandle *OutSynthetic) {
  NevercStatus Status;
  LinkGraphPluginBridge *Bridge = mutationBridge(Context, Task, Status);
  if (!Bridge || !OutSynthetic)
    return Bridge ? mutationStatus(NEVERC_STATUS_INVALID_ARGUMENT) : Status;
  *OutSynthetic = {};
  LinkMutation *Mutation =
      Bridge->mutationValue(MutationHandle, Status);
  if (!Mutation)
    return Status;
  PluginLinkSynthetic Value;
  Status = decodeSynthetic(*Bridge, Descriptor, Value);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  uint64_t ID = Mutation->addSynthetic(std::move(Value)).ID;
  auto Handle = Bridge->wrapEntity(
      LinkGraphPluginBridge::EntityKind::Synthetic, ID);
  if (!Handle) {
    consumeError(Handle.takeError());
    return mutationStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutSynthetic = *Handle;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL ReplaceSynthetic(
    void *Context, NevercTaskHandle Task,
    NevercLinkMutationHandle MutationHandle,
    NevercLinkSyntheticHandle Synthetic,
    const NevercLinkSyntheticInfo *Descriptor) {
  NevercStatus Status;
  LinkGraphPluginBridge *Bridge = mutationBridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  LinkMutation *Mutation =
      Bridge->mutationValue(MutationHandle, Status);
  if (!Mutation)
    return Status;
  uint64_t ID = 0;
  Status = Bridge->resolveEntity(
      Synthetic, LinkGraphPluginBridge::EntityKind::Synthetic, &ID);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  PluginLinkSynthetic Value;
  Status = decodeSynthetic(*Bridge, Descriptor, Value);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Error E = Mutation->replaceSynthetic(ID, std::move(Value)))
    return mutationError(std::move(E));
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL EraseSynthetic(
    void *Context, NevercTaskHandle Task,
    NevercLinkMutationHandle MutationHandle,
    NevercLinkSyntheticHandle Synthetic) {
  NevercStatus Status;
  LinkGraphPluginBridge *Bridge = mutationBridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  LinkMutation *Mutation =
      Bridge->mutationValue(MutationHandle, Status);
  if (!Mutation)
    return Status;
  uint64_t ID = 0;
  Status = Bridge->resolveEntity(
      Synthetic, LinkGraphPluginBridge::EntityKind::Synthetic, &ID);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Error E = Mutation->eraseSynthetic(ID))
    return mutationError(std::move(E));
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL CreateConstraint(
    void *Context, NevercTaskHandle Task,
    NevercLinkMutationHandle MutationHandle,
    const NevercLinkConstraintInfo *Descriptor,
    NevercLinkConstraintHandle *OutConstraint) {
  NevercStatus Status;
  LinkGraphPluginBridge *Bridge = mutationBridge(Context, Task, Status);
  if (!Bridge || !OutConstraint)
    return Bridge ? mutationStatus(NEVERC_STATUS_INVALID_ARGUMENT) : Status;
  *OutConstraint = {};
  LinkMutation *Mutation =
      Bridge->mutationValue(MutationHandle, Status);
  if (!Mutation)
    return Status;
  PluginLinkConstraint Value;
  Status = decodeConstraint(*Bridge, Descriptor, Value);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  uint64_t ID = Mutation->addConstraint(std::move(Value)).ID;
  auto Handle = Bridge->wrapEntity(
      LinkGraphPluginBridge::EntityKind::Constraint, ID);
  if (!Handle) {
    consumeError(Handle.takeError());
    return mutationStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutConstraint = *Handle;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL ReplaceConstraint(
    void *Context, NevercTaskHandle Task,
    NevercLinkMutationHandle MutationHandle,
    NevercLinkConstraintHandle Constraint,
    const NevercLinkConstraintInfo *Descriptor) {
  NevercStatus Status;
  LinkGraphPluginBridge *Bridge = mutationBridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  LinkMutation *Mutation =
      Bridge->mutationValue(MutationHandle, Status);
  if (!Mutation)
    return Status;
  uint64_t ID = 0;
  Status = Bridge->resolveEntity(
      Constraint, LinkGraphPluginBridge::EntityKind::Constraint, &ID);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  PluginLinkConstraint Value;
  Status = decodeConstraint(*Bridge, Descriptor, Value);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Error E = Mutation->replaceConstraint(ID, std::move(Value)))
    return mutationError(std::move(E));
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL EraseConstraint(
    void *Context, NevercTaskHandle Task,
    NevercLinkMutationHandle MutationHandle,
    NevercLinkConstraintHandle Constraint) {
  NevercStatus Status;
  LinkGraphPluginBridge *Bridge = mutationBridge(Context, Task, Status);
  if (!Bridge)
    return Status;
  LinkMutation *Mutation =
      Bridge->mutationValue(MutationHandle, Status);
  if (!Mutation)
    return Status;
  uint64_t ID = 0;
  Status = Bridge->resolveEntity(
      Constraint, LinkGraphPluginBridge::EntityKind::Constraint, &ID);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Error E = Mutation->eraseConstraint(ID))
    return mutationError(std::move(E));
  return neverc_status_ok();
}

} // namespace

void initializeLinkMutationAPI(NevercLinkAPI &API,
                               LinkGraphPluginBridge &Bridge) {
  API.Context = &Bridge;
  API.BeginMutation = BeginMutation;
  API.CommitMutation = CommitMutation;
  API.AbandonMutation = AbandonMutation;
  API.RebindSymbol = RebindSymbol;
  API.SetSymbolResolution = SetSymbolResolution;
  API.RetargetEdge = RetargetEdge;
  API.SetSymbolRoot = SetSymbolRoot;
  API.SetAtomLive = SetAtomLive;
  API.SetFoldLeader = SetFoldLeader;
  API.ReplaceAtomContent = ReplaceAtomContent;
  API.CreateSynthetic = CreateSynthetic;
  API.ReplaceSynthetic = ReplaceSynthetic;
  API.EraseSynthetic = EraseSynthetic;
  API.CreateConstraint = CreateConstraint;
  API.ReplaceConstraint = ReplaceConstraint;
  API.EraseConstraint = EraseConstraint;
}

} // namespace neverc::plugin
