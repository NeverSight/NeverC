#include "NeverCTestFixture.h"

#include "neverc/Foundation/AndroidKernelModuleReleaseNames.h"
#include "neverc/Foundation/AndroidKernelModuleSymbolPolicy.h"
#include "neverc/Linker/Core/Driver/LTOCacheContract.h"

#include "llvm/ADT/StringExtras.h"
#include "llvm/Analysis/TypeMetadataUtils.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Object/ELF.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <tuple>
#include <utility>

// MSVC has no POSIX setenv/unsetenv; _putenv_s keeps the CRT and Win32
// environment blocks in sync so spawned neverc children inherit the value.
static void setEnvVar(const char *Name, const char *Value) {
#ifdef _WIN32
  _putenv_s(Name, Value);
#else
  setenv(Name, Value, 1);
#endif
}

static void unsetEnvVar(const char *Name) {
#ifdef _WIN32
  _putenv_s(Name, "");
#else
  unsetenv(Name);
#endif
}

class ScopedEnvVar {
  std::string Name;
  std::optional<std::string> OldValue;

public:
  explicit ScopedEnvVar(const char *Name) : Name(Name) {
    if (const char *Old = std::getenv(Name))
      OldValue = Old;
    unsetEnvVar(Name);
  }

  ScopedEnvVar(const char *Name, const char *Value) : Name(Name) {
    if (const char *Old = std::getenv(Name))
      OldValue = Old;
    setEnvVar(Name, Value);
  }

  ScopedEnvVar(const ScopedEnvVar &) = delete;
  ScopedEnvVar &operator=(const ScopedEnvVar &) = delete;

  ~ScopedEnvVar() {
    if (OldValue)
      setEnvVar(Name.c_str(), OldValue->c_str());
    else
      unsetEnvVar(Name.c_str());
  }
};

static double medianSeconds(std::vector<double> Values) {
  assert(!Values.empty());
  std::sort(Values.begin(), Values.end());
  return Values[Values.size() / 2];
}

static llvm::Expected<uint32_t>
readELFSymbolPrefix32(llvm::StringRef Bytes, llvm::StringRef SymbolName,
                      bool MatchPCGSuffix = false) {
  auto Object = llvm::object::ObjectFile::createObjectFile(
      llvm::MemoryBufferRef(Bytes, "android-kernel-kcfi-test"));
  if (!Object)
    return Object.takeError();
  if (!(*Object)->isELF() || !(*Object)->isLittleEndian())
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "expected a little-endian ELF object");

  for (const llvm::object::SymbolRef &Symbol : (*Object)->symbols()) {
    llvm::Expected<llvm::StringRef> Name = Symbol.getName();
    if (!Name)
      return Name.takeError();
    const bool IsPCGName =
        MatchPCGSuffix && Name->starts_with(SymbolName) &&
        Name->drop_front(SymbolName.size()).starts_with(".__pcg");
    if (*Name != SymbolName && !IsPCGName)
      continue;

    llvm::Expected<uint64_t> Address = Symbol.getAddress();
    if (!Address)
      return Address.takeError();
    llvm::Expected<llvm::object::section_iterator> Section =
        Symbol.getSection();
    if (!Section)
      return Section.takeError();
    if (*Section == (*Object)->section_end())
      return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                     "entry symbol has no section");

    llvm::Expected<llvm::StringRef> Contents = (*Section)->getContents();
    if (!Contents)
      return Contents.takeError();
    const uint64_t SectionAddress = (*Section)->getAddress();
    if (*Address < SectionAddress)
      return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                     "entry symbol precedes its section");
    const uint64_t Offset = *Address - SectionAddress;
    if (Offset < sizeof(uint32_t) || Offset > Contents->size())
      return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                     "entry symbol has no 32-bit prefix");
    return llvm::support::endian::read32le(Contents->data() + Offset -
                                           sizeof(uint32_t));
  }

  return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                 "entry symbol not found");
}

using AndroidKernelReleaseSymbolClass =
    neverc::AndroidKernelModuleSymbolPolicy::SymbolClass;

struct AndroidKernelReleaseSectionIdentity {
  // Keep the logical ELF header and raw contents stable across physical file
  // rewrites: textual section identities replace sh_name/sh_link/sh_info
  // indices, while sh_offset is deliberately excluded. Occurrence separates
  // otherwise byte-identical same-name sections without using raw shndx.
  std::string Name;
  uint32_t Type = 0;
  uint64_t Flags = 0;
  uint64_t Address = 0;
  uint64_t Size = 0;
  uint64_t Alignment = 0;
  uint64_t EntrySize = 0;
  std::string LinkedSection;
  std::string InfoSection;
  uint32_t OtherInfo = 0;
  std::string Contents;
  unsigned Occurrence = 0;

  bool operator<(const AndroidKernelReleaseSectionIdentity &Other) const {
    return std::tie(Name, Type, Flags, Address, Size, Alignment, EntrySize,
                    LinkedSection, InfoSection, OtherInfo, Contents,
                    Occurrence) <
           std::tie(Other.Name, Other.Type, Other.Flags, Other.Address,
                    Other.Size, Other.Alignment, Other.EntrySize,
                    Other.LinkedSection, Other.InfoSection, Other.OtherInfo,
                    Other.Contents, Other.Occurrence);
  }

  bool operator==(const AndroidKernelReleaseSectionIdentity &Other) const {
    return std::tie(Name, Type, Flags, Address, Size, Alignment, EntrySize,
                    LinkedSection, InfoSection, OtherInfo, Contents,
                    Occurrence) ==
           std::tie(Other.Name, Other.Type, Other.Flags, Other.Address,
                    Other.Size, Other.Alignment, Other.EntrySize,
                    Other.LinkedSection, Other.InfoSection, Other.OtherInfo,
                    Other.Contents, Other.Occurrence);
  }
};

struct AndroidKernelReleaseSection {
  unsigned Index = 0;
  std::string Name;
  AndroidKernelReleaseSectionIdentity Identity;
  uint64_t Size = 0;
  uint64_t Alignment = 0;
  uint64_t AnalysisBase = 0;
  bool Allocated = false;
  bool Executable = false;
};

struct AndroidKernelReleaseSymbolSemantics {
  AndroidKernelReleaseSymbolClass Class =
      AndroidKernelReleaseSymbolClass::Undefined;
  AndroidKernelReleaseSectionIdentity Section;
  uint64_t Value = 0;
  uint64_t Size = 0;
  uint8_t Type = 0;
  uint8_t Binding = 0;
  uint8_t Other = 0;

  bool operator<(const AndroidKernelReleaseSymbolSemantics &OtherValue) const {
    return std::tie(Class, Section, Value, Size, Type, Binding, Other) <
           std::tie(OtherValue.Class, OtherValue.Section, OtherValue.Value,
                    OtherValue.Size, OtherValue.Type, OtherValue.Binding,
                    OtherValue.Other);
  }

  bool operator==(const AndroidKernelReleaseSymbolSemantics &OtherValue) const {
    return std::tie(Class, Section, Value, Size, Type, Binding, Other) ==
           std::tie(OtherValue.Class, OtherValue.Section, OtherValue.Value,
                    OtherValue.Size, OtherValue.Type, OtherValue.Binding,
                    OtherValue.Other);
  }
};

struct AndroidKernelReleaseSymbol {
  unsigned Index = 0;
  uint16_t SectionIndex = llvm::ELF::SHN_UNDEF;
  std::string Name;
  AndroidKernelReleaseSymbolSemantics Semantics;
  bool IsSectionSymbol = false;
  bool PreserveName = false;
};

struct AndroidKernelReleaseMetadata {
  unsigned SymbolTableCount = 0;
  unsigned SymbolStringTableCount = 0;
  unsigned SymtabInfo = 0;
  unsigned RelocationSectionCount = 0;
  bool HasDebugSection = false;
  bool HasCommentSection = false;
  bool HasVersionsSection = false;
  bool HasAllocTagsSection = false;
  bool SymtabLinksSymbolStringTable = false;
  std::string SymbolStringTable;
  std::vector<AndroidKernelReleaseSection> Sections;
  std::vector<AndroidKernelReleaseSymbol> Symbols;
  std::multiset<std::string> RelocationTargets;
};

struct ELF64LERawSymbolLocation {
  uint16_t SectionIndex = 0;
  uint64_t Value = 0;
  uint64_t Size = 0;
};

struct ELF64LESymtabFacts {
  uint32_t Info = 0;
  uint32_t SymbolCount = 0;
};

static llvm::Expected<std::string>
retargetELF64LESectionName(llvm::StringRef Bytes, llvm::StringRef From,
                           llvm::StringRef Existing) {
  auto Parsed = llvm::object::ELFFile<llvm::object::ELF64LE>::create(Bytes);
  if (!Parsed)
    return Parsed.takeError();
  auto Sections = Parsed->sections();
  if (!Sections)
    return Sections.takeError();

  std::optional<unsigned> SourceIndex;
  std::optional<uint32_t> ExistingNameOffset;
  for (unsigned I = 0; I < Sections->size(); ++I) {
    const llvm::object::ELF64LE::Shdr &Section = (*Sections)[I];
    auto Name = Parsed->getSectionName(Section);
    if (!Name)
      return Name.takeError();
    if (*Name == From)
      SourceIndex = I;
    if (*Name == Existing)
      ExistingNameOffset = Section.sh_name;
  }
  if (!SourceIndex || !ExistingNameOffset)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "section-name mutation target is absent");

  const llvm::object::ELF64LE::Ehdr &Header = Parsed->getHeader();
  if (Header.e_shentsize != sizeof(llvm::object::ELF64LE::Shdr))
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "section-name mutation requires native ELF64 section headers");
  const uint64_t HeaderOffset =
      Header.e_shoff + static_cast<uint64_t>(*SourceIndex) * Header.e_shentsize;
  if (HeaderOffset > Bytes.size() ||
      sizeof(llvm::object::ELF64LE::Shdr) > Bytes.size() - HeaderOffset)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "section-name mutation is out of range");

  llvm::object::ELF64LE::Shdr Mutated = (*Sections)[*SourceIndex];
  Mutated.sh_name = *ExistingNameOffset;
  std::string Result = Bytes.str();
  std::memcpy(Result.data() + HeaderOffset, &Mutated, sizeof(Mutated));
  return Result;
}

static llvm::Expected<ELF64LERawSymbolLocation>
readELF64LERawSymbolLocation(llvm::StringRef Bytes, llvm::StringRef Name) {
  auto Parsed = llvm::object::ELFFile<llvm::object::ELF64LE>::create(Bytes);
  if (!Parsed)
    return Parsed.takeError();
  auto Sections = Parsed->sections();
  if (!Sections)
    return Sections.takeError();

  const llvm::object::ELF64LE::Shdr *Symtab = nullptr;
  for (const llvm::object::ELF64LE::Shdr &Section : *Sections) {
    if (Section.sh_type != llvm::ELF::SHT_SYMTAB)
      continue;
    if (Symtab)
      return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                     "raw symbol oracle found two symtabs");
    Symtab = &Section;
  }
  if (!Symtab)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "raw symbol oracle found no symtab");

  auto Symbols = Parsed->symbols(Symtab);
  if (!Symbols)
    return Symbols.takeError();
  auto StringTable = Parsed->getStringTableForSymtab(*Symtab);
  if (!StringTable)
    return StringTable.takeError();

  std::optional<ELF64LERawSymbolLocation> Match;
  for (const llvm::object::ELF64LE::Sym &Symbol : *Symbols) {
    auto Candidate = Symbol.getName(*StringTable);
    if (!Candidate)
      return Candidate.takeError();
    if (*Candidate != Name)
      continue;
    if (Match)
      return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                     "raw symbol oracle found duplicates");
    Match = ELF64LERawSymbolLocation{Symbol.st_shndx, Symbol.st_value,
                                     Symbol.st_size};
  }
  if (!Match)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "raw symbol oracle found no match");
  return *Match;
}

static llvm::Expected<ELF64LESymtabFacts>
readELF64LESymtabFacts(llvm::StringRef Bytes) {
  auto Parsed = llvm::object::ELFFile<llvm::object::ELF64LE>::create(Bytes);
  if (!Parsed)
    return Parsed.takeError();
  auto Sections = Parsed->sections();
  if (!Sections)
    return Sections.takeError();

  std::optional<ELF64LESymtabFacts> Facts;
  for (const llvm::object::ELF64LE::Shdr &Section : *Sections) {
    if (Section.sh_type != llvm::ELF::SHT_SYMTAB)
      continue;
    if (Facts)
      return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                     "symtab oracle found two symtabs");
    auto Symbols = Parsed->symbols(&Section);
    if (!Symbols)
      return Symbols.takeError();
    if (Symbols->size() > std::numeric_limits<uint32_t>::max())
      return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                     "symtab oracle count overflows");
    Facts = ELF64LESymtabFacts{Section.sh_info,
                               static_cast<uint32_t>(Symbols->size())};
  }
  if (!Facts)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "symtab oracle found no symtab");
  return *Facts;
}

static llvm::Expected<std::string>
rewriteELF64LESymtabInfo(llvm::StringRef Bytes, uint32_t NewInfo) {
  auto Parsed = llvm::object::ELFFile<llvm::object::ELF64LE>::create(Bytes);
  if (!Parsed)
    return Parsed.takeError();
  auto Sections = Parsed->sections();
  if (!Sections)
    return Sections.takeError();

  std::optional<unsigned> SymtabIndex;
  for (unsigned I = 0; I < Sections->size(); ++I) {
    if ((*Sections)[I].sh_type != llvm::ELF::SHT_SYMTAB)
      continue;
    if (SymtabIndex)
      return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                     "symtab mutation found two symtabs");
    SymtabIndex = I;
  }
  if (!SymtabIndex)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "symtab mutation found no symtab");

  const llvm::object::ELF64LE::Ehdr &Header = Parsed->getHeader();
  if (Header.e_shentsize != sizeof(llvm::object::ELF64LE::Shdr))
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "symtab mutation requires native ELF64 section headers");
  const uint64_t HeaderOffset =
      Header.e_shoff + static_cast<uint64_t>(*SymtabIndex) * Header.e_shentsize;
  if (HeaderOffset > Bytes.size() ||
      sizeof(llvm::object::ELF64LE::Shdr) > Bytes.size() - HeaderOffset)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "symtab mutation is out of range");

  llvm::object::ELF64LE::Shdr Mutated = (*Sections)[*SymtabIndex];
  Mutated.sh_info = NewInfo;
  std::string Result = Bytes.str();
  std::memcpy(Result.data() + HeaderOffset, &Mutated, sizeof(Mutated));
  return Result;
}

static neverc::ReleaseSymbolType androidKernelReleaseSymbolType(uint8_t Type) {
  switch (Type) {
  case llvm::ELF::STT_NOTYPE:
    return neverc::ReleaseSymbolType::NoType;
  case llvm::ELF::STT_OBJECT:
    return neverc::ReleaseSymbolType::Object;
  case llvm::ELF::STT_FUNC:
    return neverc::ReleaseSymbolType::Function;
  case llvm::ELF::STT_SECTION:
    return neverc::ReleaseSymbolType::Section;
  case llvm::ELF::STT_FILE:
    return neverc::ReleaseSymbolType::File;
  case llvm::ELF::STT_TLS:
    return neverc::ReleaseSymbolType::TLS;
  case llvm::ELF::STT_GNU_IFUNC:
    return neverc::ReleaseSymbolType::GNUIFunc;
  default:
    return neverc::ReleaseSymbolType::FormatExtension;
  }
}

static uint32_t androidKernelReleaseBindingRank(uint8_t Binding) {
  switch (Binding) {
  case llvm::ELF::STB_GLOBAL:
    return 0;
  case llvm::ELF::STB_WEAK:
    return 1;
  case llvm::ELF::STB_LOCAL:
    return 2;
  default:
    return 3 + Binding;
  }
}

static AndroidKernelReleaseSymbolClass
androidKernelReleaseSymbolClass(uint16_t SectionIndex) {
  using SymbolClass = AndroidKernelReleaseSymbolClass;
  if (SectionIndex == llvm::ELF::SHN_UNDEF)
    return SymbolClass::Undefined;
  if (SectionIndex == llvm::ELF::SHN_COMMON)
    return SymbolClass::Common;
  if (SectionIndex == llvm::ELF::SHN_ABS)
    return SymbolClass::Absolute;
  if (SectionIndex ==
          neverc::AndroidKernelModuleSymbolPolicy::LivePatchSectionIndex ||
      SectionIndex >= llvm::ELF::SHN_LORESERVE)
    return SymbolClass::LivePatch;
  return SymbolClass::Defined;
}

static llvm::Expected<AndroidKernelReleaseMetadata>
inspectAndroidKernelReleaseMetadata(llvm::StringRef Bytes,
                                    bool AuditCanonicalNames = false) {
  auto Parsed = llvm::object::ELFFile<llvm::object::ELF64LE>::create(Bytes);
  if (!Parsed)
    return Parsed.takeError();
  if (Parsed->getHeader().e_type != llvm::ELF::ET_REL ||
      Parsed->getHeader().e_machine != llvm::ELF::EM_AARCH64)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "expected an AArch64 ELF64LE ET_REL module");

  auto Sections = Parsed->sections();
  if (!Sections)
    return Sections.takeError();

  AndroidKernelReleaseMetadata Metadata;
  const llvm::object::ELF64LE::Shdr *Symtab = nullptr;
  unsigned SymtabIndex = 0;
  llvm::SmallVector<neverc::ReleaseSectionDescriptor, 32> ReleaseSections;
  std::map<AndroidKernelReleaseSectionIdentity, unsigned>
      SectionIdentityOccurrences;
  ReleaseSections.reserve(Sections->size());
  Metadata.Sections.reserve(Sections->size());
  for (unsigned I = 0; I < Sections->size(); ++I) {
    const llvm::object::ELF64LE::Shdr &Section = (*Sections)[I];
    llvm::Expected<llvm::StringRef> Name = Parsed->getSectionName(Section);
    if (!Name)
      return Name.takeError();
    Metadata.SymbolTableCount +=
        *Name == ".symtab" && Section.sh_type == llvm::ELF::SHT_SYMTAB;
    Metadata.SymbolStringTableCount +=
        *Name == ".strtab" && Section.sh_type == llvm::ELF::SHT_STRTAB;
    Metadata.RelocationSectionCount += Section.sh_type == llvm::ELF::SHT_RELA ||
                                       Section.sh_type == llvm::ELF::SHT_REL;
    Metadata.HasDebugSection |=
        Name->starts_with(".debug") || Name->starts_with(".zdebug");
    Metadata.HasCommentSection |= *Name == ".comment";
    Metadata.HasVersionsSection |= *Name == "__versions";
    Metadata.HasAllocTagsSection |= *Name == ".codetag.alloc_tags";

    if (Section.sh_type == llvm::ELF::SHT_SYMTAB) {
      if (Symtab)
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                       "module has multiple symbol tables");
      Symtab = &Section;
      SymtabIndex = I;
    }
    if (I == 0)
      continue;

    AndroidKernelReleaseSectionIdentity Identity;
    Identity.Name = Name->str();
    Identity.Type = Section.sh_type;
    Identity.Flags = Section.sh_flags;
    Identity.Address = Section.sh_addr;
    Identity.Size = Section.sh_size;
    Identity.Alignment = Section.sh_addralign;
    Identity.EntrySize = Section.sh_entsize;
    if (Section.sh_link != 0) {
      if (Section.sh_link >= Sections->size())
        return llvm::createStringError(
            llvm::inconvertibleErrorCode(),
            "section identity has an out-of-range sh_link");
      auto LinkedName = Parsed->getSectionName((*Sections)[Section.sh_link]);
      if (!LinkedName)
        return LinkedName.takeError();
      Identity.LinkedSection = LinkedName->str();
    }
    if ((Section.sh_type == llvm::ELF::SHT_REL ||
         Section.sh_type == llvm::ELF::SHT_RELA) &&
        Section.sh_info != 0) {
      if (Section.sh_info >= Sections->size())
        return llvm::createStringError(
            llvm::inconvertibleErrorCode(),
            "relocation section identity has an out-of-range sh_info");
      auto InfoName = Parsed->getSectionName((*Sections)[Section.sh_info]);
      if (!InfoName)
        return InfoName.takeError();
      Identity.InfoSection = InfoName->str();
    } else {
      Identity.OtherInfo = Section.sh_info;
    }
    auto Contents = Parsed->getSectionContents(Section);
    if (!Contents)
      return Contents.takeError();
    Identity.Contents.assign(reinterpret_cast<const char *>(Contents->data()),
                             Contents->size());
    Identity.Occurrence = SectionIdentityOccurrences[Identity]++;

    AndroidKernelReleaseSection ReleaseSection;
    ReleaseSection.Index = I;
    ReleaseSection.Name = Name->str();
    ReleaseSection.Identity = std::move(Identity);
    ReleaseSection.Size = Section.sh_size;
    ReleaseSection.Alignment = Section.sh_addralign;
    ReleaseSection.Allocated = (Section.sh_flags & llvm::ELF::SHF_ALLOC) != 0;
    ReleaseSection.Executable =
        (Section.sh_flags & llvm::ELF::SHF_EXECINSTR) != 0;
    Metadata.Sections.push_back(std::move(ReleaseSection));
    ReleaseSections.push_back(
        {I, I, Section.sh_addralign, Section.sh_size,
         (Section.sh_flags & llvm::ELF::SHF_ALLOC) != 0,
         (Section.sh_flags & llvm::ELF::SHF_EXECINSTR) != 0});
  }
  if (!Symtab)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "module has no symbol table");
  if (Symtab->sh_link >= Sections->size())
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "symbol table has an out-of-range sh_link");
  const llvm::object::ELF64LE::Shdr &LinkedStringTable =
      (*Sections)[Symtab->sh_link];
  auto LinkedStringTableName = Parsed->getSectionName(LinkedStringTable);
  if (!LinkedStringTableName)
    return LinkedStringTableName.takeError();
  Metadata.SymtabLinksSymbolStringTable =
      LinkedStringTable.sh_type == llvm::ELF::SHT_STRTAB &&
      *LinkedStringTableName == ".strtab";
  auto SymbolStringTableContents =
      Parsed->getSectionContents(LinkedStringTable);
  if (!SymbolStringTableContents)
    return SymbolStringTableContents.takeError();
  Metadata.SymbolStringTable.assign(
      reinterpret_cast<const char *>(SymbolStringTableContents->data()),
      SymbolStringTableContents->size());

  auto Layout =
      neverc::computeAndroidKernelReleaseSectionLayout(ReleaseSections);
  if (!Layout)
    return Layout.takeError();
  for (const neverc::ReleaseSectionLayout &Entry : *Layout) {
    auto It = llvm::find_if(Metadata.Sections,
                            [&](const AndroidKernelReleaseSection &Section) {
                              return Section.Index == Entry.SectionID;
                            });
    if (It == Metadata.Sections.end())
      return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                     "release layout lost a section");
    It->AnalysisBase = Entry.Base;
  }

  auto Symbols = Parsed->symbols(Symtab);
  if (!Symbols)
    return Symbols.takeError();
  if (Symtab->sh_info > Symbols->size())
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "symbol table sh_info exceeds the symbol count");
  Metadata.SymtabInfo = Symtab->sh_info;
  auto StringTable = Parsed->getStringTableForSymtab(*Symtab);
  if (!StringTable)
    return StringTable.takeError();

  llvm::SmallVector<neverc::ReleaseSymbolDescriptor, 64> ReleaseSymbols;
  llvm::SmallVector<neverc::ReleaseSymbolRename, 64> ActualNames;
  ReleaseSymbols.reserve(Symbols->size());
  ActualNames.reserve(Symbols->size());
  Metadata.Symbols.reserve(Symbols->size());
  std::vector<std::string> SymbolNames(Symbols->size());
  for (unsigned I = 0; I < Symbols->size(); ++I) {
    const llvm::object::ELF64LE::Sym &Symbol = (*Symbols)[I];
    if (I < Metadata.SymtabInfo && Symbol.getBinding() != llvm::ELF::STB_LOCAL)
      return llvm::createStringError(
          llvm::inconvertibleErrorCode(),
          "non-local symbol precedes the symtab sh_info boundary");
    if (I >= Metadata.SymtabInfo && Symbol.getBinding() == llvm::ELF::STB_LOCAL)
      return llvm::createStringError(
          llvm::inconvertibleErrorCode(),
          "local symbol follows the symtab sh_info boundary");
    auto Name = Symbol.getName(*StringTable);
    if (!Name)
      return Name.takeError();
    SymbolNames[I] = Name->str();

    const AndroidKernelReleaseSymbolClass Class =
        androidKernelReleaseSymbolClass(Symbol.st_shndx);
    AndroidKernelReleaseSectionIdentity SectionIdentity;
    bool PreserveName = false;
    if (Class == AndroidKernelReleaseSymbolClass::Defined) {
      if (Symbol.st_shndx == 0 || Symbol.st_shndx >= Sections->size())
        return llvm::createStringError(
            llvm::inconvertibleErrorCode(),
            "defined symbol has an out-of-range section index");
      auto NameOrError = Parsed->getSectionName((*Sections)[Symbol.st_shndx]);
      if (!NameOrError)
        return NameOrError.takeError();
      auto SectionRecord = llvm::find_if(
          Metadata.Sections, [&](const AndroidKernelReleaseSection &Candidate) {
            return Candidate.Index == Symbol.st_shndx;
          });
      if (SectionRecord == Metadata.Sections.end())
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                       "symbol section identity is absent");
      SectionIdentity = SectionRecord->Identity;
      PreserveName = neverc::AndroidKernelModuleSymbolPolicy::
          preservesSymbolNamesInSection(*NameOrError);
    }

    AndroidKernelReleaseSymbol Record;
    Record.Index = I;
    Record.SectionIndex = Symbol.st_shndx;
    Record.Name = Name->str();
    Record.Semantics = {
        Class,          std::move(SectionIdentity), Symbol.st_value,
        Symbol.st_size, Symbol.getType(),           Symbol.getBinding(),
        Symbol.st_other};
    Record.IsSectionSymbol = Symbol.getType() == llvm::ELF::STT_SECTION;
    Record.PreserveName = PreserveName;
    Metadata.Symbols.push_back(std::move(Record));

    ReleaseSymbols.push_back(
        {I, *Name, Class, androidKernelReleaseSymbolType(Symbol.getType()),
         Class == AndroidKernelReleaseSymbolClass::Defined
             ? static_cast<uint64_t>(Symbol.st_shndx)
             : 0,
         Symbol.st_value, Symbol.st_size,
         androidKernelReleaseBindingRank(Symbol.getBinding()),
         static_cast<uint32_t>(Symbol.st_other), PreserveName});
    ActualNames.push_back({I, Name->str()});
  }

  for (unsigned I = 0; I < Sections->size(); ++I) {
    const llvm::object::ELF64LE::Shdr &Section = (*Sections)[I];
    if (Section.sh_type != llvm::ELF::SHT_RELA &&
        Section.sh_type != llvm::ELF::SHT_REL)
      continue;
    if (Section.sh_link != SymtabIndex || Section.sh_info == 0 ||
        Section.sh_info >= Sections->size())
      return llvm::createStringError(
          llvm::inconvertibleErrorCode(),
          "relocation section has invalid symbol/target section links");

    auto RecordTarget = [&](uint32_t SymbolIndex) -> llvm::Error {
      if (SymbolIndex >= SymbolNames.size())
        return llvm::createStringError(
            llvm::inconvertibleErrorCode(),
            "relocation references an out-of-range symbol index");
      Metadata.RelocationTargets.insert(SymbolNames[SymbolIndex]);
      return llvm::Error::success();
    };
    if (Section.sh_type == llvm::ELF::SHT_RELA) {
      auto Relocations = Parsed->relas(Section);
      if (!Relocations)
        return Relocations.takeError();
      for (const llvm::object::ELF64LE::Rela &Relocation : *Relocations)
        if (llvm::Error Error = RecordTarget(Relocation.getSymbol()))
          return std::move(Error);
    } else {
      auto Relocations = Parsed->rels(Section);
      if (!Relocations)
        return Relocations.takeError();
      for (const llvm::object::ELF64LE::Rel &Relocation : *Relocations)
        if (llvm::Error Error = RecordTarget(Relocation.getSymbol()))
          return std::move(Error);
    }
  }

  if (AuditCanonicalNames) {
    if (llvm::Error Audit = neverc::auditAndroidKernelReleaseNames(
            ReleaseSections, ReleaseSymbols, ActualNames))
      return std::move(Audit);
  }
  return Metadata;
}

