#include "neverc/Scan/PrepPluginHooks.h"
#include "FrontendPluginInterfaces.h"
#include "PrepBridgeInternal.h"
#include "neverc/Foundation/Diagnostic/DiagnosticDriver.h"
#include "neverc/Plugin/Host/PluginArtifactRegistry.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginPhaseExecutor.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "neverc/Scan/PrepEngine.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/Support/Error.h"
#include <limits>
#include <new>
#include <utility>

using namespace llvm;

namespace neverc::plugin {
using namespace prep_bridge_detail;

namespace {

constexpr uint64_t MaximumTokenPhaseOutput = 256;

NevercPhaseRoute defaultRoute() {
  NevercPhaseRoute Route{};
  Route.Header = {sizeof(Route), NEVERC_PLUGIN_ABI_MAJOR,
                  NEVERC_PLUGIN_ABI_MINOR, 0};
  return Route;
}

Expected<void *> cloneTokenPhaseArtifact(const void *Payload) {
  if (!Payload)
    return createStringError(inconvertibleErrorCode(),
                             "token phase artifact is null");
  return static_cast<void *>(new TokenPhaseArtifact(
      *static_cast<const TokenPhaseArtifact *>(Payload)));
}

Error verifyTokenPhaseArtifact(const void *Payload) {
  if (!Payload)
    return createStringError(inconvertibleErrorCode(),
                             "token phase artifact is null");
  const auto &Artifact = *static_cast<const TokenPhaseArtifact *>(Payload);
  if (Artifact.Tokens.size() > MaximumTokenPhaseOutput)
    return createStringError(inconvertibleErrorCode(),
                             "token phase output exceeds its token budget");
  for (const Token &Value : Artifact.Tokens) {
    if (Value.is(tok::unknown) || Value.is(tok::eod))
      return createStringError(
          inconvertibleErrorCode(),
          "token phase output contains an internal-only token");
  }
  return Error::success();
}

bool validFrame(const PluginTaskContext &Task, const NevercPhaseFrame *Frame) {
  return Frame && Frame->Header.StructSize >= sizeof(*Frame) &&
         Frame->Header.Major == NEVERC_PLUGIN_ABI_MAJOR &&
         Frame->Header.Minor <= NEVERC_PLUGIN_ABI_MINOR &&
         Frame->Header.Flags == 0 && sameHandle(Frame->Task, Task.handle()) &&
         sameHandle(Frame->Session, Task.session().handle()) &&
         samePluginInterfaceID(Frame->Phase, prepTokenPhaseID());
}

class PluginPrepHooks final : public PrepPluginHooks {
public:
  PluginPrepHooks(PluginTaskContext &TaskValue, PrepEngine &PrepValue,
                  PluginArtifactRegistry &ArtifactsValue,
                  PluginPhaseExecutor &ExecutorValue)
      : Task(TaskValue), Prep(PrepValue), Artifacts(ArtifactsValue),
        Executor(ExecutorValue) {}

  bool interceptToken(const Token &Input, SmallVectorImpl<Token> &Output,
                      bool &Replaced) override {
    Output.clear();
    Replaced = false;
    if (Failed)
      return false;
    NevercStatus Cancellation = Task.checkCancelled();
    if (Cancellation.Code != NEVERC_STATUS_OK) {
      fail(Input.getLocation(), "token phase was cancelled");
      return false;
    }
    if (Active) {
      fail(Input.getLocation(),
           "recursive token phase execution is not allowed");
      return false;
    }
    Active = true;
    auto ClearActive = make_scope_exit([&] { Active = false; });

    TokenPhaseArtifact InputArtifact;
    InputArtifact.Tokens.push_back(Input);
    auto InputView = Executor.createArtifactView(
        Task, prepTokenArtifactID(), &InputArtifact, NextGeneration++);
    if (!InputView) {
      fail(Input.getLocation(), InputView.takeError());
      return false;
    }
    auto ReleaseInput = make_scope_exit([&] {
      (void)Task.handles().release(*InputView, PluginArtifactHandleKind);
    });

    auto OutputType = Artifacts.find(prepTokenArtifactID());
    if (!OutputType) {
      fail(Input.getLocation(), "token phase artifact type is not registered");
      return false;
    }
    PluginArtifactSlot OutputSlot(std::move(OutputType));
    NevercPhaseRoute Route = defaultRoute();
    if (Error E = Executor.execute(Task.session(), Task, prepTokenPhaseID(),
                                   Route, *InputView, OutputSlot)) {
      fail(Input.getLocation(), std::move(E));
      return false;
    }
    PluginArtifactSlot::Snapshot Snapshot = OutputSlot.snapshot();
    if (!Snapshot.Payload) {
      fail(Input.getLocation(), "token phase produced no output");
      return false;
    }
    const auto &Artifact =
        *static_cast<const TokenPhaseArtifact *>(Snapshot.Payload);
    Output.append(Artifact.Tokens.begin(), Artifact.Tokens.end());
    Replaced = Artifact.Replacement;
    return true;
  }

