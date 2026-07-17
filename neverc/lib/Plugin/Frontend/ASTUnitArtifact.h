#ifndef NEVERC_PLUGIN_FRONTEND_ASTUNITARTIFACT_H
#define NEVERC_PLUGIN_FRONTEND_ASTUNITARTIFACT_H

#include "neverc/Plugin/PluginAST.h"
#include "llvm/Support/Error.h"
#include <array>
#include <string>

namespace neverc {
class TranslationUnitDecl;
class TreeContext;

namespace plugin {
class PluginArtifactRegistry;

struct ASTUnitArtifact {
  TreeContext *Context = nullptr;
  TranslationUnitDecl *TranslationUnit = nullptr;
  NevercInterfaceID Product{};
  NevercASTUnitSemanticState SemanticState = NEVERC_AST_UNIT_UNANALYZED;
  std::string SourceIdentity;
  std::array<uint8_t, 32> SourceDigest{};
  bool HasSourceDigest = false;
  bool ConsumerNotified = false;
};

NevercInterfaceID syntaxParsePhaseID();
NevercInterfaceID astUnitArtifactID();
NevercInterfaceID standardASTProductID();
llvm::Error registerASTUnitArtifactType(PluginArtifactRegistry &Artifacts);

} // namespace plugin
} // namespace neverc

#endif
