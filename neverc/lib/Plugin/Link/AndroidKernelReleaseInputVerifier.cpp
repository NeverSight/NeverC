#include "AndroidKernelReleaseInputVerifier.h"

#include "neverc/Foundation/AndroidKernelModuleSectionPolicy.h"
#include "neverc/Foundation/AndroidKernelModuleSymbolPolicy.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Object/ELF.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/Support/SHA256.h"
#include "llvm/TargetParser/Triple.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <optional>
#include <tuple>

using namespace llvm;

namespace neverc::plugin {
namespace {

Error invalid(StringRef Boundary, const Twine &Message) {
  return createStringError(errc::invalid_argument,
                           (Boundary + ": " + Message).str());
}

struct NativeInputFacts {
  AndroidKernelReleaseELFABI ABI;
  bool HasRetainedAnonymousSymbols = false;
  std::vector<AndroidKernelReleaseAnonymousSectionFact>
      RetainedAnonymousSections;
};

bool isRetainedLogicalSection(uint32_t Index, uint32_t SectionStringTableIndex,
                              uint32_t SymbolStringTableIndex, uint32_t Type) {
  if (Index == 0 || Index == SectionStringTableIndex ||
      Index == SymbolStringTableIndex)
    return false;
  return !AndroidKernelModuleSectionPolicy::regeneratesReleaseInputType(Type);
}

Expected<AndroidKernelReleaseAnonymousSectionFact>
anonymousSectionFact(const object::ELF64LEFile &File,
                     const object::ELF64LE::Shdr &Section, StringRef Boundary) {
  AndroidKernelReleaseAnonymousSectionFact Fact;
  Fact.Type = Section.sh_type;
  Fact.Flags = Section.sh_flags;
  Fact.Size = Section.sh_size;
  Fact.Alignment = Section.sh_addralign;
  Fact.EntrySize = Section.sh_entsize;
  Fact.Link = Section.sh_link;
  Fact.Info = Section.sh_info;
  if (Section.sh_type != ELF::SHT_NOBITS) {
    auto Contents = File.getSectionContents(Section);
    if (!Contents)
      return joinErrors(
          invalid(Boundary,
                  "retained anonymous section payload is outside the file"),
          Contents.takeError());
    Fact.PayloadDigest = SHA256::hash(ArrayRef<uint8_t>(
        reinterpret_cast<const uint8_t *>(Contents->data()), Contents->size()));
  }
  return Fact;
}

Expected<StringRef> symbolName(StringRef Strings, uint32_t Offset,
                               StringRef Boundary) {
  if (Offset >= Strings.size())
    return invalid(Boundary,
                   "symbol name offset is outside the selected string table");
  const char *Begin = Strings.data() + Offset;
  const size_t Available = Strings.size() - Offset;
  const size_t Length = strnlen(Begin, Available);
  if (Length == Available)
    return invalid(Boundary, "symbol name is not NUL-terminated");
  return StringRef(Begin, Length);
}

Expected<NativeInputFacts> verifyNativeInput(ArrayRef<uint8_t> Image,
                                             StringRef Boundary) {
  using ELFT = object::ELF64LE;
  auto Parsed = object::ObjectFile::createObjectFile(MemoryBufferRef(
      StringRef(reinterpret_cast<const char *>(Image.data()), Image.size()),
      Boundary));
  if (!Parsed)
    return joinErrors(invalid(Boundary, "is not a parseable object"),
                      Parsed.takeError());
  const auto *ELFObject = dyn_cast<object::ELF64LEObjectFile>(Parsed->get());
  if (!ELFObject)
    return invalid(Boundary, "Android module release input must be ELF64LE");

  const auto &Header = ELFObject->getELFFile().getHeader();
  if (Header.e_type != ELF::ET_REL || Header.e_version != ELF::EV_CURRENT ||
      Header.e_ehsize != sizeof(ELFT::Ehdr) ||
      Header.e_shentsize != sizeof(ELFT::Shdr))
    return invalid(Boundary,
                   "Android module release input has an unsupported ELF "
                   "ET_REL header");
  if (Header.e_machine != ELF::EM_AARCH64)
    return invalid(Boundary,
                   "Android module release input must target AArch64");

  const object::ELF64LEFile &File = ELFObject->getELFFile();
  auto SectionsOrError = File.sections();
  if (!SectionsOrError)
    return joinErrors(invalid(Boundary, "has an invalid section table"),
                      SectionsOrError.takeError());
  const ArrayRef<ELFT::Shdr> Sections = *SectionsOrError;
  if (Header.e_shnum == 0 || Header.e_shnum >= ELF::SHN_LORESERVE ||
      Header.e_shstrndx >= ELF::SHN_LORESERVE ||
      Header.e_shstrndx >= Sections.size() ||
      Sections[Header.e_shstrndx].sh_type != ELF::SHT_STRTAB)
    return invalid(Boundary,
                   "uses unsupported or invalid section-table numbering");

  std::optional<uint32_t> SymbolTableIndex;
  ArrayRef<ELFT::Sym> Symbols;
  StringRef SymbolStrings;
  for (uint32_t Index = 0; Index != Sections.size(); ++Index) {
    const ELFT::Shdr &Section = Sections[Index];
    if (Section.sh_type == ELF::SHT_DYNSYM)
      return invalid(Boundary,
                     "contains SHT_DYNSYM metadata hidden from ObjectGraph");
    if (Section.sh_type != ELF::SHT_SYMTAB)
      continue;
    if (SymbolTableIndex || Section.sh_entsize != sizeof(ELFT::Sym) ||
        Section.sh_size == 0 || Section.sh_size % sizeof(ELFT::Sym) != 0 ||
        Section.sh_link >= Sections.size() ||
        Sections[Section.sh_link].sh_type != ELF::SHT_STRTAB)
      return invalid(Boundary, "has an invalid or ambiguous SHT_SYMTAB");
    auto NativeSymbols = File.symbols(&Section);
    auto NativeStrings = File.getStringTableForSymtab(Section);
    if (!NativeSymbols || !NativeStrings) {
      Error Cause = Error::success();
      if (!NativeSymbols)
        Cause = joinErrors(std::move(Cause), NativeSymbols.takeError());
      if (!NativeStrings)
        Cause = joinErrors(std::move(Cause), NativeStrings.takeError());
      return joinErrors(invalid(Boundary, "has an invalid symbol/string table"),
                        std::move(Cause));
    }
    SymbolTableIndex = Index;
    Symbols = *NativeSymbols;
    SymbolStrings = *NativeStrings;
  }
  if (!SymbolTableIndex)
    return invalid(Boundary, "has no SHT_SYMTAB");
  if (Error E = verifyAndroidKernelReleaseSymbolCount(Symbols.size(), Boundary))
    return std::move(E);

  const uint32_t SymbolStringTableIndex = Sections[*SymbolTableIndex].sh_link;
  DenseSet<uint32_t> RelocationTargetSymbols;
  std::vector<AndroidKernelReleaseAnonymousSectionFact>
      RetainedAnonymousSections;
  for (uint32_t Index = 0; Index != Sections.size(); ++Index) {
    const ELFT::Shdr &Section = Sections[Index];
    auto Name = File.getSectionName(Section);
    if (!Name)
      return joinErrors(invalid(Boundary, "has an invalid section name"),
                        Name.takeError());
    if (Section.sh_addr != 0)
      return invalid(Boundary, "ELF ET_REL section '" + *Name +
                                   "' has a nonzero sh_addr");
    if (Section.sh_addralign != 0 && !isPowerOf2_64(Section.sh_addralign))
      return invalid(Boundary, "ELF section '" + *Name +
                                   "' has a non-power-of-two alignment");
    if (AndroidKernelModuleSectionPolicy::rejectsReleaseInputType(
            Section.sh_type))
      return invalid(Boundary, "contains unsupported native section type " +
                                   Twine(Section.sh_type));
    if (Section.sh_type == ELF::SHT_STRTAB && Index != Header.e_shstrndx &&
        Index != SymbolStringTableIndex)
      return invalid(Boundary,
                     "contains an additional SHT_STRTAB that canonical "
                     "release output cannot represent");
    if (Section.sh_type == ELF::SHT_REL)
      return invalid(Boundary,
                     "contains unsupported SHT_REL section '" + *Name + "'");
    if (Name->empty() &&
        isRetainedLogicalSection(Index, Header.e_shstrndx,
                                 SymbolStringTableIndex, Section.sh_type)) {
      auto Fact = anonymousSectionFact(File, Section, Boundary);
      if (!Fact)
        return Fact.takeError();
      RetainedAnonymousSections.push_back(*Fact);
    }
    if (*Name == "__versions") {
      if (Section.sh_type != ELF::SHT_PROGBITS ||
          (Section.sh_flags & ELF::SHF_ALLOC) == 0 ||
          (Section.sh_flags & ELF::SHF_COMPRESSED) != 0)
        return invalid(Boundary,
                       "__versions contribution must be an allocated, "
                       "uncompressed SHT_PROGBITS section");
      if (Section.sh_addralign < 8 || !isPowerOf2_64(Section.sh_addralign))
        return invalid(Boundary,
                       "__versions contribution alignment must be a power "
                       "of two >= 8");
      if (Section.sh_size % 64 != 0)
        return invalid(Boundary,
                       "__versions contribution size must be a multiple of "
                       "64 bytes");
    }
    if (*Name == ".modinfo") {
      auto Contents = File.getSectionContents(Section);
      if (!Contents)
        return Contents.takeError();
      if (AndroidKernelModuleSymbolPolicy::containsLivePatchModInfo(*Contents))
        return invalid(Boundary,
                       "Android module release strip does not support a "
                       "module marked livepatch in .modinfo");
    }
    const uint64_t Flags = Section.sh_flags;
    if (AndroidKernelModuleSymbolPolicy::isLivePatchSectionName(*Name) ||
        (Flags &
         AndroidKernelModuleSymbolPolicy::LivePatchRelocationSectionFlag))
      return invalid(Boundary, "Android module release strip does not support "
                               "livepatch section '" +
                                   *Name + "'");

    if (Section.sh_type == ELF::SHT_RELA) {
      if (Section.sh_link != *SymbolTableIndex || Section.sh_info == 0 ||
          Section.sh_info >= Sections.size() ||
          Section.sh_entsize != sizeof(ELFT::Rela) ||
          Section.sh_size % sizeof(ELFT::Rela) != 0)
        return invalid(Boundary, "contains malformed SHT_RELA metadata");
      auto Relocations = File.relas(Section);
      if (!Relocations)
        return Relocations.takeError();
      for (const ELFT::Rela &Relocation : *Relocations) {
        const uint32_t SymbolIndex = Relocation.getSymbol();
        if (SymbolIndex >= Symbols.size())
          return invalid(Boundary,
                         "relocation references an out-of-range symbol");
        if (SymbolIndex != 0)
          RelocationTargetSymbols.insert(SymbolIndex);
      }
    }
  }

  llvm::sort(RetainedAnonymousSections);

  bool HasRetainedAnonymousSymbols = false;
  for (size_t Index = 0; Index != Symbols.size(); ++Index) {
    const ELFT::Sym &Symbol = Symbols[Index];
    auto Name = symbolName(SymbolStrings, Symbol.st_name, Boundary);
    if (!Name)
      return Name.takeError();
    const uint16_t SectionIndex = Symbol.st_shndx;
    if (SectionIndex == ELF::SHN_COMMON)
      return invalid(Boundary,
                     "Android module release strip refuses COMMON symbol '" +
                         *Name + "'; compile final modules with -fno-common");
    if (SectionIndex == AndroidKernelModuleSymbolPolicy::LivePatchSectionIndex)
      return invalid(Boundary, "Android module release strip does not support "
                               "livepatch symbol '" +
                                   *Name + "'");
    if (SectionIndex >= ELF::SHN_LORESERVE && SectionIndex != ELF::SHN_ABS)
      return invalid(Boundary, "Android module release strip refuses symbol '" +
                                   *Name +
                                   "' with an unsupported reserved section "
                                   "index");
    const uint8_t Type = Symbol.getType();
    if (Index != 0 && Name->empty() && Type != ELF::STT_SECTION &&
        Type != ELF::STT_FILE &&
        (Symbol.getBinding() != ELF::STB_LOCAL ||
         RelocationTargetSymbols.contains(static_cast<uint32_t>(Index))))
      HasRetainedAnonymousSymbols = true;
  }

  return NativeInputFacts{{Header.e_machine, Header.e_flags,
                           Header.e_ident[ELF::EI_OSABI],
                           Header.e_ident[ELF::EI_ABIVERSION]},
                          HasRetainedAnonymousSymbols,
                          std::move(RetainedAnonymousSections)};
}

} // namespace

bool AndroidKernelReleaseAnonymousSectionFact::operator==(
    const AndroidKernelReleaseAnonymousSectionFact &Other) const {
  return std::tie(Type, Flags, Size, Alignment, EntrySize, Link, Info,
                  PayloadDigest) ==
         std::tie(Other.Type, Other.Flags, Other.Size, Other.Alignment,
                  Other.EntrySize, Other.Link, Other.Info, Other.PayloadDigest);
}

bool AndroidKernelReleaseAnonymousSectionFact::operator<(
    const AndroidKernelReleaseAnonymousSectionFact &Other) const {
  return std::tie(Type, Flags, Size, Alignment, EntrySize, Link, Info,
                  PayloadDigest) <
         std::tie(Other.Type, Other.Flags, Other.Size, Other.Alignment,
                  Other.EntrySize, Other.Link, Other.Info, Other.PayloadDigest);
}

Expected<AndroidKernelReleaseInputContract>
verifyAndroidKernelReleaseObjectMergeInputs(
    ArrayRef<PluginObjectGraph *> Objects,
    ArrayRef<ArrayRef<uint8_t>> InputImages, NevercTargetKey Target,
    StringRef Boundary) {
  if (Objects.empty())
    return invalid(Boundary, "has no ObjectGraph inputs");
  if (InputImages.size() != Objects.size())
    return invalid(Boundary,
                   "requires one immutable native image per ObjectGraph");

  const StringRef TripleText(Target.RawTriple.Data ? Target.RawTriple.Data : "",
                             static_cast<size_t>(Target.RawTriple.Length));
  const Triple ParsedTarget(Triple::normalize(TripleText));
  if (ParsedTarget.getArch() != Triple::aarch64 ||
      !ParsedTarget.isOSBinFormatELF() || Target.PointerWidth != 64 ||
      Target.Endianness != NEVERC_TARGET_ENDIAN_LITTLE)
    return invalid(Boundary,
                   "requires an AArch64 ELF64 little-endian TargetKey");

  std::optional<AndroidKernelReleaseELFABI> ExpectedABI;
  bool HasRetainedAnonymousSymbols = false;
  std::vector<AndroidKernelReleaseAnonymousSectionFact>
      RetainedAnonymousSections;
  for (size_t Index = 0; Index != Objects.size(); ++Index) {
    if (!Objects[Index])
      return invalid(Boundary,
                     "ObjectGraph input " + Twine(Index) + " is null");
    if (InputImages[Index].empty())
      return invalid(Boundary,
                     "native input image " + Twine(Index) + " is empty");
    const std::string InputBoundary =
        (Boundary + " native input image " + Twine(Index)).str();
    auto Facts = verifyNativeInput(InputImages[Index], InputBoundary);
    if (!Facts)
      return Facts.takeError();
    if (!ExpectedABI)
      ExpectedABI = Facts->ABI;
    else if (Facts->ABI != *ExpectedABI)
      return invalid(Boundary, "native input image " + Twine(Index) +
                                   " has an inconsistent ELF ABI header");
    HasRetainedAnonymousSymbols |= Facts->HasRetainedAnonymousSymbols;
    RetainedAnonymousSections.insert(RetainedAnonymousSections.end(),
                                     Facts->RetainedAnonymousSections.begin(),
                                     Facts->RetainedAnonymousSections.end());
  }
  llvm::sort(RetainedAnonymousSections);
  return AndroidKernelReleaseInputContract(
      *ExpectedABI, HasRetainedAnonymousSymbols,
      std::move(RetainedAnonymousSections));
}

Expected<std::shared_ptr<const AndroidKernelReleaseBoundOutputContract>>
bindAndroidKernelReleaseNativeOutput(
    ArrayRef<uint8_t> Image, const AndroidKernelReleaseInputContract &Contract,
    const AndroidKernelReleaseNativeOutputBindingAuthority &,
    StringRef Boundary) {
  auto Facts = verifyNativeInput(Image, Boundary);
  if (!Facts)
    return Facts.takeError();
  if (!(Facts->ABI == Contract.abi()))
    return invalid(Boundary,
                   "ELF ABI header does not match immutable release inputs");
  if (Contract.hasRetainedAnonymousSections() &&
      Facts->RetainedAnonymousSections.empty())
    return invalid(Boundary,
                   "native merger dropped every retained anonymous section");

  return std::shared_ptr<const AndroidKernelReleaseBoundOutputContract>(
      new AndroidKernelReleaseBoundOutputContract(
          Contract, SHA256::hash(Image),
          std::move(Facts->RetainedAnonymousSections)));
}

Error verifyAndroidKernelReleaseOutputContract(
    ArrayRef<uint8_t> Image, const AndroidKernelReleaseInputContract &Contract,
    StringRef Boundary) {
  if (Contract.requiresNativeImagePassthrough())
    return invalid(Boundary,
                   "native-image passthrough contract has not been bound to "
                   "the trusted merger output");
  auto Facts = verifyNativeInput(Image, Boundary);
  if (!Facts)
    return Facts.takeError();
  if (!(Facts->ABI == Contract.abi()))
    return invalid(Boundary,
                   "ELF ABI header does not match immutable release inputs");
  if (ArrayRef<AndroidKernelReleaseAnonymousSectionFact>(
          Facts->RetainedAnonymousSections) !=
      Contract.retainedAnonymousSections())
    return invalid(Boundary,
                   "retained anonymous section multiset does not match the "
                   "native release contract");
  return Error::success();
}

Error verifyAndroidKernelReleaseOutputContract(
    ArrayRef<uint8_t> Image,
    const AndroidKernelReleaseBoundOutputContract &Contract,
    StringRef Boundary) {
  auto Facts = verifyNativeInput(Image, Boundary);
  if (!Facts)
    return Facts.takeError();
  if (!(Facts->ABI == Contract.inputContract().abi()))
    return invalid(Boundary,
                   "ELF ABI header does not match immutable release inputs");
  if (ArrayRef<AndroidKernelReleaseAnonymousSectionFact>(
          Facts->RetainedAnonymousSections) !=
      Contract.outputAnonymousSections())
    return invalid(Boundary,
                   "retained anonymous section multiset does not match the "
                   "native release contract");
  if (SHA256::hash(Image) != Contract.nativeOutputDigest())
    return invalid(Boundary,
                   "output bytes do not match the bound native merger image");
  return Error::success();
}

Error verifyAndroidKernelReleaseSymbolCount(uint64_t SymbolCount,
                                            StringRef Boundary) {
  if (SymbolCount > std::numeric_limits<uint32_t>::max())
    return invalid(Boundary,
                   "symbol table exceeds the ELF64 relocation-index range");
  return Error::success();
}

} // namespace neverc::plugin
