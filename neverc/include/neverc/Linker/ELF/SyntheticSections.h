#ifndef LINKER_ELF_SYNTHETIC_SECTIONS_H
#define LINKER_ELF_SYNTHETIC_SECTIONS_H

#include "Linker/ELF/Config.h"
#include "Linker/ELF/InputSection.h"
#include "Linker/ELF/Symbols.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/StringTableBuilder.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Parallel.h"
#include "llvm/Support/Threading.h"

namespace linker::elf {
class Defined;
struct PhdrEntry;
class SymbolTableBaseSection;

struct CieRecord {
  EhSectionPiece *cie = nullptr;
  SmallVector<EhSectionPiece *, 0> fdes;
};

// Section for .eh_frame.
class EhFrameSection final : public SyntheticSection {
public:
  EhFrameSection();
  void writeTo(uint8_t *buf) override;
  void finalizeContents() override;
  bool isNeeded() const override { return !sections.empty(); }
  size_t getSize() const override { return size; }

  static bool classof(const SectionBase *d) {
    return SyntheticSection::classof(d) && d->name == ".eh_frame";
  }

  SmallVector<EhInputSection *, 0> sections;
  size_t numFdes = 0;

  struct FdeData {
    uint32_t pcRel;
    uint32_t fdeVARel;
  };

  SmallVector<FdeData, 0> getFdeData() const;
  ArrayRef<CieRecord *> getCieRecords() const { return cieRecords; }
  template <class ELFT>
  void iterateFDEWithLSDA(llvm::function_ref<void(InputSection &)> fn);

private:
  // This is used only when parsing EhInputSection. We keep it here to avoid
  // allocating one for each EhInputSection.
  llvm::DenseMap<size_t, CieRecord *> offsetToCie;

  uint64_t size = 0;

  template <class ELFT, class RelTy>
  void addRecords(EhInputSection *s, llvm::ArrayRef<RelTy> rels);
  template <class ELFT> void addSectionAux(EhInputSection *s);
  template <class ELFT, class RelTy>
  void iterateFDEWithLSDAAux(EhInputSection &sec, ArrayRef<RelTy> rels,
                             llvm::DenseSet<size_t> &ciesWithLSDA,
                             llvm::function_ref<void(InputSection &)> fn);

  template <class ELFT, class RelTy>
  CieRecord *addCie(EhSectionPiece &piece, ArrayRef<RelTy> rels);

  template <class ELFT, class RelTy>
  Defined *isFdeLive(EhSectionPiece &piece, ArrayRef<RelTy> rels);

  uint64_t getFdePc(uint8_t *buf, size_t off, uint8_t enc) const;

  SmallVector<CieRecord *, 0> cieRecords;

  // CIE records are uniquified by their contents and personality functions.
  llvm::DenseMap<std::pair<ArrayRef<uint8_t>, Symbol *>, CieRecord *> cieMap;
};

class GotSection final : public SyntheticSection {
public:
  GotSection();
  size_t getSize() const override { return size; }
  void finalizeContents() override;
  bool isNeeded() const override;
  void writeTo(uint8_t *buf) override;

  void addConstant(const Relocation &r);
  void addEntry(Symbol &sym);
  bool addTlsDescEntry(Symbol &sym);
  bool addDynTlsEntry(Symbol &sym);
  bool addTlsIndex();
  uint32_t getTlsDescOffset(const Symbol &sym) const;
  uint64_t getTlsDescAddr(const Symbol &sym) const;
  uint64_t getGlobalDynAddr(const Symbol &b) const;
  uint64_t getGlobalDynOffset(const Symbol &b) const;

  uint64_t getTlsIndexVA() { return this->getVA() + tlsIndexOff; }
  uint32_t getTlsIndexOff() const { return tlsIndexOff; }

