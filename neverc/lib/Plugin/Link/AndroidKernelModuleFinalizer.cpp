#include "AndroidKernelModuleFinalizer.h"
#include "neverc/Foundation/AndroidKernelProfileContract.h"
#include "neverc/Merge/Merger.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/MemoryBufferRef.h"

using namespace llvm;

namespace neverc::plugin {
namespace {

bool isDebugSectionName(StringRef Name) {
  return Name == ".debug" || Name.starts_with(".debug_") ||
         Name.starts_with(".zdebug_");
}

bool isDebugSection(const PluginObjectSection &Section) {
  return Section.Kind == NEVERC_OBJECT_SECTION_KIND_DEBUG ||
         (Section.Flags & NEVERC_OBJECT_SECTION_DEBUG) != 0 ||
         isDebugSectionName(Section.Name);
}

bool isPCGDefinition(const PluginObjectSymbol &Symbol) {
  return Symbol.Definition != NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED &&
         Symbol.Definition != NEVERC_OBJECT_SYMBOL_DEFINITION_COMMON &&
         StringRef(Symbol.Name).contains(neverc::merge::PcgSymbolMarker);
}

bool isStripCandidate(const PluginObjectSymbol &Symbol) {
  return Symbol.Binding == NEVERC_OBJECT_SYMBOL_BINDING_LOCAL ||
         Symbol.Definition == NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED ||
         isPCGDefinition(Symbol);
}

Error invalid(StringRef Boundary, const Twine &Message) {
  return createStringError(errc::invalid_argument,
                           (Boundary + ": " + Message).str());
}

} // namespace

Error finalizeAndroidKernelModuleObjectGraph(
    PluginObjectGraph &Object, AndroidKernelModuleFinalizationPolicy Policy,
    StringRef Boundary) {
  if (Error E = verifyPluginObjectGraph(Object))
    return joinErrors(invalid(Boundary, "invalid ObjectGraph before finalization"),
                      std::move(E));

  DenseSet<uint64_t> DroppedSections;
  for (const PluginObjectSection &Section : Object.sections()) {
    const bool ToolingContract =
        Section.Name == AndroidKernelProfileContract::NativeSection;
    const bool Debug = Policy.DropDebugInfo && isDebugSection(Section);
    const bool ProducerComment =
        Policy.StripUnneededSymbols && Section.Name == ".comment";
    if (ToolingContract || Debug || ProducerComment)
      DroppedSections.insert(Section.ID);
  }

  // Only relocations applied in retained sections make their targets live.
  // Relocations inside a discarded debug/tooling section disappear with it.
  DenseSet<uint64_t> ReferencedSymbols;
  for (const PluginObjectRelocation &Relocation : Object.relocations())
    if (!DroppedSections.contains(Relocation.SectionID) &&
        Relocation.TargetKind == NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL)
      ReferencedSymbols.insert(Relocation.TargetSymbolID);

  DenseSet<uint64_t> DroppedSymbols;
  DenseSet<uint64_t> DemotedPCGSymbols;
  for (const PluginObjectSymbol &Symbol : Object.symbols()) {
    if (Symbol.Name == AndroidKernelProfileContract::NativeSymbol ||
        DroppedSections.contains(Symbol.SectionID)) {
      DroppedSymbols.insert(Symbol.ID);
      continue;
    }
    if (Policy.StripUnneededSymbols && isStripCandidate(Symbol) &&
        !ReferencedSymbols.contains(Symbol.ID)) {
      DroppedSymbols.insert(Symbol.ID);
      continue;
    }
    if (Policy.StripUnneededSymbols && isPCGDefinition(Symbol) &&
        Symbol.Binding != NEVERC_OBJECT_SYMBOL_BINDING_LOCAL)
      DemotedPCGSymbols.insert(Symbol.ID);
  }

  // Validate the complete discard set before the first mutation.  This makes
  // failure atomic: a provider cannot leave a half-finalized graph behind.
  for (const PluginObjectRelocation &Relocation : Object.relocations()) {
    if (DroppedSections.contains(Relocation.SectionID))
      continue;
    const bool TargetsDroppedEntity =
        (Relocation.TargetKind == NEVERC_OBJECT_RELOCATION_TARGET_SECTION &&
         DroppedSections.contains(Relocation.TargetSectionID)) ||
        (Relocation.TargetKind == NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL &&
         DroppedSymbols.contains(Relocation.TargetSymbolID));
    if (TargetsDroppedEntity)
      return invalid(Boundary,
                     "retained section references an entity selected for "
                     "Android module release removal");
  }

  bool Changed = !DroppedSections.empty() || !DroppedSymbols.empty() ||
                 !DemotedPCGSymbols.empty();
  if (!Changed)
    return Error::success();

  Object.relocations().remove_if([&](const PluginObjectRelocation &Relocation) {
    return DroppedSections.contains(Relocation.SectionID);
  });
  Object.symbols().remove_if([&](const PluginObjectSymbol &Symbol) {
    return DroppedSymbols.contains(Symbol.ID);
  });
  for (PluginObjectSymbol &Symbol : Object.symbols())
    if (DemotedPCGSymbols.contains(Symbol.ID))
      Symbol.Binding = NEVERC_OBJECT_SYMBOL_BINDING_LOCAL;
  Object.sections().remove_if([&](const PluginObjectSection &Section) {
    return DroppedSections.contains(Section.ID);
  });
  Object.advanceGeneration();
  return Error::success();
}

Error verifyFinalAndroidKernelModuleObjectGraph(
    const PluginObjectGraph &Object,
    AndroidKernelModuleFinalizationPolicy Policy, StringRef Boundary) {
  if (Error E = verifyPluginObjectGraph(Object))
    return joinErrors(invalid(Boundary, "invalid finalized ObjectGraph"),
                      std::move(E));

  for (const PluginObjectSection &Section : Object.sections()) {
    if (Section.Name == AndroidKernelProfileContract::NativeSection)
      return invalid(Boundary,
                     "must not retain native Android kernel profile contract "
                     "section");
    if (Policy.DropDebugInfo && isDebugSection(Section))
      return invalid(Boundary,
                     "retains debug section '" + Twine(Section.Name) + "'");
    if (Policy.StripUnneededSymbols && Section.Name == ".comment")
      return invalid(Boundary, "retains producer .comment metadata");
  }

  DenseSet<uint64_t> ReferencedSymbols;
  for (const PluginObjectRelocation &Relocation : Object.relocations())
    if (Relocation.TargetKind == NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL)
      ReferencedSymbols.insert(Relocation.TargetSymbolID);

  for (const PluginObjectSymbol &Symbol : Object.symbols()) {
    if (Symbol.Name == AndroidKernelProfileContract::NativeSymbol)
      return invalid(Boundary,
                     "must not retain native Android kernel profile contract "
                     "symbol");
    if (!Policy.StripUnneededSymbols)
      continue;
    if (isPCGDefinition(Symbol) &&
        Symbol.Binding != NEVERC_OBJECT_SYMBOL_BINDING_LOCAL)
      return invalid(Boundary, "retains a globally visible PCG symbol '" +
                                   Twine(Symbol.Name) + "'");
    if (isStripCandidate(Symbol) &&
        !ReferencedSymbols.contains(Symbol.ID))
      return invalid(Boundary, "retains relocation-unneeded symbol '" +
                                   Twine(Symbol.Name) + "'");
  }
  return Error::success();
}

Error verifyFinalAndroidKernelModuleImage(
    ArrayRef<uint8_t> Image, AndroidKernelModuleFinalizationPolicy Policy,
    StringRef Boundary) {
  merge::Options AuditOptions;
  AuditOptions.androidKernelModule = true;
  AuditOptions.finalizeAndroidKernelModule = true;
  AuditOptions.dropDebugInfo = Policy.DropDebugInfo;
  AuditOptions.stripUnneededSymbols = Policy.StripUnneededSymbols;
  std::string AuditError;
  if (!merge::verifyAndroidKernelModuleImage(
          ArrayRef<char>(reinterpret_cast<const char *>(Image.data()),
                         Image.size()),
          AuditOptions, &AuditError))
    return invalid(Boundary, AuditError);

  auto Object = object::ObjectFile::createObjectFile(MemoryBufferRef(
      StringRef(reinterpret_cast<const char *>(Image.data()), Image.size()),
      Boundary));
  if (!Object)
    return Object.takeError();

  for (const object::SymbolRef &Symbol : (*Object)->symbols()) {
    auto Name = Symbol.getName();
    if (!Name)
      return Name.takeError();
    if (!Policy.StripUnneededSymbols)
      continue;
    auto Flags = Symbol.getFlags();
    if (!Flags)
      return Flags.takeError();
    const bool Undefined = (*Flags & object::SymbolRef::SF_Undefined) != 0;
    const bool NonLocal =
        (*Flags & (object::SymbolRef::SF_Global |
                   object::SymbolRef::SF_Weak)) != 0;
    const bool PCGDefinition =
        !Undefined && Name->contains(neverc::merge::PcgSymbolMarker);
    if (PCGDefinition && NonLocal)
      return invalid(Boundary, "retains a globally visible PCG symbol '" +
                                   Twine(*Name) + "'");
  }
  return Error::success();
}

} // namespace neverc::plugin
