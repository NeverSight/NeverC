#include "ASTUnitArtifact.h"
#include "neverc/Plugin/Host/PluginArtifactRegistry.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Decl/Decl.h"
#include <new>

using namespace llvm;

namespace neverc::plugin {
namespace {

Expected<void *> cloneASTUnit(const void *Payload) {
  if (!Payload)
    return createStringError(inconvertibleErrorCode(),
                             "AST unit payload is null");
  auto *Clone = new (std::nothrow)
      ASTUnitArtifact(*static_cast<const ASTUnitArtifact *>(Payload));
  if (!Clone)
    return createStringError(inconvertibleErrorCode(),
                             "AST unit allocation failed");
  return static_cast<void *>(Clone);
}

Error verifyASTUnit(const void *Payload) {
  if (!Payload)
    return createStringError(inconvertibleErrorCode(),
                             "AST unit payload is null");
  const auto &Unit = *static_cast<const ASTUnitArtifact *>(Payload);
  if (!Unit.Context || !Unit.TranslationUnit ||
      Unit.Context->getTranslationUnitDecl() != Unit.TranslationUnit)
    return createStringError(inconvertibleErrorCode(),
                             "AST unit has no matching translation-unit root");
  if (Unit.SourceIdentity.empty() || !Unit.HasSourceDigest)
    return createStringError(inconvertibleErrorCode(),
                             "AST unit has no source provenance");
  NevercInterfaceID Standard = standardASTProductID();
  if (Unit.Product.High != Standard.High || Unit.Product.Low != Standard.Low)
    return createStringError(inconvertibleErrorCode(),
                             "AST unit product is unsupported");
  if (Unit.SemanticState != NEVERC_AST_UNIT_UNANALYZED &&
      Unit.SemanticState != NEVERC_AST_UNIT_SEMANTICALLY_ANALYZED)
    return createStringError(inconvertibleErrorCode(),
                             "AST unit semantic state is invalid");
  return Error::success();
}

} // namespace

NevercInterfaceID syntaxParsePhaseID() {
  return {NEVERC_PHASE_SYNTAX_PARSE_HIGH, NEVERC_PHASE_SYNTAX_PARSE_LOW};
}

NevercInterfaceID astUnitArtifactID() {
  return {NEVERC_PHASE_SYNTAX_PARSE_OUTPUT_HIGH,
          NEVERC_PHASE_SYNTAX_PARSE_OUTPUT_LOW};
}

NevercInterfaceID standardASTProductID() {
  return {NEVERC_AST_PRODUCT_STANDARD_HIGH, NEVERC_AST_PRODUCT_STANDARD_LOW};
}

Error registerASTUnitArtifactType(PluginArtifactRegistry &Artifacts) {
  auto Type = Artifacts.registerType(
      {astUnitArtifactID(), "syntax.ast_unit", PluginArtifactOwnership::Owned,
       cloneASTUnit,
       [](void *Payload) { delete static_cast<ASTUnitArtifact *>(Payload); },
       verifyASTUnit});
  if (!Type)
    return Type.takeError();
  return Error::success();
}

} // namespace neverc::plugin