  // Flag to force GOT to be in output if we have relocations
  // that relies on its address.
  std::atomic<bool> hasGotOffRel = false;

protected:
  size_t numEntries = 0;
  uint32_t tlsIndexOff = -1;
  uint64_t size = 0;
};

// .note.GNU-stack section.
class GnuStackSection : public SyntheticSection {
public:
  GnuStackSection()
      : SyntheticSection(0, llvm::ELF::SHT_PROGBITS, 1, ".note.GNU-stack") {}
  void writeTo(uint8_t *buf) override {}
  size_t getSize() const override { return 0; }
};

class GnuPropertySection final : public SyntheticSection {
public:
  GnuPropertySection();
  void writeTo(uint8_t *buf) override;
  size_t getSize() const override;
};

// .note.gnu.build-id section.
class FormIdSection : public SyntheticSection {
  // First 16 bytes are a header.
  static const unsigned headerSize = 16;

public:
  const size_t hashSize;
  FormIdSection();
  void writeTo(uint8_t *buf) override;
  size_t getSize() const override { return headerSize + hashSize; }
  void writeBuildId(llvm::ArrayRef<uint8_t> buf);

private:
  uint8_t *hashBuf;
};

// BssSection is used to reserve space for copy relocations and common symbols.
// We create three instances of this class for .bss, .bss.rel.ro and "COMMON",
// that are used for writable symbols, read-only symbols and common symbols,
// respectively.
class BssSection final : public SyntheticSection {
public:
  BssSection(StringRef name, uint64_t size, uint32_t addralign);
  void writeTo(uint8_t *) override {}
  bool isNeeded() const override { return size != 0; }
  size_t getSize() const override { return size; }

  static bool classof(const SectionBase *s) { return s->bss; }
  uint64_t size;
};

class GotPltSection final : public SyntheticSection {
public:
  GotPltSection();
  void addEntry(Symbol &sym);
  size_t getSize() const override;
  void writeTo(uint8_t *buf) override;
  bool isNeeded() const override;

  // Flag to force GotPlt to be in output if we have relocations
  // that relies on its address.
  std::atomic<bool> hasGotPltOffRel = false;

private:
  SmallVector<const Symbol *, 0> entries;
};

// The IgotPltSection is a Got associated with the PltSection for GNU Ifunc
// Symbols that will be relocated by Target->IRelativeRel.
// On most Targets the IgotPltSection will immediately follow the GotPltSection
// on AArch64 the IgotPltSection will immediately follow the GotSection.
class IgotPltSection final : public SyntheticSection {
public:
  IgotPltSection();
  void addEntry(Symbol &sym);
  size_t getSize() const override;
  void writeTo(uint8_t *buf) override;
  bool isNeeded() const override { return !entries.empty(); }

private:
  SmallVector<const Symbol *, 0> entries;
};

class StringTableSection final : public SyntheticSection {
public:
  StringTableSection(StringRef name, bool dynamic);
  unsigned addString(StringRef s, bool hashIt = true);
  void writeTo(uint8_t *buf) override;
  size_t getSize() const override { return size; }
  bool isDynamic() const { return dynamic; }

private:
  const bool dynamic;

  uint64_t size = 0;

  llvm::DenseMap<llvm::CachedHashStringRef, unsigned> stringMap;
  SmallVector<StringRef, 0> strings;
};

class DynamicReloc {
public:
  enum Kind {
    /// The resulting dynamic relocation does not reference a symbol (#sym must
    /// be nullptr) and uses #addend as the result of computeAddend().
    AddendOnly,
    /// The resulting dynamic relocation will not reference a symbol: #sym is
    /// only used to compute the addend with InputSection::getRelocTargetVA().
    /// Useful for various relative and TLS relocations (e.g. R_X86_64_TPOFF64).
    AddendOnlyWithTargetVA,
    /// The resulting dynamic relocation references symbol #sym from the dynamic
    /// symbol table and uses #addend as the value of computeAddend().
    AgainstSymbol,
    /// The resulting dynamic relocation references symbol #sym from the dynamic
    /// symbol table and uses InputSection::getRelocTargetVA() + #addend for the
    /// final addend. It can be used for relocations that write the symbol VA as
    // the addend but still reference the symbol.
    AgainstSymbolWithTargetVA,
  };
  /// This constructor records a relocation against a symbol.
  DynamicReloc(RelType type, const InputSectionBase *inputSec,
               uint64_t offsetInSec, Kind kind, Symbol &sym, int64_t addend,
               RelExpr expr)
      : sym(&sym), inputSec(inputSec), offsetInSec(offsetInSec), type(type),
        addend(addend), kind(kind), expr(expr) {}
  /// This constructor records a relative relocation with no symbol.
  DynamicReloc(RelType type, const InputSectionBase *inputSec,
               uint64_t offsetInSec, int64_t addend = 0)
      : sym(nullptr), inputSec(inputSec), offsetInSec(offsetInSec), type(type),
        addend(addend), kind(AddendOnly), expr(R_ADDEND) {}

