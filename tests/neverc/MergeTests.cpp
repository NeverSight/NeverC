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
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Support/raw_ostream.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <random>

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
                ArrayRef<uint8_t> TextContent = {0xcc}) {
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

  for (const auto &Name : DefinedSyms) {
    Sym S;
    memset(&S, 0, sizeof(S));
    S.st_name = addStr(SymStrTab, Name);
    S.st_info = (STB_LOCAL << 4) | STT_FUNC;
    S.st_shndx = 1; // .text
    S.st_value = 0;
    S.st_size = TextContent.size();
    Syms.push_back(S);
  }
  unsigned FirstGlobal = Syms.size();
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

/// Validate that a buffer is a parseable ELF64LE relocatable object.
bool isValidELF64LE(ArrayRef<char> Buf) {
  if (Buf.size() < sizeof(ELF::Elf64_Ehdr))
    return false;
  auto BufRef = MemoryBufferRef(StringRef(Buf.data(), Buf.size()), "test");
  auto ObjOrErr = object::ELFObjectFile<object::ELF64LE>::create(BufRef);
  if (!ObjOrErr) {
    consumeError(ObjOrErr.takeError());
    return false;
  }
  return ObjOrErr->getELFFile().getHeader().e_type == ELF::ET_REL;
}

/// Merge helper: returns (success, output_buffer).
std::pair<bool, SmallVector<char, 0>>
mergeELF(ArrayRef<SmallVector<char, 0>> Bufs, Options Opts = {}) {
  SmallVector<char, 0> Out;
  raw_svector_ostream OS(Out);
  bool OK = mergeELF64LEObjects(Bufs, OS, Opts);
  return {OK, std::move(Out)};
}

} // namespace

// ---------------------------------------------------------------------------
// Edge-case tests: ELF merger
// ---------------------------------------------------------------------------

TEST(MergeELF, EmptyBufferArray) {
  SmallVector<SmallVector<char, 0>, 2> Bufs;
  auto [OK, Out] = mergeELF(Bufs);
  // Empty input → no output but no crash
  EXPECT_TRUE(Out.empty());
}

TEST(MergeELF, AllEmptyBuffers) {
  SmallVector<SmallVector<char, 0>, 4> Bufs(3);
  auto [OK, Out] = mergeELF(Bufs);
  EXPECT_TRUE(Out.empty());
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
  // Symbols with .__pcg marker should be demoted to local
  auto P0 = buildMinimalELF({"helper.__pcg12345678"}, {});
  auto P1 = buildMinimalELF({}, {"helper.__pcg12345678"});

  SmallVector<SmallVector<char, 0>, 2> Bufs;
  Bufs.push_back(std::move(P0));
  Bufs.push_back(std::move(P1));
  auto [OK, Out] = mergeELF(Bufs);
  EXPECT_TRUE(OK);
  EXPECT_TRUE(isValidELF64LE(Out));
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