static bool
symbolStringTableContains(const AndroidKernelReleaseMetadata &Metadata,
                          llvm::StringRef Name) {
  llvm::StringRef Remaining(Metadata.SymbolStringTable);
  while (!Remaining.empty()) {
    const auto [Entry, Tail] = Remaining.split('\0');
    if (Entry == Name)
      return true;
    if (Tail.data() == Remaining.data())
      break;
    Remaining = Tail;
  }
  return false;
}

static llvm::Expected<std::string>
canonicalReleaseBaseName(const AndroidKernelReleaseMetadata &Metadata,
                         const AndroidKernelReleaseSymbol &Symbol) {
  if (Symbol.Semantics.Class == AndroidKernelReleaseSymbolClass::Absolute)
    return neverc::formatReleaseName(neverc::ReleaseNameKind::Absolute,
                                     Symbol.Semantics.Value,
                                     Symbol.Semantics.Size);
  if (Symbol.Semantics.Class != AndroidKernelReleaseSymbolClass::Defined)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "symbol has no generated-name coordinate");

  auto Section = llvm::find_if(
      Metadata.Sections, [&](const AndroidKernelReleaseSection &Candidate) {
        return Candidate.Index == Symbol.SectionIndex;
      });
  if (Section == Metadata.Sections.end())
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "symbol section is absent from layout");
  if (!Section->Allocated)
    return ("sym_S" + llvm::utohexstr(Section->Index) + "_" +
            llvm::utohexstr(Symbol.Semantics.Value));
  if (Symbol.Semantics.Value >
      std::numeric_limits<uint64_t>::max() - Section->AnalysisBase)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "analysis EA overflows");

  neverc::ReleaseNameKind Kind;
  switch (Symbol.Semantics.Type) {
  case llvm::ELF::STT_FUNC:
    Kind = neverc::ReleaseNameKind::Function;
    break;
  case llvm::ELF::STT_OBJECT:
    Kind = neverc::ReleaseNameKind::Object;
    break;
  case llvm::ELF::STT_NOTYPE:
    Kind = Section->Executable ? neverc::ReleaseNameKind::ExecutableLabel
                               : neverc::ReleaseNameKind::Other;
    break;
  default:
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "symbol type has no generated-name kind");
  }
  return neverc::formatReleaseName(
      Kind, Section->AnalysisBase + Symbol.Semantics.Value,
      Symbol.Semantics.Size);
}

static llvm::Expected<uint64_t>
readELFSymbolSectionOffset(llvm::StringRef Bytes, llvm::StringRef SymbolName,
                           bool MatchPCGSuffix = false) {
  auto Object = llvm::object::ObjectFile::createObjectFile(
      llvm::MemoryBufferRef(Bytes, "android-kernel-kcfi-test"));
  if (!Object)
    return Object.takeError();
  if (!(*Object)->isELF())
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "expected an ELF object");

  for (const llvm::object::SymbolRef &Symbol : (*Object)->symbols()) {
    llvm::Expected<llvm::StringRef> Name = Symbol.getName();
    if (!Name)
      return Name.takeError();
    const bool IsPCGName =
        MatchPCGSuffix && Name->starts_with(SymbolName) &&
        Name->drop_front(SymbolName.size()).starts_with(".__pcg");
    if (*Name != SymbolName && !IsPCGName)
      continue;

    llvm::Expected<uint64_t> Address = Symbol.getAddress();
    if (!Address)
      return Address.takeError();
    llvm::Expected<llvm::object::section_iterator> Section =
        Symbol.getSection();
    if (!Section)
      return Section.takeError();
    if (*Section == (*Object)->section_end())
      return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                     "entry symbol has no section");

    const uint64_t SectionAddress = (*Section)->getAddress();
    if (*Address < SectionAddress)
      return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                     "entry symbol precedes its section");
    return *Address - SectionAddress;
  }

  return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                 "entry symbol not found");
}

class LTOTest : public NeverCTest {
protected:
  static size_t countLtoCacheEntries(const fs::path &Dir) {
    size_t Count = 0;
    std::error_code EC;
    for (fs::directory_iterator It(Dir, EC), End; !EC && It != End;
         It.increment(EC))
      if (It->path().filename().string().rfind(linker::ltoCacheEntryPrefix,
                                               0) == 0 &&
          It->path().extension() != linker::ltoCacheTmpSuffix)
        ++Count;
    return Count;
  }

  std::vector<std::string> writeAutoLtoLoopDenseProject(const std::string &Stem,
                                                        bool RuntimeSeed) {
    constexpr int NFiles = 12;
    constexpr int NFuncsPerFile = 12;
    auto SrcDir = tmpFile(Stem);
    fs::create_directories(SrcDir);

    std::vector<std::string> Names;
    std::vector<std::string> Sources;
    for (int FI = 0; FI < NFiles; ++FI) {
      std::string Src = "#include <stdint.h>\n";
      for (int FJ = 0; FJ < NFuncsPerFile; ++FJ) {
        std::string Name =
            "fn_" + std::to_string(FI) + "_" + std::to_string(FJ);
        Names.push_back(Name);
        unsigned C1 = (2654435761u * unsigned(FI * 131 + FJ + 1)) | 1u;
        unsigned C2 =
            (40503u * unsigned(FI + 7) + 2246822519u * unsigned(FJ + 3)) | 1u;
        unsigned C3 = (2166136261u ^ (16777619u * unsigned(FI * 17 + FJ))) | 1u;
        Src += "uint64_t " + Name + "(uint64_t x){ uint64_t a=x^" +
               std::to_string(C1) + "ULL; for(int i=0;i<7;i++){ a=a*" +
               std::to_string(C2) +
               "ULL+(a>>13)+i; if(a&1) a^=" + std::to_string(C3) +
               "ULL; } return a; }\n";
      }
      auto Path = SrcDir / ("m" + std::to_string(FI) + ".c");
      writeFile(Path, Src);
      Sources.push_back(Path.string());
    }

    std::string Main = "#include <stdint.h>\n#include <stdio.h>\n";
    for (const auto &Name : Names)
      Main += "uint64_t " + Name + "(uint64_t);\n";
    if (RuntimeSeed)
      Main += "int main(int argc, char **argv){ (void)argv; "
              "uint64_t acc=(uint64_t)argc;\n";
    else
      Main += "int main(void){ uint64_t acc=1;\n";
    Main += "for(int r=0;r<3;r++){\n";
    for (const auto &Name : Names)
      Main += "acc=acc*1000003ULL+" + Name + "(acc);\n";
    Main += "}\nprintf(\"CK=%llu\\n\",(unsigned long long)acc);"
            " return 0; }\n";

    auto MainPath = SrcDir / "main.c";
    writeFile(MainPath, Main);
    Sources.push_back(MainPath.string());
    return Sources;
  }
};

TEST_F(LTOTest, HelloLTO) {
  auto src = (testDir() / "lto/hello_lto.c").string();
  auto obj = tmpFile("hello_lto.o");
  auto exe = tmpFile("hello_lto");

  std::vector<std::string> base = {"-std=c11"};
  for (auto &f : sysrootFlags())
    base.push_back(f);
  for (auto &f : archFlags())
    base.push_back(f);

  auto c = base;
  c.insert(c.end(), {"-flto", "-c", src, "-o", obj.string()});
  ASSERT_EQ(ncc(c).exitCode, 0);

  auto l = base;
  l.erase(l.begin()); // remove -std=c11 for link
  l.insert(l.end(), {"-flto", obj.string(), "-o", exe.string()});
  ASSERT_EQ(ncc(l).exitCode, 0);

  auto r = exec(exe.string(), {});
  EXPECT_EQ(r.exitCode, 3) << "hello_lto should exit 3";
}

TEST_F(LTOTest, DarwinSaveTempsLTOProducesUsableDsym) {
  if (!isDarwin())
    GTEST_SKIP() << "dsymutil is a Darwin-host packaging tool";

  const auto Source = tmpFile("darwin_lto_save_temps_debug.c");
  const auto Image = tmpFile("darwin-lto-save-temps.macho");
  writeFile(Source,
            "__attribute__((noinline)) int lto_saved_debug_marker(void) { "
            "return 42; }\n"
            "int main(void) { return lto_saved_debug_marker(); }\n");

  const CmdResult Build =
      ncc({"--target=arm64-apple-macos", "-flto=full", "-g",
           "-save-temps=obj", "-nostdlib", "-fno-stack-protector",
           Source.string(), "-o", Image.string()});
  ASSERT_EQ(Build.exitCode, 0) << Build.err;

  const fs::path NativeObject = Image.string() + ".lto.o";
  const fs::path DwarfImage = fs::path(Image.string() + ".dSYM") /
                              "Contents/Resources/DWARF" / Image.filename();
  ASSERT_TRUE(fs::is_regular_file(NativeObject));
  ASSERT_TRUE(fs::is_regular_file(DwarfImage)) << Build.err;

  const std::string DwarfBytes = readFile(DwarfImage);
  auto DwarfObject = llvm::object::ObjectFile::createObjectFile(
      llvm::MemoryBufferRef(DwarfBytes, DwarfImage.string()));
  ASSERT_TRUE(static_cast<bool>(DwarfObject))
      << llvm::toString(DwarfObject.takeError()).str().str();

  bool HasDebugInfo = false;
  for (const llvm::object::SectionRef &Section : (*DwarfObject)->sections()) {
    llvm::Expected<llvm::StringRef> Name = Section.getName();
    ASSERT_TRUE(static_cast<bool>(Name))
        << llvm::toString(Name.takeError()).str().str();
    HasDebugInfo |= Name->starts_with(".debug_info") ||
                    Name->starts_with(".zdebug_info") ||
                    Name->starts_with("__debug_info") ||
                    Name->starts_with("__zdebug_info");
  }
  EXPECT_TRUE(HasDebugInfo) << Build.err;
}

TEST_F(LTOTest, RejectsUnloweredTypeMetadataBeforeNativeCodegen) {
  struct Target {
    const char *Name;
    const char *Triple;
    const char *DriverTarget;
    const char *DataLayout;
  };
  const Target Targets[] = {
      {"aarch64", "aarch64-unknown-linux-gnu", "aarch64-linux-gnu",
       "e-m:e-i64:64-i128:128-n32:64-S128"},
      {"x86_64", "x86_64-unknown-linux-gnu", "x86_64-linux-gnu",
       "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-"
       "n8:16:32:64-S128"},
  };
  enum class PipelineKind {
    SerialFallback,
    AutoLTO,
    DirectCodeGen,
  };
  struct Pipeline {
    const char *Name;
    PipelineKind Kind;
  };
  const Pipeline Pipelines[] = {
      {"serial_fallback", PipelineKind::SerialFallback},
      {"auto_lto", PipelineKind::AutoLTO},
      {"direct_codegen", PipelineKind::DirectCodeGen},
  };

  for (const Target &Target : Targets) {
    auto Input =
        tmpFile(std::string("unlowered_type_test_") + Target.Name + ".bc");
    llvm::LLVMContext Context;
    llvm::Module Module("unlowered_type_test", Context);
    Module.setTargetTriple(Target.Triple);
    Module.setDataLayout(Target.DataLayout);

    llvm::FunctionType *VoidType =
        llvm::FunctionType::get(llvm::Type::getVoidTy(Context), false);
    llvm::Function *Good = llvm::Function::Create(
        VoidType, llvm::GlobalValue::ExternalLinkage, "good", Module);
    llvm::IRBuilder<> GoodBuilder(
        llvm::BasicBlock::Create(Context, "entry", Good));
    GoodBuilder.CreateRetVoid();

    llvm::Metadata *TypeMetadata[] = {
        llvm::ConstantAsMetadata::get(
            llvm::ConstantInt::get(llvm::Type::getInt32Ty(Context), 0)),
        llvm::MDString::get(Context, "kernel.cfi.icall")};
    Good->setMetadata(llvm::LLVMContext::MD_type,
                      llvm::MDNode::get(Context, TypeMetadata));
    llvm::appendToUsed(Module, {Good});

    llvm::Type *PointerType = llvm::PointerType::getUnqual(Context);
    llvm::Function *Accepts = llvm::Function::Create(
        llvm::FunctionType::get(llvm::Type::getInt1Ty(Context), {PointerType},
                                false),
        llvm::GlobalValue::ExternalLinkage, "accepts", Module);
    llvm::IRBuilder<> AcceptsBuilder(
        llvm::BasicBlock::Create(Context, "entry", Accepts));
    llvm::Function *TypeTest =
        llvm::Intrinsic::getDeclaration(&Module, llvm::Intrinsic::type_test);
    llvm::Value *TypeID = llvm::MetadataAsValue::get(
        Context, llvm::MDString::get(Context, "kernel.cfi.icall"));
    llvm::Value *Result =
        AcceptsBuilder.CreateCall(TypeTest, {Accepts->getArg(0), TypeID});
    AcceptsBuilder.CreateRet(Result);

    llvm::SmallVector<char, 0> Bitcode;
    llvm::raw_svector_ostream BitcodeStream(Bitcode);
    llvm::WriteBitcodeToFile(Module, BitcodeStream);
    writeFile(Input, std::string(Bitcode.begin(), Bitcode.end()));

    for (int OptLevel : {0, 2}) {
      for (const Pipeline &Pipeline : Pipelines) {
        SCOPED_TRACE(std::string(Target.Name) + "/O" +
                     std::to_string(OptLevel) + "/" + Pipeline.Name);
        auto Output =
            tmpFile(std::string("unlowered_type_test_") + Target.Name + "_O" +
                    std::to_string(OptLevel) + "_" + Pipeline.Name + ".o");
        std::vector<std::string> Args = {std::string("--target=") +
                                             Target.DriverTarget,
                                         "-O" + std::to_string(OptLevel)};
        if (Pipeline.Kind == PipelineKind::DirectCodeGen) {
          Args.insert(Args.end(), {"-fno-lto", "-c"});
        } else if (Pipeline.Kind == PipelineKind::AutoLTO) {
          Args.insert(Args.end(), {"-nostdlib", "-r"});
          Args.insert(Args.end(), {"-mllvm", "-neverc-pcg-min-funcs=1",
                                   "-mllvm", "-neverc-pcg-weight-floor=0",
                                   "-mllvm", "-neverc-pcg-opt-weight-div=1"});
        } else {
          Args.insert(Args.end(), {"-nostdlib", "-r"});
          Args.insert(Args.end(), {"-flto=full", "-mllvm",
                                   "-neverc-pcg-min-funcs=1000000"});
        }
        Args.insert(Args.end(), {Input.string(), "-o", Output.string()});
        CmdResult Build = ncc(Args);
        EXPECT_EQ(Build.exitCode, 1)
            << "unsupported CFI lowering did not fail cleanly";
        EXPECT_TRUE(Build.stderrContains("llvm.type.test")) << Build.err;
        EXPECT_TRUE(Build.stderrContains(
            "CFI requires whole-program type metadata lowering"))
            << Build.err;
        EXPECT_TRUE(Build.stderrContains("refusing unsafe code generation"))
            << Build.err;
      }
    }
  }
}

TEST_F(LTOTest, AllowsNativeICallBranchFunnelCodegen) {
  auto Bitcode = tmpFile("icall_branch_funnel.bc");
  llvm::LLVMContext Context;
  llvm::Module Module("icall_branch_funnel", Context);
  Module.setTargetTriple("x86_64-unknown-linux-gnu");
  Module.setDataLayout("e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-"
                       "n8:16:32:64-S128");

  llvm::Type *VoidType = llvm::Type::getVoidTy(Context);
  llvm::Type *PointerType = llvm::PointerType::getUnqual(Context);
  llvm::FunctionType *TargetType = llvm::FunctionType::get(VoidType, false);
  llvm::Function *F0 = llvm::Function::Create(
      TargetType, llvm::GlobalValue::ExternalLinkage, "f0", Module);
  llvm::Function *F1 = llvm::Function::Create(
      TargetType, llvm::GlobalValue::ExternalLinkage, "f1", Module);

  llvm::ArrayType *TargetsType =
      llvm::ArrayType::get(llvm::Type::getInt8Ty(Context), 2);
  auto *Targets = new llvm::GlobalVariable(Module, TargetsType, false,
                                           llvm::GlobalValue::ExternalLinkage,
                                           nullptr, "targets");
  llvm::Constant *Zero =
      llvm::ConstantInt::get(llvm::Type::getInt64Ty(Context), 0);
  llvm::Constant *One =
      llvm::ConstantInt::get(llvm::Type::getInt64Ty(Context), 1);
  llvm::Constant *Target0Indices[] = {Zero, Zero};
  llvm::Constant *Target1Indices[] = {Zero, One};
  llvm::Constant *Target0 = llvm::ConstantExpr::getInBoundsGetElementPtr(
      TargetsType, Targets, Target0Indices);
  llvm::Constant *Target1 = llvm::ConstantExpr::getInBoundsGetElementPtr(
      TargetsType, Targets, Target1Indices);

  llvm::Function *Funnel = llvm::Function::Create(
      llvm::FunctionType::get(VoidType, {PointerType}, true),
      llvm::GlobalValue::ExternalLinkage, "funnel", Module);
  Funnel->addParamAttr(0, llvm::Attribute::Nest);
  llvm::IRBuilder<> Builder(llvm::BasicBlock::Create(Context, "entry", Funnel));
  llvm::Function *BranchFunnel = llvm::Intrinsic::getDeclaration(
      &Module, llvm::Intrinsic::icall_branch_funnel);
  llvm::CallInst *Call = Builder.CreateCall(
      BranchFunnel, {Funnel->getArg(0), Target0, F0, Target1, F1});
  Call->setTailCallKind(llvm::CallInst::TCK_MustTail);
  Builder.CreateRetVoid();

  llvm::SmallVector<char, 0> Bytes;
  llvm::raw_svector_ostream Stream(Bytes);
  llvm::WriteBitcodeToFile(Module, Stream);
  writeFile(Bitcode, std::string(Bytes.begin(), Bytes.end()));

  struct Pipeline {
    const char *Name;
    std::vector<std::string> Args;
  };
  const Pipeline Pipelines[] = {
      {"direct_codegen", {"-fno-lto", "-c"}},
      {"serial_fallback",
       {"-nostdlib", "-r", "-flto=full", "-mllvm",
        "-neverc-pcg-min-funcs=1000000"}},
      {"auto_lto",
       {"-nostdlib", "-r", "-mllvm", "-neverc-pcg-min-funcs=1", "-mllvm",
        "-neverc-pcg-weight-floor=0", "-mllvm",
        "-neverc-pcg-opt-weight-div=1"}},
  };

  for (int OptLevel : {0, 2}) {
    for (const Pipeline &Pipeline : Pipelines) {
      SCOPED_TRACE("O" + std::to_string(OptLevel) + "/" + Pipeline.Name);
      auto Output = tmpFile("icall_branch_funnel_O" + std::to_string(OptLevel) +
                            "_" + Pipeline.Name + ".o");
      std::vector<std::string> Args = {"--target=x86_64-linux-gnu",
                                       "-O" + std::to_string(OptLevel)};
      Args.insert(Args.end(), Pipeline.Args.begin(), Pipeline.Args.end());
      Args.insert(Args.end(), {Bitcode.string(), "-o", Output.string()});
      CmdResult Build = ncc(Args);
      ASSERT_TRUE(Build.ok()) << Build.err;
      EXPECT_GT(fileSize(Output), 0u);
    }
  }
}

TEST(TypeMetadataUtilsTest, FindsEveryUnloweredTypeMetadataIntrinsic) {
  static constexpr llvm::Intrinsic::ID MustBeLowered[] = {
      llvm::Intrinsic::type_test,
      llvm::Intrinsic::public_type_test,
      llvm::Intrinsic::type_checked_load,
      llvm::Intrinsic::type_checked_load_relative,
  };

  for (llvm::Intrinsic::ID ID : MustBeLowered) {
    SCOPED_TRACE(llvm::Intrinsic::getBaseName(ID).str());
    llvm::LLVMContext Context;
    llvm::Module Module("unlowered_type_metadata_intrinsic", Context);
    llvm::Function *Intrinsic = llvm::Function::Create(
        llvm::FunctionType::get(llvm::Type::getVoidTy(Context), false),
        llvm::GlobalValue::ExternalLinkage, llvm::Intrinsic::getBaseName(ID),
        Module);
    llvm::appendToUsed(Module, {Intrinsic});

    EXPECT_EQ(Intrinsic->getIntrinsicID(), ID);
    EXPECT_EQ(llvm::findUnloweredTypeMetadataIntrinsic(Module), Intrinsic);
  }
}

TEST_F(LTOTest, AArch64UnalignedCrossCcTailCallFallsBack) {
  auto src = tmpFile("aarch64_unaligned_cross_cc_tail.bc");
  auto obj = tmpFile("aarch64_unaligned_cross_cc_tail.o");
  llvm::LLVMContext context;
  llvm::Module module("aarch64_unaligned_cross_cc_tail", context);
  module.setTargetTriple("aarch64-unknown-linux-gnu");
  llvm::Type *ptrTy = llvm::PointerType::getUnqual(context);
  llvm::Function *unlock = llvm::Function::Create(
      llvm::FunctionType::get(llvm::Type::getInt32Ty(context), {ptrTy}, false),
      llvm::Function::ExternalLinkage, "unlock", module);

  std::vector<llvm::Type *> storeArgs = {ptrTy};
  storeArgs.insert(storeArgs.end(), 8, llvm::Type::getInt64Ty(context));
  llvm::Function *store = llvm::Function::Create(
      llvm::FunctionType::get(llvm::Type::getVoidTy(context), storeArgs, false),
      llvm::Function::ExternalLinkage, "store", module);
  store->setCallingConv(llvm::CallingConv::Fast);
  llvm::IRBuilder<> builder(llvm::BasicBlock::Create(context, "entry", store));
  llvm::CallInst *call = builder.CreateCall(unlock, {store->getArg(0)});
  call->setTailCallKind(llvm::CallInst::TCK_Tail);
  builder.CreateRetVoid();

  llvm::SmallVector<char, 0> bitcode;
  llvm::raw_svector_ostream bitcodeStream(bitcode);
  llvm::WriteBitcodeToFile(module, bitcodeStream);
  writeFile(src, std::string(bitcode.begin(), bitcode.end()));

  auto result = ncc({"--target=aarch64-unknown-linux-gnu", "-fno-lto", "-c",
                     src.string(), "-o", obj.string()});
  EXPECT_TRUE(result.ok()) << "AArch64 codegen rejected a valid tail-call "
                              "candidate instead of lowering it as a normal "
                              "call:\n"
                           << result.err;
}

// AArch64's "this return" shortcut is only valid when the first argument is
// marked returned.  A returned pointer in any later slot is ordinary ABI
// information: the call result must still be copied from x0.  SQLite's
// sqlite3_snprintf(int, char *returned, ...) exercises exactly this shape.
TEST_F(LTOTest, AArch64NonFirstReturnedArgumentUsesCallResult) {
  auto src = tmpFile("aarch64_nonfirst_returned_argument.bc");
  auto obj = tmpFile("aarch64_nonfirst_returned_argument.o");
  llvm::LLVMContext context;
  llvm::Module module("aarch64_nonfirst_returned_argument", context);
  module.setTargetTriple("aarch64-unknown-linux-gnu");

  llvm::Type *i32Ty = llvm::Type::getInt32Ty(context);
  llvm::Type *ptrTy = llvm::PointerType::getUnqual(context);
  llvm::Function *callee = llvm::Function::Create(
      llvm::FunctionType::get(ptrTy, {i32Ty, ptrTy, ptrTy}, true),
      llvm::Function::ExternalLinkage, "variadic_returned", module);
  callee->addParamAttr(1, llvm::Attribute::Returned);

  llvm::Function *caller = llvm::Function::Create(
      llvm::FunctionType::get(ptrTy, {i32Ty, ptrTy, ptrTy}, false),
      llvm::Function::ExternalLinkage, "caller", module);
  llvm::IRBuilder<> builder(llvm::BasicBlock::Create(context, "entry", caller));
  llvm::CallInst *call =
      builder.CreateCall(callee, {caller->getArg(0), caller->getArg(1),
                                  caller->getArg(1), caller->getArg(2)});
  call->addParamAttr(1, llvm::Attribute::Returned);
  call->setTailCallKind(llvm::CallInst::TCK_NoTail);
  builder.CreateRet(call);

  llvm::SmallVector<char, 0> bitcode;
  llvm::raw_svector_ostream bitcodeStream(bitcode);
  llvm::WriteBitcodeToFile(module, bitcodeStream);
  writeFile(src, std::string(bitcode.begin(), bitcode.end()));

  auto result = ncc({"--target=aarch64-unknown-linux-gnu", "-fno-lto", "-c",
                     src.string(), "-o", obj.string()});
  EXPECT_TRUE(result.ok())
      << "AArch64 codegen treated a non-first returned argument as the call "
         "result:\n"
      << result.err;
}

// The type-mismatched case above trips an assertion in debug builds.  Keep a
// native semantic oracle too: with two pointer arguments the same bug is type
// correct and silently substitutes the pre-call first argument for x0.
TEST_F(LTOTest, AArch64NonFirstReturnedArgumentRuntimeSemantics) {
  if (!isArm64())
    GTEST_SKIP() << "requires a native AArch64 runtime oracle";

  auto src = tmpFile("aarch64_nonfirst_returned_runtime.bc");
  auto obj = tmpFile("aarch64_nonfirst_returned_runtime.o");
  auto exe = tmpFile("aarch64_nonfirst_returned_runtime");
  llvm::LLVMContext context;
  llvm::Module module("aarch64_nonfirst_returned_runtime", context);
  module.setTargetTriple(hostTriple());

  llvm::Type *i8Ty = llvm::Type::getInt8Ty(context);
  llvm::Type *i32Ty = llvm::Type::getInt32Ty(context);
  llvm::Type *ptrTy = llvm::PointerType::getUnqual(context);
  llvm::Function *callee = llvm::Function::Create(
      llvm::FunctionType::get(ptrTy, {ptrTy, ptrTy}, false),
      llvm::Function::ExternalLinkage, "returned_second", module);
  callee->addParamAttr(1, llvm::Attribute::Returned);
  callee->addFnAttr(llvm::Attribute::NoInline);
  callee->addFnAttr(llvm::Attribute::OptimizeNone);
  llvm::IRBuilder<> calleeBuilder(
      llvm::BasicBlock::Create(context, "entry", callee));
  calleeBuilder.CreateRet(callee->getArg(1));

  llvm::Function *mainFn =
      llvm::Function::Create(llvm::FunctionType::get(i32Ty, false),
                             llvm::Function::ExternalLinkage, "main", module);
  llvm::IRBuilder<> mainBuilder(
      llvm::BasicBlock::Create(context, "entry", mainFn));
  llvm::Value *wrong = mainBuilder.CreateAlloca(i8Ty, nullptr, "wrong");
  llvm::Value *right = mainBuilder.CreateAlloca(i8Ty, nullptr, "right");
  llvm::CallInst *call = mainBuilder.CreateCall(callee, {wrong, right});
  call->addParamAttr(1, llvm::Attribute::Returned);
  call->setTailCallKind(llvm::CallInst::TCK_NoTail);
  llvm::Value *matches = mainBuilder.CreateICmpEQ(call, right);
  mainBuilder.CreateRet(
      mainBuilder.CreateSelect(matches, llvm::ConstantInt::get(i32Ty, 0),
                               llvm::ConstantInt::get(i32Ty, 1)));

  llvm::SmallVector<char, 0> bitcode;
  llvm::raw_svector_ostream bitcodeStream(bitcode);
  llvm::WriteBitcodeToFile(module, bitcodeStream);
  writeFile(src, std::string(bitcode.begin(), bitcode.end()));

  std::vector<std::string> compileArgs = {"-O0", "-fno-lto", "-c"};
  for (const auto &flag : sysrootFlags())
    compileArgs.push_back(flag);
  for (const auto &flag : archFlags())
    compileArgs.push_back(flag);
  compileArgs.insert(compileArgs.end(), {src.string(), "-o", obj.string()});
  auto compile = ncc(compileArgs);
  ASSERT_EQ(compile.exitCode, 0) << compile.err;

  std::vector<std::string> linkArgs;
  for (const auto &flag : sysrootFlags())
    linkArgs.push_back(flag);
  for (const auto &flag : archFlags())
    linkArgs.push_back(flag);
  for (const auto &flag : linkFlags())
    linkArgs.push_back(flag);
  linkArgs.insert(linkArgs.end(), {obj.string(), "-o", exe.string()});
  auto link = ncc(linkArgs);
  ASSERT_EQ(link.exitCode, 0) << link.err;

  auto run = exec(exe.string(), {});
  EXPECT_EQ(run.exitCode, 0)
      << "AArch64 returned the first argument instead of the callee's x0";
}