  uint64_t getOffset() const;
  uint32_t getSymIndex(SymbolTableBaseSection *symTab) const;
  bool needsDynSymIndex() const {
    return kind == AgainstSymbol || kind == AgainstSymbolWithTargetVA;
  }

  /// Computes the addend of the dynamic relocation. Note that this is not the
  /// same as the #addend member variable as it may also include the symbol
  /// address/the address of the corresponding GOT entry/etc.
  int64_t computeAddend() const;

  void computeRaw(SymbolTableBaseSection *symtab);

  Symbol *sym;
  const OutputSection *outputSec = nullptr;
  const InputSectionBase *inputSec;
  uint64_t offsetInSec;
  uint64_t r_offset;
  RelType type;
  uint32_t r_sym;
  // Initially input addend, then the output addend after
  // RelocationSection<ELFT>::writeTo.
  int64_t addend;

private:
  Kind kind;
  // The kind of expression used to calculate the added (required e.g. for
  // relative GOT relocations).
  RelExpr expr;
};

template <class ELFT> class DynamicSection final : public SyntheticSection {
  LLVM_ELF_IMPORT_TYPES_ELFT(ELFT)

public:
  DynamicSection();
  void finalizeContents() override;
  void writeTo(uint8_t *buf) override;
  size_t getSize() const override { return size; }

private:
  std::vector<std::pair<int32_t, uint64_t>> computeContents();
  uint64_t size = 0;
};

class RelocationBaseSection : public SyntheticSection {
public:
  RelocationBaseSection(StringRef name, uint32_t type, int32_t dynamicTag,
                        int32_t sizeDynamicTag, bool combreloc,
                        unsigned concurrency);
  /// Add a dynamic relocation without writing an addend to the output section.
  /// This overload can be used if the addends are written directly instead of
  /// using relocations on the input section.
  template <bool shard = false> void addReloc(const DynamicReloc &reloc) {
    relocs.push_back(reloc);
  }
  /// Add a dynamic relocation against \p sym with an optional addend.
  void addSymbolReloc(RelType dynType, InputSectionBase &isec,
                      uint64_t offsetInSec, Symbol &sym, int64_t addend = 0,
                      std::optional<RelType> addendRelType = {});
  /// Add a relative dynamic relocation that uses the target address of \p sym
  /// (i.e. InputSection::getRelocTargetVA()) + \p addend as the addend.
  /// This function should only be called for non-preemptible symbols or
  /// RelExpr values that refer to an address inside the output file (e.g. the
  /// address of the GOT entry for a potentially preemptible symbol).
  template <bool shard = false>
  void addRelativeReloc(RelType dynType, InputSectionBase &isec,
                        uint64_t offsetInSec, Symbol &sym, int64_t addend,
                        RelType addendRelType, RelExpr expr) {
    assert(expr != R_ADDEND && "expected non-addend relocation expression");
    addReloc<shard>(DynamicReloc::AddendOnlyWithTargetVA, dynType, isec,
                    offsetInSec, sym, addend, expr, addendRelType);
  }
  /// Add a dynamic relocation using the target address of \p sym as the addend
  /// if \p sym is non-preemptible. Otherwise add a relocation against \p sym.
  void addAddendOnlyRelocIfNonPreemptible(RelType dynType, GotSection &sec,
                                          uint64_t offsetInSec, Symbol &sym,
                                          RelType addendRelType);
  template <bool shard = false>
  void addReloc(DynamicReloc::Kind kind, RelType dynType, InputSectionBase &sec,
                uint64_t offsetInSec, Symbol &sym, int64_t addend, RelExpr expr,
                RelType addendRelType) {
    // Write the addends to the relocated address if required. We skip
    // it if the written value would be zero.
    if (config->writeAddends && (expr != R_ADDEND || addend != 0))
      sec.addReloc({expr, addendRelType, offsetInSec, addend, &sym});
    addReloc<shard>({dynType, &sec, offsetInSec, kind, sym, addend, expr});
  }
  bool isNeeded() const override {
    return !relocs.empty() ||
           llvm::any_of(relocsVec, [](auto &v) { return !v.empty(); });
  }
  size_t getSize() const override { return relocs.size() * this->entsize; }
  size_t getRelativeRelocCount() const { return numRelativeRelocs; }
  void mergeRels();
  void partitionRels();
  void finalizeContents() override;
  static bool classof(const SectionBase *d) {
    return SyntheticSection::classof(d) &&
           (d->type == llvm::ELF::SHT_RELA || d->type == llvm::ELF::SHT_REL ||
            d->type == llvm::ELF::SHT_RELR);
  }
  int32_t dynamicTag, sizeDynamicTag;
  SmallVector<DynamicReloc, 0> relocs;

protected:
  void computeRels();
  // Used when parallel relocation scanning adds relocations. The elements
  // will be moved into relocs by mergeRel().
  SmallVector<SmallVector<DynamicReloc, 0>, 0> relocsVec;
  size_t numRelativeRelocs = 0; // used by -z combreloc
  bool combreloc;
};

template <>
inline void RelocationBaseSection::addReloc<true>(const DynamicReloc &reloc) {
  relocsVec[llvm::parallel::getThreadIndex()].push_back(reloc);
}

template <class ELFT>
class RelocationSection final : public RelocationBaseSection {
  using Elf_Rel = typename ELFT::Rel;
  using Elf_Rela = typename ELFT::Rela;

public:
  RelocationSection(StringRef name, bool combreloc, unsigned concurrency);
  void writeTo(uint8_t *buf) override;
};

template <class ELFT>
class AndroidPackedRelocationSection final : public RelocationBaseSection {
  using Elf_Rel = typename ELFT::Rel;
  using Elf_Rela = typename ELFT::Rela;

public:
  AndroidPackedRelocationSection(StringRef name, unsigned concurrency);

