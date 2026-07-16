#include "FrontendPluginInterfaces.h"
#include "PrepBridgeInternal.h"
#include "neverc/Plugin/Host/PluginArtifactRegistry.h"
#include "neverc/Plugin/Host/PluginPhaseExecutor.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "neverc/Scan/Token.h"
#include "llvm/Support/Error.h"
#include <limits>
#include <new>
#include <utility>

using namespace llvm;

namespace neverc::plugin {
using namespace prep_bridge_detail;

namespace {

constexpr uint64_t MaximumHookTokenOutput = 256;

template <typename T> Expected<void *> cloneArtifact(const void *Payload) {
  if (!Payload)
    return createStringError(inconvertibleErrorCode(),
                             "prep hook artifact is null");
  return static_cast<void *>(new T(*static_cast<const T *>(Payload)));
}

template <typename T> void destroyArtifact(void *Payload) {
  delete static_cast<T *>(Payload);
}

Error verifyIncludeArtifact(const void *Payload) {
  if (!Payload)
    return createStringError(inconvertibleErrorCode(),
                             "include hook artifact is null");
  const auto &Artifact = *static_cast<const IncludePhaseArtifact *>(Payload);
  if (Artifact.Action > NEVERC_PREP_INCLUDE_REDIRECT)
    return createStringError(inconvertibleErrorCode(),
                             "include hook action is invalid");
  if (Artifact.Action == NEVERC_PREP_INCLUDE_REDIRECT &&
      Artifact.ReplacementFilename.empty())
    return createStringError(inconvertibleErrorCode(),
                             "include redirect filename is empty");
  return Error::success();
}

bool validReplacementTokens(ArrayRef<Token> Tokens) {
  if (Tokens.size() > MaximumHookTokenOutput)
    return false;
  for (const Token &Value : Tokens)
    if (Value.is(tok::unknown) || Value.is(tok::eod) || Value.is(tok::eof))
      return false;
  return true;
}

Error verifyMacroArtifact(const void *Payload) {
  if (!Payload)
    return createStringError(inconvertibleErrorCode(),
                             "macro hook artifact is null");
  const auto &Artifact = *static_cast<const MacroPhaseArtifact *>(Payload);
  if (Artifact.Operation < NEVERC_PREP_MACRO_DEFINE ||
      Artifact.Operation > NEVERC_PREP_MACRO_EXPAND_BUILTIN ||
      Artifact.Action > NEVERC_PREP_MACRO_REPLACE_TOKENS)
    return createStringError(inconvertibleErrorCode(),
                             "macro hook operation or action is invalid");
  const bool IsExpansion =
      Artifact.Operation == NEVERC_PREP_MACRO_EXPAND ||
      Artifact.Operation == NEVERC_PREP_MACRO_EXPAND_BUILTIN;
  if (Artifact.Action == NEVERC_PREP_MACRO_REPLACE_TOKENS && !IsExpansion)
    return createStringError(
        inconvertibleErrorCode(),
        "macro token replacement is only valid for expansion");
  if (Artifact.Action != NEVERC_PREP_MACRO_REPLACE_TOKENS &&
      !Artifact.ReplacementTokens.empty())
    return createStringError(inconvertibleErrorCode(),
                             "macro continuation carries replacement tokens");
  if (!validReplacementTokens(Artifact.ReplacementTokens))
    return createStringError(inconvertibleErrorCode(),
                             "macro replacement token stream is invalid");
  return Error::success();
}

Error verifyPragmaArtifact(const void *Payload) {
  if (!Payload)
    return createStringError(inconvertibleErrorCode(),
                             "pragma hook artifact is null");
  const auto &Artifact = *static_cast<const PragmaPhaseArtifact *>(Payload);
  if (Artifact.Action > NEVERC_PREP_PRAGMA_REPLACE_TOKENS)
    return createStringError(inconvertibleErrorCode(),
                             "pragma hook action is invalid");
  if (Artifact.Action != NEVERC_PREP_PRAGMA_REPLACE_TOKENS &&
      !Artifact.ReplacementTokens.empty())
    return createStringError(inconvertibleErrorCode(),
                             "pragma continuation carries replacement tokens");
  if (!validReplacementTokens(Artifact.ReplacementTokens))
    return createStringError(inconvertibleErrorCode(),
                             "pragma replacement token stream is invalid");
  return Error::success();
}

Error verifyFeatureQueryArtifact(const void *Payload) {
  if (!Payload)
    return createStringError(inconvertibleErrorCode(),
                             "feature-query hook artifact is null");
  const auto &Artifact =
      *static_cast<const FeatureQueryPhaseArtifact *>(Payload);
  if (Artifact.Kind < NEVERC_PREP_QUERY_HAS_FEATURE ||
      Artifact.Kind > NEVERC_PREP_QUERY_HAS_INCLUDE_NEXT ||
      Artifact.Action > NEVERC_PREP_QUERY_REPLACE)
    return createStringError(inconvertibleErrorCode(),
                             "feature-query hook artifact is invalid");
  return Error::success();
}

bool validFrame(const PluginTaskContext &Task, const NevercPhaseFrame *Frame,
                NevercInterfaceID Phase) {
  return Frame && Frame->Header.StructSize >= sizeof(*Frame) &&
         Frame->Header.Major == NEVERC_PLUGIN_ABI_MAJOR &&
         Frame->Header.Minor <= NEVERC_PLUGIN_ABI_MINOR &&
         Frame->Header.Flags == 0 && sameHandle(Frame->Task, Task.handle()) &&
         sameHandle(Frame->Session, Task.session().handle()) &&
         samePluginInterfaceID(Frame->Phase, Phase);
}

bool validInputHeader(const NevercABITableHeader &Header, size_t Size) {
  return Header.StructSize >= Size && Header.Major == NEVERC_PREP_API_MAJOR &&
         Header.Minor <= NEVERC_PREP_API_MINOR && Header.Flags == 0;
}

template <typename T>
NevercStatus
resolveArtifact(PluginPrepBridge &Bridge, const NevercPhaseFrame *Frame,
                NevercArtifactHandle Input, NevercInterfaceID Phase,
                NevercInterfaceID ArtifactType, const T **OutPayload) {
  if (!validFrame(Bridge.taskContext(), Frame, Phase))
    return status(NEVERC_STATUS_WRONG_SCOPE);
  if (!Bridge.phaseExecutor())
    return status(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
  if (!sameHandle(Frame->Input, Input))
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  const void *Payload = nullptr;
  NevercStatus Resolve = Bridge.phaseExecutor()->resolveArtifactPayload(
      Bridge.taskContext(), Input, ArtifactType, &Payload);
  if (Resolve.Code != NEVERC_STATUS_OK)
    return Resolve;
  *OutPayload = static_cast<const T *>(Payload);
  return neverc_status_ok();
}

template <typename T>
NevercStatus
createCandidate(PluginPrepBridge &Bridge, const NevercPhaseFrame *Frame,
                const NevercPhaseContinuation *Continuation,
                NevercInterfaceID Phase, NevercInterfaceID ArtifactType,
                T Artifact, NevercArtifactHandle *OutOutput) {
  if (!validFrame(Bridge.taskContext(), Frame, Phase))
    return status(NEVERC_STATUS_WRONG_SCOPE);
  if (!Bridge.phaseExecutor())
    return status(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
  if (!Bridge.phaseExecutor()->isActiveContinuation(Frame, Continuation))
    return status(NEVERC_STATUS_POLICY_VIOLATION);
  auto *Payload = new (std::nothrow) T(std::move(Artifact));
  if (!Payload)
    return status(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  auto Candidate = Bridge.phaseExecutor()->createCandidate(
      Bridge.taskContext(), ArtifactType, Payload);
  if (!Candidate) {
    delete Payload;
    consumeError(Candidate.takeError());
    return status(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutOutput = *Candidate;
  return neverc_status_ok();
}

NevercStatus publishLocation(PluginPrepBridge &Bridge, SourceLocation Location,
                             NevercSourceLocation *OutLocation) {
  auto Public = Bridge.locationBridge().createLocation(Location);
  if (!Public) {
    consumeError(Public.takeError());
    return status(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutLocation = *Public;
  return neverc_status_ok();
}

NevercStatus appendOutputTokens(PluginPrepBridge &Bridge,
                                const NevercTokenHandle *Tokens,
                                uint64_t TokenCount,
                                SmallVectorImpl<Token> &Output) {
  if ((!Tokens && TokenCount != 0) || TokenCount > MaximumHookTokenOutput ||
      TokenCount > std::numeric_limits<size_t>::max())
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  Output.reserve(static_cast<size_t>(TokenCount));
  for (uint64_t Index = 0; Index != TokenCount; ++Index) {
    const Token *Value = nullptr;
    NevercStatus Resolve = Bridge.resolvePublishedToken(
        Bridge.taskContext().handle(), Tokens[Index], &Value);
    if (Resolve.Code != NEVERC_STATUS_OK)
      return Resolve;
    if (Value->is(tok::unknown) || Value->is(tok::eod) || Value->is(tok::eof))
      return status(NEVERC_STATUS_VERIFICATION_FAILED);
    Output.push_back(*Value);
  }
  return neverc_status_ok();
}

} // namespace

NevercInterfaceID prepIncludePhaseID() {
  return {NEVERC_PHASE_PREP_INCLUDE_HIGH, NEVERC_PHASE_PREP_INCLUDE_LOW};
}

NevercInterfaceID prepIncludeArtifactID() {
  return {NEVERC_PHASE_PREP_INCLUDE_INPUT_HIGH,
          NEVERC_PHASE_PREP_INCLUDE_INPUT_LOW};
}

NevercInterfaceID prepMacroPhaseID() {
  return {NEVERC_PHASE_PREP_MACRO_HIGH, NEVERC_PHASE_PREP_MACRO_LOW};
}

NevercInterfaceID prepMacroArtifactID() {
  return {NEVERC_PHASE_PREP_MACRO_INPUT_HIGH,
          NEVERC_PHASE_PREP_MACRO_INPUT_LOW};
}

NevercInterfaceID prepPragmaPhaseID() {
  return {NEVERC_PHASE_PREP_PRAGMA_HIGH, NEVERC_PHASE_PREP_PRAGMA_LOW};
}

NevercInterfaceID prepPragmaArtifactID() {
  return {NEVERC_PHASE_PREP_PRAGMA_INPUT_HIGH,
          NEVERC_PHASE_PREP_PRAGMA_INPUT_LOW};
}

NevercInterfaceID prepFeatureQueryPhaseID() {
  return {NEVERC_PHASE_PREP_FEATURE_QUERY_HIGH,
          NEVERC_PHASE_PREP_FEATURE_QUERY_LOW};
}

NevercInterfaceID prepFeatureQueryArtifactID() {
  return {NEVERC_PHASE_PREP_FEATURE_QUERY_INPUT_HIGH,
          NEVERC_PHASE_PREP_FEATURE_QUERY_INPUT_LOW};
}

Error registerPrepHookArtifactTypes(PluginArtifactRegistry &Artifacts) {
  auto Include = Artifacts.registerType(
      {prepIncludeArtifactID(), "prep.include", PluginArtifactOwnership::Owned,
       cloneArtifact<IncludePhaseArtifact>,
       destroyArtifact<IncludePhaseArtifact>, verifyIncludeArtifact});
  if (!Include)
    return Include.takeError();
  auto Macro = Artifacts.registerType(
      {prepMacroArtifactID(), "prep.macro", PluginArtifactOwnership::Owned,
       cloneArtifact<MacroPhaseArtifact>, destroyArtifact<MacroPhaseArtifact>,
       verifyMacroArtifact});
  if (!Macro)
    return Macro.takeError();
  auto Pragma = Artifacts.registerType(
      {prepPragmaArtifactID(), "prep.pragma", PluginArtifactOwnership::Owned,
       cloneArtifact<PragmaPhaseArtifact>, destroyArtifact<PragmaPhaseArtifact>,
       verifyPragmaArtifact});
  if (!Pragma)
    return Pragma.takeError();
  auto Feature = Artifacts.registerType(
      {prepFeatureQueryArtifactID(), "prep.feature_query",
       PluginArtifactOwnership::Owned, cloneArtifact<FeatureQueryPhaseArtifact>,
       destroyArtifact<FeatureQueryPhaseArtifact>, verifyFeatureQueryArtifact});
  if (!Feature)
    return Feature.takeError();
  return Error::success();
}

NevercStatus NEVERC_CALL PluginPrepBridge::getIncludePhaseInput(
    void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
    NevercPrepIncludePhaseInput *OutInput) {
  if (!Context || !OutInput)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<PluginPrepBridge *>(Context);
  const IncludePhaseArtifact *Artifact = nullptr;
  NevercStatus Resolve =
      resolveArtifact(Bridge, Frame, Input, prepIncludePhaseID(),
                      prepIncludeArtifactID(), &Artifact);
  if (Resolve.Code != NEVERC_STATUS_OK)
    return Resolve;
  NevercPrepIncludePhaseInput Info{};
  Info.Header = {sizeof(Info), NEVERC_PREP_API_MAJOR, NEVERC_PREP_API_MINOR, 0};
  Resolve = publishLocation(Bridge, Artifact->Location, &Info.Location);
  if (Resolve.Code != NEVERC_STATUS_OK)
    return Resolve;
  auto IncludeToken = Bridge.createToken(Artifact->IncludeToken);
  if (!IncludeToken) {
    consumeError(IncludeToken.takeError());
    return status(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  Info.IncludeToken = *IncludeToken;
  Info.Filename = stringView(Artifact->Filename);
  Info.SearchPath = stringView(Artifact->SearchPath);
  Info.RelativePath = stringView(Artifact->RelativePath);
  Info.Action = Artifact->Action;
  Info.IsAngled = Artifact->IsAngled ? NEVERC_TRUE : NEVERC_FALSE;
  Info.IsImport = Artifact->IsImport ? NEVERC_TRUE : NEVERC_FALSE;
  Info.IsIncludeNext = Artifact->IsIncludeNext ? NEVERC_TRUE : NEVERC_FALSE;
  Info.ReplacementIsAngled =
      Artifact->ReplacementIsAngled ? NEVERC_TRUE : NEVERC_FALSE;
  Info.ReplacementFilename = stringView(Artifact->ReplacementFilename);
  return writeCallerBuffer(OutInput, Info);
}

NevercStatus NEVERC_CALL PluginPrepBridge::createIncludePhaseOutput(
    void *Context, const NevercPhaseFrame *Frame,
    const NevercPhaseContinuation *Continuation,
    const NevercPrepIncludePhaseOutput *Output,
    NevercArtifactHandle *OutOutput) {
  if (!Context || !Output || !OutOutput ||
      !validInputHeader(Output->Header, sizeof(*Output)))
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutOutput = {};
  auto &Bridge = *static_cast<PluginPrepBridge *>(Context);
  const IncludePhaseArtifact *Input = nullptr;
  NevercStatus Resolve =
      resolveArtifact(Bridge, Frame, Frame->Input, prepIncludePhaseID(),
                      prepIncludeArtifactID(), &Input);
  if (Resolve.Code != NEVERC_STATUS_OK)
    return Resolve;
  if (Output->Action > NEVERC_PREP_INCLUDE_REDIRECT ||
      Output->IsAngled > NEVERC_TRUE ||
      (Output->Action == NEVERC_PREP_INCLUDE_REDIRECT &&
       (!Output->Filename.Data || Output->Filename.Length == 0)))
    return status(NEVERC_STATUS_VERIFICATION_FAILED);
  IncludePhaseArtifact Artifact = *Input;
  Artifact.Action = Output->Action;
  Artifact.ReplacementIsAngled = Output->IsAngled != NEVERC_FALSE;
  Artifact.ReplacementFilename.clear();
  if (Output->Action == NEVERC_PREP_INCLUDE_REDIRECT)
    Artifact.ReplacementFilename.assign(Output->Filename.Data,
                                        Output->Filename.Length);
  return createCandidate(Bridge, Frame, Continuation, prepIncludePhaseID(),
                         prepIncludeArtifactID(), std::move(Artifact),
                         OutOutput);
}

NevercStatus NEVERC_CALL PluginPrepBridge::getMacroPhaseInput(
    void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
    NevercPrepMacroPhaseInput *OutInput) {
  if (!Context || !OutInput)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<PluginPrepBridge *>(Context);
  const MacroPhaseArtifact *Artifact = nullptr;
  NevercStatus Resolve =
      resolveArtifact(Bridge, Frame, Input, prepMacroPhaseID(),
                      prepMacroArtifactID(), &Artifact);
  if (Resolve.Code != NEVERC_STATUS_OK)
    return Resolve;
  NevercPrepMacroPhaseInput Info{};
  Info.Header = {sizeof(Info), NEVERC_PREP_API_MAJOR, NEVERC_PREP_API_MINOR, 0};
  Info.Operation = Artifact->Operation;
  Info.Action = Artifact->Action;
  auto NameToken = Bridge.createToken(Artifact->NameToken);
  if (!NameToken) {
    consumeError(NameToken.takeError());
    return status(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  Info.NameToken = *NameToken;
  if (Artifact->Name) {
    auto Name = Bridge.createIdentifier(Artifact->Name);
    if (!Name) {
      consumeError(Name.takeError());
      return status(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    Info.Name = *Name;
  }
  if (Artifact->Definition) {
    auto Definition =
        Bridge.createMacroDefinition(Artifact->Name, Artifact->Definition);
    if (!Definition) {
      consumeError(Definition.takeError());
      return status(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    Info.Definition = *Definition;
  }
  if (Artifact->Arguments) {
    auto Arguments = Bridge.createMacroArguments(Artifact->Arguments);
    if (!Arguments) {
      consumeError(Arguments.takeError());
      return status(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    Info.Arguments = *Arguments;
  }
  if (Artifact->Action == NEVERC_PREP_MACRO_REPLACE_TOKENS) {
    auto Tokens = Bridge.createTokenStream(Artifact->ReplacementTokens);
    if (!Tokens) {
      consumeError(Tokens.takeError());
      return status(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    Info.ReplacementTokens = *Tokens;
  }
  return writeCallerBuffer(OutInput, Info);
}

NevercStatus NEVERC_CALL PluginPrepBridge::createMacroPhaseOutput(
    void *Context, const NevercPhaseFrame *Frame,
    const NevercPhaseContinuation *Continuation,
    const NevercPrepMacroPhaseOutput *Output, NevercArtifactHandle *OutOutput) {
  if (!Context || !Output || !OutOutput ||
      !validInputHeader(Output->Header, sizeof(*Output)))
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutOutput = {};
  auto &Bridge = *static_cast<PluginPrepBridge *>(Context);
  const MacroPhaseArtifact *Input = nullptr;
  NevercStatus Resolve =
      resolveArtifact(Bridge, Frame, Frame->Input, prepMacroPhaseID(),
                      prepMacroArtifactID(), &Input);
  if (Resolve.Code != NEVERC_STATUS_OK)
    return Resolve;
  const bool IsExpansion = Input->Operation == NEVERC_PREP_MACRO_EXPAND ||
                           Input->Operation == NEVERC_PREP_MACRO_EXPAND_BUILTIN;
  if (Output->Action > NEVERC_PREP_MACRO_REPLACE_TOKENS ||
      (Output->Action == NEVERC_PREP_MACRO_REPLACE_TOKENS && !IsExpansion) ||
      (Output->Action != NEVERC_PREP_MACRO_REPLACE_TOKENS &&
       Output->TokenCount != 0))
    return status(NEVERC_STATUS_VERIFICATION_FAILED);
  MacroPhaseArtifact Artifact = *Input;
  Artifact.Action = Output->Action;
  Artifact.ReplacementTokens.clear();
  SmallVector<Token, 8> Replacement;
  Resolve = appendOutputTokens(Bridge, Output->Tokens, Output->TokenCount,
                               Replacement);
  if (Resolve.Code != NEVERC_STATUS_OK)
    return Resolve;
  Artifact.ReplacementTokens.assign(Replacement.begin(), Replacement.end());
  return createCandidate(Bridge, Frame, Continuation, prepMacroPhaseID(),
                         prepMacroArtifactID(), std::move(Artifact), OutOutput);
}

NevercStatus NEVERC_CALL PluginPrepBridge::getPragmaPhaseInput(
    void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
    NevercPrepPragmaPhaseInput *OutInput) {
  if (!Context || !OutInput)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<PluginPrepBridge *>(Context);
  const PragmaPhaseArtifact *Artifact = nullptr;
  NevercStatus Resolve =
      resolveArtifact(Bridge, Frame, Input, prepPragmaPhaseID(),
                      prepPragmaArtifactID(), &Artifact);
  if (Resolve.Code != NEVERC_STATUS_OK)
    return Resolve;
  NevercPrepPragmaPhaseInput Info{};
  Info.Header = {sizeof(Info), NEVERC_PREP_API_MAJOR, NEVERC_PREP_API_MINOR, 0};
  Resolve = publishLocation(Bridge, Artifact->Location, &Info.Location);
  if (Resolve.Code != NEVERC_STATUS_OK)
    return Resolve;
  Info.Introducer = Artifact->Introducer;
  Info.Action = Artifact->Action;
  Info.Namespace = stringView(Artifact->Namespace);
  Info.Name = stringView(Artifact->Name);
  auto Tokens = Bridge.createTokenStream(Artifact->Tokens);
  if (!Tokens) {
    consumeError(Tokens.takeError());
    return status(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  Info.Tokens = *Tokens;
  if (Artifact->Action == NEVERC_PREP_PRAGMA_REPLACE_TOKENS) {
    auto Replacement = Bridge.createTokenStream(Artifact->ReplacementTokens);
    if (!Replacement) {
      consumeError(Replacement.takeError());
      return status(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    Info.ReplacementTokens = *Replacement;
  }
  return writeCallerBuffer(OutInput, Info);
}

NevercStatus NEVERC_CALL PluginPrepBridge::createPragmaPhaseOutput(
    void *Context, const NevercPhaseFrame *Frame,
    const NevercPhaseContinuation *Continuation,
    const NevercPrepPragmaPhaseOutput *Output,
    NevercArtifactHandle *OutOutput) {
  if (!Context || !Output || !OutOutput ||
      !validInputHeader(Output->Header, sizeof(*Output)))
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutOutput = {};
  auto &Bridge = *static_cast<PluginPrepBridge *>(Context);
  const PragmaPhaseArtifact *Input = nullptr;
  NevercStatus Resolve =
      resolveArtifact(Bridge, Frame, Frame->Input, prepPragmaPhaseID(),
                      prepPragmaArtifactID(), &Input);
  if (Resolve.Code != NEVERC_STATUS_OK)
    return Resolve;
  if (Output->Action > NEVERC_PREP_PRAGMA_REPLACE_TOKENS ||
      (Output->Action != NEVERC_PREP_PRAGMA_REPLACE_TOKENS &&
       Output->TokenCount != 0))
    return status(NEVERC_STATUS_VERIFICATION_FAILED);
  PragmaPhaseArtifact Artifact = *Input;
  Artifact.Action = Output->Action;
  Artifact.ReplacementTokens.clear();
  SmallVector<Token, 8> Replacement;
  Resolve = appendOutputTokens(Bridge, Output->Tokens, Output->TokenCount,
                               Replacement);
  if (Resolve.Code != NEVERC_STATUS_OK)
    return Resolve;
  Artifact.ReplacementTokens.assign(Replacement.begin(), Replacement.end());
  return createCandidate(Bridge, Frame, Continuation, prepPragmaPhaseID(),
                         prepPragmaArtifactID(), std::move(Artifact),
                         OutOutput);
}

NevercStatus NEVERC_CALL PluginPrepBridge::getFeatureQueryPhaseInput(
    void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
    NevercPrepFeatureQueryPhaseInput *OutInput) {
  if (!Context || !OutInput)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<PluginPrepBridge *>(Context);
  const FeatureQueryPhaseArtifact *Artifact = nullptr;
  NevercStatus Resolve =
      resolveArtifact(Bridge, Frame, Input, prepFeatureQueryPhaseID(),
                      prepFeatureQueryArtifactID(), &Artifact);
  if (Resolve.Code != NEVERC_STATUS_OK)
    return Resolve;
  NevercPrepFeatureQueryPhaseInput Info{};
  Info.Header = {sizeof(Info), NEVERC_PREP_API_MAJOR, NEVERC_PREP_API_MINOR, 0};
  Resolve = publishLocation(Bridge, Artifact->Location, &Info.Location);
  if (Resolve.Code != NEVERC_STATUS_OK)
    return Resolve;
  Info.Kind = Artifact->Kind;
  Info.Action = Artifact->Action;
  Info.Name = stringView(Artifact->Name);
  Info.BuiltinValue = Artifact->BuiltinValue ? NEVERC_TRUE : NEVERC_FALSE;
  Info.Value = Artifact->Value ? NEVERC_TRUE : NEVERC_FALSE;
  return writeCallerBuffer(OutInput, Info);
}

NevercStatus NEVERC_CALL PluginPrepBridge::createFeatureQueryPhaseOutput(
    void *Context, const NevercPhaseFrame *Frame,
    const NevercPhaseContinuation *Continuation,
    const NevercPrepFeatureQueryPhaseOutput *Output,
    NevercArtifactHandle *OutOutput) {
  if (!Context || !Output || !OutOutput ||
      !validInputHeader(Output->Header, sizeof(*Output)))
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutOutput = {};
  auto &Bridge = *static_cast<PluginPrepBridge *>(Context);
  const FeatureQueryPhaseArtifact *Input = nullptr;
  NevercStatus Resolve =
      resolveArtifact(Bridge, Frame, Frame->Input, prepFeatureQueryPhaseID(),
                      prepFeatureQueryArtifactID(), &Input);
  if (Resolve.Code != NEVERC_STATUS_OK)
    return Resolve;
  if (Output->Action > NEVERC_PREP_QUERY_REPLACE || Output->Value > NEVERC_TRUE)
    return status(NEVERC_STATUS_VERIFICATION_FAILED);
  FeatureQueryPhaseArtifact Artifact = *Input;
  Artifact.Action = Output->Action;
  Artifact.Value = Output->Action == NEVERC_PREP_QUERY_REPLACE
                       ? Output->Value != NEVERC_FALSE
                       : Artifact.BuiltinValue;
  return createCandidate(Bridge, Frame, Continuation, prepFeatureQueryPhaseID(),
                         prepFeatureQueryArtifactID(), std::move(Artifact),
                         OutOutput);
}

} // namespace neverc::plugin