TEST_F(LTOTest, MultiTU_AB) {
  auto ltoDir = testDir() / "lto";
  auto objA = tmpFile("lto_a.o");
  auto objB = tmpFile("lto_b.o");
  auto exe = tmpFile("lto_ab");

  std::vector<std::string> base = {"-std=c11"};
  for (auto &f : sysrootFlags())
    base.push_back(f);
  for (auto &f : archFlags())
    base.push_back(f);

  auto a1 = base;
  a1.insert(a1.end(), {"-flto", "-c", (ltoDir / "test_lto_a.c").string(), "-o",
                       objA.string()});
  ASSERT_EQ(ncc(a1).exitCode, 0);

  auto a2 = base;
  a2.insert(a2.end(), {"-flto", "-c", (ltoDir / "test_lto_b.c").string(), "-o",
                       objB.string()});
  ASSERT_EQ(ncc(a2).exitCode, 0);

  std::vector<std::string> link;
  for (auto &f : sysrootFlags())
    link.push_back(f);
  for (auto &f : archFlags())
    link.push_back(f);
  link.insert(link.end(),
              {"-flto", objA.string(), objB.string(), "-o", exe.string()});
  ASSERT_EQ(ncc(link).exitCode, 0);

  auto r = exec(exe.string(), {});
  EXPECT_EQ(r.exitCode, 0);
  EXPECT_TRUE(r.contains("add(3,4)=7"));
}

// Guards the driver forwarding of user -mllvm flags into the link job
// (populateLinkerDriverConfig -> LinkerDriverConfig::mllvmOpts ->
// createLTOConfig's scoped profile). Under (auto-)LTO the optimizer runs at
// link time,
// so flags like -neverc-module-inliner-threshold are meaningless unless
// they reach the linker's cl::opt parsing.
TEST_F(LTOTest, MllvmReachesLinkJob) {
  auto ltoDir = testDir() / "lto";
  auto objA = tmpFile("mllvm_a.o");
  auto objB = tmpFile("mllvm_b.o");

  std::vector<std::string> base = {"-std=c11"};
  for (auto &f : sysrootFlags())
    base.push_back(f);
  for (auto &f : archFlags())
    base.push_back(f);

  auto a1 = base;
  a1.insert(a1.end(), {"-flto", "-c", (ltoDir / "test_lto_a.c").string(), "-o",
                       objA.string()});
  ASSERT_EQ(ncc(a1).exitCode, 0);

  auto a2 = base;
  a2.insert(a2.end(), {"-flto", "-c", (ltoDir / "test_lto_b.c").string(), "-o",
                       objB.string()});
  ASSERT_EQ(ncc(a2).exitCode, 0);

  std::vector<std::string> link;
  for (auto &f : sysrootFlags())
    link.push_back(f);
  for (auto &f : archFlags())
    link.push_back(f);
  link.insert(link.end(), {"-flto", objA.string(), objB.string()});

  // A valid link-stage LLVM option must be accepted and produce a working
  // binary.
  auto good = link;
  auto exeGood = tmpFile("mllvm_good");
  good.insert(good.end(), {"-mllvm", "-neverc-module-inliner-threshold=0", "-o",
                           exeGood.string()});
  ASSERT_EQ(ncc(good).exitCode, 0);
  auto r = exec(exeGood.string(), {});
  EXPECT_EQ(r.exitCode, 0);
  EXPECT_TRUE(r.contains("add(3,4)=7"));

  // An unknown option must make the link fail: this proves the flag was
  // actually parsed by the link job instead of being silently dropped
  // (the pre-fix behavior).
  auto bad = link;
  auto exeBad = tmpFile("mllvm_bad");
  bad.insert(bad.end(),
             {"-mllvm", "-neverc-no-such-option-guard", "-o", exeBad.string()});
  auto br = ncc(bad);
  EXPECT_NE(br.exitCode, 0)
      << "link must fail on unknown -mllvm option; succeeding means the "
         "flag was dropped before reaching the linker";
  EXPECT_TRUE(br.stderrContains("Unknown command line argument"))
      << "stderr: " << br.err;
}

// LTO link cache (LTOCache.cpp): a second link with identical inputs and
// flags must hit the cache and produce a bit-identical binary; disabling
// via NEVERC_LTO_CACHE=0 must not write entries; changing a flag that
// affects codegen must miss.
TEST_F(LTOTest, LtoLinkCache) {
  auto ltoDir = testDir() / "lto";
  auto cacheDir = tmpFile("ltocache_dir");
  ScopedEnvVar CacheDir(linker::ltoCacheDirEnvVar, cacheDir.string().c_str());

  std::vector<std::string> base = {"-std=c11"};
  for (auto &f : sysrootFlags())
    base.push_back(f);
  for (auto &f : archFlags())
    base.push_back(f);

  auto objA = tmpFile("ltocache_a.o");
  auto objB = tmpFile("ltocache_b.o");
  auto a1 = base;
  a1.insert(a1.end(), {"-flto", "-c", (ltoDir / "test_lto_a.c").string(), "-o",
                       objA.string()});
  ASSERT_EQ(ncc(a1).exitCode, 0);
  auto a2 = base;
  a2.insert(a2.end(), {"-flto", "-c", (ltoDir / "test_lto_b.c").string(), "-o",
                       objB.string()});
  ASSERT_EQ(ncc(a2).exitCode, 0);

  std::vector<std::string> link;
  for (auto &f : sysrootFlags())
    link.push_back(f);
  for (auto &f : archFlags())
    link.push_back(f);
  link.insert(link.end(), {"-flto", objA.string(), objB.string()});
  // COFF stamps the PE header with the wall-clock second by default
  // (incremental-linker compatibility); two otherwise identical links
  // differ whenever that second ticks over.  Request reproducible output
  // (timestamp = content hash) so the cold/warm byte comparison below
  // only measures cache correctness.
  if (isWindows())
    link.push_back("-mno-incremental-linker-compatible");

  auto countEntries = [&] {
    size_t n = 0;
    std::error_code ec;
    for (fs::directory_iterator it(cacheDir, ec), e; !ec && it != e;
         it.increment(ec))
      if (it->path().filename().string().rfind(linker::ltoCacheEntryPrefix,
                                               0) == 0 &&
          it->path().extension() != linker::ltoCacheTmpSuffix)
        ++n;
    return n;
  };

  // Disabled: no entries may be written.
  auto exeOff = tmpFile("ltocache_off");
  {
    ScopedEnvVar Disabled(linker::ltoCacheEnvVar, linker::ltoCacheDisableValue);
    auto off = link;
    off.insert(off.end(), {"-o", exeOff.string()});
    ASSERT_EQ(ncc(off).exitCode, 0);
  }
  EXPECT_EQ(countEntries(), 0u);

  // Cold link populates the cache; warm link must be bit-identical.
  auto exe = tmpFile("ltocache_exe");
  auto l1 = link;
  l1.insert(l1.end(), {"-o", exe.string()});
  ASSERT_EQ(ncc(l1).exitCode, 0);
  size_t afterCold = countEntries();
  EXPECT_GE(afterCold, 1u);
  std::string cold = readFile(exe);

  ASSERT_EQ(ncc(l1).exitCode, 0);
  std::string warm = readFile(exe);
  EXPECT_EQ(countEntries(), afterCold) << "warm link must not add entries";
  EXPECT_TRUE(cold == warm) << "cache hit produced a different binary";
  auto r = exec(exe.string(), {});
  EXPECT_EQ(r.exitCode, 0);
  EXPECT_TRUE(r.contains("add(3,4)=7"));

  // A codegen-relevant flag change must miss (new entry).
  auto exeO0 = tmpFile("ltocache_o0");
  auto l2 = link;
  l2.insert(l2.end(), {"-O0", "-o", exeO0.string()});
  ASSERT_EQ(ncc(l2).exitCode, 0);
  EXPECT_GT(countEntries(), afterCold) << "flag change must be a cache miss";
}

TEST_F(LTOTest, LtoCacheDoesNotSilenceBackendWarnings) {
  constexpr const char *Warning = "neverc-lto-cache-warning";
  auto cacheDir = tmpFile("ltocache_warning_dir");
  ScopedEnvVar CacheDir(linker::ltoCacheDirEnvVar, cacheDir.string().c_str());
  ScopedEnvVar CacheEnabled(linker::ltoCacheEnvVar, "1");
  ScopedEnvVar PartitionCacheDisabled(linker::ltoPartitionCacheEnvVar,
                                      linker::ltoCacheDisableValue);
  ScopedEnvVar DebugUnset("NEVERC_PCG_DEBUG");

  auto source = tmpFile("ltocache_warning.c");
  auto object = tmpFile("ltocache_warning.o");
  auto output = tmpFile("ltocache_warning_exe");
  writeFile(
      source,
      "__attribute__((noinline)) int warned(void) {\n"
      "  __asm__ volatile(\".warning \\\"neverc-lto-cache-warning\\\"\");\n"
      "  return 0;\n"
      "}\n"
      "int main(void) { return warned(); }\n");

  std::vector<std::string> compile = {"-std=c11"};
  for (auto &flag : sysrootFlags())
    compile.push_back(flag);
  for (auto &flag : archFlags())
    compile.push_back(flag);
  compile.insert(compile.end(),
                 {"-flto", "-c", source.string(), "-o", object.string()});
  ASSERT_EQ(ncc(compile).exitCode, 0);

  std::vector<std::string> link;
  for (auto &flag : sysrootFlags())
    link.push_back(flag);
  for (auto &flag : archFlags())
    link.push_back(flag);
  link.insert(link.end(), {"-flto", object.string(), "-o", output.string()});
  if (isWindows())
    link.push_back("-mno-incremental-linker-compatible");

  for (unsigned attempt = 0; attempt != 2; ++attempt) {
    std::error_code ec;
    fs::remove(output, ec);
    CmdResult result = ncc(link);
    ASSERT_EQ(result.exitCode, 0) << result.err;
    EXPECT_TRUE(result.stderrContains(Warning)) << result.err;
    EXPECT_TRUE(fs::exists(output));
    EXPECT_GT(fs::file_size(output), 0u);
    EXPECT_EQ(countLtoCacheEntries(cacheDir), 0u)
        << "a cache entry would let a warm link silently skip the warning";
  }
}

TEST_F(LTOTest, SuppressedLtoWarningCannotBypassLaterFatalWarnings) {
  constexpr const char *Warning = "neverc-lto-policy-warning";
  auto cacheDir = tmpFile("ltocache_warning_policy_dir");
  ScopedEnvVar CacheDir(linker::ltoCacheDirEnvVar, cacheDir.string().c_str());
  ScopedEnvVar CacheEnabled(linker::ltoCacheEnvVar, "1");
  ScopedEnvVar PartitionCacheDisabled(linker::ltoPartitionCacheEnvVar,
                                      linker::ltoCacheDisableValue);
  ScopedEnvVar DebugUnset("NEVERC_PCG_DEBUG");

  auto source = tmpFile("ltocache_warning_policy.c");
  auto object = tmpFile("ltocache_warning_policy.o");
  auto output = tmpFile("ltocache_warning_policy_exe");
  writeFile(
      source,
      "__attribute__((noinline)) int warned(void) {\n"
      "  __asm__ volatile(\".warning \\\"neverc-lto-policy-warning\\\"\");\n"
      "  return 0;\n"
      "}\n"
      "int main(void) { return warned(); }\n");

  std::vector<std::string> compile = {"-std=c11"};
  for (auto &flag : sysrootFlags())
    compile.push_back(flag);
  for (auto &flag : archFlags())
    compile.push_back(flag);
  compile.insert(compile.end(),
                 {"-flto", "-c", source.string(), "-o", object.string()});
  ASSERT_EQ(ncc(compile).exitCode, 0);

  std::vector<std::string> link;
  for (auto &flag : sysrootFlags())
    link.push_back(flag);
  for (auto &flag : archFlags())
    link.push_back(flag);
  link.insert(link.end(), {"-flto", object.string()});
  if (isWindows())
    link.push_back("-mno-incremental-linker-compatible");

  auto suppressed = link;
  suppressed.insert(suppressed.end(), {"-w", "-o", output.string()});
  CmdResult first = ncc(suppressed);
  ASSERT_EQ(first.exitCode, 0) << first.err;
  EXPECT_FALSE(first.stderrContains(Warning)) << first.err;
  ASSERT_TRUE(fs::exists(output));
  const std::string ColdOutput = readFile(output);
  EXPECT_EQ(countLtoCacheEntries(cacheDir), 1u)
      << "suppressed diagnostics may populate their policy-specific cache";

  std::error_code ec;
  fs::remove(output, ec);
  CmdResult warm = ncc(suppressed);
  ASSERT_EQ(warm.exitCode, 0) << warm.err;
  EXPECT_FALSE(warm.stderrContains(Warning)) << warm.err;
  ASSERT_TRUE(fs::exists(output));
  EXPECT_EQ(readFile(output), ColdOutput);
  EXPECT_EQ(countLtoCacheEntries(cacheDir), 1u)
      << "a warm suppressed-warning link must reuse its cache entry";

  fs::remove(output, ec);
  auto fatal = link;
  fatal.insert(fatal.end(), {"-Werror", "-o", output.string()});
  CmdResult fatalResult = ncc(fatal);
  EXPECT_NE(fatalResult.exitCode, 0)
      << "a cache entry created under -w bypassed fatal-warning policy";
  EXPECT_TRUE(fatalResult.stderrContains(Warning)) << fatalResult.err;
  EXPECT_FALSE(fs::exists(output))
      << "a fatal LTO warning must not publish a linker output";
  EXPECT_EQ(countLtoCacheEntries(cacheDir), 1u)
      << "fatal diagnostics must not publish another cache entry";
}

TEST_F(LTOTest, LtoCachePreservesConsoleOptimizationRemarks) {
  constexpr const char *Callee = "neverc_lto_cache_remark_callee";
  auto cacheDir = tmpFile("ltocache_remark_dir");
  ScopedEnvVar CacheDir(linker::ltoCacheDirEnvVar, cacheDir.string().c_str());
  ScopedEnvVar CacheEnabled(linker::ltoCacheEnvVar, "1");
  ScopedEnvVar PartitionCacheDisabled(linker::ltoPartitionCacheEnvVar,
                                      linker::ltoCacheDisableValue);
  ScopedEnvVar DebugUnset("NEVERC_PCG_DEBUG");

  auto calleeSource = tmpFile("ltocache_remark_callee.c");
  auto callerSource = tmpFile("ltocache_remark_caller.c");
  auto calleeObject = tmpFile("ltocache_remark_callee.o");
  auto callerObject = tmpFile("ltocache_remark_caller.o");
  auto output = tmpFile("ltocache_remark_exe");
  writeFile(calleeSource, "__attribute__((always_inline)) int " +
                              std::string(Callee) +
                              "(int value) { return value + 1; }\n");
  writeFile(callerSource, "extern __attribute__((always_inline)) int " +
                              std::string(Callee) +
                              "(int);\nint main(void) { return " +
                              std::string(Callee) + "(41) != 42; }\n");

  std::vector<std::string> base = {"-std=c11", "-O2", "-flto"};
  for (auto &flag : sysrootFlags())
    base.push_back(flag);
  for (auto &flag : archFlags())
    base.push_back(flag);
  auto compile = [&](const fs::path &source, const fs::path &object) {
    auto args = base;
    args.insert(args.end(), {"-c", source.string(), "-o", object.string()});
    return ncc(args);
  };
  ASSERT_EQ(compile(calleeSource, calleeObject).exitCode, 0);
  ASSERT_EQ(compile(callerSource, callerObject).exitCode, 0);

  auto link = base;
  link.insert(link.end(), {calleeObject.string(), callerObject.string(),
                           "-Rpass=^inline$", "-o", output.string()});
  if (isWindows())
    link.push_back("-mno-incremental-linker-compatible");

  for (unsigned attempt = 0; attempt != 2; ++attempt) {
    std::error_code ec;
    fs::remove(output, ec);
    CmdResult result = ncc(link);
    ASSERT_EQ(result.exitCode, 0) << result.err;
    EXPECT_TRUE(result.contains(Callee) || result.stderrContains(Callee))
        << "stdout:\n"
        << result.out << "\nstderr:\n"
        << result.err;
    EXPECT_TRUE(fs::exists(output));
    EXPECT_EQ(countLtoCacheEntries(cacheDir), 0u)
        << "raw LLVM options can have observable side effects and must bypass "
           "the cache";
  }
}

// Per-partition object cache (LTOCache.cpp + ParallelCodeGenMerge.cpp):
// partition assignment is a stable name hash, and each partition's object
// is cached keyed on its post-IPO bitcode.  Editing one function must
// invalidate only the full-link entry plus the single partition that
// contains the function; the relink mixing cached and fresh partitions
// must be byte-identical to a cache-disabled clean relink.
TEST_F(LTOTest, LtoPartitionCache) {
  auto cacheDir = tmpFile("pcache_dir");
  ScopedEnvVar CacheDir(linker::ltoCacheDirEnvVar, cacheDir.string().c_str());
  ScopedEnvVar DebugUnset("NEVERC_PCG_DEBUG");

  // Generate a project that crosses the partitioned-codegen thresholds
  // (>= 8 surviving functions, >= 10000 merged IR instructions) and
  // stays partition-stable: noinline bodies seeded from a volatile
  // global, no cross-file calls.
  constexpr int NFiles = 16, NFuncs = 4, NStmts = 100;
  auto srcDir = tmpFile("pcache_src");
  fs::create_directories(srcDir);

  auto fnBody = [&](int fi, int fj, int extra, bool emitCodegenError,
                    bool emitCodegenWarning) {
    std::string b;
    b += "__attribute__((noinline)) unsigned f_" + std::to_string(fi) + "_" +
         std::to_string(fj) + "(unsigned a) {\n";
    b += "  unsigned x = g_seed + a;\n";
    for (int s = 0; s < NStmts + extra; ++s) {
      unsigned mul = (2654435761u + 2654435761u * unsigned(s) +
                      97u * unsigned(fi) + 31u * unsigned(fj)) |
                     1u;
      b += "  x ^= x >> " + std::to_string(5 + (s % 11)) +
           "; x *= " + std::to_string(mul) + "u; x ^= x << " +
           std::to_string(3 + (s % 7)) + ";\n";
    }
    if (emitCodegenError)
      b += "  __asm__ volatile(\".error\");\n";
    if (emitCodegenWarning && fj >= NFuncs - 2)
      b += "  __asm__ volatile(\".warning "
           "\\\"neverc-pcg-cache-warning\\\"\");\n";
    b += "  return x;\n}\n";
    return b;
  };
  auto writeUnit = [&](int fi, int extraInLastFn, bool emitCodegenError = false,
                       bool emitCodegenWarning = false) {
    std::string src = "extern volatile unsigned g_seed;\n";
    for (int fj = 0; fj < NFuncs; ++fj)
      src += fnBody(fi, fj, fj == NFuncs - 1 ? extraInLastFn : 0,
                    emitCodegenError && fj == NFuncs - 1, emitCodegenWarning);
    writeFile(srcDir / ("u" + std::to_string(fi) + ".c"), src);
  };
  for (int fi = 0; fi < NFiles; ++fi)
    writeUnit(fi, 0);
  {
    std::string m = "#include <stdio.h>\nvolatile unsigned g_seed = "
                    "0x12345678u;\n";
    for (int fi = 0; fi < NFiles; ++fi)
      for (int fj = 0; fj < NFuncs; ++fj)
        m += "extern unsigned f_" + std::to_string(fi) + "_" +
             std::to_string(fj) + "(unsigned);\n";
    m += "int main(void) {\n  unsigned acc = 0;\n";
    for (int fi = 0; fi < NFiles; ++fi)
      for (int fj = 0; fj < NFuncs; ++fj)
        m += "  acc ^= f_" + std::to_string(fi) + "_" + std::to_string(fj) +
             "(" + std::to_string(fi * NFuncs + fj) + "u);\n";
    m += "  printf(\"CK=%08x\\n\", acc);\n  return 0;\n}\n";
    writeFile(srcDir / "main.c", m);
  }

  std::vector<std::string> base = {"-std=c11"};
  for (auto &f : sysrootFlags())
    base.push_back(f);
  for (auto &f : archFlags())
    base.push_back(f);

  // Default driver mode = auto-LTO: objects carry bitcode, the link runs
  // the partitioned LTO pipeline this test exercises.
  auto compileUnit = [&](const std::string &stem) {
    auto c = base;
    c.insert(c.end(), {"-c", (srcDir / (stem + ".c")).string(), "-o",
                       (srcDir / (stem + ".o")).string()});
    ASSERT_EQ(ncc(c).exitCode, 0) << stem;
  };
  for (int fi = 0; fi < NFiles; ++fi)
    compileUnit("u" + std::to_string(fi));
  compileUnit("main");

  std::vector<std::string> link;
  for (auto &f : sysrootFlags())
    link.push_back(f);
  for (auto &f : archFlags())
    link.push_back(f);
  for (int fi = 0; fi < NFiles; ++fi)
    link.push_back((srcDir / ("u" + std::to_string(fi) + ".o")).string());
  link.push_back((srcDir / "main.o").string());
  if (isWindows())
    link.push_back("-mno-incremental-linker-compatible");
  auto exe = tmpFile("pcache_exe");
  link.insert(link.end(), {"-o", exe.string()});

  auto countEntries = [](const fs::path &dir) {
    size_t n = 0;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), e; !ec && it != e;
         it.increment(ec))
      if (it->path().filename().string().rfind(linker::ltoCacheEntryPrefix,
                                               0) == 0 &&
          it->path().extension() != linker::ltoCacheTmpSuffix)
        ++n;
    return n;
  };

  // Cold link: one full-link entry + one entry per partition.
  ASSERT_EQ(ncc(link).exitCode, 0);
  size_t afterCold = countEntries(cacheDir);
  ASSERT_GE(afterCold, 3u) << "expected partitioned codegen (>= 2 partitions)";
  auto r1 = exec(exe.string(), {});
  ASSERT_EQ(r1.exitCode, 0);
  ASSERT_TRUE(r1.contains("CK=")) << r1.out;

  // Edit one function body in one unit: only that partition plus the
  // full-link key may miss.
  writeUnit(3, 2);
  compileUnit("u3");
  ASSERT_EQ(ncc(link).exitCode, 0);
  size_t afterEdit = countEntries(cacheDir);
  EXPECT_EQ(afterEdit, afterCold + 2)
      << "an edit to one function must add exactly one full-link entry and "
         "one partition entry; more means partition assignment is unstable";
  std::string mixed = readFile(exe);

  // The mixed cached/fresh link must equal a cache-disabled clean link.
  {
    ScopedEnvVar Disabled(linker::ltoCacheEnvVar, linker::ltoCacheDisableValue);
    ASSERT_EQ(ncc(link).exitCode, 0);
  }
  std::string clean = readFile(exe);
  EXPECT_TRUE(mixed == clean)
      << "cached-partition relink differs from clean relink";

  auto r2 = exec(exe.string(), {});
  EXPECT_EQ(r2.exitCode, 0);
  EXPECT_TRUE(r2.contains("CK=")) << r2.out;
  EXPECT_NE(r1.out, r2.out) << "edit must change the checksum";

  // A backend diagnostic can still leave bytes in the partition object
  // buffer.  That object is invalid and must not be committed to the cache.
  writeUnit(3, 3, /*emitCodegenError=*/true);
  compileUnit("u3");
  auto failureCacheDir = tmpFile("pcache_failure_dir");
  ASSERT_TRUE(fs::create_directory(failureCacheDir));
  ASSERT_EQ(countEntries(failureCacheDir), 0u);
  {
    ScopedEnvVar FailureCacheDir(linker::ltoCacheDirEnvVar,
                                 failureCacheDir.string().c_str());
    CmdResult firstFailure = ncc(link);
    EXPECT_NE(firstFailure.exitCode, 0);
    EXPECT_TRUE(firstFailure.stderrContains(".error directive invoked"))
        << "link did not reach the intentional backend diagnostic:\n"
        << firstFailure.err;
    EXPECT_EQ(countEntries(failureCacheDir), 0u)
        << "a failed partitioned link must not commit successful sibling "
           "partitions";

    CmdResult secondFailure = ncc(link);
    EXPECT_NE(secondFailure.exitCode, 0)
        << "a cached failed partition made an invalid link succeed";
    EXPECT_TRUE(secondFailure.stderrContains(".error directive invoked"))
        << "retry did not regenerate the failed partition:\n"
        << secondFailure.err;
    EXPECT_EQ(countEntries(failureCacheDir), 0u)
        << "retry of a failed partitioned link must leave the cache empty";
  }

  // A non-fatal partition diagnostic is just as observable as an error. It
  // must reach the request's diagnostic policy on every link, and neither the
  // partition layer nor the full-link layer may cache across it.
  writeUnit(3, 4, /*emitCodegenError=*/false,
            /*emitCodegenWarning=*/true);
  compileUnit("u3");
  auto warningCacheDir = tmpFile("pcache_warning_dir");
  ASSERT_TRUE(fs::create_directory(warningCacheDir));
  ASSERT_EQ(countEntries(warningCacheDir), 0u);
  {
    ScopedEnvVar WarningCacheDir(linker::ltoCacheDirEnvVar,
                                 warningCacheDir.string().c_str());
    ScopedEnvVar CacheEnabled(linker::ltoCacheEnvVar, "1");
    ScopedEnvVar PartitionCacheEnabled(linker::ltoPartitionCacheEnvVar, "1");
    for (unsigned attempt = 0; attempt != 2; ++attempt) {
      std::error_code ec;
      fs::remove(exe, ec);
      CmdResult result = ncc(link);
      ASSERT_EQ(result.exitCode, 0) << result.err;
      EXPECT_EQ(StringRef(result.err).count("warning: "), 2u)
          << "non-error diagnostics must preserve occurrence count:\n"
          << result.err;
      EXPECT_TRUE(fs::exists(exe));
      EXPECT_EQ(countEntries(warningCacheDir), 0u)
          << "a partition warning must block both partition and full-link "
             "cache publication";
    }
  }

  // Debug output is observable and is generated only when the partitioned
  // pipeline actually runs. A full-link cache hit must not swallow it.
  writeUnit(3, 5);
  compileUnit("u3");
  auto debugCacheDir = tmpFile("pcache_debug_dir");
  ASSERT_TRUE(fs::create_directory(debugCacheDir));
  {
    ScopedEnvVar DebugCacheDir(linker::ltoCacheDirEnvVar,
                               debugCacheDir.string().c_str());
    ScopedEnvVar CacheEnabled(linker::ltoCacheEnvVar, "1");
    ScopedEnvVar PartitionCacheEnabled(linker::ltoPartitionCacheEnvVar, "1");
    ScopedEnvVar Debug("NEVERC_PCG_DEBUG", "1");
    for (unsigned Attempt = 0; Attempt != 2; ++Attempt) {
      std::error_code EC;
      fs::remove(exe, EC);
      CmdResult Result = ncc(link);
      ASSERT_EQ(Result.exitCode, 0) << Result.err;
      EXPECT_TRUE(Result.stderrContains("[pcg]")) << Result.err;
      EXPECT_TRUE(fs::exists(exe));
      EXPECT_EQ(countEntries(debugCacheDir), 0u)
          << "PCG debug output must not be hidden by either cache layer";
    }
  }
}