  bool hasIncludeInterceptor() const override {
    return Executor.hasBindings(prepIncludePhaseID());
  }

  bool hasMacroInterceptor() const override {
    return Executor.hasBindings(prepMacroPhaseID());
  }

  bool hasPragmaInterceptor() const override {
    return Executor.hasBindings(prepPragmaPhaseID());
  }

  bool hasFeatureQueryInterceptor() const override {
    return Executor.hasBindings(prepFeatureQueryPhaseID());
  }

  bool interceptInclude(PrepIncludeHook &Hook) override {
    IncludePhaseArtifact Artifact;
    Artifact.Location = Hook.Location;
    Artifact.IncludeToken = Hook.IncludeToken;
    Artifact.Filename = Hook.Filename.str();
    Artifact.SearchPath = Hook.SearchPath.str();
    Artifact.RelativePath = Hook.RelativePath.str();
    Artifact.IsAngled = Hook.IsAngled;
    Artifact.IsImport = Hook.IsImport;
    Artifact.IsIncludeNext = Hook.IsIncludeNext;
    if (!executeHook(Hook.Location, prepIncludePhaseID(),
                     prepIncludeArtifactID(), Artifact, "include"))
      return false;
    switch (Artifact.Action) {
    case NEVERC_PREP_INCLUDE_CONTINUE:
      Hook.Result = PrepIncludeHook::Action::Continue;
      break;
    case NEVERC_PREP_INCLUDE_SKIP:
      Hook.Result = PrepIncludeHook::Action::Skip;
      break;
    case NEVERC_PREP_INCLUDE_REDIRECT:
      Hook.Result = PrepIncludeHook::Action::Redirect;
      break;
    default:
      fail(Hook.Location, "include phase returned an invalid action");
      return false;
    }
    Hook.ReplacementFilename = std::move(Artifact.ReplacementFilename);
    Hook.ReplacementIsAngled = Artifact.ReplacementIsAngled;
    return true;
  }

  bool interceptMacro(PrepMacroHook &Hook) override {
    MacroPhaseArtifact Artifact;
    switch (Hook.Kind) {
    case PrepMacroHook::Operation::Define:
      Artifact.Operation = NEVERC_PREP_MACRO_DEFINE;
      break;
    case PrepMacroHook::Operation::Undefine:
      Artifact.Operation = NEVERC_PREP_MACRO_UNDEFINE;
      break;
    case PrepMacroHook::Operation::Expand:
      Artifact.Operation = NEVERC_PREP_MACRO_EXPAND;
      break;
    case PrepMacroHook::Operation::ExpandBuiltin:
      Artifact.Operation = NEVERC_PREP_MACRO_EXPAND_BUILTIN;
      break;
    }
    Artifact.NameToken = Hook.NameToken;
    Artifact.Name = Hook.Name;
    Artifact.Definition = Hook.Definition;
    Artifact.Arguments = Hook.Arguments;
    if (!executeHook(Hook.NameToken.getLocation(), prepMacroPhaseID(),
                     prepMacroArtifactID(), Artifact, "macro"))
      return false;
    switch (Artifact.Action) {
    case NEVERC_PREP_MACRO_CONTINUE:
      Hook.Result = PrepMacroHook::Action::Continue;
      break;
    case NEVERC_PREP_MACRO_SUPPRESS:
      Hook.Result = PrepMacroHook::Action::Suppress;
      break;
    case NEVERC_PREP_MACRO_REPLACE_TOKENS:
      Hook.Result = PrepMacroHook::Action::ReplaceTokens;
      break;
    default:
      fail(Hook.NameToken.getLocation(),
           "macro phase returned an invalid action");
      return false;
    }
    Hook.ReplacementTokens.assign(Artifact.ReplacementTokens.begin(),
                                  Artifact.ReplacementTokens.end());
    return true;
  }

