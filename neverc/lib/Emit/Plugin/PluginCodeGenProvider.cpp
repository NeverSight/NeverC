#include "neverc/Plugin/Host/PluginCodeGenProvider.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/Support/Error.h"
#include <cstddef>

using namespace llvm;

namespace neverc::plugin {
namespace {

Error providerError(const Twine &Message) {
  return createStringError(inconvertibleErrorCode(), Message);
}

bool nonzero(NevercInterfaceID ID) {
  return ID.High != 0 || ID.Low != 0;
}

bool sameID(NevercInterfaceID Left, NevercInterfaceID Right) {
  return Left.High == Right.High && Left.Low == Right.Low;
}

bool nonnull(NevercHandle Handle) {
  return Handle.Owner != 0 && Handle.Value != 0;
}

bool canonicalNull(NevercHandle Handle) {
  return Handle.Owner == 0 && Handle.Value == 0;
}

Error validateStatus(NevercStatus Status, StringRef Callback) {
  if (Status.Code == NEVERC_STATUS_OK) {
    if (Status.Flags == 0 && Status.Detail == 0)
      return Error::success();
    return providerError(Callback + " returned invalid success metadata");
  }
  return providerError(Callback + " failed with status code " +
                       Twine(Status.Code));
}

Error validateCandidate(
    const PluginTargetSnapshot::CodeGenEdgeRecord &Edge,
    const NevercCodeGenProductCandidate &Candidate) {
  constexpr size_t Required =
      offsetof(NevercCodeGenProductCandidate, Flags) +
      sizeof(NevercCodeGenProductCandidate::Flags);
  if (Candidate.Header.StructSize < Required ||
      Candidate.Header.Major != NEVERC_TARGET_API_MAJOR ||
      Candidate.Header.Minor > NEVERC_TARGET_API_MINOR ||
      Candidate.Header.Flags != 0 || Candidate.Reserved != 0 ||
      Candidate.Flags != 0)
    return providerError("codegen provider returned malformed product");
  if (Candidate.Kind != Edge.OutputKind)
    return providerError("codegen provider returned wrong product kind");
  if (!nonnull(Candidate.Artifact))
    return providerError("codegen provider returned a null artifact");
  if (!nonzero(Candidate.ProductID) ||
      !sameID(Candidate.ProductID, Edge.ProductID))
    return providerError("codegen provider returned wrong product ID");
  if (!canonicalNull(Candidate.Proof))
    return providerError(
        "codegen provider returned a plugin-supplied proof");
  return Error::success();
}

} // namespace

Expected<VerifiedCodeGenProduct>
PluginCodeGenProviderRuntime::execute(
    const PlannedCodeGenRoute &Route,
    const CodeGenExecutionRequest &Request, BuiltinLower Builtin) {
  if (Route.edges().empty())
    return providerError("codegen route is empty");
  if (Route.compatibilityKey() != Request.CompatibilityKey)
    return providerError("codegen route compatibility key mismatch");
  if (Route.edges().front()->InputKind != Request.InputKind ||
      Route.edges().back()->OutputKind != Request.OutputKind)
    return providerError("codegen route endpoints do not match request");

  CodeGenExecutionRequest Current = Request;
  VerifiedCodeGenProduct Result;
  for (const auto *Edge : Route.edges()) {
    if (!Edge || Edge->InputKind != Current.InputKind)
      return providerError("codegen route has a discontinuous edge");
    if (!Edge->CompatibilityKey.empty() &&
        Edge->CompatibilityKey != Current.CompatibilityKey)
      return providerError("codegen edge compatibility key mismatch");
    const bool Coarse =
        (Edge->Flags & NEVERC_CODEGEN_EDGE_COARSE) != 0;
    if (Coarse && Edge->InputKind == NEVERC_CODEGEN_PRODUCT_IR &&
        !Current.HasFinalIRProof)
      return providerError(
          "coarse codegen provider requires a final IR proof");

    NevercCodeGenRequest CallbackRequest{};
    CallbackRequest.Header = {sizeof(CallbackRequest),
                              NEVERC_TARGET_API_MAJOR,
                              NEVERC_TARGET_API_MINOR, 0};
    CallbackRequest.Target = Current.Target;
    CallbackRequest.Input = Current.Input;
    CallbackRequest.InputKind = Current.InputKind;
    CallbackRequest.OutputKind = Edge->OutputKind;
    CallbackRequest.OptimizationLevel = Current.OptimizationLevel;
    CallbackRequest.HasFinalIRProof =
        Current.HasFinalIRProof ? NEVERC_TRUE : NEVERC_FALSE;

    NevercCodeGenProductCandidate Candidate{};
    bool UsedBuiltin = false;
    if (Edge->CoarseLower) {
      Candidate.Header = {sizeof(Candidate), NEVERC_TARGET_API_MAJOR,
                          NEVERC_TARGET_API_MINOR, 0};
      auto Invoke = [&] {
        return Edge->CoarseLower(
            Edge->CallbackUserData, Current.Task, &CallbackRequest,
            &Candidate);
      };
      NevercStatus Status{};
      if (Current.TaskContext) {
        auto Invoked = Current.TaskContext->invokeCallback(
            Edge->PluginID, "codegen/coarse-lower", Invoke);
        if (!Invoked)
          return Invoked.takeError();
        Status = *Invoked;
      } else {
        Status = Invoke();
      }
      if (Error E = validateStatus(Status, "coarse codegen callback"))
        return std::move(E);
    } else {
      if (!Builtin)
        return providerError("codegen edge has no lowering provider");
      auto Lowered = Builtin(*Edge, Current);
      if (!Lowered)
        return Lowered.takeError();
      Candidate = std::move(*Lowered);
      UsedBuiltin = true;
    }
    if (Error E = validateCandidate(*Edge, Candidate))
      return std::move(E);

    NevercCodeGenVerificationObligations Obligations =
        NEVERC_CODEGEN_VERIFY_TARGET_KEY |
        NEVERC_CODEGEN_VERIFY_PRODUCT_KIND |
        NEVERC_CODEGEN_VERIFY_PRODUCT_ID |
        NEVERC_CODEGEN_VERIFY_STRUCTURE;
    if (Coarse && Edge->InputKind == NEVERC_CODEGEN_PRODUCT_IR)
      Obligations |= NEVERC_CODEGEN_VERIFY_FINAL_IR;
    if (Edge->VerifyProduct) {
      auto Invoke = [&] {
        return Edge->VerifyProduct(
            Edge->CallbackUserData, Current.Task, &CallbackRequest,
            &Candidate, Obligations);
      };
      NevercStatus Status{};
      if (Current.TaskContext) {
        auto Invoked = Current.TaskContext->invokeCallback(
            Edge->PluginID, "codegen/verify-product", Invoke);
        if (!Invoked)
          return Invoked.takeError();
        Status = *Invoked;
      } else {
        Status = Invoke();
      }
      if (Error E =
              validateStatus(Status, "codegen product verifier"))
        return std::move(E);
    } else if (!UsedBuiltin) {
      return providerError(
          "plugin codegen product bypassed mandatory verifier");
    }

    Result.Candidate = Candidate;
    Result.HostVerified = true;
    Current.Input = Candidate.Artifact;
    Current.InputKind = Candidate.Kind;
    Current.HasFinalIRProof =
        Candidate.Kind == NEVERC_CODEGEN_PRODUCT_IR;
  }
  return Result;
}

} // namespace neverc::plugin
