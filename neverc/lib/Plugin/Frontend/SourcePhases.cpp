#include "ASTUnitArtifact.h"
#include "FrontendPluginInterfaces.h"
#include "PluginTreeConsumer.h"
#include "PluginTokenLexer.h"
#include "PrepBridgeInternal.h"
#include "SemaProvider.h"
#include "SemanticUnitArtifact.h"
#include "TokenStream.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "neverc/Plugin/Host/FrontendPluginBridge.h"
#include "neverc/Plugin/Host/PluginArtifactRegistry.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginIOBridge.h"
#include "neverc/Plugin/Host/PluginPhaseExecutor.h"
#include "neverc/Plugin/Host/PluginPhaseGraph.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "neverc/Analyze/Sema.h"
#include "neverc/Analyze/SemaPluginHooks.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Scan/PrepPluginHooks.h"
#include "neverc/Syntax/ParserPluginHooks.h"
#include "neverc/Syntax/RunParser.h"
#include "neverc/Tree/Core/TreeConsumer.h"
#include "neverc/Tree/Decl/Decl.h"
#include "neverc/Tree/Decl/DeclGroup.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/SHA256.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>
#include <new>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;

namespace neverc::plugin {
namespace {

constexpr uint64_t MaximumSourceTextBytes = UINT64_C(1) << 32;

NevercInterfaceID sourceInputArtifactID() {
  return {NEVERC_ARTIFACT_SOURCE_INPUT_HIGH, NEVERC_ARTIFACT_SOURCE_INPUT_LOW};
}

NevercInterfaceID sourceUnitArtifactID() {
  return {NEVERC_ARTIFACT_SOURCE_UNIT_HIGH, NEVERC_ARTIFACT_SOURCE_UNIT_LOW};
}

NevercInterfaceID sourceOutputSinkArtifactID() {
  return {NEVERC_ARTIFACT_SOURCE_OUTPUT_SINK_HIGH,
          NEVERC_ARTIFACT_SOURCE_OUTPUT_SINK_LOW};
}

NevercInterfaceID sourceResolveInputPhaseID() {
  return {NEVERC_PHASE_SOURCE_RESOLVE_INPUT_HIGH,
          NEVERC_PHASE_SOURCE_RESOLVE_INPUT_LOW};
}

NevercInterfaceID sourceOpenPhaseID() {
  return {NEVERC_PHASE_SOURCE_OPEN_HIGH, NEVERC_PHASE_SOURCE_OPEN_LOW};
}

NevercInterfaceID sourceAfterOpenPhaseID() {
  return {NEVERC_PHASE_SOURCE_AFTER_OPEN_HIGH,
          NEVERC_PHASE_SOURCE_AFTER_OPEN_LOW};
}

NevercStatus sourceStatus(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

bool sameHandle(NevercHandle Left, NevercHandle Right) {
  return Left.Owner == Right.Owner && Left.Value == Right.Value;
}

bool validText(StringRef Text, bool AllowEmpty = false) {
  return (AllowEmpty || !Text.empty()) && !Text.contains('\0') &&
         json::isUTF8(Text);
}

bool copyText(NevercStringView View, bool AllowEmpty, std::string &Out) {
  if (View.Length > MaximumSourceTextBytes ||
      View.Length > std::numeric_limits<size_t>::max() ||
      (!View.Data && View.Length != 0))
    return false;
  StringRef Text(View.Data ? View.Data : "", static_cast<size_t>(View.Length));
  if (!validText(Text, AllowEmpty))
    return false;
  Out = Text.str();
  return true;
}

template <typename T>
NevercStatus writeCallerRecord(T *OutValue, const T &Value) {
  if (!OutValue)
    return sourceStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  const uint32_t Capacity = OutValue->Header.StructSize;
  if (Capacity < sizeof(NevercABITableHeader))
    return sourceStatus(NEVERC_STATUS_ABI_MISMATCH);
  const size_t Writable = std::min<size_t>(Capacity, sizeof(Value));
  std::memset(OutValue, 0, Writable);
  std::memcpy(OutValue, &Value, Writable);
  return Capacity < sizeof(Value) ? sourceStatus(NEVERC_STATUS_ABI_MISMATCH)
                                  : neverc_status_ok();
}

struct SourceInputArtifact {
  std::string Path;
  std::vector<uint8_t> Buffer;
  uint32_t Language = 0;
  bool HasBuffer = false;
  bool System = false;
  bool Preprocessed = false;
};

struct SourceUnitArtifact {
  std::string LogicalPath;
  std::string CanonicalIdentity;
  std::string ProviderID;
  std::vector<uint8_t> Content;
  FileID MainFile;
  bool MemoryBacked = false;
  bool System = false;
  bool Deterministic = false;
  bool Cacheable = false;
  bool Installed = false;
};

struct SourceOutputSinkArtifact {
  NevercOutputSinkHandle Sink{};
};

Expected<void *> cloneSourceInput(const void *Payload) {
  if (!Payload)
    return createStringError(inconvertibleErrorCode(),
                             "source input payload is null");
  return static_cast<void *>(new SourceInputArtifact(
      *static_cast<const SourceInputArtifact *>(Payload)));
}

Expected<void *> cloneSourceUnit(const void *Payload) {
  if (!Payload)
    return createStringError(inconvertibleErrorCode(),
                             "source unit payload is null");
  return static_cast<void *>(new SourceUnitArtifact(
      *static_cast<const SourceUnitArtifact *>(Payload)));
}

Expected<void *> cloneSourceOutputSink(const void *Payload) {
  if (!Payload)
    return createStringError(inconvertibleErrorCode(),
                             "source output sink payload is null");
  return static_cast<void *>(new SourceOutputSinkArtifact(
      *static_cast<const SourceOutputSinkArtifact *>(Payload)));
}

Error verifySourceInput(const void *Payload) {
  if (!Payload)
    return createStringError(inconvertibleErrorCode(),
                             "source input payload is null");
  const auto &Input = *static_cast<const SourceInputArtifact *>(Payload);
  if (!validText(Input.Path))
    return createStringError(inconvertibleErrorCode(),
                             "source input path is invalid");
  if (Input.HasBuffer && Input.Buffer.size() > MaximumSourceTextBytes)
    return createStringError(inconvertibleErrorCode(),
                             "source input buffer is too large");
  return Error::success();
}

Error verifySourceUnit(const void *Payload) {
  if (!Payload)
    return createStringError(inconvertibleErrorCode(),
                             "source unit payload is null");
  const auto &Unit = *static_cast<const SourceUnitArtifact *>(Payload);
  if (!validText(Unit.LogicalPath) || !validText(Unit.CanonicalIdentity) ||
      !validText(Unit.ProviderID) || Unit.Content.empty() ||
      Unit.Content.back() != 0)
    return createStringError(inconvertibleErrorCode(),
                             "source unit identity or buffer is invalid");
  if (Unit.Cacheable && !Unit.Deterministic)
    return createStringError(inconvertibleErrorCode(),
                             "cacheable source unit must be deterministic");
  if (Unit.Installed && !Unit.MainFile.isValid())
    return createStringError(inconvertibleErrorCode(),
                             "installed source unit has no main file");
  return Error::success();
}

Error verifySourceOutputSink(const void *Payload) {
  if (!Payload)
    return createStringError(inconvertibleErrorCode(),
                             "source output sink payload is null");
  const auto &Output = *static_cast<const SourceOutputSinkArtifact *>(Payload);
  if (neverc_handle_is_null(Output.Sink))
    return createStringError(inconvertibleErrorCode(),
                             "source output sink handle is null");
  return Error::success();
}

NevercStringView stringView(StringRef Text) {
  return {Text.data(), static_cast<uint64_t>(Text.size())};
}

NevercPhaseRoute defaultRoute() {
  NevercPhaseRoute Route{};
  Route.Header = {sizeof(Route), NEVERC_PLUGIN_ABI_MAJOR,
                  NEVERC_PLUGIN_ABI_MINOR, 0};
  return Route;
}

class FrontendPluginProcessBridge;

std::shared_ptr<FrontendPluginProcessBridge>
findFrontendProcessBridge(PluginProcessServices &Services);

} // namespace

struct PluginSourcePhaseRuntime::Impl {
  PluginTaskContext &Task;
  SourceManager &SourceMgr;
  const LangOptions &LangOpts;
  std::shared_ptr<FrontendPluginProcessBridge> ProcessBridge;
  std::unique_ptr<FrontendPluginBridge> Locations;
  std::unique_ptr<PluginPrepBridge> PrepBridge;
  std::unique_ptr<PluginASTBridge> ASTBridge;
  std::unique_ptr<PluginSemaBridge> SemaBridge;
  std::unique_ptr<PluginSemaProviderRuntime> SemaProvider;
  std::unique_ptr<SemaPluginHooks> SemaHooks;
  std::unique_ptr<PrepPluginHooks> PrepHooks;
  std::unique_ptr<ParserPluginHooks> ParserHooks;
  PrepEngine *AttachedPrep = nullptr;
  PluginPhaseGraph Graph;
  PluginArtifactRegistry Artifacts;
  std::unique_ptr<PluginPhaseExecutor> Executor;
  std::unique_ptr<PluginArtifactSlot> ResolvedInput;
  std::unique_ptr<PluginArtifactSlot> SourceUnit;
  std::unique_ptr<PluginArtifactSlot> TokenStream;
  std::unique_ptr<PluginArtifactSlot> ASTUnit;
  std::unique_ptr<PluginArtifactSlot> SemanticUnit;
  Sema *AttachedSema = nullptr;
  Sema *ActiveSema = nullptr;
  bool ActivePrintStats = false;
  bool ActiveParserInputInitialized = false;
  BuiltinOpen OpenBuiltin;
  std::string BuiltinFailure;

