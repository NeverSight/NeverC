//===- MergeTests.cpp - Fuzzing & edge-case tests for object mergers -----===//
//
// Exercises the ELF, COFF, and MachO merger paths with adversarial and
// boundary inputs.  Each test feeds raw byte buffers through the merger
// and asserts it either produces valid output or returns false — never
// crashes.  The fuzz-style helpers use deterministic seeds so these run
// in CI without a fuzzer harness.
//
//===--------------------------------------------------------------------===//

#include "neverc/Merge/Merger.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/Support/raw_ostream.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using namespace llvm;
using namespace neverc::merge;

// ---------------------------------------------------------------------------
// Helpers: minimal valid object file builders
// ---------------------------------------------------------------------------

namespace {

/// Build a minimal valid ELF64LE relocatable object with:
///   - 1 .text section (SHT_PROGBITS, SHF_ALLOC|SHF_EXECINSTR)
///   - N defined symbols + M undefined symbols
///   - 1 relocation per undefined symbol (R_X86_64_64 → addend 0)
SmallVector<char, 0>
buildMinimalELF(ArrayRef<std::string> DefinedSyms,
                ArrayRef<std::string> UndefinedSyms,
                ArrayRef<uint8_t> TextContent = {0xcc},
                bool DefinedAsGlobal = false) {
  using namespace ELF;
  using Ehdr = Elf64_Ehdr;
  using Shdr = Elf64_Shdr;
  using Sym = Elf64_Sym;
  using Rela = Elf64_Rela;

  // String tables
  SmallVector<char, 256> ShStrTab, SymStrTab;
  ShStrTab.push_back('\0');
  SymStrTab.push_back('\0');

  auto addStr = [](SmallVector<char, 256> &Tab, StringRef S) -> uint32_t {
    uint32_t Off = Tab.size();
    Tab.append(S.begin(), S.end());
    Tab.push_back('\0');
    return Off;
  };

  uint32_t TextNameOff = addStr(ShStrTab, ".text");
  uint32_t SymTabNameOff = addStr(ShStrTab, ".symtab");
  uint32_t StrTabNameOff = addStr(ShStrTab, ".strtab");
  uint32_t ShStrTabNameOff = addStr(ShStrTab, ".shstrtab");
  uint32_t RelaNameOff = addStr(ShStrTab, ".rela.text");

  // Symbols: [0]=null, [1..N]=defined (LOCAL), [N+1..N+M]=undefined (GLOBAL)
  SmallVector<Sym, 16> Syms;
  Sym NullSym;
  memset(&NullSym, 0, sizeof(NullSym));
  Syms.push_back(NullSym);

  SmallVector<Sym, 8> DeferredGlobalDefs;
  for (const auto &Name : DefinedSyms) {
    Sym S;
    memset(&S, 0, sizeof(S));
    S.st_name = addStr(SymStrTab, Name);
    S.st_shndx = 1; // .text
    S.st_value = 0;
    S.st_size = TextContent.size();
    if (DefinedAsGlobal) {
      S.st_info = (STB_GLOBAL << 4) | STT_FUNC;
      DeferredGlobalDefs.push_back(S);
    } else {
      S.st_info = (STB_LOCAL << 4) | STT_FUNC;
      Syms.push_back(S);
    }
  }
  unsigned FirstGlobal = Syms.size();
  Syms.append(DeferredGlobalDefs.begin(), DeferredGlobalDefs.end());
  for (const auto &Name : UndefinedSyms) {
    Sym S;
    memset(&S, 0, sizeof(S));
    S.st_name = addStr(SymStrTab, Name);
    S.st_info = (STB_GLOBAL << 4) | STT_NOTYPE;
    S.st_shndx = SHN_UNDEF;
    S.st_value = 0;
    S.st_size = 0;
    Syms.push_back(S);
  }

  // Relocations: one per undefined symbol, targeting .text
  SmallVector<Rela, 8> Relas;
  for (unsigned i = 0; i < UndefinedSyms.size(); ++i) {
    Rela R;
    R.r_offset = 0;
    R.r_info = ((uint64_t)(FirstGlobal + i) << 32) | R_X86_64_64;
    R.r_addend = 0;
    Relas.push_back(R);
  }

  // Sections: [0]=null, [1]=.text, [2]=.symtab, [3]=.strtab,
  //           [4]=.shstrtab, [5]=.rela.text (if relas)
  bool HasRela = !Relas.empty();
  unsigned NumSections = HasRela ? 6 : 5;

  // Layout
  uint64_t Off = sizeof(Ehdr);
  uint64_t TextOff = Off;
  Off += TextContent.size();
  Off = (Off + 7) & ~(uint64_t)7;
  uint64_t SymTabOff = Off;
  Off += Syms.size() * sizeof(Sym);
  uint64_t StrTabOff = Off;
  Off += SymStrTab.size();
  uint64_t ShStrTabOff = Off;
  Off += ShStrTab.size();
  uint64_t RelaOff = Off;
  if (HasRela)
    Off += Relas.size() * sizeof(Rela);
  Off = (Off + 7) & ~(uint64_t)7;
  uint64_t ShOff = Off;

  SmallVector<char, 0> Buf(ShOff + NumSections * sizeof(Shdr), 0);

  // ELF header
  auto *H = reinterpret_cast<Ehdr *>(Buf.data());
  memcpy(H->e_ident, ElfMagic, 4);
  H->e_ident[EI_CLASS] = ELFCLASS64;
  H->e_ident[EI_DATA] = ELFDATA2LSB;
  H->e_ident[EI_VERSION] = EV_CURRENT;
  H->e_type = ET_REL;
  H->e_machine = EM_X86_64;
  H->e_version = EV_CURRENT;
  H->e_ehsize = sizeof(Ehdr);
  H->e_shentsize = sizeof(Shdr);
  H->e_shoff = ShOff;
  H->e_shnum = NumSections;
  H->e_shstrndx = 4;

  // Section headers
  auto *Sec = reinterpret_cast<Shdr *>(Buf.data() + ShOff);
  // [0] null — already zeroed
  // [1] .text
  Sec[1].sh_name = TextNameOff;
  Sec[1].sh_type = SHT_PROGBITS;
  Sec[1].sh_flags = SHF_ALLOC | SHF_EXECINSTR;
  Sec[1].sh_offset = TextOff;
  Sec[1].sh_size = TextContent.size();
  Sec[1].sh_addralign = 16;
  // [2] .symtab
  Sec[2].sh_name = SymTabNameOff;
  Sec[2].sh_type = SHT_SYMTAB;
  Sec[2].sh_offset = SymTabOff;
  Sec[2].sh_size = Syms.size() * sizeof(Sym);
  Sec[2].sh_link = 3; // .strtab
  Sec[2].sh_info = FirstGlobal;
  Sec[2].sh_entsize = sizeof(Sym);
  Sec[2].sh_addralign = 8;
  // [3] .strtab
  Sec[3].sh_name = StrTabNameOff;
  Sec[3].sh_type = SHT_STRTAB;
  Sec[3].sh_offset = StrTabOff;
  Sec[3].sh_size = SymStrTab.size();
  Sec[3].sh_addralign = 1;
  // [4] .shstrtab
  Sec[4].sh_name = ShStrTabNameOff;
  Sec[4].sh_type = SHT_STRTAB;
  Sec[4].sh_offset = ShStrTabOff;
  Sec[4].sh_size = ShStrTab.size();
  Sec[4].sh_addralign = 1;
  // [5] .rela.text
  if (HasRela) {
    Sec[5].sh_name = RelaNameOff;
    Sec[5].sh_type = SHT_RELA;
    Sec[5].sh_offset = RelaOff;
    Sec[5].sh_size = Relas.size() * sizeof(Rela);
    Sec[5].sh_link = 2; // .symtab
    Sec[5].sh_info = 1; // applies to .text
    Sec[5].sh_entsize = sizeof(Rela);
    Sec[5].sh_addralign = 8;
  }

  // Write data
  memcpy(Buf.data() + TextOff, TextContent.data(), TextContent.size());
  memcpy(Buf.data() + SymTabOff, Syms.data(), Syms.size() * sizeof(Sym));
  memcpy(Buf.data() + StrTabOff, SymStrTab.data(), SymStrTab.size());
  memcpy(Buf.data() + ShStrTabOff, ShStrTab.data(), ShStrTab.size());
  if (HasRela)
    memcpy(Buf.data() + RelaOff, Relas.data(), Relas.size() * sizeof(Rela));

  return Buf;
}

/// Validate that a buffer looks like a well-formed ELF64LE relocatable object.
/// Uses raw header checks instead of LLVM's ELFObjectFile to avoid RTTI
/// dependencies (LLVM is built with -fno-rtti).
bool isValidELF64LE(ArrayRef<char> Buf) {
  if (Buf.size() < sizeof(ELF::Elf64_Ehdr))
    return false;
  auto *H = reinterpret_cast<const ELF::Elf64_Ehdr *>(Buf.data());
  return memcmp(H->e_ident, ELF::ElfMagic, 4) == 0 &&
         H->e_ident[ELF::EI_CLASS] == ELF::ELFCLASS64 &&
         H->e_ident[ELF::EI_DATA] == ELF::ELFDATA2LSB &&
         H->e_type == ELF::ET_REL &&
         H->e_shoff > 0 && H->e_shoff < Buf.size();
}

/// Merge helper: returns (success, output_buffer).
std::pair<bool, SmallVector<char, 0>>
mergeELF(ArrayRef<SmallVector<char, 0>> Bufs, Options Opts = {}) {
  SmallVector<char, 0> Out;
  raw_svector_ostream OS(Out);
  bool OK = mergeELF64LEObjects(Bufs, OS, Opts);
  return {OK, std::move(Out)};
}

// ---------------------------------------------------------------------------
// Semantic-correctness helpers
//
// The fuzz/edge tests above only prove the merger "produces a parseable ELF
// and never crashes".  They would happily pass the real-world bug where every
// merged-section symbol value and relocation offset collapsed to 0 — that
// output is a perfectly valid ELF that loads and then jumps to the wrong
// address.  The builder + raw parser below let tests assert what the merged
// object *means*: each symbol's final st_value, each relocation's r_offset and
// target symbol, and each merged section's size — exactly the invariants that
// bug violated.  The parser uses raw struct reads (no ELFObjectFile) to keep
// the check independent of the same library the merger relies on.
// ---------------------------------------------------------------------------

struct SecSpec {
  std::string Name;
  uint64_t Size;
  uint32_t Align = 16;
  uint32_t Type = ELF::SHT_PROGBITS;
  uint64_t Flags = ELF::SHF_ALLOC | ELF::SHF_EXECINSTR;
  uint8_t Fill = 0; // non-zero => fill PROGBITS content with this byte
};

struct SymSpec {
  std::string Name;
  int SecIdx;     // 0-based index into the SecSpec list; -1 => undefined
  uint64_t Value; // section-relative value for defined symbols
  bool Global = true;
};

struct RelSpec {
  int SecIdx;          // 0-based user section the relocation applies to
  uint64_t Offset;     // section-relative offset of the relocation site
  std::string SymName; // symbol referenced (by name)
  uint32_t Type = ELF::R_X86_64_64;
  int64_t Addend = 0;
};

/// Build an ELF64LE relocatable object with caller-controlled sections,
/// symbols (at known section-relative offsets), and relocations.
SmallVector<char, 0> buildSectionedELF(ArrayRef<SecSpec> Secs,
                                       ArrayRef<SymSpec> Syms,
                                       ArrayRef<RelSpec> Rels) {
  using namespace ELF;
  using Ehdr = Elf64_Ehdr;
  using Shdr = Elf64_Shdr;
  using Sym = Elf64_Sym;
  using Rela = Elf64_Rela;

  SmallVector<char, 256> ShStr, SymStr;
  ShStr.push_back('\0');
  SymStr.push_back('\0');
  auto addStr = [](SmallVector<char, 256> &T, StringRef S) -> uint32_t {
    uint32_t Off = T.size();
    T.append(S.begin(), S.end());
    T.push_back('\0');
    return Off;
  };

  unsigned K = Secs.size();

  // Which user sections carry relocations (→ get a .rela.<name> section).
  SmallVector<bool, 8> HasRel(K, false);
  for (auto &R : Rels)
    if (R.SecIdx >= 0 && (unsigned)R.SecIdx < K)
      HasRel[R.SecIdx] = true;

  SmallVector<uint32_t, 8> SecNameOff(K);
  for (unsigned i = 0; i < K; ++i)
    SecNameOff[i] = addStr(ShStr, Secs[i].Name);
  uint32_t SymTabNameOff = addStr(ShStr, ".symtab");
  uint32_t StrTabNameOff = addStr(ShStr, ".strtab");
  uint32_t ShStrTabNameOff = addStr(ShStr, ".shstrtab");
  SmallVector<uint32_t, 8> RelaNameOff(K, 0);
  for (unsigned i = 0; i < K; ++i)
    if (HasRel[i])
      RelaNameOff[i] = addStr(ShStr, std::string(".rela") + Secs[i].Name);

  // Symbols: [0]=null, then locals, then globals (ELF requires this order).
  SmallVector<Sym, 16> OutSyms;
  Sym Null;
  memset(&Null, 0, sizeof(Null));
  OutSyms.push_back(Null);
  StringMap<unsigned> SymIndex;
  auto emitSym = [&](const SymSpec &S) {
    Sym E;
    memset(&E, 0, sizeof(E));
    E.st_name = addStr(SymStr, S.Name);
    if (S.SecIdx < 0) {
      E.st_shndx = SHN_UNDEF;
      E.st_value = 0;
    } else {
      E.st_shndx = 1 + S.SecIdx;
      E.st_value = S.Value;
    }
    E.st_info = ((S.Global ? STB_GLOBAL : STB_LOCAL) << 4) | STT_FUNC;
    SymIndex[S.Name] = OutSyms.size();
    OutSyms.push_back(E);
  };
  for (auto &S : Syms)
    if (!S.Global)
      emitSym(S);
  unsigned FirstGlobal = OutSyms.size();
  for (auto &S : Syms)
    if (S.Global)
      emitSym(S);

  // Per-target-section relocation tables.
  SmallVector<SmallVector<Rela, 4>, 8> RelTab(K);
  for (auto &R : Rels) {
    if (R.SecIdx < 0 || (unsigned)R.SecIdx >= K)
      continue;
    Rela RE;
    RE.r_offset = R.Offset;
    auto It = SymIndex.find(R.SymName);
    unsigned SymIdx = It != SymIndex.end() ? It->second : 0;
    RE.r_info = ((uint64_t)SymIdx << 32) | R.Type;
    RE.r_addend = R.Addend;
    RelTab[R.SecIdx].push_back(RE);
  }

  // File layout: Ehdr, section contents, symtab, strtab, shstrtab, relas, shdrs.
  uint64_t Off = sizeof(Ehdr);
  SmallVector<uint64_t, 8> SecOff(K, 0);
  for (unsigned i = 0; i < K; ++i) {
    if (Secs[i].Type == SHT_NOBITS) {
      SecOff[i] = Off; // NOBITS occupies no file space
      continue;
    }
    uint32_t A = Secs[i].Align ? Secs[i].Align : 1;
    Off = (Off + A - 1) & ~(uint64_t)(A - 1);
    SecOff[i] = Off;
    Off += Secs[i].Size;
  }
  Off = (Off + 7) & ~(uint64_t)7;
  uint64_t SymTabOff = Off;
  Off += OutSyms.size() * sizeof(Sym);
  uint64_t StrTabOff = Off;
  Off += SymStr.size();
  uint64_t ShStrOff = Off;
  Off += ShStr.size();
  SmallVector<uint64_t, 8> RelaOff(K, 0);
  unsigned NumRela = 0;
  for (unsigned i = 0; i < K; ++i)
    if (HasRel[i]) {
      Off = (Off + 7) & ~(uint64_t)7;
      RelaOff[i] = Off;
      Off += RelTab[i].size() * sizeof(Rela);
      NumRela++;
    }
  Off = (Off + 7) & ~(uint64_t)7;
  uint64_t ShOff = Off;

  unsigned NumSec = 1 + K + 3 + NumRela; // null + user + symtab/strtab/shstrtab
  unsigned SymTabIdx = 1 + K;
  unsigned StrTabIdx = SymTabIdx + 1;
  unsigned ShStrIdx = StrTabIdx + 1;

  SmallVector<char, 0> Buf(ShOff + NumSec * sizeof(Shdr), 0);

  auto *H = reinterpret_cast<Ehdr *>(Buf.data());
  memcpy(H->e_ident, ElfMagic, 4);
  H->e_ident[EI_CLASS] = ELFCLASS64;
  H->e_ident[EI_DATA] = ELFDATA2LSB;
  H->e_ident[EI_VERSION] = EV_CURRENT;
  H->e_type = ET_REL;
  H->e_machine = EM_X86_64;
  H->e_version = EV_CURRENT;
  H->e_ehsize = sizeof(Ehdr);
  H->e_shentsize = sizeof(Shdr);
  H->e_shoff = ShOff;
  H->e_shnum = NumSec;
  H->e_shstrndx = ShStrIdx;

  auto *Sec = reinterpret_cast<Shdr *>(Buf.data() + ShOff);
  for (unsigned i = 0; i < K; ++i) {
    Shdr &S = Sec[1 + i];
    S.sh_name = SecNameOff[i];
    S.sh_type = Secs[i].Type;
    S.sh_flags = Secs[i].Flags;
    S.sh_offset = SecOff[i];
    S.sh_size = Secs[i].Size;
    S.sh_addralign = Secs[i].Align;
  }
  {
    Shdr &S = Sec[SymTabIdx];
    S.sh_name = SymTabNameOff;
    S.sh_type = SHT_SYMTAB;
    S.sh_offset = SymTabOff;
    S.sh_size = OutSyms.size() * sizeof(Sym);
    S.sh_link = StrTabIdx;
    S.sh_info = FirstGlobal;
    S.sh_entsize = sizeof(Sym);
    S.sh_addralign = 8;
  }
  {
    Shdr &S = Sec[StrTabIdx];
    S.sh_name = StrTabNameOff;
    S.sh_type = SHT_STRTAB;
    S.sh_offset = StrTabOff;
    S.sh_size = SymStr.size();
    S.sh_addralign = 1;
  }
  {
    Shdr &S = Sec[ShStrIdx];
    S.sh_name = ShStrTabNameOff;
    S.sh_type = SHT_STRTAB;
    S.sh_offset = ShStrOff;
    S.sh_size = ShStr.size();
    S.sh_addralign = 1;
  }
  unsigned RIdx = ShStrIdx + 1;
  for (unsigned i = 0; i < K; ++i) {
    if (!HasRel[i])
      continue;
    Shdr &S = Sec[RIdx++];
    S.sh_name = RelaNameOff[i];
    S.sh_type = SHT_RELA;
    S.sh_offset = RelaOff[i];
    S.sh_size = RelTab[i].size() * sizeof(Rela);
    S.sh_link = SymTabIdx;
    S.sh_info = 1 + i;
    S.sh_entsize = sizeof(Rela);
    S.sh_addralign = 8;
  }

  for (unsigned i = 0; i < K; ++i)
    if (Secs[i].Type != SHT_NOBITS && Secs[i].Fill != 0)
      memset(Buf.data() + SecOff[i], Secs[i].Fill, Secs[i].Size);
  memcpy(Buf.data() + SymTabOff, OutSyms.data(), OutSyms.size() * sizeof(Sym));
  memcpy(Buf.data() + StrTabOff, SymStr.data(), SymStr.size());
  memcpy(Buf.data() + ShStrOff, ShStr.data(), ShStr.size());
  for (unsigned i = 0; i < K; ++i)
    if (HasRel[i])
      memcpy(Buf.data() + RelaOff[i], RelTab[i].data(),
             RelTab[i].size() * sizeof(Rela));

  return Buf;
}

struct ParsedSec {
  std::string Name;
  uint32_t Type = 0;
  uint64_t Size = 0;
  uint64_t Align = 0;
  std::vector<uint8_t> Data; // on-disk bytes (empty for NOBITS)
};
struct ParsedSym {
  std::string Name;
  uint64_t Value = 0;
  uint16_t Shndx = 0;
  uint8_t Bind = 0;
};
struct ParsedRela {
  uint64_t Offset = 0;
  uint32_t Sym = 0;
  uint32_t Type = 0;
  int64_t Addend = 0;
  uint32_t TargetSec = 0; // 1-based section index the relocation applies to
};

/// Minimal raw ELF64LE reader for semantic assertions.
struct ElfView {
  bool Ok = false;
  std::vector<ParsedSec> Secs;
  std::vector<ParsedSym> Syms;
  std::vector<ParsedRela> Relas;

