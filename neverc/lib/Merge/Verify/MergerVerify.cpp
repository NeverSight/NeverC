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

#include "Common/MergerCommon.h"
#include "neverc/Merge/Merger.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/BinaryFormat/MachO.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <utility>

using namespace llvm;

namespace neverc::merge {

namespace {

// ---------------------------------------------------------------------------
// Private raw ELF64LE reader — intentionally separate from both the merger's
// ELFObjectFile-based parse and the test harness, so the verifier shares no
// code path with what it verifies.
// ---------------------------------------------------------------------------

struct RawSec {
  StringRef Name;
  uint32_t Type = 0;
  uint64_t Flags = 0;
  uint64_t Size = 0;
  uint64_t Offset = 0;
  uint32_t Link = 0;
  uint32_t Info = 0;
};

struct RawSym {
  StringRef Name;
  uint64_t Value = 0;
  uint16_t Shndx = 0;
  uint8_t Info = 0;
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
  uint16_t Machine = 0; // e_machine, for the independent arch-consistency check
  SmallVector<RawSec, 0> Secs;
  SmallVector<RawSym, 0> Syms;
  SmallVector<RawRela, 0> Relas;
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
    if (S.Offset > Buf.size() || S.Size > Buf.size() - S.Offset)
      return {};
    return ArrayRef<uint8_t>(
        reinterpret_cast<const uint8_t *>(Buf.data()) + S.Offset, S.Size);
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

// Read a trivially-copyable POD of type T from Buf at byte offset Off into a
// properly aligned local.  Callers must bounds-check [Off, Off+sizeof(T)) first.
//
// The verifier also audits hostile / externally-produced objects whose headers
// can sit at file offsets that are not aligned to the struct's required
// alignment (e.g. an nlist_64 at an odd LC_SYMTAB symoff).  Reading those with
// `reinterpret_cast<const T*>(Buf.data() + Off)` followed by member access is a
// misaligned load — undefined behavior the merge fuzzer trips under UBSan, and a
// real fault on stricter ISAs.  Going through memcpy is the same byte-wise
// discipline parseRawCOFF and the relocation readers already use; for the
// merger's own (always aligned) output it is identical to a direct read.
template <typename T> T readPOD(ArrayRef<char> Buf, uint64_t Off) {
  T V;
  memcpy(&V, Buf.data() + Off, sizeof(T));
  return V;
}

bool parseRawELF(ArrayRef<char> Buf, RawELF &Out) {
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
  Out.Machine = H.e_machine;

  uint64_t ShOff = H.e_shoff;
  unsigned ShNum = H.e_shnum;
  if (ShOff == 0 || ShNum == 0)
    return false;
  if (ShOff > Buf.size() ||
      (uint64_t)ShNum * sizeof(Shdr) > Buf.size() - ShOff)
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
  uint64_t ShStrBase = SH[H.e_shstrndx].sh_offset;
  uint64_t ShStrSize = SH[H.e_shstrndx].sh_size;

  Out.Secs.reserve(ShNum);
  for (unsigned I = 0; I < ShNum; ++I) {
    RawSec RS;
    RS.Name = cstrAt(Buf, ShStrBase, ShStrSize, SH[I].sh_name);
    RS.Type = SH[I].sh_type;
    RS.Flags = SH[I].sh_flags;
    RS.Size = SH[I].sh_size;
    RS.Offset = SH[I].sh_offset;
    RS.Link = SH[I].sh_link;
    RS.Info = SH[I].sh_info;
    Out.Secs.push_back(RS);
  }

  // First SYMTAB + its linked STRTAB.
  for (unsigned I = 0; I < ShNum; ++I) {
    if (SH[I].sh_type != SHT_SYMTAB)
      continue;
    unsigned StrIdx = SH[I].sh_link;
    if (StrIdx >= ShNum)
      break;
    uint64_t StrBase = SH[StrIdx].sh_offset;
    uint64_t StrSize = SH[StrIdx].sh_size;
    uint64_t SymOff = SH[I].sh_offset;
    uint64_t SymSize = SH[I].sh_size;
    if (SymOff > Buf.size() || SymSize > Buf.size() - SymOff)
      break;
    unsigned N = SymSize / sizeof(Sym);
    Out.SymtabInfo = SH[I].sh_info;
    Out.HasSymtab = true;
    Out.Syms.reserve(N);
    for (unsigned k = 0; k < N; ++k) {
      Sym Sy = readPOD<Sym>(Buf, SymOff + (uint64_t)k * sizeof(Sym));
      RawSym PS;
      PS.Name = cstrAt(Buf, StrBase, StrSize, Sy.st_name);
      PS.Value = Sy.st_value;
      PS.Shndx = Sy.st_shndx;
      PS.Info = Sy.st_info;
      Out.Syms.push_back(PS);
    }
    break;
  }

  // RELA relocations (the merger only emits RELA).
  for (unsigned I = 0; I < Out.Secs.size(); ++I) {
    if (Out.Secs[I].Type != SHT_RELA)
      continue;
    uint64_t Off = Out.Secs[I].Offset;
    uint64_t Sz = Out.Secs[I].Size;
    if (Off > Buf.size() || Sz > Buf.size() - Off)
      continue;
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
      Out.Relas.push_back(RE);
    }
  }
  return true;
}

// The merger's section-name canonicalization, via the single shared helper in
// Common/MergerCommon.h that ELF/MergerELF.cpp's Phase-1 renaming also calls — so the
// verifier predicts exactly the output section name the merger produced, with
// no second copy to drift out of sync.
StringRef mergedSectionName(const RawSec &Sec, const Options &Opts) {
  return detail::canonicalELFSectionName(Sec.Name, Sec.Flags, Opts.mergeSections,
                                         Opts.preservedSections);
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
  if (Opts.dropDebugInfo &&
      (S.Name.starts_with(".debug_") || S.Name == ".debug" ||
       S.Name.starts_with(".zdebug_")))
    return true;
  return false;
}

bool fail(std::string *Err, const Twine &Msg) {
  if (Err)
    *Err = Msg.str();
  return false;
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
    std::stable_sort(KV.second.begin(), KV.second.end(),
                     [](const Anchor &A, const Anchor &B) {
                       return A.first < B.first;
                     });
}

// Greatest-value anchor whose value <= Off, or nullptr if none.  Requires
// Sorted to be ascending by value (see sortAnchors).
const Anchor *findAnchor(const AnchorVec &Sorted, uint64_t Off) {
  auto It = std::upper_bound(
      Sorted.begin(), Sorted.end(), Off,
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
  int64_t Delta;      // merged offset - input offset for the first witness
  unsigned OutSec;    // merged section the first witness landed in
  StringRef Witness;  // its name, for diagnostics
};

// One input section's reconstructed placement inside a merged output section: it
// occupies the half-open byte range [Base, Base + Size).  Distinct input
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
bool checkDisjointRanges(DenseMap<unsigned, SmallVector<OutSecRange, 0>> &Ranges,
                         SizeFn SecSize, NameFn SecName, std::string *Err) {
  for (auto &KV : Ranges) {
    uint64_t Size = SecSize(KV.first);
    StringRef Name = SecName(KV.first);
    auto &R = KV.second;
    std::stable_sort(R.begin(), R.end(),
                     [](const OutSecRange &A, const OutSecRange &B) {
                       return A.Base < B.Base;
                     });
    for (unsigned i = 0; i < R.size(); ++i) {
      if (R[i].Base + R[i].Size > Size)
        return fail(Err, "verify: input section of symbol '" + R[i].Witness +
                             "' spans [0x" + Twine::utohexstr(R[i].Base) +
                             ",0x" + Twine::utohexstr(R[i].Base + R[i].Size) +
                             ") past the end (0x" + Twine::utohexstr(Size) +
                             ") of merged section '" + Name +
                             "' (offset collapsed or mis-shifted)");
      if (i + 1 < R.size() && R[i].Base + R[i].Size > R[i + 1].Base)
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

  RawELF Out;
  if (!parseRawELF(Output, Out))
    return fail(Err, "verify: merged output is not a parseable ELF64LE object");

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
      return fail(Err, "verify: relocation section '" + S.Name +
                           "' sh_link " + Twine(S.Link) +
                           " does not reference a symbol table");
    if (S.Info == 0 || S.Info >= Out.Secs.size())
      return fail(Err, "verify: relocation section '" + S.Name +
                           "' sh_info " + Twine(S.Info) +
                           " does not reference a valid target section");
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
  // a section sits at a *distinct* offset (a collapse aliases several onto one),
  // checked here; and (b) the merged reloc count equals the sum of the inputs'
  // (none dropped or duplicated), accumulated across inputs and checked after
  // the input loop.  Both hold for any conformant -r merge, so neither
  // false-rejects (a real linker's -r output included).
  std::map<std::string, std::pair<uint64_t, std::set<uint64_t>>> OutLinkOrderRel;
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
      return fail(Err, "verify: SHF_LINK_ORDER section '" + KV.first + "' has " +
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

  for (unsigned p = 0; p < Inputs.size(); ++p) {
    if (Inputs[p].empty())
      continue;
    RawELF In;
    if (!parseRawELF(ArrayRef<char>(Inputs[p].data(), Inputs[p].size()), In))
      return fail(Err, "verify: input partition " + Twine(p) +
                           " is not a parseable ELF64LE object");

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

    for (const RawSym &S : In.Syms) {
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

      auto It = OutByName.find(S.Name);
      if (It == OutByName.end())
        return fail(Err, "verify: defined input symbol '" + S.Name +
                             "' missing from merged output");
      if (isCoalescibleDefinition(S)) {
        // A weak/unique definition may legitimately resolve to a different
        // input's copy. It is therefore not an offset anchor for this input
        // section. Still require the surviving output body to match at least
        // one of the input definitions whenever bytes are available.
        bool HasDefinition = false;
        auto MIt = OutByNameMulti.find(S.Name);
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
              CoalescibleDecidable.insert(S.Name.str());
              if (memcmp(InData.data() + S.Value,
                         OutData.data() + Cand.Value, W) == 0)
                CoalescibleMatched.insert(S.Name.str());
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
        auto MIt = OutByNameMulti.find(S.Name);
        if (MIt == OutByNameMulti.end())
          return fail(Err, "verify: defined input symbol '" + S.Name +
                               "' missing from merged output");
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
          return fail(Err, "verify: duplicate-named symbol '" + S.Name +
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
        return fail(Err, "verify: symbol '" + S.Name +
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
      uint64_t Avail = std::min(InSec.Size - S.Value, OutSec.Size - OutSym.Value);
      uint64_t W = std::min<uint64_t>(16, Avail);
      if (W == 0)
        continue;
      if (S.Value + W > InData.size() || OutSym.Value + W > OutData.size())
        continue; // bytes not actually present on disk — skip rather than guess
      if (memcmp(InData.data() + S.Value, OutData.data() + OutSym.Value, W) !=
          0)
        return fail(Err, "verify: symbol '" + S.Name +
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
        if (S.Shndx == 0 || S.Shndx >= SHN_LORESERVE || S.Shndx >= In.Secs.size())
          continue;
        const RawSec &InSec = In.Secs[S.Shndx];
        if (isExcludedInputSection(InSec, Opts) || (InSec.Flags & SHF_MERGE))
          continue;
        if (S.Value > InSec.Size)
          continue;
        auto It = OutByName.find(S.Name);
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
          return fail(Err, "verify: symbols '" + DIt->second.Witness + "' and '" +
                               S.Name +
                               "' share one input section but landed in different "
                               "merged sections (section split or mis-routed)");
        if (DIt->second.Delta != Delta)
          return fail(Err, "verify: symbols '" + DIt->second.Witness + "' and '" +
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
        auto It = OutByName.find(S.Name);
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
    for (const RawSym &S : In.Syms) {
      if (S.Name.empty() || S.type() == STT_SECTION)
        continue;
      if (isCoalescibleDefinition(S))
        continue;
      if (S.Shndx == 0 || S.Shndx >= SHN_LORESERVE || S.Shndx >= In.Secs.size())
        continue;
      auto It = OutByName.find(S.Name);
      if (It == OutByName.end() || It->second < 0)
        continue;
      Anchors[S.Shndx].push_back({S.Value, S.Name});
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
      StringRef SymN = Tgt.Name;
      // A section-relative relocation (target is an STT_SECTION symbol, so it
      // has no name) is "section base + addend".  It is keyed by the target
      // section, defined-in-section only, so the merged section base is known.
      bool IsSecTarget = SymN.empty() && Tgt.type() == STT_SECTION &&
                         Tgt.Shndx != 0 && Tgt.Shndx < In.Secs.size() &&
                         !isExcludedInputSection(In.Secs[Tgt.Shndx], Opts);
      if (SymN.empty() && !IsSecTarget)
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
      auto MIt = OutRelocs.find(
          (MergedSec + "\x01" + SymN + "\x01" + Twine(R.Type)).str());
      if (MIt == OutRelocs.end() || !MIt->second.count(Expected))
        return fail(Err, "verify: relocation against '" + SymN +
                             "' in section '" + MergedSec +
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
      if (!MIt->second.at(Expected).count(R.Addend))
        return fail(Err, "verify: relocation against '" + SymN +
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
      return fail(Err, "verify: SHF_LINK_ORDER section '" + KV.first +
                           "' merged " + Twine(OutCnt) +
                           " relocations but inputs contributed " +
                           Twine(KV.second) +
                           " (relocations dropped or duplicated)");
  }

  // Disjoint-range check: distinct input sections folded into one merged
  // section must occupy non-overlapping ranges that fit inside it.
  if (!checkDisjointRanges(
          SecRanges,
          [&](unsigned I) { return I < Out.Secs.size() ? Out.Secs[I].Size : 0; },
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
  StringRef symNameByRaw(uint32_t RawIdx) const {
    // A relocation's SymbolTableIndex is attacker-controlled and can equal
    // DenseMap's reserved empty/tombstone keys, on which find() is undefined and
    // can return an uninitialized slot index -> out-of-bounds Syms[] read (the
    // merge fuzzer hit this as a BUS).  Treat reserved keys as absent.
    if (detail::isReservedDenseKey(RawIdx))
      return {};
    auto It = RawToSym.find(RawIdx);
    if (It == RawToSym.end())
      return {};
    return Syms[It->second].Name;
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
                         const Options &, std::string *Err) {
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
          return fail(Err, "verify: duplicate-named symbol '" + S.Name +
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
      if (memcmp(InData.data() + S.Value, OutData.data() + OutSym.Value, W) != 0)
        return fail(Err, "verify: symbol '" + S.Name +
                             "' content at its merged offset does not match the "
                             "input (offset collapsed or mis-shifted)");
    }

    // Weak external aux TagIndex consistency.  A COFF weak external names its
    // default definition by a symbol index stored in an aux record; the merge
    // concatenates symbol tables, so that index must be remapped.  A stale index
    // aliases the weak symbol onto an unrelated definition — invisible to the
    // content/offset anchors because aux records hold no section bytes.  Re-
    // derive it by name: the merged weak external must name the same default
    // symbol the input did.  Duplicate-named weak externals are skipped as
    // ambiguous (their absence, if any, is caught by the symbol checks above).
    for (const RawCoffSym &S : In.Syms) {
      if (S.Storage != IMAGE_SYM_CLASS_WEAK_EXTERNAL || S.WeakTag < 0)
        continue;
      StringRef InDef = In.symNameByRaw((uint32_t)S.WeakTag);
      if (InDef.empty() || S.Name.empty())
        continue;
      auto It = OutByName.find(S.Name);
      if (It == OutByName.end() || It->second < 0)
        continue;
      const RawCoffSym &OutW = Out.Syms[(unsigned)It->second];
      if (OutW.WeakTag < 0)
        return fail(Err, "verify: COFF weak external '" + S.Name +
                             "' lost its aux record in the merged output");
      StringRef OutDef = Out.symNameByRaw((uint32_t)OutW.WeakTag);
      if (OutDef != InDef)
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
        auto [DIt, Inserted] = Shift.try_emplace(
            (unsigned)S.SecNum, SecShift{Delta, (unsigned)OutSym.SecNum, S.Name});
        if (Inserted)
          continue;
        if (DIt->second.OutSec != (unsigned)OutSym.SecNum)
          return fail(Err, "verify: COFF symbols '" + DIt->second.Witness +
                               "' and '" + S.Name +
                               "' share one input section but landed in different "
                               "merged sections");
        if (DIt->second.Delta != Delta)
          return fail(Err, "verify: COFF symbols '" + DIt->second.Witness +
                               "' and '" + S.Name +
                               "' share one input section but shifted by different "
                               "amounts (" + Twine(DIt->second.Delta) + " vs " +
                               Twine(Delta) + ") — offset collapsed or mis-shifted");
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
            return (I >= 1 && I <= Out.Secs.size()) ? Out.Secs[I - 1].RawSize : 0;
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
  void relocSites(unsigned I0, SmallVectorImpl<std::pair<uint64_t, uint64_t>> &R)
      const {
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
      // smaller than symtab_command could otherwise let the field reads run past
      // the buffer when this is the last load command.
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
            // candidate.  Record it: when *no* candidate matched but a plausible
            // one was reloc-skipped, the real home may be exactly that skipped
            // copy, so the whole symbol is undecidable rather than a collapse.
            // Without this, a duplicate local label (Mach-O 'ltmp0' at every
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
          return fail(Err, "verify: duplicate-named symbol '" + S.Name +
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
                             OutSec.Seg + "," + OutSec.Sect + ") but expected (" +
                             InSec.Seg + "," + WantSect + ")");

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

      uint64_t Avail = std::min<uint64_t>(InSec.Size - InRel, OutSec.Size - OutRel);
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
        return fail(Err, "verify: symbol '" + S.Name +
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
          return fail(Err, "verify: Mach-O symbols '" + DIt->second.Witness +
                               "' and '" + S.Name +
                               "' share one input section but landed in different "
                               "merged sections");
        if (DIt->second.Delta != Delta)
          return fail(Err, "verify: Mach-O symbols '" + DIt->second.Witness +
                               "' and '" + S.Name +
                               "' share one input section but shifted by different "
                               "amounts (" + Twine(DIt->second.Delta) + " vs " +
                               Twine(Delta) + ") — offset collapsed or mis-shifted");
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
      auto MIt = OutSecRelocs.find((InApp.Seg + "\x01" + AppSect + "\x01" +
                                    InTgt.Seg + "\x01" + TgtSect + "\x01" +
                                    Twine(R.Type))
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
      if (R.Type == 0 /* *_RELOC_UNSIGNED */ && !R.Pcrel && R.SecIdx0 < In.Secs.size()) {
        ArrayRef<uint8_t> InApplData = In.secData(R.SecIdx0);
        ArrayRef<uint8_t> OutApplData = Out.secData(OutAnchor.Sect - 1);
        if (R.Address + 8 <= InApplData.size() &&
            Expected + 8 <= OutApplData.size()) {
          uint64_t Vin, Vout;
          memcpy(&Vin, InApplData.data() + R.Address, 8);
          memcpy(&Vout, OutApplData.data() + Expected, 8);
          unsigned ISec, OSec;
          uint64_t IOff, OOff;
          if (In.addrToSec(Vin, ISec, IOff) && Out.addrToSec(Vout, OSec, OOff) &&
              !In.isZerofill(ISec) && !Out.isZerofill(OSec)) {
            // The rewritten pointer must land in the merged version of the
            // section the input pointer pointed into.
            StringRef WantSc = expectedSect(In.Secs[ISec].Seg, In.Secs[ISec].Sect);
            if (Out.Secs[OSec].Seg != In.Secs[ISec].Seg ||
                Out.Secs[OSec].Sect != WantSc)
              return fail(Err,
                          "verify: Mach-O in-place pointer in (" + InApp.Seg +
                              "," + AppSect + ") at 0x" +
                              Twine::utohexstr(Expected) +
                              " was rewritten to point into (" +
                              Out.Secs[OSec].Seg + "," + Out.Secs[OSec].Sect +
                              ") instead of (" + In.Secs[ISec].Seg + "," +
                              WantSc + ") (in-place fixup mis-targeted)");
            ArrayRef<uint8_t> ITgt = In.secData(ISec);
            ArrayRef<uint8_t> OTgt = Out.secData(OSec);
            uint64_t Avail = std::min<uint64_t>(ITgt.size() - IOff,
                                                OTgt.size() - OOff);
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
              return fail(Err,
                          "verify: Mach-O in-place pointer at 0x" +
                              Twine::utohexstr(Expected) + " in (" + InApp.Seg +
                              "," + AppSect +
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

} // anonymous namespace

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

} // namespace neverc::merge
