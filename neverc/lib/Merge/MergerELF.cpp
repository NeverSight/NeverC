//===- MergerELF.cpp - ELF64LE relocatable merge (ported from LLD -r) --===//
//
// Relocatable object merger for ELF64LE.  The algorithms here are ported
// from the LLD ELF backend's `-r` (partial link) code paths:
//
//   Section merging   ← Layout/OutputSections.cpp  (canMergeToProgbits,
//                        commitSection, section (name,type,flags) grouping)
//   Symbol resolution ← Symbols/Symbols.cpp        (GLOBAL>WEAK>UNDEF,
//                        SHN_COMMON preserved, binding kept as-is in -r)
//   Relocation remap  ← Emit/ElfImageEmitter.cpp   (copyRelocs path,
//                        no scanRelocations in -r, just index remap)
//   Output writing    ← Emit/ElfImageEmitter.cpp   (ET_REL, addr=0,
//                        no PHDR, section symbols via addSectionSymbols)
//   .eh_frame         ← kept as regular section in -r (LLD skips
//                        ehFrame synthesis when config->relocatable)
//
// C++ specific logic (COMDAT groups, SHT_GROUP, .gnu.linkonce.*,
// template instantiation dedup) is intentionally removed — neverc is
// a pure-C compiler.  .eh_frame is preserved as a normal section so
// that C unwinding / stack traces work correctly.
//
//===------------------------------------------------------------------===//

#include "MergerCommon.h"
#include "neverc/Merge/Merger.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Support/Error.h"

#include <cstdint>
#include <cstring>

using namespace llvm;

