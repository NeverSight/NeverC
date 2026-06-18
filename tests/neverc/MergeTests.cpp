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

// Used only by the NEVERC_BINARY-gated differential suite at end of file, but
// harmless to include unconditionally.
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"

#include <gtest/gtest.h>

#include <optional>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
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
  int Link = -1;    // >=0: sh_link to that 0-based user section (SHF_LINK_ORDER)
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
  std::string SymName; // symbol referenced (by name); ignored if TargetSecSym>=0
  uint32_t Type = ELF::R_X86_64_64;
  int64_t Addend = 0;
  // When >= 0, the relocation targets the STT_SECTION symbol of that 0-based
  // section instead of a named symbol — i.e. a "section base + addend"
  // (section-relative) relocation, the kind LLVM emits for local .rodata/jump
  // tables.  The builder auto-creates the section symbol and routes the reloc.
  int TargetSecSym = -1;
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
  // Section symbols (STT_SECTION, empty name, value 0) for any section targeted
  // by a section-relative relocation.  They are local, so emitted before the
  // named locals/globals; record their index for the reloc table below.
  SmallVector<int, 8> SecSymIndex(K, -1);
  for (auto &R : Rels)
    if (R.TargetSecSym >= 0 && (unsigned)R.TargetSecSym < K &&
        SecSymIndex[R.TargetSecSym] < 0) {
      unsigned Si = R.TargetSecSym;
      Sym E;
      memset(&E, 0, sizeof(E));
      E.st_name = 0; // STT_SECTION symbols are nameless
      E.st_shndx = 1 + Si;
      E.st_value = 0;
      E.st_info = (STB_LOCAL << 4) | STT_SECTION;
      SecSymIndex[Si] = (int)OutSyms.size();
      OutSyms.push_back(E);
    }
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
    unsigned SymIdx;
    if (R.TargetSecSym >= 0 && (unsigned)R.TargetSecSym < K &&
        SecSymIndex[R.TargetSecSym] >= 0) {
      SymIdx = (unsigned)SecSymIndex[R.TargetSecSym];
    } else {
      auto It = SymIndex.find(R.SymName);
      SymIdx = It != SymIndex.end() ? It->second : 0;
    }
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
    if (Secs[i].Link >= 0 && (unsigned)Secs[i].Link < K)
      S.sh_link = 1 + Secs[i].Link; // +1 for the leading null section
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
  uint64_t Flags = 0;
  uint64_t Size = 0;
  uint64_t Align = 0;
  uint32_t Link = 0;
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
    PS.Flags = Secs[i].sh_flags;
    PS.Size = Secs[i].sh_size;
    PS.Align = Secs[i].sh_addralign;
    PS.Link = Secs[i].sh_link;
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

/// Overwrite *every* symbol named Name (not just the first) — the faithful
/// shape of the historical bug, which collapsed all symbol values at once.
/// patchSymValue stops at the first match, so it cannot reproduce a collapse of
/// duplicate-named symbols (two file-local statics that share a name); this can.
bool patchAllSymValues(SmallVectorImpl<char> &Buf, StringRef Name,
                       uint64_t NewVal) {
  using namespace ELF;
  if (Buf.size() < sizeof(Elf64_Ehdr))
    return false;
  auto *H = reinterpret_cast<Elf64_Ehdr *>(Buf.data());
  if (H->e_shoff + (uint64_t)H->e_shnum * sizeof(Elf64_Shdr) > Buf.size())
    return false;
  auto *Secs = reinterpret_cast<Elf64_Shdr *>(Buf.data() + H->e_shoff);
  bool Any = false;
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
        Any = true;
      }
  }
  return Any;
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

/// Force every relocation's r_addend to NewVal — simulates an addend being
/// corrupted while the site offset stays correct (the reloc points at the
/// right slot but resolves to the wrong target address).
bool patchAllRelaAddends(SmallVectorImpl<char> &Buf, int64_t NewVal) {
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
      R[k].r_addend = NewVal;
      Any = true;
    }
  }
  return Any;
}

// Rewrite the sh_info (first-global boundary) of the first SHT_SYMTAB section
// in an ELF, to exercise the verifier's symbol-ordering invariant.
bool patchElfSymtabShInfo(SmallVectorImpl<char> &Buf, uint32_t NewInfo) {
  using namespace ELF;
  if (Buf.size() < sizeof(Elf64_Ehdr))
    return false;
  auto *H = reinterpret_cast<Elf64_Ehdr *>(Buf.data());
  if (H->e_shoff + (uint64_t)H->e_shnum * sizeof(Elf64_Shdr) > Buf.size())
    return false;
  auto *Secs = reinterpret_cast<Elf64_Shdr *>(Buf.data() + H->e_shoff);
  for (unsigned i = 0; i < H->e_shnum; ++i)
    if (Secs[i].sh_type == SHT_SYMTAB) {
      Secs[i].sh_info = NewInfo;
      return true;
    }
  return false;
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
  // >=0 marks a WeakExternal symbol that carries one coff_aux_weak_external
  // record whose TagIndex names Syms[WeakDefTag]'s on-disk slot.  -1 (the
  // default) is an ordinary symbol with no aux, so existing call sites keep
  // emitting exactly one slot per symbol and stay byte-identical.
  int WeakDefTag = -1;
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

  // On-disk symbol slots: a weak external occupies two (itself + one aux
  // record), every other symbol occupies one.  Relocations and weak-aux
  // TagIndex fields reference these slots, not the CoffSymSpec array index, so
  // map names to slots.  With no weak externals SlotOf[i]==i, so ordinary
  // call sites are unchanged.
  SmallVector<unsigned, 16> SlotOf(M);
  unsigned TotalSlots = 0;
  for (unsigned i = 0; i < M; ++i) {
    SlotOf[i] = TotalSlots;
    TotalSlots += (Syms[i].WeakDefTag >= 0) ? 2 : 1;
  }

  StringMap<unsigned> SymIndex;
  for (unsigned i = 0; i < M; ++i)
    SymIndex[Syms[i].Name] = SlotOf[i];

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
  Off += TotalSlots * 18;
  // String table (just the mandatory 4-byte length).

  SmallVector<char, 0> Buf;
  putU16(Buf, Machine);
  putU16(Buf, (uint16_t)N);
  putU32(Buf, 0);      // TimeDateStamp
  putU32(Buf, SymPtr);     // PointerToSymbolTable
  putU32(Buf, TotalSlots); // NumberOfSymbols (includes aux records)
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

  for (unsigned i = 0; i < M; ++i) {
    const auto &S = Syms[i];
    char Name[8] = {0};
    memcpy(Name, S.Name.data(), std::min<size_t>(S.Name.size(), 8));
    Buf.append(Name, Name + 8);
    putU32(Buf, S.Value);
    putU16(Buf, (uint16_t)S.SectionNumber);
    putU16(Buf, 0);               // Type
    Buf.push_back((char)S.StorageClass);
    bool IsWeak = S.WeakDefTag >= 0;
    Buf.push_back(IsWeak ? 1 : 0); // NumberOfAuxSymbols
    if (IsWeak) {
      // coff_aux_weak_external (18 bytes): TagIndex(4), Characteristics(4),
      // Unused(10).  TagIndex is the default definition's on-disk slot.
      uint32_t Tag =
          (unsigned)S.WeakDefTag < M ? SlotOf[(unsigned)S.WeakDefTag] : 0u;
      putU32(Buf, Tag);
      putU32(Buf, (uint32_t)IMAGE_WEAK_EXTERN_SEARCH_ALIAS);
      Buf.append(10, (char)0);
    }
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

/// Overwrite *every* COFF symbol named Name (patchCoffSymValue stops at the
/// first), so a collapse of duplicate-named statics can be simulated.
bool patchAllCoffSymValues(SmallVectorImpl<char> &Buf, StringRef Name,
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
  bool Any = false;
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
      Any = true;
    }
    k += 1u + NAux;
  }
  return Any;
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
  std::string SymName; // target symbol (extern relocation); ignored if !Extern
  uint8_t Type;
  uint8_t Length;      // log2 byte size (2 => 4 bytes, 3 => 8 bytes)
  bool Extern = true;  // false => section-relative (non-extern) relocation
  int TargetSec = -1;  // 0-based target section when !Extern (r_symbolnum=sec+1)
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
        char *P = Buf.data() + RelOff[i] + r * 8;
        uint32_t Addr = R.Address;
        memcpy(P, &Addr, 4);
        uint32_t SymOrSec, ExtBit;
        if (R.Extern) {
          unsigned Sym = 0;
          auto It = SymIdx.find(R.SymName);
          if (It != SymIdx.end())
            Sym = It->second;
          SymOrSec = Sym & 0xFFFFFFu;
          ExtBit = 1u << 27;
        } else {
          // Non-extern: r_symbolnum is a 1-based section number, extern bit 0.
          SymOrSec =
              (uint32_t)((R.TargetSec >= 0 ? R.TargetSec + 1 : 0) & 0xFFFFFFu);
          ExtBit = 0u;
        }
        uint32_t W = SymOrSec | (((uint32_t)R.Length & 0x3u) << 25) | ExtBit |
                     ((uint32_t)R.Type << 28);
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

/// Overwrite *every* Mach-O symbol named Name (patchMachoSymValue stops at the
/// first), so a collapse of duplicate-named local symbols can be simulated.
bool patchAllMachoSymValues(SmallVectorImpl<char> &Buf, StringRef Name,
                            uint64_t NewVal) {
  namespace MO = llvm::MachO;
  if (Buf.size() < sizeof(MO::mach_header_64))
    return false;
  auto *MH = reinterpret_cast<MO::mach_header_64 *>(Buf.data());
  uint64_t Cmd = sizeof(MO::mach_header_64);
  bool Any = false;
  for (unsigned c = 0; c < MH->ncmds; ++c) {
    if (Cmd + sizeof(MO::load_command) > Buf.size())
      return Any;
    auto *LC = reinterpret_cast<MO::load_command *>(Buf.data() + Cmd);
    if (LC->cmd == MO::LC_SYMTAB) {
      auto *SC = reinterpret_cast<MO::symtab_command *>(Buf.data() + Cmd);
      const char *Str = Buf.data() + SC->stroff;
      for (unsigned i = 0; i < SC->nsyms; ++i) {
        auto *NL = reinterpret_cast<MO::nlist_64 *>(
            Buf.data() + SC->symoff + i * sizeof(MO::nlist_64));
        if (NL->n_strx < SC->strsize && Name == (Str + NL->n_strx)) {
          NL->n_value = NewVal;
          Any = true;
        }
      }
    }
    Cmd += LC->cmdsize;
    if (LC->cmdsize == 0)
      break;
  }
  return Any;
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

/// Overwrite the six LC_DYSYMTAB range fields of a (merged) Mach-O object so a
/// test can corrupt the local/extdef/undef partition the verifier audits.
bool patchMachoDysymtab(SmallVectorImpl<char> &Buf, uint32_t ILocal,
                        uint32_t NLocal, uint32_t IExtdef, uint32_t NExtdef,
                        uint32_t IUndef, uint32_t NUndef) {
  namespace MO = llvm::MachO;
  if (Buf.size() < sizeof(MO::mach_header_64))
    return false;
  auto *MH = reinterpret_cast<MO::mach_header_64 *>(Buf.data());
  uint64_t Cmd = sizeof(MO::mach_header_64);
  for (unsigned c = 0; c < MH->ncmds; ++c) {
    if (Cmd + sizeof(MO::load_command) > Buf.size())
      break;
    auto *LC = reinterpret_cast<MO::load_command *>(Buf.data() + Cmd);
    if (LC->cmd == MO::LC_DYSYMTAB &&
        Cmd + sizeof(MO::dysymtab_command) <= Buf.size()) {
      auto *DC = reinterpret_cast<MO::dysymtab_command *>(Buf.data() + Cmd);
      DC->ilocalsym = ILocal;
      DC->nlocalsym = NLocal;
      DC->iextdefsym = IExtdef;
      DC->nextdefsym = NExtdef;
      DC->iundefsym = IUndef;
      DC->nundefsym = NUndef;
      return true;
    }
    Cmd += LC->cmdsize;
    if (LC->cmdsize == 0)
      break;
  }
  return false;
}

/// Overwrite the 8-byte little-endian word at (Seg,Sect)+SecRelOff in a Mach-O
/// object's *section data* (not the relocation table).  Used to plant a real
/// pointer at a non-extern relocation site and, post-merge, to corrupt the
/// in-place-rewritten pointer so the verifier's value check is exercised.
bool patchMachoSecQword(SmallVectorImpl<char> &Buf, StringRef Seg,
                        StringRef Sect, uint32_t SecRelOff, uint64_t NewVal) {
  namespace MO = llvm::MachO;
  if (Buf.size() < sizeof(MO::mach_header_64))
    return false;
  auto *MH = reinterpret_cast<MO::mach_header_64 *>(Buf.data());
  uint64_t Cmd = sizeof(MO::mach_header_64);
  for (unsigned c = 0; c < MH->ncmds; ++c) {
    if (Cmd + sizeof(MO::load_command) > Buf.size())
      break;
    auto *LC = reinterpret_cast<MO::load_command *>(Buf.data() + Cmd);
    if (LC->cmd == MO::LC_SEGMENT_64) {
      auto *Seg64 = reinterpret_cast<MO::segment_command_64 *>(Buf.data() + Cmd);
      char *SP = Buf.data() + Cmd + sizeof(MO::segment_command_64);
      for (unsigned i = 0; i < Seg64->nsects; ++i) {
        auto *S =
            reinterpret_cast<MO::section_64 *>(SP + i * sizeof(MO::section_64));
        StringRef Sn(S->sectname, strnlen(S->sectname, 16));
        StringRef Sg(S->segname, strnlen(S->segname, 16));
        if (Sg == Seg && Sn == Sect) {
          uint64_t Fo = (uint64_t)S->offset + SecRelOff;
          if (Fo + 8 > Buf.size())
            return false;
          memcpy(Buf.data() + Fo, &NewVal, 8);
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

TEST(MergeELFSemantic, DistinctNotesConcatenatedIdenticalDeduped) {
  using namespace ELF;
  // This merger is also the linker's general `-r` path over arbitrary user
  // objects, where two same-named SHT_NOTE sections can carry *different* bytes
  // (distinct .note.gnu.property feature sets, build-ids, ...).  The historical
  // "keep the first copy unconditionally" dedup silently dropped the later
  // note — invisible to verifyMerge, which excludes NOTE sections.  The merger
  // must now dedup only byte-identical notes and concatenate distinct ones, so
  // no data is lost on a heterogeneous -r while same-source partitions (which
  // re-emit byte-identical notes) still collapse to one copy.

  // Distinct content across the two partitions → both must survive.
  {
    SecSpec N0{".note.x", 0x10, 4, SHT_NOTE, SHF_ALLOC, /*Fill=*/0xAA};
    SecSpec N1{".note.x", 0x10, 4, SHT_NOTE, SHF_ALLOC, /*Fill=*/0xBB};
    auto O0 = buildSectionedELF({N0}, {}, {});
    auto O1 = buildSectionedELF({N1}, {}, {});
    SmallVector<SmallVector<char, 0>, 2> Bufs;
    Bufs.push_back(std::move(O0));
    Bufs.push_back(std::move(O1));
    auto [OK, Out] = mergeELF(Bufs);
    ASSERT_TRUE(OK);
    ElfView V = parseELF(Out);
    ASSERT_TRUE(V.Ok);
    int Idx = V.findSec(".note.x");
    ASSERT_GE(Idx, 0);
    EXPECT_EQ(V.Secs[Idx].Size, 0x20u)
        << "distinct .note.x from two inputs must concatenate, not drop one";
    // Both fill patterns must be present in the merged note bytes.
    const auto &D = V.Secs[Idx].Data;
    ASSERT_EQ(D.size(), 0x20u);
    bool SawAA = false, SawBB = false;
    for (uint8_t B : D) {
      SawAA |= (B == 0xAA);
      SawBB |= (B == 0xBB);
    }
    EXPECT_TRUE(SawAA) << "first input's note bytes were dropped";
    EXPECT_TRUE(SawBB) << "second input's note bytes were dropped";
  }

  // Byte-identical content across partitions (the same-source case) → dedup to
  // a single copy, exactly as before.
  {
    SecSpec N0{".note.x", 0x10, 4, SHT_NOTE, SHF_ALLOC, /*Fill=*/0xAA};
    SecSpec N1{".note.x", 0x10, 4, SHT_NOTE, SHF_ALLOC, /*Fill=*/0xAA};
    auto O0 = buildSectionedELF({N0}, {}, {});
    auto O1 = buildSectionedELF({N1}, {}, {});
    SmallVector<SmallVector<char, 0>, 2> Bufs;
    Bufs.push_back(std::move(O0));
    Bufs.push_back(std::move(O1));
    auto [OK, Out] = mergeELF(Bufs);
    ASSERT_TRUE(OK);
    ElfView V = parseELF(Out);
    ASSERT_TRUE(V.Ok);
    int Idx = V.findSec(".note.x");
    ASSERT_GE(Idx, 0);
    EXPECT_EQ(V.Secs[Idx].Size, 0x10u)
        << "byte-identical .note.x must dedup to one copy";
  }
}

TEST(MergeELFSemantic, NobitsAndProgbitsSameNameFoldWithoutOverlap) {
  // A NOBITS section and a PROGBITS section that share a name + flag set are
  // merge-compatible (canMergeToProgbits), so they collapse into one PROGBITS
  // output.  The NOBITS contribution must be materialized as zero bytes so the
  // other partition's bytes (and symbols) land *after* it; otherwise both
  // partitions restart at offset 0, the NOBITS reserve vanishes from the
  // output, and the two symbols alias the same address.  The self-verifier
  // skips NOBITS content windows, so it cannot catch this — assert it directly.
  // Both orderings are exercised because the promotion path differs
  // (NOBITS-first vs PROGBITS-first).
  auto check = [](bool NobitsFirst) {
    SecSpec Nb{"X", 0x40, 16, ELF::SHT_NOBITS,
               ELF::SHF_ALLOC | ELF::SHF_WRITE};
    SecSpec Pb{"X", 0x40, 16, ELF::SHT_PROGBITS,
               ELF::SHF_ALLOC | ELF::SHF_WRITE, 0xBB};
    SymSpec A{"a", 0, 0}; // defined in the NOBITS partition
    SymSpec B{"b", 0, 0}; // defined in the PROGBITS partition
    auto ObjNb = buildSectionedELF({Nb}, {A}, {});
    auto ObjPb = buildSectionedELF({Pb}, {B}, {});
    SmallVector<SmallVector<char, 0>, 2> Bufs;
    if (NobitsFirst) {
      Bufs.push_back(std::move(ObjNb));
      Bufs.push_back(std::move(ObjPb));
    } else {
      Bufs.push_back(std::move(ObjPb));
      Bufs.push_back(std::move(ObjNb));
    }
    auto [OK, Out] = mergeELF(Bufs);
    ASSERT_TRUE(OK) << "NobitsFirst=" << NobitsFirst;
    ElfView V = parseELF(Out);
    ASSERT_TRUE(V.Ok);
    int Idx = V.findSec("X");
    ASSERT_GE(Idx, 0);
    // Mixed NOBITS+PROGBITS must become PROGBITS holding both contributions.
    EXPECT_EQ(V.Secs[Idx].Type, (uint32_t)ELF::SHT_PROGBITS)
        << "NobitsFirst=" << NobitsFirst;
    EXPECT_EQ(V.Secs[Idx].Size, 0x80u)
        << "NobitsFirst=" << NobitsFirst; // 0x40 zero-fill + 0x40 progbits
    const ParsedSym *PA = V.findSym("a");
    const ParsedSym *PB = V.findSym("b");
    ASSERT_NE(PA, nullptr);
    ASSERT_NE(PB, nullptr);
    // The two partitions' symbols must occupy distinct, non-overlapping
    // offsets: one at 0, the other at 0x40 (order depends on which came first).
    EXPECT_NE(PA->Value, PB->Value)
        << "NobitsFirst=" << NobitsFirst << ": symbols collapsed onto offset 0";
    EXPECT_TRUE((PA->Value == 0 && PB->Value == 0x40) ||
                (PA->Value == 0x40 && PB->Value == 0))
        << "NobitsFirst=" << NobitsFirst << ": a=" << PA->Value
        << " b=" << PB->Value;
  };
  check(/*NobitsFirst=*/true);
  check(/*NobitsFirst=*/false);
}

TEST(MergeELFSemantic, RandomizedNobitsProgbitsMixNoCollapse) {
  // Property mirror of the NOBITS+PROGBITS fix: any random mix of same-named
  // NOBITS/PROGBITS partitions must fold into one section whose per-partition
  // symbols occupy distinct, non-overlapping offsets — never the offset-0
  // collapse the merger produced when a NOBITS run and a PROGBITS run each kept
  // their own offset counter.  An all-NOBITS draw must stay NOBITS; any PROGBITS
  // contributor promotes the whole section to PROGBITS.
  std::mt19937 Rng(0xB1775EEDu);
  for (int Trial = 0; Trial < 200; ++Trial) {
    unsigned NP = 2 + (Rng() % 4); // 2..5 partitions
    SmallVector<SmallVector<char, 0>, 5> Bufs;
    SmallVector<std::string, 8> SymNames;
    bool AnyProgbits = false;
    for (unsigned p = 0; p < NP; ++p) {
      bool Nobits = (Rng() & 1u) != 0;
      if (!Nobits)
        AnyProgbits = true;
      uint64_t Size = 0x10 + (Rng() % 0x40);
      SecSpec S{"X", Size, 16, Nobits ? ELF::SHT_NOBITS : ELF::SHT_PROGBITS,
                ELF::SHF_ALLOC | ELF::SHF_WRITE,
                Nobits ? (uint8_t)0 : (uint8_t)(0x10 + p)};
      std::string Nm = "s" + std::to_string(p);
      SymNames.push_back(Nm);
      Bufs.push_back(buildSectionedELF({S}, {SymSpec{Nm, 0, 0}}, {}));
    }
    auto [OK, Out] = mergeELF(Bufs);
    ASSERT_TRUE(OK) << "trial " << Trial;
    ElfView V = parseELF(Out);
    ASSERT_TRUE(V.Ok);
    int Idx = V.findSec("X");
    ASSERT_GE(Idx, 0) << "trial " << Trial;
    EXPECT_EQ(V.Secs[Idx].Type,
              (uint32_t)(AnyProgbits ? ELF::SHT_PROGBITS : ELF::SHT_NOBITS))
        << "trial " << Trial;
    SmallVector<uint64_t, 8> Vals;
    for (auto &Nm : SymNames) {
      const ParsedSym *S = V.findSym(Nm);
      ASSERT_NE(S, nullptr) << "trial " << Trial << " sym " << Nm;
      Vals.push_back(S->Value);
    }
    std::sort(Vals.begin(), Vals.end());
    for (unsigned i = 1; i < Vals.size(); ++i)
      EXPECT_NE(Vals[i - 1], Vals[i])
          << "trial " << Trial << ": two partition symbols collapsed onto offset "
          << Vals[i];
  }
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

TEST(MergeELFSemantic, MergeIsDeterministic) {
  using namespace ELF;
  // Reproducible builds and the per-partition object cache both require the
  // merge to be a pure function of its inputs: identical inputs must yield
  // byte-identical output every time (StringMap lookups must never leak their
  // hash-iteration order into section/symbol/string-table ordering).  Exercise
  // a non-trivial .text/.data/.bss + cross-partition relocated merge twice.
  SecSpec S0{".text", 0x40, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xAA};
  SecSpec D0{".data", 0x20, 8, SHT_PROGBITS, SHF_ALLOC | SHF_WRITE, 0xCC};
  SecSpec B0{".bss", 0x30, 16, SHT_NOBITS, SHF_ALLOC | SHF_WRITE};
  SecSpec S1{".text", 0x30, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xBB};
  SymSpec F0{"f0", 0, 0, true};
  SymSpec G0{"g0", 1, 0, true};
  SymSpec V0{"v0", 2, 0, true};
  SymSpec F1{"f1", 0, 0, true};
  SymSpec Ext{"ext", -1, 0, true};
  RelSpec R1{0, 0x10, "ext", R_X86_64_64, 7};
  auto O0 = buildSectionedELF({S0, D0, B0}, {F0, G0, V0}, {});
  auto O1 = buildSectionedELF({S1}, {F1, Ext}, {R1});
  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  auto [OK1, Out1] = mergeELF(Bufs);
  auto [OK2, Out2] = mergeELF(Bufs);
  ASSERT_TRUE(OK1);
  ASSERT_TRUE(OK2);
  ASSERT_EQ(Out1.size(), Out2.size());
  EXPECT_EQ(0, std::memcmp(Out1.data(), Out2.data(), Out1.size()))
      << "ELF merge is not deterministic — breaks reproducible builds and the "
         "per-partition object cache";
}

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

TEST(MergeELFVerify, CatchesCollapsedDuplicateNamedSymbol) {
  using namespace ELF;
  // Two partitions each define a *file-local* symbol with the SAME name "dup"
  // (the legitimate two-statics-share-a-name case).  In the merged symtab the
  // name is therefore ambiguous, which the verifier's unique-name anchor skips
  // wholesale — historically a blind spot where an offset collapse of exactly
  // these symbols would sail through.  The duplicate-name content anchor closes
  // it: a correct merge places each "dup" at a same-named output symbol whose
  // bytes match, so collapsing them all to 0 (the faithful shape of the bug)
  // must be rejected because partition 1's "dup" no longer matches any 0xBB
  // window.  Distinct fills make a mis-placed symbol read the wrong code.
  SecSpec S0{".text", 0x20, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xAA};
  SecSpec S1{".text", 0x20, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xBB};
  SymSpec D0{"dup", 0, 0, /*Global=*/false};
  SymSpec D1{"dup", 0, 0, /*Global=*/false};
  auto O0 = buildSectionedELF({S0}, {D0}, {});
  auto O1 = buildSectionedELF({S1}, {D1}, {});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  auto [OK, Out] = mergeELF(Bufs); // internal verify must accept the good merge
  ASSERT_TRUE(OK);

  // Sanity: the output really does carry two same-named "dup" symbols, so this
  // exercises the ambiguous (not the unique) verify path.
  {
    ElfView V = parseELF(Out);
    unsigned NDup = 0;
    for (const auto &S : V.Syms)
      if (S.Name == "dup")
        ++NDup;
    EXPECT_EQ(NDup, 2u) << "test setup expected two duplicate-named symbols";
  }

  std::string Err;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::ELF64LE, {}, &Err))
      << Err;

  // Collapse every "dup" to offset 0 (the historical bug).  Partition 1's dup
  // then reads partition 0's 0xAA bytes instead of its own 0xBB → rejected.
  auto Collapsed = Out;
  ASSERT_TRUE(patchAllSymValues(Collapsed, "dup", 0x0));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Collapsed), Format::ELF64LE, {}, &Err))
      << "verifier accepted a collapsed duplicate-named symbol offset";
}

TEST(MergeELFVerify, CatchesCollapsedDuplicateNamedSymbolMergeSections) {
  using namespace ELF;
  // The exact Android-kernel-module shape that the historical bug crashed: two
  // translation units each define a file-local helper of the *same* name in its
  // own per-function section (.text.dup), and mergeSections folds both into one
  // .text.  The merged name is therefore ambiguous AND re-homed across the
  // section rename — the precise intersection of the two features the bug lived
  // in.  A correct merge places each "dup" over its own bytes; collapsing them
  // all to 0 must be rejected because partition 1's "dup" then reads 0xAA.
  SecSpec T0{".text.dup", 0x40, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR,
             0xAA};
  SecSpec T1{".text.dup", 0x40, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR,
             0xBB};
  SymSpec D0{"dup", 0, 0, /*Global=*/false};
  SymSpec D1{"dup", 0, 0, /*Global=*/false};
  auto O0 = buildSectionedELF({T0}, {D0}, {});
  auto O1 = buildSectionedELF({T1}, {D1}, {});

  Options Opts;
  Opts.mergeSections = true;

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  auto [OK, Out] = mergeELF(Bufs, Opts);
  ASSERT_TRUE(OK); // internal verify (with mergeSections) must accept

  // Both "dup" folded into one .text and the per-function sections are gone.
  {
    ElfView V = parseELF(Out);
    EXPECT_GE(V.findSec(".text"), 0);
    EXPECT_LT(V.findSec(".text.dup"), 0);
    unsigned NDup = 0;
    for (const auto &S : V.Syms)
      if (S.Name == "dup")
        ++NDup;
    EXPECT_EQ(NDup, 2u);
  }

  std::string Err;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::ELF64LE, Opts, &Err))
      << Err;

  auto Collapsed = Out;
  ASSERT_TRUE(patchAllSymValues(Collapsed, "dup", 0x0));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Collapsed), Format::ELF64LE, Opts,
                           &Err))
      << "verifier accepted a collapsed duplicate-named symbol under "
         "mergeSections (the kernel-module configuration)";
}

