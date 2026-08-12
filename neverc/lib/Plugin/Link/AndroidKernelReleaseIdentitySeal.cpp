#include "AndroidKernelReleaseIdentitySeal.h"

#include "AndroidKernelModuleFinalizer.h"
#include "../AndroidKernelReleaseWriterPolicy.h"
#include "neverc/Foundation/AndroidKernelModuleReleaseNames.h"
#include "neverc/Foundation/AndroidKernelModuleSectionPolicy.h"
#include "neverc/Foundation/AndroidKernelModuleSymbolPolicy.h"
#include "neverc/Plugin/Host/BuiltinObjectExtension.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Object/ELF.h"
#include "llvm/Support/Errc.h"

#include <optional>
#include <string>
#include <tuple>
#include <utility>

using namespace llvm;

namespace neverc::plugin {
namespace {

using SymbolClass = AndroidKernelModuleSymbolPolicy::SymbolClass;

struct GraphSectionIdentity {
  uint64_t OwnerID = 0;
  uint64_t Ordinal = 0;
  std::string Name;

  bool operator==(const GraphSectionIdentity &Other) const {
    return std::tie(OwnerID, Ordinal, Name) ==
           std::tie(Other.OwnerID, Other.Ordinal, Other.Name);
  }
};

struct GraphSymbolIdentity {
  uint64_t OwnerID = 0;
  std::string Name;
  SymbolClass Class = SymbolClass::Undefined;
  uint64_t SectionID = 0;
  uint64_t Value = 0;
  uint64_t Size = 0;
  uint8_t Binding = 0;
  uint8_t Type = 0;
  uint8_t Other = 0;

  bool operator==(const GraphSymbolIdentity &OtherIdentity) const {
    return std::tie(OwnerID, Name, Class, SectionID, Value, Size, Binding, Type,
                    Other) ==
           std::tie(
               OtherIdentity.OwnerID, OtherIdentity.Name, OtherIdentity.Class,
               OtherIdentity.SectionID, OtherIdentity.Value, OtherIdentity.Size,
               OtherIdentity.Binding, OtherIdentity.Type, OtherIdentity.Other);
  }
};

struct ImageSectionIdentity {
  uint64_t Ordinal = 0;
  std::string Name;

  bool operator==(const ImageSectionIdentity &Other) const {
    return std::tie(Ordinal, Name) == std::tie(Other.Ordinal, Other.Name);
  }
};

struct ImageSymbolIdentity {
  uint64_t Slot = 0;
  std::string Name;
  SymbolClass Class = SymbolClass::Undefined;
  uint16_t SectionIndex = 0;
  uint64_t Value = 0;
  uint64_t Size = 0;
  uint8_t Binding = 0;
  uint8_t Type = 0;
  uint8_t Other = 0;

