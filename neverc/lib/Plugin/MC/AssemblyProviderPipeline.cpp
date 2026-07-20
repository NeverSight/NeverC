#include "neverc/Plugin/Host/PluginAssemblyPipeline.h"
#include "neverc/Plugin/Host/MCPluginBridge.h"
#include "neverc/Plugin/Host/MCUnit.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginPhaseExecutor.h"
#include "neverc/Plugin/Host/PluginPhaseGraph.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTargetRegistry.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/JSON.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>
#include <new>
#include <utility>

using namespace llvm;

namespace neverc::plugin {
namespace {

Error pipelineError(const Twine &Message) {
  return createStringError(errc::invalid_argument, Message);
}

NevercStatus pipelineStatus(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

NevercInterfaceID assemblyProviderInterfaceID() {
  return {NEVERC_INTERFACE_ASSEMBLY_PROVIDER_HIGH,
          NEVERC_INTERFACE_ASSEMBLY_PROVIDER_LOW};
}

NevercInterfaceID assemblyParsePhaseID() {
  return {NEVERC_PHASE_ASSEMBLY_PARSE_HIGH,
          NEVERC_PHASE_ASSEMBLY_PARSE_LOW};
}

NevercInterfaceID assemblyPrintPhaseID() {
  return {NEVERC_PHASE_ASSEMBLY_PRINT_HIGH,
          NEVERC_PHASE_ASSEMBLY_PRINT_LOW};
}

NevercInterfaceID mcUnitArtifactID() {
  return {NEVERC_PHASE_ASSEMBLY_PARSE_OUTPUT_HIGH,
          NEVERC_PHASE_ASSEMBLY_PARSE_OUTPUT_LOW};
}

bool sameID(NevercInterfaceID Left, NevercInterfaceID Right) {
  return Left.High == Right.High && Left.Low == Right.Low;
}

bool sameHandle(NevercHandle Left, NevercHandle Right) {
  return Left.Owner == Right.Owner && Left.Value == Right.Value;
}

bool validHeader(const NevercABITableHeader &Header, uint64_t Required,
                 uint16_t Major, uint16_t Minor) {
  return Header.StructSize >= Required && Header.Major == Major &&
         Header.Minor <= Minor && Header.Flags == 0;
}

bool validView(NevercStringView View) {
  if ((!View.Data && View.Length != 0) ||
      View.Length > std::numeric_limits<size_t>::max())
    return false;
  StringRef Text(View.Data ? View.Data : "",
                 static_cast<size_t>(View.Length));
  return !Text.contains('\0') && json::isUTF8(Text);
}

StringRef view(NevercStringView Value) {
  return StringRef(Value.Data ? Value.Data : "",
                   static_cast<size_t>(Value.Length));
}

template <typename T>
NevercStatus writeRecord(T *Output, const T &Value) {
  if (!Output ||
      !validHeader(Output->Header, sizeof(T),
                   NEVERC_ASSEMBLY_PROVIDER_API_MAJOR,
                   NEVERC_ASSEMBLY_PROVIDER_API_MINOR))
    return pipelineStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *Output = Value;
  return neverc_status_ok();
}

PluginPhaseDefinition parsePhase() {
  PluginPhaseDefinition Phase;
  Phase.ID = assemblyParsePhaseID();
  Phase.CanonicalName = NEVERC_PHASE_ASSEMBLY_PARSE_NAME;
  Phase.Domain = NEVERC_PHASE_ASSEMBLY_PARSE_DOMAIN;
  Phase.Verifier = NEVERC_PHASE_ASSEMBLY_PARSE_VERIFIER;
  Phase.InputArtifact = assemblySourceArtifactID();
  Phase.OutputArtifact = mcUnitArtifactID();
  Phase.Policy = NEVERC_PHASE_ASSEMBLY_PARSE_POLICY;
  Phase.ObserverPoints = NEVERC_PHASE_ASSEMBLY_PARSE_OBSERVER_POINTS;
  Phase.Gate = PluginPhaseGateKind::Transition;
  Phase.Stability = PluginPhaseStability::Experimental;
  Phase.HasBuiltinFallback =
      NEVERC_PHASE_ASSEMBLY_PARSE_BUILTIN_FALLBACK != 0;
  return Phase;
}

PluginPhaseDefinition printPhase() {
  PluginPhaseDefinition Phase;
  Phase.ID = assemblyPrintPhaseID();
  Phase.CanonicalName = NEVERC_PHASE_ASSEMBLY_PRINT_NAME;
  Phase.Domain = NEVERC_PHASE_ASSEMBLY_PRINT_DOMAIN;
  Phase.Verifier = NEVERC_PHASE_ASSEMBLY_PRINT_VERIFIER;
  Phase.InputArtifact = mcUnitArtifactID();
  Phase.OutputArtifact = assemblyOutputArtifactID();
  Phase.Policy = NEVERC_PHASE_ASSEMBLY_PRINT_POLICY;
  Phase.ObserverPoints = NEVERC_PHASE_ASSEMBLY_PRINT_OBSERVER_POINTS;
  Phase.Gate = PluginPhaseGateKind::Transition;
  Phase.Stability = PluginPhaseStability::Experimental;
  Phase.HasBuiltinFallback =
      NEVERC_PHASE_ASSEMBLY_PRINT_BUILTIN_FALLBACK != 0;
  return Phase;
}

} // namespace

struct PluginAssemblyPipelineRuntime::Impl {
  enum class ActiveMode : uint8_t { None, Parse, Print };