  const ParsedSym *findSym(StringRef N) const {
    for (auto &S : Syms)
      if (S.Name == N)
        return &S;
    return nullptr;
  }
  int findSec(StringRef N) const {
    for (unsigned i = 0; i < Secs.size(); ++i)
      if (Secs[i].Name == N)
        return (int)i;
    return -1;
  }
};

ElfView parseELF(ArrayRef<char> Buf) {
  using namespace ELF;
  using Ehdr = Elf64_Ehdr;
  using Shdr = Elf64_Shdr;
  using Sym = Elf64_Sym;
  using Rela = Elf64_Rela;

  ElfView V;
  if (Buf.size() < sizeof(Ehdr))
    return V;
  auto *H = reinterpret_cast<const Ehdr *>(Buf.data());
  if (memcmp(H->e_ident, ElfMagic, 4) != 0)
    return V;
  uint64_t ShOff = H->e_shoff;
  unsigned ShNum = H->e_shnum;
  if (ShOff == 0 || ShOff + (uint64_t)ShNum * sizeof(Shdr) > Buf.size())
    return V;
  const Shdr *Secs = reinterpret_cast<const Shdr *>(Buf.data() + ShOff);
  if (H->e_shstrndx >= ShNum)
    return V;
  const Shdr &ShStr = Secs[H->e_shstrndx];
  if (ShStr.sh_offset + ShStr.sh_size > Buf.size())
    return V;
  const char *ShStrData = Buf.data() + ShStr.sh_offset;

  auto nameAt = [](const char *Base, uint64_t Size, uint32_t Off) -> std::string {
    if (Off >= Size)
      return "";
    return std::string(Base + Off);
  };

  for (unsigned i = 0; i < ShNum; ++i) {
    ParsedSec PS;
    PS.Name = nameAt(ShStrData, ShStr.sh_size, Secs[i].sh_name);
    PS.Type = Secs[i].sh_type;
    PS.Size = Secs[i].sh_size;
    PS.Align = Secs[i].sh_addralign;
    if (Secs[i].sh_type != SHT_NOBITS && Secs[i].sh_size > 0 &&
        Secs[i].sh_offset + Secs[i].sh_size <= Buf.size()) {
      const uint8_t *D =
          reinterpret_cast<const uint8_t *>(Buf.data() + Secs[i].sh_offset);
      PS.Data.assign(D, D + Secs[i].sh_size);
    }
    V.Secs.push_back(std::move(PS));
  }

  for (unsigned i = 0; i < ShNum; ++i) {
    if (Secs[i].sh_type != SHT_SYMTAB)
      continue;
    unsigned StrIdx = Secs[i].sh_link;
    if (StrIdx >= ShNum)
      continue;
    const Shdr &Str = Secs[StrIdx];
    if (Str.sh_offset + Str.sh_size > Buf.size())
      continue;
    if (Secs[i].sh_offset + Secs[i].sh_size > Buf.size())
      continue;
    const char *StrData = Buf.data() + Str.sh_offset;
    const Sym *S = reinterpret_cast<const Sym *>(Buf.data() + Secs[i].sh_offset);
    unsigned N = Secs[i].sh_size / sizeof(Sym);
    for (unsigned k = 0; k < N; ++k) {
      ParsedSym PSym;
      PSym.Name = nameAt(StrData, Str.sh_size, S[k].st_name);
      PSym.Value = S[k].st_value;
      PSym.Shndx = S[k].st_shndx;
      PSym.Bind = S[k].st_info >> 4;
      V.Syms.push_back(std::move(PSym));
    }
  }

  for (unsigned i = 0; i < ShNum; ++i) {
    if (Secs[i].sh_type != SHT_RELA)
      continue;
    if (Secs[i].sh_offset + Secs[i].sh_size > Buf.size())
      continue;
    const Rela *R =
        reinterpret_cast<const Rela *>(Buf.data() + Secs[i].sh_offset);
    unsigned N = Secs[i].sh_size / sizeof(Rela);
    for (unsigned k = 0; k < N; ++k) {
      ParsedRela PR;
      PR.Offset = R[k].r_offset;
      PR.Sym = (uint32_t)(R[k].r_info >> 32);
      PR.Type = (uint32_t)(R[k].r_info & 0xffffffff);
      PR.Addend = R[k].r_addend;
      PR.TargetSec = Secs[i].sh_info;
      V.Relas.push_back(std::move(PR));
    }
  }

  V.Ok = true;
  return V;
}

/// Overwrite a named symbol's st_value in a merged ELF, in place.  Used to
/// *simulate* the historical "offset collapse" corruption so a test can prove
/// the verifier rejects it (the merger no longer produces such output, so the
/// only way to test the rejection path is to inject the bug after the fact).
bool patchSymValue(SmallVectorImpl<char> &Buf, StringRef Name,
                   uint64_t NewVal) {
  using namespace ELF;
  if (Buf.size() < sizeof(Elf64_Ehdr))
    return false;
  auto *H = reinterpret_cast<Elf64_Ehdr *>(Buf.data());
  if (H->e_shoff + (uint64_t)H->e_shnum * sizeof(Elf64_Shdr) > Buf.size())
    return false;
  auto *Secs = reinterpret_cast<Elf64_Shdr *>(Buf.data() + H->e_shoff);
  for (unsigned i = 0; i < H->e_shnum; ++i) {
    if (Secs[i].sh_type != SHT_SYMTAB)
      continue;
    if (Secs[i].sh_link >= H->e_shnum)
      return false;
    const char *StrD = Buf.data() + Secs[Secs[i].sh_link].sh_offset;
    auto *Sy = reinterpret_cast<Elf64_Sym *>(Buf.data() + Secs[i].sh_offset);
    unsigned Cnt = Secs[i].sh_size / sizeof(Elf64_Sym);
    for (unsigned k = 0; k < Cnt; ++k)
      if (Name == StrD + Sy[k].st_name) {
        Sy[k].st_value = NewVal;
        return true;
      }
  }
  return false;
}

/// Force every relocation's r_offset to NewVal — simulates the reloc half of
/// the offset-collapse bug without touching symbol values.
bool patchAllRelaOffsets(SmallVectorImpl<char> &Buf, uint64_t NewVal) {
  using namespace ELF;
  if (Buf.size() < sizeof(Elf64_Ehdr))
    return false;
  auto *H = reinterpret_cast<Elf64_Ehdr *>(Buf.data());
  if (H->e_shoff + (uint64_t)H->e_shnum * sizeof(Elf64_Shdr) > Buf.size())
    return false;
  auto *Secs = reinterpret_cast<Elf64_Shdr *>(Buf.data() + H->e_shoff);
  bool Any = false;
  for (unsigned i = 0; i < H->e_shnum; ++i) {
    if (Secs[i].sh_type != SHT_RELA)
      continue;
    auto *R = reinterpret_cast<Elf64_Rela *>(Buf.data() + Secs[i].sh_offset);
    unsigned N = Secs[i].sh_size / sizeof(Elf64_Rela);
    for (unsigned k = 0; k < N; ++k) {
      R[k].r_offset = NewVal;
      Any = true;
    }
  }
  return Any;
}

// ---------------------------------------------------------------------------
// COFF semantic helpers.  coff_symbol16 is an 18-byte on-disk record but the
// C++ struct can be padded, so build/parse with explicit little-endian byte
// I/O (the merger itself writes symbols as raw 18-byte records for the same
// reason).
// ---------------------------------------------------------------------------

void putU16(SmallVectorImpl<char> &B, uint16_t V) {
  B.push_back((char)(V & 0xff));
  B.push_back((char)((V >> 8) & 0xff));
}
void putU32(SmallVectorImpl<char> &B, uint32_t V) {
  for (int i = 0; i < 4; ++i)
    B.push_back((char)((V >> (8 * i)) & 0xff));
}
uint16_t getU16(const char *P) {
  return (uint16_t)((uint8_t)P[0] | ((uint8_t)P[1] << 8));
}
uint32_t getU32(const char *P) {
  return (uint32_t)((uint8_t)P[0]) | ((uint32_t)(uint8_t)P[1] << 8) |
         ((uint32_t)(uint8_t)P[2] << 16) | ((uint32_t)(uint8_t)P[3] << 24);
}

struct CoffSecSpec {
  std::string Name; // <= 8 chars for these tests
  uint32_t Size;
  uint32_t Characteristics;
  uint8_t Fill = 0; // non-zero => fill non-BSS content with this byte
};
struct CoffSymSpec {
  std::string Name; // <= 8 chars
  uint32_t Value;
  int16_t SectionNumber; // 0 = undefined, 1-based otherwise
  uint8_t StorageClass;
};
struct CoffRelSpec {
  int SecIdx;
  uint32_t VA;
  std::string SymName;
  uint16_t Type;
};

SmallVector<char, 0> buildCOFF(uint16_t Machine, ArrayRef<CoffSecSpec> Secs,
                               ArrayRef<CoffSymSpec> Syms,
                               ArrayRef<CoffRelSpec> Rels) {
  using namespace COFF;
  unsigned N = Secs.size();
  unsigned M = Syms.size();

  StringMap<unsigned> SymIndex;
  for (unsigned i = 0; i < M; ++i)
    SymIndex[Syms[i].Name] = i;

  SmallVector<SmallVector<CoffRelSpec, 4>, 8> RelTab(N);
  for (auto &R : Rels)
    if (R.SecIdx >= 0 && (unsigned)R.SecIdx < N)
      RelTab[R.SecIdx].push_back(R);

  // On-disk COFF record sizes are fixed by the spec (header 20, section header
  // 40, symbol 18, relocation 10); use the literals, not sizeof, so struct
  // padding cannot skew the layout.
  uint32_t Off = 20 + N * 40;
  SmallVector<uint32_t, 8> DataPtr(N, 0), RelPtr(N, 0);
  for (unsigned i = 0; i < N; ++i) {
    bool IsBSS =
        (Secs[i].Characteristics & IMAGE_SCN_CNT_UNINITIALIZED_DATA) != 0;
    if (!IsBSS && Secs[i].Size > 0) {
      DataPtr[i] = Off;
      Off += Secs[i].Size;
    }
  }
  for (unsigned i = 0; i < N; ++i)
    if (!RelTab[i].empty()) {
      RelPtr[i] = Off;
      Off += RelTab[i].size() * 10; // coff_relocation is 10 bytes on disk
    }
  uint32_t SymPtr = Off;
  Off += M * 18;
  // String table (just the mandatory 4-byte length).

  SmallVector<char, 0> Buf;
  putU16(Buf, Machine);
  putU16(Buf, (uint16_t)N);
  putU32(Buf, 0);      // TimeDateStamp
  putU32(Buf, SymPtr); // PointerToSymbolTable
  putU32(Buf, M);      // NumberOfSymbols
  putU16(Buf, 0);      // SizeOfOptionalHeader
  putU16(Buf, 0);      // Characteristics

  for (unsigned i = 0; i < N; ++i) {
    char Name[8] = {0};
    memcpy(Name, Secs[i].Name.data(), std::min<size_t>(Secs[i].Name.size(), 8));
    Buf.append(Name, Name + 8);
    bool IsBSS =
        (Secs[i].Characteristics & IMAGE_SCN_CNT_UNINITIALIZED_DATA) != 0;
    putU32(Buf, 0);              // VirtualSize
    putU32(Buf, 0);              // VirtualAddress
    putU32(Buf, Secs[i].Size);   // SizeOfRawData
    putU32(Buf, IsBSS ? 0 : DataPtr[i]);          // PointerToRawData
    putU32(Buf, RelPtr[i]);                        // PointerToRelocations
    putU32(Buf, 0);                                // PointerToLinenumbers
    putU16(Buf, (uint16_t)RelTab[i].size());       // NumberOfRelocations
    putU16(Buf, 0);                                // NumberOfLinenumbers
    putU32(Buf, Secs[i].Characteristics);
  }

  for (unsigned i = 0; i < N; ++i) {
    bool IsBSS =
        (Secs[i].Characteristics & IMAGE_SCN_CNT_UNINITIALIZED_DATA) != 0;
    if (!IsBSS && Secs[i].Size > 0)
      Buf.append(Secs[i].Size, (char)Secs[i].Fill);
  }
  for (unsigned i = 0; i < N; ++i)
    for (auto &R : RelTab[i]) {
      auto It = SymIndex.find(R.SymName);
      uint32_t SymIdx = It != SymIndex.end() ? It->second : 0;
      putU32(Buf, R.VA);
      putU32(Buf, SymIdx);
      putU16(Buf, R.Type);
    }

  for (auto &S : Syms) {
    char Name[8] = {0};
    memcpy(Name, S.Name.data(), std::min<size_t>(S.Name.size(), 8));
    Buf.append(Name, Name + 8);
    putU32(Buf, S.Value);
    putU16(Buf, (uint16_t)S.SectionNumber);
    putU16(Buf, 0);               // Type
    Buf.push_back((char)S.StorageClass);
    Buf.push_back(0);             // NumberOfAuxSymbols
  }

  putU32(Buf, 4); // empty string table (length field counts itself)
  return Buf;
}

struct CoffParsedSym {
  std::string Name;
  uint32_t Value = 0;
  int16_t SectionNumber = 0;
  uint8_t StorageClass = 0;
};
struct CoffParsedRel {
  uint32_t VA = 0;
  uint32_t SymIdx = 0;
  uint16_t Type = 0;
  unsigned SecIdx = 0; // 0-based section the relocation belongs to
};
struct CoffView {
  bool Ok = false;
  std::vector<std::string> SecNames;
  std::vector<uint32_t> SecSizes;
  std::vector<CoffParsedSym> Syms;
  std::vector<CoffParsedRel> Rels;