TEST(MergeELFVerify, CatchesCollapsedBssSymbolOffset) {
  using namespace ELF;
  // The .bss twin of the historical .text collapse.  Two uninitialized globals
  // share one input .bss at distinct offsets.  A NOBITS section has no bytes, so
  // the verifier's byte-content anchor *must* skip it — the same-section
  // relative-distance invariant is the only check that can see a collapse here.
  // Before that invariant existed, collapsing bss_b onto bss_a silently passed
  // verification (st_value is what the final linker resolves the symbol to, so
  // the merged module would access the wrong .bss slot at run time).
  SecSpec B{".bss", 0x40, 16, SHT_NOBITS, SHF_ALLOC | SHF_WRITE};
  SymSpec VA{"bss_a", 0, 0x0, /*Global=*/true};
  SymSpec VB{"bss_b", 0, 0x20, /*Global=*/true};
  auto Obj = buildSectionedELF({B}, {VA, VB}, {});

  SmallVector<SmallVector<char, 0>, 1> Bufs;
  Bufs.push_back(Obj);
  auto [OK, Out] = mergeELF(Bufs); // internal verify must accept the good merge
  ASSERT_TRUE(OK);

  // Sanity: both globals kept their distinct .bss offsets.
  {
    ElfView V = parseELF(Out);
    const ParsedSym *PA = V.findSym("bss_a");
    const ParsedSym *PB = V.findSym("bss_b");
    ASSERT_NE(PA, nullptr);
    ASSERT_NE(PB, nullptr);
    EXPECT_EQ(PA->Value, 0x0u);
    EXPECT_EQ(PB->Value, 0x20u);
  }

  std::string Err;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::ELF64LE, {}, &Err))
      << Err;

  // Collapse bss_b onto bss_a's offset.  No bytes exist to compare, so only the
  // relative-distance invariant can reject this — and it must.
  auto Collapsed = Out;
  ASSERT_TRUE(patchSymValue(Collapsed, "bss_b", 0x0));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Collapsed), Format::ELF64LE, {}, &Err))
      << "verifier accepted a collapsed .bss symbol offset (NOBITS blind spot)";
}

TEST(MergeELFVerify, CatchesCollapsedSingletonBssDistinctSections) {
  using namespace ELF;
  // The ordinary multi-file .bss blind spot: two translation units each define
  // exactly *one* uninitialized global, each in its own input .bss.  A lone
  // symbol per input section gives the same-input-section relative-distance
  // invariant no sibling to compare, and NOBITS denies the byte-content anchor —
  // so before the disjoint-range invariant existed, collapsing g1 onto g0's slot
  // (the historical bug's shape) passed verification on perfectly ordinary code,
  // not just kernel modules.  The disjoint-range invariant reconstructs each
  // input .bss's merged base from its single symbol and forbids the overlap.
  SecSpec B0{".bss", 0x40, 16, SHT_NOBITS, SHF_ALLOC | SHF_WRITE};
  SecSpec B1{".bss", 0x40, 16, SHT_NOBITS, SHF_ALLOC | SHF_WRITE};
  SymSpec V0{"g0", 0, 0x0, /*Global=*/true};
  SymSpec V1{"g1", 0, 0x0, /*Global=*/true};
  auto O0 = buildSectionedELF({B0}, {V0}, {});
  auto O1 = buildSectionedELF({B1}, {V1}, {});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  auto [OK, Out] = mergeELF(Bufs); // internal verify must accept the good merge
  ASSERT_TRUE(OK);

  // p0's .bss lands at 0, p1's after it at 0x40 — distinct, disjoint ranges.
  {
    ElfView V = parseELF(Out);
    const ParsedSym *P0 = V.findSym("g0");
    const ParsedSym *P1 = V.findSym("g1");
    ASSERT_NE(P0, nullptr);
    ASSERT_NE(P1, nullptr);
    EXPECT_EQ(P0->Value, 0x0u);
    EXPECT_EQ(P1->Value, 0x40u);
  }

  std::string Err;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::ELF64LE, {}, &Err))
      << Err;

  // Collapse p1's g1 onto p0's slot: its input .bss range [0,0x40) now overlaps
  // p0's [0,0x40) → must be rejected even though no bytes exist to compare.
  auto Collapsed = Out;
  ASSERT_TRUE(patchSymValue(Collapsed, "g1", 0x0));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Collapsed), Format::ELF64LE, {}, &Err))
      << "verifier accepted a collapsed singleton .bss symbol across partitions "
         "(the ordinary multi-file NOBITS blind spot)";
}

TEST(MergeELFVerify, CatchesCollapsedSingletonBssMergeSections) {
  using namespace ELF;
  // The kernel-module (.ko) shape that scared us: -fdata-sections puts each
  // global in its own .bss.<name>, and mergeSections folds them all into one
  // .bss.  Each input .bss.<name> holds exactly one symbol, so this is the
  // singleton case the relative-distance invariant cannot see; NOBITS denies the
  // content anchor.  The disjoint-range invariant is the only line of defense —
  // exactly the gap the historical offset-collapse bug would have hidden in on a
  // real .ko, where there is no execution fallback to catch it later.
  SecSpec BA{".bss.a", 0x40, 16, SHT_NOBITS, SHF_ALLOC | SHF_WRITE};
  SecSpec BB{".bss.b", 0x40, 16, SHT_NOBITS, SHF_ALLOC | SHF_WRITE};
  SymSpec VA{"var_a", 0, 0x0, /*Global=*/true};
  SymSpec VB{"var_b", 1, 0x0, /*Global=*/true};
  auto Obj = buildSectionedELF({BA, BB}, {VA, VB}, {});

  Options Opts;
  Opts.mergeSections = true;

  SmallVector<SmallVector<char, 0>, 1> Bufs;
  Bufs.push_back(Obj);
  auto [OK, Out] = mergeELF(Bufs, Opts); // internal verify (mergeSections) accepts
  ASSERT_TRUE(OK);

  // Both .bss.* folded into one .bss; var_a at 0, var_b after it.
  {
    ElfView V = parseELF(Out);
    EXPECT_GE(V.findSec(".bss"), 0);
    EXPECT_LT(V.findSec(".bss.a"), 0);
    const ParsedSym *PA = V.findSym("var_a");
    const ParsedSym *PB = V.findSym("var_b");
    ASSERT_NE(PA, nullptr);
    ASSERT_NE(PB, nullptr);
    EXPECT_EQ(PA->Value, 0x0u);
    EXPECT_EQ(PB->Value, 0x40u);
  }

  std::string Err;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::ELF64LE, Opts, &Err))
      << Err;

  // Collapse var_b onto var_a's slot.  .bss.b's reconstructed range now overlaps
  // .bss.a's → rejected, closing the NOBITS singleton blind spot on the .ko path.
  auto Collapsed = Out;
  ASSERT_TRUE(patchSymValue(Collapsed, "var_b", 0x0));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Collapsed), Format::ELF64LE, Opts,
                           &Err))
      << "verifier accepted a collapsed singleton .bss under mergeSections "
         "(the kernel-module NOBITS blind spot)";
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

TEST(MergeELFVerify, CatchesCorruptedRelocAddend) {
  using namespace ELF;
  // A relocation against a *named* symbol carries an explicit addend the -r
  // merge must copy verbatim (only the site offset shifts past earlier .text).
  // The offset anchor already proves the reloc re-lands in the right slot; this
  // proves its addend survived too.  A corrupted addend resolves to the wrong
  // target address even when the site offset is perfect — a "loads fine, reads
  // the wrong place" miscompile the offset check alone cannot catch.  The two
  // partitions use *distinct* non-zero addends so collapsing both to one value
  // cannot accidentally still match.
  SecSpec S0{".text", 0x40, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xAA};
  SecSpec S1{".text", 0x40, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xBB};
  SymSpec F0{"f0", 0, 0, true}; // anchors p0's .text at offset 0
  SymSpec F1{"f1", 0, 0, true}; // anchors p1's .text (shifts to 0x40)
  SymSpec Ext{"ext", -1, 0, true};            // shared undefined named target
  RelSpec R0{0, 0x10, "ext", R_X86_64_64, 0x1234};
  RelSpec R1{0, 0x10, "ext", R_X86_64_64, 0x5678};
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

  ASSERT_TRUE(patchAllRelaAddends(Out, 0x0));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Out), Format::ELF64LE, {}, &Err))
      << "verifier accepted a corrupted relocation addend";
}