  class ProviderService final : public PluginHostService {
  public:
    ProviderService() {
      API.Header = {sizeof(API), NEVERC_ASSEMBLY_PROVIDER_API_MAJOR,
                    NEVERC_ASSEMBLY_PROVIDER_API_MINOR, 0};
      API.Context = this;
      API.GetParseInput = getParseInput;
      API.PeekSourceToken = peekSourceToken;
      API.AdvanceSourceToken = advanceSourceToken;
      API.GetParseMCBuilder = getParseMCBuilder;
      API.PublishParsedMCUnit = publishParsedMCUnit;
      API.GetPrintInput = getPrintInput;
      API.WritePrintOutput = writePrintOutput;
      API.PublishAssemblyOutput = publishAssemblyOutput;
    }

    const NevercAssemblyProviderAPI &api() const { return API; }

    Error attach(Impl &Runtime) {
      std::lock_guard<std::mutex> Lock(Mutex);
      const auto Key =
          std::make_pair(Runtime.Task.handle().Owner,
                         Runtime.Task.handle().Value);
      if (!Active.emplace(Key, &Runtime).second)
        return pipelineError(
            "assembly provider runtime is already active for this task");
      return Error::success();
    }

    void detach(NevercTaskHandle Task) {
      std::lock_guard<std::mutex> Lock(Mutex);
      Active.erase(std::make_pair(Task.Owner, Task.Value));
    }

    void taskScopeUnregistered(NevercTaskHandle Task) noexcept override {
      detach(Task);
    }

  private:
    Impl *find(NevercTaskHandle Task) {
      std::lock_guard<std::mutex> Lock(Mutex);
      auto It = Active.find(std::make_pair(Task.Owner, Task.Value));
      return It == Active.end() ? nullptr : It->second;
    }

    static ProviderService *service(void *Context) {
      return static_cast<ProviderService *>(Context);
    }

    static NevercStatus NEVERC_CALL getParseInput(
        void *Context, const NevercPhaseFrame *Frame,
        NevercArtifactHandle Input,
        NevercAssemblyParseInputInfo *OutInfo) {
      if (!Context || !Frame)
        return pipelineStatus(NEVERC_STATUS_INVALID_ARGUMENT);
      Impl *Runtime = service(Context)->find(Frame->Task);
      return Runtime ? Runtime->getParseInput(Frame, Input, OutInfo)
                     : pipelineStatus(NEVERC_STATUS_STALE_HANDLE);
    }

    static NevercStatus NEVERC_CALL peekSourceToken(
        void *Context, const NevercPhaseFrame *Frame,
        NevercAssemblySourceCursorHandle Cursor,
        NevercAssemblyTokenInfo *OutToken) {
            if (!Context || !Frame)
              return pipelineStatus(NEVERC_STATUS_INVALID_ARGUMENT);
            Impl *Runtime = service(Context)->find(Frame->Task);
            return Runtime
                       ? Runtime->peekSourceToken(
                             Frame, Cursor, OutToken)
                       : pipelineStatus(NEVERC_STATUS_STALE_HANDLE);
    }

    static NevercStatus NEVERC_CALL advanceSourceToken(
        void *Context, const NevercPhaseFrame *Frame,
        NevercAssemblySourceCursorHandle Cursor) {
            if (!Context || !Frame)
              return pipelineStatus(NEVERC_STATUS_INVALID_ARGUMENT);
            Impl *Runtime = service(Context)->find(Frame->Task);
            return Runtime
                       ? Runtime->advanceSourceToken(Frame, Cursor)
                       : pipelineStatus(NEVERC_STATUS_STALE_HANDLE);
    }