TEST_F(LTOTest, ParallelCodegenPreservesAliasUsers) {
  auto cacheDir = tmpFile("pcg_alias_cache");
  ScopedEnvVar CacheDir(linker::ltoCacheDirEnvVar, cacheDir.string().c_str());
  ScopedEnvVar Strict("NEVERC_PCG_STRICT", "1");
  ScopedEnvVar Debug("NEVERC_PCG_DEBUG", "1");

  auto buildAndRun = [&](const std::string &Tag, bool DisablePartitionCache) {
    auto src = tmpFile("pcg_alias_" + Tag + ".c");
    auto obj = tmpFile("pcg_alias_" + Tag + ".o");
    std::string code = "typedef unsigned long long u64;\n"
                       "__attribute__((noinline)) u64 alias_target(u64 x) {\n"
                       "  return x * 3ULL + 1ULL;\n"
                       "}\n"
                       "extern u64 public_alias(u64) "
                       "__attribute__((alias(\"alias_target\")));\n";
    for (unsigned I = 0; I < 32; ++I)
      code += "__attribute__((noinline)) u64 alias_user_" + std::to_string(I) +
              "(u64 x) { return public_alias(x + " + std::to_string(I) +
              "ULL); }\n";
    code += "int main(void) {\n";
    for (unsigned I = 0; I < 32; ++I)
      code += "  if (alias_user_" + std::to_string(I) + "(1) != ((1ULL + " +
              std::to_string(I) + "ULL) * 3ULL + 1ULL)) return " +
              std::to_string(I + 1) + ";\n";
    code += "  return 0;\n}\n";
    writeFile(src, code);

    std::vector<std::string> args = {
        "--target=x86_64-unknown-linux-gnu",
        "-O0",
        "-std=gnu11",
        "-fno-lto",
        "-c",
        "-mllvm",
        "-neverc-pcg-min-funcs=2",
        "-mllvm",
        "-neverc-pcg-weight-floor=1",
        "-mllvm",
        "-neverc-pcg-cg-weight-div=1",
    };
    args.insert(args.end(), {src.string(), "-o", obj.string()});

    std::optional<ScopedEnvVar> PartitionCache;
    if (DisablePartitionCache)
      PartitionCache.emplace(linker::ltoPartitionCacheEnvVar,
                             linker::ltoCacheDisableValue);
    else
      PartitionCache.emplace(linker::ltoPartitionCacheEnvVar, "1");

    CmdResult compile = ncc(args);
    ASSERT_EQ(compile.exitCode, 0) << Tag << ":\n" << compile.err;
    EXPECT_TRUE(compile.stderrContains("[pcg] SUCCESS"))
        << Tag << " did not exercise merged parallel codegen:\n"
        << compile.err;
    EXPECT_FALSE(readFile(obj).empty()) << Tag << " emitted an empty object";
  };

  buildAndRun("cached", false);
  buildAndRun("uncached", true);
}

TEST_F(LTOTest, ParallelCodegenEmitsMsvcLinkerDirectivesExactlyOnce) {
  auto src = tmpFile("pcg_linker_options.c");
  std::string code = "#pragma comment(lib, \"advapi32.lib\")\n"
                     "#pragma detect_mismatch(\"pcg-metadata\", \"stable\")\n"
                     "__declspec(dllexport) __attribute__((used))\n"
                     "int pcg_metadata_anchor(void) { return 7; }\n";
  for (unsigned I = 0; I < 32; ++I)
    code += "__attribute__((noinline)) int pcg_metadata_user_" +
            std::to_string(I) + "(int x) { return x * " +
            std::to_string(I + 3) + " + pcg_metadata_anchor(); }\n";
  writeFile(src, code);

  ScopedEnvVar Strict("NEVERC_PCG_STRICT", "1");
  ScopedEnvVar Debug("NEVERC_PCG_DEBUG", "1");
  unsigned TargetIndex = 0;
  for (const std::string &Target :
       {"x86_64-pc-windows-msvc", "aarch64-pc-windows-msvc"}) {
    SCOPED_TRACE(Target);
    auto obj =
        tmpFile("pcg_linker_options_" + std::to_string(TargetIndex++) + ".obj");
    std::vector<std::string> args = {
        "--target=" + Target,
        "-O0",
        "-std=gnu11",
        "-fno-lto",
        "-c",
        "-mllvm",
        "-neverc-pcg-min-funcs=2",
        "-mllvm",
        "-neverc-pcg-weight-floor=1",
        "-mllvm",
        "-neverc-pcg-cg-weight-div=1",
        src.string(),
        "-o",
        obj.string(),
    };
    CmdResult compile = ncc(args);
    ASSERT_EQ(compile.exitCode, 0) << compile.err;
    ASSERT_TRUE(compile.stderrContains("[pcg] SUCCESS"))
        << "test did not exercise merged parallel codegen:\n"
        << compile.err;

    const std::string bytes = readFile(obj);
    auto count = [&](const std::string &needle) {
      size_t result = 0;
      for (size_t pos = 0; (pos = bytes.find(needle, pos)) != std::string::npos;
           pos += needle.size())
        ++result;
      return result;
    };
    EXPECT_EQ(count("/DEFAULTLIB:advapi32.lib"), 1u);
    EXPECT_EQ(count("/FAILIFMISMATCH:\"pcg-metadata=stable\""), 1u);
    EXPECT_EQ(count("/EXPORT:pcg_metadata_anchor"), 1u);
    EXPECT_EQ(count("/INCLUDE:pcg_metadata_anchor"), 1u);
    EXPECT_EQ(count("--defaultlib=advapi32.lib"), 0u);
    EXPECT_EQ(count("--failifmismatch="), 0u);
    EXPECT_EQ(count("--export="), 0u);
    EXPECT_EQ(count("--include="), 0u);
  }
}

// Auto-LTO compile-time cliff guard for the two cooperating valves that tame
// it: the inline cap (Inliner.cpp's NevercInlineMaxCallerLoops) and the
// full-unroll cap (LoopUnrollPass.cpp's NevercFullUnrollMaxLoopsPerFunc).  When
// main calls many small loop-bearing leaves exactly once, last-call-to-static
// inlining wants to fold them all into main -- a single function with hundreds
// of fully-unrollable constant-trip loops -- and full unrolling then makes
// ScalarEvolution's trip-count / exit-value machinery superlinear in the loop
// count (measured ~O(N^2); N=360 used to time out entirely).
//
// The two caps are complementary, which is *why this test must disable both* to
// see the cliff: the inline cap alone already stops main growing past its loop
// limit (NevercInlineMaxCallerLoops, default 32), so toggling only the unroll
// cap barely moves the needle (measured 0.6s vs 0.6s -- a coin-flip timing
// assertion, the historical flake here).  With both caps off the collapse and
// the unroll blowup both fire and the link detonates (measured ~0.7s vs ~6s+, a
// ~10x gap), giving the guard a wide, non-flaky margin on any hardware.
//
// This pins both halves of the contract: (1) the same program links far faster
// with the caps at their defaults than with both disabled, and (2) the two
// binaries produce identical output -- the caps may only withdraw an
// optimization, never change program semantics.
TEST_F(LTOTest, AutoLtoLoopDenseNoCompileCliff) {
  // Cold, comparable links: disable both cache layers so neither timing is a
  // cache hit of the other.
  ScopedEnvVar NoLtoCache(linker::ltoCacheEnvVar, linker::ltoCacheDisableValue);
  ScopedEnvVar NoPartitionCache(linker::ltoPartitionCacheEnvVar,
                                linker::ltoCacheDisableValue);

  constexpr int NFiles = 15, NFuncsPerFile = 10; // 150 single-call leaves
  auto srcDir = tmpFile("cliff_src");
  fs::create_directories(srcDir);

  std::vector<std::string> names;
  for (int fi = 0; fi < NFiles; ++fi) {
    std::string src = "#include <stdint.h>\n";
    for (int fj = 0; fj < NFuncsPerFile; ++fj) {
      std::string nm = "fn_" + std::to_string(fi) + "_" + std::to_string(fj);
      names.push_back(nm);
      // Distinct odd constants per function so nothing folds them together;
      // a constant-trip (7) loop makes each a full-unroll candidate.
      unsigned c1 = (2654435761u * unsigned(fi * 131 + fj + 1)) | 1u;
      unsigned c2 =
          (40503u * unsigned(fi + 7) + 2246822519u * unsigned(fj + 3)) | 1u;
      unsigned c3 = (2166136261u ^ (16777619u * unsigned(fi * 17 + fj))) | 1u;
      src += "uint64_t " + nm + "(uint64_t x){ uint64_t a=x^" +
             std::to_string(c1) + "ULL; for(int i=0;i<7;i++){ a=a*" +
             std::to_string(c2) +
             "ULL+(a>>13)+i; if(a&1) a^=" + std::to_string(c3) +
             "ULL; } return a; }\n";
    }
    writeFile(srcDir / ("m" + std::to_string(fi) + ".c"), src);
  }
  {
    std::string m = "#include <stdint.h>\n#include <stdio.h>\n";
    for (auto &n : names)
      m += "uint64_t " + n + "(uint64_t);\n";
    m += "int main(void){ uint64_t acc=1;\n for(int r=0;r<3;r++){\n";
    for (auto &n : names)
      m += "  acc=acc*1000003ULL+" + n + "(acc);\n";
    m += " }\n printf(\"CK=%llu\\n\",(unsigned long long)acc); return 0; }\n";
    writeFile(srcDir / "main.c", m);
  }

  std::vector<std::string> base = {"-std=c11"};
  for (auto &f : sysrootFlags())
    base.push_back(f);
  for (auto &f : archFlags())
    base.push_back(f);

  // Default driver mode = auto-LTO: objects carry bitcode and the whole-program
  // optimizer (inliner + unroller) runs at link time, which is where the cliff
  // lives.
  std::vector<std::string> objs;
  auto compileUnit = [&](const std::string &stem) {
    auto c = base;
    auto o = (srcDir / (stem + ".o")).string();
    c.insert(c.end(), {"-c", (srcDir / (stem + ".c")).string(), "-o", o});
    ASSERT_EQ(ncc(c).exitCode, 0) << stem;
    objs.push_back(o);
  };
  for (int fi = 0; fi < NFiles; ++fi)
    compileUnit("m" + std::to_string(fi));
  compileUnit("main");

  auto linkArgs = [&](const std::string &exe, bool capsOff) {
    std::vector<std::string> l;
    for (auto &f : sysrootFlags())
      l.push_back(f);
    for (auto &f : archFlags())
      l.push_back(f);
    for (auto &o : objs)
      l.push_back(o);
    if (capsOff) {
      // Reproduce the pre-fix pathology *in full*.  Both caps must be off:
      // disabling only the unroll cap leaves the inline cap holding main at
      // ~12 loops, so the superlinear blowup never forms and the timing arms
      // become indistinguishable (the historical flake).  Off together, main
      // collapses to one giant function and the unroller detonates SCEV.
      l.push_back("-mllvm");
      l.push_back("-neverc-full-unroll-max-loops-per-function=0");
      l.push_back("-mllvm");
      l.push_back("-neverc-inline-max-caller-loops=0");
    }
    if (isWindows())
      l.push_back("-mno-incremental-linker-compatible");
    l.insert(l.end(), {"-o", exe});
    return l;
  };

  auto exeOn = tmpFile("cliff_on");
  auto t0 = std::chrono::steady_clock::now();
  auto rOn = ncc(linkArgs(exeOn.string(), /*capsOff=*/false));
  auto t1 = std::chrono::steady_clock::now();
  ASSERT_EQ(rOn.exitCode, 0) << rOn.err;
  double tOn = std::chrono::duration<double>(t1 - t0).count();

  auto exeOff = tmpFile("cliff_off");
  auto t2 = std::chrono::steady_clock::now();
  auto rOff = ncc(linkArgs(exeOff.string(), /*capsOff=*/true));
  auto t3 = std::chrono::steady_clock::now();
  ASSERT_EQ(rOff.exitCode, 0) << rOff.err;
  double tOff = std::chrono::duration<double>(t3 - t2).count();

  // (1) Semantics must be unchanged by the caps.
  auto outOn = exec(exeOn.string(), {});
  auto outOff = exec(exeOff.string(), {});
  EXPECT_EQ(outOn.exitCode, 0);
  EXPECT_EQ(outOff.exitCode, 0);
  EXPECT_TRUE(outOn.contains("CK=")) << outOn.out;
  EXPECT_EQ(outOn.out, outOff.out)
      << "a cap changed program output (caps must be semantics-preserving)";

  // (2) The caps must mitigate the superlinear blowup.  The real separation
  // with both caps off is ~10x (measured ~0.6s vs ~6s on a 16-core host), so
  // requiring the capped link to be under half the uncapped link is a wide,
  // non-flaky margin that still fails loudly if either cap regresses (then the
  // collapse/unroll fires in the "on" arm too and the times converge).
  EXPECT_LT(tOn, tOff * 0.5)
      << "loop-density caps gave no link-time benefit (tOn=" << tOn
      << "s tOff=" << tOff
      << "s): NevercInlineMaxCallerLoops or "
         "NevercFullUnrollMaxLoopsPerFunc may have regressed";
}

// Each auto-LTO compile-cost control must be independently overrideable without
// changing observable program behavior. Keep this normal regression free of
// wall-clock assertions: timing acceptance is covered by the explicitly
// enabled benchmark below.
TEST_F(LTOTest, AutoLtoBoundedIndVarWideningSemanticsPreserved) {
  ScopedEnvVar NoLtoCache(linker::ltoCacheEnvVar, linker::ltoCacheDisableValue);
  ScopedEnvVar NoPartitionCache(linker::ltoPartitionCacheEnvVar,
                                linker::ltoCacheDisableValue);

  const auto Sources =
      writeAutoLtoLoopDenseProject("bounded_indvars_src", false);

  struct BuildArm {
    const char *Tag;
    std::vector<std::string> Extra;
  };
  const std::vector<BuildArm> Arms = {
      {"default", {}},
      {"bounded_widening",
       {"-mllvm", "-neverc-auto-lto-indvars-widen-max-function-loops=31"}},
      {"former_behavior",
       {"-mllvm", "-neverc-auto-lto-scev-huge-expr-threshold=512"}},
      {"bounded_old_scev",
       {"-mllvm", "-neverc-auto-lto-indvars-widen-max-function-loops=31",
        "-mllvm", "-neverc-auto-lto-scev-huge-expr-threshold=512"}},
  };

  auto build = [&](const std::string &Tag,
                   const std::vector<std::string> &Extra) {
    fs::path Output = tmpFile("bounded_indvars_" + Tag);
    std::vector<std::string> Args = {"-O2", "-std=c11"};
    for (const auto &Flag : sysrootFlags())
      Args.push_back(Flag);
    for (const auto &Flag : archFlags())
      Args.push_back(Flag);
    Args.insert(Args.end(), Extra.begin(), Extra.end());
    Args.insert(Args.end(), Sources.begin(), Sources.end());
    if (isWindows())
      Args.push_back("-mno-incremental-linker-compatible");
    Args.insert(Args.end(), {"-o", Output.string()});

    CmdResult Result = ncc(Args);
    return std::make_pair(std::move(Result), std::move(Output));
  };

  {
    ScopedEnvVar Debug("NEVERC_PCG_DEBUG", "1");
    auto [Result, Output] = build("probe", {});
    ASSERT_EQ(Result.exitCode, 0) << Result.err;
    EXPECT_TRUE(Result.stderrContains("[pcg] p-opt engaged")) << Result.err;
  }

  std::vector<fs::path> Outputs;
  for (const BuildArm &Arm : Arms) {
    auto [Result, Output] = build(Arm.Tag, Arm.Extra);
    ASSERT_EQ(Result.exitCode, 0) << Arm.Tag << ":\n" << Result.err;
    Outputs.push_back(std::move(Output));
  }

  std::optional<std::string> ExpectedOutput;
  for (size_t I = 0; I < Arms.size(); ++I) {
    CmdResult Run = exec(Outputs[I].string(), {});
    ASSERT_EQ(Run.exitCode, 0) << Arms[I].Tag << ":\n" << Run.err;
    EXPECT_TRUE(Run.contains("CK=")) << Arms[I].Tag << ":\n" << Run.out;
    if (!ExpectedOutput)
      ExpectedOutput = Run.out;
    else
      EXPECT_EQ(Run.out, *ExpectedOutput) << Arms[I].Tag;
  }

  ASSERT_EQ(Outputs.size(), 4u);
  EXPECT_LE(fileSize(Outputs[0]),
            static_cast<size_t>(fileSize(Outputs[2]) * 1.01) + 1);
}

// Keep the quantitative 25% target for explicitly bounded widening as an
// acceptance benchmark rather than a normal unit-test gate. This pathological
// mode is intentionally not the production default. Run with
// NEVERC_RUN_PERF_BENCHMARKS=1.
TEST_F(LTOTest, AutoLtoBoundedIndVarWideningCompileBenchmark) {
  const char *RunBenchmarks = std::getenv("NEVERC_RUN_PERF_BENCHMARKS");
  if (!RunBenchmarks || std::string(RunBenchmarks) == "0")
    GTEST_SKIP() << "set NEVERC_RUN_PERF_BENCHMARKS=1 to run timing acceptance";

  ScopedEnvVar NoLtoCache(linker::ltoCacheEnvVar, linker::ltoCacheDisableValue);
  ScopedEnvVar NoPartitionCache(linker::ltoPartitionCacheEnvVar,
                                linker::ltoCacheDisableValue);

  const auto Sources =
      writeAutoLtoLoopDenseProject("bounded_indvars_bench_src", false);

  struct TimedBuild {
    CmdResult Result;
    double Seconds;
    fs::path Output;
  };
  auto build = [&](const std::string &Tag, unsigned Run,
                   const std::vector<std::string> &Extra) {
    fs::path Output =
        tmpFile("bounded_indvars_bench_" + Tag + "_" + std::to_string(Run));
    std::vector<std::string> Args = {"-O2", "-std=c11"};
    for (const auto &Flag : sysrootFlags())
      Args.push_back(Flag);
    for (const auto &Flag : archFlags())
      Args.push_back(Flag);
    Args.insert(Args.end(), Extra.begin(), Extra.end());
    Args.insert(Args.end(), Sources.begin(), Sources.end());
    if (isWindows())
      Args.push_back("-mno-incremental-linker-compatible");
    Args.insert(Args.end(), {"-o", Output.string()});

    auto Start = std::chrono::steady_clock::now();
    CmdResult Result = ncc(Args);
    double Seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - Start)
            .count();
    return TimedBuild{std::move(Result), Seconds, std::move(Output)};
  };

  const std::vector<std::string> BoundedBehavior = {
      "-mllvm", "-neverc-auto-lto-indvars-widen-max-function-loops=31"};
  const std::vector<std::string> OldBehavior = {
      "-mllvm", "-neverc-auto-lto-indvars-widen-max-function-loops=0", "-mllvm",
      "-neverc-auto-lto-scev-huge-expr-threshold=512"};

  std::vector<double> BoundedTimes;
  std::vector<double> FormerTimes;
  fs::path BoundedOutput;
  fs::path FormerOutput;
  for (unsigned Run = 0; Run < 5; ++Run) {
    auto runBounded = [&] {
      TimedBuild Build = build("bounded", Run, BoundedBehavior);
      if (Build.Result.exitCode != 0) {
        ADD_FAILURE() << Build.Result.err;
        return false;
      }
      BoundedTimes.push_back(Build.Seconds);
      BoundedOutput = std::move(Build.Output);
      return true;
    };
    auto runFormer = [&] {
      TimedBuild Build = build("former", Run, OldBehavior);
      if (Build.Result.exitCode != 0) {
        ADD_FAILURE() << Build.Result.err;
        return false;
      }
      FormerTimes.push_back(Build.Seconds);
      FormerOutput = std::move(Build.Output);
      return true;
    };

    if ((Run & 1) == 0) {
      ASSERT_TRUE(runBounded());
      ASSERT_TRUE(runFormer());
    } else {
      ASSERT_TRUE(runFormer());
      ASSERT_TRUE(runBounded());
    }
  }

  const double BoundedMedian = medianSeconds(BoundedTimes);
  const double FormerMedian = medianSeconds(FormerTimes);
  RecordProperty("bounded_median_seconds", BoundedMedian);
  RecordProperty("former_median_seconds", FormerMedian);
  EXPECT_LE(BoundedMedian, FormerMedian * 0.75)
      << "explicit bounded IV widening must improve the interleaved complete "
         "cold-build median by at least 25%";

  CmdResult BoundedRun = exec(BoundedOutput.string(), {});
  CmdResult FormerRun = exec(FormerOutput.string(), {});
  ASSERT_EQ(BoundedRun.exitCode, 0) << BoundedRun.err;
  ASSERT_EQ(FormerRun.exitCode, 0) << FormerRun.err;
  EXPECT_TRUE(BoundedRun.contains("CK=")) << BoundedRun.out;
  EXPECT_EQ(BoundedRun.out, FormerRun.out);
  EXPECT_LE(fileSize(BoundedOutput),
            static_cast<size_t>(fileSize(FormerOutput) * 1.01) + 1);
}

// Auto-LTO determinism contract: the parallel-codegen + merge pipeline must be
// a pure function of its inputs, independent of how many worker threads happen
// to run it.  The partition count is derived only from the module (instruction
// / loop / function counts), never from hardware_concurrency(), and partition
// results are collected by index, not completion order -- so a 1-thread build,
// a 4-thread build and a 16-thread build of the same sources must emit a
// byte-identical object.  Pinning this guards two things at once: that
// execution parallelism never leaks into the output (e.g. a future change
// collecting results in finish order), and that the object is reproducible
// across machines with different core counts (the same property, since
// NEVERC_PCG_THREADS here stands in for a different host's core count).
//
// The artifact compared is the relocatable (`-r`) merge -- the merger's direct
// output and exactly the shape a kernel module (.ko) ships -- not a final
// executable, whose linker-generated UUID / ad-hoc code signature legitimately
// vary run to run and would mask the property under test.
TEST_F(LTOTest, AutoLtoMergeIsThreadCountIndependent) {
  // Disable both cache layers so every link genuinely re-runs parallel codegen
  // rather than restoring a previous link's stored object (which would make the
  // comparison trivially pass without exercising codegen at all).
  ScopedEnvVar NoLtoCache(linker::ltoCacheEnvVar, linker::ltoCacheDisableValue);
  ScopedEnvVar NoPartitionCache(linker::ltoPartitionCacheEnvVar,
                                linker::ltoCacheDisableValue);

  // 128 loop-bearing leaves: well above the parallel-codegen engagement floors
  // (>= 8 functions, >= 56 loops) so the path is exercised, and enough loops
  // that the work estimate asks for several partitions (a multi-partition merge
  // is what could expose a thread-order dependency).
  constexpr int NFiles = 16, NFuncsPerFile = 8;
  auto srcDir = tmpFile("det_src");
  fs::create_directories(srcDir);

  std::vector<std::string> names;
  for (int fi = 0; fi < NFiles; ++fi) {
    std::string src = "#include <stdint.h>\n";
    for (int fj = 0; fj < NFuncsPerFile; ++fj) {
      std::string nm = "fn_" + std::to_string(fi) + "_" + std::to_string(fj);
      names.push_back(nm);
      unsigned c1 = (2654435761u * unsigned(fi * 131 + fj + 1)) | 1u;
      unsigned c2 =
          (40503u * unsigned(fi + 7) + 2246822519u * unsigned(fj + 3)) | 1u;
      src += "uint64_t " + nm + "(uint64_t x){ uint64_t a=x^" +
             std::to_string(c1) + "ULL; for(int i=0;i<7;i++){ a=a*" +
             std::to_string(c2) + "ULL+(a>>13)+i; } return a; }\n";
    }
    writeFile(srcDir / ("m" + std::to_string(fi) + ".c"), src);
  }
  {
    std::string m = "#include <stdint.h>\n#include <stdio.h>\n";
    for (auto &n : names)
      m += "uint64_t " + n + "(uint64_t);\n";
    m += "int main(void){ uint64_t acc=1;\n";
    for (auto &n : names)
      m += "  acc=acc*1000003ULL+" + n + "(acc);\n";
    m += " printf(\"CK=%llu\\n\",(unsigned long long)acc); return 0; }\n";
    writeFile(srcDir / "main.c", m);
  }

  std::vector<std::string> base = {"-std=c11"};
  for (auto &f : sysrootFlags())
    base.push_back(f);
  for (auto &f : archFlags())
    base.push_back(f);

  std::vector<std::string> objs;
  auto compileUnit = [&](const std::string &stem) {
    auto c = base;
    auto o = (srcDir / (stem + ".o")).string();
    c.insert(c.end(), {"-c", (srcDir / (stem + ".c")).string(), "-o", o});
    ASSERT_EQ(ncc(c).exitCode, 0) << stem;
    objs.push_back(o);
  };
  for (int fi = 0; fi < NFiles; ++fi)
    compileUnit("m" + std::to_string(fi));
  compileUnit("main");

  // Relocatable (`-r`) merge under a given worker-thread count.
  auto mergeWithThreads = [&](const char *threads) -> std::string {
    ScopedEnvVar WorkerThreads("NEVERC_PCG_THREADS", threads);
    std::vector<std::string> l;
    for (auto &f : sysrootFlags())
      l.push_back(f);
    for (auto &f : archFlags())
      l.push_back(f);
    // COFF stamps the PE header with the wall-clock second by default
    // (incremental-linker compatibility); the 1-, 4- and 16-thread merges run
    // seconds apart, so that timestamp byte alone differs and masquerades as a
    // parallelism leak.  Request reproducible output (timestamp = content hash)
    // so this comparison only measures codegen determinism, matching the
    // LtoLinkCache test above.
    if (isWindows())
      l.push_back("-mno-incremental-linker-compatible");
    l.push_back("-r");
    for (auto &o : objs)
      l.push_back(o);
    auto out = tmpFile(std::string("det_merge_") + threads + ".o");
    l.insert(l.end(), {"-o", out.string()});
    auto r = ncc(l);
    EXPECT_EQ(r.exitCode, 0) << "threads=" << threads << ": " << r.err;
    return readFile(out);
  };

  std::string o1 = mergeWithThreads("1");
  std::string o4 = mergeWithThreads("4");
  std::string o16 = mergeWithThreads("16");

  ASSERT_FALSE(o1.empty()) << "relocatable merge produced no object";
  EXPECT_EQ(o1, o4) << "auto-LTO object differs between 1 and 4 worker threads "
                       "-- execution parallelism leaked into the emitted bytes "
                       "(non-reproducible build)";
  EXPECT_EQ(o1, o16)
      << "auto-LTO object differs between 1 and 16 worker threads "
         "-- execution parallelism leaked into the emitted bytes";
}