TEST(MergeELFVerify, CatchesCollapsedSectionRelativeReloc) {
  using namespace ELF;
  // Section-relative relocation: each partition's .text references ".rodata +
  // addend" via an STT_SECTION target (no symbol name) — the class the verifier
  // used to skip entirely.  f0/f1 anchor each .text so the merged site offset
  // is predictable; after merge they sit at 0x10 (p0) and 0x50 (p1).  A
  // collapse of these offsets must now be rejected too.
  SecSpec T0{".text", 0x40, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xAA};
  SecSpec D0{".rodata", 0x20, 16, SHT_PROGBITS, SHF_ALLOC, 0xCC};
  SecSpec T1{".text", 0x40, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xBB};
  SecSpec D1{".rodata", 0x20, 16, SHT_PROGBITS, SHF_ALLOC, 0xDD};
  SymSpec F0{"f0", 0, 0, true};
  SymSpec F1{"f1", 0, 0, true};
  // Applies in .text (sec 0), targets the STT_SECTION symbol of .rodata (sec 1).
  RelSpec R0{0, 0x10, "", R_X86_64_PC32, 0, /*TargetSecSym=*/1};
  RelSpec R1{0, 0x10, "", R_X86_64_PC32, 0, /*TargetSecSym=*/1};
  auto O0 = buildSectionedELF({T0, D0}, {F0}, {R0});
  auto O1 = buildSectionedELF({T1, D1}, {F1}, {R1});

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
      << "verifier accepted a collapsed section-relative relocation offset";
}

TEST(MergeELFVerify, SectionRelativeRelocWithMergeSections) {
  using namespace ELF;
  // The Android-kernel -r path (mergeSections=true): per-function/per-object
  // sections (.text.fN, .rodata.rN) fold into .text/.rodata, and section-
  // relative relocations must re-anchor across the rename without the verifier
  // raising a false positive (which would hard-fail the .ko link).  A real
  // collapse must still be caught.
  SecSpec T0{".text.f0", 0x40, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR,
             0xAA};
  SecSpec D0{".rodata.r0", 0x20, 16, SHT_PROGBITS, SHF_ALLOC, 0xCC};
  SecSpec T1{".text.f1", 0x40, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR,
             0xBB};
  SecSpec D1{".rodata.r1", 0x20, 16, SHT_PROGBITS, SHF_ALLOC, 0xDD};
  SymSpec F0{"f0", 0, 0, true};
  SymSpec F1{"f1", 0, 0, true};
  RelSpec R0{0, 0x10, "", R_X86_64_PC32, 0, /*TargetSecSym=*/1};
  RelSpec R1{0, 0x10, "", R_X86_64_PC32, 0, /*TargetSecSym=*/1};
  auto O0 = buildSectionedELF({T0, D0}, {F0}, {R0});
  auto O1 = buildSectionedELF({T1, D1}, {F1}, {R1});

  Options Opts;
  Opts.mergeSections = true;

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  auto [OK, Out] = mergeELF(Bufs, Opts);
  ASSERT_TRUE(OK); // internal verify (with mergeSections) must accept

  std::string Err;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::ELF64LE, Opts, &Err))
      << Err;

  ASSERT_TRUE(patchAllRelaOffsets(Out, 0x0));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Out), Format::ELF64LE, Opts, &Err))
      << "verifier accepted a collapsed section-relative reloc under "
         "mergeSections";
}

TEST(MergeELFVerify, RandomizedCollapseAlwaysRejected) {
  using namespace ELF;
  // No-false-*negatives* property test, the mirror of RandomizedLayoutOracle's
  // no-false-*positives* sweep.  Each partition contributes a .text whose single
  // function symbol is pinned at offset 0 (so every relocation is anchorable —
  // exactly the shape real codegen emits, where each reloc site lives inside a
  // function that has a symbol at its entry) and one relocation at a non-zero
  // offset against a shared undefined extern.  Merging >=2 such partitions
  // shifts later relocs past earlier .text, so collapsing every reloc offset to
  // 0 — the precise shape of the shipped bug — must ALWAYS be rejected by the
  // independent verifier, for arbitrary random sizes/offsets, not just the
  // hand-built cases above.
  std::mt19937 Rng(0x5EED1234u);
  for (int Trial = 0; Trial < 200; ++Trial) {
    unsigned NP = 2 + (Rng() % 3); // >=2 so merged offsets actually shift
    SmallVector<SmallVector<char, 0>, 4> Bufs;
    for (unsigned p = 0; p < NP; ++p) {
      uint64_t Size = 0x40 + (Rng() % 0x80);
      uint8_t Fill = (uint8_t)(0x11 + p); // distinct, non-zero per partition
      SecSpec T{".text", Size, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR,
                Fill};
      SymSpec F{"fn_" + std::to_string(p), 0, 0, true}; // pinned at offset 0
      SymSpec E{"ext", 0, 0, false};                    // shared undefined ref
      uint64_t RO = 1 + (Rng() % (Size - 1));           // strictly inside, > 0
      RelSpec R{0, RO, "ext", R_X86_64_PC32, 0};
      Bufs.push_back(buildSectionedELF({T}, {F, E}, {R}));
    }
    auto [OK, Out] = mergeELF(Bufs);
    ASSERT_TRUE(OK) << "trial " << Trial;
    std::string VErr;
    ASSERT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                            ArrayRef<char>(Out), Format::ELF64LE, {}, &VErr))
        << "trial " << Trial << ": good merge unexpectedly rejected: " << VErr;
    ASSERT_TRUE(patchAllRelaOffsets(Out, 0x0)) << "trial " << Trial;
    std::string CErr;
    EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                             ArrayRef<char>(Out), Format::ELF64LE, {}, &CErr))
        << "trial " << Trial << ": verifier accepted collapsed reloc offsets";
  }
}

TEST(MergeELFVerify, RandomizedAddendCorruptionAlwaysRejected) {
  using namespace ELF;
  // Property mirror of RandomizedCollapseAlwaysRejected for the *addend* half of
  // a relocation.  Each partition pins one function at offset 0 (so its reloc
  // is anchorable) and emits a relocation against a shared named symbol with a
  // distinct, non-zero addend.  A faithful -r merge copies every addend
  // verbatim, so the good merge must verify; collapsing all addends to 0 must
  // then ALWAYS be rejected (every partition's non-zero addend now mismatches),
  // for arbitrary random addends/offsets — not just the hand-built case above.
  std::mt19937 Rng(0xADDE6D00u);
  for (int Trial = 0; Trial < 200; ++Trial) {
    unsigned NP = 2 + (Rng() % 3); // >=2 so merged offsets actually shift
    SmallVector<SmallVector<char, 0>, 4> Bufs;
    for (unsigned p = 0; p < NP; ++p) {
      uint64_t Size = 0x40 + (Rng() % 0x80);
      uint8_t Fill = (uint8_t)(0x11 + p);
      SecSpec T{".text", Size, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR,
                Fill};
      SymSpec F{"fn_" + std::to_string(p), 0, 0, true}; // pinned at offset 0
      SymSpec E{"ext", -1, 0, true};                    // shared named target
      uint64_t RO = 1 + (Rng() % (Size - 8));           // 8-byte slot, inside
      int64_t Add = (int64_t)(1 + (Rng() % 0x7FFF));    // distinct, non-zero
      RelSpec R{0, RO, "ext", R_X86_64_64, Add};
      Bufs.push_back(buildSectionedELF({T}, {F, E}, {R}));
    }
    auto [OK, Out] = mergeELF(Bufs);
    ASSERT_TRUE(OK) << "trial " << Trial;
    std::string VErr;
    ASSERT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                            ArrayRef<char>(Out), Format::ELF64LE, {}, &VErr))
        << "trial " << Trial << ": good merge unexpectedly rejected: " << VErr;
    ASSERT_TRUE(patchAllRelaAddends(Out, 0)) << "trial " << Trial;
    std::string CErr;
    EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                             ArrayRef<char>(Out), Format::ELF64LE, {}, &CErr))
        << "trial " << Trial << ": verifier accepted corrupted reloc addends";
  }
}

// ---------------------------------------------------------------------------
// Fail-loud guards: the merger must REFUSE inputs whose features it does not
// fully model, rather than silently dropping them and shipping a wrong object.
// On the kernel/-r path a refusal becomes a loud `error("relocatable merge
// failed")`; on the parallel-codegen path it falls back to serial codegen.
// Either way the device never sees a miscompile.
// ---------------------------------------------------------------------------

TEST(MergeELFLinkOrder, MergedWithRemappedShLink) {
  using namespace ELF;
  // __patchable_function_entries (emitted by -fpatchable-function-entry for
  // ftrace) is SHF_LINK_ORDER with sh_link → its code section.  Two partitions
  // each contribute a .text plus a PFE pointing at their own .text; after -r
  // merge both .text fold into one and both PFE fold into one whose sh_link must
  // be remapped to the merged .text.  (This is the ftrace .ko path the merger
  // used to refuse outright, forcing a serial-codegen fallback.)
  SecSpec Text0{".text", 0x40, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR,
                0xAA};
  SecSpec Pfe0{"__patchable_function_entries", 0x10, 8, SHT_PROGBITS,
               SHF_ALLOC | SHF_LINK_ORDER, 0xBB, /*Link=*/0};
  SymSpec F{"f", 0, 0, true};
  auto O0 = buildSectionedELF({Text0, Pfe0}, {F}, {});
  SecSpec Text1{".text", 0x40, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR,
                0xCC};
  SecSpec Pfe1{"__patchable_function_entries", 0x10, 8, SHT_PROGBITS,
               SHF_ALLOC | SHF_LINK_ORDER, 0xDD, /*Link=*/0};
  SymSpec G{"g", 0, 0, true};
  auto O1 = buildSectionedELF({Text1, Pfe1}, {G}, {});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  auto [OK, Out] = mergeELF(Bufs);
  ASSERT_TRUE(OK) << "merger refused a well-formed SHF_LINK_ORDER merge";

  ElfView V = parseELF(Out);
  ASSERT_TRUE(V.Ok);
  int PfeIdx = V.findSec("__patchable_function_entries");
  int TextIdx = V.findSec(".text");
  ASSERT_GE(PfeIdx, 0) << "merged PFE section missing";
  ASSERT_GE(TextIdx, 0) << "merged .text section missing";
  const ParsedSec &Pfe = V.Secs[PfeIdx];
  EXPECT_TRUE(Pfe.Flags & SHF_LINK_ORDER);
  EXPECT_EQ(Pfe.Link, (uint32_t)TextIdx)
      << "merged SHF_LINK_ORDER sh_link not remapped to the merged .text";
  EXPECT_EQ(Pfe.Size, 0x20u) << "both PFE contributions must be concatenated";

  std::string Err;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::ELF64LE, {}, &Err))
      << Err;
}

TEST(MergeELFLinkOrder, MergeSectionsFoldsTargetsConsistently) {
  using namespace ELF;
  // The kernel-module path (mergeSections=true) folds per-function
  // .text.foo/.text.bar into one .text, so both PFE link targets canonicalize
  // to ".text" — consistent — and the merged PFE sh_link points at the single
  // merged .text.
  SecSpec TextFoo{".text.foo", 0x40, 16, SHT_PROGBITS,
                  SHF_ALLOC | SHF_EXECINSTR, 0xAA};
  SecSpec Pfe0{"__patchable_function_entries", 0x10, 8, SHT_PROGBITS,
               SHF_ALLOC | SHF_LINK_ORDER, 0xBB, /*Link=*/0};
  SymSpec F{"f", 0, 0, true};
  auto O0 = buildSectionedELF({TextFoo, Pfe0}, {F}, {});
  SecSpec TextBar{".text.bar", 0x40, 16, SHT_PROGBITS,
                  SHF_ALLOC | SHF_EXECINSTR, 0xCC};
  SecSpec Pfe1{"__patchable_function_entries", 0x10, 8, SHT_PROGBITS,
               SHF_ALLOC | SHF_LINK_ORDER, 0xDD, /*Link=*/0};
  SymSpec G{"g", 0, 0, true};
  auto O1 = buildSectionedELF({TextBar, Pfe1}, {G}, {});

  Options Opts;
  Opts.mergeSections = true;
  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  auto [OK, Out] = mergeELF(Bufs, Opts);
  ASSERT_TRUE(OK) << "merger refused a consistent mergeSections PFE merge";

  ElfView V = parseELF(Out);
  ASSERT_TRUE(V.Ok);
  int PfeIdx = V.findSec("__patchable_function_entries");
  int TextIdx = V.findSec(".text");
  ASSERT_GE(PfeIdx, 0);
  ASSERT_GE(TextIdx, 0);
  EXPECT_EQ(V.Secs[PfeIdx].Link, (uint32_t)TextIdx);
  std::string Err;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::ELF64LE, Opts, &Err))
      << Err;
}

TEST(MergeELFLinkOrder, InconsistentTargetsRefused) {
  using namespace ELF;
  // Without mergeSections, per-function .text.foo/.text.bar are NOT folded, so
  // two PFE inputs pointing at different code sections would need one output
  // sh_link to name two targets — impossible.  The merger must refuse rather
  // than silently pick one (which would drop the other's ordering dependency).
  SecSpec TextFoo{".text.foo", 0x40, 16, SHT_PROGBITS,
                  SHF_ALLOC | SHF_EXECINSTR, 0xAA};
  SecSpec Pfe0{"__patchable_function_entries", 0x10, 8, SHT_PROGBITS,
               SHF_ALLOC | SHF_LINK_ORDER, 0xBB, /*Link=*/0};
  SymSpec F{"f", 0, 0, true};
  auto O0 = buildSectionedELF({TextFoo, Pfe0}, {F}, {});
  SecSpec TextBar{".text.bar", 0x40, 16, SHT_PROGBITS,
                  SHF_ALLOC | SHF_EXECINSTR, 0xCC};
  SecSpec Pfe1{"__patchable_function_entries", 0x10, 8, SHT_PROGBITS,
               SHF_ALLOC | SHF_LINK_ORDER, 0xDD, /*Link=*/0};
  SymSpec G{"g", 0, 0, true};
  auto O1 = buildSectionedELF({TextBar, Pfe1}, {G}, {});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  auto [OK, Out] = mergeELF(Bufs); // default: mergeSections=false
  EXPECT_FALSE(OK)
      << "merger accepted SHF_LINK_ORDER inputs with inconsistent link targets";
}

TEST(MergeELFLinkOrder, VerifyCatchesCollapsedPfeRelocOffset) {
  using namespace ELF;
  // PFE relocations have no defined symbol to anchor on, so the symbol-anchored
  // reloc check skips them; the SHF_LINK_ORDER conservation check (distinct
  // offsets) is their guard.  Merge two partitions whose PFE carries
  // relocations into their own functions, then collapse every reloc offset to 0
  // (the PFE half of the historical offset-collapse bug) and confirm the
  // independent verifier rejects it.
  SecSpec T0{".text", 0x40, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xAA};
  SecSpec P0{"__patchable_function_entries", 0x10, 8, SHT_PROGBITS,
             SHF_ALLOC | SHF_LINK_ORDER, 0, /*Link=*/0};
  SymSpec F0{"f0", 0, 0, true};
  SymSpec G0{"g0", 0, 0x20, true};
  RelSpec R0a{1, 0, "f0", R_X86_64_64, 0};
  RelSpec R0b{1, 8, "g0", R_X86_64_64, 0};
  auto O0 = buildSectionedELF({T0, P0}, {F0, G0}, {R0a, R0b});

  SecSpec T1{".text", 0x40, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xCC};
  SecSpec P1{"__patchable_function_entries", 0x10, 8, SHT_PROGBITS,
             SHF_ALLOC | SHF_LINK_ORDER, 0, /*Link=*/0};
  SymSpec F1{"f1", 0, 0, true};
  SymSpec G1{"g1", 0, 0x20, true};
  RelSpec R1a{1, 0, "f1", R_X86_64_64, 0};
  RelSpec R1b{1, 8, "g1", R_X86_64_64, 0};
  auto O1 = buildSectionedELF({T1, P1}, {F1, G1}, {R1a, R1b});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  auto [OK, Out] = mergeELF(Bufs);
  ASSERT_TRUE(OK) << "merger refused a well-formed PFE-with-relocs merge";
  std::string Err;
  ASSERT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::ELF64LE, {}, &Err))
      << Err;

  // Collapse every relocation offset to 0: the merged PFE now has 4 relocations
  // aliased onto one offset — the exact shape the conservation check guards.
  ASSERT_TRUE(patchAllRelaOffsets(Out, 0));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Out), Format::ELF64LE, {}, &Err))
      << "verifier accepted collapsed PFE relocation offsets";
}

TEST(MergeELFVerify, CatchesCorruptedSymtabShInfo) {
  using namespace ELF;
  // The __pcg demotion reorders the symbol table; a bug there would mis-set
  // sh_info (the local/global boundary) and silently corrupt how every binding
  // is read.  The structural check rejects any output whose locals are not all
  // before sh_info — independent of the merger's own bookkeeping.
  SecSpec T0{".text", 0x40, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xAA};
  SecSpec T1{".text", 0x40, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xCC};
  SymSpec La{"la", 0, 0, /*Global=*/false};
  SymSpec Ga{"ga", 0, 0, /*Global=*/true};
  SymSpec Lb{"lb", 0, 0, /*Global=*/false};
  SymSpec Gb{"gb", 0, 0, /*Global=*/true};
  auto O0 = buildSectionedELF({T0}, {La, Ga}, {});
  auto O1 = buildSectionedELF({T1}, {Lb, Gb}, {});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  auto [OK, Out] = mergeELF(Bufs);
  ASSERT_TRUE(OK);
  std::string Err;
  ASSERT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::ELF64LE, {}, &Err))
      << Err;

  // sh_info=1 places the real local defs (la/lb, at indices >= 1) after the
  // claimed boundary — exactly the binding-order corruption to catch.
  ASSERT_TRUE(patchElfSymtabShInfo(Out, 1));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Out), Format::ELF64LE, {}, &Err))
      << "verifier accepted a corrupted symtab sh_info (local/global boundary)";
}

TEST(MergeELFVerify, CatchesLinkOrderSectionWithZeroShLink) {
  using namespace ELF;
  // verifyMerge also audits objects produced by other linkers (the differential
  // suite feeds it real LLD -r output).  A SHF_LINK_ORDER section is correct
  // only with a real sh_link; the sh_link=0 shape (which the merger's own
  // sh_link remap never produces) must be rejected by the independent audit
  // too, so a wrong object can never slip through whatever produced it.
  SecSpec Text{".text", 0x40, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xAA};
  SecSpec Pfe{"__patchable_function_entries", 0x10, 8, SHT_PROGBITS,
              SHF_ALLOC | SHF_LINK_ORDER, 0xBB};
  SymSpec F{"f", 0, 0, true};
  // buildSectionedELF leaves content-section sh_link at 0.
  auto Obj = buildSectionedELF({Text, Pfe}, {F}, {});

  SmallVector<SmallVector<char, 0>, 1> Bufs;
  Bufs.push_back(Obj);
  std::string Err;
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Obj), Format::ELF64LE, {}, &Err))
      << "verifier accepted a SHF_LINK_ORDER section with sh_link=0";
}

// ---------------------------------------------------------------------------
// COFF semantic tests — same invariant on the Windows object path.
// ---------------------------------------------------------------------------