    static NevercStatus NEVERC_CALL getParseMCBuilder(
        void *Context, const NevercPhaseFrame *Frame,
        const NevercMCAPI **OutMC, NevercMCUnitHandle *OutUnit) {
      if (!Context || !Frame)
        return pipelineStatus(NEVERC_STATUS_INVALID_ARGUMENT);
      Impl *Runtime = service(Context)->find(Frame->Task);
      return Runtime ? Runtime->getParseMCBuilder(Frame, OutMC, OutUnit)
                     : pipelineStatus(NEVERC_STATUS_STALE_HANDLE);
    }

    static NevercStatus NEVERC_CALL publishParsedMCUnit(
        void *Context, const NevercPhaseFrame *Frame,
        NevercArtifactHandle *OutUnit) {
      if (!Context || !Frame)
        return pipelineStatus(NEVERC_STATUS_INVALID_ARGUMENT);
      Impl *Runtime = service(Context)->find(Frame->Task);
      return Runtime ? Runtime->publishParsedMCUnit(Frame, OutUnit)
                     : pipelineStatus(NEVERC_STATUS_STALE_HANDLE);
    }

    static NevercStatus NEVERC_CALL getPrintInput(
        void *Context, const NevercPhaseFrame *Frame,
        NevercArtifactHandle Input,
        NevercAssemblyPrintInputInfo *OutInfo,
        const NevercMCAPI **OutMC, NevercMCUnitHandle *OutUnit) {
      if (!Context || !Frame)
        return pipelineStatus(NEVERC_STATUS_INVALID_ARGUMENT);
      Impl *Runtime = service(Context)->find(Frame->Task);
      return Runtime
                 ? Runtime->getPrintInput(
                       Frame, Input, OutInfo, OutMC, OutUnit)
                 : pipelineStatus(NEVERC_STATUS_STALE_HANDLE);
    }

    static NevercStatus NEVERC_CALL writePrintOutput(
        void *Context, const NevercPhaseFrame *Frame,
        NevercStringView Text) {
      if (!Context || !Frame)
        return pipelineStatus(NEVERC_STATUS_INVALID_ARGUMENT);
      Impl *Runtime = service(Context)->find(Frame->Task);
      return Runtime ? Runtime->writePrintOutput(Frame, Text)
                     : pipelineStatus(NEVERC_STATUS_STALE_HANDLE);
    }

    static NevercStatus NEVERC_CALL publishAssemblyOutput(
        void *Context, const NevercPhaseFrame *Frame,
        const NevercAssemblyOutputMetadata *Metadata,
        NevercArtifactHandle *OutOutput) {
      if (!Context || !Frame)
        return pipelineStatus(NEVERC_STATUS_INVALID_ARGUMENT);
      Impl *Runtime = service(Context)->find(Frame->Task);
      return Runtime
                 ? Runtime->publishAssemblyOutput(
                       Frame, Metadata, OutOutput)
                 : pipelineStatus(NEVERC_STATUS_STALE_HANDLE);
    }

    NevercAssemblyProviderAPI API{};
    std::mutex Mutex;
    std::map<std::pair<uint64_t, uint64_t>, Impl *> Active;
  };

  Impl(PluginTaskContext &TaskValue,
       std::shared_ptr<const PluginTargetSnapshot> SnapshotValue,
       std::shared_ptr<ProviderService> ServiceValue)
      : Task(TaskValue), Snapshot(std::move(SnapshotValue)),
        Service(std::move(ServiceValue)) {}

  Error initialize() {
    if (!Snapshot)
      return pipelineError("assembly pipeline has no Target snapshot");
    if (Error E = Graph.addPhase(parsePhase()))
      return E;
    if (Error E = Graph.addPhase(printPhase()))
      return E;
    if (Error E = Graph.addEdge(
            assemblyParsePhaseID(), assemblyPrintPhaseID(), true))
      return E;
    if (Error E = Graph.finalize())
      return E;

    auto AssemblyTypes = registerAssemblyArtifactTypes(Artifacts);
    if (!AssemblyTypes)
      return AssemblyTypes.takeError();
    auto MCType = Artifacts.registerType(
        {mcUnitArtifactID(), "mc.unit", PluginArtifactOwnership::Borrowed,
         {}, {},
         [](const void *Payload) -> Error {
           if (!Payload)
             return pipelineError("MC unit artifact payload is null");
           return Error::success();
         }});
    if (!MCType)
      return MCType.takeError();
    if (Error E = Artifacts.freeze())
      return E;

    Executor = std::make_unique<PluginPhaseExecutor>(Graph, Artifacts);
    if (Error E = Executor->importSessionRegistrations(Task.session()))
      return E;
    ReplaceParser = Executor->hasProvider(assemblyParsePhaseID());
    ReplacePrinter = Executor->hasProvider(assemblyPrintPhaseID());
    return Executor->freeze();
  }

