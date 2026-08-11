#include "BuiltinELFTableCanonicalizer.h"

#include "neverc/Foundation/ELFDebugSectionPolicy.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Object/ELF.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <vector>

using namespace llvm;

namespace neverc::plugin {
namespace {

using ELFT = object::ELF64LE;
using Ehdr = ELFT::Ehdr;
using Shdr = ELFT::Shdr;
using Sym = ELFT::Sym;
using Rel = ELFT::Rel;
using Rela = ELFT::Rela;
using Word = ELFT::Word;

Error invalidELF(const Twine &Reason) {
  return createStringError(inconvertibleErrorCode(),
                           "cannot canonicalize ELF tables: " + Reason);
}

class StringTableBuilder {
public:
  StringTableBuilder() { Bytes.push_back('\0'); }

  Expected<uint32_t> add(StringRef Value) {
    if (Value.empty())
      return uint32_t{0};
    const auto Found = Offsets.find(Value);
    if (Found != Offsets.end())
      return Found->second;
    if (Value.size() >= std::numeric_limits<uint32_t>::max() - Bytes.size())
      return invalidELF("a rebuilt string table exceeds the ELF32 name range");
    const uint32_t Offset = static_cast<uint32_t>(Bytes.size());
    Offsets.try_emplace(Value, Offset);
    Bytes.append(Value.begin(), Value.end());
    Bytes.push_back('\0');
    return Offset;
  }