  bool updateAllocSize() override;
  size_t getSize() const override { return relocData.size(); }
  void writeTo(uint8_t *buf) override {
    memcpy(buf, relocData.data(), relocData.size());
  }

private:
  SmallVector<char, 0> relocData;
};

struct RelativeReloc {
  uint64_t getOffset() const { return inputSec->getVA(offsetInSec); }

  const InputSectionBase *inputSec;
  uint64_t offsetInSec;
};

class RelrBaseSection : public SyntheticSection {
public:
  RelrBaseSection(unsigned concurrency);
  void mergeRels();
  bool isNeeded() const override {
    return !relocs.empty() ||
           llvm::any_of(relocsVec, [](auto &v) { return !v.empty(); });
  }
  SmallVector<RelativeReloc, 0> relocs;
  SmallVector<SmallVector<RelativeReloc, 0>, 0> relocsVec;
};

// RelrSection is used to encode offsets for relative relocations.
// Proposal for adding SHT_RELR sections to generic-abi is here:
//   https://groups.google.com/forum/#!topic/generic-abi/bX460iggiKg
// For more details, see the comment in RelrSection::updateAllocSize().
template <class ELFT> class RelrSection final : public RelrBaseSection {
  using Elf_Relr = typename ELFT::Relr;

public:
  RelrSection(unsigned concurrency);

  bool updateAllocSize() override;
  size_t getSize() const override { return relrRelocs.size() * this->entsize; }
  void writeTo(uint8_t *buf) override {
    memcpy(buf, relrRelocs.data(), getSize());
  }

private:
  SmallVector<Elf_Relr, 0> relrRelocs;
};

struct SymbolTableEntry {
  Symbol *sym;
  size_t strTabOffset;
};

class SymbolTableBaseSection : public SyntheticSection {
public:
  SymbolTableBaseSection(StringTableSection &strTabSec);
  void finalizeContents() override;
  size_t getSize() const override { return getNumSymbols() * entsize; }
  void addSymbol(Symbol *sym);
  unsigned getNumSymbols() const { return symbols.size() + 1; }
  size_t getSymbolIndex(Symbol *sym);
  ArrayRef<SymbolTableEntry> getSymbols() const { return symbols; }

protected:
  void sortSymTabSymbols();

  // A vector of symbols and their string table offsets.
  SmallVector<SymbolTableEntry, 0> symbols;

  StringTableSection &strTabSec;

