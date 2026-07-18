#include "SemanticUnitArtifact.h"
#include "neverc/Plugin/Host/PluginArtifactRegistry.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Decl/Decl.h"
#include <new>

using namespace llvm;

namespace neverc::plugin {
namespace {

bool nonnull(NevercInterfaceID ID) { return ID.High != 0 || ID.Low != 0; }

Expected<void *> cloneSemanticUnit(const void *Payload) {
  if (!Payload)
    return createStringError(inconvertibleErrorCode(),
                             "semantic unit payload is null");
  auto *Clone = new (std::nothrow)
      SemanticUnitArtifact(*static_cast<const SemanticUnitArtifact *>(Payload));
  if (!Clone)
    return createStringError(inconvertibleErrorCode(),
                             "semantic unit allocation failed");
  return static_cast<void *>(Clone);
}

Error verifySemanticUnit(const void *Payload) {
  if (!Payload)
    return createStringError(inconvertibleErrorCode(),
                             "semantic unit payload is null");
  const auto &Unit = *static_cast<const SemanticUnitArtifact *>(Payload);
  if (!Unit.Context || !Unit.TranslationUnit ||
      Unit.Context->getTranslationUnitDecl() != Unit.TranslationUnit)
    return createStringError(
        inconvertibleErrorCode(),
        "semantic unit has no matching translation-unit root");
  if (!nonnull(Unit.Product))
    return createStringError(inconvertibleErrorCode(),
                             "semantic unit has no product ID");
  if (Unit.DiagnosticState != NEVERC_SEMANTIC_DIAGNOSTICS_CLEAN &&
      Unit.DiagnosticState != NEVERC_SEMANTIC_DIAGNOSTICS_HAS_ERROR)
    return createStringError(inconvertibleErrorCode(),
                             "semantic unit diagnostic state is invalid");
  if (Unit.VerifierSummary.empty())
    return createStringError(inconvertibleErrorCode(),
                             "semantic unit has no verifier summary");
  if (Unit.SourceIdentity.empty() || !Unit.HasSourceDigest)
    return createStringError(inconvertibleErrorCode(),
                             "semantic unit has no source provenance");
  return Error::success();
}

} // namespace

NevercInterfaceID semaAnalyzePhaseID() {
  return {NEVERC_PHASE_SEMA_ANALYZE_HIGH, NEVERC_PHASE_SEMA_ANALYZE_LOW};
}

NevercInterfaceID semanticUnitArtifactID() {
  return {NEVERC_PHASE_SEMA_ANALYZE_OUTPUT_HIGH,
          NEVERC_PHASE_SEMA_ANALYZE_OUTPUT_LOW};
}

NevercInterfaceID standardSemanticProductID() {
  return {NEVERC_SEMANTIC_PRODUCT_STANDARD_HIGH,
          NEVERC_SEMANTIC_PRODUCT_STANDARD_LOW};
}

Error registerSemanticUnitArtifactType(PluginArtifactRegistry &Artifacts) {
  auto Type = Artifacts.registerType(
      {semanticUnitArtifactID(), "sema.semantic_unit",
       PluginArtifactOwnership::Owned, cloneSemanticUnit,
       [](void *Payload) {
         delete static_cast<SemanticUnitArtifact *>(Payload);
       },
       verifySemanticUnit});
  if (!Type)
    return Type.takeError();
  return Error::success();
}

} // namespace neverc::plugin
