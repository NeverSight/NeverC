//===- MergerVerify.cpp - Independent post-merge correctness check ----===//
//
// A merged relocatable object can be a perfectly valid ELF that still
// *means* the wrong thing: the historical bug left every merged-section
// symbol value (and relocation offset) at 0, producing an object that
// loaded fine and then jumped to the wrong address.  Fuzzers and "is it
// parseable" checks sail straight past that class of bug.
//
// This file closes the gap with a verifier that is deliberately
// *independent* of the merger's own offset arithmetic:
//
//   * It re-parses both the inputs and the produced output with a private
//     raw reader (no LLVM ObjectFile, no shared SecOff/SymMap state), so a
//     bug in the merge math cannot be masked by reusing the same math to
//     check it.
//   * It content-anchors every uniquely-named defined symbol: in -r mode
//     section bytes are copied verbatim, so the bytes at a symbol's *merged*
//     value must byte-match the bytes at its *input* value.  A collapsed or
//     mis-shifted symbol points at the wrong code and the windows diverge.
//
// The check is conservative: anything ambiguous (a symbol name that is not
// unique in the output, a symbol in a dropped/deduped/zero-content section)
// is skipped rather than guessed.  So a `false` is always a real corruption,
// never a heuristic false alarm — which is what lets the merger treat a
// failed verify as "refuse to emit, fall back / error loudly".
//
//===------------------------------------------------------------------===//

#include "Common/DwarfRebase.h"
#include "Common/MergerCommon.h"
#include "neverc/Foundation/AndroidKernelModuleReleaseNames.h"
#include "neverc/Foundation/AndroidKernelModuleRelocationPolicy.h"
#include "neverc/Foundation/AndroidKernelModuleSectionPolicy.h"
#include "neverc/Foundation/AndroidKernelModuleSymbolPolicy.h"
#include "neverc/Foundation/ELFDebugSectionPolicy.h"
#include "neverc/Merge/Merger.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/DWARF/DWARFDie.h"
#include "llvm/DebugInfo/DWARF/DWARFFormValue.h"
#include "llvm/DebugInfo/DWARF/DWARFTypeUnit.h"
#include "llvm/DebugInfo/DWARF/DWARFUnit.h"
#include "llvm/DebugInfo/DWARF/DWARFUnitIndex.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Compression.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace llvm;

namespace neverc::merge {

namespace {

/// Mach-O is the one format whose DWARF a merge does not copy verbatim.  It
/// writes cross-section DWARF references as plain offsets instead of
/// relocations, so the merger re-points them (see Common/DwarfRebase.h) and
/// these sections' bytes are deliberately not any single input's.  The
/// byte-window comparison below therefore cannot apply to them.
///
/// Ordinary ELF and COFF objects are not exempt: both express the same
/// references as relocations, which the merger re-points instead, leaving the
/// bytes untouched and fully checkable.  Standalone Split-DWARF packages are
/// the exception — see isSplitDwarfRewrittenSection.
bool isRebasedMachOSection(StringRef Name) {
  return dwarfSectionContentsAreRebased(classifyDwarfSection(Name));
}

/// `.debug_str_offsets(.dwo)` is the only Split-DWARF contribution whose
/// payload is rewritten during package finalization: entries become absolute
/// within the merged string section. Contribution-relative bases elsewhere
/// stay intact and are resolved through `.debug_{cu,tu}_index`, so they remain
/// content-anchorable.
bool isSplitDwarfRewrittenSection(StringRef Name, const Options &Opts) {
  return Opts.artifact == ArtifactKind::SplitDwarf &&
         classifyDwarfSection(Name) == DwarfSection::StrOffsets;
}

// ---------------------------------------------------------------------------
// Private raw ELF64LE reader — intentionally separate from both the merger's
// ELFObjectFile-based parse and the test harness, so the verifier shares no
// code path with what it verifies.
// ---------------------------------------------------------------------------

struct RawSec {
  StringRef Name;
  uint32_t NameOffset = 0;
  uint32_t Type = 0;
  uint64_t Flags = 0;
  // Symbol values and relocation offsets in an SHF_COMPRESSED section are
  // expressed against its logical, uncompressed contents. Keep that size
  // separate from the encoded byte count stored in sh_size.
  uint64_t Size = 0;
  uint64_t FileSize = 0;
  uint64_t Offset = 0;
  uint64_t Address = 0;
  uint64_t Alignment = 0;
  uint64_t Entsize = 0;
  uint32_t Link = 0;
  uint32_t Info = 0;
  uint32_t CompressionType = 0;
  uint64_t CompressionAlignment = 0;
};

struct RawSym {
  StringRef Name;
  uint32_t NameOffset = 0;
  uint64_t Value = 0;
  uint64_t Size = 0;
  uint16_t Shndx = 0;
  uint8_t Info = 0;
  uint8_t Other = 0;
  uint8_t bind() const { return Info >> 4; }
  uint8_t type() const { return Info & 0xf; }
};

bool isCoalescibleDefinition(const RawSym &S) {
  return S.bind() == ELF::STB_WEAK || S.bind() == ELF::STB_GNU_UNIQUE;
}

struct RawRela {
  unsigned TargetSec = 0; // section the relocation applies to (sh_info)
  uint64_t Offset = 0;
  uint32_t Sym = 0;
  uint32_t Type = 0;
  int64_t Addend = 0;
};

struct RawELF {
  ArrayRef<char> Buf;
  uint16_t Type = 0;    // e_type
  uint16_t Machine = 0; // e_machine, for the independent arch-consistency check
  uint32_t Flags = 0;
  uint8_t OSABI = 0;
  uint8_t ABIVersion = 0;
  unsigned SymbolStringTableIndex = std::numeric_limits<unsigned>::max();
  unsigned SectionStringTableIndex = std::numeric_limits<unsigned>::max();
  SmallVector<RawSec, 0> Secs;
  SmallVector<RawSym, 0> Syms;
  SmallVector<RawRela, 0> Relas;
  SmallVector<SmallVector<uint8_t, 0>, 0> DecompressedSecs;
  // sh_info of the first SYMTAB (= index of the first non-local symbol).  An
  // independent record so the verifier can audit the locals-before-globals
  // invariant without trusting the merger's own bookkeeping.
  uint32_t SymtabInfo = 0;
  bool HasSymtab = false;

  ArrayRef<uint8_t> secData(unsigned I) const {
    if (I >= Secs.size())
      return {};
    const RawSec &S = Secs[I];
    if (S.Type == ELF::SHT_NOBITS)
      return {};
    if (S.Flags & ELF::SHF_COMPRESSED)
      return DecompressedSecs[I];
    if (S.Offset > Buf.size() || S.FileSize > Buf.size() - S.Offset)
      return {};
    return ArrayRef<uint8_t>(
        reinterpret_cast<const uint8_t *>(Buf.data()) + S.Offset, S.FileSize);
  }
};

struct MaterializationLedger {
  uint64_t Limit = 0;
  uint64_t Used = 0;