  llvm::once_flag onceFlag;
  llvm::DenseMap<Symbol *, size_t> symbolIndexMap;
  llvm::DenseMap<OutputSection *, size_t> sectionIndexMap;
};

template <class ELFT>
class SymbolTableSection final : public SymbolTableBaseSection {
  using Elf_Sym = typename ELFT::Sym;

public:
  SymbolTableSection(StringTableSection &strTabSec);
  void writeTo(uint8_t *buf) override;
};

class SymtabShndxSection final : public SyntheticSection {
public:
  SymtabShndxSection();

  void writeTo(uint8_t *buf) override;
  size_t getSize() const override;
  bool isNeeded() const override;
  void finalizeContents() override;
};

// Outputs GNU Hash section. For detailed explanation see:
// https://blogs.oracle.com/ali/entry/gnu_hash_elf_sections
class GnuHashTableSection final : public SyntheticSection {
public:
  GnuHashTableSection();
  void finalizeContents() override;
  void writeTo(uint8_t *buf) override;
  size_t getSize() const override { return size; }

  // Adds symbols to the hash table.
  // Sorts the input to satisfy GNU hash section requirements.
  void addSymbols(llvm::SmallVectorImpl<SymbolTableEntry> &symbols);

private:
  // See the comment in writeBloomFilter.
  enum { Shift2 = 26 };

  struct Entry {
    Symbol *sym;
    size_t strTabOffset;
    uint32_t hash;
    uint32_t bucketIdx;
  };

  SmallVector<Entry, 0> symbols;
  size_t maskWords;
  size_t nBuckets = 0;
  size_t size = 0;
};

class HashTableSection final : public SyntheticSection {
public:
  HashTableSection();
  void finalizeContents() override;
  void writeTo(uint8_t *buf) override;
  size_t getSize() const override { return size; }

private:
  size_t size = 0;
};

// Used for PLT entries. It usually has a PLT header for lazy binding. Each PLT
// entry is associated with a JUMP_SLOT relocation, which may be resolved lazily
// at runtime.
// On x86 when IBT is enabled, this section (.plt.sec) contains PLT call stubs.
// A call instruction jumps to a .plt.sec entry, which will then jump to the
// target (BIND_NOW) or a .plt entry.
class PltSection : public SyntheticSection {
public:
  PltSection();
  void writeTo(uint8_t *buf) override;
  size_t getSize() const override;
  bool isNeeded() const override;
  void addSymbols();
  void addEntry(Symbol &sym);
  size_t getNumEntries() const { return entries.size(); }

  size_t headerSize;

  SmallVector<const Symbol *, 0> entries;
};

// Used for non-preemptible ifuncs. It does not have a header. Each entry is
// associated with an IRELATIVE relocation, which will be resolved eagerly at
// runtime. PltSection can only contain entries associated with JUMP_SLOT
// relocations, so IPLT entries are in a separate section.
class IpltSection final : public SyntheticSection {
  SmallVector<const Symbol *, 0> entries;

public:
  IpltSection();
  void writeTo(uint8_t *buf) override;
  size_t getSize() const override;
  bool isNeeded() const override { return !entries.empty(); }
  void addSymbols();
  void addEntry(Symbol &sym);
};

// This is x86-only.
class IBTPltSection : public SyntheticSection {
public:
  IBTPltSection();
  void writeTo(uint8_t *Buf) override;
  bool isNeeded() const override;
  size_t getSize() const override;
};

// Used to align the end of the PT_GNU_RELRO segment and the associated PT_LOAD
// segment to a common-page-size boundary. This padding section ensures that all
// pages in the PT_LOAD segment is covered by at least one section.
class RelroPaddingSection final : public SyntheticSection {
public:
  RelroPaddingSection();
  size_t getSize() const override { return 0; }
  void writeTo(uint8_t *buf) override {}
};

class GdbIndexSection final : public SyntheticSection {
public:
  struct AddressEntry {
    InputSection *section;
    uint64_t lowAddress;
    uint64_t highAddress;
    uint32_t cuIndex;
  };

  struct CuEntry {
    uint64_t cuOffset;
    uint64_t cuLength;
  };

  struct NameAttrEntry {
    llvm::CachedHashStringRef name;
    uint32_t cuIndexAndAttrs;
  };

  struct GdbChunk {
    InputSection *sec;
    SmallVector<AddressEntry, 0> addressAreas;
    SmallVector<CuEntry, 0> compilationUnits;
  };

