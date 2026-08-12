#include "AndroidKernelModuleFinalizer.h"
#include "../AndroidKernelReleaseWriterPolicy.h"
#include "Object/BuiltinObjectWriterPreflight.h"
#include "neverc/Foundation/AndroidKernelModuleReleaseNames.h"
#include "neverc/Foundation/AndroidKernelModuleSymbolPolicy.h"
#include "neverc/Foundation/AndroidKernelProfileContract.h"
#include "neverc/Foundation/ELFDebugSectionPolicy.h"
#include "neverc/Merge/Merger.h"
#include "neverc/Plugin/Host/BuiltinObjectExtension.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/Support/SHA256.h"
#include "llvm/TargetParser/Triple.h"

#include <limits>
#include <optional>

using namespace llvm;

namespace neverc::plugin {
namespace {

bool isDebugSection(const PluginObjectSection &Section) {
  return Section.Kind == NEVERC_OBJECT_SECTION_KIND_DEBUG ||
         (Section.Flags & NEVERC_OBJECT_SECTION_DEBUG) != 0 ||
         ELFDebugSectionPolicy::isDebugSectionName(Section.Name);
}

bool isAllocatedDebugSection(const PluginObjectSection &Section) {
  return isDebugSection(Section) &&
         (Section.Flags & NEVERC_OBJECT_SECTION_ALLOCATED) != 0;
}

bool isUnsupportedLivePatchSection(const PluginObjectSection &Section) {
  using namespace AndroidKernelModuleSymbolPolicy;
  if (isLivePatchSectionName(Section.Name))
    return true;
  if (!builtinext::hasTag(Section.Extension.Bytes, builtinext::SectionTag) ||
      builtinext::version(Section.Extension.Bytes) < 1)
    return false;
  const std::optional<uint64_t> NativeFlags =
      builtinext::field(Section.Extension.Bytes, builtinext::SectionFlags);
  return NativeFlags && (*NativeFlags & LivePatchRelocationSectionFlag) != 0;
}

bool marksLivePatchModule(const PluginObjectSection &Section) {
  return Section.Name == ".modinfo" &&
         AndroidKernelModuleSymbolPolicy::containsLivePatchModInfo(
             Section.Data);
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

Expected<AndroidKernelModuleSymbolPolicy::SymbolClass>
releaseSymbolClass(const PluginObjectSymbol &Symbol, StringRef Boundary) {
  using SymbolClass = AndroidKernelModuleSymbolPolicy::SymbolClass;
  switch (Symbol.Definition) {
  case NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED:
    return SymbolClass::Defined;
  case NEVERC_OBJECT_SYMBOL_DEFINITION_ABSOLUTE:
    return SymbolClass::Absolute;
  case NEVERC_OBJECT_SYMBOL_DEFINITION_COMMON:
    return SymbolClass::Common;
  case NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED:
    return SymbolClass::Undefined;
  }
  return invalid(Boundary, "unknown ObjectGraph symbol definition " +
                               Twine(Symbol.Definition));
}

Expected<ReleaseSymbolType> releaseSymbolType(const PluginObjectSymbol &Symbol,
                                              StringRef Boundary) {
  switch (Symbol.Type) {
  case NEVERC_OBJECT_SYMBOL_TYPE_NO_TYPE:
    return ReleaseSymbolType::NoType;
  case NEVERC_OBJECT_SYMBOL_TYPE_OBJECT:
    return ReleaseSymbolType::Object;
  case NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION:
    return ReleaseSymbolType::Function;
  case NEVERC_OBJECT_SYMBOL_TYPE_SECTION:
    return ReleaseSymbolType::Section;
  case NEVERC_OBJECT_SYMBOL_TYPE_TLS:
    return ReleaseSymbolType::TLS;
  case NEVERC_OBJECT_SYMBOL_TYPE_FILE:
    return ReleaseSymbolType::File;
  case NEVERC_OBJECT_SYMBOL_TYPE_INDIRECT_FUNCTION:
    return ReleaseSymbolType::GNUIFunc;
  case NEVERC_OBJECT_SYMBOL_TYPE_FORMAT_EXTENSION:
    return ReleaseSymbolType::FormatExtension;
  }
  return invalid(Boundary,
                 "unknown ObjectGraph symbol type " + Twine(Symbol.Type));
}

Expected<uint32_t> releaseBindingRank(NevercObjectSymbolBinding Binding,
                                      StringRef Boundary) {
  switch (Binding) {
  case NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL:
    return 0;
  case NEVERC_OBJECT_SYMBOL_BINDING_WEAK:
    return 1;
  case NEVERC_OBJECT_SYMBOL_BINDING_LOCAL:
    return 2;
  case NEVERC_OBJECT_SYMBOL_BINDING_UNIQUE:
    // Match ELF STB_GNU_UNIQUE's position after the three canonical bindings;
    // never order by the plugin schema's deliberately opaque wire value.
    return 13;
  case NEVERC_OBJECT_SYMBOL_BINDING_FORMAT_EXTENSION:
    return invalid(Boundary, "format-extension ObjectGraph symbol binding is "
                             "unsupported for Android release naming");
  }
  return invalid(Boundary,
                 "unknown ObjectGraph symbol binding " + Twine(Binding));
}

Expected<uint32_t>
releaseStableVisibility(NevercObjectSymbolVisibility Visibility,
                        StringRef Boundary) {
  switch (Visibility) {
  case NEVERC_OBJECT_SYMBOL_VISIBILITY_DEFAULT:
    return 0;
  case NEVERC_OBJECT_SYMBOL_VISIBILITY_INTERNAL:
    return 1;
  case NEVERC_OBJECT_SYMBOL_VISIBILITY_HIDDEN:
    return 2;
  case NEVERC_OBJECT_SYMBOL_VISIBILITY_PROTECTED:
    return 3;
  case NEVERC_OBJECT_SYMBOL_VISIBILITY_FORMAT_EXTENSION:
    return invalid(Boundary,
                   "format-extension ObjectGraph symbol visibility is "
                   "unsupported for Android release naming");
  }
  return invalid(Boundary,
                 "unknown ObjectGraph symbol visibility " + Twine(Visibility));
}

struct ReleaseSymbolOrderingFacts {
  ReleaseSymbolType Type = ReleaseSymbolType::NoType;
  uint64_t Size = 0;
  uint32_t BindingRank = 0;
  uint32_t OtherValue = 0;
  bool OriginalNameEmpty = false;
};

Expected<ReleaseSymbolOrderingFacts>
stableReleaseSymbolOrderingFacts(const PluginObjectSymbol &Symbol,
                                 NevercObjectSymbolBinding Binding,
                                 StringRef Boundary) {
  auto Type = releaseSymbolType(Symbol, Boundary);
  if (!Type)
    return Type.takeError();
  auto BindingRank = releaseBindingRank(Binding, Boundary);
  if (!BindingRank)
    return BindingRank.takeError();
  auto OtherValue = releaseStableVisibility(Symbol.Visibility, Boundary);
  if (!OtherValue)
    return OtherValue.takeError();
  return ReleaseSymbolOrderingFacts{*Type, Symbol.Size, *BindingRank,
                                    *OtherValue, Symbol.Name.empty()};
}

struct NativeELFSymbolFacts {
  uint8_t Type = 0;
  uint8_t Binding = 0;
  uint8_t Other = 0;
  uint64_t Size = 0;
  bool OriginalNameEmpty = false;
};

Expected<NativeELFSymbolFacts>
exactNativeELFSymbolFacts(const PluginObjectSymbol &Symbol,
                          StringRef Boundary) {
  using namespace builtinext;

  constexpr size_t ExpectedPayloadSize =
      HeaderSize + (SymbolNameState + 1) * sizeof(uint64_t);
  if (!hasTag(Symbol.Extension.Bytes, SymbolTag) ||
      Symbol.Extension.Version != SymbolVersion ||
      version(Symbol.Extension.Bytes) != SymbolVersion ||
      Symbol.Extension.Bytes.size() != ExpectedPayloadSize)
    return invalid(Boundary, "release ObjectGraph symbol '" +
                                 Twine(Symbol.Name) +
                                 "' must carry the exact version-2 payload "
                                 "for its native symbol extension");

  const std::optional<uint64_t> NativeType =
      field(Symbol.Extension.Bytes, SymbolType);
  const std::optional<uint64_t> NativeBinding =
      field(Symbol.Extension.Bytes, SymbolBinding);
  const std::optional<uint64_t> NativeOther =
      field(Symbol.Extension.Bytes, SymbolOther);
  const std::optional<uint64_t> NativeSize =
      field(Symbol.Extension.Bytes, SymbolAuxiliary);
  const std::optional<uint64_t> NativeNameState =
      field(Symbol.Extension.Bytes, SymbolNameState);
  if (!NativeType || !NativeBinding || !NativeOther || !NativeSize ||
      !NativeNameState)
    return invalid(Boundary, "release ObjectGraph symbol '" +
                                 Twine(Symbol.Name) +
                                 "' must carry the exact version-2 payload "
                                 "for its native symbol extension");
  if (*NativeType > UINT64_C(0xf))
    return invalid(Boundary, "release ObjectGraph symbol '" +
                                 Twine(Symbol.Name) +
                                 "' native SymbolType does not fit ELF "
                                 "st_info");
  if (*NativeBinding > UINT64_C(0xf))
    return invalid(Boundary, "release ObjectGraph symbol '" +
                                 Twine(Symbol.Name) +
                                 "' native SymbolBinding does not fit ELF "
                                 "st_info");
  if (*NativeOther > std::numeric_limits<uint8_t>::max())
    return invalid(Boundary, "release ObjectGraph symbol '" +
                                 Twine(Symbol.Name) +
                                 "' native SymbolOther does not fit ELF "
                                 "st_other");
  if (*NativeNameState > SymbolNameEmpty)
    return invalid(Boundary, "release ObjectGraph symbol '" +
                                 Twine(Symbol.Name) +
                                 "' has an invalid native SymbolNameState");
  const bool OriginalNameEmpty = *NativeNameState == SymbolNameEmpty;
  if (OriginalNameEmpty != Symbol.Name.empty())
    return invalid(Boundary, "release ObjectGraph symbol '" +
                                 Twine(Symbol.Name) +
                                 "' native SymbolNameState disagrees with "
                                 "the stable name");
  return NativeELFSymbolFacts{
      static_cast<uint8_t>(*NativeType), static_cast<uint8_t>(*NativeBinding),
      static_cast<uint8_t>(*NativeOther), *NativeSize, OriginalNameEmpty};
}

Expected<NevercObjectSymbolType>
readerProjectedELFSymbolType(const PluginObjectGraph &Object,
                             const PluginObjectSymbol &Symbol,
                             uint8_t NativeType, StringRef Boundary) {
  if (Symbol.Definition == NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED) {
    const PluginObjectSection *Section = Object.findSection(Symbol.SectionID);
    if (!Section)
      return invalid(Boundary, "release ObjectGraph symbol '" +
                                   Twine(Symbol.Name) +
                                   "' references no section while replaying "
                                   "native symbol facts");
    if (Section->Kind == NEVERC_OBJECT_SECTION_KIND_TLS_DATA ||
        Section->Kind == NEVERC_OBJECT_SECTION_KIND_TLS_ZERO_FILL)
      return NEVERC_OBJECT_SYMBOL_TYPE_TLS;
  }
  switch (NativeType) {
  case ELF::STT_NOTYPE:
    return NEVERC_OBJECT_SYMBOL_TYPE_NO_TYPE;
  case ELF::STT_OBJECT:
  case ELF::STT_COMMON:
    return NEVERC_OBJECT_SYMBOL_TYPE_OBJECT;
  case ELF::STT_FUNC:
    return NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION;
  case ELF::STT_SECTION:
  case ELF::STT_FILE:
    return invalid(Boundary, "release ObjectGraph symbol '" +
                                 Twine(Symbol.Name) +
                                 "' carries a native synthetic symbol type "
                                 "that the built-in reader must omit");
  case ELF::STT_TLS:
  case ELF::STT_GNU_IFUNC:
  default:
    // llvm::object projects every remaining ELF type through ST_Other and the
    // built-in ObjectGraph reader records that generic class as NO_TYPE.
    return NEVERC_OBJECT_SYMBOL_TYPE_NO_TYPE;
  }
}

NevercObjectSymbolBinding readerProjectedELFSymbolBinding(uint8_t Binding) {
  if (Binding == ELF::STB_LOCAL)
    return NEVERC_OBJECT_SYMBOL_BINDING_LOCAL;
  if (Binding == ELF::STB_WEAK)
    return NEVERC_OBJECT_SYMBOL_BINDING_WEAK;
  return NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL;
}

NevercObjectSymbolVisibility readerProjectedELFSymbolVisibility(uint8_t Other) {
  return (Other & UINT8_C(3)) == ELF::STV_HIDDEN
             ? NEVERC_OBJECT_SYMBOL_VISIBILITY_HIDDEN
             : NEVERC_OBJECT_SYMBOL_VISIBILITY_DEFAULT;
}

ReleaseSymbolType nativeReleaseSymbolType(uint8_t Type) {
  switch (Type) {
  case ELF::STT_NOTYPE:
    return ReleaseSymbolType::NoType;
  case ELF::STT_OBJECT:
    return ReleaseSymbolType::Object;
  case ELF::STT_FUNC:
    return ReleaseSymbolType::Function;
  case ELF::STT_SECTION:
    return ReleaseSymbolType::Section;
  case ELF::STT_FILE:
    return ReleaseSymbolType::File;
  case ELF::STT_TLS:
    return ReleaseSymbolType::TLS;
  case ELF::STT_GNU_IFUNC:
    return ReleaseSymbolType::GNUIFunc;
  default:
    return ReleaseSymbolType::FormatExtension;
  }
}

uint32_t nativeReleaseBindingRank(uint8_t Binding) {
  switch (Binding) {
  case ELF::STB_GLOBAL:
    return 0;
  case ELF::STB_WEAK:
    return 1;
  case ELF::STB_LOCAL:
    return 2;
  default:
    return 3 + Binding;
  }
}

Expected<ReleaseSymbolOrderingFacts>
canonicalReleaseSymbolOrderingFacts(const PluginObjectGraph &Object,
                                    const PluginObjectSymbol &Symbol,
                                    StringRef Boundary) {
  if (Symbol.Extension.empty())
    return invalid(Boundary, "canonical Android release symbol '" +
                                 Twine(Symbol.Name) +
                                 "' is missing the exact native NCSY facts "
                                 "emitted by the built-in ELF reader");

  auto Native = exactNativeELFSymbolFacts(Symbol, Boundary);
  if (!Native)
    return Native.takeError();
  auto ProjectedType =
      readerProjectedELFSymbolType(Object, Symbol, Native->Type, Boundary);
  if (!ProjectedType)
    return ProjectedType.takeError();
  if (*ProjectedType != Symbol.Type)
    return invalid(Boundary, "release ObjectGraph symbol '" +
                                 Twine(Symbol.Name) +
                                 "' native SymbolType disagrees with stable "
                                 "type");
  if (readerProjectedELFSymbolBinding(Native->Binding) != Symbol.Binding)
    return invalid(Boundary, "release ObjectGraph symbol '" +
                                 Twine(Symbol.Name) +
                                 "' native SymbolBinding disagrees with "
                                 "stable binding");
  if (readerProjectedELFSymbolVisibility(Native->Other) != Symbol.Visibility)
    return invalid(Boundary, "release ObjectGraph symbol '" +
                                 Twine(Symbol.Name) +
                                 "' native SymbolOther disagrees with stable "
                                 "visibility");
  const uint64_t ProjectedSize =
      Symbol.Definition == NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED ||
              Symbol.Definition == NEVERC_OBJECT_SYMBOL_DEFINITION_COMMON
          ? Native->Size
          : 0;
  if (ProjectedSize != Symbol.Size)
    return invalid(Boundary, "release ObjectGraph symbol '" +
                                 Twine(Symbol.Name) +
                                 "' native SymbolAuxiliary disagrees with "
                                 "stable size projection");
  return ReleaseSymbolOrderingFacts{nativeReleaseSymbolType(Native->Type),
                                    Native->Size,
                                    nativeReleaseBindingRank(Native->Binding),
                                    Native->Other, Native->OriginalNameEmpty};
}

Expected<uint64_t> releaseSectionSize(const PluginObjectSection &Section,
                                      StringRef Boundary) {
  const uint64_t InitializedSize = Section.Data.size();
  if (Section.ZeroFillSize >
      std::numeric_limits<uint64_t>::max() - InitializedSize)
    return invalid(Boundary, "section '" + Twine(Section.Name) +
                                 "' size overflows while planning Android "
                                 "release names");
  return InitializedSize + Section.ZeroFillSize;
}

bool sameID(NevercInterfaceID Left, NevercInterfaceID Right) {
  return Left.High == Right.High && Left.Low == Right.Low;
}

bool equivalentUndefinedSymbols(const PluginObjectSymbol &Left,
                                const PluginObjectSymbol &Right) {
  return Left.Binding == Right.Binding && Left.Visibility == Right.Visibility &&
         Left.Type == Right.Type &&
         Left.Definition == NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED &&
         Right.Definition == NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED &&
         Left.SectionID == Right.SectionID && Left.Value == Right.Value &&
         Left.Size == Right.Size && Left.Alignment == Right.Alignment &&
         Left.ComdatID == Right.ComdatID && Left.Flags == Right.Flags &&
         sameID(Left.Extension.Owner, Right.Extension.Owner) &&
         Left.Extension.Version == Right.Extension.Version &&
         Left.Extension.Bytes == Right.Extension.Bytes;
}

Error verifyReleaseNameOwnership(
    const PluginObjectGraph &Object, const DenseSet<uint64_t> &DroppedSymbols,
    const DenseMap<uint64_t, std::string> *PlannedNames, StringRef Boundary) {
  StringMap<const PluginObjectSymbol *> Owners;
  for (const PluginObjectSymbol &Symbol : Object.symbols()) {
    if (DroppedSymbols.contains(Symbol.ID))
      continue;
    StringRef OutputName(Symbol.Name);
    if (PlannedNames) {
      const auto Planned = PlannedNames->find(Symbol.ID);
      if (Planned == PlannedNames->end())
        return invalid(Boundary,
                       "release name-ownership preflight has no planned name "
                       "for retained symbol '" +
                           Twine(Symbol.Name) + "'");
      OutputName = Planned->second;
    }
    // An empty ordinary ELF symbol name does not claim the source-level symbol
    // namespace. Native passthrough may preserve it; portable writing fails at
    // the separate representability boundary.
    if (OutputName.empty())
      continue;

    auto [Owner, Inserted] = Owners.try_emplace(OutputName, &Symbol);
    if (Inserted)
      continue;
    const PluginObjectSymbol &Existing = *Owner->second;
    const bool BothUndefined =
        Existing.Definition == NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED &&
        Symbol.Definition == NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED;
    if (!BothUndefined)
      return invalid(Boundary, "planned output name '" + Twine(OutputName) +
                                   "' is owned by multiple retained symbols; "
                                   "the built-in ELF writer cannot preserve "
                                   "their independent identities");
    if (!equivalentUndefinedSymbols(Existing, Symbol))
      return invalid(Boundary,
                     "undefined symbols sharing planned output name '" +
                         Twine(OutputName) +
                         "' have different observable attributes");
  }
  return Error::success();
}

struct GraphReleaseModel {
  SmallVector<ReleaseSectionDescriptor, 32> Sections;
  SmallVector<ReleaseSymbolDescriptor, 64> Symbols;
  SmallVector<ReleaseSymbolRename, 64> ActualNames;
};

enum class ReleaseModelFactSource : uint8_t {
  PortableWriter,
  StableGraph,
  CanonicalNative,
};

Expected<GraphReleaseModel>
buildReleaseModel(const PluginObjectGraph &Object,
                  const DenseSet<uint64_t> &DroppedSections,
                  const DenseSet<uint64_t> &DroppedSymbols,
                  const DenseSet<uint64_t> &DemotedPCGSymbols,
                  ReleaseModelFactSource FactSource, bool IncludeActualNames,
                  StringRef Boundary) {
  GraphReleaseModel Model;
  Model.Sections.reserve(Object.sectionCount());
  uint64_t FinalOrdinal = 1;
  for (const PluginObjectSection &Section : Object.sections()) {
    if (DroppedSections.contains(Section.ID))
      continue;
    auto Size = releaseSectionSize(Section, Boundary);
    if (!Size)
      return Size.takeError();
    Model.Sections.push_back(
        {Section.ID, FinalOrdinal++, Section.Alignment, *Size,
         (Section.Flags & NEVERC_OBJECT_SECTION_ALLOCATED) != 0,
         (Section.Flags & NEVERC_OBJECT_SECTION_EXECUTABLE) != 0});
  }

  Model.Symbols.reserve(Object.symbolCount());
  if (IncludeActualNames)
    Model.ActualNames.reserve(Object.symbolCount());
  for (const PluginObjectSymbol &Symbol : Object.symbols()) {
    if (DroppedSymbols.contains(Symbol.ID))
      continue;
    auto Class = releaseSymbolClass(Symbol, Boundary);
    if (!Class)
      return Class.takeError();
    const NevercObjectSymbolBinding FinalBinding =
        DemotedPCGSymbols.contains(Symbol.ID)
            ? NEVERC_OBJECT_SYMBOL_BINDING_LOCAL
            : Symbol.Binding;
    if (FactSource == ReleaseModelFactSource::PortableWriter)
      if (Error E = verifyPortableAndroidKernelReleaseWriterSymbol(
              Symbol, FinalBinding, Boundary))
        return std::move(E);
    Expected<ReleaseSymbolOrderingFacts> OrderingFacts =
        FactSource == ReleaseModelFactSource::CanonicalNative
            ? canonicalReleaseSymbolOrderingFacts(Object, Symbol, Boundary)
            : stableReleaseSymbolOrderingFacts(Symbol, FinalBinding, Boundary);
    if (!OrderingFacts)
      return OrderingFacts.takeError();

    bool PreserveName = false;
    if (*Class == AndroidKernelModuleSymbolPolicy::SymbolClass::Defined) {
      const PluginObjectSection *Section = Object.findSection(Symbol.SectionID);
      if (!Section || DroppedSections.contains(Symbol.SectionID))
        return invalid(Boundary, "retained symbol '" + Twine(Symbol.Name) +
                                     "' references no retained section");
      PreserveName =
          AndroidKernelModuleSymbolPolicy::preservesSymbolNamesInSection(
              Section->Name);
    }

    const StringRef OriginalName =
        OrderingFacts->OriginalNameEmpty ? StringRef() : StringRef(Symbol.Name);
    Model.Symbols.push_back(
        {Symbol.ID, OriginalName, *Class, OrderingFacts->Type,
         *Class == AndroidKernelModuleSymbolPolicy::SymbolClass::Defined
             ? Symbol.SectionID
             : 0,
         Symbol.Value, OrderingFacts->Size, OrderingFacts->BindingRank,
         OrderingFacts->OtherValue, PreserveName});
    if (IncludeActualNames)
      Model.ActualNames.push_back({Symbol.ID, OriginalName.str()});
  }
  return Model;
}

} // namespace

Error finalizeAndroidKernelModuleObjectGraph(
    PluginObjectGraph &Object, AndroidKernelModuleFinalizationPolicy Policy,
    StringRef Boundary, AndroidKernelReleaseSymbolMap *ReleaseSymbolMap) {
  if (ReleaseSymbolMap)
    ReleaseSymbolMap->clear();
  AndroidKernelReleaseSymbolMap PendingReleaseSymbolMap;

  if (Error E = verifyPluginObjectGraph(Object))
    return joinErrors(
        invalid(Boundary, "invalid ObjectGraph before finalization"),
        std::move(E));

  if (Policy.SymbolNameState ==
      AndroidKernelSymbolNameState::CanonicalRelease) {
    if (!Policy.StripUnneededSymbols)
      return invalid(Boundary,
                     "canonical Android release symbol provenance requires "
                     "the release symbol policy");
    // The built-in byte merger has already performed and independently
    // verified the complete release transform. Re-audit its imported graph,
    // but do not mutate it: keeping the generation stable is what lets the
    // bridge retain the merger's lossless native image instead of rewriting it
    // through the portable ObjectGraph writer.
    return verifyFinalAndroidKernelModuleObjectGraph(Object, Policy, Boundary);
  }

  for (const PluginObjectSection &Section : Object.sections()) {
    if (Policy.DropDebugInfo && isAllocatedDebugSection(Section))
      return invalid(Boundary, "cannot drop allocated debug section '" +
                                   Twine(Section.Name) + "'");
    if (Policy.StripUnneededSymbols && marksLivePatchModule(Section))
      return invalid(Boundary,
                     "Android module release strip does not support a module "
                     "marked livepatch in .modinfo");
    if (Policy.StripUnneededSymbols && isUnsupportedLivePatchSection(Section))
      return invalid(Boundary, "Android module release strip does not support "
                               "livepatch section '" +
                                   Twine(Section.Name) + "'");
  }

  if (Policy.StripUnneededSymbols)
    for (const PluginObjectSymbol &Symbol : Object.symbols())
      if (Symbol.Definition == NEVERC_OBJECT_SYMBOL_DEFINITION_COMMON)
        return invalid(Boundary,
                       "Android module release strip refuses COMMON symbol '" +
                           Twine(Symbol.Name) +
                           "'; compile final modules with -fno-common");

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

  // Prove only the final retained entity set. Debug/profile/comment sections
  // and the relocations they own are intentionally absent from the portable
  // write, so native facts attached exclusively to those entities cannot be
  // lost. Every retained section/relocation must be exactly representable
  // before the graph's first mutation.
  if (Policy.StripUnneededSymbols) {
    if (!Object.comdats().empty())
      return invalid(Boundary,
                     "Android release ObjectGraph retains COMDAT metadata "
                     "that the portable writer cannot reproduce");
    const NevercTargetKey TargetKey = Object.targetKey();
    const StringRef TripleText(
        TargetKey.RawTriple.Data ? TargetKey.RawTriple.Data : "",
        static_cast<size_t>(TargetKey.RawTriple.Length));
    const Triple WriterTarget(Triple::normalize(TripleText));
    for (const PluginObjectSection &Section : Object.sections()) {
      if (DroppedSections.contains(Section.ID))
        continue;
      if (Error E = verifyBuiltinObjectWriterSectionRepresentability(
              Section, WriterTarget, Boundary))
        return E;
      if (Error E = verifyPortableAndroidKernelReleaseWriterSection(Section,
                                                                    Boundary))
        return E;
    }
    for (const PluginObjectRelocation &Relocation : Object.relocations()) {
      if (DroppedSections.contains(Relocation.SectionID))
        continue;
      if (Error E = verifyPortableAndroidKernelReleaseWriterRelocation(
              Object, Relocation, Boundary))
        return E;
    }
  }

  // Plan against the complete final retained graph before the first mutation.
  // The shared planner owns exact loader/CFI names, structural coordinates,
  // alias suffixes, and collision rejection; this adapter only supplies the
  // ObjectGraph-to-canonical descriptor mapping.
  DenseMap<uint64_t, std::string> RenamedSymbols;
  DenseMap<uint64_t, std::vector<uint8_t>> RewrittenSymbolExtensions;
  bool NamesChanged = false;
  if (Policy.StripUnneededSymbols) {
    auto Model = buildReleaseModel(Object, DroppedSections, DroppedSymbols,
                                   DemotedPCGSymbols,
                                   ReleaseModelFactSource::PortableWriter,
                                   /*IncludeActualNames=*/false, Boundary);
    if (!Model)
      return Model.takeError();
    auto RenamePlan =
        planAndroidKernelReleaseNames(Model->Sections, Model->Symbols);
    if (!RenamePlan)
      return joinErrors(
          invalid(Boundary, "cannot plan Android release symbol names"),
          RenamePlan.takeError());
    if (RenamePlan->size() != Model->Symbols.size())
      return invalid(Boundary,
                     "Android release symbol planner returned an incomplete "
                     "rename plan");
    RenamedSymbols.reserve(RenamePlan->size());
    for (ReleaseSymbolRename &Rename : *RenamePlan)
      if (!RenamedSymbols
               .try_emplace(Rename.SymbolID, std::move(Rename.OutputName))
               .second)
        return invalid(Boundary,
                       "Android release symbol planner returned duplicate "
                       "symbol IDs");
    const NevercTargetKey TargetKey = Object.targetKey();
    const StringRef TripleText(
        TargetKey.RawTriple.Data ? TargetKey.RawTriple.Data : "",
        static_cast<size_t>(TargetKey.RawTriple.Length));
    const Triple WriterTarget(Triple::normalize(TripleText));
    for (const PluginObjectSymbol &Symbol : Object.symbols()) {
      if (DroppedSymbols.contains(Symbol.ID))
        continue;
      const auto It = RenamedSymbols.find(Symbol.ID);
      if (It == RenamedSymbols.end())
        return invalid(Boundary,
                       "Android release symbol planner omitted retained "
                       "symbol '" +
                           Twine(Symbol.Name) + "'");
      if (Error E = verifyBuiltinObjectWriterSymbolNameRepresentability(
              It->second, WriterTarget, Boundary))
        return E;
      NamesChanged |= It->second != Symbol.Name;
    }
    if (Error E = verifyReleaseNameOwnership(Object, DroppedSymbols,
                                             &RenamedSymbols, Boundary))
      return E;

    // A PCG definition becomes local as part of release finalization. Plan
    // the matching native ELF extension rewrite before any graph mutation so
    // stable Binding, st_info binding, and EXPORTED never disagree.
    RewrittenSymbolExtensions.reserve(DemotedPCGSymbols.size());
    for (const PluginObjectSymbol &Symbol : Object.symbols()) {
      if (!DemotedPCGSymbols.contains(Symbol.ID))
        continue;
      auto Extension = planPortableAndroidKernelReleaseWriterSymbolExtension(
          Symbol, NEVERC_OBJECT_SYMBOL_BINDING_LOCAL, Boundary);
      if (!Extension)
        return Extension.takeError();
      if (!RewrittenSymbolExtensions
               .try_emplace(Symbol.ID, std::move(*Extension))
               .second)
        return invalid(Boundary,
                       "Android release symbol planner returned duplicate "
                       "PCG symbol IDs");
    }
    if (RewrittenSymbolExtensions.size() != DemotedPCGSymbols.size())
      return invalid(Boundary,
                     "Android release symbol planner omitted a PCG symbol "
                     "binding mutation");

    for (const PluginObjectSymbol &Symbol : Object.symbols()) {
      if (DroppedSymbols.contains(Symbol.ID))
        continue;
      const auto Renamed = RenamedSymbols.find(Symbol.ID);
      if (Renamed != RenamedSymbols.end() && !Symbol.Name.empty() &&
          Renamed->second != Symbol.Name)
        PendingReleaseSymbolMap.Symbols.push_back(
            {Symbol.Name, Renamed->second});
    }
  }

  bool Changed = !DroppedSections.empty() || !DroppedSymbols.empty() ||
                 !DemotedPCGSymbols.empty() || NamesChanged;
  if (!Changed)
    return Error::success();

  Object.relocations().remove_if([&](const PluginObjectRelocation &Relocation) {
    return DroppedSections.contains(Relocation.SectionID);
  });
  Object.symbols().remove_if([&](const PluginObjectSymbol &Symbol) {
    return DroppedSymbols.contains(Symbol.ID);
  });
  for (PluginObjectSymbol &Symbol : Object.symbols()) {
    auto Extension = RewrittenSymbolExtensions.find(Symbol.ID);
    if (Extension != RewrittenSymbolExtensions.end()) {
      Symbol.Binding = NEVERC_OBJECT_SYMBOL_BINDING_LOCAL;
      Symbol.Flags &= ~NEVERC_OBJECT_SYMBOL_EXPORTED;
      Symbol.Extension.Bytes = std::move(Extension->second);
    }
  }
  for (PluginObjectSymbol &Symbol : Object.symbols()) {
    auto It = RenamedSymbols.find(Symbol.ID);
    if (It != RenamedSymbols.end())
      Symbol.Name = std::move(It->second);
  }
  Object.sections().remove_if([&](const PluginObjectSection &Section) {
    return DroppedSections.contains(Section.ID);
  });
  Object.advanceGeneration();
  if (ReleaseSymbolMap)
    *ReleaseSymbolMap = std::move(PendingReleaseSymbolMap);
  return Error::success();
}

Error verifyFinalAndroidKernelModuleObjectGraph(
    const PluginObjectGraph &Object,
    AndroidKernelModuleFinalizationPolicy Policy, StringRef Boundary) {
  if (Error E = verifyPluginObjectGraph(Object))
    return joinErrors(invalid(Boundary, "invalid finalized ObjectGraph"),
                      std::move(E));

  if (Policy.StripUnneededSymbols &&
      Policy.SymbolNameState ==
          AndroidKernelSymbolNameState::CanonicalRelease) {
    const NevercTargetKey TargetKey = Object.targetKey();
    const StringRef TripleText(
        TargetKey.RawTriple.Data ? TargetKey.RawTriple.Data : "",
        static_cast<size_t>(TargetKey.RawTriple.Length));
    const Triple Target(Triple::normalize(TripleText));
    if (Target.getArch() != Triple::aarch64 || !Target.isOSBinFormatELF() ||
        TargetKey.PointerWidth != 64 ||
        TargetKey.Endianness != NEVERC_TARGET_ENDIAN_LITTLE)
      return invalid(Boundary,
                     "canonical Android release provenance requires an "
                     "AArch64 ELF64 little-endian ObjectGraph");
  }

  uint64_t CanonicalSectionIndex = 1;
  for (const PluginObjectSection &Section : Object.sections()) {
    if (Policy.StripUnneededSymbols &&
        Policy.SymbolNameState ==
            AndroidKernelSymbolNameState::CanonicalRelease)
      if (Error E = verifyCanonicalAndroidKernelReleaseReaderSection(
              Section, CanonicalSectionIndex, Boundary))
        return E;
    ++CanonicalSectionIndex;
    if (Section.Name == AndroidKernelProfileContract::NativeSection)
      return invalid(Boundary,
                     "must not retain native Android kernel profile contract "
                     "section");
    if (Policy.DropDebugInfo && isDebugSection(Section)) {
      if (isAllocatedDebugSection(Section))
        return invalid(Boundary, "retains allocated debug section '" +
                                     Twine(Section.Name) + "'");
      return invalid(Boundary,
                     "retains debug section '" + Twine(Section.Name) + "'");
    }
    if (Policy.StripUnneededSymbols && Section.Name == ".comment")
      return invalid(Boundary, "retains producer .comment metadata");
    if (Policy.StripUnneededSymbols && marksLivePatchModule(Section))
      return invalid(Boundary,
                     "retains .modinfo metadata marking a livepatch module");
    if (Policy.StripUnneededSymbols && isUnsupportedLivePatchSection(Section))
      return invalid(Boundary, "retains unsupported livepatch section '" +
                                   Twine(Section.Name) + "'");
  }

  DenseSet<uint64_t> ReferencedSymbols;
  for (const PluginObjectRelocation &Relocation : Object.relocations()) {
    if (Policy.StripUnneededSymbols &&
        Policy.SymbolNameState ==
            AndroidKernelSymbolNameState::CanonicalRelease)
      if (Error E = verifyCanonicalAndroidKernelReleaseReaderRelocation(
              Object, Relocation, Boundary))
        return E;
    if (Relocation.TargetKind == NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL)
      ReferencedSymbols.insert(Relocation.TargetSymbolID);
  }

  for (const PluginObjectSymbol &Symbol : Object.symbols()) {
    if (Symbol.Name == AndroidKernelProfileContract::NativeSymbol)
      return invalid(Boundary,
                     "must not retain native Android kernel profile contract "
                     "symbol");
    if (!Policy.StripUnneededSymbols)
      continue;
    if (Symbol.Definition == NEVERC_OBJECT_SYMBOL_DEFINITION_COMMON)
      return invalid(Boundary, "retains unsupported COMMON symbol '" +
                                   Twine(Symbol.Name) + "'");
    if (isPCGDefinition(Symbol) &&
        Symbol.Binding != NEVERC_OBJECT_SYMBOL_BINDING_LOCAL)
      return invalid(Boundary, "retains a globally visible PCG symbol '" +
                                   Twine(Symbol.Name) + "'");
    if (isStripCandidate(Symbol) && !ReferencedSymbols.contains(Symbol.ID))
      return invalid(Boundary, "retains relocation-unneeded symbol '" +
                                   Twine(Symbol.Name) + "'");
  }

  if (Policy.StripUnneededSymbols) {
    DenseSet<uint64_t> Empty;
    const ReleaseModelFactSource FactSource =
        Policy.SymbolNameState == AndroidKernelSymbolNameState::CanonicalRelease
            ? ReleaseModelFactSource::CanonicalNative
            : ReleaseModelFactSource::StableGraph;
    auto Model = buildReleaseModel(Object, Empty, Empty, Empty, FactSource,
                                   /*IncludeActualNames=*/true, Boundary);
    if (!Model)
      return Model.takeError();
    if (Error Audit = auditAndroidKernelReleaseNames(
            Model->Sections, Model->Symbols, Model->ActualNames))
      return joinErrors(
          invalid(Boundary, "invalid Android release symbol plan"),
          std::move(Audit));
    if (Error E = verifyReleaseNameOwnership(Object, Empty, nullptr, Boundary))
      return E;
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
    const bool NonLocal = (*Flags & (object::SymbolRef::SF_Global |
                                     object::SymbolRef::SF_Weak)) != 0;
    const bool PCGDefinition =
        !Undefined && Name->contains(neverc::merge::PcgSymbolMarker);
    if (PCGDefinition && NonLocal)
      return invalid(Boundary, "retains a globally visible PCG symbol '" +
                                   Twine(*Name) + "'");
  }
  return Error::success();
}

Error bindAndroidKernelReleaseSymbolMapToImage(
    AndroidKernelReleaseSymbolMap &Map, ArrayRef<uint8_t> Image,
    StringRef Boundary) {
  auto Object = object::ObjectFile::createObjectFile(MemoryBufferRef(
      StringRef(reinterpret_cast<const char *>(Image.data()), Image.size()),
      Boundary));
  if (!Object)
    return joinErrors(invalid(Boundary, "cannot parse final symbol map image"),
                      Object.takeError());

  StringMap<unsigned> SymbolNameCounts;
  for (const object::SymbolRef &Symbol : (*Object)->symbols()) {
    auto Name = Symbol.getName();
    if (!Name)
      return joinErrors(
          invalid(Boundary, "cannot read a final symbol name"),
          Name.takeError());
    if (!Name->empty())
      ++SymbolNameCounts[*Name];
  }

  for (const AndroidKernelReleaseSymbolMapEntry &Entry : Map.Symbols)
    if (SymbolNameCounts.lookup(Entry.ReleaseName) > 1)
      return invalid(Boundary, "release symbol map name '" +
                                   Twine(Entry.ReleaseName) +
                                   "' is ambiguous in the final image");

  Map.Symbols.erase(
      llvm::remove_if(Map.Symbols,
                      [&](const AndroidKernelReleaseSymbolMapEntry &Entry) {
                        return SymbolNameCounts.lookup(Entry.ReleaseName) == 0;
                      }),
      Map.Symbols.end());
  Map.ImageSHA256 = SHA256::hash(Image);
  return Error::success();
}

} // namespace neverc::plugin
