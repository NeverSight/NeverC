#ifndef NEVERC_PLUGIN_FRONTEND_SEMANTICUNITARTIFACT_H
#define NEVERC_PLUGIN_FRONTEND_SEMANTICUNITARTIFACT_H

#include "neverc/Plugin/PluginSema.h"
#include "llvm/Support/Error.h"
#include <array>
#include <string>

namespace neverc {
class TranslationUnitDecl;
class TreeContext;

namespace plugin {
class PluginArtifactRegistry;

struct SemanticUnitArtifact {
  TreeContext *Context = nullptr;
  TranslationUnitDecl *TranslationUnit = nullptr;
  NevercInterfaceID Product{};
  NevercSemanticDiagnosticState DiagnosticState =
      NEVERC_SEMANTIC_DIAGNOSTICS_CLEAN;
  bool Replayed = false;
  std::string VerifierSummary;
  std::string SourceIdentity;
  std::array<uint8_t, 32> SourceDigest{};
  bool HasSourceDigest = false;
};

NevercInterfaceID semaAnalyzePhaseID();
NevercInterfaceID semanticUnitArtifactID();
NevercInterfaceID standardSemanticProductID();
llvm::Error
registerSemanticUnitArtifactType(PluginArtifactRegistry &Artifacts);

} // namespace plugin
} // namespace neverc

#endif