  struct GdbSymbol {
    llvm::CachedHashStringRef name;
    SmallVector<uint32_t, 0> cuVector;
    uint32_t nameOff;
    uint32_t cuVectorOff;
  };

  GdbIndexSection();
  template <typename ELFT> static GdbIndexSection *create();
  void writeTo(uint8_t *buf) override;
  size_t getSize() const override { return size; }
  bool isNeeded() const override;

private:
  struct GdbIndexHeader {
    llvm::support::ulittle32_t version;
    llvm::support::ulittle32_t cuListOff;
    llvm::support::ulittle32_t cuTypesOff;
    llvm::support::ulittle32_t addressAreaOff;
    llvm::support::ulittle32_t symtabOff;
    llvm::support::ulittle32_t constantPoolOff;
  };

  size_t computeSymtabSize() const;

  // Each chunk contains information gathered from debug sections of a
  // single object file.
  SmallVector<GdbChunk, 0> chunks;

  // A symbol table for this .gdb_index section.
  SmallVector<GdbSymbol, 0> symbols;

  size_t size;
};

// --eh-frame-hdr option tells linker to construct a header for all the
// .eh_frame sections. This header is placed to a section named .eh_frame_hdr
// and also to a PT_GNU_EH_FRAME segment.
// At runtime the unwinder then can find all the PT_GNU_EH_FRAME segments by
// calling dl_iterate_phdr.
// This section contains a lookup table for quick binary search of FDEs.
// Detailed info about internals can be found in Ian Lance Taylor's blog:
// http://www.airs.com/blog/archives/460 (".eh_frame")
// http://www.airs.com/blog/archives/462 (".eh_frame_hdr")
class EhFrameHeader final : public SyntheticSection {
public:
  EhFrameHeader();
  void write();
  void writeTo(uint8_t *buf) override;
  size_t getSize() const override;
  bool isNeeded() const override;
};

// For more information about .gnu.version and .gnu.version_r see:
// https://www.akkadia.org/drepper/symbol-versioning

// The .gnu.version_d section which has a section type of SHT_GNU_verdef shall
// contain symbol version definitions. The number of entries in this section
// shall be contained in the DT_VERDEFNUM entry of the .dynamic section.
// The section shall contain an array of Elf_Verdef structures, optionally
// followed by an array of Elf_Verdaux structures.
class VersionDefinitionSection final : public SyntheticSection {
public:
  VersionDefinitionSection();
  void finalizeContents() override;
  size_t getSize() const override;
  void writeTo(uint8_t *buf) override;

private:
  enum { EntrySize = 28 };
  void writeOne(uint8_t *buf, uint32_t index, StringRef name, size_t nameOff);
  StringRef getFileDefName();

  unsigned fileDefNameOff;
  SmallVector<unsigned, 0> verDefNameOffs;
};

// The .gnu.version section specifies the required version of each symbol in the
// dynamic symbol table. It contains one Elf_Versym for each dynamic symbol
// table entry. An Elf_Versym is just a 16-bit integer that refers to a version
// identifier defined in the either .gnu.version_r or .gnu.version_d section.
// The values 0 and 1 are reserved. All other values are used for versions in
// the own object or in any of the dependencies.
class VersionTableSection final : public SyntheticSection {
public:
  VersionTableSection();
  void finalizeContents() override;
  size_t getSize() const override;
  void writeTo(uint8_t *buf) override;
  bool isNeeded() const override;
};

// The .gnu.version_r section defines the version identifiers used by
// .gnu.version. It contains a linked list of Elf_Verneed data structures. Each
// Elf_Verneed specifies the version requirements for a single DSO, and contains
// a reference to a linked list of Elf_Vernaux data structures which define the
// mapping from version identifiers to version names.
template <class ELFT> class VersionNeedSection final : public SyntheticSection {
  using Elf_Verneed = typename ELFT::Verneed;
  using Elf_Vernaux = typename ELFT::Vernaux;

  struct Vernaux {
    uint64_t hash;
    uint32_t verneedIndex;
    uint64_t nameStrTab;
  };

  struct Verneed {
    uint64_t nameStrTab;
    std::vector<Vernaux> vernauxs;
  };

  SmallVector<Verneed, 0> verneeds;

public:
  VersionNeedSection();
  void finalizeContents() override;
  void writeTo(uint8_t *buf) override;
  size_t getSize() const override;
  bool isNeeded() const override;
};

// MergeSyntheticSection is a class that allows us to put mergeable sections
// with different attributes in a single output sections. To do that
// we put them into MergeSyntheticSection synthetic input sections which are
// attached to regular output sections.
class MergeSyntheticSection : public SyntheticSection {
public:
  void addSection(MergeInputSection *ms);
  SmallVector<MergeInputSection *, 0> sections;

protected:
  MergeSyntheticSection(StringRef name, uint32_t type, uint64_t flags,
                        uint32_t addralign)
      : SyntheticSection(flags, type, addralign, name) {}
};

class MergeTailSection final : public MergeSyntheticSection {
public:
  MergeTailSection(StringRef name, uint32_t type, uint64_t flags,
                   uint32_t addralign);