// The auto-LTO loop-density inline cap (Inliner.cpp's
// NevercInlineMaxCallerLoops) withdraws *cost-driven* inlining of loop-bearing
// callees into an already-loop-dense caller; it must never change program
// semantics.  main() calls every leaf exactly once, so last-call-to-static
// inlining would otherwise fold them all into main -- the very shape the cap
// targets.  Build the same program twice, once with the cap at a deliberately
// low value (so it engages hard) and once disabled, and require byte-identical
// program *output*: the cap may only trade code shape / compile time, never
// results.
TEST_F(LTOTest, AutoLtoInlineCapSemanticsPreserved) {
  ScopedEnvVar NoLtoCache(linker::ltoCacheEnvVar, linker::ltoCacheDisableValue);
  ScopedEnvVar NoPartitionCache(linker::ltoPartitionCacheEnvVar,
                                linker::ltoCacheDisableValue);

  constexpr int NFiles = 10, NFuncsPerFile = 8; // 80 single-call leaves
  auto srcDir = tmpFile("inlinecap_src");
  fs::create_directories(srcDir);

  std::vector<std::string> names;
  for (int fi = 0; fi < NFiles; ++fi) {
    std::string src = "#include <stdint.h>\n";
    for (int fj = 0; fj < NFuncsPerFile; ++fj) {
      std::string nm = "lf_" + std::to_string(fi) + "_" + std::to_string(fj);
      names.push_back(nm);
      unsigned c1 = (2246822519u * unsigned(fi * 71 + fj + 1)) | 1u;
      unsigned c2 =
          (3266489917u * unsigned(fi + 5) + 668265263u * unsigned(fj + 2)) | 1u;
      // A constant-trip loop with a data-dependent branch: a loop-bearing leaf
      // the cap can choose to hold back from main.
      src += "uint64_t " + nm + "(uint64_t x){ uint64_t a=x^" +
             std::to_string(c1) + "ULL; for(int i=0;i<9;i++){ a=a*" +
             std::to_string(c2) +
             "ULL+(a>>11)+i; if(a&2) a+=" + std::to_string(c1) +
             "ULL; } return a; }\n";
    }
    writeFile(srcDir / ("m" + std::to_string(fi) + ".c"), src);
  }
  {
    std::string m = "#include <stdint.h>\n#include <stdio.h>\n";
    for (auto &n : names)
      m += "uint64_t " + n + "(uint64_t);\n";
    m += "int main(void){ uint64_t acc=1;\n for(int r=0;r<2;r++){\n";
    for (auto &n : names)
      m += "  acc=acc*1000003ULL+" + n + "(acc);\n";
    m += " }\n printf(\"CK=%llu\\n\",(unsigned long long)acc); return 0; }\n";
    writeFile(srcDir / "main.c", m);
  }

  std::vector<std::string> base = {"-std=c11"};
  for (auto &f : sysrootFlags())
    base.push_back(f);
  for (auto &f : archFlags())
    base.push_back(f);

  std::vector<std::string> objs;
  auto compileUnit = [&](const std::string &stem) {
    auto c = base;
    auto o = (srcDir / (stem + ".o")).string();
    c.insert(c.end(), {"-c", (srcDir / (stem + ".c")).string(), "-o", o});
    ASSERT_EQ(ncc(c).exitCode, 0) << stem;
    objs.push_back(o);
  };
  for (int fi = 0; fi < NFiles; ++fi)
    compileUnit("m" + std::to_string(fi));
  compileUnit("main");

  auto linkExe = [&](const std::string &exe, int capValue) {
    std::vector<std::string> l;
    for (auto &f : sysrootFlags())
      l.push_back(f);
    for (auto &f : archFlags())
      l.push_back(f);
    for (auto &o : objs)
      l.push_back(o);
    l.push_back("-mllvm");
    l.push_back("-neverc-inline-max-caller-loops=" + std::to_string(capValue));
    if (isWindows())
      l.push_back("-mno-incremental-linker-compatible");
    l.insert(l.end(), {"-o", exe});
    return ncc(l);
  };

  auto exeCap = tmpFile("inlinecap_on");
  ASSERT_EQ(linkExe(exeCap.string(), /*capValue=*/4).exitCode, 0);
  auto exeNoCap = tmpFile("inlinecap_off");
  ASSERT_EQ(linkExe(exeNoCap.string(), /*capValue=*/0).exitCode, 0);

  auto outCap = exec(exeCap.string(), {});
  auto outNoCap = exec(exeNoCap.string(), {});
  EXPECT_EQ(outCap.exitCode, 0) << outCap.err;
  EXPECT_EQ(outNoCap.exitCode, 0) << outNoCap.err;
  EXPECT_TRUE(outCap.contains("CK=")) << outCap.out;
  EXPECT_EQ(outCap.out, outNoCap.out)
      << "the loop-density inline cap changed program output -- it must be "
         "purely an optimization withdrawal, never a semantic change";
}

// The auto-LTO SCEV huge-expression bound (ParallelCodeGenMerge's
// PcgScevHugeExprThreshold, which lowers ScalarEvolution's HugeExprThreshold
// for the per-partition optimization) must be a pure compile-cost knob: making
// SCEV fall back to its conservative *unsimplified* form on oversized
// expressions -- exactly what the MaxArithDepth check beside it already does --
// can change code shape and compile time, never the computed result.  Build the
// same program with the bound set deliberately tiny (so it fires on essentially
// every expression the whole-program functions produce) and with it disabled
// (stock ScalarEvolution), and require byte-identical program output.  This is
// a timing -free invariant, so it can never flake.
TEST_F(LTOTest, AutoLtoScevHugeThresholdSemanticsPreserved) {
  ScopedEnvVar NoLtoCache(linker::ltoCacheEnvVar, linker::ltoCacheDisableValue);
  ScopedEnvVar NoPartitionCache(linker::ltoPartitionCacheEnvVar,
                                linker::ltoCacheDisableValue);

  constexpr int NFiles = 10, NFuncsPerFile = 8; // 80 loop+select leaves
  auto srcDir = tmpFile("scevsem_src");
  fs::create_directories(srcDir);

  std::vector<std::string> names;
  for (int fi = 0; fi < NFiles; ++fi) {
    std::string src = "#include <stdint.h>\n";
    for (int fj = 0; fj < NFuncsPerFile; ++fj) {
      std::string nm = "sf_" + std::to_string(fi) + "_" + std::to_string(fj);
      names.push_back(nm);
      unsigned c1 = (2246822519u * unsigned(fi * 71 + fj + 1)) | 1u;
      unsigned c2 =
          (3266489917u * unsigned(fi + 5) + 668265263u * unsigned(fj + 2)) | 1u;
      // A constant-trip loop with a data-dependent branch: inlined into main it
      // helps build the large SCEV expressions the bound targets.
      src += "uint64_t " + nm + "(uint64_t x){ uint64_t a=x^" +
             std::to_string(c1) + "ULL; for(int i=0;i<9;i++){ a=a*" +
             std::to_string(c2) +
             "ULL+(a>>11)+i; if(a&2) a+=" + std::to_string(c1) +
             "ULL; } return a; }\n";
    }
    writeFile(srcDir / ("m" + std::to_string(fi) + ".c"), src);
  }
  {
    std::string m = "#include <stdint.h>\n#include <stdio.h>\n";
    for (auto &n : names)
      m += "uint64_t " + n + "(uint64_t);\n";
    m += "int main(void){ uint64_t acc=1;\n for(int r=0;r<2;r++){\n";
    for (auto &n : names)
      m += "  acc=acc*1000003ULL+" + n + "(acc);\n";
    m += " }\n printf(\"CK=%llu\\n\",(unsigned long long)acc); return 0; }\n";
    writeFile(srcDir / "main.c", m);
  }

  std::vector<std::string> base = {"-std=c11"};
  for (auto &f : sysrootFlags())
    base.push_back(f);
  for (auto &f : archFlags())
    base.push_back(f);

  std::vector<std::string> objs;
  auto compileUnit = [&](const std::string &stem) {
    auto c = base;
    auto o = (srcDir / (stem + ".o")).string();
    c.insert(c.end(), {"-c", (srcDir / (stem + ".c")).string(), "-o", o});
    ASSERT_EQ(ncc(c).exitCode, 0) << stem;
    objs.push_back(o);
  };
  for (int fi = 0; fi < NFiles; ++fi)
    compileUnit("m" + std::to_string(fi));
  compileUnit("main");

  auto linkExe = [&](const std::string &exe, unsigned scevThreshold) {
    std::vector<std::string> l;
    for (auto &f : sysrootFlags())
      l.push_back(f);
    for (auto &f : archFlags())
      l.push_back(f);
    for (auto &o : objs)
      l.push_back(o);
    l.push_back("-mllvm");
    l.push_back("-neverc-auto-lto-scev-huge-expr-threshold=" +
                std::to_string(scevThreshold));
    if (isWindows())
      l.push_back("-mno-incremental-linker-compatible");
    l.insert(l.end(), {"-o", exe});
    return ncc(l);
  };

  // Tiny bound (4): SCEV gives up simplifying almost immediately.
  auto exeTiny = tmpFile("scevsem_tiny");
  ASSERT_EQ(linkExe(exeTiny.string(), /*scevThreshold=*/4).exitCode, 0);
  // Disabled (0): ScalarEvolution's stock threshold.
  auto exeOff = tmpFile("scevsem_off");
  ASSERT_EQ(linkExe(exeOff.string(), /*scevThreshold=*/0).exitCode, 0);

  auto outTiny = exec(exeTiny.string(), {});
  auto outOff = exec(exeOff.string(), {});
  EXPECT_EQ(outTiny.exitCode, 0) << outTiny.err;
  EXPECT_EQ(outOff.exitCode, 0) << outOff.err;
  EXPECT_TRUE(outTiny.contains("CK=")) << outTiny.out;
  EXPECT_EQ(outTiny.out, outOff.out)
      << "the auto-LTO SCEV huge-expression bound changed program output -- it "
         "must only withdraw simplification, never change a result";
}

// Real auto-LTO + mergeSections E2E: compile the in-tree Android kernel
// multifile example (per-function .text.* sections folded into .text) and
// assert every exported function lands at a distinct, non-zero offset.  This is
// the exact shape that bit us when PartOffsets lookup collapsed every symbol to
// 0 — syntactic mergeTests cover the math, but only a neverc-emitted .ko
// exercises the full IPO → parallel-codegen → mergeSections → verify chain on
// real codegen.
TEST_F(LTOTest, AndroidKernelMultifileMergeSectionOffsets) {
  auto exDir =
      fs::canonical(testDir() / "../../examples/android-kernel-multifile");
  if (!fs::exists(exDir / "main.c"))
    GTEST_SKIP() << "android-kernel-multifile example not found";

  std::string llvmNm = "llvm-nm";
  if (exec("which", {"llvm-nm"}).exitCode != 0) {
    if (exec("/opt/homebrew/opt/llvm/bin/llvm-nm", {"--version"}).exitCode == 0)
      llvmNm = "/opt/homebrew/opt/llvm/bin/llvm-nm";
    else if (exec("/opt/homebrew/opt/llvm@22/bin/llvm-nm", {"--version"})
                 .exitCode == 0)
      llvmNm = "/opt/homebrew/opt/llvm@22/bin/llvm-nm";
    else
      GTEST_SKIP() << "llvm-nm not available";
  }

  auto ko = tmpFile("nvk_multi.ko");
  std::vector<std::string> args = {
      "--target=aarch64-linux-android",
      "-fandroid-kernel-driver-mode",
      "-DNVK_KERNEL=510",
      "-r",
      "-nostdlib",
      "-o",
      ko.string(),
      (exDir / "main.c").string(),
      (exDir / "interposes.c").string(),
      (exDir / "utils.c").string(),
  };
  auto link = ncc(args);
  ASSERT_EQ(link.exitCode, 0) << link.err;

  auto symtab = exec(llvmNm, {ko.string()});
  ASSERT_EQ(symtab.exitCode, 0) << symtab.err;

  auto parseOffset = [&](const char *Name) -> uint64_t {
    std::string needle = std::string(" ") + Name;
    std::istringstream in(symtab.out);
    std::string line;
    while (std::getline(in, line)) {
      if (line.find(needle) == std::string::npos)
        continue;
      uint64_t off = std::strtoull(line.c_str(), nullptr, 16);
      if (off != 0 || line[0] == '0')
        return off;
    }
    ADD_FAILURE() << "symbol not found: " << Name;
    return 0;
  };

  uint64_t interposesInit = parseOffset("interposes_init");
  uint64_t interposesCleanup = parseOffset("interposes_cleanup");
  uint64_t initMod = parseOffset("init_module");
  uint64_t cleanupMod = parseOffset("cleanup_module");
  ASSERT_NE(interposesInit, 0u)
      << "interposes_init collapsed to .text+0 (SecOff regression)";
  ASSERT_NE(interposesCleanup, 0u);
  ASSERT_NE(initMod, 0u);
  ASSERT_NE(cleanupMod, 0u);
  EXPECT_NE(interposesInit, interposesCleanup);
  EXPECT_NE(interposesInit, initMod);
  EXPECT_NE(initMod, cleanupMod);
}

// Android GKI 6.1+ calls module entries through KCFI-checked function
// pointers. The compiler must place the profile's 32-bit type ID immediately
// before each exported entry symbol; a zero prefix reproduces the load-time
// KCFI failure this test guards against.
TEST_F(LTOTest, AndroidKernelProfilesEmitKcfiEntryTypeIds) {
  auto Source = fs::canonical(
      testDir() / "../../runtime/android/kernel/tools/gki-qemu-smoke-module.c");

  struct Profile {
    const char *Name;
    uint32_t InitTypeId;
    uint32_t ExitTypeId;
  };
  const Profile Profiles[] = {
      {"510", 0x00000000, 0x00000000}, {"51013", 0x00000000, 0x00000000},
      {"515", 0x00000000, 0x00000000}, {"51514", 0x00000000, 0x00000000},
      {"601", 0x36b1c5a6, 0xa540670c}, {"606", 0x36b1c5a6, 0xa540670c},
      {"612", 0x6fbb3035, 0xe5c47d60}, {"618", 0x6fbb3035, 0xe5c47d60},
  };

  for (const Profile &P : Profiles) {
    SCOPED_TRACE(P.Name);
    auto Ko = tmpFile(std::string("nvk_kcfi_") + P.Name + ".ko");
    auto Build = ncc({
        "--target=aarch64-linux-android",
        "-fandroid-kernel-driver-mode",
        std::string("-DNVK_KERNEL=") + P.Name,
        "-r",
        "-nostdlib",
        "-o",
        Ko.string(),
        Source.string(),
    });
    ASSERT_EQ(Build.exitCode, 0) << Build.err;

    const std::string Bytes = readFile(Ko);
    auto Init = readELFSymbolPrefix32(Bytes, "init_module");
    ASSERT_TRUE(static_cast<bool>(Init))
        << llvm::toString(Init.takeError()).str().str();
    EXPECT_EQ(*Init, P.InitTypeId);

    auto Exit = readELFSymbolPrefix32(Bytes, "cleanup_module");
    ASSERT_TRUE(static_cast<bool>(Exit))
        << llvm::toString(Exit.takeError()).str().str();
    EXPECT_EQ(*Exit, P.ExitTypeId);
  }
}

// KCFI protects every externally visible function definition and every
// address-taken local definition, not only init_module/cleanup_module. Clang
// deliberately drops the metadata from a local function that is called only
// directly. Keep every test function in its own non-.text.* section so its
// section-relative symbol offset says unambiguously whether a 4-byte prefix
// was emitted: 0 means no prefix and 4 means one exact type ID precedes it.
TEST_F(LTOTest, AndroidKernelProfilesEmitKcfiPrefixesForAllFunctions) {
  auto Source = tmpFile("android_kernel_kcfi_all_functions.c");
  writeFile(Source, R"c(
typedef long ssize_t;
typedef unsigned long size_t;
typedef long long loff_t;
typedef unsigned long long u64;
typedef int kcfi_array3[3];

struct file { int unused; };
struct notifier_block { int unused; };
struct kprobe { int unused; };
struct pt_regs { int unused; };
struct dir_context { int unused; };

__attribute__((noinline, section(".kcfi_test.int_void")))
int kcfi_int_void(void) { return 1; }

__attribute__((noinline, section(".kcfi_test.void_void")))
void kcfi_void_void(void) {}

__attribute__((noinline, section(".kcfi_test.fops_read")))
ssize_t kcfi_fops_read(struct file *file, char *buf, size_t len,
                       loff_t *pos) {
  return (ssize_t)(file != (void *)0) + (ssize_t)(buf != (void *)0) +
         (ssize_t)len + (ssize_t)(pos != (void *)0);
}

__attribute__((noinline, section(".kcfi_test.notifier")))
int kcfi_notifier(struct notifier_block *nb, unsigned long action,
                  void *data) {
  return (int)(nb != (void *)0) + (int)action + (int)(data != (void *)0);
}

__attribute__((noinline, section(".kcfi_test.kprobe")))
int kcfi_kprobe(struct kprobe *kp, struct pt_regs *regs) {
  return (int)(kp != (void *)0) + (int)(regs != (void *)0);
}

// In the normalized spelling, _Bool and Android's unsigned plain char share
// u2u8, so const char * uses the Itanium substitution spelling PKS_.
__attribute__((noinline, section(".kcfi_test.qualifier_substitution")))
_Bool kcfi_qualifier_substitution(struct dir_context *ctx, const char *name,
                                  int namelen, loff_t offset, u64 ino,
                                  unsigned int dtype) {
  return ctx != (void *)0 && name != (void *)0 && namelen != 0 &&
         offset != 0 && ino != 0 && dtype != 0;
}

// Itanium mangling pushes an array's const qualifier down to its element
// type: _ZTSFvPA3_KiE, not _ZTSFvPKA3_iE.
__attribute__((noinline, section(".kcfi_test.qualified_array")))
void kcfi_qualified_array(const kcfi_array3 *array) { (void)array; }

__attribute__((noinline, section(".kcfi_test.vla")))
void kcfi_vla(int n, int (*array)[n]) { (void)array; }

__attribute__((noinline, section(".kcfi_test.noescape")))
void kcfi_noescape(void *pointer __attribute__((noescape))) {
  (void)pointer;
}

__attribute__((noinline, ms_abi, section(".kcfi_test.noproto")))
void kcfi_noproto() {}

static __attribute__((noinline, section(".kcfi_test.static_taken")))
int kcfi_static_taken(void) { return 2; }
int (*kcfi_static_taken_slot)(void) = kcfi_static_taken;

volatile int kcfi_direct_seed;
static __attribute__((noinline, section(".kcfi_test.static_direct_only")))
int kcfi_static_direct_only(void) { return kcfi_direct_seed; }

__attribute__((noinline, section(".kcfi_test.call_direct_only")))
int kcfi_call_direct_only(void) { return kcfi_static_direct_only(); }
)c");

  struct Profile {
    const char *Name;
    bool HasKCFI;
    uint32_t IntVoid;
    uint32_t VoidVoid;
    uint32_t FopsRead;
    uint32_t Notifier;
    uint32_t Kprobe;
    uint32_t QualifierSubstitution;
    uint32_t QualifiedArray;
    uint32_t VLA;
    uint32_t NoEscape;
    uint32_t NoProto;
  };
  const Profile Profiles[] = {
      // Android's Linux 5.x kernel profiles predate KCFI, so these functions
      // begin at section+0.
      {"510", false, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
      // `_ZTSF...E`, XXH64(seed=0), low 32 bits.
      {"601", true, 0x36b1c5a6, 0xa540670c, 0xe866e2f4, 0x2a4cec24, 0xa6be5dd9,
       0xb1edbef0, 0xc70e1b41, 0xefb34d3c, 0x0b3d818b, 0xbcf98444},
      // Same canonical names with integer normalization and `.normalized`.
      {"612", true, 0x6fbb3035, 0xe5c47d60, 0xf4e9d97c, 0xd5127a3b, 0xc073bc77,
       0x4737d63b, 0x6d6ad630, 0xbd6434cc, 0x931474c0, 0x521bb1c4},
  };

  struct Mode {
    const char *Name;
    const char *Flag;
  };
  const Mode Modes[] = {
      {"no_lto", "-fno-lto"},
      {"default_lto", nullptr},
      {"full_lto", "-flto=full"},
  };

  for (const Profile &P : Profiles) {
    for (const Mode &M : Modes) {
      SCOPED_TRACE(std::string(P.Name) + "/" + M.Name);
      auto Object =
          tmpFile(std::string("nvk_kcfi_all_") + P.Name + "_" + M.Name + ".ko");
      std::vector<std::string> Args = {
          "--target=aarch64-linux-android",
          "-std=c11",
          "-fandroid-kernel-driver-mode",
          std::string("-DNVK_KERNEL=") + P.Name,
      };
      if (M.Flag)
        Args.push_back(M.Flag);
      Args.insert(Args.end(),
                  {"-r", "-nostdlib", "-o", Object.string(), Source.string()});
      auto Build = ncc(Args);
      ASSERT_EQ(Build.exitCode, 0) << Build.err;

      const std::string Bytes = readFile(Object);
      auto ExpectDefinition = [&](const char *Name, uint32_t TypeId) {
        auto Offset = readELFSymbolSectionOffset(Bytes, Name);
        ASSERT_TRUE(static_cast<bool>(Offset))
            << llvm::toString(Offset.takeError()).str().str();
        EXPECT_EQ(*Offset, P.HasKCFI ? 4u : 0u) << Name;
        if (!P.HasKCFI)
          return;

        auto Prefix = readELFSymbolPrefix32(Bytes, Name);
        ASSERT_TRUE(static_cast<bool>(Prefix))
            << llvm::toString(Prefix.takeError()).str().str();
        EXPECT_EQ(*Prefix, TypeId) << Name;
      };

      ExpectDefinition("kcfi_int_void", P.IntVoid);
      ExpectDefinition("kcfi_void_void", P.VoidVoid);
      ExpectDefinition("kcfi_fops_read", P.FopsRead);
      ExpectDefinition("kcfi_notifier", P.Notifier);
      ExpectDefinition("kcfi_kprobe", P.Kprobe);
      ExpectDefinition("kcfi_qualifier_substitution", P.QualifierSubstitution);
      ExpectDefinition("kcfi_qualified_array", P.QualifiedArray);
      ExpectDefinition("kcfi_vla", P.VLA);
      ExpectDefinition("kcfi_noescape", P.NoEscape);
      ExpectDefinition("kcfi_noproto", P.NoProto);
      ExpectDefinition("kcfi_static_taken", P.IntVoid);
      ExpectDefinition("kcfi_call_direct_only", P.IntVoid);

      // Upstream Clang removes !kcfi_type from local definitions whose
      // address is never taken. The volatile load and noinline call preserve
      // this function through both LTO pipelines without making it
      // address-taken.
      auto DirectOnly =
          readELFSymbolSectionOffset(Bytes, "kcfi_static_direct_only");
      ASSERT_TRUE(static_cast<bool>(DirectOnly))
          << llvm::toString(DirectOnly.takeError()).str().str();
      EXPECT_EQ(*DirectOnly, 0u);
    }
  }
}

// Parallel full-LTO promotes static functions to hidden external symbols and
// divides their use-lists between partitions. KCFI must still use the facts
// from before that transformation: an address-taken local keeps its prefix,
// while a direct-only local remains prefix-free.
TEST_F(LTOTest, AndroidKernelKcfiSurvivesParallelFullLtoPartitions) {
  auto Source = tmpFile("android_kernel_kcfi_pcg.c");
  writeFile(Source, R"c(
volatile int kcfi_pcg_seed;

static __attribute__((noinline, section(".kcfi_test.pcg_taken")))
int kcfi_pcg_taken(void) { return kcfi_pcg_seed + 1; }
int (*kcfi_pcg_slot)(void) = kcfi_pcg_taken;

static __attribute__((noinline, section(".kcfi_test.pcg_direct")))
int kcfi_pcg_direct(void) { return kcfi_pcg_seed + 2; }
int kcfi_pcg_call_direct(void) { return kcfi_pcg_direct(); }

#define KCFI_PCG_HELPER(N)                                                    \
  __attribute__((noinline)) int kcfi_pcg_helper_##N(int value) {             \
    return value + N + kcfi_pcg_seed;                                        \
  }
KCFI_PCG_HELPER(0)
KCFI_PCG_HELPER(1)
KCFI_PCG_HELPER(2)
KCFI_PCG_HELPER(3)
KCFI_PCG_HELPER(4)
KCFI_PCG_HELPER(5)
KCFI_PCG_HELPER(6)
KCFI_PCG_HELPER(7)
)c");

  ScopedEnvVar Strict("NEVERC_PCG_STRICT", "1");
  ScopedEnvVar Debug("NEVERC_PCG_DEBUG", "1");
  struct Pipeline {
    const char *Name;
    const char *LTOFlag;
  };
  const Pipeline Pipelines[] = {
      {"auto", nullptr},
      {"explicit_full", "-flto=full"},
  };
  for (const Pipeline &P : Pipelines) {
    SCOPED_TRACE(P.Name);
    auto Object =
        tmpFile(std::string("android_kernel_kcfi_pcg_") + P.Name + ".ko");
    std::vector<std::string> Args = {
        "--target=aarch64-linux-android", "-std=c11",         "-O2",
        "-fandroid-kernel-driver-mode",   "-DNVK_KERNEL=612",
    };
    if (P.LTOFlag)
      Args.push_back(P.LTOFlag);
    Args.insert(Args.end(), {
                                "-mllvm",
                                "-neverc-pcg-min-funcs=2",
                                "-mllvm",
                                "-neverc-pcg-weight-floor=0",
                                "-mllvm",
                                "-neverc-pcg-opt-weight-div=1",
                                "-r",
                                "-nostdlib",
                                "-o",
                                Object.string(),
                                Source.string(),
                            });
    auto Build = ncc(Args);
    ASSERT_EQ(Build.exitCode, 0) << Build.err;
    ASSERT_TRUE(Build.stderrContains("[pcg] SUCCESS"))
        << "test did not exercise parallel codegen in " << P.Name << ":\n"
        << Build.err;

    const std::string Bytes = readFile(Object);
    auto TakenOffset =
        readELFSymbolSectionOffset(Bytes, "kcfi_pcg_taken", true);
    ASSERT_TRUE(static_cast<bool>(TakenOffset))
        << llvm::toString(TakenOffset.takeError()).str().str();
    EXPECT_EQ(*TakenOffset, 4u);

    auto TakenPrefix = readELFSymbolPrefix32(Bytes, "kcfi_pcg_taken", true);
    ASSERT_TRUE(static_cast<bool>(TakenPrefix))
        << llvm::toString(TakenPrefix.takeError()).str().str();
    EXPECT_EQ(*TakenPrefix, 0x6fbb3035u);

    auto DirectOffset =
        readELFSymbolSectionOffset(Bytes, "kcfi_pcg_direct", true);
    ASSERT_TRUE(static_cast<bool>(DirectOffset))
        << llvm::toString(DirectOffset.takeError()).str().str();
    EXPECT_EQ(*DirectOffset, 0u);
  }
}

