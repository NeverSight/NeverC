//===- MergerELF.cpp - ELF64LE relocatable merge (ported from LLD -r) --===//
//
// Relocatable object merger for ELF64LE.  The algorithms here are ported
// from the LLD ELF backend's `-r` (partial link) code paths:
//
//   Section merging   ← Layout/OutputSections.cpp  (canMergeToProgbits,
//                        commitSection, header-compatible grouping)
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

#include "Common/DwarfRebase.h"
#include "Common/MergerCommon.h"
#include "neverc/Foundation/AndroidKernelModuleReleaseNames.h"
#include "neverc/Foundation/AndroidKernelModuleRelocationPolicy.h"
#include "neverc/Foundation/AndroidKernelModuleSectionPolicy.h"
#include "neverc/Foundation/AndroidKernelModuleSymbolPolicy.h"
#include "neverc/Foundation/AndroidKernelReleaseSymbolMap.h"
#include "neverc/Foundation/ELFDebugSectionPolicy.h"
#include "neverc/Merge/Merger.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Support/Compression.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/SHA256.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <optional>
#include <tuple>

using namespace llvm;

namespace neverc::merge {

namespace {

constexpr uint64_t AndroidModuleVersionEntrySize = 64;

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
bool sectionEntsizesCompatible(uint32_t TypeA, uint64_t FlagsA,
                               uint64_t EntsizeA, uint32_t TypeB,
                               uint64_t FlagsB, uint64_t EntsizeB) {
  using namespace llvm::ELF;
  const auto IsArray = [](uint32_t Type) {
    return Type == SHT_INIT_ARRAY || Type == SHT_PREINIT_ARRAY ||
           Type == SHT_FINI_ARRAY;
  };
  const bool MustMatch = EntsizeA != 0 || EntsizeB != 0 ||
                         ((FlagsA | FlagsB) & (SHF_MERGE | SHF_STRINGS)) != 0 ||
                         IsArray(TypeA) || IsArray(TypeB);
  return !MustMatch || EntsizeA == EntsizeB;
}

bool sectionsCompatible(uint32_t typeA, uint64_t flagsA, uint64_t entsizeA,
                        uint32_t typeB, uint64_t flagsB, uint64_t entsizeB,
                        uint16_t machine) {
  if (!sectionEntsizesCompatible(typeA, flagsA, entsizeA, typeB, flagsB,
                                 entsizeB))
    return false;
  if (typeA == typeB)
    return flagsA == flagsB;
  if (canMergeToProgbits(typeA, machine) && canMergeToProgbits(typeB, machine))
    return flagsA == flagsB;
  return false;
}

uint64_t clampAlign(uint64_t A) {
  constexpr uint64_t MaxAlign = uint64_t(1) << 20;
  if (A <= 1)
    return 1;
  if ((A & (A - 1)) != 0)
    return 1;
  return std::min(A, MaxAlign);
}

bool isValidReleaseAlignment(uint64_t Alignment) {
  return Alignment <= 1 || (Alignment & (Alignment - 1)) == 0;
}

uint64_t normalizeReleaseAlignment(uint64_t Alignment) {
  return std::max<uint64_t>(Alignment, 1);
}

bool checkedAlignOffset(uint64_t Offset, uint64_t Alignment,
                        uint64_t &Aligned) {
  Alignment = std::max<uint64_t>(Alignment, 1);
  const uint64_t Remainder = Offset % Alignment;
  const uint64_t Padding = Remainder == 0 ? 0 : Alignment - Remainder;
  if (Padding > std::numeric_limits<uint64_t>::max() - Offset)
    return false;
  Aligned = Offset + Padding;
  return true;
}

// ---------------------------------------------------------------------------
// Symbol resolution priority — ported from LLD Symbols/Symbols.cpp
// ---------------------------------------------------------------------------

// In -r mode LLD preserves symbol bindings as-is but still resolves
// duplicates: a defined GLOBAL beats a WEAK which beats an UNDEF.
// This priority mirrors Symbol::resolve() for the relocatable case.
enum SymPriority : uint8_t {
  PRI_WEAK_UNDEF = 0,
  PRI_GLOBAL_UNDEF = 1,
  PRI_COMMON = 2,
  PRI_WEAK_DEF = 3,
  PRI_GLOBAL_DEF = 4,
};

template <typename SymT> SymPriority getSymPriority(const SymT &S) {
  using namespace llvm::ELF;
  bool isDefined = S.st_shndx != SHN_UNDEF;
  bool isCommon = S.st_shndx == SHN_COMMON;
  bool isWeak = S.getBinding() == STB_WEAK;

  if (!isDefined && !isCommon)
    return isWeak ? PRI_WEAK_UNDEF : PRI_GLOBAL_UNDEF;
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
  using Rel = typename ELFT::Rel;

  AndroidKernelReleaseSymbolMap PendingReleaseSymbolMap;

  if (Opts.finalizeAndroidKernelModule && !Opts.androidKernelModule) {
    errs() << "neverc: Android module finalization requires Android module "
              "merge semantics\n";
    return false;
  }
  if (Opts.stripUnneededSymbols &&
      (!Opts.androidKernelModule || !Opts.finalizeAndroidKernelModule)) {
    errs() << "neverc: unneeded-symbol stripping is only valid while "
              "finalizing an Android kernel module\n";
    return false;
  }
  if (Opts.stripUnneededSymbols && Opts.artifact == ArtifactKind::SplitDwarf) {
    errs() << "neverc: Android module release stripping cannot produce a "
              "Split-DWARF package\n";
    return false;
  }

  detail::DedupStrTab ShStrTab, SymStrTab;

  struct RelocEntry {
    Rela Entry;
    unsigned PartIdx;
    unsigned InputTargetSection;
    uint64_t InputOffset;
  };

  struct LinkOrderContribution {
    unsigned Partition;
    unsigned InputSection;
    unsigned LinkedInputSection;
    uint32_t InputType;
    uint64_t Alignment;
    uint64_t Size;
    uint64_t OriginalOffset;
  };

  struct MergedSection {
    std::string Name;
    Shdr Template;
    SmallVector<char, 0> Data;
    SmallVector<RelocEntry, 0> Relocs;
    uint64_t VirtualSize = 0;
    // Preserve contribution identity until every referenced target has its
    // final output section/offset. SHF_LINK_ORDER then requires these bytes to
    // be re-laid out in that target placement order, not input metadata order.
    SmallVector<LinkOrderContribution, 2> LinkOrderContributions;
  };
  SmallVector<MergedSection, 32> MergedSections;
  StringMap<SmallVector<unsigned, 2>> SectionIndex;

  uint16_t Machine = 0;
  uint32_t EFlags = 0;
  unsigned char OSABI = 0, ABIVer = 0;
  bool HaveArch = false;

  // Single fold entry-point for this merge: every header property the policy
  // may rewrite travels together so producer and verifier cannot drift.
  auto foldSection = [&](StringRef Name,
                         const Shdr &Header) -> detail::ELFSectionFold {
    return detail::foldELFSection({Name, Header.sh_type, Header.sh_flags,
                                   Header.sh_entsize, Header.sh_addralign},
                                  Opts.mergeSections, Opts.androidKernelModule,
                                  Opts.preservedSections);
  };

  // Section merge: group by (name, compatible_type, flags).
  // Ported from LLD LinkerScript::addSection + OutputSections.cpp.
  auto findOrCreateSection = [&](StringRef Name, const Shdr &S) -> unsigned {
    auto &Candidates = SectionIndex[Name];
    uint64_t Flags = S.sh_flags & ~(uint64_t)SHF_GROUP;
    for (unsigned idx : Candidates)
      if (sectionsCompatible(MergedSections[idx].Template.sh_type,
                             MergedSections[idx].Template.sh_flags,
                             MergedSections[idx].Template.sh_entsize, S.sh_type,
                             Flags, S.sh_entsize, Machine))
        return idx;
    MergedSection MS;
    MS.Name = Name.str();
    MS.Template = S;
    MS.Template.sh_flags = Flags;
    MS.Template.sh_addralign =
        Opts.stripUnneededSymbols
            ? normalizeReleaseAlignment(MS.Template.sh_addralign)
            : clampAlign(MS.Template.sh_addralign);
    // Metadata links are regenerated below. Ordinary links are either remapped
    // precisely through LinkOrderContributions or rejected by release
    // preflight.
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
    DenseMap<unsigned, uint64_t> SecOff;
    DenseSet<unsigned> DroppedSecs;
    unsigned ReleaseSymbolStringTable = std::numeric_limits<unsigned>::max();
    unsigned ReleaseSectionStringTable = std::numeric_limits<unsigned>::max();
  };
  SmallVector<PerPartition, 8> Maps;
  SmallVector<PartitionDwarf, 8> PartDwarfs(Buffers.size());

  // Symbol table: locals first, then globals (ELF convention).
  // In -r mode LLD does NOT recompute bindings (ElfImageEmitter.cpp:1829).
  SmallVector<Sym, 64> LocalSyms, GlobalSyms;
  LocalSyms.push_back({});
  memset(&LocalSyms[0], 0, sizeof(Sym));
  struct SymbolPlacementOrigin {
    unsigned Partition;
    unsigned InputSection;
    uint64_t InputValue;
  };
  SmallVector<std::optional<SymbolPlacementOrigin>, 64> LocalSymbolOrigins,
      GlobalSymbolOrigins;
  LocalSymbolOrigins.push_back(std::nullopt);

  // Global symbol dedup: ported from LLD SymbolTable::insert +
  // Symbol::resolve.  In -r mode, GLOBAL>WEAK>UNDEF priority, and
  // SHN_COMMON is preserved (not converted to BSS).
  struct GlobalDedup {
    unsigned SlotIdx; // index into GlobalSyms
    SymPriority Pri;
    // A "strong" definition is a defined STB_GLOBAL (not WEAK, not COMMON, not
    // STB_GNU_UNIQUE).  Two of them for one name is an ODR violation; tracked
    // here so the second can be refused rather than silently dropped.
    bool Strong;
  };
  StringMap<GlobalDedup> GlobalMap;

  // Sum of all input bytes.  A relocatable merge only writes bytes copied from
  // an input section's on-disk contents (each copied once) plus a little
  // alignment padding, so the merged on-disk image cannot legitimately exceed
  // this.  SHT_NOBITS sections are the lone exception: they declare an sh_size
  // backed by *no* file bytes (a 64-byte section header can claim a 2^64 size),
  // and when such a section is folded into a PROGBITS output it must be
  // materialized as real zero bytes.  A garbage/hostile sh_size then drives an
  // unbounded SmallVector resize — the merge fuzzer hit a 7.6 EB allocation
  // (sh_size spelling "\1__mod_i") -> ASan allocation-size-too-big abort.  Cap
  // the cumulative materialized zero-fill at the total input size and refuse
  // beyond it: the parallel-codegen path never promotes NOBITS->PROGBITS so it
  // never trips, and the general -r path safely falls back to a real linker.
  uint64_t TotalInputBytes = 0;
  for (const auto &B : Buffers) {
    if (B.size() > std::numeric_limits<uint64_t>::max() - TotalInputBytes)
      return false;
    TotalInputBytes += B.size();
  }
  // Release alignment is preserved exactly, so a tiny object can legitimately
  // request a multi-megabyte gap. Keep that behavior without allowing an
  // attacker-controlled power-of-two sh_addralign to request an unbounded
  // SmallVector allocation. This is an allocation budget, not an alignment
  // clamp: alignments above the budget are rejected rather than rewritten.
  constexpr uint64_t ReleasePaddingBudget = uint64_t(64) << 20;
  const uint64_t ReleaseMaterializedBudget =
      TotalInputBytes >
              std::numeric_limits<uint64_t>::max() - ReleasePaddingBudget
          ? std::numeric_limits<uint64_t>::max()
          : TotalInputBytes + ReleasePaddingBudget;
  uint64_t MaterializedZeroFill = 0;
  uint64_t ReleaseMaterializedBytes = 0;

  auto checkedMaterializedSize = [&](uint64_t Current, uint64_t Growth,
                                     uint64_t &NewSize) {
    if (Growth > std::numeric_limits<uint64_t>::max() - Current)
      return false;
    NewSize = Current + Growth;
    if (NewSize > std::numeric_limits<size_t>::max())
      return false;
    if (!Opts.stripUnneededSymbols)
      return true;
    if (Growth > ReleaseMaterializedBudget - ReleaseMaterializedBytes)
      return false;
    ReleaseMaterializedBytes += Growth;
    return true;
  };

  for (unsigned p = 0; p < Buffers.size(); ++p) {
    if (Buffers[p].empty())
      continue;

    // ELFObjectFile<ELFT>::create assumes its caller has already identified the
    // container and only checks that an ELF header fits. Feeding arbitrary
    // bytes directly to that typed parser can therefore manufacture a seemingly
    // valid section table from a non-ELF fuzz input. Validate the format
    // contract before any eager parsing or allocation.
    const auto &Input = Buffers[p];
    if (Input.size() < EI_NIDENT || memcmp(Input.data(), ElfMagic, 4) != 0 ||
        static_cast<uint8_t>(Input[EI_CLASS]) != ELFCLASS64 ||
        static_cast<uint8_t>(Input[EI_DATA]) != ELFDATA2LSB ||
        static_cast<uint8_t>(Input[EI_VERSION]) != EV_CURRENT) {
      errs() << "neverc: relocatable merge: input is not an ELF64LE object; "
                "refusing to merge\n";
      return false;
    }

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
    if (!HaveArch) {
      HaveArch = true;
      Machine = Hdr.e_machine;
      EFlags = Hdr.e_flags;
      OSABI = Hdr.e_ident[EI_OSABI];
      ABIVer = Hdr.e_ident[EI_ABIVERSION];
    } else if (Hdr.e_machine != Machine) {
      // Every input must target the same architecture.  Two ELF64LE objects of
      // different e_machine (e.g. x86-64 and AArch64) both parse cleanly as
      // ELF64LE here yet describe incompatible code; concatenating them would
      // emit one valid ELF header (the first input's e_machine) over a body
      // whose later partitions are the wrong ISA — a "loads then executes
      // garbage" miscompile no symbol/offset/content anchor can see, because
      // each partition's bytes still match its own input; only the architecture
      // is wrong.  The parallel-codegen path can never hit this (all partitions
      // share one TargetMachine); the general `-r` driver path over arbitrary
      // user objects can, and a real linker (`ld -r`) refuses it too.  Refuse
      // rather than emit a cross-ISA object.
      errs() << "neverc: relocatable merge: input has e_machine "
             << Hdr.e_machine << " but an earlier input had " << Machine
             << " (mixed architectures); refusing to merge\n";
      return false;
    } else if (Opts.stripUnneededSymbols &&
               (Hdr.e_flags != EFlags || Hdr.e_ident[EI_OSABI] != OSABI ||
                Hdr.e_ident[EI_ABIVERSION] != ABIVer)) {
      errs() << "neverc: Android kernel release: input ELF ABI header does "
                "not match the earlier partitions; refusing to merge\n";
      return false;
    }

    auto SecsOrErr = EF.sections();
    if (!SecsOrErr) {
      consumeError(SecsOrErr.takeError());
      return false;
    }
    ArrayRef<Shdr> Secs = *SecsOrErr;

    // Drop-debug is a metadata filter, not permission to remove memory-backed
    // content. A debug spelling carrying SHF_ALLOC may participate in runtime
    // layout despite its name, so reject it before planning any output.
    if (Opts.dropDebugInfo) {
      for (unsigned I = 1; I < Secs.size(); ++I) {
        auto SectionName = EF.getSectionName(Secs[I]);
        if (!SectionName) {
          consumeError(SectionName.takeError());
          return false;
        }
        if (ELFDebugSectionPolicy::isDebugSectionName(*SectionName) &&
            (Secs[I].sh_flags & SHF_ALLOC)) {
          errs() << "neverc: relocatable merge cannot drop allocated debug "
                    "section '"
                 << *SectionName << "'\n";
          return false;
        }
      }
    }

    // Release finalization is fail-closed even when the optional post-merge
    // verifier is disabled. The generic -r merger historically tolerated a
    // handful of malformed ELF shapes by treating missing names/symbol maps as
    // empty/zero; that is unsuitable for an artifact whose symbol table and
    // relocations are consumed directly by the kernel module loader.
    if (Opts.stripUnneededSymbols) {
      if (Hdr.e_type != ET_REL || Hdr.e_version != EV_CURRENT ||
          Hdr.e_machine != EM_AARCH64 || Hdr.e_ehsize != sizeof(Ehdr) ||
          Hdr.e_shentsize != sizeof(Shdr)) {
        errs() << "neverc: Android module release input has an unsupported "
                  "ELF header\n";
        return false;
      }
      if (Hdr.e_shnum == 0 || Hdr.e_shnum >= SHN_LORESERVE ||
          Hdr.e_shstrndx >= SHN_LORESERVE) {
        errs() << "neverc: Android module release input uses unsupported "
                  "extended section numbering\n";
        return false;
      }
      if (Hdr.e_shstrndx >= Secs.size() ||
          Secs[Hdr.e_shstrndx].sh_type != SHT_STRTAB) {
        errs() << "neverc: Android module release input has an invalid "
                  "section-name string table\n";
        return false;
      }

      std::optional<unsigned> SelectedSymtab;
      ArrayRef<Sym> SelectedSymbols;
      StringRef SelectedStrings;
      // Discover the sole symbol table before classifying any SHT_STRTAB.
      // String-table type alone is not metadata provenance: only e_shstrndx
      // and this selected symtab's sh_link are regenerated by the canonical
      // writer. A third table is content the canonical schema cannot express.
      for (unsigned I = 0; I < Secs.size(); ++I) {
        const Shdr &Section = Secs[I];
        if (Section.sh_type != SHT_SYMTAB)
          continue;
        if (SelectedSymtab || Section.sh_entsize != sizeof(Sym) ||
            Section.sh_size % sizeof(Sym) != 0 ||
            Section.sh_link >= Secs.size() ||
            Secs[Section.sh_link].sh_type != SHT_STRTAB) {
          errs() << "neverc: Android module release input has an invalid or "
                    "ambiguous symbol table\n";
          return false;
        }
        auto Symbols = EF.symbols(&Section);
        auto Strings = EF.getStringTableForSymtab(Section);
        if (!Symbols || !Strings) {
          if (!Symbols)
            consumeError(Symbols.takeError());
          if (!Strings)
            consumeError(Strings.takeError());
          errs() << "neverc: Android module release input has an invalid "
                    "symbol/string table\n";
          return false;
        }
        SelectedSymtab = I;
        SelectedSymbols = *Symbols;
        SelectedStrings = *Strings;
      }
      PM.ReleaseSectionStringTable = Hdr.e_shstrndx;
      if (SelectedSymtab)
        PM.ReleaseSymbolStringTable = Secs[*SelectedSymtab].sh_link;

      for (unsigned I = 0; I < Secs.size(); ++I) {
        const Shdr &Section = Secs[I];
        auto SectionName = EF.getSectionName(Section);
        if (!SectionName) {
          consumeError(SectionName.takeError());
          errs() << "neverc: Android module release input has an invalid "
                    "section name\n";
          return false;
        }
        // Each contribution must already have the loader-facing
        // modversion_info table shape. Checking before metadata routing and
        // folding prevents SHT_NULL from disappearing, SHF_ALLOC from being
        // ORed in, or a weak alignment from being raised by EnsureSection.
        if (*SectionName == "__versions") {
          if (Section.sh_type != SHT_PROGBITS ||
              !(Section.sh_flags & SHF_ALLOC) ||
              (Section.sh_flags & SHF_COMPRESSED)) {
            errs() << "neverc: Android module release input __versions must "
                      "be an allocated, uncompressed SHT_PROGBITS section\n";
            return false;
          }
          if (Section.sh_addralign < 8 ||
              (Section.sh_addralign & (Section.sh_addralign - 1)) != 0) {
            errs() << "neverc: Android module release input __versions "
                      "alignment must be a power of two >= 8\n";
            return false;
          }
          if (Section.sh_size % AndroidModuleVersionEntrySize != 0) {
            errs() << "neverc: Android module release input __versions size "
                      "must be a multiple of 64 bytes\n";
            return false;
          }
        }
        if (AndroidKernelModuleSectionPolicy::rejectsReleaseInputType(
                Section.sh_type)) {
          errs() << "neverc: Android module release input has an unsupported "
                    "section type\n";
          return false;
        }
        if (Section.sh_addr != 0) {
          errs() << "neverc: Android module release ET_REL input section has "
                    "a non-zero sh_addr\n";
          return false;
        }
        if (!isValidReleaseAlignment(Section.sh_addralign)) {
          errs() << "neverc: Android module release input section has an "
                    "invalid sh_addralign\n";
          return false;
        }
        const bool IsSelectedStringTable = Section.sh_type == SHT_STRTAB &&
                                           (I == PM.ReleaseSectionStringTable ||
                                            I == PM.ReleaseSymbolStringTable);
        if (Section.sh_type == SHT_STRTAB && !IsSelectedStringTable) {
          errs() << "neverc: Android module release input has an additional "
                    "string table that the canonical output cannot represent\n";
          return false;
        }
        const bool IsRegeneratedMetadata =
            IsSelectedStringTable ||
            AndroidKernelModuleSectionPolicy::regeneratesReleaseInputType(
                Section.sh_type);
        if (!IsRegeneratedMetadata) {
          if (Section.sh_flags & SHF_INFO_LINK) {
            errs() << "neverc: Android module release does not support "
                      "ordinary SHF_INFO_LINK sections\n";
            return false;
          }
          if (Section.sh_flags & SHF_LINK_ORDER) {
            if (Section.sh_link == 0 || Section.sh_link >= Secs.size() ||
                Section.sh_info != 0) {
              errs() << "neverc: Android module release input has malformed "
                        "SHF_LINK_ORDER metadata\n";
              return false;
            }
          } else if (Section.sh_link != 0 || Section.sh_info != 0) {
            errs() << "neverc: Android module release input ordinary section "
                      "has unsupported sh_link/sh_info metadata\n";
            return false;
          }
        }
        if (Section.sh_type != SHT_NOBITS &&
            (Section.sh_offset > Input.size() ||
             Section.sh_size > Input.size() - Section.sh_offset)) {
          errs() << "neverc: Android module release input section payload is "
                    "outside the file\n";
          return false;
        }
        if (!Opts.dropDebugInfo &&
            detail::isLegacyELFCompressedDebugSection(*SectionName)) {
          errs() << "neverc: Android module release cannot retain legacy "
                    "GNU compressed debug section '"
                 << *SectionName
                 << "'; use dropDebugInfo or regenerate standard DWARF\n";
          return false;
        }
      }

      if (SelectedSymtab) {
        for (const Sym &Symbol : SelectedSymbols) {
          if (Symbol.st_name >= SelectedStrings.size()) {
            errs() << "neverc: Android module release input symbol name "
                      "offset is outside its string table\n";
            return false;
          }
          const char *Begin = SelectedStrings.data() + Symbol.st_name;
          const size_t Available = SelectedStrings.size() - Symbol.st_name;
          if (strnlen(Begin, Available) == Available) {
            errs() << "neverc: Android module release input symbol name is "
                      "not NUL-terminated\n";
            return false;
          }
          if (Symbol.st_shndx != SHN_UNDEF && Symbol.st_shndx < SHN_LORESERVE &&
              Symbol.st_shndx >= Secs.size()) {
            errs() << "neverc: Android module release input symbol has an "
                      "out-of-range section index\n";
            return false;
          }
        }
      }

      for (const Shdr &Section : Secs) {
        if (Section.sh_type != SHT_RELA && Section.sh_type != SHT_REL)
          continue;
        const uint64_t ExpectedEntsize =
            Section.sh_type == SHT_RELA ? sizeof(Rela) : sizeof(Rel);
        if (!SelectedSymtab || Section.sh_link != *SelectedSymtab ||
            Section.sh_info == 0 || Section.sh_info >= Secs.size() ||
            Section.sh_entsize != ExpectedEntsize ||
            Section.sh_size % ExpectedEntsize != 0) {
          errs() << "neverc: Android module release input has malformed "
                    "relocation metadata\n";
          return false;
        }
        if (Section.sh_type == SHT_RELA) {
          auto Relocations = EF.relas(Section);
          if (!Relocations) {
            consumeError(Relocations.takeError());
            return false;
          }
          for (const Rela &Relocation : *Relocations) {
            const auto Width = AndroidKernelModuleRelocationPolicy::writeWidth(
                Relocation.getType());
            const uint64_t TargetSize = Secs[Section.sh_info].sh_size;
            if (Relocation.getSymbol() >= SelectedSymbols.size() || !Width ||
                Relocation.r_offset > TargetSize ||
                *Width > TargetSize - Relocation.r_offset) {
              errs() << "neverc: Android module release input relocation has "
                        "an unsupported type, out-of-range symbol, or "
                        "overrunning site\n";
              return false;
            }
          }
        } else {
          auto Relocations = EF.rels(Section);
          if (!Relocations) {
            consumeError(Relocations.takeError());
            return false;
          }
          for (const Rel &Relocation : *Relocations) {
            const auto Width = AndroidKernelModuleRelocationPolicy::writeWidth(
                Relocation.getType());
            const uint64_t TargetSize = Secs[Section.sh_info].sh_size;
            if (Relocation.getSymbol() >= SelectedSymbols.size() || !Width ||
                Relocation.r_offset > TargetSize ||
                *Width > TargetSize - Relocation.r_offset) {
              errs() << "neverc: Android module release input relocation has "
                        "an unsupported type, out-of-range symbol, or "
                        "overrunning site\n";
              return false;
            }
          }
        }
      }
    }

    const Shdr *SymTabHdr = nullptr;
    StringRef SymStr;
    for (const Shdr &S : Secs) {
      if (S.sh_type == SHT_SYMTAB) {
        SymTabHdr = &S;
        if (auto R = EF.getStringTableForSymtab(S))
          SymStr = *R;
        else
          // Leave SymStr empty (every symbol name becomes "") rather than
          // letting the Expected's error go unchecked — an unconsumed llvm
          // Error aborts the process in assertions builds.  A malformed symbol
          // string table only reaches here on a hostile/garbage -r input; the
          // parallel-codegen path always produces a well-formed one.
          consumeError(R.takeError());
        break;
      }
    }

    // Section indices that some SHT_RELA/SHT_REL targets via sh_info.  Used
    // only to make the SHT_NOTE dedup below reloc-safe: a relocated note must
    // never be folded onto an earlier byte-identical copy, because Phase 3
    // would then append this note's relocations onto the front copy as well,
    // double-applying them (or, if the two notes carry relocations to different
    // symbols, silently keeping only the front's).  A note with relocations
    // therefore takes the normal concatenating path, where Phase 3 places its
    // relocations against its own SecOff like any other section.  Real notes
    // (.note.gnu.property, build-id, .note.GNU-stack) carry no relocations, so
    // this only ever changes the exotic relocated-note case.
    DenseSet<unsigned> SectionHasRelocTarget;
    for (const Shdr &RS : Secs)
      if (RS.sh_type == SHT_RELA || RS.sh_type == SHT_REL)
        // sh_info (the relocated target section index) is read straight from
        // the input, so a hostile/fuzzed object can set it to DenseMap's
        // reserved empty (~0u) or tombstone (~0u-1) key — inserting which
        // asserts under LLVM_ENABLE_ASSERTIONS (the merge fuzzer's recurring
        // "Empty/Tombstone value shouldn't be inserted" abort) and is UB
        // otherwise.  A real reloc target is always a valid section index, and
        // the sole consumer (count(i) with i in [1, Secs.size())) can never
        // match an out-of-range value, so bounding it here is
        // behavior-preserving and also excludes both reserved keys.
        if (RS.sh_info < Secs.size())
          SectionHasRelocTarget.insert(RS.sh_info);

    // ----- Phase 1: Merge sections -----
    // Skip metadata sections that are regenerated in the output.
    // SHT_GROUP is skipped because neverc is pure C — no COMDAT.
    // LLVM linker metadata does not survive -r. In particular, concatenating
    // unused raw FatLTO payloads would not produce valid bitcode. COFF and
    // Mach-O drop the call graph profile too, for the further reason that their
    // copies of it hold symbol table indices this merge invalidates; the three
    // formats state one policy through isCOFFCallGraphProfileSection /
    // isMachOCallGraphProfileSection in Common/MergerCommon.h, which ELF
    // expresses by section type instead.
    for (unsigned i = 1; i < Secs.size(); ++i) {
      const Shdr &S = Secs[i];
      // A COMDAT/section group (SHT_GROUP) implies dedup semantics this pure-C
      // -r merge does not implement.  Pure C post-LTO never emits one, so its
      // presence means an upstream assumption is violated; refuse rather than
      // silently drop the group and risk merging members that should have been
      // deduplicated into a single copy.
      if (S.sh_type == SHT_GROUP) {
        errs() << "neverc: relocatable merge does not support SHT_GROUP "
                  "(COMDAT) sections; refusing to merge\n";
        return false;
      }
      // A malformed sh_name (offset outside the section-header string table)
      // makes getSectionName return an Expected error.  The prior
      // `NameOrErr ? *NameOrErr : ""` consulted the value but never *consumed*
      // the error, so in an assertions / ABI-breaking-checks build the
      // Expected's destructor aborted the process ("Expected<T> must be checked
      // before access or destruction") at the next scope exit — the same
      // unchecked-Expected crash class the merge fuzzer found in the COFF path.
      // Consume the error and refuse: a processable section we cannot name
      // cannot be routed to the right merged section.  A valid object never
      // errors here, so this never false-rejects.
      auto NameOrErr = EF.getSectionName(S);
      if (!NameOrErr) {
        consumeError(NameOrErr.takeError());
        return false;
      }
      StringRef SecName = *NameOrErr;

      if (Opts.stripUnneededSymbols && SecName == ".modinfo") {
        auto Contents = EF.getSectionContents(S);
        if (!Contents) {
          consumeError(Contents.takeError());
          return false;
        }
        if (AndroidKernelModuleSymbolPolicy::containsLivePatchModInfo(
                *Contents)) {
          errs() << "neverc: Android module release strip does not support a "
                    "module marked livepatch in .modinfo\n";
          return false;
        }
      }

      // Livepatch modules require their original symbol indices/order and
      // SHF_RELA_LIVEPATCH records to survive byte-for-byte. Release pruning
      // and structural renaming intentionally change both, and the typed plugin
      // graph cannot represent SHN_LIVEPATCH losslessly.  Refuse this distinct
      // artifact class instead of producing a valid-looking broken module.
      if (Opts.stripUnneededSymbols &&
          (AndroidKernelModuleSymbolPolicy::isLivePatchSectionName(SecName) ||
           (S.sh_flags &
            AndroidKernelModuleSymbolPolicy::LivePatchRelocationSectionFlag))) {
        errs() << "neverc: Android module release strip does not support "
                  "livepatch section '"
               << SecName << "'\n";
        return false;
      }

      // Metadata sections are regenerated below, but release-policy checks
      // must run first: livepatch's canonical `.klp.rela.*` sections are
      // themselves SHT_RELA and would otherwise disappear through this early
      // continue even when verification is disabled.
      const bool IsRegeneratedMetadata =
          Opts.stripUnneededSymbols
              ? ((S.sh_type == SHT_STRTAB &&
                  (i == PM.ReleaseSymbolStringTable ||
                   i == PM.ReleaseSectionStringTable)) ||
                 AndroidKernelModuleSectionPolicy::regeneratesReleaseInputType(
                     S.sh_type))
              : S.sh_type == SHT_SYMTAB || S.sh_type == SHT_STRTAB ||
                    S.sh_type == SHT_RELA || S.sh_type == SHT_REL ||
                    S.sh_type == SHT_LLVM_LTO ||
                    S.sh_type == SHT_LLVM_ADDRSIG ||
                    S.sh_type == SHT_LLVM_CALL_GRAPH_PROFILE;
      if (IsRegeneratedMetadata)
        continue;

      // Decide name and header semantics together.  In particular, an exact
      // preserved pool must retain SHF_MERGE, SHF_STRINGS, and sh_entsize as a
      // unit rather than keeping only its name.
      const detail::ELFSectionFold Fold = foldSection(SecName, S);
      SecName = Fold.Name;

      if (Opts.dropDebugInfo &&
          ELFDebugSectionPolicy::isDebugSectionName(SecName)) {
        PM.DroppedSecs.insert(i);
        continue;
      }

      // `--strip` on a delivered `.ko` intentionally models the narrow
      // `llvm-strip --strip-unneeded` boundary, not strip-all.  `.comment` is
      // producer metadata; loader-facing module sections are preserved.
      if (Opts.stripUnneededSymbols && Opts.finalizeAndroidKernelModule &&
          detail::isAndroidKernelReleaseDiscardableSection(SecName)) {
        PM.DroppedSecs.insert(i);
        continue;
      }

      // Final Android `.ko` merge: drop the NeverC profile-contract fingerprint
      // after the native/plugin callers have already verified input equality.
      if (Opts.finalizeAndroidKernelModule &&
          detail::isAndroidKernelProfileContractSection(SecName)) {
        PM.DroppedSecs.insert(i);
        continue;
      }

      // The merger lays out and concatenates on-disk section bytes. For an
      // SHF_COMPRESSED input those bytes are a compression header plus an
      // independent frame, while symbol values and relocations address the
      // logical uncompressed contents. Concatenating such frames would make the
      // later contributions invisible and invalidate all following offsets.
      // Refuse so callers use the regular linker instead. Output compression is
      // still supported after uncompressed contributions have been merged.
      if (S.sh_flags & SHF_COMPRESSED) {
        errs() << "neverc: relocatable merge does not support pre-compressed "
                  "section '"
               << SecName << "'; refusing to merge\n";
        return false;
      }

      // SHT_NOTE dedup.  When parallel codegen splits one module every
      // partition re-emits byte-identical notes (e.g. .note.gnu.property for
      // BTI/PAC/CET), so collapsing them to a single copy is both correct and
      // tidy.  But this merger is *also* the linker's general `-r` path
      // (ElfDriver/MachODriver) over arbitrary, possibly heterogeneous user
      // objects, where two same-named notes can legitimately carry *different*
      // bytes (distinct GNU-property feature sets, build-ids, ...).  Dropping
      // the later one there silently loses data the verifier cannot catch (it
      // excludes NOTE sections from its content anchor).  So dedup only when
      // the incoming note is byte-identical to the copy already merged;
      // otherwise fall through to the normal concatenating path.  A
      // byte-identical note starts at the first copy's offset 0, so deduped
      // symbols into it need no offset shift.  A note that is itself a
      // relocation target is never deduped (SectionHasRelocTarget guard): see
      // its definition above for why double-applied relocations would result.
      if (S.sh_type == SHT_NOTE && p > 0 && !SectionHasRelocTarget.count(i) &&
          !(S.sh_flags & SHF_LINK_ORDER)) {
        auto CIt = SectionIndex.find(SecName);
        if (CIt != SectionIndex.end() && !CIt->second.empty()) {
          unsigned FrontIdx = CIt->second.front();
          const MergedSection &Front = MergedSections[FrontIdx];
          if (Front.Template.sh_type == SHT_NOTE) {
            auto D = EF.getSectionContents(S);
            if (!D) {
              consumeError(D.takeError());
              return false;
            }
            ArrayRef<uint8_t> NB = *D;
            if (NB.size() == Front.Data.size() &&
                (NB.empty() ||
                 memcmp(NB.data(), Front.Data.data(), NB.size()) == 0)) {
              PM.SecMap[i] = FrontIdx + 1;
              PM.SecOff[i] = 0;
              continue;
            }
          }
        }
      }

      // Fold is authoritative for the output header shape.  Content layout
      // below still reads size/bytes from the original input header `S`.
      Shdr SCopy = S;
      SCopy.sh_type = Fold.Type;
      SCopy.sh_flags = Fold.Flags & ~(uint64_t)SHF_GROUP;
      SCopy.sh_entsize = Fold.Entsize;
      SCopy.sh_addralign = Fold.Alignment;
      unsigned MIdx = findOrCreateSection(SecName, SCopy);
      auto &MS = MergedSections[MIdx];

      // sh_entsize participates in compatibility whenever either contributor
      // has a nonzero value, carries MERGE/STRINGS semantics, or is an array
      // section. Umbrella `.rodata` is a concatenated byte range (Fold clears
      // entsize for both ordinary and demoted landings), so re-apply that
      // canonical zero on join. Type/flags need no post-join rewrite:
      // demotion reshapes SCopy before grouping.
      if (Fold.Name == ".rodata")
        MS.Template.sh_entsize = Fold.Entsize;

      // Track max alignment (LLD: OutputSection::commitSection).
      {
        uint64_t SafeAlign = Opts.stripUnneededSymbols
                                 ? normalizeReleaseAlignment(SCopy.sh_addralign)
                                 : clampAlign(SCopy.sh_addralign);
        if (SafeAlign > MS.Template.sh_addralign)
          MS.Template.sh_addralign = SafeAlign;
      }

      // When types differ but are compatible (canMergeToProgbits),
      // promote to SHT_PROGBITS (LLD behavior).
      if (MS.Template.sh_type != SCopy.sh_type &&
          canMergeToProgbits(MS.Template.sh_type, Machine) &&
          canMergeToProgbits(SCopy.sh_type, Machine))
        MS.Template.sh_type = SHT_PROGBITS;

      uint64_t Align = MS.Template.sh_addralign;
      uint64_t PartOffset;
      // Lay out by the *merged* section's type, not this input's.  Once any
      // PROGBITS-compatible input joins a NOBITS group the whole output section
      // is promoted to PROGBITS (above), and every NOBITS contribution must
      // then be materialized as zero bytes so all partitions measure their
      // offset against one continuous byte stream.  Keying off the input type
      // instead let a NOBITS run and a PROGBITS run both restart at offset 0 —
      // aliasing their symbols and dropping the NOBITS reserve from the output.
      if (MS.Template.sh_type == SHT_NOBITS) {
        // Pure NOBITS so far (this input is NOBITS too): no on-disk bytes.
        PartOffset = MS.VirtualSize;
        if (Align > 1) {
          const uint64_t Padding = (Align - (PartOffset % Align)) % Align;
          if (Padding > std::numeric_limits<uint64_t>::max() - PartOffset) {
            errs() << "neverc: relocatable merge: NOBITS section alignment "
                      "overflows its merged size; refusing to merge\n";
            return false;
          }
          PartOffset += Padding;
        }
        if (S.sh_size > std::numeric_limits<uint64_t>::max() - PartOffset) {
          errs() << "neverc: relocatable merge: NOBITS contribution size "
                    "overflows its merged section; refusing to merge\n";
          return false;
        }
        MS.VirtualSize = PartOffset + S.sh_size;
      } else {
        // Merged section is (or just became) PROGBITS.  Materialize any
        // NOBITS-only space accumulated before the promotion as zero bytes so
        // the running offset stays continuous across the type change.  The
        // accumulated VirtualSize is attacker-controlled (it comes from a
        // NOBITS sh_size), so bound the zero-fill against the input size before
        // resizing (see MaterializedZeroFill above).
        if (MS.Data.size() < MS.VirtualSize) {
          uint64_t Fill = MS.VirtualSize - MS.Data.size();
          if (Fill > TotalInputBytes - MaterializedZeroFill) {
            errs() << "neverc: relocatable merge: materializing a NOBITS "
                      "section as "
                   << MS.VirtualSize
                   << " zero bytes exceeds the total input size; refusing to "
                      "merge\n";
            return false;
          }
          uint64_t NewSize = 0;
          if (!checkedMaterializedSize(MS.Data.size(), Fill, NewSize)) {
            errs() << "neverc: relocatable merge: materialized section "
                      "exceeds the release allocation budget\n";
            return false;
          }
          MaterializedZeroFill += Fill;
          MS.Data.resize(NewSize, 0);
        }
        PartOffset = MS.Data.size();
        if (Align > 1) {
          uint64_t Padding = (Align - (PartOffset % Align)) % Align;
          uint64_t NewSize = 0;
          if (!checkedMaterializedSize(MS.Data.size(), Padding, NewSize)) {
            errs() << "neverc: relocatable merge: aligned section padding "
                      "exceeds the release allocation budget\n";
            return false;
          }
          MS.Data.resize(NewSize, 0);
          PartOffset = MS.Data.size();
        }
        if (S.sh_type == SHT_NOBITS) {
          // A NOBITS input folded into a PROGBITS output occupies real zero
          // bytes (it can no longer ride the pure-virtual-size path).  sh_size
          // is read straight from the input header and backed by no file bytes,
          // so bound it before the resize exactly as the accumulated fill above
          // (the merge fuzzer drove this very resize to a 7.6 EB allocation).
          if (S.sh_size > TotalInputBytes - MaterializedZeroFill) {
            errs() << "neverc: relocatable merge: materializing a NOBITS "
                      "section of "
                   << S.sh_size
                   << " zero bytes exceeds the total input size; refusing to "
                      "merge\n";
            return false;
          }
          uint64_t NewSize = 0;
          if (!checkedMaterializedSize(MS.Data.size(), S.sh_size, NewSize)) {
            errs() << "neverc: relocatable merge: materialized NOBITS "
                      "section exceeds the release allocation budget\n";
            return false;
          }
          MaterializedZeroFill += S.sh_size;
          MS.Data.resize(NewSize, 0);
        } else {
          auto D = EF.getSectionContents(S);
          if (!D) {
            // We have already committed this input's PartOffset.  Silently
            // skipping its bytes would shift every later section and alias this
            // section's symbols onto the next one's data — the same class of
            // wrong-address corruption as a collapsed offset.  Refuse instead.
            consumeError(D.takeError());
            return false;
          }
          uint64_t NewSize = 0;
          if (!checkedMaterializedSize(MS.Data.size(), D->size(), NewSize)) {
            errs() << "neverc: relocatable merge: section contents exceed "
                      "the release allocation budget\n";
            return false;
          }
          MS.Data.append(D->begin(), D->end());
        }
        MS.VirtualSize = MS.Data.size();
      }

      if (S.sh_flags & SHF_LINK_ORDER)
        MS.LinkOrderContributions.push_back(
            {p, i, static_cast<unsigned>(S.sh_link), S.sh_type,
             normalizeReleaseAlignment(SCopy.sh_addralign), S.sh_size,
             PartOffset});
      PM.SecMap[i] = MIdx + 1;
      PM.SecOff[i] = PartOffset;
      if (Opts.artifact == ArtifactKind::SplitDwarf)
        PartDwarfs[p].record(SecName, MIdx, PartOffset, S.sh_size);
    }

    // ----- Phase 2: Merge symbols -----
    // Ported from LLD finalizeSections → addSymbol.
    // Locals are concatenated; globals are deduped by name with
    // GLOBAL>WEAK>UNDEF priority (Symbol::resolve for -r).
    // Ordinary partial links preserve SHN_COMMON (LLD: getCommonSec returns
    // nullptr for non-relocated common symbols). Final Android release
    // hardening rejects it below because the module loader requires
    // -fno-common semantics.
    if (SymTabHdr) {
      auto SymsOrErr = EF.symbols(SymTabHdr);
      if (!SymsOrErr) {
        consumeError(SymsOrErr.takeError());
        return false;
      }
      ArrayRef<Sym> Syms = *SymsOrErr;

      for (unsigned i = 1; i < Syms.size(); ++i) {
        Sym OutS = Syms[i];
        std::optional<SymbolPlacementOrigin> PlacementOrigin;

        // strnlen-bound the name to the string table extent.  llvm's
        // getStringTableForSymtab guarantees a trailing NUL (it errors
        // otherwise), so for well-formed input this is identical to the
        // implicit strlen; the explicit bound keeps every merger's symbol-name
        // read uniformly safe (matching the MachO merger and the verifier) and
        // immune to any future reader that stops validating the terminator.
        StringRef Name;
        if (OutS.st_name < SymStr.size())
          Name = StringRef(SymStr.data() + OutS.st_name,
                           strnlen(SymStr.data() + OutS.st_name,
                                   SymStr.size() - OutS.st_name));

        if (Opts.stripUnneededSymbols) {
          if (OutS.st_shndx == SHN_COMMON) {
            errs() << "neverc: Android module release strip refuses COMMON "
                      "symbol '"
                   << Name << "'; compile final modules with -fno-common\n";
            return false;
          }
          if (OutS.st_shndx ==
              AndroidKernelModuleSymbolPolicy::LivePatchSectionIndex) {
            errs() << "neverc: Android module release strip does not support "
                      "livepatch symbol '"
                   << Name << "'\n";
            return false;
          }
          if (OutS.st_shndx >= SHN_LORESERVE && OutS.st_shndx != SHN_ABS) {
            errs() << "neverc: Android module release strip refuses symbol '"
                   << Name << "' with unsupported reserved section index "
                   << OutS.st_shndx << "\n";
            return false;
          }
        }

        // Remap section index and adjust value.
        if (OutS.st_shndx < SHN_LORESERVE) {
          if (PM.DroppedSecs.contains(OutS.st_shndx))
            continue;
          auto It = PM.SecMap.find(OutS.st_shndx);
          if (It != PM.SecMap.end()) {
            unsigned origShndx = OutS.st_shndx;
            OutS.st_shndx = It->second;
            if (It->second != 0) {
              auto OffIt = PM.SecOff.find(origShndx);
              if (OffIt != PM.SecOff.end()) {
                PlacementOrigin =
                    SymbolPlacementOrigin{p, origShndx, OutS.st_value};
                if (OutS.st_value >
                    std::numeric_limits<uint64_t>::max() - OffIt->second) {
                  errs() << "neverc: Android module release strip: symbol '"
                         << Name << "' value overflows its merged section\n";
                  return false;
                }
                OutS.st_value += OffIt->second;
              }
            }
          } else {
            OutS.st_shndx = 0;
          }
        }
        // SHN_COMMON: preserved as-is in -r mode (LLD behavior).

        // Belt-and-suspenders: never re-emit the tooling contract symbol even
        // if a malformed input placed it outside the dedicated section.
        if (Opts.finalizeAndroidKernelModule &&
            detail::isAndroidKernelProfileContractSymbol(Name))
          continue;
        OutS.st_name = SymStrTab.add(Name);

        if (Syms[i].getBinding() == STB_LOCAL) {
          PM.SymMap[i] = LocalSyms.size();
          LocalSyms.push_back(OutS);
          LocalSymbolOrigins.push_back(PlacementOrigin);
        } else {
          // Global symbol dedup: same-name globals are resolved by
          // priority (defined GLOBAL > WEAK > COMMON > UNDEF).
          // Ported from LLD SymbolTable::insert / Symbol::resolve.
          SymPriority Pri = getSymPriority(Syms[i]);
          // A strong definition is specifically a defined STB_GLOBAL: WEAK and
          // COMMON resolve by priority (legal), and STB_GNU_UNIQUE (a C++
          // vague-linkage symbol this pure-C merger never emits) is excluded so
          // it is never mistaken for an ODR clash.
          bool Strong =
              Pri == PRI_GLOBAL_DEF && Syms[i].getBinding() == STB_GLOBAL;
          auto [It, Inserted] = GlobalMap.try_emplace(
              Name, GlobalDedup{(unsigned)GlobalSyms.size(), Pri, Strong});

          if (Inserted) {
            unsigned Slot = GlobalSyms.size();
            GlobalSyms.push_back(OutS);
            GlobalSymbolOrigins.push_back(PlacementOrigin);
            // Record slot; will add FirstGlobal offset after the loop.
            PM.SymMap[i] = Slot | 0x80000000u;
          } else {
            if (Strong && It->second.Strong) {
              // Two strong (STB_GLOBAL, defined-in-section) definitions of one
              // name is an ODR violation.  A real link errors ("multiple
              // definition"); LLD -r keeps both and defers the error.  This
              // priority dedup would instead silently keep the first and drop
              // the second — the "loads fine, wrong symbol wins" divergence the
              // merger refuses elsewhere.  The parallel-codegen path can never
              // produce it (each global is defined in exactly one partition;
              // .__pcg locals carry unique names), so only a malformed -r input
              // reaches here; refuse rather than silently pick one.
              errs()
                  << "neverc: relocatable merge: multiple strong definitions "
                     "of symbol '"
                  << Name << "'; refusing to merge\n";
              return false;
            }
            unsigned Slot = It->second.SlotIdx;
            if (Pri > It->second.Pri) {
              GlobalSyms[Slot] = OutS;
              GlobalSymbolOrigins[Slot] = PlacementOrigin;
              It->second.Pri = Pri;
              It->second.Strong = Strong;
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
    ArrayRef<Sym> RelocSyms;
    if (SymTabHdr) {
      auto SymsOrErr = EF.symbols(SymTabHdr);
      if (!SymsOrErr) {
        consumeError(SymsOrErr.takeError());
        return false;
      }
      RelocSyms = *SymsOrErr;
    }
    auto ReferencesDroppedSymbol = [&](unsigned SymIdx) {
      if (SymIdx >= RelocSyms.size())
        return false;
      const Sym &Target = RelocSyms[SymIdx];
      if (Target.st_shndx < SHN_LORESERVE &&
          PM.DroppedSecs.contains(Target.st_shndx))
        return true;
      if (!Opts.finalizeAndroidKernelModule || Target.st_name >= SymStr.size())
        return false;
      StringRef Name(SymStr.data() + Target.st_name,
                     strnlen(SymStr.data() + Target.st_name,
                             SymStr.size() - Target.st_name));
      return detail::isAndroidKernelProfileContractSymbol(Name);
    };
    for (unsigned i = 1; i < Secs.size(); ++i) {
      if (Secs[i].sh_type != SHT_RELA && Secs[i].sh_type != SHT_REL)
        continue;
      // sh_info names the relocated section and is attacker-controlled; a value
      // equal to a DenseMap reserved key makes find() undefined, so skip it (it
      // cannot name a real section anyway).
      if (detail::isReservedDenseKey((unsigned)Secs[i].sh_info))
        continue;
      auto TargetIt = PM.SecMap.find(Secs[i].sh_info);
      if (TargetIt == PM.SecMap.end() || TargetIt->second == 0)
        continue;
      unsigned targetMIdx = TargetIt->second - 1;
      uint64_t dataOff = 0;
      auto OffIt = PM.SecOff.find(Secs[i].sh_info);
      if (OffIt != PM.SecOff.end())
        dataOff = OffIt->second;

      if (Secs[i].sh_type == SHT_RELA) {
        auto R = EF.relas(Secs[i]);
        if (!R) {
          // Silently dropping a relocation section would leave its
          // cross-references unrelocated in the output — a miscompile that
          // loads fine and then reads/jumps to the wrong place.  Refuse.
          consumeError(R.takeError());
          return false;
        }
        for (const Rela &Re : *R) {
          if (ReferencesDroppedSymbol(Re.getSymbol())) {
            errs() << "neverc: Android module finalization: retained section "
                      "has a relocation to the dropped profile contract\n";
            return false;
          }
          Rela Adjusted = Re;
          if (Adjusted.r_offset >
              std::numeric_limits<uint64_t>::max() - dataOff) {
            errs() << "neverc: Android module release relocation offset "
                      "overflows during section placement\n";
            return false;
          }
          Adjusted.r_offset += dataOff;
          MergedSections[targetMIdx].Relocs.push_back(
              {Adjusted, p, static_cast<unsigned>(Secs[i].sh_info),
               Re.r_offset});
        }
      } else {
        // SHT_REL (implicit-addend form).  A faithful REL->RELA conversion must
        // recover each relocation's implicit addend from the bytes it applies
        // to — architecture- and type-specific arithmetic.  The 64-bit ELF
        // targets neverc emits all use RELA, so this branch is unreachable in
        // practice; the prior code set r_addend=0, which silently dropped any
        // non-zero implicit addend, and the verifier checks reloc *offsets*,
        // not addend values, so it could not catch the resulting wrong value.
        // Refuse the merge instead so a hypothetical REL input falls back to
        // the proven path (serial codegen) or errors loudly, never miscompiles.
        return false;
      }
    }
  }

  if (Opts.stripUnneededSymbols) {
    // gABI SHF_LINK_ORDER: metadata contributions follow the relative order of
    // the referenced contributions after those targets have their final output
    // placement. Resolve dependencies first (a linked target may itself be a
    // link-order section), then rebuild each affected byte stream. Completely
    // tied target placements retain deterministic original input order.
    SmallVector<uint8_t, 32> SortState(MergedSections.size(), 0);
    std::function<bool(unsigned)> SortLinkOrder = [&](unsigned MIdx) {
      if (SortState[MIdx] == 2)
        return true;
      if (SortState[MIdx] == 1) {
        errs() << "neverc: Android module release has cyclic SHF_LINK_ORDER "
                  "dependencies\n";
        return false;
      }
      SortState[MIdx] = 1;
      MergedSection &MS = MergedSections[MIdx];
      std::optional<unsigned> FinalTargetSection;
      for (const LinkOrderContribution &Contribution :
           MS.LinkOrderContributions) {
        if (Contribution.Partition >= Maps.size())
          return false;
        auto Target = Maps[Contribution.Partition].SecMap.find(
            Contribution.LinkedInputSection);
        if (Target == Maps[Contribution.Partition].SecMap.end() ||
            Target->second == 0) {
          errs() << "neverc: Android module release SHF_LINK_ORDER section '"
                 << MS.Name << "' targets a section absent from output\n";
          return false;
        }
        if (Maps[Contribution.Partition].SecOff.find(
                Contribution.LinkedInputSection) ==
            Maps[Contribution.Partition].SecOff.end())
          return false;
        if (!FinalTargetSection)
          FinalTargetSection = Target->second;
        else if (*FinalTargetSection != Target->second) {
          errs() << "neverc: Android module release SHF_LINK_ORDER section '"
                 << MS.Name
                 << "' has contributors with distinct final targets\n";
          return false;
        }
        const unsigned TargetMIdx = Target->second - 1;
        if (!MergedSections[TargetMIdx].LinkOrderContributions.empty() &&
            !SortLinkOrder(TargetMIdx))
          return false;
      }

      if (!MS.LinkOrderContributions.empty()) {
        auto PlacementKey = [&](const LinkOrderContribution &Contribution) {
          const auto Target = Maps[Contribution.Partition].SecMap.find(
              Contribution.LinkedInputSection);
          const auto Offset = Maps[Contribution.Partition].SecOff.find(
              Contribution.LinkedInputSection);
          return std::tuple<unsigned, uint64_t, unsigned, unsigned>(
              Target->second, Offset->second, Contribution.Partition,
              Contribution.InputSection);
        };
        std::stable_sort(MS.LinkOrderContributions.begin(),
                         MS.LinkOrderContributions.end(),
                         [&](const LinkOrderContribution &Left,
                             const LinkOrderContribution &Right) {
                           return PlacementKey(Left) < PlacementKey(Right);
                         });

        SmallVector<char, 0> OriginalData = std::move(MS.Data);
        if (OriginalData.size() > ReleaseMaterializedBytes)
          return false;
        ReleaseMaterializedBytes -= OriginalData.size();
        SmallVector<char, 0> ReorderedData;
        uint64_t NewVirtualSize = 0;
        for (LinkOrderContribution &Contribution : MS.LinkOrderContributions) {
          const uint64_t Alignment =
              normalizeReleaseAlignment(Contribution.Alignment);
          const uint64_t Remainder = NewVirtualSize % Alignment;
          const uint64_t Padding = Remainder == 0 ? 0 : Alignment - Remainder;
          if (Padding > std::numeric_limits<uint64_t>::max() - NewVirtualSize)
            return false;
          NewVirtualSize += Padding;
          Maps[Contribution.Partition].SecOff[Contribution.InputSection] =
              NewVirtualSize;

          if (Contribution.Size >
              std::numeric_limits<uint64_t>::max() - NewVirtualSize)
            return false;
          if (MS.Template.sh_type != SHT_NOBITS) {
            uint64_t NewSize = 0;
            if (!checkedMaterializedSize(ReorderedData.size(), Padding,
                                         NewSize))
              return false;
            ReorderedData.resize(NewSize, 0);
            if (Contribution.InputType == SHT_NOBITS) {
              if (!checkedMaterializedSize(ReorderedData.size(),
                                           Contribution.Size, NewSize))
                return false;
              ReorderedData.resize(NewSize, 0);
            } else {
              if (Contribution.OriginalOffset > OriginalData.size() ||
                  Contribution.Size >
                      OriginalData.size() - Contribution.OriginalOffset)
                return false;
              if (!checkedMaterializedSize(ReorderedData.size(),
                                           Contribution.Size, NewSize))
                return false;
              ReorderedData.append(
                  OriginalData.begin() + Contribution.OriginalOffset,
                  OriginalData.begin() + Contribution.OriginalOffset +
                      Contribution.Size);
            }
          }
          NewVirtualSize += Contribution.Size;
        }
        MS.Data = std::move(ReorderedData);
        MS.VirtualSize = NewVirtualSize;
      }
      SortState[MIdx] = 2;
      return true;
    };

    for (unsigned MIdx = 0; MIdx < MergedSections.size(); ++MIdx)
      if (!MergedSections[MIdx].LinkOrderContributions.empty() &&
          !SortLinkOrder(MIdx))
        return false;

    auto RebaseSymbols =
        [&](MutableArrayRef<Sym> Symbols,
            ArrayRef<std::optional<SymbolPlacementOrigin>> Origins) {
          if (Symbols.size() != Origins.size())
            return false;
          for (unsigned I = 0; I < Symbols.size(); ++I) {
            if (!Origins[I])
              continue;
            const SymbolPlacementOrigin &Origin = *Origins[I];
            if (Origin.Partition >= Maps.size())
              return false;
            auto Offset =
                Maps[Origin.Partition].SecOff.find(Origin.InputSection);
            if (Offset == Maps[Origin.Partition].SecOff.end() ||
                Origin.InputValue >
                    std::numeric_limits<uint64_t>::max() - Offset->second)
              return false;
            Symbols[I].st_value = Origin.InputValue + Offset->second;
          }
          return true;
        };
    if (!RebaseSymbols(LocalSyms, LocalSymbolOrigins) ||
        !RebaseSymbols(GlobalSyms, GlobalSymbolOrigins))
      return false;

    for (MergedSection &MS : MergedSections)
      for (RelocEntry &Relocation : MS.Relocs) {
        if (Relocation.PartIdx >= Maps.size())
          return false;
        auto Offset =
            Maps[Relocation.PartIdx].SecOff.find(Relocation.InputTargetSection);
        if (Offset == Maps[Relocation.PartIdx].SecOff.end() ||
            Relocation.InputOffset >
                std::numeric_limits<uint64_t>::max() - Offset->second)
          return false;
        Relocation.Entry.r_offset = Relocation.InputOffset + Offset->second;
      }
  }

  if (Opts.stripUnneededSymbols && (!HaveArch || Machine != EM_AARCH64)) {
    errs() << "neverc: Android module release requires at least one valid "
              "AArch64 input object\n";
    return false;
  }

  // Multiple standalone DWO files form a DWARF package, not a plain
  // concatenate-and-rebase image. Split units deliberately omit their
  // abbreviation/string/range bases; package indexes select the matching
  // contribution by DWO ID. Rewriting those fields as absolute offsets would
  // make an indexed consumer apply the partition shift twice.
  if (Opts.artifact == ArtifactKind::SplitDwarf) {
    DwarfPackageIndexes Indexes;
    if (!finalizeDwarfPackage(
            PartDwarfs,
            [&](unsigned Idx) {
              return Idx < MergedSections.size()
                         ? MutableArrayRef<char>(MergedSections[Idx].Data)
                         : MutableArrayRef<char>();
            },
            /*IsLittleEndian=*/true, Indexes))
      return false;

    auto AddIndex = [&](StringRef Name, SmallVector<char, 0> Data) {
      if (Data.empty())
        return true;
      if (SectionIndex.find(Name) != SectionIndex.end())
        return false;
      MergedSection Section;
      Section.Name = Name.str();
      memset(&Section.Template, 0, sizeof(Shdr));
      Section.Template.sh_type = SHT_PROGBITS;
      // Package indexes are split-debug payload and must never enter the
      // linked image if a DWP is accidentally passed to the linker.
      Section.Template.sh_flags = SHF_EXCLUDE;
      Section.Template.sh_addralign = 4;
      Section.Data = std::move(Data);
      Section.VirtualSize = Section.Data.size();
      SectionIndex[Name].push_back(MergedSections.size());
      MergedSections.push_back(std::move(Section));
      return true;
    };
    if (!AddIndex(".debug_cu_index", std::move(Indexes.CompileUnits)) ||
        !AddIndex(".debug_tu_index", std::move(Indexes.TypeUnits)))
      return false;
  }

  // Android's module linker script supplies a small loader-facing ABI that a
  // plain relocatable merge otherwise lacks.  Synthesize one canonical set per
  // output; partial `.o` links keep it valid for a later final `.ko` merge.
  if (Opts.androidKernelModule) {
    auto EnsureSection =
        [&](StringRef Name, uint64_t RequiredFlags,
            uint64_t RequiredAlign) -> std::optional<unsigned> {
      auto It = SectionIndex.find(Name);
      if (It == SectionIndex.end()) {
        Shdr Header{};
        Header.sh_type = SHT_PROGBITS;
        Header.sh_flags = RequiredFlags;
        Header.sh_addralign = RequiredAlign;
        return findOrCreateSection(Name, Header);
      }
      // One output name cannot describe incompatible section types/flags.
      // findOrCreateSection intentionally keeps those as separate candidates;
      // the kernel module ABI requires exactly one canonical range.
      if (It->second.size() != 1)
        return std::nullopt;
      MergedSection &Section = MergedSections[It->second.front()];
      if (Section.Template.sh_type != SHT_PROGBITS)
        return std::nullopt;
      Section.Template.sh_flags |= RequiredFlags;
      Section.Template.sh_addralign =
          std::max<uint64_t>(Section.Template.sh_addralign, RequiredAlign);
      return It->second.front();
    };

    auto Versions = EnsureSection("__versions", SHF_ALLOC, 8);
    if (!Versions) {
      errs() << "neverc: Android module merge: incompatible __versions "
                "section\n";
      return false;
    }
    // struct modversion_info is a fixed 64-byte loader ABI record. Enforce the
    // final concatenated table shape at the producer's non-optional
    // finalization boundary so Options::verify=false cannot commit a module the
    // independent verifier (and kernel-side consumers) reject.
    const uint64_t VersionsSize = MergedSections[*Versions].Data.size();
    if (VersionsSize % AndroidModuleVersionEntrySize != 0) {
      errs() << "neverc: Android module merge: __versions size must be a "
                "multiple of 64 bytes\n";
      return false;
    }
    auto AllocTags =
        EnsureSection(".codetag.alloc_tags", SHF_ALLOC | SHF_WRITE, 8);
    if (!AllocTags) {
      errs() << "neverc: Android module merge: incompatible "
                ".codetag.alloc_tags section\n";
      return false;
    }

    const uint64_t AllocTagsSize =
        MergedSections[*AllocTags].Template.sh_type == SHT_NOBITS
            ? MergedSections[*AllocTags].VirtualSize
            : MergedSections[*AllocTags].Data.size();
    auto DefineBoundary = [&](StringRef Name, uint64_t Value) {
      for (const Sym &Local : LocalSyms) {
        if (Local.st_name < SymStrTab.Data.size() &&
            StringRef(SymStrTab.Data.data() + Local.st_name) == Name) {
          errs() << "neverc: Android module merge: local symbol '" << Name
                 << "' conflicts with the alloc_tags boundary\n";
          return false;
        }
      }
      auto Existing = GlobalMap.find(Name);
      if (Existing != GlobalMap.end()) {
        Sym &Symbol = GlobalSyms[Existing->second.SlotIdx];
        if (Symbol.st_shndx != SHN_UNDEF &&
            (Symbol.st_shndx != *AllocTags + 1 || Symbol.st_value != Value)) {
          errs() << "neverc: Android module merge: symbol '" << Name
                 << "' conflicts with the alloc_tags boundary\n";
          return false;
        }
        Symbol.st_shndx = *AllocTags + 1;
        Symbol.st_value = Value;
        Symbol.st_size = 0;
        Symbol.setBindingAndType(STB_GLOBAL, STT_NOTYPE);
        Existing->second.Pri = PRI_GLOBAL_DEF;
        Existing->second.Strong = true;
        return true;
      }

      Sym Symbol{};
      Symbol.st_name = SymStrTab.add(Name);
      Symbol.st_shndx = *AllocTags + 1;
      Symbol.st_value = Value;
      Symbol.setBindingAndType(STB_GLOBAL, STT_NOTYPE);
      const unsigned Slot = GlobalSyms.size();
      GlobalSyms.push_back(Symbol);
      GlobalMap.try_emplace(Name, GlobalDedup{Slot, PRI_GLOBAL_DEF, true});
      return true;
    };

    if (!DefineBoundary("__start_alloc_tags", 0) ||
        !DefineBoundary("__stop_alloc_tags", AllocTagsSize))
      return false;

    // Linux exports every !sect_empty() module section under
    // /sys/module/<name>/sections/<section-name>.  Since 6.12, a duplicate name
    // there aborts loading, so refuse even when the optional full verifier is
    // off.  The size here is the laid-out virtual size (covers SHT_NOBITS).
    StringSet<> LoadedSectionNames;
    for (const MergedSection &Section : MergedSections) {
      if (!detail::isLoadedELFModuleSection(Section.Template.sh_flags,
                                            Section.VirtualSize))
        continue;
      if (!LoadedSectionNames.insert(Section.Name).second) {
        errs()
            << "neverc: Android module merge: duplicate loaded section name '"
            << Section.Name << "'\n";
        return false;
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
      // origSym comes from r_info and is attacker-controlled; a reserved
      // DenseMap key would make find() undefined.  Leave newSym = 0 (the
      // undefined-symbol slot) for such a relocation.
      if (!detail::isReservedDenseKey(origSym)) {
        auto It = Maps[RE.PartIdx].SymMap.find(origSym);
        if (It != Maps[RE.PartIdx].SymMap.end())
          newSym = It->second;
        else if (Opts.stripUnneededSymbols && origSym != 0) {
          errs() << "neverc: Android module release relocation symbol has no "
                    "surviving symbol-table mapping\n";
          return false;
        }
      } else if (Opts.stripUnneededSymbols) {
        errs() << "neverc: Android module release relocation uses a reserved "
                  "symbol-table map key\n";
        return false;
      }
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
      if (Name.contains(PcgSymbolMarker)) {
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

  // ----- Harden symbols for a delivered Android `.ko` -----
  // A kernel module is ET_REL and the loader requires its symbol/string tables
  // and relocations.  Consequently `--strip` here must never mean ELF
  // strip-all.  Match llvm-strip's safe ET_REL boundary, then structurally name
  // retained ordinary definitions: after every merge, resolution, PCG
  // demotion, and relocation remap is final, retain symbol zero, all relocation
  // targets, and all defined non-local symbols; remove only other
  // local/undefined symbols.  Undefined imports and loader/CFI ABI names stay
  // exact.  Rebuild `.strtab` so removed and replaced names cannot survive as
  // unreachable bytes.
  if (Opts.stripUnneededSymbols) {
    const unsigned OldSymbolCount = FirstGlobal + GlobalSyms.size();
    DenseSet<unsigned> ReferencedSymbols;
    for (const MergedSection &MS : MergedSections) {
      for (const RelocEntry &RE : MS.Relocs) {
        const unsigned SymbolIndex = RE.Entry.getSymbol();
        if (SymbolIndex >= OldSymbolCount) {
          errs() << "neverc: Android module release strip: relocation "
                    "references out-of-range symbol index "
                 << SymbolIndex << "\n";
          return false;
        }
        ReferencedSymbols.insert(SymbolIndex);
      }
    }

    auto KeepSymbol = [&](const Sym &Symbol, unsigned Index) {
      if (Index == 0 || ReferencedSymbols.contains(Index))
        return true;
      return Symbol.getBinding() != STB_LOCAL && Symbol.st_shndx != SHN_UNDEF;
    };

    auto SymbolClass = [](const Sym &Symbol) {
      using Policy = AndroidKernelModuleSymbolPolicy::SymbolClass;
      if (Symbol.st_shndx == SHN_UNDEF)
        return Policy::Undefined;
      if (Symbol.st_shndx == SHN_COMMON)
        return Policy::Common;
      if (Symbol.st_shndx ==
          AndroidKernelModuleSymbolPolicy::LivePatchSectionIndex)
        return Policy::LivePatch;
      if (Symbol.st_shndx == SHN_ABS)
        return Policy::Absolute;
      return Policy::Defined;
    };

    auto SymbolType = [](const Sym &Symbol) {
      switch (Symbol.getType()) {
      case STT_NOTYPE:
        return ReleaseSymbolType::NoType;
      case STT_OBJECT:
        return ReleaseSymbolType::Object;
      case STT_FUNC:
        return ReleaseSymbolType::Function;
      case STT_SECTION:
        return ReleaseSymbolType::Section;
      case STT_FILE:
        return ReleaseSymbolType::File;
      case STT_TLS:
        return ReleaseSymbolType::TLS;
      case STT_GNU_IFUNC:
        return ReleaseSymbolType::GNUIFunc;
      default:
        return ReleaseSymbolType::FormatExtension;
      }
    };

    auto BindingRank = [](const Sym &Symbol) -> uint32_t {
      switch (Symbol.getBinding()) {
      case STB_GLOBAL:
        return 0;
      case STB_WEAK:
        return 1;
      case STB_LOCAL:
        return 2;
      default:
        return 3 + Symbol.getBinding();
      }
    };

    auto OriginalName = [&](const Sym &Symbol) -> std::optional<StringRef> {
      if (Symbol.st_name >= SymStrTab.Data.size()) {
        errs() << "neverc: Android module release strip: symbol name offset "
                  "is outside .strtab\n";
        return std::nullopt;
      }
      const char *Begin = SymStrTab.Data.data() + Symbol.st_name;
      const size_t Available = SymStrTab.Data.size() - Symbol.st_name;
      const size_t Length = strnlen(Begin, Available);
      if (Length == Available) {
        errs() << "neverc: Android module release strip: symbol name is not "
                  "NUL-terminated in .strtab\n";
        return std::nullopt;
      }
      return StringRef(Begin, Length);
    };

    struct RetainedSymbol {
      const Sym *Symbol;
      unsigned OldIndex;
      unsigned FinalIndex;
      bool IsLocal;
    };
    SmallVector<RetainedSymbol, 64> Retained;
    Retained.reserve(OldSymbolCount);
    for (unsigned I = 0; I < LocalSyms.size(); ++I) {
      if (!KeepSymbol(LocalSyms[I], I))
        continue;
      Retained.push_back(
          {&LocalSyms[I], I, static_cast<unsigned>(Retained.size()), true});
    }

    const unsigned PrunedFirstGlobal = Retained.size();
    for (unsigned I = 0; I < GlobalSyms.size(); ++I) {
      const unsigned OldIndex = FirstGlobal + I;
      if (!KeepSymbol(GlobalSyms[I], OldIndex))
        continue;
      Retained.push_back({&GlobalSyms[I], OldIndex,
                          static_cast<unsigned>(Retained.size()), false});
    }

    SmallVector<ReleaseSectionDescriptor, 32> ReleaseSections;
    ReleaseSections.reserve(MergedSections.size());
    for (unsigned I = 0; I < MergedSections.size(); ++I) {
      const MergedSection &Section = MergedSections[I];
      const uint64_t FinalSize = Section.Template.sh_type == SHT_NOBITS
                                     ? Section.VirtualSize
                                     : Section.Data.size();
      ReleaseSections.push_back(
          {I + 1, I + 1, Section.Template.sh_addralign, FinalSize,
           (Section.Template.sh_flags & SHF_ALLOC) != 0,
           (Section.Template.sh_flags & SHF_EXECINSTR) != 0});
    }

    SmallVector<ReleaseSymbolDescriptor, 64> ReleaseSymbols;
    ReleaseSymbols.reserve(Retained.size());
    for (const RetainedSymbol &Entry : Retained) {
      const Sym &Symbol = *Entry.Symbol;
      auto Name = OriginalName(Symbol);
      if (!Name)
        return false;
      const auto Class = SymbolClass(Symbol);
      bool PreserveName = false;
      if (Class == AndroidKernelModuleSymbolPolicy::SymbolClass::Defined &&
          Symbol.st_shndx > 0 && Symbol.st_shndx <= MergedSections.size())
        PreserveName =
            AndroidKernelModuleSymbolPolicy::preservesSymbolNamesInSection(
                MergedSections[Symbol.st_shndx - 1].Name);
      ReleaseSymbols.push_back(
          {Entry.FinalIndex, *Name, Class, SymbolType(Symbol),
           static_cast<uint64_t>(
               Class == AndroidKernelModuleSymbolPolicy::SymbolClass::Defined
                   ? Symbol.st_shndx
                   : 0),
           Symbol.st_value, Symbol.st_size, BindingRank(Symbol),
           static_cast<uint32_t>(Symbol.st_other), PreserveName});
    }

    auto RenamePlan =
        planAndroidKernelReleaseNames(ReleaseSections, ReleaseSymbols);
    if (!RenamePlan) {
      errs() << "neverc: Android module release strip: "
             << toString(RenamePlan.takeError()) << "\n";
      return false;
    }

    SmallVector<StringRef, 64> PlannedNames(Retained.size());
    SmallVector<bool, 64> HasPlannedName(Retained.size(), false);
    for (const ReleaseSymbolRename &Rename : *RenamePlan) {
      if (Rename.SymbolID >= PlannedNames.size() ||
          HasPlannedName[Rename.SymbolID]) {
        errs() << "neverc: Android module release strip: invalid rename plan "
                  "symbol identity\n";
        return false;
      }
      PlannedNames[Rename.SymbolID] = Rename.OutputName;
      HasPlannedName[Rename.SymbolID] = true;
    }
    if (llvm::any_of(HasPlannedName, [](bool Present) { return !Present; })) {
      errs()
          << "neverc: Android module release strip: incomplete rename plan\n";
      return false;
    }

    for (const RetainedSymbol &Entry : Retained) {
      const StringRef Original =
          ReleaseSymbols[Entry.FinalIndex].OriginalName;
      const StringRef Release = PlannedNames[Entry.FinalIndex];
      if (!Original.empty() && Original != Release)
        PendingReleaseSymbolMap.Symbols.push_back(
            {Original.str(), Release.str()});
    }

    // The complete plan is now immutable.  Only at this point build replacement
    // symbol/string tables and then commit the relocation-index remap.
    detail::DedupStrTab PrunedStrTab;
    SmallVector<Sym, 64> PrunedLocals, PrunedGlobals;
    DenseMap<unsigned, unsigned> PrunedIndex;
    for (const RetainedSymbol &Entry : Retained) {
      Sym Copy = *Entry.Symbol;
      Copy.st_name = PrunedStrTab.add(PlannedNames[Entry.FinalIndex]);
      PrunedIndex[Entry.OldIndex] = Entry.FinalIndex;
      if (Entry.IsLocal)
        PrunedLocals.push_back(Copy);
      else
        PrunedGlobals.push_back(Copy);
    }

    for (MergedSection &MS : MergedSections) {
      for (RelocEntry &RE : MS.Relocs) {
        const unsigned OldIndex = RE.Entry.getSymbol();
        auto It = PrunedIndex.find(OldIndex);
        if (It == PrunedIndex.end()) {
          errs() << "neverc: Android module release strip: refusing to "
                    "remove a relocation-required symbol\n";
          return false;
        }
        RE.Entry.setSymbolAndType(It->second, RE.Entry.getType());
      }
    }

    LocalSyms = std::move(PrunedLocals);
    GlobalSyms = std::move(PrunedGlobals);
    FirstGlobal = PrunedFirstGlobal;
    SymStrTab = std::move(PrunedStrTab);
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
  AndroidKernelModuleSectionPolicy::initializeCanonicalMetadataHeader(
      OutSections[0].Hdr,
      AndroidKernelModuleSectionPolicy::CanonicalMetadataKind::Null);

  for (auto &MS : MergedSections) {
    OutSection Out;
    Out.Hdr = MS.Template;
    Out.Hdr.sh_name = ShStrTab.add(MS.Name);
    Out.Data = std::move(MS.Data);
    Out.VirtualSize = MS.VirtualSize;
    OutSections.push_back(std::move(Out));
  }

  // Partition object writers are forced to emit uncompressed debug sections.
  // Compress only after concatenation and Split-DWARF rebasing, producing one
  // canonical frame per final section.  A round trip is required before the
  // bytes become observable so an unavailable/broken codec can only trigger
  // the serial fallback, never commit a corrupt debug artifact.
  if (Opts.debugCompression != DebugCompressionType::None) {
    const compression::Format CompressionFormat =
        compression::formatFor(Opts.debugCompression);
    if (const char *Reason =
            compression::getReasonIfUnsupported(CompressionFormat)) {
      errs() << "neverc: cannot compress merged DWARF: " << Reason << "\n";
      return false;
    }

    using Chdr = typename ELFT::Chdr;
    const uint32_t ChType = Opts.debugCompression == DebugCompressionType::Zlib
                                ? ELFCOMPRESS_ZLIB
                                : ELFCOMPRESS_ZSTD;
    for (unsigned I = 1; I < OutSections.size(); ++I) {
      OutSection &S = OutSections[I];
      const StringRef Name = MergedSections[I - 1].Name;
      if (!Name.starts_with(".debug_") || (S.Hdr.sh_flags & SHF_ALLOC) ||
          S.Hdr.sh_type == SHT_NOBITS || S.Data.empty())
        continue;
      if (S.Hdr.sh_flags & SHF_COMPRESSED)
        return false;

      ArrayRef<uint8_t> Uncompressed(
          reinterpret_cast<const uint8_t *>(S.Data.data()), S.Data.size());
      SmallVector<uint8_t, 0> Compressed;
      compression::compress(compression::Params(CompressionFormat),
                            Uncompressed, Compressed);
      if (Uncompressed.size() <= sizeof(Chdr) + Compressed.size())
        continue;

      SmallVector<uint8_t, 0> RoundTrip;
      if (Error E = compression::decompress(CompressionFormat, Compressed,
                                            RoundTrip, Uncompressed.size())) {
        consumeError(std::move(E));
        return false;
      }
      if (RoundTrip.size() != Uncompressed.size() ||
          !std::equal(RoundTrip.begin(), RoundTrip.end(), Uncompressed.begin()))
        return false;

      Chdr Header{};
      Header.ch_type = ChType;
      Header.ch_reserved = 0;
      Header.ch_size = Uncompressed.size();
      Header.ch_addralign = Opts.stripUnneededSymbols
                                ? normalizeReleaseAlignment(S.Hdr.sh_addralign)
                                : clampAlign(S.Hdr.sh_addralign);
      SmallVector<char, 0> Encoded;
      Encoded.append(reinterpret_cast<const char *>(&Header),
                     reinterpret_cast<const char *>(&Header) + sizeof(Header));
      Encoded.append(reinterpret_cast<const char *>(Compressed.data()),
                     reinterpret_cast<const char *>(Compressed.data()) +
                         Compressed.size());
      S.Data = std::move(Encoded);
      S.VirtualSize = S.Data.size();
      S.Hdr.sh_flags |= SHF_COMPRESSED;
      // ELFT::Chdr uses packed endian-aware integer wrappers and therefore has
      // C++ alignment 1. The ELF64 ABI still requires the on-disk compression
      // header to be 8-byte aligned.
      S.Hdr.sh_addralign = alignof(Elf64_Chdr);
    }
  }

  // Remap SHF_LINK_ORDER sh_link through exact input-section identity now that
  // every partition's SecMap is complete. Output section index == SecMap value
  // (slot 0 is the null section). All contributors must resolve to one ID.
  for (unsigned m = 0; m < MergedSections.size(); ++m) {
    const auto &MS = MergedSections[m];
    if (MS.LinkOrderContributions.empty())
      continue;
    std::optional<unsigned> FinalTarget;
    for (const LinkOrderContribution &Contribution :
         MS.LinkOrderContributions) {
      const unsigned Partition = Contribution.Partition;
      const unsigned InputSection = Contribution.LinkedInputSection;
      if (Partition >= Maps.size())
        return false;
      auto Target = Maps[Partition].SecMap.find(InputSection);
      if (Target == Maps[Partition].SecMap.end() || Target->second == 0) {
        errs() << "neverc: relocatable merge: SHF_LINK_ORDER section '"
               << MS.Name
               << "' targets a section absent from the merged output; "
                  "refusing\n";
        return false;
      }
      if (!FinalTarget)
        FinalTarget = Target->second;
      else if (*FinalTarget != Target->second) {
        errs() << "neverc: relocatable merge: SHF_LINK_ORDER section '"
               << MS.Name
               << "' has contributors with differing final target sections; "
                  "refusing\n";
        return false;
      }
    }
    OutSections[m + 1].Hdr.sh_link = *FinalTarget;
  }

  unsigned SymTabIdx = OutSections.size();
  {
    OutSection S;
    AndroidKernelModuleSectionPolicy::initializeCanonicalMetadataHeader(
        S.Hdr, AndroidKernelModuleSectionPolicy::CanonicalMetadataKind::Symtab,
        ShStrTab.add(".symtab"), /*Link=*/0, FirstGlobal);
    S.Data.assign(reinterpret_cast<const char *>(FinalSyms.data()),
                  reinterpret_cast<const char *>(FinalSyms.data()) +
                      FinalSyms.size() * sizeof(Sym));
    OutSections.push_back(std::move(S));
  }

  unsigned StrTabIdx = OutSections.size();
  {
    OutSection S;
    AndroidKernelModuleSectionPolicy::initializeCanonicalMetadataHeader(
        S.Hdr, AndroidKernelModuleSectionPolicy::CanonicalMetadataKind::Strtab,
        ShStrTab.add(".strtab"));
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
    AndroidKernelModuleSectionPolicy::initializeCanonicalMetadataHeader(
        S.Hdr, AndroidKernelModuleSectionPolicy::CanonicalMetadataKind::Rela,
        ShStrTab.add((".rela" + MS.Name).c_str()), SymTabIdx, m + 1);
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
    AndroidKernelModuleSectionPolicy::initializeCanonicalMetadataHeader(
        S.Hdr,
        AndroidKernelModuleSectionPolicy::CanonicalMetadataKind::Shstrtab,
        nameOff);
    S.Data.assign(ShStrTab.Data.begin(), ShStrTab.Data.end());
    OutSections.push_back(std::move(S));
  }

  // e_shnum is a 16-bit field; a section count >= SHN_LORESERVE requires the
  // SHN_XINDEX escape (e_shnum=0, real count in section[0].sh_size) which this
  // -r merge does not emit.  Writing the count straight into the 16-bit field
  // would silently truncate and produce a corrupt object — the same
  // valid-looking-but-wrong failure class the verifier exists to stop, but at a
  // layer the verifier (which trusts the parsed header) cannot see.  Refuse
  // instead so the caller falls back to serial codegen / a real linker.  Only
  // reachable on a single link of tens of thousands of per-function sections
  // (FunctionSections); the kernel-module mergeSections path folds to a
  // handful.
  if (OutSections.size() >= ELF::SHN_LORESERVE) {
    errs() << "neverc: relocatable merge produced " << OutSections.size()
           << " sections, exceeding the ELF e_shnum limit ("
           << ELF::SHN_LORESERVE << "); refusing to emit a truncated object\n";
    return false;
  }

  // ----- Layout -----
  // All section addresses are 0 in -r mode (ElfImageEmitter.cpp:1552).
  uint64_t Off = sizeof(Ehdr);
  for (unsigned i = 1; i < OutSections.size(); ++i) {
    uint64_t Align =
        Opts.stripUnneededSymbols
            ? normalizeReleaseAlignment(OutSections[i].Hdr.sh_addralign)
            : clampAlign(OutSections[i].Hdr.sh_addralign);
    if (!checkedAlignOffset(Off, Align, Off))
      return false;
    OutSections[i].Hdr.sh_offset = Off;
    if (OutSections[i].Hdr.sh_type == SHT_NOBITS) {
      OutSections[i].Hdr.sh_size = OutSections[i].VirtualSize;
    } else {
      OutSections[i].Hdr.sh_size = OutSections[i].Data.size();
      if (OutSections[i].Data.size() >
          std::numeric_limits<uint64_t>::max() - Off)
        return false;
      Off += OutSections[i].Data.size();
    }
  }
  if (!checkedAlignOffset(Off, 8, Off))
    return false;
  uint64_t ShOff = Off;

  // ----- Write output -----
  // Ported from LLD ElfImageEmitter::writeHeader / writeResult.
  SmallVector<char, 0> OutBuf;
  if (OutSections.size() > std::numeric_limits<uint64_t>::max() / sizeof(Shdr))
    return false;
  const uint64_t SectionTableBytes = OutSections.size() * sizeof(Shdr);
  if (SectionTableBytes > std::numeric_limits<uint64_t>::max() - ShOff)
    return false;
  const uint64_t OutputSize = ShOff + SectionTableBytes;
  if (OutputSize > std::numeric_limits<size_t>::max() ||
      (Opts.stripUnneededSymbols && OutputSize > ReleaseMaterializedBudget))
    return false;
  OutBuf.resize(OutputSize, 0);

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

  // Self-verify before committing: an independent re-parse content-anchors
  // every uniquely-named defined symbol back to its input bytes.  On any
  // divergence we refuse to write the object and return false, so the caller
  // falls back to the proven path (serial codegen) or errors loudly rather
  // than emitting a valid-looking but semantically wrong .o.
  if (Opts.verify) {
    SmallVector<StringRef, 8> Views;
    Views.reserve(Buffers.size());
    for (const auto &B : Buffers)
      Views.push_back(StringRef(B.data(), B.size()));
    std::string VErr;
    if (!verifyMerge(ArrayRef<StringRef>(Views), ArrayRef<char>(OutBuf),
                     Format::ELF64LE, Opts, &VErr)) {
      errs() << "neverc: relocatable merge self-check failed: " << VErr << "\n";
      return false;
    }
  }

  OS.write(OutBuf.data(), OutBuf.size());
  if (Opts.stripUnneededSymbols && Opts.releaseSymbolMap) {
    PendingReleaseSymbolMap.ImageSHA256 = SHA256::hash(ArrayRef<uint8_t>(
        reinterpret_cast<const uint8_t *>(OutBuf.data()), OutBuf.size()));
    *Opts.releaseSymbolMap = std::move(PendingReleaseSymbolMap);
  }
  return true;
}

} // anonymous namespace

bool mergeELF64LEObjects(ArrayRef<SmallVector<char, 0>> Buffers,
                         raw_pwrite_stream &OS, const Options &Opts) {
  return detail::runMergeSafely(
      [&]() { return mergeELF64LEImpl(Buffers, OS, Opts); });
}

bool mergeELF64LEObjects(ArrayRef<StringRef> Buffers, raw_pwrite_stream &OS,
                         const Options &Opts) {
  return detail::runMergeSafely(
      [&]() { return mergeELF64LEImpl(Buffers, OS, Opts); });
}

} // namespace neverc::merge
