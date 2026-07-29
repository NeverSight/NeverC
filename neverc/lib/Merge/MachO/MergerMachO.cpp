//===- MergerMachO.cpp - MachO64 relocatable merge (ported from LLD -r) ===//
//
// Relocatable object merger for Mach-O 64.  The algorithms here are
// ported from the LLD MachO backend's `-r` (MH_OBJECT output) code paths:
//
//   Section merging   ← Layout/ConcatOutputSection.cpp (section grouping
//                        by (segment, section) key)
//   Symbol resolution ← Symbols/SymbolTable.cpp (GLOBAL>WEAK>UNDEF,
//                        defined beats undefined, binding preserved in -r)
//   Relocation remap  ← Layout/Relocations.cpp (no dynamic reloc
//                        synthesis for MH_OBJECT, just index remap)
//   Output writing    ← Emit/MachOImageEmitter.cpp (MH_OBJECT filetype,
//                        LC_SYMTAB + LC_DYSYMTAB, no LC_MAIN/LC_DYLD_INFO)
//   .eh_frame         ← kept as regular section in -r (LLD MachO:
//                        __eh_frame is not split/deduped for MH_OBJECT)
//   __compact_unwind  ← kept as regular section (pure C, no LSDA dedup)
//
// ARM64 `ARM64_RELOC_ADDEND` pseudo relocations and `__common` → `__bss`
// rename for segment-relative stability are handled.
//
// C++ specific logic (COMDAT, weak_def_can_be_hidden for templates,
// __cstring dedup) is intentionally removed.
//
//===------------------------------------------------------------------===//

#include "Common/DwarfRebase.h"
#include "Common/MergerCommon.h"
#include "neverc/Merge/Merger.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/Object/MachO.h"
#include "llvm/Support/Error.h"

#include <cstdint>
#include <cstring>

using namespace llvm;