// Release finalization runs after PCG's temporary hidden externals have been
// merged and demoted back to local symbols. Reuse the proven KCFI/PCG workload
// above so this test audits that exact boundary instead of relying on a
// heuristic single-function fixture that may legitimately decline PCG.
TEST_F(LTOTest, AndroidKernelReleaseStripPreservesPcgDemotionAcrossLtoModes) {
  auto Source = tmpFile("android_kernel_release_pcg_demotion.c");
  writeFile(Source, R"c(
volatile int kcfi_pcg_seed;

static __attribute__((noinline, section(".kcfi_test.pcg_taken")))
int kcfi_pcg_taken(void) { return kcfi_pcg_seed + 1; }
int (*kcfi_pcg_slot)(void) = kcfi_pcg_taken;

static __attribute__((noinline, section(".kcfi_test.pcg_direct")))
int kcfi_pcg_direct(void) { return kcfi_pcg_seed + 2; }
int kcfi_pcg_call_direct(void) { return kcfi_pcg_direct(); }

#define KCFI_PCG_HELPER(N)                                                    \
  __attribute__((noinline)) int kcfi_pcg_helper_##N(int value) {             \
    return value + N + kcfi_pcg_seed;                                        \
  }
KCFI_PCG_HELPER(0)
KCFI_PCG_HELPER(1)
KCFI_PCG_HELPER(2)
KCFI_PCG_HELPER(3)
KCFI_PCG_HELPER(4)
KCFI_PCG_HELPER(5)
KCFI_PCG_HELPER(6)
KCFI_PCG_HELPER(7)
)c");

  ScopedEnvVar NoLtoCache(linker::ltoCacheEnvVar, linker::ltoCacheDisableValue);
  ScopedEnvVar NoPartitionCache(linker::ltoPartitionCacheEnvVar,
                                linker::ltoCacheDisableValue);
  ScopedEnvVar Strict("NEVERC_PCG_STRICT", "1");
  ScopedEnvVar DebugLog("NEVERC_PCG_DEBUG", "1");

  struct Pipeline {
    const char *Name;
    const char *LTOFlag;
  };
  const Pipeline Pipelines[] = {
      {"auto", nullptr},
      {"explicit_full", "-flto=full"},
  };

  for (const Pipeline &P : Pipelines) {
    SCOPED_TRACE(P.Name);
    auto DebugObject = tmpFile(
        std::string("android_kernel_release_pcg_debug_") + P.Name + ".ko");
    auto ReleaseObject = tmpFile(
        std::string("android_kernel_release_pcg_stripped_") + P.Name + ".ko");
    auto Build = [&](const fs::path &Output, bool Strip) {
      std::vector<std::string> Args = {
          "--target=aarch64-linux-android", "-std=c11",         "-O2",
          "-fandroid-kernel-driver-mode",   "-DNVK_KERNEL=612",
      };
      if (P.LTOFlag)
        Args.push_back(P.LTOFlag);
      if (Strip)
        Args.push_back("--strip");
      Args.insert(Args.end(), {
                                  "-mllvm",
                                  "-neverc-pcg-min-funcs=2",
                                  "-mllvm",
                                  "-neverc-pcg-weight-floor=0",
                                  "-mllvm",
                                  "-neverc-pcg-opt-weight-div=1",
                                  "-r",
                                  "-nostdlib",
                                  "-o",
                                  Output.string(),
                                  Source.string(),
                              });
      return ncc(Args);
    };

    auto DebugBuild = Build(DebugObject, false);
    ASSERT_EQ(DebugBuild.exitCode, 0) << DebugBuild.err;
    ASSERT_TRUE(DebugBuild.stderrContains("[pcg] SUCCESS")) << DebugBuild.err;
    auto Debug = inspectAndroidKernelReleaseMetadata(readFile(DebugObject));
    ASSERT_TRUE(static_cast<bool>(Debug))
        << llvm::toString(Debug.takeError()).str().str();

    std::vector<const AndroidKernelReleaseSymbol *> DemotedInputs;
    for (const AndroidKernelReleaseSymbol &Symbol : Debug->Symbols)
      if (llvm::StringRef(Symbol.Name).starts_with("kcfi_pcg_taken.__pcg"))
        DemotedInputs.push_back(&Symbol);
    ASSERT_EQ(DemotedInputs.size(), 1u);
    EXPECT_EQ(DemotedInputs.front()->Semantics.Binding, llvm::ELF::STB_LOCAL);
    EXPECT_GT(Debug->RelocationTargets.count(DemotedInputs.front()->Name), 0u);

    auto ReleaseBuild = Build(ReleaseObject, true);
    ASSERT_EQ(ReleaseBuild.exitCode, 0) << ReleaseBuild.err;
    ASSERT_TRUE(ReleaseBuild.stderrContains("[pcg] SUCCESS"))
        << ReleaseBuild.err;
    auto Release = inspectAndroidKernelReleaseMetadata(
        readFile(ReleaseObject), /*AuditCanonicalNames=*/true);
    ASSERT_TRUE(static_cast<bool>(Release))
        << llvm::toString(Release.takeError()).str().str();

    std::vector<const AndroidKernelReleaseSymbol *> FinalSymbols;
    for (const AndroidKernelReleaseSymbol &Symbol : Release->Symbols)
      if (Symbol.Semantics == DemotedInputs.front()->Semantics)
        FinalSymbols.push_back(&Symbol);
    ASSERT_EQ(FinalSymbols.size(), 1u);
    EXPECT_EQ(FinalSymbols.front()->Semantics.Binding, llvm::ELF::STB_LOCAL);
    EXPECT_LT(FinalSymbols.front()->Index, Release->SymtabInfo);
    auto ExpectedName =
        canonicalReleaseBaseName(*Release, *FinalSymbols.front());
    ASSERT_TRUE(static_cast<bool>(ExpectedName))
        << llvm::toString(ExpectedName.takeError()).str().str();
    EXPECT_EQ(FinalSymbols.front()->Name, *ExpectedName);
    EXPECT_TRUE(llvm::StringRef(FinalSymbols.front()->Name).starts_with("fn_"));
    EXPECT_FALSE(
        llvm::StringRef(FinalSymbols.front()->Name).contains(".__pcg"));
    EXPECT_GT(Release->RelocationTargets.count(FinalSymbols.front()->Name), 0u);
    EXPECT_FALSE(
        symbolStringTableContains(*Release, DemotedInputs.front()->Name));
  }
}

// An IR optimization provider may publish the input module while deliberately
// suppressing NeverC's builtin pipeline. KCFI preparation/finalization is a
// code-generation invariant and must still run in both native and full-LTO
// backends on that path.
TEST_F(LTOTest, AndroidKernelKcfiSurvivesOptimizationProviderBypass) {
  auto Source = tmpFile("android_kernel_kcfi_provider.c");
  writeFile(Source, R"c(
__attribute__((noinline, section(".kcfi_test.provider")))
int kcfi_provider_callback(void) { return 1; }
)c");

  struct Mode {
    const char *Name;
    const char *Flag;
  };
  const Mode Modes[] = {
      {"no_lto", "-fno-lto"},
      {"full_lto", "-flto=full"},
  };

  for (const Mode &M : Modes) {
    SCOPED_TRACE(M.Name);
    auto Object = tmpFile(std::string("nvk_kcfi_provider_") + M.Name + ".ko");
    auto Build = ncc({
        std::string("-fplugin=") +
            NEVERC_TEST_IR_OPTIMIZATION_PASSTHROUGH_PLUGIN,
        "--target=aarch64-linux-android",
        "-std=c11",
        "-fandroid-kernel-driver-mode",
        "-DNVK_KERNEL=612",
        M.Flag,
        "-r",
        "-nostdlib",
        "-o",
        Object.string(),
        Source.string(),
    });
    ASSERT_EQ(Build.exitCode, 0) << Build.err;

    const std::string Bytes = readFile(Object);
    auto Offset = readELFSymbolSectionOffset(Bytes, "kcfi_provider_callback");
    ASSERT_TRUE(static_cast<bool>(Offset))
        << llvm::toString(Offset.takeError()).str().str();
    EXPECT_EQ(*Offset, 4u);

    auto Prefix = readELFSymbolPrefix32(Bytes, "kcfi_provider_callback");
    ASSERT_TRUE(static_cast<bool>(Prefix))
        << llvm::toString(Prefix.takeError()).str().str();
    EXPECT_EQ(*Prefix, 0x6fbb3035u);
  }
}

// A provider that publishes the complete optimized module suppresses every
// builtin IR optimization stage, including the deferred per-partition stage.
// Parallel full-LTO may still split that final module for code generation, but
// it must use the codegen-only hook: rerunning the partition optimizer would
// violate the provider contract and would mutate functions after KCFI prefixes
// have already been sealed.
TEST_F(LTOTest, AndroidKernelKcfiProviderBypassUsesParallelCodegenOnly) {
  auto Source = tmpFile("android_kernel_kcfi_provider_pcg.c");
  auto Object = tmpFile("nvk_kcfi_provider_pcg.ko");
  writeFile(Source, R"c(
volatile int kcfi_provider_pcg_seed;

__attribute__((noinline, section(".kcfi_test.provider_pcg")))
int kcfi_provider_pcg_callback(void) { return kcfi_provider_pcg_seed; }

#define KCFI_PROVIDER_HELPER(N)                                               \
  __attribute__((noinline)) int kcfi_provider_helper_##N(int value) {         \
    return value + N + kcfi_provider_pcg_seed;                               \
  }
KCFI_PROVIDER_HELPER(0)
KCFI_PROVIDER_HELPER(1)
KCFI_PROVIDER_HELPER(2)
KCFI_PROVIDER_HELPER(3)
KCFI_PROVIDER_HELPER(4)
KCFI_PROVIDER_HELPER(5)
KCFI_PROVIDER_HELPER(6)
KCFI_PROVIDER_HELPER(7)
)c");

  ScopedEnvVar Strict("NEVERC_PCG_STRICT", "1");
  ScopedEnvVar Debug("NEVERC_PCG_DEBUG", "1");
  auto Build = ncc({
      std::string("-fplugin=") + NEVERC_TEST_IR_OPTIMIZATION_PASSTHROUGH_PLUGIN,
      "--target=aarch64-linux-android",
      "-std=c11",
      "-O2",
      "-fandroid-kernel-driver-mode",
      "-DNVK_KERNEL=612",
      "-flto=full",
      "-mllvm",
      "-neverc-pcg-min-funcs=2",
      "-mllvm",
      "-neverc-pcg-weight-floor=0",
      "-mllvm",
      "-neverc-pcg-opt-weight-div=1",
      "-r",
      "-nostdlib",
      "-o",
      Object.string(),
      Source.string(),
  });
  ASSERT_EQ(Build.exitCode, 0) << Build.err;
  ASSERT_TRUE(Build.stderrContains("[pcg] SUCCESS"))
      << "test did not exercise parallel full-LTO:\n"
      << Build.err;
  EXPECT_FALSE(Build.stderrContains("[pcg] p-opt engaged"))
      << "provider-owned optimization must use parallel codegen only:\n"
      << Build.err;

  const std::string Bytes = readFile(Object);
  auto Offset = readELFSymbolSectionOffset(Bytes, "kcfi_provider_pcg_callback");
  ASSERT_TRUE(static_cast<bool>(Offset))
      << llvm::toString(Offset.takeError()).str().str();
  EXPECT_EQ(*Offset, 4u);

  auto Prefix = readELFSymbolPrefix32(Bytes, "kcfi_provider_pcg_callback");
  ASSERT_TRUE(static_cast<bool>(Prefix))
      << llvm::toString(Prefix.takeError()).str().str();
  EXPECT_EQ(*Prefix, 0x6fbb3035u);
}

// Pre-link modules commonly contain a declaration in one TU and the matching
// definition in another. Their source type carriers must coalesce to one
// selected KCFI attachment when full LTO merges the modules.
TEST_F(LTOTest, AndroidKernelKcfiFullLtoCoalescesDeclarationMetadata) {
  auto Caller = tmpFile("android_kernel_kcfi_decl.c");
  auto Definition = tmpFile("android_kernel_kcfi_def.c");
  auto Object = tmpFile("android_kernel_kcfi_decl_def.ko");
  writeFile(Caller, R"c(
int kcfi_cross_tu_callback(void);
int kcfi_cross_tu_caller(void) { return kcfi_cross_tu_callback(); }
)c");
  writeFile(Definition, R"c(
__attribute__((noinline, section(".kcfi_test.cross_tu")))
int kcfi_cross_tu_callback(void) { return 7; }
)c");

  auto Build = ncc({
      "--target=aarch64-linux-android",
      "-std=c11",
      "-fandroid-kernel-driver-mode",
      "-DNVK_KERNEL=612",
      "-flto=full",
      "-r",
      "-nostdlib",
      "-o",
      Object.string(),
      Caller.string(),
      Definition.string(),
  });
  ASSERT_EQ(Build.exitCode, 0) << Build.err;

  const std::string Bytes = readFile(Object);
  auto Offset = readELFSymbolSectionOffset(Bytes, "kcfi_cross_tu_callback");
  ASSERT_TRUE(static_cast<bool>(Offset))
      << llvm::toString(Offset.takeError()).str().str();
  EXPECT_EQ(*Offset, 4u);

  auto Prefix = readELFSymbolPrefix32(Bytes, "kcfi_cross_tu_callback");
  ASSERT_TRUE(static_cast<bool>(Prefix))
      << llvm::toString(Prefix.takeError()).str().str();
  EXPECT_EQ(*Prefix, 0x6fbb3035u);
}

// A function alias shares its aliasee's machine-code prefix. Reject a source
// declaration whose KCFI type differs even when both declarations lower to
// the same LLVM function type; otherwise indirect calls through the alias
// would compare against a prefix that can never match.
TEST_F(LTOTest, AndroidKernelKcfiRejectsMismatchedFunctionAlias) {
  auto Source = tmpFile("android_kernel_kcfi_alias_mismatch.c");
  writeFile(Source, R"c(
enum kcfi_alias_argument { KCFI_ALIAS_ARGUMENT_ZERO };
int kcfi_alias_target(int value) { return value; }
extern int kcfi_alias(enum kcfi_alias_argument)
    __attribute__((alias("kcfi_alias_target")));
)c");

  auto Result = ncc({
      "--target=aarch64-linux-android",
      "-std=c11",
      "-fandroid-kernel-driver-mode",
      "-DNVK_KERNEL=601",
      "-fno-lto",
      "-c",
      "-o",
      tmpFile("nvk_kcfi_alias_mismatch.o").string(),
      Source.string(),
  });
  EXPECT_NE(Result.exitCode, 0);
  EXPECT_NE(
      Result.err.find("conflicting source-level Android kernel KCFI type IDs"),
      std::string::npos)
      << Result.err;
}

// The alias and its target share one prefix, but only the active profile's
// source type spelling matters. long and long long lower to the same AArch64
// LLVM type; classic KCFI distinguishes them while normalized KCFI does not.
// A non-KCFI profile must not inspect either dormant ID.
TEST_F(LTOTest, AndroidKernelAliasChecksOnlySelectedKcfiProfile) {
  auto Source = tmpFile("android_kernel_kcfi_alias_selected_mode.c");
  writeFile(Source, R"c(
long kcfi_alias_target(long value) { return value; }
extern long long kcfi_alias(long long)
    __attribute__((alias("kcfi_alias_target")));
)c");

  struct Profile {
    const char *Name;
    bool Accepted;
  };
  const Profile Profiles[] = {
      {"510", true},
      {"601", false},
      {"612", true},
  };
  for (const Profile &P : Profiles) {
    SCOPED_TRACE(P.Name);
    auto Result = ncc({
        "--target=aarch64-linux-android",
        "-std=c11",
        "-fandroid-kernel-driver-mode",
        std::string("-DNVK_KERNEL=") + P.Name,
        "-fno-lto",
        "-c",
        "-o",
        tmpFile(std::string("nvk_kcfi_alias_selected_") + P.Name + ".o")
            .string(),
        Source.string(),
    });
    if (P.Accepted) {
      EXPECT_EQ(Result.exitCode, 0) << Result.err;
    } else {
      EXPECT_NE(Result.exitCode, 0);
      EXPECT_NE(Result.err.find(
                    "conflicting source-level Android kernel KCFI type IDs"),
                std::string::npos)
          << Result.err;
    }
  }
}

// KCFI-disabled profiles must never ask the KCFI-only canonical mangler to
// describe a legal source type. This block-scope tag deliberately lies outside
// the supported KCFI subset but is valid for an ordinary 5.10 module.
TEST_F(LTOTest, AndroidKernelWithoutKcfiSkipsUnsupportedTypeMangling) {
  auto Source = tmpFile("android_kernel_no_kcfi_local_tag.c");
  writeFile(Source, R"c(
int kcfi_local_tag_user(void) {
  struct kcfi_local_tag { int value; };
  extern int kcfi_local_tag_callback(struct kcfi_local_tag *);
  return kcfi_local_tag_callback((struct kcfi_local_tag *)0);
}
)c");

  auto Result = ncc({
      "--target=aarch64-linux-android",
      "-std=c11",
      "-fandroid-kernel-driver-mode",
      "-DNVK_KERNEL=510",
      "-fno-lto",
      "-c",
      "-o",
      tmpFile("nvk_no_kcfi_local_tag.o").string(),
      Source.string(),
  });
  EXPECT_EQ(Result.exitCode, 0) << Result.err;
}

// The compiler-generated target_clones resolver has no source-level KCFI
// signature. Until its complete loader/call semantics are defined, reject the
// construct at the source location instead of failing later in the backend.
TEST_F(LTOTest, AndroidKernelKcfiRejectsFunctionMultiversioning) {
  auto Source = tmpFile("android_kernel_kcfi_target_clones.c");
  writeFile(Source, R"c(
#define NVK_KERNEL (612U)
#include <nvkmod_version.h>
__attribute__((target_clones("default", "crc")))
int kcfi_multiversion(int value) { return value + 1; }
)c");

  auto Result = ncc({
      "--target=aarch64-linux-android",
      "-std=c11",
      "-fandroid-kernel-driver-mode",
      "-fno-lto",
      "-c",
      "-o",
      tmpFile("nvk_kcfi_target_clones.o").string(),
      Source.string(),
  });
  EXPECT_NE(Result.exitCode, 0);
  EXPECT_NE(Result.err.find(
                "function multiversioning is not supported in Android kernel "
                "KCFI mode"),
            std::string::npos)
      << Result.err;
  EXPECT_EQ(Result.err.find("lacks a source-level KCFI type ID"),
            std::string::npos)
      << Result.err;
}

TEST_F(LTOTest, AndroidKernelWithoutKcfiAllowsFunctionMultiversioning) {
  auto Source = tmpFile("android_kernel_no_kcfi_target_clones.c");
  writeFile(Source, R"c(
__attribute__((target_clones("default", "crc")))
int no_kcfi_multiversion(int value) { return value + 1; }
)c");

  auto Result = ncc({
      "--target=aarch64-linux-android",
      "-std=c11",
      "-fandroid-kernel-driver-mode",
      "-DNVK_KERNEL=510",
      "-fno-lto",
      "-c",
      "-o",
      tmpFile("nvk_no_kcfi_target_clones.o").string(),
      Source.string(),
  });
  EXPECT_EQ(Result.exitCode, 0) << Result.err;
}

// The preprocessor can select a profile through source configuration or an
// integer spelling that no driver-side mini-parser should attempt to evaluate.
// The generated source marker is authoritative.
TEST_F(LTOTest, AndroidKernelSourceProfileMarkerSelectsKcfiMode) {
  auto Source = tmpFile("android_kernel_kcfi_source_profile.c");
  writeFile(Source, R"c(
#define NVK_KERNEL (612U)
#include <nvkmod_version.h>
__attribute__((__noinline__, section(".kcfi_test.source_profile")))
int kcfi_source_profile_callback(void) { return 1; }
)c");

  const char *Modes[] = {"-fno-lto", "-flto=full"};
  for (const char *Mode : Modes) {
    SCOPED_TRACE(Mode);
    auto Object =
        tmpFile(std::string("nvk_kcfi_source_profile_") +
                (std::string(Mode) == "-fno-lto" ? "none" : "full") + ".ko");
    auto Build = ncc({
        "--target=aarch64-linux-android",
        "-std=c11",
        "-fandroid-kernel-driver-mode",
        Mode,
        "-r",
        "-nostdlib",
        "-o",
        Object.string(),
        Source.string(),
    });
    ASSERT_EQ(Build.exitCode, 0) << Build.err;

    const std::string Bytes = readFile(Object);
    auto Prefix = readELFSymbolPrefix32(Bytes, "kcfi_source_profile_callback");
    ASSERT_TRUE(static_cast<bool>(Prefix))
        << llvm::toString(Prefix.takeError()).str().str();
    EXPECT_EQ(*Prefix, 0x6fbb3035u);
    EXPECT_EQ(Bytes.find("__neverc_krt_kcfi_mode_marker"), std::string::npos);
  }
}

// Profile spelling is C preprocessor syntax, not a driver mini-language.  The
// driver forces the generated marker policy and lets preprocessing resolve
// forms such as parenthesized integer constants with suffixes.
TEST_F(LTOTest, AndroidKernelImplicitProfileMarkerSelectsNormalizedKcfi) {
  auto Source = tmpFile("android_kernel_kcfi_implicit_profile.c");
  auto Object = tmpFile("android_kernel_kcfi_implicit_profile.o");
  writeFile(Source, R"c(
__attribute__((noinline, section(".kcfi_test.implicit_profile")))
int kcfi_implicit_profile_callback(void) { return 1; }
)c");

  auto Build = ncc({
      "--target=aarch64-linux-android",
      "-std=c11",
      "-fandroid-kernel-driver-mode",
      "-DNVK_KERNEL=(612U)",
      "-fno-lto",
      "-c",
      "-o",
      Object.string(),
      Source.string(),
  });
  ASSERT_EQ(Build.exitCode, 0) << Build.err;

  const std::string Bytes = readFile(Object);
  auto Prefix = readELFSymbolPrefix32(Bytes, "kcfi_implicit_profile_callback");
  ASSERT_TRUE(static_cast<bool>(Prefix))
      << llvm::toString(Prefix.takeError()).str().str();
  EXPECT_EQ(*Prefix, 0x6fbb3035u);
  EXPECT_EQ(Bytes.find("__neverc_krt_kcfi_mode_marker"), std::string::npos);
  EXPECT_EQ(Bytes.find("__neverc_krt_profile_marker"), std::string::npos);
  EXPECT_EQ(Bytes.find("__neverc_krt_scs_mode_marker"), std::string::npos);
}

TEST_F(LTOTest, AndroidKernelUserForcedConfigPrecedesProfileMarker) {
  auto Config = tmpFile("android_kernel_profile_config.h");
  auto Source = tmpFile("android_kernel_profile_config.c");
  auto Object = tmpFile("android_kernel_profile_config.o");
  writeFile(Config, "#define NVK_KERNEL (612U)\n");
  writeFile(Source, R"c(
__attribute__((noinline, section(".kcfi_test.forced_config")))
int kcfi_forced_config_callback(void) { return 1; }
)c");

  auto Build = ncc({
      "--target=aarch64-linux-android",
      "-std=c11",
      "-fandroid-kernel-driver-mode",
      "-include",
      Config.string(),
      "-fno-lto",
      "-c",
      "-o",
      Object.string(),
      Source.string(),
  });
  ASSERT_EQ(Build.exitCode, 0) << Build.err;

  const std::string Bytes = readFile(Object);
  auto Prefix = readELFSymbolPrefix32(Bytes, "kcfi_forced_config_callback");
  ASSERT_TRUE(static_cast<bool>(Prefix))
      << llvm::toString(Prefix.takeError()).str().str();
  EXPECT_EQ(*Prefix, 0x6fbb3035u);
  EXPECT_EQ(Bytes.find("__neverc_krt_kcfi_mode_marker"), std::string::npos);
  EXPECT_EQ(Bytes.find("__neverc_krt_profile_marker"), std::string::npos);
  EXPECT_EQ(Bytes.find("__neverc_krt_scs_mode_marker"), std::string::npos);
}

TEST_F(LTOTest, AndroidKernelTextualIROutputKeepsProfileContractPrintable) {
  auto Source = tmpFile("android_kernel_profile_textual_ir.c");
  auto Output = tmpFile("android_kernel_profile_textual_ir.ll");
  writeFile(Source, "int android_profile_textual_ir(void) { return 1; }\n");

  auto Result = ncc({
      "--target=aarch64-linux-android",
      "-std=c11",
      "-fandroid-kernel-driver-mode",
      "-DNVK_KERNEL=612",
      "-fno-lto",
      "-S",
      "-emit-llvm",
      "-o",
      Output.string(),
      Source.string(),
  });
  ASSERT_EQ(Result.exitCode, 0) << Result.err;

  const std::string IR = readFile(Output);
  EXPECT_NE(IR.find("neverc.android.kernel.profile"), std::string::npos);
  EXPECT_NE(IR.find(".neverc.android.kernel.profile"), std::string::npos);
  EXPECT_EQ(IR.find("shadowcallstack"), std::string::npos);
  EXPECT_NE(IR.find("\"branch-target-enforcement\"=\"true\""),
            std::string::npos);
  EXPECT_NE(IR.find("\"sign-return-address\"=\"all\""),
            std::string::npos);
  EXPECT_NE(IR.find("\"sign-return-address-key\"=\"a_key\""),
            std::string::npos);
  std::istringstream Lines(IR);
  for (std::string Line; std::getline(Lines, Line);)
    if (Line.rfind("attributes #", 0) == 0)
      EXPECT_EQ(Line.find("uwtable"), std::string::npos) << Line;
}

// Android kernel code must never acquire an FP/SIMD/SVE-capable per-function
// subtarget while IR is reconstructed or optimized.  A variadic 32-byte
// aggregate is the code shape that otherwise lets AArch64 lower the copy
// through Q registers, so assert the public compiler output keeps the
// general-register-only feature contract on the final function.
TEST_F(LTOTest, AndroidKernelDriverModeRetainsGeneralRegsOnlyFeatures) {
  auto Source = tmpFile("android_kernel_general_regs_only.c");
  auto Output = tmpFile("android_kernel_general_regs_only.ll");
  writeFile(Source, R"c(
#include <stdarg.h>

extern int neverc_krt_mem_init(void);

struct aggregate32 {
  unsigned long words[4];
};

__attribute__((noinline))
unsigned long android_kernel_general_regs_probe(int count, ...) {
  va_list args;
  struct aggregate32 value;
  va_start(args, count);
  value = va_arg(args, struct aggregate32);
  va_end(args);
  return value.words[0] + value.words[3] +
         (unsigned long)neverc_krt_mem_init();
}
)c");

  auto Result = ncc({
      "--target=aarch64-linux-android",
      "-std=c11",
      "-O2",
      "-fandroid-kernel-driver-mode",
      "-DNVK_KERNEL=612",
      "-fno-lto",
      "-S",
      "-emit-llvm",
      "-o",
      Output.string(),
      Source.string(),
  });
  ASSERT_EQ(Result.exitCode, 0) << Result.err;

  const std::string IR = readFile(Output);
  EXPECT_NE(IR.find("@android_kernel_general_regs_probe"), std::string::npos);
  EXPECT_NE(IR.find("\"target-features\"="), std::string::npos);
  EXPECT_NE(IR.find("-fp-armv8"), std::string::npos) << IR;
  EXPECT_NE(IR.find("-neon"), std::string::npos) << IR;
  EXPECT_NE(IR.find("-sve"), std::string::npos) << IR;
  EXPECT_NE(IR.find("-sve2"), std::string::npos) << IR;
  EXPECT_NE(IR.find("-sme"), std::string::npos) << IR;
  EXPECT_NE(IR.find("-sme2"), std::string::npos) << IR;
  EXPECT_EQ(IR.find("+fp-armv8"), std::string::npos) << IR;
  EXPECT_EQ(IR.find("+neon"), std::string::npos) << IR;
  EXPECT_EQ(IR.find("+sve"), std::string::npos) << IR;
  EXPECT_EQ(IR.find("+sve2"), std::string::npos) << IR;
  EXPECT_EQ(IR.find("+sme"), std::string::npos) << IR;
  EXPECT_EQ(IR.find("+sme2"), std::string::npos) << IR;

  // The runtime bitcode is linked before KernelFunctionAttrsPass.  Its target
  // attributes were stripped while retargeting, so the pass must materialize
  // the same contract on runtime definitions rather than relying on the
  // source TU's cc1 defaults.
  size_t RuntimeDef = std::string::npos;
  for (size_t Search = IR.find("define "); Search != std::string::npos;
       Search = IR.find("define ", Search + 1)) {
    const size_t LineEnd = IR.find('\n', Search);
    const size_t Name = IR.find("@neverc_krt_mem_init()", Search);
    if (Name != std::string::npos &&
        (LineEnd == std::string::npos || Name < LineEnd)) {
      RuntimeDef = Search;
      break;
    }
  }
  ASSERT_NE(RuntimeDef, std::string::npos) << IR;
  const size_t RuntimeDefEnd = IR.find('\n', RuntimeDef);
  const size_t RuntimeGroup = IR.find(" #", RuntimeDef);
  ASSERT_NE(RuntimeDefEnd, std::string::npos) << IR;
  ASSERT_NE(RuntimeGroup, std::string::npos) << IR;
  ASSERT_LT(RuntimeGroup, RuntimeDefEnd) << IR;
  const size_t RuntimeGroupBegin = RuntimeGroup + 2;
  const size_t RuntimeGroupEnd =
      IR.find_first_not_of("0123456789", RuntimeGroupBegin);
  ASSERT_NE(RuntimeGroupEnd, std::string::npos) << IR;
  const std::string RuntimeAttrMarker =
      "attributes #" +
      IR.substr(RuntimeGroupBegin, RuntimeGroupEnd - RuntimeGroupBegin) + " =";
  const size_t RuntimeAttrsBegin = IR.find(RuntimeAttrMarker);
  ASSERT_NE(RuntimeAttrsBegin, std::string::npos) << IR;
  const size_t RuntimeAttrsEnd = IR.find('\n', RuntimeAttrsBegin);
  const std::string RuntimeAttrs =
      IR.substr(RuntimeAttrsBegin, RuntimeAttrsEnd - RuntimeAttrsBegin);
  EXPECT_NE(RuntimeAttrs.find("+reserve-x18"), std::string::npos)
      << RuntimeAttrs;
  EXPECT_NE(RuntimeAttrs.find("+v8a"), std::string::npos) << RuntimeAttrs;
  EXPECT_NE(RuntimeAttrs.find("-fp-armv8"), std::string::npos) << RuntimeAttrs;
  EXPECT_NE(RuntimeAttrs.find("-crypto"), std::string::npos) << RuntimeAttrs;
  EXPECT_NE(RuntimeAttrs.find("-neon"), std::string::npos) << RuntimeAttrs;
  EXPECT_NE(RuntimeAttrs.find("-sve"), std::string::npos) << RuntimeAttrs;
  EXPECT_NE(RuntimeAttrs.find("-sve2"), std::string::npos) << RuntimeAttrs;
  EXPECT_NE(RuntimeAttrs.find("-sme"), std::string::npos) << RuntimeAttrs;
  EXPECT_NE(RuntimeAttrs.find("-sme2"), std::string::npos) << RuntimeAttrs;
}