// P0 arch-consistency guard: two ELF64LE objects of different e_machine both
// parse cleanly as ELF64LE but describe incompatible code.  The merger must
// refuse rather than emit one header (the first input's e_machine) over a
// cross-ISA body — a silent miscompile no content/offset anchor can see.  A
// real `ld -r` refuses this too, so refusing never regresses a legitimate link.
TEST(MergeELF, RefusesMixedEMachine) {
  using namespace ELF;
  auto A = buildMinimalELF({"a"}, {});
  auto B = buildMinimalELF({"b"}, {});

  // Positive control: same machine still merges (guards against a false-reject
  // regression that would silently disable parallel codegen).
  {
    SmallVector<SmallVector<char, 0>, 2> Bufs;
    Bufs.push_back(A);
    Bufs.push_back(B);
    EXPECT_TRUE(mergeELF(Bufs).first);
  }

  // Flip the second object's e_machine to AArch64 -> mixed arch -> must refuse.
  ASSERT_GE(B.size(), sizeof(Elf64_Ehdr));
  reinterpret_cast<Elf64_Ehdr *>(B.data())->e_machine = EM_AARCH64;
  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(std::move(A));
  Bufs.push_back(std::move(B));
  EXPECT_FALSE(mergeELF(Bufs).first)
      << "merger accepted mixed e_machine inputs";
}

// P0 arch-consistency guard, verifier leg: even if a future merger bug wrote a
// wrong output header, the independent verifier must catch an output e_machine
// that disagrees with the inputs (every section byte still anchors to its
// input; only the architecture lies).
TEST(MergeELFVerify, CatchesMismatchedOutputMachine) {
  using namespace ELF;
  SecSpec S0{".text", 0x20, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xAA};
  SecSpec S1{".text", 0x20, 16, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0xBB};
  SymSpec FA{"fa", 0, 0, true};
  SymSpec FB{"fb", 0, 0, true};
  auto O0 = buildSectionedELF({S0}, {FA}, {});
  auto O1 = buildSectionedELF({S1}, {FB}, {});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  auto [OK, Out] = mergeELF(Bufs);
  ASSERT_TRUE(OK);

  std::string Err;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::ELF64LE, {}, &Err))
      << Err;

  auto Bad = Out;
  ASSERT_GE(Bad.size(), sizeof(Elf64_Ehdr));
  reinterpret_cast<Elf64_Ehdr *>(Bad.data())->e_machine = EM_AARCH64;
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Bad), Format::ELF64LE, {}, &Err))
      << "verifier accepted an output with a mismatched e_machine";
}

// P1: two strong (STB_GLOBAL, defined) definitions of one symbol is an ODR
// violation the ELF dedup would silently resolve by dropping one.  Refuse it
// (a real `ld -r` also rejects/defers; the parallel-codegen path never makes
// dup strong defs, so this never false-rejects that path).
TEST(MergeELF, RefusesMultipleStrongDefinitions) {
  auto A = buildMinimalELF({"dup"}, {}, {0xcc}, /*DefinedAsGlobal=*/true);
  auto B = buildMinimalELF({"dup"}, {}, {0xdd}, /*DefinedAsGlobal=*/true);
  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(std::move(A));
  Bufs.push_back(std::move(B));
  EXPECT_FALSE(mergeELF(Bufs).first)
      << "merger accepted two strong definitions of the same symbol";

  // Control: distinct strong globals merge fine (no false-reject regression).
  auto C = buildMinimalELF({"g0"}, {}, {0xcc}, /*DefinedAsGlobal=*/true);
  auto D = buildMinimalELF({"g1"}, {}, {0xdd}, /*DefinedAsGlobal=*/true);
  SmallVector<SmallVector<char, 0>, 2> Bufs2;
  Bufs2.push_back(std::move(C));
  Bufs2.push_back(std::move(D));
  EXPECT_TRUE(mergeELF(Bufs2).first);
}

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

TEST(MergeCOFFSemantic, MergeIsDeterministic) {
  using namespace COFF;
  // Identical inputs must produce byte-identical output (see the ELF analogue);
  // the merger zeroes TimeDateStamp so only ordering bugs could break this.
  uint32_t TextChars = IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE |
                       IMAGE_SCN_MEM_READ | IMAGE_SCN_ALIGN_16BYTES;
  uint32_t DataChars = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ |
                       IMAGE_SCN_MEM_WRITE | IMAGE_SCN_ALIGN_8BYTES;
  uint32_t BssChars = IMAGE_SCN_CNT_UNINITIALIZED_DATA | IMAGE_SCN_MEM_READ |
                      IMAGE_SCN_MEM_WRITE | IMAGE_SCN_ALIGN_16BYTES;
  CoffSecSpec S0{".text", 0x40, TextChars, 0xAA};
  CoffSecSpec D0{".data", 0x20, DataChars, 0xCC};
  CoffSecSpec B0{".bss", 0x30, BssChars};
  CoffSecSpec S1{".text", 0x20, TextChars, 0xBB};
  CoffSymSpec F0{"f0", 0, 1, IMAGE_SYM_CLASS_EXTERNAL};
  CoffSymSpec G0{"g0", 0, 2, IMAGE_SYM_CLASS_EXTERNAL};
  CoffSymSpec V0{"v0", 0, 3, IMAGE_SYM_CLASS_EXTERNAL};
  CoffSymSpec F1{"f1", 0, 1, IMAGE_SYM_CLASS_EXTERNAL};
  auto O0 = buildCOFF(IMAGE_FILE_MACHINE_AMD64, {S0, D0, B0}, {F0, G0, V0}, {});
  auto O1 = buildCOFF(IMAGE_FILE_MACHINE_AMD64, {S1}, {F1}, {});
  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  SmallVector<char, 0> Out1, Out2;
  {
    raw_svector_ostream OS(Out1);
    ASSERT_TRUE(mergeObjects(Bufs, OS, Format::COFF));
  }
  {
    raw_svector_ostream OS(Out2);
    ASSERT_TRUE(mergeObjects(Bufs, OS, Format::COFF));
  }
  ASSERT_EQ(Out1.size(), Out2.size());
  EXPECT_EQ(0, std::memcmp(Out1.data(), Out2.data(), Out1.size()))
      << "COFF merge is not deterministic";
}

TEST(MergeCOFFSemantic, WeakExternalAuxTagIndexRemapped) {
  using namespace COFF;
  // A COFF weak external symbol carries a coff_aux_weak_external whose TagIndex
  // is a *symbol-table index* naming the default definition.  The merge appends
  // every partition's symbols, shifting all indices, so a copied-verbatim
  // TagIndex aliases the weak symbol onto an unrelated definition — a silent
  // miscompile the content/offset anchors cannot see (aux records carry no
  // section bytes).  Each partition defines its own default and a weak external
  // pointing at it; after merge each weak aux must still name *its* default.
  uint32_t TextChars = IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE |
                       IMAGE_SCN_MEM_READ | IMAGE_SCN_ALIGN_16BYTES;
  CoffSecSpec S0{".text", 0x20, TextChars, 0xAA};
  CoffSymSpec Def0{"wdef0", 0, 1, IMAGE_SYM_CLASS_EXTERNAL};
  CoffSymSpec Weak0{"wfn0", 0, 0, IMAGE_SYM_CLASS_WEAK_EXTERNAL};
  Weak0.WeakDefTag = 0; // -> Def0 (index 0 within partition 0's specs)
  auto O0 = buildCOFF(IMAGE_FILE_MACHINE_AMD64, {S0}, {Def0, Weak0}, {});

  CoffSecSpec S1{".text", 0x20, TextChars, 0xBB};
  CoffSymSpec Def1{"wdef1", 0, 1, IMAGE_SYM_CLASS_EXTERNAL};
  CoffSymSpec Weak1{"wfn1", 0, 0, IMAGE_SYM_CLASS_WEAK_EXTERNAL};
  Weak1.WeakDefTag = 0; // -> Def1 (index 0 within partition 1's specs)
  auto O1 = buildCOFF(IMAGE_FILE_MACHINE_AMD64, {S1}, {Def1, Weak1}, {});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  SmallVector<char, 0> Out;
  {
    raw_svector_ostream OS(Out);
    ASSERT_TRUE(mergeObjects(Bufs, OS, Format::COFF));
  }

  // Walk the merged symbol table; each weak external's aux TagIndex must name
  // the matching default definition by content, not a stale partition-local
  // index.
  ASSERT_GE(Out.size(), 20u);
  uint32_t SymPtr = getU32(Out.data() + 8);
  uint32_t NSym = getU32(Out.data() + 12);
  ASSERT_LE(SymPtr + (uint64_t)NSym * 18, Out.size());
  uint64_t StrBase = (uint64_t)SymPtr + (uint64_t)NSym * 18;
  auto slotName = [&](uint32_t Slot) -> std::string {
    const char *P = Out.data() + SymPtr + (uint64_t)Slot * 18;
    if (getU32(P) == 0) { // long-name escape: 4 zero bytes, then strtab offset
      uint32_t O = getU32(P + 4);
      if (StrBase + O >= Out.size())
        return {};
      const char *S = Out.data() + StrBase + O;
      return std::string(S, strnlen(S, Out.size() - (StrBase + O)));
    }
    return std::string(P, strnlen(P, 8));
  };
  bool SawW0 = false, SawW1 = false;
  for (uint32_t k = 0; k < NSym;) {
    const char *P = Out.data() + SymPtr + (uint64_t)k * 18;
    std::string Nm = slotName(k);
    uint8_t Storage = (uint8_t)P[16];
    uint8_t NAux = (uint8_t)P[17];
    if (Storage == IMAGE_SYM_CLASS_WEAK_EXTERNAL && NAux >= 1 && k + 1 < NSym) {
      uint32_t Tag = getU32(Out.data() + SymPtr + (uint64_t)(k + 1) * 18);
      ASSERT_LT(Tag, NSym) << "weak '" << Nm << "' aux TagIndex out of range";
      std::string TagName = slotName(Tag);
      if (Nm == "wfn0") {
        EXPECT_EQ(TagName, "wdef0")
            << "weak external wfn0 aux TagIndex not remapped to its default";
        SawW0 = true;
      } else if (Nm == "wfn1") {
        EXPECT_EQ(TagName, "wdef1")
            << "weak external wfn1 aux TagIndex not remapped to its default";
        SawW1 = true;
      }
    }
    k += 1u + NAux;
  }
  EXPECT_TRUE(SawW0) << "wfn0 missing from merged output";
  EXPECT_TRUE(SawW1) << "wfn1 missing from merged output";
}

TEST(MergeCOFFVerify, CatchesWeakExternalTagIndexNotRemapped) {
  using namespace COFF;
  // Independent proof that the verifier closes the aux blind spot: build a good
  // merge, then corrupt wfn1's aux TagIndex back to the historical "copied
  // verbatim" value (0, which names wdef0 instead of wdef1) and confirm
  // verifyMerge rejects it even though every section byte still matches.
  uint32_t TextChars = IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE |
                       IMAGE_SCN_MEM_READ | IMAGE_SCN_ALIGN_16BYTES;
  CoffSecSpec S0{".text", 0x20, TextChars, 0xAA};
  CoffSymSpec Def0{"wdef0", 0, 1, IMAGE_SYM_CLASS_EXTERNAL};
  CoffSymSpec Weak0{"wfn0", 0, 0, IMAGE_SYM_CLASS_WEAK_EXTERNAL};
  Weak0.WeakDefTag = 0;
  auto O0 = buildCOFF(IMAGE_FILE_MACHINE_AMD64, {S0}, {Def0, Weak0}, {});
  CoffSecSpec S1{".text", 0x20, TextChars, 0xBB};
  CoffSymSpec Def1{"wdef1", 0, 1, IMAGE_SYM_CLASS_EXTERNAL};
  CoffSymSpec Weak1{"wfn1", 0, 0, IMAGE_SYM_CLASS_WEAK_EXTERNAL};
  Weak1.WeakDefTag = 0;
  auto O1 = buildCOFF(IMAGE_FILE_MACHINE_AMD64, {S1}, {Def1, Weak1}, {});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  SmallVector<char, 0> Out;
  {
    raw_svector_ostream OS(Out);
    ASSERT_TRUE(mergeObjects(Bufs, OS, Format::COFF));
  }
  std::string Err;
  ASSERT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::COFF, {}, &Err))
      << Err;

  uint32_t SymPtr = getU32(Out.data() + 8);
  uint32_t NSym = getU32(Out.data() + 12);
  bool Patched = false;
  for (uint32_t k = 0; k + 1 < NSym;) {
    char *P = Out.data() + SymPtr + (uint64_t)k * 18;
    std::string Nm(P, strnlen(P, 8));
    uint8_t Storage = (uint8_t)P[16];
    uint8_t NAux = (uint8_t)P[17];
    if (Nm == "wfn1" && Storage == IMAGE_SYM_CLASS_WEAK_EXTERNAL && NAux >= 1) {
      char *Aux = Out.data() + SymPtr + (uint64_t)(k + 1) * 18;
      Aux[0] = Aux[1] = Aux[2] = Aux[3] = 0; // TagIndex = 0 -> wrong default
      Patched = true;
      break;
    }
    k += 1u + NAux;
  }
  ASSERT_TRUE(Patched) << "could not locate wfn1 weak external to corrupt";

  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Out), Format::COFF, {}, &Err))
      << "verifier accepted a weak external whose aux TagIndex points at the "
         "wrong default (the aux blind spot)";
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

TEST(MergeCOFFVerify, CatchesCollapsedDuplicateNamedSymbol) {
  using namespace COFF;
  uint32_t TextChars = IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE |
                       IMAGE_SCN_MEM_READ | IMAGE_SCN_ALIGN_16BYTES;
  // Two partitions each define a file-local (STATIC) symbol named "dup" — the
  // COFF analogue of two same-named statics.  Ambiguous by name, so the unique
  // anchor skips it; the duplicate-name content anchor must still reject a
  // collapse of every "dup" to 0 (partition 1's then reads 0xAA, not 0xBB).
  CoffSecSpec S0{".text", 0x40, TextChars, 0xAA};
  CoffSecSpec S1{".text", 0x40, TextChars, 0xBB};
  CoffSymSpec D0{"dup", 0, 1, IMAGE_SYM_CLASS_STATIC};
  CoffSymSpec D1{"dup", 0, 1, IMAGE_SYM_CLASS_STATIC};
  auto O0 = buildCOFF(IMAGE_FILE_MACHINE_AMD64, {S0}, {D0}, {});
  auto O1 = buildCOFF(IMAGE_FILE_MACHINE_AMD64, {S1}, {D1}, {});

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

  auto Collapsed = Out;
  ASSERT_TRUE(patchAllCoffSymValues(Collapsed, "dup", 0x0));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Collapsed), Format::COFF, {}, &Err))
      << "COFF verifier accepted a collapsed duplicate-named symbol offset";
}

TEST(MergeCOFFVerify, CatchesCollapsedBssSymbolOffset) {
  using namespace COFF;
  // COFF .bss twin of the historical collapse.  Two uninitialized externals
  // share one input .bss (IMAGE_SCN_CNT_UNINITIALIZED_DATA, no on-disk bytes,
  // so the content anchor skips it).  Only the same-section relative-distance
  // invariant can catch collapsing bss_b onto bss_a.
  uint32_t BssChars = IMAGE_SCN_CNT_UNINITIALIZED_DATA | IMAGE_SCN_MEM_READ |
                      IMAGE_SCN_MEM_WRITE | IMAGE_SCN_ALIGN_16BYTES;
  CoffSecSpec B{".bss", 0x40, BssChars};
  CoffSymSpec VA{"bss_a", 0x0, 1, IMAGE_SYM_CLASS_EXTERNAL};
  CoffSymSpec VB{"bss_b", 0x20, 1, IMAGE_SYM_CLASS_EXTERNAL};
  auto Obj = buildCOFF(IMAGE_FILE_MACHINE_AMD64, {B}, {VA, VB}, {});

  SmallVector<SmallVector<char, 0>, 1> Bufs;
  Bufs.push_back(Obj);
  SmallVector<char, 0> Out;
  raw_svector_ostream OS(Out);
  ASSERT_TRUE(mergeObjects(Bufs, OS, Format::COFF)); // internal verify accepts

  {
    CoffView V = parseCOFF(Out);
    const CoffParsedSym *PA = V.findSym("bss_a");
    const CoffParsedSym *PB = V.findSym("bss_b");
    ASSERT_NE(PA, nullptr);
    ASSERT_NE(PB, nullptr);
    EXPECT_EQ(PA->Value, 0x0u);
    EXPECT_EQ(PB->Value, 0x20u);
  }

  std::string Err;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::COFF, {}, &Err))
      << Err;

  auto Collapsed = Out;
  ASSERT_TRUE(patchCoffSymValue(Collapsed, "bss_b", 0x0));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Collapsed), Format::COFF, {}, &Err))
      << "verifier accepted a collapsed COFF .bss symbol offset";
}

TEST(MergeCOFFVerify, CatchesCollapsedSingletonBssDistinctSections) {
  using namespace COFF;
  // COFF parity for the singleton .bss blind spot: two objects each with a
  // single uninitialized external in their own .bss.  One symbol per input
  // section starves the relative-distance invariant, and BSS has no on-disk
  // bytes for the content anchor — only the disjoint-range invariant can reject
  // collapsing g1 onto g0's slot.
  uint32_t BssChars = IMAGE_SCN_CNT_UNINITIALIZED_DATA | IMAGE_SCN_MEM_READ |
                      IMAGE_SCN_MEM_WRITE | IMAGE_SCN_ALIGN_16BYTES;
  CoffSecSpec B0{".bss", 0x40, BssChars};
  CoffSecSpec B1{".bss", 0x40, BssChars};
  CoffSymSpec V0{"g0", 0x0, 1, IMAGE_SYM_CLASS_EXTERNAL};
  CoffSymSpec V1{"g1", 0x0, 1, IMAGE_SYM_CLASS_EXTERNAL};
  auto O0 = buildCOFF(IMAGE_FILE_MACHINE_AMD64, {B0}, {V0}, {});
  auto O1 = buildCOFF(IMAGE_FILE_MACHINE_AMD64, {B1}, {V1}, {});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  SmallVector<char, 0> Out;
  raw_svector_ostream OS(Out);
  ASSERT_TRUE(mergeObjects(Bufs, OS, Format::COFF)); // internal verify accepts

  {
    CoffView V = parseCOFF(Out);
    const CoffParsedSym *P0 = V.findSym("g0");
    const CoffParsedSym *P1 = V.findSym("g1");
    ASSERT_NE(P0, nullptr);
    ASSERT_NE(P1, nullptr);
    EXPECT_EQ(P0->Value, 0x0u);
    EXPECT_EQ(P1->Value, 0x40u);
  }

  std::string Err;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::COFF, {}, &Err))
      << Err;

  auto Collapsed = Out;
  ASSERT_TRUE(patchCoffSymValue(Collapsed, "g1", 0x0));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Collapsed), Format::COFF, {}, &Err))
      << "verifier accepted a collapsed singleton COFF .bss symbol across "
         "partitions";
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