  const CoffParsedSym *findSym(StringRef N) const {
    for (auto &S : Syms)
      if (S.Name == N)
        return &S;
    return nullptr;
  }
};

CoffView parseCOFF(ArrayRef<char> Buf) {
  CoffView V;
  if (Buf.size() < 20)
    return V;
  const char *P = Buf.data();
  unsigned N = getU16(P + 2);
  uint32_t SymPtr = getU32(P + 8);
  uint32_t NumSym = getU32(P + 12);

  uint32_t SecHdrOff = 20;
  if (SecHdrOff + (uint64_t)N * 40 > Buf.size())
    return V;

  const char *StrTab = nullptr;
  uint32_t StrTabSize = 0;
  uint32_t StrTabOff = SymPtr + NumSym * 18;
  if (SymPtr != 0 && StrTabOff + 4 <= Buf.size()) {
    StrTab = Buf.data() + StrTabOff;
    StrTabSize = getU32(StrTab);
  }
  auto resolveName = [&](const char *Field) -> std::string {
    if (getU32(Field) == 0) {
      uint32_t SOff = getU32(Field + 4);
      if (StrTab && SOff < StrTabSize)
        return std::string(StrTab + SOff);
      return "";
    }
    char Tmp[9] = {0};
    memcpy(Tmp, Field, 8);
    return std::string(Tmp);
  };

  struct SecInfo {
    uint32_t RelPtr, NRel;
  };
  std::vector<SecInfo> SI(N);
  for (unsigned i = 0; i < N; ++i) {
    const char *H = Buf.data() + SecHdrOff + i * 40;
    V.SecNames.push_back(resolveName(H));
    V.SecSizes.push_back(getU32(H + 16)); // SizeOfRawData
    SI[i] = {getU32(H + 24), getU16(H + 32)};
  }

  if (SymPtr != 0 && SymPtr + (uint64_t)NumSym * 18 <= Buf.size()) {
    unsigned k = 0;
    while (k < NumSym) {
      const char *S = Buf.data() + SymPtr + k * 18;
      CoffParsedSym PS;
      PS.Name = resolveName(S);
      PS.Value = getU32(S + 8);
      PS.SectionNumber = (int16_t)getU16(S + 12);
      PS.StorageClass = (uint8_t)S[16];
      uint8_t NAux = (uint8_t)S[17];
      V.Syms.push_back(std::move(PS));
      k += 1 + NAux; // aux records keep their absolute index slots
      for (uint8_t a = 0; a < NAux; ++a)
        V.Syms.push_back(CoffParsedSym{}); // placeholder to preserve indices
    }
  }

  for (unsigned i = 0; i < N; ++i) {
    if (SI[i].NRel == 0 || SI[i].RelPtr == 0)
      continue;
    if (SI[i].RelPtr + (uint64_t)SI[i].NRel * 10 > Buf.size())
      continue;
    for (unsigned r = 0; r < SI[i].NRel; ++r) {
      const char *R = Buf.data() + SI[i].RelPtr + r * 10;
      CoffParsedRel PR;
      PR.VA = getU32(R);
      PR.SymIdx = getU32(R + 4);
      PR.Type = getU16(R + 8);
      PR.SecIdx = i;
      V.Rels.push_back(PR);
    }
  }

  V.Ok = true;
  return V;
}

/// Overwrite a named COFF symbol's Value in place — the COFF analogue of
/// patchSymValue, used to simulate offset-collapse corruption for the verifier.
bool patchCoffSymValue(SmallVectorImpl<char> &Buf, StringRef Name,
                       uint32_t NewVal) {
  if (Buf.size() < 20)
    return false;
  uint32_t SymOff = getU32(Buf.data() + 8);
  uint32_t NSym = getU32(Buf.data() + 12);
  if (SymOff == 0 || SymOff + (uint64_t)NSym * 18 > Buf.size())
    return false;
  uint32_t StrOff = SymOff + NSym * 18;
  const char *StrTab = (StrOff + 4 <= Buf.size()) ? Buf.data() + StrOff : nullptr;
  uint32_t StrSize = StrTab ? getU32(StrTab) : 0;
  unsigned k = 0;
  while (k < NSym) {
    char *S = Buf.data() + SymOff + k * 18;
    std::string Nm;
    if (getU32(S) == 0) {
      uint32_t O = getU32(S + 4);
      if (StrTab && O < StrSize)
        Nm = std::string(StrTab + O);
    } else {
      char T[9] = {0};
      memcpy(T, S, 8);
      Nm = std::string(T);
    }
    uint8_t NAux = (uint8_t)S[17];
    if (Name == Nm) {
      S[8] = (char)(NewVal & 0xff);
      S[9] = (char)((NewVal >> 8) & 0xff);
      S[10] = (char)((NewVal >> 16) & 0xff);
      S[11] = (char)((NewVal >> 24) & 0xff);
      return true;
    }
    k += 1u + NAux;
  }
  return false;
}

/// Force every COFF relocation's VirtualAddress to NewVal — simulates the
/// relocation half of the offset-collapse bug for the COFF verifier.
bool patchAllCoffRelocVAs(SmallVectorImpl<char> &Buf, uint32_t NewVal) {
  if (Buf.size() < 20)
    return false;
  unsigned N = getU16(Buf.data() + 2);
  uint16_t OptSize = getU16(Buf.data() + 16);
  uint64_t SecBase = 20ull + OptSize;
  if (SecBase + (uint64_t)N * 40 > Buf.size())
    return false;
  bool Any = false;
  for (unsigned i = 0; i < N; ++i) {
    const char *H = Buf.data() + SecBase + i * 40;
    uint32_t RelPtr = getU32(H + 24);
    uint16_t NRel = getU16(H + 32);
    if (RelPtr == 0 || NRel == 0)
      continue;
    if ((uint64_t)RelPtr + (uint64_t)NRel * 10 > Buf.size())
      continue;
    for (unsigned r = 0; r < NRel; ++r) {
      char *R = Buf.data() + RelPtr + r * 10;
      R[0] = (char)(NewVal & 0xff);
      R[1] = (char)((NewVal >> 8) & 0xff);
      R[2] = (char)((NewVal >> 16) & 0xff);
      R[3] = (char)((NewVal >> 24) & 0xff);
      Any = true;
    }
  }
  return Any;
}

// ---------------------------------------------------------------------------
// Mach-O semantic helpers.  The Mach-O structs in BinaryFormat/MachO.h match
// the on-disk layout, and host + target are little-endian here, so the merger
// (and this builder) write them directly.
// ---------------------------------------------------------------------------

struct MachoSecSpec {
  std::string Seg;  // e.g. "__TEXT"
  std::string Sect; // e.g. "__text"
  uint64_t Size;
  uint32_t AlignExp; // power-of-two exponent (4 => 16-byte)
  uint32_t Flags;
  uint8_t Fill = 0; // non-zero => fill section content with this byte
};
struct MachoSymSpec {
  std::string Name;
  uint8_t Type;   // e.g. N_SECT | N_EXT
  uint8_t Sect;   // 1-based section index, 0 = none
  uint64_t Value; // section-relative offset for defined symbols
  uint16_t Desc;
};
struct MachoRelSpec {
  int SecIdx;          // 0-based section the relocation applies to
  uint32_t Address;    // section-relative offset of the relocation site
  std::string SymName; // target symbol (extern relocation)
  uint8_t Type;
  uint8_t Length;      // log2 byte size (2 => 4 bytes, 3 => 8 bytes)
};

SmallVector<char, 0> buildMachO(uint32_t CpuType, uint32_t CpuSubType,
                                ArrayRef<MachoSecSpec> Secs,
                                ArrayRef<MachoSymSpec> Syms,
                                ArrayRef<MachoRelSpec> Rels = {}) {
  namespace MO = llvm::MachO;
  unsigned N = Secs.size();
  uint32_t SegCmdSize =
      sizeof(MO::segment_command_64) + N * sizeof(MO::section_64);
  uint32_t SymCmdSize = sizeof(MO::symtab_command);
  uint32_t SizeOfCmds = SegCmdSize + SymCmdSize;
  uint64_t DataStart = sizeof(MO::mach_header_64) + SizeOfCmds;

  uint64_t Off = DataStart;
  SmallVector<uint64_t, 8> SecOff(N), SecAddr(N);
  for (unsigned i = 0; i < N; ++i) {
    uint64_t Align = 1ULL << std::min(Secs[i].AlignExp, 20u);
    Off = (Off + Align - 1) & ~(Align - 1);
    SecOff[i] = Off;
    SecAddr[i] = Off - DataStart;
    Off += Secs[i].Size;
  }
  uint64_t FileSizeSecs = Off - DataStart;

  // Per-section relocation tables (8 bytes each), grouped by section and laid
  // out after section content (link-edit data, outside the segment vmsize).
  SmallVector<SmallVector<MachoRelSpec, 4>, 8> RelBySec(N);
  for (auto &R : Rels)
    if (R.SecIdx >= 0 && (unsigned)R.SecIdx < N)
      RelBySec[R.SecIdx].push_back(R);
  SmallVector<uint32_t, 8> RelOff(N, 0);
  for (unsigned i = 0; i < N; ++i)
    if (!RelBySec[i].empty()) {
      Off = (Off + 3) & ~(uint64_t)3;
      RelOff[i] = (uint32_t)Off;
      Off += RelBySec[i].size() * 8;
    }

  Off = (Off + 7) & ~(uint64_t)7;
  uint64_t SymOff = Off;

  SmallVector<char, 0> StrTab;
  StrTab.push_back('\0');
  SmallVector<MO::nlist_64, 16> NList;
  for (auto &S : Syms) {
    MO::nlist_64 NL;
    memset(&NL, 0, sizeof(NL));
    NL.n_strx = StrTab.size();
    StrTab.append(S.Name.begin(), S.Name.end());
    StrTab.push_back('\0');
    NL.n_type = S.Type;
    NL.n_sect = S.Sect;
    NL.n_desc = S.Desc;
    NL.n_value =
        (S.Sect >= 1 && S.Sect <= N) ? SecAddr[S.Sect - 1] + S.Value : S.Value;
    NList.push_back(NL);
  }
  Off += NList.size() * sizeof(MO::nlist_64);
  uint64_t StrOff = Off;
  Off += StrTab.size();

  SmallVector<char, 0> Buf;
  Buf.resize(Off, 0);

  // Section content (so the verifier's content anchor is meaningful).
  for (unsigned i = 0; i < N; ++i)
    if (Secs[i].Fill != 0)
      memset(Buf.data() + SecOff[i], Secs[i].Fill, Secs[i].Size);

  auto *MH = reinterpret_cast<MO::mach_header_64 *>(Buf.data());
  MH->magic = MO::MH_MAGIC_64;
  MH->cputype = CpuType;
  MH->cpusubtype = CpuSubType;
  MH->filetype = MO::MH_OBJECT;
  MH->ncmds = 2;
  MH->sizeofcmds = SizeOfCmds;
  MH->flags = MO::MH_SUBSECTIONS_VIA_SYMBOLS;
  MH->reserved = 0;

  char *Cmd = Buf.data() + sizeof(MO::mach_header_64);
  auto *Seg = reinterpret_cast<MO::segment_command_64 *>(Cmd);
  memset(Seg, 0, sizeof(MO::segment_command_64));
  Seg->cmd = MO::LC_SEGMENT_64;
  Seg->cmdsize = SegCmdSize;
  Seg->vmaddr = 0;
  Seg->vmsize = FileSizeSecs;
  Seg->fileoff = DataStart;
  Seg->filesize = FileSizeSecs;
  Seg->maxprot = 7;
  Seg->initprot = 7;
  Seg->nsects = N;
  Seg->flags = 0;

  auto *SH = reinterpret_cast<MO::section_64 *>(Cmd +
                                               sizeof(MO::segment_command_64));
  for (unsigned i = 0; i < N; ++i) {
    memset(&SH[i], 0, sizeof(MO::section_64));
    memcpy(SH[i].sectname, Secs[i].Sect.data(),
           std::min<size_t>(Secs[i].Sect.size(), 16));
    memcpy(SH[i].segname, Secs[i].Seg.data(),
           std::min<size_t>(Secs[i].Seg.size(), 16));
    SH[i].addr = SecAddr[i];
    SH[i].size = Secs[i].Size;
    SH[i].offset = (uint32_t)SecOff[i];
    SH[i].align = Secs[i].AlignExp;
    SH[i].reloff = RelOff[i];
    SH[i].nreloc = (uint32_t)RelBySec[i].size();
    SH[i].flags = Secs[i].Flags;
  }

  Cmd += SegCmdSize;
  auto *SymCmd = reinterpret_cast<MO::symtab_command *>(Cmd);
  memset(SymCmd, 0, sizeof(MO::symtab_command));
  SymCmd->cmd = MO::LC_SYMTAB;
  SymCmd->cmdsize = SymCmdSize;
  SymCmd->symoff = (uint32_t)SymOff;
  SymCmd->nsyms = NList.size();
  SymCmd->stroff = (uint32_t)StrOff;
  SymCmd->strsize = (uint32_t)StrTab.size();

  // Relocation entries (non-scattered: r_address i32, then packed word
  // symbolnum:24, pcrel:1, length:2, extern:1, type:4 — always extern here).
  {
    StringMap<unsigned> SymIdx;
    for (unsigned i = 0; i < Syms.size(); ++i)
      SymIdx[Syms[i].Name] = i;
    for (unsigned i = 0; i < N; ++i)
      for (unsigned r = 0; r < RelBySec[i].size(); ++r) {
        const MachoRelSpec &R = RelBySec[i][r];
        unsigned Sym = 0;
        auto It = SymIdx.find(R.SymName);
        if (It != SymIdx.end())
          Sym = It->second;
        char *P = Buf.data() + RelOff[i] + r * 8;
        uint32_t Addr = R.Address;
        memcpy(P, &Addr, 4);
        uint32_t W = (Sym & 0xFFFFFFu) | (((uint32_t)R.Length & 0x3u) << 25) |
                     (1u << 27) | ((uint32_t)R.Type << 28);
        memcpy(P + 4, &W, 4);
      }
  }

  if (!NList.empty())
    memcpy(Buf.data() + SymOff, NList.data(),
           NList.size() * sizeof(MO::nlist_64));
  memcpy(Buf.data() + StrOff, StrTab.data(), StrTab.size());
  return Buf;
}

struct MachoParsedSec {
  std::string Seg, Sect;
  uint64_t Addr = 0;
  uint64_t Size = 0;
};
struct MachoParsedSym {
  std::string Name;
  uint8_t Sect = 0;
  uint64_t Value = 0;
};
struct MachoView {
  bool Ok = false;
  std::vector<MachoParsedSec> Secs;
  std::vector<MachoParsedSym> Syms;