  ArrayRef<char> bytes() const { return Bytes; }

private:
  StringMap<uint32_t> Offsets;
  SmallVector<char, 0> Bytes;
};

template <class T>
void appendRecord(SmallVectorImpl<char> &Output, const T &V) {
  const char *First = reinterpret_cast<const char *>(&V);
  Output.append(First, First + sizeof(T));
}

Error appendPaddingTo(SmallVectorImpl<char> &Output, uint64_t Offset) {
  if (Offset > std::numeric_limits<size_t>::max())
    return invalidELF("the output offset exceeds the host address range");
  if (Offset < Output.size())
    return invalidELF("an output offset moved backwards");
  Output.resize(static_cast<size_t>(Offset), 0);
  return Error::success();
}

Expected<uint64_t> alignedOffset(uint64_t Offset, uint64_t Alignment) {
  if (Alignment == 0)
    Alignment = 1;
  if (!isPowerOf2_64(Alignment))
    return invalidELF("a section has a non-power-of-two alignment");
  const uint64_t Mask = Alignment - 1;
  if (Offset > std::numeric_limits<uint64_t>::max() - Mask)
    return invalidELF("section alignment overflows the ELF offset range");
  return (Offset + Mask) & ~Mask;
}

Expected<StringRef> checkedStringAt(StringRef Table, uint32_t Offset,
                                    StringRef Kind) {
  if (Offset >= Table.size())
    return invalidELF(Kind + " name offset is outside its string table");
  const StringRef Tail = Table.drop_front(Offset);
  const size_t End = Tail.find('\0');
  if (End == StringRef::npos)
    return invalidELF(Kind + " name is not NUL-terminated");
  return Tail.take_front(End);
}

enum class DebugNameDisposition {
  DropCandidate,
  PreserveStructural,
  Reject,
};

DebugNameDisposition debugNameDisposition(const Shdr &Section) {
  // Allocated bytes are part of the load image even if their spelling looks
  // like DWARF. A release policy must never erase runtime state by name.
  if ((Section.sh_flags & ELF::SHF_ALLOC) != 0)
    return DebugNameDisposition::Reject;

  switch (Section.sh_type) {
  case ELF::SHT_SYMTAB_SHNDX:
    // Selected structurally by SHT_SYMTAB + SHN_XINDEX, not by its name.
    return DebugNameDisposition::PreserveStructural;
  case ELF::SHT_PROGBITS:
  case ELF::SHT_NOBITS: {
    const bool HasLinkOrder = (Section.sh_flags & ELF::SHF_LINK_ORDER) != 0;
    const bool HasInfoLink = (Section.sh_flags & ELF::SHF_INFO_LINK) != 0;
    if ((Section.sh_link != 0 && !HasLinkOrder) ||
        (Section.sh_info != 0 && !HasInfoLink))
      return DebugNameDisposition::Reject;
    return DebugNameDisposition::DropCandidate;
  }
  case ELF::SHT_REL:
  case ELF::SHT_RELA:
  case ELF::SHT_GROUP:
    // These can represent debug-owned payload, relocations, or COMDATs. The
    // dependency-closure audit below decides whether deleting them is safe.
    return DebugNameDisposition::DropCandidate;
  default:
    // Linker metadata and other structural tables are not proven to be debug
    // data merely because a producer gave them a .debug_* alias.
    return DebugNameDisposition::Reject;
  }
}

Error validateRetainedIndices(const object::ELFFile<ELFT> &File,
                              ArrayRef<Shdr> Sections,
                              uint32_t SymbolTableIndex,
                              ArrayRef<Sym> Symbols) {
  const Shdr *ExtendedIndexSection = nullptr;
  ArrayRef<Word> ExtendedIndices;
  for (const Shdr &Section : Sections) {
    if (Section.sh_type != ELF::SHT_SYMTAB_SHNDX)
      continue;
    if (Symbols.size() > std::numeric_limits<uint64_t>::max() / sizeof(Word))
      return invalidELF("SHT_SYMTAB_SHNDX size overflows the ELF range");
    const uint64_t ExpectedSize = Symbols.size() * sizeof(Word);
    if (ExtendedIndexSection || Section.sh_link != SymbolTableIndex ||
        Section.sh_entsize != sizeof(Word) || Section.sh_size != ExpectedSize)
      return invalidELF("SHT_SYMTAB_SHNDX has an invalid shape or sh_link");
    auto Values = File.template getSectionContentsAsArray<Word>(Section);
    if (!Values)
      return joinErrors(invalidELF("SHT_SYMTAB_SHNDX is out of bounds"),
                        Values.takeError());
    ExtendedIndexSection = &Section;
    ExtendedIndices = *Values;
  }

  for (size_t Index = 0; Index != Symbols.size(); ++Index) {
    const Sym &Symbol = Symbols[Index];
    if ((Index < Sections[SymbolTableIndex].sh_info) !=
        (Symbol.getBinding() == ELF::STB_LOCAL))
      return invalidELF("SHT_SYMTAB has a noncanonical local-symbol boundary");
    if (Symbol.st_shndx == ELF::SHN_XINDEX) {
      if (!ExtendedIndexSection || ExtendedIndices[Index] == 0 ||
          static_cast<uint32_t>(ExtendedIndices[Index]) >= Sections.size())
        return invalidELF("a symbol has an invalid extended section index");
      continue;
    }
    if (ExtendedIndexSection && ExtendedIndices[Index] != 0)
      return invalidELF(
          "SHT_SYMTAB_SHNDX has a value for a direct-index symbol");
    const uint16_t SectionIndex = Symbol.st_shndx;
    if (SectionIndex < ELF::SHN_LORESERVE && SectionIndex >= Sections.size())
      return invalidELF("a symbol has a dangling section index");
  }

  std::vector<uint32_t> GroupMemberships(Sections.size(), 0);
  for (size_t Index = 1; Index != Sections.size(); ++Index) {
    const Shdr &Section = Sections[Index];
    if (Section.sh_link != 0 && Section.sh_link >= Sections.size())
      return invalidELF("a retained section has a dangling sh_link");
    if ((Section.sh_flags & ELF::SHF_INFO_LINK) != 0 &&
        Section.sh_info >= Sections.size())
      return invalidELF("a retained section has a dangling sh_info");

    switch (Section.sh_type) {
    case ELF::SHT_GROUP: {
      if (Section.sh_link != SymbolTableIndex ||
          Section.sh_info >= Symbols.size() ||
          Section.sh_entsize != sizeof(Word) ||
          Section.sh_size < sizeof(Word) || Section.sh_size % sizeof(Word) != 0)
        return invalidELF("SHT_GROUP has invalid symbol-table metadata");
      auto Words = File.template getSectionContentsAsArray<Word>(Section);
      if (!Words)
        return joinErrors(invalidELF("SHT_GROUP payload is out of bounds"),
                          Words.takeError());
      for (Word Member : Words->drop_front()) {
        const uint32_t MemberIndex = Member;
        if (MemberIndex == 0 || MemberIndex >= Sections.size())
          return invalidELF("SHT_GROUP has a dangling member index");
        if ((Sections[MemberIndex].sh_flags & ELF::SHF_GROUP) == 0)
          return invalidELF(
              "SHT_GROUP contains a member without the SHF_GROUP flag");
        if (++GroupMemberships[MemberIndex] != 1)
          return invalidELF(
              "an SHF_GROUP section belongs to more than one SHT_GROUP");
      }
      break;
    }
    case ELF::SHT_REL: {
      if (Section.sh_link != SymbolTableIndex || Section.sh_info == 0 ||
          Section.sh_info >= Sections.size() ||
          Section.sh_entsize != sizeof(Rel) ||
          Section.sh_size % sizeof(Rel) != 0)
        return invalidELF("SHT_REL has invalid link, target, or entry size");
      auto Relocations = File.rels(Section);
      if (!Relocations)
        return joinErrors(invalidELF("SHT_REL payload is out of bounds"),
                          Relocations.takeError());
      for (const Rel &Relocation : *Relocations)
        if (Relocation.getSymbol() >= Symbols.size())
          return invalidELF("SHT_REL has a dangling symbol index");
      break;
    }
    case ELF::SHT_RELA: {
      if (Section.sh_link != SymbolTableIndex || Section.sh_info == 0 ||
          Section.sh_info >= Sections.size() ||
          Section.sh_entsize != sizeof(Rela) ||
          Section.sh_size % sizeof(Rela) != 0)
        return invalidELF("SHT_RELA has invalid link, target, or entry size");
      auto Relocations = File.relas(Section);
      if (!Relocations)
        return joinErrors(invalidELF("SHT_RELA payload is out of bounds"),
                          Relocations.takeError());
      for (const Rela &Relocation : *Relocations)
        if (Relocation.getSymbol() >= Symbols.size())
          return invalidELF("SHT_RELA has a dangling symbol index");
      break;
    }
    case ELF::SHT_LLVM_ADDRSIG: {
      if (Section.sh_link != SymbolTableIndex)
        return invalidELF("SHT_LLVM_ADDRSIG does not link to SHT_SYMTAB");
      auto Bytes = File.getSectionContents(Section);
      if (!Bytes)
        return joinErrors(invalidELF("SHT_LLVM_ADDRSIG is out of bounds"),
                          Bytes.takeError());
      const uint8_t *Current = Bytes->data();
      const uint8_t *End = Current + Bytes->size();
      while (Current != End) {
        unsigned Length = 0;
        const char *DecodeError = nullptr;
        const uint64_t Symbol =
            decodeULEB128(Current, &Length, End, &DecodeError);
        if (DecodeError || Length == 0 || Symbol >= Symbols.size())
          return invalidELF("SHT_LLVM_ADDRSIG has a malformed symbol index");
        Current += Length;
      }
      break;
    }
    case ELF::SHT_LLVM_CALL_GRAPH_PROFILE:
      if (Section.sh_link != SymbolTableIndex ||
          Section.sh_entsize != sizeof(uint64_t) ||
          Section.sh_size % sizeof(uint64_t) != 0)
        return invalidELF(
            "SHT_LLVM_CALL_GRAPH_PROFILE has invalid table metadata");
      break;
    case ELF::SHT_SYMTAB_SHNDX:
      // Validated as one symbol-table-sized companion above.
      break;
    default:
      break;
    }
  }
  for (size_t Index = 1; Index != Sections.size(); ++Index)
    if ((Sections[Index].sh_flags & ELF::SHF_GROUP) != 0 &&
        GroupMemberships[Index] != 1)
      return invalidELF(
          "an SHF_GROUP section has no unique SHT_GROUP membership");
  return Error::success();
}

} // namespace

Expected<SmallVector<char, 0>>
canonicalizeBuiltinELFTables(StringRef Input, bool DropDebugInfo) {
  auto Parsed = object::ELFFile<ELFT>::create(Input);
  if (!Parsed)
    return joinErrors(invalidELF("input is not ELF64LE"), Parsed.takeError());
  const Ehdr &InputHeader = Parsed->getHeader();
  if (!InputHeader.checkMagic() ||
      InputHeader.e_ident[ELF::EI_CLASS] != ELF::ELFCLASS64 ||
      InputHeader.e_ident[ELF::EI_DATA] != ELF::ELFDATA2LSB ||
      InputHeader.e_ident[ELF::EI_VERSION] != ELF::EV_CURRENT ||
      InputHeader.e_version != ELF::EV_CURRENT)
    return invalidELF(
        "input has incompatible ELF magic, class, data, or version fields");
  if (InputHeader.e_type != ELF::ET_REL ||
      InputHeader.e_ehsize != sizeof(Ehdr) ||
      InputHeader.e_shentsize != sizeof(Shdr) || InputHeader.e_phnum != 0 ||
      InputHeader.e_phoff != 0)
    return invalidELF("input is not a section-only ELF64LE relocatable");

  auto InputSectionsOr = Parsed->sections();
  if (!InputSectionsOr)
    return joinErrors(invalidELF("section table is malformed"),
                      InputSectionsOr.takeError());
  const ArrayRef<Shdr> InputSections = *InputSectionsOr;
  if (InputSections.empty() || InputSections.front().sh_type != ELF::SHT_NULL)
    return invalidELF("section zero is missing or is not SHT_NULL");
  if (InputSections.size() > std::numeric_limits<uint32_t>::max())
    return invalidELF("section count exceeds the ELF extended-index range");

  uint32_t SectionStringIndex = InputHeader.e_shstrndx;
  if (SectionStringIndex == ELF::SHN_XINDEX)
    SectionStringIndex = InputSections.front().sh_link;
  if (SectionStringIndex == 0 || SectionStringIndex >= InputSections.size() ||
      InputSections[SectionStringIndex].sh_type != ELF::SHT_STRTAB)
    return invalidELF("e_shstrndx does not name an SHT_STRTAB section");

  std::optional<uint32_t> SymbolTableIndex;
  for (uint32_t Index = 1; Index != InputSections.size(); ++Index) {
    if (InputSections[Index].sh_type != ELF::SHT_SYMTAB)
      continue;
    if (SymbolTableIndex)
      return invalidELF("more than one SHT_SYMTAB is present");
    SymbolTableIndex = Index;
  }
  if (!SymbolTableIndex)
    return invalidELF("SHT_SYMTAB is missing");
  const Shdr &InputSymtab = InputSections[*SymbolTableIndex];
  if (InputSymtab.sh_entsize != sizeof(Sym) || InputSymtab.sh_size == 0 ||
      InputSymtab.sh_size % sizeof(Sym) != 0 || InputSymtab.sh_link == 0 ||
      InputSymtab.sh_link >= InputSections.size() ||
      InputSections[InputSymtab.sh_link].sh_type != ELF::SHT_STRTAB)
    return invalidELF("SHT_SYMTAB has an invalid shape or sh_link");
  const uint32_t SymbolStringIndex = InputSymtab.sh_link;

  const bool SharedTable = SymbolStringIndex == SectionStringIndex;
  for (uint32_t Index = 1; Index != InputSections.size(); ++Index) {
    if (Index != *SymbolTableIndex &&
        InputSections[Index].sh_link == SymbolStringIndex)
      return invalidELF(
          "the selected symbol string table has an additional consumer");
    if (!SharedTable && InputSections[Index].sh_link == SectionStringIndex)
      return invalidELF(
          "the selected section string table has an additional consumer");
    if (DropDebugInfo && InputSections[Index].sh_type == ELF::SHT_DYNSYM)
      return invalidELF(
          "DROP_DEBUG_INFO cannot remap an additional dynamic symbol table");
  }

  auto SectionStringsOr =
      Parsed->getSectionContents(InputSections[SectionStringIndex]);
  auto SymbolStringsOr =
      Parsed->getSectionContents(InputSections[SymbolStringIndex]);
  auto SymbolsOr = Parsed->symbols(&InputSymtab);
  if (!SectionStringsOr || !SymbolStringsOr || !SymbolsOr) {
    Error Cause = Error::success();
    if (!SectionStringsOr)
      Cause = joinErrors(std::move(Cause), SectionStringsOr.takeError());
    if (!SymbolStringsOr)
      Cause = joinErrors(std::move(Cause), SymbolStringsOr.takeError());
    if (!SymbolsOr)
      Cause = joinErrors(std::move(Cause), SymbolsOr.takeError());
    return joinErrors(invalidELF("a name table cannot be read"),
                      std::move(Cause));
  }
  const StringRef SectionStrings(
      reinterpret_cast<const char *>(SectionStringsOr->data()),
      SectionStringsOr->size());
  const StringRef SymbolStrings(
      reinterpret_cast<const char *>(SymbolStringsOr->data()),
      SymbolStringsOr->size());
  const ArrayRef<Sym> InputSymbols = *SymbolsOr;
  if (InputSymbols.size() > std::numeric_limits<uint32_t>::max())
    return invalidELF("symbol count exceeds the ELF64 relocation-index range");
  if (SectionStrings.empty() || SymbolStrings.empty() ||
      SectionStrings.front() != '\0' || SymbolStrings.front() != '\0' ||
      InputSymtab.sh_info > InputSymbols.size())
    return invalidELF("a name table or the local-symbol boundary is invalid");
  if (Error E = validateRetainedIndices(*Parsed, InputSections,
                                        *SymbolTableIndex, InputSymbols))
    return std::move(E);

  std::vector<std::string> InputSectionNames;
  InputSectionNames.reserve(InputSections.size());
  for (const Shdr &Section : InputSections) {
    auto Name = checkedStringAt(SectionStrings, Section.sh_name, "section");
    if (!Name)
      return Name.takeError();
    InputSectionNames.push_back(Name->str());
  }

  std::vector<SmallVector<char, 0>> InputContents(InputSections.size());
  for (size_t Index = 1; Index != InputSections.size(); ++Index) {
    if (InputSections[Index].sh_type == ELF::SHT_NOBITS)
      continue;
    auto Bytes = Parsed->getSectionContents(InputSections[Index]);
    if (!Bytes)
      return joinErrors(invalidELF("a section payload is out of bounds"),
                        Bytes.takeError());
    InputContents[Index].append(Bytes->begin(), Bytes->end());
  }

  constexpr uint32_t NoIndex = std::numeric_limits<uint32_t>::max();

  // DROP_DEBUG_INFO is a filtering policy, not permission to run a linker over
  // the object. Determine the closed set of debug-owned sections first;
  // section and symbol maps below then rewrite only the indices made stale by
  // removing that set.
  std::vector<bool> DropSection(InputSections.size(), false);
  std::vector<bool> DebugNameSelected(InputSections.size(), false);
  if (DropDebugInfo) {
    for (size_t Index = 1; Index != InputSections.size(); ++Index) {
      DebugNameSelected[Index] =
          ELFDebugSectionPolicy::isDebugSectionName(InputSectionNames[Index]);
      if (!DebugNameSelected[Index])
        continue;
      switch (debugNameDisposition(InputSections[Index])) {
      case DebugNameDisposition::DropCandidate:
        DropSection[Index] = true;
        break;
      case DebugNameDisposition::PreserveStructural:
        break;
      case DebugNameDisposition::Reject:
        return invalidELF(
            "a debug-named section carries runtime or structural semantics");
      }
    }
    if (DropSection[*SymbolTableIndex] || DropSection[SymbolStringIndex] ||
        DropSection[SectionStringIndex])
      return invalidELF("a required name table is classified as debug data");

    bool Changed = true;
    while (Changed) {
      Changed = false;
      for (size_t Index = 1; Index != InputSections.size(); ++Index) {
        if (DropSection[Index])
          continue;
        const Shdr &Section = InputSections[Index];
        const bool TargetWasDropped = (Section.sh_type == ELF::SHT_REL ||
                                       Section.sh_type == ELF::SHT_RELA) &&
                                      Section.sh_info < DropSection.size() &&
                                      DropSection[Section.sh_info];
        const bool InfoWasDropped =
            (Section.sh_flags & ELF::SHF_INFO_LINK) != 0 &&
            Section.sh_info < DropSection.size() &&
            DropSection[Section.sh_info];
        const bool LinkOrderTargetWasDropped =
            (Section.sh_flags & ELF::SHF_LINK_ORDER) != 0 &&
            Section.sh_link < DropSection.size() &&
            DropSection[Section.sh_link];
        if (!TargetWasDropped && !InfoWasDropped && !LinkOrderTargetWasDropped)
          continue;
        if ((Section.sh_flags & ELF::SHF_ALLOC) != 0)
          return invalidELF("DROP_DEBUG_INFO dependency closure reaches an "
                            "allocated section");
        if (Index == *SymbolTableIndex || Index == SymbolStringIndex ||
            Index == SectionStringIndex)
          return invalidELF(
              "a required table with a dropped dependency cannot be proven "
              "debug-owned");

        // A dependency on removed debug data is not evidence of debug
        // ownership. The only generic dependent with a format-defined,
        // complete ownership edge is a relocation table whose sh_info target
        // is being removed. Its
        // retained SHT_SYMTAB sh_link is required to interpret the payload and
        // is therefore explicitly allowed. Every other structural consumer is
        // retained and diagnosed instead of being silently erased.
        if (!TargetWasDropped)
          return invalidELF(
              "a propagated section cannot be proven debug-owned");
        if ((Section.sh_flags & ELF::SHF_LINK_ORDER) != 0 &&
            Section.sh_link != 0 && !DropSection[Section.sh_link])
          return invalidELF(
              "a propagated relocation retains SHF_LINK_ORDER semantics");
        DropSection[Index] = true;
        Changed = true;
      }

      for (size_t Index = 1; Index != InputSections.size(); ++Index) {
        if (DropSection[Index] ||
            InputSections[Index].sh_type != ELF::SHT_GROUP)
          continue;
        auto Words = Parsed->template getSectionContentsAsArray<Word>(
            InputSections[Index]);
        if (!Words)
          return Words.takeError();
        const bool HasDroppedMember =
            llvm::any_of(Words->drop_front(), [&](Word Member) {
              return DropSection[static_cast<uint32_t>(Member)];
            });
        const bool HasRetainedMember =
            llvm::any_of(Words->drop_front(), [&](Word Member) {
              return !DropSection[static_cast<uint32_t>(Member)];
            });
        if (!HasDroppedMember || HasRetainedMember)
          continue;
        if ((InputSections[Index].sh_flags & ELF::SHF_ALLOC) != 0)
          return invalidELF("DROP_DEBUG_INFO dependency closure reaches an "
                            "allocated section");
        if ((InputSections[Index].sh_flags &
             (ELF::SHF_INFO_LINK | ELF::SHF_LINK_ORDER)) != 0)
          return invalidELF(
              "a propagated SHT_GROUP cannot be proven debug-owned");
        DropSection[Index] = true;
        Changed = true;
      }
    }

    for (size_t Index = 1; Index != InputSections.size(); ++Index)
      if (DropSection[Index] &&
          (InputSections[Index].sh_flags & ELF::SHF_ALLOC) != 0)
        return invalidELF(
            "DROP_DEBUG_INFO dependency closure reaches an allocated section");
    if (DropSection[*SymbolTableIndex] || DropSection[SymbolStringIndex] ||
        DropSection[SectionStringIndex])
      return invalidELF(
          "DROP_DEBUG_INFO dependency closure reaches a required table");

    for (size_t Index = 1; Index != InputSections.size(); ++Index) {
      if (!DebugNameSelected[Index] || !DropSection[Index])
        continue;
      const Shdr &Section = InputSections[Index];
      if ((Section.sh_type == ELF::SHT_REL ||
           Section.sh_type == ELF::SHT_RELA) &&
          !DropSection[Section.sh_info])
        return invalidELF(
            "a debug-named relocation section targets a retained section");
      if ((Section.sh_flags & ELF::SHF_INFO_LINK) != 0 &&
          Section.sh_info != 0 && !DropSection[Section.sh_info])
        return invalidELF(
            "a debug-named SHF_INFO_LINK section targets a retained section");
      if ((Section.sh_flags & ELF::SHF_LINK_ORDER) != 0 &&
          Section.sh_link != 0 && !DropSection[Section.sh_link])
        return invalidELF(
            "a debug-named SHF_LINK_ORDER section targets a retained section");
    }

    std::vector<uint32_t> GroupOwner(InputSections.size(), NoIndex);
    for (size_t GroupIndex = 1; GroupIndex != InputSections.size();
         ++GroupIndex) {
      if (InputSections[GroupIndex].sh_type != ELF::SHT_GROUP)
        continue;
      auto Words = Parsed->template getSectionContentsAsArray<Word>(
          InputSections[GroupIndex]);
      if (!Words)
        return Words.takeError();
      for (Word Member : Words->drop_front())
        GroupOwner[static_cast<uint32_t>(Member)] =
            static_cast<uint32_t>(GroupIndex);
    }
    for (size_t Index = 1; Index != InputSections.size(); ++Index) {
      if (DropSection[Index] ||
          (InputSections[Index].sh_flags & ELF::SHF_GROUP) == 0)
        continue;
      if (GroupOwner[Index] == NoIndex || DropSection[GroupOwner[Index]])
        return invalidELF(
            "a dropped SHT_GROUP still owns a retained SHF_GROUP member");
    }
  }

  std::vector<uint32_t> SectionMap(InputSections.size(), NoIndex);
  SmallVector<Shdr, 0> Sections;
  std::vector<SmallVector<char, 0>> Contents;
  std::vector<std::string> SectionNames;
  Sections.reserve(InputSections.size() + (SharedTable ? 1 : 0));
  Contents.reserve(InputSections.size() + (SharedTable ? 1 : 0));
  SectionNames.reserve(InputSections.size() + (SharedTable ? 1 : 0));
  for (size_t OldIndex = 0; OldIndex != InputSections.size(); ++OldIndex) {
    if (DropSection[OldIndex])
      continue;
    SectionMap[OldIndex] = static_cast<uint32_t>(Sections.size());
    Sections.push_back(InputSections[OldIndex]);
    Contents.push_back(InputContents[OldIndex]);
    SectionNames.push_back(InputSectionNames[OldIndex]);
  }

  auto mappedSection = [&](uint32_t OldIndex,
                           StringRef Context) -> Expected<uint32_t> {
    if (OldIndex >= SectionMap.size() || SectionMap[OldIndex] == NoIndex)
      return invalidELF(Context + " refers to a removed section");
    return SectionMap[OldIndex];
  };
  for (size_t OldIndex = 1; OldIndex != InputSections.size(); ++OldIndex) {
    if (DropSection[OldIndex])
      continue;
    Shdr &Section = Sections[SectionMap[OldIndex]];
    if (Section.sh_link != 0) {
      auto Link = mappedSection(Section.sh_link, "sh_link");
      if (!Link)
        return Link.takeError();
      Section.sh_link = *Link;
    }
    if (Section.sh_type == ELF::SHT_REL || Section.sh_type == ELF::SHT_RELA ||
        (Section.sh_flags & ELF::SHF_INFO_LINK) != 0) {
      auto Info = mappedSection(Section.sh_info, "sh_info");
      if (!Info)
        return Info.takeError();
      Section.sh_info = *Info;
    }
  }

  auto OutputSymbolTableIndexOr =
      mappedSection(*SymbolTableIndex, "required SHT_SYMTAB");
  if (!OutputSymbolTableIndexOr)
    return OutputSymbolTableIndexOr.takeError();
  auto OutputSymbolStringIndexOr =
      mappedSection(SymbolStringIndex, "required symbol string table");
  if (!OutputSymbolStringIndexOr)
    return OutputSymbolStringIndexOr.takeError();
  auto OutputSectionStringIndexOr =
      mappedSection(SectionStringIndex, "required section string table");
  if (!OutputSectionStringIndexOr)
    return OutputSectionStringIndexOr.takeError();
  const uint32_t OutputSymbolTableIndex = *OutputSymbolTableIndexOr;
  uint32_t OutputSymbolStringIndex = *OutputSymbolStringIndexOr;
  uint32_t OutputSectionStringIndex = *OutputSectionStringIndexOr;

  ArrayRef<Word> InputExtendedIndices;
  std::optional<uint32_t> InputExtendedIndexSection;
  for (uint32_t Index = 1; Index != InputSections.size(); ++Index) {
    if (InputSections[Index].sh_type != ELF::SHT_SYMTAB_SHNDX)
      continue;
    auto Values =
        Parsed->template getSectionContentsAsArray<Word>(InputSections[Index]);
    if (!Values)
      return Values.takeError();
    InputExtendedIndices = *Values;
    InputExtendedIndexSection = Index;
  }

  std::vector<uint32_t> ResolvedSymbolSections(InputSymbols.size(), NoIndex);
  std::vector<bool> DropSymbol(InputSymbols.size(), false);
  for (size_t Index = 0; Index != InputSymbols.size(); ++Index) {
    const Sym &Symbol = InputSymbols[Index];
    uint32_t OldSection = NoIndex;
    if (Symbol.st_shndx == ELF::SHN_XINDEX)
      OldSection = InputExtendedIndices[Index];
    else if (Symbol.st_shndx != ELF::SHN_UNDEF &&
             Symbol.st_shndx < ELF::SHN_LORESERVE)
      OldSection = Symbol.st_shndx;
    ResolvedSymbolSections[Index] = OldSection;
    DropSymbol[Index] =
        DropDebugInfo && OldSection != NoIndex && DropSection[OldSection];
  }
  if (!DropSymbol.empty() && DropSymbol.front())
    return invalidELF("the null symbol cannot be removed");

  bool RetainsExtendedIndexSymbol = false;
  for (size_t Index = 0; Index != InputSymbols.size(); ++Index)
    RetainsExtendedIndexSymbol |=
        !DropSymbol[Index] && InputSymbols[Index].st_shndx == ELF::SHN_XINDEX;
  if (RetainsExtendedIndexSymbol &&
      (!InputExtendedIndexSection || DropSection[*InputExtendedIndexSection] ||
       SectionMap[*InputExtendedIndexSection] == NoIndex))
    return invalidELF(
        "a retained SHN_XINDEX symbol lost its SHT_SYMTAB_SHNDX companion");

  std::vector<uint32_t> SymbolMap(InputSymbols.size(), NoIndex);
  size_t OutputSymbolCount = 0;
  for (size_t Index = 0; Index != InputSymbols.size(); ++Index)
    if (!DropSymbol[Index])
      SymbolMap[Index] = static_cast<uint32_t>(OutputSymbolCount++);

  StringTableBuilder SymbolStringsOut;
  SmallVector<char, 0> SymbolTableBytes;
  if (OutputSymbolCount > std::numeric_limits<size_t>::max() / sizeof(Sym))
    return invalidELF("the rebuilt symbol table exceeds the host range");
  SymbolTableBytes.reserve(OutputSymbolCount * sizeof(Sym));
  uint32_t OutputLocalCount = 0;
  for (size_t Index = 0; Index != InputSymbols.size(); ++Index) {
    if (DropSymbol[Index])
      continue;
    const Sym &InputSymbol = InputSymbols[Index];
    auto Name = checkedStringAt(SymbolStrings, InputSymbol.st_name, "symbol");
    if (!Name)
      return Name.takeError();
    auto NameOffset = SymbolStringsOut.add(*Name);
    if (!NameOffset)
      return NameOffset.takeError();
    Sym OutputSymbol = InputSymbol;
    OutputSymbol.st_name = *NameOffset;
    const uint32_t OldSection = ResolvedSymbolSections[Index];
    if (OldSection != NoIndex) {
      auto NewSection = mappedSection(OldSection, "st_shndx");
      if (!NewSection)
        return NewSection.takeError();
      if (InputSymbol.st_shndx != ELF::SHN_XINDEX) {
        if (*NewSection >= ELF::SHN_LORESERVE)
          return invalidELF(
              "a direct symbol index now requires SHT_SYMTAB_SHNDX");
        OutputSymbol.st_shndx = static_cast<uint16_t>(*NewSection);
      }
    }
    if (OutputSymbol.getBinding() == ELF::STB_LOCAL)
      ++OutputLocalCount;
    appendRecord(SymbolTableBytes, OutputSymbol);
  }
  Contents[OutputSymbolTableIndex] = std::move(SymbolTableBytes);
  Sections[OutputSymbolTableIndex].sh_info = OutputLocalCount;
  Contents[OutputSymbolStringIndex].assign(SymbolStringsOut.bytes().begin(),
                                           SymbolStringsOut.bytes().end());
  SectionNames[OutputSymbolStringIndex] = ".strtab";

  const bool RemovedSymbols = OutputSymbolCount != InputSymbols.size();
  bool RebuiltExtendedIndexSection = false;
  for (size_t OldIndex = 1; OldIndex != InputSections.size(); ++OldIndex) {
    if (DropSection[OldIndex])
      continue;
    const Shdr &InputSection = InputSections[OldIndex];
    Shdr &OutputSection = Sections[SectionMap[OldIndex]];
    switch (InputSection.sh_type) {
    case ELF::SHT_GROUP: {
      if (SymbolMap[InputSection.sh_info] == NoIndex)
        return invalidELF("a retained SHT_GROUP lost its signature symbol");
      OutputSection.sh_info = SymbolMap[InputSection.sh_info];
      auto InputWords =
          Parsed->template getSectionContentsAsArray<Word>(InputSection);
      if (!InputWords)
        return InputWords.takeError();
      SmallVector<char, 0> OutputWords;
      appendRecord(OutputWords, InputWords->front());
      for (Word Member : InputWords->drop_front()) {
        const uint32_t OldMember = Member;
        if (DropSection[OldMember])
          continue;
        Word NewMember(SectionMap[OldMember]);
        appendRecord(OutputWords, NewMember);
      }
      Contents[SectionMap[OldIndex]] = std::move(OutputWords);
      break;
    }
    case ELF::SHT_REL: {
      auto InputRelocations = Parsed->rels(InputSection);
      if (!InputRelocations)
        return InputRelocations.takeError();
      SmallVector<char, 0> OutputRelocations;
      for (Rel Relocation : *InputRelocations) {
        if (SymbolMap[Relocation.getSymbol()] == NoIndex)
          return invalidELF(
              "a retained SHT_REL refers to a removed debug symbol");
        Relocation.setSymbol(SymbolMap[Relocation.getSymbol()]);
        appendRecord(OutputRelocations, Relocation);
      }
      Contents[SectionMap[OldIndex]] = std::move(OutputRelocations);
      break;
    }
    case ELF::SHT_RELA: {
      auto InputRelocations = Parsed->relas(InputSection);
      if (!InputRelocations)
        return InputRelocations.takeError();
      SmallVector<char, 0> OutputRelocations;
      for (Rela Relocation : *InputRelocations) {
        if (SymbolMap[Relocation.getSymbol()] == NoIndex)
          return invalidELF(
              "a retained SHT_RELA refers to a removed debug symbol");
        Relocation.setSymbol(SymbolMap[Relocation.getSymbol()]);
        appendRecord(OutputRelocations, Relocation);
      }
      Contents[SectionMap[OldIndex]] = std::move(OutputRelocations);
      break;
    }
    case ELF::SHT_SYMTAB_SHNDX: {
      if (RebuiltExtendedIndexSection)
        return invalidELF(
            "more than one retained SHT_SYMTAB_SHNDX companion is present");
      RebuiltExtendedIndexSection = true;
      SmallVector<char, 0> OutputIndices;
      for (size_t SymbolIndex = 0; SymbolIndex != InputSymbols.size();
           ++SymbolIndex) {
        if (DropSymbol[SymbolIndex])
          continue;
        uint32_t Value = InputExtendedIndices[SymbolIndex];
        if (Value != 0) {
          auto NewSection = mappedSection(Value, "SHT_SYMTAB_SHNDX");
          if (!NewSection)
            return NewSection.takeError();
          Value = *NewSection;
        }
        Word NewValue(Value);
        appendRecord(OutputIndices, NewValue);
      }
      Contents[SectionMap[OldIndex]] = std::move(OutputIndices);
      break;
    }
    case ELF::SHT_LLVM_ADDRSIG:
      if (RemovedSymbols) {
        auto InputBytes = Parsed->getSectionContents(InputSection);
        if (!InputBytes)
          return InputBytes.takeError();
        SmallVector<char, 0> OutputBytes;
        raw_svector_ostream Encoded(OutputBytes);
        const uint8_t *Current = InputBytes->data();
        const uint8_t *End = Current + InputBytes->size();
        while (Current != End) {
          unsigned Length = 0;
          const char *DecodeError = nullptr;
          const uint64_t OldSymbol =
              decodeULEB128(Current, &Length, End, &DecodeError);
          if (DecodeError || Length == 0)
            return invalidELF("SHT_LLVM_ADDRSIG cannot be remapped");
          Current += Length;
          if (SymbolMap[OldSymbol] != NoIndex)
            encodeULEB128(SymbolMap[OldSymbol], Encoded);
        }
        Contents[SectionMap[OldIndex]] = std::move(OutputBytes);
      }
      break;
    case ELF::SHT_LLVM_CALL_GRAPH_PROFILE:
      // Its symbol references live in SHT_REL[A] and were remapped above.
      break;
    default:
      if (RemovedSymbols && InputSection.sh_link == *SymbolTableIndex &&
          InputSection.sh_type != ELF::SHT_SYMTAB)
        return invalidELF(
            "an opaque symbol-index section cannot be remapped safely");
      break;
    }
  }
  if (RetainsExtendedIndexSymbol && !RebuiltExtendedIndexSection)
    return invalidELF(
        "a retained SHN_XINDEX symbol has no rebuilt SHT_SYMTAB_SHNDX payload");

  if (SharedTable) {
    if (Sections.size() == std::numeric_limits<uint32_t>::max())
      return invalidELF("splitting the name tables overflows section indices");
    Shdr NewSection{};
    NewSection.sh_type = ELF::SHT_STRTAB;
    NewSection.sh_addralign = 1;
    Sections.push_back(NewSection);
    Contents.emplace_back();
    SectionNames.emplace_back(".shstrtab");
    OutputSectionStringIndex = static_cast<uint32_t>(Sections.size() - 1);
  } else {
    SectionNames[OutputSectionStringIndex] = ".shstrtab";
  }

  StringTableBuilder SectionStringsOut;
  for (size_t Index = 0; Index != Sections.size(); ++Index) {
    auto NameOffset = SectionStringsOut.add(SectionNames[Index]);
    if (!NameOffset)
      return NameOffset.takeError();
    Sections[Index].sh_name = *NameOffset;
  }
  Contents[OutputSectionStringIndex].assign(SectionStringsOut.bytes().begin(),
                                            SectionStringsOut.bytes().end());

  // Section zero carries extended-numbering values only while the matching
  // ELF header sentinel is in use. Input may use the extended spelling even
  // for a small table; once this output uses ordinary fields, stale sh_size or
  // sh_link values would make the null header noncanonical and ambiguous.
  Sections.front() = Shdr{};
  const bool ExtendedSectionCount = Sections.size() >= ELF::SHN_LORESERVE;
  const bool ExtendedSectionNames =
      OutputSectionStringIndex >= ELF::SHN_LORESERVE;
  if (ExtendedSectionCount)
    Sections.front().sh_size = Sections.size();
  if (ExtendedSectionNames)
    Sections.front().sh_link = OutputSectionStringIndex;

  SmallVector<char, 0> Output(sizeof(Ehdr), 0);
  for (size_t Index = 1; Index != Sections.size(); ++Index) {
    Shdr &Section = Sections[Index];
    auto Offset = alignedOffset(Output.size(), Section.sh_addralign);
    if (!Offset)
      return Offset.takeError();
    Section.sh_offset = *Offset;
    // SHT_NOBITS has a conceptual file offset but occupies no file bytes. Its
    // alignment therefore must not pad or advance the physical output cursor;
    // later file-backed sections and the section table continue at
    // Output.size().
    if (Section.sh_type == ELF::SHT_NOBITS)
      continue;
    if (Error E = appendPaddingTo(Output, *Offset))
      return std::move(E);
    Section.sh_size = Contents[Index].size();
    if (Contents[Index].size() >
        std::numeric_limits<size_t>::max() - Output.size())
      return invalidELF("section payloads exceed the host address range");
    Output.append(Contents[Index].begin(), Contents[Index].end());
  }

  auto SectionHeaderOffset = alignedOffset(Output.size(), alignof(Shdr));
  if (!SectionHeaderOffset)
    return SectionHeaderOffset.takeError();
  if (Error E = appendPaddingTo(Output, *SectionHeaderOffset))
    return std::move(E);
  if (Sections.size() >
      (std::numeric_limits<size_t>::max() - Output.size()) / sizeof(Shdr))
    return invalidELF("section headers exceed the host address range");
  for (const Shdr &Section : Sections)
    appendRecord(Output, Section);

  Ehdr OutputHeader = InputHeader;
  OutputHeader.e_shoff = *SectionHeaderOffset;
  OutputHeader.e_shnum =
      ExtendedSectionCount ? 0 : static_cast<uint16_t>(Sections.size());
  OutputHeader.e_shstrndx =
      ExtendedSectionNames ? ELF::SHN_XINDEX
                           : static_cast<uint16_t>(OutputSectionStringIndex);
  std::memcpy(Output.data(), &OutputHeader, sizeof(OutputHeader));
  return Output;
}

} // namespace neverc::plugin