  size_t getSize() const override;
  void writeTo(uint8_t *buf) override;
  void finalizeContents() override;

private:
  llvm::StringTableBuilder builder;
};

class MergeNoTailSection final : public MergeSyntheticSection {
public:
  MergeNoTailSection(StringRef name, uint32_t type, uint64_t flags,
                     uint32_t addralign)
      : MergeSyntheticSection(name, type, flags, addralign) {}

  size_t getSize() const override { return size; }
  void writeTo(uint8_t *buf) override;
  void finalizeContents() override;

private:
  // We use the most significant bits of a hash as a shard ID.
  // The reason why we don't want to use the least significant bits is
  // because DenseMap also uses lower bits to determine a bucket ID.
  // If we use lower bits, it significantly increases the probability of
  // hash collisions.
  size_t getShardId(uint32_t hash) {
    assert((hash >> 31) == 0);
    return hash >> (31 - llvm::countr_zero(numShards));
  }

  // Section size
  size_t size;

  // String table contents
  constexpr static size_t numShards = 32;
  SmallVector<llvm::StringTableBuilder, 0> shards;
  size_t shardOffsets[numShards];
};

// A container for one or more linker generated thunks.
class ThunkSection final : public SyntheticSection {
public:
  // ThunkSection in OS, with desired outSecOff of Off
  ThunkSection(OutputSection *os, uint64_t off);

  // Add a newly created Thunk to this container:
  // Thunk is given offset from start of this InputSection
  // Thunk defines a symbol in this InputSection that can be used as target
  // of a relocation
  void addThunk(Thunk *t);
  size_t getSize() const override;
  void writeTo(uint8_t *buf) override;
  InputSection *getTargetInputSection() const;
  bool assignOffsets();

  // When true, round up reported size of section to 4 KiB. See comment
  // in addThunkSection() for more details.
  bool roundUpSizeForErrata = false;

private:
  SmallVector<Thunk *, 0> thunks;
  size_t size = 0;
};

template <typename ELFT>
class PartitionElfHeaderSection final : public SyntheticSection {
public:
  PartitionElfHeaderSection();
  size_t getSize() const override;
  void writeTo(uint8_t *buf) override;
};

template <typename ELFT>
class PartitionProgramHeadersSection final : public SyntheticSection {
public:
  PartitionProgramHeadersSection();
  size_t getSize() const override;
  void writeTo(uint8_t *buf) override;
};

class PartitionIndexSection final : public SyntheticSection {
public:
  PartitionIndexSection();
  size_t getSize() const override;
  void finalizeContents() override;
  void writeTo(uint8_t *buf) override;
};

// See the following link for the Android-specific loader code that operates on
// this section:
// https://cs.android.com/android/platform/superproject/+/master:bionic/libc/bionic/libc_init_static.cpp;drc=9425b16978f9c5aa8f2c50c873db470819480d1d;l=192
class MemtagAndroidNote final : public SyntheticSection {
public:
  MemtagAndroidNote()
      : SyntheticSection(llvm::ELF::SHF_ALLOC, llvm::ELF::SHT_NOTE,
                         /*alignment=*/4, ".note.android.memtag") {}
  void writeTo(uint8_t *buf) override;
  size_t getSize() const override;
};

class PackageMetadataNote final : public SyntheticSection {
public:
  PackageMetadataNote()
      : SyntheticSection(llvm::ELF::SHF_ALLOC, llvm::ELF::SHT_NOTE,
                         /*alignment=*/4, ".note.package") {}
  void writeTo(uint8_t *buf) override;
  size_t getSize() const override;
};

class MemtagDescriptors final : public SyntheticSection {
public:
  MemtagDescriptors()
      : SyntheticSection(llvm::ELF::SHF_ALLOC,
                         llvm::ELF::SHT_AARCH64_MEMTAG_GLOBALS_DYNAMIC,
                         /*alignment=*/4, ".memtag.globals.dynamic") {}
  void writeTo(uint8_t *buf) override;
  // The size of the section is non-computable until all addresses are
  // synthetized, because the section's contents contain a sorted
  // varint-compressed list of pointers to global variables. We only know the
  // final size after `finalizeAddressDependentContent()`.
  size_t getSize() const override;
  bool updateAllocSize() override;

