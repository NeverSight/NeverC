#include "TokenStream.h"
#include "FrontendPluginInterfaces.h"
#include "PrepBridgeInternal.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Plugin/Host/PluginArtifactRegistry.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginIOBridge.h"
#include "neverc/Plugin/Host/PluginPhaseExecutor.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "neverc/Scan/PrepEngine.h"
#include "llvm/Support/Error.h"
#include <limits>
#include <new>

using namespace llvm;

namespace neverc::plugin {
namespace {

using namespace prep_bridge_detail;

NevercInterfaceID sourceUnitArtifactID() {
  return {NEVERC_PHASE_PREP_BUILD_TOKEN_STREAM_INPUT_HIGH,
          NEVERC_PHASE_PREP_BUILD_TOKEN_STREAM_INPUT_LOW};
}

bool sameID(NevercInterfaceID Left, NevercInterfaceID Right) {
  return Left.High == Right.High && Left.Low == Right.Low;
}

bool validFrame(PluginTaskContext &Task, const NevercPhaseFrame *Frame) {
  return Frame && sameHandle(Frame->Task, Task.handle()) && !Task.isEnded() &&
         sameID(Frame->Phase, prepBuildTokenStreamPhaseID());
}

Expected<void *> cloneTokenStreamArtifact(const void *Payload) {
  if (!Payload)
    return createStringError(inconvertibleErrorCode(),
                             "preprocessor token stream payload is null");
  return static_cast<void *>(new PrepTokenStreamArtifact(
      *static_cast<const PrepTokenStreamArtifact *>(Payload)));
}

Error verifyTokenStreamArtifact(const void *Payload) {
  if (!Payload)
    return createStringError(inconvertibleErrorCode(),
                             "preprocessor token stream payload is null");
  const auto &Artifact = *static_cast<const PrepTokenStreamArtifact *>(Payload);
  if (!Artifact.Engine)
    return createStringError(inconvertibleErrorCode(),
                             "preprocessor token stream has no engine");
  if (Artifact.Dependencies.empty())
    return createStringError(
        inconvertibleErrorCode(),
        "preprocessor token stream has no recorded input dependency");
  if (Artifact.BuiltinLazy) {
    if (!Artifact.Tokens.empty())
      return createStringError(
          inconvertibleErrorCode(),
          "lazy builtin preprocessor stream must not own tokens");
    return Error::success();
  }
  if (Artifact.Tokens.empty())
    return createStringError(inconvertibleErrorCode(),
                             "preprocessor token stream is empty");
  if (Artifact.Tokens.size() > NEVERC_PREP_TOKEN_STREAM_MAX_TOKENS)
    return createStringError(inconvertibleErrorCode(),
                             "preprocessor token stream exceeds token limit");

  SourceManager &SourceMgr = Artifact.Engine->getSourceManager();
  for (size_t Index = 0; Index != Artifact.Tokens.size(); ++Index) {
    const Token &Value = Artifact.Tokens[Index];
    if (Value.isOneOf(tok::unknown, tok::eod))
      return createStringError(inconvertibleErrorCode(),
                               "preprocessor token stream contains invalid "
                               "control token");
    if (Value.getLocation().isInvalid() ||
        SourceMgr.getFileID(SourceMgr.getSpellingLoc(Value.getLocation()))
            .isInvalid())
      return createStringError(
          inconvertibleErrorCode(),
          "preprocessor token stream contains an unmapped token");
    if (Value.is(tok::eof) && Index + 1 != Artifact.Tokens.size())
      return createStringError(
          inconvertibleErrorCode(),
          "preprocessor token stream contains an early EOF token");
  }
  if (!Artifact.Tokens.back().is(tok::eof))
    return createStringError(inconvertibleErrorCode(),
                             "preprocessor token stream is missing EOF");
  return Error::success();
}

NevercStatus resolveStreamBuilder(PluginPrepBridge &Bridge,
                                  NevercTaskHandle TaskHandle,
                                  NevercTokenStreamBuilderHandle Builder,
                                  TokenStreamBuilderPayload **OutPayload) {
  if (!OutPayload)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutPayload = nullptr;
  if (!sameHandle(TaskHandle, Bridge.taskContext().handle()))
    return status(NEVERC_STATUS_WRONG_SCOPE);
  if (Bridge.taskContext().isEnded())
    return status(NEVERC_STATUS_INVALID_STATE);
  void *RawPayload = nullptr;
  NevercStatus Resolve = Bridge.taskContext().handles().resolve(
      Builder, PluginTokenStreamBuilderHandleKind, &RawPayload);
  if (Resolve.Code != NEVERC_STATUS_OK)
    return Resolve;
  auto *Payload = static_cast<TokenStreamBuilderPayload *>(RawPayload);
  *OutPayload = Payload;
  return neverc_status_ok();
}

} // namespace

NevercInterfaceID prepBuildTokenStreamPhaseID() {
  return {NEVERC_PHASE_PREP_BUILD_TOKEN_STREAM_HIGH,
          NEVERC_PHASE_PREP_BUILD_TOKEN_STREAM_LOW};
}

NevercInterfaceID prepTokenStreamArtifactID() {
  return {NEVERC_PHASE_PREP_BUILD_TOKEN_STREAM_OUTPUT_HIGH,
          NEVERC_PHASE_PREP_BUILD_TOKEN_STREAM_OUTPUT_LOW};
}

Error registerPrepTokenStreamArtifactType(PluginArtifactRegistry &Artifacts) {
  auto Type = Artifacts.registerType(
      {prepTokenStreamArtifactID(), "prep.token_stream",
       PluginArtifactOwnership::Owned, cloneTokenStreamArtifact,
       [](void *Payload) {
         delete static_cast<PrepTokenStreamArtifact *>(Payload);
       },
       verifyTokenStreamArtifact});
  if (!Type)
    return Type.takeError();
  return Error::success();
}

NevercStatus NEVERC_CALL PluginPrepBridge::getTokenStreamPhaseInput(
    void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
    NevercPrepTokenStreamPhaseInput *OutInput) {
  if (!Context || !OutInput)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<PluginPrepBridge *>(Context);
  if (!validFrame(Bridge.Task, Frame))
    return status(NEVERC_STATUS_WRONG_SCOPE);
  if (!sameHandle(Frame->Input, Input))
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  if (!Bridge.TokenPhaseExecutor)
    return status(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);

  const void *Payload = nullptr;
  NevercStatus Resolve = Bridge.TokenPhaseExecutor->resolveArtifactPayload(
      Bridge.Task, Input, sourceUnitArtifactID(), &Payload);
  if (Resolve.Code != NEVERC_STATUS_OK)
    return Resolve;
  if (!Payload)
    return status(NEVERC_STATUS_VERIFICATION_FAILED);

  SourceManager &SourceMgr = Bridge.Prep.getSourceManager();
  FileID MainFile = SourceMgr.getMainFileID();
  if (MainFile.isInvalid())
    return status(NEVERC_STATUS_INVALID_STATE);
  auto Start =
      Bridge.Locations.createLocation(SourceMgr.getLocForStartOfFile(MainFile));
  if (!Start) {
    consumeError(Start.takeError());
    return status(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  auto End =
      Bridge.Locations.createLocation(SourceMgr.getLocForEndOfFile(MainFile));
  if (!End) {
    consumeError(End.takeError());
    return status(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }

  NevercPrepTokenStreamPhaseInput Result{};
  Result.Header = {sizeof(Result), NEVERC_PREP_API_MAJOR, NEVERC_PREP_API_MINOR,
                   0};
  Result.StartLocation = *Start;
  Result.EndLocation = *End;
  Result.MaximumTokenCount = NEVERC_PREP_TOKEN_STREAM_MAX_TOKENS;
  return writeCallerBuffer(OutInput, Result);
}

NevercStatus NEVERC_CALL PluginPrepBridge::createTokenStreamBuilder(
    void *Context, NevercTaskHandle TaskHandle,
    NevercTokenStreamBuilderHandle *OutBuilder) {
  if (!Context || !OutBuilder)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutBuilder = {};
  auto &Bridge = *static_cast<PluginPrepBridge *>(Context);
  if (!sameHandle(TaskHandle, Bridge.Task.handle()))
    return status(NEVERC_STATUS_WRONG_SCOPE);
  if (Bridge.Task.isEnded())
    return status(NEVERC_STATUS_INVALID_STATE);

  auto *Payload = new (std::nothrow) TokenStreamBuilderPayload();
  if (!Payload)
    return status(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  Payload->Engine = &Bridge.Prep;
  auto Handle = Bridge.Task.handles().create(
      PluginTokenStreamBuilderHandleKind, Payload, [](void *Value) {
        delete static_cast<TokenStreamBuilderPayload *>(Value);
      });
  if (!Handle) {
    delete Payload;
    consumeError(Handle.takeError());
    return status(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutBuilder = *Handle;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL PluginPrepBridge::tokenStreamBuilderAppend(
    void *Context, NevercTaskHandle TaskHandle,
    NevercTokenStreamBuilderHandle Builder, const NevercTokenHandle *Tokens,
    uint64_t TokenCount) {
  if (!Context || (!Tokens && TokenCount != 0))
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<PluginPrepBridge *>(Context);
  TokenStreamBuilderPayload *Payload = nullptr;
  NevercStatus Resolve =
      resolveStreamBuilder(Bridge, TaskHandle, Builder, &Payload);
  if (Resolve.Code != NEVERC_STATUS_OK)
    return Resolve;
  if (Payload->Engine != &Bridge.Prep)
    return status(NEVERC_STATUS_WRONG_SCOPE);
  if (Payload->Committed)
    return status(NEVERC_STATUS_INVALID_STATE);
  if (TokenCount > NEVERC_PREP_TOKEN_STREAM_MAX_TOKENS ||
      Payload->TokenCount > NEVERC_PREP_TOKEN_STREAM_MAX_TOKENS - TokenCount)
    return status(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  if (TokenCount > std::numeric_limits<size_t>::max())
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  if (TokenCount == 0)
    return neverc_status_ok();

  std::vector<Token> Chunk;
  Chunk.reserve(static_cast<size_t>(TokenCount));
  for (uint64_t Index = 0; Index != TokenCount; ++Index) {
    const Token *Value = nullptr;
    Resolve = Bridge.resolvePublishedToken(TaskHandle, Tokens[Index], &Value);
    if (Resolve.Code != NEVERC_STATUS_OK)
      return Resolve;
    Chunk.push_back(*Value);
  }
  Payload->Chunks.push_back(std::move(Chunk));
  Payload->TokenCount += TokenCount;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL PluginPrepBridge::tokenStreamBuilderCommit(
    void *Context, const NevercPhaseFrame *Frame,
    NevercTokenStreamBuilderHandle Builder, NevercArtifactHandle *OutOutput) {
  if (!Context || !OutOutput)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutOutput = {};
  auto &Bridge = *static_cast<PluginPrepBridge *>(Context);
  if (!validFrame(Bridge.Task, Frame))
    return status(NEVERC_STATUS_WRONG_SCOPE);
  if (!Bridge.TokenPhaseExecutor)
    return status(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);

  const void *InputPayload = nullptr;
  NevercStatus Resolve = Bridge.TokenPhaseExecutor->resolveArtifactPayload(
      Bridge.Task, Frame->Input, sourceUnitArtifactID(), &InputPayload);
  if (Resolve.Code != NEVERC_STATUS_OK)
    return Resolve;
  if (!InputPayload)
    return status(NEVERC_STATUS_VERIFICATION_FAILED);

  TokenStreamBuilderPayload *Payload = nullptr;
  Resolve = resolveStreamBuilder(Bridge, Frame->Task, Builder, &Payload);
  if (Resolve.Code != NEVERC_STATUS_OK)
    return Resolve;
  if (Payload->Engine != &Bridge.Prep)
    return status(NEVERC_STATUS_WRONG_SCOPE);
  if (Payload->Committed)
    return status(NEVERC_STATUS_INVALID_STATE);

  auto *Artifact = new (std::nothrow) PrepTokenStreamArtifact();
  if (!Artifact)
    return status(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  Artifact->Engine = &Bridge.Prep;
  Artifact->Dependencies = getPluginDependencies(Bridge.Task);
  Artifact->Tokens.reserve(static_cast<size_t>(Payload->TokenCount));
  for (const std::vector<Token> &Chunk : Payload->Chunks)
    Artifact->Tokens.insert(Artifact->Tokens.end(), Chunk.begin(), Chunk.end());

  auto Candidate = Bridge.TokenPhaseExecutor->createCandidate(
      Bridge.Task, prepTokenStreamArtifactID(), Artifact);
  if (!Candidate) {
    delete Artifact;
    consumeError(Candidate.takeError());
    return status(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  Payload->Committed = true;
  *OutOutput = *Candidate;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL PluginPrepBridge::destroyTokenStreamBuilder(
    void *Context, NevercTaskHandle TaskHandle,
    NevercTokenStreamBuilderHandle Builder) {
  if (!Context)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<PluginPrepBridge *>(Context);
  if (!sameHandle(TaskHandle, Bridge.Task.handle()))
    return status(NEVERC_STATUS_WRONG_SCOPE);
  return Bridge.Task.handles().release(Builder,
                                       PluginTokenStreamBuilderHandleKind);
}

} // namespace neverc::plugin