  const MachoParsedSym *findSym(StringRef N) const {
    for (auto &S : Syms)
      if (S.Name == N)
        return &S;
    return nullptr;
  }
  const MachoParsedSec *findSec(StringRef Sg, StringRef St) const {
    for (auto &S : Secs)
      if (S.Seg == Sg && S.Sect == St)
        return &S;
    return nullptr;
  }
};

MachoView parseMachO(ArrayRef<char> Buf) {
  namespace MO = llvm::MachO;
  MachoView V;
  if (Buf.size() < sizeof(MO::mach_header_64))
    return V;
  auto *MH = reinterpret_cast<const MO::mach_header_64 *>(Buf.data());
  if (MH->magic != MO::MH_MAGIC_64)
    return V;

  auto cstr16 = [](const char *P) -> std::string {
    char Tmp[17] = {0};
    memcpy(Tmp, P, 16);
    return std::string(Tmp);
  };

  uint64_t Cmd = sizeof(MO::mach_header_64);
  for (unsigned c = 0; c < MH->ncmds; ++c) {
    if (Cmd + sizeof(MO::load_command) > Buf.size())
      return V;
    auto *LC = reinterpret_cast<const MO::load_command *>(Buf.data() + Cmd);
    if (LC->cmd == MO::LC_SEGMENT_64) {
      auto *Seg =
          reinterpret_cast<const MO::segment_command_64 *>(Buf.data() + Cmd);
      const char *SP = Buf.data() + Cmd + sizeof(MO::segment_command_64);
      for (unsigned i = 0; i < Seg->nsects; ++i) {
        auto *S = reinterpret_cast<const MO::section_64 *>(
            SP + i * sizeof(MO::section_64));
        MachoParsedSec PS;
        PS.Seg = cstr16(S->segname);
        PS.Sect = cstr16(S->sectname);
        PS.Addr = S->addr;
        PS.Size = S->size;
        V.Secs.push_back(std::move(PS));
      }
    } else if (LC->cmd == MO::LC_SYMTAB) {
      auto *SymCmd =
          reinterpret_cast<const MO::symtab_command *>(Buf.data() + Cmd);
      if ((uint64_t)SymCmd->stroff + SymCmd->strsize > Buf.size())
        return V;
      if ((uint64_t)SymCmd->symoff + (uint64_t)SymCmd->nsyms * 16 > Buf.size())
        return V;
      const char *Str = Buf.data() + SymCmd->stroff;
      for (unsigned i = 0; i < SymCmd->nsyms; ++i) {
        auto *NL = reinterpret_cast<const MO::nlist_64 *>(
            Buf.data() + SymCmd->symoff + i * sizeof(MO::nlist_64));
        MachoParsedSym PS;
        if (NL->n_strx < SymCmd->strsize)
          PS.Name = std::string(Str + NL->n_strx);
        PS.Sect = NL->n_sect;
        PS.Value = NL->n_value;
        V.Syms.push_back(std::move(PS));
      }
    }
    Cmd += LC->cmdsize;
    if (LC->cmdsize == 0)
      break;
  }
  V.Ok = true;
  return V;
}

/// Overwrite a named Mach-O symbol's n_value in place — the Mach-O analogue of
/// patchSymValue, used to simulate offset-collapse corruption for the verifier.
bool patchMachoSymValue(SmallVectorImpl<char> &Buf, StringRef Name,
                        uint64_t NewVal) {
  namespace MO = llvm::MachO;
  if (Buf.size() < sizeof(MO::mach_header_64))
    return false;
  auto *MH = reinterpret_cast<MO::mach_header_64 *>(Buf.data());
  uint64_t Cmd = sizeof(MO::mach_header_64);
  for (unsigned c = 0; c < MH->ncmds; ++c) {
    if (Cmd + sizeof(MO::load_command) > Buf.size())
      return false;
    auto *LC = reinterpret_cast<MO::load_command *>(Buf.data() + Cmd);
    if (LC->cmd == MO::LC_SYMTAB) {
      auto *SC = reinterpret_cast<MO::symtab_command *>(Buf.data() + Cmd);
      const char *Str = Buf.data() + SC->stroff;
      for (unsigned i = 0; i < SC->nsyms; ++i) {
        auto *NL = reinterpret_cast<MO::nlist_64 *>(
            Buf.data() + SC->symoff + i * sizeof(MO::nlist_64));
        if (NL->n_strx < SC->strsize && Name == (Str + NL->n_strx)) {
          NL->n_value = NewVal;
          return true;
        }
      }
    }
    Cmd += LC->cmdsize;
    if (LC->cmdsize == 0)
      break;
  }
  return false;
}

/// Force every Mach-O relocation's r_address to NewVal — simulates the reloc
/// half of the offset-collapse bug for the Mach-O verifier.
bool patchAllMachoRelocAddrs(SmallVectorImpl<char> &Buf, uint32_t NewVal) {
  namespace MO = llvm::MachO;
  if (Buf.size() < sizeof(MO::mach_header_64))
    return false;
  auto *MH = reinterpret_cast<MO::mach_header_64 *>(Buf.data());
  uint64_t Cmd = sizeof(MO::mach_header_64);
  bool Any = false;
  for (unsigned c = 0; c < MH->ncmds; ++c) {
    if (Cmd + sizeof(MO::load_command) > Buf.size())
      break;
    auto *LC = reinterpret_cast<MO::load_command *>(Buf.data() + Cmd);
    if (LC->cmd == MO::LC_SEGMENT_64) {
      auto *Seg = reinterpret_cast<MO::segment_command_64 *>(Buf.data() + Cmd);
      char *SP = Buf.data() + Cmd + sizeof(MO::segment_command_64);
      for (unsigned i = 0; i < Seg->nsects; ++i) {
        auto *S =
            reinterpret_cast<MO::section_64 *>(SP + i * sizeof(MO::section_64));
        if (S->reloff == 0 || S->nreloc == 0)
          continue;
        if ((uint64_t)S->reloff + (uint64_t)S->nreloc * 8 > Buf.size())
          continue;
        for (unsigned r = 0; r < S->nreloc; ++r)
          memcpy(Buf.data() + S->reloff + r * 8, &NewVal, 4);
        Any = true;
      }
    }
    Cmd += LC->cmdsize;
    if (LC->cmdsize == 0)
      break;
  }
  return Any;
}

} // namespace

// ---------------------------------------------------------------------------
// Edge-case tests: ELF merger
// ---------------------------------------------------------------------------

TEST(MergeELF, EmptyBufferArray) {
  SmallVector<SmallVector<char, 0>, 2> Bufs;
  auto [OK, Out] = mergeELF(Bufs);
  (void)OK;
  // No crash is the success criterion; merger may produce minimal output
}

TEST(MergeELF, AllEmptyBuffers) {
  SmallVector<SmallVector<char, 0>, 4> Bufs(3);
  auto [OK, Out] = mergeELF(Bufs);
  (void)OK;
  // No crash is the success criterion
}