TEST(MergeCOFFVerify, CatchesCollapsedSectionRelativeReloc) {
  using namespace COFF;
  uint32_t TextChars = IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE |
                       IMAGE_SCN_MEM_READ | IMAGE_SCN_ALIGN_16BYTES;
  uint32_t DataChars =
      IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_ALIGN_16BYTES;
  // Section-relative relocation: each partition's .text references the .rdata
  // *section symbol* — a STATIC symbol named after the section (".rdata"), not a
  // function — at offset 0x10.  This is the COFF analogue of an ELF STT_SECTION
  // target: the name is *not unique* in the merged output (every partition
  // contributes its own ".rdata" section symbol), so the reloc is keyed only by
  // the section-name string.  f0/f1 anchor each .text; after merge the sites sit
  // at 0x10 (p0) and 0x50 (p1, shifted past p0's 0x40 .text).  Collapsing the
  // reloc offsets to 0 must be caught even though the target symbol is ambiguous
  // by name — the gap the ELF/MachO verifiers just closed, asserted here too so
  // all three object paths reject this class symmetrically.
  CoffSecSpec T0{".text", 0x40, TextChars, 0xAA};
  CoffSecSpec D0{".rdata", 0x20, DataChars, 0xCC};
  CoffSecSpec T1{".text", 0x40, TextChars, 0xBB};
  CoffSecSpec D1{".rdata", 0x20, DataChars, 0xDD};
  CoffSymSpec F0{"f0", 0, 1, IMAGE_SYM_CLASS_EXTERNAL};
  CoffSymSpec F1{"f1", 0, 1, IMAGE_SYM_CLASS_EXTERNAL};
  // Defined STATIC section symbol for .rdata (section 2) — the reloc target.
  CoffSymSpec SD0{".rdata", 0, 2, IMAGE_SYM_CLASS_STATIC};
  CoffSymSpec SD1{".rdata", 0, 2, IMAGE_SYM_CLASS_STATIC};
  CoffRelSpec R0{0, 0x10, ".rdata", (uint16_t)IMAGE_REL_AMD64_REL32};
  CoffRelSpec R1{0, 0x10, ".rdata", (uint16_t)IMAGE_REL_AMD64_REL32};
  auto O0 = buildCOFF(IMAGE_FILE_MACHINE_AMD64, {T0, D0}, {F0, SD0}, {R0});
  auto O1 = buildCOFF(IMAGE_FILE_MACHINE_AMD64, {T1, D1}, {F1, SD1}, {R1});

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
      << "COFF verifier accepted a collapsed section-relative reloc offset";
}

// ---------------------------------------------------------------------------
// Mach-O semantic tests — same invariant on the Darwin object path, where the
// symbol n_value fix-up (section-relative → segment-relative) lived.
// ---------------------------------------------------------------------------

// P0 arch-consistency guard (COFF): mixing IMAGE_FILE_MACHINE values must be
// refused.  COFF's only caller today is parallel codegen (always one machine),
// so this guards a future general path against a cross-ISA object.
TEST(MergeCOFF, RefusesMixedMachine) {
  using namespace COFF;
  uint32_t TextChars = IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE |
                       IMAGE_SCN_MEM_READ | IMAGE_SCN_ALIGN_16BYTES;
  CoffSecSpec S0{".text", 0x20, TextChars};
  CoffSecSpec S1{".text", 0x20, TextChars};
  CoffSymSpec P0{"p0", 0, 1, IMAGE_SYM_CLASS_EXTERNAL};
  CoffSymSpec P1{"p1", 0, 1, IMAGE_SYM_CLASS_EXTERNAL};
  auto Obj0 = buildCOFF(IMAGE_FILE_MACHINE_AMD64, {S0}, {P0}, {});

  // Positive control: same machine merges.
  {
    auto C1 = buildCOFF(IMAGE_FILE_MACHINE_AMD64, {S1}, {P1}, {});
    SmallVector<SmallVector<char, 0>, 2> Bufs;
    Bufs.push_back(Obj0);
    Bufs.push_back(std::move(C1));
    SmallVector<char, 0> Out;
    raw_svector_ostream OS(Out);
    EXPECT_TRUE(mergeObjects(Bufs, OS, Format::COFF));
  }

  auto Obj1 = buildCOFF(IMAGE_FILE_MACHINE_ARM64, {S1}, {P1}, {});
  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(std::move(Obj0));
  Bufs.push_back(std::move(Obj1));
  SmallVector<char, 0> Out;
  raw_svector_ostream OS(Out);
  EXPECT_FALSE(mergeObjects(Bufs, OS, Format::COFF))
      << "merger accepted mixed COFF machine inputs";
}

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

// nlist_64::n_sect is a uint8_t, so a Mach-O object can address at most 255
// sections (0 == NO_SECT).  The merger must refuse to emit more rather than
// silently truncate every section number past 255 — the Mach-O twin of the ELF
// e_shnum guard and the COFF NumberOfSections guard.  255 distinct
// (segment, section) sections is the boundary that must still merge; 256 must be
// refused (and that refusal lets the parallel-codegen caller fall back to serial
// codegen instead of emitting a wrong object).
// P0 arch-consistency guard (Mach-O): mixing cputype must be refused — besides
// the cross-ISA body, it would corrupt the IsARM64-gated ARM64_RELOC_ADDEND /
// in-place fixup logic.
TEST(MergeMachO, RefusesMixedCpuType) {
  namespace MO = llvm::MachO;
  uint32_t TextFlags =
      MO::S_ATTR_PURE_INSTRUCTIONS | MO::S_ATTR_SOME_INSTRUCTIONS;
  MachoSecSpec S0{"__TEXT", "__text", 0x20, 4, TextFlags};
  MachoSecSpec S1{"__TEXT", "__text", 0x20, 4, TextFlags};
  uint8_t DefExt = MO::N_SECT | MO::N_EXT;
  MachoSymSpec P0{"_p0", DefExt, 1, 0, 0};
  MachoSymSpec P1{"_p1", DefExt, 1, 0, 0};
  auto Obj0 =
      buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {S0}, {P0});

  // Positive control: same cputype merges.
  {
    auto C1 =
        buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {S1}, {P1});
    SmallVector<SmallVector<char, 0>, 2> Bufs;
    Bufs.push_back(Obj0);
    Bufs.push_back(std::move(C1));
    SmallVector<char, 0> Out;
    raw_svector_ostream OS(Out);
    EXPECT_TRUE(mergeObjects(Bufs, OS, Format::MachO64));
  }

  auto Obj1 = buildMachO(MO::CPU_TYPE_X86_64, MO::CPU_SUBTYPE_X86_64_ALL, {S1},
                         {P1});
  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(std::move(Obj0));
  Bufs.push_back(std::move(Obj1));
  SmallVector<char, 0> Out;
  raw_svector_ostream OS(Out);
  EXPECT_FALSE(mergeObjects(Bufs, OS, Format::MachO64))
      << "merger accepted mixed Mach-O cputype inputs";
}

// P1 (Mach-O): same ODR-violation refuse as ELF — two strong external defs of
// one name would be silently resolved to one by the priority dedup.
TEST(MergeMachO, RefusesMultipleStrongDefinitions) {
  namespace MO = llvm::MachO;
  uint32_t TextFlags =
      MO::S_ATTR_PURE_INSTRUCTIONS | MO::S_ATTR_SOME_INSTRUCTIONS;
  MachoSecSpec S0{"__TEXT", "__text", 0x20, 4, TextFlags};
  MachoSecSpec S1{"__TEXT", "__text", 0x20, 4, TextFlags};
  uint8_t DefExt = MO::N_SECT | MO::N_EXT;
  MachoSymSpec D0{"_dup", DefExt, 1, 0, 0};
  MachoSymSpec D1{"_dup", DefExt, 1, 0, 0};
  auto O0 =
      buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {S0}, {D0});
  auto O1 =
      buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {S1}, {D1});
  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(std::move(O0));
  Bufs.push_back(std::move(O1));
  SmallVector<char, 0> Out;
  raw_svector_ostream OS(Out);
  EXPECT_FALSE(mergeObjects(Bufs, OS, Format::MachO64))
      << "merger accepted two strong Mach-O definitions of the same symbol";
}

TEST(MergeMachO, Accepts255SectionsRefuses256) {
  namespace MO = llvm::MachO;
  auto buildN = [](unsigned NSec) {
    std::vector<MachoSecSpec> Secs;
    for (unsigned i = 0; i < NSec; ++i)
      Secs.push_back({"__TEXT", "__s" + std::to_string(i), 4, 0,
                      (uint32_t)MO::S_REGULAR, (uint8_t)0});
    return buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, Secs, {});
  };

  {
    SmallVector<SmallVector<char, 0>, 1> Bufs;
    Bufs.push_back(buildN(255));
    SmallVector<char, 0> Out;
    raw_svector_ostream OS(Out);
    EXPECT_TRUE(mergeObjects(Bufs, OS, Format::MachO64))
        << "255 distinct sections is within the n_sect limit and must merge";
  }
  {
    SmallVector<SmallVector<char, 0>, 1> Bufs;
    Bufs.push_back(buildN(256));
    SmallVector<char, 0> Out;
    raw_svector_ostream OS(Out);
    EXPECT_FALSE(mergeObjects(Bufs, OS, Format::MachO64))
        << "256 sections overflow the 8-bit n_sect; the merger must refuse "
           "rather than emit a truncated object";
  }
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

TEST(MergeMachOSemantic, MergeIsDeterministic) {
  namespace MO = llvm::MachO;
  // Identical inputs must produce byte-identical output (see the ELF analogue).
  uint32_t TextFlags =
      MO::S_ATTR_PURE_INSTRUCTIONS | MO::S_ATTR_SOME_INSTRUCTIONS;
  MachoSecSpec S0{"__TEXT", "__text", 0x40, 4, TextFlags, 0xAA};
  MachoSecSpec C0{"__DATA", "__data", 0x20, 3, 0u, 0xCC};
  MachoSecSpec B0{"__DATA", "__bss", 0x30, 4, MO::S_ZEROFILL};
  MachoSecSpec S1{"__TEXT", "__text", 0x20, 4, TextFlags, 0xBB};
  uint8_t DefExt = MO::N_SECT | MO::N_EXT;
  MachoSymSpec F0{"_f0", DefExt, 1, 0, 0};
  MachoSymSpec G0{"_g0", DefExt, 2, 0, 0};
  MachoSymSpec V0{"_v0", DefExt, 3, 0, 0};
  MachoSymSpec F1{"_f1", DefExt, 1, 0, 0};
  auto O0 = buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL,
                       {S0, C0, B0}, {F0, G0, V0});
  auto O1 = buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {S1}, {F1});
  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  SmallVector<char, 0> Out1, Out2;
  {
    raw_svector_ostream OS(Out1);
    ASSERT_TRUE(mergeObjects(Bufs, OS, Format::MachO64));
  }
  {
    raw_svector_ostream OS(Out2);
    ASSERT_TRUE(mergeObjects(Bufs, OS, Format::MachO64));
  }
  ASSERT_EQ(Out1.size(), Out2.size());
  EXPECT_EQ(0, std::memcmp(Out1.data(), Out2.data(), Out1.size()))
      << "Mach-O merge is not deterministic";
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

TEST(MergeMachOVerify, CatchesCollapsedDuplicateNamedSymbol) {
  namespace MO = llvm::MachO;
  uint32_t TextFlags =
      MO::S_ATTR_PURE_INSTRUCTIONS | MO::S_ATTR_SOME_INSTRUCTIONS;
  MachoSecSpec S0{"__TEXT", "__text", 0x40, 4, TextFlags, 0xAA};
  MachoSecSpec S1{"__TEXT", "__text", 0x40, 4, TextFlags, 0xBB};
  // Local (no N_EXT) symbols sharing the name "_dup": the Darwin analogue of
  // two file-local statics.  Ambiguous by name → skipped by the unique anchor;
  // the duplicate-name content anchor must still reject a collapse onto one
  // location (partition 1's then reads 0xAA instead of its own 0xBB).
  uint8_t DefLocal = MO::N_SECT;
  MachoSymSpec D0{"_dup", DefLocal, 1, 0, 0};
  MachoSymSpec D1{"_dup", DefLocal, 1, 0, 0};
  auto O0 =
      buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {S0}, {D0});
  auto O1 =
      buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {S1}, {D1});

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

  // The first "_dup" (partition 0) sits at the merged __text base over 0xAA.
  MachoView V = parseMachO(Out);
  const MachoParsedSym *PD = V.findSym("_dup");
  ASSERT_NE(PD, nullptr);

  // Collapse every "_dup" onto partition 0's location: partition 1's window now
  // reads 0xAA instead of its own 0xBB → must be rejected.
  auto Collapsed = Out;
  ASSERT_TRUE(patchAllMachoSymValues(Collapsed, "_dup", PD->Value));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Collapsed), Format::MachO64, {}, &Err))
      << "Mach-O verifier accepted a collapsed duplicate-named symbol offset";
}

TEST(MergeMachOVerify, CatchesCorruptedDysymtabRanges) {
  namespace MO = llvm::MachO;
  // The merger sorts symbols into local | external-defined | undefined and
  // writes the LC_DYSYMTAB ranges describing that partition.  A bug there would
  // silently mislead consumers about which symbols are exported vs. undefined
  // without touching any byte the content anchor inspects, so the verifier
  // audits the ranges directly.  Two defined externals merge into a table of
  // exactly two external-defined symbols (no locals, no undefs).
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
  ASSERT_TRUE(mergeObjects(Bufs, OS, Format::MachO64));
  std::string Err;
  ASSERT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::MachO64, {}, &Err))
      << Err;

  // (a) Non-contiguous ranges: claim one local without shifting iextdefsym, so
  // the local and external-defined ranges overlap at symbol 0.
  auto BadContig = Out;
  ASSERT_TRUE(patchMachoDysymtab(BadContig, 0, 1, 0, 2, 2, 0));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(BadContig), Format::MachO64, {}, &Err))
      << "verifier accepted non-contiguous LC_DYSYMTAB ranges";

  // (b) Contiguous but mis-classified: an external-defined symbol parked in the
  // local range (local[0,1), extdef[1,2), undef[2,2)).
  auto BadClass = Out;
  ASSERT_TRUE(patchMachoDysymtab(BadClass, 0, 1, 1, 1, 2, 0));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(BadClass), Format::MachO64, {}, &Err))
      << "verifier accepted an external symbol inside the local range";
}

TEST(MergeMachOVerify, CatchesCollapsedZerofillSymbolOffset) {
  namespace MO = llvm::MachO;
  // Mach-O __bss twin of the historical collapse.  Two S_ZEROFILL globals share
  // one input section (no on-disk bytes, so the content anchor skips them).
  // Only the same-section relative-distance invariant can catch collapsing
  // _bss_b onto _bss_a.  n_value is segment-relative, so "collapse" means
  // setting _bss_b's n_value back to the section base (= _bss_a's value).
  MachoSecSpec B{"__DATA", "__bss", 0x40, 4, MO::S_ZEROFILL};
  uint8_t DefExt = MO::N_SECT | MO::N_EXT;
  MachoSymSpec VA{"_bss_a", DefExt, 1, 0x0, 0};
  MachoSymSpec VB{"_bss_b", DefExt, 1, 0x20, 0};
  auto Obj =
      buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {B}, {VA, VB});

  SmallVector<SmallVector<char, 0>, 1> Bufs;
  Bufs.push_back(Obj);
  SmallVector<char, 0> Out;
  raw_svector_ostream OS(Out);
  ASSERT_TRUE(mergeObjects(Bufs, OS, Format::MachO64)); // internal verify accepts

  uint64_t BaseVal = 0;
  {
    MachoView V = parseMachO(Out);
    const MachoParsedSym *PA = V.findSym("_bss_a");
    const MachoParsedSym *PB = V.findSym("_bss_b");
    ASSERT_NE(PA, nullptr);
    ASSERT_NE(PB, nullptr);
    EXPECT_EQ(PB->Value - PA->Value, 0x20u);
    BaseVal = PA->Value;
  }

  std::string Err;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::MachO64, {}, &Err))
      << Err;

  auto Collapsed = Out;
  ASSERT_TRUE(patchMachoSymValue(Collapsed, "_bss_b", BaseVal));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Collapsed), Format::MachO64, {}, &Err))
      << "verifier accepted a collapsed Mach-O zerofill symbol offset";
}

TEST(MergeMachOVerify, CatchesCollapsedSingletonZerofillDistinctSections) {
  namespace MO = llvm::MachO;
  // Mach-O parity for the singleton blind spot: two objects each with a single
  // S_ZEROFILL global in their own __bss.  One symbol per input section starves
  // the relative-distance invariant, and zerofill has no on-disk bytes — only
  // the disjoint-range invariant catches collapsing _g1 back onto the shared
  // section base (the segment-relative form of an offset collapse).
  MachoSecSpec B0{"__DATA", "__bss", 0x40, 4, MO::S_ZEROFILL};
  MachoSecSpec B1{"__DATA", "__bss", 0x40, 4, MO::S_ZEROFILL};
  uint8_t DefExt = MO::N_SECT | MO::N_EXT;
  MachoSymSpec V0{"_g0", DefExt, 1, 0x0, 0};
  MachoSymSpec V1{"_g1", DefExt, 1, 0x0, 0};
  auto O0 = buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {B0}, {V0});
  auto O1 = buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {B1}, {V1});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  SmallVector<char, 0> Out;
  raw_svector_ostream OS(Out);
  ASSERT_TRUE(mergeObjects(Bufs, OS, Format::MachO64)); // internal verify accepts

  uint64_t BaseVal = 0;
  {
    MachoView V = parseMachO(Out);
    const MachoParsedSym *P0 = V.findSym("_g0");
    const MachoParsedSym *P1 = V.findSym("_g1");
    ASSERT_NE(P0, nullptr);
    ASSERT_NE(P1, nullptr);
    EXPECT_EQ(P1->Value - P0->Value, 0x40u); // p1's __bss shifted past p0's
    BaseVal = P0->Value;
  }

  std::string Err;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::MachO64, {}, &Err))
      << Err;

  auto Collapsed = Out;
  ASSERT_TRUE(patchMachoSymValue(Collapsed, "_g1", BaseVal));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Collapsed), Format::MachO64, {}, &Err))
      << "verifier accepted a collapsed singleton Mach-O zerofill symbol across "
         "partitions";
}