  NevercPhaseRoute route(
      const PluginTargetSnapshot::TargetRecord &Target) const {
    NevercPhaseRoute Route{};
    Route.Header = {sizeof(Route), NEVERC_PLUGIN_ABI_MAJOR,
                    NEVERC_PLUGIN_ABI_MINOR, 0};
    Route.TargetTriple = {Target.Machine.RawTriple.data(),
                          Target.Machine.RawTriple.size()};
    Route.CPU = {Target.Machine.DefaultCPU.data(),
                 Target.Machine.DefaultCPU.size()};
    return Route;
  }

  bool validFrame(const NevercPhaseFrame *Frame,
                  NevercInterfaceID Phase, ActiveMode Mode) const {
    return Frame && Active == Mode &&
           sameHandle(Frame->Task, Task.handle()) &&
           sameID(Frame->Phase, Phase);
  }

  Error attach() { return Service->attach(*this); }

  void clearActive() {
    Service->detach(Task.handle());
          if (ActiveCursor.Owner != 0 || ActiveCursor.Value != 0)
            (void)Task.handles().release(
                ActiveCursor, PluginAssemblySourceCursorHandleKind);
    Active = ActiveMode::None;
    ActiveSource = nullptr;
    ActiveBridge = nullptr;
    ActiveOutput = nullptr;
    ActiveTarget = nullptr;
    ActiveSchema = nullptr;
    ActiveInput = {};
          ActiveCursor = {};
          CursorState = {};
    Published = false;
  }

  Error executeParsePhase(const AssemblySourceArtifact &Source,
                          MCPluginBridge &Bridge,
                          const PluginTargetSnapshot::TargetRecord &Target,
                          const PluginTargetSnapshot::NamedRecord &Schema) {
    auto Input = Executor->createArtifactView(
        Task, assemblySourceArtifactID(), &Source, Source.Generation);
    if (!Input)
      return Input.takeError();
    const NevercArtifactHandle InputHandle = *Input;
    auto ReleaseInput = make_scope_exit([&] {
      (void)Task.handles().release(
          InputHandle, PluginArtifactHandleKind);
    });

    PluginArtifactSlot Output(Artifacts.find(mcUnitArtifactID()));
    Active = ActiveMode::Parse;
    ActiveSource = &Source;
    ActiveBridge = &Bridge;
    ActiveTarget = &Target;
    ActiveSchema = &Schema;
    ActiveInput = *Input;
    Published = false;
          if (Source.Preprocessed) {
            CursorState.Source = &Source;
            auto Cursor = Task.handles().create(
                PluginAssemblySourceCursorHandleKind, &CursorState,
                [](void *) {});
            if (!Cursor) {
              Active = ActiveMode::None;
              return Cursor.takeError();
            }
            ActiveCursor = *Cursor;
          }
    if (Error E = attach()) {
            clearActive();
      return E;
    }
    auto Detach = make_scope_exit([&] { clearActive(); });
    if (Error E = Executor->execute(
            Task.session(), Task, assemblyParsePhaseID(),
            route(Target), *Input, Output))
      return E;
    if (!Published || Output.payload() != &Bridge.unitValue())
      return pipelineError(
          "assembly parser published a foreign MC unit");
    return Error::success();
  }

  Expected<AssemblyOutputArtifact>
  executePrintPhase(PluginMCUnit &Unit,
                    const PluginTargetSnapshot::TargetRecord &Target,
                    const PluginTargetSnapshot::NamedRecord &Schema) {
    if (Error E = verifyPluginMCUnit(Unit, &Schema))
      return joinErrors(
          pipelineError("assembly print input verification failed"),
          std::move(E));
    MCPluginBridge Bridge(Task, Unit, &Schema,
                          /*AllowMutation=*/false);
    auto Input = Executor->createArtifactView(
        Task, mcUnitArtifactID(), &Unit, Bridge.unitGeneration());
    if (!Input)
      return Input.takeError();
    const NevercArtifactHandle InputHandle = *Input;
    auto ReleaseInput = make_scope_exit([&] {
      (void)Task.handles().release(
          InputHandle, PluginArtifactHandleKind);
    });

    PluginArtifactSlot Output(
        Artifacts.find(assemblyOutputArtifactID()));
    AssemblyOutputBuilder Builder(UINT64_C(16) * 1024 * 1024);
    Active = ActiveMode::Print;
    ActiveBridge = &Bridge;
    ActiveOutput = &Builder;
    ActiveTarget = &Target;
    ActiveSchema = &Schema;
    ActiveInput = *Input;
    Published = false;
    if (Error E = attach()) {
      Active = ActiveMode::None;
      return std::move(E);
    }
    auto Detach = make_scope_exit([&] { clearActive(); });
    if (Error E = Executor->execute(
            Task.session(), Task, assemblyPrintPhaseID(),
            route(Target), *Input, Output)) {
      Builder.rollback();
      return std::move(E);
    }
    if (!Published || !Output.payload())
      return pipelineError(
          "assembly printer did not publish an output");
    return *static_cast<const AssemblyOutputArtifact *>(Output.payload());
  }