TEST(MergeELF, SingleValidBuffer) {
  auto Obj = buildMinimalELF({"main"}, {});
  ASSERT_TRUE(isValidELF64LE(Obj));

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(std::move(Obj));
  auto [OK, Out] = mergeELF(Bufs);
  EXPECT_TRUE(OK);
  EXPECT_TRUE(isValidELF64LE(Out));
}

TEST(MergeELF, TwoPartitionsWithUndefinedSymbols) {
  // Partition 0: defines "foo", references "bar" (undefined)
  auto P0 = buildMinimalELF({"foo"}, {"bar"});
  // Partition 1: defines "bar", references "foo" (undefined)
  auto P1 = buildMinimalELF({"bar"}, {"foo"});
  ASSERT_TRUE(isValidELF64LE(P0));
  ASSERT_TRUE(isValidELF64LE(P1));

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(std::move(P0));
  Bufs.push_back(std::move(P1));
  auto [OK, Out] = mergeELF(Bufs);
  EXPECT_TRUE(OK);
  EXPECT_TRUE(isValidELF64LE(Out));
}

TEST(MergeELF, ManyUndefinedSymbols) {
  // Stress test: many undefined symbols per partition
  std::vector<std::string> Defs0, Undefs0, Defs1, Undefs1;
  for (int i = 0; i < 100; ++i) {
    std::string Name = "sym" + std::to_string(i);
    if (i % 2 == 0) {
      Defs0.push_back(Name);
      Undefs1.push_back(Name);
    } else {
      Defs1.push_back(Name);
      Undefs0.push_back(Name);
    }
  }
  auto P0 = buildMinimalELF(Defs0, Undefs0);
  auto P1 = buildMinimalELF(Defs1, Undefs1);

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(std::move(P0));
  Bufs.push_back(std::move(P1));
  auto [OK, Out] = mergeELF(Bufs);
  EXPECT_TRUE(OK);
  EXPECT_TRUE(isValidELF64LE(Out));
}

TEST(MergeELF, PartitionGapEmptyMiddle) {
  // [valid, empty, valid] — tests Maps.resize(p+1) with gap
  auto P0 = buildMinimalELF({"a"}, {});
  auto P2 = buildMinimalELF({"b"}, {});

  SmallVector<SmallVector<char, 0>, 4> Bufs;
  Bufs.push_back(std::move(P0));
  Bufs.emplace_back(); // empty partition 1
  Bufs.push_back(std::move(P2));
  auto [OK, Out] = mergeELF(Bufs);
  EXPECT_TRUE(OK);
  EXPECT_TRUE(isValidELF64LE(Out));
}

TEST(MergeELF, OnlyUndefinedSymbols) {
  // All symbols are undefined — the scenario that caused the original crash
  auto P0 = buildMinimalELF({}, {"ext1", "ext2", "ext3"});
  auto P1 = buildMinimalELF({}, {"ext4", "ext5"});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(std::move(P0));
  Bufs.push_back(std::move(P1));
  auto [OK, Out] = mergeELF(Bufs);
  EXPECT_TRUE(OK);
  EXPECT_TRUE(isValidELF64LE(Out));
}

TEST(MergeELF, PcgSymbolDemotion) {
  // Symbols with .__pcg marker should be demoted to local.
  // PCG symbols are always GLOBAL in real parallel codegen output.
  auto P0 = buildMinimalELF({"helper.__pcg12345678"}, {},
                             {0xcc}, /*DefinedAsGlobal=*/true);
  auto P1 = buildMinimalELF({}, {"helper.__pcg12345678"});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(std::move(P0));
  Bufs.push_back(std::move(P1));
  auto [OK, Out] = mergeELF(Bufs);
  EXPECT_TRUE(OK);
  EXPECT_TRUE(isValidELF64LE(Out));
}

// ---------------------------------------------------------------------------
// Semantic-correctness tests: assert the *meaning* of the merged object.
// Every test here fails on the historical "merged offsets collapse to 0" bug.
// ---------------------------------------------------------------------------

TEST(MergeELFSemantic, SectionMergeSymbolOffsets) {
  // One partition with two function sections that collapse into .text when
  // mergeSections is on (the Android-kernel-module case).  Symbols in the
  // *second* merged section must be shifted past the first — the exact
  // invariant the PartOffsets/SecOff bug violated by leaving them all at 0.
  SecSpec SA{".text.a", 0x34, 16};
  SecSpec SB{".text.b", 0x20, 16};
  SymSpec A{"a", 0, 0};           // start of .text.a
  SymSpec AMid{"a_mid", 0, 0x10}; // inside .text.a
  SymSpec B{"b", 1, 0};           // start of .text.b
  SymSpec BMid{"b_mid", 1, 0x8};  // inside .text.b

  auto Obj = buildSectionedELF({SA, SB}, {A, AMid, B, BMid}, {});
  ASSERT_TRUE(isValidELF64LE(Obj));

  SmallVector<SmallVector<char, 0>, 1> Bufs;
  Bufs.push_back(std::move(Obj));
  Options Opts;
  Opts.mergeSections = true;
  auto [OK, Out] = mergeELF(Bufs, Opts);
  ASSERT_TRUE(OK);

  ElfView V = parseELF(Out);
  ASSERT_TRUE(V.Ok);

  EXPECT_GE(V.findSec(".text"), 0);
  EXPECT_LT(V.findSec(".text.a"), 0);
  EXPECT_LT(V.findSec(".text.b"), 0);

  const ParsedSym *PA = V.findSym("a");
  const ParsedSym *PAMid = V.findSym("a_mid");
  const ParsedSym *PB = V.findSym("b");
  const ParsedSym *PBMid = V.findSym("b_mid");
  ASSERT_NE(PA, nullptr);
  ASSERT_NE(PAMid, nullptr);
  ASSERT_NE(PB, nullptr);
  ASSERT_NE(PBMid, nullptr);

  // .text.a at [0, 0x34); .text.b padded to align 16 → starts at 0x40.
  EXPECT_EQ(PA->Value, 0x0u);
  EXPECT_EQ(PAMid->Value, 0x10u);
  EXPECT_EQ(PB->Value, 0x40u);   // the bug made this 0
  EXPECT_EQ(PBMid->Value, 0x48u); // the bug made this 0x8

  // All four resolve into the same merged section.
  EXPECT_EQ(PA->Shndx, PB->Shndx);
  EXPECT_EQ(PA->Shndx, PAMid->Shndx);
  EXPECT_EQ(PA->Shndx, PBMid->Shndx);
}

TEST(MergeELFSemantic, CrossPartitionSymbolAndRelocOffsets) {
  // Two partitions each carrying their own .text.  After merge, partition 1's
  // symbols *and* relocations must be shifted by partition 0's .text size.
  SecSpec S0{".text", 0x40, 16};
  SecSpec S1{".text", 0x20, 16};
  SymSpec P0{"p0", 0, 0};
  SymSpec P1{"p1", 0, 0};
  SymSpec Ext{"ext", -1, 0}; // undefined, referenced by partition 1's reloc
  RelSpec R1{0, 0, "ext", ELF::R_X86_64_64, 0}; // at offset 0 of P1's .text

  auto Obj0 = buildSectionedELF({S0}, {P0}, {});
  auto Obj1 = buildSectionedELF({S1}, {P1, Ext}, {R1});
  ASSERT_TRUE(isValidELF64LE(Obj0));
  ASSERT_TRUE(isValidELF64LE(Obj1));

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(std::move(Obj0));
  Bufs.push_back(std::move(Obj1));
  auto [OK, Out] = mergeELF(Bufs);
  ASSERT_TRUE(OK);

  ElfView V = parseELF(Out);
  ASSERT_TRUE(V.Ok);

  const ParsedSym *PP0 = V.findSym("p0");
  const ParsedSym *PP1 = V.findSym("p1");
  ASSERT_NE(PP0, nullptr);
  ASSERT_NE(PP1, nullptr);
  EXPECT_EQ(PP0->Value, 0x0u);
  EXPECT_EQ(PP1->Value, 0x40u); // shifted by partition 0's .text size

  // The relocation from partition 1 keeps pointing at "ext" but its r_offset
  // moves to 0x40 in the merged .text.
  ASSERT_EQ(V.Relas.size(), 1u);
  EXPECT_EQ(V.Relas[0].Offset, 0x40u); // the bug made this 0
  ASSERT_LT(V.Relas[0].Sym, V.Syms.size());
  EXPECT_EQ(V.Syms[V.Relas[0].Sym].Name, std::string("ext"));
}

TEST(MergeELFSemantic, MergedSectionSizeIsPaddedSum) {
  // A higher-aligned second section forces real padding; the merged section
  // size and alignment must reflect it.
  SecSpec SA{".text.a", 0x34, 16};
  SecSpec SB{".text.b", 0x20, 32};
  auto Obj = buildSectionedELF({SA, SB}, {}, {});

  SmallVector<SmallVector<char, 0>, 1> Bufs;
  Bufs.push_back(std::move(Obj));
  Options Opts;
  Opts.mergeSections = true;
  auto [OK, Out] = mergeELF(Bufs, Opts);
  ASSERT_TRUE(OK);

  ElfView V = parseELF(Out);
  ASSERT_TRUE(V.Ok);
  int Idx = V.findSec(".text");
  ASSERT_GE(Idx, 0);
  // .text.a [0,0x34) padded to align 32 → 0x40, then + 0x20 = 0x60.
  EXPECT_EQ(V.Secs[Idx].Size, 0x60u);
  EXPECT_EQ(V.Secs[Idx].Align, 32u);
}

TEST(MergeELFSemantic, BssSectionsMergeByVirtualSize) {
  // NOBITS sections have no file content; offsets come from a running virtual
  // size, and the merged section must stay NOBITS with the summed size.
  SecSpec BA{".bss.a", 0x30, 16, ELF::SHT_NOBITS,
             ELF::SHF_ALLOC | ELF::SHF_WRITE};
  SecSpec BB{".bss.b", 0x10, 16, ELF::SHT_NOBITS,
             ELF::SHF_ALLOC | ELF::SHF_WRITE};
  SymSpec VA{"va", 0, 0};
  SymSpec VB{"vb", 1, 0};
  auto Obj = buildSectionedELF({BA, BB}, {VA, VB}, {});

  SmallVector<SmallVector<char, 0>, 1> Bufs;
  Bufs.push_back(std::move(Obj));
  Options Opts;
  Opts.mergeSections = true;
  auto [OK, Out] = mergeELF(Bufs, Opts);
  ASSERT_TRUE(OK);

  ElfView V = parseELF(Out);
  ASSERT_TRUE(V.Ok);
  int Idx = V.findSec(".bss");
  ASSERT_GE(Idx, 0);
  EXPECT_EQ(V.Secs[Idx].Type, (uint32_t)ELF::SHT_NOBITS);
  EXPECT_EQ(V.Secs[Idx].Size, 0x40u); // 0x30 + 0x10
  const ParsedSym *PVB = V.findSym("vb");
  ASSERT_NE(PVB, nullptr);
  EXPECT_EQ(PVB->Value, 0x30u); // shifted past .bss.a
}

TEST(MergeELFSemantic, PreservedSectionsNotMerged) {
  // Kernel-module mode: .text.* collapses to .text, but a preserved section
  // (e.g. .modinfo) keeps its name and is never folded away.
  SecSpec T{".text.foo", 0x20, 16};
  SecSpec Modinfo{".modinfo", 0x10, 8, ELF::SHT_PROGBITS, ELF::SHF_ALLOC};
  auto Obj = buildSectionedELF({T, Modinfo}, {}, {});

  SmallVector<SmallVector<char, 0>, 1> Bufs;
  Bufs.push_back(std::move(Obj));
  Options Opts;
  Opts.mergeSections = true;
  Opts.preservedSections.push_back(".modinfo");
  auto [OK, Out] = mergeELF(Bufs, Opts);
  ASSERT_TRUE(OK);

  ElfView V = parseELF(Out);
  ASSERT_TRUE(V.Ok);
  EXPECT_GE(V.findSec(".text"), 0);
  EXPECT_GE(V.findSec(".modinfo"), 0);
  EXPECT_LT(V.findSec(".text.foo"), 0);
}

TEST(MergeELFSemantic, GlobalSymbolDedupKeepsDefinition) {
  // Partition 0 defines "shared"; partition 1 references it (undefined) and
  // relocates against it.  The merged object must contain a single defined
  // "shared" and the relocation must resolve onto that defined slot.
  SecSpec S0{".text", 0x20, 16};
  SecSpec S1{".text", 0x20, 16};
  SymSpec Def{"shared", 0, 0};      // defined in partition 0
  SymSpec Undef{"shared", -1, 0};   // undefined in partition 1
  RelSpec R1{0, 0, "shared", ELF::R_X86_64_PLT32, 0};

  auto Obj0 = buildSectionedELF({S0}, {Def}, {});
  auto Obj1 = buildSectionedELF({S1}, {Undef}, {R1});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(std::move(Obj0));
  Bufs.push_back(std::move(Obj1));
  auto [OK, Out] = mergeELF(Bufs);
  ASSERT_TRUE(OK);

  ElfView V = parseELF(Out);
  ASSERT_TRUE(V.Ok);

  unsigned NShared = 0, NDefined = 0;
  for (auto &S : V.Syms)
    if (S.Name == "shared") {
      NShared++;
      if (S.Shndx != ELF::SHN_UNDEF)
        NDefined++;
    }
  EXPECT_EQ(NShared, 1u);  // deduped to a single entry
  EXPECT_EQ(NDefined, 1u); // and it is the definition, not the undef

  ASSERT_EQ(V.Relas.size(), 1u);
  ASSERT_LT(V.Relas[0].Sym, V.Syms.size());
  EXPECT_EQ(V.Syms[V.Relas[0].Sym].Name, std::string("shared"));
  EXPECT_NE(V.Syms[V.Relas[0].Sym].Shndx, ELF::SHN_UNDEF);
}