TEST(MergeMachOVerify, AcceptsDuplicateLocalLabelWithRelocInFirstWindow) {
  namespace MO = llvm::MachO;
  // Regression for a verifier *false positive* found by the randomized
  // execution fuzzer (MergeFuzzExecution).  LLVM's Mach-O backend emits a local
  // label 'ltmp0' at the start of every object's __text, so merging N partition
  // objects yields N same-named locals — the ambiguous (duplicate-name) verify
  // path.  When a later module's true 'ltmp0' begins with a relocated
  // instruction (a call here), its content window overlaps a relocation site and
  // is correctly skipped as undecidable, while the *earlier* module's 'ltmp0' is
  // decidable but holds different bytes.  The verifier used to read that as "no
  // copy matches → collapse" and reject a perfectly correct merge, forcing a
  // spurious fallback to serial codegen on macOS.  It must now ACCEPT this shape.
  // (Proven correct independently: with verify off, every such merge executes
  // byte-identically to a plain link.)
  uint32_t TextFlags =
      MO::S_ATTR_PURE_INSTRUCTIONS | MO::S_ATTR_SOME_INSTRUCTIONS;
  MachoSecSpec S0{"__TEXT", "__text", 0x40, 4, TextFlags, 0xAA};
  MachoSecSpec S1{"__TEXT", "__text", 0x40, 4, TextFlags, 0xBB};
  uint8_t DefLocal = MO::N_SECT;
  uint8_t UndefExt = MO::N_EXT;
  MachoSymSpec L0{"ltmp0", DefLocal, 1, 0, 0}; // module 0's start label
  MachoSymSpec L1{"ltmp0", DefLocal, 1, 0, 0}; // module 1's start label (dup)
  MachoSymSpec Ext{"_ext", UndefExt, 0, 0, 0};
  // A relocation in module 1's first 16 bytes: after merge its 'ltmp0' lands at
  // 0x40 and that window overlaps the reloc site, so it is skipped — the exact
  // trigger for the old false positive.
  MachoRelSpec R1{0, 0, "_ext", (uint8_t)MO::ARM64_RELOC_BRANCH26, 2, true, -1};
  auto O0 =
      buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {S0}, {L0});
  auto O1 = buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {S1},
                       {L1, Ext}, {R1});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  SmallVector<char, 0> Out;
  raw_svector_ostream OS(Out);
  // Internal verify (default on) must ACCEPT — pre-fix it rejected here, which
  // would make the real merger return false and fall back to serial codegen.
  ASSERT_TRUE(mergeObjects(Bufs, OS, Format::MachO64))
      << "merger false-rejected a correct merge of duplicate local labels whose "
         "true home overlaps a relocation site";
  std::string Err;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::MachO64, {}, &Err))
      << Err;
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

TEST(MergeMachOVerify, CatchesCollapsedSectionRelativeReloc) {
  namespace MO = llvm::MachO;
  uint32_t TextFlags =
      MO::S_ATTR_PURE_INSTRUCTIONS | MO::S_ATTR_SOME_INSTRUCTIONS;
  // Section-relative (non-extern) relocation: __text holds an 8-byte pointer
  // into __const, which the merger rewrites *in place* via its own offset
  // arithmetic (a separate code path from extern relocs, previously unchecked).
  // f0/f1 anchor each __text; merged sites land at 0x10 (p0) and 0x50 (p1).
  MachoSecSpec S0{"__TEXT", "__text", 0x40, 4, TextFlags, 0xAA};
  MachoSecSpec C0{"__DATA", "__const", 0x20, 4, 0u, 0xCC};
  MachoSecSpec S1{"__TEXT", "__text", 0x40, 4, TextFlags, 0xBB};
  MachoSecSpec C1{"__DATA", "__const", 0x20, 4, 0u, 0xDD};
  uint8_t DefExt = MO::N_SECT | MO::N_EXT;
  MachoSymSpec F0{"_f0", DefExt, 1, 0, 0};
  MachoSymSpec F1{"_f1", DefExt, 1, 0, 0};
  // Non-extern UNSIGNED pointer in __text (sec 0) → __const (sec 1), 8 bytes.
  MachoRelSpec R0{0, 0x10, "", (uint8_t)MO::ARM64_RELOC_UNSIGNED, 3,
                  /*Extern=*/false, /*TargetSec=*/1};
  MachoRelSpec R1{0, 0x10, "", (uint8_t)MO::ARM64_RELOC_UNSIGNED, 3,
                  /*Extern=*/false, /*TargetSec=*/1};
  auto O0 = buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {S0, C0},
                       {F0}, {R0});
  auto O1 = buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {S1, C1},
                       {F1}, {R1});

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
      << "Mach-O verifier accepted a collapsed section-relative reloc offset";
}

TEST(MergeMachOVerify, CatchesWrongInPlacePointerDelta) {
  namespace MO = llvm::MachO;
  uint32_t TextFlags =
      MO::S_ATTR_PURE_INSTRUCTIONS | MO::S_ATTR_SOME_INSTRUCTIONS;
  // Distinct __const fills so a mis-targeted pointer reads the wrong bytes.
  MachoSecSpec S0{"__TEXT", "__text", 0x40, 4, TextFlags, 0xAA};
  MachoSecSpec C0{"__DATA", "__const", 0x20, 4, 0u, 0xCC};
  MachoSecSpec S1{"__TEXT", "__text", 0x40, 4, TextFlags, 0xBB};
  MachoSecSpec C1{"__DATA", "__const", 0x20, 4, 0u, 0xDD};
  uint8_t DefExt = MO::N_SECT | MO::N_EXT;
  MachoSymSpec F0{"_f0", DefExt, 1, 0, 0};
  MachoSymSpec F1{"_f1", DefExt, 1, 0, 0};
  // 8-byte absolute (UNSIGNED) non-extern pointer in __text → __const.
  MachoRelSpec R0{0, 0x10, "", (uint8_t)MO::ARM64_RELOC_UNSIGNED, 3,
                  /*Extern=*/false, /*TargetSec=*/1};
  MachoRelSpec R1{0, 0x10, "", (uint8_t)MO::ARM64_RELOC_UNSIGNED, 3,
                  /*Extern=*/false, /*TargetSec=*/1};
  auto O0 = buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {S0, C0},
                       {F0}, {R0});
  auto O1 = buildMachO(MO::CPU_TYPE_ARM64, MO::CPU_SUBTYPE_ARM64_ALL, {S1, C1},
                       {F1}, {R1});

  // Plant a real pointer at each __text reloc site pointing at that partition's
  // __const base, so the merger's in-place fixup has something to relocate (and
  // the verifier's value check engages instead of skipping a garbage pointer).
  {
    MachoView V0 = parseMachO(O0);
    const MachoParsedSec *Cs0 = V0.findSec("__DATA", "__const");
    ASSERT_NE(Cs0, nullptr);
    ASSERT_TRUE(patchMachoSecQword(O0, "__TEXT", "__text", 0x10, Cs0->Addr));
    MachoView V1 = parseMachO(O1);
    const MachoParsedSec *Cs1 = V1.findSec("__DATA", "__const");
    ASSERT_NE(Cs1, nullptr);
    ASSERT_TRUE(patchMachoSecQword(O1, "__TEXT", "__text", 0x10, Cs1->Addr));
  }

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(O0);
  Bufs.push_back(O1);
  SmallVector<char, 0> Out;
  raw_svector_ostream OS(Out);
  // The internal verify runs the value check too; a good merge must pass it.
  ASSERT_TRUE(mergeObjects(Bufs, OS, Format::MachO64));
  std::string Err;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                          ArrayRef<char>(Out), Format::MachO64, {}, &Err))
      << Err;

  // Corrupt p0's merged in-place pointer (at __text+0x10) to 0 — now it points
  // into __text rather than __const.  The site is unchanged, so only the value
  // check can catch this.
  auto Bad = Out;
  ASSERT_TRUE(patchMachoSecQword(Bad, "__TEXT", "__text", 0x10, 0));
  Err.clear();
  EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Bufs),
                           ArrayRef<char>(Bad), Format::MachO64, {}, &Err))
      << "value check missed a mis-targeted in-place pointer";
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

// ===========================================================================
// Differential tests against the bundled LLD relocatable linker (`-r`).
//
// The synthetic suites above prove the merger and its self-verifier agree on
// hand-built objects.  This suite raises the bar: it compiles *real* objects
// with the bundled frontend, then uses the bundled, battle-tested LLD `-r`
// path as an INDEPENDENT producer of a known-good relocatable merge of the
// exact same inputs.  Both the in-process merger's output and LLD's output
// must satisfy verifyMerge(), which anchors every output symbol/relocation
// back to the input bytes it came from.  Consequences:
//
//   * verifyMerge() must never false-reject a real, correct merge — a false
//     reject would make the auto-LTO pipeline spuriously fall back to serial
//     codegen on perfectly good objects.  Feeding it LLD's output proves this
//     on a second, independent linker.
//   * Because both outputs are proven faithful to the *same* inputs, they are
//     transitively semantically equivalent — without a brittle byte/structure
//     diff between two linkers that legitimately lay sections out differently.
//   * The historical offset-collapse bug is shown to be caught on real,
//     linker-shaped objects, not just synthetic ones.
//
// Only ELF and MachO are exercised: the bundled COFF driver requires a full PE
// link (it errors "subsystem must be defined") and exposes no clean `-r`, so
// COFF keeps the synthetic verify suite above.  Any target whose toolchain is
// unavailable in the running environment GTEST_SKIPs instead of failing, so the
// suite contributes coverage wherever it can (e.g. ELF+MachO on a macOS dev
// box, ELF on Linux CI) and never breaks a host that lacks a cross sysroot.
// ===========================================================================
#ifdef NEVERC_BINARY
namespace {

// A unique scratch directory that removes itself on scope exit.
struct ScratchDir {
  SmallString<128> Path;
  bool Ok = false;
  ScratchDir() { Ok = !sys::fs::createUniqueDirectory("nvk-merge-diff", Path); }
  ~ScratchDir() {
    if (Ok)
      sys::fs::remove_directories(Path);
  }
  std::string file(const Twine &Name) const {
    SmallString<160> P(Path);
    sys::path::append(P, Name);
    return std::string(P.str());
  }
};

// Spawn the bundled neverc with Args (argv[0] is prepended automatically).
// stdout+stderr are routed to a scratch log so a skipped/failed cross-compile
// does not pollute test output.  Returns the child exit code, or -1 if the
// process could not be launched at all.
int runNeverc(const ScratchDir &Dir, ArrayRef<StringRef> Args) {
  SmallVector<StringRef, 24> Argv;
  Argv.push_back(StringRef(NEVERC_BINARY));
  Argv.append(Args.begin(), Args.end());
  std::string LogPath = Dir.file("spawn.log");
  // {stdin, stdout, stderr}: null StringRef = inherit, real path = redirect.
  StringRef Redirects[3] = {StringRef(), StringRef(LogPath), StringRef(LogPath)};
  bool Failed = false;
  int RC = sys::ExecuteAndWait(StringRef(NEVERC_BINARY), Argv, /*Env=*/{},
                               Redirects, /*SecondsToWait=*/120,
                               /*MemoryLimit=*/0, /*ErrMsg=*/nullptr, &Failed);
  return Failed ? -1 : RC;
}

bool readObj(StringRef Path, SmallVectorImpl<char> &Out) {
  auto MB = MemoryBuffer::getFile(Path, /*IsText=*/false,
                                  /*RequiresNullTerminator=*/false);
  if (!MB)
    return false;
  StringRef D = (*MB)->getBuffer();
  Out.assign(D.begin(), D.end());
  return !Out.empty();
}

// Compile `Src` to a real (non-bitcode) relocatable object for `Target`.
// `-fno-lto` is essential: the default auto-LTO path emits a bitcode wrapper,
// not the machine-code object the merger consumes.
bool compileRealObj(const ScratchDir &Dir, StringRef Stem, StringRef Src,
                    StringRef Target, std::string &ObjPath,
                    SmallVectorImpl<char> &Bytes) {
  std::string CPath = Dir.file(Stem + ".c");
  {
    std::error_code EC;
    raw_fd_ostream OS(CPath, EC);
    if (EC)
      return false;
    OS << Src;
  }
  ObjPath = Dir.file(Stem + ".o");
  SmallVector<StringRef, 12> Args;
  if (!Target.empty()) {
    Args.push_back("-target");
    Args.push_back(Target);
  }
  Args.push_back("-fno-lto");
  Args.push_back("-O2");
  Args.push_back("-c");
  Args.push_back(CPath);
  Args.push_back("-o");
  Args.push_back(ObjPath);
  if (runNeverc(Dir, Args) != 0)
    return false;
  return readObj(ObjPath, Bytes);
}

// Relocatable (`-r`) merge of ObjPaths via the bundled LLD.
bool lldRelocatable(const ScratchDir &Dir, ArrayRef<std::string> ObjPaths,
                    StringRef Target, SmallVectorImpl<char> &Out) {
  std::string OutPath = Dir.file("lld_r.o");
  SmallVector<StringRef, 12> Args;
  if (!Target.empty()) {
    Args.push_back("-target");
    Args.push_back(Target);
  }
  Args.push_back("-r");
  for (const std::string &P : ObjPaths)
    Args.push_back(P);
  Args.push_back("-o");
  Args.push_back(OutPath);
  if (runNeverc(Dir, Args) != 0)
    return false;
  return readObj(OutPath, Out);
}

// Two translation units whose merge stresses the offset arithmetic that
// historically collapsed: several symbols share one .text (default codegen,
// no -ffunction-sections), so B's functions land at a non-zero offset past
// A's, and the cross-TU calls emit relocations whose offsets must re-land
// exactly at their shifted positions.  Beyond plain int code + a global array
// + a .bss array, the bodies deliberately exercise the section/relocation
// shapes most prone to offset bugs, each landing in its own merge-compatible
// output section so the merger's per-section offset tracking is tested broadly:
//   * double constants  -> a .rodata constant pool referenced PC/section-rel
//   * string literals    -> .rodata.str (SHF_MERGE|SHF_STRINGS) of differing len
//   * a static const table indexed at runtime -> .rodata + section-relative reloc
//   * a const function-pointer table          -> .data.rel.ro with absolute
//                                                relocations onto both a global
//                                                and a static (local) function
// LLD `-r` and the in-process merger must agree (via verifyMerge) on every one.
static const char DiffSrcA[] =
    "int shared_helper(int x){ return x*3+1; }\n"
    "static int a_local(int x){ return (x^0x5a5a) + shared_helper(x); }\n"
    "int a_entry(int x){ return a_local(x) + shared_helper(x*2); }\n"
    "int a_data[4] = {11,22,33,44};\n"
    "double a_scale(double x){ return x*3.14159265358979 + 2.71828; }\n"
    "const char *a_name(void){ return \"neverc-merger-A\"; }\n"
    "static const int a_tbl[6] = {2,3,5,7,11,13};\n"
    "int a_pick(int i){ return a_tbl[(unsigned)i % 6u]; }\n"
    "static int (*const a_fns[2])(int) = {a_entry, a_local};\n"
    "int a_dispatch(int i, int x){ return a_fns[i & 1](x); }\n";
static const char DiffSrcB[] =
    "int shared_helper(int);\n"
    "int a_entry(int);\n"
    "int b_entry(int x){ return shared_helper(x) + a_entry(x) + 9; }\n"
    "int b_more(int x){ return b_entry(x) ^ a_entry(x + 1); }\n"
    "long b_bss[8];\n"
    "double b_mix(double x){ return x*1.4142135623 - 0.5772156649; }\n"
    "const char *b_name(void){ return \"neverc-merger-B-longer-string\"; }\n"
    "static const long b_tbl[4] = {100,200,300,400};\n"
    "long b_pick(int i){ return b_tbl[(unsigned)i & 3u]; }\n";

// Shared body: compile the two TUs for `Target`, then assert the merger and
// the bundled LLD `-r` both produce merges the verifier accepts, and that a
// collapsed-offset corruption of LLD's real output is rejected.
void runLldDifferential(StringRef Target, Format Fmt) {
  ScratchDir Dir;
  if (!Dir.Ok)
    GTEST_SKIP() << "could not create scratch directory";

  std::string PA, PB;
  SmallVector<char, 0> OA, OB;
  if (!compileRealObj(Dir, "ta", DiffSrcA, Target, PA, OA) ||
      !compileRealObj(Dir, "tb", DiffSrcB, Target, PB, OB))
    GTEST_SKIP() << "frontend for target '" << Target.str()
                 << "' unavailable in this environment";

  SmallVector<SmallVector<char, 0>, 2> Inputs;
  Inputs.push_back(OA);
  Inputs.push_back(OB);

  // (1) The in-process merger's output is a faithful merge of the inputs.
  SmallVector<char, 0> NvkOut;
  {
    raw_svector_ostream OS(NvkOut);
    ASSERT_TRUE(mergeObjects(Inputs, OS, Fmt))
        << "merger failed on real objects for target '" << Target.str() << "'";
  }
  std::string NvkErr;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Inputs),
                          ArrayRef<char>(NvkOut), Fmt, {}, &NvkErr))
      << "verifier rejected the merger's own output: " << NvkErr;

  // (2) The bundled LLD `-r` merge of the SAME inputs must ALSO verify.  This
  //     is the differential heart: an independent linker's correct relocatable
  //     output proves verifyMerge() does not false-reject real merges, and
  //     hence that the merger's output is semantically equivalent to LLD's.
  SmallVector<char, 0> LldOut;
  if (!lldRelocatable(Dir, {PA, PB}, Target, LldOut))
    GTEST_SKIP() << "bundled -r unavailable for target '" << Target.str()
                 << "'";
  std::string LldErr;
  EXPECT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Inputs),
                          ArrayRef<char>(LldOut), Fmt, {}, &LldErr))
      << "verifier false-rejected the bundled LLD -r output — either verify is "
         "too strict or it fails to model a real -r transform: "
      << LldErr;

  // (3) Collapsing every relocation offset in LLD's known-good output (the
  //     reloc half of the historical bug) must be caught on this real,
  //     linker-shaped object.  Only asserted when the patch actually changed
  //     bytes, so a layout where every offset was already 0 cannot misfire.
  SmallVector<char, 0> Collapsed(LldOut.begin(), LldOut.end());
  bool Patched = (Fmt == Format::ELF64LE)
                     ? patchAllRelaOffsets(Collapsed, 0)
                     : patchAllMachoRelocAddrs(Collapsed, 0);
  bool ReallyChanged = Patched && Collapsed.size() == LldOut.size() &&
                       std::memcmp(Collapsed.data(), LldOut.data(),
                                   LldOut.size()) != 0;
  if (ReallyChanged) {
    std::string CErr;
    EXPECT_FALSE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Inputs),
                             ArrayRef<char>(Collapsed), Fmt, {}, &CErr))
        << "verifier accepted a collapsed-offset corruption of real LLD output";
  }
}

// ===========================================================================
// End-to-end execution equivalence (native host only).
//
// verifyMerge proves the merged object is *structurally* faithful; the LLD
// differential proves verify does not false-reject.  This raises the bar one
// final notch: it LINKS the in-process merger's output into a real executable
// and RUNS it, asserting byte-identical stdout to a plain link of the same
// inputs.  It is the execution-level analogue of the .ko insmod check — the
// historical offset-collapse bug would surface here as wrong output or a crash,
// fully automated, no device required.  Native target only (we execute it).
// ===========================================================================

bool writeBytes(StringRef Path, ArrayRef<char> Bytes) {
  std::error_code EC;
  raw_fd_ostream OS(Path, EC);
  if (EC)
    return false;
  OS.write(Bytes.data(), Bytes.size());
  OS.flush();
  return !OS.has_error();
}

// Link object files into a native executable with the bundled driver.
bool linkExe(const ScratchDir &Dir, ArrayRef<std::string> Objs,
             StringRef OutExe) {
  SmallVector<StringRef, 8> Args;
  for (const std::string &O : Objs)
    Args.push_back(O);
  Args.push_back("-o");
  Args.push_back(OutExe);
  return runNeverc(Dir, Args) == 0;
}

