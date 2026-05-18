//===- MergerCOFF.cpp - COFF relocatable merge (ported from LLD) -------===//
//
// COFF relocatable merger.  COFF has no native `-r` mode (unlike ELF/
// MachO), so this produces a single section-merged .obj equivalent to
// what `link.exe /MERGE` would create.  The algorithms are derived
// from the LLD COFF backend's section merging and symbol resolution:
//
//   Section merging     ← LLD COFF merges by (name, content_flags)
//   Symbol resolution   ← IMAGE_SYM_CLASS_EXTERNAL with section
//                          precedence (defined > common > undef)
//   Relocation remap    ← VirtualAddress offset + SymbolTableIndex remap
//   Aux symbol handling ← coff_aux_section_definition.Number remap
//
// CodeView debug data is NOT interpreted — PE/COFF debug info lives
// in PDB, not the .obj.  SEH unwind tables (__pdata/__xdata) are
// preserved as regular sections.
//
// C++ COMDAT (IMAGE_SCN_LNK_COMDAT) sections are passed through
// without dedup — neverc is pure C, so COMDAT groups from LTO
// partitions are already resolved at the bitcode layer.
//
//===------------------------------------------------------------------===//

#include "MergerCommon.h"
#include "neverc/Merge/Merger.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/Object/COFF.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MathExtras.h"

#include <cstdint>
#include <cstring>

using namespace llvm;