TEST(MergeELFSemantic, PcgSymbolResolvesToLocalDefinition) {
  // The parallel-codegen hot path: a module-local symbol is externalized with
  // the ".__pcg<hash>" suffix so a cross-partition reference resolves.  After
  // merge it must (1) dedup to a single entry, (2) be demoted back to LOCAL so
  // it never leaks into the final binary's exports, (3) keep its correct merged
  // offset, and (4) be the symbol the cross-partition relocation points at.
  const char *PcgName = "helper.__pcg12345678";
  SecSpec S0{".text", 0x40, 16};
  SecSpec S1{".text", 0x20, 16};
  SymSpec Def{PcgName, 0, 0x10, true};  // defined GLOBAL in partition 0
  SymSpec Ref{PcgName, -1, 0, true};    // undefined reference in partition 1
  RelSpec R1{0, 0, PcgName, ELF::R_X86_64_PLT32, 0};

  auto Obj0 = buildSectionedELF({S0}, {Def}, {});
  auto Obj1 = buildSectionedELF({S1}, {Ref}, {R1});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(std::move(Obj0));
  Bufs.push_back(std::move(Obj1));
  auto [OK, Out] = mergeELF(Bufs);
  ASSERT_TRUE(OK);

  ElfView V = parseELF(Out);
  ASSERT_TRUE(V.Ok);

  unsigned NPcg = 0;
  const ParsedSym *Pcg = nullptr;
  for (auto &S : V.Syms)
    if (S.Name == PcgName) {
      NPcg++;
      Pcg = &S;
    }
  ASSERT_EQ(NPcg, 1u);                       // deduped to one entry
  ASSERT_NE(Pcg, nullptr);
  EXPECT_EQ(Pcg->Bind, (uint8_t)ELF::STB_LOCAL); // demoted from GLOBAL
  EXPECT_NE(Pcg->Shndx, ELF::SHN_UNDEF);     // it is the definition
  EXPECT_EQ(Pcg->Value, 0x10u);              // partition 0 base 0 + 0x10

  ASSERT_EQ(V.Relas.size(), 1u);
  ASSERT_LT(V.Relas[0].Sym, V.Syms.size());
  EXPECT_EQ(V.Syms[V.Relas[0].Sym].Name, std::string(PcgName));
  EXPECT_EQ(V.Syms[V.Relas[0].Sym].Bind, (uint8_t)ELF::STB_LOCAL);
}

TEST(MergeELFSemantic, SectionContentBytesPlacedCorrectly) {
  // Symbol offsets can be right while the *content* bytes are copied to the
  // wrong place (or dropped).  Fill each input section with a distinct byte
  // pattern and verify it lands at the expected offset in the merged section.
  SecSpec SA{".text.a", 0x20, 16, ELF::SHT_PROGBITS,
             ELF::SHF_ALLOC | ELF::SHF_EXECINSTR, 0xAA};
  SecSpec SB{".text.b", 0x20, 16, ELF::SHT_PROGBITS,
             ELF::SHF_ALLOC | ELF::SHF_EXECINSTR, 0xBB};
  auto Obj0 = buildSectionedELF({SA, SB}, {}, {});

  SecSpec SC{".text.c", 0x10, 16, ELF::SHT_PROGBITS,
             ELF::SHF_ALLOC | ELF::SHF_EXECINSTR, 0xCC};
  auto Obj1 = buildSectionedELF({SC}, {}, {});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(std::move(Obj0));
  Bufs.push_back(std::move(Obj1));
  Options Opts;
  Opts.mergeSections = true;
  auto [OK, Out] = mergeELF(Bufs, Opts);
  ASSERT_TRUE(OK);

  ElfView V = parseELF(Out);
  ASSERT_TRUE(V.Ok);
  int Idx = V.findSec(".text");
  ASSERT_GE(Idx, 0);
  const auto &D = V.Secs[Idx].Data;
  // Layout: [0x00,0x20) = 0xAA, [0x20,0x40) = 0xBB, [0x40,0x50) = 0xCC.
  ASSERT_EQ(D.size(), 0x50u);
  for (unsigned i = 0x00; i < 0x20; ++i)
    ASSERT_EQ(D[i], 0xAA) << "offset " << i;
  for (unsigned i = 0x20; i < 0x40; ++i)
    ASSERT_EQ(D[i], 0xBB) << "offset " << i;
  for (unsigned i = 0x40; i < 0x50; ++i)
    ASSERT_EQ(D[i], 0xCC) << "offset " << i;
}

TEST(MergeELFSemantic, RandomizedLayoutOracle) {
  // Property test: generate random multi-partition, multi-section layouts and
  // independently predict every symbol's merged offset with a from-scratch
  // re-implementation of the concatenate-and-align algorithm.  Any divergence
  // in the merger's offset math (for *any* layout, not just hand-picked ones)
  // makes this fail.  This is the strongest guard against silent offset bugs.
  struct Group {
    const char *Name;
    uint32_t Type;
    uint64_t Flags;
  };
  const Group Groups[4] = {
      {".text", ELF::SHT_PROGBITS, ELF::SHF_ALLOC | ELF::SHF_EXECINSTR},
      {".data", ELF::SHT_PROGBITS, ELF::SHF_ALLOC | ELF::SHF_WRITE},
      {".rodata", ELF::SHT_PROGBITS, ELF::SHF_ALLOC},
      {".bss", ELF::SHT_NOBITS, ELF::SHF_ALLOC | ELF::SHF_WRITE},
  };

  std::mt19937 Rng(0xC0FFEEu);
  for (int Trial = 0; Trial < 300; ++Trial) {
    unsigned NP = 1 + (Rng() % 3);
    uint64_t CurSize[4] = {0, 0, 0, 0};
    uint32_t CurAlign[4] = {1, 1, 1, 1};
    std::vector<std::pair<std::string, uint64_t>> Expected;
    std::vector<uint64_t> ExpectedRelOffs;

    SmallVector<SmallVector<char, 0>, 4> Bufs;
    for (unsigned p = 0; p < NP; ++p) {
      std::vector<SecSpec> Secs;
      std::vector<SymSpec> Syms;
      std::vector<RelSpec> Rels;
      unsigned NS = 1 + (Rng() % 4);
      for (unsigned s = 0; s < NS; ++s) {
        unsigned g = Rng() % 4;
        uint32_t Align = 1u << (Rng() % 7); // 1..64, power of two
        uint64_t Size = 1 + (Rng() % 0x200);
        std::string Nm = std::string(Groups[g].Name) + "." +
                         std::to_string(p) + "_" + std::to_string(s);
        unsigned SecIdx = Secs.size();
        // Distinct non-zero fill per content section so the verifier's
        // content anchor is meaningful (a mis-shifted symbol would read a
        // different section's fill byte).
        uint8_t Fill = Groups[g].Type == ELF::SHT_NOBITS
                           ? 0
                           : (uint8_t)(1 + ((p * 7 + s * 3 + g) & 0x7e));
        Secs.push_back(SecSpec{Nm, Size, Align, Groups[g].Type,
                               Groups[g].Flags, Fill});

        // Oracle: mirror the merger's running-max-align + pad + append.
        if (Align > CurAlign[g])
          CurAlign[g] = Align;
        uint64_t Pad = (CurAlign[g] - (CurSize[g] % CurAlign[g])) % CurAlign[g];
        uint64_t Base = CurSize[g] + Pad;
        CurSize[g] = Base + Size;

        unsigned NSym = 1 + (Rng() % 3);
        for (unsigned k = 0; k < NSym; ++k) {
          uint64_t SOff = Rng() % Size;
          std::string SN = "s_" + std::to_string(p) + "_" +
                           std::to_string(s) + "_" + std::to_string(k);
          Syms.push_back(SymSpec{SN, (int)SecIdx, SOff, true});
          Expected.push_back({SN, Base + SOff});
        }

        // Optionally drop a relocation into this content-bearing section and
        // predict its merged offset, so reloc-offset collapse is caught too.
        if (Groups[g].Type != ELF::SHT_NOBITS && (Rng() % 2)) {
          uint64_t RO = Rng() % Size;
          std::string Anchor =
              "s_" + std::to_string(p) + "_" + std::to_string(s) + "_0";
          Rels.push_back(RelSpec{(int)SecIdx, RO, Anchor, ELF::R_X86_64_64, 0});
          ExpectedRelOffs.push_back(Base + RO);
        }
      }
      Bufs.push_back(buildSectionedELF(Secs, Syms, Rels));
    }

    Options Opts;
    Opts.mergeSections = true;
    auto [OK, Out] = mergeELF(Bufs, Opts);
    ASSERT_TRUE(OK) << "trial " << Trial;
    ElfView V = parseELF(Out);
    ASSERT_TRUE(V.Ok) << "trial " << Trial;
    for (auto &E : Expected) {
      const ParsedSym *PS = V.findSym(E.first);
      ASSERT_NE(PS, nullptr) << "trial " << Trial << " sym " << E.first;
      EXPECT_EQ(PS->Value, E.second)
          << "trial " << Trial << " sym " << E.first;
    }

    // Every relocation's merged offset must equal its independently predicted
    // base+offset; the historical bug collapsed these to (near) zero.
    std::vector<uint64_t> ActualRelOffs;
    for (auto &R : V.Relas)
      ActualRelOffs.push_back(R.Offset);
    std::sort(ExpectedRelOffs.begin(), ExpectedRelOffs.end());
    std::sort(ActualRelOffs.begin(), ActualRelOffs.end());
    ASSERT_EQ(ActualRelOffs.size(), ExpectedRelOffs.size()) << "trial " << Trial;
    for (size_t i = 0; i < ExpectedRelOffs.size(); ++i)
      EXPECT_EQ(ActualRelOffs[i], ExpectedRelOffs[i])
          << "trial " << Trial << " reloc " << i;

    // The independent verifier must accept every random valid layout (proves
    // it has no false positives across hundreds of shapes).
    std::string VErr;
    EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                            ArrayRef<char>(Out), Format::ELF64LE, Opts, &VErr))
        << "trial " << Trial << ": " << VErr;
  }
}

// ---------------------------------------------------------------------------
// Verifier tests — prove the independent post-merge self-check accepts a sound
// merge and *rejects* the exact corruption class that shipped before (symbol
// offset collapse), which the merger now refuses to emit.
// ---------------------------------------------------------------------------

TEST(MergeELFVerify, AcceptsGoodMergeRejectsCollapse) {
  using namespace ELF;
  // Distinct fill bytes so a mis-placed symbol reads the wrong section's code.
  SecSpec S0{".text", 0x20, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xAA};
  SecSpec S1{".text", 0x20, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xBB};
  SymSpec FA{"fa", 0, 0, true};
  SymSpec FB{"fb", 0, 0, true};
  auto O0 = buildSectionedELF({S0}, {FA}, {});
  auto O1 = buildSectionedELF({S1}, {FB}, {});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  auto [OK, Out] = mergeELF(Bufs); // internal verify must already pass
  ASSERT_TRUE(OK);

  std::string Err;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::ELF64LE, {}, &Err))
      << Err;

  // Sanity: fb really landed past partition 0's .text.
  {
    ElfView V = parseELF(Out);
    const ParsedSym *PFB = V.findSym("fb");
    ASSERT_NE(PFB, nullptr);
    EXPECT_EQ(PFB->Value, 0x20u);
  }

  // Collapse fb to offset 0 (the historical bug): its content window now reads
  // partition 0's 0xAA bytes instead of its own 0xBB → must be rejected.
  auto Collapsed = Out;
  ASSERT_TRUE(patchSymValue(Collapsed, "fb", 0x0));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Collapsed), Format::ELF64LE, {}, &Err))
      << "verifier accepted a collapsed symbol offset";

  // Out-of-bounds value → the other rejection path.
  auto OOB = Out;
  ASSERT_TRUE(patchSymValue(OOB, "fb", 0x9999));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(OOB), Format::ELF64LE, {}, &Err))
      << "verifier accepted an out-of-bounds symbol value";
}

TEST(MergeELFVerify, CatchesCollapsedRelocOffset) {
  using namespace ELF;
  // Each partition defines a function spanning its .text and relocates against
  // an undefined "ext" at offset 0x10.  After merge the relocations sit at
  // 0x10 (p0) and 0x50 (p1); collapsing them to 0 must be caught even though
  // the symbols themselves are still correct.
  SecSpec S0{".text", 0x40, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xAA};
  SecSpec S1{".text", 0x40, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xBB};
  SymSpec F0{"f0", 0, 0, true};
  SymSpec F1{"f1", 0, 0, true};
  SymSpec Ext{"ext", -1, 0, true};
  RelSpec R0{0, 0x10, "ext", R_X86_64_PLT32, 0};
  RelSpec R1{0, 0x10, "ext", R_X86_64_PLT32, 0};
  auto O0 = buildSectionedELF({S0}, {F0, Ext}, {R0});
  auto O1 = buildSectionedELF({S1}, {F1, Ext}, {R1});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  auto [OK, Out] = mergeELF(Bufs);
  ASSERT_TRUE(OK);
  std::string Err;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::ELF64LE, {}, &Err))
      << Err;

  ASSERT_TRUE(patchAllRelaOffsets(Out, 0x0));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Out), Format::ELF64LE, {}, &Err))
      << "verifier accepted collapsed relocation offsets";
}

// ---------------------------------------------------------------------------
// COFF semantic tests — same invariant on the Windows object path.
// ---------------------------------------------------------------------------

