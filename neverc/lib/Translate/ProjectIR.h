#ifndef NEVERC_TRANSLATE_PROJECTIR_H
#define NEVERC_TRANSLATE_PROJECTIR_H

#include "TranslateIR.h"
#include "llvm/ADT/ArrayRef.h"

namespace neverc::translate {

struct FunctionDeclaration {
  std::string Name, SemanticID;
  Type Result;
  std::vector<Variable> Params;
  bool Internal = false, CExport = false, Inline = false;
  SourceLocation Loc;
};
struct GlobalDeclaration {
  std::string Name, SemanticID;
  Type ValueType;
  bool Internal = true;
  SourceLocation Loc;
};
enum class EntityKind { Record, Function, Global };
struct ODREvidence {
  EntityKind Kind = EntityKind::Record;
  std::string Name, SemanticID, OwnerTU;
  bool Inline = false;
  SourceLocation Origin;
  std::string TokensSHA256, BindingsSHA256;
};
struct ProjectUnit {
  Module Definitions;
  std::string TranslationUnit, ConfigurationID;
  std::vector<FunctionDeclaration> FunctionDeclarations;
  std::vector<GlobalDeclaration> GlobalDeclarations;
  std::vector<ODREvidence> ODR;
};
struct ProjectEntity {
  ODREvidence Representative;
  // Parallel vectors, sorted by translation unit; every accepted definition is
  // retained.
  std::vector<SourceLocation> Origins;
  std::vector<std::string> TranslationUnits;
};
struct ProjectUnitIdentity {
  std::string TranslationUnit, ConfigurationID;
};
struct MergedProject {
  Module Definitions;
  std::vector<ProjectUnitIdentity> Units;
  std::vector<ProjectEntity> Entities;
};
struct EmittedFile {
  std::string RelativePath, Text;
  std::vector<SourceMapEntry> Map;
};
struct EmittedProject {
  std::vector<EmittedFile> Files;
};

// Like the core API, these functions own their output and never access the
// filesystem.
bool parseProjectUnit(llvm::StringRef JSON, ProjectUnit &Out, Diagnostics &D);
bool verifyProjectUnit(const ProjectUnit &U, const VerificationContext &Context,
                       Diagnostics &D);
bool mergeProjectUnits(llvm::ArrayRef<ProjectUnit> Units,
                       const VerificationContext &Context, MergedProject &Out,
                       Diagnostics &D);
bool verifyMergedProject(const MergedProject &P,
                         const VerificationContext &Context, Diagnostics &D);
bool emitProject(const MergedProject &P, const VerificationContext &Context,
                 EmittedProject &Out, Diagnostics &D);

} // namespace neverc::translate
#endif