  bool interceptPragma(PrepPragmaHook &Hook) override {
    PragmaPhaseArtifact Artifact;
    Artifact.Location = Hook.Location;
    Artifact.Introducer =
        Hook.Introducer == PIK_HashPragma ? NEVERC_PREP_PRAGMA_HASH
        : Hook.Introducer == PIK__Pragma  ? NEVERC_PREP_PRAGMA_OPERATOR
                                          : NEVERC_PREP_PRAGMA_MS;
    Artifact.Namespace = Hook.Namespace.str();
    Artifact.Name = Hook.Name.str();
    Artifact.Tokens.assign(Hook.Tokens.begin(), Hook.Tokens.end());
    if (!executeHook(Hook.Location, prepPragmaPhaseID(), prepPragmaArtifactID(),
                     Artifact, "pragma"))
      return false;
    switch (Artifact.Action) {
    case NEVERC_PREP_PRAGMA_CONTINUE:
      Hook.Result = PrepPragmaHook::Action::Continue;
      break;
    case NEVERC_PREP_PRAGMA_HANDLED:
      Hook.Result = PrepPragmaHook::Action::Handled;
      break;
    case NEVERC_PREP_PRAGMA_REPLACE_TOKENS:
      Hook.Result = PrepPragmaHook::Action::ReplaceTokens;
      break;
    default:
      fail(Hook.Location, "pragma phase returned an invalid action");
      return false;
    }
    Hook.ReplacementTokens.assign(Artifact.ReplacementTokens.begin(),
                                  Artifact.ReplacementTokens.end());
    return true;
  }