  bool operator==(const ImageSymbolIdentity &OtherIdentity) const {
    return std::tie(Slot, Name, Class, SectionIndex, Value, Size, Binding, Type,
                    Other) ==
           std::tie(OtherIdentity.Slot, OtherIdentity.Name, OtherIdentity.Class,
                    OtherIdentity.SectionIndex, OtherIdentity.Value,
                    OtherIdentity.Size, OtherIdentity.Binding,
                    OtherIdentity.Type, OtherIdentity.Other);
  }
};

struct NativeSymbolIdentityFacts {
  uint64_t Size = 0;
  uint8_t Binding = 0;
  uint8_t Type = 0;
  uint8_t Other = 0;
};

struct GraphIdentitySnapshot {
  SmallVector<GraphSectionIdentity, 16> Sections;
  SmallVector<GraphSymbolIdentity, 16> Symbols;
};

struct ImageIdentitySnapshot {
  SmallVector<ImageSectionIdentity, 16> Sections;
  SmallVector<ImageSymbolIdentity, 16> Symbols;
  uint64_t SymbolCount = 0;
};

Error invalid(StringRef Boundary, const Twine &Message) {
  return createStringError(errc::invalid_argument,
                           (Boundary + ": " + Message).str());
}

Expected<SymbolClass> graphSymbolClass(const PluginObjectSymbol &Symbol,
                                       StringRef Boundary) {
  switch (Symbol.Definition) {
  case NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED:
    return SymbolClass::Defined;
  case NEVERC_OBJECT_SYMBOL_DEFINITION_ABSOLUTE:
    return SymbolClass::Absolute;
  case NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED:
    return SymbolClass::Undefined;
  case NEVERC_OBJECT_SYMBOL_DEFINITION_COMMON:
    return SymbolClass::Common;
  }
  return invalid(Boundary, "unknown ObjectGraph symbol definition " +
                               Twine(Symbol.Definition));
}

Expected<SymbolClass> nativeSymbolClass(uint16_t SectionIndex,
                                        size_t SectionCount,
                                        StringRef Boundary) {
  if (SectionIndex == ELF::SHN_UNDEF)
    return SymbolClass::Undefined;
  if (SectionIndex == ELF::SHN_ABS)
    return SymbolClass::Absolute;
  if (SectionIndex == ELF::SHN_COMMON)
    return SymbolClass::Common;
  if (SectionIndex == AndroidKernelModuleSymbolPolicy::LivePatchSectionIndex)
    return SymbolClass::LivePatch;
  if (SectionIndex >= ELF::SHN_LORESERVE || SectionIndex >= SectionCount)
    return invalid(Boundary,
                   "release identity symbol has an unsupported section "
                   "index");
  return SymbolClass::Defined;
}

bool hasSealedIdentity(StringRef Name, SymbolClass Class, uint8_t Type,
                       bool PreserveName) {
  return hasCanonicalReleaseNameShape(Name) ||
         AndroidKernelModuleSymbolPolicy::hasExactReleaseName(
             Name, Class, Type == ELF::STT_SECTION, PreserveName);
}

Expected<NativeSymbolIdentityFacts>
portableSymbolFacts(const PluginObjectSymbol &Symbol, StringRef Boundary) {
  if (Error E = verifyPortableAndroidKernelReleaseWriterSymbol(
          Symbol, Symbol.Binding, Boundary))
    return std::move(E);

  NativeSymbolIdentityFacts Facts;
  Facts.Size = Symbol.Size;
  switch (Symbol.Binding) {
  case NEVERC_OBJECT_SYMBOL_BINDING_LOCAL:
    Facts.Binding = ELF::STB_LOCAL;
    break;
  case NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL:
    Facts.Binding = ELF::STB_GLOBAL;
    break;
  case NEVERC_OBJECT_SYMBOL_BINDING_WEAK:
    Facts.Binding = ELF::STB_WEAK;
    break;
  case NEVERC_OBJECT_SYMBOL_BINDING_UNIQUE:
  case NEVERC_OBJECT_SYMBOL_BINDING_FORMAT_EXTENSION:
  default:
    return invalid(Boundary,
                   "release identity symbol has an unrepresentable binding");
  }

  switch (Symbol.Type) {
  case NEVERC_OBJECT_SYMBOL_TYPE_NO_TYPE:
    Facts.Type = ELF::STT_NOTYPE;
    break;
  case NEVERC_OBJECT_SYMBOL_TYPE_OBJECT:
    Facts.Type = ELF::STT_OBJECT;
    break;
  case NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION:
    Facts.Type = ELF::STT_FUNC;
    break;
  case NEVERC_OBJECT_SYMBOL_TYPE_SECTION:
  case NEVERC_OBJECT_SYMBOL_TYPE_TLS:
  case NEVERC_OBJECT_SYMBOL_TYPE_FILE:
  case NEVERC_OBJECT_SYMBOL_TYPE_INDIRECT_FUNCTION:
  case NEVERC_OBJECT_SYMBOL_TYPE_FORMAT_EXTENSION:
  default:
    return invalid(Boundary,
                   "release identity symbol has an unrepresentable type");
  }

  switch (Symbol.Visibility) {
  case NEVERC_OBJECT_SYMBOL_VISIBILITY_DEFAULT:
    Facts.Other = ELF::STV_DEFAULT;
    break;
  case NEVERC_OBJECT_SYMBOL_VISIBILITY_HIDDEN:
    Facts.Other = ELF::STV_HIDDEN;
    break;
  case NEVERC_OBJECT_SYMBOL_VISIBILITY_INTERNAL:
  case NEVERC_OBJECT_SYMBOL_VISIBILITY_PROTECTED:
  case NEVERC_OBJECT_SYMBOL_VISIBILITY_FORMAT_EXTENSION:
  default:
    return invalid(Boundary,
                   "release identity symbol has an unrepresentable visibility");
  }
  return Facts;
}

Expected<NativeSymbolIdentityFacts>
canonicalSymbolFacts(const PluginObjectSymbol &Symbol, StringRef Boundary) {
  using namespace builtinext;
  constexpr size_t ExactSize =
      HeaderSize + (SymbolNameState + 1) * sizeof(uint64_t);
  if (!hasTag(Symbol.Extension.Bytes, SymbolTag) ||
      Symbol.Extension.Version != SymbolVersion ||
      version(Symbol.Extension.Bytes) != SymbolVersion ||
      Symbol.Extension.Bytes.size() != ExactSize)
    return invalid(Boundary, "canonical release identity symbol '" +
                                 Twine(Symbol.Name) +
                                 "' has no exact NCSY v2 identity");

  const std::optional<uint64_t> Type =
      field(Symbol.Extension.Bytes, SymbolType);
  const std::optional<uint64_t> Binding =
      field(Symbol.Extension.Bytes, SymbolBinding);
  const std::optional<uint64_t> Other =
      field(Symbol.Extension.Bytes, SymbolOther);
  const std::optional<uint64_t> Size =
      field(Symbol.Extension.Bytes, SymbolAuxiliary);
  const std::optional<uint64_t> NativeNameState =
      field(Symbol.Extension.Bytes, SymbolNameState);
  const uint64_t ExpectedNameState =
      Symbol.Name.empty() ? SymbolNameEmpty : SymbolNameNonEmpty;
  if (!Type || !Binding || !Other || !Size || !NativeNameState || *Type > 0xf ||
      *Binding > 0xf || *Other > UINT8_MAX ||
      *NativeNameState != ExpectedNameState)
    return invalid(Boundary, "canonical release identity symbol '" +
                                 Twine(Symbol.Name) +
                                 "' has invalid NCSY v2 identity");
  return NativeSymbolIdentityFacts{*Size, static_cast<uint8_t>(*Binding),
                                   static_cast<uint8_t>(*Type),
                                   static_cast<uint8_t>(*Other)};
}

Expected<GraphIdentitySnapshot>
graphIdentities(const PluginObjectGraph &Object,
                AndroidKernelSymbolNameState NameState, StringRef Boundary) {
  GraphIdentitySnapshot Snapshot;
  Snapshot.Sections.reserve(Object.sectionCount());
  DenseMap<uint64_t, size_t> SectionSlots;
  SectionSlots.reserve(Object.sectionCount());

  uint64_t Ordinal = 1;
  for (const PluginObjectSection &Section : Object.sections()) {
    if (!SectionSlots.try_emplace(Section.ID, Snapshot.Sections.size()).second)
      return invalid(Boundary, "ObjectGraph has duplicate section identity " +
                                   Twine(Section.ID));
    Snapshot.Sections.push_back(
        GraphSectionIdentity{Section.ID, Ordinal++, Section.Name});
  }

  Snapshot.Symbols.reserve(Object.symbolCount());
  for (const PluginObjectSymbol &Symbol : Object.symbols()) {
    auto Class = graphSymbolClass(Symbol, Boundary);
    if (!Class)
      return Class.takeError();

    const GraphSectionIdentity *Section = nullptr;
    bool PreserveName = false;
    if (*Class == SymbolClass::Defined) {
      const auto Found = SectionSlots.find(Symbol.SectionID);
      if (Found == SectionSlots.end())
        return invalid(Boundary, "release identity symbol '" +
                                     Twine(Symbol.Name) +
                                     "' references no logical section");
      Section = &Snapshot.Sections[Found->second];
      PreserveName =
          AndroidKernelModuleSymbolPolicy::preservesSymbolNamesInSection(
              Section->Name);
    }

    auto Facts = NameState == AndroidKernelSymbolNameState::CanonicalRelease
                     ? canonicalSymbolFacts(Symbol, Boundary)
                     : portableSymbolFacts(Symbol, Boundary);
    if (!Facts)
      return Facts.takeError();
    if (!hasSealedIdentity(Symbol.Name, *Class, Facts->Type, PreserveName))
      continue;

    GraphSymbolIdentity Identity;
    Identity.OwnerID = Symbol.ID;
    Identity.Name = Symbol.Name;
    Identity.Class = *Class;
    if (Section)
      Identity.SectionID = Section->OwnerID;
    Identity.Value = Symbol.Value;
    Identity.Size = Facts->Size;
    Identity.Binding = Facts->Binding;
    Identity.Type = Facts->Type;
    Identity.Other = Facts->Other;
    Snapshot.Symbols.push_back(std::move(Identity));
  }
  llvm::sort(Snapshot.Symbols, [](const GraphSymbolIdentity &Left,
                                  const GraphSymbolIdentity &Right) {
    return Left.OwnerID < Right.OwnerID;
  });
  return Snapshot;
}

Expected<ImageIdentitySnapshot> imageIdentities(ArrayRef<uint8_t> Image,
                                                StringRef Boundary) {
  using ELFT = object::ELF64LE;
  const StringRef Bytes(reinterpret_cast<const char *>(Image.data()),
                        Image.size());
  auto FileOrError = object::ELFFile<ELFT>::create(Bytes);
  if (!FileOrError)
    return joinErrors(invalid(Boundary, "is not a parseable ELF64LE image"),
                      FileOrError.takeError());
  const object::ELFFile<ELFT> &File = *FileOrError;
  const ELFT::Ehdr &Header = File.getHeader();
  if (Header.e_type != ELF::ET_REL || Header.e_machine != ELF::EM_AARCH64)
    return invalid(Boundary,
                   "release identity seal requires an AArch64 ET_REL image");

  auto SectionsOrError = File.sections();
  if (!SectionsOrError)
    return joinErrors(invalid(Boundary, "has an invalid section table"),
                      SectionsOrError.takeError());
  const ArrayRef<ELFT::Shdr> Sections = *SectionsOrError;
  if (Header.e_shstrndx >= Sections.size())
    return invalid(Boundary, "has an invalid section-name table index");

  const ELFT::Shdr *SymbolTable = nullptr;
  uint32_t SymbolTableIndex = 0;
  for (uint32_t Index = 0; Index != Sections.size(); ++Index) {
    if (Sections[Index].sh_type != ELF::SHT_SYMTAB)
      continue;
    if (SymbolTable)
      return invalid(Boundary, "has multiple symbol tables");
    SymbolTable = &Sections[Index];
    SymbolTableIndex = Index;
  }
  if (!SymbolTable || SymbolTable->sh_link >= Sections.size() ||
      Sections[SymbolTable->sh_link].sh_type != ELF::SHT_STRTAB)
    return invalid(Boundary, "has no canonical symbol/string table pair");

  const uint32_t SymbolStringTableIndex = SymbolTable->sh_link;
  ImageIdentitySnapshot Snapshot;
  SmallVector<std::optional<ImageSectionIdentity>, 32> LogicalSections(
      Sections.size());
  uint64_t Ordinal = 1;
  for (uint32_t Index = 1; Index != Sections.size(); ++Index) {
    const ELFT::Shdr &Section = Sections[Index];
    if (Index == SymbolTableIndex || Index == SymbolStringTableIndex ||
        Index == Header.e_shstrndx ||
        AndroidKernelModuleSectionPolicy::regeneratesReleaseInputType(
            Section.sh_type))
      continue;
    if (Section.sh_type == ELF::SHT_STRTAB)
      return invalid(Boundary,
                     "has an unexpected logical string-table section");
    auto Name = File.getSectionName(Section);
    if (!Name)
      return Name.takeError();
    ImageSectionIdentity Identity{Ordinal++, Name->str()};
    LogicalSections[Index] = Identity;
    Snapshot.Sections.push_back(std::move(Identity));
  }

  auto Symbols = File.symbols(SymbolTable);
  auto Strings = File.getStringTableForSymtab(*SymbolTable);
  if (!Symbols || !Strings) {
    Error Cause = Error::success();
    if (!Symbols)
      Cause = joinErrors(std::move(Cause), Symbols.takeError());
    if (!Strings)
      Cause = joinErrors(std::move(Cause), Strings.takeError());
    return joinErrors(invalid(Boundary, "has an invalid symbol table"),
                      std::move(Cause));
  }

  Snapshot.SymbolCount = Symbols->size();
  Snapshot.Symbols.reserve(Symbols->size());
  uint64_t Slot = 0;
  for (const ELFT::Sym &Symbol : *Symbols) {
    auto Name = Symbol.getName(*Strings);
    if (!Name)
      return Name.takeError();
    auto Class = nativeSymbolClass(Symbol.st_shndx, Sections.size(), Boundary);
    if (!Class)
      return Class.takeError();

    const ImageSectionIdentity *Section = nullptr;
    bool PreserveName = false;
    if (*Class == SymbolClass::Defined) {
      if (Symbol.st_shndx >= LogicalSections.size() ||
          !LogicalSections[Symbol.st_shndx])
        return invalid(Boundary, "release identity symbol '" + Twine(*Name) +
                                     "' references no logical section");
      Section = &*LogicalSections[Symbol.st_shndx];
      PreserveName =
          AndroidKernelModuleSymbolPolicy::preservesSymbolNamesInSection(
              Section->Name);
    }
    if (hasSealedIdentity(*Name, *Class, Symbol.getType(), PreserveName)) {
      ImageSymbolIdentity Identity;
      Identity.Slot = Slot;
      Identity.Name = Name->str();
      Identity.Class = *Class;
      Identity.SectionIndex = Symbol.st_shndx;
      Identity.Value = Symbol.st_value;
      Identity.Size = Symbol.st_size;
      Identity.Binding = Symbol.getBinding();
      Identity.Type = Symbol.getType();
      Identity.Other = Symbol.st_other;
      Snapshot.Symbols.push_back(std::move(Identity));
    }
    ++Slot;
  }
  return Snapshot;
}

Error compareGraphIdentities(const GraphIdentitySnapshot &Expected,
                             const GraphIdentitySnapshot &Actual,
                             StringRef Boundary) {
  if (Expected.Sections.size() != Actual.Sections.size())
    return invalid(Boundary,
                   "immutable release layout identity seal section count "
                   "changed from " +
                       Twine(Expected.Sections.size()) + " to " +
                       Twine(Actual.Sections.size()));
  for (size_t Index = 0; Index != Expected.Sections.size(); ++Index) {
    if (Expected.Sections[Index] == Actual.Sections[Index])
      continue;
    return invalid(Boundary,
                   "immutable release layout identity seal changed section "
                   "at final ordinal " +
                       Twine(Index + 1) + " ('" +
                       Twine(Expected.Sections[Index].Name) + "' became '" +
                       Twine(Actual.Sections[Index].Name) + "')");
  }

  if (Expected.Symbols.size() != Actual.Symbols.size())
    return invalid(Boundary,
                   "immutable release identity seal protected symbol count "
                   "changed from " +
                       Twine(Expected.Symbols.size()) + " to " +
                       Twine(Actual.Symbols.size()));
  for (size_t Index = 0; Index != Expected.Symbols.size(); ++Index) {
    if (Expected.Symbols[Index] == Actual.Symbols[Index])
      continue;
    return invalid(Boundary,
                   "immutable release identity seal changed symbol owner " +
                       Twine(Expected.Symbols[Index].OwnerID) + " ('" +
                       Twine(Expected.Symbols[Index].Name) + "' became '" +
                       Twine(Actual.Symbols[Index].Name) + "')");
  }
  return Error::success();
}

Error compareImageIdentities(const ImageIdentitySnapshot &Expected,
                             const ImageIdentitySnapshot &Actual,
                             StringRef Boundary) {
  if (Expected.Sections.size() != Actual.Sections.size())
    return invalid(Boundary,
                   "immutable release layout identity seal section count "
                   "changed from " +
                       Twine(Expected.Sections.size()) + " to " +
                       Twine(Actual.Sections.size()));
  for (size_t Index = 0; Index != Expected.Sections.size(); ++Index) {
    if (Expected.Sections[Index] == Actual.Sections[Index])
      continue;
    return invalid(Boundary,
                   "immutable release layout identity seal changed section "
                   "at logical ordinal " +
                       Twine(Index + 1) + " ('" +
                       Twine(Expected.Sections[Index].Name) + "' became '" +
                       Twine(Actual.Sections[Index].Name) + "')");
  }

  if (Expected.SymbolCount != Actual.SymbolCount)
    return invalid(Boundary,
                   "immutable release identity seal symbol-table count "
                   "changed from " +
                       Twine(Expected.SymbolCount) + " to " +
                       Twine(Actual.SymbolCount));
  if (Expected.Symbols.size() != Actual.Symbols.size())
    return invalid(Boundary,
                   "immutable release identity seal protected symbol count "
                   "changed from " +
                       Twine(Expected.Symbols.size()) + " to " +
                       Twine(Actual.Symbols.size()));
  for (size_t Index = 0; Index != Expected.Symbols.size(); ++Index) {
    if (Expected.Symbols[Index] == Actual.Symbols[Index])
      continue;
    return invalid(Boundary,
                   "immutable release identity seal changed symbol-table "
                   "slot " +
                       Twine(Expected.Symbols[Index].Slot) + " ('" +
                       Twine(Expected.Symbols[Index].Name) + "' became '" +
                       Twine(Actual.Symbols[Index].Name) + "')");
  }
  return Error::success();
}

} // namespace

struct AndroidKernelReleaseGraphIdentitySeal::Impl {
  explicit Impl(GraphIdentitySnapshot SnapshotValue)
      : Snapshot(std::move(SnapshotValue)) {}