namespace neverc::merge {

namespace {

template <typename BufT>
bool mergeMachO64Impl(ArrayRef<BufT> Buffers, raw_pwrite_stream &OS,
                      const Options &Opts) {
  using namespace llvm::object;
  namespace MO = llvm::MachO;

  struct SectionKey {
    char SegName[16];
    char SectName[16];
    bool operator==(const SectionKey &O) const {
      return memcmp(SegName, O.SegName, 16) == 0 &&
             memcmp(SectName, O.SectName, 16) == 0;
    }
  };

  struct RelocEntry {
    MO::relocation_info Info;
    unsigned PartIdx;
  };

  struct MergedSection {
    SectionKey Key;
    uint32_t Align = 0;
    uint32_t Flags = 0;
    SmallVector<char, 0> Data;
    SmallVector<RelocEntry, 0> Relocs;
    uint32_t Reserved1 = 0, Reserved2 = 0;
    // Section attributes are taken from the first contributing partition and
    // never clobbered by later ones (same-named sections from one LTO module
    // share flags; for heterogeneous -r inputs the first wins, matching LLD).
    bool Initialized = false;
    // For zerofill sections (S_ZEROFILL / S_GB_ZEROFILL /
    // S_THREAD_LOCAL_ZEROFILL): the section has no on-disk content but does
    // occupy memory. We track its memory size separately and never append
    // to Data; PartOffset comes from VirtualSize instead.
    uint64_t VirtualSize = 0;
  };

  SmallVector<MergedSection, 16> MergedSections;
  StringMap<unsigned> SectionIndex;
  detail::DedupStrTab StrTab;
  SmallVector<MO::nlist_64, 64> AllSyms;

  auto findOrCreateSection = [&](const SectionKey &K) -> unsigned {
    SmallString<33> MapKey(StringRef(K.SegName, 16));
    MapKey.push_back('\0');
    MapKey.append(StringRef(K.SectName, 16));
    auto It = SectionIndex.find(MapKey);
    if (It != SectionIndex.end())
      return It->second;
    unsigned Idx = MergedSections.size();
    MergedSections.push_back({});
    MergedSections.back().Key = K;
    SectionIndex[MapKey] = Idx;
    return Idx;
  };

  uint32_t CPUType = 0, CPUSubType = 0;
  bool HaveArch = false;

  struct PerPartition {
    DenseMap<unsigned, unsigned> SecMap;
    DenseMap<unsigned, unsigned> SymMap;
    DenseMap<unsigned, uint64_t> OrigSecAddr;
    DenseMap<unsigned, uint64_t> SecOff;
  };
  SmallVector<PerPartition, 8> Maps;
  SmallVector<PartitionDwarf, 8> PartDwarfs;
  PartDwarfs.resize(Buffers.size());

  // External symbol dedup — ported from LLD MachO SymbolTable::addDefined /
  // SymbolTable::addUndefined.  In -r mode, defined GLOBAL beats WEAK beats
  // UNDEF.  Each partition emits defs for its own symbols plus undefs for
  // cross-partition references; we resolve them here so the merged object
  // has exactly one entry per name.
  enum MachOSymPri : uint8_t { MP_UNDEF = 0, MP_WEAK_DEF = 1, MP_DEF = 2 };
  struct MachOSymDedup {
    unsigned SlotIdx;
    MachOSymPri Pri;
  };
  StringMap<MachOSymDedup> ExtNameToIdx;

  for (unsigned p = 0; p < Buffers.size(); ++p) {
    if (Buffers[p].empty())
      continue;
    Maps.resize(p + 1);
    auto &PM = Maps[p];

    // Resource-exhaustion guard (found by the merge fuzzer): LLVM's
    // MachOObjectFile constructor walks `ncmds` load commands, pushing one
    // LoadCommandInfo into a SmallVector per iteration.  A load command with
    // cmdsize 0 does not advance the cursor, so a crafted header that pairs a
    // huge ncmds with such a command makes that vector grow to ~ncmds*16 bytes
    // (a ~640 MB allocation from a 1 KB input) before a single byte is checked
    // against the buffer.  A conformant object's load commands each occupy at
    // least sizeof(load_command) (8) bytes and all live in the region right
    // after the header, so there can be no more than (size - header)/8 of them.
    // Refuse anything claiming more — it cannot be a real object — before
    // handing the bytes to the eager parser, mirroring the cputype / 255-section
    // guards.  Never false-rejects (the bound holds for every valid Mach-O); the
    // parallel-codegen path emits a truthful ncmds and never trips it, only the
    // general -r path over hostile objects can.
    {
      const auto &B = Buffers[p];
      if (B.size() >= 20) {
        uint32_t Magic, NCmds;
        memcpy(&Magic, B.data(), 4);
        memcpy(&NCmds, B.data() + 16, 4); // ncmds at offset 16 in 32/64 headers
        bool Is64 = Magic == MO::MH_MAGIC_64 || Magic == MO::MH_CIGAM_64;
        bool Swap = Magic == MO::MH_CIGAM || Magic == MO::MH_CIGAM_64;
        bool Thin = Is64 || Magic == MO::MH_MAGIC || Magic == MO::MH_CIGAM;
        if (Thin) {
          if (Swap)
            NCmds = (NCmds << 24) | ((NCmds & 0xFF00u) << 8) |
                    ((NCmds >> 8) & 0xFF00u) | (NCmds >> 24);
          uint64_t HdrSize = Is64 ? 32 : 28;
          if (B.size() < HdrSize ||
              NCmds > (B.size() - HdrSize) / sizeof(MO::load_command)) {
            errs() << "neverc: Mach-O relocatable merge: header declares "
                   << NCmds
                   << " load commands, more than the object's " << B.size()
                   << " bytes can hold; refusing to merge\n";
            return false;
          }
        }
      }
    }

    auto Buf = MemoryBufferRef(StringRef(Buffers[p].data(), Buffers[p].size()),
                               "part");
    auto ObjOrErr = ObjectFile::createMachOObjectFile(Buf);
    if (!ObjOrErr) {
      consumeError(ObjOrErr.takeError());
      return false;
    }
    auto &Obj = **ObjOrErr;

    if (!HaveArch) {
      HaveArch = true;
      CPUType = Obj.getHeader().cputype;
      CPUSubType = Obj.getHeader().cpusubtype;
    } else if (Obj.getHeader().cputype != CPUType) {
      // All inputs must share one CPU type.  Mixing (e.g. arm64 + x86_64) would
      // emit one valid mach_header (the first input's cputype) over a body whose
      // later partitions are the wrong ISA, and would also corrupt the
      // IsARM64-gated ARM64_RELOC_ADDEND / in-place fixup logic below.  The
      // parallel-codegen path shares one TargetMachine so it never trips; the
      // general `-r` path over arbitrary objects can, and `ld -r` refuses it
      // too.  Refuse rather than emit a cross-ISA object.
      errs() << "neverc: Mach-O relocatable merge: input has cputype "
             << Obj.getHeader().cputype << " but an earlier input had "
             << CPUType << " (mixed architectures); refusing to merge\n";
      return false;
    }

    unsigned PartSecOrdinal = 1;
    for (const auto &Sec : Obj.sections()) {
      SectionKey Key;
      auto NameOrErr = Sec.getName();
      if (!NameOrErr) {
        // Skipping the section while still bumping PartSecOrdinal would drop its
        // bytes and re-home every symbol that names it to NO_SECT — a silent,
        // incomplete merge.  Refuse instead so the caller falls back to serial
        // codegen / a real linker (mirrors the getContents() handling below and
        // the ELF/COFF "refuse rather than miscompile" stance).
        consumeError(NameOrErr.takeError());
        return false;
      }

      Expected<StringRef> SegNameOrErr =
          Obj.getSectionFinalSegmentName(Sec.getRawDataRefImpl());
      if (!SegNameOrErr) {
        // Defaulting to "__TEXT" on failure could fold a __DATA/__bss section
        // into the wrong segment, mis-placing its symbols; refuse instead.
        consumeError(SegNameOrErr.takeError());
        return false;
      }
      StringRef SegName = *SegNameOrErr;

      memset(&Key, 0, sizeof(Key));
      memcpy(Key.SegName, SegName.data(), std::min(SegName.size(), size_t(16)));
      StringRef SectName = *NameOrErr;
      // Strip segment prefix if present.
      if (SectName.contains(','))
        SectName = SectName.split(',').second;

      // The call graph profile addresses its functions by symbol table index,
      // which this merge renumbers; see isMachOCallGraphProfileSection.  The
      // ordinal still advances so the sections after it keep the numbers this
      // input's symbols and relocations use, and no SecMap entry is left
      // behind: anything defined here would be re-homed to NO_SECT, which is
      // the right answer for a section codegen puts no symbol in.
      if (detail::isMachOCallGraphProfileSection(SegName, SectName)) {
        ++PartSecOrdinal;
        continue;
      }

      // Rename "__common" -> "__bss" so the host linker does not treat the
      // merged zerofill data as C COMMON storage (which it would freely
      // re-pack per-symbol, discarding our precomputed per-partition
      // offsets, and miscomputing addends in r_extern relocations that
      // point into a re-packed common slot).  "__bss" has the same
      // S_ZEROFILL semantics for memory layout but linker keeps our
      // section-relative offsets intact.
      if (SegName == "__DATA" && SectName == "__common")
        SectName = "__bss";
      memcpy(Key.SectName, SectName.data(),
             std::min(SectName.size(), size_t(16)));

      unsigned MIdx = findOrCreateSection(Key);
      auto &MS = MergedSections[MIdx];

      const MO::section_64 &S64 = Obj.getSection64(Sec.getRawDataRefImpl());

      if (!MS.Initialized) {
        MS.Flags = S64.flags;
        MS.Reserved1 = S64.reserved1;
        MS.Reserved2 = S64.reserved2;
        MS.Initialized = true;
      }
      {
        constexpr uint32_t MaxAlignExp = 20;
        uint32_t SafeExp = std::min(S64.align, MaxAlignExp);
        if (SafeExp > MS.Align)
          MS.Align = SafeExp;
      }

      // Distinguish zerofill (BSS-like) sections from regular sections.
      // Zerofill sections have no on-disk content; their layout uses
      // VirtualSize, not Data.size().
      uint32_t SecType = S64.flags & MO::SECTION_TYPE;
      bool IsZerofill =
          (SecType == MO::S_ZEROFILL || SecType == MO::S_GB_ZEROFILL ||
           SecType == MO::S_THREAD_LOCAL_ZEROFILL);

      uint64_t SecAlign = 1ULL << std::min(MS.Align, 20u);
      uint64_t PartOffset;
      if (IsZerofill) {
        PartOffset = MS.VirtualSize;
        uint64_t Padding = (SecAlign - (PartOffset % SecAlign)) % SecAlign;
        PartOffset += Padding;
        MS.VirtualSize = PartOffset + S64.size;
      } else {
        PartOffset = MS.Data.size();
        uint64_t Padding = (SecAlign - (PartOffset % SecAlign)) % SecAlign;
        MS.Data.resize(MS.Data.size() + Padding, 0);
        PartOffset = MS.Data.size();
        auto ContentsOrErr = Sec.getContents();
        if (!ContentsOrErr) {
          // PartOffset is already committed; skipping the bytes would shift
          // every later section and mis-place this section's symbols.  Refuse
          // rather than emit a corrupted object.
          consumeError(ContentsOrErr.takeError());
          return false;
        }
        MS.Data.append(ContentsOrErr->begin(), ContentsOrErr->end());
      }

      PM.SecMap[PartSecOrdinal] = MIdx + 1;
      PM.OrigSecAddr[PartSecOrdinal] = S64.addr;
      PM.SecOff[PartSecOrdinal] = PartOffset;

      // DWARF sections address each other with plain integers rather than
      // relocations, so note where this partition landed in each of them; the
      // offsets are rewritten once every partition has been appended.
      // The size is what was actually appended rather than the value in the
      // section header, so a header that overstates its contents cannot make
      // the slice run into the next partition's bytes.
      PartDwarfs[p].record(SectName, MIdx, PartOffset,
                           IsZerofill ? 0 : MS.Data.size() - PartOffset);

      for (const auto &R : Sec.relocations()) {
        MO::relocation_info RI;
        auto RawRI = Obj.getRelocation(R.getRawDataRefImpl());
        memcpy(&RI, &RawRI, sizeof(RI));
        RI.r_address += PartOffset;
        MS.Relocs.push_back({RI, p});
      }

      PartSecOrdinal++;
    }

    StringRef PartStrTab = Obj.getStringTableData();
    unsigned SymIdx = 0;
    for (const auto &Sym : Obj.symbols()) {
      MO::nlist_64 NL = Obj.getSymbol64TableEntry(Sym.getRawDataRefImpl());

      MO::nlist_64 OutSym;
      memset(&OutSym, 0, sizeof(OutSym));

      // Bound the name to the string table extent.  NL.n_strx only guarantees
      // the *start* is in range; unlike ELF (llvm's getStringTableForSymtab
      // errors on a non-NUL-terminated strtab), MachOObjectFile::
      // getStringTableData() returns the raw [stroff, stroff+strsize) slice with
      // no trailing-NUL guarantee.  A plain StringRef(const char *) here would
      // strlen() past the end of the input buffer on a malformed/hostile object
      // (an out-of-bounds read).  strnlen-bound it exactly as the independent
      // verifier already does (Verify/MergerVerify.cpp) so the merger and its checker
      // share one safe convention.  Found by the merge fuzzer.
      StringRef Name;
      if (NL.n_strx < PartStrTab.size())
        Name = StringRef(PartStrTab.data() + NL.n_strx,
                         strnlen(PartStrTab.data() + NL.n_strx,
                                 PartStrTab.size() - NL.n_strx));
      OutSym.n_strx = StrTab.add(Name);
      OutSym.n_type = NL.n_type;
      OutSym.n_desc = NL.n_desc;
      OutSym.n_value = NL.n_value;

      if (NL.n_sect != 0) {
        auto It = PM.SecMap.find(NL.n_sect);
        if (It != PM.SecMap.end()) {
          OutSym.n_sect = It->second;
          unsigned mIdx = It->second - 1;
          if (mIdx < MergedSections.size()) {
            uint64_t origAddr = 0;
            auto AI = PM.OrigSecAddr.find(NL.n_sect);
            if (AI != PM.OrigSecAddr.end())
              origAddr = AI->second;
            OutSym.n_value -= origAddr;
            auto OffIt = PM.SecOff.find(NL.n_sect);
            if (OffIt != PM.SecOff.end())
              OutSym.n_value += OffIt->second;
          }
        } else {
          OutSym.n_sect = 0;
        }
      } else {
        OutSym.n_sect = 0;
      }

      // Symbols that we externalized only to satisfy cross-partition
      // references (the .__pcg<hash> suffix) are an implementation detail
      // and must not bloat the final binary's symbol table.
      bool IsPcgInternal = Name.contains(PcgSymbolMarker);

      bool IsExt = (OutSym.n_type & MO::N_EXT) != 0;
      bool IsDefined = IsExt && (OutSym.n_type & MO::N_TYPE) != MO::N_UNDF;
      if (IsExt && IsPcgInternal && IsDefined) {
        // Defined .__pcg symbol: turn it back into a local definition so
        // the host linker doesn't export it (and -dead_strip drops the
        // hidden alias).  Reloc consumers still find it by symbol index.
        OutSym.n_type &= ~MO::N_EXT;
        OutSym.n_type |= MO::N_PEXT;
      }

      if (IsExt && !Name.empty()) {
        // Priority-based dedup: DEF > WEAK_DEF > UNDEF.
        // Ported from LLD MachO SymbolTable resolve logic.
        bool IsWeakDef = IsDefined && (OutSym.n_desc & MO::N_WEAK_DEF);
        MachOSymPri Pri =
            IsDefined ? (IsWeakDef ? MP_WEAK_DEF : MP_DEF) : MP_UNDEF;
        auto [It, Inserted] = ExtNameToIdx.try_emplace(
            Name, MachOSymDedup{(unsigned)AllSyms.size(), Pri});
        if (Inserted) {
          PM.SymMap[SymIdx] = AllSyms.size();
          AllSyms.push_back(OutSym);
        } else {
          if (Pri == MP_DEF && It->second.Pri == MP_DEF) {
            // Two strong (defined, non-weak, external) definitions of one name
            // is an ODR violation that this priority dedup would silently
            // resolve by keeping the first and dropping the second.  The
            // parallel-codegen path never produces it (each external is defined
            // in one partition; .__pcg locals are demoted to N_PEXT and carry
            // unique names), so only a malformed -r input reaches here; refuse
            // rather than silently pick one.
            errs() << "neverc: Mach-O relocatable merge: multiple strong "
                      "definitions of '"
                   << Name << "'; refusing to merge\n";
            return false;
          }
          if (Pri > It->second.Pri) {
            AllSyms[It->second.SlotIdx] = OutSym;
            It->second.Pri = Pri;
          }
          PM.SymMap[SymIdx] = It->second.SlotIdx;
        }
      } else {
        PM.SymMap[SymIdx] = AllSyms.size();
        AllSyms.push_back(OutSym);
      }
      SymIdx++;
    }
  }

  // Every partition has now been appended, so each one's DWARF sections have
  // a known home in the merged output.  Re-point the offsets the units carry
  // for each other; until this runs, every partition after the first reads the
  // first one's abbreviation and string tables.
  if (!rebaseMergedDwarf(
          PartDwarfs,
          [&](unsigned Idx) {
            return Idx < MergedSections.size()
                       ? MutableArrayRef<char>(MergedSections[Idx].Data)
                       : MutableArrayRef<char>();
          },
          /*IsLittleEndian=*/true))
    return false;

  // Remap relocation symbol/section indices.  Non-external relocation
  // data adjustment is deferred to after section layout is computed.
  struct NonExternRelocFixup {
    unsigned MergedSecIdx;
    unsigned RelocIdx;
    unsigned TargetMIdx;
    unsigned OrigSec;
    unsigned PartIdx;
  };
  SmallVector<NonExternRelocFixup, 32> DeferredFixups;

  // Whether the host is ARM64 (mach-o cputype 0x100000C). For ARM64,
  // ARM64_RELOC_ADDEND (r_type==10, r_extern==0) is a pseudo relocation
  // whose r_symbolnum field is *not* a section index but the addend value
  // for the next PAGE21/PAGEOFF12 relocation.  We must leave that alone.
  const bool IsARM64 = (CPUType == MO::CPU_TYPE_ARM64);
  for (unsigned mi = 0; mi < MergedSections.size(); ++mi) {
    auto &MS = MergedSections[mi];
    for (unsigned ri = 0; ri < MS.Relocs.size(); ++ri) {
      auto &RE = MS.Relocs[ri];
      if (RE.PartIdx >= Maps.size())
        continue;
      auto &PM = Maps[RE.PartIdx];
      if (RE.Info.r_extern) {
        auto It = PM.SymMap.find(RE.Info.r_symbolnum);
        if (It != PM.SymMap.end())
          RE.Info.r_symbolnum = It->second;
      } else if (IsARM64 && RE.Info.r_type == MO::ARM64_RELOC_ADDEND) {
        // r_symbolnum carries the addend value, not a section index.
        // Leave it untouched.
      } else {
        unsigned OrigSec = RE.Info.r_symbolnum;
        auto It = PM.SecMap.find(OrigSec);
        if (It != PM.SecMap.end()) {
          unsigned targetMIdx = It->second - 1;
          RE.Info.r_symbolnum = It->second;
          // Only a relocation whose field is a word of its own can be adjusted
          // by adding the layout delta to the bytes it covers; see
          // machOFieldIsWholeWord.  One that patches a field inside an
          // instruction has to be refused rather than rewritten, because the
          // arithmetic would run through the opcode and the registers beside
          // it and the result would still assemble and link.
          if (!RE.Info.r_pcrel && targetMIdx < MergedSections.size()) {
            if (!detail::machOFieldIsWholeWord(CPUType, RE.Info.r_type)) {
              errs() << "neverc: Mach-O relocatable merge: relocation type "
                     << (unsigned)RE.Info.r_type
                     << " patches a field inside an instruction, which cannot "
                        "be adjusted by rewriting the bytes it covers; "
                        "refusing to merge\n";
              return false;
            }
            DeferredFixups.push_back({mi, ri, targetMIdx, OrigSec, RE.PartIdx});
          }
        }
      }
    }
  }

  // Sort symbols: locals, then defined externals, then undefined (LC_DYSYMTAB).
  SmallVector<MO::nlist_64, 64> Locals, ExtDef, Undef;
  for (auto &S : AllSyms) {
    if ((S.n_type & MO::N_EXT) == 0)
      Locals.push_back(S);
    else if ((S.n_type & MO::N_TYPE) == MO::N_UNDF)
      Undef.push_back(S);
    else
      ExtDef.push_back(S);
  }
  SmallVector<MO::nlist_64, 64> FinalSyms;
  FinalSyms.append(Locals.begin(), Locals.end());
  unsigned NLocalSym = FinalSyms.size();
  FinalSyms.append(ExtDef.begin(), ExtDef.end());
  unsigned NExtDefSym = ExtDef.size();
  FinalSyms.append(Undef.begin(), Undef.end());
  unsigned NUndefSym = Undef.size();

  DenseMap<unsigned, unsigned> SymReindex;
  {
    unsigned li = 0, ei = 0, ui = 0;
    for (unsigned i = 0; i < AllSyms.size(); ++i) {
      if ((AllSyms[i].n_type & MO::N_EXT) == 0)
        SymReindex[i] = li++;
      else if ((AllSyms[i].n_type & MO::N_TYPE) == MO::N_UNDF)
        SymReindex[i] = NLocalSym + NExtDefSym + ui++;
      else
        SymReindex[i] = NLocalSym + ei++;
    }
  }
  for (auto &MS : MergedSections)
    for (auto &RE : MS.Relocs)
      if (RE.Info.r_extern) {
        auto It = SymReindex.find(RE.Info.r_symbolnum);
        if (It != SymReindex.end())
          RE.Info.r_symbolnum = It->second;
      }

  unsigned NSects = MergedSections.size();
  // nlist_64::n_sect is a uint8_t: a defined symbol can name at most section
  // 255 (0 == NO_SECT).  More merged sections than that silently truncate every
  // n_sect past 255 to a wrong section number — the same valid-looking-but-wrong
  // corruption class the ELF e_shnum guard and the COFF NumberOfSections guard
  // refuse, and the one format whose count field this merger had left
  // unchecked.  Mach-O -r keys sections by (segment, section) so the
  // parallel-codegen path never approaches the limit, but the general -r path
  // over arbitrary user objects can; refuse rather than miscompile so the caller
  // falls back to serial codegen / a real linker.  Checked before any n_sect is
  // committed to the output (the symbol loop above stored untruncated unsigned
  // values; the truncation would only happen on the write below, which we now
  // never reach).
  if (NSects > 255) {
    errs() << "neverc: Mach-O merge produced " << NSects
           << " sections, exceeding the 8-bit nlist n_sect limit (255); "
              "refusing to emit a truncated object\n";
    return false;
  }
  uint32_t HeaderSize = sizeof(MO::mach_header_64);
  uint32_t SegCmdSize =
      sizeof(MO::segment_command_64) + NSects * sizeof(MO::section_64);
  uint32_t SymTabCmdSize = sizeof(MO::symtab_command);
  uint32_t DySymTabCmdSize = sizeof(MO::dysymtab_command);
  uint32_t LoadCmdsSize = SegCmdSize + SymTabCmdSize + DySymTabCmdSize;
  uint32_t DataStart = HeaderSize + LoadCmdsSize;

  uint64_t CurOff = DataStart;
  struct SectionLayout {
    uint64_t Offset, Size;
    uint32_t RelocOff, NReloc;
    bool IsZerofill;
  };
  SmallVector<SectionLayout, 16> Layouts(NSects);
  // First pass: lay out non-zerofill sections (they occupy on-disk space).
  for (unsigned i = 0; i < NSects; ++i) {
    uint32_t SType = MergedSections[i].Flags & MO::SECTION_TYPE;
    bool IsZf = (SType == MO::S_ZEROFILL || SType == MO::S_GB_ZEROFILL ||
                 SType == MO::S_THREAD_LOCAL_ZEROFILL);
    Layouts[i].IsZerofill = IsZf;
    if (IsZf)
      continue;
    uint64_t Align = 1ULL << std::min(MergedSections[i].Align, 20u);
    CurOff = (CurOff + Align - 1) & ~(Align - 1);
    Layouts[i].Offset = CurOff;
    Layouts[i].Size = MergedSections[i].Data.size();
    CurOff += Layouts[i].Size;
  }
  // Second pass: lay out zerofill sections.  They get a virtual address that
  // continues past the on-disk sections (used for the section header's addr
  // field and for symbol n_value), but occupy NO file space — so their
  // attacker-controlled, file-unbacked VirtualSize must advance a *separate* vm
  // cursor, never the file cursor.  Folding it into CurOff (as the on-disk
  // sections do) placed the relocation tables, symbol table, string table and
  // the output buffer itself that many bytes later, so a crafted __bss size
  // drove SmallVector Out(CurOff) to an allocation-size-too-big abort / OOM —
  // the merge fuzzer found this in mergeMachO64Impl, the exact sibling of the
  // ELF NOBITS materialization crash.  Mach-O requires zerofill sections to come
  // last with file offset 0, which is precisely what this produces.
  uint64_t VmCur = CurOff;
  for (unsigned i = 0; i < NSects; ++i) {
    if (!Layouts[i].IsZerofill)
      continue;
    uint64_t Align = 1ULL << std::min(MergedSections[i].Align, 20u);
    VmCur = (VmCur + Align - 1) & ~(Align - 1);
    Layouts[i].Offset = VmCur;
    Layouts[i].Size = MergedSections[i].VirtualSize;
    VmCur += Layouts[i].Size;
  }

  for (unsigned i = 0; i < NSects; ++i) {
    unsigned NReloc = MergedSections[i].Relocs.size();
    Layouts[i].NReloc = NReloc;
    if (NReloc > 0) {
      CurOff = (CurOff + 3) & ~(uint64_t)3;
      Layouts[i].RelocOff = CurOff;
      CurOff += NReloc * sizeof(MO::relocation_info);
    } else {
      Layouts[i].RelocOff = 0;
    }
  }

  // Now that section addresses are known, fix up symbol n_value fields
  // from section-relative to segment-relative.
  for (auto &S : FinalSyms) {
    if (S.n_sect != 0 && S.n_sect <= NSects)
      S.n_value += Layouts[S.n_sect - 1].Offset - DataStart;
  }

  // Fix non-external relocation data: the raw value at the relocation
  // site is a segment-relative address in the original partition .o.
  // Adjust it to the merged segment layout:
  //   new_value = old_value - orig_target_section_addr
  //             + merged_target_section_addr + partition_offset
  for (auto &F : DeferredFixups) {
    auto &MS = MergedSections[F.MergedSecIdx];
    auto &RE = MS.Relocs[F.RelocIdx];
    // Bound the in-place fixup site in 64-bit.  r_address is a 32-bit field, so
    // a value within `len` of UINT32_MAX would overflow a 32-bit `addr + len`,
    // wrap to a small number that passes the guard, and then index MS.Data ~4
    // GiB out of bounds in the memcpy/byte writes below.  Reinterpreting the
    // 32-bit field as unsigned and computing the bound in uint64_t makes a
    // malformed/hostile r_address (including one with the high bit set, which a
    // valid relocatable section offset never has) skip the fixup instead of
    // corrupting memory.
    uint64_t addr = (uint32_t)RE.Info.r_address;
    uint64_t len = 1ull << RE.Info.r_length;
    if (addr + len > MS.Data.size())
      continue;

    uint64_t origAddr = 0;
    if (F.PartIdx < Maps.size()) {
      auto AI = Maps[F.PartIdx].OrigSecAddr.find(F.OrigSec);
      if (AI != Maps[F.PartIdx].OrigSecAddr.end())
        origAddr = AI->second;
    }

    uint64_t mergedAddr = Layouts[F.TargetMIdx].Offset - DataStart;
    uint64_t partOff = 0;
    if (F.PartIdx < Maps.size()) {
      auto OffIt = Maps[F.PartIdx].SecOff.find(F.OrigSec);
      if (OffIt != Maps[F.PartIdx].SecOff.end())
        partOff = OffIt->second;
    }

    int64_t delta = (int64_t)(mergedAddr + partOff) - (int64_t)origAddr;
    if (delta == 0)
      continue;

    if (len == 8) {
      uint64_t val;
      memcpy(&val, MS.Data.data() + addr, 8);
      val += delta;
      memcpy(MS.Data.data() + addr, &val, 8);
    } else if (len == 4) {
      uint32_t val;
      memcpy(&val, MS.Data.data() + addr, 4);
      val += (int32_t)delta;
      memcpy(MS.Data.data() + addr, &val, 4);
    } else if (len == 2) {
      uint16_t val;
      memcpy(&val, MS.Data.data() + addr, 2);
      val = (uint16_t)(val + (int16_t)delta);
      memcpy(MS.Data.data() + addr, &val, 2);
    } else if (len == 1) {
      uint8_t val = (uint8_t)MS.Data[addr];
      val = (uint8_t)(val + (int8_t)delta);
      MS.Data[addr] = (char)val;
    }
  }

  CurOff = (CurOff + 7) & ~(uint64_t)7;
  uint32_t SymTabOff = CurOff;
  CurOff += FinalSyms.size() * sizeof(MO::nlist_64);
  uint32_t StrTabOff = CurOff;
  uint32_t StrTabSz = StrTab.Data.size();
  CurOff += StrTabSz;

  SmallVector<char, 0> Out(CurOff, 0);

  auto *MH = reinterpret_cast<MO::mach_header_64 *>(Out.data());
  MH->magic = MO::MH_MAGIC_64;
  MH->cputype = CPUType;
  MH->cpusubtype = CPUSubType;
  MH->filetype = MO::MH_OBJECT;
  MH->ncmds = 3;
  MH->sizeofcmds = LoadCmdsSize;
  MH->flags = MO::MH_SUBSECTIONS_VIA_SYMBOLS;
  MH->reserved = 0;

  char *Cmd = Out.data() + HeaderSize;
  auto *Seg = reinterpret_cast<MO::segment_command_64 *>(Cmd);
  memset(Seg, 0, sizeof(MO::segment_command_64));
  Seg->cmd = MO::LC_SEGMENT_64;
  Seg->cmdsize = SegCmdSize;
  // vmsize spans every section's memory image, including the zerofill sections
  // laid out past the on-disk data (VmCur); the relocation/symbol/string tables
  // that follow in the file are not part of the segment's memory, so they are
  // excluded.  filesize below counts only the on-disk (non-zerofill) sections.
  Seg->vmsize = VmCur - DataStart;
  Seg->fileoff = DataStart;
  // Filesize must not count zerofill sections (they live only in memory).
  uint64_t TotalFileSize = 0;
  for (unsigned i = 0; i < NSects; ++i)
    if (!Layouts[i].IsZerofill)
      TotalFileSize = std::max<uint64_t>(
          TotalFileSize, Layouts[i].Offset + Layouts[i].Size - DataStart);
  Seg->filesize = TotalFileSize;
  Seg->maxprot = MO::VM_PROT_READ | MO::VM_PROT_WRITE | MO::VM_PROT_EXECUTE;
  Seg->initprot = MO::VM_PROT_READ | MO::VM_PROT_WRITE | MO::VM_PROT_EXECUTE;
  Seg->nsects = NSects;

  auto *SecHdrs =
      reinterpret_cast<MO::section_64 *>(Cmd + sizeof(MO::segment_command_64));
  for (unsigned i = 0; i < NSects; ++i) {
    memset(&SecHdrs[i], 0, sizeof(MO::section_64));
    memcpy(SecHdrs[i].sectname, MergedSections[i].Key.SectName, 16);
    memcpy(SecHdrs[i].segname, MergedSections[i].Key.SegName, 16);
    SecHdrs[i].addr = Layouts[i].Offset - DataStart;
    SecHdrs[i].size = Layouts[i].Size;
    // Zerofill sections have no on-disk file offset.
    SecHdrs[i].offset = Layouts[i].IsZerofill ? 0 : (uint32_t)Layouts[i].Offset;
    SecHdrs[i].align = MergedSections[i].Align;
    SecHdrs[i].reloff = Layouts[i].RelocOff;
    SecHdrs[i].nreloc = Layouts[i].NReloc;
    SecHdrs[i].flags = MergedSections[i].Flags;
    SecHdrs[i].reserved1 = MergedSections[i].Reserved1;
    SecHdrs[i].reserved2 = MergedSections[i].Reserved2;
  }

  Cmd += SegCmdSize;
  auto *SymCmd = reinterpret_cast<MO::symtab_command *>(Cmd);
  SymCmd->cmd = MO::LC_SYMTAB;
  SymCmd->cmdsize = SymTabCmdSize;
  SymCmd->symoff = SymTabOff;
  SymCmd->nsyms = FinalSyms.size();
  SymCmd->stroff = StrTabOff;
  SymCmd->strsize = StrTabSz;

  Cmd += SymTabCmdSize;
  auto *DyCmd = reinterpret_cast<MO::dysymtab_command *>(Cmd);
  memset(DyCmd, 0, sizeof(MO::dysymtab_command));
  DyCmd->cmd = MO::LC_DYSYMTAB;
  DyCmd->cmdsize = DySymTabCmdSize;
  DyCmd->ilocalsym = 0;
  DyCmd->nlocalsym = NLocalSym;
  DyCmd->iextdefsym = NLocalSym;
  DyCmd->nextdefsym = NExtDefSym;
  DyCmd->iundefsym = NLocalSym + NExtDefSym;
  DyCmd->nundefsym = NUndefSym;

  for (unsigned i = 0; i < NSects; ++i) {
    // Zerofill sections have no on-disk data; do not copy anything.
    if (!Layouts[i].IsZerofill && !MergedSections[i].Data.empty())
      memcpy(Out.data() + Layouts[i].Offset, MergedSections[i].Data.data(),
             MergedSections[i].Data.size());
    if (Layouts[i].NReloc > 0) {
      char *Dst = Out.data() + Layouts[i].RelocOff;
      for (unsigned r = 0; r < Layouts[i].NReloc; ++r)
        memcpy(Dst + r * sizeof(MO::relocation_info),
               &MergedSections[i].Relocs[r].Info, sizeof(MO::relocation_info));
    }
  }

  memcpy(Out.data() + SymTabOff, FinalSyms.data(),
         FinalSyms.size() * sizeof(MO::nlist_64));
  memcpy(Out.data() + StrTabOff, StrTab.Data.data(), StrTabSz);

  // Self-verify before committing (see ELF/MergerELF.cpp): the Mach-O check skips
  // relocation sites, which the in-place fixups above legitimately rewrite.
  if (Opts.verify) {
    SmallVector<StringRef, 8> Views;
    Views.reserve(Buffers.size());
    for (const auto &B : Buffers)
      Views.push_back(StringRef(B.data(), B.size()));
    std::string VErr;
    if (!verifyMerge(ArrayRef<StringRef>(Views), ArrayRef<char>(Out),
                     Format::MachO64, Opts, &VErr)) {
      errs() << "neverc: Mach-O merge self-check failed: " << VErr << "\n";
      return false;
    }
  }

  OS.write(Out.data(), Out.size());
  return true;
}

} // anonymous namespace

bool mergeMachO64Objects(ArrayRef<SmallVector<char, 0>> Buffers,
                         raw_pwrite_stream &OS, const Options &Opts) {
  return detail::runMergeSafely(
      [&]() { return mergeMachO64Impl(Buffers, OS, Opts); });
}

bool mergeMachO64Objects(ArrayRef<StringRef> Buffers, raw_pwrite_stream &OS,
                         const Options &Opts) {
  return detail::runMergeSafely(
      [&]() { return mergeMachO64Impl(Buffers, OS, Opts); });
}

} // namespace neverc::merge