// Run an executable, capturing stdout into Out.  Returns the exit code, or -1
// if the process could not be launched.
int runExeCapture(const ScratchDir &Dir, StringRef Exe, std::string &Out) {
  std::string OutPath = Dir.file("run.out");
  StringRef Redirects[3] = {StringRef(), StringRef(OutPath), StringRef()};
  bool Failed = false;
  int RC = sys::ExecuteAndWait(Exe, {Exe}, /*Env=*/{}, Redirects,
                               /*SecondsToWait=*/60, /*MemoryLimit=*/0,
                               /*ErrMsg=*/nullptr, &Failed);
  if (Failed)
    return -1;
  SmallVector<char, 0> Bytes;
  if (readObj(OutPath, Bytes))
    Out.assign(Bytes.begin(), Bytes.end());
  return RC;
}

// A main TU that calls across the A/B merge boundary in many shapes (direct
// calls, a dispatch through a function-pointer table, .rodata table reads, and
// string-literal returns), so a mis-placed symbol or relocation changes the
// printed checksum or crashes.
static const char DiffSrcMain[] =
    "#include <stdio.h>\n"
    "int a_entry(int); int a_dispatch(int,int); int a_pick(int);\n"
    "int b_entry(int); int b_more(int); long b_pick(int);\n"
    "const char *a_name(void); const char *b_name(void);\n"
    "int main(void){\n"
    "  long acc=0;\n"
    "  for(int i=0;i<50;i++)\n"
    "    acc += a_entry(i)+b_entry(i)+a_dispatch(i&1,i)+a_pick(i)\n"
    "         + (int)b_pick(i)+b_more(i);\n"
    "  printf(\"%ld|%s|%s\\n\", acc, a_name(), b_name());\n"
    "  return 0;\n"
    "}\n";

void runMergedExecutionEquivalence(Format Fmt) {
  ScratchDir Dir;
  if (!Dir.Ok)
    GTEST_SKIP() << "could not create scratch directory";

  std::string PA, PB, PMain;
  SmallVector<char, 0> OA, OB, OMain;
  if (!compileRealObj(Dir, "ea", DiffSrcA, /*Target=*/"", PA, OA) ||
      !compileRealObj(Dir, "eb", DiffSrcB, /*Target=*/"", PB, OB) ||
      !compileRealObj(Dir, "emain", DiffSrcMain, /*Target=*/"", PMain, OMain))
    GTEST_SKIP() << "native frontend unavailable in this environment";

  SmallVector<SmallVector<char, 0>, 2> Inputs;
  Inputs.push_back(OA);
  Inputs.push_back(OB);

  SmallVector<char, 0> Merged;
  {
    raw_svector_ostream OS(Merged);
    ASSERT_TRUE(mergeObjects(Inputs, OS, Fmt)) << "merger failed on real objects";
  }
  // The merge must still self-verify, then prove itself at runtime.
  std::string VErr;
  ASSERT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Inputs),
                          ArrayRef<char>(Merged), Fmt, {}, &VErr))
      << VErr;

  std::string MergedPath = Dir.file("merged.o");
  ASSERT_TRUE(writeBytes(MergedPath, Merged));

  std::string ExeMerged = Dir.file("exe_merged");
  std::string ExePlain = Dir.file("exe_plain");
  if (!linkExe(Dir, {MergedPath, PMain}, ExeMerged))
    GTEST_SKIP() << "native link of the merged object unavailable";
  ASSERT_TRUE(linkExe(Dir, {PA, PB, PMain}, ExePlain))
      << "plain link of the same inputs failed";

  std::string OutMerged, OutPlain;
  int RCm = runExeCapture(Dir, ExeMerged, OutMerged);
  int RCp = runExeCapture(Dir, ExePlain, OutPlain);
  ASSERT_EQ(RCp, 0) << "plain-link executable did not exit cleanly";
  ASSERT_EQ(RCm, 0) << "merged-object executable did not exit cleanly (the "
                       "merge produced a loadable but wrong object)";
  EXPECT_FALSE(OutMerged.empty());
  EXPECT_EQ(OutMerged, OutPlain)
      << "merged-object program output diverged from the plain link — the "
         "merge mis-placed a symbol or relocation";
}

// ===========================================================================
// Randomized cross-module execution differential ("the fuzzer").
//
// The fixed DiffSrc* suite above proves one hand-tuned shape; this generates
// *randomized* multi-module programs and runs the same merge-vs-plain-link
// execution equivalence on each, so unknown offset/relocation blind spots are
// driven out instead of having to be foreseen.  Every seed emits N modules
// whose objects exercise the merger's whole risk surface at once:
//   * many functions per .text (default codegen, no -ffunction-sections), so
//     later functions land at non-zero merged offsets;
//   * cross-module calls to a shared symbol and to module 0's first function,
//     forcing the global-symbol dedup + relocation remap paths;
//   * an initialized array (.data) and an *uninitialized* array (.bss) per
//     module that the bodies read AND write, so a collapsed .bss offset (the
//     class P0's verifier hardening targets) becomes a wrong runtime checksum.
// The merged object must self-verify, load, and print byte-identical output to
// a plain link of the same objects — because both link the identical compiled
// bodies, any divergence is unambiguously a merge bug.  Deterministic seeds
// keep it reproducible in CI; the native frontend/link gate it via GTEST_SKIP.
// ===========================================================================

std::string genFuzzModule(unsigned Seed, unsigned Idx, unsigned NumFns,
                          unsigned ArrLen) {
  std::mt19937 R(Seed * 7919u + Idx);
  std::string S;
  raw_string_ostream OS(S);
  OS << "int shared(int);\n";
  if (Idx != 0)
    OS << "int mod0_f0(int);\n";
  else
    OS << "int shared(int x){ return x * 3 + 1; }\n";
  OS << "int g" << Idx << "[" << ArrLen << "] = {";
  for (unsigned k = 0; k < ArrLen; ++k)
    OS << (k ? "," : "") << (int)(R() % 97);
  OS << "};\n";
  OS << "long b" << Idx << "[" << ArrLen << "];\n";
  for (unsigned f = 0; f < NumFns; ++f) {
    OS << "int mod" << Idx << "_f" << f << "(int x){ unsigned u=(unsigned)x; ";
    OS << "int t = g" << Idx << "[u % " << ArrLen << "u]; ";
    if (f == 0)
      OS << "t += shared(x)";
    else
      OS << "t += mod" << Idx << "_f" << (f - 1) << "(x & 0x3ff)";
    if (Idx != 0)
      OS << " + mod0_f0(x & 0x1ff)";
    for (int op = 0; op < 3; ++op) {
      switch (R() % 4) {
      case 0:
        OS << " + " << (int)(R() % 1000);
        break;
      case 1:
        OS << " ^ " << (int)(R() % 255);
        break;
      case 2:
        OS << " + (int)(u >> " << (1 + R() % 7) << ")";
        break;
      default:
        OS << " - " << (int)(R() % 500);
        break;
      }
    }
    OS << "; return t; }\n";
  }
  OS << "long mod" << Idx << "_sum(int x){ long s = 0; ";
  for (unsigned f = 0; f < NumFns; ++f)
    OS << "s += mod" << Idx << "_f" << f << "(x + " << f << "); ";
  OS << "b" << Idx << "[(unsigned)x % " << ArrLen << "u] = s; ";
  OS << "s += b" << Idx << "[(unsigned)(x + 1) % " << ArrLen << "u]; ";
  OS << "return s; }\n";
  return S;
}

std::string genFuzzMain(unsigned NumMods) {
  std::string S;
  raw_string_ostream OS(S);
  OS << "#include <stdio.h>\n";
  for (unsigned i = 0; i < NumMods; ++i)
    OS << "long mod" << i << "_sum(int);\n";
  OS << "int main(void){ long acc = 0; for (int i = 0; i < 40; i++){ ";
  for (unsigned i = 0; i < NumMods; ++i)
    OS << "acc += mod" << i << "_sum(i + " << i << "); ";
  OS << "} printf(\"%ld\\n\", acc); return 0; }\n";
  return S;
}

void runMergeFuzzExecution(Format Fmt, unsigned Seed) {
  ScratchDir Dir;
  if (!Dir.Ok)
    GTEST_SKIP() << "could not create scratch directory";
  std::mt19937 R(Seed);
  unsigned NumMods = 2 + (R() % 3); // 2..4 mergeable modules

  SmallVector<std::string, 6> ObjPaths;
  SmallVector<SmallVector<char, 0>, 6> Inputs;
  for (unsigned i = 0; i < NumMods; ++i) {
    unsigned NumFns = 3 + (R() % 5); // 3..7 functions
    unsigned ArrLen = 4 + (R() % 12);
    std::string Src = genFuzzModule(Seed, i, NumFns, ArrLen);
    std::string P;
    SmallVector<char, 0> O;
    if (!compileRealObj(Dir, ("m" + Twine(i)).str(), Src, /*Target=*/"", P, O))
      GTEST_SKIP() << "native frontend unavailable in this environment";
    ObjPaths.push_back(P);
    Inputs.push_back(std::move(O));
  }
  std::string PMain;
  SmallVector<char, 0> OMain;
  if (!compileRealObj(Dir, "mmain", genFuzzMain(NumMods), /*Target=*/"", PMain,
                      OMain))
    GTEST_SKIP() << "native frontend unavailable in this environment";

  SmallVector<char, 0> Merged;
  {
    raw_svector_ostream OS(Merged);
    ASSERT_TRUE(mergeObjects(Inputs, OS, Fmt))
        << "seed " << Seed << ": merger failed on " << NumMods << " modules";
  }
  std::string VErr;
  ASSERT_TRUE(verifyMerge(ArrayRef<SmallVector<char, 0>>(Inputs),
                          ArrayRef<char>(Merged), Fmt, {}, &VErr))
      << "seed " << Seed << ": " << VErr;

  std::string MergedPath = Dir.file("merged.o");
  ASSERT_TRUE(writeBytes(MergedPath, Merged));
  std::string ExeMerged = Dir.file("exe_merged");
  std::string ExePlain = Dir.file("exe_plain");
  if (!linkExe(Dir, {MergedPath, PMain}, ExeMerged))
    GTEST_SKIP() << "native link of the merged object unavailable";
  SmallVector<std::string, 7> PlainObjs(ObjPaths.begin(), ObjPaths.end());
  PlainObjs.push_back(PMain);
  ASSERT_TRUE(linkExe(Dir, PlainObjs, ExePlain))
      << "seed " << Seed << ": plain link of the same inputs failed";

  std::string OutMerged, OutPlain;
  int RCm = runExeCapture(Dir, ExeMerged, OutMerged);
  int RCp = runExeCapture(Dir, ExePlain, OutPlain);
  ASSERT_EQ(RCp, 0) << "seed " << Seed << ": plain-link executable crashed";
  ASSERT_EQ(RCm, 0) << "seed " << Seed
                    << ": merged-object executable crashed (loadable but wrong)";
  EXPECT_FALSE(OutMerged.empty());
  EXPECT_EQ(OutMerged, OutPlain)
      << "seed " << Seed
      << ": merged-object output diverged from the plain link — the merge "
         "mis-placed a symbol or relocation";
}

// ===========================================================================
// End-to-end guard for the parallel-codegen -> merger path under STRICT mode.
//
// Every suite above calls the merger in-process on -fno-lto objects.  This one
// drives the *default auto-LTO link*, which is the production path that
// partitions the post-IPO module, codegen's the partitions in parallel, and
// stitches them back with the in-process merger.  NEVERC_PCG_STRICT makes that
// path abort (non-zero exit) on any merge/self-verify failure instead of
// silently falling back to serial codegen — so a reintroduced offset-collapse
// bug becomes a hard CI failure here, rather than a build that merely compiles
// slower while quietly never exercising the merger.  Output is checked against
// a -fno-lto build of the identical sources, so a merge that loads but is wrong
// is also caught.  Native host only; POSIX only (injects the env knob via
// setenv, which MSVC lacks — Windows would GTEST_SKIP at runtime regardless).
// ===========================================================================
#ifndef _WIN32
// Restore an environment variable to its prior value on scope exit, so the
// strict-mode knob never leaks into other tests when the whole gtest binary is
// run in one process.
struct ScopedEnv {
  std::string Name;
  std::string Old;
  bool HadOld;
  ScopedEnv(const char *N, const char *V) : Name(N) {
    const char *Prev = ::getenv(N);
    HadOld = Prev != nullptr;
    if (HadOld)
      Old = Prev;
    ::setenv(N, V, 1);
  }
  ~ScopedEnv() {
    if (HadOld)
      ::setenv(Name.c_str(), Old.c_str(), 1);
    else
      ::unsetenv(Name.c_str());
  }
};

// One module of `NumFns` noinline, deliberately heavy functions.  noinline
// keeps them distinct after whole-program inlining, so the post-IPO module
// still clears the parallel thresholds (FuncCount>=8, TotalWeight>=10000) and
// the partitioner actually engages.
std::string genHeavyModule(unsigned Idx, unsigned NumFns) {
  std::string S;
  raw_string_ostream OS(S);
  OS << "#include <stdint.h>\n";
  for (unsigned f = 0; f < NumFns; ++f) {
    OS << "__attribute__((noinline)) uint64_t heavy_" << Idx << "_" << f
       << "(uint64_t x){\n"
       << "  uint64_t a=x, b=x^0x9e3779b97f4a7c15ULL, c=" << (Idx * 131u + f)
       << "ULL;\n"
       << "  for (uint64_t i=0;i<61;i++){\n"
       << "    a = a*6364136223846793005ULL + 1442695040888963407ULL;\n"
       << "    b ^= (a>>13); b += (a<<7);\n"
       << "    c += (a^b) + (a&b) - (a|b);\n"
       << "    c = (c<<5) | (c>>59);\n"
       << "    a ^= c*0xff51afd7ed558ccdULL;\n"
       << "    b = b*0x100000001b3ULL ^ (c>>17);\n"
       << "  }\n"
       << "  return a^b^c;\n}\n";
  }
  return S;
}

std::string genHeavyMain(unsigned NumMods, unsigned NumFns) {
  std::string S;
  raw_string_ostream OS(S);
  OS << "#include <stdint.h>\n#include <stdio.h>\n";
  for (unsigned m = 0; m < NumMods; ++m)
    for (unsigned f = 0; f < NumFns; ++f)
      OS << "uint64_t heavy_" << m << "_" << f << "(uint64_t);\n";
  OS << "int main(int argc, char **argv){\n"
     << "  (void)argv; uint64_t s=0, k=(uint64_t)argc;\n";
  unsigned i = 0;
  for (unsigned m = 0; m < NumMods; ++m)
    for (unsigned f = 0; f < NumFns; ++f) {
      OS << "  s += heavy_" << m << "_" << f << "(" << i << "ULL+k) ^ heavy_"
         << m << "_" << f << "(" << (i * 7u + 1u) << "ULL+k);\n";
      i++;
    }
  OS << "  printf(\"%llu\\n\",(unsigned long long)s);\n  return 0;\n}\n";
  return S;
}

// Compile+link several sources in ONE neverc invocation; the auto-LTO link
// happens here when -fno-lto is absent.  ExtraArgs (e.g. -fno-lto -O2) precede
// the sources.  Returns true only on a clean (exit 0) compile+link.
bool compileLinkMulti(const ScratchDir &Dir, ArrayRef<std::string> Srcs,
                      ArrayRef<StringRef> ExtraArgs, StringRef OutExe) {
  SmallVector<StringRef, 40> Args;
  Args.append(ExtraArgs.begin(), ExtraArgs.end());
  for (const std::string &S : Srcs)
    Args.push_back(S);
  Args.push_back("-o");
  Args.push_back(OutExe);
  return runNeverc(Dir, Args) == 0;
}

// A module that, beyond heavy .text, defines a cross-module-referenced
// initialized global array (.data) and an uninitialized one (.bss), and whose
// functions both read and write them.  This is deliberately the shape the
// historical offset-collapse bug corrupted: in auto-LTO the partitioner pins
// every global *initializer* to partition 0 and references it as external from
// the others, so the merge must shift each global's symbol value and every
// cross-partition relocation into .data/.bss by exactly its merged section
// offset — the arithmetic that once collapsed to 0 and produced a loadable but
// wrong .ko.  noinline keeps the functions distinct post-IPO so the partitioner
// engages.
std::string genGlobalsModule(unsigned Idx, unsigned NumFns) {
  std::string S;
  raw_string_ostream OS(S);
  OS << "#include <stdint.h>\n";
  OS << "uint64_t gdata_" << Idx << "[8] = {";
  for (unsigned j = 0; j < 8; ++j) {
    uint64_t V = (uint64_t)(Idx * 8u + j) * 0x9e3779b97f4a7c15ULL + 1u;
    OS << (j ? "," : "") << V << "ULL";
  }
  OS << "};\n";
  OS << "uint64_t gbss_" << Idx << "[8];\n";
  for (unsigned f = 0; f < NumFns; ++f) {
    OS << "__attribute__((noinline)) uint64_t gfn_" << Idx << "_" << f
       << "(uint64_t x){\n"
       << "  uint64_t a = x ^ gdata_" << Idx << "[" << (f % 8) << "];\n"
       << "  for (uint64_t i=0;i<53;i++){\n"
       << "    a = a*6364136223846793005ULL + 1442695040888963407ULL;\n"
       << "    a ^= gbss_" << Idx << "[i & 7] + (a>>11);\n"
       << "    a = (a<<7) | (a>>57);\n"
       << "  }\n"
       << "  gbss_" << Idx << "[" << (f % 8) << "] += a;\n"
       << "  return a ^ gbss_" << Idx << "[" << ((f + 1) % 8) << "];\n}\n";
  }
  return S;
}

std::string genGlobalsMain(unsigned NumMods, unsigned NumFns) {
  std::string S;
  raw_string_ostream OS(S);
  OS << "#include <stdint.h>\n#include <stdio.h>\n";
  for (unsigned m = 0; m < NumMods; ++m) {
    OS << "extern uint64_t gdata_" << m << "[8];\n";
    OS << "extern uint64_t gbss_" << m << "[8];\n";
    for (unsigned f = 0; f < NumFns; ++f)
      OS << "uint64_t gfn_" << m << "_" << f << "(uint64_t);\n";
  }
  OS << "int main(int argc, char **argv){\n"
     << "  (void)argv; uint64_t s=0, k=(uint64_t)argc;\n";
  // Write .bss at runtime so it is genuinely uninitialized storage (not
  // constant-foldable) and cross-module addressed.
  for (unsigned m = 0; m < NumMods; ++m)
    OS << "  gbss_" << m << "[k & 7] ^= (uint64_t)(" << (m * 7u + 1u)
       << "ULL + k);\n";
  unsigned i = 0;
  for (unsigned m = 0; m < NumMods; ++m)
    for (unsigned f = 0; f < NumFns; ++f) {
      OS << "  s += gfn_" << m << "_" << f << "(" << i << "ULL+k) ^ gdata_" << m
         << "[" << (f % 8) << "];\n";
      i++;
    }
  // Read .bss across every module after the calls mutated it.
  for (unsigned m = 0; m < NumMods; ++m)
    OS << "  s += gbss_" << m << "[(k+1) & 7];\n";
  OS << "  printf(\"%llu\\n\",(unsigned long long)s);\n  return 0;\n}\n";
  return S;
}