namespace neverc::merge {

namespace {

// ---------------------------------------------------------------------------
// Section merge compatibility — ported from LLD OutputSections.cpp
// ---------------------------------------------------------------------------

// LLD allows merging several section types into SHT_PROGBITS when they
// end up in the same output section.  This mirrors `canMergeToProgbits()`
// in Layout/OutputSections.cpp.
bool canMergeToProgbits(uint32_t type, uint16_t machine) {
  using namespace llvm::ELF;
  return type == SHT_NOBITS || type == SHT_PROGBITS || type == SHT_INIT_ARRAY ||
         type == SHT_PREINIT_ARRAY || type == SHT_FINI_ARRAY ||
         type == SHT_NOTE ||
         (type == SHT_X86_64_UNWIND && machine == EM_X86_64);
}

// Two input sections can share an output section when:
//   1) Same name
//   2) Compatible types (both progbits-mergeable, or identical type)
//   3) Same flag set (modulo SHF_GROUP which we strip for pure C)
//
// Ported from the section matching logic in LLD's LinkerScript.cpp
// (addSection / getOutputSectionName) and OutputSections.cpp.
bool sectionsCompatible(uint32_t typeA, uint64_t flagsA, uint32_t typeB,
                        uint64_t flagsB, uint16_t machine) {
  if (typeA == typeB)
    return flagsA == flagsB;
  if (canMergeToProgbits(typeA, machine) && canMergeToProgbits(typeB, machine))
    return flagsA == flagsB;
  return false;
}

// ---------------------------------------------------------------------------
// Symbol resolution priority — ported from LLD Symbols/Symbols.cpp
// ---------------------------------------------------------------------------

// In -r mode LLD preserves symbol bindings as-is but still resolves
// duplicates: a defined GLOBAL beats a WEAK which beats an UNDEF.
// This priority mirrors Symbol::resolve() for the relocatable case.
enum SymPriority : uint8_t {
  PRI_UNDEF = 0,
  PRI_COMMON = 1,
  PRI_WEAK_DEF = 2,
  PRI_GLOBAL_DEF = 3,
};

template <typename SymT> SymPriority getSymPriority(const SymT &S) {
  using namespace llvm::ELF;
  bool isDefined = S.st_shndx != SHN_UNDEF;
  bool isCommon = S.st_shndx == SHN_COMMON;
  bool isWeak = S.getBinding() == STB_WEAK;

  if (!isDefined && !isCommon)
    return PRI_UNDEF;
  if (isCommon)
    return PRI_COMMON;
  if (isWeak)
    return PRI_WEAK_DEF;
  return PRI_GLOBAL_DEF;
}

// ---------------------------------------------------------------------------
// Core merger implementation
// ---------------------------------------------------------------------------

template <typename BufT>
bool mergeELF64LEImpl(ArrayRef<BufT> Buffers, raw_pwrite_stream &OS,
                      const Options &Opts) {
  using namespace llvm::object;
  using namespace llvm::ELF;
  using ELFT = ELF64LE;
  using Ehdr = typename ELFT::Ehdr;
  using Shdr = typename ELFT::Shdr;
  using Sym = typename ELFT::Sym;
  using Rela = typename ELFT::Rela;

  detail::DedupStrTab ShStrTab, SymStrTab;

  struct RelocEntry {
    Rela Entry;
    unsigned PartIdx;
  };

  struct MergedSection {
    std::string Name;
    Shdr Template;
    SmallVector<char, 0> Data;
    SmallVector<RelocEntry, 0> Relocs;
    SmallVector<std::pair<unsigned, uint64_t>, 4> PartOffsets;
    uint64_t VirtualSize = 0;
  };
  SmallVector<MergedSection, 32> MergedSections;
  StringMap<SmallVector<unsigned, 2>> SectionIndex;

  uint16_t Machine = 0;
  uint32_t EFlags = 0;
  unsigned char OSABI = 0, ABIVer = 0;

  // Section merge: group by (name, compatible_type, flags).
  // Ported from LLD LinkerScript::addSection + OutputSections.cpp.
  auto findOrCreateSection = [&](StringRef Name, const Shdr &S) -> unsigned {
    auto &Candidates = SectionIndex[Name];
    uint64_t Flags = S.sh_flags & ~(uint64_t)SHF_GROUP;
    for (unsigned idx : Candidates)
      if (sectionsCompatible(MergedSections[idx].Template.sh_type,
                             MergedSections[idx].Template.sh_flags, S.sh_type,
                             Flags, Machine))
        return idx;
    MergedSection MS;
    MS.Name = Name.str();
    MS.Template = S;
    MS.Template.sh_flags = Flags;
    MS.Template.sh_link = 0;
    MS.Template.sh_info = 0;
    unsigned NewIdx = MergedSections.size();
    MergedSections.push_back(std::move(MS));
    Candidates.push_back(NewIdx);
    return NewIdx;
  };

  struct PerPartition {
    DenseMap<unsigned, unsigned> SecMap;
    DenseMap<unsigned, unsigned> SymMap;
  };
  SmallVector<PerPartition, 8> Maps;

  // Symbol table: locals first, then globals (ELF convention).
  // In -r mode LLD does NOT recompute bindings (ElfImageEmitter.cpp:1829).
  SmallVector<Sym, 64> LocalSyms, GlobalSyms;
  LocalSyms.push_back({});
  memset(&LocalSyms[0], 0, sizeof(Sym));

  // Global symbol dedup: ported from LLD SymbolTable::insert +
  // Symbol::resolve.  In -r mode, GLOBAL>WEAK>UNDEF priority, and
  // SHN_COMMON is preserved (not converted to BSS).
  struct GlobalDedup {
    unsigned SlotIdx; // index into GlobalSyms
    SymPriority Pri;
  };
  StringMap<GlobalDedup> GlobalMap;

  for (unsigned p = 0; p < Buffers.size(); ++p) {
    if (Buffers[p].empty())
      continue;
    Maps.resize(p + 1);
    auto &PM = Maps[p];
    PM.SecMap[0] = 0;

    auto Buf = MemoryBufferRef(StringRef(Buffers[p].data(), Buffers[p].size()),
                               "part");
    auto ObjOrErr = ELFObjectFile<ELFT>::create(Buf);
    if (!ObjOrErr) {
      consumeError(ObjOrErr.takeError());
      return false;
    }
    const auto &EF = ObjOrErr->getELFFile();
    const Ehdr &Hdr = EF.getHeader();
    if (Machine == 0) {
      Machine = Hdr.e_machine;
      EFlags = Hdr.e_flags;
      OSABI = Hdr.e_ident[EI_OSABI];
      ABIVer = Hdr.e_ident[EI_ABIVERSION];
    }

    auto SecsOrErr = EF.sections();
    if (!SecsOrErr) {
      consumeError(SecsOrErr.takeError());
      return false;
    }
    ArrayRef<Shdr> Secs = *SecsOrErr;

    const Shdr *SymTabHdr = nullptr;
    StringRef SymStr;
    for (const Shdr &S : Secs) {
      if (S.sh_type == SHT_SYMTAB) {
        SymTabHdr = &S;
        if (auto R = EF.getStringTableForSymtab(S))
          SymStr = *R;
        break;
      }
    }

    // ----- Phase 1: Merge sections -----
    // Skip metadata sections that are regenerated in the output.
    // SHT_GROUP is skipped because neverc is pure C — no COMDAT.
    // SHT_LLVM_ADDRSIG / SHT_LLVM_CALL_GRAPH_PROFILE are linker
    // metadata that don't survive -r.
    for (unsigned i = 1; i < Secs.size(); ++i) {
      const Shdr &S = Secs[i];
      if (S.sh_type == SHT_SYMTAB || S.sh_type == SHT_STRTAB ||
          S.sh_type == SHT_RELA || S.sh_type == SHT_REL ||
          S.sh_type == SHT_GROUP || S.sh_type == SHT_LLVM_ADDRSIG ||
          S.sh_type == SHT_LLVM_CALL_GRAPH_PROFILE)
        continue;

      auto NameOrErr = EF.getSectionName(S);
      StringRef SecName = NameOrErr ? *NameOrErr : "";

      if (Opts.dropDebugInfo &&
          (SecName.starts_with(".debug_") || SecName == ".debug" ||
           SecName.starts_with(".zdebug_")))
        continue;

      // SHT_NOTE: identical across partitions from the same source
      // module (LTO splits one module).  Dedup by keeping first copy.
      if (S.sh_type == SHT_NOTE && p > 0 && SectionIndex.count(SecName)) {
        PM.SecMap[i] = SectionIndex[SecName].front() + 1;
        continue;
      }

      Shdr SCopy = S;
      SCopy.sh_flags &= ~(uint64_t)SHF_GROUP;
      unsigned MIdx = findOrCreateSection(SecName, SCopy);
      auto &MS = MergedSections[MIdx];

      // Track max alignment (LLD: OutputSection::commitSection).
      if (S.sh_addralign > MS.Template.sh_addralign)
        MS.Template.sh_addralign = S.sh_addralign;

      // When types differ but are compatible (canMergeToProgbits),
      // promote to SHT_PROGBITS (LLD behavior).
      if (MS.Template.sh_type != S.sh_type &&
          canMergeToProgbits(MS.Template.sh_type, Machine) &&
          canMergeToProgbits(S.sh_type, Machine))
        MS.Template.sh_type = SHT_PROGBITS;

      uint64_t Align = MS.Template.sh_addralign;
      uint64_t PartOffset;
      if (S.sh_type == SHT_NOBITS) {
        PartOffset = MS.VirtualSize;
        if (Align > 1) {
          uint64_t Padding = (Align - (PartOffset % Align)) % Align;
          PartOffset += Padding;
        }
        MS.VirtualSize = PartOffset + S.sh_size;
      } else {
        PartOffset = MS.Data.size();
        if (Align > 1) {
          uint64_t Padding = (Align - (PartOffset % Align)) % Align;
          MS.Data.resize(MS.Data.size() + Padding, 0);
          PartOffset = MS.Data.size();
        }
        auto D = EF.getSectionContents(S);
        if (D)
          MS.Data.append(D->begin(), D->end());
      }

      MS.PartOffsets.push_back({p, PartOffset});
      PM.SecMap[i] = MIdx + 1;
    }

    // ----- Phase 2: Merge symbols -----
    // Ported from LLD finalizeSections → addSymbol.
    // Locals are concatenated; globals are deduped by name with
    // GLOBAL>WEAK>UNDEF priority (Symbol::resolve for -r).
    // SHN_COMMON symbols are preserved (LLD: getCommonSec returns
    // nullptr for non-relocated common symbols).
    if (SymTabHdr) {
      auto SymsOrErr = EF.symbols(SymTabHdr);
      if (!SymsOrErr) {
        consumeError(SymsOrErr.takeError());
        return false;
      }
      ArrayRef<Sym> Syms = *SymsOrErr;

      for (unsigned i = 1; i < Syms.size(); ++i) {
        Sym OutS = Syms[i];

        // Remap section index and adjust value.
        if (OutS.st_shndx < SHN_LORESERVE) {
          auto It = PM.SecMap.find(OutS.st_shndx);
          if (It != PM.SecMap.end()) {
            unsigned mIdx = It->second - 1;
            OutS.st_shndx = It->second;
            for (auto &[pp, off] : MergedSections[mIdx].PartOffsets) {
              if (pp == p) {
                OutS.st_value += off;
                break;
              }
            }
          } else {
            OutS.st_shndx = 0;
          }
        }
        // SHN_COMMON: preserved as-is in -r mode (LLD behavior).

        StringRef Name;
        if (OutS.st_name < SymStr.size())
          Name = SymStr.data() + OutS.st_name;
        OutS.st_name = SymStrTab.add(Name);

        if (Syms[i].getBinding() == STB_LOCAL) {
          PM.SymMap[i] = LocalSyms.size();
          LocalSyms.push_back(OutS);
        } else {
          // Global symbol dedup: same-name globals are resolved by
          // priority (defined GLOBAL > WEAK > COMMON > UNDEF).
          // Ported from LLD SymbolTable::insert / Symbol::resolve.
          SymPriority Pri = getSymPriority(Syms[i]);
          auto [It, Inserted] = GlobalMap.try_emplace(
              Name, GlobalDedup{(unsigned)GlobalSyms.size(), Pri});

          if (Inserted) {
            unsigned Slot = GlobalSyms.size();
            GlobalSyms.push_back(OutS);
            // Record slot; will add FirstGlobal offset after the loop.
            PM.SymMap[i] = Slot | 0x80000000u;
          } else {
            unsigned Slot = It->second.SlotIdx;
            if (Pri > It->second.Pri) {
              GlobalSyms[Slot] = OutS;
              It->second.Pri = Pri;
            }
            PM.SymMap[i] = Slot | 0x80000000u;
          }
        }
      }
    }

    // ----- Phase 3: Collect relocations -----
    // In -r mode LLD skips scanRelocations() entirely
    // (ElfImageEmitter.cpp:1773) and just copies relocs with remapped
    // indices (copyRelocs path).
    for (unsigned i = 1; i < Secs.size(); ++i) {
      if (Secs[i].sh_type != SHT_RELA && Secs[i].sh_type != SHT_REL)
        continue;
      auto TargetIt = PM.SecMap.find(Secs[i].sh_info);
      if (TargetIt == PM.SecMap.end())
        continue;
      unsigned targetMIdx = TargetIt->second - 1;
      uint64_t dataOff = 0;
      for (auto &[pp, off] : MergedSections[targetMIdx].PartOffsets)
        if (pp == p) {
          dataOff = off;
          break;
        }

      if (Secs[i].sh_type == SHT_RELA) {
        auto R = EF.relas(Secs[i]);
        if (!R) {
          consumeError(R.takeError());
          continue;
        }
        for (const Rela &Re : *R) {
          Rela Adjusted = Re;
          Adjusted.r_offset += dataOff;
          MergedSections[targetMIdx].Relocs.push_back({Adjusted, p});
        }
      } else {
        using Rel = typename ELFT::Rel;
        auto R = EF.rels(Secs[i]);
        if (!R) {
          consumeError(R.takeError());
          continue;
        }
        for (const Rel &Re : *R) {
          Rela Adjusted;
          Adjusted.r_offset = Re.r_offset + dataOff;
          Adjusted.setSymbolAndType(Re.getSymbol(), Re.getType());
          Adjusted.r_addend = 0;
          MergedSections[targetMIdx].Relocs.push_back({Adjusted, p});
        }
      }
    }
  }

  // ----- Finalize global symbol indices -----
  // During the loop, global SymMap entries store (slot | 0x80000000).
  // Now that FirstGlobal is known, resolve to actual indices.
  unsigned FirstGlobal = LocalSyms.size();
  for (auto &PM : Maps)
    for (auto &[K, V] : PM.SymMap)
      if (V & 0x80000000u)
        V = FirstGlobal + (V & 0x7FFFFFFFu);

  // Remap relocation symbol indices.
  for (auto &MS : MergedSections) {
    for (auto &RE : MS.Relocs) {
      if (RE.PartIdx >= Maps.size())
        continue;
      unsigned origSym = RE.Entry.getSymbol();
      unsigned newSym = 0;
      auto It = Maps[RE.PartIdx].SymMap.find(origSym);
      if (It != Maps[RE.PartIdx].SymMap.end())
        newSym = It->second;
      RE.Entry.setSymbolAndType(newSym, RE.Entry.getType());
    }
  }

  // ----- Demote __pcg symbols to local (matching MachO merger) -----
  // Symbols externalized only for cross-partition references (with the
  // .__pcg<hash> suffix) are an implementation detail of parallel codegen
  // and must not bloat the final binary's symbol table.  Convert defined
  // __pcg globals to STB_LOCAL and reorder the symbol table accordingly.
  {
    DenseSet<unsigned> DemoteSlots;
    for (unsigned i = 0; i < GlobalSyms.size(); ++i) {
      if (GlobalSyms[i].st_shndx == ELF::SHN_UNDEF ||
          GlobalSyms[i].st_shndx == ELF::SHN_COMMON)
        continue;
      StringRef Name(SymStrTab.Data.data() + GlobalSyms[i].st_name);
      if (Name.contains(".__pcg")) {
        GlobalSyms[i].setBinding(ELF::STB_LOCAL);
        DemoteSlots.insert(i);
      }
    }
    if (!DemoteSlots.empty()) {
      SmallVector<Sym, 64> NewLocals, NewGlobals;
      DenseMap<unsigned, unsigned> ReorderMap;
      // Identity-map existing locals.
      for (unsigned i = 0; i < LocalSyms.size(); ++i)
        ReorderMap[i] = i;
      NewLocals = std::move(LocalSyms);
      // Append demoted globals into the local section.
      for (unsigned i = 0; i < GlobalSyms.size(); ++i) {
        unsigned OldIdx = FirstGlobal + i;
        if (DemoteSlots.count(i)) {
          ReorderMap[OldIdx] = NewLocals.size();
          NewLocals.push_back(GlobalSyms[i]);
        }
      }
      unsigned NewFirstGlobal = NewLocals.size();
      for (unsigned i = 0; i < GlobalSyms.size(); ++i) {
        unsigned OldIdx = FirstGlobal + i;
        if (!DemoteSlots.count(i)) {
          ReorderMap[OldIdx] = NewFirstGlobal + NewGlobals.size();
          NewGlobals.push_back(GlobalSyms[i]);
        }
      }
      LocalSyms = std::move(NewLocals);
      GlobalSyms = std::move(NewGlobals);
      FirstGlobal = NewFirstGlobal;
      // Remap relocation symbol indices to match the new ordering.
      for (auto &MS : MergedSections)
        for (auto &RE : MS.Relocs) {
          unsigned OldSym = RE.Entry.getSymbol();
          auto It = ReorderMap.find(OldSym);
          if (It != ReorderMap.end())
            RE.Entry.setSymbolAndType(It->second, RE.Entry.getType());
        }
    }
  }

  // ----- Build output sections -----
  SmallVector<Sym, 64> FinalSyms;
  FinalSyms.append(LocalSyms.begin(), LocalSyms.end());
  FinalSyms.append(GlobalSyms.begin(), GlobalSyms.end());

  struct OutSection {
    Shdr Hdr;
    SmallVector<char, 0> Data;
    uint64_t VirtualSize = 0;
  };
  SmallVector<OutSection, 32> OutSections;
  OutSections.push_back({});
  memset(&OutSections[0].Hdr, 0, sizeof(Shdr));

  for (auto &MS : MergedSections) {
    OutSection Out;
    Out.Hdr = MS.Template;
    Out.Hdr.sh_name = ShStrTab.add(MS.Name);
    Out.Data = std::move(MS.Data);
    Out.VirtualSize = MS.VirtualSize;
    OutSections.push_back(std::move(Out));
  }

  unsigned SymTabIdx = OutSections.size();
  {
    OutSection S;
    memset(&S.Hdr, 0, sizeof(Shdr));
    S.Hdr.sh_type = SHT_SYMTAB;
    S.Hdr.sh_entsize = sizeof(Sym);
    S.Hdr.sh_addralign = 8;
    S.Hdr.sh_info = FirstGlobal;
    S.Hdr.sh_name = ShStrTab.add(".symtab");
    S.Data.assign(reinterpret_cast<const char *>(FinalSyms.data()),
                  reinterpret_cast<const char *>(FinalSyms.data()) +
                      FinalSyms.size() * sizeof(Sym));
    OutSections.push_back(std::move(S));
  }

  unsigned StrTabIdx = OutSections.size();
  {
    OutSection S;
    memset(&S.Hdr, 0, sizeof(Shdr));
    S.Hdr.sh_type = SHT_STRTAB;
    S.Hdr.sh_addralign = 1;
    S.Hdr.sh_name = ShStrTab.add(".strtab");
    S.Data.assign(SymStrTab.Data.begin(), SymStrTab.Data.end());
    OutSections.push_back(std::move(S));
  }
  OutSections[SymTabIdx].Hdr.sh_link = StrTabIdx;

  // Emit .rela sections for each merged section with relocations.
  for (unsigned m = 0; m < MergedSections.size(); ++m) {
    auto &MS = MergedSections[m];
    if (MS.Relocs.empty())
      continue;
    OutSection S;
    memset(&S.Hdr, 0, sizeof(Shdr));
    S.Hdr.sh_type = SHT_RELA;
    S.Hdr.sh_entsize = sizeof(Rela);
    S.Hdr.sh_addralign = 8;
    S.Hdr.sh_link = SymTabIdx;
    S.Hdr.sh_info = m + 1;
    S.Hdr.sh_name = ShStrTab.add((".rela" + MS.Name).c_str());
    SmallVector<Rela, 0> FlatRelas;
    for (auto &RE : MS.Relocs)
      FlatRelas.push_back(RE.Entry);
    S.Data.assign(reinterpret_cast<const char *>(FlatRelas.data()),
                  reinterpret_cast<const char *>(FlatRelas.data()) +
                      FlatRelas.size() * sizeof(Rela));
    OutSections.push_back(std::move(S));
  }

  unsigned ShStrTabIdx = OutSections.size();
  {
    uint32_t nameOff = ShStrTab.add(".shstrtab");
    OutSection S;
    memset(&S.Hdr, 0, sizeof(Shdr));
    S.Hdr.sh_type = SHT_STRTAB;
    S.Hdr.sh_addralign = 1;
    S.Hdr.sh_name = nameOff;
    S.Data.assign(ShStrTab.Data.begin(), ShStrTab.Data.end());
    OutSections.push_back(std::move(S));
  }

  // ----- Layout -----
  // All section addresses are 0 in -r mode (ElfImageEmitter.cpp:1552).
  uint64_t Off = sizeof(Ehdr);
  for (unsigned i = 1; i < OutSections.size(); ++i) {
    uint64_t Align = std::max<uint64_t>(OutSections[i].Hdr.sh_addralign, 1);
    Off = (Off + Align - 1) & ~(Align - 1);
    OutSections[i].Hdr.sh_offset = Off;
    if (OutSections[i].Hdr.sh_type == SHT_NOBITS) {
      OutSections[i].Hdr.sh_size = OutSections[i].VirtualSize;
    } else {
      OutSections[i].Hdr.sh_size = OutSections[i].Data.size();
      Off += OutSections[i].Data.size();
    }
  }
  Off = (Off + 7) & ~(uint64_t)7;
  uint64_t ShOff = Off;

  // ----- Write output -----
  // Ported from LLD ElfImageEmitter::writeHeader / writeResult.
  SmallVector<char, 0> OutBuf;
  OutBuf.resize(ShOff + OutSections.size() * sizeof(Shdr), 0);

  Ehdr *H = reinterpret_cast<Ehdr *>(OutBuf.data());
  memset(H, 0, sizeof(Ehdr));
  memcpy(H->e_ident, ElfMagic, 4);
  H->e_ident[EI_CLASS] = ELFCLASS64;
  H->e_ident[EI_DATA] = ELFDATA2LSB;
  H->e_ident[EI_VERSION] = EV_CURRENT;
  H->e_ident[EI_OSABI] = OSABI;
  H->e_ident[EI_ABIVERSION] = ABIVer;
  H->e_type = ET_REL;
  H->e_machine = Machine;
  H->e_version = EV_CURRENT;
  H->e_ehsize = sizeof(Ehdr);
  H->e_shentsize = sizeof(Shdr);
  H->e_shoff = ShOff;
  H->e_shnum = OutSections.size();
  H->e_shstrndx = ShStrTabIdx;
  H->e_flags = EFlags;
  // No e_phoff / e_phentsize / e_phnum — ET_REL has no program headers
  // (LLD: ElfImageEmitter.cpp:2625 skips these when config->relocatable).

  for (unsigned i = 1; i < OutSections.size(); ++i)
    if (!OutSections[i].Data.empty())
      memcpy(OutBuf.data() + OutSections[i].Hdr.sh_offset,
             OutSections[i].Data.data(), OutSections[i].Data.size());

  for (unsigned i = 0; i < OutSections.size(); ++i)
    memcpy(OutBuf.data() + ShOff + i * sizeof(Shdr), &OutSections[i].Hdr,
           sizeof(Shdr));

  OS.write(OutBuf.data(), OutBuf.size());
  return true;
}

} // anonymous namespace

bool mergeELF64LEObjects(ArrayRef<SmallVector<char, 0>> Buffers,
                         raw_pwrite_stream &OS, const Options &Opts) {
  return mergeELF64LEImpl(Buffers, OS, Opts);
}

bool mergeELF64LEObjects(ArrayRef<StringRef> Buffers, raw_pwrite_stream &OS,
                         const Options &Opts) {
  return mergeELF64LEImpl(Buffers, OS, Opts);
}

} // namespace neverc::merge