TEST(MergeCOFFSemantic, CrossPartitionSymbolAndRelocOffsets) {
  using namespace COFF;
  uint32_t TextChars = IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE |
                       IMAGE_SCN_MEM_READ | IMAGE_SCN_ALIGN_16BYTES;
  CoffSecSpec S0{".text", 0x40, TextChars};
  CoffSecSpec S1{".text", 0x20, TextChars};
  CoffSymSpec P0{"p0", 0, 1, IMAGE_SYM_CLASS_EXTERNAL};
  CoffSymSpec P1{"p1", 0, 1, IMAGE_SYM_CLASS_EXTERNAL};
  CoffSymSpec Ext{"ext", 0, 0, IMAGE_SYM_CLASS_EXTERNAL}; // undefined external
  CoffRelSpec R1{0, 0, "ext", (uint16_t)IMAGE_REL_AMD64_REL32};

  auto Obj0 = buildCOFF(IMAGE_FILE_MACHINE_AMD64, {S0}, {P0}, {});
  auto Obj1 = buildCOFF(IMAGE_FILE_MACHINE_AMD64, {S1}, {P1, Ext}, {R1});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(std::move(Obj0));
  Bufs.push_back(std::move(Obj1));
  SmallVector<char, 0> Out;
  raw_svector_ostream OS(Out);
  ASSERT_TRUE(mergeObjects(Bufs, OS, Format::COFF));

  CoffView V = parseCOFF(Out);
  ASSERT_TRUE(V.Ok);

  const CoffParsedSym *PP0 = V.findSym("p0");
  const CoffParsedSym *PP1 = V.findSym("p1");
  ASSERT_NE(PP0, nullptr);
  ASSERT_NE(PP1, nullptr);
  EXPECT_EQ(PP0->Value, 0x0u);
  EXPECT_EQ(PP1->Value, 0x40u); // shifted past partition 0's .text

  ASSERT_EQ(V.Rels.size(), 1u);
  EXPECT_EQ(V.Rels[0].VA, 0x40u); // relocation site moved too
  ASSERT_LT(V.Rels[0].SymIdx, V.Syms.size());
  EXPECT_EQ(V.Syms[V.Rels[0].SymIdx].Name, std::string("ext"));
}

TEST(MergeCOFFSemantic, BssSectionsMergeByVirtualSize) {
  using namespace COFF;
  uint32_t BssChars = IMAGE_SCN_CNT_UNINITIALIZED_DATA | IMAGE_SCN_MEM_READ |
                      IMAGE_SCN_MEM_WRITE | IMAGE_SCN_ALIGN_16BYTES;
  CoffSecSpec B0{".bss", 0x30, BssChars};
  CoffSecSpec B1{".bss", 0x10, BssChars};
  CoffSymSpec V0{"v0", 0, 1, IMAGE_SYM_CLASS_EXTERNAL};
  CoffSymSpec V1{"v1", 0, 1, IMAGE_SYM_CLASS_EXTERNAL};

  auto Obj0 = buildCOFF(IMAGE_FILE_MACHINE_AMD64, {B0}, {V0}, {});
  auto Obj1 = buildCOFF(IMAGE_FILE_MACHINE_AMD64, {B1}, {V1}, {});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(std::move(Obj0));
  Bufs.push_back(std::move(Obj1));
  SmallVector<char, 0> Out;
  raw_svector_ostream OS(Out);
  ASSERT_TRUE(mergeObjects(Bufs, OS, Format::COFF));

  CoffView V = parseCOFF(Out);
  ASSERT_TRUE(V.Ok);
  const CoffParsedSym *PV1 = V.findSym("v1");
  ASSERT_NE(PV1, nullptr);
  EXPECT_EQ(PV1->Value, 0x30u); // shifted past partition 0's .bss
}

TEST(MergeCOFFSemantic, RandomizedLayoutOracle) {
  // COFF analogue of MergeELFSemantic.RandomizedLayoutOracle: random
  // multi-partition layouts with an independent concatenate-and-align oracle
  // predicting every symbol's merged Value.  COFF folds sections by
  // (name, content-class) and tracks a running max alignment, so the oracle
  // mirrors exactly that.  Any divergence in the merger's offset math for any
  // shape — not just the hand-picked ones above — fails here.
  using namespace COFF;
  struct Group {
    const char *Name;
    uint32_t Chars;
    bool IsBSS;
  };
  const Group Groups[4] = {
      {".text", IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ,
       false},
      {".data",
       IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE,
       false},
      {".rdata", IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ, false},
      {".bss",
       IMAGE_SCN_CNT_UNINITIALIZED_DATA | IMAGE_SCN_MEM_READ |
           IMAGE_SCN_MEM_WRITE,
       true},
  };

  std::mt19937 Rng(0xB0BACAFEu);
  for (int Trial = 0; Trial < 300; ++Trial) {
    unsigned NP = 1 + (Rng() % 3);
    uint64_t CurSize[4] = {0, 0, 0, 0};
    uint32_t CurAlign[4] = {1, 1, 1, 1};
    std::vector<std::pair<std::string, uint64_t>> Expected;

    SmallVector<SmallVector<char, 0>, 4> Bufs;
    for (unsigned p = 0; p < NP; ++p) {
      std::vector<CoffSecSpec> Secs;
      std::vector<CoffSymSpec> Syms;
      for (unsigned g = 0; g < 4; ++g) {
        if (Rng() % 3 == 0)
          continue; // a partition may lack a group
        unsigned AlignExp = Rng() % 7; // 1..64
        uint32_t Align = 1u << AlignExp;
        uint64_t Size = 1 + (Rng() % 0x200);
        uint32_t Chars = Groups[g].Chars | ((AlignExp + 1) << 20);
        unsigned SecIdx = Secs.size();
        uint8_t Fill =
            Groups[g].IsBSS ? 0 : (uint8_t)(1 + ((p * 7 + g * 3) & 0x7e));
        Secs.push_back(CoffSecSpec{Groups[g].Name, (uint32_t)Size, Chars, Fill});

        if (Align > CurAlign[g])
          CurAlign[g] = Align;
        uint64_t Pad = (CurAlign[g] - (CurSize[g] % CurAlign[g])) % CurAlign[g];
        uint64_t Base = CurSize[g] + Pad;
        CurSize[g] = Base + Size;

        unsigned NSym = 1 + (Rng() % 3);
        for (unsigned k = 0; k < NSym; ++k) {
          uint64_t SOff = Rng() % Size;
          std::string SN = "s_" + std::to_string(p) + "_" + std::to_string(g) +
                           "_" + std::to_string(k);
          Syms.push_back(CoffSymSpec{SN, (uint32_t)SOff, (int16_t)(1 + SecIdx),
                                     IMAGE_SYM_CLASS_EXTERNAL});
          Expected.push_back({SN, Base + SOff});
        }
      }
      Bufs.push_back(buildCOFF(IMAGE_FILE_MACHINE_AMD64, Secs, Syms, {}));
    }

    SmallVector<char, 0> Out;
    raw_svector_ostream OS(Out);
    ASSERT_TRUE(mergeObjects(Bufs, OS, Format::COFF)) << "trial " << Trial;
    CoffView V = parseCOFF(Out);
    ASSERT_TRUE(V.Ok) << "trial " << Trial;
    for (auto &E : Expected) {
      const CoffParsedSym *PS = V.findSym(E.first);
      ASSERT_NE(PS, nullptr) << "trial " << Trial << " sym " << E.first;
      EXPECT_EQ(PS->Value, E.second) << "trial " << Trial << " sym " << E.first;
    }
    std::string VErr;
    EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                            ArrayRef<char>(Out), Format::COFF, {}, &VErr))
        << "trial " << Trial << ": " << VErr;
  }
}

TEST(MergeCOFFVerify, AcceptsGoodMergeRejectsCollapse) {
  using namespace COFF;
  uint32_t TextChars = IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE |
                       IMAGE_SCN_MEM_READ | IMAGE_SCN_ALIGN_16BYTES;
  CoffSecSpec S0{".text", 0x40, TextChars, 0xAA};
  CoffSecSpec S1{".text", 0x20, TextChars, 0xBB};
  CoffSymSpec FA{"fa", 0, 1, IMAGE_SYM_CLASS_EXTERNAL};
  CoffSymSpec FB{"fb", 0, 1, IMAGE_SYM_CLASS_EXTERNAL};
  auto O0 = buildCOFF(IMAGE_FILE_MACHINE_AMD64, {S0}, {FA}, {});
  auto O1 = buildCOFF(IMAGE_FILE_MACHINE_AMD64, {S1}, {FB}, {});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  SmallVector<char, 0> Out;
  raw_svector_ostream OS(Out);
  ASSERT_TRUE(mergeObjects(Bufs, OS, Format::COFF)); // internal verify passes

  std::string Err;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::COFF, {}, &Err))
      << Err;
  {
    CoffView V = parseCOFF(Out);
    const CoffParsedSym *PFB = V.findSym("fb");
    ASSERT_NE(PFB, nullptr);
    EXPECT_EQ(PFB->Value, 0x40u);
  }

  // Collapse fb to 0: content at 0 is partition 0's 0xAA, not fb's 0xBB.
  auto Collapsed = Out;
  ASSERT_TRUE(patchCoffSymValue(Collapsed, "fb", 0x0));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Collapsed), Format::COFF, {}, &Err))
      << "COFF verifier accepted a collapsed symbol offset";

  auto OOB = Out;
  ASSERT_TRUE(patchCoffSymValue(OOB, "fb", 0x9999));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(OOB), Format::COFF, {}, &Err))
      << "COFF verifier accepted an out-of-bounds symbol value";
}

TEST(MergeCOFFVerify, CatchesCollapsedRelocOffset) {
  using namespace COFF;
  uint32_t TextChars = IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE |
                       IMAGE_SCN_MEM_READ | IMAGE_SCN_ALIGN_16BYTES;
  // Each partition: a function spanning its .text and a relocation against the
  // undefined "ext" at offset 0x10.  After merge the relocs sit at 0x10 (p0)
  // and 0x50 (p1, shifted past p0's 0x40 .text); collapsing them to 0 must be
  // caught even though the symbols themselves stay correct.
  CoffSecSpec S0{".text", 0x40, TextChars, 0xAA};
  CoffSecSpec S1{".text", 0x40, TextChars, 0xBB};
  CoffSymSpec F0{"f0", 0, 1, IMAGE_SYM_CLASS_EXTERNAL};
  CoffSymSpec F1{"f1", 0, 1, IMAGE_SYM_CLASS_EXTERNAL};
  CoffSymSpec Ext{"ext", 0, 0, IMAGE_SYM_CLASS_EXTERNAL};
  CoffRelSpec R0{0, 0x10, "ext", (uint16_t)IMAGE_REL_AMD64_REL32};
  CoffRelSpec R1{0, 0x10, "ext", (uint16_t)IMAGE_REL_AMD64_REL32};
  auto O0 = buildCOFF(IMAGE_FILE_MACHINE_AMD64, {S0}, {F0, Ext}, {R0});
  auto O1 = buildCOFF(IMAGE_FILE_MACHINE_AMD64, {S1}, {F1, Ext}, {R1});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  SmallVector<char, 0> Out;
  raw_svector_ostream OS(Out);
  ASSERT_TRUE(mergeObjects(Bufs, OS, Format::COFF));
  std::string Err;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::COFF, {}, &Err))
      << Err;

  ASSERT_TRUE(patchAllCoffRelocVAs(Out, 0x0));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Out), Format::COFF, {}, &Err))
      << "COFF verifier accepted collapsed relocation offsets";
}

// ---------------------------------------------------------------------------
// Mach-O semantic tests — same invariant on the Darwin object path, where the
// symbol n_value fix-up (section-relative → segment-relative) lived.
// ---------------------------------------------------------------------------

TEST(MergeMachOSemantic, CrossPartitionSymbolOffsets) {
  namespace MO = llvm::MachO;
  uint32_t TextFlags = MO::S_ATTR_PURE_INSTRUCTIONS | MO::S_ATTR_SOME_INSTRUCTIONS;
  MachoSecSpec S0{"__TEXT", "__text", 0x40, 4, TextFlags};
  MachoSecSpec S1{"__TEXT", "__text", 0x20, 4, TextFlags};
  uint8_t DefExt = MO::N_SECT | MO::N_EXT;
  MachoSymSpec P0{"_p0", DefExt, 1, 0, 0};
  MachoSymSpec P1{"_p1", DefExt, 1, 0, 0};

  auto Obj0 =
      buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {S0}, {P0});
  auto Obj1 =
      buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {S1}, {P1});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(std::move(Obj0));
  Bufs.push_back(std::move(Obj1));
  SmallVector<char, 0> Out;
  raw_svector_ostream OS(Out);
  ASSERT_TRUE(mergeObjects(Bufs, OS, Format::MachO64));

  MachoView V = parseMachO(Out);
  ASSERT_TRUE(V.Ok);
  const MachoParsedSec *Text = V.findSec("__TEXT", "__text");
  ASSERT_NE(Text, nullptr);
  const MachoParsedSym *PP0 = V.findSym("_p0");
  const MachoParsedSym *PP1 = V.findSym("_p1");
  ASSERT_NE(PP0, nullptr);
  ASSERT_NE(PP1, nullptr);
  // n_value is segment-relative in the merged object; subtracting the merged
  // section address recovers the section-relative offset.
  EXPECT_EQ(PP0->Value - Text->Addr, 0x0u);
  EXPECT_EQ(PP1->Value - Text->Addr, 0x40u); // shifted past partition 0
}

TEST(MergeMachOSemantic, ZerofillSectionsMergeByVirtualSize) {
  namespace MO = llvm::MachO;
  MachoSecSpec B0{"__DATA", "__bss", 0x30, 4, MO::S_ZEROFILL};
  MachoSecSpec B1{"__DATA", "__bss", 0x10, 4, MO::S_ZEROFILL};
  uint8_t DefExt = MO::N_SECT | MO::N_EXT;
  MachoSymSpec V0{"_v0", DefExt, 1, 0, 0};
  MachoSymSpec V1{"_v1", DefExt, 1, 0, 0};

  auto Obj0 =
      buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {B0}, {V0});
  auto Obj1 =
      buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {B1}, {V1});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(std::move(Obj0));
  Bufs.push_back(std::move(Obj1));
  SmallVector<char, 0> Out;
  raw_svector_ostream OS(Out);
  ASSERT_TRUE(mergeObjects(Bufs, OS, Format::MachO64));

  MachoView V = parseMachO(Out);
  ASSERT_TRUE(V.Ok);
  const MachoParsedSec *Bss = V.findSec("__DATA", "__bss");
  ASSERT_NE(Bss, nullptr);
  const MachoParsedSym *PV1 = V.findSym("_v1");
  ASSERT_NE(PV1, nullptr);
  EXPECT_EQ(PV1->Value - Bss->Addr, 0x30u); // shifted past partition 0's zerofill
}