  void addSymbol(const Symbol &sym) { symbols.push_back(&sym); }

  bool isNeeded() const override { return !symbols.empty(); }

private:
  SmallVector<const Symbol *, 0> symbols;
};

InputSection *createInterpSection();
template <class ELFT> void splitSections();
void combineEhSections();

template <typename ELFT> void writeEhdr(uint8_t *buf, Partition &part);
template <typename ELFT> void writePhdrs(uint8_t *buf, Partition &part);

Defined *addSyntheticLocal(StringRef name, uint8_t type, uint64_t value,
                           uint64_t size, InputSectionBase &section);

void addVerneed(Symbol *ss);

// Linker generated per-partition sections.
struct Partition {
  StringRef name;
  uint64_t nameStrTab;

  std::unique_ptr<SyntheticSection> elfHeader;
  std::unique_ptr<SyntheticSection> programHeaders;
  SmallVector<PhdrEntry *, 0> phdrs;

  std::unique_ptr<FormIdSection> buildId;
  std::unique_ptr<SyntheticSection> dynamic;
  std::unique_ptr<StringTableSection> dynStrTab;
  std::unique_ptr<SymbolTableBaseSection> dynSymTab;
  std::unique_ptr<EhFrameHeader> ehFrameHdr;
  std::unique_ptr<EhFrameSection> ehFrame;
  std::unique_ptr<GnuHashTableSection> gnuHashTab;
  std::unique_ptr<HashTableSection> hashTab;
  std::unique_ptr<MemtagAndroidNote> memtagAndroidNote;
  std::unique_ptr<MemtagDescriptors> memtagDescriptors;
  std::unique_ptr<PackageMetadataNote> packageMetadataNote;
  std::unique_ptr<RelocationBaseSection> relaDyn;
  std::unique_ptr<RelrBaseSection> relrDyn;
  std::unique_ptr<VersionDefinitionSection> verDef;
  std::unique_ptr<SyntheticSection> verNeed;
  std::unique_ptr<VersionTableSection> verSym;

  unsigned getNumber() const { return this - &partitions[0] + 1; }
};

LLVM_LIBRARY_VISIBILITY extern Partition *mainPart;

inline Partition &SectionBase::getPartition() const {
  assert(isLive());
  return partitions[partition - 1];
}

// Linker generated sections which can be used as inputs and are not specific to
// a partition.
struct InStruct {
  std::unique_ptr<InputSection> attributes;
  std::unique_ptr<BssSection> bss;
  std::unique_ptr<BssSection> bssRelRo;
  std::unique_ptr<GotSection> got;
  std::unique_ptr<GotPltSection> gotPlt;
  std::unique_ptr<IgotPltSection> igotPlt;
  std::unique_ptr<RelroPaddingSection> relroPadding;
  std::unique_ptr<SyntheticSection> partEnd;
  std::unique_ptr<SyntheticSection> partIndex;
  std::unique_ptr<PltSection> plt;
  std::unique_ptr<IpltSection> iplt;
  std::unique_ptr<IBTPltSection> ibtPlt;
  std::unique_ptr<RelocationBaseSection> relaPlt;
  std::unique_ptr<RelocationBaseSection> relaIplt;
  std::unique_ptr<StringTableSection> shStrTab;
  std::unique_ptr<StringTableSection> strTab;
  std::unique_ptr<SymbolTableBaseSection> symTab;
  std::unique_ptr<SymtabShndxSection> symTabShndx;

  void reset();
};

LLVM_LIBRARY_VISIBILITY extern InStruct in;

} // namespace linker::elf

#endif