  bool interceptFeatureQuery(PrepFeatureQueryHook &Hook) override {
    FeatureQueryPhaseArtifact Artifact;
    Artifact.Location = Hook.Location;
    switch (Hook.Query) {
    case PrepFeatureQueryHook::Kind::HasFeature:
      Artifact.Kind = NEVERC_PREP_QUERY_HAS_FEATURE;
      break;
    case PrepFeatureQueryHook::Kind::HasExtension:
      Artifact.Kind = NEVERC_PREP_QUERY_HAS_EXTENSION;
      break;
    case PrepFeatureQueryHook::Kind::HasBuiltin:
      Artifact.Kind = NEVERC_PREP_QUERY_HAS_BUILTIN;
      break;
    case PrepFeatureQueryHook::Kind::HasInclude:
      Artifact.Kind = NEVERC_PREP_QUERY_HAS_INCLUDE;
      break;
    case PrepFeatureQueryHook::Kind::HasIncludeNext:
      Artifact.Kind = NEVERC_PREP_QUERY_HAS_INCLUDE_NEXT;
      break;
    }
    Artifact.Name = Hook.Name.str();
    Artifact.BuiltinValue = Hook.BuiltinValue;
    Artifact.Value = Hook.BuiltinValue;
    if (!executeHook(Hook.Location, prepFeatureQueryPhaseID(),
                     prepFeatureQueryArtifactID(), Artifact, "feature query"))
      return false;
    Hook.Value = Artifact.Value;
    Hook.Replaced = Artifact.Action == NEVERC_PREP_QUERY_REPLACE;
    return true;
  }

private:
  template <typename T>
  bool executeHook(SourceLocation Location, NevercInterfaceID Phase,
                   NevercInterfaceID ArtifactType, T &Artifact,
                   StringRef HookName) {
    if (Failed)
      return false;
    NevercStatus Cancellation = Task.checkCancelled();
    if (Cancellation.Code != NEVERC_STATUS_OK) {
      fail(Location, (HookName + " phase was cancelled").str());
      return false;
    }
    if (Active) {
      fail(Location,
           ("recursive " + HookName + " phase execution is not allowed").str());
      return false;
    }
    Active = true;
    auto ClearActive = make_scope_exit([&] { Active = false; });
    auto InputView = Executor.createArtifactView(Task, ArtifactType, &Artifact,
                                                 NextGeneration++);
    if (!InputView) {
      fail(Location, InputView.takeError());
      return false;
    }
    auto ReleaseInput = make_scope_exit([&] {
      (void)Task.handles().release(*InputView, PluginArtifactHandleKind);
    });
    auto OutputType = Artifacts.find(ArtifactType);
    if (!OutputType) {
      fail(Location,
           (HookName + " phase artifact type is not registered").str());
      return false;
    }
    PluginArtifactSlot OutputSlot(std::move(OutputType));
    NevercPhaseRoute Route = defaultRoute();
    if (Error E = Executor.execute(Task.session(), Task, Phase, Route,
                                   *InputView, OutputSlot)) {
      fail(Location, std::move(E));
      return false;
    }
    PluginArtifactSlot::Snapshot Snapshot = OutputSlot.snapshot();
    if (!Snapshot.Payload) {
      fail(Location, (HookName + " phase produced no output").str());
      return false;
    }
    Artifact = *static_cast<const T *>(Snapshot.Payload);
    return true;
  }

  void fail(SourceLocation Location, StringRef Message) {
    if (Failed)
      return;
    Failed = true;
    Prep.getDiagnostics().Report(Location, diag::err_drv_plugin_phase)
        << Message;
  }

  void fail(SourceLocation Location, Error ErrorValue) {
    std::string Message = toString(std::move(ErrorValue)).str().str();
    fail(Location, Message);
  }