  NevercStatus getParseInput(
      const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
      NevercAssemblyParseInputInfo *OutInfo) {
    if (!validFrame(
            Frame, assemblyParsePhaseID(), ActiveMode::Parse) ||
        !ActiveSource || !ActiveTarget || !ActiveSchema ||
        !sameHandle(Input, Frame->Input) ||
        !sameHandle(Input, ActiveInput))
      return pipelineStatus(NEVERC_STATUS_WRONG_SCOPE);
    const void *Payload = nullptr;
    NevercStatus Status = Executor->resolveArtifactPayload(
        Task, Input, assemblySourceArtifactID(), &Payload);
    if (Status.Code != NEVERC_STATUS_OK || Payload != ActiveSource)
      return pipelineStatus(NEVERC_STATUS_WRONG_SCOPE);

    NevercAssemblyParseInputInfo Value{};
    Value.Header = {sizeof(Value), NEVERC_ASSEMBLY_PROVIDER_API_MAJOR,
                    NEVERC_ASSEMBLY_PROVIDER_API_MINOR, 0};
    Value.TargetID = ActiveTarget->ID;
    Value.SchemaID = ActiveSchema->ID;
    Value.TargetSchemaDigest = {
        ActiveSchema->Digest.data(), ActiveSchema->Digest.size()};
    Value.Source.Header = {
        sizeof(Value.Source), NEVERC_ASSEMBLY_PROVIDER_API_MAJOR,
        NEVERC_ASSEMBLY_PROVIDER_API_MINOR, 0};
    Value.Source.Representation =
        ActiveSource->Preprocessed
            ? NEVERC_ASSEMBLY_SOURCE_RENDERED_TOKENS
            : NEVERC_ASSEMBLY_SOURCE_BUFFER;
    Value.Source.Preprocessed =
        ActiveSource->Preprocessed ? NEVERC_TRUE : NEVERC_FALSE;
    Value.Source.Identifier = {
        ActiveSource->Identifier.data(), ActiveSource->Identifier.size()};
    Value.Source.Buffer = {
        ActiveSource->Buffer.data(), ActiveSource->Buffer.size()};
           if (ActiveSource->Preprocessed)
             Value.Source.Cursor = ActiveCursor;
    Value.Source.Generation = ActiveSource->Generation;
    return writeRecord(OutInfo, Value);
  }

         std::pair<size_t, size_t> currentTokenRange() const {
           if (!CursorState.Source)
             return {0, 0};
           StringRef Buffer = CursorState.Source->Buffer;
           size_t Begin = static_cast<size_t>(std::min<uint64_t>(
               CursorState.Offset, Buffer.size()));
           while (Begin < Buffer.size() &&
                  std::isspace(
                      static_cast<unsigned char>(Buffer[Begin])))
             ++Begin;
           size_t End = Begin;
           while (End < Buffer.size() &&
                  !std::isspace(
                      static_cast<unsigned char>(Buffer[End])))
             ++End;
           return {Begin, End};
         }

         bool validCursorFrame(
             const NevercPhaseFrame *Frame,
             NevercAssemblySourceCursorHandle Cursor) const {
           return validFrame(
                      Frame, assemblyParsePhaseID(),
                      ActiveMode::Parse) &&
                  ActiveSource && ActiveSource->Preprocessed &&
                  sameHandle(Cursor, ActiveCursor);
         }