  Impl(PluginTaskContext &TaskValue, SourceManager &SourceMgrValue,
       const LangOptions &LangOptsValue, PluginPhaseGraph GraphValue)
      : Task(TaskValue), SourceMgr(SourceMgrValue), LangOpts(LangOptsValue),
        Graph(std::move(GraphValue)) {}

  NevercStatus getSourceInput(const NevercPhaseFrame *Frame,
                              NevercArtifactHandle Input,
                              NevercSourceInputInfo *OutInfo);
  NevercStatus
  createMemorySourceUnit(const NevercPhaseFrame *Frame,
                         NevercArtifactHandle Input,
                         const NevercMemorySourceUnitDescriptor *Descriptor,
                         NevercArtifactHandle *OutUnit);
  NevercStatus getSourceUnit(const NevercPhaseFrame *Frame,
                             NevercArtifactHandle Unit,
                             NevercSourceUnitInfo *OutInfo);
  NevercStatus resolveInputBuiltin(const NevercPhaseFrame *Frame,
                                   NevercPhaseResult *Result);
  NevercStatus openBuiltin(const NevercPhaseFrame *Frame,
                           NevercPhaseResult *Result);
  NevercStatus tokenBuiltin(const NevercPhaseFrame *Frame,
                            NevercPhaseResult *Result);
  NevercStatus tokenStreamBuiltin(const NevercPhaseFrame *Frame,
                                  NevercPhaseResult *Result);
  NevercStatus prepHookBuiltin(const NevercPhaseFrame *Frame,
                               NevercPhaseResult *Result);
  NevercStatus parserBuiltin(const NevercPhaseFrame *Frame,
                             NevercPhaseResult *Result);
  NevercStatus semaBuiltin(const NevercPhaseFrame *Frame,
                           NevercPhaseResult *Result);
  Error installAndVerify(SourceUnitArtifact &Unit);
  Error recordAndVerifyDependency(const SourceUnitArtifact &Unit);
};

namespace {

class FrontendPluginProcessBridge final
    : public PluginHostService,
      public std::enable_shared_from_this<FrontendPluginProcessBridge> {
public:
  explicit FrontendPluginProcessBridge(PluginProcessServices &ServicesValue)
      : Services(ServicesValue) {
    API.Header = {sizeof(API), NEVERC_SOURCE_API_MAJOR, NEVERC_SOURCE_API_MINOR,
                  0};
    API.Context = this;
    API.GetLocationInfo = GetLocationInfo;
    API.GetSpellingLocation = GetSpellingLocation;
    API.GetExpansionLocation = GetExpansionLocation;
    API.GetFileLocation = GetFileLocation;
    API.GetRangeInfo = GetRangeInfo;
    API.GetSourceText = GetSourceText;
    API.GetPresumedLocation = GetPresumedLocation;
    API.GetLocationFile = GetLocationFile;
    API.GetIncludeLocation = GetIncludeLocation;
    API.GetFileInfo = GetFileInfo;
    API.GetCharacterData = GetCharacterData;
    API.GetTokenEnd = GetTokenEnd;
    API.GetLocationInfoBatch = GetLocationInfoBatch;
    API.GetSourceInput = getSourceInput;
    API.CreateMemorySourceUnit = createMemorySourceUnit;
    API.GetSourceUnit = getSourceUnit;
  }

  const NevercSourceLocationAPI &api() const { return API; }

  void attach(PluginTaskContext &Task, FrontendPluginBridge &Locations,
              PluginSourcePhaseRuntime::Impl &Runtime) {
    const auto Key = std::make_pair(Task.handle().Owner, Task.handle().Value);
    std::lock_guard<std::mutex> Lock(Mutex);
    Tasks[Key] = {&Locations, &Runtime};
  }

  void detach(NevercTaskHandle Task) {
    std::lock_guard<std::mutex> Lock(Mutex);
    Tasks.erase(std::make_pair(Task.Owner, Task.Value));
  }

  void taskScopeUnregistered(NevercTaskHandle Task) noexcept override {
    detach(Task);
  }

private:
  struct TaskBridge {
    FrontendPluginBridge *Locations = nullptr;
    PluginSourcePhaseRuntime::Impl *Runtime = nullptr;
  };

  TaskBridge find(NevercTaskHandle Task) {
    std::lock_guard<std::mutex> Lock(Mutex);
    auto It = Tasks.find(std::make_pair(Task.Owner, Task.Value));
    return It == Tasks.end() ? TaskBridge{} : It->second;
  }

  static FrontendPluginProcessBridge *bridge(void *Context) {
    return static_cast<FrontendPluginProcessBridge *>(Context);
  }

#define NEVERC_FORWARD_LOCATION(Name, Signature, ...)                          \
  static NevercStatus NEVERC_CALL Name Signature {                             \
    if (!Context)                                                              \
      return sourceStatus(NEVERC_STATUS_INVALID_ARGUMENT);                     \
    TaskBridge Bound = bridge(Context)->find(Task);                            \
    if (!Bound.Locations)                                                      \
      return sourceStatus(NEVERC_STATUS_STALE_HANDLE);                         \
    const NevercSourceLocationAPI &Local =                                     \
        Bound.Locations->sourceLocationAPI();                                  \
    return Local.Name(Local.Context, __VA_ARGS__);                             \
  }

  NEVERC_FORWARD_LOCATION(GetLocationInfo,
                          (void *Context, NevercTaskHandle Task,
                           NevercSourceLocation Location,
                           NevercSourceLocationInfo *OutInfo),
                          Task, Location, OutInfo)
  NEVERC_FORWARD_LOCATION(GetSpellingLocation,
                          (void *Context, NevercTaskHandle Task,
                           NevercSourceLocation Location,
                           NevercSourceLocation *OutLocation),
                          Task, Location, OutLocation)
  NEVERC_FORWARD_LOCATION(GetExpansionLocation,
                          (void *Context, NevercTaskHandle Task,
                           NevercSourceLocation Location,
                           NevercSourceLocation *OutLocation),
                          Task, Location, OutLocation)
  NEVERC_FORWARD_LOCATION(GetFileLocation,
                          (void *Context, NevercTaskHandle Task,
                           NevercSourceLocation Location,
                           NevercSourceLocation *OutLocation),
                          Task, Location, OutLocation)
  NEVERC_FORWARD_LOCATION(GetRangeInfo,
                          (void *Context, NevercTaskHandle Task,
                           NevercSourceRange Range,
                           NevercSourceRangeInfo *OutInfo),
                          Task, Range, OutInfo)
  NEVERC_FORWARD_LOCATION(GetSourceText,
                          (void *Context, NevercTaskHandle Task,
                           NevercSourceRange Range, NevercBufferView *OutText),
                          Task, Range, OutText)
  NEVERC_FORWARD_LOCATION(GetPresumedLocation,
                          (void *Context, NevercTaskHandle Task,
                           NevercSourceLocation Location,
                           NevercPresumedLocation *OutLocation),
                          Task, Location, OutLocation)
  NEVERC_FORWARD_LOCATION(GetLocationFile,
                          (void *Context, NevercTaskHandle Task,
                           NevercSourceLocation Location,
                           NevercFileHandle *OutFile),
                          Task, Location, OutFile)
  NEVERC_FORWARD_LOCATION(GetIncludeLocation,
                          (void *Context, NevercTaskHandle Task,
                           NevercSourceLocation Location,
                           NevercSourceLocation *OutLocation),
                          Task, Location, OutLocation)
  NEVERC_FORWARD_LOCATION(GetFileInfo,
                          (void *Context, NevercTaskHandle Task,
                           NevercFileHandle File, NevercFileInfo *OutInfo),
                          Task, File, OutInfo)
  NEVERC_FORWARD_LOCATION(GetCharacterData,
                          (void *Context, NevercTaskHandle Task,
                           NevercSourceLocation Location,
                           NevercBufferView *OutData),
                          Task, Location, OutData)
  NEVERC_FORWARD_LOCATION(GetTokenEnd,
                          (void *Context, NevercTaskHandle Task,
                           NevercSourceLocation Location,
                           NevercSourceLocation *OutLocation),
                          Task, Location, OutLocation)
  NEVERC_FORWARD_LOCATION(
      GetLocationInfoBatch,
      (void *Context, NevercTaskHandle Task,
       const NevercSourceLocation *Locations, uint64_t LocationCount,
       NevercSourceLocationInfo *OutInfos, uint64_t OutInfoCapacity),
      Task, Locations, LocationCount, OutInfos, OutInfoCapacity)

#undef NEVERC_FORWARD_LOCATION

  static NevercStatus
      NEVERC_CALL getSourceInput(void *Context, const NevercPhaseFrame *Frame,
                                 NevercArtifactHandle Input,
                                 NevercSourceInputInfo *OutInfo) {
    if (!Context || !Frame)
      return sourceStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    TaskBridge Bound = bridge(Context)->find(Frame->Task);
    return Bound.Runtime ? Bound.Runtime->getSourceInput(Frame, Input, OutInfo)
                         : sourceStatus(NEVERC_STATUS_STALE_HANDLE);
  }

  static NevercStatus NEVERC_CALL createMemorySourceUnit(
      void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
      const NevercMemorySourceUnitDescriptor *Descriptor,
      NevercArtifactHandle *OutUnit) {
    if (!Context || !Frame)
      return sourceStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    TaskBridge Bound = bridge(Context)->find(Frame->Task);
    return Bound.Runtime ? Bound.Runtime->createMemorySourceUnit(
                               Frame, Input, Descriptor, OutUnit)
                         : sourceStatus(NEVERC_STATUS_STALE_HANDLE);
  }

  static NevercStatus NEVERC_CALL getSourceUnit(void *Context,
                                                const NevercPhaseFrame *Frame,
                                                NevercArtifactHandle Unit,
                                                NevercSourceUnitInfo *OutInfo) {
    if (!Context || !Frame)
      return sourceStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    TaskBridge Bound = bridge(Context)->find(Frame->Task);
    return Bound.Runtime ? Bound.Runtime->getSourceUnit(Frame, Unit, OutInfo)
                         : sourceStatus(NEVERC_STATUS_STALE_HANDLE);
  }

  PluginProcessServices &Services;
  NevercSourceLocationAPI API{};
  std::mutex Mutex;
  std::map<std::pair<uint64_t, uint64_t>, TaskBridge> Tasks;
};

std::shared_ptr<FrontendPluginProcessBridge>
findFrontendProcessBridge(PluginProcessServices &Services) {
  return std::static_pointer_cast<FrontendPluginProcessBridge>(
      Services.findHostService(sourceLocationPluginInterfaceID()));
}

} // namespace

NevercStatus
PluginSourcePhaseRuntime::Impl::getSourceInput(const NevercPhaseFrame *Frame,
                                               NevercArtifactHandle Input,
                                               NevercSourceInputInfo *OutInfo) {
  if (!Executor || !Frame || !OutInfo ||
      !sameHandle(Frame->Task, Task.handle()))
    return sourceStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  const void *Payload = nullptr;
  NevercStatus Status = Executor->resolveArtifactPayload(
      Task, Input, sourceInputArtifactID(), &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  const auto &Value = *static_cast<const SourceInputArtifact *>(Payload);
  NevercSourceInputInfo Info{};
  Info.Header = {sizeof(Info), NEVERC_SOURCE_API_MAJOR, NEVERC_SOURCE_API_MINOR,
                 0};
  Info.Path = stringView(Value.Path);
  Info.Kind =
      Value.HasBuffer ? NEVERC_SOURCE_INPUT_BUFFER : NEVERC_SOURCE_INPUT_FILE;
  Info.Language = Value.Language;
  Info.System = Value.System ? NEVERC_TRUE : NEVERC_FALSE;
  Info.Preprocessed = Value.Preprocessed ? NEVERC_TRUE : NEVERC_FALSE;
  return writeCallerRecord(OutInfo, Info);
}

NevercStatus PluginSourcePhaseRuntime::Impl::createMemorySourceUnit(
    const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
    const NevercMemorySourceUnitDescriptor *Descriptor,
    NevercArtifactHandle *OutUnit) {
  if (!Executor || !Frame || !Descriptor || !OutUnit)
    return sourceStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutUnit = {};
  if (!sameHandle(Frame->Task, Task.handle()) ||
      !samePluginInterfaceID(Frame->Phase, sourceOpenPhaseID()) ||
      Descriptor->Header.StructSize < sizeof(*Descriptor) ||
      Descriptor->Header.Major != NEVERC_SOURCE_API_MAJOR ||
      Descriptor->Header.Minor > NEVERC_SOURCE_API_MINOR ||
      Descriptor->Header.Flags != 0 || Descriptor->Reserved != 0 ||
      Descriptor->Content.Length > MaximumSourceTextBytes ||
      Descriptor->Content.Length > std::numeric_limits<size_t>::max() ||
      (!Descriptor->Content.Data && Descriptor->Content.Length != 0) ||
      (Descriptor->System != NEVERC_FALSE &&
       Descriptor->System != NEVERC_TRUE) ||
      (Descriptor->Deterministic != NEVERC_FALSE &&
       Descriptor->Deterministic != NEVERC_TRUE) ||
      (Descriptor->Cacheable != NEVERC_FALSE &&
       Descriptor->Cacheable != NEVERC_TRUE))
    return sourceStatus(NEVERC_STATUS_INVALID_ARGUMENT);

  const void *InputPayload = nullptr;
  NevercStatus InputStatus = Executor->resolveArtifactPayload(
      Task, Input, sourceInputArtifactID(), &InputPayload);
  if (InputStatus.Code != NEVERC_STATUS_OK)
    return InputStatus;
  const auto &SourceInput =
      *static_cast<const SourceInputArtifact *>(InputPayload);

  auto Unit = std::make_unique<SourceUnitArtifact>();
  if (!copyText(Descriptor->LogicalPath, false, Unit->LogicalPath) ||
      !copyText(Descriptor->CanonicalIdentity, false,
                Unit->CanonicalIdentity) ||
      !copyText(Descriptor->ProviderID, false, Unit->ProviderID) ||
      Unit->LogicalPath != SourceInput.Path ||
      (Descriptor->System == NEVERC_TRUE) != SourceInput.System)
    return sourceStatus(NEVERC_STATUS_VERIFICATION_FAILED);
  Unit->Content.assign(Descriptor->Content.Data,
                       Descriptor->Content.Data +
                           static_cast<size_t>(Descriptor->Content.Length));
  Unit->Content.push_back(0);
  Unit->MemoryBacked = true;
  Unit->System = Descriptor->System == NEVERC_TRUE;
  Unit->Deterministic = Descriptor->Deterministic == NEVERC_TRUE;
  Unit->Cacheable = Descriptor->Cacheable == NEVERC_TRUE;
  if (Error E = verifySourceUnit(Unit.get())) {
    consumeError(std::move(E));
    return sourceStatus(NEVERC_STATUS_VERIFICATION_FAILED);
  }
  auto Candidate =
      Executor->createCandidate(Task, sourceUnitArtifactID(), Unit.release());
  if (!Candidate) {
    consumeError(Candidate.takeError());
    return sourceStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutUnit = *Candidate;
  return neverc_status_ok();
}

NevercStatus
PluginSourcePhaseRuntime::Impl::getSourceUnit(const NevercPhaseFrame *Frame,
                                              NevercArtifactHandle Unit,
                                              NevercSourceUnitInfo *OutInfo) {
  if (!Executor || !Frame || !OutInfo ||
      !sameHandle(Frame->Task, Task.handle()))
    return sourceStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  const void *Payload = nullptr;
  NevercStatus Status = Executor->resolveArtifactPayload(
      Task, Unit, sourceUnitArtifactID(), &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  const auto &Value = *static_cast<const SourceUnitArtifact *>(Payload);
  NevercSourceUnitInfo Info{};
  Info.Header = {sizeof(Info), NEVERC_SOURCE_API_MAJOR, NEVERC_SOURCE_API_MINOR,
                 0};
  Info.LogicalPath = stringView(Value.LogicalPath);
  Info.CanonicalIdentity = stringView(Value.CanonicalIdentity);
  Info.ProviderID = stringView(Value.ProviderID);
  Info.Size = Value.Content.empty() ? 0 : Value.Content.size() - 1;
  Info.MemoryBacked = Value.MemoryBacked ? NEVERC_TRUE : NEVERC_FALSE;
  Info.System = Value.System ? NEVERC_TRUE : NEVERC_FALSE;
  Info.Deterministic = Value.Deterministic ? NEVERC_TRUE : NEVERC_FALSE;
  Info.Cacheable = Value.Cacheable ? NEVERC_TRUE : NEVERC_FALSE;
  return writeCallerRecord(OutInfo, Info);
}

NevercStatus PluginSourcePhaseRuntime::Impl::resolveInputBuiltin(
    const NevercPhaseFrame *Frame, NevercPhaseResult *Result) {
  if (!Frame || !Result)
    return sourceStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  const void *Payload = nullptr;
  NevercStatus Status = Executor->resolveArtifactPayload(
      Task, Frame->Input, sourceInputArtifactID(), &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *Candidate = new (std::nothrow)
      SourceInputArtifact(*static_cast<const SourceInputArtifact *>(Payload));
  if (!Candidate)
    return sourceStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  auto Handle =
      Executor->createCandidate(Task, sourceInputArtifactID(), Candidate);
  if (!Handle) {
    delete Candidate;
    consumeError(Handle.takeError());
    return sourceStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *Result = {};
  Result->Header = {sizeof(*Result), NEVERC_PLUGIN_ABI_MAJOR,
                    NEVERC_PLUGIN_ABI_MINOR, 0};
  Result->Action = NEVERC_PHASE_REPLACE;
  Result->Output = *Handle;
  return neverc_status_ok();
}

NevercStatus
PluginSourcePhaseRuntime::Impl::openBuiltin(const NevercPhaseFrame *Frame,
                                            NevercPhaseResult *Result) {
  if (!Frame || !Result || !OpenBuiltin)
    return sourceStatus(NEVERC_STATUS_INVALID_STATE);
  if (Error E = OpenBuiltin()) {
    BuiltinFailure = toString(std::move(E)).str().str();
    return sourceStatus(NEVERC_STATUS_PLUGIN_FAILURE);
  }
  FileID MainFile = SourceMgr.getMainFileID();
  auto Buffer = SourceMgr.getBufferOrNone(MainFile);
  if (!MainFile.isValid() || !Buffer)
    return sourceStatus(NEVERC_STATUS_VERIFICATION_FAILED);

  const void *InputPayload = nullptr;
  NevercStatus InputStatus = Executor->resolveArtifactPayload(
      Task, Frame->Input, sourceInputArtifactID(), &InputPayload);
  if (InputStatus.Code != NEVERC_STATUS_OK)
    return InputStatus;
  const auto &Input = *static_cast<const SourceInputArtifact *>(InputPayload);

  auto Unit = std::make_unique<SourceUnitArtifact>();
  Unit->LogicalPath = Input.Path;
  if (OptionalFileEntryRef File = SourceMgr.getFileEntryRefForID(MainFile)) {
    Unit->CanonicalIdentity =
        SourceMgr.getFileManager().getCanonicalName(*File).str();
  } else {
    Unit->CanonicalIdentity = Input.Path;
  }
  if (Unit->CanonicalIdentity.empty())
    Unit->CanonicalIdentity = "<stdin>";
  Unit->ProviderID = "neverc.host.vfs";
  StringRef Contents = Buffer->getBuffer();
  Unit->Content.assign(Contents.bytes_begin(), Contents.bytes_end());
  Unit->Content.push_back(0);
  Unit->MainFile = MainFile;
  Unit->MemoryBacked = !SourceMgr.getFileEntryRefForID(MainFile);
  Unit->System = Input.System;
  Unit->Deterministic = true;
  Unit->Cacheable = true;
  Unit->Installed = true;
  auto Candidate =
      Executor->createCandidate(Task, sourceUnitArtifactID(), Unit.release());
  if (!Candidate) {
    consumeError(Candidate.takeError());
    return sourceStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *Result = {};
  Result->Header = {sizeof(*Result), NEVERC_PLUGIN_ABI_MAJOR,
                    NEVERC_PLUGIN_ABI_MINOR, 0};
  Result->Action = NEVERC_PHASE_REPLACE;
  Result->Output = *Candidate;
  return neverc_status_ok();
}

NevercStatus
PluginSourcePhaseRuntime::Impl::tokenBuiltin(const NevercPhaseFrame *Frame,
                                             NevercPhaseResult *Result) {
  if (!Frame || !Result)
    return sourceStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  const void *Payload = nullptr;
  NevercStatus Resolve = Executor->resolveArtifactPayload(
      Task, Frame->Input, prepTokenArtifactID(), &Payload);
  if (Resolve.Code != NEVERC_STATUS_OK)
    return Resolve;
  auto *Candidate = new (std::nothrow) prep_bridge_detail::TokenPhaseArtifact(
      *static_cast<const prep_bridge_detail::TokenPhaseArtifact *>(Payload));
  if (!Candidate)
    return sourceStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  auto Handle =
      Executor->createCandidate(Task, prepTokenArtifactID(), Candidate);
  if (!Handle) {
    delete Candidate;
    consumeError(Handle.takeError());
    return sourceStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *Result = {};
  Result->Header = {sizeof(*Result), NEVERC_PLUGIN_ABI_MAJOR,
                    NEVERC_PLUGIN_ABI_MINOR, 0};
  Result->Action = NEVERC_PHASE_REPLACE;
  Result->Output = *Handle;
  return neverc_status_ok();
}

NevercStatus PluginSourcePhaseRuntime::Impl::tokenStreamBuiltin(
    const NevercPhaseFrame *Frame, NevercPhaseResult *Result) {
  if (!Frame || !Result || !AttachedPrep)
    return sourceStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  const void *Payload = nullptr;
  NevercStatus Resolve = Executor->resolveArtifactPayload(
      Task, Frame->Input, sourceUnitArtifactID(), &Payload);
  if (Resolve.Code != NEVERC_STATUS_OK)
    return Resolve;
  if (!Payload)
    return sourceStatus(NEVERC_STATUS_VERIFICATION_FAILED);

  auto *Candidate =
      new (std::nothrow) prep_bridge_detail::PrepTokenStreamArtifact();
  if (!Candidate)
    return sourceStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  Candidate->Engine = AttachedPrep;
  Candidate->Dependencies = getPluginDependencies(Task);
  Candidate->BuiltinLazy = true;
  auto Handle =
      Executor->createCandidate(Task, prepTokenStreamArtifactID(), Candidate);
  if (!Handle) {
    delete Candidate;
    consumeError(Handle.takeError());
    return sourceStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *Result = {};
  Result->Header = {sizeof(*Result), NEVERC_PLUGIN_ABI_MAJOR,
                    NEVERC_PLUGIN_ABI_MINOR, 0};
  Result->Action = NEVERC_PHASE_REPLACE;
  Result->Output = *Handle;
  return neverc_status_ok();
}

NevercStatus
PluginSourcePhaseRuntime::Impl::prepHookBuiltin(const NevercPhaseFrame *Frame,
                                                NevercPhaseResult *Result) {
  if (!Frame || !Result)
    return sourceStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  NevercInterfaceID ArtifactType{};
  void *Candidate = nullptr;
  const void *Payload = nullptr;
  if (samePluginInterfaceID(Frame->Phase, prepIncludePhaseID())) {
    ArtifactType = prepIncludeArtifactID();
    NevercStatus Resolve = Executor->resolveArtifactPayload(
        Task, Frame->Input, ArtifactType, &Payload);
    if (Resolve.Code != NEVERC_STATUS_OK)
      return Resolve;
    Candidate = new (std::nothrow) prep_bridge_detail::IncludePhaseArtifact(
        *static_cast<const prep_bridge_detail::IncludePhaseArtifact *>(
            Payload));
  } else if (samePluginInterfaceID(Frame->Phase, prepMacroPhaseID())) {
    ArtifactType = prepMacroArtifactID();
    NevercStatus Resolve = Executor->resolveArtifactPayload(
        Task, Frame->Input, ArtifactType, &Payload);
    if (Resolve.Code != NEVERC_STATUS_OK)
      return Resolve;
    Candidate = new (std::nothrow) prep_bridge_detail::MacroPhaseArtifact(
        *static_cast<const prep_bridge_detail::MacroPhaseArtifact *>(Payload));
  } else if (samePluginInterfaceID(Frame->Phase, prepPragmaPhaseID())) {
    ArtifactType = prepPragmaArtifactID();
    NevercStatus Resolve = Executor->resolveArtifactPayload(
        Task, Frame->Input, ArtifactType, &Payload);
    if (Resolve.Code != NEVERC_STATUS_OK)
      return Resolve;
    Candidate = new (std::nothrow) prep_bridge_detail::PragmaPhaseArtifact(
        *static_cast<const prep_bridge_detail::PragmaPhaseArtifact *>(Payload));
  } else if (samePluginInterfaceID(Frame->Phase, prepFeatureQueryPhaseID())) {
    ArtifactType = prepFeatureQueryArtifactID();
    NevercStatus Resolve = Executor->resolveArtifactPayload(
        Task, Frame->Input, ArtifactType, &Payload);
    if (Resolve.Code != NEVERC_STATUS_OK)
      return Resolve;
    Candidate =
        new (std::nothrow) prep_bridge_detail::FeatureQueryPhaseArtifact(
            *static_cast<const prep_bridge_detail::FeatureQueryPhaseArtifact *>(
                Payload));
  } else {
    return sourceStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  }
  if (!Candidate)
    return sourceStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  auto Handle = Executor->createCandidate(Task, ArtifactType, Candidate);
  if (!Handle) {
    auto Type = Artifacts.find(ArtifactType);
    if (Type)
      Type->destroyPayload(Candidate);
    consumeError(Handle.takeError());
    return sourceStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *Result = {};
  Result->Header = {sizeof(*Result), NEVERC_PLUGIN_ABI_MAJOR,
                    NEVERC_PLUGIN_ABI_MINOR, 0};
  Result->Action = NEVERC_PHASE_REPLACE;
  Result->Output = *Handle;
  return neverc_status_ok();
}

NevercStatus PluginSourcePhaseRuntime::Impl::parserBuiltin(
    const NevercPhaseFrame *Frame, NevercPhaseResult *Result) {
  if (!Frame || !Result || !ActiveSema)
    return sourceStatus(NEVERC_STATUS_INVALID_STATE);
  const void *InputPayload = nullptr;
  NevercStatus Status = Executor->resolveArtifactPayload(
      Task, Frame->Input, prepTokenStreamArtifactID(), &InputPayload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  const auto &Stream =
      *static_cast<const prep_bridge_detail::PrepTokenStreamArtifact *>(
          InputPayload);
  auto Source = llvm::find_if(
      Stream.Dependencies, [](const PluginDependencySnapshot &Dependency) {
        return Dependency.Kind == NEVERC_INPUT_DEPENDENCY_SOURCE &&
               !Dependency.CanonicalPath.empty();
      });
  if (Source == Stream.Dependencies.end())
    return sourceStatus(NEVERC_STATUS_VERIFICATION_FAILED);

  RunParser(*ActiveSema, ParserHooks.get(), ActivePrintStats,
            ActiveParserInputInitialized);

  auto *Candidate = new (std::nothrow) ASTUnitArtifact;
  if (!Candidate)
    return sourceStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  Candidate->Context = &ActiveSema->getTreeContext();
  Candidate->TranslationUnit =
      ActiveSema->getTreeContext().getTranslationUnitDecl();
  Candidate->Product = standardASTProductID();
  Candidate->SemanticState = NEVERC_AST_UNIT_SEMANTICALLY_ANALYZED;
  Candidate->SourceIdentity = Source->CanonicalPath;
  Candidate->SourceDigest = Source->ContentDigest;
  Candidate->HasSourceDigest = true;
  Candidate->ConsumerNotified = true;
  auto Handle =
      Executor->createCandidate(Task, astUnitArtifactID(), Candidate);
  if (!Handle) {
    delete Candidate;
    consumeError(Handle.takeError());
    return sourceStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *Result = {};
  Result->Header = {sizeof(*Result), NEVERC_PLUGIN_ABI_MAJOR,
                    NEVERC_PLUGIN_ABI_MINOR, 0};
  Result->Action = NEVERC_PHASE_REPLACE;
  Result->Output = *Handle;
  return neverc_status_ok();
}

NevercStatus PluginSourcePhaseRuntime::Impl::semaBuiltin(
    const NevercPhaseFrame *Frame, NevercPhaseResult *Result) {
  if (!SemaProvider)
    return sourceStatus(NEVERC_STATUS_INVALID_STATE);
  return SemaProvider->builtinProvider(Frame, Result);
}

Error PluginSourcePhaseRuntime::Impl::recordAndVerifyDependency(
    const SourceUnitArtifact &Unit) {
  auto Query = Task.processServices().interfaces().query(
      ioPluginInterfaceID(), NEVERC_IO_API_MAJOR, NEVERC_IO_API_MINOR);
  if (!Query)
    return Query.takeError();
  const auto *IO = static_cast<const NevercIOAPI *>(Query->Table);
  if (!IO || !IO->RecordDependency)
    return createStringError(inconvertibleErrorCode(),
                             "plugin IO dependency API is unavailable");
  ArrayRef<uint8_t> Content(Unit.Content.data(),
                            Unit.Content.empty() ? 0 : Unit.Content.size() - 1);
  std::array<uint8_t, 32> Digest = SHA256::hash(Content);
  NevercDependencyDescriptor Descriptor{};
  Descriptor.Header = {sizeof(Descriptor), NEVERC_IO_API_MAJOR,
                       NEVERC_IO_API_MINOR, 0};
  Descriptor.CanonicalPath = stringView(Unit.CanonicalIdentity);
  Descriptor.ContentDigest = {Digest.data(), Digest.size()};
  Descriptor.Kind = NEVERC_INPUT_DEPENDENCY_SOURCE;
  Descriptor.System = Unit.System ? NEVERC_TRUE : NEVERC_FALSE;
  Descriptor.ProviderID = stringView(Unit.ProviderID);
  NevercDependencyHandle Dependency{};
  NevercStatus Status = IO->RecordDependency(IO->Context, Task.handle(),
                                             &Descriptor, &Dependency);
  if (Status.Code != NEVERC_STATUS_OK)
    return createStringError(inconvertibleErrorCode(),
                             "failed to record source dependency");

  const std::vector<PluginDependencySnapshot> Dependencies =
      getPluginDependencies(Task);
  auto Match =
      llvm::find_if(Dependencies, [&](const PluginDependencySnapshot &Value) {
        return Value.CanonicalPath == Unit.CanonicalIdentity &&
               Value.ContentDigest == Digest &&
               Value.Kind == NEVERC_INPUT_DEPENDENCY_SOURCE &&
               Value.System == Unit.System &&
               Value.ProviderID == Unit.ProviderID;
      });
  if (Match == Dependencies.end())
    return createStringError(inconvertibleErrorCode(),
                             "source dependency record failed verification");
  return Error::success();
}

Error PluginSourcePhaseRuntime::Impl::installAndVerify(
    SourceUnitArtifact &Unit) {
  if (!Unit.Installed) {
    if (!Unit.MemoryBacked || Unit.Content.empty() || Unit.Content.back() != 0)
      return createStringError(inconvertibleErrorCode(),
                               "replacement source unit is not installable");
    StringRef Contents(reinterpret_cast<const char *>(Unit.Content.data()),
                       Unit.Content.size() - 1);
    MemoryBufferRef Buffer(Contents, Unit.LogicalPath);
    SrcMgr::CharacteristicKind Kind =
        Unit.System ? SrcMgr::C_System : SrcMgr::C_User;
    Unit.MainFile = SourceMgr.createFileID(Buffer, Kind);
    if (!Unit.MainFile.isValid())
      return createStringError(inconvertibleErrorCode(),
                               "replacement source has no main file");
    SourceMgr.setMainFileID(Unit.MainFile);
    Unit.Installed = true;
  }
  if (SourceMgr.getMainFileID() != Unit.MainFile)
    return createStringError(
        inconvertibleErrorCode(),
        "source unit main file identity does not match SourceManager");
  auto Buffer = SourceMgr.getBufferOrNone(Unit.MainFile);
  if (!Buffer || Buffer->getBufferSize() + 1 != Unit.Content.size() ||
      !std::equal(Buffer->getBuffer().bytes_begin(),
                  Buffer->getBuffer().bytes_end(), Unit.Content.begin()))
    return createStringError(
        inconvertibleErrorCode(),
        "source unit buffer failed post-publication verification");
  return recordAndVerifyDependency(Unit);
}

PluginSourcePhaseRuntime::PluginSourcePhaseRuntime(
    std::unique_ptr<Impl> StateValue)
    : State(std::move(StateValue)) {}

PluginSourcePhaseRuntime::~PluginSourcePhaseRuntime() {
  if (State && State->SemaBridge)
    State->SemaBridge->setPhaseAPI(nullptr);
  if (State && State->SemaProvider)
    State->SemaProvider->detach();
  if (State && State->AttachedSema &&
      State->AttachedSema->getPluginHooks() == State->SemaHooks.get())
    State->AttachedSema->setPluginHooks(nullptr);
  if (State && State->AttachedPrep &&
      State->AttachedPrep->getPluginHooks() == State->PrepHooks.get())
    State->AttachedPrep->setPluginHooks(nullptr);
  if (State && State->ProcessBridge)
    State->ProcessBridge->detach(State->Task.handle());
}

Expected<std::unique_ptr<PluginSourcePhaseRuntime>>
PluginSourcePhaseRuntime::create(PluginTaskContext &Task,
                                 SourceManager &SourceMgr,
                                 const LangOptions &LangOpts) {
  auto ProcessBridge = findFrontendProcessBridge(Task.processServices());
  if (!ProcessBridge)
    return createStringError(inconvertibleErrorCode(),
                             "plugin frontend interface is not registered");
  auto Graph = PluginPhaseGraph::createBuiltinSourceGraph();
  if (!Graph)
    return Graph.takeError();
  auto State =
      std::make_unique<Impl>(Task, SourceMgr, LangOpts, std::move(*Graph));
  State->ProcessBridge = std::move(ProcessBridge);
  State->Locations =
      std::make_unique<FrontendPluginBridge>(Task, SourceMgr, LangOpts);

  auto RegisterType = [&](PluginArtifactTypeDescriptor Descriptor) -> Error {
    auto Type = State->Artifacts.registerType(std::move(Descriptor));
    if (!Type)
      return Type.takeError();
    return Error::success();
  };
  if (Error E =
          RegisterType({sourceInputArtifactID(), "source.input",
                        PluginArtifactOwnership::Owned, cloneSourceInput,
                        [](void *Payload) {
                          delete static_cast<SourceInputArtifact *>(Payload);
                        },
                        verifySourceInput}))
    return std::move(E);
  if (Error E =
          RegisterType({sourceUnitArtifactID(), "source.unit",
                        PluginArtifactOwnership::Owned, cloneSourceUnit,
                        [](void *Payload) {
                          delete static_cast<SourceUnitArtifact *>(Payload);
                        },
                        verifySourceUnit}))
    return std::move(E);
  if (Error E = RegisterType(
          {sourceOutputSinkArtifactID(), "source.output_sink",
           PluginArtifactOwnership::Owned, cloneSourceOutputSink,
           [](void *Payload) {
             delete static_cast<SourceOutputSinkArtifact *>(Payload);
           },
           verifySourceOutputSink}))
    return std::move(E);
  if (Error E = registerPrepTokenArtifactType(State->Artifacts))
    return std::move(E);
  if (Error E = registerPrepTokenStreamArtifactType(State->Artifacts))
    return std::move(E);
  if (Error E = registerPrepHookArtifactTypes(State->Artifacts))
    return std::move(E);
  if (Error E = registerParserExtensionArtifactType(State->Artifacts))
    return std::move(E);
  if (Error E = registerSemaExtensionArtifactType(State->Artifacts))
    return std::move(E);
  if (Error E = registerASTUnitArtifactType(State->Artifacts))
    return std::move(E);
  if (Error E = registerSemanticUnitArtifactType(State->Artifacts))
    return std::move(E);
  if (Error E = State->Artifacts.freeze())
    return std::move(E);

  State->Executor =
      std::make_unique<PluginPhaseExecutor>(State->Graph, State->Artifacts);
  State->SemaProvider = std::make_unique<PluginSemaProviderRuntime>(
      Task, State->Artifacts, *State->Executor);
  if (Error E = State->Executor->importSessionRegistrations(Task.session()))
    return std::move(E);
  Impl *Raw = State.get();
  if (Error E = State->Executor->setBuiltinProvider(
          sourceResolveInputPhaseID(),
          [Raw](const NevercPhaseFrame *Frame, NevercPhaseResult *Result) {
            return Raw->resolveInputBuiltin(Frame, Result);
          }))
    return std::move(E);
  if (Error E = State->Executor->setBuiltinProvider(
          sourceOpenPhaseID(),
          [Raw](const NevercPhaseFrame *Frame, NevercPhaseResult *Result) {
            return Raw->openBuiltin(Frame, Result);
          }))
    return std::move(E);
  if (Error E = State->Executor->setBuiltinProvider(
          prepTokenPhaseID(),
          [Raw](const NevercPhaseFrame *Frame, NevercPhaseResult *Result) {
            return Raw->tokenBuiltin(Frame, Result);
          }))
    return std::move(E);
  if (Error E = State->Executor->setBuiltinProvider(
          prepBuildTokenStreamPhaseID(),
          [Raw](const NevercPhaseFrame *Frame, NevercPhaseResult *Result) {
            return Raw->tokenStreamBuiltin(Frame, Result);
          }))
    return std::move(E);
  for (NevercInterfaceID Phase :
       {prepIncludePhaseID(), prepMacroPhaseID(), prepPragmaPhaseID(),
        prepFeatureQueryPhaseID()})
    if (Error E = State->Executor->setBuiltinProvider(
            Phase,
            [Raw](const NevercPhaseFrame *Frame, NevercPhaseResult *Result) {
              return Raw->prepHookBuiltin(Frame, Result);
            }))
      return std::move(E);
  if (Error E = State->Executor->setBuiltinProvider(
          syntaxParsePhaseID(),
          [Raw](const NevercPhaseFrame *Frame, NevercPhaseResult *Result) {
            return Raw->parserBuiltin(Frame, Result);
          }))
    return std::move(E);
  if (Error E = State->Executor->setBuiltinProvider(
          semaAnalyzePhaseID(),
          [Raw](const NevercPhaseFrame *Frame, NevercPhaseResult *Result) {
            return Raw->semaBuiltin(Frame, Result);
          }))
    return std::move(E);
  if (Error E = registerParserBuiltinProviders(Task, *State->Executor))
    return std::move(E);
  if (Error E = registerSemaBuiltinProviders(Task, *State->Executor))
    return std::move(E);
  if (Error E = State->Executor->freeze())
    return std::move(E);
  State->ProcessBridge->attach(Task, *State->Locations, *State);
  return std::unique_ptr<PluginSourcePhaseRuntime>(
      new PluginSourcePhaseRuntime(std::move(State)));
}

Error PluginSourcePhaseRuntime::initialize(const PluginSourceInput &Input,
                                           BuiltinOpen OpenBuiltin) {
  if (!State || !OpenBuiltin)
    return createStringError(inconvertibleErrorCode(),
                             "source phase runtime is not initialized");
  State->BuiltinFailure.clear();
  State->OpenBuiltin = std::move(OpenBuiltin);

  SourceInputArtifact Initial;
  Initial.Path = Input.Path.str();
  Initial.Language = Input.Language;
  Initial.HasBuffer = Input.HasBuffer;
  Initial.System = Input.System;
  Initial.Preprocessed = Input.Preprocessed;
  if (Input.HasBuffer)
    Initial.Buffer.assign(Input.Buffer.begin(), Input.Buffer.end());
  if (Error E = verifySourceInput(&Initial))
    return E;

  NevercPhaseRoute Route = defaultRoute();
  auto InitialView = State->Executor->createArtifactView(
      State->Task, sourceInputArtifactID(), &Initial, 1);
  if (!InitialView)
    return InitialView.takeError();
  auto ReleaseInitial = make_scope_exit([&] {
    (void)State->Task.handles().release(*InitialView, PluginArtifactHandleKind);
  });

  State->ResolvedInput = std::make_unique<PluginArtifactSlot>(
      State->Artifacts.find(sourceInputArtifactID()));
  if (Error E = State->Executor->execute(State->Task.session(), State->Task,
                                         sourceResolveInputPhaseID(), Route,
                                         *InitialView, *State->ResolvedInput))
    return E;
  PluginArtifactSlot::Snapshot Resolved = State->ResolvedInput->snapshot();
  auto ResolvedView = State->Executor->createArtifactView(
      State->Task, sourceInputArtifactID(), Resolved.Payload,
      Resolved.Generation);
  if (!ResolvedView)
    return ResolvedView.takeError();
  auto ReleaseResolved = make_scope_exit([&] {
    (void)State->Task.handles().release(*ResolvedView,
                                        PluginArtifactHandleKind);
  });

  State->SourceUnit = std::make_unique<PluginArtifactSlot>(
      State->Artifacts.find(sourceUnitArtifactID()));
  if (Error E = State->Executor->execute(State->Task.session(), State->Task,
                                         sourceOpenPhaseID(), Route,
                                         *ResolvedView, *State->SourceUnit)) {
    if (!State->BuiltinFailure.empty())
      return joinErrors(
          std::move(E),
          createStringError(inconvertibleErrorCode(), State->BuiltinFailure));
    return E;
  }
  auto UnitSnapshot = State->SourceUnit->snapshot();
  auto &Unit = *const_cast<SourceUnitArtifact *>(
      static_cast<const SourceUnitArtifact *>(UnitSnapshot.Payload));
  if (Error E = State->installAndVerify(Unit))
    return E;

  auto UnitView = State->Executor->createArtifactView(
      State->Task, sourceUnitArtifactID(), UnitSnapshot.Payload,
      UnitSnapshot.Generation);
  if (!UnitView)
    return UnitView.takeError();
  auto ReleaseUnit = make_scope_exit([&] {
    (void)State->Task.handles().release(*UnitView, PluginArtifactHandleKind);
  });
  return State->Executor->notify(State->Task.session(), State->Task,
                                 sourceAfterOpenPhaseID(), Route, *UnitView);
}

Error PluginSourcePhaseRuntime::attachPrepEngine(PrepEngine &Prep) {
  if (!State || !State->Locations)
    return createStringError(inconvertibleErrorCode(),
                             "source phase runtime is not initialized");
  if (State->PrepBridge)
    return createStringError(inconvertibleErrorCode(),
                             "prep engine is already attached");
  if (&Prep.getSourceManager() != &State->SourceMgr)
    return createStringError(inconvertibleErrorCode(),
                             "prep engine uses a different source manager");
  auto Bridge =
      std::make_unique<PluginPrepBridge>(State->Task, Prep, *State->Locations);
  Bridge->attachTokenPhaseExecutor(*State->Executor);
  if (Error E = Bridge->attachProcessInterface())
    return E;

  std::unique_ptr<PrepPluginHooks> Hooks;
  if (State->Executor->hasBindings(prepTokenPhaseID()) ||
      State->Executor->hasBindings(prepIncludePhaseID()) ||
      State->Executor->hasBindings(prepMacroPhaseID()) ||
      State->Executor->hasBindings(prepPragmaPhaseID()) ||
      State->Executor->hasBindings(prepFeatureQueryPhaseID())) {
    auto Created = createPrepPluginHooks(State->Task, Prep, State->Artifacts,
                                         *State->Executor, *Bridge);
    if (!Created)
      return Created.takeError();
    Hooks = std::move(*Created);
  }

  if (!State->SourceUnit)
    return createStringError(inconvertibleErrorCode(),
                             "source unit is unavailable for preprocessing");
  State->AttachedPrep = &Prep;
  auto ResetAttached = make_scope_exit([&] { State->AttachedPrep = nullptr; });
  PluginArtifactSlot::Snapshot Unit = State->SourceUnit->snapshot();
  auto UnitView = State->Executor->createArtifactView(
      State->Task, sourceUnitArtifactID(), Unit.Payload, Unit.Generation);
  if (!UnitView)
    return UnitView.takeError();
  auto ReleaseUnit = make_scope_exit([&] {
    (void)State->Task.handles().release(*UnitView, PluginArtifactHandleKind);
  });
  auto TokenStream = std::make_unique<PluginArtifactSlot>(
      State->Artifacts.find(prepTokenStreamArtifactID()));
  NevercPhaseRoute Route = defaultRoute();
  if (Error E = State->Executor->execute(State->Task.session(), State->Task,
                                         prepBuildTokenStreamPhaseID(), Route,
                                         *UnitView, *TokenStream))
    return E;
  PluginArtifactSlot::Snapshot Stream = TokenStream->snapshot();
  const auto &Artifact =
      *static_cast<const prep_bridge_detail::PrepTokenStreamArtifact *>(
          Stream.Payload);
  if (!Artifact.BuiltinLazy)
    if (Error E = PluginTokenLexer::install(Prep, Artifact.Tokens))
      return E;

  Prep.setPluginHooks(Hooks.get(),
                      State->Executor->hasBindings(prepTokenPhaseID()));
  State->AttachedPrep = &Prep;
  State->PrepHooks = std::move(Hooks);
  State->PrepBridge = std::move(Bridge);
  State->TokenStream = std::move(TokenStream);
  ResetAttached.release();
  return Error::success();
}

Error PluginSourcePhaseRuntime::attachTreeContext(TreeContext &Context) {
  if (State->ASTBridge)
    return Error::success();
  if (!State->Locations)
    return createStringError(inconvertibleErrorCode(),
                             "source location bridge is unavailable for AST");
  auto Bridge =
      std::make_unique<PluginASTBridge>(State->Task, Context, *State->Locations,
                                        State->PrepBridge.get());
  if (Error E = Bridge->attachProcessInterface())
    return E;
  State->ASTBridge = std::move(Bridge);
  if (!State->PrepBridge)
    return createStringError(
        inconvertibleErrorCode(),
        "preprocessor bridge is unavailable for parser execution");
  auto Hooks = createParserPluginHooks(State->Task, State->Artifacts,
                                       *State->Executor, *State->PrepBridge,
                                       *State->ASTBridge, *State->Locations);
  if (!Hooks)
    return Hooks.takeError();
  State->ParserHooks = std::move(*Hooks);
  return Error::success();
}

Error PluginSourcePhaseRuntime::attachSema(Sema &SemanticAnalyzer) {
  if (!State || !State->Locations || !State->ASTBridge)
    return createStringError(inconvertibleErrorCode(),
                             "AST bridge is unavailable for Sema");
  if (State->SemaBridge && State->AttachedSema == &SemanticAnalyzer)
    return Error::success();
  if (State->AttachedSema &&
      State->AttachedSema->getPluginHooks() == State->SemaHooks.get())
    State->AttachedSema->setPluginHooks(nullptr);
  if (State->SemaBridge)
    State->SemaBridge->setPhaseAPI(nullptr);
  if (State->SemaProvider)
    State->SemaProvider->detach();
  State->SemaHooks.reset();
  State->SemaBridge.reset();
  State->AttachedSema = nullptr;
  auto Bridge = std::make_unique<PluginSemaBridge>(
      State->Task, SemanticAnalyzer, *State->ASTBridge, *State->Locations);
  if (Error E = Bridge->attachProcessInterface())
    return E;
  if (!State->SemaProvider)
    return createStringError(inconvertibleErrorCode(),
                             "Sema provider runtime is unavailable");
  std::unique_ptr<SemaPluginHooks> Hooks;
  if (hasSemaExtensionBindings(*State->Executor)) {
    auto Created = createSemaPluginHooks(
        State->Task, State->Artifacts, *State->Executor, *State->ASTBridge,
        *State->Locations, *Bridge);
    if (!Created)
      return Created.takeError();
    Hooks = std::move(*Created);
    SemanticAnalyzer.setPluginHooks(Hooks.get());
  }
  State->SemaProvider->attach(SemanticAnalyzer, *State->ASTBridge);
  Bridge->setPhaseAPI(State->SemaProvider.get());
  State->AttachedSema = &SemanticAnalyzer;
  State->SemaBridge = std::move(Bridge);
  State->SemaHooks = std::move(Hooks);
  return Error::success();
}

Error PluginSourcePhaseRuntime::runParserPhase(Sema &SemanticAnalyzer,
                                               bool PrintStats) {
  if (!State || !State->Executor || !State->TokenStream || !State->ASTBridge)
    return createStringError(inconvertibleErrorCode(),
                             "parser phase runtime is not initialized");
  if (State->ActiveSema)
    return createStringError(inconvertibleErrorCode(),
                             "parser phase is already active");

  PluginArtifactSlot::Snapshot Stream = State->TokenStream->snapshot();
  if (!Stream.Payload)
    return createStringError(inconvertibleErrorCode(),
                             "parser phase has no token-stream input");
  PrepPluginHooks *SavedPrepHooks = nullptr;
  bool RestorePrepHooks = false;
  bool ParserInputInitialized = false;
  if (State->Executor->hasBindings(syntaxParsePhaseID())) {
    if (!State->AttachedPrep)
      return createStringError(inconvertibleErrorCode(),
                               "parser phase has no preprocessor");
    auto &MutableStream =
        *const_cast<prep_bridge_detail::PrepTokenStreamArtifact *>(
            static_cast<const prep_bridge_detail::PrepTokenStreamArtifact *>(
                Stream.Payload));
    std::vector<Token> Tokens;
    Tokens.reserve(MutableStream.Tokens.size());
    State->AttachedPrep->InitMainInput();
    if (!State->AttachedPrep->getCurrentLexer())
      return createStringError(inconvertibleErrorCode(),
                               "parser token stream has no lexer");
    for (;;) {
      if (Tokens.size() >= NEVERC_PREP_TOKEN_STREAM_MAX_TOKENS)
        return createStringError(inconvertibleErrorCode(),
                                 "parser token stream exceeds token limit");
      Token Current;
      State->AttachedPrep->Lex(Current);
      Tokens.push_back(Current);
      if (Current.is(tok::eof))
        break;
    }
    MutableStream.Tokens = std::move(Tokens);
    MutableStream.BuiltinLazy = false;
    SavedPrepHooks = State->AttachedPrep->getPluginHooks();
    State->AttachedPrep->setPluginHooks(nullptr);
    RestorePrepHooks = true;
    State->AttachedPrep->PushTokenStream(
        ArrayRef<Token>(MutableStream.Tokens), /*DisableMacroExpansion=*/true,
        /*IsReinject=*/true);
    ParserInputInitialized = true;
  }
  auto RestoreHooks = make_scope_exit([&] {
    if (RestorePrepHooks)
      State->AttachedPrep->setPluginHooks(
          SavedPrepHooks,
          State->Executor->hasBindings(prepTokenPhaseID()));
  });
  auto StreamView = State->Executor->createArtifactView(
      State->Task, prepTokenStreamArtifactID(), Stream.Payload,
      Stream.Generation);
  if (!StreamView)
    return StreamView.takeError();
  auto ReleaseStream = make_scope_exit([&] {
    (void)State->Task.handles().release(*StreamView,
                                        PluginArtifactHandleKind);
  });

  State->ActiveSema = &SemanticAnalyzer;
  State->ActivePrintStats = PrintStats;
  State->ActiveParserInputInitialized = ParserInputInitialized;
  auto ResetActive = make_scope_exit([&] {
    State->ActiveSema = nullptr;
    State->ActivePrintStats = false;
    State->ActiveParserInputInitialized = false;
  });

  auto Unit = std::make_unique<PluginArtifactSlot>(
      State->Artifacts.find(astUnitArtifactID()));
  NevercPhaseRoute Route = defaultRoute();
  if (Error E = State->Executor->execute(
          State->Task.session(), State->Task, syntaxParsePhaseID(), Route,
          *StreamView, *Unit))
    return E;

  PluginArtifactSlot::Snapshot Published = Unit->snapshot();
  if (!Published.Payload)
    return createStringError(inconvertibleErrorCode(),
                             "parser phase published no AST unit");
  auto &AST = *const_cast<ASTUnitArtifact *>(
      static_cast<const ASTUnitArtifact *>(Published.Payload));
  if (AST.Context != &SemanticAnalyzer.getTreeContext() ||
      AST.TranslationUnit !=
          SemanticAnalyzer.getTreeContext().getTranslationUnitDecl())
    return createStringError(inconvertibleErrorCode(),
                             "parser AST unit belongs to another tree context");

  auto ASTView = State->Executor->createArtifactView(
      State->Task, astUnitArtifactID(), Published.Payload,
      Published.Generation);
  if (!ASTView)
    return ASTView.takeError();
  auto ReleaseAST = make_scope_exit([&] {
    (void)State->Task.handles().release(*ASTView, PluginArtifactHandleKind);
  });
  auto SemanticUnit = std::make_unique<PluginArtifactSlot>(
      State->Artifacts.find(semanticUnitArtifactID()));
  if (!State->SemaProvider)
    return createStringError(inconvertibleErrorCode(),
                             "Sema provider runtime is unavailable");
  State->SemaProvider->clearFailureMessage();
  if (Error E = State->Executor->execute(
          State->Task.session(), State->Task, semaAnalyzePhaseID(), Route,
          *ASTView, *SemanticUnit)) {
    if (!State->SemaProvider->failureMessage().empty())
      return joinErrors(
          std::move(E),
          createStringError(inconvertibleErrorCode(),
                            State->SemaProvider->failureMessage()));
    return E;
  }
  PluginArtifactSlot::Snapshot Semantic = SemanticUnit->snapshot();
  if (!Semantic.Payload)
    return createStringError(inconvertibleErrorCode(),
                             "Sema phase published no semantic unit");
  const auto &Analyzed =
      *static_cast<const SemanticUnitArtifact *>(Semantic.Payload);
  if (Analyzed.Context != AST.Context ||
      Analyzed.TranslationUnit != AST.TranslationUnit)
    return createStringError(
        inconvertibleErrorCode(),
        "semantic unit belongs to another parser AST unit");
  if (!samePluginInterfaceID(Analyzed.Product, standardSemanticProductID()))
    return createStringError(
        inconvertibleErrorCode(),
        "semantic product has no matching downstream IR provider");
  if (Analyzed.DiagnosticState != NEVERC_SEMANTIC_DIAGNOSTICS_CLEAN)
    return createStringError(inconvertibleErrorCode(),
                             "semantic unit contains compilation errors");

  if (!AST.ConsumerNotified) {
    TreeConsumer &Consumer = SemanticAnalyzer.getTreeConsumer();
    for (Decl *Declaration : AST.TranslationUnit->decls())
      if (!Consumer.ProcessTopLevelDecl(DeclGroupRef(Declaration))) {
        AST.ConsumerNotified = true;
        State->ASTUnit = std::move(Unit);
        State->SemanticUnit = std::move(SemanticUnit);
        return Error::success();
      }
    Consumer.ProcessTranslationUnit(*AST.Context);
    AST.ConsumerNotified = true;
  }
  State->ASTUnit = std::move(Unit);
  State->SemanticUnit = std::move(SemanticUnit);
  return Error::success();
}

ParserPluginHooks *PluginSourcePhaseRuntime::parserPluginHooks() const {
  return State ? State->ParserHooks.get() : nullptr;
}

std::unique_ptr<TreeConsumer>
PluginSourcePhaseRuntime::createTreeConsumer() {
  if (!State || !State->ASTBridge)
    return nullptr;
  return std::make_unique<PluginTreeConsumer>(*State->ASTBridge);
}

Error registerPluginFrontendInterface(PluginProcessServices &Services) {
  if (Services.interfaces().isFrozen())
    return createStringError(
        inconvertibleErrorCode(),
        "cannot register plugin frontend after interface freeze");
  auto Bridge = std::make_shared<FrontendPluginProcessBridge>(Services);
  if (Error E = Services.registerHostService(sourceLocationPluginInterfaceID(),
                                             Bridge))
    return E;
  if (Error E = Services.interfaces().registerInterface(
          sourceLocationPluginInterfaceID(), NEVERC_INTERFACE_STABLE,
          &Bridge->api(), {}))
    return E;
  if (Error E = registerPluginPrepInterface(Services))
    return E;
  if (Error E = registerPluginASTInterface(Services))
    return E;
  if (Error E = registerPluginParserInterface(Services))
    return E;
  return registerPluginSemaInterface(Services);
}

} // namespace neverc::plugin