  GraphIdentitySnapshot Snapshot;
};

struct AndroidKernelReleaseImageIdentitySeal::Impl {
  explicit Impl(ImageIdentitySnapshot SnapshotValue)
      : Snapshot(std::move(SnapshotValue)) {}

  ImageIdentitySnapshot Snapshot;
};

Expected<AndroidKernelReleaseGraphIdentitySeal>
captureAndroidKernelReleaseGraphIdentitySeal(
    const PluginObjectGraph &Object, AndroidKernelSymbolNameState NameState,
    StringRef Boundary) {
  auto Snapshot = graphIdentities(Object, NameState, Boundary);
  if (!Snapshot)
    return Snapshot.takeError();
  return AndroidKernelReleaseGraphIdentitySeal(
      std::make_shared<const AndroidKernelReleaseGraphIdentitySeal::Impl>(
          std::move(*Snapshot)));
}

Expected<AndroidKernelReleaseImageIdentitySeal>
captureAndroidKernelReleaseImageIdentitySeal(ArrayRef<uint8_t> Image,
                                             StringRef Boundary) {
  auto Snapshot = imageIdentities(Image, Boundary);
  if (!Snapshot)
    return Snapshot.takeError();
  return AndroidKernelReleaseImageIdentitySeal(
      std::make_shared<const AndroidKernelReleaseImageIdentitySeal::Impl>(
          std::move(*Snapshot)));
}

Error verifyAndroidKernelReleaseGraphIdentitySeal(
    const PluginObjectGraph &Object, AndroidKernelSymbolNameState NameState,
    const AndroidKernelReleaseGraphIdentitySeal &Seal, StringRef Boundary) {
  if (!Seal.State)
    return invalid(Boundary, "has no immutable graph release identity seal");
  auto Snapshot = graphIdentities(Object, NameState, Boundary);
  if (!Snapshot)
    return Snapshot.takeError();
  return compareGraphIdentities(Seal.State->Snapshot, *Snapshot, Boundary);
}

Error verifyAndroidKernelReleaseImageIdentitySeal(
    ArrayRef<uint8_t> Image, const AndroidKernelReleaseImageIdentitySeal &Seal,
    StringRef Boundary) {
  if (!Seal.State)
    return invalid(Boundary, "has no immutable image release identity seal");
  auto Snapshot = imageIdentities(Image, Boundary);
  if (!Snapshot)
    return Snapshot.takeError();
  return compareImageIdentities(Seal.State->Snapshot, *Snapshot, Boundary);
}

} // namespace neverc::plugin