         NevercStatus peekSourceToken(
             const NevercPhaseFrame *Frame,
             NevercAssemblySourceCursorHandle Cursor,
             NevercAssemblyTokenInfo *OutToken) {
           if (!validCursorFrame(Frame, Cursor) || !OutToken)
             return pipelineStatus(NEVERC_STATUS_WRONG_SCOPE);
           void *Payload = nullptr;
           NevercStatus Status = Task.handles().resolve(
               Cursor, PluginAssemblySourceCursorHandleKind, &Payload);
           if (Status.Code != NEVERC_STATUS_OK ||
               Payload != &CursorState)
             return pipelineStatus(NEVERC_STATUS_STALE_HANDLE);
           const auto [Begin, End] = currentTokenRange();
           if (Begin == End)
             return pipelineStatus(NEVERC_STATUS_NOT_FOUND);
           auto Location = ActiveSource->locate(Begin);
           if (!Location) {
             consumeError(Location.takeError());
             return pipelineStatus(NEVERC_STATUS_INVALID_STATE);
           }

           NevercAssemblyTokenInfo Value{};
           Value.Header = {
               sizeof(Value), NEVERC_ASSEMBLY_PROVIDER_API_MAJOR,
               NEVERC_ASSEMBLY_PROVIDER_API_MINOR, 0};
           Value.Spelling = {
               ActiveSource->Buffer.data() + Begin, End - Begin};
           Value.FileID = Location->FileID;
           Value.ByteOffset = Location->ByteOffset;
           Value.Line = Location->Line;
           Value.Column = Location->Column;
           bool StartOfLine = Begin == 0;
           for (size_t I =
                    static_cast<size_t>(CursorState.Offset);
                I < Begin; ++I)
             StartOfLine |= ActiveSource->Buffer[I] == '\n';
           Value.StartOfLine =
               StartOfLine ? NEVERC_TRUE : NEVERC_FALSE;
           Value.LeadingSpace =
               Begin > CursorState.Offset ? NEVERC_TRUE : NEVERC_FALSE;
           return writeRecord(OutToken, Value);
         }

         NevercStatus advanceSourceToken(
             const NevercPhaseFrame *Frame,
             NevercAssemblySourceCursorHandle Cursor) {
           if (!validCursorFrame(Frame, Cursor))
             return pipelineStatus(NEVERC_STATUS_WRONG_SCOPE);
           void *Payload = nullptr;
           NevercStatus Status = Task.handles().resolve(
               Cursor, PluginAssemblySourceCursorHandleKind, &Payload);
           if (Status.Code != NEVERC_STATUS_OK ||
               Payload != &CursorState)
             return pipelineStatus(NEVERC_STATUS_STALE_HANDLE);
           const auto [Begin, End] = currentTokenRange();
           if (Begin == End)
             return pipelineStatus(NEVERC_STATUS_NOT_FOUND);
           CursorState.Offset = End;
           return neverc_status_ok();
         }

