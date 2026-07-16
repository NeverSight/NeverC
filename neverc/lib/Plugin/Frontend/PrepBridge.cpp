#include "FrontendPluginInterfaces.h"
#include "PluginPrepObserver.h"
#include "PrepBridgeInternal.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Scan/SourceScanner.h"
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;

namespace neverc::plugin {
using namespace prep_bridge_detail;

namespace {

constexpr uint64_t MaximumBuilderSpellingBytes = UINT64_C(1) << 20;
constexpr NevercTokenFlags BuilderTokenFlags =
    NEVERC_TOKEN_FLAG_START_OF_LINE | NEVERC_TOKEN_FLAG_LEADING_SPACE |
    NEVERC_TOKEN_FLAG_DISABLE_EXPANSION | NEVERC_TOKEN_FLAG_LEADING_EMPTY_MACRO;

NevercStatus setNativeFlags(Token &Value, NevercTokenFlags Flags) {
  if ((Flags & ~NEVERC_TOKEN_FLAG_ALL) != 0 ||
      (Flags & ~BuilderTokenFlags) != 0)
    return status(NEVERC_STATUS_VERIFICATION_FAILED);
  if ((Flags & NEVERC_TOKEN_FLAG_START_OF_LINE) != 0)
    Value.setFlag(Token::StartOfLine);
  if ((Flags & NEVERC_TOKEN_FLAG_LEADING_SPACE) != 0)
    Value.setFlag(Token::LeadingSpace);
  if ((Flags & NEVERC_TOKEN_FLAG_DISABLE_EXPANSION) != 0)
    Value.setFlag(Token::DisableExpand);
  if ((Flags & NEVERC_TOKEN_FLAG_LEADING_EMPTY_MACRO) != 0)
    Value.setFlag(Token::LeadingEmptyMacro);
  return neverc_status_ok();
}

bool validLiteralSpelling(PrepEngine &Prep, const Token &Value,
                          StringRef Spelling) {
  Token Scanned;
  SourceLocation SpellingLocation =
      Prep.getSourceManager().getSpellingLoc(Value.getLocation());
  if (SourceScanner::scanRawToken(SpellingLocation, Scanned,
                                  Prep.getSourceManager(), Prep.getLangOpts(),
                                  /*IgnoreWhiteSpace=*/true))
    return false;
  return Scanned.getKind() == Value.getKind() &&
         Scanned.getLength() == Spelling.size();
}

template <typename PayloadT>
NevercStatus resolveBuilderPayload(PluginTaskContext &Task, PrepEngine &Prep,
                                   NevercTaskHandle TaskHandle,
                                   NevercHandle Handle, PluginHandleKind Kind,
                                   PayloadT **OutPayload) {
  if (!OutPayload)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutPayload = nullptr;
  if (!sameHandle(TaskHandle, Task.handle()))
    return status(NEVERC_STATUS_WRONG_SCOPE);
  if (Task.isEnded())
    return status(NEVERC_STATUS_INVALID_STATE);
  void *RawPayload = nullptr;
  NevercStatus Status = Task.handles().resolve(Handle, Kind, &RawPayload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *Payload = static_cast<PayloadT *>(RawPayload);
  if (Payload->Engine != &Prep)
    return status(NEVERC_STATUS_WRONG_SCOPE);
  *OutPayload = Payload;
  return neverc_status_ok();
}

} // namespace

class PluginPrepProcessBridge final
    : public PluginHostService,
      public std::enable_shared_from_this<PluginPrepProcessBridge> {
public:
  explicit PluginPrepProcessBridge(PluginProcessServices &ServicesValue)
      : Services(ServicesValue) {
    API.Header = {sizeof(API), NEVERC_PREP_API_MAJOR, NEVERC_PREP_API_MINOR, 0};
    API.Context = this;
    API.GetTokenInfo = GetTokenInfo;
    API.GetTokenInfoBatch = GetTokenInfoBatch;
    API.GetTokenStreamView = GetTokenStreamView;
    API.GetTokenStreamToken = GetTokenStreamToken;
    API.GetIdentifierInfo = GetIdentifierInfo;
    API.GetOrCreateIdentifier = GetOrCreateIdentifier;
    API.GetMacroDefinitionForIdentifier = GetMacroDefinitionForIdentifier;
    API.GetMacroDefinitionInfo = GetMacroDefinitionInfo;
    API.GetMacroParameter = GetMacroParameter;
    API.GetMacroReplacementToken = GetMacroReplacementToken;
    API.GetMacroDirectiveInfo = GetMacroDirectiveInfo;
    API.GetMacroArgumentInfo = GetMacroArgumentInfo;
    API.GetMacroArgumentTokenStream = GetMacroArgumentTokenStream;
    API.CreateTokenBuilder = CreateTokenBuilder;
    API.TokenBuilderSetKind = TokenBuilderSetKind;
    API.TokenBuilderSetIdentifier = TokenBuilderSetIdentifier;
    API.TokenBuilderSetLiteral = TokenBuilderSetLiteral;
    API.TokenBuilderSetLocation = TokenBuilderSetLocation;
    API.TokenBuilderSetFlags = TokenBuilderSetFlags;
    API.TokenBuilderCommit = TokenBuilderCommit;
    API.DestroyTokenBuilder = DestroyTokenBuilder;
    API.RegisterEventObserver = RegisterEventObserver;
    API.GetTokenPhaseInput = GetTokenPhaseInput;
    API.CreateTokenPhaseOutput = CreateTokenPhaseOutput;
    API.GetIncludePhaseInput = GetIncludePhaseInput;
    API.CreateIncludePhaseOutput = CreateIncludePhaseOutput;
    API.GetMacroPhaseInput = GetMacroPhaseInput;
    API.CreateMacroPhaseOutput = CreateMacroPhaseOutput;
    API.GetPragmaPhaseInput = GetPragmaPhaseInput;
    API.CreatePragmaPhaseOutput = CreatePragmaPhaseOutput;
    API.GetFeatureQueryPhaseInput = GetFeatureQueryPhaseInput;
    API.CreateFeatureQueryPhaseOutput = CreateFeatureQueryPhaseOutput;
    API.GetTokenStreamPhaseInput = GetTokenStreamPhaseInput;
    API.CreateTokenStreamBuilder = CreateTokenStreamBuilder;
    API.TokenStreamBuilderAppend = TokenStreamBuilderAppend;
    API.TokenStreamBuilderCommit = TokenStreamBuilderCommit;
    API.DestroyTokenStreamBuilder = DestroyTokenStreamBuilder;
  }

  const NevercPrepAPI &api() const { return API; }

  Error attach(PluginTaskContext &Task, PluginPrepBridge &Bridge) {
    const auto Key = std::make_pair(Task.handle().Owner, Task.handle().Value);
    bool HasObservers = false;
    {
      std::lock_guard<std::mutex> Lock(Mutex);
      TaskBinding &Binding = Tasks[Key];
      if (Binding.Bridge && Binding.Bridge != &Bridge)
        return createStringError(inconvertibleErrorCode(),
                                 "plugin prep task is already attached");
      Binding.Bridge = &Bridge;
      HasObservers = !Binding.Observers.empty();
    }
    if (HasObservers) {
      if (Error E = Bridge.ensureObserverAttached()) {
        std::lock_guard<std::mutex> Lock(Mutex);
        auto It = Tasks.find(Key);
        if (It != Tasks.end() && It->second.Bridge == &Bridge)
          It->second.Bridge = nullptr;
        return E;
      }
    }
    return Error::success();
  }

  void detach(NevercTaskHandle Task) {
    std::lock_guard<std::mutex> Lock(Mutex);
    auto It = Tasks.find(std::make_pair(Task.Owner, Task.Value));
    if (It != Tasks.end())
      It->second.Bridge = nullptr;
  }

  void taskScopeUnregistered(NevercTaskHandle Task) noexcept override {
    std::lock_guard<std::mutex> Lock(Mutex);
    Tasks.erase(std::make_pair(Task.Owner, Task.Value));
  }

  NevercStatus
  registerObserver(NevercTaskHandle TaskHandle,
                   const NevercPrepObserverDescriptor *Descriptor) {
    if (!Descriptor || Descriptor->Header.StructSize < sizeof(*Descriptor) ||
        Descriptor->Header.Major != NEVERC_PREP_API_MAJOR ||
        Descriptor->Header.Minor > NEVERC_PREP_API_MINOR ||
        Descriptor->Header.Flags != 0 || !Descriptor->Callback ||
        Descriptor->Events == 0 ||
        (Descriptor->Events & ~NEVERC_PREP_EVENT_MASK_ALL) != 0)
      return status(NEVERC_STATUS_INVALID_DESCRIPTOR);
    PluginTaskContext *Task = Services.findTaskScope(TaskHandle);
    if (!Task)
      return status(NEVERC_STATUS_STALE_HANDLE);
    if (Task->isEnded())
      return status(NEVERC_STATUS_INVALID_STATE);
    StringRef PluginID = Task->session().currentCallbackPluginID();
    if (PluginID.empty())
      return status(NEVERC_STATUS_REENTRANCY_DENIED);

    PluginPrepBridge *Bound = nullptr;
    {
      std::lock_guard<std::mutex> Lock(Mutex);
      TaskBinding &Binding =
          Tasks[std::make_pair(TaskHandle.Owner, TaskHandle.Value)];
      Binding.Observers.push_back({PluginID.str(), Descriptor->Events,
                                   Descriptor->Callback, Descriptor->UserData});
      Bound = Binding.Bridge;
    }
    if (Bound) {
      if (Error E = Bound->ensureObserverAttached()) {
        consumeError(std::move(E));
        return status(NEVERC_STATUS_RESOURCE_EXHAUSTED);
      }
    }
    return neverc_status_ok();
  }

  NevercStatus dispatch(PluginTaskContext &Task, const NevercPrepEvent &Event) {
    if (Event.Kind == 0 || Event.Kind > NEVERC_PREP_EVENT_COUNT)
      return status(NEVERC_STATUS_INVALID_ARGUMENT);
    std::vector<ObserverRegistration> Observers;
    {
      std::lock_guard<std::mutex> Lock(Mutex);
      auto It =
          Tasks.find(std::make_pair(Task.handle().Owner, Task.handle().Value));
      if (It == Tasks.end())
        return status(NEVERC_STATUS_STALE_HANDLE);
      Observers = It->second.Observers;
    }
    const NevercPrepEventMask EventMask = NEVERC_PREP_EVENT_MASK(Event.Kind);
    for (const ObserverRegistration &Observer : Observers) {
      if ((Observer.Events & EventMask) == 0)
        continue;
      auto Result = Task.invokeCallback(Observer.PluginID, "PrepObserver", [&] {
        return Observer.Callback(Task.handle(), &Event, Observer.UserData);
      });
      if (!Result) {
        consumeError(Result.takeError());
        return status(NEVERC_STATUS_PLUGIN_EXCEPTION);
      }
      if (Result->Code != NEVERC_STATUS_OK)
        return *Result;
    }
    return neverc_status_ok();
  }

private:
  struct ObserverRegistration {
    std::string PluginID;
    NevercPrepEventMask Events = 0;
    NevercPrepEventObserverFn Callback = nullptr;
    void *UserData = nullptr;
  };

  struct TaskBinding {
    PluginPrepBridge *Bridge = nullptr;
    std::vector<ObserverRegistration> Observers;
  };

  PluginPrepBridge *find(NevercTaskHandle Task) {
    std::lock_guard<std::mutex> Lock(Mutex);
    auto It = Tasks.find(std::make_pair(Task.Owner, Task.Value));
    return It == Tasks.end() ? nullptr : It->second.Bridge;
  }

  static PluginPrepProcessBridge *bridge(void *Context) {
    return static_cast<PluginPrepProcessBridge *>(Context);
  }

#define NEVERC_FORWARD_PREP(Name, Signature, ...)                              \
  static NevercStatus NEVERC_CALL Name Signature {                             \
    if (!Context)                                                              \
      return status(NEVERC_STATUS_INVALID_ARGUMENT);                           \
    PluginPrepBridge *Bound = bridge(Context)->find(Task);                     \
    if (!Bound)                                                                \
      return status(NEVERC_STATUS_STALE_HANDLE);                               \
    const NevercPrepAPI &Local = Bound->prepAPI();                             \
    return Local.Name(Local.Context, __VA_ARGS__);                             \
  }

  NEVERC_FORWARD_PREP(GetTokenInfo,
                      (void *Context, NevercTaskHandle Task,
                       NevercTokenHandle Token, NevercTokenInfo *OutInfo),
                      Task, Token, OutInfo)
  NEVERC_FORWARD_PREP(GetTokenInfoBatch,
                      (void *Context, NevercTaskHandle Task,
                       const NevercTokenHandle *Tokens, uint64_t TokenCount,
                       NevercTokenInfo *OutInfos, uint64_t OutInfoCapacity),
                      Task, Tokens, TokenCount, OutInfos, OutInfoCapacity)
  NEVERC_FORWARD_PREP(GetTokenStreamView,
                      (void *Context, NevercTaskHandle Task,
                       NevercTokenStreamHandle Stream,
                       NevercTokenViewList *OutView),
                      Task, Stream, OutView)
  NEVERC_FORWARD_PREP(GetTokenStreamToken,
                      (void *Context, NevercTaskHandle Task,
                       NevercTokenStreamHandle Stream, uint64_t Index,
                       NevercTokenHandle *OutToken),
                      Task, Stream, Index, OutToken)
  NEVERC_FORWARD_PREP(GetIdentifierInfo,
                      (void *Context, NevercTaskHandle Task,
                       NevercIdentifierHandle Identifier,
                       NevercIdentifierInfo *OutInfo),
                      Task, Identifier, OutInfo)
  NEVERC_FORWARD_PREP(GetOrCreateIdentifier,
                      (void *Context, NevercTaskHandle Task,
                       NevercStringView Name,
                       NevercIdentifierHandle *OutIdentifier),
                      Task, Name, OutIdentifier)
  NEVERC_FORWARD_PREP(GetMacroDefinitionForIdentifier,
                      (void *Context, NevercTaskHandle Task,
                       NevercIdentifierHandle Identifier,
                       NevercMacroDefinitionHandle *OutDefinition),
                      Task, Identifier, OutDefinition)
  NEVERC_FORWARD_PREP(GetMacroDefinitionInfo,
                      (void *Context, NevercTaskHandle Task,
                       NevercMacroDefinitionHandle Definition,
                       NevercMacroDefinitionInfo *OutInfo),
                      Task, Definition, OutInfo)
  NEVERC_FORWARD_PREP(GetMacroParameter,
                      (void *Context, NevercTaskHandle Task,
                       NevercMacroDefinitionHandle Definition, uint32_t Index,
                       NevercIdentifierHandle *OutParameter),
                      Task, Definition, Index, OutParameter)
  NEVERC_FORWARD_PREP(GetMacroReplacementToken,
                      (void *Context, NevercTaskHandle Task,
                       NevercMacroDefinitionHandle Definition, uint32_t Index,
                       NevercTokenHandle *OutToken),
                      Task, Definition, Index, OutToken)
  NEVERC_FORWARD_PREP(GetMacroDirectiveInfo,
                      (void *Context, NevercTaskHandle Task,
                       NevercMacroDirectiveHandle Directive,
                       NevercMacroDirectiveInfo *OutInfo),
                      Task, Directive, OutInfo)
  NEVERC_FORWARD_PREP(GetMacroArgumentInfo,
                      (void *Context, NevercTaskHandle Task,
                       NevercMacroArgumentHandle Arguments,
                       NevercMacroArgumentInfo *OutInfo),
                      Task, Arguments, OutInfo)
  NEVERC_FORWARD_PREP(GetMacroArgumentTokenStream,
                      (void *Context, NevercTaskHandle Task,
                       NevercMacroArgumentHandle Arguments, uint32_t Index,
                       NevercTokenStreamHandle *OutStream),
                      Task, Arguments, Index, OutStream)
  NEVERC_FORWARD_PREP(CreateTokenBuilder,
                      (void *Context, NevercTaskHandle Task,
                       NevercTokenBuilderHandle *OutBuilder),
                      Task, OutBuilder)
  NEVERC_FORWARD_PREP(TokenBuilderSetKind,
                      (void *Context, NevercTaskHandle Task,
                       NevercTokenBuilderHandle Builder, NevercTokenKind Kind),
                      Task, Builder, Kind)
  NEVERC_FORWARD_PREP(TokenBuilderSetIdentifier,
                      (void *Context, NevercTaskHandle Task,
                       NevercTokenBuilderHandle Builder,
                       NevercIdentifierHandle Identifier),
                      Task, Builder, Identifier)
  NEVERC_FORWARD_PREP(TokenBuilderSetLiteral,
                      (void *Context, NevercTaskHandle Task,
                       NevercTokenBuilderHandle Builder, NevercTokenKind Kind,
                       NevercStringView Spelling),
                      Task, Builder, Kind, Spelling)
  NEVERC_FORWARD_PREP(TokenBuilderSetLocation,
                      (void *Context, NevercTaskHandle Task,
                       NevercTokenBuilderHandle Builder,
                       NevercSourceLocation Location),
                      Task, Builder, Location)
  NEVERC_FORWARD_PREP(TokenBuilderSetFlags,
                      (void *Context, NevercTaskHandle Task,
                       NevercTokenBuilderHandle Builder,
                       NevercTokenFlags Flags),
                      Task, Builder, Flags)
  NEVERC_FORWARD_PREP(TokenBuilderCommit,
                      (void *Context, NevercTaskHandle Task,
                       NevercTokenBuilderHandle Builder,
                       NevercTokenHandle *OutToken),
                      Task, Builder, OutToken)
  NEVERC_FORWARD_PREP(DestroyTokenBuilder,
                      (void *Context, NevercTaskHandle Task,
                       NevercTokenBuilderHandle Builder),
                      Task, Builder)
  NEVERC_FORWARD_PREP(CreateTokenStreamBuilder,
                      (void *Context, NevercTaskHandle Task,
                       NevercTokenStreamBuilderHandle *OutBuilder),
                      Task, OutBuilder)
  NEVERC_FORWARD_PREP(TokenStreamBuilderAppend,
                      (void *Context, NevercTaskHandle Task,
                       NevercTokenStreamBuilderHandle Builder,
                       const NevercTokenHandle *Tokens, uint64_t TokenCount),
                      Task, Builder, Tokens, TokenCount)
  NEVERC_FORWARD_PREP(DestroyTokenStreamBuilder,
                      (void *Context, NevercTaskHandle Task,
                       NevercTokenStreamBuilderHandle Builder),
                      Task, Builder)

#undef NEVERC_FORWARD_PREP

  static NevercStatus NEVERC_CALL
  RegisterEventObserver(void *Context, NevercTaskHandle Task,
                        const NevercPrepObserverDescriptor *Descriptor) {
    if (!Context)
      return status(NEVERC_STATUS_INVALID_ARGUMENT);
    return bridge(Context)->registerObserver(Task, Descriptor);
  }

  static NevercStatus NEVERC_CALL
  GetTokenPhaseInput(void *Context, const NevercPhaseFrame *Frame,
                     NevercArtifactHandle Input, NevercTokenHandle *OutToken) {
    if (!Context || !Frame)
      return status(NEVERC_STATUS_INVALID_ARGUMENT);
    PluginPrepBridge *Bound = bridge(Context)->find(Frame->Task);
    if (!Bound)
      return status(NEVERC_STATUS_STALE_HANDLE);
    const NevercPrepAPI &Local = Bound->prepAPI();
    return Local.GetTokenPhaseInput(Local.Context, Frame, Input, OutToken);
  }

  static NevercStatus NEVERC_CALL
  CreateTokenPhaseOutput(void *Context, const NevercPhaseFrame *Frame,
                         const NevercPhaseContinuation *Continuation,
                         const NevercTokenHandle *Tokens, uint64_t TokenCount,
                         NevercArtifactHandle *OutOutput) {
    if (!Context || !Frame)
      return status(NEVERC_STATUS_INVALID_ARGUMENT);
    PluginPrepBridge *Bound = bridge(Context)->find(Frame->Task);
    if (!Bound)
      return status(NEVERC_STATUS_STALE_HANDLE);
    const NevercPrepAPI &Local = Bound->prepAPI();
    return Local.CreateTokenPhaseOutput(Local.Context, Frame, Continuation,
                                        Tokens, TokenCount, OutOutput);
  }

  static NevercStatus NEVERC_CALL GetTokenStreamPhaseInput(
      void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
      NevercPrepTokenStreamPhaseInput *OutInput) {
    if (!Context || !Frame)
      return status(NEVERC_STATUS_INVALID_ARGUMENT);
    PluginPrepBridge *Bound = bridge(Context)->find(Frame->Task);
    if (!Bound)
      return status(NEVERC_STATUS_STALE_HANDLE);
    const NevercPrepAPI &Local = Bound->prepAPI();
    return Local.GetTokenStreamPhaseInput(Local.Context, Frame, Input,
                                          OutInput);
  }

  static NevercStatus NEVERC_CALL TokenStreamBuilderCommit(
      void *Context, const NevercPhaseFrame *Frame,
      NevercTokenStreamBuilderHandle Builder, NevercArtifactHandle *OutOutput) {
    if (!Context || !Frame)
      return status(NEVERC_STATUS_INVALID_ARGUMENT);
    PluginPrepBridge *Bound = bridge(Context)->find(Frame->Task);
    if (!Bound)
      return status(NEVERC_STATUS_STALE_HANDLE);
    const NevercPrepAPI &Local = Bound->prepAPI();
    return Local.TokenStreamBuilderCommit(Local.Context, Frame, Builder,
                                          OutOutput);
  }

#define NEVERC_FORWARD_PREP_PHASE_INPUT(Name, InputType)                       \
  static NevercStatus NEVERC_CALL Name(                                        \
      void *Context, const NevercPhaseFrame *Frame,                            \
      NevercArtifactHandle Input, InputType *OutInput) {                       \
    if (!Context || !Frame)                                                    \
      return status(NEVERC_STATUS_INVALID_ARGUMENT);                           \
    PluginPrepBridge *Bound = bridge(Context)->find(Frame->Task);              \
    if (!Bound)                                                                \
      return status(NEVERC_STATUS_STALE_HANDLE);                               \
    const NevercPrepAPI &Local = Bound->prepAPI();                             \
    return Local.Name(Local.Context, Frame, Input, OutInput);                  \
  }

#define NEVERC_FORWARD_PREP_PHASE_OUTPUT(Name, OutputType)                     \
  static NevercStatus NEVERC_CALL Name(                                        \
      void *Context, const NevercPhaseFrame *Frame,                            \
      const NevercPhaseContinuation *Continuation, const OutputType *Output,   \
      NevercArtifactHandle *OutOutput) {                                       \
    if (!Context || !Frame)                                                    \
      return status(NEVERC_STATUS_INVALID_ARGUMENT);                           \
    PluginPrepBridge *Bound = bridge(Context)->find(Frame->Task);              \
    if (!Bound)                                                                \
      return status(NEVERC_STATUS_STALE_HANDLE);                               \
    const NevercPrepAPI &Local = Bound->prepAPI();                             \
    return Local.Name(Local.Context, Frame, Continuation, Output, OutOutput);  \
  }

  NEVERC_FORWARD_PREP_PHASE_INPUT(GetIncludePhaseInput,
                                  NevercPrepIncludePhaseInput)
  NEVERC_FORWARD_PREP_PHASE_OUTPUT(CreateIncludePhaseOutput,
                                   NevercPrepIncludePhaseOutput)
  NEVERC_FORWARD_PREP_PHASE_INPUT(GetMacroPhaseInput, NevercPrepMacroPhaseInput)
  NEVERC_FORWARD_PREP_PHASE_OUTPUT(CreateMacroPhaseOutput,
                                   NevercPrepMacroPhaseOutput)
  NEVERC_FORWARD_PREP_PHASE_INPUT(GetPragmaPhaseInput,
                                  NevercPrepPragmaPhaseInput)
  NEVERC_FORWARD_PREP_PHASE_OUTPUT(CreatePragmaPhaseOutput,
                                   NevercPrepPragmaPhaseOutput)
  NEVERC_FORWARD_PREP_PHASE_INPUT(GetFeatureQueryPhaseInput,
                                  NevercPrepFeatureQueryPhaseInput)
  NEVERC_FORWARD_PREP_PHASE_OUTPUT(CreateFeatureQueryPhaseOutput,
                                   NevercPrepFeatureQueryPhaseOutput)

#undef NEVERC_FORWARD_PREP_PHASE_OUTPUT
#undef NEVERC_FORWARD_PREP_PHASE_INPUT

  PluginProcessServices &Services;
  NevercPrepAPI API{};
  std::mutex Mutex;
  std::map<std::pair<uint64_t, uint64_t>, TaskBinding> Tasks;
};

namespace {

std::shared_ptr<PluginPrepProcessBridge>
findPrepProcessBridge(PluginProcessServices &Services) {
  return std::static_pointer_cast<PluginPrepProcessBridge>(
      Services.findHostService(prepPluginInterfaceID()));
}

} // namespace

PluginPrepBridge::PluginPrepBridge(PluginTaskContext &TaskValue,
                                   PrepEngine &PrepValue,
                                   FrontendPluginBridge &LocationsValue)
    : Task(TaskValue), Prep(PrepValue), Locations(LocationsValue) {
  PrepAPI.Header = {sizeof(PrepAPI), NEVERC_PREP_API_MAJOR,
                    NEVERC_PREP_API_MINOR, 0};
  PrepAPI.Context = this;
  PrepAPI.GetTokenInfo = getTokenInfo;
  PrepAPI.GetTokenInfoBatch = getTokenInfoBatch;
  PrepAPI.GetTokenStreamView = getTokenStreamView;
  PrepAPI.GetTokenStreamToken = getTokenStreamToken;
  PrepAPI.GetIdentifierInfo = getIdentifierInfo;
  PrepAPI.GetOrCreateIdentifier = getOrCreateIdentifier;
  PrepAPI.GetMacroDefinitionForIdentifier = getMacroDefinitionForIdentifier;
  PrepAPI.GetMacroDefinitionInfo = getMacroDefinitionInfo;
  PrepAPI.GetMacroParameter = getMacroParameter;
  PrepAPI.GetMacroReplacementToken = getMacroReplacementToken;
  PrepAPI.GetMacroDirectiveInfo = getMacroDirectiveInfo;
  PrepAPI.GetMacroArgumentInfo = getMacroArgumentInfo;
  PrepAPI.GetMacroArgumentTokenStream = getMacroArgumentTokenStream;
  PrepAPI.CreateTokenBuilder = createTokenBuilder;
  PrepAPI.TokenBuilderSetKind = tokenBuilderSetKind;
  PrepAPI.TokenBuilderSetIdentifier = tokenBuilderSetIdentifier;
  PrepAPI.TokenBuilderSetLiteral = tokenBuilderSetLiteral;
  PrepAPI.TokenBuilderSetLocation = tokenBuilderSetLocation;
  PrepAPI.TokenBuilderSetFlags = tokenBuilderSetFlags;
  PrepAPI.TokenBuilderCommit = tokenBuilderCommit;
  PrepAPI.DestroyTokenBuilder = destroyTokenBuilder;
  PrepAPI.RegisterEventObserver = registerEventObserver;
  PrepAPI.GetTokenPhaseInput = getTokenPhaseInput;
  PrepAPI.CreateTokenPhaseOutput = createTokenPhaseOutput;
  PrepAPI.GetIncludePhaseInput = getIncludePhaseInput;
  PrepAPI.CreateIncludePhaseOutput = createIncludePhaseOutput;
  PrepAPI.GetMacroPhaseInput = getMacroPhaseInput;
  PrepAPI.CreateMacroPhaseOutput = createMacroPhaseOutput;
  PrepAPI.GetPragmaPhaseInput = getPragmaPhaseInput;
  PrepAPI.CreatePragmaPhaseOutput = createPragmaPhaseOutput;
  PrepAPI.GetFeatureQueryPhaseInput = getFeatureQueryPhaseInput;
  PrepAPI.CreateFeatureQueryPhaseOutput = createFeatureQueryPhaseOutput;
  PrepAPI.GetTokenStreamPhaseInput = getTokenStreamPhaseInput;
  PrepAPI.CreateTokenStreamBuilder = createTokenStreamBuilder;
  PrepAPI.TokenStreamBuilderAppend = tokenStreamBuilderAppend;
  PrepAPI.TokenStreamBuilderCommit = tokenStreamBuilderCommit;
  PrepAPI.DestroyTokenStreamBuilder = destroyTokenStreamBuilder;
}

PluginPrepBridge::~PluginPrepBridge() { detachProcessInterface(); }

Error PluginPrepBridge::attachProcessInterface() {
  if (AttachedToProcess)
    return Error::success();
  auto ProcessBridge = findPrepProcessBridge(Task.processServices());
  if (!ProcessBridge)
    return createStringError(inconvertibleErrorCode(),
                             "plugin prep interface is not registered");
  if (Error E = ProcessBridge->attach(Task, *this))
    return E;
  AttachedToProcess = true;
  return Error::success();
}

Error PluginPrepBridge::ensureObserverAttached() {
  if (ObserverAttached)
    return Error::success();
  auto Observer = std::unique_ptr<PluginPrepObserver>(
      new (std::nothrow) PluginPrepObserver(*this));
  if (!Observer)
    return createStringError(inconvertibleErrorCode(),
                             "unable to allocate plugin prep observer");
  Prep.addObserver(std::move(Observer));
  ObserverAttached = true;
  return Error::success();
}

NevercStatus PluginPrepBridge::dispatchEvent(const NevercPrepEvent &Event) {
  auto ProcessBridge = findPrepProcessBridge(Task.processServices());
  if (!ProcessBridge)
    return status(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
  return ProcessBridge->dispatch(Task, Event);
}

void PluginPrepBridge::detachProcessInterface() {
  if (!AttachedToProcess)
    return;
  if (auto ProcessBridge = findPrepProcessBridge(Task.processServices()))
    ProcessBridge->detach(Task.handle());
  AttachedToProcess = false;
}

NevercStatus NEVERC_CALL PluginPrepBridge::registerEventObserver(
    void *Context, NevercTaskHandle TaskHandle,
    const NevercPrepObserverDescriptor *Descriptor) {
  if (!Context)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<PluginPrepBridge *>(Context);
  if (!sameHandle(TaskHandle, Bridge.Task.handle()))
    return status(NEVERC_STATUS_WRONG_SCOPE);
  auto ProcessBridge = findPrepProcessBridge(Bridge.Task.processServices());
  if (!ProcessBridge)
    return status(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
  return ProcessBridge->registerObserver(TaskHandle, Descriptor);
}

NevercStatus NEVERC_CALL
PluginPrepBridge::createTokenBuilder(void *Context, NevercTaskHandle TaskHandle,
                                     NevercTokenBuilderHandle *OutBuilder) {
  if (!Context || !OutBuilder)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutBuilder = {};
  auto &Bridge = *static_cast<PluginPrepBridge *>(Context);
  if (!sameHandle(TaskHandle, Bridge.Task.handle()))
    return status(NEVERC_STATUS_WRONG_SCOPE);
  if (Bridge.Task.isEnded())
    return status(NEVERC_STATUS_INVALID_STATE);
  auto *Payload = new (std::nothrow) TokenBuilderPayload();
  if (!Payload)
    return status(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  Payload->Engine = &Bridge.Prep;
  auto Handle = Bridge.Task.handles().create(
      PluginTokenBuilderHandleKind, Payload,
      [](void *Value) { delete static_cast<TokenBuilderPayload *>(Value); });
  if (!Handle) {
    delete Payload;
    consumeError(Handle.takeError());
    return status(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutBuilder = *Handle;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL PluginPrepBridge::tokenBuilderSetKind(
    void *Context, NevercTaskHandle TaskHandle,
    NevercTokenBuilderHandle Builder, NevercTokenKind Kind) {
  if (!Context)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<PluginPrepBridge *>(Context);
  TokenBuilderPayload *Payload = nullptr;
  NevercStatus Status =
      resolveBuilderPayload(Bridge.Task, Bridge.Prep, TaskHandle, Builder,
                            PluginTokenBuilderHandleKind, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Payload->Committed)
    return status(NEVERC_STATUS_INVALID_STATE);
  tok::TokenKind NativeKind;
  if (!nativeTokenKind(Kind, &NativeKind) || !tokenKindConstructible(Kind) ||
      (tokenCategory(Kind) != NEVERC_TOKEN_CATEGORY_PUNCTUATOR &&
       Kind != NEVERC_TOKEN_EOF))
    return status(NEVERC_STATUS_VERIFICATION_FAILED);
  Payload->Kind = Kind;
  Payload->Identifier = nullptr;
  Payload->Spelling.clear();
  Payload->HasKind = true;
  Payload->HasIdentifier = false;
  Payload->HasLiteral = false;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL PluginPrepBridge::tokenBuilderSetIdentifier(
    void *Context, NevercTaskHandle TaskHandle,
    NevercTokenBuilderHandle Builder, NevercIdentifierHandle IdentifierHandle) {
  if (!Context)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<PluginPrepBridge *>(Context);
  TokenBuilderPayload *Payload = nullptr;
  NevercStatus Status =
      resolveBuilderPayload(Bridge.Task, Bridge.Prep, TaskHandle, Builder,
                            PluginTokenBuilderHandleKind, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Payload->Committed)
    return status(NEVERC_STATUS_INVALID_STATE);
  IdentifierInfo *Identifier = nullptr;
  Status = Bridge.resolveIdentifier(TaskHandle, IdentifierHandle, &Identifier);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Payload->Kind = NEVERC_TOKEN_IDENTIFIER;
  Payload->Identifier = Identifier;
  Payload->Spelling.clear();
  Payload->HasKind = true;
  Payload->HasIdentifier = true;
  Payload->HasLiteral = false;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL PluginPrepBridge::tokenBuilderSetLiteral(
    void *Context, NevercTaskHandle TaskHandle,
    NevercTokenBuilderHandle Builder, NevercTokenKind Kind,
    NevercStringView Spelling) {
  if (!Context)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<PluginPrepBridge *>(Context);
  TokenBuilderPayload *Payload = nullptr;
  NevercStatus Status =
      resolveBuilderPayload(Bridge.Task, Bridge.Prep, TaskHandle, Builder,
                            PluginTokenBuilderHandleKind, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Payload->Committed)
    return status(NEVERC_STATUS_INVALID_STATE);
  if ((!Spelling.Data && Spelling.Length != 0) || Spelling.Length == 0 ||
      Spelling.Length > MaximumBuilderSpellingBytes ||
      Spelling.Length > std::numeric_limits<size_t>::max())
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  tok::TokenKind NativeKind;
  if (!nativeTokenKind(Kind, &NativeKind) || !tok::isLiteral(NativeKind) ||
      !tokenKindConstructible(Kind))
    return status(NEVERC_STATUS_VERIFICATION_FAILED);
  StringRef NativeSpelling(Spelling.Data, static_cast<size_t>(Spelling.Length));
  if (NativeSpelling.contains('\0'))
    return status(NEVERC_STATUS_VERIFICATION_FAILED);
  Payload->Kind = Kind;
  Payload->Identifier = nullptr;
  Payload->Spelling = NativeSpelling.str();
  Payload->HasKind = true;
  Payload->HasIdentifier = false;
  Payload->HasLiteral = true;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL PluginPrepBridge::tokenBuilderSetLocation(
    void *Context, NevercTaskHandle TaskHandle,
    NevercTokenBuilderHandle Builder, NevercSourceLocation Location) {
  if (!Context)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<PluginPrepBridge *>(Context);
  TokenBuilderPayload *Payload = nullptr;
  NevercStatus Status =
      resolveBuilderPayload(Bridge.Task, Bridge.Prep, TaskHandle, Builder,
                            PluginTokenBuilderHandleKind, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Payload->Committed)
    return status(NEVERC_STATUS_INVALID_STATE);
  SourceLocation NativeLocation;
  Status =
      Bridge.Locations.resolveLocation(TaskHandle, Location, &NativeLocation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (NativeLocation.isInvalid())
    return status(NEVERC_STATUS_VERIFICATION_FAILED);
  Payload->Location = NativeLocation;
  Payload->HasLocation = true;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL PluginPrepBridge::tokenBuilderSetFlags(
    void *Context, NevercTaskHandle TaskHandle,
    NevercTokenBuilderHandle Builder, NevercTokenFlags Flags) {
  if (!Context)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<PluginPrepBridge *>(Context);
  TokenBuilderPayload *Payload = nullptr;
  NevercStatus Status =
      resolveBuilderPayload(Bridge.Task, Bridge.Prep, TaskHandle, Builder,
                            PluginTokenBuilderHandleKind, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Payload->Committed)
    return status(NEVERC_STATUS_INVALID_STATE);
  if ((Flags & ~BuilderTokenFlags) != 0)
    return status(NEVERC_STATUS_VERIFICATION_FAILED);
  Payload->Flags = Flags;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL PluginPrepBridge::tokenBuilderCommit(
    void *Context, NevercTaskHandle TaskHandle,
    NevercTokenBuilderHandle Builder, NevercTokenHandle *OutToken) {
  if (!Context || !OutToken)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutToken = {};
  auto &Bridge = *static_cast<PluginPrepBridge *>(Context);
  TokenBuilderPayload *Payload = nullptr;
  NevercStatus Status =
      resolveBuilderPayload(Bridge.Task, Bridge.Prep, TaskHandle, Builder,
                            PluginTokenBuilderHandleKind, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Payload->Committed)
    return status(NEVERC_STATUS_INVALID_STATE);
  if (!Payload->HasKind || !Payload->HasLocation)
    return status(NEVERC_STATUS_VERIFICATION_FAILED);

  tok::TokenKind NativeKind;
  if (!nativeTokenKind(Payload->Kind, &NativeKind) ||
      !tokenKindConstructible(Payload->Kind))
    return status(NEVERC_STATUS_VERIFICATION_FAILED);
  if ((Payload->Flags & NEVERC_TOKEN_FLAG_DISABLE_EXPANSION) != 0 &&
      Payload->Kind != NEVERC_TOKEN_IDENTIFIER)
    return status(NEVERC_STATUS_VERIFICATION_FAILED);
  if (Payload->Kind == NEVERC_TOKEN_EOF &&
      (Payload->Flags & ~(NEVERC_TOKEN_FLAG_START_OF_LINE |
                          NEVERC_TOKEN_FLAG_LEADING_SPACE)) != 0)
    return status(NEVERC_STATUS_VERIFICATION_FAILED);

  StringRef NativeSpelling;
  if (Payload->Kind == NEVERC_TOKEN_IDENTIFIER) {
    if (!Payload->HasIdentifier || !Payload->Identifier)
      return status(NEVERC_STATUS_VERIFICATION_FAILED);
    NativeSpelling = Payload->Identifier->getName();
  } else if (tok::isLiteral(NativeKind)) {
    if (!Payload->HasLiteral)
      return status(NEVERC_STATUS_VERIFICATION_FAILED);
    NativeSpelling = Payload->Spelling;
  } else if (Payload->Kind != NEVERC_TOKEN_EOF) {
    if (tokenCategory(Payload->Kind) != NEVERC_TOKEN_CATEGORY_PUNCTUATOR)
      return status(NEVERC_STATUS_VERIFICATION_FAILED);
    const char *Punctuation = tok::getPunctuatorSpelling(NativeKind);
    if (!Punctuation)
      return status(NEVERC_STATUS_VERIFICATION_FAILED);
    NativeSpelling = Punctuation;
  }

  Token Value;
  Value.startToken();
  Value.setKind(NativeKind);
  Status = setNativeFlags(Value, Payload->Flags);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Payload->Kind == NEVERC_TOKEN_EOF) {
    Value.setLocation(Payload->Location);
    Value.setLength(0);
  } else {
    Bridge.Prep.WriteScratch(NativeSpelling, Value, Payload->Location,
                             Payload->Location);
    if (Payload->Kind == NEVERC_TOKEN_IDENTIFIER)
      Value.setIdentifierInfo(Payload->Identifier);
    if (tok::isLiteral(NativeKind) &&
        !validLiteralSpelling(Bridge.Prep, Value, NativeSpelling))
      return status(NEVERC_STATUS_VERIFICATION_FAILED);
  }

  auto PublicToken =
      Bridge.createTokenWithOrigin(Value, NEVERC_TOKEN_ORIGIN_SYNTHESIZED);
  if (!PublicToken) {
    consumeError(PublicToken.takeError());
    return status(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  Payload->Committed = true;
  *OutToken = *PublicToken;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL PluginPrepBridge::destroyTokenBuilder(
    void *Context, NevercTaskHandle TaskHandle,
    NevercTokenBuilderHandle Builder) {
  if (!Context)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<PluginPrepBridge *>(Context);
  if (!sameHandle(TaskHandle, Bridge.Task.handle()))
    return status(NEVERC_STATUS_WRONG_SCOPE);
  if (Bridge.Task.isEnded())
    return status(NEVERC_STATUS_INVALID_STATE);
  return Bridge.Task.handles().release(Builder, PluginTokenBuilderHandleKind);
}

Error registerPluginPrepInterface(PluginProcessServices &Services) {
  if (Services.interfaces().isFrozen())
    return createStringError(
        inconvertibleErrorCode(),
        "cannot register plugin prep interface after interface freeze");
  auto Bridge = std::make_shared<PluginPrepProcessBridge>(Services);
  if (Error E = Services.registerHostService(prepPluginInterfaceID(), Bridge))
    return E;
  return Services.interfaces().registerInterface(
      prepPluginInterfaceID(), NEVERC_INTERFACE_STABLE, &Bridge->api(), {});
}

} // namespace neverc::plugin
