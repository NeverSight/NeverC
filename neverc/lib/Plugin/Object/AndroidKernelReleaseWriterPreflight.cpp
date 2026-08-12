#include "../AndroidKernelReleaseWriterPolicy.h"

#include "neverc/Foundation/AndroidKernelModuleRelocationPolicy.h"
#include "neverc/Plugin/Host/BuiltinObjectExtension.h"
#include "neverc/Plugin/Host/NativeELFSectionFacts.h"
#include "neverc/Plugin/Host/NativeRelocationFacts.h"
#include "neverc/Plugin/Host/ObjectSectionRole.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Object/ELF.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/TargetParser/Triple.h"

#include <cstddef>
#include <limits>
#include <optional>
#include <utility>

using namespace llvm;

namespace neverc::plugin {
namespace {

Error invalid(StringRef Boundary, const Twine &Message) {
  return createStringError(errc::invalid_argument,
                           (Boundary + ": " + Message).str());
}

Triple graphTriple(const PluginObjectGraph &Object);
uint32_t readU32(ArrayRef<uint8_t> Bytes, size_t Offset);

Expected<uint32_t>
verifyCanonicalNativeRelocationFacts(const PluginObjectGraph &Object,
                                     const PluginObjectRelocation &Relocation,
                                     StringRef Boundary) {
  const Triple Target = graphTriple(Object);
  if (!Target.isOSBinFormatELF() || Target.getArch() != Triple::aarch64)
    return invalid(Boundary, "canonical Android release relocation provenance "
                             "requires an AArch64 ELF graph");

  using namespace builtinext;
  if (Relocation.Extension.empty() ||
      !hasTag(Relocation.Extension.Bytes, RelocationTag))
    return invalid(Boundary, "release ObjectGraph relocation " +
                                 Twine(Relocation.ID) +
                                 " has no supported native AArch64 ELF "
                                 "extension");
  const uint32_t EmbeddedVersion = version(Relocation.Extension.Bytes);
  if (Relocation.Extension.Version != EmbeddedVersion)
    return invalid(Boundary, "release ObjectGraph relocation " +
                                 Twine(Relocation.ID) +
                                 " native extension version metadata "
                                 "disagrees with its payload");
  if (EmbeddedVersion != RelocationVersion)
    return invalid(Boundary, "release ObjectGraph relocation " +
                                 Twine(Relocation.ID) +
                                 " uses an unsupported native relocation "
                                 "extension version");

  constexpr size_t NameOffset = RelocationNameLengthOffset + sizeof(uint32_t);
  if (Relocation.Extension.Bytes.size() < NameOffset)
    return invalid(Boundary, "release ObjectGraph relocation " +
                                 Twine(Relocation.ID) +
                                 " must carry the exact version-1 payload");
  const uint32_t NameLength =
      readU32(Relocation.Extension.Bytes, RelocationNameLengthOffset);
  if (NameLength == 0 ||
      Relocation.Extension.Bytes.size() != NameOffset + NameLength)
    return invalid(Boundary, "release ObjectGraph relocation " +
                                 Twine(Relocation.ID) +
                                 " must carry the exact version-1 payload");
  const std::optional<uint64_t> NativeType =
      field(Relocation.Extension.Bytes, RelocationNativeType);
  if (!NativeType || *NativeType > std::numeric_limits<uint32_t>::max())
    return invalid(Boundary, "release ObjectGraph relocation " +
                                 Twine(Relocation.ID) +
                                 " has an invalid native relocation type");
  const uint32_t Type = static_cast<uint32_t>(*NativeType);
  const StringRef NativeName(
      reinterpret_cast<const char *>(Relocation.Extension.Bytes.data() +
                                     NameOffset),
      NameLength);
  const StringRef OfficialName =
      object::getELFRelocationTypeName(ELF::EM_AARCH64, Type);
  if (OfficialName == "Unknown" || NativeName != OfficialName)
    return invalid(Boundary, "release ObjectGraph relocation " +
                                 Twine(Relocation.ID) +
                                 " does not carry its official relocation "
                                 "name");

  const std::optional<uint8_t> LoaderWidth =
      AndroidKernelModuleRelocationPolicy::writeWidth(Type);
  if (!LoaderWidth)
    return invalid(Boundary, "release ObjectGraph relocation " +
                                 Twine(Relocation.ID) +
                                 " is not supported by the Android AArch64 "
                                 "module loader");
  const std::optional<NativeRelocationFacts> NativeFacts =
      nativeRelocationFacts(Target, Type);
  if (!NativeFacts || NativeFacts->IsNoOp ||
      NativeFacts->Width != Relocation.Width ||
      NativeFacts->IsPCRelative != Relocation.IsPCRelative ||
      NativeFacts->IsSigned != Relocation.IsSigned ||
      NativeFacts->Kind != Relocation.Kind ||
      *LoaderWidth * 8 != Relocation.Width)
    return invalid(Boundary, "release ObjectGraph relocation " +
                                 Twine(Relocation.ID) +
                                 " stable relocation facts disagree with "
                                 "its native AArch64 type");
  return Type;
}

Triple graphTriple(const PluginObjectGraph &Object) {
  const NevercTargetKey Target = Object.targetKey();
  const StringRef TripleText(Target.RawTriple.Data ? Target.RawTriple.Data : "",
                             static_cast<size_t>(Target.RawTriple.Length));
  return Triple(Triple::normalize(TripleText));
}

Expected<uint64_t> sectionSize(const PluginObjectSection &Section,
                               StringRef Boundary) {
  const uint64_t Initialized = Section.Data.size();
  if (Section.ZeroFillSize > std::numeric_limits<uint64_t>::max() - Initialized)
    return invalid(Boundary,
                   "section '" + Twine(Section.Name) + "' size overflows");
  return Initialized + Section.ZeroFillSize;
}

uint32_t readU32(ArrayRef<uint8_t> Bytes, size_t Offset) {
  uint32_t Value = 0;
  for (unsigned I = 0; I != 4; ++I)
    Value |= static_cast<uint32_t>(Bytes[Offset + I]) << (I * 8);
  return Value;
}

} // namespace

Error verifyPortableAndroidKernelReleaseWriterSymbol(
    const PluginObjectSymbol &Symbol, NevercObjectSymbolBinding FinalBinding,
    StringRef Boundary) {
  if (Symbol.Name.empty())
    return invalid(Boundary,
                   "release ObjectGraph contains an anonymous native symbol; "
                   "the built-in portable ELF writer cannot reproduce "
                   "an empty native symbol name");
  if (Symbol.Definition == NEVERC_OBJECT_SYMBOL_DEFINITION_COMMON)
    return invalid(Boundary, "release ObjectGraph symbol '" +
                                 Twine(Symbol.Name) +
                                 "' is COMMON; compile final modules with "
                                 "-fno-common");
  if ((Symbol.Definition == NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED ||
       Symbol.Definition == NEVERC_OBJECT_SYMBOL_DEFINITION_ABSOLUTE) &&
      Symbol.Size != 0)
    return invalid(Boundary, "release ObjectGraph symbol '" +
                                 Twine(Symbol.Name) +
                                 "' has a nonzero size that the built-in ELF "
                                 "writer cannot round-trip for an undefined "
                                 "or absolute symbol");
  if (Symbol.ComdatID != 0)
    return invalid(Boundary, "release ObjectGraph symbol '" +
                                 Twine(Symbol.Name) +
                                 "' retains COMDAT metadata the built-in "
                                 "Android release writer cannot round-trip");
  if (Symbol.Definition == NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED &&
      FinalBinding == NEVERC_OBJECT_SYMBOL_BINDING_LOCAL)
    return invalid(Boundary, "release ObjectGraph undefined symbol '" +
                                 Twine(Symbol.Name) +
                                 "' has LOCAL binding, which the built-in "
                                 "ELF writer would serialize as GLOBAL");

  switch (FinalBinding) {
  case NEVERC_OBJECT_SYMBOL_BINDING_LOCAL:
  case NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL:
  case NEVERC_OBJECT_SYMBOL_BINDING_WEAK:
    break;
  case NEVERC_OBJECT_SYMBOL_BINDING_UNIQUE:
    return invalid(Boundary, "release ObjectGraph symbol '" +
                                 Twine(Symbol.Name) +
                                 "' uses UNIQUE binding, which the built-in "
                                 "ELF writer cannot round-trip");
  case NEVERC_OBJECT_SYMBOL_BINDING_FORMAT_EXTENSION:
    return invalid(Boundary, "release ObjectGraph symbol '" +
                                 Twine(Symbol.Name) +
                                 "' uses a format-extension binding, which "
                                 "the built-in ELF writer cannot round-trip");
  default:
    return invalid(Boundary, "release ObjectGraph symbol '" +
                                 Twine(Symbol.Name) +
                                 "' uses an unknown binding");
  }

  uint8_t OriginalNativeBinding = 0;
  switch (Symbol.Binding) {
  case NEVERC_OBJECT_SYMBOL_BINDING_LOCAL:
    OriginalNativeBinding = ELF::STB_LOCAL;
    break;
  case NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL:
    OriginalNativeBinding = ELF::STB_GLOBAL;
    break;
  case NEVERC_OBJECT_SYMBOL_BINDING_WEAK:
    OriginalNativeBinding = ELF::STB_WEAK;
    break;
  case NEVERC_OBJECT_SYMBOL_BINDING_UNIQUE:
  case NEVERC_OBJECT_SYMBOL_BINDING_FORMAT_EXTENSION:
    return invalid(Boundary, "release ObjectGraph symbol '" +
                                 Twine(Symbol.Name) +
                                 "' has an original binding the built-in ELF "
                                 "writer cannot round-trip");
  default:
    return invalid(Boundary, "release ObjectGraph symbol '" +
                                 Twine(Symbol.Name) +
                                 "' uses an unknown original binding");
  }

  uint8_t NativeVisibility = 0;
  switch (Symbol.Visibility) {
  case NEVERC_OBJECT_SYMBOL_VISIBILITY_DEFAULT:
    NativeVisibility = ELF::STV_DEFAULT;
    break;
  case NEVERC_OBJECT_SYMBOL_VISIBILITY_HIDDEN:
    NativeVisibility = ELF::STV_HIDDEN;
    break;
  case NEVERC_OBJECT_SYMBOL_VISIBILITY_PROTECTED:
    return invalid(Boundary, "release ObjectGraph symbol '" +
                                 Twine(Symbol.Name) +
                                 "' uses PROTECTED visibility, which the "
                                 "built-in ELF writer cannot round-trip");
  case NEVERC_OBJECT_SYMBOL_VISIBILITY_INTERNAL:
    return invalid(Boundary, "release ObjectGraph symbol '" +
                                 Twine(Symbol.Name) +
                                 "' uses INTERNAL visibility, which the "
                                 "built-in ELF writer cannot round-trip");
  case NEVERC_OBJECT_SYMBOL_VISIBILITY_FORMAT_EXTENSION:
    return invalid(Boundary, "release ObjectGraph symbol '" +
                                 Twine(Symbol.Name) +
                                 "' uses format-extension visibility, which "
                                 "the built-in ELF writer cannot round-trip");
  default:
    return invalid(Boundary, "release ObjectGraph symbol '" +
                                 Twine(Symbol.Name) +
                                 "' uses unknown visibility");
  }

  uint8_t NativeType = ELF::STT_NOTYPE;
  switch (Symbol.Type) {
  case NEVERC_OBJECT_SYMBOL_TYPE_NO_TYPE:
    break;
  case NEVERC_OBJECT_SYMBOL_TYPE_OBJECT:
    if (Symbol.Definition != NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED)
      return invalid(Boundary, "release ObjectGraph symbol '" +
                                   Twine(Symbol.Name) +
                                   "' has an OBJECT type that the built-in "
                                   "ELF writer cannot round-trip for a "
                                   "non-section definition");
    NativeType = ELF::STT_OBJECT;
    break;
  case NEVERC_OBJECT_SYMBOL_TYPE_FUNCTION:
    if (Symbol.Definition != NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED)
      return invalid(Boundary, "release ObjectGraph symbol '" +
                                   Twine(Symbol.Name) +
                                   "' has a FUNCTION type that the built-in "
                                   "ELF writer cannot round-trip for a "
                                   "non-section definition");
    NativeType = ELF::STT_FUNC;
    break;
  case NEVERC_OBJECT_SYMBOL_TYPE_SECTION:
    return invalid(Boundary, "release ObjectGraph symbol '" +
                                 Twine(Symbol.Name) +
                                 "' has SECTION type, which the built-in ELF "
                                 "writer cannot round-trip");
  case NEVERC_OBJECT_SYMBOL_TYPE_TLS:
    return invalid(Boundary, "release ObjectGraph symbol '" +
                                 Twine(Symbol.Name) +
                                 "' has unsupported Android release TLS type");
  case NEVERC_OBJECT_SYMBOL_TYPE_FILE:
    return invalid(Boundary, "release ObjectGraph symbol '" +
                                 Twine(Symbol.Name) +
                                 "' has unsupported Android release FILE "
                                 "type");
  case NEVERC_OBJECT_SYMBOL_TYPE_INDIRECT_FUNCTION:
    return invalid(Boundary, "release ObjectGraph symbol '" +
                                 Twine(Symbol.Name) +
                                 "' has unsupported Android release GNU "
                                 "IFUNC type");
  case NEVERC_OBJECT_SYMBOL_TYPE_FORMAT_EXTENSION:
    return invalid(Boundary, "release ObjectGraph symbol '" +
                                 Twine(Symbol.Name) +
                                 "' has unsupported Android release "
                                 "format-extension type");
  default:
    return invalid(Boundary, "release ObjectGraph symbol '" +
                                 Twine(Symbol.Name) + "' uses an unknown type");
  }

  if (Symbol.Extension.empty())
    return Error::success();
  using namespace builtinext;
  if (!hasTag(Symbol.Extension.Bytes, SymbolTag))
    return invalid(Boundary, "release ObjectGraph symbol '" +
                                 Twine(Symbol.Name) +
                                 "' has native extension facts the built-in "
                                 "ELF writer cannot round-trip");
  const uint32_t EmbeddedVersion = version(Symbol.Extension.Bytes);
  if (Symbol.Extension.Version != EmbeddedVersion || EmbeddedVersion < 1 ||
      EmbeddedVersion > SymbolVersion)
    return invalid(Boundary, "release ObjectGraph symbol '" +
                                 Twine(Symbol.Name) +
                                 "' has native extension version metadata the "
                                 "built-in ELF writer cannot round-trip");
  const size_t ExpectedPayloadSize =
      HeaderSize +
      ((EmbeddedVersion == 1 ? SymbolAuxiliary : SymbolNameState) + 1) *
          sizeof(uint64_t);
  if (Symbol.Extension.Bytes.size() != ExpectedPayloadSize)
    return invalid(Boundary, "release ObjectGraph symbol '" +
                                 Twine(Symbol.Name) +
                                 "' must carry the exact version-" +
                                 Twine(EmbeddedVersion) +
                                 " payload "
                                 "for its native symbol extension");
  const std::optional<uint64_t> ExtendedType =
      field(Symbol.Extension.Bytes, SymbolType);
  const std::optional<uint64_t> ExtendedBinding =
      field(Symbol.Extension.Bytes, SymbolBinding);
  const std::optional<uint64_t> ExtendedOther =
      field(Symbol.Extension.Bytes, SymbolOther);
  const std::optional<uint64_t> ExtendedAuxiliary =
      field(Symbol.Extension.Bytes, SymbolAuxiliary);
  if (!ExtendedType || !ExtendedBinding || !ExtendedOther || !ExtendedAuxiliary)
    return invalid(Boundary, "release ObjectGraph symbol '" +
                                 Twine(Symbol.Name) +
                                 "' has incomplete native extension facts "
                                 "that the built-in ELF writer cannot "
                                 "round-trip");
  if (*ExtendedType != NativeType ||
      *ExtendedBinding != OriginalNativeBinding ||
      (*ExtendedOther & 3) != NativeVisibility || (*ExtendedOther & ~3) != 0)
    return invalid(Boundary, "release ObjectGraph symbol '" +
                                 Twine(Symbol.Name) +
                                 "' has native type/binding/visibility facts "
                                 "the built-in ELF writer cannot round-trip");
  if (*ExtendedAuxiliary != Symbol.Size)
    return invalid(Boundary, "release ObjectGraph symbol '" +
                                 Twine(Symbol.Name) +
                                 "' native st_size differs from ObjectGraph "
                                 "size");
  if (EmbeddedVersion >= 2) {
    const std::optional<uint64_t> NameState =
        field(Symbol.Extension.Bytes, SymbolNameState);
    if (!NameState || *NameState > SymbolNameEmpty)
      return invalid(Boundary, "release ObjectGraph symbol '" +
                                   Twine(Symbol.Name) +
                                   "' has an invalid native symbol name "
                                   "provenance state");
    if (*NameState != SymbolNameNonEmpty)
      return invalid(Boundary,
                     "release ObjectGraph symbol '" + Twine(Symbol.Name) +
                         "' originated with an empty native name, which the "
                         "built-in portable ELF writer cannot reproduce");
  }
  return Error::success();
}

Expected<std::vector<uint8_t>>
planPortableAndroidKernelReleaseWriterSymbolExtension(
    const PluginObjectSymbol &Symbol, NevercObjectSymbolBinding FinalBinding,
    StringRef Boundary) {
  if (Error E = verifyPortableAndroidKernelReleaseWriterSymbol(
          Symbol, FinalBinding, Boundary))
    return std::move(E);

  if (Symbol.Extension.empty())
    return std::vector<uint8_t>();

  uint64_t NativeBinding = 0;
  switch (FinalBinding) {
  case NEVERC_OBJECT_SYMBOL_BINDING_LOCAL:
    NativeBinding = ELF::STB_LOCAL;
    break;
  case NEVERC_OBJECT_SYMBOL_BINDING_GLOBAL:
    NativeBinding = ELF::STB_GLOBAL;
    break;
  case NEVERC_OBJECT_SYMBOL_BINDING_WEAK:
    NativeBinding = ELF::STB_WEAK;
    break;
  case NEVERC_OBJECT_SYMBOL_BINDING_UNIQUE:
  case NEVERC_OBJECT_SYMBOL_BINDING_FORMAT_EXTENSION:
  default:
    return invalid(Boundary,
                   "release ObjectGraph symbol has a final binding the "
                   "built-in ELF writer cannot encode");
  }

  std::vector<uint8_t> Rewritten = Symbol.Extension.Bytes;
  const size_t Offset =
      builtinext::HeaderSize + builtinext::SymbolBinding * sizeof(uint64_t);
  for (unsigned I = 0; I != sizeof(uint64_t); ++I)
    Rewritten[Offset + I] = static_cast<uint8_t>(NativeBinding >> (I * 8));
  return Rewritten;
}

Error verifyPortableAndroidKernelReleaseWriterSection(
    const PluginObjectSection &Section, StringRef Boundary) {
  constexpr NevercObjectSectionFlags WriterStableFlags =
      NEVERC_OBJECT_SECTION_ALLOCATED | NEVERC_OBJECT_SECTION_EXECUTABLE |
      NEVERC_OBJECT_SECTION_WRITABLE | NEVERC_OBJECT_SECTION_MERGEABLE |
      NEVERC_OBJECT_SECTION_STRINGS | NEVERC_OBJECT_SECTION_TLS;
  constexpr NevercObjectSectionFlags InformationalStableFlags =
      NEVERC_OBJECT_SECTION_DEBUG | NEVERC_OBJECT_SECTION_UNWIND;
  constexpr NevercObjectSectionFlags UnsupportedStableFlags =
      NEVERC_OBJECT_SECTION_DISCARDABLE | NEVERC_OBJECT_SECTION_RETAIN;
  constexpr NevercObjectSectionFlags KnownStableFlags =
      WriterStableFlags | InformationalStableFlags | UnsupportedStableFlags;

  if ((Section.Flags & ~KnownStableFlags) != 0 ||
      (Section.Flags & UnsupportedStableFlags) != 0)
    return invalid(Boundary, "release ObjectGraph section '" +
                                 Twine(Section.Name) +
                                 "' has stable section flags the built-in "
                                 "ELF writer cannot round-trip");
  if (Section.ComdatID != 0)
    return invalid(Boundary, "release ObjectGraph section '" +
                                 Twine(Section.Name) +
                                 "' retains COMDAT metadata, which would "
                                 "produce a rejected SHT_GROUP section");
  const bool Mergeable = (Section.Flags & NEVERC_OBJECT_SECTION_MERGEABLE) != 0;
  const bool Strings = (Section.Flags & NEVERC_OBJECT_SECTION_STRINGS) != 0;
  if (Strings && !Mergeable)
    return invalid(Boundary, "release ObjectGraph section '" +
                                 Twine(Section.Name) +
                                 "' has STRINGS without MERGEABLE");
  if (Section.Extension.empty()) {
    if (Mergeable)
      return invalid(Boundary, "release ObjectGraph mergeable section '" +
                                   Twine(Section.Name) +
                                   "' has no nonzero entry size");
    return Error::success();
  }

  using namespace builtinext;
  if (!hasTag(Section.Extension.Bytes, SectionTag))
    return invalid(Boundary, "release ObjectGraph section '" +
                                 Twine(Section.Name) +
                                 "' has an unsupported native section "
                                 "extension");
  const uint32_t EmbeddedVersion = version(Section.Extension.Bytes);
  if (Section.Extension.Version != EmbeddedVersion)
    return invalid(Boundary, "release ObjectGraph section '" +
                                 Twine(Section.Name) +
                                 "' native extension version metadata "
                                 "disagrees with its payload");
  if (EmbeddedVersion < 1 || EmbeddedVersion > SectionVersion)
    return invalid(Boundary, "release ObjectGraph section '" +
                                 Twine(Section.Name) +
                                 "' uses an unsupported native section "
                                 "extension version");
  const size_t ExpectedPayloadSize =
      HeaderSize +
      ((EmbeddedVersion == 1 ? SectionOffset : SectionEntrySize) + 1) *
          sizeof(uint64_t);
  if (Section.Extension.Bytes.size() != ExpectedPayloadSize)
    return invalid(Boundary, "release ObjectGraph section '" +
                                 Twine(Section.Name) +
                                 "' must carry the exact version-" +
                                 Twine(EmbeddedVersion) +
                                 " payload for its native section extension");

  const std::optional<uint64_t> NativeType =
      field(Section.Extension.Bytes, SectionType);
  const std::optional<uint64_t> NativeFlags =
      field(Section.Extension.Bytes, SectionFlags);
  const std::optional<uint64_t> NativeEntrySize =
      EmbeddedVersion >= 2 ? field(Section.Extension.Bytes, SectionEntrySize)
                           : std::optional<uint64_t>(0);
  if (!NativeType || !NativeFlags || !NativeEntrySize)
    return invalid(Boundary, "release ObjectGraph section '" +
                                 Twine(Section.Name) +
                                 "' has incomplete native section facts");

  const bool ZeroFill =
      Section.Kind == NEVERC_OBJECT_SECTION_KIND_ZERO_FILL ||
      Section.Kind == NEVERC_OBJECT_SECTION_KIND_TLS_ZERO_FILL;
  const uint64_t ExpectedType = ZeroFill ? ELF::SHT_NOBITS : ELF::SHT_PROGBITS;
  if (*NativeType != ExpectedType)
    return invalid(Boundary, "release ObjectGraph section '" +
                                 Twine(Section.Name) +
                                 "' has a native section type the built-in "
                                 "ELF writer cannot round-trip");

  uint64_t ExpectedFlags = 0;
  if ((Section.Flags & NEVERC_OBJECT_SECTION_ALLOCATED) != 0)
    ExpectedFlags |= ELF::SHF_ALLOC;
  if ((Section.Flags & NEVERC_OBJECT_SECTION_WRITABLE) != 0)
    ExpectedFlags |= ELF::SHF_WRITE;
  if ((Section.Flags & NEVERC_OBJECT_SECTION_EXECUTABLE) != 0)
    ExpectedFlags |= ELF::SHF_EXECINSTR;
  if (Mergeable)
    ExpectedFlags |= ELF::SHF_MERGE;
  if (Strings)
    ExpectedFlags |= ELF::SHF_STRINGS;
  if ((Section.Flags & NEVERC_OBJECT_SECTION_TLS) != 0)
    ExpectedFlags |= ELF::SHF_TLS;
  if (*NativeFlags != ExpectedFlags)
    return invalid(Boundary, "release ObjectGraph section '" +
                                 Twine(Section.Name) +
                                 "' has native section flags the built-in "
                                 "ELF writer cannot round-trip");
  if (Mergeable && *NativeEntrySize == 0)
    return invalid(Boundary, "release ObjectGraph mergeable section '" +
                                 Twine(Section.Name) +
                                 "' has no nonzero entry size");
  if (!Mergeable && *NativeEntrySize != 0)
    return invalid(Boundary, "release ObjectGraph section '" +
                                 Twine(Section.Name) +
                                 "' has an entry size the built-in ELF "
                                 "writer would discard");
  return Error::success();
}

Error verifyPortableAndroidKernelReleaseWriterRelocation(
    const PluginObjectGraph &Object, const PluginObjectRelocation &Relocation,
    StringRef Boundary) {
  const Triple Target = graphTriple(Object);
  // Generic unit graphs exercise the format-neutral finalizer without going
  // through the Android writer. The public writer entry point below rejects
  // every non-AArch64 target before a sink is opened.
  if (!Target.isOSBinFormatELF() || Target.getArch() != Triple::aarch64)
    return Error::success();

  auto NativeType =
      verifyCanonicalNativeRelocationFacts(Object, Relocation, Boundary);
  if (!NativeType)
    return NativeType.takeError();

  switch (Relocation.TargetKind) {
  case NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL:
    if (Relocation.TargetValue != 0)
      return invalid(Boundary, "release ObjectGraph symbol relocation " +
                                   Twine(Relocation.ID) +
                                   " has an unrepresentable target value");
    break;
  case NEVERC_OBJECT_RELOCATION_TARGET_SECTION: {
    const PluginObjectSection *TargetSection =
        Object.findSection(Relocation.TargetSectionID);
    if (!TargetSection)
      return invalid(Boundary, "release ObjectGraph relocation " +
                                   Twine(Relocation.ID) +
                                   " references no target section");
    auto TargetSize = sectionSize(*TargetSection, Boundary);
    if (!TargetSize)
      return TargetSize.takeError();
    if (Relocation.TargetValue > *TargetSize)
      return invalid(Boundary, "release ObjectGraph relocation " +
                                   Twine(Relocation.ID) +
                                   " target value is outside its target "
                                   "section");
    if (Relocation.TargetValue >
        static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
      return invalid(Boundary, "release ObjectGraph relocation " +
                                   Twine(Relocation.ID) +
                                   " target value exceeds the writer's "
                                   "signed expression range");
    int64_t Combined = 0;
    if (AddOverflow(static_cast<int64_t>(Relocation.TargetValue),
                    Relocation.Addend, Combined))
      return invalid(Boundary, "release ObjectGraph relocation " +
                                   Twine(Relocation.ID) +
                                   " target value plus addend overflows");
    break;
  }
  case NEVERC_OBJECT_RELOCATION_TARGET_ABSOLUTE:
  case NEVERC_OBJECT_RELOCATION_TARGET_FORMAT_EXTENSION:
    return invalid(Boundary, "release ObjectGraph relocation " +
                                 Twine(Relocation.ID) +
                                 " has a target kind the built-in ELF writer "
                                 "cannot round-trip");
  default:
    return invalid(Boundary, "release ObjectGraph relocation " +
                                 Twine(Relocation.ID) +
                                 " has an unknown target kind");
  }
  return Error::success();
}

Error verifyCanonicalAndroidKernelReleaseReaderSection(
    const PluginObjectSection &Section, uint64_t ExpectedNativeIndex,
    StringRef Boundary) {
  using namespace builtinext;
  constexpr size_t ExactSize =
      HeaderSize + (SectionEntrySize + 1) * sizeof(uint64_t);
  if (!hasTag(Section.Extension.Bytes, SectionTag) ||
      Section.Extension.Version != SectionVersion ||
      version(Section.Extension.Bytes) != SectionVersion ||
      Section.Extension.Bytes.size() != ExactSize)
    return invalid(Boundary, "canonical release section '" +
                                 Twine(Section.Name) +
                                 "' must carry the exact NCSE v2 payload "
                                 "emitted by the built-in ELF reader");

  const std::optional<uint64_t> NativeIndex =
      field(Section.Extension.Bytes, SectionIndex);
  const std::optional<uint64_t> NativeAddress =
      field(Section.Extension.Bytes, SectionAddress);
  const std::optional<uint64_t> NativeType =
      field(Section.Extension.Bytes, SectionType);
  const std::optional<uint64_t> NativeFlags =
      field(Section.Extension.Bytes, SectionFlags);
  const std::optional<uint64_t> NativeOffset =
      field(Section.Extension.Bytes, SectionOffset);
  const std::optional<uint64_t> NativeEntrySize =
      field(Section.Extension.Bytes, SectionEntrySize);
  if (!NativeIndex || !NativeAddress || !NativeType || !NativeFlags ||
      !NativeOffset || !NativeEntrySize)
    return invalid(Boundary, "canonical release section '" +
                                 Twine(Section.Name) +
                                 "' has incomplete NCSE v2 facts");
  if (*NativeIndex != ExpectedNativeIndex)
    return invalid(Boundary, "canonical release section '" +
                                 Twine(Section.Name) +
                                 "' NCSE index disagrees with final section "
                                 "order");
  if (*NativeAddress != 0)
    return invalid(Boundary, "canonical release section '" +
                                 Twine(Section.Name) +
                                 "' has nonzero ET_REL sh_addr provenance");

  if (*NativeType > std::numeric_limits<uint32_t>::max())
    return invalid(Boundary, "canonical release section '" +
                                 Twine(Section.Name) +
                                 "' has an invalid native section type");

  const NativeELFSectionProjection Projection =
      projectNativeELFSection(Section.Name, *NativeType, *NativeFlags);
  if (Section.Kind != Projection.Kind)
    return invalid(Boundary, "canonical release section '" +
                                 Twine(Section.Name) +
                                 "' native facts disagree with stable kind");
  if (Section.Flags != Projection.Flags)
    return invalid(Boundary, "canonical release section '" +
                                 Twine(Section.Name) +
                                 "' native flags disagree with stable flags");
  if ((Section.Flags & NEVERC_OBJECT_SECTION_STRINGS) != 0 &&
      (Section.Flags & NEVERC_OBJECT_SECTION_MERGEABLE) == 0)
    return invalid(Boundary, "canonical release section '" +
                                 Twine(Section.Name) +
                                 "' has SHF_STRINGS without SHF_MERGE");
  if ((Section.Flags & NEVERC_OBJECT_SECTION_MERGEABLE) != 0 &&
      *NativeEntrySize == 0)
    return invalid(Boundary, "canonical release mergeable section '" +
                                 Twine(Section.Name) +
                                 "' has zero native sh_entsize");
  return Error::success();
}

Error verifyCanonicalAndroidKernelReleaseReaderRelocation(
    const PluginObjectGraph &Object, const PluginObjectRelocation &Relocation,
    StringRef Boundary) {
  auto NativeType =
      verifyCanonicalNativeRelocationFacts(Object, Relocation, Boundary);
  if (!NativeType)
    return NativeType.takeError();

  switch (Relocation.TargetKind) {
  case NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL:
    if (Relocation.TargetValue != 0)
      return invalid(Boundary, "canonical release symbol relocation " +
                                   Twine(Relocation.ID) +
                                   " has a nonzero target value");
    break;
  case NEVERC_OBJECT_RELOCATION_TARGET_SECTION: {
    const PluginObjectSection *TargetSection =
        Object.findSection(Relocation.TargetSectionID);
    if (!TargetSection)
      return invalid(Boundary, "canonical release relocation " +
                                   Twine(Relocation.ID) +
                                   " references no target section");
    auto TargetSize = sectionSize(*TargetSection, Boundary);
    if (!TargetSize)
      return TargetSize.takeError();
    if (Relocation.TargetValue > *TargetSize)
      return invalid(Boundary, "canonical release relocation " +
                                   Twine(Relocation.ID) +
                                   " target value is outside its target "
                                   "section");
    break;
  }
  case NEVERC_OBJECT_RELOCATION_TARGET_FORMAT_EXTENSION:
    // LLVM reports r_sym == 0 as symbol_end(). The built-in reader preserves
    // that native null-symbol target with the relocation-type-plus-one token;
    // it is lossless in unchanged native-image passthrough even though MC's
    // portable expression writer cannot synthesize it.
    if (Relocation.TargetValue != 0 ||
        Relocation.TargetExtensionKind != *NativeType + 1)
      return invalid(Boundary, "canonical release relocation " +
                                   Twine(Relocation.ID) +
                                   " native null-symbol target disagrees "
                                   "with its NCRL type");
    break;
  case NEVERC_OBJECT_RELOCATION_TARGET_ABSOLUTE:
    return invalid(Boundary, "canonical release relocation " +
                                 Twine(Relocation.ID) +
                                 " has an unsupported absolute target");
  default:
    return invalid(Boundary, "canonical release relocation " +
                                 Twine(Relocation.ID) +
                                 " has an unknown target kind");
  }
  return Error::success();
}

Error verifyPortableAndroidKernelReleaseWriterGraph(
    const PluginObjectGraph &Object, StringRef Boundary) {
  if (Error E = verifyPluginObjectGraph(Object))
    return joinErrors(invalid(Boundary, "invalid release ObjectGraph"),
                      std::move(E));

  const Triple Target = graphTriple(Object);
  if (!Target.isOSBinFormatELF() || Target.getArch() != Triple::aarch64)
    return invalid(Boundary,
                   "Android kernel release graph writing requires AArch64 "
                   "ELF");

  for (const PluginObjectSection &Section : Object.sections())
    if (Error E =
            verifyPortableAndroidKernelReleaseWriterSection(Section, Boundary))
      return E;
  for (const PluginObjectSymbol &Symbol : Object.symbols())
    if (Error E = verifyPortableAndroidKernelReleaseWriterSymbol(
            Symbol, Symbol.Binding, Boundary))
      return E;
  for (const PluginObjectRelocation &Relocation : Object.relocations())
    if (Error E = verifyPortableAndroidKernelReleaseWriterRelocation(
            Object, Relocation, Boundary))
      return E;
  return Error::success();
}

} // namespace neverc::plugin