TEST_F(LTOTest, AndroidKernelLegacyProfileRetainsStaticShadowCallStack) {
  auto Source = tmpFile("android_kernel_legacy_scs.c");
  auto Output = tmpFile("android_kernel_legacy_scs.ll");
  writeFile(Source, "int android_kernel_legacy_scs(void) { return 1; }\n");

  auto Result = ncc({
      "--target=aarch64-linux-android",
      "-std=c11",
      "-fandroid-kernel-driver-mode",
      "-DNVK_KERNEL=510",
      "-fno-lto",
      "-S",
      "-emit-llvm",
      "-o",
      Output.string(),
      Source.string(),
  });
  ASSERT_EQ(Result.exitCode, 0) << Result.err;

  const std::string IR = readFile(Output);
  EXPECT_NE(IR.find("shadowcallstack"), std::string::npos);
  EXPECT_NE(IR.find("\"branch-target-enforcement\"=\"true\""),
            std::string::npos);
  EXPECT_NE(IR.find("\"sign-return-address\"=\"all\""),
            std::string::npos);
  EXPECT_NE(IR.find("\"sign-return-address-key\"=\"a_key\""),
            std::string::npos);
}

TEST_F(LTOTest, AndroidKernelProfileContractFailsClosed) {
  auto Plain = tmpFile("android_kernel_profile_contract_plain.c");
  auto InvalidMarker =
      tmpFile("android_kernel_profile_contract_invalid_marker.c");
  auto ZeroProfile = tmpFile("android_kernel_profile_contract_zero_profile.c");
  auto MissingSCS = tmpFile("android_kernel_profile_contract_missing_scs.c");
  writeFile(Plain, "int android_profile_contract_plain(void) { return 1; }\n");
  writeFile(InvalidMarker, R"c(
__attribute__((visibility("hidden")))
const unsigned int __neverc_krt_kcfi_mode_marker = 7;
__attribute__((visibility("hidden")))
const unsigned int __neverc_krt_profile_marker = 1234;
int android_profile_contract_invalid(void) { return 1; }
)c");
  writeFile(ZeroProfile, R"c(
__attribute__((visibility("hidden")))
const unsigned int __neverc_krt_kcfi_mode_marker = 0;
__attribute__((visibility("hidden")))
const unsigned int __neverc_krt_profile_marker = 0;
int android_profile_contract_zero(void) { return 1; }
)c");
  writeFile(MissingSCS, R"c(
__attribute__((visibility("hidden")))
const unsigned int __neverc_krt_kcfi_mode_marker = 2;
__attribute__((visibility("hidden")))
const unsigned int __neverc_krt_profile_marker = 612;
int android_profile_contract_missing_scs(void) { return 1; }
)c");

  struct Failure {
    const char *Name;
    std::vector<std::string> ExtraArgs;
    fs::path Source;
    const char *Diagnostic;
  };
  const Failure Failures[] = {
      {"missing_markers",
       {},
       Plain,
       "Android kernel source is missing a KCFI mode marker"},
      {"missing_profile",
       {"-fandroid-kernel-kcfi-mode=0"},
       Plain,
       "Android kernel source is missing a profile marker"},
      {"explicit_conflict",
       {"-DNVK_KERNEL=612", "-fandroid-kernel-kcfi-mode=0"},
       Plain,
       "conflicting command-line and source Android kernel KCFI modes"},
      {"invalid_source_marker",
       {},
       InvalidMarker,
       "invalid Android kernel source KCFI mode marker"},
      {"zero_profile_marker",
       {},
       ZeroProfile,
       "invalid Android kernel source profile marker"},
      {"missing_scs_marker",
       {},
       MissingSCS,
       "invalid Android kernel source shadow-call-stack mode marker"},
      {"unsupported_profile",
       {"-DNVK_KERNEL=620"},
       Plain,
       "Unsupported NEVERC_KRT_KERNEL profile"},
      {"invalid_explicit_mode",
       {"-fandroid-kernel-kcfi-mode=4"},
       Plain,
       "invalid value"},
  };

  for (const Failure &Failure : Failures) {
    SCOPED_TRACE(Failure.Name);
    std::vector<std::string> Args = {
        "--target=aarch64-linux-android",
        "-std=c11",
        "-fandroid-kernel-driver-mode",
        "-fno-lto",
        "-c",
        "-o",
        tmpFile(std::string(Failure.Name) + ".o").string(),
    };
    Args.insert(Args.end(), Failure.ExtraArgs.begin(), Failure.ExtraArgs.end());
    Args.push_back(Failure.Source.string());
    auto Result = ncc(Args);
    EXPECT_NE(Result.exitCode, 0);
    EXPECT_NE(Result.err.find(Failure.Diagnostic), std::string::npos)
        << Result.err;
  }
}

TEST_F(LTOTest, AndroidKernelKcfiRejectsProviderThatDropsMode) {
  auto Source = tmpFile("android_kernel_kcfi_provider_drops_mode.c");
  writeFile(Source, "int kcfi_provider_input(void) { return 1; }\n");

  const char *Modes[] = {"-fno-lto", "-flto=full"};
  for (const char *Mode : Modes) {
    SCOPED_TRACE(Mode);
    auto Result = ncc({
        std::string("-fplugin=") + NEVERC_TEST_IR_OPTIMIZATION_PROVIDER_PLUGIN,
        "--target=aarch64-linux-android",
        "-std=c11",
        "-fandroid-kernel-driver-mode",
        "-DNVK_KERNEL=612",
        Mode,
        "-c",
        "-o",
        tmpFile("nvk_kcfi_provider_drops_mode.o").string(),
        Source.string(),
    });
    EXPECT_NE(Result.exitCode, 0);
    EXPECT_NE(Result.err.find("profile contract"), std::string::npos)
        << Result.err;
  }
}

// Link-only Android kernel invocations can receive precompiled full-LTO
// objects.  A plain object has neither the selected profile nor source type
// carriers, so accepting it would silently emit unprefixed callbacks.
TEST_F(LTOTest, AndroidKernelKcfiRejectsFlaglessFullLtoInput) {
  auto Source = tmpFile("android_kernel_kcfi_flagless_input.c");
  auto ContractSource =
      tmpFile("android_kernel_kcfi_contract_hitchhike_input.c");
  auto Input = tmpFile("android_kernel_kcfi_flagless_input.o");
  auto ContractInput =
      tmpFile("android_kernel_kcfi_contract_hitchhike_input.o");
  auto PlainOutput = tmpFile("android_kernel_kcfi_flagless_plain.o");
  auto Output = tmpFile("android_kernel_kcfi_flagless_output.ko");
  auto CacheDir = tmpFile("android_kernel_kcfi_flagless_cache");
  ScopedEnvVar CacheDirOverride(linker::ltoCacheDirEnvVar,
                                CacheDir.string().c_str());
  ScopedEnvVar CacheEnabled(linker::ltoCacheEnvVar, "1");
  writeFile(Source, R"c(
__attribute__((noinline))
int kcfi_flagless_callback(void) { return 1; }
)c");
  writeFile(ContractSource, R"c(
__attribute__((noinline))
int kcfi_contract_callback(void) { return 2; }
)c");

  auto Compile = ncc({
      "--target=aarch64-linux-android",
      "-std=c11",
      "-flto=full",
      "-c",
      "-o",
      Input.string(),
      Source.string(),
  });
  ASSERT_EQ(Compile.exitCode, 0) << Compile.err;

  auto ContractCompile = ncc({
      "--target=aarch64-linux-android",
      "-std=c11",
      "-fandroid-kernel-driver-mode",
      "-DNVK_KERNEL=612",
      "-flto=full",
      "-c",
      "-o",
      ContractInput.string(),
      ContractSource.string(),
  });
  ASSERT_EQ(ContractCompile.exitCode, 0) << ContractCompile.err;

  // Warm the full-link cache with the same flagless bitcode in an ordinary
  // relocatable link.  Android mode must be part of the cache key; otherwise
  // the next link could bypass its pre-optimization invariant check entirely.
  auto PlainLink = ncc({
      "--target=aarch64-linux-android",
      "-flto=full",
      "-r",
      "-nostdlib",
      "-o",
      PlainOutput.string(),
      Input.string(),
  });
  ASSERT_EQ(PlainLink.exitCode, 0) << PlainLink.err;

  auto Link = ncc({
      "--target=aarch64-linux-android",
      "-fandroid-kernel-driver-mode",
      "-DNVK_KERNEL=612",
      "-flto=full",
      "-r",
      "-nostdlib",
      "-o",
      Output.string(),
      ContractInput.string(),
      Input.string(),
  });
  EXPECT_NE(Link.exitCode, 0);
  EXPECT_NE(Link.err.find("Android kernel LTO input is missing the profile "
                          "contract"),
            std::string::npos)
      << Link.err;
}

TEST_F(LTOTest, AndroidKernelKcfiAcceptsSeparateFullLtoInput) {
  auto Source = tmpFile("android_kernel_kcfi_separate_input.c");
  auto Input = tmpFile("android_kernel_kcfi_separate_input.o");
  auto Output = tmpFile("android_kernel_kcfi_separate_output.ko");
  writeFile(Source, R"c(
__attribute__((noinline, section(".kcfi_test.separate")))
int kcfi_separate_callback(void) { return 1; }
)c");

  const std::vector<std::string> AndroidArgs = {
      "--target=aarch64-linux-android",
      "-fandroid-kernel-driver-mode",
      "-DNVK_KERNEL=612",
      "-flto=full",
  };
  auto CompileArgs = AndroidArgs;
  CompileArgs.insert(CompileArgs.end(),
                     {"-std=c11", "-c", "-o", Input.string(), Source.string()});
  auto Compile = ncc(CompileArgs);
  ASSERT_EQ(Compile.exitCode, 0) << Compile.err;

  auto LinkArgs = AndroidArgs;
  LinkArgs.insert(LinkArgs.end(),
                  {"-r", "-nostdlib", "-o", Output.string(), Input.string()});
  auto Link = ncc(LinkArgs);
  ASSERT_EQ(Link.exitCode, 0) << Link.err;

  const std::string Bytes = readFile(Output);
  auto Offset = readELFSymbolSectionOffset(Bytes, "kcfi_separate_callback");
  ASSERT_TRUE(static_cast<bool>(Offset))
      << llvm::toString(Offset.takeError()).str().str();
  EXPECT_EQ(*Offset, 4u);
  auto Prefix = readELFSymbolPrefix32(Bytes, "kcfi_separate_callback");
  ASSERT_TRUE(static_cast<bool>(Prefix))
      << llvm::toString(Prefix.takeError()).str().str();
  EXPECT_EQ(*Prefix, 0x6fbb3035u);
}

TEST_F(LTOTest, AndroidKernelFullLtoRejectsMixedOpaqueProfiles) {
  auto SourceA = tmpFile("android_kernel_profile_a.c");
  auto SourceB = tmpFile("android_kernel_profile_b.c");
  auto InputA = tmpFile("android_kernel_profile_a.o");
  auto InputB = tmpFile("android_kernel_profile_b.o");
  auto Output = tmpFile("android_kernel_mixed_profiles.ko");
  writeFile(SourceA, "int android_profile_a(void) { return 1; }\n");
  writeFile(SourceB, "int android_profile_b(void) { return 2; }\n");

  auto Compile = [&](llvm::StringRef Profile, const fs::path &Source,
                     const fs::path &Object) {
    return ncc({
        "--target=aarch64-linux-android",
        "-std=c11",
        "-fandroid-kernel-driver-mode",
        std::string("-DNVK_KERNEL=") + Profile.str(),
        "-flto=full",
        "-c",
        "-o",
        Object.string(),
        Source.string(),
    });
  };
  auto CompileA = Compile("612", SourceA, InputA);
  ASSERT_EQ(CompileA.exitCode, 0) << CompileA.err;
  auto CompileB = Compile("618", SourceB, InputB);
  ASSERT_EQ(CompileB.exitCode, 0) << CompileB.err;

  auto Link = ncc({
      "--target=aarch64-linux-android",
      "-fandroid-kernel-driver-mode",
      "-DNVK_KERNEL=612",
      "-flto=full",
      "-r",
      "-nostdlib",
      "-o",
      Output.string(),
      InputA.string(),
      InputB.string(),
  });
  EXPECT_NE(Link.exitCode, 0);
  EXPECT_NE(Link.err.find("neverc.android.kernel.profile"), std::string::npos)
      << Link.err;
}

TEST_F(LTOTest, AndroidKernelAutoLtoRejectsMixedOpaqueProfiles) {
  auto SourceA = tmpFile("android_kernel_auto_profile_a.c");
  auto SourceB = tmpFile("android_kernel_auto_profile_b.c");
  auto Output = tmpFile("android_kernel_auto_mixed_profiles.ko");
  writeFile(SourceA, R"c(
#define NVK_KERNEL 612
#include <nvkmod_version.h>
int android_auto_profile_a(void) { return 1; }
)c");
  writeFile(SourceB, R"c(
#define NVK_KERNEL 618
#include <nvkmod_version.h>
int android_auto_profile_b(void) { return 2; }
)c");

  auto Link = ncc({
      "--target=aarch64-linux-android",
      "-std=c11",
      "-fandroid-kernel-driver-mode",
      "-r",
      "-nostdlib",
      "-o",
      Output.string(),
      SourceA.string(),
      SourceB.string(),
  });
  EXPECT_NE(Link.exitCode, 0);
  EXPECT_NE(Link.err.find("neverc.android.kernel.profile"), std::string::npos)
      << Link.err;
}

TEST_F(LTOTest, AndroidKernelFullLtoAcceptsMatchingOpaqueProfiles) {
  auto SourceA = tmpFile("android_kernel_matching_profile_a.c");
  auto SourceB = tmpFile("android_kernel_matching_profile_b.c");
  auto InputA = tmpFile("android_kernel_matching_profile_a.o");
  auto InputB = tmpFile("android_kernel_matching_profile_b.o");
  auto Output = tmpFile("android_kernel_matching_profiles.ko");
  writeFile(SourceA, "int android_matching_profile_a(void) { return 1; }\n");
  writeFile(SourceB, "int android_matching_profile_b(void) { return 2; }\n");

  for (const auto &[Source, Object] :
       {std::pair{SourceA, InputA}, std::pair{SourceB, InputB}}) {
    auto Compile = ncc({
        "--target=aarch64-linux-android",
        "-std=c11",
        "-fandroid-kernel-driver-mode",
        "-DNVK_KERNEL=612",
        "-flto=full",
        "-c",
        "-o",
        Object.string(),
        Source.string(),
    });
    ASSERT_EQ(Compile.exitCode, 0) << Compile.err;
  }

  auto Link = ncc({
      "--target=aarch64-linux-android",
      "-fandroid-kernel-driver-mode",
      "-DNVK_KERNEL=612",
      "-flto=full",
      "-r",
      "-nostdlib",
      "-o",
      Output.string(),
      InputA.string(),
      InputB.string(),
  });
  EXPECT_EQ(Link.exitCode, 0) << Link.err;
}

TEST_F(LTOTest, AndroidKernelFinalModuleDropsContractAcrossLtoModes) {
  auto Source = tmpFile("android_kernel_final_contract.c");
  writeFile(Source, "int android_kernel_final_contract(void) { return 1; }\n");

  struct Mode {
    const char *Name;
    const char *Flag;
  };
  const Mode Modes[] = {
      {"native", "-fno-lto"},
      {"auto", nullptr},
      {"full", "-flto=full"},
  };
  for (const Mode &M : Modes) {
    SCOPED_TRACE(M.Name);
    auto Output =
        tmpFile(std::string("android_kernel_final_contract_") + M.Name + ".ko");
    std::vector<std::string> Args = {
        "--target=aarch64-linux-android",
        "-std=c11",
        "-fandroid-kernel-driver-mode",
        "-DNVK_KERNEL=612",
    };
    if (M.Flag)
      Args.push_back(M.Flag);
    Args.insert(Args.end(),
                {"-r", "-nostdlib", "-o", Output.string(), Source.string()});

    auto Link = ncc(Args);
    ASSERT_EQ(Link.exitCode, 0) << Link.err;
    const std::string Bytes = readFile(Output);
    EXPECT_EQ(Bytes.find(".neverc.android.kernel.profile"), std::string::npos);
    EXPECT_EQ(Bytes.find("__neverc_android_kernel_profile_contract"),
              std::string::npos);
  }
}

TEST_F(LTOTest,
       AndroidKernelReleaseInspectorDistinguishesDuplicateSectionNames) {
  auto Source = tmpFile("android_kernel_release_duplicate_sections.c");
  auto Module = tmpFile("android_kernel_release_duplicate_sections.ko");
  writeFile(Source, R"c(
__asm__(
    ".pushsection .neverc.release.dup.first,\"a\",@progbits\n"
    ".p2align 3\n"
    ".globl neverc_release_dup_first\n"
    ".type neverc_release_dup_first,@notype\n"
    "neverc_release_dup_first:\n"
    ".quad 0\n"
    ".size neverc_release_dup_first, .-neverc_release_dup_first\n"
    ".popsection\n"
    ".pushsection .neverc.release.dup.second,\"a\",@progbits\n"
    ".p2align 3\n"
    ".globl neverc_release_dup_second\n"
    ".type neverc_release_dup_second,@notype\n"
    "neverc_release_dup_second:\n"
    ".quad 0\n"
    ".size neverc_release_dup_second, .-neverc_release_dup_second\n"
    ".popsection\n");
)c");

  auto Build = ncc({
      "--target=aarch64-linux-android",
      "-std=c11",
      "-fandroid-kernel-driver-mode",
      "-DNVK_KERNEL=612",
      "-fno-lto",
      "-r",
      "-nostdlib",
      "-o",
      Module.string(),
      Source.string(),
  });
  ASSERT_EQ(Build.exitCode, 0) << Build.err;

  auto Mutated =
      retargetELF64LESectionName(readFile(Module), ".neverc.release.dup.second",
                                 ".neverc.release.dup.first");
  ASSERT_TRUE(static_cast<bool>(Mutated))
      << llvm::toString(Mutated.takeError()).str().str();

  auto FirstRaw =
      readELF64LERawSymbolLocation(*Mutated, "neverc_release_dup_first");
  ASSERT_TRUE(static_cast<bool>(FirstRaw))
      << llvm::toString(FirstRaw.takeError()).str().str();
  auto SecondRaw =
      readELF64LERawSymbolLocation(*Mutated, "neverc_release_dup_second");
  ASSERT_TRUE(static_cast<bool>(SecondRaw))
      << llvm::toString(SecondRaw.takeError()).str().str();
  ASSERT_LT(FirstRaw->SectionIndex, SecondRaw->SectionIndex)
      << "fixture must place the retargeted section after the original";

  auto Inspected = inspectAndroidKernelReleaseMetadata(*Mutated);
  ASSERT_TRUE(static_cast<bool>(Inspected))
      << llvm::toString(Inspected.takeError()).str().str();

  auto FindSymbol = [&](llvm::StringRef Name) {
    return llvm::find_if(Inspected->Symbols,
                         [&](const AndroidKernelReleaseSymbol &Symbol) {
                           return Symbol.Name == Name;
                         });
  };
  auto FirstSymbol = FindSymbol("neverc_release_dup_first");
  auto SecondSymbol = FindSymbol("neverc_release_dup_second");
  ASSERT_NE(FirstSymbol, Inspected->Symbols.end());
  ASSERT_NE(SecondSymbol, Inspected->Symbols.end());

  auto FindSection = [&](uint16_t RawIndex) {
    return llvm::find_if(Inspected->Sections,
                         [&](const AndroidKernelReleaseSection &Section) {
                           return Section.Index == RawIndex;
                         });
  };
  auto FirstSection = FindSection(FirstRaw->SectionIndex);
  auto SecondSection = FindSection(SecondRaw->SectionIndex);
  ASSERT_NE(FirstSection, Inspected->Sections.end());
  ASSERT_NE(SecondSection, Inspected->Sections.end());
  ASSERT_EQ(FirstSection->Name, ".neverc.release.dup.first");
  ASSERT_EQ(SecondSection->Name, ".neverc.release.dup.first");
  ASSERT_NE(FirstSection->AnalysisBase, SecondSection->AnalysisBase);

  // The two serialized symbols have identical tuples except for their raw
  // st_shndx. A name-only section identity therefore makes the old inspector
  // report a false semantic match after the one-field sh_name mutation.
  EXPECT_FALSE(FirstSymbol->Semantics == SecondSymbol->Semantics);

  const std::string ExpectedSecondName = neverc::formatReleaseName(
      neverc::ReleaseNameKind::Other,
      SecondSection->AnalysisBase + SecondRaw->Value, SecondRaw->Size);
  auto ActualSecondName = canonicalReleaseBaseName(*Inspected, *SecondSymbol);
  ASSERT_TRUE(static_cast<bool>(ActualSecondName))
      << llvm::toString(ActualSecondName.takeError()).str().str();
  EXPECT_EQ(*ActualSecondName, ExpectedSecondName)
      << "canonical coordinates must use the symbol's raw section index";
}

TEST_F(LTOTest,
       AndroidKernelReleaseInspectorRejectsInvalidSymtabLocalBoundary) {
  auto Source = tmpFile("android_kernel_release_symtab_boundary.c");
  auto Module = tmpFile("android_kernel_release_symtab_boundary.ko");
  writeFile(Source, R"c(
static __attribute__((used)) int neverc_release_local_object = 7;

__attribute__((used, noinline))
int neverc_release_global_function(void) {
  return neverc_release_local_object;
}
)c");

  auto Build = ncc({
      "--target=aarch64-linux-android",
      "-std=c11",
      "-fandroid-kernel-driver-mode",
      "-DNVK_KERNEL=612",
      "-fno-lto",
      "-r",
      "-nostdlib",
      "-o",
      Module.string(),
      Source.string(),
  });
  ASSERT_EQ(Build.exitCode, 0) << Build.err;

  const std::string Bytes = readFile(Module);
  auto Facts = readELF64LESymtabFacts(Bytes);
  ASSERT_TRUE(static_cast<bool>(Facts))
      << llvm::toString(Facts.takeError()).str().str();
  ASSERT_GT(Facts->Info, 0u);
  ASSERT_LT(Facts->Info, Facts->SymbolCount);
  ASSERT_LT(Facts->SymbolCount, std::numeric_limits<uint32_t>::max());

  const uint32_t InvalidInfos[] = {0, Facts->SymbolCount,
                                   Facts->SymbolCount + 1};
  for (uint32_t InvalidInfo : InvalidInfos) {
    SCOPED_TRACE(InvalidInfo);
    auto Mutated = rewriteELF64LESymtabInfo(Bytes, InvalidInfo);
    ASSERT_TRUE(static_cast<bool>(Mutated))
        << llvm::toString(Mutated.takeError()).str().str();

    auto Inspected = inspectAndroidKernelReleaseMetadata(*Mutated);
    EXPECT_FALSE(static_cast<bool>(Inspected))
        << "inspector accepted a corrupted .symtab sh_info boundary";
    if (!Inspected)
      llvm::consumeError(Inspected.takeError());
  }
}