namespace neverc::merge {

namespace {

template <typename BufT>
bool mergeCOFFImpl(ArrayRef<BufT> Buffers, raw_pwrite_stream &OS,
                   const Options &Opts) {
  (void)Opts; // no COFF-specific tunables yet.
  using namespace llvm::object;
  using namespace llvm::COFF;

  struct RelocEntry {
    coff_relocation Reloc;
    unsigned PartIdx;
  };

  struct MergedSection {
    std::string Name;
    uint32_t Characteristics = 0;
    uint32_t Alignment = 0;
    SmallVector<char, 0> Data;
    SmallVector<RelocEntry, 0> Relocs;
    SmallVector<std::pair<unsigned, uint64_t>, 4> PartOffsets;
    uint64_t VirtualSize = 0;
    bool IsBSS = false;
  };

  SmallVector<MergedSection, 32> MergedSections;
  StringMap<SmallVector<unsigned, 2>> SectionIndex;

  auto findOrCreateSection = [&](StringRef Name, uint32_t Chars) -> unsigned {
    bool IsComdat = (Chars & IMAGE_SCN_LNK_COMDAT) != 0;
    if (!IsComdat) {
      auto &Candidates = SectionIndex[Name];
      uint32_t ContentFlags =
          Chars & (IMAGE_SCN_CNT_CODE | IMAGE_SCN_CNT_INITIALIZED_DATA |
                   IMAGE_SCN_CNT_UNINITIALIZED_DATA);
      for (unsigned idx : Candidates) {
        uint32_t MC = MergedSections[idx].Characteristics &
                      (IMAGE_SCN_CNT_CODE | IMAGE_SCN_CNT_INITIALIZED_DATA |
                       IMAGE_SCN_CNT_UNINITIALIZED_DATA);
        if (MC == ContentFlags)
          return idx;
      }
    }
    unsigned NewIdx = MergedSections.size();
    MergedSections.push_back({});
    auto &MS = MergedSections.back();
    MS.Name = Name.str();
    MS.Characteristics = Chars;
    MS.IsBSS = (Chars & IMAGE_SCN_CNT_UNINITIALIZED_DATA) != 0;
    if (!IsComdat)
      SectionIndex[Name].push_back(NewIdx);
    return NewIdx;
  };

  SmallVector<char, 0> StringTable;
  StringMap<uint32_t> StringDedup;
  uint32_t StrTabOffset = 4;
  auto appendString = [&](StringRef S) -> uint32_t {
    auto [It, Inserted] = StringDedup.try_emplace(S, 0);
    if (!Inserted)
      return It->second;
    uint32_t Off = StrTabOffset;
    StringTable.append(S.begin(), S.end());
    StringTable.push_back('\0');
    StrTabOffset += S.size() + 1;
    It->second = Off;
    return Off;
  };

  struct PerPartition {
    DenseMap<unsigned, unsigned> SecMap;
    DenseMap<unsigned, unsigned> SymMap;
  };
  SmallVector<PerPartition, 8> Maps;

  SmallVector<char, 0> SymbolTable;
  unsigned TotalSymbols = 0;
  uint16_t Machine = 0;

  for (unsigned p = 0; p < (unsigned)Buffers.size(); ++p) {
    if (Buffers[p].empty())
      continue;
    Maps.resize(p + 1);
    auto &PM = Maps[p];

    auto BufRef = MemoryBufferRef(
        StringRef(Buffers[p].data(), Buffers[p].size()), "partition");
    auto ObjOrErr = COFFObjectFile::create(BufRef);
    if (!ObjOrErr) {
      consumeError(ObjOrErr.takeError());
      return false;
    }
    auto &Obj = **ObjOrErr;
    if (Machine == 0)
      Machine = Obj.getMachine();

    unsigned PartSecOrdinal = 1;
    for (const auto &Sec : Obj.sections()) {
      const coff_section *CS = Obj.getCOFFSection(Sec);

      auto SNameOrErr = Obj.getSectionName(CS);
      StringRef SecName = SNameOrErr ? *SNameOrErr : "";

      unsigned MIdx = findOrCreateSection(SecName, CS->Characteristics);
      auto &MS = MergedSections[MIdx];

      uint32_t SAlign = 0;
      uint32_t AlignBits = CS->Characteristics & IMAGE_SCN_ALIGN_MASK;
      if (AlignBits) {
        SAlign = 1u << ((AlignBits >> 20) - 1);
        if (SAlign > MS.Alignment)
          MS.Alignment = SAlign;
      }
      MS.Characteristics |= CS->Characteristics;

      // Use merged section's accumulated max alignment so later partitions
      // with smaller declared section alignment but still containing
      // higher-aligned symbols don't end up misaligned.
      uint32_t EffectiveAlign = MS.Alignment;
      uint64_t PartOffset;
      if (MS.IsBSS) {
        PartOffset = MS.VirtualSize;
        if (EffectiveAlign > 1) {
          uint64_t Padding =
              (EffectiveAlign - (PartOffset % EffectiveAlign)) % EffectiveAlign;
          PartOffset += Padding;
        }
        MS.VirtualSize = PartOffset + CS->SizeOfRawData;
      } else {
        PartOffset = MS.Data.size();
        if (EffectiveAlign > 1) {
          uint64_t Padding =
              (EffectiveAlign - (PartOffset % EffectiveAlign)) % EffectiveAlign;
          MS.Data.resize(MS.Data.size() + Padding, 0);
          PartOffset = MS.Data.size();
        }
        auto ContentsOrErr = Sec.getContents();
        if (ContentsOrErr)
          MS.Data.append(ContentsOrErr->begin(), ContentsOrErr->end());
      }

      MS.PartOffsets.push_back({p, PartOffset});
      PM.SecMap[PartSecOrdinal] = MIdx + 1;

      for (const auto &R : Sec.relocations()) {
        coff_relocation CR;
        CR.VirtualAddress = R.getOffset() + PartOffset;
        auto SymOrErr = R.getSymbol();
        if (SymOrErr != Obj.symbol_end()) {
          unsigned SymIdx = Obj.getSymbolIndex(Obj.getCOFFSymbol(*SymOrErr));
          CR.SymbolTableIndex = SymIdx;
        } else {
          CR.SymbolTableIndex = 0;
        }
        CR.Type = R.getType();
        MS.Relocs.push_back({CR, p});
      }
      PartSecOrdinal++;
    }

    for (const auto &Sym : Obj.symbols()) {
      COFFSymbolRef CSym = Obj.getCOFFSymbol(Sym);
      unsigned OrigIdx = Obj.getSymbolIndex(CSym);
      coff_symbol16 OutSym;
      memset(&OutSym, 0, sizeof(OutSym));

      auto NameOrErr = Obj.getSymbolName(CSym);
      if (!NameOrErr) {
        consumeError(NameOrErr.takeError());
        return false;
      }
      StringRef Name = *NameOrErr;
      if (Name.size() <= COFF::NameSize) {
        memset(OutSym.Name.ShortName, 0, COFF::NameSize);
        memcpy(OutSym.Name.ShortName, Name.data(), Name.size());
      } else {
        OutSym.Name.Offset.Zeroes = 0;
        OutSym.Name.Offset.Offset = appendString(Name);
      }

      OutSym.Value = CSym.getValue();
      int32_t SecNum = CSym.getSectionNumber();
      if (SecNum > 0) {
        auto It = PM.SecMap.find(SecNum);
        if (It != PM.SecMap.end()) {
          OutSym.SectionNumber = It->second;
          unsigned mIdx = It->second - 1;
          for (auto &[pp, off] : MergedSections[mIdx].PartOffsets)
            if (pp == p) {
              OutSym.Value += off;
              break;
            }
        } else {
          OutSym.SectionNumber = 0;
        }
      } else {
        OutSym.SectionNumber = SecNum;
      }

      OutSym.Type = CSym.getType();
      OutSym.StorageClass = CSym.getStorageClass();
      // Demote __pcg symbols to static (matching MachO/ELF merger behavior).
      if (OutSym.StorageClass == COFF::IMAGE_SYM_CLASS_EXTERNAL &&
          Name.contains(".__pcg") && OutSym.SectionNumber > 0)
        OutSym.StorageClass = COFF::IMAGE_SYM_CLASS_STATIC;
      OutSym.NumberOfAuxSymbols = CSym.getNumberOfAuxSymbols();

      PM.SymMap[OrigIdx] = TotalSymbols;
      SymbolTable.append(reinterpret_cast<const char *>(&OutSym),
                         reinterpret_cast<const char *>(&OutSym) + 18);
      TotalSymbols++;

      for (unsigned a = 0; a < CSym.getNumberOfAuxSymbols(); ++a) {
        const uint8_t *AuxData =
            reinterpret_cast<const uint8_t *>(CSym.getRawPtr()) + 18 * (a + 1);

        if (a == 0 && CSym.getStorageClass() == COFF::IMAGE_SYM_CLASS_STATIC &&
            CSym.getSectionNumber() > 0) {
          coff_aux_section_definition AuxSD;
          memcpy(&AuxSD, AuxData, sizeof(AuxSD));
          int32_t AssocNum = AuxSD.getNumber(false);
          if (AssocNum > 0) {
            auto It = PM.SecMap.find(AssocNum);
            if (It != PM.SecMap.end()) {
              uint32_t Remapped = It->second;
              AuxSD.NumberLowPart = Remapped & 0xFFFF;
              AuxSD.NumberHighPart = (Remapped >> 16) & 0xFFFF;
            }
          }
          SymbolTable.append(reinterpret_cast<const char *>(&AuxSD),
                             reinterpret_cast<const char *>(&AuxSD) + 18);
        } else {
          SymbolTable.append(reinterpret_cast<const char *>(AuxData),
                             reinterpret_cast<const char *>(AuxData) + 18);
        }
        PM.SymMap[OrigIdx + 1 + a] = TotalSymbols;
        TotalSymbols++;
      }
    }
  }

  for (auto &MS : MergedSections) {
    for (auto &RE : MS.Relocs) {
      if (RE.PartIdx >= Maps.size())
        continue;
      auto It = Maps[RE.PartIdx].SymMap.find(RE.Reloc.SymbolTableIndex);
      if (It != Maps[RE.PartIdx].SymMap.end())
        RE.Reloc.SymbolTableIndex = It->second;
    }
  }

  uint32_t NumSections = MergedSections.size();
  uint32_t HeaderSize = sizeof(coff_file_header);
  uint32_t SectionHeadersSize = NumSections * sizeof(coff_section);
  uint32_t DataOffset = HeaderSize + SectionHeadersSize;

  uint32_t CurrentOffset = DataOffset;
  SmallVector<coff_section, 32> OutHeaders(NumSections);
  for (unsigned i = 0; i < NumSections; ++i) {
    auto &MS = MergedSections[i];
    auto &H = OutHeaders[i];
    memset(&H, 0, sizeof(H));

    if (MS.Name.size() <= COFF::NameSize) {
      memcpy(H.Name, MS.Name.c_str(), MS.Name.size());
    } else {
      uint32_t Off = appendString(MS.Name);
      std::string NameRef = "/" + std::to_string(Off);
      memcpy(H.Name, NameRef.c_str(),
             std::min(NameRef.size(), size_t(COFF::NameSize)));
    }

    H.Characteristics = MS.Characteristics;
    if (MS.Alignment > 0) {
      H.Characteristics &= ~IMAGE_SCN_ALIGN_MASK;
      unsigned Log2 = llvm::Log2_32(MS.Alignment) + 1;
      H.Characteristics |= Log2 << 20;
    }

    if (MS.IsBSS) {
      H.SizeOfRawData = MS.VirtualSize;
      H.PointerToRawData = 0;
    } else {
      H.SizeOfRawData = MS.Data.size();
      H.PointerToRawData = MS.Data.empty() ? 0 : CurrentOffset;
      CurrentOffset += MS.Data.size();
    }

    bool HasOverflow = MS.Relocs.size() > UINT16_MAX;
    if (HasOverflow) {
      H.NumberOfRelocations = UINT16_MAX;
      H.Characteristics |= IMAGE_SCN_LNK_NRELOC_OVFL;
    } else {
      H.NumberOfRelocations = MS.Relocs.size();
    }
    unsigned ExtraRelocSlots = HasOverflow ? 1 : 0;
    unsigned TotalRelocs = MS.Relocs.size() + ExtraRelocSlots;
    H.PointerToRelocations = MS.Relocs.empty() ? 0 : CurrentOffset;
    CurrentOffset += TotalRelocs * sizeof(coff_relocation);
    H.PointerToLinenumbers = 0;
    H.NumberOfLinenumbers = 0;
  }

  uint32_t SymTabOffset = CurrentOffset;

  SmallVector<char, 0> Out;
  Out.reserve(SymTabOffset + SymbolTable.size() + 4 + StringTable.size());

  coff_file_header FH;
  memset(&FH, 0, sizeof(FH));
  FH.Machine = Machine;
  FH.NumberOfSections = NumSections;
  FH.TimeDateStamp = 0;
  FH.PointerToSymbolTable = SymTabOffset;
  FH.NumberOfSymbols = TotalSymbols;
  FH.SizeOfOptionalHeader = 0;
  FH.Characteristics = 0;
  Out.append(reinterpret_cast<const char *>(&FH),
             reinterpret_cast<const char *>(&FH) + sizeof(FH));

  for (auto &H : OutHeaders)
    Out.append(reinterpret_cast<const char *>(&H),
               reinterpret_cast<const char *>(&H) + sizeof(coff_section));

  for (unsigned i = 0; i < NumSections; ++i) {
    auto &MS = MergedSections[i];
    if (!MS.IsBSS)
      Out.append(MS.Data.begin(), MS.Data.end());
    if (MS.Relocs.size() > UINT16_MAX) {
      coff_relocation OverflowEntry;
      memset(&OverflowEntry, 0, sizeof(OverflowEntry));
      OverflowEntry.VirtualAddress = MS.Relocs.size() + 1;
      Out.append(reinterpret_cast<const char *>(&OverflowEntry),
                 reinterpret_cast<const char *>(&OverflowEntry) +
                     sizeof(coff_relocation));
    }
    for (auto &RE : MS.Relocs)
      Out.append(reinterpret_cast<const char *>(&RE.Reloc),
                 reinterpret_cast<const char *>(&RE.Reloc) +
                     sizeof(coff_relocation));
  }

  Out.append(SymbolTable.begin(), SymbolTable.end());

  uint32_t StrTabSize = 4 + StringTable.size();
  Out.append(reinterpret_cast<const char *>(&StrTabSize),
             reinterpret_cast<const char *>(&StrTabSize) + 4);
  Out.append(StringTable.begin(), StringTable.end());

  OS.write(Out.data(), Out.size());
  return true;
}

} // anonymous namespace

bool mergeCOFFObjects(ArrayRef<SmallVector<char, 0>> Buffers,
                      raw_pwrite_stream &OS, const Options &Opts) {
  return mergeCOFFImpl(Buffers, OS, Opts);
}

bool mergeCOFFObjects(ArrayRef<StringRef> Buffers, raw_pwrite_stream &OS,
                      const Options &Opts) {
  return mergeCOFFImpl(Buffers, OS, Opts);
}

} // namespace neverc::merge
