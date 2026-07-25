#include "neverc/Plugin/Host/MCEmissionPlan.h"
#include "MCEmissionBridge.h"
#include "neverc/Plugin/Host/MCPluginBridge.h"
#include "neverc/Plugin/Host/MCUnit.h"
#include "neverc/Plugin/Host/PluginArtifactRegistry.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginPhaseExecutor.h"
#include "neverc/Plugin/Host/PluginPhaseGraph.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTargetRegistry.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "neverc/Plugin/Schema/PluginPhaseSchema.inc"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/MC/MCAsmLayout.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCFragment.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/Errc.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;

namespace neverc::plugin {
namespace {

constexpr uint64_t MarkerMagic = UINT64_C(0x4e434d43454d4b01);
constexpr uint64_t InstructionMagic = UINT64_C(0x4e434d43454d4901);
constexpr uint64_t EncodedMagic = UINT64_C(0x4e434d43454d4501);
constexpr uint64_t LayoutMagic = UINT64_C(0x4e434d43454d4c01);

struct MarkerArtifact {
  uint64_t Magic = MarkerMagic;
};

struct InstructionArtifact {
  uint64_t Magic = InstructionMagic;
  MCInst Instruction;
};

struct EncodedArtifact {
  uint64_t Magic = EncodedMagic;
};

struct LayoutArtifact {
  uint64_t Magic = LayoutMagic;
};

struct SectionLayoutRecord {
  std::string Name;
  uint64_t AddressSize = 0;
  uint64_t FileSize = 0;
  uint64_t FragmentCount = 0;
};

struct FragmentLayoutRecord {
  uint64_t SectionIndex = 0;
  uint64_t FragmentIndex = 0;
  uint64_t Offset = 0;
  uint64_t Size = 0;
  NevercMCFragmentKind Kind = NEVERC_MC_FRAGMENT_FORMAT_EXTENSION;
};

struct SymbolLayoutRecord {
  std::string Name;
  uint64_t Value = 0;
  bool IsDefined = false;
  bool IsResolved = false;
};

struct FixupLayoutRecord {
  uint64_t SectionIndex = 0;
  uint64_t FragmentIndex = 0;
  uint64_t Offset = 0;
  uint64_t Value = 0;
  uint32_t Kind = 0;
  bool IsResolved = false;
};

NevercStatus status(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

bool sameID(NevercInterfaceID Left, NevercInterfaceID Right) {
  return Left.High == Right.High && Left.Low == Right.Low;
}

bool sameHandle(NevercHandle Left, NevercHandle Right) {
  return Left.Owner == Right.Owner && Left.Value == Right.Value;
}

NevercStringView stringView(StringRef Text) {
  return {Text.data(), static_cast<uint64_t>(Text.size())};
}

template <typename T>
NevercStatus writeRecord(T *OutValue, const T &Value) {
  if (!OutValue)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  const uint32_t Capacity = OutValue->Header.StructSize;
  if (Capacity < sizeof(NevercABITableHeader))
    return status(NEVERC_STATUS_ABI_MISMATCH);
  const size_t Writable = std::min<size_t>(Capacity, sizeof(Value));
  std::memset(OutValue, 0, Writable);
  std::memcpy(OutValue, &Value, Writable);
  return Capacity < sizeof(Value) ? status(NEVERC_STATUS_ABI_MISMATCH)
                                  : neverc_status_ok();
}

NevercInterfaceID unitArtifactID() {
  return {NEVERC_ARTIFACT_MC_UNIT_HIGH, NEVERC_ARTIFACT_MC_UNIT_LOW};
}

NevercInterfaceID encodedArtifactID() {
  return {NEVERC_ARTIFACT_MC_ENCODED_HIGH,
          NEVERC_ARTIFACT_MC_ENCODED_LOW};
}

NevercInterfaceID layoutArtifactID() {
  return {NEVERC_ARTIFACT_MC_LAYOUT_HIGH,
          NEVERC_ARTIFACT_MC_LAYOUT_LOW};
}

NevercInterfaceID instructionArtifactID() {
  return {NEVERC_ARTIFACT_MC_INSTRUCTION_HIGH,
          NEVERC_ARTIFACT_MC_INSTRUCTION_LOW};
}

#define NEVERC_EMISSION_PHASE_ID(Symbol)                                   \
  NevercInterfaceID{NEVERC_PHASE_MC_EMISSION_##Symbol##_HIGH,              \
                    NEVERC_PHASE_MC_EMISSION_##Symbol##_LOW}

NevercInterfaceID unitBeginPhaseID() {
  return NEVERC_EMISSION_PHASE_ID(UNIT_BEGIN);
}
NevercInterfaceID unitEndPhaseID() {
  return NEVERC_EMISSION_PHASE_ID(UNIT_END);
}
NevercInterfaceID sectionChangePhaseID() {
  return NEVERC_EMISSION_PHASE_ID(SECTION_CHANGE);
}
NevercInterfaceID preInstructionPhaseID() {
  return NEVERC_EMISSION_PHASE_ID(PRE_INSTRUCTION);
}
NevercInterfaceID postInstructionPhaseID() {
  return NEVERC_EMISSION_PHASE_ID(POST_INSTRUCTION);
}
NevercInterfaceID postEncodePhaseID() {
  return NEVERC_EMISSION_PHASE_ID(POST_ENCODE);
}
NevercInterfaceID fixupPhaseID() {
  return NEVERC_EMISSION_PHASE_ID(FIXUP);
}
NevercInterfaceID relaxationRoundPhaseID() {
  return NEVERC_EMISSION_PHASE_ID(RELAXATION_ROUND);
}
NevercInterfaceID preLayoutPhaseID() {
  return NEVERC_EMISSION_PHASE_ID(PRE_LAYOUT);
}
NevercInterfaceID postLayoutPhaseID() {
  return NEVERC_EMISSION_PHASE_ID(POST_LAYOUT);
}

#undef NEVERC_EMISSION_PHASE_ID

NevercMCFragmentKind mapFragmentKind(MCFragment::FragmentType Kind) {
  switch (Kind) {
  case MCFragment::FT_Align:
    return NEVERC_MC_FRAGMENT_ALIGN;
  case MCFragment::FT_Data:
    return NEVERC_MC_FRAGMENT_DATA;
  case MCFragment::FT_CompactEncodedInst:
    return NEVERC_MC_FRAGMENT_ENCODED_WITH_FIXUPS;
  case MCFragment::FT_Fill:
    return NEVERC_MC_FRAGMENT_FILL;
  case MCFragment::FT_Nops:
    return NEVERC_MC_FRAGMENT_NOP;
  case MCFragment::FT_Relaxable:
    return NEVERC_MC_FRAGMENT_RELAXABLE;
  case MCFragment::FT_Org:
    return NEVERC_MC_FRAGMENT_ORG;
  case MCFragment::FT_Dwarf:
    return NEVERC_MC_FRAGMENT_DEBUG;
  case MCFragment::FT_DwarfFrame:
    return NEVERC_MC_FRAGMENT_CFI;
  case MCFragment::FT_LEB:
    return NEVERC_MC_FRAGMENT_LEB;
  case MCFragment::FT_BoundaryAlign:
    return NEVERC_MC_FRAGMENT_BOUNDARY_ALIGN;
  case MCFragment::FT_SymbolId:
    return NEVERC_MC_FRAGMENT_SYMBOL_ID;
  case MCFragment::FT_PseudoProbe:
    return NEVERC_MC_FRAGMENT_PSEUDO_PROBE;
  case MCFragment::FT_Dummy:
    return NEVERC_MC_FRAGMENT_DUMMY;
  }
  return NEVERC_MC_FRAGMENT_FORMAT_EXTENSION;
}

Error verifyMarker(const void *Payload) {
  if (!Payload ||
      static_cast<const MarkerArtifact *>(Payload)->Magic != MarkerMagic)
    return createStringError(inconvertibleErrorCode(),
                             "invalid MC emission marker artifact");
  return Error::success();
}

Error verifyInstruction(const void *Payload) {
  if (!Payload || static_cast<const InstructionArtifact *>(Payload)->Magic !=
                      InstructionMagic)
    return createStringError(inconvertibleErrorCode(),
                             "invalid MC instruction artifact");
  return Error::success();
}

Error verifyEncoded(const void *Payload) {
  if (!Payload ||
      static_cast<const EncodedArtifact *>(Payload)->Magic != EncodedMagic)
    return createStringError(inconvertibleErrorCode(),
                             "invalid MC encoded artifact");
  return Error::success();
}

Error verifyLayout(const void *Payload) {
  if (!Payload ||
      static_cast<const LayoutArtifact *>(Payload)->Magic != LayoutMagic)
    return createStringError(inconvertibleErrorCode(),
                             "invalid MC layout artifact");
  return Error::success();
}

} // namespace

struct MCEmissionPlan::Impl final : MCEmissionRuntimeAccess {
  PluginTaskContext &Task;
  std::shared_ptr<const PluginTargetSnapshot> Snapshot;
  const PluginTargetSnapshot::TargetRecord *Target = nullptr;
  const PluginTargetSnapshot::NamedRecord *Schema = nullptr;
  std::shared_ptr<MCEmissionProcessService> Service;
  PluginPhaseGraph Graph;
  PluginArtifactRegistry Artifacts;
  std::unique_ptr<PluginPhaseExecutor> Executor;

  std::string TargetTriple;
  std::string CPU;
  std::string Features;
  std::string ObjectFormat;

  NevercInterfaceID ActivePhase{};
  NevercMCEmissionEventKind ActiveKind = 0;
  const MCInst *ActiveInstruction = nullptr;
  StringRef ActiveSection;
  StringRef ActiveBytes;
  unsigned ActiveRound = 0;
  bool ActiveLayoutChanged = false;
  FixupLayoutRecord ActiveFixup;
  bool HasActiveFixup = false;
  uint64_t EventGeneration = 0;

  std::vector<SectionLayoutRecord> Sections;
  std::vector<FragmentLayoutRecord> Fragments;
  std::vector<SymbolLayoutRecord> Symbols;
  std::vector<FixupLayoutRecord> Fixups;

  PluginMCUnit ReadUnit;
  std::unique_ptr<MCPluginBridge> ReadBridge;
  const void *ReadPayload = nullptr;

  PluginMCUnit ReplacementUnit;
  std::unique_ptr<MCPluginBridge> ReplacementBridge;
  bool ReplacementStarted = false;

  Impl(PluginTaskContext &TaskValue,
       std::shared_ptr<const PluginTargetSnapshot> SnapshotValue,
       std::shared_ptr<MCEmissionProcessService> ServiceValue,
       PluginPhaseGraph GraphValue)
      : Task(TaskValue), Snapshot(std::move(SnapshotValue)),
        Service(std::move(ServiceValue)), Graph(std::move(GraphValue)) {}

  NevercTaskHandle taskHandle() const override { return Task.handle(); }

  Error initialize() {
    if (Snapshot) {
      Target = Snapshot->selectedTarget();
      if (Target)
        Schema = Snapshot->findMCSchema(Target->MCSchemaID);
      if (const OwnedTargetKey *Key = Snapshot->targetKey()) {
        NevercTargetKey View = Key->view();
        TargetTriple =
            StringRef(View.RawTriple.Data,
                      static_cast<size_t>(View.RawTriple.Length))
                .str();
        CPU = StringRef(View.CPU.Data,
                        static_cast<size_t>(View.CPU.Length))
                  .str();
        const auto *Data =
            reinterpret_cast<const uint8_t *>(View.Features.Data);
        for (uint64_t Index = 0; Index != View.Features.Count; ++Index) {
          const auto *Feature =
              reinterpret_cast<const NevercStringView *>(
                  Data + Index * View.Features.ElementStride);
          if (!Features.empty())
            Features.push_back(',');
          Features.append(Feature->Data,
                          static_cast<size_t>(Feature->Length));
        }
        if (Target && Target->DefaultObjectFormatID.High != 0)
          if (const auto *Format = Snapshot->findObjectFormat(
                  Target->DefaultObjectFormatID))
            ObjectFormat = Format->CanonicalName;
      }
      if (TargetTriple.empty() && Target) {
        TargetTriple = Target->Machine.RawTriple;
        CPU = Target->Machine.DefaultCPU;
      }
    }

    auto RegisterType = [&](PluginArtifactTypeDescriptor Descriptor) -> Error {
      auto Registered = Artifacts.registerType(std::move(Descriptor));
      return Registered ? Error::success() : Registered.takeError();
    };
    if (Error E = RegisterType(
            {unitArtifactID(), "mc.unit",
             PluginArtifactOwnership::Borrowed, {}, {}, verifyMarker}))
      return E;
    if (Error E = RegisterType(
            {encodedArtifactID(), "mc.encoded",
             PluginArtifactOwnership::Borrowed, {}, {}, verifyEncoded}))
      return E;
    if (Error E = RegisterType(
            {layoutArtifactID(), "mc.layout",
             PluginArtifactOwnership::Borrowed, {}, {}, verifyLayout}))
      return E;
    if (Error E = RegisterType(
            {instructionArtifactID(), "mc.instruction",
             PluginArtifactOwnership::Owned, {},
             [](void *Payload) {
               delete static_cast<InstructionArtifact *>(Payload);
             },
             verifyInstruction}))
      return E;
    if (Error E = Artifacts.freeze())
      return E;

    Executor = std::make_unique<PluginPhaseExecutor>(Graph, Artifacts);
    if (Error E = Executor->importSessionRegistrations(Task.session()))
      return E;
    if (Error E = Executor->setBuiltinProvider(
            preInstructionPhaseID(),
            [this](const NevercPhaseFrame *Frame,
                   NevercPhaseResult *Result) {
              return builtinInstruction(Frame, Result);
            }))
      return E;
    return Executor->freeze();
  }

  bool empty() const {
    for (NevercInterfaceID Phase :
         {unitBeginPhaseID(), unitEndPhaseID(),
          sectionChangePhaseID(), preInstructionPhaseID(),
          postInstructionPhaseID(), postEncodePhaseID(), fixupPhaseID(),
          relaxationRoundPhaseID(), preLayoutPhaseID(),
          postLayoutPhaseID()})
      if (Executor->hasBindings(Phase))
        return false;
    return true;
  }

  NevercPhaseRoute route(StringRef FallbackTriple = {}) const {
    NevercPhaseRoute Route{};
    Route.Header = {sizeof(Route), NEVERC_PLUGIN_ABI_MAJOR,
                    NEVERC_PLUGIN_ABI_MINOR, 0};
    Route.TargetTriple =
        stringView(TargetTriple.empty() ? FallbackTriple : TargetTriple);
    Route.CPU = stringView(CPU);
    Route.Features = stringView(Features);
    Route.ObjectFormat = stringView(ObjectFormat);
    return Route;
  }

  void resetInvocation() {
    ReadBridge.reset();
    ReadUnit = PluginMCUnit();
    ReadPayload = nullptr;
    ReplacementBridge.reset();
    ReplacementUnit = PluginMCUnit();
    ReplacementStarted = false;
    ActivePhase = {};
    ActiveKind = 0;
    ActiveInstruction = nullptr;
    ActiveSection = {};
    ActiveBytes = {};
    ActiveRound = 0;
    ActiveLayoutChanged = false;
    ActiveFixup = {};
    HasActiveFixup = false;
  }

  Error beginInvocation(NevercInterfaceID Phase,
                        NevercMCEmissionEventKind Kind) {
    ActivePhase = Phase;
    ActiveKind = Kind;
    if (Error E = Service->attach(*this)) {
      resetInvocation();
      return E;
    }
    return Error::success();
  }

  Error notify(NevercInterfaceID Phase, NevercMCEmissionEventKind Kind,
               NevercInterfaceID ArtifactType, const void *Payload,
               StringRef FallbackTriple = {}) {
    // Callers stash event context (ActiveInstruction, ActiveBytes, ...) that
    // points into their own stack frame before calling here, so every exit has
    // to clear it -- including the no-bindings early return below. Otherwise a
    // later event reads a dangling pointer.
    auto Clear = make_scope_exit([&] { resetInvocation(); });
    if (!Executor->hasBindings(Phase))
      return Error::success();
    if (Error E = beginInvocation(Phase, Kind))
      return E;
    auto End = make_scope_exit([&] { Service->detach(Task.handle()); });
    auto Artifact = Executor->createArtifactView(
        Task, ArtifactType, Payload, ++EventGeneration);
    if (!Artifact)
      return Artifact.takeError();
    NevercArtifactHandle Handle = *Artifact;
    auto Release = make_scope_exit([&] {
      (void)Task.handles().release(Handle, PluginArtifactHandleKind);
    });
    return Executor->notify(Task.session(), Task, Phase,
                            route(FallbackTriple), Handle);
  }

  Expected<MCInst> interceptInstruction(
      const MCInst &Instruction, StringRef FallbackTriple) {
    NevercInterfaceID Phase = preInstructionPhaseID();
    if (!Executor->hasBindings(Phase))
      return Instruction;
    if (Error E =
            beginInvocation(Phase, NEVERC_MC_EMISSION_PRE_INSTRUCTION))
      return std::move(E);
    auto End = make_scope_exit([&] {
      Service->detach(Task.handle());
      resetInvocation();
    });
    InstructionArtifact Input{InstructionMagic, Instruction};
    ActiveInstruction = &Input.Instruction;
    auto InputHandle = Executor->createArtifactView(
        Task, instructionArtifactID(), &Input, ++EventGeneration);
    if (!InputHandle)
      return InputHandle.takeError();
    NevercArtifactHandle Handle = *InputHandle;
    auto Release = make_scope_exit([&] {
      (void)Task.handles().release(Handle, PluginArtifactHandleKind);
    });
    auto Type = Artifacts.find(instructionArtifactID());
    if (!Type)
      return createStringError(inconvertibleErrorCode(),
                               "MC instruction artifact type is unavailable");
    PluginArtifactSlot Output(Type);
    if (Error E = Executor->execute(
            Task.session(), Task, Phase, route(FallbackTriple), Handle,
            Output))
      return std::move(E);
    const auto *Published =
        static_cast<const InstructionArtifact *>(Output.payload());
    if (!Published)
      return createStringError(inconvertibleErrorCode(),
                               "MC instruction interceptor published no output");
    return Published->Instruction;
  }

  NevercStatus builtinInstruction(const NevercPhaseFrame *Frame,
                                  NevercPhaseResult *Result) {
    if (!Frame || !Result || !sameID(Frame->Phase, preInstructionPhaseID()))
      return status(NEVERC_STATUS_WRONG_SCOPE);
    const void *Payload = nullptr;
    NevercStatus Resolved = Executor->resolveArtifactPayload(
        Task, Frame->Input, instructionArtifactID(), &Payload);
    if (Resolved.Code != NEVERC_STATUS_OK)
      return Resolved;
    const auto *Input = static_cast<const InstructionArtifact *>(Payload);
    auto *Candidate =
        new InstructionArtifact{InstructionMagic, Input->Instruction};
    auto Handle = Executor->createCandidate(
        Task, instructionArtifactID(), Candidate);
    if (!Handle) {
      delete Candidate;
      consumeError(Handle.takeError());
      return status(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    Result->Action = NEVERC_PHASE_REPLACE;
    Result->Output = *Handle;
    return neverc_status_ok();
  }

  bool validFrame(const NevercPhaseFrame *Frame) const {
    return Frame && sameHandle(Frame->Task, Task.handle()) &&
           sameID(Frame->Phase, ActivePhase);
  }

  NevercInterfaceID activeArtifactType() const {
    if (sameID(ActivePhase, preInstructionPhaseID()) ||
        sameID(ActivePhase, postInstructionPhaseID()))
      return instructionArtifactID();
    if (sameID(ActivePhase, postEncodePhaseID()))
      return encodedArtifactID();
    if (sameID(ActivePhase, postLayoutPhaseID()))
      return layoutArtifactID();
    return unitArtifactID();
  }

  NevercStatus resolveActiveArtifact(
      const NevercPhaseFrame *Frame, NevercArtifactHandle Artifact,
      const void **OutPayload) {
    if (!validFrame(Frame) || !OutPayload ||
        (!sameHandle(Artifact, Frame->Input) &&
         !sameHandle(Artifact, Frame->CurrentOutput)))
      return status(NEVERC_STATUS_WRONG_SCOPE);
    return Executor->resolveArtifactPayload(
        Task, Artifact, activeArtifactType(), OutPayload);
  }

  Error buildReadView(const MCInst &Instruction, const void *Payload) {
    if (ReadBridge && ReadPayload == Payload)
      return Error::success();
    ReadBridge.reset();
    ReadUnit = PluginMCUnit();
    if (Target && Schema)
      ReadUnit.setTargetIdentity(Target->ID, Schema->Digest);
    MCInst &Stored =
        ReadUnit.append(std::make_unique<MCInst>(Instruction));
    ReadBridge =
        std::make_unique<MCPluginBridge>(Task, ReadUnit, Schema, false);
    auto Unit = ReadBridge->unit();
    if (!Unit)
      return Unit.takeError();
    auto Wrapped = ReadBridge->wrapInstruction(Stored);
    if (!Wrapped)
      return Wrapped.takeError();
    ReadPayload = Payload;
    return Error::success();
  }

  NevercStatus getEvent(
      const NevercPhaseFrame *Frame, NevercArtifactHandle Artifact,
      NevercMCEmissionEventInfo *OutInfo) override {
    const void *Payload = nullptr;
    NevercStatus Resolved =
        resolveActiveArtifact(Frame, Artifact, &Payload);
    if (Resolved.Code != NEVERC_STATUS_OK)
      return Resolved;

    const MCInst *Instruction = ActiveInstruction;
    if (Payload && sameID(activeArtifactType(), instructionArtifactID()))
      Instruction =
          &static_cast<const InstructionArtifact *>(Payload)->Instruction;
    if (Instruction)
      if (Error E = buildReadView(*Instruction, Payload)) {
        consumeError(std::move(E));
        return status(NEVERC_STATUS_RESOURCE_EXHAUSTED);
      }

    NevercMCEmissionEventInfo Value{};
    Value.Header = {sizeof(Value), NEVERC_MC_EMISSION_API_MAJOR,
                    NEVERC_MC_EMISSION_API_MINOR, 0};
    Value.Kind = ActiveKind;
    Value.RelaxationRound = ActiveRound;
    Value.SectionName = stringView(ActiveSection);
    Value.EncodedBytes = {
        reinterpret_cast<const uint8_t *>(ActiveBytes.data()),
        static_cast<uint64_t>(ActiveBytes.size())};
    Value.LayoutChanged =
        ActiveLayoutChanged ? NEVERC_TRUE : NEVERC_FALSE;
    if (!ActiveSection.empty())
      Value.Flags |= NEVERC_MC_EMISSION_HAS_SECTION;
    if (Instruction) {
      MCInst *Stored = ReadUnit.at(0);
      if (!ReadBridge || !Stored)
        return status(NEVERC_STATUS_RESOURCE_EXHAUSTED);
      Value.Flags |= NEVERC_MC_EMISSION_HAS_INSTRUCTION;
      Value.MC = &ReadBridge->api();
      auto Unit = ReadBridge->unit();
      auto Wrapped = ReadBridge->wrapInstruction(*Stored);
      if (!Unit || !Wrapped) {
        if (!Unit)
          consumeError(Unit.takeError());
        if (!Wrapped)
          consumeError(Wrapped.takeError());
        return status(NEVERC_STATUS_RESOURCE_EXHAUSTED);
      }
      Value.Unit = *Unit;
      Value.Instruction = *Wrapped;
    }
    if (!ActiveBytes.empty())
      Value.Flags |= NEVERC_MC_EMISSION_HAS_ENCODING;
    if (HasActiveFixup) {
      Value.Flags |= NEVERC_MC_EMISSION_HAS_FIXUP;
      Value.Fixup.Header = {
          sizeof(Value.Fixup), NEVERC_MC_EMISSION_API_MAJOR,
          NEVERC_MC_EMISSION_API_MINOR, 0};
      Value.Fixup.SectionIndex = ActiveFixup.SectionIndex;
      Value.Fixup.FragmentIndex = ActiveFixup.FragmentIndex;
      Value.Fixup.Offset = ActiveFixup.Offset;
      Value.Fixup.Value = ActiveFixup.Value;
      Value.Fixup.Kind = ActiveFixup.Kind;
      Value.Fixup.IsResolved =
          ActiveFixup.IsResolved ? NEVERC_TRUE : NEVERC_FALSE;
    }
    if (ActiveKind == NEVERC_MC_EMISSION_POST_LAYOUT) {
      Value.Flags |= NEVERC_MC_EMISSION_HAS_LAYOUT;
      Value.LayoutSectionCount = Sections.size();
      Value.LayoutFragmentCount = Fragments.size();
      Value.LayoutSymbolCount = Symbols.size();
      Value.LayoutFixupCount = Fixups.size();
    }
    if (ActiveKind == NEVERC_MC_EMISSION_PRE_INSTRUCTION)
      Value.Flags |= NEVERC_MC_EMISSION_CAN_REPLACE_INSTRUCTION;
    return writeRecord(OutInfo, Value);
  }

  NevercStatus beginInstructionReplacement(
      const NevercPhaseFrame *Frame,
      NevercPhaseContinuation *Continuation,
      const NevercMCAPI **OutMC, NevercMCUnitHandle *OutUnit,
      NevercMCInstHandle *OutInstruction) override {
    *OutMC = nullptr;
    *OutUnit = {};
    *OutInstruction = {};
    if (!validFrame(Frame) ||
        !sameID(ActivePhase, preInstructionPhaseID()) ||
        !Executor->isActiveContinuation(Frame, Continuation))
      return status(NEVERC_STATUS_WRONG_SCOPE);
    const void *Payload = nullptr;
    NevercStatus Resolved = Executor->resolveArtifactPayload(
        Task, Frame->Input, instructionArtifactID(), &Payload);
    if (Resolved.Code != NEVERC_STATUS_OK)
      return Resolved;
    const auto *Input = static_cast<const InstructionArtifact *>(Payload);

    ReplacementBridge.reset();
    ReplacementUnit = PluginMCUnit();
    if (Target && Schema)
      ReplacementUnit.setTargetIdentity(Target->ID, Schema->Digest);
    MCInst &Stored = ReplacementUnit.append(
        std::make_unique<MCInst>(Input->Instruction));
    ReplacementBridge =
        std::make_unique<MCPluginBridge>(Task, ReplacementUnit, Schema, true);
    auto Unit = ReplacementBridge->unit();
    auto Wrapped = ReplacementBridge->wrapInstruction(Stored);
    if (!Unit || !Wrapped) {
      if (!Unit)
        consumeError(Unit.takeError());
      if (!Wrapped)
        consumeError(Wrapped.takeError());
      ReplacementBridge.reset();
      ReplacementStarted = false;
      return status(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    ReplacementStarted = true;
    *OutMC = &ReplacementBridge->api();
    *OutUnit = *Unit;
    *OutInstruction = *Wrapped;
    return neverc_status_ok();
  }

  NevercStatus publishInstructionReplacement(
      const NevercPhaseFrame *Frame,
      NevercPhaseContinuation *Continuation,
      NevercArtifactHandle *OutInstruction) override {
    *OutInstruction = {};
    if (!validFrame(Frame) ||
        !sameID(ActivePhase, preInstructionPhaseID()) ||
        !Executor->isActiveContinuation(Frame, Continuation) ||
        !ReplacementStarted || !ReplacementBridge)
      return status(NEVERC_STATUS_WRONG_SCOPE);
    if (ReplacementBridge->hasActiveMutation() ||
        ReplacementUnit.size() != 1)
      return status(NEVERC_STATUS_INVALID_STATE);
    if (Schema)
      if (Error E = verifyPluginMCUnit(ReplacementUnit, Schema)) {
        consumeError(std::move(E));
        return status(NEVERC_STATUS_VERIFICATION_FAILED);
      }
    auto *Candidate = new InstructionArtifact{
        InstructionMagic, *ReplacementUnit.at(0)};
    auto Artifact = Executor->createCandidate(
        Task, instructionArtifactID(), Candidate);
    if (!Artifact) {
      delete Candidate;
      consumeError(Artifact.takeError());
      return status(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    ReplacementStarted = false;
    *OutInstruction = *Artifact;
    return neverc_status_ok();
  }

  NevercStatus validateLayoutAccess(
      const NevercPhaseFrame *Frame, NevercArtifactHandle Artifact) {
    if (ActiveKind != NEVERC_MC_EMISSION_POST_LAYOUT)
      return status(NEVERC_STATUS_WRONG_SCOPE);
    const void *Payload = nullptr;
    return resolveActiveArtifact(Frame, Artifact, &Payload);
  }

  NevercStatus getLayoutSection(
      const NevercPhaseFrame *Frame, NevercArtifactHandle Artifact,
      uint64_t Index,
      NevercMCEmissionSectionLayoutInfo *OutInfo) override {
    NevercStatus Valid = validateLayoutAccess(Frame, Artifact);
    if (Valid.Code != NEVERC_STATUS_OK)
      return Valid;
    if (Index >= Sections.size())
      return status(NEVERC_STATUS_INVALID_ARGUMENT);
    const SectionLayoutRecord &Record =
        Sections[static_cast<size_t>(Index)];
    NevercMCEmissionSectionLayoutInfo Value{};
    Value.Header = {sizeof(Value), NEVERC_MC_EMISSION_API_MAJOR,
                    NEVERC_MC_EMISSION_API_MINOR, 0};
    Value.Name = stringView(Record.Name);
    Value.AddressSize = Record.AddressSize;
    Value.FileSize = Record.FileSize;
    Value.FragmentCount = Record.FragmentCount;
    return writeRecord(OutInfo, Value);
  }

  NevercStatus getLayoutFragment(
      const NevercPhaseFrame *Frame, NevercArtifactHandle Artifact,
      uint64_t Index,
      NevercMCEmissionFragmentLayoutInfo *OutInfo) override {
    NevercStatus Valid = validateLayoutAccess(Frame, Artifact);
    if (Valid.Code != NEVERC_STATUS_OK)
      return Valid;
    if (Index >= Fragments.size())
      return status(NEVERC_STATUS_INVALID_ARGUMENT);
    const FragmentLayoutRecord &Record =
        Fragments[static_cast<size_t>(Index)];
    NevercMCEmissionFragmentLayoutInfo Value{};
    Value.Header = {sizeof(Value), NEVERC_MC_EMISSION_API_MAJOR,
                    NEVERC_MC_EMISSION_API_MINOR, 0};
    Value.SectionIndex = Record.SectionIndex;
    Value.FragmentIndex = Record.FragmentIndex;
    Value.Offset = Record.Offset;
    Value.Size = Record.Size;
    Value.Kind = Record.Kind;
    return writeRecord(OutInfo, Value);
  }

  NevercStatus getLayoutSymbol(
      const NevercPhaseFrame *Frame, NevercArtifactHandle Artifact,
      uint64_t Index,
      NevercMCEmissionSymbolLayoutInfo *OutInfo) override {
    NevercStatus Valid = validateLayoutAccess(Frame, Artifact);
    if (Valid.Code != NEVERC_STATUS_OK)
      return Valid;
    if (Index >= Symbols.size())
      return status(NEVERC_STATUS_INVALID_ARGUMENT);
    const SymbolLayoutRecord &Record =
        Symbols[static_cast<size_t>(Index)];
    NevercMCEmissionSymbolLayoutInfo Value{};
    Value.Header = {sizeof(Value), NEVERC_MC_EMISSION_API_MAJOR,
                    NEVERC_MC_EMISSION_API_MINOR, 0};
    Value.Name = stringView(Record.Name);
    Value.Value = Record.Value;
    Value.IsDefined = Record.IsDefined ? NEVERC_TRUE : NEVERC_FALSE;
    Value.IsResolved = Record.IsResolved ? NEVERC_TRUE : NEVERC_FALSE;
    return writeRecord(OutInfo, Value);
  }

  NevercStatus getLayoutFixup(
      const NevercPhaseFrame *Frame, NevercArtifactHandle Artifact,
      uint64_t Index,
      NevercMCEmissionFixupLayoutInfo *OutInfo) override {
    NevercStatus Valid = validateLayoutAccess(Frame, Artifact);
    if (Valid.Code != NEVERC_STATUS_OK)
      return Valid;
    if (Index >= Fixups.size())
      return status(NEVERC_STATUS_INVALID_ARGUMENT);
    const FixupLayoutRecord &Record =
        Fixups[static_cast<size_t>(Index)];
    NevercMCEmissionFixupLayoutInfo Value{};
    Value.Header = {sizeof(Value), NEVERC_MC_EMISSION_API_MAJOR,
                    NEVERC_MC_EMISSION_API_MINOR, 0};
    Value.SectionIndex = Record.SectionIndex;
    Value.FragmentIndex = Record.FragmentIndex;
    Value.Offset = Record.Offset;
    Value.Value = Record.Value;
    Value.Kind = Record.Kind;
    Value.IsResolved =
        Record.IsResolved ? NEVERC_TRUE : NEVERC_FALSE;
    return writeRecord(OutInfo, Value);
  }

  std::pair<uint64_t, uint64_t>
  fragmentIndices(MCAssembler &Assembler,
                  const MCFragment &Fragment) const {
    uint64_t SectionIndex = 0;
    for (const MCSection &Section : Assembler) {
      uint64_t FragmentIndex = 0;
      for (const MCFragment &Current : Section) {
        if (&Current == &Fragment)
          return {SectionIndex, FragmentIndex};
        ++FragmentIndex;
      }
      ++SectionIndex;
    }
    return {std::numeric_limits<uint64_t>::max(),
            std::numeric_limits<uint64_t>::max()};
  }

  void captureLayout(MCAssembler &Assembler,
                     const MCAsmLayout &Layout) {
    Sections.clear();
    Fragments.clear();
    Symbols.clear();
    uint64_t SectionIndex = 0;
    for (const MCSection &Section : Assembler) {
      SectionLayoutRecord SectionRecord;
      SectionRecord.Name = Section.getName().str();
      SectionRecord.AddressSize =
          Layout.getSectionAddressSize(&Section);
      SectionRecord.FileSize = Layout.getSectionFileSize(&Section);
      uint64_t FragmentIndex = 0;
      for (const MCFragment &Fragment : Section) {
        FragmentLayoutRecord Record;
        Record.SectionIndex = SectionIndex;
        Record.FragmentIndex = FragmentIndex++;
        Record.Offset = Layout.getFragmentOffset(&Fragment);
        Record.Size = Assembler.computeFragmentSize(Layout, Fragment);
        Record.Kind = mapFragmentKind(Fragment.getKind());
        Fragments.push_back(Record);
      }
      SectionRecord.FragmentCount = FragmentIndex;
      Sections.push_back(std::move(SectionRecord));
      ++SectionIndex;
    }
    for (const MCSymbol &Symbol : Assembler.symbols()) {
      SymbolLayoutRecord Record;
      Record.Name = Symbol.getName().str();
      Record.IsDefined = Symbol.isDefined();
      Record.IsResolved =
          Record.IsDefined && Layout.getSymbolOffset(Symbol, Record.Value);
      Symbols.push_back(std::move(Record));
    }
  }
};

Expected<std::unique_ptr<MCEmissionPlan>>
MCEmissionPlan::create(PluginTaskContext &Task) {
  auto Service =
      findMCEmissionProcessService(Task.processServices());
  if (!Service)
    return createStringError(
        inconvertibleErrorCode(),
        "MC emission plugin interface is not registered");
  auto Graph = PluginPhaseGraph::createBuiltinMCGraph();
  if (!Graph)
    return Graph.takeError();
  auto Snapshot = findPluginTargetSnapshot(
      Task.processServices(), Task.session().handle());
  auto State = std::make_unique<Impl>(
      Task, std::move(Snapshot), std::move(Service), std::move(*Graph));
  if (Error E = State->initialize())
    return std::move(E);
  return std::unique_ptr<MCEmissionPlan>(
      new MCEmissionPlan(std::move(State)));
}

MCEmissionPlan::MCEmissionPlan(std::unique_ptr<Impl> StateValue)
    : State(std::move(StateValue)) {}

MCEmissionPlan::~MCEmissionPlan() = default;

bool MCEmissionPlan::empty() const { return State->empty(); }

Error MCEmissionPlan::onUnitBegin(MCStreamer &Streamer) {
  MarkerArtifact Artifact;
  return State->notify(unitBeginPhaseID(), NEVERC_MC_EMISSION_UNIT_BEGIN,
                       unitArtifactID(), &Artifact,
                       Streamer.getContext().getTargetTriple().str());
}

Error MCEmissionPlan::onUnitEnd(MCStreamer &Streamer) {
  MarkerArtifact Artifact;
  return State->notify(unitEndPhaseID(), NEVERC_MC_EMISSION_UNIT_END,
                       unitArtifactID(), &Artifact,
                       Streamer.getContext().getTargetTriple().str());
}

Error MCEmissionPlan::onSectionChange(MCStreamer &Streamer,
                                      const MCSection &Section,
                                      const MCExpr *) {
  MarkerArtifact Artifact;
  State->ActiveSection = Section.getName();
  return State->notify(sectionChangePhaseID(),
                       NEVERC_MC_EMISSION_SECTION_CHANGE,
                       unitArtifactID(), &Artifact,
                       Streamer.getContext().getTargetTriple().str());
}

Expected<MCInst> MCEmissionPlan::onPreInstruction(
    MCStreamer &Streamer, const MCInst &Instruction,
    const MCSubtargetInfo &) {
  return State->interceptInstruction(
      Instruction, Streamer.getContext().getTargetTriple().str());
}

Error MCEmissionPlan::onPostInstruction(
    MCStreamer &Streamer, const MCInst &Instruction,
    const MCSubtargetInfo &) {
  InstructionArtifact Artifact{InstructionMagic, Instruction};
  State->ActiveInstruction = &Artifact.Instruction;
  return State->notify(postInstructionPhaseID(),
                       NEVERC_MC_EMISSION_POST_INSTRUCTION,
                       instructionArtifactID(), &Artifact,
                       Streamer.getContext().getTargetTriple().str());
}

Error MCEmissionPlan::onPostEncode(MCContext &Context,
                                   const MCInst &Instruction,
                                   StringRef Bytes,
                                   ArrayRef<MCFixup>) {
  EncodedArtifact Artifact;
  State->ActiveInstruction = &Instruction;
  State->ActiveBytes = Bytes;
  return State->notify(postEncodePhaseID(),
                       NEVERC_MC_EMISSION_POST_ENCODE,
                       encodedArtifactID(), &Artifact,
                       Context.getTargetTriple().str());
}

Error MCEmissionPlan::onFixup(MCAssembler &Assembler,
                              const MCAsmLayout &Layout,
                              const MCFragment &Fragment,
                              const MCFixup &Fixup, uint64_t Value,
                              bool IsResolved) {
  MarkerArtifact Artifact;
  auto [SectionIndex, FragmentIndex] =
      State->fragmentIndices(Assembler, Fragment);
  FixupLayoutRecord Record;
  Record.SectionIndex = SectionIndex;
  Record.FragmentIndex = FragmentIndex;
  Record.Offset =
      Layout.getFragmentOffset(&Fragment) + Fixup.getOffset();
  Record.Value = Value;
  Record.Kind = static_cast<uint32_t>(Fixup.getKind());
  Record.IsResolved = IsResolved;
  State->Fixups.push_back(Record);
  State->ActiveFixup = Record;
  State->HasActiveFixup = true;
  return State->notify(fixupPhaseID(), NEVERC_MC_EMISSION_FIXUP,
                       unitArtifactID(), &Artifact,
                       Assembler.getContext().getTargetTriple().str());
}

Error MCEmissionPlan::onRelaxationRound(
    MCAssembler &Assembler, const MCAsmLayout &, unsigned Round,
    bool Changed) {
  MarkerArtifact Artifact;
  State->ActiveRound = Round;
  State->ActiveLayoutChanged = Changed;
  return State->notify(relaxationRoundPhaseID(),
                       NEVERC_MC_EMISSION_RELAXATION_ROUND,
                       unitArtifactID(), &Artifact,
                       Assembler.getContext().getTargetTriple().str());
}

Error MCEmissionPlan::onPreLayout(MCAssembler &Assembler) {
  MarkerArtifact Artifact;
  State->Fixups.clear();
  State->Sections.clear();
  State->Fragments.clear();
  State->Symbols.clear();
  return State->notify(preLayoutPhaseID(),
                       NEVERC_MC_EMISSION_PRE_LAYOUT,
                       unitArtifactID(), &Artifact,
                       Assembler.getContext().getTargetTriple().str());
}

Error MCEmissionPlan::onPostLayout(MCAssembler &Assembler,
                                   const MCAsmLayout &Layout) {
  LayoutArtifact Artifact;
  State->captureLayout(Assembler, Layout);
  return State->notify(postLayoutPhaseID(),
                       NEVERC_MC_EMISSION_POST_LAYOUT,
                       layoutArtifactID(), &Artifact,
                       Assembler.getContext().getTargetTriple().str());
}

Error MCEmissionPlan::onPreObjectWrite(MCAssembler &,
                                       const MCAsmLayout &) {
  // The replaceable object.pre_write transition is materialized by the
  // ObjectGraph pipeline. The neutral LLVM hook remains available here so
  // that pipeline can attach without adding another LLVM seam.
  return Error::success();
}

} // namespace neverc::plugin