TEST(MergeMachOSemantic, RandomizedLayoutOracle) {
  // Mach-O analogue of the ELF/COFF randomized oracle.  Mach-O folds by
  // (segment, section) and tracks a running max align *exponent*; n_value in
  // the merged object is segment-relative, so the check subtracts the merged
  // section address to recover the predicted section-relative offset.
  namespace MO = llvm::MachO;
  struct Group {
    const char *Seg;
    const char *Sect;
    uint32_t Flags;
    bool IsZf;
  };
  const Group Groups[4] = {
      {"__TEXT", "__text",
       MO::S_ATTR_PURE_INSTRUCTIONS | MO::S_ATTR_SOME_INSTRUCTIONS, false},
      {"__DATA", "__data", 0, false},
      {"__TEXT", "__const", 0, false},
      {"__DATA", "__bss", MO::S_ZEROFILL, true},
  };
  struct ExpSym {
    std::string Name;
    unsigned Group;
    uint64_t Rel;
  };

  std::mt19937 Rng(0xD00DFEEDu);
  for (int Trial = 0; Trial < 300; ++Trial) {
    unsigned NP = 1 + (Rng() % 3);
    uint64_t CurSize[4] = {0, 0, 0, 0};
    uint32_t CurAlignExp[4] = {0, 0, 0, 0};
    std::vector<ExpSym> Expected;

    SmallVector<SmallVector<char, 0>, 4> Bufs;
    for (unsigned p = 0; p < NP; ++p) {
      std::vector<MachoSecSpec> Secs;
      std::vector<MachoSymSpec> Syms;
      for (unsigned g = 0; g < 4; ++g) {
        if (Rng() % 3 == 0)
          continue;
        uint32_t AlignExp = Rng() % 7;
        uint64_t Size = 1 + (Rng() % 0x200);
        unsigned SecIdx = Secs.size();
        uint8_t Fill =
            Groups[g].IsZf ? 0 : (uint8_t)(1 + ((p * 7 + g * 3) & 0x7e));
        Secs.push_back(MachoSecSpec{Groups[g].Seg, Groups[g].Sect, Size,
                                    AlignExp, Groups[g].Flags, Fill});

        if (AlignExp > CurAlignExp[g])
          CurAlignExp[g] = AlignExp;
        uint64_t A = 1ull << CurAlignExp[g];
        uint64_t Pad = (A - (CurSize[g] % A)) % A;
        uint64_t Base = CurSize[g] + Pad;
        CurSize[g] = Base + Size;

        unsigned NSym = 1 + (Rng() % 3);
        for (unsigned k = 0; k < NSym; ++k) {
          uint64_t SOff = Rng() % Size;
          std::string SN = "_s_" + std::to_string(p) + "_" +
                           std::to_string(g) + "_" + std::to_string(k);
          Syms.push_back(MachoSymSpec{SN, (uint8_t)(MO::N_SECT | MO::N_EXT),
                                      (uint8_t)(1 + SecIdx), SOff, 0});
          Expected.push_back({SN, g, Base + SOff});
        }
      }
      Bufs.push_back(buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL,
                                Secs, Syms));
    }

    SmallVector<char, 0> Out;
    raw_svector_ostream OS(Out);
    ASSERT_TRUE(mergeObjects(Bufs, OS, Format::MachO64)) << "trial " << Trial;
    MachoView V = parseMachO(Out);
    ASSERT_TRUE(V.Ok) << "trial " << Trial;
    for (auto &E : Expected) {
      const MachoParsedSym *PS = V.findSym(E.Name);
      ASSERT_NE(PS, nullptr) << "trial " << Trial << " sym " << E.Name;
      const MachoParsedSec *Sec =
          V.findSec(Groups[E.Group].Seg, Groups[E.Group].Sect);
      ASSERT_NE(Sec, nullptr) << "trial " << Trial << " sec for " << E.Name;
      EXPECT_EQ(PS->Value - Sec->Addr, E.Rel)
          << "trial " << Trial << " sym " << E.Name;
    }
    std::string VErr;
    EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                            ArrayRef<char>(Out), Format::MachO64, {}, &VErr))
        << "trial " << Trial << ": " << VErr;
  }
}

TEST(MergeMachOVerify, AcceptsGoodMergeRejectsCollapse) {
  namespace MO = llvm::MachO;
  uint32_t TextFlags =
      MO::S_ATTR_PURE_INSTRUCTIONS | MO::S_ATTR_SOME_INSTRUCTIONS;
  MachoSecSpec S0{"__TEXT", "__text", 0x40, 4, TextFlags, 0xAA};
  MachoSecSpec S1{"__TEXT", "__text", 0x20, 4, TextFlags, 0xBB};
  uint8_t DefExt = MO::N_SECT | MO::N_EXT;
  MachoSymSpec FA{"_fa", DefExt, 1, 0, 0};
  MachoSymSpec FB{"_fb", DefExt, 1, 0, 0};
  auto O0 =
      buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {S0}, {FA});
  auto O1 =
      buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {S1}, {FB});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  SmallVector<char, 0> Out;
  raw_svector_ostream OS(Out);
  ASSERT_TRUE(mergeObjects(Bufs, OS, Format::MachO64)); // internal verify passes

  std::string Err;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::MachO64, {}, &Err))
      << Err;

  MachoView V = parseMachO(Out);
  const MachoParsedSym *PFA = V.findSym("_fa");
  const MachoParsedSym *PFB = V.findSym("_fb");
  ASSERT_NE(PFA, nullptr);
  ASSERT_NE(PFB, nullptr);
  EXPECT_EQ(PFB->Value - PFA->Value, 0x40u);

  // Collapse _fb onto _fa's location: its content window now reads 0xAA, not
  // its own 0xBB → must be rejected.
  auto Collapsed = Out;
  ASSERT_TRUE(patchMachoSymValue(Collapsed, "_fb", PFA->Value));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Collapsed), Format::MachO64, {}, &Err))
      << "Mach-O verifier accepted a collapsed symbol offset";

  // Past the end of the merged section.
  auto OOB = Out;
  ASSERT_TRUE(patchMachoSymValue(OOB, "_fb", PFA->Value + 0x9999));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(OOB), Format::MachO64, {}, &Err))
      << "Mach-O verifier accepted an out-of-bounds symbol value";
}

TEST(MergeMachOVerify, CatchesCollapsedRelocOffset) {
  namespace MO = llvm::MachO;
  uint32_t TextFlags =
      MO::S_ATTR_PURE_INSTRUCTIONS | MO::S_ATTR_SOME_INSTRUCTIONS;
  // Each partition: a function spanning its __text and an extern relocation
  // against undefined "_ext" at section offset 0x10.  After merge the relocs
  // sit at 0x10 (p0) and 0x50 (p1, past p0's 0x40 __text); collapsing them to
  // 0 must be caught even though the symbols stay correct.
  MachoSecSpec S0{"__TEXT", "__text", 0x40, 4, TextFlags, 0xAA};
  MachoSecSpec S1{"__TEXT", "__text", 0x40, 4, TextFlags, 0xBB};
  uint8_t DefExt = MO::N_SECT | MO::N_EXT;
  uint8_t UndefExt = MO::N_EXT; // N_UNDF (0) | N_EXT
  MachoSymSpec F0{"_f0", DefExt, 1, 0, 0};
  MachoSymSpec F1{"_f1", DefExt, 1, 0, 0};
  MachoSymSpec Ext{"_ext", UndefExt, 0, 0, 0};
  MachoRelSpec R0{0, 0x10, "_ext", (uint8_t)MO::ARM64_RELOC_BRANCH26, 2};
  MachoRelSpec R1{0, 0x10, "_ext", (uint8_t)MO::ARM64_RELOC_BRANCH26, 2};
  auto O0 = buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {S0},
                       {F0, Ext}, {R0});
  auto O1 = buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {S1},
                       {F1, Ext}, {R1});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  SmallVector<char, 0> Out;
  raw_svector_ostream OS(Out);
  ASSERT_TRUE(mergeObjects(Bufs, OS, Format::MachO64));
  std::string Err;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::MachO64, {}, &Err))
      << Err;

  ASSERT_TRUE(patchAllMachoRelocAddrs(Out, 0x0));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Out), Format::MachO64, {}, &Err))
      << "Mach-O verifier accepted collapsed relocation offsets";
}

// ---------------------------------------------------------------------------
// Fuzz-style: random corruption of valid objects
// ---------------------------------------------------------------------------

TEST(MergeELF, FuzzCorruptedHeaders) {
  // Take a valid ELF, corrupt random bytes, verify no crash
  std::mt19937 Rng(42);
  auto Base = buildMinimalELF({"func"}, {"ext"});

  for (int Trial = 0; Trial < 200; ++Trial) {
    auto Copy = Base;
    unsigned NumCorruptions = 1 + (Rng() % 5);
    for (unsigned c = 0; c < NumCorruptions; ++c) {
      unsigned Pos = Rng() % Copy.size();
      Copy[Pos] = Rng() & 0xFF;
    }
    SmallVector<SmallVector<char, 0>, 2> Bufs;
    Bufs.push_back(std::move(Copy));
    SmallVector<char, 0> Out;
    raw_svector_ostream OS(Out);
    mergeELF64LEObjects(Bufs, OS); // must not crash
  }
}

TEST(MergeELF, FuzzRandomGarbage) {
  // Completely random buffers — parser should reject, not crash
  std::mt19937 Rng(1337);
  for (int Trial = 0; Trial < 100; ++Trial) {
    SmallVector<char, 0> Garbage;
    unsigned Sz = 64 + (Rng() % 4096);
    Garbage.resize(Sz);
    for (unsigned i = 0; i < Sz; ++i)
      Garbage[i] = Rng() & 0xFF;

    SmallVector<SmallVector<char, 0>, 2> Bufs;
    Bufs.push_back(std::move(Garbage));
    SmallVector<char, 0> Out;
    raw_svector_ostream OS(Out);
    mergeELF64LEObjects(Bufs, OS); // must not crash
  }
}

TEST(MergeELF, FuzzTwoCorruptedPartitions) {
  std::mt19937 Rng(9999);
  auto Base0 = buildMinimalELF({"a", "b"}, {"c"});
  auto Base1 = buildMinimalELF({"c"}, {"a", "b"});

  for (int Trial = 0; Trial < 100; ++Trial) {
    auto C0 = Base0, C1 = Base1;
    // Corrupt one of the two
    auto &Target = (Rng() % 2 == 0) ? C0 : C1;
    unsigned Pos = Rng() % Target.size();
    Target[Pos] = Rng() & 0xFF;

    SmallVector<SmallVector<char, 0>, 2> Bufs;
    Bufs.push_back(std::move(C0));
    Bufs.push_back(std::move(C1));
    SmallVector<char, 0> Out;
    raw_svector_ostream OS(Out);
    mergeELF64LEObjects(Bufs, OS); // must not crash
  }
}

// ---------------------------------------------------------------------------
// Edge-case tests: dispatch layer
// ---------------------------------------------------------------------------

TEST(MergeDispatch, InvalidFormatNoCrash) {
  auto Elf = buildMinimalELF({"x"}, {});
  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(std::move(Elf));

  {
    SmallVector<char, 0> Out;
    raw_svector_ostream OS(Out);
    // Feed an ELF to the MachO merger — should return false, not crash
    bool OK = mergeObjects(Bufs, OS, Format::MachO64);
    EXPECT_FALSE(OK);
  }
  {
    SmallVector<char, 0> Out;
    raw_svector_ostream OS(Out);
    // Feed an ELF to the COFF merger — should return false, not crash
    bool OK = mergeObjects(Bufs, OS, Format::COFF);
    EXPECT_FALSE(OK);
  }
}

TEST(MergeDispatch, EmptyInputAllFormats) {
  SmallVector<SmallVector<char, 0>, 2> Bufs;
  for (auto Fmt : {Format::ELF64LE, Format::MachO64, Format::COFF}) {
    SmallVector<char, 0> Out;
    raw_svector_ostream OS(Out);
    mergeObjects(Bufs, OS, Fmt); // must not crash
  }
}

// ---------------------------------------------------------------------------
// Fuzz-style: random garbage to all three merger formats
// ---------------------------------------------------------------------------

TEST(MergeFuzz, AllFormatsRandomGarbage) {
  std::mt19937 Rng(0xDEAD);
  for (auto Fmt : {Format::ELF64LE, Format::MachO64, Format::COFF}) {
    for (int Trial = 0; Trial < 50; ++Trial) {
      SmallVector<char, 0> Garbage;
      unsigned Sz = 128 + (Rng() % 2048);
      Garbage.resize(Sz);
      for (unsigned i = 0; i < Sz; ++i)
        Garbage[i] = Rng() & 0xFF;

      SmallVector<SmallVector<char, 0>, 2> Bufs;
      Bufs.push_back(std::move(Garbage));
      SmallVector<char, 0> Out;
      raw_svector_ostream OS(Out);
      mergeObjects(Bufs, OS, Fmt); // must not crash
    }
  }
}

TEST(MergeFuzz, AllFormatsMultipleRandomGarbage) {
  std::mt19937 Rng(0xBEEF);
  for (auto Fmt : {Format::ELF64LE, Format::MachO64, Format::COFF}) {
    for (int Trial = 0; Trial < 30; ++Trial) {
      unsigned NumBufs = 2 + (Rng() % 4);
      SmallVector<SmallVector<char, 0>, 8> Bufs;
      for (unsigned b = 0; b < NumBufs; ++b) {
        SmallVector<char, 0> G;
        unsigned Sz = 64 + (Rng() % 1024);
        G.resize(Sz);
        for (unsigned i = 0; i < Sz; ++i)
          G[i] = Rng() & 0xFF;
        Bufs.push_back(std::move(G));
      }
      SmallVector<char, 0> Out;
      raw_svector_ostream OS(Out);
      mergeObjects(Bufs, OS, Fmt); // must not crash
    }
  }
}
