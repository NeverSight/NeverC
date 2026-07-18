#include "SemaProvider.h"
#include "ASTUnitArtifact.h"
#include "SemanticUnitArtifact.h"
#include "neverc/Analyze/Sema.h"
#include "neverc/Analyze/SemaReplay.h"
#include "neverc/Plugin/Host/PluginArtifactRegistry.h"
#include "neverc/Plugin/Host/PluginPhaseExecutor.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "neverc/Tree/Decl/Decl.h"
#include <algorithm>
#include <cstring>
#include <new>

using namespace llvm;

namespace neverc::plugin {
namespace {

NevercStatus providerStatus(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

bool sameHandle(NevercHandle Left, NevercHandle Right) {
  return Left.Owner == Right.Owner && Left.Value == Right.Value;
}

bool validHeader(const NevercABITableHeader &Header, size_t Size) {
  return Header.StructSize >= Size && Header.Major == NEVERC_SEMA_API_MAJOR &&
         Header.Minor <= NEVERC_SEMA_API_MINOR && Header.Flags == 0;
}

bool nonnull(NevercInterfaceID ID) { return ID.High != 0 || ID.Low != 0; }

template <typename T>
NevercStatus writeCallerRecord(T *OutValue, const T &Value) {
  if (!OutValue)
    return providerStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  const uint32_t Capacity = OutValue->Header.StructSize;
  if (Capacity < sizeof(NevercABITableHeader))
    return providerStatus(NEVERC_STATUS_ABI_MISMATCH);
  const size_t Writable = std::min<size_t>(Capacity, sizeof(Value));
  std::memset(OutValue, 0, Writable);
  std::memcpy(OutValue, &Value, Writable);
  return Capacity < sizeof(Value)
             ? providerStatus(NEVERC_STATUS_ABI_MISMATCH)
             : neverc_status_ok();
}

NevercStringView stringView(StringRef Text) {
  return {Text.data(), static_cast<uint64_t>(Text.size())};
}

} // namespace

PluginSemaProviderRuntime::PluginSemaProviderRuntime(
    PluginTaskContext &TaskValue, PluginArtifactRegistry &ArtifactsValue,
    PluginPhaseExecutor &ExecutorValue)
    : Task(TaskValue), Artifacts(ArtifactsValue), Executor(ExecutorValue) {}

void PluginSemaProviderRuntime::attach(Sema &SemanticAnalyzerValue,
                                       PluginASTBridge &ASTValue) {
  SemanticAnalyzer = &SemanticAnalyzerValue;
  AST = &ASTValue;
}

void PluginSemaProviderRuntime::detach() {
  SemanticAnalyzer = nullptr;
  AST = nullptr;
}

NevercStatus PluginSemaProviderRuntime::getAnalyzePhaseInput(
    const NevercPhaseFrame *Frame, NevercArtifactHandle Input,
    NevercSemaPhaseInput *OutInput) {
  if (!Frame || !OutInput || !SemanticAnalyzer || !AST ||
      !sameHandle(Frame->Task, Task.handle()) ||
      !samePluginInterfaceID(Frame->Phase, semaAnalyzePhaseID()) ||
      !sameHandle(Input, Frame->Input))
    return providerStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  const void *Payload = nullptr;
  NevercStatus Status =
      Executor.resolveArtifactPayload(Task, Input, astUnitArtifactID(), &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  const auto &Unit = *static_cast<const ASTUnitArtifact *>(Payload);
  auto TranslationUnit = AST->publishDecl(Unit.TranslationUnit);
  if (!TranslationUnit) {
    consumeError(TranslationUnit.takeError());
    return providerStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }

  NevercSemaPhaseInput Value{};
  Value.Header = {sizeof(Value), NEVERC_SEMA_API_MAJOR, NEVERC_SEMA_API_MINOR,
                  0};
  Value.ASTUnit = Input;
  Value.ASTProduct = Unit.Product;
  Value.TranslationUnit = *TranslationUnit;
  Value.SemanticState = Unit.SemanticState;
  return writeCallerRecord(OutInput, Value);
}

NevercStatus PluginSemaProviderRuntime::createSemanticUnit(
    const NevercPhaseFrame *Frame,
    const NevercSemanticUnitDescriptor *Descriptor,
    NevercArtifactHandle *OutOutput) {
  if (!Frame || !Descriptor || !OutOutput || !SemanticAnalyzer || !AST ||
      !sameHandle(Frame->Task, Task.handle()) ||
      !samePluginInterfaceID(Frame->Phase, semaAnalyzePhaseID()) ||
      !validHeader(Descriptor->Header, sizeof(*Descriptor)) ||
      !nonnull(Descriptor->Product) ||
      Descriptor->SemanticComplete != NEVERC_TRUE ||
      Descriptor->Reserved[0] != 0 || Descriptor->Reserved[1] != 0 ||
      Descriptor->Reserved[2] != 0)
    return providerStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutOutput = {};

  const void *InputPayload = nullptr;
  NevercStatus Status = Executor.resolveArtifactPayload(
      Task, Frame->Input, astUnitArtifactID(), &InputPayload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  const auto &Input = *static_cast<const ASTUnitArtifact *>(InputPayload);

  const void *Native = nullptr;
  Status = AST->resolvePublishedNode(Task.handle(), Descriptor->TranslationUnit,
                                     NEVERC_AST_SCHEMA_DOMAIN_DECL, &Native);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Native != Input.TranslationUnit ||
      !isa<TranslationUnitDecl>(static_cast<const Decl *>(Native)))
    return providerStatus(NEVERC_STATUS_VERIFICATION_FAILED);

  auto *Candidate = new (std::nothrow) SemanticUnitArtifact;
  if (!Candidate)
    return providerStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  Candidate->Context = Input.Context;
  Candidate->TranslationUnit = Input.TranslationUnit;
  Candidate->Product = Descriptor->Product;
  Candidate->DiagnosticState =
      SemanticAnalyzer->getDiagnostics().hasErrorOccurred()
          ? NEVERC_SEMANTIC_DIAGNOSTICS_HAS_ERROR
          : NEVERC_SEMANTIC_DIAGNOSTICS_CLEAN;
  Candidate->VerifierSummary =
      "plugin Sema provider declared a complete semantic unit";
  Candidate->SourceIdentity = Input.SourceIdentity;
  Candidate->SourceDigest = Input.SourceDigest;
  Candidate->HasSourceDigest = Input.HasSourceDigest;
  auto Handle =
      Executor.createCandidate(Task, semanticUnitArtifactID(), Candidate);
  if (!Handle) {
    delete Candidate;
    consumeError(Handle.takeError());
    return providerStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutOutput = *Handle;
  return neverc_status_ok();
}

NevercStatus PluginSemaProviderRuntime::getSemanticUnitInfo(
    const NevercPhaseFrame *Frame, NevercArtifactHandle Unit,
    NevercSemanticUnitInfo *OutInfo) {
  if (!Frame || !OutInfo || !AST ||
      !sameHandle(Frame->Task, Task.handle()) ||
      !samePluginInterfaceID(Frame->Phase, semaAnalyzePhaseID()))
    return providerStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  const void *Payload = nullptr;
  NevercStatus Status = Executor.resolveArtifactPayload(
      Task, Unit, semanticUnitArtifactID(), &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  const auto &Value = *static_cast<const SemanticUnitArtifact *>(Payload);
  auto TranslationUnit = AST->publishDecl(Value.TranslationUnit);
  if (!TranslationUnit) {
    consumeError(TranslationUnit.takeError());
    return providerStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }

  NevercSemanticUnitInfo Info{};
  Info.Header = {sizeof(Info), NEVERC_SEMA_API_MAJOR, NEVERC_SEMA_API_MINOR, 0};
  Info.Product = Value.Product;
  Info.TranslationUnit = *TranslationUnit;
  Info.DiagnosticState = Value.DiagnosticState;
  Info.Replayed = Value.Replayed ? NEVERC_TRUE : NEVERC_FALSE;
  Info.VerifierSummary = stringView(Value.VerifierSummary);
  Info.SourceIdentity = stringView(Value.SourceIdentity);
  Info.SourceDigest = {Value.SourceDigest.data(),
                       Value.HasSourceDigest ? Value.SourceDigest.size() : 0};
  return writeCallerRecord(OutInfo, Info);
}

NevercStatus PluginSemaProviderRuntime::builtinProvider(
    const NevercPhaseFrame *Frame, NevercPhaseResult *Result) {
  FailureMessage.clear();
  if (!Frame || !Result || !SemanticAnalyzer || !AST)
    return providerStatus(NEVERC_STATUS_INVALID_STATE);
  const void *Payload = nullptr;
  NevercStatus Status = Executor.resolveArtifactPayload(
      Task, Frame->Input, astUnitArtifactID(), &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto &Input =
      *const_cast<ASTUnitArtifact *>(static_cast<const ASTUnitArtifact *>(
          Payload));
  if (!samePluginInterfaceID(Input.Product, standardASTProductID())) {
    FailureMessage =
        "builtin Sema accepts only the standard AST product";
    return providerStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
  }

  bool Replayed = false;
  std::string Summary = "fused parser and Sema output; replay skipped";
  if (Input.SemanticState == NEVERC_AST_REQUIRES_SEMA_REPLAY) {
    SemaReplayResult Replay =
        SemaReplay::run(*SemanticAnalyzer, *Input.TranslationUnit);
    if (!Replay) {
      FailureMessage = Replay.Message;
      return providerStatus(
          Replay.Status == SemaReplayStatus::UnsupportedASTKind
              ? NEVERC_STATUS_CAPABILITY_UNAVAILABLE
              : NEVERC_STATUS_VERIFICATION_FAILED);
    }
    Replayed = true;
    Summary = std::move(Replay.Message);
  } else if (Input.SemanticState !=
             NEVERC_AST_UNIT_SEMANTICALLY_ANALYZED) {
    FailureMessage = "AST unit has an invalid semantic state";
    return providerStatus(NEVERC_STATUS_VERIFICATION_FAILED);
  }

  auto *Candidate = new (std::nothrow) SemanticUnitArtifact;
  if (!Candidate)
    return providerStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  Candidate->Context = Input.Context;
  Candidate->TranslationUnit = Input.TranslationUnit;
  Candidate->Product = standardSemanticProductID();
  Candidate->DiagnosticState =
      SemanticAnalyzer->getDiagnostics().hasErrorOccurred()
          ? NEVERC_SEMANTIC_DIAGNOSTICS_HAS_ERROR
          : NEVERC_SEMANTIC_DIAGNOSTICS_CLEAN;
  Candidate->Replayed = Replayed;
  Candidate->VerifierSummary = std::move(Summary);
  Candidate->SourceIdentity = Input.SourceIdentity;
  Candidate->SourceDigest = Input.SourceDigest;
  Candidate->HasSourceDigest = Input.HasSourceDigest;
  auto Handle =
      Executor.createCandidate(Task, semanticUnitArtifactID(), Candidate);
  if (!Handle) {
    delete Candidate;
    consumeError(Handle.takeError());
    return providerStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  Input.SemanticState = NEVERC_AST_UNIT_SEMANTICALLY_ANALYZED;

  *Result = {};
  Result->Header = {sizeof(*Result), NEVERC_PLUGIN_ABI_MAJOR,
                    NEVERC_PLUGIN_ABI_MINOR, 0};
  Result->Action = NEVERC_PHASE_REPLACE;
  Result->Output = *Handle;
  return neverc_status_ok();
}

} // namespace neverc::plugin
