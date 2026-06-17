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

#include "neverc/Merge/Merger.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/BinaryFormat/MachO.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>

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

struct RawRela {
  unsigned TargetSec = 0; // section the relocation applies to (sh_info)
  uint64_t Offset = 0;
  uint32_t Sym = 0;
  uint32_t Type = 0;
};

struct RawELF {
  ArrayRef<char> Buf;
  SmallVector<RawSec, 0> Secs;
  SmallVector<RawSym, 0> Syms;
  SmallVector<RawRela, 0> Relas;

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

bool parseRawELF(ArrayRef<char> Buf, RawELF &Out) {
  using namespace ELF;
  using Ehdr = Elf64_Ehdr;
  using Shdr = Elf64_Shdr;
  using Sym = Elf64_Sym;

  Out.Buf = Buf;
  if (Buf.size() < sizeof(Ehdr))
    return false;
  const auto *H = reinterpret_cast<const Ehdr *>(Buf.data());
  if (memcmp(H->e_ident, ElfMagic, 4) != 0)
    return false;
  if (H->e_ident[EI_CLASS] != ELFCLASS64 || H->e_ident[EI_DATA] != ELFDATA2LSB)
    return false;

  uint64_t ShOff = H->e_shoff;
  unsigned ShNum = H->e_shnum;
  if (ShOff == 0 || ShNum == 0)
    return false;
  if (ShOff > Buf.size() ||
      (uint64_t)ShNum * sizeof(Shdr) > Buf.size() - ShOff)
    return false;
  const auto *Secs = reinterpret_cast<const Shdr *>(Buf.data() + ShOff);

  if (H->e_shstrndx >= ShNum)
    return false;
  uint64_t ShStrBase = Secs[H->e_shstrndx].sh_offset;
  uint64_t ShStrSize = Secs[H->e_shstrndx].sh_size;

  Out.Secs.reserve(ShNum);
  for (unsigned I = 0; I < ShNum; ++I) {
    RawSec RS;
    RS.Name = cstrAt(Buf, ShStrBase, ShStrSize, Secs[I].sh_name);
    RS.Type = Secs[I].sh_type;
    RS.Flags = Secs[I].sh_flags;
    RS.Size = Secs[I].sh_size;
    RS.Offset = Secs[I].sh_offset;
    RS.Link = Secs[I].sh_link;
    RS.Info = Secs[I].sh_info;
    Out.Secs.push_back(RS);
  }

  // First SYMTAB + its linked STRTAB.
  for (unsigned I = 0; I < ShNum; ++I) {
    if (Secs[I].sh_type != SHT_SYMTAB)
      continue;
    unsigned StrIdx = Secs[I].sh_link;
    if (StrIdx >= ShNum)
      break;
    uint64_t StrBase = Secs[StrIdx].sh_offset;
    uint64_t StrSize = Secs[StrIdx].sh_size;
    uint64_t SymOff = Secs[I].sh_offset;
    uint64_t SymSize = Secs[I].sh_size;
    if (SymOff > Buf.size() || SymSize > Buf.size() - SymOff)
      break;
    unsigned N = SymSize / sizeof(Sym);
    const auto *Sy = reinterpret_cast<const Sym *>(Buf.data() + SymOff);
    Out.Syms.reserve(N);
    for (unsigned k = 0; k < N; ++k) {
      RawSym PS;
      PS.Name = cstrAt(Buf, StrBase, StrSize, Sy[k].st_name);
      PS.Value = Sy[k].st_value;
      PS.Shndx = Sy[k].st_shndx;
      PS.Info = Sy[k].st_info;
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
    const auto *R = reinterpret_cast<const Elf64_Rela *>(Buf.data() + Off);
    for (unsigned k = 0; k < N; ++k) {
      RawRela RE;
      RE.TargetSec = Out.Secs[I].Info;
      RE.Offset = R[k].r_offset;
      RE.Sym = (uint32_t)(R[k].r_info >> 32);
      RE.Type = (uint32_t)(R[k].r_info & 0xffffffffu);
      Out.Relas.push_back(RE);
    }
  }
  return true;
}

// Mirror of the merger's section-name canonicalization.  Kept in lock-step
// with MergerELF.cpp's Phase-1 renaming so the verifier predicts the same
// output section name a symbol's input section should land in.
StringRef mergedSectionName(StringRef SecName, const Options &Opts) {
  if (!Opts.mergeSections || SecName.empty())
    return SecName;
  for (StringRef Ps : Opts.preservedSections)
    if (SecName == Ps)
      return SecName;
  if (SecName.starts_with(".text."))
    return ".text";
  if (SecName.starts_with(".bss."))
    return ".bss";
  if (SecName.starts_with(".data."))
    return ".data";
  if (SecName.starts_with(".rodata."))
    return ".rodata";
  return SecName;
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

bool verifyMergeELFImpl(ArrayRef<StringRef> Inputs, ArrayRef<char> Output,
                        const Options &Opts, std::string *Err) {
  using namespace ELF;

  RawELF Out;
  if (!parseRawELF(Output, Out))
    return fail(Err, "verify: merged output is not a parseable ELF64LE object");

  // Index output symbols by name; only names that resolve to a *single*
  // output symbol can be anchored unambiguously.
  StringMap<int> OutByName; // name -> sym index, or -1 if duplicated
  for (unsigned i = 0; i < Out.Syms.size(); ++i) {
    StringRef N = Out.Syms[i].Name;
    if (N.empty())
      continue;
    auto It = OutByName.find(N);
    if (It == OutByName.end())
      OutByName[N] = (int)i;
    else
      It->second = -1;
  }

  // Index output relocations by (merged target section, target symbol name,
  // type) → the offsets they appear at, so each input relocation can be
  // confirmed to re-land at its independently predicted merged offset.
  std::map<std::string, std::set<uint64_t>> OutRelocs;
  for (const RawRela &R : Out.Relas) {
    if (R.TargetSec >= Out.Secs.size())
      continue;
    StringRef SecN = Out.Secs[R.TargetSec].Name;
    StringRef SymN =
        R.Sym < Out.Syms.size() ? Out.Syms[R.Sym].Name : StringRef();
    OutRelocs[(SecN + "\x01" + SymN + "\x01" + Twine(R.Type)).str()].insert(
        R.Offset);
  }

  for (unsigned p = 0; p < Inputs.size(); ++p) {
    if (Inputs[p].empty())
      continue;
    RawELF In;
    if (!parseRawELF(ArrayRef<char>(Inputs[p].data(), Inputs[p].size()), In))
      return fail(Err, "verify: input partition " + Twine(p) +
                           " is not a parseable ELF64LE object");

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
      if (It->second < 0)
        continue; // ambiguous (name not unique in output) — conservatively skip
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

      StringRef Expected = mergedSectionName(InSec.Name, Opts);
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

    // Symbol-anchored relocation check: every relocation site sits inside some
    // defined symbol; using that symbol's already-verified merged value we
    // independently predict where the relocation must re-land and confirm an
    // output relocation for the same target/type exists there.  This is the
    // relocation half of the offset-collapse bug (symbols above are the other).
    DenseMap<unsigned, SmallVector<std::pair<uint64_t, StringRef>, 0>> Anchors;
    for (const RawSym &S : In.Syms) {
      if (S.Name.empty() || S.type() == STT_SECTION)
        continue;
      if (S.Shndx == 0 || S.Shndx >= SHN_LORESERVE || S.Shndx >= In.Secs.size())
        continue;
      auto It = OutByName.find(S.Name);
      if (It == OutByName.end() || It->second < 0)
        continue;
      Anchors[S.Shndx].push_back({S.Value, S.Name});
    }
    for (const RawRela &R : In.Relas) {
      if (R.TargetSec == 0 || R.TargetSec >= In.Secs.size())
        continue;
      const RawSec &T = In.Secs[R.TargetSec];
      if (isExcludedInputSection(T, Opts))
        continue;
      StringRef SymN =
          R.Sym < In.Syms.size() ? In.Syms[R.Sym].Name : StringRef();
      if (SymN.empty())
        continue; // section-relative / unnamed target — can't key it cleanly
      auto AIt = Anchors.find(R.TargetSec);
      if (AIt == Anchors.end())
        continue;
      // Anchor = the defined symbol with the greatest value <= reloc offset
      // (the function/object the relocation site lives in).
      const std::pair<uint64_t, StringRef> *Best = nullptr;
      for (const auto &PR : AIt->second)
        if (PR.first <= R.Offset && (!Best || PR.first >= Best->first))
          Best = &PR;
      if (!Best)
        continue;
      auto OIt = OutByName.find(Best->second);
      if (OIt == OutByName.end() || OIt->second < 0)
        continue;
      uint64_t Expected =
          Out.Syms[(unsigned)OIt->second].Value + (R.Offset - Best->first);
      StringRef MergedSec = mergedSectionName(T.Name, Opts);
      auto MIt = OutRelocs.find(
          (MergedSec + "\x01" + SymN + "\x01" + Twine(R.Type)).str());
      if (MIt == OutRelocs.end() || !MIt->second.count(Expected))
        return fail(Err, "verify: relocation against '" + SymN +
                             "' in section '" + MergedSec +
                             "' expected at merged offset 0x" +
                             Twine::utohexstr(Expected) +
                             " is missing (offset collapsed or mis-routed)");
    }
  }
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
};
struct RawCoffRel {
  unsigned SecIdx0 = 0; // 0-based section the relocation applies to
  uint32_t VA = 0;      // section-relative offset of the relocation site
  uint32_t SymRaw = 0;  // raw symbol-table index of the target
  uint16_t Type = 0;
};
struct RawCOFF {
  ArrayRef<char> Buf;
  SmallVector<RawCoffSec, 0> Secs;
  SmallVector<RawCoffSym, 0> Syms; // aux records skipped (Raw keeps the slot)
  SmallVector<RawCoffRel, 0> Rels;

  // Relocations carry a *raw* symbol-table index (aux records included), but
  // Syms drops aux entries; map back via the recorded Raw slot.
  StringRef symNameByRaw(uint32_t RawIdx) const {
    for (const RawCoffSym &S : Syms)
      if (S.Raw == RawIdx)
        return S.Name;
    return {};
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
  for (unsigned i = 0; i < Out.Syms.size(); ++i) {
    StringRef N = Out.Syms[i].Name;
    if (N.empty())
      continue;
    auto It = OutByName.find(N);
    if (It == OutByName.end())
      OutByName[N] = (int)i;
    else
      It->second = -1;
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

  for (unsigned p = 0; p < Inputs.size(); ++p) {
    if (Inputs[p].empty())
      continue;
    RawCOFF In;
    if (!parseRawCOFF(ArrayRef<char>(Inputs[p].data(), Inputs[p].size()), In))
      return fail(Err, "verify: input partition " + Twine(p) +
                           " is not a parseable COFF object");

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
      if (It->second < 0)
        continue; // ambiguous (name not unique in output)
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

    // Symbol-anchored relocation check (mirrors the ELF path): every reloc site
    // sits inside some defined symbol; that symbol's already-verified merged
    // value predicts where the reloc must re-land, and an output reloc for the
    // same target/type must exist there.  Catches the relocation half of an
    // offset collapse on the Windows object path.
    DenseMap<unsigned, SmallVector<std::pair<uint64_t, StringRef>, 0>> Anchors;
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
    for (const RawCoffRel &R : In.Rels) {
      if (R.SecIdx0 >= In.Secs.size())
        continue;
      StringRef SymN = In.symNameByRaw(R.SymRaw);
      if (SymN.empty())
        continue; // unnamed/section target — can't key it cleanly
      auto AIt = Anchors.find(R.SecIdx0);
      if (AIt == Anchors.end())
        continue;
      const std::pair<uint64_t, StringRef> *Best = nullptr;
      for (const auto &PR : AIt->second)
        if (PR.first <= R.VA && (!Best || PR.first >= Best->first))
          Best = &PR;
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
  uint64_t Value = 0;
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
  SmallVector<RawMachoSec, 0> Secs;
  SmallVector<RawMachoSym, 0> Syms;
  SmallVector<RawMachoRel, 0> Rels;

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
      const auto *RI = reinterpret_cast<const MO::relocation_info *>(
          Buf.data() + S.RelOff + i * sizeof(MO::relocation_info));
      uint32_t Addr = (uint32_t)RI->r_address;
      if (Addr & MO::R_SCATTERED)
        continue; // not used on 64-bit targets
      uint32_t W;
      memcpy(&W, reinterpret_cast<const char *>(RI) + 4, 4);
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
  const auto *MH = reinterpret_cast<const MO::mach_header_64 *>(Buf.data());
  if (MH->magic != MO::MH_MAGIC_64)
    return false;

  auto cstr16 = [](const char *P) -> StringRef {
    return StringRef(P, strnlen(P, 16));
  };

  uint64_t Cmd = sizeof(MO::mach_header_64);
  for (unsigned c = 0; c < MH->ncmds; ++c) {
    if (Cmd + sizeof(MO::load_command) > Buf.size())
      return false;
    const auto *LC = reinterpret_cast<const MO::load_command *>(Buf.data() + Cmd);
    if (LC->cmdsize == 0 || Cmd + LC->cmdsize > Buf.size())
      return false;
    if (LC->cmd == MO::LC_SEGMENT_64) {
      if (Cmd + sizeof(MO::segment_command_64) > Buf.size())
        return false;
      const auto *Seg =
          reinterpret_cast<const MO::segment_command_64 *>(Buf.data() + Cmd);
      uint64_t SP = Cmd + sizeof(MO::segment_command_64);
      for (unsigned i = 0; i < Seg->nsects; ++i) {
        if (SP + (uint64_t)(i + 1) * sizeof(MO::section_64) > Buf.size())
          return false;
        const auto *S = reinterpret_cast<const MO::section_64 *>(
            Buf.data() + SP + i * sizeof(MO::section_64));
        RawMachoSec RS;
        RS.Seg = cstr16(S->segname);
        RS.Sect = cstr16(S->sectname);
        RS.Addr = S->addr;
        RS.Size = S->size;
        RS.Offset = S->offset;
        RS.Flags = S->flags;
        RS.RelOff = S->reloff;
        RS.NReloc = S->nreloc;
        Out.Secs.push_back(RS);
      }
    } else if (LC->cmd == MO::LC_SYMTAB) {
      const auto *SC =
          reinterpret_cast<const MO::symtab_command *>(Buf.data() + Cmd);
      if ((uint64_t)SC->stroff + SC->strsize > Buf.size())
        return false;
      if ((uint64_t)SC->symoff + (uint64_t)SC->nsyms * sizeof(MO::nlist_64) >
          Buf.size())
        return false;
      const char *Str = Buf.data() + SC->stroff;
      for (unsigned i = 0; i < SC->nsyms; ++i) {
        const auto *NL = reinterpret_cast<const MO::nlist_64 *>(
            Buf.data() + SC->symoff + i * sizeof(MO::nlist_64));
        RawMachoSym PS;
        if (NL->n_strx < SC->strsize)
          PS.Name = StringRef(Str + NL->n_strx,
                              strnlen(Str + NL->n_strx, SC->strsize - NL->n_strx));
        PS.Type = NL->n_type;
        PS.Sect = NL->n_sect;
        PS.Value = NL->n_value;
        Out.Syms.push_back(PS);
      }
    }
    Cmd += LC->cmdsize;
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

  StringMap<int> OutByName;
  for (unsigned i = 0; i < Out.Syms.size(); ++i) {
    StringRef N = Out.Syms[i].Name;
    if (N.empty())
      continue;
    auto It = OutByName.find(N);
    if (It == OutByName.end())
      OutByName[N] = (int)i;
    else
      It->second = -1;
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
  // reloc do not), so non-extern entries are skipped.
  std::map<std::string, std::set<uint64_t>> OutRelocs;
  for (const RawMachoRel &R : Out.Rels) {
    if (!R.Extern || R.SecIdx0 >= Out.Secs.size() || R.SymNum >= Out.Syms.size())
      continue;
    const RawMachoSec &Sec = Out.Secs[R.SecIdx0];
    StringRef SymN = Out.Syms[R.SymNum].Name;
    OutRelocs[(Sec.Seg + "\x01" + Sec.Sect + "\x01" + SymN + "\x01" +
               Twine(R.Type))
                  .str()]
        .insert(R.Address);
  }

  for (unsigned p = 0; p < Inputs.size(); ++p) {
    if (Inputs[p].empty())
      continue;
    RawMacho In;
    if (!parseRawMachO(ArrayRef<char>(Inputs[p].data(), Inputs[p].size()), In))
      return fail(Err, "verify: input partition " + Twine(p) +
                           " is not a parseable Mach-O object");

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
      if (It->second < 0)
        continue; // ambiguous
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

    // Symbol-anchored relocation check (extern relocations only).  n_value is
    // segment-relative, so subtract the section base to work in section-
    // relative offsets, matching the merged reloc r_address.
    DenseMap<unsigned, SmallVector<std::pair<uint64_t, StringRef>, 0>> Anchors;
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
      const std::pair<uint64_t, StringRef> *Best = nullptr;
      for (const auto &PR : AIt->second)
        if (PR.first <= R.Address && (!Best || PR.first >= Best->first))
          Best = &PR;
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
  }
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