// A module mixing plain heavy functions (weight + multi-partition spread, so the
// merger actually runs) with computed-goto "interpreters".  Each interpreter
// holds a function-local `static const void *tab[]` of label addresses — the
// exact blockaddress-in-a-global-initializer shape Lua's luaV_execute / CPython's
// ceval use.  Global initializers all live in partition 0, so if parallel codegen
// bins such a function into a partition != 0 its blockaddress constants in p0
// collapse to inttoptr(1) and the program jumps to address 1 (the real lua_lto
// SIGTRAP).  The fix pins every address-taken-block function to partition 0; this
// generator exists so a regression of that pinning turns this test red, because
// the object self-verifier *cannot* catch it (the partition object is already
// wrong before the merge).
std::string genComputedGotoModule(unsigned Idx, unsigned NumHeavy,
                                  unsigned NumCG) {
  std::string S;
  raw_string_ostream OS(S);
  OS << "#include <stdint.h>\n";
  // Heavy body matches genHeavyModule's static instruction count (weight is the
  // sum of BB sizes, independent of the loop trip count), so a handful of these
  // per module clears the parallel threshold (TotalWeight>=10000) and the
  // partitioner actually engages — without that the whole thing compiles serially
  // and never exercises the merger or the pin.
  for (unsigned f = 0; f < NumHeavy; ++f)
    OS << "__attribute__((noinline)) uint64_t cgheavy_" << Idx << "_" << f
       << "(uint64_t x){\n"
       << "  uint64_t a=x, b=x^0x9e3779b97f4a7c15ULL, c=" << (Idx * 131u + f)
       << "ULL;\n"
       << "  for (uint64_t i=0;i<61;i++){\n"
       << "    a = a*6364136223846793005ULL + 1442695040888963407ULL;\n"
       << "    b ^= (a>>13); b += (a<<7);\n"
       << "    c += (a^b) + (a&b) - (a|b);\n"
       << "    c = (c<<5) | (c>>59);\n"
       << "    a ^= c*0xff51afd7ed558ccdULL;\n"
       << "    b = b*0x100000001b3ULL ^ (c>>17);\n"
       << "  }\n"
       << "  return a^b^c;\n}\n";
  // The dispatch index (st & 3) is driven by a runtime-evolving PRNG state, not
  // a compile-time-constant program — otherwise -O2 would devirtualize the
  // indirectbr into direct branches and delete the blockaddress table, hiding
  // the bug.  Lua's real luaV_execute is exactly this shape: the next opcode is
  // runtime bytecode, so the table must survive to runtime.
  for (unsigned f = 0; f < NumCG; ++f)
    OS << "__attribute__((noinline)) uint64_t cgvm_" << Idx << "_" << f
       << "(uint64_t x){\n"
       << "  static const void *const tab[] = {&&A,&&B,&&C,&&D,&&E};\n"
       << "  uint64_t acc=x, st=x^" << (Idx * 2654435761u + f + 1u)
       << "ULL; int steps=0;\n"
       << "  goto *tab[st & 3];\n"
       << "A: acc+=st; st=st*6364136223846793005ULL+1442695040888963407ULL;"
          " if(++steps<96) goto *tab[st & 3]; goto E;\n"
       << "B: acc^=(acc>>13); st=st*6364136223846793005ULL+1442695040888963407ULL;"
          " if(++steps<96) goto *tab[st & 3]; goto E;\n"
       << "C: acc+=(acc<<7); st=st*6364136223846793005ULL+1442695040888963407ULL;"
          " if(++steps<96) goto *tab[st & 3]; goto E;\n"
       << "D: acc=(acc<<5)|(acc>>59);"
          " st=st*6364136223846793005ULL+1442695040888963407ULL;"
          " if(++steps<96) goto *tab[st & 3]; goto E;\n"
       << "E: return acc;\n}\n";
  return S;
}

std::string genComputedGotoMain(unsigned NumMods, unsigned NumHeavy,
                                unsigned NumCG) {
  std::string S;
  raw_string_ostream OS(S);
  OS << "#include <stdint.h>\n#include <stdio.h>\n";
  for (unsigned m = 0; m < NumMods; ++m) {
    for (unsigned f = 0; f < NumHeavy; ++f)
      OS << "uint64_t cgheavy_" << m << "_" << f << "(uint64_t);\n";
    for (unsigned f = 0; f < NumCG; ++f)
      OS << "uint64_t cgvm_" << m << "_" << f << "(uint64_t);\n";
  }
  OS << "int main(int argc, char **argv){\n"
     << "  (void)argv; uint64_t s=0, k=(uint64_t)argc;\n";
  unsigned i = 0;
  for (unsigned m = 0; m < NumMods; ++m) {
    for (unsigned f = 0; f < NumHeavy; ++f)
      OS << "  s += cgheavy_" << m << "_" << f << "(" << i++ << "ULL+k);\n";
    for (unsigned f = 0; f < NumCG; ++f)
      OS << "  s += cgvm_" << m << "_" << f << "(" << i++ << "ULL+k);\n";
  }
  OS << "  printf(\"%llu\\n\",(unsigned long long)s);\n  return 0;\n}\n";
  return S;
}
#endif // _WIN32

} // namespace

TEST(MergeDifferentialLLD, ElfArm64FaithfulVsBundledLinker) {
  runLldDifferential("aarch64-linux-gnu", Format::ELF64LE);
}
TEST(MergeDifferentialLLD, ElfX8664FaithfulVsBundledLinker) {
  runLldDifferential("x86_64-linux-gnu", Format::ELF64LE);
}
TEST(MergeDifferentialLLD, MachOArm64FaithfulVsBundledLinker) {
  runLldDifferential("arm64-apple-darwin", Format::MachO64);
}
TEST(MergeDifferentialLLD, MachOX8664FaithfulVsBundledLinker) {
  runLldDifferential("x86_64-apple-darwin", Format::MachO64);
}

// Execution-level proof on the native host: merge -> link -> run must match a
// plain link byte-for-byte.  Picks the host's object format; skips on any host
// whose format the merger does not target.
TEST(MergeDifferentialLLD, NativeMergedExecutionMatchesPlainLink) {
  Triple Host(sys::getProcessTriple());
  if (Host.isOSBinFormatMachO())
    runMergedExecutionEquivalence(Format::MachO64);
  else if (Host.isOSBinFormatELF())
    runMergedExecutionEquivalence(Format::ELF64LE);
  else
    GTEST_SKIP() << "host object format not exercised by this test";
}

// Randomized differential: several seeds, each a fresh multi-module program,
// merged-and-run vs plain-linked-and-run.  Native host only (it executes the
// result).  Skips cleanly where the frontend/linker is unavailable.
TEST(MergeFuzzExecution, RandomCrossModuleNativeMatchesPlainLink) {
  Triple Host(sys::getProcessTriple());
  Format Fmt;
  if (Host.isOSBinFormatMachO())
    Fmt = Format::MachO64;
  else if (Host.isOSBinFormatELF())
    Fmt = Format::ELF64LE;
  else
    GTEST_SKIP() << "host object format not exercised by this test";
  // Default to a CI-friendly seed count; NEVERC_MERGE_FUZZ_SEEDS cranks it for
  // soak testing (e.g. =1000 overnight).  Each seed is an independent program.
  unsigned NumSeeds = 6;
  if (const char *E = ::getenv("NEVERC_MERGE_FUZZ_SEEDS")) {
    unsigned V = (unsigned)strtoul(E, nullptr, 10);
    if (V > 0)
      NumSeeds = V;
  }
  for (unsigned Seed = 1; Seed <= NumSeeds; ++Seed) {
    runMergeFuzzExecution(Fmt, Seed);
    if (::testing::Test::IsSkipped())
      return;
  }
}

// The strict-mode tripwire (see ScopedEnv/genHeavyModule above).  A merger
// regression makes the auto-LTO link abort under NEVERC_PCG_STRICT; a merge
// that loads but is wrong makes the output diverge from the -fno-lto build.
#ifndef _WIN32
TEST(MergeParallelCodegenStrict, MultiFileAutoLtoLinkUnderStrictMode) {
  Triple Host(sys::getProcessTriple());
  if (!Host.isOSBinFormatMachO() && !Host.isOSBinFormatELF())
    GTEST_SKIP() << "host object format not exercised by this test";
  ScratchDir Dir;
  if (!Dir.Ok)
    GTEST_SKIP() << "could not create scratch directory";

  // 24 modules x 16 functions = 384 noinline heavy functions + main, sized to
  // clear the parallel thresholds with margin even after IPO.
  const unsigned NumMods = 24, NumFns = 16;
  SmallVector<std::string, 32> Srcs;
  for (unsigned m = 0; m < NumMods; ++m) {
    std::string CPath = Dir.file(("hm" + Twine(m) + ".c").str());
    std::string Src = genHeavyModule(m, NumFns);
    if (!writeBytes(CPath, ArrayRef<char>(Src.data(), Src.size())))
      GTEST_SKIP() << "could not write source";
    Srcs.push_back(CPath);
  }
  {
    std::string MainPath = Dir.file("hmain.c");
    std::string Src = genHeavyMain(NumMods, NumFns);
    if (!writeBytes(MainPath, ArrayRef<char>(Src.data(), Src.size())))
      GTEST_SKIP() << "could not write source";
    Srcs.push_back(MainPath);
  }

  // Reference: -fno-lto build (serial per-TU codegen, no parallel merger).  Its
  // failure means the host frontend/linker is unavailable -> skip, not fail.
  std::string ExeRef = Dir.file("exe_ref");
  StringRef RefArgs[] = {"-fno-lto", "-O2"};
  if (!compileLinkMulti(Dir, Srcs, RefArgs, ExeRef))
    GTEST_SKIP() << "native frontend/link unavailable in this environment";
  std::string OutRef;
  ASSERT_EQ(runExeCapture(Dir, ExeRef, OutRef), 0)
      << "-fno-lto reference executable did not exit cleanly";
  ASSERT_FALSE(OutRef.empty());

  // Auto-LTO build with STRICT on (caches off so the partitions are really
  // codegen'd + merged on this run).  A merge/self-verify failure now aborts
  // the compiler -> non-zero exit -> this ASSERT fails loudly, instead of the
  // silent serial fallback that would mask the regression.
  std::string ExeLto = Dir.file("exe_lto");
  {
    ScopedEnv Strict("NEVERC_PCG_STRICT", "1");
    ScopedEnv NoCache("NEVERC_LTO_CACHE", "0");
    ScopedEnv NoPCache("NEVERC_LTO_PCACHE", "0");
    StringRef LtoArgs[] = {"-O2"};
    ASSERT_TRUE(compileLinkMulti(Dir, Srcs, LtoArgs, ExeLto))
        << "auto-LTO link failed under NEVERC_PCG_STRICT — the parallel-codegen "
           "merger failed self-verify or could not emit a merged object (a "
           "merger regression); see the scratch spawn.log";
  }
  std::string OutLto;
  ASSERT_EQ(runExeCapture(Dir, ExeLto, OutLto), 0)
      << "auto-LTO executable did not exit cleanly (the merge produced a "
         "loadable but wrong object)";
  EXPECT_EQ(OutLto, OutRef)
      << "auto-LTO program output diverged from the -fno-lto build — the "
         "parallel-codegen merge mis-placed a symbol or relocation";
}

// Same strict-mode tripwire, but the modules carry cross-module-referenced
// .data (initialized) and .bss (uninitialized) globals.  This is the precise
// shape of the historical offset-collapse bug, which mis-shifted .bss/.data
// symbol values and cross-partition relocations to produce a loadable but wrong
// object (the .ko that crashed).  Driving it through the *real* auto-LTO merge
// under STRICT means a reintroduced collapse either aborts the link or makes
// the program diverge from the -fno-lto reference — never a silent pass.
TEST(MergeParallelCodegenStrict, MultiFileAutoLtoGlobalsUnderStrictMode) {
  Triple Host(sys::getProcessTriple());
  if (!Host.isOSBinFormatMachO() && !Host.isOSBinFormatELF())
    GTEST_SKIP() << "host object format not exercised by this test";
  ScratchDir Dir;
  if (!Dir.Ok)
    GTEST_SKIP() << "could not create scratch directory";

  // 20 modules x 12 functions, each touching its own .data + .bss globals,
  // sized to clear the parallel thresholds with margin even after IPO.
  const unsigned NumMods = 20, NumFns = 12;
  SmallVector<std::string, 32> Srcs;
  for (unsigned m = 0; m < NumMods; ++m) {
    std::string CPath = Dir.file(("gm" + Twine(m) + ".c").str());
    std::string Src = genGlobalsModule(m, NumFns);
    if (!writeBytes(CPath, ArrayRef<char>(Src.data(), Src.size())))
      GTEST_SKIP() << "could not write source";
    Srcs.push_back(CPath);
  }
  {
    std::string MainPath = Dir.file("gmain.c");
    std::string Src = genGlobalsMain(NumMods, NumFns);
    if (!writeBytes(MainPath, ArrayRef<char>(Src.data(), Src.size())))
      GTEST_SKIP() << "could not write source";
    Srcs.push_back(MainPath);
  }

  std::string ExeRef = Dir.file("exe_ref");
  StringRef RefArgs[] = {"-fno-lto", "-O2"};
  if (!compileLinkMulti(Dir, Srcs, RefArgs, ExeRef))
    GTEST_SKIP() << "native frontend/link unavailable in this environment";
  std::string OutRef;
  ASSERT_EQ(runExeCapture(Dir, ExeRef, OutRef), 0)
      << "-fno-lto reference executable did not exit cleanly";
  ASSERT_FALSE(OutRef.empty());

  std::string ExeLto = Dir.file("exe_lto");
  {
    ScopedEnv Strict("NEVERC_PCG_STRICT", "1");
    ScopedEnv NoCache("NEVERC_LTO_CACHE", "0");
    ScopedEnv NoPCache("NEVERC_LTO_PCACHE", "0");
    StringRef LtoArgs[] = {"-O2"};
    ASSERT_TRUE(compileLinkMulti(Dir, Srcs, LtoArgs, ExeLto))
        << "auto-LTO link of cross-module .data/.bss globals failed under "
           "NEVERC_PCG_STRICT — the parallel-codegen merger failed self-verify "
           "or could not emit a merged object (a merger regression); see the "
           "scratch spawn.log";
  }
  std::string OutLto;
  ASSERT_EQ(runExeCapture(Dir, ExeLto, OutLto), 0)
      << "auto-LTO executable did not exit cleanly (the merge produced a "
         "loadable but wrong object)";
  EXPECT_EQ(OutLto, OutRef)
      << "auto-LTO program output diverged from the -fno-lto build — the "
         "parallel-codegen merge mis-placed a .data/.bss symbol or a "
         "cross-partition relocation (the historical offset-collapse shape)";
}

// Same strict-mode tripwire for the computed-goto / blockaddress shape — the
// real lua_lto SIGTRAP.  Lua's luaV_execute keeps a `static const void *[]`
// dispatch table of `&&label` addresses; that table's initializer lives in
// partition 0, so binning luaV_execute into any other partition rewrites its
// blockaddress constants to inttoptr(1) and the interpreter jumps to address 1.
// The merger's self-verifier *cannot* see this (the partition object is wrong
// before it is merged), so the only guard is pinning address-taken-block
// functions to partition 0 — and the only regression alarm is an end-to-end run.
// Without the pin, the cgvm_* interpreters binned outside partition 0 jump to 1
// and this program crashes (non-zero exit) or diverges from the -fno-lto build.
TEST(MergeParallelCodegenStrict, MultiFileAutoLtoComputedGotoUnderStrictMode) {
  Triple Host(sys::getProcessTriple());
  if (!Host.isOSBinFormatMachO() && !Host.isOSBinFormatELF())
    GTEST_SKIP() << "host object format not exercised by this test";
  ScratchDir Dir;
  if (!Dir.Ok)
    GTEST_SKIP() << "could not create scratch directory";

  // 24 modules x (16 heavy + 4 computed-goto): 384 plain heavy functions (the
  // proven scale that clears TotalWeight>=10000 so the partitioner engages and
  // the merger runs) plus 96 computed-goto interpreters.  With 96 address-taken-
  // block functions across several partitions, a dropped pin lands at least one
  // outside partition 0 with overwhelming probability, so the regression is
  // caught essentially deterministically.
  const unsigned NumMods = 24, NumHeavy = 16, NumCG = 4;
  SmallVector<std::string, 32> Srcs;
  for (unsigned m = 0; m < NumMods; ++m) {
    std::string CPath = Dir.file(("cg" + Twine(m) + ".c").str());
    std::string Src = genComputedGotoModule(m, NumHeavy, NumCG);
    if (!writeBytes(CPath, ArrayRef<char>(Src.data(), Src.size())))
      GTEST_SKIP() << "could not write source";
    Srcs.push_back(CPath);
  }
  {
    std::string MainPath = Dir.file("cgmain.c");
    std::string Src = genComputedGotoMain(NumMods, NumHeavy, NumCG);
    if (!writeBytes(MainPath, ArrayRef<char>(Src.data(), Src.size())))
      GTEST_SKIP() << "could not write source";
    Srcs.push_back(MainPath);
  }

  std::string ExeRef = Dir.file("exe_ref");
  StringRef RefArgs[] = {"-fno-lto", "-O2"};
  if (!compileLinkMulti(Dir, Srcs, RefArgs, ExeRef))
    GTEST_SKIP() << "native frontend/link unavailable in this environment";
  std::string OutRef;
  ASSERT_EQ(runExeCapture(Dir, ExeRef, OutRef), 0)
      << "-fno-lto reference executable did not exit cleanly";
  ASSERT_FALSE(OutRef.empty());

  std::string ExeLto = Dir.file("exe_lto");
  {
    ScopedEnv Strict("NEVERC_PCG_STRICT", "1");
    ScopedEnv NoCache("NEVERC_LTO_CACHE", "0");
    ScopedEnv NoPCache("NEVERC_LTO_PCACHE", "0");
    StringRef LtoArgs[] = {"-O2"};
    ASSERT_TRUE(compileLinkMulti(Dir, Srcs, LtoArgs, ExeLto))
        << "auto-LTO link of computed-goto interpreters failed under "
           "NEVERC_PCG_STRICT (a merger/pinning regression); see scratch "
           "spawn.log";
  }
  std::string OutLto;
  ASSERT_EQ(runExeCapture(Dir, ExeLto, OutLto), 0)
      << "auto-LTO computed-goto executable crashed — a blockaddress dispatch "
         "table collapsed to inttoptr(1) because an address-taken-block "
         "function was not pinned to partition 0 (the lua_lto SIGTRAP shape)";
  EXPECT_EQ(OutLto, OutRef)
      << "auto-LTO program output diverged from the -fno-lto build — a "
         "computed-goto dispatch table was corrupted by the partition split";
}
#endif // _WIN32
#endif // NEVERC_BINARY