  bool charge(uint64_t Bytes) {
    if (Used > Limit || Bytes > Limit - Used)
      return false;
    Used += Bytes;
    return true;
  }
};

StringRef cstrAt(ArrayRef<char> Buf, uint64_t Base, uint64_t Size,
                 uint32_t Off) {
  if (Base > Buf.size() || Size > Buf.size() - Base)
    return {};
  if (Off >= Size)
    return {};
  const char *P = Buf.data() + Base + Off;
  uint64_t Max = Size - Off;
  uint64_t Len = strnlen(P, Max);
  return StringRef(P, Len);
}

std::optional<StringRef> checkedCStrAt(ArrayRef<char> Buf, uint64_t Base,
                                       uint64_t Size, uint32_t Off) {
  if (Base > Buf.size() || Size > Buf.size() - Base || Off >= Size)
    return std::nullopt;
  const char *Begin = Buf.data() + Base + Off;
  const uint64_t Available = Size - Off;
  const uint64_t Length = strnlen(Begin, Available);
  if (Length == Available)
    return std::nullopt;
  return StringRef(Begin, Length);
}

// Read a trivially-copyable POD of type T from Buf at byte offset Off into a
// properly aligned local.  Callers must bounds-check [Off, Off+sizeof(T))
// first.
//
// The verifier also audits hostile / externally-produced objects whose headers
// can sit at file offsets that are not aligned to the struct's required
// alignment (e.g. an nlist_64 at an odd LC_SYMTAB symoff).  Reading those with
// `reinterpret_cast<const T*>(Buf.data() + Off)` followed by member access is a
// misaligned load — undefined behavior the merge fuzzer trips under UBSan, and
// a real fault on stricter ISAs.  Going through memcpy is the same byte-wise
// discipline parseRawCOFF and the relocation readers already use; for the
// merger's own (always aligned) output it is identical to a direct read.
template <typename T> T readPOD(ArrayRef<char> Buf, uint64_t Off) {
  T V;
  memcpy(&V, Buf.data() + Off, sizeof(T));
  return V;
}

bool parseRawELF(ArrayRef<char> Buf, RawELF &Out, uint64_t MaxDecompressedBytes,
                 bool StrictRelease = false,
                 MaterializationLedger *GlobalLedger = nullptr) {
  using namespace ELF;
  using Ehdr = Elf64_Ehdr;
  using Shdr = Elf64_Shdr;
  using Sym = Elf64_Sym;

  Out.Buf = Buf;
  if (Buf.size() < sizeof(Ehdr))
    return false;
  Ehdr H = readPOD<Ehdr>(Buf, 0);
  if (memcmp(H.e_ident, ElfMagic, 4) != 0)
    return false;
  if (H.e_ident[EI_CLASS] != ELFCLASS64 || H.e_ident[EI_DATA] != ELFDATA2LSB)
    return false;
  if (StrictRelease &&
      (H.e_ident[EI_VERSION] != EV_CURRENT || H.e_ehsize != sizeof(Ehdr) ||
       H.e_type != ET_REL || H.e_version != EV_CURRENT ||
       H.e_shentsize != sizeof(Shdr) || H.e_shnum == 0 ||
       H.e_shnum >= SHN_LORESERVE || H.e_shstrndx >= SHN_LORESERVE))
    return false;
  Out.Type = H.e_type;
  Out.Machine = H.e_machine;
  Out.Flags = H.e_flags;
  Out.OSABI = H.e_ident[EI_OSABI];
  Out.ABIVersion = H.e_ident[EI_ABIVERSION];

  uint64_t ShOff = H.e_shoff;
  unsigned ShNum = H.e_shnum;
  if (ShOff == 0 || ShNum == 0)
    return false;
  if (ShOff > Buf.size() || (uint64_t)ShNum * sizeof(Shdr) > Buf.size() - ShOff)
    return false;

  // Copy every section header out through memcpy: they sit at an
  // attacker-controlled e_shoff that need not be 8-aligned, so a typed-pointer
  // dereference would be a misaligned load.
  SmallVector<Shdr, 0> SH;
  SH.reserve(ShNum);
  for (unsigned I = 0; I < ShNum; ++I)
    SH.push_back(readPOD<Shdr>(Buf, ShOff + (uint64_t)I * sizeof(Shdr)));

  if (H.e_shstrndx >= ShNum)
    return false;
  Out.SectionStringTableIndex = H.e_shstrndx;
  if (StrictRelease && SH[H.e_shstrndx].sh_type != SHT_STRTAB)
    return false;
  uint64_t ShStrBase = SH[H.e_shstrndx].sh_offset;
  uint64_t ShStrSize = SH[H.e_shstrndx].sh_size;
  if (StrictRelease &&
      (ShStrBase > Buf.size() || ShStrSize > Buf.size() - ShStrBase))
    return false;

  Out.Secs.reserve(ShNum);
  Out.DecompressedSecs.resize(ShNum);
  uint64_t RemainingDecompressedBytes = MaxDecompressedBytes;
  for (unsigned I = 0; I < ShNum; ++I) {
    if (StrictRelease && SH[I].sh_type != SHT_NOBITS &&
        (SH[I].sh_offset > Buf.size() ||
         SH[I].sh_size > Buf.size() - SH[I].sh_offset))
      return false;
    if (StrictRelease && SH[I].sh_addr != 0)
      return false;
    if (StrictRelease && SH[I].sh_addralign > 1 &&
        (SH[I].sh_addralign & (SH[I].sh_addralign - 1)) != 0)
      return false;
    RawSec RS;
    if (StrictRelease) {
      auto Name = checkedCStrAt(Buf, ShStrBase, ShStrSize, SH[I].sh_name);
      if (!Name)
        return false;
      RS.Name = *Name;
    } else {
      RS.Name = cstrAt(Buf, ShStrBase, ShStrSize, SH[I].sh_name);
    }
    RS.NameOffset = SH[I].sh_name;
    RS.Type = SH[I].sh_type;
    RS.Flags = SH[I].sh_flags;
    RS.Size = SH[I].sh_size;
    RS.FileSize = SH[I].sh_size;
    RS.Offset = SH[I].sh_offset;
    RS.Address = SH[I].sh_addr;
    RS.Alignment = SH[I].sh_addralign;
    RS.Entsize = SH[I].sh_entsize;
    RS.Link = SH[I].sh_link;
    RS.Info = SH[I].sh_info;

    if (RS.Flags & SHF_COMPRESSED) {
      using Chdr = Elf64_Chdr;
      if (RS.Type == SHT_NOBITS || RS.FileSize < sizeof(Chdr) ||
          RS.Offset > Buf.size() || RS.FileSize > Buf.size() - RS.Offset)
        return false;

      Chdr Header = readPOD<Chdr>(Buf, RS.Offset);
      compression::Format CompressionFormat;
      if (Header.ch_type == ELFCOMPRESS_ZLIB)
        CompressionFormat = compression::Format::Zlib;
      else if (Header.ch_type == ELFCOMPRESS_ZSTD)
        CompressionFormat = compression::Format::Zstd;
      else
        return false;
      if (Header.ch_reserved != 0 || Header.ch_addralign == 0 ||
          (Header.ch_addralign & (Header.ch_addralign - 1)) != 0 ||
          Header.ch_size > std::numeric_limits<size_t>::max() ||
          compression::getReasonIfUnsupported(CompressionFormat))
        return false;
      if (GlobalLedger) {
        if (!GlobalLedger->charge(Header.ch_size))
          return false;
      } else {
        if (Header.ch_size > RemainingDecompressedBytes)
          return false;
        RemainingDecompressedBytes -= Header.ch_size;
      }

      ArrayRef<uint8_t> Payload(reinterpret_cast<const uint8_t *>(Buf.data()) +
                                    RS.Offset + sizeof(Chdr),
                                RS.FileSize - sizeof(Chdr));
      SmallVector<uint8_t, 0> &Decoded = Out.DecompressedSecs[I];
      if (Error E = compression::decompress(CompressionFormat, Payload, Decoded,
                                            Header.ch_size)) {
        consumeError(std::move(E));
        return false;
      }
      if (Decoded.size() != Header.ch_size)
        return false;
      RS.Size = Header.ch_size;
      RS.CompressionType = Header.ch_type;
      RS.CompressionAlignment = Header.ch_addralign;
    }
    Out.Secs.push_back(RS);
  }

  // First SYMTAB + its linked STRTAB.
  if (StrictRelease && llvm::count_if(SH, [](const Shdr &Section) {
                         return Section.sh_type == SHT_SYMTAB;
                       }) > 1)
    return false;
  unsigned SymtabCount = 0;
  unsigned SelectedSymtabIndex = std::numeric_limits<unsigned>::max();
  for (unsigned I = 0; I < ShNum; ++I) {
    if (SH[I].sh_type != SHT_SYMTAB)
      continue;
    ++SymtabCount;
    if (StrictRelease &&
        (SymtabCount != 1 || SH[I].sh_entsize != sizeof(Sym) ||
         SH[I].sh_size % sizeof(Sym) != 0 || SH[I].sh_link >= ShNum ||
         SH[SH[I].sh_link].sh_type != SHT_STRTAB))
      return false;
    unsigned StrIdx = SH[I].sh_link;
    if (StrIdx >= ShNum) {
      if (StrictRelease)
        return false;
      break;
    }
    uint64_t StrBase = SH[StrIdx].sh_offset;
    uint64_t StrSize = SH[StrIdx].sh_size;
    uint64_t SymOff = SH[I].sh_offset;
    uint64_t SymSize = SH[I].sh_size;
    if (SymOff > Buf.size() || SymSize > Buf.size() - SymOff) {
      if (StrictRelease)
        return false;
      break;
    }
    unsigned N = SymSize / sizeof(Sym);
    if (StrictRelease && SH[I].sh_info > N)
      return false;
    Out.SymtabInfo = SH[I].sh_info;
    Out.HasSymtab = true;
    SelectedSymtabIndex = I;
    Out.SymbolStringTableIndex = StrIdx;
    Out.Syms.reserve(N);
    for (unsigned k = 0; k < N; ++k) {
      Sym Sy = readPOD<Sym>(Buf, SymOff + (uint64_t)k * sizeof(Sym));
      RawSym PS;
      if (StrictRelease) {
        auto Name = checkedCStrAt(Buf, StrBase, StrSize, Sy.st_name);
        if (!Name)
          return false;
        PS.Name = *Name;
        if (Sy.st_shndx != SHN_UNDEF && Sy.st_shndx < SHN_LORESERVE &&
            Sy.st_shndx >= ShNum)
          return false;
      } else {
        PS.Name = cstrAt(Buf, StrBase, StrSize, Sy.st_name);
      }
      PS.Value = Sy.st_value;
      PS.NameOffset = Sy.st_name;
      PS.Size = Sy.st_size;
      PS.Shndx = Sy.st_shndx;
      PS.Info = Sy.st_info;
      PS.Other = Sy.st_other;
      Out.Syms.push_back(PS);
    }
    break;
  }

  if (StrictRelease) {
    // Determine string-table provenance from raw section indices, independently
    // of the producer's metadata routing. The canonical schema can regenerate
    // only the section-name table and the selected symtab's linked name table;
    // accepting any other SHT_STRTAB would make input-aware replay bless data
    // the producer silently discarded (including definitions in SHF_ALLOC
    // custom tables).
    for (unsigned I = 0; I < ShNum; ++I) {
      if (SH[I].sh_type != SHT_STRTAB)
        continue;
      const bool IsSectionNames = I == H.e_shstrndx;
      const bool IsSymbolNames =
          Out.HasSymtab && I == Out.SymbolStringTableIndex;
      if (!IsSectionNames && !IsSymbolNames)
        return false;
    }
  }

  // RELA relocations (the merger only emits RELA).
  for (unsigned I = 0; I < Out.Secs.size(); ++I) {
    if (Out.Secs[I].Type != SHT_RELA)
      continue;
    uint64_t Off = Out.Secs[I].Offset;
    uint64_t Sz = Out.Secs[I].Size;
    if (StrictRelease &&
        (Out.Secs[I].Entsize != sizeof(Elf64_Rela) ||
         Sz % sizeof(Elf64_Rela) != 0 ||
         Out.Secs[I].Link != SelectedSymtabIndex || Out.Secs[I].Info == 0 ||
         Out.Secs[I].Info >= Out.Secs.size()))
      return false;
    if (Off > Buf.size() || Sz > Buf.size() - Off) {
      if (StrictRelease)
        return false;
      continue;
    }
    unsigned N = Sz / sizeof(Elf64_Rela);
    for (unsigned k = 0; k < N; ++k) {
      Elf64_Rela Re =
          readPOD<Elf64_Rela>(Buf, Off + (uint64_t)k * sizeof(Elf64_Rela));
      RawRela RE;
      RE.TargetSec = Out.Secs[I].Info;
      RE.Offset = Re.r_offset;
      RE.Sym = (uint32_t)(Re.r_info >> 32);
      RE.Type = (uint32_t)(Re.r_info & 0xffffffffu);
      RE.Addend = (int64_t)Re.r_addend;
      if (StrictRelease) {
        const auto Width =
            AndroidKernelModuleRelocationPolicy::writeWidth(RE.Type);
        const uint64_t TargetSize = Out.Secs[RE.TargetSec].Size;
        if (RE.Sym >= Out.Syms.size() || !Width || RE.Offset > TargetSize ||
            *Width > TargetSize - RE.Offset)
          return false;
      }
      Out.Relas.push_back(RE);
    }
  }
  if (StrictRelease) {
    for (const RawSec &Section : Out.Secs) {
      if (Section.Type == SHT_SYMTAB && !Out.HasSymtab)
        return false;
      if (Section.Type == SHT_REL) {
        if (Section.Entsize != sizeof(Elf64_Rel) ||
            Section.Size % sizeof(Elf64_Rel) != 0 ||
            Section.Link != SelectedSymtabIndex || Section.Info == 0 ||
            Section.Info >= Out.Secs.size())
          return false;
        const unsigned Count = Section.Size / sizeof(Elf64_Rel);
        for (unsigned I = 0; I < Count; ++I) {
          const Elf64_Rel Relocation = readPOD<Elf64_Rel>(
              Buf, Section.Offset + uint64_t(I) * sizeof(Elf64_Rel));
          const uint32_t Symbol = uint32_t(Relocation.r_info >> 32);
          const uint32_t Type = uint32_t(Relocation.r_info);
          const auto Width =
              AndroidKernelModuleRelocationPolicy::writeWidth(Type);
          const uint64_t TargetSize = Out.Secs[Section.Info].Size;
          if (Symbol >= Out.Syms.size() || !Width ||
              Relocation.r_offset > TargetSize ||
              *Width > TargetSize - Relocation.r_offset)
            return false;
        }
      }
    }
  }
  return true;
}

// The merger's complete section-fold policy, via the single shared helper in
// Common/MergerCommon.h that ELF/MergerELF.cpp's Phase-1 transform also calls.
// The verifier therefore predicts the output name and header properties with no
// second policy copy to drift out of sync.
detail::ELFSectionFold foldedSection(const RawSec &Sec, const Options &Opts) {
  return detail::foldELFSection(
      {Sec.Name, Sec.Type, Sec.Flags, Sec.Entsize, Sec.Alignment},
      Opts.mergeSections, Opts.androidKernelModule, Opts.preservedSections);
}

StringRef mergedSectionName(const RawSec &Sec, const Options &Opts) {
  return foldedSection(Sec, Opts).Name;
}

AndroidKernelModuleSymbolPolicy::SymbolClass
classifyAndroidKernelReleaseSymbol(const RawSym &Symbol) {
  using Policy = AndroidKernelModuleSymbolPolicy::SymbolClass;
  if (Symbol.Shndx == ELF::SHN_UNDEF)
    return Policy::Undefined;
  if (Symbol.Shndx == ELF::SHN_COMMON)
    return Policy::Common;
  if (Symbol.Shndx == AndroidKernelModuleSymbolPolicy::LivePatchSectionIndex)
    return Policy::LivePatch;
  if (Symbol.Shndx == ELF::SHN_ABS)
    return Policy::Absolute;
  return Policy::Defined;
}

bool hasLivePatchModInfo(const RawELF &Image) {
  for (unsigned I = 0; I < Image.Secs.size(); ++I)
    if (Image.Secs[I].Name == ".modinfo" &&
        AndroidKernelModuleSymbolPolicy::containsLivePatchModInfo(
            Image.secData(I)))
      return true;
  return false;
}

// Sections the merger does not fold into the output as addressable content
// (so a symbol defined there is legitimately re-homed to SHN_UNDEF=0 and
// must be excluded from the content anchor to avoid false positives).
bool isExcludedInputSection(const RawSec &S, const Options &Opts) {
  using namespace ELF;
  switch (S.Type) {
  case SHT_SYMTAB:
  case SHT_STRTAB:
  case SHT_RELA:
  case SHT_REL:
  case SHT_GROUP:
  case SHT_LLVM_ADDRSIG:
  case SHT_LLVM_CALL_GRAPH_PROFILE:
    return true;
  case SHT_NOTE:
    // NOTE sections are content-deduped across partitions; a later
    // partition's symbol points into the first copy, so the offset/content
    // anchor does not hold.  Conservatively skip.
    return true;
  default:
    break;
  }
  if (Opts.dropDebugInfo && ELFDebugSectionPolicy::isDebugSectionName(S.Name))
    return true;
  if (Opts.stripUnneededSymbols && Opts.finalizeAndroidKernelModule &&
      detail::isAndroidKernelReleaseDiscardableSection(S.Name))
    return true;
  // The final Android `.ko` merge drops this NeverC tooling section after the
  // caller has verified input contracts; the independent verifier must treat
  // it as absent from the output for the same reason.
  if (Opts.finalizeAndroidKernelModule &&
      detail::isAndroidKernelProfileContractSection(S.Name))
    return true;
  return false;
}

bool fail(std::string *Err, const Twine &Msg) {
  if (Err)
    *Err = Msg.str();
  return false;
}

bool verifyDroppableELFDebugSections(const RawELF &Input, const Options &Opts,
                                     unsigned Partition, std::string *Err) {
  if (!Opts.dropDebugInfo)
    return true;
  for (const RawSec &Section : Input.Secs)
    if ((Section.Flags & ELF::SHF_ALLOC) &&
        ELFDebugSectionPolicy::isDebugSectionName(Section.Name))
      return fail(Err, "verify: input partition " + Twine(Partition) +
                           " contains allocated debug section '" +
                           Section.Name + "' that dropDebugInfo cannot remove");
  return true;
}

ReleaseSymbolType releaseSymbolType(uint8_t Type) {
  using namespace ELF;
  switch (Type) {
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
}

uint32_t releaseBindingRank(uint8_t Binding) {
  using namespace ELF;
  switch (Binding) {
  case STB_GLOBAL:
    return 0;
  case STB_WEAK:
    return 1;
  case STB_LOCAL:
    return 2;
  default:
    return 3 + Binding;
  }
}

bool auditAndroidKernelReleaseStringTableReachability(const RawELF &Out,
                                                      std::string *Err) {
  if (Out.Syms.empty())
    return fail(Err, "verify: Android release symbol table has no null record");
  const RawSym &Null = Out.Syms.front();
  if (!Null.Name.empty() || Null.NameOffset != 0 || Null.Value != 0 ||
      Null.Size != 0 || Null.Shndx != 0 || Null.Info != 0 || Null.Other != 0)
    return fail(Err, "verify: Android release symbol[0] is not entirely zero");

  auto AuditTable = [&](unsigned TableIndex, auto &&Entries,
                        StringRef TableName, StringRef OffsetField) {
    if (TableIndex >= Out.Secs.size())
      return fail(Err, "verify: Android release " + TableName +
                           " string table is missing");
    const RawSec &Table = Out.Secs[TableIndex];
    const ArrayRef<uint8_t> Bytes = Out.secData(TableIndex);
    if (Table.Type != ELF::SHT_STRTAB || Bytes.size() != Table.Size ||
        Bytes.empty() || Bytes.front() != 0)
      return fail(Err, "verify: Android release " + TableName +
                           " is malformed or does not start with NUL");

    SmallVector<bool, 0> ReferencedBytes(Bytes.size(), false);
    for (const auto &Entry : Entries) {
      const uint32_t Offset = Entry.NameOffset;
      if (Offset >= Bytes.size() || Entry.Name.size() >= Bytes.size() - Offset)
        return fail(Err, "verify: Android release " + TableName + " " +
                             OffsetField + " span is out of range");
      for (uint64_t I = 0; I <= Entry.Name.size(); ++I)
        ReferencedBytes[Offset + I] = true;
    }
    for (unsigned I = 0; I < Bytes.size(); ++I)
      if (Bytes[I] != 0 && !ReferencedBytes[I])
        return fail(Err, "verify: Android release " + TableName +
                             " contains nonzero bytes unreachable from every " +
                             OffsetField);
    return true;
  };

  if (!AuditTable(Out.SymbolStringTableIndex, Out.Syms, ".strtab", "st_name") ||
      !AuditTable(Out.SectionStringTableIndex, Out.Secs, ".shstrtab",
                  "sh_name"))
    return false;
  return true;
}

bool auditSerializedAndroidKernelReleaseNames(const RawELF &Out,
                                              std::string *Err) {
  using SymbolClass = AndroidKernelModuleSymbolPolicy::SymbolClass;

  if (!auditAndroidKernelReleaseStringTableReachability(Out, Err))
    return false;

  SmallVector<ReleaseSectionDescriptor, 32> Sections;
  Sections.reserve(Out.Secs.size());
  for (unsigned I = 1; I < Out.Secs.size(); ++I) {
    const RawSec &Section = Out.Secs[I];
    Sections.push_back({I, I, Section.Alignment, Section.Size,
                        (Section.Flags & ELF::SHF_ALLOC) != 0,
                        (Section.Flags & ELF::SHF_EXECINSTR) != 0});
  }

  SmallVector<ReleaseSymbolDescriptor, 64> Symbols;
  SmallVector<ReleaseSymbolRename, 64> ActualNames;
  Symbols.reserve(Out.Syms.size());
  ActualNames.reserve(Out.Syms.size());
  for (unsigned I = 0; I < Out.Syms.size(); ++I) {
    const RawSym &Symbol = Out.Syms[I];
    const SymbolClass Class = classifyAndroidKernelReleaseSymbol(Symbol);
    bool PreserveName = false;
    if (Class == SymbolClass::Defined && Symbol.Shndx > 0 &&
        Symbol.Shndx < Out.Secs.size())
      PreserveName =
          AndroidKernelModuleSymbolPolicy::preservesSymbolNamesInSection(
              Out.Secs[Symbol.Shndx].Name);
    Symbols.push_back(
        {I, Symbol.Name, Class, releaseSymbolType(Symbol.type()),
         Class == SymbolClass::Defined ? static_cast<uint64_t>(Symbol.Shndx)
                                       : 0,
         Symbol.Value, Symbol.Size, releaseBindingRank(Symbol.bind()),
         static_cast<uint32_t>(Symbol.Other), PreserveName});
    ActualNames.push_back({I, Symbol.Name.str()});
  }

  if (Error Audit =
          auditAndroidKernelReleaseNames(Sections, Symbols, ActualNames))
    return fail(Err, "verify: invalid Android release symbol plan: " +
                         toString(std::move(Audit)));
  return true;
}

struct ReleaseInputNamePlan {
  static constexpr unsigned InvalidIndex = std::numeric_limits<unsigned>::max();
  struct ExchangeClass {
    std::vector<unsigned> FinalIndices;
    std::vector<std::string> Names;
  };
  struct Entry {
    enum class State : uint8_t {
      Invalid,
      Pruned,
      Survivor
    } Status = State::Invalid;
    unsigned ClassID = InvalidIndex;
    unsigned FinalIndex = InvalidIndex;
  };
  std::vector<ExchangeClass> Classes;
  std::vector<std::vector<Entry>> Symbols;
};

bool releaseCanMergeToProgbits(uint32_t Type, uint16_t Machine) {
  using namespace ELF;
  return Type == SHT_NOBITS || Type == SHT_PROGBITS || Type == SHT_INIT_ARRAY ||
         Type == SHT_PREINIT_ARRAY || Type == SHT_FINI_ARRAY ||
         Type == SHT_NOTE ||
         (Type == SHT_X86_64_UNWIND && Machine == EM_X86_64);
}

bool releaseSectionEntsizesCompatible(uint32_t LType, uint64_t LFlags,
                                      uint64_t LEntsize, uint32_t RType,
                                      uint64_t RFlags, uint64_t REntsize) {
  using namespace ELF;
  const auto IsArray = [](uint32_t Type) {
    return Type == SHT_INIT_ARRAY || Type == SHT_PREINIT_ARRAY ||
           Type == SHT_FINI_ARRAY;
  };
  const bool MustMatch = LEntsize != 0 || REntsize != 0 ||
                         ((LFlags | RFlags) & (SHF_MERGE | SHF_STRINGS)) != 0 ||
                         IsArray(LType) || IsArray(RType);
  return !MustMatch || LEntsize == REntsize;
}

bool releaseSectionsCompatible(uint32_t LType, uint64_t LFlags,
                               uint64_t LEntsize, uint32_t RType,
                               uint64_t RFlags, uint64_t REntsize,
                               uint16_t Machine) {
  if (!releaseSectionEntsizesCompatible(LType, LFlags, LEntsize, RType, RFlags,
                                        REntsize))
    return false;
  if (LType == RType)
    return LFlags == RFlags;
  return LFlags == RFlags && releaseCanMergeToProgbits(LType, Machine) &&
         releaseCanMergeToProgbits(RType, Machine);
}

uint64_t releaseNormalizeAlignment(uint64_t Alignment) {
  return std::max<uint64_t>(Alignment, 1);
}

Expected<uint64_t> releaseAlign(uint64_t Offset, uint64_t Alignment) {
  Alignment = std::max<uint64_t>(Alignment, 1);
  const uint64_t Remainder = Offset % Alignment;
  if (Remainder == 0)
    return Offset;
  const uint64_t Padding = Alignment - Remainder;
  if (Padding > std::numeric_limits<uint64_t>::max() - Offset)
    return createStringError(
        inconvertibleErrorCode(),
        "Android release input section alignment overflow");
  return Offset + Padding;
}

Expected<ReleaseInputNamePlan> reconstructAndroidKernelReleaseInputNames(
    ArrayRef<StringRef> InputBuffers, const RawELF &Out, const Options &Opts,
    uint64_t MaxDecompressedBytes, MaterializationLedger *GlobalLedger) {
  using namespace ELF;
  using SymbolClass = AndroidKernelModuleSymbolPolicy::SymbolClass;

  SmallVector<RawELF, 8> Inputs;
  Inputs.reserve(InputBuffers.size());
  for (unsigned P = 0; P < InputBuffers.size(); ++P) {
    RawELF Input;
    if (InputBuffers[P].empty()) {
      Inputs.push_back(std::move(Input));
      continue;
    }
    if (!parseRawELF(
            ArrayRef<char>(InputBuffers[P].data(), InputBuffers[P].size()),
            Input, MaxDecompressedBytes, /*StrictRelease=*/true, GlobalLedger))
      return createStringError(
          inconvertibleErrorCode(),
          "input partition is not a parseable ELF64LE object");
    Inputs.push_back(std::move(Input));
  }

  struct SectionPlacement {
    bool Emitted = false;
    bool Dropped = false;
    unsigned SectionID = 0;
    uint64_t Offset = 0;
  };
  struct ModeledLinkOrderContribution {
    unsigned Partition = 0;
    unsigned InputSection = 0;
    unsigned LinkedInputSection = 0;
    uint32_t InputType = 0;
    uint64_t Alignment = 1;
    uint64_t Size = 0;
  };
  struct ModeledSection {
    std::string Name;
    uint32_t Type = 0;
    uint64_t Flags = 0;
    uint64_t Alignment = 1;
    uint64_t Size = 0;
    uint64_t Entsize = 0;
    uint32_t Link = 0;
    uint32_t Info = 0;
    SmallVector<uint8_t, 0> NoteData;
    SmallVector<uint8_t, 0> DebugData;
    SmallVector<uint8_t, 0> LinkOrderData;
    SmallVector<ModeledLinkOrderContribution, 2> LinkOrderContributions;
  };

  std::vector<std::vector<SectionPlacement>> Placements(Inputs.size());
  SmallVector<ModeledSection, 32> ModeledSections;
  std::map<std::string, SmallVector<unsigned, 2>> SectionCandidates;

  for (unsigned P = 0; P < Inputs.size(); ++P) {
    const RawELF &Input = Inputs[P];
    Placements[P].resize(Input.Secs.size());
    std::set<unsigned> RelocationTargets;
    for (const RawSec &RelocationSection : Input.Secs)
      if ((RelocationSection.Type == SHT_RELA ||
           RelocationSection.Type == SHT_REL) &&
          RelocationSection.Info < Input.Secs.size())
        RelocationTargets.insert(RelocationSection.Info);

    for (unsigned I = 1; I < Input.Secs.size(); ++I) {
      const RawSec &Section = Input.Secs[I];
      if (AndroidKernelModuleSectionPolicy::rejectsReleaseInputType(
              Section.Type))
        return createStringError(inconvertibleErrorCode(),
                                 "Android release input contains an "
                                 "unsupported section type");
      const bool IsSelectedStringTable =
          Section.Type == SHT_STRTAB &&
          (I == Input.SectionStringTableIndex ||
           (Input.HasSymtab && I == Input.SymbolStringTableIndex));
      if (IsSelectedStringTable ||
          AndroidKernelModuleSectionPolicy::regeneratesReleaseInputType(
              Section.Type))
        continue;

      if (Section.Flags & SHF_INFO_LINK)
        return createStringError(
            inconvertibleErrorCode(),
            "Android release input contains ordinary SHF_INFO_LINK");
      if (Section.Flags & SHF_LINK_ORDER) {
        if (Section.Link == 0 || Section.Link >= Input.Secs.size() ||
            Section.Info != 0)
          return createStringError(
              inconvertibleErrorCode(),
              "Android release input has malformed SHF_LINK_ORDER");
      } else if (Section.Link != 0 || Section.Info != 0) {
        return createStringError(
            inconvertibleErrorCode(),
            "Android release ordinary section has nonzero link metadata");
      }

      const detail::ELFSectionFold Fold = foldedSection(Section, Opts);
      if ((Opts.dropDebugInfo &&
           ELFDebugSectionPolicy::isDebugSectionName(Fold.Name)) ||
          (Opts.stripUnneededSymbols && Opts.finalizeAndroidKernelModule &&
           detail::isAndroidKernelReleaseDiscardableSection(Fold.Name)) ||
          (Opts.finalizeAndroidKernelModule &&
           detail::isAndroidKernelProfileContractSection(Fold.Name))) {
        Placements[P][I].Dropped = true;
        continue;
      }
      if (Section.Flags & SHF_COMPRESSED)
        return createStringError(
            inconvertibleErrorCode(),
            "Android release input contains a compressed section");

      const uint32_t FoldedType = Fold.Type;
      const uint64_t FoldedFlags = Fold.Flags & ~(uint64_t)SHF_GROUP;
      auto &Candidates = SectionCandidates[Fold.Name.str()];

      if (FoldedType == SHT_NOTE && P > 0 && !RelocationTargets.count(I) &&
          !(Section.Flags & SHF_LINK_ORDER) && !Candidates.empty()) {
        ModeledSection &Front = ModeledSections[Candidates.front()];
        const ArrayRef<uint8_t> Bytes = Input.secData(I);
        if (Front.Type == SHT_NOTE && Bytes.size() == Front.NoteData.size() &&
            std::equal(Bytes.begin(), Bytes.end(), Front.NoteData.begin())) {
          Placements[P][I] = {true, false, Candidates.front() + 1, 0};
          continue;
        }
      }

      unsigned SectionIndex = ModeledSections.size();
      for (unsigned Candidate : Candidates)
        if (releaseSectionsCompatible(ModeledSections[Candidate].Type,
                                      ModeledSections[Candidate].Flags,
                                      ModeledSections[Candidate].Entsize,
                                      FoldedType, FoldedFlags, Fold.Entsize,
                                      Input.Machine)) {
          SectionIndex = Candidate;
          break;
        }
      if (SectionIndex == ModeledSections.size()) {
        ModeledSection NewSection;
        NewSection.Name = Fold.Name.str();
        NewSection.Type = FoldedType;
        NewSection.Flags = FoldedFlags;
        NewSection.Alignment = releaseNormalizeAlignment(Fold.Alignment);
        NewSection.Entsize = Fold.Entsize;
        ModeledSections.push_back(std::move(NewSection));
        Candidates.push_back(SectionIndex);
      }

      ModeledSection &Merged = ModeledSections[SectionIndex];
      if (Section.Flags & SHF_LINK_ORDER)
        Merged.LinkOrderContributions.push_back(
            {P, I, Section.Link, Section.Type,
             releaseNormalizeAlignment(Fold.Alignment), Section.Size});
      if (Fold.Name == ".rodata")
        Merged.Entsize = Fold.Entsize;
      Merged.Alignment =
          std::max(Merged.Alignment, releaseNormalizeAlignment(Fold.Alignment));
      if (Merged.Type != FoldedType &&
          releaseCanMergeToProgbits(Merged.Type, Input.Machine) &&
          releaseCanMergeToProgbits(FoldedType, Input.Machine))
        Merged.Type = SHT_PROGBITS;

      auto PartOffset = releaseAlign(Merged.Size, Merged.Alignment);
      if (!PartOffset)
        return PartOffset.takeError();
      if (Section.Size > std::numeric_limits<uint64_t>::max() - *PartOffset)
        return createStringError(inconvertibleErrorCode(),
                                 "Android release input section size overflow");
      Merged.Size = *PartOffset + Section.Size;
      if (FoldedType == SHT_NOTE && !(Section.Flags & SHF_LINK_ORDER)) {
        if (Merged.NoteData.size() < *PartOffset) {
          if (GlobalLedger &&
              !GlobalLedger->charge(*PartOffset - Merged.NoteData.size()))
            return createStringError(
                inconvertibleErrorCode(),
                "Android release NOTE reconstruction padding exceeds the "
                "global materialization budget");
          Merged.NoteData.resize(*PartOffset, 0);
        }
        ArrayRef<uint8_t> Bytes = Input.secData(I);
        Merged.NoteData.append(Bytes.begin(), Bytes.end());
      }
      if (Fold.Name.starts_with(".debug_") && !(FoldedFlags & SHF_ALLOC) &&
          FoldedType != SHT_NOBITS && !(Section.Flags & SHF_LINK_ORDER)) {
        if (Merged.DebugData.size() < *PartOffset) {
          if (GlobalLedger &&
              !GlobalLedger->charge(*PartOffset - Merged.DebugData.size()))
            return createStringError(
                inconvertibleErrorCode(),
                "Android release debug reconstruction padding exceeds the "
                "global materialization budget");
          Merged.DebugData.resize(*PartOffset, 0);
        }
        const ArrayRef<uint8_t> Bytes = Input.secData(I);
        if (Bytes.size() != Section.Size)
          return createStringError(
              inconvertibleErrorCode(),
              "Android release debug section payload size mismatch");
        Merged.DebugData.append(Bytes.begin(), Bytes.end());
      }
      Placements[P][I] = {true, false, SectionIndex + 1, *PartOffset};
    }
    for (const RawSec &RelocationSection : Input.Secs)
      if (RelocationSection.Type == SHT_REL &&
          RelocationSection.Info < Placements[P].size() &&
          Placements[P][RelocationSection.Info].Emitted)
        return createStringError(
            inconvertibleErrorCode(),
            "Android release input contains retained SHT_REL relocations");
  }

  SmallVector<uint8_t, 32> LinkOrderState(ModeledSections.size(), 0);
  std::function<Error(unsigned)> SortLinkOrder =
      [&](unsigned SectionIndex) -> Error {
    if (LinkOrderState[SectionIndex] == 2)
      return Error::success();
    if (LinkOrderState[SectionIndex] == 1)
      return createStringError(
          inconvertibleErrorCode(),
          "Android release has cyclic SHF_LINK_ORDER dependencies");
    LinkOrderState[SectionIndex] = 1;
    ModeledSection &Section = ModeledSections[SectionIndex];
    std::optional<unsigned> FinalTarget;
    for (const ModeledLinkOrderContribution &Contribution :
         Section.LinkOrderContributions) {
      if (Contribution.Partition >= Placements.size() ||
          Contribution.LinkedInputSection >=
              Placements[Contribution.Partition].size())
        return createStringError(
            inconvertibleErrorCode(),
            "Android release SHF_LINK_ORDER target is out of range");
      const SectionPlacement &Target =
          Placements[Contribution.Partition][Contribution.LinkedInputSection];
      if (!Target.Emitted || Target.SectionID == 0)
        return createStringError(
            inconvertibleErrorCode(),
            "Android release SHF_LINK_ORDER target was not emitted");
      if (!FinalTarget)
        FinalTarget = Target.SectionID;
      else if (*FinalTarget != Target.SectionID)
        return createStringError(
            inconvertibleErrorCode(),
            "Android release SHF_LINK_ORDER contributors have distinct "
            "final targets");
      const unsigned TargetIndex = Target.SectionID - 1;
      if (!ModeledSections[TargetIndex].LinkOrderContributions.empty())
        if (Error E = SortLinkOrder(TargetIndex))
          return E;
    }

    if (!Section.LinkOrderContributions.empty()) {
      auto PlacementKey = [&](const ModeledLinkOrderContribution
                                  &Contribution) {
        const SectionPlacement &Target =
            Placements[Contribution.Partition][Contribution.LinkedInputSection];
        return std::tuple<unsigned, uint64_t, unsigned, unsigned>(
            Target.SectionID, Target.Offset, Contribution.Partition,
            Contribution.InputSection);
      };
      std::stable_sort(Section.LinkOrderContributions.begin(),
                       Section.LinkOrderContributions.end(),
                       [&](const ModeledLinkOrderContribution &Left,
                           const ModeledLinkOrderContribution &Right) {
                         return PlacementKey(Left) < PlacementKey(Right);
                       });

      Section.Size = 0;
      Section.LinkOrderData.clear();
      for (const ModeledLinkOrderContribution &Contribution :
           Section.LinkOrderContributions) {
        auto Offset = releaseAlign(Section.Size, Contribution.Alignment);
        if (!Offset)
          return Offset.takeError();
        if (Contribution.Size > std::numeric_limits<uint64_t>::max() - *Offset)
          return createStringError(
              inconvertibleErrorCode(),
              "Android release SHF_LINK_ORDER contribution size overflow");
        SectionPlacement &Placement =
            Placements[Contribution.Partition][Contribution.InputSection];
        Placement.Offset = *Offset;
        if (Section.Type != SHT_NOBITS) {
          if (Section.LinkOrderData.size() < *Offset) {
            if (GlobalLedger &&
                !GlobalLedger->charge(*Offset - Section.LinkOrderData.size()))
              return createStringError(
                  inconvertibleErrorCode(),
                  "Android release SHF_LINK_ORDER reconstruction padding "
                  "exceeds the global materialization budget");
            Section.LinkOrderData.resize(*Offset, 0);
          }
          if (Contribution.InputType == SHT_NOBITS) {
            if (GlobalLedger && !GlobalLedger->charge(Contribution.Size))
              return createStringError(
                  inconvertibleErrorCode(),
                  "Android release SHF_LINK_ORDER NOBITS reconstruction "
                  "exceeds the global materialization budget");
            Section.LinkOrderData.resize(*Offset + Contribution.Size, 0);
          } else {
            const ArrayRef<uint8_t> Bytes =
                Inputs[Contribution.Partition].secData(
                    Contribution.InputSection);
            if (Bytes.size() != Contribution.Size)
              return createStringError(
                  inconvertibleErrorCode(),
                  "Android release SHF_LINK_ORDER payload size mismatch");
            Section.LinkOrderData.append(Bytes.begin(), Bytes.end());
          }
        }
        Section.Size = *Offset + Contribution.Size;
      }
      Section.Link = *FinalTarget;
    }
    LinkOrderState[SectionIndex] = 2;
    return Error::success();
  };

  for (unsigned I = 0; I < ModeledSections.size(); ++I)
    if (!ModeledSections[I].LinkOrderContributions.empty())
      if (Error E = SortLinkOrder(I))
        return std::move(E);

  auto EnsureSection = [&](StringRef Name, uint64_t RequiredFlags,
                           uint64_t RequiredAlignment) -> Expected<unsigned> {
    auto It = SectionCandidates.find(Name.str());
    if (It == SectionCandidates.end()) {
      ModeledSection Section;
      Section.Name = Name.str();
      Section.Type = SHT_PROGBITS;
      Section.Flags = RequiredFlags;
      Section.Alignment = RequiredAlignment;
      const unsigned Index = ModeledSections.size();
      ModeledSections.push_back(std::move(Section));
      SectionCandidates[Name.str()].push_back(Index);
      return Index;
    }
    if (It->second.size() != 1)
      return createStringError(inconvertibleErrorCode(),
                               "ambiguous Android release synthetic section");
    ModeledSection &Section = ModeledSections[It->second.front()];
    if (Section.Type != SHT_PROGBITS)
      return createStringError(
          inconvertibleErrorCode(),
          "incompatible Android release synthetic section");
    Section.Flags |= RequiredFlags;
    Section.Alignment = std::max(Section.Alignment, RequiredAlignment);
    return It->second.front();
  };

  auto Versions = EnsureSection("__versions", SHF_ALLOC, 8);
  if (!Versions)
    return Versions.takeError();
  auto AllocTags =
      EnsureSection(".codetag.alloc_tags", SHF_ALLOC | SHF_WRITE, 8);
  if (!AllocTags)
    return AllocTags.takeError();

  if (Out.Secs.size() <= ModeledSections.size())
    return createStringError(inconvertibleErrorCode(),
                             "Android release output has too few sections");
  for (unsigned I = 0; I < ModeledSections.size(); ++I) {
    const ModeledSection &Expected = ModeledSections[I];
    const RawSec &Actual = Out.Secs[I + 1];
    uint64_t ExpectedAlignment = Expected.Alignment;
    uint64_t ExpectedFlags = Expected.Flags;
    uint32_t ExpectedCompressionType = 0;
    uint64_t ExpectedCompressionAlignment = 0;
    const ArrayRef<uint8_t> ExpectedDebugBytes =
        !Expected.LinkOrderContributions.empty()
            ? ArrayRef<uint8_t>(Expected.LinkOrderData)
            : ArrayRef<uint8_t>(Expected.DebugData);
    if (Opts.debugCompression != DebugCompressionType::None &&
        StringRef(Expected.Name).starts_with(".debug_") &&
        !(Expected.Flags & SHF_ALLOC) && Expected.Type != SHT_NOBITS &&
        !ExpectedDebugBytes.empty()) {
      const compression::Format Format =
          compression::formatFor(Opts.debugCompression);
      if (compression::getReasonIfUnsupported(Format))
        return createStringError(inconvertibleErrorCode(),
                                 "Android release debug codec unavailable");
      SmallVector<uint8_t, 0> Compressed;
      compression::compress(Format, ExpectedDebugBytes, Compressed);
      if (ExpectedDebugBytes.size() > sizeof(Elf64_Chdr) + Compressed.size()) {
        ExpectedFlags |= SHF_COMPRESSED;
        ExpectedAlignment = alignof(Elf64_Chdr);
        ExpectedCompressionType =
            Opts.debugCompression == DebugCompressionType::Zlib
                ? ELFCOMPRESS_ZLIB
                : ELFCOMPRESS_ZSTD;
        ExpectedCompressionAlignment = Expected.Alignment;
      }
    }
    if (Actual.Name != Expected.Name || Actual.Type != Expected.Type ||
        Actual.Size != Expected.Size || Actual.Flags != ExpectedFlags ||
        Actual.Alignment != ExpectedAlignment ||
        Actual.Entsize != Expected.Entsize || Actual.Link != Expected.Link ||
        Actual.Info != Expected.Info ||
        Actual.CompressionType != ExpectedCompressionType ||
        Actual.CompressionAlignment != ExpectedCompressionAlignment)
      return createStringError(
          inconvertibleErrorCode(),
          "Android release output section layout or .shstrtab-derived name "
          "differs from inputs");
    if (!ExpectedDebugBytes.empty()) {
      const ArrayRef<uint8_t> ActualData = Out.secData(I + 1);
      if (ActualData.size() != ExpectedDebugBytes.size() ||
          !std::equal(ActualData.begin(), ActualData.end(),
                      ExpectedDebugBytes.begin()))
        return createStringError(
            inconvertibleErrorCode(),
            "Android release output debug bytes differ from inputs");
    }
    if (!Expected.LinkOrderContributions.empty() &&
        Expected.Type != SHT_NOBITS) {
      const ArrayRef<uint8_t> ActualData = Out.secData(I + 1);
      if (ActualData.size() != Expected.LinkOrderData.size() ||
          !std::equal(ActualData.begin(), ActualData.end(),
                      Expected.LinkOrderData.begin()))
        return createStringError(
            inconvertibleErrorCode(),
            "Android release SHF_LINK_ORDER bytes are not in linked-target "
            "placement order");
    }
  }
  for (unsigned I = ModeledSections.size() + 1; I < Out.Secs.size(); ++I)
    if (Out.Secs[I].Type != SHT_SYMTAB && Out.Secs[I].Type != SHT_STRTAB &&
        Out.Secs[I].Type != SHT_RELA)
      return createStringError(
          inconvertibleErrorCode(),
          "Android release output contains an unmodeled content section");

  struct ModeledSymbol {
    RawSym Symbol;
  };
  struct SymbolReference {
    enum class Kind : uint8_t { None, Local, Global } K = Kind::None;
    unsigned Index = 0;
  };
  struct GlobalOwner {
    unsigned Slot = 0;
    unsigned Priority = 0;
    bool Strong = false;
  };

  std::vector<std::vector<SymbolReference>> InputSymbolRefs(Inputs.size());
  SmallVector<ModeledSymbol, 64> LocalSymbols;
  SmallVector<ModeledSymbol, 64> GlobalSymbols;
  std::map<std::string, GlobalOwner> GlobalOwners;
  LocalSymbols.push_back({RawSym{}});

  auto Priority = [](const RawSym &Symbol) {
    if (Symbol.Shndx == SHN_UNDEF)
      return Symbol.bind() == STB_WEAK ? 0u : 1u;
    if (Symbol.Shndx == SHN_COMMON)
      return 2u;
    if (Symbol.bind() == STB_WEAK)
      return 3u;
    return 4u;
  };

  for (unsigned P = 0; P < Inputs.size(); ++P) {
    const RawELF &Input = Inputs[P];
    InputSymbolRefs[P].resize(Input.Syms.size());
    if (!Input.Syms.empty())
      InputSymbolRefs[P][0] = {SymbolReference::Kind::Local, 0};
    for (unsigned I = 1; I < Input.Syms.size(); ++I) {
      const RawSym &InputSymbol = Input.Syms[I];
      RawSym Symbol = InputSymbol;
      if (Symbol.Shndx < SHN_LORESERVE) {
        if (Symbol.Shndx < Placements[P].size() &&
            Placements[P][Symbol.Shndx].Dropped)
          continue;
        if (Symbol.Shndx < Placements[P].size() &&
            Placements[P][Symbol.Shndx].Emitted) {
          const SectionPlacement &Placement = Placements[P][Symbol.Shndx];
          Symbol.Shndx = Placement.SectionID;
          if (Symbol.Value >
              std::numeric_limits<uint64_t>::max() - Placement.Offset)
            return createStringError(
                inconvertibleErrorCode(),
                "Android release input symbol value overflow");
          Symbol.Value += Placement.Offset;
        } else {
          Symbol.Shndx = SHN_UNDEF;
        }
      }
      if (Opts.finalizeAndroidKernelModule &&
          detail::isAndroidKernelProfileContractSymbol(Symbol.Name))
        continue;

      if (InputSymbol.bind() == STB_LOCAL) {
        const unsigned Index = LocalSymbols.size();
        LocalSymbols.push_back({Symbol});
        InputSymbolRefs[P][I] = {SymbolReference::Kind::Local, Index};
        continue;
      }

      const unsigned SymbolPriority = Priority(InputSymbol);
      const bool Strong =
          SymbolPriority == 4 && InputSymbol.bind() == STB_GLOBAL;
      auto [OwnerIt, Inserted] = GlobalOwners.emplace(
          Symbol.Name.str(),
          GlobalOwner{static_cast<unsigned>(GlobalSymbols.size()),
                      SymbolPriority, Strong});
      if (Inserted) {
        GlobalSymbols.push_back({Symbol});
      } else {
        if (Strong && OwnerIt->second.Strong)
          return createStringError(
              inconvertibleErrorCode(),
              "multiple strong Android release definitions");
        if (SymbolPriority > OwnerIt->second.Priority) {
          GlobalSymbols[OwnerIt->second.Slot] = {Symbol};
          OwnerIt->second.Priority = SymbolPriority;
          OwnerIt->second.Strong = Strong;
        }
      }
      InputSymbolRefs[P][I] = {SymbolReference::Kind::Global,
                               OwnerIt->second.Slot};
    }
  }

  static constexpr StringLiteral StartAllocTags = "__start_alloc_tags";
  static constexpr StringLiteral StopAllocTags = "__stop_alloc_tags";
  auto DefineBoundary = [&](StringRef Name, uint64_t Value) -> Error {
    for (const ModeledSymbol &Local : LocalSymbols)
      if (Local.Symbol.Name == Name)
        return createStringError(inconvertibleErrorCode(),
                                 "local Android release boundary collision");
    auto Existing = GlobalOwners.find(Name.str());
    if (Existing != GlobalOwners.end()) {
      RawSym &Symbol = GlobalSymbols[Existing->second.Slot].Symbol;
      if (Symbol.Shndx != SHN_UNDEF &&
          (Symbol.Shndx != *AllocTags + 1 || Symbol.Value != Value))
        return createStringError(inconvertibleErrorCode(),
                                 "conflicting Android release boundary");
      Symbol.Shndx = *AllocTags + 1;
      Symbol.Value = Value;
      Symbol.Size = 0;
      Symbol.Info = (STB_GLOBAL << 4) | STT_NOTYPE;
      Existing->second.Priority = 4;
      Existing->second.Strong = true;
      return Error::success();
    }
    RawSym Symbol;
    Symbol.Name = Name;
    Symbol.Shndx = *AllocTags + 1;
    Symbol.Value = Value;
    Symbol.Info = (STB_GLOBAL << 4) | STT_NOTYPE;
    const unsigned Slot = GlobalSymbols.size();
    GlobalSymbols.push_back({Symbol});
    GlobalOwners.emplace(Name.str(), GlobalOwner{Slot, 4, true});
    return Error::success();
  };
  if (Error Boundary = DefineBoundary(StartAllocTags, 0))
    return std::move(Boundary);
  if (Error Boundary =
          DefineBoundary(StopAllocTags, ModeledSections[*AllocTags].Size))
    return std::move(Boundary);

  const unsigned FirstGlobal = LocalSymbols.size();
  const unsigned OldSymbolCount = FirstGlobal + GlobalSymbols.size();
  DenseSet<unsigned> ReferencedOldSymbols;
  for (unsigned P = 0; P < Inputs.size(); ++P) {
    for (const RawRela &Relocation : Inputs[P].Relas) {
      if (Relocation.TargetSec >= Placements[P].size() ||
          !Placements[P][Relocation.TargetSec].Emitted)
        continue;
      unsigned OldIndex = 0;
      if (Relocation.Sym < InputSymbolRefs[P].size()) {
        const SymbolReference &Reference = InputSymbolRefs[P][Relocation.Sym];
        if (Reference.K == SymbolReference::Kind::Local)
          OldIndex = Reference.Index;
        else if (Reference.K == SymbolReference::Kind::Global)
          OldIndex = FirstGlobal + Reference.Index;
        else {
          const RawSym &Target = Inputs[P].Syms[Relocation.Sym];
          const bool TargetsDroppedSection =
              Target.Shndx < Placements[P].size() &&
              Placements[P][Target.Shndx].Dropped;
          if (TargetsDroppedSection ||
              (Opts.finalizeAndroidKernelModule &&
               detail::isAndroidKernelProfileContractSymbol(Target.Name)))
            return createStringError(
                inconvertibleErrorCode(),
                "Android release retained relocation targets a dropped symbol");
        }
      }
      ReferencedOldSymbols.insert(OldIndex);
    }
  }

  SmallVector<ModeledSymbol, 64> ReorderedLocals = LocalSymbols;
  SmallVector<ModeledSymbol, 64> ReorderedGlobals;
  SmallVector<unsigned, 64> OldToReordered(
      OldSymbolCount, std::numeric_limits<unsigned>::max());
  for (unsigned I = 0; I < LocalSymbols.size(); ++I)
    OldToReordered[I] = I;
  for (unsigned I = 0; I < GlobalSymbols.size(); ++I) {
    RawSym &Symbol = GlobalSymbols[I].Symbol;
    if (Symbol.Shndx != SHN_UNDEF && Symbol.Shndx != SHN_COMMON &&
        Symbol.Name.contains(PcgSymbolMarker)) {
      Symbol.Info = (STB_LOCAL << 4) | Symbol.type();
      OldToReordered[FirstGlobal + I] = ReorderedLocals.size();
      ReorderedLocals.push_back({Symbol});
    }
  }
  const unsigned ReorderedFirstGlobal = ReorderedLocals.size();
  for (unsigned I = 0; I < GlobalSymbols.size(); ++I) {
    const RawSym &Symbol = GlobalSymbols[I].Symbol;
    if (Symbol.bind() == STB_LOCAL)
      continue;
    OldToReordered[FirstGlobal + I] =
        ReorderedFirstGlobal + ReorderedGlobals.size();
    ReorderedGlobals.push_back({Symbol});
  }

  DenseSet<unsigned> ReferencedSymbols;
  for (unsigned OldIndex : ReferencedOldSymbols)
    if (OldIndex < OldToReordered.size() &&
        OldToReordered[OldIndex] != std::numeric_limits<unsigned>::max())
      ReferencedSymbols.insert(OldToReordered[OldIndex]);

  struct RetainedSymbol {
    ModeledSymbol Symbol;
    unsigned ReorderedIndex;
  };
  SmallVector<RetainedSymbol, 64> Retained;
  SmallVector<unsigned, 64> ReorderedToFinal(
      ReorderedFirstGlobal + ReorderedGlobals.size(),
      std::numeric_limits<unsigned>::max());
  auto KeepSymbol = [&](const RawSym &Symbol, unsigned Index) {
    return Index == 0 || ReferencedSymbols.contains(Index) ||
           (Symbol.bind() != STB_LOCAL && Symbol.Shndx != SHN_UNDEF);
  };
  for (unsigned I = 0; I < ReorderedLocals.size(); ++I) {
    if (!KeepSymbol(ReorderedLocals[I].Symbol, I))
      continue;
    ReorderedToFinal[I] = Retained.size();
    Retained.push_back({ReorderedLocals[I], I});
  }
  for (unsigned I = 0; I < ReorderedGlobals.size(); ++I) {
    const unsigned Index = ReorderedFirstGlobal + I;
    if (!KeepSymbol(ReorderedGlobals[I].Symbol, Index))
      continue;
    ReorderedToFinal[Index] = Retained.size();
    Retained.push_back({ReorderedGlobals[I], Index});
  }

  SmallVector<ReleaseSectionDescriptor, 32> ReleaseSections;
  for (unsigned I = 0; I < ModeledSections.size(); ++I) {
    const ModeledSection &Section = ModeledSections[I];
    ReleaseSections.push_back({I + 1, I + 1, Section.Alignment, Section.Size,
                               (Section.Flags & SHF_ALLOC) != 0,
                               (Section.Flags & SHF_EXECINSTR) != 0});
  }
  SmallVector<ReleaseSymbolDescriptor, 64> ReleaseSymbols;
  for (unsigned I = 0; I < Retained.size(); ++I) {
    const RawSym &Symbol = Retained[I].Symbol.Symbol;
    const SymbolClass Class = classifyAndroidKernelReleaseSymbol(Symbol);
    bool PreserveName = false;
    if (Class == SymbolClass::Defined && Symbol.Shndx > 0 &&
        Symbol.Shndx <= ModeledSections.size())
      PreserveName =
          AndroidKernelModuleSymbolPolicy::preservesSymbolNamesInSection(
              ModeledSections[Symbol.Shndx - 1].Name);
    ReleaseSymbols.push_back(
        {I, Symbol.Name, Class, releaseSymbolType(Symbol.type()),
         Class == SymbolClass::Defined ? static_cast<uint64_t>(Symbol.Shndx)
                                       : 0,
         Symbol.Value, Symbol.Size, releaseBindingRank(Symbol.bind()),
         static_cast<uint32_t>(Symbol.Other), PreserveName});
  }
  auto Planned = planAndroidKernelReleaseNames(ReleaseSections, ReleaseSymbols);
  if (!Planned)
    return Planned.takeError();
  SmallVector<std::string, 64> FinalNames(Retained.size());
  for (const ReleaseSymbolRename &Rename : *Planned) {
    if (Rename.SymbolID >= FinalNames.size())
      return createStringError(inconvertibleErrorCode(),
                               "invalid Android release input rename plan");
    FinalNames[Rename.SymbolID] = Rename.OutputName;
  }
  auto PlannedClasses = computeAndroidKernelReleaseNameExchangeClasses(
      ReleaseSections, ReleaseSymbols);
  if (!PlannedClasses)
    return PlannedClasses.takeError();

  std::vector<ReleaseInputNamePlan::ExchangeClass> ExchangeClasses;
  ExchangeClasses.reserve(PlannedClasses->size());
  std::vector<unsigned> ClassIDByFinalIndex(Retained.size(),
                                            ReleaseInputNamePlan::InvalidIndex);
  for (const ReleaseSymbolExchangeClass &PlannedClass : *PlannedClasses) {
    const unsigned ClassID = ExchangeClasses.size();
    ReleaseInputNamePlan::ExchangeClass Exchange;
    Exchange.FinalIndices.reserve(PlannedClass.SymbolIDs.size());
    for (uint64_t SymbolID : PlannedClass.SymbolIDs) {
      if (SymbolID > std::numeric_limits<unsigned>::max())
        return createStringError(inconvertibleErrorCode(),
                                 "invalid Android release exchange-class ID");
      Exchange.FinalIndices.push_back(static_cast<unsigned>(SymbolID));
    }
    Exchange.Names.reserve(Exchange.FinalIndices.size());
    for (unsigned I : Exchange.FinalIndices) {
      if (I >= FinalNames.size() ||
          ClassIDByFinalIndex[I] != ReleaseInputNamePlan::InvalidIndex)
        return createStringError(inconvertibleErrorCode(),
                                 "invalid Android release exchange class");
      ClassIDByFinalIndex[I] = ClassID;
      Exchange.Names.push_back(FinalNames[I]);
    }
    llvm::sort(Exchange.Names);
    ExchangeClasses.push_back(std::move(Exchange));
  }

  if (Out.Syms.size() != Retained.size())
    return createStringError(inconvertibleErrorCode(),
                             "Android release output symbol count differs from "
                             "reconstructed inputs");
  for (unsigned I = 0; I < Retained.size(); ++I) {
    const RawSym &Expected = Retained[I].Symbol.Symbol;
    const RawSym &Actual = Out.Syms[I];
    const unsigned ClassID = ClassIDByFinalIndex[I];
    if (ClassID >= ExchangeClasses.size())
      return createStringError(inconvertibleErrorCode(),
                               "missing Android release exchange class");
    const std::vector<std::string> &AllowedNames =
        ExchangeClasses[ClassID].Names;
    if (Expected.Value != Actual.Value || Expected.Size != Actual.Size ||
        Expected.Shndx != Actual.Shndx || Expected.Info != Actual.Info ||
        Expected.Other != Actual.Other ||
        !std::binary_search(AllowedNames.begin(), AllowedNames.end(),
                            Actual.Name.str()))
      return createStringError(
          inconvertibleErrorCode(),
          "Android release output symbol order or observable record differs "
          "from reconstructed inputs");
  }
  for (const ReleaseInputNamePlan::ExchangeClass &Class : ExchangeClasses) {
    std::vector<std::string> ActualNameMultiset;
    ActualNameMultiset.reserve(Class.FinalIndices.size());
    for (unsigned I : Class.FinalIndices)
      ActualNameMultiset.push_back(Out.Syms[I].Name.str());
    llvm::sort(ActualNameMultiset);
    if (Class.Names != ActualNameMultiset)
      return createStringError(
          inconvertibleErrorCode(),
          "Android release output exact-tie name multiset differs from "
          "reconstructed inputs");
  }

  ReleaseInputNamePlan Result;
  Result.Classes = std::move(ExchangeClasses);
  Result.Symbols.resize(Inputs.size());
  for (unsigned P = 0; P < Inputs.size(); ++P) {
    Result.Symbols[P].resize(Inputs[P].Syms.size());
    for (ReleaseInputNamePlan::Entry &Entry : Result.Symbols[P])
      Entry.Status = ReleaseInputNamePlan::Entry::State::Pruned;
    for (unsigned I = 0; I < InputSymbolRefs[P].size(); ++I) {
      const SymbolReference &Reference = InputSymbolRefs[P][I];
      unsigned OldIndex = std::numeric_limits<unsigned>::max();
      if (Reference.K == SymbolReference::Kind::Local)
        OldIndex = Reference.Index;
      else if (Reference.K == SymbolReference::Kind::Global)
        OldIndex = FirstGlobal + Reference.Index;
      if (OldIndex >= OldToReordered.size())
        continue;
      const unsigned ReorderedIndex = OldToReordered[OldIndex];
      if (ReorderedIndex >= ReorderedToFinal.size())
        continue;
      const unsigned FinalIndex = ReorderedToFinal[ReorderedIndex];
      if (FinalIndex < FinalNames.size()) {
        ReleaseInputNamePlan::Entry &Entry = Result.Symbols[P][I];
        Entry.Status = ReleaseInputNamePlan::Entry::State::Survivor;
        Entry.FinalIndex = FinalIndex;
        Entry.ClassID = ClassIDByFinalIndex[FinalIndex];
      }
    }
  }

  using RelocationIdentity =
      std::tuple<unsigned, uint64_t, unsigned, uint32_t, int64_t>;
  std::map<RelocationIdentity, uint64_t> ExpectedRelocations;
  for (unsigned P = 0; P < Inputs.size(); ++P) {
    const RawELF &Input = Inputs[P];
    for (const RawRela &Relocation : Input.Relas) {
      if (Relocation.TargetSec >= Placements[P].size() ||
          Relocation.TargetSec >= Input.Secs.size())
        return createStringError(
            inconvertibleErrorCode(),
            "Android release relocation has an invalid target section");
      const SectionPlacement &Placement = Placements[P][Relocation.TargetSec];
      if (!Placement.Emitted)
        continue;
      const auto Width =
          AndroidKernelModuleRelocationPolicy::writeWidth(Relocation.Type);
      const uint64_t InputTargetSize = Input.Secs[Relocation.TargetSec].Size;
      if (!Width || Relocation.Offset > InputTargetSize ||
          *Width > InputTargetSize - Relocation.Offset)
        return createStringError(
            inconvertibleErrorCode(),
            "Android release relocation type is unsupported or its write "
            "span overruns the input section");
      if (Relocation.Offset >
          std::numeric_limits<uint64_t>::max() - Placement.Offset)
        return createStringError(inconvertibleErrorCode(),
                                 "Android release relocation offset overflow");
      if (Relocation.Sym >= Result.Symbols[P].size())
        return createStringError(
            inconvertibleErrorCode(),
            "Android release relocation has an invalid symbol index");
      const ReleaseInputNamePlan::Entry &Projection =
          Result.Symbols[P][Relocation.Sym];
      if (Projection.Status != ReleaseInputNamePlan::Entry::State::Survivor ||
          Projection.FinalIndex >= Out.Syms.size() ||
          Projection.ClassID >= Result.Classes.size())
        return createStringError(
            inconvertibleErrorCode(),
            "Android release relocation target is not a reconstructed "
            "survivor");
      ++ExpectedRelocations[{
          Placement.SectionID, Placement.Offset + Relocation.Offset,
          Projection.FinalIndex, Relocation.Type, Relocation.Addend}];
    }
  }

  std::map<RelocationIdentity, uint64_t> ActualRelocations;
  for (const RawRela &Relocation : Out.Relas) {
    if (Relocation.TargetSec == 0 || Relocation.TargetSec >= Out.Secs.size() ||
        Relocation.Sym >= Out.Syms.size())
      return createStringError(
          inconvertibleErrorCode(),
          "Android release output relocation has an invalid site or symbol "
          "index");
    const auto Width =
        AndroidKernelModuleRelocationPolicy::writeWidth(Relocation.Type);
    const uint64_t OutputTargetSize = Out.Secs[Relocation.TargetSec].Size;
    if (!Width || Relocation.Offset > OutputTargetSize ||
        *Width > OutputTargetSize - Relocation.Offset)
      return createStringError(
          inconvertibleErrorCode(),
          "Android release output relocation type is unsupported or its "
          "write span overruns the target section");
    ++ActualRelocations[{Relocation.TargetSec, Relocation.Offset,
                         Relocation.Sym, Relocation.Type, Relocation.Addend}];
  }
  if (ExpectedRelocations != ActualRelocations)
    return createStringError(
        inconvertibleErrorCode(),
        "Android release output relocations differ from exact input replay");
  return Result;
}

bool auditAndroidKernelReleaseDebugCompression(const RawELF &Out,
                                               const Options &Opts,
                                               std::string *Err) {
  using namespace ELF;
  const bool CompressionRequested =
      Opts.debugCompression != DebugCompressionType::None;
  compression::Format ExpectedFormat = compression::Format::Zlib;
  uint32_t ExpectedType = 0;
  if (CompressionRequested) {
    ExpectedFormat = compression::formatFor(Opts.debugCompression);
    if (const char *Reason =
            compression::getReasonIfUnsupported(ExpectedFormat))
      return fail(Err, "verify: requested Android release debug compression "
                       "codec is unavailable: " +
                           Twine(Reason));
    ExpectedType = Opts.debugCompression == DebugCompressionType::Zlib
                       ? ELFCOMPRESS_ZLIB
                       : ELFCOMPRESS_ZSTD;
  }

  for (unsigned I = 0; I < Out.Secs.size(); ++I) {
    const RawSec &Section = Out.Secs[I];
    if (!Opts.dropDebugInfo &&
        detail::isLegacyELFCompressedDebugSection(Section.Name))
      return fail(Err, "verify: Android release cannot retain legacy GNU "
                       "compressed debug section '" +
                           Section.Name + "'");

    const bool Eligible = Section.Name.starts_with(".debug_") &&
                          !(Section.Flags & SHF_ALLOC) &&
                          Section.Type != SHT_NOBITS && Section.Size != 0;
    const bool IsCompressed = (Section.Flags & SHF_COMPRESSED) != 0;
    if (IsCompressed && (!Eligible || !CompressionRequested))
      return fail(Err, "verify: Android release section '" + Section.Name +
                           "' has unsupported compression semantics");
    if (!Eligible || !CompressionRequested)
      continue;

    const ArrayRef<uint8_t> Logical = Out.secData(I);
    if (Logical.size() != Section.Size)
      return fail(Err, "verify: Android release debug section '" +
                           Section.Name + "' did not round-trip");
    SmallVector<uint8_t, 0> Recompressed;
    compression::compress(ExpectedFormat, Logical, Recompressed);
    const bool ShouldCompress =
        Logical.size() > sizeof(Elf64_Chdr) + Recompressed.size();
    if (IsCompressed != ShouldCompress)
      return fail(Err, "verify: Android release debug section '" +
                           Section.Name +
                           "' does not follow the final compression savings "
                           "decision");
    if (!IsCompressed)
      continue;

    if (Section.CompressionType != ExpectedType ||
        Section.CompressionAlignment == 0 ||
        (Section.CompressionAlignment & (Section.CompressionAlignment - 1)) !=
            0 ||
        Section.Alignment != alignof(Elf64_Chdr) ||
        Section.FileSize != sizeof(Elf64_Chdr) + Recompressed.size())
      return fail(Err, "verify: Android release debug section '" +
                           Section.Name +
                           "' has a noncanonical compression header");
    if (Section.Offset > Out.Buf.size() ||
        Section.FileSize > Out.Buf.size() - Section.Offset)
      return fail(Err, "verify: Android release compressed payload is out of "
                       "range");
    const ArrayRef<uint8_t> Encoded(
        reinterpret_cast<const uint8_t *>(Out.Buf.data()) + Section.Offset +
            sizeof(Elf64_Chdr),
        Section.FileSize - sizeof(Elf64_Chdr));
    if (!std::equal(Encoded.begin(), Encoded.end(), Recompressed.begin()))
      return fail(Err, "verify: Android release debug section '" +
                           Section.Name +
                           "' payload is not the canonical requested codec "
                           "encoding");
  }
  return true;
}

AndroidKernelModuleSectionPolicy::CanonicalSectionShapeView
canonicalSectionShape(const RawSec &Section) {
  return {Section.Name,    Section.NameOffset,
          Section.Type,    Section.Flags,
          Section.Size,    Section.Offset,
          Section.Address, Section.Alignment,
          Section.Entsize, Section.Link,
          Section.Info,    (Section.Flags & ELF::SHF_COMPRESSED) != 0};
}

bool verifyCanonicalAndroidReleaseMetadataSuffix(const RawELF &Out,
                                                 std::string *Err) {
  using Kind = AndroidKernelModuleSectionPolicy::CanonicalMetadataKind;
  using AndroidKernelModuleSectionPolicy::matchesCanonicalMetadataShape;
  using namespace ELF;

  if (Out.Secs.empty())
    return fail(Err, "verify: Android release has no section-zero header");
  if (!Out.Secs.front().Name.empty())
    return fail(Err, "verify: Android release .shstrtab offset-zero entry "
                     "does not decode to an empty section-zero name");
  if (!matchesCanonicalMetadataShape(Kind::Null,
                                     canonicalSectionShape(Out.Secs.front())))
    return fail(Err, "verify: Android release section zero is not the "
                     "canonical all-zero header");

  unsigned SymtabIndex = std::numeric_limits<unsigned>::max();
  unsigned StrtabIndex = std::numeric_limits<unsigned>::max();
  unsigned ShstrtabIndex = std::numeric_limits<unsigned>::max();
  unsigned SymtabNames = 0;
  unsigned StrtabNames = 0;
  unsigned ShstrtabNames = 0;
  unsigned Symtabs = 0;
  for (unsigned I = 1; I < Out.Secs.size(); ++I) {
    const RawSec &Section = Out.Secs[I];
    if (Section.Type == SHT_SYMTAB) {
      ++Symtabs;
      SymtabIndex = I;
    }
    if (Section.Name == ".symtab")
      ++SymtabNames;
    if (Section.Name == ".strtab") {
      ++StrtabNames;
      StrtabIndex = I;
    }
    if (Section.Name == ".shstrtab") {
      ++ShstrtabNames;
      ShstrtabIndex = I;
    }
  }

  if (Symtabs != 1 || SymtabNames != 1 || StrtabNames != 1 ||
      ShstrtabNames != 1 || SymtabIndex == 0 ||
      SymtabIndex == std::numeric_limits<unsigned>::max() ||
      StrtabIndex != SymtabIndex + 1 || ShstrtabIndex + 1 != Out.Secs.size() ||
      Out.SectionStringTableIndex != ShstrtabIndex ||
      Out.SymbolStringTableIndex != StrtabIndex)
    return fail(Err, "verify: Android release metadata is not one ordered "
                     ".symtab/.strtab/.rela*/.shstrtab suffix");

  if (!matchesCanonicalMetadataShape(
          Kind::Symtab, canonicalSectionShape(Out.Secs[SymtabIndex]),
          StrtabIndex, Out.SymtabInfo) ||
      !matchesCanonicalMetadataShape(
          Kind::Strtab, canonicalSectionShape(Out.Secs[StrtabIndex])) ||
      !matchesCanonicalMetadataShape(
          Kind::Shstrtab, canonicalSectionShape(Out.Secs[ShstrtabIndex])))
    return fail(Err, "verify: Android release symbol/string metadata header "
                     "shape is noncanonical");

  for (unsigned I = 1; I < SymtabIndex; ++I)
    if (AndroidKernelModuleSectionPolicy::isCanonicalReleaseOutputMetadataType(
            Out.Secs[I].Type))
      return fail(Err, "verify: Android release metadata precedes the fixed "
                       "metadata suffix");

  unsigned PreviousTarget = 0;
  for (unsigned I = StrtabIndex + 1; I < ShstrtabIndex; ++I) {
    const RawSec &RelocationSection = Out.Secs[I];
    if (RelocationSection.Type != SHT_RELA ||
        RelocationSection.Info <= PreviousTarget ||
        RelocationSection.Info >= SymtabIndex ||
        !matchesCanonicalMetadataShape(
            Kind::Rela, canonicalSectionShape(RelocationSection), SymtabIndex,
            RelocationSection.Info, Out.Secs[RelocationSection.Info].Name))
      return fail(Err, "verify: Android release relocation metadata is "
                       "noncanonical, empty, duplicated, or out of target "
                       "section order");
    PreviousTarget = RelocationSection.Info;
  }

  return true;
}

bool verifyAndroidKernelModuleContract(const RawELF &Out,
                                       bool RequireFinalizedOutput,
                                       std::string *Err) {
  using namespace ELF;

  if (Out.Type != ET_REL)
    return fail(Err, "verify: Android kernel module output is not ET_REL");
  if (Out.Machine != EM_AARCH64)
    return fail(Err,
                "verify: Android kernel module output does not target AArch64");

  const RawSec *Versions = nullptr;
  const RawSec *AllocTags = nullptr;
  const RawSec *SymbolTable = nullptr;
  unsigned VersionsCount = 0;
  unsigned AllocTagsCount = 0;
  unsigned SymbolTableCount = 0;
  unsigned SymbolStringTableCount = 0;
  unsigned SectionStringTableCount = 0;
  unsigned StringTableCount = 0;
  unsigned AllocTagsIndex = 0;
  StringSet<> LoadedSectionNames;
  for (unsigned I = 0; I < Out.Secs.size(); ++I) {
    const RawSec &S = Out.Secs[I];
    if (RequireFinalizedOutput &&
        AndroidKernelModuleSectionPolicy::rejectsReleaseOutputTypeAtIndex(
            S.Type, I == 0))
      return fail(Err, "verify: Android kernel module release output contains "
                       "an unsupported section type at index " +
                           Twine(I));
    if (!AndroidKernelModuleSectionPolicy::isCanonicalReleaseOutputMetadataType(
            S.Type)) {
      if (S.Flags & SHF_INFO_LINK)
        return fail(Err, "verify: Android kernel module ordinary section '" +
                             S.Name + "' uses unsupported SHF_INFO_LINK");
      if (S.Flags & SHF_LINK_ORDER) {
        if (S.Link == 0 || S.Link >= Out.Secs.size() || S.Info != 0)
          return fail(Err, "verify: Android kernel module SHF_LINK_ORDER "
                           "section '" +
                               S.Name + "' has malformed link metadata");
      } else if (S.Link != 0 || S.Info != 0) {
        return fail(Err, "verify: Android kernel module ordinary section '" +
                             S.Name + "' has nonzero sh_link/sh_info");
      }
    }
    // Same !sect_empty() uniqueness contract as the producer guard.
    if (detail::isLoadedELFModuleSection(S.Flags, S.Size) &&
        !LoadedSectionNames.insert(S.Name).second)
      return fail(Err, "verify: duplicate loaded section name '" + S.Name +
                           "' in Android kernel module output");
    if (S.Type == SHT_SYMTAB) {
      SymbolTable = &S;
      ++SymbolTableCount;
    }
    if (S.Type == SHT_STRTAB)
      ++StringTableCount;
    if (S.Name == ".strtab" && S.Type == SHT_STRTAB)
      ++SymbolStringTableCount;
    if (S.Name == ".shstrtab" && S.Type == SHT_STRTAB)
      ++SectionStringTableCount;
    if (S.Name == "__versions") {
      Versions = &S;
      ++VersionsCount;
    } else if (S.Name == ".codetag.alloc_tags") {
      AllocTags = &S;
      AllocTagsIndex = I;
      ++AllocTagsCount;
    } else if (S.Name == "alloc_tags") {
      return fail(Err, "verify: uncollected alloc_tags input section remains "
                       "in Android kernel module output");
    }
  }

  if (RequireFinalizedOutput &&
      !verifyCanonicalAndroidReleaseMetadataSuffix(Out, Err))
    return false;

  if (VersionsCount != 1)
    return fail(Err, "verify: Android kernel module output must contain "
                     "exactly one __versions section (found " +
                         Twine(VersionsCount) + ")");
  if (Versions->Type != SHT_PROGBITS || !(Versions->Flags & SHF_ALLOC) ||
      (Versions->Flags & SHF_COMPRESSED))
    return fail(Err, "verify: __versions must be an allocated, uncompressed "
                     "SHT_PROGBITS section");
  if (Versions->Alignment < 8 ||
      (Versions->Alignment & (Versions->Alignment - 1)) != 0)
    return fail(Err,
                "verify: __versions alignment must be a power of two >= 8");
  constexpr uint64_t ModVersionEntrySize = 64;
  if (Versions->Size % ModVersionEntrySize != 0)
    return fail(Err, "verify: __versions size must be a multiple of 64 bytes");

  // The Linux module loader rejects a fully stripped ET_REL module: it needs
  // exactly one SHT_SYMTAB and that table's linked string table to resolve
  // imports and apply relocations.  Audit this before any symbol-level checks.
  if (SymbolTableCount != 1)
    return fail(Err, "verify: Android kernel module output must contain "
                     "exactly one symbol table (found " +
                         Twine(SymbolTableCount) + ")");
  if (SymbolStringTableCount != 1)
    return fail(Err, "verify: Android kernel module output must contain "
                     "exactly one .strtab (found " +
                         Twine(SymbolStringTableCount) + ")");
  if (StringTableCount != 2 || SectionStringTableCount != 1 ||
      Out.SymbolStringTableIndex >= Out.Secs.size() ||
      Out.SectionStringTableIndex >= Out.Secs.size() ||
      Out.SymbolStringTableIndex == Out.SectionStringTableIndex ||
      Out.Secs[Out.SymbolStringTableIndex].Name != ".strtab" ||
      Out.Secs[Out.SectionStringTableIndex].Name != ".shstrtab")
    return fail(Err, "verify: Android kernel module output must contain "
                     "exactly the canonical .strtab and .shstrtab tables");
  const auto IsCanonicalStringTable = [](const RawSec &Table) {
    return Table.Type == SHT_STRTAB && Table.Flags == 0 &&
           Table.Alignment == 1 && Table.Entsize == 0 && Table.Link == 0 &&
           Table.Info == 0;
  };
  if (!IsCanonicalStringTable(Out.Secs[Out.SymbolStringTableIndex]) ||
      !IsCanonicalStringTable(Out.Secs[Out.SectionStringTableIndex]))
    return fail(Err, "verify: Android kernel module string tables must be "
                     "uncompressed canonical SHT_STRTAB sections");
  if (!SymbolTable || SymbolTable->Link >= Out.Secs.size() ||
      Out.Secs[SymbolTable->Link].Type != SHT_STRTAB ||
      Out.Secs[SymbolTable->Link].Name != ".strtab")
    return fail(Err, "verify: Android kernel module symbol table must link "
                     "to .strtab");

  if (RequireFinalizedOutput) {
    for (const RawSec &S : Out.Secs) {
      if (detail::isAndroidKernelProfileContractSection(S.Name))
        return fail(Err, "verify: finalized Android kernel module must not "
                         "retain the NeverC profile-contract section");
    }
    for (const RawSym &S : Out.Syms) {
      if (detail::isAndroidKernelProfileContractSymbol(S.Name))
        return fail(Err, "verify: finalized Android kernel module must not "
                         "retain the NeverC profile-contract symbol");
    }
  }

  if (AllocTagsCount != 1)
    return fail(Err, "verify: Android kernel module output must contain "
                     "exactly one .codetag.alloc_tags section (found " +
                         Twine(AllocTagsCount) + ")");
  constexpr uint64_t RequiredAllocTagFlags = SHF_ALLOC | SHF_WRITE;
  if (AllocTags->Type != SHT_PROGBITS ||
      (AllocTags->Flags & RequiredAllocTagFlags) != RequiredAllocTagFlags ||
      (AllocTags->Flags & SHF_COMPRESSED))
    return fail(Err, "verify: .codetag.alloc_tags must be an uncompressed "
                     "SHT_PROGBITS section with SHF_ALLOC | SHF_WRITE");
  if (AllocTags->Alignment < 8 ||
      (AllocTags->Alignment & (AllocTags->Alignment - 1)) != 0)
    return fail(Err, "verify: .codetag.alloc_tags alignment must be a power "
                     "of two >= 8");

  if (!Out.HasSymtab)
    return fail(Err,
                "verify: Android kernel module output has no symbol table");

  const RawSym *Start = nullptr;
  const RawSym *Stop = nullptr;
  unsigned StartCount = 0;
  unsigned StopCount = 0;
  for (const RawSym &S : Out.Syms) {
    if (S.Name == "__start_alloc_tags") {
      Start = &S;
      ++StartCount;
    } else if (S.Name == "__stop_alloc_tags") {
      Stop = &S;
      ++StopCount;
    }
  }
  if (StartCount != 1)
    return fail(Err, "verify: Android kernel module output must define exactly "
                     "one __start_alloc_tags symbol (found " +
                         Twine(StartCount) + ")");
  if (StopCount != 1)
    return fail(Err, "verify: Android kernel module output must define exactly "
                     "one __stop_alloc_tags symbol (found " +
                         Twine(StopCount) + ")");

  auto CheckBoundary = [&](StringRef Name, const RawSym &S,
                           uint64_t ExpectedValue) {
    if (S.bind() != STB_GLOBAL)
      return fail(Err, "verify: " + Name + " must have STB_GLOBAL binding");
    if (S.type() != STT_NOTYPE)
      return fail(Err, "verify: " + Name + " must have STT_NOTYPE type");
    if (S.Shndx != AllocTagsIndex)
      return fail(Err, "verify: " + Name +
                           " must be defined in .codetag.alloc_tags");
    if (S.Value != ExpectedValue)
      return fail(Err, "verify: " + Name + " has value " + Twine(S.Value) +
                           ", expected " + Twine(ExpectedValue));
    return true;
  };

  return CheckBoundary("__start_alloc_tags", *Start, 0) &&
         CheckBoundary("__stop_alloc_tags", *Stop, AllocTags->Size);
}

// ---------------------------------------------------------------------------
// Relocation-site anchoring.  Every relocation sits inside some defined symbol;
// the verifier finds "the defined symbol with the greatest value <= the reloc
// offset" (the function/object the site lives in) and uses its already-verified
// merged value to predict where the reloc must re-land.  Originally each
// relocation linear-scanned every anchor in its section — and default codegen
// puts every function in one .text/__text, so that was O(relocs x symbols)
// = O(n^2) per partition, a real cost on large modules.  Sorting the anchors
// by value once and binary-searching per relocation makes it O(n log n) with
// identical results.
// ---------------------------------------------------------------------------
using Anchor = std::pair<uint64_t, StringRef>; // (symbol value, symbol name)
using AnchorVec = SmallVector<Anchor, 0>;
using AnchorMap = DenseMap<unsigned, AnchorVec>; // section index -> anchors

void sortAnchors(AnchorMap &Anchors) {
  for (auto &KV : Anchors)
    // Stable so that, among anchors sharing one value, the last in symbol-table
    // order stays last — exactly the tie-break the old linear scan used
    // ("PR.first >= Best->first" lets a later equal-valued anchor win).
    std::stable_sort(
        KV.second.begin(), KV.second.end(),
        [](const Anchor &A, const Anchor &B) { return A.first < B.first; });
}

// Greatest-value anchor whose value <= Off, or nullptr if none.  Requires
// Sorted to be ascending by value (see sortAnchors).
const Anchor *findAnchor(const AnchorVec &Sorted, uint64_t Off) {
  auto It =
      std::upper_bound(Sorted.begin(), Sorted.end(), Off,
                       [](uint64_t O, const Anchor &P) { return O < P.first; });
  if (It == Sorted.begin())
    return nullptr;
  return &*(It - 1);
}

// ---------------------------------------------------------------------------
// Same-input-section relative-distance invariant.  Every symbol defined in one
// input section is shifted into the merged output by that section's single
// PartOffset, so any two uniquely-named defined symbols sharing an input
// section must keep an identical (merged offset - input offset) and land in the
// same merged section.  This invariant is *content-free*, which is exactly why
// it matters: the byte-content anchor must skip sections that carry no bytes
// (SHT_NOBITS / IMAGE_SCN_CNT_UNINITIALIZED_DATA / S_ZEROFILL, i.e. .bss), so a
// ".bss symbol values all collapse to 0" bug — the BSS twin of the historical
// .text collapse — would otherwise sail through verification.  It holds for any
// conformant -r merge (neverc's own and a real linker's), so it never
// false-rejects.  Residual: a symbol alone in its input section has no sibling
// to anchor against here; that narrow case is left to the content anchor (for
// sections with bytes) and the differential/execution tests (for .bss).
struct SecShift {
  int64_t Delta;     // merged offset - input offset for the first witness
  unsigned OutSec;   // merged section the first witness landed in
  StringRef Witness; // its name, for diagnostics
};

// One input section's reconstructed placement inside a merged output section:
// it occupies the half-open byte range [Base, Base + Size).  Distinct input
// sections folded into one output section must keep these ranges pairwise
// disjoint and wholly inside the merged section — an invariant that holds for
// any conformant -r merge (neverc's and a real linker's), so it never
// false-rejects.  It is the missing third leg of the offset-collapse defense:
//   * the byte-content anchor needs on-disk bytes, so it skips NOBITS/zerofill;
//   * the same-input-section relative-distance invariant needs *two* symbols
//     sharing one input section, so it skips singletons;
//   * this needs only *one* anchorable symbol per input section and no bytes,
//     so it covers a lone .bss/.data global in its own -fdata-sections section
//     — the exact shape the kernel-module mergeSections path and ordinary
//     multi-file .bss both produce, where a collapsed offset previously slipped
//     through every other check.
struct OutSecRange {
  uint64_t Base;
  uint64_t Size;
  StringRef Witness;
};

// Sort one merged section's contributing ranges by base and reject any that run
// past the section end or overlap their neighbor.  Shared by all three format
// verifiers (their raw structs differ but the range arithmetic is identical).
template <typename SizeFn, typename NameFn>
bool checkDisjointRanges(
    DenseMap<unsigned, SmallVector<OutSecRange, 0>> &Ranges, SizeFn SecSize,
    NameFn SecName, std::string *Err) {
  for (auto &KV : Ranges) {
    uint64_t Size = SecSize(KV.first);
    StringRef Name = SecName(KV.first);
    auto &R = KV.second;
    std::stable_sort(R.begin(), R.end(),
                     [](const OutSecRange &A, const OutSecRange &B) {
                       return A.Base < B.Base;
                     });
    for (unsigned i = 0; i < R.size(); ++i) {
      if (R[i].Base > Size || R[i].Size > Size - R[i].Base)
        return fail(Err, "verify: input section of symbol '" + R[i].Witness +
                             "' starts at 0x" + Twine::utohexstr(R[i].Base) +
                             " with size 0x" + Twine::utohexstr(R[i].Size) +
                             ", past the end (0x" + Twine::utohexstr(Size) +
                             ") of merged section '" + Name +
                             "' (offset collapsed or mis-shifted)");
      const uint64_t End = R[i].Base + R[i].Size;
      if (i + 1 < R.size() && End > R[i + 1].Base)
        return fail(Err, "verify: input sections of symbols '" + R[i].Witness +
                             "' and '" + R[i + 1].Witness +
                             "' overlap in merged section '" + Name +
                             "' (offset collapsed or mis-shifted)");
    }
  }
  return true;
}

bool verifyMergeELFImpl(ArrayRef<StringRef> Inputs, ArrayRef<char> Output,
                        const Options &Opts, std::string *Err) {
  using namespace ELF;

  if (Opts.stripUnneededSymbols &&
      (!Opts.androidKernelModule || !Opts.finalizeAndroidKernelModule))
    return fail(Err, "verify: unneeded-symbol stripping is only valid for a "
                     "final Android kernel module");
  if (Opts.stripUnneededSymbols && Opts.artifact == ArtifactKind::SplitDwarf)
    return fail(Err, "verify: Android module release stripping cannot audit "
                     "a Split-DWARF package");

  // A supported merge starts from uncompressed input sections and only adds
  // output-sized metadata such as package indexes. Bound materialized logical
  // bytes by all actual input and output bytes the caller supplied. This keeps
  // generated metadata valid while preventing a tiny hostile object from
  // amplifying an untrusted ch_size into an unbounded allocation.
  uint64_t MaxDecompressedBytes = 0;
  for (StringRef Input : Inputs) {
    if (Input.size() >
        std::numeric_limits<uint64_t>::max() - MaxDecompressedBytes)
      MaxDecompressedBytes = std::numeric_limits<uint64_t>::max();
    else
      MaxDecompressedBytes += Input.size();
  }
  if (Output.size() >
      std::numeric_limits<uint64_t>::max() - MaxDecompressedBytes)
    MaxDecompressedBytes = std::numeric_limits<uint64_t>::max();
  else
    MaxDecompressedBytes += Output.size();
  // A standalone release audit has no uncompressed inputs to contribute to
  // the decompression budget. Permit the same bounded padding/debug expansion
  // as the producer while retaining a hard cap against hostile ch_size values.
  if (Opts.stripUnneededSymbols && Inputs.empty()) {
    constexpr uint64_t StandaloneReleaseExpansionBudget = uint64_t(64) << 20;
    if (StandaloneReleaseExpansionBudget >
        std::numeric_limits<uint64_t>::max() - MaxDecompressedBytes)
      MaxDecompressedBytes = std::numeric_limits<uint64_t>::max();
    else
      MaxDecompressedBytes += StandaloneReleaseExpansionBudget;
  }
  MaterializationLedger ReleaseLedger{MaxDecompressedBytes, 0};
  MaterializationLedger *GlobalLedger =
      Opts.stripUnneededSymbols ? &ReleaseLedger : nullptr;

  RawELF Out;
  if (!parseRawELF(Output, Out, MaxDecompressedBytes,
                   /*StrictRelease=*/Opts.stripUnneededSymbols, GlobalLedger))
    return fail(Err, "verify: merged output is not a parseable ELF64LE object");
  if (Opts.dropDebugInfo)
    for (const RawSec &Section : Out.Secs)
      if (ELFDebugSectionPolicy::isDebugSectionName(Section.Name))
        return fail(Err, "verify: merged output retains debug section '" +
                             Section.Name + "' despite dropDebugInfo");
  if (Opts.stripUnneededSymbols &&
      !auditAndroidKernelReleaseDebugCompression(Out, Opts, Err))
    return false;

  // Reject unsupported input artifact classes independently of the producer.
  if (Opts.stripUnneededSymbols) {
    constexpr uint64_t ModVersionEntrySize = 64;
    std::optional<std::tuple<uint16_t, uint32_t, uint8_t, uint8_t>> InputABI;
    for (unsigned P = 0; P < Inputs.size(); ++P) {
      if (Inputs[P].empty())
        continue;
      RawELF Input;
      if (!parseRawELF(ArrayRef<char>(Inputs[P].data(), Inputs[P].size()),
                       Input, MaxDecompressedBytes,
                       /*StrictRelease=*/true, GlobalLedger))
        return fail(Err, "verify: input partition " + Twine(P) +
                             " is not a parseable ELF64LE object");
      if (!verifyDroppableELFDebugSections(Input, Opts, P, Err))
        return false;

      const auto ABI = std::make_tuple(Input.Machine, Input.Flags, Input.OSABI,
                                       Input.ABIVersion);
      if (!InputABI) {
        InputABI = ABI;
        const auto OutputABI =
            std::make_tuple(Out.Machine, Out.Flags, Out.OSABI, Out.ABIVersion);
        if (OutputABI != ABI)
          return fail(Err, "verify: Android release output ELF ABI header "
                           "does not match the first valid input partition");
      } else if (*InputABI != ABI)
        return fail(Err, "verify: Android release input partition " + Twine(P) +
                             " has an ELF ABI header inconsistent with the "
                             "earlier partitions");

      if (hasLivePatchModInfo(Input))
        return fail(Err, "verify: Android module release strip does not "
                         "support a module marked livepatch in .modinfo");

      for (const RawSec &Section : Input.Secs) {
        // Audit each raw contribution before reconstruction can discard an
        // inactive type or normalize loader-facing flags/alignment. This is
        // intentionally independent of both the producer preflight and the
        // final output's canonical __versions check.
        if (Section.Name == "__versions") {
          if (Section.Type != SHT_PROGBITS || !(Section.Flags & SHF_ALLOC) ||
              (Section.Flags & SHF_COMPRESSED))
            return fail(
                Err,
                "verify: Android module release input __versions must be an "
                "allocated, uncompressed SHT_PROGBITS section");
          if (Section.Alignment < 8 ||
              (Section.Alignment & (Section.Alignment - 1)) != 0)
            return fail(
                Err,
                "verify: Android module release input __versions alignment "
                "must be a power of two >= 8");
          if (Section.Size % ModVersionEntrySize != 0)
            return fail(Err, "verify: Android module release input "
                             "__versions size must be a multiple of 64 bytes");
        }
        if (AndroidKernelModuleSectionPolicy::rejectsReleaseInputType(
                Section.Type))
          return fail(Err, "verify: Android module release input contains an "
                           "unsupported section type");
        if (!Opts.dropDebugInfo &&
            detail::isLegacyELFCompressedDebugSection(Section.Name))
          return fail(Err, "verify: Android module release input cannot "
                           "retain legacy GNU compressed debug section '" +
                               Section.Name + "'");
        if (AndroidKernelModuleSymbolPolicy::isLivePatchSectionName(
                Section.Name) ||
            (Section.Flags &
             AndroidKernelModuleSymbolPolicy::LivePatchRelocationSectionFlag))
          return fail(Err, "verify: Android module release strip does not "
                           "support livepatch section '" +
                               Section.Name + "'");
      }

      for (const RawSym &Symbol : Input.Syms) {
        if (Symbol.Shndx == SHN_COMMON)
          return fail(Err, "verify: Android module release strip refuses "
                           "COMMON symbol '" +
                               Symbol.Name + "'");
        if (Symbol.Shndx ==
            AndroidKernelModuleSymbolPolicy::LivePatchSectionIndex)
          return fail(Err, "verify: Android module release strip does not "
                           "support livepatch symbol '" +
                               Symbol.Name + "'");
        if (Symbol.Shndx >= SHN_LORESERVE && Symbol.Shndx != SHN_ABS)
          return fail(Err, "verify: Android module release strip refuses "
                           "symbol '" +
                               Symbol.Name +
                               "' with an unsupported reserved section index");
      }
    }
  }

  std::optional<ReleaseInputNamePlan> InputReleaseNames;
  if (Opts.stripUnneededSymbols && !Inputs.empty()) {
    auto Reconstructed = reconstructAndroidKernelReleaseInputNames(
        Inputs, Out, Opts, MaxDecompressedBytes, GlobalLedger);
    if (!Reconstructed)
      return fail(Err, "verify: cannot reconstruct Android release names: " +
                           toString(Reconstructed.takeError()));
    InputReleaseNames = std::move(*Reconstructed);
  }

  auto ProjectionFor =
      [&](unsigned Partition, const RawELF &Input,
          const RawSym &Symbol) -> const ReleaseInputNamePlan::Entry * {
    if (!Opts.stripUnneededSymbols || !InputReleaseNames ||
        Partition >= InputReleaseNames->Symbols.size() || Input.Syms.empty() ||
        &Symbol < Input.Syms.data() ||
        &Symbol >= Input.Syms.data() + Input.Syms.size())
      return nullptr;
    const size_t SymbolIndex = &Symbol - Input.Syms.data();
    if (SymbolIndex >= InputReleaseNames->Symbols[Partition].size())
      return nullptr;
    return &InputReleaseNames->Symbols[Partition][SymbolIndex];
  };
  // ---- Structural integrity of the merged object itself ----
  // These are input-independent invariants that catch corruption classes the
  // per-symbol content anchor cannot see (it only looks at uniquely-named
  // symbols).  They hold for any conformant -r object, so they never
  // false-reject a real linker's output either.

  // (1) Symbol-table ordering.  ELF requires every STB_LOCAL symbol to precede
  // the first non-local one, with sh_info marking that boundary.  The merger's
  // __pcg demotion rewrites this ordering, so a bug there would mis-set the
  // boundary and silently corrupt how the loader/linker reads every binding.
  if (Out.HasSymtab) {
    uint32_t FirstGlobal = Out.SymtabInfo;
    if (FirstGlobal > Out.Syms.size())
      return fail(Err, "verify: symtab sh_info (" + Twine(FirstGlobal) +
                           ") exceeds the symbol count " +
                           Twine(Out.Syms.size()));
    for (unsigned i = 0; i < Out.Syms.size(); ++i) {
      bool IsLocal = Out.Syms[i].bind() == STB_LOCAL;
      if (i < FirstGlobal && !IsLocal)
        return fail(Err, "verify: non-local symbol '" + Out.Syms[i].Name +
                             "' at index " + Twine(i) +
                             " precedes the symtab sh_info boundary " +
                             Twine(FirstGlobal) + " (binding order corrupted)");
      if (i >= FirstGlobal && IsLocal)
        return fail(Err, "verify: local symbol '" + Out.Syms[i].Name +
                             "' at index " + Twine(i) +
                             " follows the symtab sh_info boundary " +
                             Twine(FirstGlobal) + " (binding order corrupted)");
    }
  }

  // (2) SHF_LINK_ORDER sections must carry a valid sh_link.  The merger folds
  // such inputs (e.g. ftrace's __patchable_function_entries) and remaps sh_link
  // to the merged target section, so this independently re-checks the produced
  // link is in range.  verifyMerge also audits objects produced elsewhere (e.g.
  // the bundled LLD -r), whose SHF_LINK_ORDER output is likewise only correct
  // with a real sh_link.  A zero or out-of-range link is exactly the
  // ftrace-corruption shape this guards.
  for (unsigned i = 0; i < Out.Secs.size(); ++i) {
    const RawSec &S = Out.Secs[i];
    if (!(S.Flags & SHF_LINK_ORDER))
      continue;
    if (S.Link == 0 || S.Link >= Out.Secs.size())
      return fail(Err, "verify: SHF_LINK_ORDER section '" + S.Name +
                           "' has an invalid sh_link " + Twine(S.Link) +
                           " (ordering/dependency dropped)");
  }

  // (3) Relocation sections must link to a symbol table and target a real
  // section, or their relocations would be applied against the wrong table or
  // the wrong section.
  for (unsigned i = 0; i < Out.Secs.size(); ++i) {
    const RawSec &S = Out.Secs[i];
    if (S.Type != SHT_RELA && S.Type != SHT_REL)
      continue;
    if (S.Link >= Out.Secs.size() || Out.Secs[S.Link].Type != SHT_SYMTAB)
      return fail(Err, "verify: relocation section '" + S.Name + "' sh_link " +
                           Twine(S.Link) +
                           " does not reference a symbol table");
    if (S.Info == 0 || S.Info >= Out.Secs.size())
      return fail(Err, "verify: relocation section '" + S.Name + "' sh_info " +
                           Twine(S.Info) +
                           " does not reference a valid target section");
  }

  // Classify unsupported livepatch artifacts before applying the ordinary
  // Android module contract. A livepatch module can intentionally have a
  // different section shape (and even no `.klp.*` relocations yet), so a later
  // contract error would hide the actual policy boundary.
  if (Opts.stripUnneededSymbols) {
    if (hasLivePatchModInfo(Out))
      return fail(Err, "verify: release Android kernel module retains "
                       ".modinfo metadata marking a livepatch module");
    for (const RawSec &Section : Out.Secs) {
      if (Section.Type == SHT_REL)
        return fail(Err, "verify: release Android kernel module retains an "
                         "unsupported SHT_REL relocation section '" +
                             Section.Name + "'");
      if (AndroidKernelModuleSymbolPolicy::isLivePatchSectionName(
              Section.Name) ||
          (Section.Flags &
           AndroidKernelModuleSymbolPolicy::LivePatchRelocationSectionFlag))
        return fail(Err, "verify: release Android kernel module retains "
                         "unsupported livepatch section '" +
                             Section.Name + "'");
    }
    for (const RawSym &Symbol : Out.Syms)
      if (Symbol.Shndx ==
          AndroidKernelModuleSymbolPolicy::LivePatchSectionIndex)
        return fail(Err, "verify: release Android kernel module retains "
                         "unsupported livepatch symbol '" +
                             Symbol.Name + "'");
  }

  // Android's module loader consumes this small linker-script ABI before the
  // module init function runs. Audit it independently of the merger's
  // synthesis code so a future regression is refused before a broken .ko is
  // written. Ordinary ELF merges deliberately remain unaffected.
  if (Opts.androidKernelModule &&
      !verifyAndroidKernelModuleContract(Out, Opts.finalizeAndroidKernelModule,
                                         Err))
    return false;

  if (Opts.stripUnneededSymbols) {
    for (const RawSec &Section : Out.Secs) {
      if (detail::isAndroidKernelReleaseDiscardableSection(Section.Name))
        return fail(Err, "verify: release Android kernel module retains "
                         "discardable section '" +
                             Section.Name + "'");
    }

    DenseSet<unsigned> ReferencedSymbols;
    for (const RawRela &Relocation : Out.Relas) {
      if (Relocation.Sym >= Out.Syms.size())
        return fail(Err, "verify: release Android kernel module relocation "
                         "references an out-of-range symbol index");
      ReferencedSymbols.insert(Relocation.Sym);
    }
    for (unsigned I = 1; I < Out.Syms.size(); ++I) {
      const RawSym &Symbol = Out.Syms[I];
      if (Symbol.Shndx == SHN_COMMON)
        return fail(Err, "verify: release Android kernel module retains "
                         "unsupported COMMON symbol '" +
                             Symbol.Name + "'");
      if (Symbol.Shndx >= SHN_LORESERVE && Symbol.Shndx != SHN_ABS)
        return fail(Err, "verify: release Android kernel module symbol '" +
                             Symbol.Name +
                             "' uses an unsupported reserved section index");
      if ((Symbol.bind() == STB_LOCAL || Symbol.Shndx == SHN_UNDEF) &&
          !ReferencedSymbols.contains(I))
        return fail(Err, "verify: release Android kernel module retains "
                         "relocation-unneeded symbol '" +
                             Symbol.Name + "'");

      if (Symbol.Shndx != SHN_UNDEF && Symbol.Shndx < SHN_LORESERVE) {
        if (Symbol.Shndx >= Out.Secs.size())
          return fail(Err, "verify: release Android kernel module symbol '" +
                               Symbol.Name +
                               "' has an out-of-range section index");
      }
    }
    if (!auditSerializedAndroidKernelReleaseNames(Out, Err))
      return false;
  }

  // Index output symbols by name; only names that resolve to a *single*
  // output symbol can be anchored unambiguously.
  StringMap<int> OutByName; // name -> sym index, or -1 if duplicated
  // Every output symbol index for a name (not just the unique case).  A name
  // can repeat legitimately in -r output — two file-local statics keep their
  // name — so the unique-anchor path skips duplicates.  This parallel index
  // lets the duplicate case still be content-checked: a correct merge places
  // such a symbol at *some* same-named output symbol whose bytes match, so the
  // offset-collapse anchor extends to them via "at least one faithful
  // representative" instead of being skipped wholesale.
  StringMap<SmallVector<int, 2>> OutByNameMulti;
  for (unsigned i = 0; i < Out.Syms.size(); ++i) {
    StringRef N = Out.Syms[i].Name;
    if (N.empty())
      continue;
    auto It = OutByName.find(N);
    if (It == OutByName.end())
      OutByName[N] = (int)i;
    else
      It->second = -1;
    OutByNameMulti[N].push_back((int)i);
  }
  auto SelectOutputName = [&](unsigned Partition, const RawELF &Input,
                              const RawSym &Symbol) -> std::string {
    if (!Opts.stripUnneededSymbols)
      return Symbol.Name.str();
    const ReleaseInputNamePlan::Entry *Projection =
        ProjectionFor(Partition, Input, Symbol);
    if (!Projection ||
        Projection->Status != ReleaseInputNamePlan::Entry::State::Survivor ||
        Projection->FinalIndex >= Out.Syms.size() || !InputReleaseNames ||
        Projection->ClassID >= InputReleaseNames->Classes.size())
      return {};
    return Out.Syms[Projection->FinalIndex].Name.str();
  };

  // Index output relocations by (merged target section, target symbol name,
  // type) → (offset → the addends seen there), so each input relocation can be
  // confirmed to re-land at its independently predicted merged offset *and*
  // carry the same addend (a wrong addend points at the wrong place even when
  // the site offset is correct).
  std::map<std::string, std::map<uint64_t, std::set<int64_t>>> OutRelocs;
  // Section-target relocations carry no symbol name (their target is an
  // STT_SECTION symbol), so they need a parallel index keyed by the *target
  // section* instead.  Without this they were invisible to the verifier even
  // though they ride the exact same SecOff offset math whose collapse was the
  // historical bug.
  std::map<std::string, std::set<uint64_t>> OutSecRelocs;
  for (const RawRela &R : Out.Relas) {
    if (R.TargetSec >= Out.Secs.size())
      continue;
    StringRef SecN = Out.Secs[R.TargetSec].Name;
    if (R.Sym < Out.Syms.size() && Out.Syms[R.Sym].type() == STT_SECTION) {
      const RawSym &TS = Out.Syms[R.Sym];
      if (TS.Shndx != 0 && TS.Shndx < Out.Secs.size())
        OutSecRelocs[(SecN + "\x01" + Out.Secs[TS.Shndx].Name + "\x01" +
                      Twine(R.Type))
                         .str()]
            .insert(R.Offset);
    }
    StringRef SymN =
        R.Sym < Out.Syms.size() ? Out.Syms[R.Sym].Name : StringRef();
    OutRelocs[(SecN + "\x01" + SymN + "\x01" + Twine(R.Type)).str()][R.Offset]
        .insert(R.Addend);
  }

  // SHF_LINK_ORDER relocation accounting.  A section like ftrace's
  // __patchable_function_entries carries no defined symbol, so the
  // symbol-anchored reloc check below skips its relocations entirely — yet they
  // ride the same per-section PartOffset whose collapse was the historical bug.
  // Two content-free invariants close that gap: (a) every merged reloc in such
  // a section sits at a *distinct* offset (a collapse aliases several onto
  // one), checked here; and (b) the merged reloc count equals the sum of the
  // inputs' (none dropped or duplicated), accumulated across inputs and checked
  // after the input loop.  Both hold for any conformant -r merge, so neither
  // false-rejects (a real linker's -r output included).
  std::map<std::string, std::pair<uint64_t, std::set<uint64_t>>>
      OutLinkOrderRel;
  for (const RawRela &R : Out.Relas) {
    if (R.TargetSec >= Out.Secs.size())
      continue;
    const RawSec &TS = Out.Secs[R.TargetSec];
    if (!(TS.Flags & SHF_LINK_ORDER))
      continue;
    auto &E = OutLinkOrderRel[TS.Name.str()];
    E.first++;
    E.second.insert(R.Offset);
  }
  for (auto &KV : OutLinkOrderRel)
    if (KV.second.first != KV.second.second.size())
      return fail(Err,
                  "verify: SHF_LINK_ORDER section '" + KV.first + "' has " +
                      Twine(KV.second.first) + " relocations at only " +
                      Twine(KV.second.second.size()) +
                      " distinct offsets (offset collapsed or mis-shifted)");
  std::map<std::string, uint64_t> InLinkOrderRelCount;

  // Disjoint-range accumulator (see OutSecRange): one reconstructed range per
  // input section, grouped by the merged output section it folded into, checked
  // for overlap/overflow after every input is processed.
  DenseMap<unsigned, SmallVector<OutSecRange, 0>> SecRanges;
  std::set<std::string> CoalescibleDecidable;
  std::set<std::string> CoalescibleMatched;
  StringMap<SmallVector<unsigned, 2>> OutSectionsByName;
  for (unsigned I = 0; I < Out.Secs.size(); ++I)
    OutSectionsByName[Out.Secs[I].Name].push_back(I);

  for (unsigned p = 0; p < Inputs.size(); ++p) {
    if (Inputs[p].empty())
      continue;
    RawELF In;
    if (!parseRawELF(ArrayRef<char>(Inputs[p].data(), Inputs[p].size()), In,
                     Inputs[p].size(), /*StrictRelease=*/false, GlobalLedger))
      return fail(Err, "verify: input partition " + Twine(p) +
                           " is not a parseable ELF64LE object");
    if (!Opts.stripUnneededSymbols &&
        !verifyDroppableELFDebugSections(In, Opts, p, Err))
      return false;
    if (Opts.stripUnneededSymbols)
      for (const RawSym &Symbol : In.Syms) {
        const ReleaseInputNamePlan::Entry *Projection =
            ProjectionFor(p, In, Symbol);
        if (!Projection ||
            Projection->Status == ReleaseInputNamePlan::Entry::State::Invalid)
          return fail(Err, "verify: incomplete reconstructed release-symbol "
                           "state in input partition " +
                               Twine(p));
      }

    // Independent architecture-consistency leg (mirrors the merger's e_machine
    // refuse).  A conformant -r output carries the inputs' e_machine, so this
    // never false-rejects; a cross-ISA merge — the output header claims one
    // arch but an input is another — is exactly the silent miscompile this
    // catches, and the content/offset anchors cannot (each partition's bytes
    // still match its own input; only the ISA is wrong).
    if (In.Machine != 0 && Out.Machine != In.Machine)
      return fail(Err, "verify: merged output e_machine " + Twine(Out.Machine) +
                           " does not match input partition " + Twine(p) +
                           " e_machine " + Twine(In.Machine) +
                           " (mixed architectures or wrong output header)");

    // Name routing alone cannot prove that an Android mergeable pool was
    // actually demoted.  Audit the independently parsed output header for each
    // eligible input contribution; a malformed same-named input is classified
    // as Kind::None and remains free to occupy its own original section.
    for (const RawSec &InputSection : In.Secs) {
      const detail::ELFSectionFold Expected = foldedSection(InputSection, Opts);
      if (Expected.Kind != detail::ELFSectionFoldKind::AndroidMergeableRodata)
        continue;
      unsigned MatchingHeaders = 0;
      auto It = OutSectionsByName.find(Expected.Name);
      if (It != OutSectionsByName.end())
        for (unsigned OutputIndex : It->second) {
          const RawSec &OutputSection = Out.Secs[OutputIndex];
          if (OutputSection.Type == Expected.Type &&
              OutputSection.Flags == Expected.Flags &&
              OutputSection.Entsize == Expected.Entsize)
            ++MatchingHeaders;
        }
      if (MatchingHeaders != 1)
        return fail(Err, "verify: folded Android mergeable rodata '" +
                             Expected.Name +
                             "' must be SHT_PROGBITS with SHF_ALLOC and "
                             "sh_entsize 0");
    }

    // Accumulate this input's SHF_LINK_ORDER relocation counts per merged
    // section name (the count half of the conservation check above).
    for (const RawRela &R : In.Relas) {
      if (R.TargetSec >= In.Secs.size())
        continue;
      const RawSec &TS = In.Secs[R.TargetSec];
      if (!(TS.Flags & SHF_LINK_ORDER) || isExcludedInputSection(TS, Opts))
        continue;
      InLinkOrderRelCount[mergedSectionName(TS, Opts).str()]++;
    }

    for (unsigned SymbolIndex = 0; SymbolIndex < In.Syms.size();
         ++SymbolIndex) {
      const RawSym &S = In.Syms[SymbolIndex];
      if (S.Name.empty() || S.type() == STT_SECTION)
        continue;
      // Defined-in-section only (skip UNDEF, COMMON, ABS, reserved).
      if (S.Shndx == 0 || S.Shndx >= SHN_LORESERVE)
        continue;
      if (S.Shndx >= In.Secs.size())
        continue;
      const RawSec &InSec = In.Secs[S.Shndx];
      if (isExcludedInputSection(InSec, Opts))
        continue;
      if (isSplitDwarfRewrittenSection(InSec.Name, Opts))
        continue;

      if (Opts.stripUnneededSymbols) {
        const ReleaseInputNamePlan::Entry *Projection = ProjectionFor(p, In, S);
        if (!Projection ||
            Projection->Status == ReleaseInputNamePlan::Entry::State::Invalid)
          return fail(Err,
                      "verify: no reconstructed release-symbol state for '" +
                          S.Name + "'");
        if (Projection->Status == ReleaseInputNamePlan::Entry::State::Pruned)
          continue;
      }
      const std::string OutputName = SelectOutputName(p, In, S);
      if (Opts.stripUnneededSymbols && OutputName.empty())
        return fail(Err, "verify: surviving input symbol '" + S.Name +
                             "' has no allowed output name");
      auto It = OutByName.find(OutputName);
      if (It == OutByName.end()) {
        return fail(Err, "verify: defined input symbol '" + S.Name +
                             "' (projected as '" + OutputName +
                             "') missing from merged output");
      }
      if (isCoalescibleDefinition(S)) {
        // A weak/unique definition may legitimately resolve to a different
        // input's copy. It is therefore not an offset anchor for this input
        // section. Still require the surviving output body to match at least
        // one of the input definitions whenever bytes are available.
        bool HasDefinition = false;
        auto MIt = OutByNameMulti.find(OutputName);
        if (MIt != OutByNameMulti.end())
          for (int Idx : MIt->second) {
            const RawSym &Cand = Out.Syms[(unsigned)Idx];
            if (Cand.Shndx != 0 && Cand.Shndx < SHN_LORESERVE &&
                Cand.Shndx < Out.Secs.size()) {
              HasDefinition = true;
              const RawSec &CandSec = Out.Secs[Cand.Shndx];
              StringRef Expected = mergedSectionName(InSec, Opts);
              if (InSec.Type == SHT_NOBITS || CandSec.Type == SHT_NOBITS ||
                  CandSec.Name != Expected || S.Value > InSec.Size ||
                  Cand.Value > CandSec.Size)
                continue;
              ArrayRef<uint8_t> InData = In.secData(S.Shndx);
              ArrayRef<uint8_t> OutData = Out.secData(Cand.Shndx);
              uint64_t Avail =
                  std::min(InSec.Size - S.Value, CandSec.Size - Cand.Value);
              uint64_t W = std::min<uint64_t>(16, Avail);
              if (W == 0 || S.Value + W > InData.size() ||
                  Cand.Value + W > OutData.size())
                continue;
              CoalescibleDecidable.insert(OutputName);
              if (memcmp(InData.data() + S.Value, OutData.data() + Cand.Value,
                         W) == 0)
                CoalescibleMatched.insert(OutputName);
            }
          }
        if (!HasDefinition)
          return fail(Err, "verify: coalescible input symbol '" + S.Name +
                               "' has no definition in merged output");
        continue;
      }
      if (It->second < 0) {
        // Name is not unique in the output (e.g. two file-local statics that
        // share a name).  The unique-name anchor below can't be used, but a
        // correct -r merge still copies this symbol's bytes verbatim into the
        // expected merged section, so *some* same-named output symbol must land
        // there with a byte-matching window.  Require such a faithful
        // representative to exist; only when the input window is decidable
        // (on disk, non-NOBITS) yet no same-named output symbol matches is it a
        // real offset collapse/mis-shift.  All-ambiguous windows still skip.
        if (InSec.Type == SHT_NOBITS || S.Value > InSec.Size)
          continue;
        ArrayRef<uint8_t> InData = In.secData(S.Shndx);
        StringRef Expected = mergedSectionName(InSec, Opts);
        bool AnyDecidable = false, AnyMatch = false;
        auto MIt = OutByNameMulti.find(OutputName);
        if (MIt == OutByNameMulti.end())
          return fail(Err, "verify: defined input symbol '" + S.Name +
                               "' (projected as '" + OutputName +
                               "') missing from merged output");
        for (int Idx : MIt->second) {
          const RawSym &Cand = Out.Syms[(unsigned)Idx];
          if (Cand.Shndx == 0 || Cand.Shndx >= SHN_LORESERVE ||
              Cand.Shndx >= Out.Secs.size())
            continue;
          const RawSec &CSec = Out.Secs[Cand.Shndx];
          if (CSec.Name != Expected || CSec.Type == SHT_NOBITS ||
              Cand.Value > CSec.Size)
            continue; // wrong section / no content — not this representative
          ArrayRef<uint8_t> OutData = Out.secData(Cand.Shndx);
          uint64_t Avail =
              std::min(InSec.Size - S.Value, CSec.Size - Cand.Value);
          uint64_t W = std::min<uint64_t>(16, Avail);
          if (W == 0 || S.Value + W > InData.size() ||
              Cand.Value + W > OutData.size())
            continue; // bytes not on disk — can't decide from this candidate
          AnyDecidable = true;
          if (memcmp(InData.data() + S.Value, OutData.data() + Cand.Value, W) ==
              0) {
            AnyMatch = true;
            break;
          }
        }
        if (AnyDecidable && !AnyMatch)
          return fail(Err,
                      "verify: duplicate-named symbol '" + S.Name +
                          "' has no same-named merged symbol whose content "
                          "matches the input (offset collapsed or "
                          "mis-shifted)");
        continue;
      }
      const RawSym &OutSym = Out.Syms[(unsigned)It->second];

      // An input definition must remain a definition in the output.
      if (OutSym.Shndx == 0 || OutSym.Shndx >= SHN_LORESERVE)
        return fail(Err, "verify: symbol '" + S.Name +
                             "' was defined in input but is undefined/absolute "
                             "in the merged output");
      if (OutSym.Shndx >= Out.Secs.size())
        return fail(Err,
                    "verify: symbol '" + S.Name +
                        "' points at an out-of-range section in the output");
      const RawSec &OutSec = Out.Secs[OutSym.Shndx];

      StringRef Expected = mergedSectionName(InSec, Opts);
      if (OutSec.Name != Expected)
        return fail(Err, "verify: symbol '" + S.Name + "' landed in section '" +
                             OutSec.Name + "' but expected '" + Expected + "'");

      // Content anchor: in -r mode the section bytes are copied verbatim, so
      // the window at the merged value must byte-match the window at the
      // input value.  This is what catches a collapsed/mis-shifted offset.
      if (InSec.Type == SHT_NOBITS || OutSec.Type == SHT_NOBITS)
        continue;
      if (S.Value > InSec.Size)
        continue; // malformed input value — not our class of bug to police
      if (OutSym.Value > OutSec.Size)
        return fail(Err, "verify: symbol '" + S.Name + "' value 0x" +
                             Twine::utohexstr(OutSym.Value) +
                             " is past the end of its merged section");

      ArrayRef<uint8_t> InData = In.secData(S.Shndx);
      ArrayRef<uint8_t> OutData = Out.secData(OutSym.Shndx);
      uint64_t Avail =
          std::min(InSec.Size - S.Value, OutSec.Size - OutSym.Value);
      uint64_t W = std::min<uint64_t>(16, Avail);
      if (W == 0)
        continue;
      if (S.Value + W > InData.size() || OutSym.Value + W > OutData.size())
        continue; // bytes not actually present on disk — skip rather than guess
      if (memcmp(InData.data() + S.Value, OutData.data() + OutSym.Value, W) !=
          0)
        return fail(Err,
                    "verify: symbol '" + S.Name +
                        "' content at its merged offset does not match the "
                        "input (offset collapsed or mis-shifted)");
    }

    // Same-input-section relative-distance invariant (NOBITS-safe; see SecShift
    // above).  SHF_MERGE input sections are excluded: a real linker may
    // de-duplicate their members non-uniformly (neverc's merger never does, but
    // verifyMerge also audits external -r output).
    {
      DenseMap<unsigned, SecShift> Shift;
      for (const RawSym &S : In.Syms) {
        if (S.Name.empty() || S.type() == STT_SECTION)
          continue;
        if (isCoalescibleDefinition(S))
          continue;
        if (S.Shndx == 0 || S.Shndx >= SHN_LORESERVE ||
            S.Shndx >= In.Secs.size())
          continue;
        const RawSec &InSec = In.Secs[S.Shndx];
        if (isExcludedInputSection(InSec, Opts) || (InSec.Flags & SHF_MERGE))
          continue;
        if (S.Value > InSec.Size)
          continue;
        const std::string OutputName = SelectOutputName(p, In, S);
        auto It = OutByName.find(OutputName);
        if (It == OutByName.end() || It->second < 0)
          continue;
        const RawSym &OutSym = Out.Syms[(unsigned)It->second];
        if (OutSym.Shndx == 0 || OutSym.Shndx >= SHN_LORESERVE ||
            OutSym.Shndx >= Out.Secs.size())
          continue;
        int64_t Delta = (int64_t)OutSym.Value - (int64_t)S.Value;
        auto [DIt, Inserted] = Shift.try_emplace(
            (unsigned)S.Shndx, SecShift{Delta, OutSym.Shndx, S.Name});
        if (Inserted)
          continue;
        if (DIt->second.OutSec != OutSym.Shndx)
          return fail(Err,
                      "verify: symbols '" + DIt->second.Witness + "' and '" +
                          S.Name +
                          "' share one input section but landed in different "
                          "merged sections (section split or mis-routed)");
        if (DIt->second.Delta != Delta)
          return fail(Err,
                      "verify: symbols '" + DIt->second.Witness + "' and '" +
                          S.Name +
                          "' share one input section but their merged offsets "
                          "shifted by different amounts (" +
                          Twine(DIt->second.Delta) + " vs " + Twine(Delta) +
                          ") — offset collapsed or mis-shifted");
      }
    }

    // Disjoint-range invariant (singleton- and NOBITS-safe; see OutSecRange).
    // Reconstruct each input section's merged base from one anchorable defined
    // symbol (base = merged value - input value) and remember its [base, size).
    // SHF_MERGE inputs are excluded: a real linker may coalesce their members.
    {
      DenseMap<unsigned, char> SeenInputSec; // input shndx -> recorded
      for (const RawSym &S : In.Syms) {
        if (S.Name.empty() || S.type() == STT_SECTION)
          continue;
        if (isCoalescibleDefinition(S))
          continue;
        if (S.Shndx == 0 || S.Shndx >= SHN_LORESERVE ||
            S.Shndx >= In.Secs.size())
          continue;
        const RawSec &InSec = In.Secs[S.Shndx];
        if (isExcludedInputSection(InSec, Opts) || (InSec.Flags & SHF_MERGE))
          continue;
        if (InSec.Size == 0 || S.Value > InSec.Size)
          continue;
        if (SeenInputSec.count((unsigned)S.Shndx))
          continue;
        const std::string OutputName = SelectOutputName(p, In, S);
        auto It = OutByName.find(OutputName);
        if (It == OutByName.end() || It->second < 0)
          continue; // not anchorable from this symbol; another may serve
        const RawSym &OutSym = Out.Syms[(unsigned)It->second];
        if (OutSym.Shndx == 0 || OutSym.Shndx >= SHN_LORESERVE ||
            OutSym.Shndx >= Out.Secs.size())
          continue;
        // A defined symbol can never move *before* its own section offset in a
        // concatenating -r merge (the section base is >= 0), so this alone is a
        // collapse on a section whose bytes the content anchor could not see.
        if (OutSym.Value < S.Value)
          return fail(Err, "verify: symbol '" + S.Name + "' merged value 0x" +
                               Twine::utohexstr(OutSym.Value) +
                               " is below its input section offset 0x" +
                               Twine::utohexstr(S.Value) +
                               " (offset collapsed or mis-shifted)");
        SeenInputSec[(unsigned)S.Shndx] = 1;
        SecRanges[OutSym.Shndx].push_back(
            {OutSym.Value - S.Value, InSec.Size, S.Name});
      }
    }

    // Symbol-anchored relocation check: every relocation site sits inside some
    // defined symbol; using that symbol's already-verified merged value we
    // independently predict where the relocation must re-land and confirm an
    // output relocation for the same target/type exists there.  This is the
    // relocation half of the offset-collapse bug (symbols above are the other).
    AnchorMap Anchors;
    // AnchorMap is shared with the COFF and Mach-O verifiers and stores
    // StringRef. Keep independently projected release names alive for the
    // complete relocation audit below instead of pointing at temporaries.
    std::set<std::string> ProjectedAnchorNames;
    for (const RawSym &S : In.Syms) {
      if (S.Name.empty() || S.type() == STT_SECTION)
        continue;
      if (isCoalescibleDefinition(S))
        continue;
      if (S.Shndx == 0 || S.Shndx >= SHN_LORESERVE || S.Shndx >= In.Secs.size())
        continue;
      const std::string OutputName = SelectOutputName(p, In, S);
      auto It = OutByName.find(OutputName);
      if (It == OutByName.end() || It->second < 0)
        continue;
      const auto [Stored, Inserted] =
          ProjectedAnchorNames.insert(std::move(OutputName));
      (void)Inserted;
      Anchors[S.Shndx].push_back({S.Value, *Stored});
    }
    sortAnchors(Anchors);
    for (const RawRela &R : In.Relas) {
      if (R.TargetSec == 0 || R.TargetSec >= In.Secs.size())
        continue;
      const RawSec &T = In.Secs[R.TargetSec];
      if (isExcludedInputSection(T, Opts))
        continue;
      if (R.Sym >= In.Syms.size())
        continue;
      const RawSym &Tgt = In.Syms[R.Sym];
      // Release mode already reconstructed and compared the complete raw RELA
      // multiset by final symbol index above. Do not weaken that proof by
      // re-keying relocations through serialized names here: exact preserved
      // names can repeat, generated tie names can exchange, and empty names
      // carry no identity at all.
      if (Opts.stripUnneededSymbols)
        continue;
      SmallVector<StringRef, 4> TargetNames;
      TargetNames.push_back(Tgt.Name);
      const bool HasNamedTarget = llvm::any_of(
          TargetNames, [](StringRef Name) { return !Name.empty(); });
      // A section-relative relocation (target is an STT_SECTION symbol, so it
      // has no name) is "section base + addend".  It is keyed by the target
      // section, defined-in-section only, so the merged section base is known.
      bool IsSecTarget = !HasNamedTarget && Tgt.type() == STT_SECTION &&
                         Tgt.Shndx != 0 && Tgt.Shndx < In.Secs.size() &&
                         !isExcludedInputSection(In.Secs[Tgt.Shndx], Opts);
      if (!HasNamedTarget && !IsSecTarget)
        continue; // genuinely unnameable target — can't key it cleanly
      auto AIt = Anchors.find(R.TargetSec);
      if (AIt == Anchors.end())
        continue;
      // Anchor = the defined symbol with the greatest value <= reloc offset
      // (the function/object the relocation site lives in).
      const Anchor *Best = findAnchor(AIt->second, R.Offset);
      if (!Best)
        continue;
      auto OIt = OutByName.find(Best->second);
      if (OIt == OutByName.end() || OIt->second < 0)
        continue;
      uint64_t Expected =
          Out.Syms[(unsigned)OIt->second].Value + (R.Offset - Best->first);
      StringRef MergedSec = mergedSectionName(T, Opts);
      if (IsSecTarget) {
        StringRef TgtSec = mergedSectionName(In.Secs[Tgt.Shndx], Opts);
        auto MIt = OutSecRelocs.find(
            (MergedSec + "\x01" + TgtSec + "\x01" + Twine(R.Type)).str());
        if (MIt == OutSecRelocs.end() || !MIt->second.count(Expected))
          return fail(Err, "verify: section-relative relocation into '" +
                               TgtSec + "' applied in '" + MergedSec +
                               "' expected at merged offset 0x" +
                               Twine::utohexstr(Expected) +
                               " is missing (offset collapsed or mis-routed)");
        continue;
      }
      bool FoundOffset = false;
      bool FoundAddend = false;
      StringRef DiagnosticName;
      for (StringRef SymN : TargetNames) {
        if (SymN.empty())
          continue;
        if (DiagnosticName.empty())
          DiagnosticName = SymN;
        auto MIt = OutRelocs.find(
            (MergedSec + "\x01" + SymN + "\x01" + Twine(R.Type)).str());
        if (MIt == OutRelocs.end() || !MIt->second.count(Expected))
          continue;
        FoundOffset = true;
        if (MIt->second.at(Expected).count(R.Addend)) {
          FoundAddend = true;
          break;
        }
      }
      if (!FoundOffset)
        return fail(Err,
                    "verify: relocation against allowed target class of '" +
                        DiagnosticName + "' in section '" + MergedSec +
                        "' expected at merged offset 0x" +
                        Twine::utohexstr(Expected) +
                        " is missing (offset collapsed or mis-routed)");
      // The reloc re-landed at the right offset; its addend must survive the
      // merge verbatim too.  In -r both neverc and a real linker copy r_addend
      // unchanged for a *named*-symbol target (only section-relative targets,
      // which take the OutSecRelocs path above and are skipped here, ever
      // re-base the addend), so a divergence here is a corrupted addend — a
      // "loads fine, then reads/jumps to the wrong place" miscompile that the
      // offset check alone cannot see.
      if (!FoundAddend)
        return fail(Err, "verify: relocation against '" + DiagnosticName +
                             "' in section '" + MergedSec +
                             "' at merged offset 0x" +
                             Twine::utohexstr(Expected) + " has input addend " +
                             Twine(R.Addend) +
                             " with no matching merged addend (addend "
                             "corrupted)");
    }
  }

  for (const std::string &Name : CoalescibleDecidable)
    if (!CoalescibleMatched.count(Name))
      return fail(Err, "verify: coalescible symbol '" + Name +
                           "' has no surviving definition whose content "
                           "matches any input copy");

  // Count half of the SHF_LINK_ORDER conservation check: every input
  // contribution must survive into the merged section (the distinct-offset half
  // ran before the input loop).
  for (auto &KV : InLinkOrderRelCount) {
    auto It = OutLinkOrderRel.find(KV.first);
    uint64_t OutCnt = It == OutLinkOrderRel.end() ? 0 : It->second.first;
    if (OutCnt != KV.second)
      return fail(
          Err, "verify: SHF_LINK_ORDER section '" + KV.first + "' merged " +
                   Twine(OutCnt) + " relocations but inputs contributed " +
                   Twine(KV.second) + " (relocations dropped or duplicated)");
  }

  // Disjoint-range check: distinct input sections folded into one merged
  // section must occupy non-overlapping ranges that fit inside it.
  if (!checkDisjointRanges(
          SecRanges,
          [&](unsigned I) {
            return I < Out.Secs.size() ? Out.Secs[I].Size : 0;
          },
          [&](unsigned I) {
            return I < Out.Secs.size() ? Out.Secs[I].Name : StringRef();
          },
          Err))
    return false;

  return true;
}

// ---------------------------------------------------------------------------
// COFF verifier.  COFF copies section bytes verbatim — relocations live in a
// side table, with no in-place fixups — so the same content anchor as ELF
// holds.  COFF symbol values are already section-relative, and the merger has
// no section-rename knob, so the expected output section name is the input's.
// ---------------------------------------------------------------------------

uint16_t rd16(const char *P) {
  return (uint16_t)((uint8_t)P[0] | ((uint8_t)P[1] << 8));
}
uint32_t rd32(const char *P) {
  return (uint32_t)((uint8_t)P[0]) | ((uint32_t)(uint8_t)P[1] << 8) |
         ((uint32_t)(uint8_t)P[2] << 16) | ((uint32_t)(uint8_t)P[3] << 24);
}

struct RawCoffSec {
  StringRef Name;
  uint32_t RawSize = 0;
  uint32_t RawPtr = 0;
  uint32_t Chars = 0;
  uint32_t RelPtr = 0;
  uint32_t NRel = 0;
};
struct RawCoffSym {
  StringRef Name;
  uint32_t Value = 0;
  int16_t SecNum = 0;
  uint8_t Storage = 0;
  uint32_t Raw = 0; // index in the on-disk symbol table (aux slots included)
  // For a WeakExternal symbol, the coff_aux_weak_external::TagIndex — a raw
  // symbol-table slot naming the default definition — else -1.  Aux records
  // carry no section bytes, so a wrong TagIndex is invisible to the content and
  // offset anchors; capturing it here lets the verifier re-derive it by name.
  int64_t WeakTag = -1;
};
struct RawCoffRel {
  unsigned SecIdx0 = 0; // 0-based section the relocation applies to
  uint32_t VA = 0;      // section-relative offset of the relocation site
  uint32_t SymRaw = 0;  // raw symbol-table index of the target
  uint16_t Type = 0;
};
struct RawCOFF {
  ArrayRef<char> Buf;
  uint16_t Machine = 0; // IMAGE_FILE_HEADER.Machine, for the arch check
  SmallVector<RawCoffSec, 0> Secs;
  SmallVector<RawCoffSym, 0> Syms; // aux records skipped (Raw keeps the slot)
  SmallVector<RawCoffRel, 0> Rels;
  // Raw on-disk symbol-table slot (aux records included) -> index into Syms.
  // Built once at parse time so symNameByRaw is O(1); a relocation's target is
  // a raw slot, and looking it up by linear scan per relocation was O(n^2)
  // (called for every relocation over every symbol).  uint32_t key 0 is a
  // valid DenseMap key (the reserved keys are ~0u and ~0u-1, far beyond any
  // real symbol count), so slot 0 needs no special-casing.
  DenseMap<uint32_t, unsigned> RawToSym;

  // Relocations carry a *raw* symbol-table index (aux records included), but
  // Syms drops aux entries; map back via the recorded Raw slot.
  const RawCoffSym *symByRaw(uint32_t RawIdx) const {
    // A relocation's SymbolTableIndex is attacker-controlled and can equal
    // DenseMap's reserved empty/tombstone keys, on which find() is undefined
    // and can return an uninitialized slot index -> out-of-bounds Syms[] read
    // (the merge fuzzer hit this as a BUS).  Treat reserved keys as absent.
    if (detail::isReservedDenseKey(RawIdx))
      return nullptr;
    auto It = RawToSym.find(RawIdx);
    if (It == RawToSym.end())
      return nullptr;
    return &Syms[It->second];
  }

  StringRef symNameByRaw(uint32_t RawIdx) const {
    const RawCoffSym *S = symByRaw(RawIdx);
    return S ? S->Name : StringRef();
  }

  bool isBSS(unsigned N1) const {
    if (N1 == 0 || N1 > Secs.size())
      return false;
    return (Secs[N1 - 1].Chars &
            llvm::COFF::IMAGE_SCN_CNT_UNINITIALIZED_DATA) != 0;
  }
  ArrayRef<uint8_t> secData(unsigned N1) const { // 1-based section number
    if (N1 == 0 || N1 > Secs.size())
      return {};
    const RawCoffSec &S = Secs[N1 - 1];
    if (isBSS(N1) || S.RawPtr == 0 || S.RawSize == 0)
      return {};
    if (S.RawPtr > Buf.size() || S.RawSize > Buf.size() - S.RawPtr)
      return {};
    return ArrayRef<uint8_t>(
        reinterpret_cast<const uint8_t *>(Buf.data()) + S.RawPtr, S.RawSize);
  }
};

bool parseRawCOFF(ArrayRef<char> Buf, RawCOFF &Out) {
  Out.Buf = Buf;
  if (Buf.size() < 20)
    return false;
  const char *P = Buf.data();
  Out.Machine = rd16(P); // IMAGE_FILE_HEADER.Machine (offset 0)
  unsigned NSec = rd16(P + 2);
  uint32_t SymOff = rd32(P + 8);
  uint32_t NSym = rd32(P + 12);
  uint16_t OptSize = rd16(P + 16);
  uint64_t SecBase = 20ull + OptSize;
  if (SecBase + (uint64_t)NSec * 40 > Buf.size())
    return false;

  uint64_t StrBase = (uint64_t)SymOff + (uint64_t)NSym * 18;
  uint64_t StrSize = 0;
  if (SymOff != 0 && StrBase + 4 <= Buf.size())
    StrSize = rd32(Buf.data() + StrBase);

  auto longName = [&](uint32_t Off) -> StringRef {
    if (Off >= StrSize || StrBase + Off >= Buf.size())
      return {};
    const char *S = Buf.data() + StrBase + Off;
    return StringRef(S, strnlen(S, Buf.size() - (StrBase + Off)));
  };
  auto inlineName = [&](const char *F) -> StringRef {
    return StringRef(F, strnlen(F, 8));
  };
  auto symName = [&](const char *F) -> StringRef {
    if (rd32(F) == 0) // long-name escape: 4 zero bytes, then strtab offset
      return longName(rd32(F + 4));
    return inlineName(F);
  };

  Out.Secs.reserve(NSec);
  for (unsigned i = 0; i < NSec; ++i) {
    const char *H = Buf.data() + SecBase + i * 40;
    RawCoffSec RS;
    if (H[0] == '/') {
      char Tmp[8] = {0};
      memcpy(Tmp, H + 1, 7);
      RS.Name = longName((uint32_t)strtoul(Tmp, nullptr, 10));
    } else {
      RS.Name = inlineName(H);
    }
    RS.RawSize = rd32(H + 16);
    RS.RawPtr = rd32(H + 20);
    RS.RelPtr = rd32(H + 24);
    RS.NRel = rd16(H + 32);
    RS.Chars = rd32(H + 36);
    Out.Secs.push_back(RS);
  }

  if (SymOff != 0 && SymOff + (uint64_t)NSym * 18 <= Buf.size()) {
    unsigned k = 0;
    while (k < NSym) {
      const char *S = Buf.data() + SymOff + (uint64_t)k * 18;
      RawCoffSym PS;
      PS.Name = symName(S);
      PS.Value = rd32(S + 8);
      PS.SecNum = (int16_t)rd16(S + 12);
      PS.Storage = (uint8_t)S[16];
      PS.Raw = k;
      uint8_t NAux = (uint8_t)S[17];
      if (PS.Storage == llvm::COFF::IMAGE_SYM_CLASS_WEAK_EXTERNAL &&
          NAux >= 1 && (uint64_t)k + 1 < NSym) {
        const char *Aux = Buf.data() + SymOff + (uint64_t)(k + 1) * 18;
        PS.WeakTag = (int64_t)rd32(Aux); // TagIndex is the aux's first 4 bytes
      }
      Out.RawToSym[k] = Out.Syms.size();
      Out.Syms.push_back(PS);
      k += 1u + NAux; // aux records occupy index slots but are not symbols
    }
  }

  // Per-section relocation tables (each on-disk entry is 10 bytes:
  // VirtualAddress u32, SymbolTableIndex u32, Type u16).  Sections flagged
  // IMAGE_SCN_LNK_NRELOC_OVFL stash the real count in the first entry's
  // VirtualAddress; conservatively skip those (the merger only produces them
  // past 65535 relocations, far beyond what these checks need).
  for (unsigned i = 0; i < Out.Secs.size(); ++i) {
    const RawCoffSec &S = Out.Secs[i];
    uint32_t NRel = S.NRel;
    if ((S.Chars & llvm::COFF::IMAGE_SCN_LNK_NRELOC_OVFL) && NRel == 0xffff)
      continue;
    if (NRel == 0 || S.RelPtr == 0)
      continue;
    if ((uint64_t)S.RelPtr + (uint64_t)NRel * 10 > Buf.size())
      continue;
    for (unsigned r = 0; r < NRel; ++r) {
      const char *R = Buf.data() + S.RelPtr + (uint64_t)r * 10;
      RawCoffRel RE;
      RE.SecIdx0 = i;
      RE.VA = rd32(R);
      RE.SymRaw = rd32(R + 4);
      RE.Type = rd16(R + 8);
      Out.Rels.push_back(RE);
    }
  }
  return true;
}

bool verifyMergeCOFFImpl(ArrayRef<StringRef> Inputs, ArrayRef<char> Output,
                         const Options &Opts, std::string *Err) {
  using namespace llvm::COFF;

  RawCOFF Out;
  if (!parseRawCOFF(Output, Out))
    return fail(Err, "verify: merged output is not a parseable COFF object");

  StringMap<int> OutByName;
  // Every output symbol index per name, so a duplicate-named symbol (two
  // file-local statics that share a name) is still content-checked rather than
  // skipped — see the ELF path for the rationale.
  StringMap<SmallVector<int, 2>> OutByNameMulti;
  for (unsigned i = 0; i < Out.Syms.size(); ++i) {
    StringRef N = Out.Syms[i].Name;
    if (N.empty())
      continue;
    auto It = OutByName.find(N);
    if (It == OutByName.end())
      OutByName[N] = (int)i;
    else
      It->second = -1;
    OutByNameMulti[N].push_back((int)i);
  }

  // Output relocations keyed by (section name, target symbol name, type) → the
  // offsets they appear at, so each input relocation can be confirmed to
  // re-land at its independently predicted merged offset.
  std::map<std::string, std::set<uint64_t>> OutRelocs;
  for (const RawCoffRel &R : Out.Rels) {
    if (R.SecIdx0 >= Out.Secs.size())
      continue;
    StringRef SecN = Out.Secs[R.SecIdx0].Name;
    StringRef SymN = Out.symNameByRaw(R.SymRaw);
    OutRelocs[(SecN + "\x01" + SymN + "\x01" + Twine(R.Type)).str()].insert(
        R.VA);
  }

  // Disjoint-range accumulator (see OutSecRange): one reconstructed range per
  // input section, grouped by the merged section it folded into.  COFF symbol
  // values are already section-relative and BSS RawSize carries the virtual
  // size, so the same arithmetic as ELF applies.
  DenseMap<unsigned, SmallVector<OutSecRange, 0>> SecRanges;

  for (unsigned p = 0; p < Inputs.size(); ++p) {
    if (Inputs[p].empty())
      continue;
    RawCOFF In;
    if (!parseRawCOFF(ArrayRef<char>(Inputs[p].data(), Inputs[p].size()), In))
      return fail(Err, "verify: input partition " + Twine(p) +
                           " is not a parseable COFF object");

    // Independent architecture-consistency leg (mirrors the merger's machine
    // refuse).  IMAGE_FILE_MACHINE_UNKNOWN (0) is a wildcard (directives-only
    // objects), so it is skipped; two real but differing machines are the
    // cross-ISA miscompile this catches.
    if (In.Machine != IMAGE_FILE_MACHINE_UNKNOWN &&
        Out.Machine != IMAGE_FILE_MACHINE_UNKNOWN && Out.Machine != In.Machine)
      return fail(Err, "verify: merged output COFF machine " +
                           Twine(Out.Machine) +
                           " does not match input partition " + Twine(p) +
                           " machine " + Twine(In.Machine) +
                           " (mixed architectures or wrong output header)");

    for (const RawCoffSym &S : In.Syms) {
      if (S.Name.empty() || S.SecNum <= 0)
        continue; // undefined / absolute / debug-relative
      if (S.Storage != IMAGE_SYM_CLASS_EXTERNAL &&
          S.Storage != IMAGE_SYM_CLASS_STATIC)
        continue;
      if ((unsigned)S.SecNum > In.Secs.size())
        continue;
      const RawCoffSec &InSec = In.Secs[S.SecNum - 1];
      // Package finalization rewrites `.debug_str_offsets.dwo` in place; COFF
      // also names each section with a STATIC symbol of the same spelling, so
      // the content anchor would otherwise false-reject a correct merge.
      if (isSplitDwarfRewrittenSection(InSec.Name, Opts))
        continue;

      auto It = OutByName.find(S.Name);
      if (It == OutByName.end())
        return fail(Err, "verify: defined input symbol '" + S.Name +
                             "' missing from merged output");
      if (It->second < 0) {
        // Ambiguous name: require at least one same-named output symbol that
        // lands in the expected (same, COFF has no rename) section with a
        // byte-matching window.  Skips when the input window is undecidable.
        if (In.isBSS(S.SecNum) || S.Value > InSec.RawSize)
          continue;
        ArrayRef<uint8_t> InData = In.secData(S.SecNum);
        bool AnyDecidable = false, AnyMatch = false;
        auto MIt = OutByNameMulti.find(S.Name);
        if (MIt == OutByNameMulti.end())
          return fail(Err, "verify: defined input symbol '" + S.Name +
                               "' missing from merged output");
        for (int Idx : MIt->second) {
          const RawCoffSym &Cand = Out.Syms[(unsigned)Idx];
          if (Cand.SecNum <= 0 || (unsigned)Cand.SecNum > Out.Secs.size())
            continue;
          const RawCoffSec &CSec = Out.Secs[Cand.SecNum - 1];
          if (CSec.Name != InSec.Name || Out.isBSS(Cand.SecNum) ||
              Cand.Value > CSec.RawSize)
            continue;
          ArrayRef<uint8_t> OutData = Out.secData(Cand.SecNum);
          uint64_t Avail = std::min<uint64_t>(InSec.RawSize - S.Value,
                                              CSec.RawSize - Cand.Value);
          uint64_t W = std::min<uint64_t>(16, Avail);
          if (W == 0 || S.Value + W > InData.size() ||
              Cand.Value + W > OutData.size())
            continue;
          AnyDecidable = true;
          if (memcmp(InData.data() + S.Value, OutData.data() + Cand.Value, W) ==
              0) {
            AnyMatch = true;
            break;
          }
        }
        if (AnyDecidable && !AnyMatch)
          return fail(Err,
                      "verify: duplicate-named symbol '" + S.Name +
                          "' has no same-named merged symbol whose content "
                          "matches the input (offset collapsed or "
                          "mis-shifted)");
        continue;
      }
      const RawCoffSym &OutSym = Out.Syms[(unsigned)It->second];
      if (OutSym.SecNum <= 0 || (unsigned)OutSym.SecNum > Out.Secs.size())
        return fail(Err, "verify: symbol '" + S.Name +
                             "' was defined in input but is undefined in the "
                             "merged output");
      const RawCoffSec &OutSec = Out.Secs[OutSym.SecNum - 1];
      if (OutSec.Name != InSec.Name)
        return fail(Err, "verify: symbol '" + S.Name + "' landed in section '" +
                             OutSec.Name + "' but expected '" + InSec.Name +
                             "'");

      if (In.isBSS(S.SecNum) || Out.isBSS(OutSym.SecNum))
        continue;
      if (S.Value > InSec.RawSize)
        continue;
      if (OutSym.Value > OutSec.RawSize)
        return fail(Err, "verify: symbol '" + S.Name +
                             "' value is past the end of its merged section");
      ArrayRef<uint8_t> InData = In.secData(S.SecNum);
      ArrayRef<uint8_t> OutData = Out.secData(OutSym.SecNum);
      uint64_t Avail = std::min<uint64_t>(InSec.RawSize - S.Value,
                                          OutSec.RawSize - OutSym.Value);
      uint64_t W = std::min<uint64_t>(16, Avail);
      if (W == 0)
        continue;
      if (S.Value + W > InData.size() || OutSym.Value + W > OutData.size())
        continue;
      if (memcmp(InData.data() + S.Value, OutData.data() + OutSym.Value, W) !=
          0)
        return fail(Err,
                    "verify: symbol '" + S.Name +
                        "' content at its merged offset does not match the "
                        "input (offset collapsed or mis-shifted)");
    }

    // Weak external aux TagIndex consistency.  A COFF weak external names its
    // default definition by a symbol index stored in an aux record; the merge
    // concatenates symbol tables, so that index must be remapped.  A stale
    // index aliases the weak symbol onto an unrelated definition — invisible to
    // the content/offset anchors because aux records hold no section bytes. Re-
    // derive it by name: the merged weak external must name the same default
    // symbol the input did.  Duplicate-named weak externals are skipped as
    // ambiguous (their absence, if any, is caught by the symbol checks above).
    //
    // The one name difference that is not a mis-remap is a placeholder
    // default.  "There is no default definition" is encoded as an absolute
    // symbol at zero, and MergerCOFF recognises that encoding to collapse the
    // per-partition copies of a single extern_weak rule onto one record.  The
    // object writer names each partition's placeholder after whichever
    // external symbol that partition happened to list first
    // (WinCOFFWriter::setWeakDefaultNames), so the surviving record's
    // placeholder legitimately carries a different name than the collapsed
    // one's.  Compare those structurally; a stale index pointing at a real
    // definition still fails, since a real definition is not absolute-at-zero.
    auto namesPlaceholderDefault = [](const RawCoffSym *S) {
      return S && S->SecNum == IMAGE_SYM_ABSOLUTE && S->Value == 0;
    };
    for (const RawCoffSym &S : In.Syms) {
      if (S.Storage != IMAGE_SYM_CLASS_WEAK_EXTERNAL || S.WeakTag < 0)
        continue;
      const RawCoffSym *InDefSym = In.symByRaw((uint32_t)S.WeakTag);
      StringRef InDef = InDefSym ? InDefSym->Name : StringRef();
      if (InDef.empty() || S.Name.empty())
        continue;
      auto It = OutByName.find(S.Name);
      if (It == OutByName.end() || It->second < 0)
        continue;
      const RawCoffSym &OutW = Out.Syms[(unsigned)It->second];
      if (OutW.WeakTag < 0)
        return fail(Err, "verify: COFF weak external '" + S.Name +
                             "' lost its aux record in the merged output");
      const RawCoffSym *OutDefSym = Out.symByRaw((uint32_t)OutW.WeakTag);
      StringRef OutDef = OutDefSym ? OutDefSym->Name : StringRef();
      if (OutDef == InDef)
        continue;
      if (namesPlaceholderDefault(InDefSym) &&
          namesPlaceholderDefault(OutDefSym))
        continue;
      return fail(Err, "verify: COFF weak external '" + S.Name +
                           "' aux TagIndex names '" + OutDef +
                           "' but the input default was '" + InDef +
                           "' (weak alias TagIndex not remapped)");
    }

    // Same-input-section relative-distance invariant (BSS-safe; see SecShift).
    // COFF symbol values are section-relative, so two EXTERNAL/STATIC symbols
    // sharing one input section must keep an identical (merged value - input
    // value) and land in the same merged section — covering
    // IMAGE_SCN_CNT_UNINITIALIZED_DATA (.bss) symbols the content anchor skips.
    // COFF has no SHF_MERGE; COMDAT sections each merge alone, so their members
    // still shift uniformly.
    {
      DenseMap<unsigned, SecShift> Shift;
      for (const RawCoffSym &S : In.Syms) {
        if (S.Name.empty() || S.SecNum <= 0 ||
            (unsigned)S.SecNum > In.Secs.size())
          continue;
        if (S.Storage != IMAGE_SYM_CLASS_EXTERNAL &&
            S.Storage != IMAGE_SYM_CLASS_STATIC)
          continue;
        const RawCoffSec &InSec = In.Secs[S.SecNum - 1];
        if (S.Value > InSec.RawSize)
          continue;
        auto It = OutByName.find(S.Name);
        if (It == OutByName.end() || It->second < 0)
          continue;
        const RawCoffSym &OutSym = Out.Syms[(unsigned)It->second];
        if (OutSym.SecNum <= 0 || (unsigned)OutSym.SecNum > Out.Secs.size())
          continue;
        int64_t Delta = (int64_t)OutSym.Value - (int64_t)S.Value;
        auto [DIt, Inserted] =
            Shift.try_emplace((unsigned)S.SecNum,
                              SecShift{Delta, (unsigned)OutSym.SecNum, S.Name});
        if (Inserted)
          continue;
        if (DIt->second.OutSec != (unsigned)OutSym.SecNum)
          return fail(Err,
                      "verify: COFF symbols '" + DIt->second.Witness +
                          "' and '" + S.Name +
                          "' share one input section but landed in different "
                          "merged sections");
        if (DIt->second.Delta != Delta)
          return fail(Err,
                      "verify: COFF symbols '" + DIt->second.Witness +
                          "' and '" + S.Name +
                          "' share one input section but shifted by different "
                          "amounts (" +
                          Twine(DIt->second.Delta) + " vs " + Twine(Delta) +
                          ") — offset collapsed or mis-shifted");
      }
    }

    // Disjoint-range invariant (singleton- and BSS-safe; see OutSecRange).
    {
      DenseMap<unsigned, char> SeenInputSec; // input section number -> recorded
      for (const RawCoffSym &S : In.Syms) {
        if (S.Name.empty() || S.SecNum <= 0 ||
            (unsigned)S.SecNum > In.Secs.size())
          continue;
        if (S.Storage != IMAGE_SYM_CLASS_EXTERNAL &&
            S.Storage != IMAGE_SYM_CLASS_STATIC)
          continue;
        const RawCoffSec &InSec = In.Secs[S.SecNum - 1];
        if (InSec.RawSize == 0 || S.Value > InSec.RawSize)
          continue;
        if (SeenInputSec.count((unsigned)S.SecNum))
          continue;
        auto It = OutByName.find(S.Name);
        if (It == OutByName.end() || It->second < 0)
          continue;
        const RawCoffSym &OutSym = Out.Syms[(unsigned)It->second];
        if (OutSym.SecNum <= 0 || (unsigned)OutSym.SecNum > Out.Secs.size())
          continue;
        if (OutSym.Value < S.Value)
          return fail(Err, "verify: COFF symbol '" + S.Name +
                               "' merged value 0x" +
                               Twine::utohexstr(OutSym.Value) +
                               " is below its input section offset 0x" +
                               Twine::utohexstr(S.Value) +
                               " (offset collapsed or mis-shifted)");
        SeenInputSec[(unsigned)S.SecNum] = 1;
        SecRanges[(unsigned)OutSym.SecNum].push_back(
            {OutSym.Value - S.Value, InSec.RawSize, S.Name});
      }
    }

    // Symbol-anchored relocation check (mirrors the ELF path): every reloc site
    // sits inside some defined symbol; that symbol's already-verified merged
    // value predicts where the reloc must re-land, and an output reloc for the
    // same target/type must exist there.  Catches the relocation half of an
    // offset collapse on the Windows object path.
    AnchorMap Anchors;
    for (const RawCoffSym &S : In.Syms) {
      if (S.Name.empty() || S.SecNum <= 0 ||
          (unsigned)S.SecNum > In.Secs.size())
        continue;
      if (S.Storage != IMAGE_SYM_CLASS_EXTERNAL &&
          S.Storage != IMAGE_SYM_CLASS_STATIC)
        continue;
      auto It = OutByName.find(S.Name);
      if (It == OutByName.end() || It->second < 0)
        continue;
      Anchors[(unsigned)(S.SecNum - 1)].push_back({S.Value, S.Name});
    }
    sortAnchors(Anchors);
    for (const RawCoffRel &R : In.Rels) {
      if (R.SecIdx0 >= In.Secs.size())
        continue;
      StringRef SymN = In.symNameByRaw(R.SymRaw);
      if (SymN.empty())
        continue; // unnamed/section target — can't key it cleanly
      auto AIt = Anchors.find(R.SecIdx0);
      if (AIt == Anchors.end())
        continue;
      const Anchor *Best = findAnchor(AIt->second, R.VA);
      if (!Best)
        continue;
      auto OIt = OutByName.find(Best->second);
      if (OIt == OutByName.end() || OIt->second < 0)
        continue;
      uint64_t Expected =
          Out.Syms[(unsigned)OIt->second].Value + (R.VA - Best->first);
      StringRef MergedSec = In.Secs[R.SecIdx0].Name; // COFF has no rename
      auto MIt = OutRelocs.find(
          (MergedSec + "\x01" + SymN + "\x01" + Twine(R.Type)).str());
      if (MIt == OutRelocs.end() || !MIt->second.count(Expected))
        return fail(Err, "verify: COFF relocation against '" + SymN +
                             "' in section '" + MergedSec +
                             "' expected at merged offset 0x" +
                             Twine::utohexstr(Expected) +
                             " is missing (offset collapsed or mis-routed)");
    }
  }

  // Disjoint-range check (keys are 1-based COFF section numbers).
  if (!checkDisjointRanges(
          SecRanges,
          [&](unsigned I) {
            return (I >= 1 && I <= Out.Secs.size()) ? Out.Secs[I - 1].RawSize
                                                    : 0;
          },
          [&](unsigned I) {
            return (I >= 1 && I <= Out.Secs.size()) ? Out.Secs[I - 1].Name
                                                    : StringRef();
          },
          Err))
    return false;

  return true;
}

// ---------------------------------------------------------------------------
// Mach-O verifier.  Unlike ELF/COFF, the Mach-O merger rewrites bytes at some
// relocation sites in place (non-extern, non-pcrel section-relative refs), so
// the content anchor must *skip* any window overlapping an output relocation
// site.  Symbol n_value is segment-relative in the merged object; subtracting
// the merged section's addr recovers the section-relative offset to compare.
// ---------------------------------------------------------------------------

struct RawMachoSec {
  StringRef Seg, Sect;
  uint64_t Addr = 0;
  uint64_t Size = 0;
  uint32_t Offset = 0;
  uint32_t Flags = 0;
  uint32_t RelOff = 0;
  uint32_t NReloc = 0;
};
struct RawMachoSym {
  StringRef Name;
  uint8_t Type = 0;
  uint8_t Sect = 0;
  uint16_t Desc = 0;
  uint64_t Value = 0;
  // N_WEAK_DEF may be coalesced independently across inputs, so it cannot
  // serve as a fixed section-shift anchor (mirrors ELF
  // isCoalescibleDefinition).  N_WEAK_REF only marks weak *undef* references
  // and never reaches this defined-in-section path; keep the predicate to the
  // definition flag alone.  Content anchors still audit the survivor.
  bool isCoalescible() const {
    namespace MO = llvm::MachO;
    return (Desc & MO::N_WEAK_DEF) != 0;
  }
};
struct RawMachoRel {
  unsigned SecIdx0 = 0; // 0-based section the relocation applies to
  uint32_t Address = 0; // section-relative offset of the relocation site
  uint32_t SymNum = 0;  // symtab index when Extern (symtab carries no aux)
  uint8_t Type = 0;
  bool Extern = false;
  bool Pcrel = false;
};
struct RawMacho {
  ArrayRef<char> Buf;
  uint32_t CPUType = 0; // mach_header cputype, for the arch-consistency check
  SmallVector<RawMachoSec, 0> Secs;
  SmallVector<RawMachoSym, 0> Syms;
  SmallVector<RawMachoRel, 0> Rels;
  // LC_DYSYMTAB symbol partition (local | external-defined | undefined).
  // Captured independently so the verifier can audit that the ranges actually
  // describe the symbol table the merger emitted.
  uint32_t ILocal = 0, NLocal = 0, IExtdef = 0, NExtdef = 0, IUndef = 0,
           NUndef = 0;
  bool HasDysymtab = false;

  bool isZerofill(unsigned I0) const {
    namespace MO = llvm::MachO;
    if (I0 >= Secs.size())
      return false;
    uint32_t T = Secs[I0].Flags & MO::SECTION_TYPE;
    return T == MO::S_ZEROFILL || T == MO::S_GB_ZEROFILL ||
           T == MO::S_THREAD_LOCAL_ZEROFILL;
  }
  ArrayRef<uint8_t> secData(unsigned I0) const { // 0-based
    if (I0 >= Secs.size() || isZerofill(I0))
      return {};
    const RawMachoSec &S = Secs[I0];
    if (S.Offset > Buf.size() || S.Size > Buf.size() - S.Offset)
      return {};
    return ArrayRef<uint8_t>(
        reinterpret_cast<const uint8_t *>(Buf.data()) + S.Offset, S.Size);
  }
  // Map a segment-relative address to the (0-based) section containing it and
  // the offset within that section.  Returns false if no section covers it
  // (e.g. a one-past-end or out-of-segment pointer), so the caller skips
  // rather than guessing.
  bool addrToSec(uint64_t Addr, unsigned &SecOut, uint64_t &OffOut) const {
    for (unsigned i = 0; i < Secs.size(); ++i) {
      const RawMachoSec &S = Secs[i];
      if (S.Size != 0 && Addr >= S.Addr && Addr < S.Addr + S.Size) {
        SecOut = i;
        OffOut = Addr - S.Addr;
        return true;
      }
    }
    return false;
  }
  // Section-relative byte ranges that may have been rewritten in place.
  void relocSites(unsigned I0,
                  SmallVectorImpl<std::pair<uint64_t, uint64_t>> &R) const {
    namespace MO = llvm::MachO;
    if (I0 >= Secs.size())
      return;
    const RawMachoSec &S = Secs[I0];
    if (S.RelOff == 0 || S.NReloc == 0)
      return;
    if ((uint64_t)S.RelOff + (uint64_t)S.NReloc * sizeof(MO::relocation_info) >
        Buf.size())
      return;
    for (unsigned i = 0; i < S.NReloc; ++i) {
      const char *RI = Buf.data() + S.RelOff + i * sizeof(MO::relocation_info);
      uint32_t Addr, W;
      memcpy(&Addr, RI, 4);
      memcpy(&W, RI + 4, 4);
      if (Addr & MO::R_SCATTERED)
        continue; // not used on 64-bit targets
      uint64_t Len = 1ull << ((W >> 25) & 0x3);
      R.push_back({Addr, Len});
    }
  }
};

bool parseRawMachO(ArrayRef<char> Buf, RawMacho &Out) {
  namespace MO = llvm::MachO;
  Out.Buf = Buf;
  if (Buf.size() < sizeof(MO::mach_header_64))
    return false;
  MO::mach_header_64 MH = readPOD<MO::mach_header_64>(Buf, 0);
  if (MH.magic != MO::MH_MAGIC_64)
    return false;
  Out.CPUType = MH.cputype;

  auto cstr16 = [](const char *P) -> StringRef {
    return StringRef(P, strnlen(P, 16));
  };

  uint64_t Cmd = sizeof(MO::mach_header_64);
  for (unsigned c = 0; c < MH.ncmds; ++c) {
    if (Cmd + sizeof(MO::load_command) > Buf.size())
      return false;
    MO::load_command LC = readPOD<MO::load_command>(Buf, Cmd);
    if (LC.cmdsize == 0 || Cmd + LC.cmdsize > Buf.size())
      return false;
    if (LC.cmd == MO::LC_SEGMENT_64) {
      if (Cmd + sizeof(MO::segment_command_64) > Buf.size())
        return false;
      MO::segment_command_64 Seg = readPOD<MO::segment_command_64>(Buf, Cmd);
      uint64_t SP = Cmd + sizeof(MO::segment_command_64);
      for (unsigned i = 0; i < Seg.nsects; ++i) {
        uint64_t SO = SP + (uint64_t)i * sizeof(MO::section_64);
        if (SO + sizeof(MO::section_64) > Buf.size())
          return false;
        MO::section_64 S = readPOD<MO::section_64>(Buf, SO);
        RawMachoSec RS;
        // segname/sectname are char[16] (no alignment requirement): read them
        // straight from Buf so the StringRefs stay valid for Buf's lifetime.
        // S is a local copy that dies at the end of this iteration, so
        // cstr16(S.segname) would dangle (a use-after-scope the merge fuzzer
        // caught immediately under ASan).
        RS.Seg = cstr16(Buf.data() + SO + offsetof(MO::section_64, segname));
        RS.Sect = cstr16(Buf.data() + SO + offsetof(MO::section_64, sectname));
        RS.Addr = S.addr;
        RS.Size = S.size;
        RS.Offset = S.offset;
        RS.Flags = S.flags;
        RS.RelOff = S.reloff;
        RS.NReloc = S.nreloc;
        Out.Secs.push_back(RS);
      }
    } else if (LC.cmd == MO::LC_SYMTAB) {
      // Bounds-check the command before reading its fields: a malformed cmdsize
      // smaller than symtab_command could otherwise let the field reads run
      // past the buffer when this is the last load command.
      if (Cmd + sizeof(MO::symtab_command) > Buf.size())
        return false;
      MO::symtab_command SC = readPOD<MO::symtab_command>(Buf, Cmd);
      if ((uint64_t)SC.stroff + SC.strsize > Buf.size())
        return false;
      if ((uint64_t)SC.symoff + (uint64_t)SC.nsyms * sizeof(MO::nlist_64) >
          Buf.size())
        return false;
      const char *Str = Buf.data() + SC.stroff;
      for (unsigned i = 0; i < SC.nsyms; ++i) {
        MO::nlist_64 NL = readPOD<MO::nlist_64>(
            Buf, SC.symoff + (uint64_t)i * sizeof(MO::nlist_64));
        RawMachoSym PS;
        if (NL.n_strx < SC.strsize)
          PS.Name = StringRef(Str + NL.n_strx,
                              strnlen(Str + NL.n_strx, SC.strsize - NL.n_strx));
        PS.Type = NL.n_type;
        PS.Sect = NL.n_sect;
        PS.Desc = NL.n_desc;
        PS.Value = NL.n_value;
        Out.Syms.push_back(PS);
      }
    } else if (LC.cmd == MO::LC_DYSYMTAB) {
      if (Cmd + sizeof(MO::dysymtab_command) > Buf.size())
        return false;
      MO::dysymtab_command DC = readPOD<MO::dysymtab_command>(Buf, Cmd);
      Out.ILocal = DC.ilocalsym;
      Out.NLocal = DC.nlocalsym;
      Out.IExtdef = DC.iextdefsym;
      Out.NExtdef = DC.nextdefsym;
      Out.IUndef = DC.iundefsym;
      Out.NUndef = DC.nundefsym;
      Out.HasDysymtab = true;
    }
    Cmd += LC.cmdsize;
  }

  // Relocations per section.  Non-scattered relocation_info is 8 bytes:
  // r_address (i32) then a packed little-endian word:
  //   symbolnum:24, pcrel:1, length:2, extern:1, type:4.
  for (unsigned i = 0; i < Out.Secs.size(); ++i) {
    const RawMachoSec &S = Out.Secs[i];
    if (S.RelOff == 0 || S.NReloc == 0)
      continue;
    if ((uint64_t)S.RelOff + (uint64_t)S.NReloc * 8 > Buf.size())
      continue;
    for (unsigned r = 0; r < S.NReloc; ++r) {
      const char *P = Buf.data() + S.RelOff + (uint64_t)r * 8;
      uint32_t Addr, W;
      memcpy(&Addr, P, 4);
      memcpy(&W, P + 4, 4);
      if (Addr & MO::R_SCATTERED)
        continue; // scattered relocs unused on 64-bit
      RawMachoRel RE;
      RE.SecIdx0 = i;
      RE.Address = Addr;
      RE.SymNum = W & 0xFFFFFF;
      RE.Pcrel = ((W >> 24) & 1) != 0;
      RE.Extern = ((W >> 27) & 1) != 0;
      RE.Type = (uint8_t)((W >> 28) & 0xF);
      Out.Rels.push_back(RE);
    }
  }
  return true;
}

bool verifyMergeMachOImpl(ArrayRef<StringRef> Inputs, ArrayRef<char> Output,
                          const Options &, std::string *Err) {
  namespace MO = llvm::MachO;

  RawMacho Out;
  if (!parseRawMachO(Output, Out))
    return fail(Err, "verify: merged output is not a parseable Mach-O object");

  // ---- Structural integrity of the merged object itself ----
  // LC_DYSYMTAB must partition the symbol table into contiguous local |
  // external-defined | undefined ranges, and each symbol's actual class must
  // match the range it is declared in.  The merger sorts symbols into exactly
  // these three groups and writes the ranges; a bug there would silently
  // mislead every consumer about which symbols are exported vs. undefined,
  // without ever changing a byte the content anchor inspects.  Conformant
  // output from any linker satisfies this, so it never false-rejects.
  if (Out.HasDysymtab) {
    unsigned N = Out.Syms.size();
    if (Out.ILocal != 0 || (uint64_t)Out.ILocal + Out.NLocal != Out.IExtdef ||
        (uint64_t)Out.IExtdef + Out.NExtdef != Out.IUndef ||
        (uint64_t)Out.IUndef + Out.NUndef != N)
      return fail(Err, "verify: Mach-O LC_DYSYMTAB ranges do not contiguously "
                       "partition the symbol table (local/extdef/undef "
                       "boundaries corrupted)");
    for (unsigned i = 0; i < N; ++i) {
      const RawMachoSym &S = Out.Syms[i];
      bool IsExt = (S.Type & MO::N_EXT) != 0;
      bool IsUndef = (S.Type & MO::N_TYPE) == MO::N_UNDF;
      if (i < Out.NLocal) {
        if (IsExt)
          return fail(Err, "verify: Mach-O external symbol '" + S.Name +
                               "' sits inside the LC_DYSYMTAB local range");
      } else if (i < Out.IUndef) {
        if (!IsExt || IsUndef)
          return fail(Err, "verify: Mach-O symbol '" + S.Name +
                               "' in the external-defined range is not an "
                               "external definition");
      } else {
        if (!IsExt || !IsUndef)
          return fail(Err, "verify: Mach-O symbol '" + S.Name +
                               "' in the undefined range is not an undefined "
                               "external");
      }
    }
  }

  StringMap<int> OutByName;
  // Every output symbol index per name, so a duplicate-named symbol is still
  // content-checked rather than skipped (see the ELF path for the rationale).
  StringMap<SmallVector<int, 2>> OutByNameMulti;
  for (unsigned i = 0; i < Out.Syms.size(); ++i) {
    StringRef N = Out.Syms[i].Name;
    if (N.empty())
      continue;
    auto It = OutByName.find(N);
    if (It == OutByName.end())
      OutByName[N] = (int)i;
    else
      It->second = -1;
    OutByNameMulti[N].push_back((int)i);
  }

  // Mirror the merger's __DATA,__common -> __DATA,__bss rename.
  auto expectedSect = [](StringRef Seg, StringRef Sect) -> StringRef {
    if (Seg == "__DATA" && Sect == "__common")
      return "__bss";
    return Sect;
  };

  // Output relocations keyed by (segment, section, target symbol name, type) →
  // the section-relative addresses they appear at.  Only extern relocations
  // carry a nameable target (section-relative refs and the ARM64 ADDEND pseudo
  // reloc do not), so non-extern entries are skipped here.
  std::map<std::string, std::set<uint64_t>> OutRelocs;
  // Non-extern (section-relative) relocations go through a *separate* in-place
  // byte-fixup path in the merger (the riskiest offset arithmetic), so index
  // them too — keyed by (applied seg/sect, target seg/sect, type) — to anchor
  // their sites independently.  r_symbolnum is a 1-based section number for
  // these; ARM64_RELOC_ADDEND (type 10) instead stores an addend, never a real
  // section target on any target (x86-64 has no type 10), so it is excluded.
  std::map<std::string, std::set<uint64_t>> OutSecRelocs;
  for (const RawMachoRel &R : Out.Rels) {
    if (R.SecIdx0 >= Out.Secs.size())
      continue;
    if (R.Extern) {
      if (R.SymNum >= Out.Syms.size())
        continue;
      const RawMachoSec &Sec = Out.Secs[R.SecIdx0];
      StringRef SymN = Out.Syms[R.SymNum].Name;
      OutRelocs[(Sec.Seg + "\x01" + Sec.Sect + "\x01" + SymN + "\x01" +
                 Twine(R.Type))
                    .str()]
          .insert(R.Address);
      continue;
    }
    if (R.Type == MO::ARM64_RELOC_ADDEND || R.SymNum == 0 ||
        R.SymNum > Out.Secs.size())
      continue;
    const RawMachoSec &App = Out.Secs[R.SecIdx0];
    const RawMachoSec &Tgt = Out.Secs[R.SymNum - 1];
    OutSecRelocs[(App.Seg + "\x01" + App.Sect + "\x01" + Tgt.Seg + "\x01" +
                  Tgt.Sect + "\x01" + Twine(R.Type))
                     .str()]
        .insert(R.Address);
  }

  // Disjoint-range accumulator (see OutSecRange).  n_value is segment-relative,
  // so each section-relative offset is n_value - section addr; the input
  // section's [base, size) within its merged section follows as in ELF/COFF.
  DenseMap<unsigned, SmallVector<OutSecRange, 0>> SecRanges;
  // Weak definitions may resolve to a different input's copy.  Accumulate
  // across all inputs (mirrors ELF CoalescibleDecidable/Matched): the survivor
  // must match at least one decidable input body, not every input body.
  std::set<std::string> CoalescibleDecidable;
  std::set<std::string> CoalescibleMatched;

  for (unsigned p = 0; p < Inputs.size(); ++p) {
    if (Inputs[p].empty())
      continue;
    RawMacho In;
    if (!parseRawMachO(ArrayRef<char>(Inputs[p].data(), Inputs[p].size()), In))
      return fail(Err, "verify: input partition " + Twine(p) +
                           " is not a parseable Mach-O object");

    // Independent architecture-consistency leg (mirrors the merger's cputype
    // refuse); a conformant -r output carries the inputs' cputype, so this
    // never false-rejects.
    if (In.CPUType != 0 && Out.CPUType != In.CPUType)
      return fail(Err, "verify: merged output cputype " + Twine(Out.CPUType) +
                           " does not match input partition " + Twine(p) +
                           " cputype " + Twine(In.CPUType) +
                           " (mixed architectures or wrong output header)");

    for (const RawMachoSym &S : In.Syms) {
      if (S.Name.empty())
        continue;
      if (S.Type & MO::N_STAB)
        continue; // debug symbol
      if ((S.Type & MO::N_TYPE) != MO::N_SECT || S.Sect == 0)
        continue; // not defined-in-section
      unsigned InIdx = S.Sect - 1;
      if (InIdx >= In.Secs.size())
        continue;
      const RawMachoSec &InSec = In.Secs[InIdx];
      if (isRebasedMachOSection(InSec.Sect))
        continue;
      if (S.Value < InSec.Addr)
        continue; // malformed
      uint64_t InRel = S.Value - InSec.Addr;

      auto It = OutByName.find(S.Name);
      if (It == OutByName.end())
        return fail(Err, "verify: defined input symbol '" + S.Name +
                             "' missing from merged output");
      if (S.isCoalescible()) {
        // A weak definition may legitimately resolve to a different input's
        // copy, so it is not a per-input offset/content anchor.  Still require
        // the surviving output body to match at least one input definition
        // whenever bytes are decidable (and not rewritten by a reloc).
        bool HasDefinition = false;
        auto MIt = OutByNameMulti.find(S.Name);
        if (MIt != OutByNameMulti.end())
          for (int Idx : MIt->second) {
            const RawMachoSym &Cand = Out.Syms[(unsigned)Idx];
            if ((Cand.Type & MO::N_TYPE) != MO::N_SECT || Cand.Sect == 0 ||
                Cand.Sect > Out.Secs.size())
              continue;
            HasDefinition = true;
            if (In.isZerofill(InIdx) || Out.isZerofill(Cand.Sect - 1) ||
                InRel > InSec.Size || Cand.Value < Out.Secs[Cand.Sect - 1].Addr)
              continue;
            const RawMachoSec &CandSec = Out.Secs[Cand.Sect - 1];
            StringRef WantSect = expectedSect(InSec.Seg, InSec.Sect);
            if (CandSec.Seg != InSec.Seg || CandSec.Sect != WantSect)
              continue;
            uint64_t COff = Cand.Value - CandSec.Addr;
            if (COff > CandSec.Size)
              continue;
            ArrayRef<uint8_t> InData = In.secData(InIdx);
            ArrayRef<uint8_t> OutData = Out.secData(Cand.Sect - 1);
            uint64_t Avail =
                std::min<uint64_t>(InSec.Size - InRel, CandSec.Size - COff);
            uint64_t W = std::min<uint64_t>(16, Avail);
            if (W == 0 || InRel + W > InData.size() ||
                COff + W > OutData.size())
              continue;
            SmallVector<std::pair<uint64_t, uint64_t>, 8> Sites;
            Out.relocSites(Cand.Sect - 1, Sites);
            bool Overlaps = false;
            for (auto &Site : Sites)
              if (Site.first < COff + W && COff < Site.first + Site.second) {
                Overlaps = true;
                break;
              }
            if (Overlaps)
              continue;
            CoalescibleDecidable.insert(S.Name.str());
            if (memcmp(InData.data() + InRel, OutData.data() + COff, W) == 0)
              CoalescibleMatched.insert(S.Name.str());
          }
        if (!HasDefinition)
          return fail(Err, "verify: coalescible input symbol '" + S.Name +
                               "' has no definition in merged output");
        continue;
      }
      if (It->second < 0) {
        // Ambiguous name: require at least one same-named output symbol that
        // lands in the expected (segment,section) with a byte-matching window,
        // skipping windows that overlap an output relocation site (rewritten in
        // place) and any undecidable (zerofill / off-disk) candidate.
        if (In.isZerofill(InIdx) || InRel > InSec.Size)
          continue;
        ArrayRef<uint8_t> InData = In.secData(InIdx);
        StringRef WantSect = expectedSect(InSec.Seg, InSec.Sect);
        bool AnyDecidable = false, AnyMatch = false, AnySkippedForReloc = false;
        auto MIt = OutByNameMulti.find(S.Name);
        if (MIt == OutByNameMulti.end())
          return fail(Err, "verify: defined input symbol '" + S.Name +
                               "' missing from merged output");
        for (int Idx : MIt->second) {
          const RawMachoSym &Cand = Out.Syms[(unsigned)Idx];
          if ((Cand.Type & MO::N_TYPE) != MO::N_SECT || Cand.Sect == 0 ||
              Cand.Sect > Out.Secs.size())
            continue;
          const RawMachoSec &CSec = Out.Secs[Cand.Sect - 1];
          if (CSec.Seg != InSec.Seg || CSec.Sect != WantSect ||
              Out.isZerofill(Cand.Sect - 1) || Cand.Value < CSec.Addr)
            continue;
          uint64_t COff = Cand.Value - CSec.Addr;
          if (COff > CSec.Size)
            continue;
          ArrayRef<uint8_t> OutData = Out.secData(Cand.Sect - 1);
          uint64_t Avail =
              std::min<uint64_t>(InSec.Size - InRel, CSec.Size - COff);
          uint64_t W = std::min<uint64_t>(16, Avail);
          if (W == 0 || InRel + W > InData.size() || COff + W > OutData.size())
            continue;
          SmallVector<std::pair<uint64_t, uint64_t>, 8> Sites;
          Out.relocSites(Cand.Sect - 1, Sites);
          bool Overlaps = false;
          for (auto &Site : Sites)
            if (Site.first < COff + W && COff < Site.first + Site.second) {
              Overlaps = true;
              break;
            }
          if (Overlaps) {
            // This positionally-plausible home was rewritten in place, so its
            // bytes legitimately differ from the input — undecidable from this
            // candidate.  Record it: when *no* candidate matched but a
            // plausible one was reloc-skipped, the real home may be exactly
            // that skipped copy, so the whole symbol is undecidable rather than
            // a collapse. Without this, a duplicate local label (Mach-O 'ltmp0'
            // at every
            // __text start) whose true copy begins with a relocated instruction
            // would false-reject a *correct* merge — the merger would then
            // refuse it and spuriously fall back to serial codegen.
            AnySkippedForReloc = true;
            continue;
          }
          AnyDecidable = true;
          if (memcmp(InData.data() + InRel, OutData.data() + COff, W) == 0) {
            AnyMatch = true;
            break;
          }
        }
        if (AnyDecidable && !AnyMatch && !AnySkippedForReloc)
          return fail(Err,
                      "verify: duplicate-named symbol '" + S.Name +
                          "' has no same-named merged symbol whose content "
                          "matches the input (offset collapsed or "
                          "mis-shifted)");
        continue;
      }
      const RawMachoSym &OutSym = Out.Syms[(unsigned)It->second];
      if ((OutSym.Type & MO::N_TYPE) != MO::N_SECT || OutSym.Sect == 0 ||
          OutSym.Sect > Out.Secs.size())
        return fail(Err, "verify: symbol '" + S.Name +
                             "' was defined in input but is undefined in the "
                             "merged output");
      const RawMachoSec &OutSec = Out.Secs[OutSym.Sect - 1];

      StringRef WantSect = expectedSect(InSec.Seg, InSec.Sect);
      if (OutSec.Seg != InSec.Seg || OutSec.Sect != WantSect)
        return fail(Err, "verify: symbol '" + S.Name + "' landed in (" +
                             OutSec.Seg + "," + OutSec.Sect +
                             ") but expected (" + InSec.Seg + "," + WantSect +
                             ")");

      if (In.isZerofill(InIdx) || Out.isZerofill(OutSym.Sect - 1))
        continue;
      if (OutSym.Value < OutSec.Addr)
        return fail(Err, "verify: symbol '" + S.Name +
                             "' value is below its merged section base");
      uint64_t OutRel = OutSym.Value - OutSec.Addr;
      if (InRel > InSec.Size)
        continue;
      if (OutRel > OutSec.Size)
        return fail(Err, "verify: symbol '" + S.Name +
                             "' value is past the end of its merged section");

      uint64_t Avail =
          std::min<uint64_t>(InSec.Size - InRel, OutSec.Size - OutRel);
      uint64_t W = std::min<uint64_t>(16, Avail);
      if (W == 0)
        continue;
      ArrayRef<uint8_t> InData = In.secData(InIdx);
      ArrayRef<uint8_t> OutData = Out.secData(OutSym.Sect - 1);
      if (InRel + W > InData.size() || OutRel + W > OutData.size())
        continue;
      // Skip windows overlapping a relocation site (those bytes are rewritten
      // in place by the merger and legitimately differ from the input).
      SmallVector<std::pair<uint64_t, uint64_t>, 8> Sites;
      Out.relocSites(OutSym.Sect - 1, Sites);
      bool Overlaps = false;
      for (auto &Site : Sites)
        if (Site.first < OutRel + W && OutRel < Site.first + Site.second) {
          Overlaps = true;
          break;
        }
      if (Overlaps)
        continue;
      if (memcmp(InData.data() + InRel, OutData.data() + OutRel, W) != 0)
        return fail(Err,
                    "verify: symbol '" + S.Name +
                        "' content at its merged offset does not match the "
                        "input (offset collapsed or mis-shifted)");
    }

    // Same-input-section relative-distance invariant (zerofill-safe; see
    // SecShift).  n_value is segment-relative; subtract each section's base to
    // get section-relative offsets.  Two symbols sharing one input section must
    // keep an identical (merged offset - input offset) and land in the same
    // merged section — covering S_ZEROFILL (__bss) symbols the content anchor
    // skips.  Literal sections (cstring / N-byte / pointer literals) are
    // excluded: a real -r linker may coalesce their members non-uniformly.
    // Weak definitions are excluded for the same reason (independently
    // coalesced survivors need not preserve a single section shift).
    {
      DenseMap<unsigned, SecShift> Shift;
      for (const RawMachoSym &S : In.Syms) {
        if (S.Name.empty() || (S.Type & MO::N_STAB))
          continue;
        if ((S.Type & MO::N_TYPE) != MO::N_SECT || S.Sect == 0 ||
            S.Sect > In.Secs.size())
          continue;
        if (S.isCoalescible())
          continue;
        const RawMachoSec &InSec = In.Secs[S.Sect - 1];
        uint32_t InT = InSec.Flags & MO::SECTION_TYPE;
        if (InT == MO::S_CSTRING_LITERALS || InT == MO::S_4BYTE_LITERALS ||
            InT == MO::S_8BYTE_LITERALS || InT == MO::S_16BYTE_LITERALS ||
            InT == MO::S_LITERAL_POINTERS)
          continue;
        if (S.Value < InSec.Addr)
          continue;
        uint64_t InRel = S.Value - InSec.Addr;
        if (InRel > InSec.Size)
          continue;
        auto It = OutByName.find(S.Name);
        if (It == OutByName.end() || It->second < 0)
          continue;
        const RawMachoSym &OutSym = Out.Syms[(unsigned)It->second];
        if ((OutSym.Type & MO::N_TYPE) != MO::N_SECT || OutSym.Sect == 0 ||
            OutSym.Sect > Out.Secs.size())
          continue;
        const RawMachoSec &OutSec = Out.Secs[OutSym.Sect - 1];
        if (OutSym.Value < OutSec.Addr)
          continue;
        uint64_t OutRel = OutSym.Value - OutSec.Addr;
        int64_t Delta = (int64_t)OutRel - (int64_t)InRel;
        auto [DIt, Inserted] = Shift.try_emplace(
            (unsigned)S.Sect, SecShift{Delta, (unsigned)OutSym.Sect, S.Name});
        if (Inserted)
          continue;
        if (DIt->second.OutSec != (unsigned)OutSym.Sect)
          return fail(Err,
                      "verify: Mach-O symbols '" + DIt->second.Witness +
                          "' and '" + S.Name +
                          "' share one input section but landed in different "
                          "merged sections");
        if (DIt->second.Delta != Delta)
          return fail(Err,
                      "verify: Mach-O symbols '" + DIt->second.Witness +
                          "' and '" + S.Name +
                          "' share one input section but shifted by different "
                          "amounts (" +
                          Twine(DIt->second.Delta) + " vs " + Twine(Delta) +
                          ") — offset collapsed or mis-shifted");
      }
    }

    // Disjoint-range invariant (singleton- and zerofill-safe; see OutSecRange).
    // Literal sections and weak definitions are excluded for the same reason as
    // the relative-distance invariant above.
    {
      DenseMap<unsigned, char> SeenInputSec; // input section number -> recorded
      for (const RawMachoSym &S : In.Syms) {
        if (S.Name.empty() || (S.Type & MO::N_STAB))
          continue;
        if ((S.Type & MO::N_TYPE) != MO::N_SECT || S.Sect == 0 ||
            S.Sect > In.Secs.size())
          continue;
        if (S.isCoalescible())
          continue;
        const RawMachoSec &InSec = In.Secs[S.Sect - 1];
        uint32_t InT = InSec.Flags & MO::SECTION_TYPE;
        if (InT == MO::S_CSTRING_LITERALS || InT == MO::S_4BYTE_LITERALS ||
            InT == MO::S_8BYTE_LITERALS || InT == MO::S_16BYTE_LITERALS ||
            InT == MO::S_LITERAL_POINTERS)
          continue;
        if (InSec.Size == 0 || S.Value < InSec.Addr)
          continue;
        uint64_t InRel = S.Value - InSec.Addr;
        if (InRel > InSec.Size)
          continue;
        if (SeenInputSec.count((unsigned)S.Sect))
          continue;
        auto It = OutByName.find(S.Name);
        if (It == OutByName.end() || It->second < 0)
          continue;
        const RawMachoSym &OutSym = Out.Syms[(unsigned)It->second];
        if ((OutSym.Type & MO::N_TYPE) != MO::N_SECT || OutSym.Sect == 0 ||
            OutSym.Sect > Out.Secs.size())
          continue;
        const RawMachoSec &OutSec = Out.Secs[OutSym.Sect - 1];
        if (OutSym.Value < OutSec.Addr)
          continue;
        uint64_t OutRel = OutSym.Value - OutSec.Addr;
        if (OutRel < InRel)
          return fail(Err, "verify: Mach-O symbol '" + S.Name +
                               "' merged section-relative offset 0x" +
                               Twine::utohexstr(OutRel) +
                               " is below its input offset 0x" +
                               Twine::utohexstr(InRel) +
                               " (offset collapsed or mis-shifted)");
        SeenInputSec[(unsigned)S.Sect] = 1;
        SecRanges[(unsigned)OutSym.Sect].push_back(
            {OutRel - InRel, InSec.Size, S.Name});
      }
    }

    // Symbol-anchored relocation check (extern relocations only).  n_value is
    // segment-relative, so subtract the section base to work in section-
    // relative offsets, matching the merged reloc r_address.
    AnchorMap Anchors;
    for (const RawMachoSym &S : In.Syms) {
      if (S.Name.empty() || (S.Type & MO::N_STAB))
        continue;
      if ((S.Type & MO::N_TYPE) != MO::N_SECT || S.Sect == 0 ||
          S.Sect > In.Secs.size())
        continue;
      const RawMachoSec &Sec = In.Secs[S.Sect - 1];
      if (S.Value < Sec.Addr)
        continue;
      auto It = OutByName.find(S.Name);
      if (It == OutByName.end() || It->second < 0)
        continue;
      Anchors[(unsigned)(S.Sect - 1)].push_back({S.Value - Sec.Addr, S.Name});
    }
    sortAnchors(Anchors);
    for (const RawMachoRel &R : In.Rels) {
      if (!R.Extern || R.SecIdx0 >= In.Secs.size() ||
          R.SymNum >= In.Syms.size())
        continue;
      StringRef SymN = In.Syms[R.SymNum].Name;
      if (SymN.empty())
        continue;
      auto AIt = Anchors.find(R.SecIdx0);
      if (AIt == Anchors.end())
        continue;
      const Anchor *Best = findAnchor(AIt->second, R.Address);
      if (!Best)
        continue;
      auto OIt = OutByName.find(Best->second);
      if (OIt == OutByName.end() || OIt->second < 0)
        continue;
      const RawMachoSym &OutAnchor = Out.Syms[(unsigned)OIt->second];
      if (OutAnchor.Sect == 0 || OutAnchor.Sect > Out.Secs.size())
        continue;
      const RawMachoSec &OutAnchorSec = Out.Secs[OutAnchor.Sect - 1];
      if (OutAnchor.Value < OutAnchorSec.Addr)
        continue;
      uint64_t Expected =
          (OutAnchor.Value - OutAnchorSec.Addr) + (R.Address - Best->first);
      const RawMachoSec &InSec = In.Secs[R.SecIdx0];
      StringRef WantSect = expectedSect(InSec.Seg, InSec.Sect);
      auto MIt = OutRelocs.find((InSec.Seg + "\x01" + WantSect + "\x01" + SymN +
                                 "\x01" + Twine(R.Type))
                                    .str());
      if (MIt == OutRelocs.end() || !MIt->second.count(Expected))
        return fail(Err, "verify: Mach-O relocation against '" + SymN +
                             "' in (" + InSec.Seg + "," + WantSect +
                             ") expected at merged offset 0x" +
                             Twine::utohexstr(Expected) +
                             " is missing (offset collapsed or mis-routed)");
    }

    // Section-relative (non-extern) relocation site check.  These targets are
    // 1-based section numbers, not symbols, and are rewritten in place by the
    // merger; anchor the site the same way and confirm an output non-extern
    // reloc into the same merged target section re-lands at the predicted spot.
    for (const RawMachoRel &R : In.Rels) {
      if (R.Extern || R.SecIdx0 >= In.Secs.size())
        continue;
      if (R.Type == MO::ARM64_RELOC_ADDEND || R.SymNum == 0 ||
          R.SymNum > In.Secs.size())
        continue;
      auto AIt = Anchors.find(R.SecIdx0);
      if (AIt == Anchors.end())
        continue;
      const Anchor *Best = findAnchor(AIt->second, R.Address);
      if (!Best)
        continue;
      auto OIt = OutByName.find(Best->second);
      if (OIt == OutByName.end() || OIt->second < 0)
        continue;
      const RawMachoSym &OutAnchor = Out.Syms[(unsigned)OIt->second];
      if (OutAnchor.Sect == 0 || OutAnchor.Sect > Out.Secs.size())
        continue;
      const RawMachoSec &OutAnchorSec = Out.Secs[OutAnchor.Sect - 1];
      if (OutAnchor.Value < OutAnchorSec.Addr)
        continue;
      uint64_t Expected =
          (OutAnchor.Value - OutAnchorSec.Addr) + (R.Address - Best->first);
      const RawMachoSec &InApp = In.Secs[R.SecIdx0];
      const RawMachoSec &InTgt = In.Secs[R.SymNum - 1];
      StringRef AppSect = expectedSect(InApp.Seg, InApp.Sect);
      StringRef TgtSect = expectedSect(InTgt.Seg, InTgt.Sect);
      auto MIt =
          OutSecRelocs.find((InApp.Seg + "\x01" + AppSect + "\x01" + InTgt.Seg +
                             "\x01" + TgtSect + "\x01" + Twine(R.Type))
                                .str());
      if (MIt == OutSecRelocs.end() || !MIt->second.count(Expected))
        return fail(Err, "verify: Mach-O section-relative relocation into (" +
                             InTgt.Seg + "," + TgtSect +
                             ") expected at merged offset 0x" +
                             Twine::utohexstr(Expected) +
                             " is missing (offset collapsed or mis-routed)");

      // Value check for absolute 64-bit pointers (UNSIGNED == 0 on both ARM64
      // and x86-64, length 8, non-pcrel): the merger rewrites these bytes *in
      // place* via a delta — its riskiest arithmetic, and the only thing the
      // site check above does NOT exercise.  Confirm the rewritten pointer
      // still references the same logical bytes the input pointer did: read the
      // raw pointer at the input and output sites, map both back to a section,
      // and content-compare the target windows.  All ambiguity (zerofill,
      // out-of-section pointer, reloc-overlapped window, bytes off disk) is
      // skipped rather than guessed, so this only ever fails on a real
      // divergence — and on the parallel-codegen path a false reject merely
      // falls back to serial codegen, never a wrong object.
      if (R.Type == 0 /* *_RELOC_UNSIGNED */ && !R.Pcrel &&
          R.SecIdx0 < In.Secs.size()) {
        ArrayRef<uint8_t> InApplData = In.secData(R.SecIdx0);
        ArrayRef<uint8_t> OutApplData = Out.secData(OutAnchor.Sect - 1);
        if (R.Address + 8 <= InApplData.size() &&
            Expected + 8 <= OutApplData.size()) {
          uint64_t Vin, Vout;
          memcpy(&Vin, InApplData.data() + R.Address, 8);
          memcpy(&Vout, OutApplData.data() + Expected, 8);
          unsigned ISec, OSec;
          uint64_t IOff, OOff;
          if (In.addrToSec(Vin, ISec, IOff) &&
              Out.addrToSec(Vout, OSec, OOff) && !In.isZerofill(ISec) &&
              !Out.isZerofill(OSec)) {
            // The rewritten pointer must land in the merged version of the
            // section the input pointer pointed into.
            StringRef WantSc =
                expectedSect(In.Secs[ISec].Seg, In.Secs[ISec].Sect);
            if (Out.Secs[OSec].Seg != In.Secs[ISec].Seg ||
                Out.Secs[OSec].Sect != WantSc)
              return fail(Err, "verify: Mach-O in-place pointer in (" +
                                   InApp.Seg + "," + AppSect + ") at 0x" +
                                   Twine::utohexstr(Expected) +
                                   " was rewritten to point into (" +
                                   Out.Secs[OSec].Seg + "," +
                                   Out.Secs[OSec].Sect + ") instead of (" +
                                   In.Secs[ISec].Seg + "," + WantSc +
                                   ") (in-place fixup mis-targeted)");
            ArrayRef<uint8_t> ITgt = In.secData(ISec);
            ArrayRef<uint8_t> OTgt = Out.secData(OSec);
            uint64_t Avail =
                std::min<uint64_t>(ITgt.size() - IOff, OTgt.size() - OOff);
            uint64_t W = std::min<uint64_t>(16, Avail);
            // Skip if the compared window overlaps a relocation site in the
            // output target (those bytes are themselves rewritten in place).
            SmallVector<std::pair<uint64_t, uint64_t>, 8> TgtSites;
            Out.relocSites(OSec, TgtSites);
            bool Overlaps = false;
            for (auto &S : TgtSites)
              if (S.first < OOff + W && OOff < S.first + S.second) {
                Overlaps = true;
                break;
              }
            if (!Overlaps && W > 0 && IOff + W <= ITgt.size() &&
                OOff + W <= OTgt.size() &&
                memcmp(ITgt.data() + IOff, OTgt.data() + OOff, W) != 0)
              return fail(Err, "verify: Mach-O in-place pointer at 0x" +
                                   Twine::utohexstr(Expected) + " in (" +
                                   InApp.Seg + "," + AppSect +
                                   ") references different content after merge "
                                   "(in-place fixup delta is wrong)");
          }
        }
      }
    }
  }

  // Disjoint-range check (keys are 1-based Mach-O section numbers).
  if (!checkDisjointRanges(
          SecRanges,
          [&](unsigned I) {
            return (I >= 1 && I <= Out.Secs.size()) ? Out.Secs[I - 1].Size : 0;
          },
          [&](unsigned I) {
            return (I >= 1 && I <= Out.Secs.size()) ? Out.Secs[I - 1].Sect
                                                    : StringRef();
          },
          Err))
    return false;

  for (const std::string &Name : CoalescibleDecidable)
    if (!CoalescibleMatched.count(Name))
      return fail(Err, "verify: coalescible symbol '" + Name +
                           "' has no surviving definition whose content "
                           "matches any input copy");

  return true;
}

bool validatePackageIndexEntry(const DWARFUnitIndex &Index,
                               const DWARFUnit &Unit, uint64_t Signature,
                               std::string &Reason) {
  if (!Index || Index.getVersion() != 5) {
    Reason = "missing or non-DWARF-5 package index";
    return false;
  }
  const DWARFUnitIndex::Entry *Entry = Index.getFromHash(Signature);
  if (!Entry) {
    Reason = (Twine("package index has no row for signature 0x") +
              Twine::utohexstr(Signature))
                 .str();
    return false;
  }

  const auto *Info = Entry->getContribution(DW_SECT_INFO);
  const uint64_t UnitSize = Unit.getNextUnitOffset() - Unit.getOffset();
  if (!Info || Info->getOffset() != Unit.getOffset() ||
      Info->getLength() != UnitSize) {
    Reason =
        (Twine("package index has the wrong .debug_info.dwo contribution for "
               "signature 0x") +
         Twine::utohexstr(Signature))
            .str();
    return false;
  }

  const auto *Abbrev = Entry->getContribution(DW_SECT_ABBREV);
  if (!Abbrev || Abbrev->getLength() == 0 ||
      Abbrev->getOffset() != Unit.getAbbrOffset()) {
    Reason =
        (Twine("package index has the wrong .debug_abbrev.dwo contribution for "
               "signature 0x") +
         Twine::utohexstr(Signature))
            .str();
    return false;
  }
  return true;
}

bool validateDIEValues(DWARFDie Root, std::string &Reason) {
  SmallVector<DWARFDie, 32> Pending;
  std::set<uint64_t> Visited;
  Pending.push_back(Root);
  while (!Pending.empty()) {
    const DWARFDie Die = Pending.pop_back_val();
    if (!Die || !Visited.insert(Die.getOffset()).second) {
      Reason = "malformed or cyclic DIE tree";
      return false;
    }

    for (const DWARFAttribute &Attribute : Die.attributes()) {
      const DWARFFormValue &Value = Attribute.Value;
      if (Value.isFormClass(DWARFFormValue::FC_String)) {
        Expected<const char *> String = Value.getAsCString();
        if (!String) {
          auto Message = llvm::toString(String.takeError());
          Reason.assign(Message.begin(), Message.end());
          Reason.insert(0, "cannot resolve a DIE string: ");
          return false;
        }
      }

      if (Value.isFormClass(DWARFFormValue::FC_Address) &&
          !Value.getAsAddress()) {
        Reason = "cannot resolve an indexed DIE address";
        return false;
      }

      if (Value.getAsRelativeReference() &&
          !Die.getAttributeValueAsReferencedDie(Value)) {
        Reason = "DIE reference points outside its indexed contribution";
        return false;
      }

      DWARFUnit *Unit = Die.getDwarfUnit();
      if (Value.getForm() == dwarf::DW_FORM_rnglistx &&
          !Unit->getRnglistOffset(Value.getRawUValue())) {
        Reason = "DW_FORM_rnglistx index is outside its package contribution";
        return false;
      }
      if (Value.getForm() == dwarf::DW_FORM_loclistx) {
        if (!Unit->getLoclistOffset(Value.getRawUValue())) {
          Reason = "DW_FORM_loclistx index is outside its package contribution";
          return false;
        }
        Expected<DWARFLocationExpressionsVector> Locations =
            Die.getLocations(Attribute.Attr);
        if (!Locations) {
          auto Message = llvm::toString(Locations.takeError());
          Reason.assign(Message.begin(), Message.end());
          Reason.insert(0, "cannot resolve DIE location list: ");
          return false;
        }
      }
    }

    if (Die.find({dwarf::DW_AT_ranges, dwarf::DW_AT_low_pc})) {
      Expected<DWARFAddressRangesVector> Ranges = Die.getAddressRanges();
      if (!Ranges) {
        auto Message = llvm::toString(Ranges.takeError());
        Reason.assign(Message.begin(), Message.end());
        Reason.insert(0, "cannot resolve DIE address ranges: ");
        return false;
      }
    }

    for (DWARFDie Child : Die.children())
      Pending.push_back(Child);
  }
  return true;
}

bool getDWOName(DWARFDie UnitDIE, std::string &Name, std::string &Reason) {
  std::optional<DWARFFormValue> Value =
      UnitDIE.find({dwarf::DW_AT_dwo_name, dwarf::DW_AT_GNU_dwo_name});
  if (!Value) {
    Reason = "split compile unit has no DWO name";
    return false;
  }
  Expected<const char *> String = Value->getAsCString();
  if (!String) {
    auto Message = llvm::toString(String.takeError());
    Reason.assign(Message.begin(), Message.end());
    Reason.insert(0, "cannot resolve split compile unit DWO name: ");
    return false;
  }
  if (!*String || **String == '\0') {
    Reason = "split compile unit has an empty DWO name";
    return false;
  }
  Name = *String;
  return true;
}

} // anonymous namespace

bool verifyAndroidKernelModuleImage(ArrayRef<char> Output, const Options &Opts,
                                    std::string *Err) {
  if (!Opts.androidKernelModule || !Opts.finalizeAndroidKernelModule)
    return fail(Err, "verify: final Android kernel module audit requires "
                     "Android module finalization semantics");
  return verifyMergeELFImpl(ArrayRef<StringRef>{}, Output, Opts, Err);
}

bool verifyMerge(ArrayRef<StringRef> Inputs, ArrayRef<char> Output, Format Fmt,
                 const Options &Opts, std::string *Err) {
  switch (Fmt) {
  case Format::ELF64LE:
    return verifyMergeELFImpl(Inputs, Output, Opts, Err);
  case Format::COFF:
    return verifyMergeCOFFImpl(Inputs, Output, Opts, Err);
  case Format::MachO64:
    return verifyMergeMachOImpl(Inputs, Output, Opts, Err);
  }
  return true;
}

bool verifyMerge(ArrayRef<SmallVector<char, 0>> Inputs, ArrayRef<char> Output,
                 Format Fmt, const Options &Opts, std::string *Err) {
  SmallVector<StringRef, 8> Views;
  Views.reserve(Inputs.size());
  for (const auto &B : Inputs)
    Views.push_back(StringRef(B.data(), B.size()));
  return verifyMerge(ArrayRef<StringRef>(Views), Output, Fmt, Opts, Err);
}

bool verifySplitDwarfPair(ArrayRef<char> Object, ArrayRef<char> SplitDwarf,
                          Format Fmt, std::string *Err) {
  if (Object.empty() || SplitDwarf.empty())
    return fail(Err, "split-DWARF pair contains an empty artifact");

  auto ParseObject =
      [&](ArrayRef<char> Bytes,
          StringRef Name) -> Expected<std::unique_ptr<object::ObjectFile>> {
    return object::ObjectFile::createObjectFile(
        MemoryBufferRef(StringRef(Bytes.data(), Bytes.size()), Name));
  };
  auto MainOrErr = ParseObject(Object, "parallel-main-object");
  if (!MainOrErr)
    return fail(Err, "cannot parse merged main object: " +
                         toString(MainOrErr.takeError()));
  auto DwoOrErr = ParseObject(SplitDwarf, "parallel-split-dwarf");
  if (!DwoOrErr)
    return fail(Err, "cannot parse merged split-DWARF package: " +
                         toString(DwoOrErr.takeError()));

  auto MatchesFormat = [&](const object::ObjectFile &Obj) {
    switch (Fmt) {
    case Format::ELF64LE:
      return Obj.isELF();
    case Format::COFF:
      return Obj.isCOFF();
    case Format::MachO64:
      return Obj.isMachO();
    }
    return false;
  };
  if (!MatchesFormat(**MainOrErr) || !MatchesFormat(**DwoOrErr))
    return fail(Err, "main and split-DWARF artifacts do not match the "
                     "requested object format");

  std::string MainDiagMsg;
  std::string DwoDiagMsg;
  auto MainHandler = [&](Error E) {
    if (MainDiagMsg.empty())
      MainDiagMsg = toString(std::move(E)).str();
    else
      consumeError(std::move(E));
  };
  auto DwoHandler = [&](Error E) {
    if (DwoDiagMsg.empty())
      DwoDiagMsg = toString(std::move(E)).str();
    else
      consumeError(std::move(E));
  };
  auto MainContext = DWARFContext::create(
      **MainOrErr, DWARFContext::ProcessDebugRelocations::Process, nullptr, "",
      MainHandler, MainHandler);
  auto DwoContext = DWARFContext::create(
      **DwoOrErr, DWARFContext::ProcessDebugRelocations::Process, nullptr, "",
      DwoHandler, DwoHandler);

  std::set<uint64_t> SkeletonIds;
  std::map<uint64_t, std::string> SkeletonNames;
  std::map<uint64_t, DWARFUnit *> SkeletonUnits;
  for (const std::unique_ptr<DWARFUnit> &Unit :
       MainContext->info_section_units()) {
    if (Unit->getVersion() != 5)
      return fail(Err, "merged main object contains a non-DWARF-5 unit");
    const DWARFDie UnitDIE = Unit->getUnitDIE(false);
    if (!UnitDIE)
      return fail(Err, "merged main object contains a malformed unit DIE");
    if (Unit->getUnitType() != dwarf::DW_UT_skeleton)
      continue;
    std::optional<uint64_t> Id = Unit->getDWOId();
    if (!Id)
      return fail(Err, "skeleton compile unit has no DWO ID");
    if (!SkeletonIds.insert(*Id).second)
      return fail(Err, "duplicate skeleton DWO ID 0x" + Twine::utohexstr(*Id));
    SkeletonUnits.emplace(*Id, Unit.get());
    std::string Reason;
    if (!validateDIEValues(UnitDIE, Reason))
      return fail(Err, "malformed skeleton DIE values: " + Reason);
    std::string DWOName;
    if (!getDWOName(UnitDIE, DWOName, Reason))
      return fail(Err, Reason);
    SkeletonNames.emplace(*Id, std::move(DWOName));
    auto Line = MainContext->getLineTableForUnit(Unit.get(), MainHandler);
    if (!Line) {
      consumeError(Line.takeError());
      return fail(Err, "skeleton compile unit has a malformed line table");
    }
    if (!*Line)
      return fail(Err, "skeleton compile unit has no line table");
  }

  const DWARFUnitIndex &CUIndex = DwoContext->getCUIndex();
  const DWARFUnitIndex &TUIndex = DwoContext->getTUIndex();
  std::set<uint64_t> SplitIds;
  std::set<uint64_t> TypeHashes;
  std::map<uint64_t, std::string> SplitNames;
  for (const std::unique_ptr<DWARFUnit> &Unit :
       DwoContext->dwo_info_section_units()) {
    if (Unit->getVersion() != 5)
      return fail(Err, "merged DWP contains a non-DWARF-5 unit");
    const DWARFDie UnitDIE = Unit->getUnitDIE(false);
    if (!UnitDIE)
      return fail(Err, "merged DWP contains a malformed unit DIE");
    std::string Reason;
    if (Unit->getUnitType() == dwarf::DW_UT_split_compile) {
      std::optional<uint64_t> Id = Unit->getDWOId();
      if (!Id)
        return fail(Err, "split compile unit has no DWO ID");
      if (!SplitIds.insert(*Id).second)
        return fail(Err,
                    "duplicate split-unit DWO ID 0x" + Twine::utohexstr(*Id));
      auto Skeleton = SkeletonUnits.find(*Id);
      if (Skeleton == SkeletonUnits.end())
        return fail(Err, "split unit has no matching skeleton DWO ID 0x" +
                             Twine::utohexstr(*Id));
      Unit->setSkeletonUnit(Skeleton->second);
      if (std::optional<uint64_t> AddrBase =
              Skeleton->second->getAddrOffsetSectionBase()) {
        Unit->setAddrOffsetSection(&MainContext->getDWARFObj().getAddrSection(),
                                   *AddrBase);
      }
      if (!validatePackageIndexEntry(CUIndex, *Unit, *Id, Reason))
        return fail(Err, Reason);
      if (!validateDIEValues(UnitDIE, Reason))
        return fail(Err, "malformed split-unit DIE values: " + Reason);
      std::string DWOName;
      if (!getDWOName(UnitDIE, DWOName, Reason))
        return fail(Err, Reason);
      SplitNames.emplace(*Id, std::move(DWOName));
      auto Line = DwoContext->getLineTableForUnit(Unit.get(), DwoHandler);
      if (!Line) {
        consumeError(Line.takeError());
        return fail(Err, "split compile unit has a malformed line table");
      }
      continue;
    }

    // DWARF 5 moved type units from .debug_types.dwo into .debug_info.dwo.
    // Treating every info-section unit as a split CU rejects the exact output
    // produced by -fdebug-types-section.
    if (Unit->getUnitType() != dwarf::DW_UT_split_type)
      return fail(Err, "merged DWP contains an unexpected unit type");
    const uint64_t TypeHash = cast<DWARFTypeUnit>(Unit.get())->getTypeHash();
    if (!TypeHashes.insert(TypeHash).second)
      return fail(Err, "duplicate split type signature 0x" +
                           Twine::utohexstr(TypeHash));
    if (!validatePackageIndexEntry(TUIndex, *Unit, TypeHash, Reason))
      return fail(Err, Reason);
    if (!validateDIEValues(UnitDIE, Reason))
      return fail(Err, "malformed type-unit DIE values: " + Reason);
  }

  // Keep accepting a producer that uses the legacy .debug_types.dwo section,
  // but index and validate it through the same TU signature set.
  for (const std::unique_ptr<DWARFUnit> &Unit :
       DwoContext->dwo_types_section_units()) {
    if (Unit->getVersion() != 5 ||
        Unit->getUnitType() != dwarf::DW_UT_split_type)
      return fail(Err, "merged DWP contains an unexpected type unit");
    const DWARFDie UnitDIE = Unit->getUnitDIE(false);
    if (!UnitDIE)
      return fail(Err, "merged DWP contains a malformed type-unit DIE");
    std::string Reason;
    const uint64_t TypeHash = cast<DWARFTypeUnit>(Unit.get())->getTypeHash();
    if (!TypeHashes.insert(TypeHash).second)
      return fail(Err, "duplicate split type signature 0x" +
                           Twine::utohexstr(TypeHash));
    if (!validatePackageIndexEntry(TUIndex, *Unit, TypeHash, Reason))
      return fail(Err, Reason);
    if (!validateDIEValues(UnitDIE, Reason))
      return fail(Err, "malformed type-unit DIE values: " + Reason);
  }

  auto IndexMatchesUnits = [&](const DWARFUnitIndex &Index,
                               const std::set<uint64_t> &Expected,
                               StringRef IndexName) {
    std::set<uint64_t> Indexed;
    for (const DWARFUnitIndex::Entry &Entry : Index.getRows())
      if (Entry.getContributions())
        Indexed.insert(Entry.getSignature());
    if (Indexed == Expected)
      return true;
    return fail(Err, IndexName + " contains missing or extraneous unit rows");
  };
  if (!IndexMatchesUnits(CUIndex, SplitIds, ".debug_cu_index") ||
      !IndexMatchesUnits(TUIndex, TypeHashes, ".debug_tu_index"))
    return false;

  // LLVM's COFF DWARF reloc applicator still warns on some well-formed
  // IMAGE_REL_AMD64_ADDR32 debug_line → debug_line_str fixups (serial
  // objects do too). Treat parser diagnostics as soft: they enrich later
  // failure messages, but the skeleton/split semantic checks below are the
  // authority on whether the pair is usable.
  if (SkeletonIds.empty() || SplitIds.empty()) {
    Twine Detail =
        (MainDiagMsg.empty() ? Twine() : Twine("; main: ") + MainDiagMsg) +
        (DwoDiagMsg.empty() ? Twine() : Twine("; dwo: ") + DwoDiagMsg);
    return fail(Err,
                "split-DWARF pair contains no matched compile units" + Detail);
  }
  if (SkeletonIds != SplitIds)
    return fail(Err, "skeleton and split-unit DWO ID sets differ");
  if (SkeletonNames != SplitNames)
    return fail(Err, "skeleton and split-unit DWO names differ");
  return true;
}

} // namespace neverc::merge