  NevercStatus getParseMCBuilder(
      const NevercPhaseFrame *Frame, const NevercMCAPI **OutMC,
      NevercMCUnitHandle *OutUnit) {
    if (OutMC)
      *OutMC = nullptr;
    if (OutUnit)
      *OutUnit = {};
    if (!validFrame(
            Frame, assemblyParsePhaseID(), ActiveMode::Parse) ||
        !ActiveBridge || !OutMC || !OutUnit)
      return pipelineStatus(NEVERC_STATUS_WRONG_SCOPE);
    auto Unit = ActiveBridge->unit();
    if (!Unit) {
      consumeError(Unit.takeError());
      return pipelineStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    *OutMC = &ActiveBridge->api();
    *OutUnit = *Unit;
    return neverc_status_ok();
  }

  NevercStatus publishParsedMCUnit(
      const NevercPhaseFrame *Frame,
      NevercArtifactHandle *OutUnit) {
    if (OutUnit)
      *OutUnit = {};
    if (!validFrame(
            Frame, assemblyParsePhaseID(), ActiveMode::Parse) ||
        !ActiveBridge || !OutUnit || Published)
      return pipelineStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    if (ActiveBridge->hasActiveMutation())
      return pipelineStatus(NEVERC_STATUS_INVALID_STATE);
    auto Candidate = Executor->createCandidate(
        Task, mcUnitArtifactID(), &ActiveBridge->unitValue());
    if (!Candidate) {
      consumeError(Candidate.takeError());
      return pipelineStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    *OutUnit = *Candidate;
    Published = true;
    return neverc_status_ok();
  }

  NevercStatus getPrintInput(
      const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
      NevercAssemblyPrintInputInfo *OutInfo,
      const NevercMCAPI **OutMC, NevercMCUnitHandle *OutUnit) {
    if (OutMC)
      *OutMC = nullptr;
    if (OutUnit)
      *OutUnit = {};
    if (!validFrame(
            Frame, assemblyPrintPhaseID(), ActiveMode::Print) ||
        !ActiveBridge || !ActiveTarget || !ActiveSchema ||
        !sameHandle(Input, Frame->Input) ||
        !sameHandle(Input, ActiveInput) || !OutInfo || !OutMC ||
        !OutUnit)
      return pipelineStatus(NEVERC_STATUS_WRONG_SCOPE);
    const void *Payload = nullptr;
    NevercStatus Status = Executor->resolveArtifactPayload(
        Task, Input, mcUnitArtifactID(), &Payload);
    if (Status.Code != NEVERC_STATUS_OK ||
        Payload != &ActiveBridge->unitValue())
      return pipelineStatus(NEVERC_STATUS_WRONG_SCOPE);

    auto Unit = ActiveBridge->unit();
    if (!Unit) {
      consumeError(Unit.takeError());
      return pipelineStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    NevercAssemblyPrintInputInfo Value{};
    Value.Header = {sizeof(Value), NEVERC_ASSEMBLY_PROVIDER_API_MAJOR,
                    NEVERC_ASSEMBLY_PROVIDER_API_MINOR, 0};
    Value.TargetID = ActiveTarget->ID;
    Value.SchemaID = ActiveSchema->ID;
    Value.TargetSchemaDigest = {
        ActiveSchema->Digest.data(), ActiveSchema->Digest.size()};
    Value.UnitGeneration = ActiveBridge->unitGeneration();
    Status = writeRecord(OutInfo, Value);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    *OutMC = &ActiveBridge->api();
    *OutUnit = *Unit;
    return neverc_status_ok();
  }

  NevercStatus writePrintOutput(const NevercPhaseFrame *Frame,
                                NevercStringView Text) {
    if (!validFrame(
            Frame, assemblyPrintPhaseID(), ActiveMode::Print) ||
        !ActiveOutput)
      return pipelineStatus(NEVERC_STATUS_WRONG_SCOPE);
    if (!validView(Text))
      return pipelineStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    if (Error E = ActiveOutput->write(view(Text))) {
      consumeError(std::move(E));
      return pipelineStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    return neverc_status_ok();
  }

  NevercStatus publishAssemblyOutput(
      const NevercPhaseFrame *Frame,
      const NevercAssemblyOutputMetadata *Metadata,
      NevercArtifactHandle *OutOutput) {
    if (OutOutput)
      *OutOutput = {};
    if (!validFrame(
            Frame, assemblyPrintPhaseID(), ActiveMode::Print) ||
        !ActiveOutput || !ActiveBridge || !ActiveTarget ||
        !ActiveSchema || !Metadata || !OutOutput || Published ||
        !validHeader(Metadata->Header, sizeof(*Metadata),
                     NEVERC_ASSEMBLY_PROVIDER_API_MAJOR,
                     NEVERC_ASSEMBLY_PROVIDER_API_MINOR) ||
        !validView(Metadata->Syntax) ||
        !validView(Metadata->Comment) || Metadata->Flags != 0)
      return pipelineStatus(NEVERC_STATUS_INVALID_ARGUMENT);

    auto Finished = ActiveOutput->finish(
        ActiveTarget->ID, ActiveSchema->Digest,
        ActiveBridge->unitGeneration());
    if (!Finished) {
      consumeError(Finished.takeError());
      return pipelineStatus(NEVERC_STATUS_INVALID_STATE);
    }
    Finished->Syntax = view(Metadata->Syntax).str();
    Finished->Comment = view(Metadata->Comment).str();
    Finished->Flags = Metadata->Flags;
    if (Error E = Finished->verify()) {
      consumeError(std::move(E));
      return pipelineStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    }
    auto *Payload =
        new (std::nothrow) AssemblyOutputArtifact(std::move(*Finished));
    if (!Payload)
      return pipelineStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    auto Candidate = Executor->createCandidate(
        Task, assemblyOutputArtifactID(), Payload);
    if (!Candidate) {
      consumeError(Candidate.takeError());
      return pipelineStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    *OutOutput = *Candidate;
    Published = true;
    return neverc_status_ok();
  }

  PluginTaskContext &Task;
  std::shared_ptr<const PluginTargetSnapshot> Snapshot;
  std::shared_ptr<ProviderService> Service;
  PluginPhaseGraph Graph;
  PluginArtifactRegistry Artifacts;
  std::unique_ptr<PluginPhaseExecutor> Executor;
  bool ReplaceParser = false;
  bool ReplacePrinter = false;

  ActiveMode Active = ActiveMode::None;
  const AssemblySourceArtifact *ActiveSource = nullptr;
  MCPluginBridge *ActiveBridge = nullptr;
  AssemblyOutputBuilder *ActiveOutput = nullptr;
  const PluginTargetSnapshot::TargetRecord *ActiveTarget = nullptr;
  const PluginTargetSnapshot::NamedRecord *ActiveSchema = nullptr;
  NevercArtifactHandle ActiveInput{};
         struct SourceCursorState {
           const AssemblySourceArtifact *Source = nullptr;
           uint64_t Offset = 0;
         } CursorState;
         NevercAssemblySourceCursorHandle ActiveCursor{};
  bool Published = false;
};

PluginAssemblyPipelineRuntime::PluginAssemblyPipelineRuntime(
    std::unique_ptr<Impl> StateValue)
    : State(std::move(StateValue)) {}

PluginAssemblyPipelineRuntime::~PluginAssemblyPipelineRuntime() = default;

Expected<std::unique_ptr<PluginAssemblyPipelineRuntime>>
PluginAssemblyPipelineRuntime::create(
    PluginTaskContext &Task,
    std::shared_ptr<const PluginTargetSnapshot> Snapshot) {
  auto Service = std::static_pointer_cast<Impl::ProviderService>(
      Task.processServices().findHostService(
          assemblyProviderInterfaceID()));
  if (!Service)
    return pipelineError(
        "assembly provider interface is not registered");
  auto State = std::make_unique<Impl>(
      Task, std::move(Snapshot), std::move(Service));
  if (Error E = State->initialize())
    return std::move(E);
  return std::unique_ptr<PluginAssemblyPipelineRuntime>(
      new PluginAssemblyPipelineRuntime(std::move(State)));
}

bool PluginAssemblyPipelineRuntime::replacesParser() const {
  return State->ReplaceParser;
}

bool PluginAssemblyPipelineRuntime::replacesPrinter() const {
  return State->ReplacePrinter;
}

Expected<std::unique_ptr<PluginMCUnit>>
PluginAssemblyPipelineRuntime::parse(
    const AssemblySourceArtifact &Source, NevercTargetID TargetID,
    MCAsmParserProviderRuntime::BuiltinProvider Builtin) {
  const auto *Target = State->Snapshot->findTarget(TargetID);
  if (!Target)
    return pipelineError("assembly parser target is not registered");
  const auto *Schema =
      State->Snapshot->findMCSchema(Target->MCSchemaID);
  if (!Schema)
    return pipelineError("assembly parser target has no MC schema");

  AssemblyParseExecutionRequest Request;
  Request.Task = &State->Task;
  Request.Snapshot = State->Snapshot.get();
  Request.Source = &Source;
  Request.TargetID = TargetID;
  MCAsmParserProviderRuntime::ReplacementProvider Replacement;
  if (State->ReplaceParser)
    Replacement =
        [&](const AssemblySourceArtifact &Input,
            MCPluginBridge &Bridge) {
          return State->executeParsePhase(
              Input, Bridge, *Target, *Schema);
        };
  return MCAsmParserProviderRuntime::execute(
      Request, std::move(Replacement), std::move(Builtin));
}

Expected<AssemblyOutputArtifact>
PluginAssemblyPipelineRuntime::print(
    PluginMCUnit &Unit,
    MCAsmPrinterProviderRuntime::BuiltinProvider Builtin) {
  if (!State->ReplacePrinter) {
    AssemblyPrintExecutionRequest Request;
    Request.Task = &State->Task;
    Request.Snapshot = State->Snapshot.get();
    Request.Unit = &Unit;
    return MCAsmPrinterProviderRuntime::execute(
        Request, {}, std::move(Builtin));
  }

  const auto *Target =
      State->Snapshot->findTarget(Unit.targetID());
  if (!Target)
    return pipelineError("assembly printer target is not registered");
  const auto *Schema =
      State->Snapshot->findMCSchema(Target->MCSchemaID);
  if (!Schema || Unit.targetSchemaDigest() != Schema->Digest)
    return pipelineError("assembly printer input has a foreign MC schema");
  return State->executePrintPhase(Unit, *Target, *Schema);
}

Error registerPluginAssemblyProviderInterface(
    PluginProcessServices &Services) {
  if (Services.interfaces().isFrozen())
    return pipelineError(
        "cannot register assembly provider interface after interface freeze");
  auto Service =
      std::make_shared<PluginAssemblyPipelineRuntime::Impl::ProviderService>();
  if (Error E = Services.registerHostService(
          assemblyProviderInterfaceID(), Service))
    return E;
  return Services.interfaces().registerInterface(
      assemblyProviderInterfaceID(),
      NEVERC_ASSEMBLY_PROVIDER_INTERFACE_STABILITY,
      &Service->api(), {});
}

} // namespace neverc::plugin