  PluginTaskContext &Task;
  PrepEngine &Prep;
  PluginArtifactRegistry &Artifacts;
  PluginPhaseExecutor &Executor;
  uint64_t NextGeneration = 1;
  bool Active = false;
  bool Failed = false;
};

} // namespace

NevercInterfaceID prepTokenPhaseID() {
  return {NEVERC_PHASE_PREP_TOKEN_HIGH, NEVERC_PHASE_PREP_TOKEN_LOW};
}

NevercInterfaceID prepTokenArtifactID() {
  return {NEVERC_PHASE_PREP_TOKEN_INPUT_HIGH,
          NEVERC_PHASE_PREP_TOKEN_INPUT_LOW};
}

Error registerPrepTokenArtifactType(PluginArtifactRegistry &Artifacts) {
  auto Type = Artifacts.registerType(
      {prepTokenArtifactID(), "prep.token", PluginArtifactOwnership::Owned,
       cloneTokenPhaseArtifact,
       [](void *Payload) { delete static_cast<TokenPhaseArtifact *>(Payload); },
       verifyTokenPhaseArtifact});
  if (!Type)
    return Type.takeError();
  return Error::success();
}

Expected<std::unique_ptr<PrepPluginHooks>>
createPrepPluginHooks(PluginTaskContext &Task, PrepEngine &Prep,
                      PluginArtifactRegistry &Artifacts,
                      PluginPhaseExecutor &Executor, PluginPrepBridge &) {
  return std::make_unique<PluginPrepHooks>(Task, Prep, Artifacts, Executor);
}

void PluginPrepBridge::attachTokenPhaseExecutor(PluginPhaseExecutor &Executor) {
  TokenPhaseExecutor = &Executor;
}

NevercStatus NEVERC_CALL PluginPrepBridge::getTokenPhaseInput(
    void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
    NevercTokenHandle *OutToken) {
  if (!Context || !OutToken)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutToken = {};
  auto &Bridge = *static_cast<PluginPrepBridge *>(Context);
  if (!validFrame(Bridge.Task, Frame))
    return status(NEVERC_STATUS_WRONG_SCOPE);
  if (!Bridge.TokenPhaseExecutor)
    return status(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
  if (!sameHandle(Frame->Input, Input))
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  const void *Payload = nullptr;
  NevercStatus Resolve = Bridge.TokenPhaseExecutor->resolveArtifactPayload(
      Bridge.Task, Input, prepTokenArtifactID(), &Payload);
  if (Resolve.Code != NEVERC_STATUS_OK)
    return Resolve;
  const auto &Artifact = *static_cast<const TokenPhaseArtifact *>(Payload);
  if (Artifact.Tokens.size() != 1)
    return status(NEVERC_STATUS_VERIFICATION_FAILED);
  auto Handle = Bridge.createToken(Artifact.Tokens.front());
  if (!Handle) {
    consumeError(Handle.takeError());
    return status(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutToken = *Handle;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL PluginPrepBridge::createTokenPhaseOutput(
    void *Context, const NevercPhaseFrame *Frame,
    const NevercPhaseContinuation *Continuation,
    const NevercTokenHandle *Tokens, uint64_t TokenCount,
    NevercArtifactHandle *OutOutput) {
  if (!Context || !OutOutput || (!Tokens && TokenCount != 0) ||
      TokenCount > MaximumTokenPhaseOutput ||
      TokenCount > std::numeric_limits<size_t>::max())
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutOutput = {};
  auto &Bridge = *static_cast<PluginPrepBridge *>(Context);
  if (!validFrame(Bridge.Task, Frame))
    return status(NEVERC_STATUS_WRONG_SCOPE);
  if (!Bridge.TokenPhaseExecutor)
    return status(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
  if (!Bridge.TokenPhaseExecutor->isActiveContinuation(Frame, Continuation))
    return status(NEVERC_STATUS_POLICY_VIOLATION);

  const void *InputPayload = nullptr;
  NevercStatus Resolve = Bridge.TokenPhaseExecutor->resolveArtifactPayload(
      Bridge.Task, Frame->Input, prepTokenArtifactID(), &InputPayload);
  if (Resolve.Code != NEVERC_STATUS_OK)
    return Resolve;
  const auto &InputArtifact =
      *static_cast<const TokenPhaseArtifact *>(InputPayload);
  if (InputArtifact.Tokens.size() != 1)
    return status(NEVERC_STATUS_VERIFICATION_FAILED);
  const bool InputIsEOF = InputArtifact.Tokens.front().is(tok::eof);

  TokenPhaseArtifact Artifact;
  Artifact.Replacement = true;
  Artifact.Tokens.reserve(static_cast<size_t>(TokenCount));
  for (uint64_t Index = 0; Index != TokenCount; ++Index) {
    const Token *Value = nullptr;
    Resolve = Bridge.resolveToken(Bridge.Task.handle(), Tokens[Index], &Value);
    if (Resolve.Code != NEVERC_STATUS_OK)
      return Resolve;
    if (Value->is(tok::unknown) || Value->is(tok::eod) ||
        (!InputIsEOF && Value->is(tok::eof)))
      return status(NEVERC_STATUS_VERIFICATION_FAILED);
    Artifact.Tokens.push_back(*Value);
  }
  if (InputIsEOF &&
      (Artifact.Tokens.size() != 1 || !Artifact.Tokens.front().is(tok::eof)))
    return status(NEVERC_STATUS_VERIFICATION_FAILED);

  auto *CandidatePayload =
      new (std::nothrow) TokenPhaseArtifact(std::move(Artifact));
  if (!CandidatePayload)
    return status(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  auto Candidate = Bridge.TokenPhaseExecutor->createCandidate(
      Bridge.Task, prepTokenArtifactID(), CandidatePayload);
  if (!Candidate) {
    delete CandidatePayload;
    consumeError(Candidate.takeError());
    return status(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutOutput = *Candidate;
  return neverc_status_ok();
}

} // namespace neverc::plugin