TEST_F(LTOTest, AndroidKernelReleaseStripIsRelocationSafeAcrossLtoModes) {
  auto Source = tmpFile("android_kernel_release_strip.c");
  writeFile(Source, R"c(
extern int neverc_release_needed_import(int);

static __attribute__((used, noinline))
int neverc_release_unneeded_local(int value) {
  return value + 17;
}

static __attribute__((noinline))
int neverc_release_pcg_local(int value) {
  return value * 3 + 1;
}

__attribute__((used))
int (*neverc_release_pcg_slot)(int) = neverc_release_pcg_local;

__attribute__((noinline))
int neverc_release_public_definition(int value) {
  return neverc_release_needed_import(value);
}

__attribute__((used, noinline))
int init_module(void) {
  return neverc_release_public_definition(0);
}

__attribute__((used, noinline))
void cleanup_module(void) {
  __asm__ volatile("" ::: "memory");
}

__attribute__((noinline))
int neverc_release_alias_target(int value) {
  return value + 29;
}

extern __typeof(neverc_release_alias_target) neverc_release_alias
    __attribute__((alias("neverc_release_alias_target")));

int neverc_release_object_definition = 23;

// Android's scripts/kallsyms.c treats CFI type-ID prefixes specially, and
// include/linux/cfi_types.h emits `.4byte __kcfi_typeid_<function>` for
// assembly entry annotations. These are ABI/tooling names, not cosmetic debug
// labels:
// https://android.googlesource.com/kernel/common/+/47d26684185d09e083669bbbd0c465ab3493a51f/scripts/kallsyms.c
// https://android.googlesource.com/kernel/common/+/refs/tags/android14-6.1-2025-05_r5/include/linux/cfi_types.h
const unsigned int __kcfi_typeid_sample = 0x6fbb3035u;
__attribute__((used))
const void *neverc_release_kcfi_reference = &__kcfi_typeid_sample;

__asm__(
    ".pushsection .text.neverc_release_code_label,\"ax\",@progbits\n"
    ".globl neverc_release_code_label\n"
    ".type neverc_release_code_label,@notype\n"
    "neverc_release_code_label:\n"
    ".byte 0\n"
    ".size neverc_release_code_label, .-neverc_release_code_label\n"
    ".popsection\n"
    ".pushsection .rodata.neverc_release_notype,\"a\",@progbits\n"
    ".globl neverc_release_notype_definition\n"
    ".type neverc_release_notype_definition,@notype\n"
    "neverc_release_notype_definition:\n"
    ".quad 0\n"
    ".size neverc_release_notype_definition, "
    ".-neverc_release_notype_definition\n"
    ".popsection\n"
    ".globl neverc_release_absolute_definition\n"
    ".type neverc_release_absolute_definition,@notype\n"
    ".set neverc_release_absolute_definition, 0x2b\n"
    ".globl __typeid__sample_global_addr\n"
    ".type __typeid__sample_global_addr,@notype\n"
    ".set __typeid__sample_global_addr, 0x2a\n");
)c");

  struct Mode {
    const char *Name;
    const char *Flag;
  };
  const Mode Modes[] = {
      {"native", "-fno-lto"},
      {"auto", nullptr},
      {"full", "-flto=full"},
  };

  ScopedEnvVar NoLtoCache(linker::ltoCacheEnvVar, linker::ltoCacheDisableValue);
  ScopedEnvVar NoPartitionCache(linker::ltoPartitionCacheEnvVar,
                                linker::ltoCacheDisableValue);

  for (const Mode &M : Modes) {
    SCOPED_TRACE(M.Name);
    auto DebugModule =
        tmpFile(std::string("android_kernel_debug_") + M.Name + ".ko");
    auto ReleaseModule =
        tmpFile(std::string("android_kernel_release_") + M.Name + ".ko");
    auto RepeatReleaseModule =
        tmpFile(std::string("android_kernel_release_repeat_") + M.Name + ".ko");

    auto Build = [&](const fs::path &Output, bool Strip) {
      std::vector<std::string> Args = {
          "--target=aarch64-linux-android", "-std=c11",         "-O2",
          "-fandroid-kernel-driver-mode",   "-DNVK_KERNEL=612", "-g",
      };
      if (M.Flag)
        Args.push_back(M.Flag);
      if (Strip)
        Args.push_back("--strip");
      Args.insert(Args.end(),
                  {"-r", "-nostdlib", "-o", Output.string(), Source.string()});
      return ncc(Args);
    };

    auto DebugBuild = Build(DebugModule, false);
    ASSERT_EQ(DebugBuild.exitCode, 0) << DebugBuild.err;
    auto Debug = inspectAndroidKernelReleaseMetadata(readFile(DebugModule));
    ASSERT_TRUE(static_cast<bool>(Debug))
        << llvm::toString(Debug.takeError()).str().str();
    EXPECT_TRUE(Debug->HasDebugSection);
    EXPECT_TRUE(
        symbolStringTableContains(*Debug, "neverc_release_unneeded_local"));

    auto ReleaseBuild = Build(ReleaseModule, true);
    ASSERT_EQ(ReleaseBuild.exitCode, 0) << ReleaseBuild.err;
    const std::string ReleaseBytes = readFile(ReleaseModule);
    auto Release =
        inspectAndroidKernelReleaseMetadata(ReleaseBytes,
                                            /*AuditCanonicalNames=*/true);
    ASSERT_TRUE(static_cast<bool>(Release))
        << llvm::toString(Release.takeError()).str().str();

    EXPECT_EQ(Release->SymbolTableCount, 1u);
    EXPECT_EQ(Release->SymbolStringTableCount, 1u);
    EXPECT_TRUE(Release->SymtabLinksSymbolStringTable);
    EXPECT_GT(Release->RelocationSectionCount, 0u);
    EXPECT_FALSE(Release->HasDebugSection);
    EXPECT_FALSE(Release->HasCommentSection);
    EXPECT_TRUE(Release->HasVersionsSection);
    EXPECT_TRUE(Release->HasAllocTagsSection);

    auto NamedSymbols = [](const AndroidKernelReleaseMetadata &Image,
                           llvm::StringRef Name) {
      std::vector<const AndroidKernelReleaseSymbol *> Matches;
      for (const AndroidKernelReleaseSymbol &Symbol : Image.Symbols)
        if (Symbol.Name == Name)
          Matches.push_back(&Symbol);
      return Matches;
    };
    auto SemanticallyEquivalent =
        [](const AndroidKernelReleaseMetadata &Image,
           const AndroidKernelReleaseSymbolSemantics &Semantics) {
          std::vector<const AndroidKernelReleaseSymbol *> Matches;
          for (const AndroidKernelReleaseSymbol &Symbol : Image.Symbols)
            if (Symbol.Semantics == Semantics)
              Matches.push_back(&Symbol);
          return Matches;
        };

    // Stripping may prune relocation-unneeded locals, but it must not mutate
    // any serialized field of a surviving symbol. Compare complete structural
    // tuples as a multiset so aliases remain order-independent.
    std::map<AndroidKernelReleaseSymbolSemantics, unsigned> DebugSemantics;
    std::map<AndroidKernelReleaseSymbolSemantics, unsigned> ReleaseSemantics;
    for (const AndroidKernelReleaseSymbol &Symbol : Debug->Symbols)
      ++DebugSemantics[Symbol.Semantics];
    for (const AndroidKernelReleaseSymbol &Symbol : Release->Symbols)
      ++ReleaseSemantics[Symbol.Semantics];
    for (const auto &[Semantics, Count] : ReleaseSemantics)
      EXPECT_GE(DebugSemantics[Semantics], Count)
          << "release changed a surviving symbol's "
             "value/type/binding/st_other/size";

    // Exact loader/import/CFI spellings must retain both their bytes and their
    // complete ELF semantics. The two type-ID fixtures cover SHN_ABS and a
    // relocation-targeted allocated definition respectively.
    std::set<std::string> ExactNames = {
        "neverc_release_needed_import",
        "init_module",
        "cleanup_module",
        "__cfi_check",
        "__cfi_check_fail",
        "__typeid__sample_global_addr",
        "__kcfi_typeid_sample",
    };
    for (llvm::StringRef OptionalJumpTable :
         {llvm::StringRef("__cfi_jt_init_module"),
          llvm::StringRef("__cfi_jt_cleanup_module")})
      if (!NamedSymbols(*Debug, OptionalJumpTable).empty())
        ExactNames.insert(OptionalJumpTable.str());
    for (const std::string &ExactNameStorage : ExactNames) {
      const llvm::StringRef ExactName(ExactNameStorage);
      const auto DebugMatches = NamedSymbols(*Debug, ExactName);
      const auto ReleaseMatches = NamedSymbols(*Release, ExactName);
      ASSERT_FALSE(DebugMatches.empty()) << ExactName.str();
      ASSERT_EQ(ReleaseMatches.size(), DebugMatches.size()) << ExactName.str();
      std::multiset<AndroidKernelReleaseSymbolSemantics> DebugValues;
      std::multiset<AndroidKernelReleaseSymbolSemantics> ReleaseValues;
      for (const AndroidKernelReleaseSymbol *Symbol : DebugMatches)
        DebugValues.insert(Symbol->Semantics);
      for (const AndroidKernelReleaseSymbol *Symbol : ReleaseMatches)
        ReleaseValues.insert(Symbol->Semantics);
      EXPECT_EQ(ReleaseValues, DebugValues) << ExactName.str();
    }
    EXPECT_GT(Release->RelocationTargets.count("__kcfi_typeid_sample"), 0u);
    EXPECT_GT(Release->RelocationTargets.count("neverc_release_needed_import"),
              0u);

    // Exercise every generated coordinate class against an independently
    // reconstructed final section layout, not merely a prefix/regex shape.
    const std::pair<llvm::StringRef, llvm::StringRef> GeneratedNames[] = {
        {"neverc_release_public_definition", "fn_"},
        {"neverc_release_object_definition", "obj_"},
        {"neverc_release_code_label", "code_"},
        {"neverc_release_notype_definition", "sym_"},
        {"neverc_release_absolute_definition", "abs_"},
    };
    for (const auto &[OriginalName, Prefix] : GeneratedNames) {
      const auto Original = NamedSymbols(*Debug, OriginalName);
      ASSERT_EQ(Original.size(), 1u) << OriginalName.str();
      const auto Renamed =
          SemanticallyEquivalent(*Release, Original.front()->Semantics);
      ASSERT_EQ(Renamed.size(), 1u) << OriginalName.str();
      auto ExpectedBase = canonicalReleaseBaseName(*Release, *Renamed.front());
      ASSERT_TRUE(static_cast<bool>(ExpectedBase))
          << llvm::toString(ExpectedBase.takeError()).str().str();
      EXPECT_EQ(Renamed.front()->Name, *ExpectedBase) << OriginalName.str();
      EXPECT_TRUE(llvm::StringRef(Renamed.front()->Name).starts_with(Prefix))
          << OriginalName.str();
    }

    // A same-address function alias owns one exact canonical name multiset;
    // which indistinguishable ELF entry owns the unsuffixed spelling is not a
    // contract. The standalone audit above verifies that no suffix can be
    // skipped, duplicated, or exchanged across structural classes.
    const auto AliasTarget =
        NamedSymbols(*Debug, "neverc_release_alias_target");
    const auto Alias = NamedSymbols(*Debug, "neverc_release_alias");
    ASSERT_EQ(AliasTarget.size(), 1u);
    ASSERT_EQ(Alias.size(), 1u);
    auto AliasBase = canonicalReleaseBaseName(*Release, *AliasTarget.front());
    ASSERT_TRUE(static_cast<bool>(AliasBase))
        << llvm::toString(AliasBase.takeError()).str().str();
    std::set<std::string> AliasNames;
    for (const AndroidKernelReleaseSymbol &Symbol : Release->Symbols)
      if (Symbol.Name == *AliasBase ||
          llvm::StringRef(Symbol.Name).starts_with(*AliasBase + "_"))
        AliasNames.insert(Symbol.Name);
    EXPECT_EQ(AliasNames,
              (std::set<std::string>{*AliasBase, *AliasBase + "_1"}));

    // The rebuilt .strtab must not retain unreachable source/runtime names.
    // Determine this from the exact-name policy, never from the legacy
    // name-eligibility/hash-shape helper.
    for (const AndroidKernelReleaseSymbol &Symbol : Debug->Symbols) {
      const bool Exact =
          neverc::AndroidKernelModuleSymbolPolicy::hasExactReleaseName(
              Symbol.Name, Symbol.Semantics.Class, Symbol.IsSectionSymbol,
              Symbol.PreserveName);
      if (!Exact && !Symbol.Name.empty())
        EXPECT_FALSE(symbolStringTableContains(*Release, Symbol.Name))
            << Symbol.Name;
    }
    EXPECT_FALSE(
        symbolStringTableContains(*Release, "neverc_release_unneeded_local"));

    auto RepeatReleaseBuild = Build(RepeatReleaseModule, true);
    ASSERT_EQ(RepeatReleaseBuild.exitCode, 0) << RepeatReleaseBuild.err;
    const std::string RepeatReleaseBytes = readFile(RepeatReleaseModule);
    EXPECT_EQ(RepeatReleaseBytes, ReleaseBytes)
        << "identical input under one toolchain was not byte-identical";
  }
}

TEST_F(LTOTest, AndroidKernelNativeProfileContractIsAtomicAcrossLinks) {
  auto SourceA = tmpFile("android_kernel_native_profile_a.c");
  auto SourceB = tmpFile("android_kernel_native_profile_b.c");
  auto SourceC = tmpFile("android_kernel_native_profile_c.c");
  auto Assembly = tmpFile("android_kernel_native_profile_asm.S");
  auto SourceLocalAssembly =
      tmpFile("android_kernel_native_profile_asm_source_local.S");
  auto ObjectA612 = tmpFile("android_kernel_native_profile_a_612.o");
  auto ObjectA510 = tmpFile("android_kernel_native_profile_a_510.o");
  auto ObjectB612 = tmpFile("android_kernel_native_profile_b_612.o");
  auto ObjectB618 = tmpFile("android_kernel_native_profile_b_618.o");
  auto ObjectC612 = tmpFile("android_kernel_native_profile_c_612.o");
  auto LTOObjectC612 = tmpFile("android_kernel_lto_profile_c_612.o");
  auto LTOObjectC618 = tmpFile("android_kernel_lto_profile_c_618.o");
  auto PlainObject = tmpFile("android_kernel_native_profile_plain.o");
  auto AssemblyObject = tmpFile("android_kernel_native_profile_asm.o");
  auto DefaultAssemblyObject =
      tmpFile("android_kernel_native_profile_asm_default.o");
  auto SourceLocalAssemblyObject =
      tmpFile("android_kernel_native_profile_asm_source_local.o");
  writeFile(SourceA, "int android_native_profile_a(void) { return 1; }\n");
  writeFile(SourceB, "int android_native_profile_b(void) { return 2; }\n");
  writeFile(SourceC, "int android_native_profile_c(void) { return 3; }\n");
  writeFile(Assembly, R"s(
#include <nvkmod_version.h>
.text
.globl android_native_profile_asm
.type android_native_profile_asm,%function
android_native_profile_asm:
  mov w0, #4
  ret
.size android_native_profile_asm, .-android_native_profile_asm
)s");
  writeFile(SourceLocalAssembly, R"s(
#define NVK_KERNEL 618
#include <nvkmod_version.h>
.text
.globl android_native_profile_asm_source_local
.type android_native_profile_asm_source_local,%function
android_native_profile_asm_source_local:
  mov w0, #5
  ret
.size android_native_profile_asm_source_local, .-android_native_profile_asm_source_local
)s");

  auto Compile = [&](llvm::StringRef Profile, const fs::path &Source,
                     const fs::path &Object) {
    return ncc({
        "--target=aarch64-linux-android",
        "-std=c11",
        "-fandroid-kernel-driver-mode",
        std::string("-DNVK_KERNEL=") + Profile.str(),
        "-fno-lto",
        "-c",
        "-o",
        Object.string(),
        Source.string(),
    });
  };
  for (const auto &[Profile, Source, Object] : {
           std::tuple{"612", SourceA, ObjectA612},
           std::tuple{"510", SourceA, ObjectA510},
           std::tuple{"612", SourceB, ObjectB612},
           std::tuple{"618", SourceB, ObjectB618},
           std::tuple{"612", SourceC, ObjectC612},
       }) {
    auto Result = Compile(Profile, Source, Object);
    ASSERT_EQ(Result.exitCode, 0) << Result.err;
  }
  auto CompileLTO = [&](llvm::StringRef Profile, const fs::path &Object) {
    return ncc({
        "--target=aarch64-linux-android",
        "-std=c11",
        "-fandroid-kernel-driver-mode",
        std::string("-DNVK_KERNEL=") + Profile.str(),
        "-flto=full",
        "-c",
        "-o",
        Object.string(),
        SourceC.string(),
    });
  };
  for (const auto &[Profile, Object] : {
           std::pair{"612", LTOObjectC612},
           std::pair{"618", LTOObjectC618},
       }) {
    auto Result = CompileLTO(Profile, Object);
    ASSERT_EQ(Result.exitCode, 0) << Result.err;
  }
  auto PlainCompile = ncc({
      "--target=aarch64-linux-android",
      "-std=c11",
      "-fno-lto",
      "-c",
      "-o",
      PlainObject.string(),
      SourceC.string(),
  });
  ASSERT_EQ(PlainCompile.exitCode, 0) << PlainCompile.err;
  auto AssemblyCompile = ncc({
      "--target=aarch64-linux-android",
      "-fandroid-kernel-driver-mode",
      "-DNVK_KERNEL=612",
      "-fno-lto",
      "-c",
      "-o",
      AssemblyObject.string(),
      Assembly.string(),
  });
  ASSERT_EQ(AssemblyCompile.exitCode, 0) << AssemblyCompile.err;
  auto DefaultAssemblyCompile = ncc({
      "--target=aarch64-linux-android",
      "-fandroid-kernel-driver-mode",
      "-fno-lto",
      "-c",
      "-o",
      DefaultAssemblyObject.string(),
      Assembly.string(),
  });
  ASSERT_EQ(DefaultAssemblyCompile.exitCode, 0) << DefaultAssemblyCompile.err;
  auto SourceLocalAssemblyCompile = ncc({
      "--target=aarch64-linux-android",
      "-fandroid-kernel-driver-mode",
      "-fno-lto",
      "-c",
      "-o",
      SourceLocalAssemblyObject.string(),
      SourceLocalAssembly.string(),
  });
  ASSERT_EQ(SourceLocalAssemblyCompile.exitCode, 0)
      << SourceLocalAssemblyCompile.err;

  auto Link = [&](const std::vector<fs::path> &Inputs, const fs::path &Output,
                  const char *Plugin = nullptr) {
    std::vector<std::string> Args;
    if (Plugin)
      Args.push_back(std::string("-fplugin=") + Plugin);
    Args.insert(Args.end(), {
                                "--target=aarch64-linux-android",
                                "-fandroid-kernel-driver-mode",
                                "-fno-lto",
                                "-r",
                                "-nostdlib",
                                "-o",
                                Output.string(),
                            });
    for (const fs::path &Input : Inputs)
      Args.push_back(Input.string());
    return ncc(Args);
  };

  auto MatchingOutput = tmpFile("android_kernel_native_matching.ko");
  auto Matching = Link({ObjectA612, ObjectB612}, MatchingOutput);
  ASSERT_EQ(Matching.exitCode, 0) << Matching.err;

  auto AssemblyOutput = tmpFile("android_kernel_native_assembly.ko");
  auto AssemblyLink = Link({ObjectA612, AssemblyObject}, AssemblyOutput);
  EXPECT_EQ(AssemblyLink.exitCode, 0) << AssemblyLink.err;
  auto DefaultAssemblyOutput =
      tmpFile("android_kernel_native_assembly_default.ko");
  auto DefaultAssemblyLink =
      Link({ObjectA510, DefaultAssemblyObject}, DefaultAssemblyOutput);
  EXPECT_EQ(DefaultAssemblyLink.exitCode, 0) << DefaultAssemblyLink.err;
  auto SourceLocalAssemblyOutput =
      tmpFile("android_kernel_native_assembly_source_local.ko");
  auto SourceLocalAssemblyLink =
      Link({ObjectB618, SourceLocalAssemblyObject}, SourceLocalAssemblyOutput);
  EXPECT_EQ(SourceLocalAssemblyLink.exitCode, 0) << SourceLocalAssemblyLink.err;

  // A partial Android-kernel link keeps the contract so build systems can
  // aggregate compiler-produced objects before the final `.ko` link.
  auto PartialOutput = tmpFile("android_kernel_native_partial.o");
  auto Partial = Link({ObjectA612, ObjectB612}, PartialOutput);
  ASSERT_EQ(Partial.exitCode, 0) << Partial.err;
  auto RelinkedOutput = tmpFile("android_kernel_native_relinked.ko");
  auto Relinked = Link({PartialOutput, ObjectC612}, RelinkedOutput);
  EXPECT_EQ(Relinked.exitCode, 0) << Relinked.err;

  // A delivered `.ko` has deliberately discarded the intermediate contract
  // and remains invalid as an input to another contract-checked link.
  auto FromDeliveredKo = Link({MatchingOutput, ObjectC612},
                              tmpFile("android_kernel_from_delivered_ko.ko"));
  EXPECT_NE(FromDeliveredKo.exitCode, 0);
  EXPECT_NE(FromDeliveredKo.err.find(
                "missing native Android kernel profile contract"),
            std::string::npos)
      << FromDeliveredKo.err;

  auto LinkMixed = [&](const fs::path &Native, const fs::path &Bitcode,
                       const fs::path &Output) {
    return ncc({
        "--target=aarch64-linux-android",
        "-fandroid-kernel-driver-mode",
        "-flto=full",
        "-r",
        "-nostdlib",
        "-o",
        Output.string(),
        Native.string(),
        Bitcode.string(),
    });
  };
  auto MixedMatchingOutput = tmpFile("android_kernel_native_lto_matching.ko");
  auto MixedMatching =
      LinkMixed(ObjectA612, LTOObjectC612, MixedMatchingOutput);
  EXPECT_EQ(MixedMatching.exitCode, 0) << MixedMatching.err;

  auto MixedMismatchOutput = tmpFile("android_kernel_native_lto_mismatched.ko");
  auto MixedMismatch =
      LinkMixed(ObjectA612, LTOObjectC618, MixedMismatchOutput);
  EXPECT_NE(MixedMismatch.exitCode, 0);
  EXPECT_NE(
      MixedMismatch.err.find("incompatible Android kernel profile contracts"),
      std::string::npos)
      << MixedMismatch.err;

  auto MismatchedOutput = tmpFile("android_kernel_native_mismatched.ko");
  auto Mismatched = Link({ObjectA612, ObjectB618}, MismatchedOutput);
  EXPECT_NE(Mismatched.exitCode, 0);
  EXPECT_NE(
      Mismatched.err.find("incompatible Android kernel profile contracts"),
      std::string::npos)
      << Mismatched.err;

  auto MissingOutput = tmpFile("android_kernel_native_missing.ko");
  auto Missing = Link({ObjectA612, PlainObject}, MissingOutput);
  EXPECT_NE(Missing.exitCode, 0);
  EXPECT_NE(Missing.err.find("missing native Android kernel profile contract"),
            std::string::npos)
      << Missing.err;

  // The object-plugin route has its own merge boundary and must enforce the
  // same contract before a provider can select or discard one input record.
  auto PluginOutput = tmpFile("android_kernel_native_plugin_matching.ko");
  auto PluginMatching = Link({ObjectA612, ObjectB612}, PluginOutput,
                             NEVERC_TEST_OBJECT_POST_WRITE_PLUGIN);
  EXPECT_EQ(PluginMatching.exitCode, 0) << PluginMatching.err;

  // Final-output invariants are host-owned: a plugin must not reintroduce the
  // profile-contract fingerprint after the merger has removed it.
  auto PluginCorruptOutput = tmpFile("android_kernel_native_plugin_corrupt.ko");
  auto PluginCorrupt = Link({ObjectA612, ObjectB612}, PluginCorruptOutput,
                            NEVERC_TEST_OBJECT_CONTRACT_CORRUPT_PLUGIN);
  EXPECT_NE(PluginCorrupt.exitCode, 0);
  EXPECT_NE(PluginCorrupt.err.find(
                "must not retain native Android kernel profile contract"),
            std::string::npos)
      << PluginCorrupt.err;
  EXPECT_FALSE(fs::exists(PluginCorruptOutput))
      << "a plugin must not publish a re-fingerprinted .ko";

  auto PluginMismatchOutput =
      tmpFile("android_kernel_native_plugin_mismatched.ko");
  auto PluginMismatch = Link({ObjectA612, ObjectB618}, PluginMismatchOutput,
                             NEVERC_TEST_OBJECT_POST_WRITE_PLUGIN);
  EXPECT_NE(PluginMismatch.exitCode, 0);
  EXPECT_NE(
      PluginMismatch.err.find("incompatible Android kernel profile contracts"),
      std::string::npos)
      << PluginMismatch.err;
}

// NVK_KERNEL=618 must select the 6.18 preset (vermagic + file_operations
// layout).
TEST_F(LTOTest, AndroidKernel618PresetFromNvkKernel) {
  auto exDir =
      fs::canonical(testDir() / "../../examples/android-kernel-chardev");
  if (!fs::exists(exDir / "main.c"))
    GTEST_SKIP() << "android-kernel-chardev example not found";

  auto ko = tmpFile("nvk_chardev_618.ko");
  std::vector<std::string> args = {
      "--target=aarch64-linux-android",
      "-fandroid-kernel-driver-mode",
      "-DNVK_KERNEL=618",
      "-r",
      "-nostdlib",
      "-o",
      ko.string(),
      (exDir / "main.c").string(),
  };
  auto link = ncc(args);
  ASSERT_EQ(link.exitCode, 0) << link.err;

  auto stringsOut = exec("strings", {ko.string()});
  ASSERT_EQ(stringsOut.exitCode, 0) << stringsOut.err;
  EXPECT_NE(stringsOut.out.find("vermagic=6.18.24-android17-5"),
            std::string::npos)
      << "618 preset vermagic missing; NVK_KERNEL may not map to "
         "NEVERC_KRT_KERNEL";
}

// Compare complete cold builds from matching C sources. This benchmark is
// explicitly opt-in because it depends on an external clang-22 installation
// and takes long enough to be inappropriate for the normal unit-test suite.
TEST_F(LTOTest, AutoLtoCompleteBuildBeatsClang22FullLTO) {
  const char *ClangPath = std::getenv("NEVERC_BENCH_CLANG");
  if (!ClangPath || !*ClangPath)
    GTEST_SKIP() << "set NEVERC_BENCH_CLANG to a clang-22 executable";

  const std::string Clang = ClangPath;
  CmdResult Version = exec(Clang, {"--version"});
  if (Version.exitCode != 0)
    GTEST_SKIP() << "comparison clang is not executable: " << Version.err;
  const std::string VersionText = Version.out + Version.err;
  if (VersionText.find("clang version 22") == std::string::npos)
    GTEST_SKIP() << "comparison compiler is not clang-22: " << VersionText;

  ScopedEnvVar NoLtoCache(linker::ltoCacheEnvVar, linker::ltoCacheDisableValue);
  ScopedEnvVar NoPartitionCache(linker::ltoPartitionCacheEnvVar,
                                linker::ltoCacheDisableValue);

  const auto Sources =
      writeAutoLtoLoopDenseProject("clang22_complete_src", true);

  struct TimedBuild {
    CmdResult Result;
    double Seconds;
    fs::path Output;
  };
  auto build = [&](bool UseNeverc, const std::string &Tag, unsigned Run) {
    fs::path Output =
        tmpFile("clang22_complete_" + Tag + "_" + std::to_string(Run));
    std::vector<std::string> Args = {"-O2", "-std=c11"};
    if (!UseNeverc)
      Args.push_back("-flto=full");
    for (const auto &Flag : sysrootFlags())
      Args.push_back(Flag);
    for (const auto &Flag : archFlags())
      Args.push_back(Flag);
    Args.insert(Args.end(), Sources.begin(), Sources.end());
    if (UseNeverc && isWindows())
      Args.push_back("-mno-incremental-linker-compatible");
    Args.insert(Args.end(), {"-o", Output.string()});

    auto Start = std::chrono::steady_clock::now();
    CmdResult Result = UseNeverc ? ncc(Args) : exec(Clang, Args);
    double Seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - Start)
            .count();
    return TimedBuild{std::move(Result), Seconds, std::move(Output)};
  };

  TimedBuild NevercProbe = build(true, "neverc_probe", 0);
  ASSERT_EQ(NevercProbe.Result.exitCode, 0) << NevercProbe.Result.err;
  TimedBuild ClangProbe = build(false, "clang_probe", 0);
  if (ClangProbe.Result.exitCode != 0)
    GTEST_SKIP() << "clang-22 cannot build the comparison workload: "
                 << ClangProbe.Result.err;

  std::vector<double> NevercTimes;
  std::vector<double> ClangTimes;
  fs::path NevercOutput;
  fs::path ClangOutput;
  for (unsigned Run = 0; Run < 5; ++Run) {
    auto runNeverc = [&] {
      TimedBuild Build = build(true, "neverc", Run);
      if (Build.Result.exitCode != 0) {
        ADD_FAILURE() << Build.Result.err;
        return false;
      }
      NevercTimes.push_back(Build.Seconds);
      NevercOutput = std::move(Build.Output);
      return true;
    };
    auto runClang = [&] {
      TimedBuild Build = build(false, "clang", Run);
      if (Build.Result.exitCode != 0) {
        ADD_FAILURE() << Build.Result.err;
        return false;
      }
      ClangTimes.push_back(Build.Seconds);
      ClangOutput = std::move(Build.Output);
      return true;
    };

    if ((Run & 1) == 0) {
      ASSERT_TRUE(runNeverc());
      ASSERT_TRUE(runClang());
    } else {
      ASSERT_TRUE(runClang());
      ASSERT_TRUE(runNeverc());
    }
  }

  const double NevercMedian = medianSeconds(NevercTimes);
  const double ClangMedian = medianSeconds(ClangTimes);
  RecordProperty("neverc_complete_median_seconds", NevercMedian);
  RecordProperty("clang22_complete_median_seconds", ClangMedian);
  EXPECT_LT(NevercMedian, ClangMedian);

  CmdResult NevercRun = exec(NevercOutput.string(), {});
  CmdResult ClangRun = exec(ClangOutput.string(), {});
  ASSERT_EQ(NevercRun.exitCode, 0) << NevercRun.err;
  ASSERT_EQ(ClangRun.exitCode, 0) << ClangRun.err;
  EXPECT_TRUE(NevercRun.contains("CK=")) << NevercRun.out;
  EXPECT_EQ(NevercRun.out, ClangRun.out);
}

TEST_F(LTOTest, InlineAsmLTO) {
  auto asmDir = testDir() / "asm";
  auto objMain = tmpFile("asm_lto_main.o");
  auto objHelper = tmpFile("asm_lto_helper.o");
  auto exe = tmpFile("asm_lto");

  std::vector<std::string> base = {"-std=gnu11"};
  for (auto &f : sysrootFlags())
    base.push_back(f);
  for (auto &f : archFlags())
    base.push_back(f);

  auto a1 = base;
  a1.insert(a1.end(),
            {"-flto", "-c", (asmDir / "test_inline_asm_lto_main.c").string(),
             "-o", objMain.string()});
  ASSERT_EQ(ncc(a1).exitCode, 0);

  auto a2 = base;
  a2.insert(a2.end(),
            {"-flto", "-c", (asmDir / "test_inline_asm_lto_helper.c").string(),
             "-o", objHelper.string()});
  ASSERT_EQ(ncc(a2).exitCode, 0);

  std::vector<std::string> link;
  for (auto &f : sysrootFlags())
    link.push_back(f);
  for (auto &f : archFlags())
    link.push_back(f);
  link.insert(link.end(), {"-flto", objMain.string(), objHelper.string(), "-o",
                           exe.string()});
  ASSERT_EQ(ncc(link).exitCode, 0);

  auto r = exec(exe.string(), {});
  EXPECT_EQ(r.exitCode, 0);
  EXPECT_TRUE(r.contains("test_inline_asm_lto: ALL PASSED"));
}

TEST_F(LTOTest, InlineAsmGCCWithLTO) {
  auto src = (testDir() / "asm/test_inline_asm_gcc.c").string();
  auto obj = tmpFile("inline_asm_gcc_lto.o");
  auto exe = tmpFile("inline_asm_gcc_lto");

  std::vector<std::string> base = {"-std=gnu11"};
  for (auto &f : sysrootFlags())
    base.push_back(f);
  for (auto &f : archFlags())
    base.push_back(f);

  auto c = base;
  c.insert(c.end(), {"-flto", "-c", src, "-o", obj.string()});
  ASSERT_EQ(ncc(c).exitCode, 0);

  std::vector<std::string> link;
  for (auto &f : sysrootFlags())
    link.push_back(f);
  for (auto &f : archFlags())
    link.push_back(f);
  link.insert(link.end(), {"-flto", obj.string(), "-o", exe.string()});
  ASSERT_EQ(ncc(link).exitCode, 0);

  auto r = exec(exe.string(), {});
  EXPECT_EQ(r.exitCode, 0);
  EXPECT_TRUE(r.contains("test_inline_asm_gcc: ALL PASSED"));
}
