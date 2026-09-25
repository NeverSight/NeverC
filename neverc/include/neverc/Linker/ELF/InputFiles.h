#ifndef LINKER_ELF_INPUT_FILES_H
#define LINKER_ELF_INPUT_FILES_H

#include "Linker/Core/Runtime/Diagnostic.h"
#include "Linker/Core/Support/LlvmAliases.h"
#include "Linker/ELF/Config.h"
#include "Linker/ELF/Symbols.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/Object/ELF.h"
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/Support/Threading.h"
#include <atomic>
#include <memory>
#include <vector>

namespace llvm {
struct DILineInfo;
namespace lto {
class InputFile;
}
} // namespace llvm

namespace linker {
class DWARFCache;

// Returns "<internal>", "foo.a(bar.o)" or "baz.o".
std::string toString(const elf::InputFile *f);

namespace elf {

class InputSection;
class Symbol;
struct SymbolNameSlot;

// Opens a given file.
std::optional<MemoryBufferRef> readFile(StringRef path);
bool isKnownSectionType(uint32_t type, uint64_t flags);
// Whether a lazy archive member defines `name` other than as a common
// symbol, so that --fortran-common extracts it over a common definition.
bool definesNonCommon(const InputFile *file, StringRef name);

// Add symbols in File to the symbol table.
void parseFile(InputFile *file);

// The root class of input files.
class InputFile {
protected:
  std::unique_ptr<Symbol *[]> symbols;
  uint32_t numSymbols = 0;
  SmallVector<InputSectionBase *, 0> sections;

public:
  enum Kind : uint8_t {
    ObjKind,
    SharedKind,
    BitcodeKind,
    BinaryKind,
  };

  Kind kind() const { return fileKind; }

  bool isElf() const {
    Kind k = kind();
    return k == ObjKind || k == SharedKind;
  }

  StringRef getName() const { return mb.getBufferIdentifier(); }
  MemoryBufferRef mb;

  // Returns sections. It is a runtime error to call this function
  // on files that don't have the notion of sections.
  ArrayRef<InputSectionBase *> getSections() const {
    assert(fileKind == ObjKind || fileKind == BinaryKind);
    return sections;
  }

  // Returns object file symbols. It is a runtime error to call this
  // function on files of other types.
  ArrayRef<Symbol *> getSymbols() const {
    assert(fileKind == BinaryKind || fileKind == ObjKind ||
           fileKind == BitcodeKind);
    return {symbols.get(), numSymbols};
  }

  MutableArrayRef<Symbol *> getMutableSymbols() {
    assert(fileKind == BinaryKind || fileKind == ObjKind ||
           fileKind == BitcodeKind);
    return {symbols.get(), numSymbols};
  }

  // Get filename to use for linker script processing.
  StringRef getNameForScript() const;

  // groupId is used for --warn-backrefs which is an optional error
  // checking feature. All files within the same --{start,end}-group or
  // --{start,end}-lib get the same group ID. Otherwise, each file gets a new
  // group ID. For more info, see checkDependency() in SymbolTable.cpp.
  uint32_t groupId;

  // If this is an architecture-specific file, the following members
  // have ELF type (i.e. ELF{32,64}{LE,BE}) and target machine type.
  uint16_t emachine = llvm::ELF::EM_NONE;
  const Kind fileKind;
  ELFKind ekind = ELFNoneKind;
  uint8_t osabi = 0;
  uint8_t abiVersion = 0;

  // True if this is a relocatable object file/bitcode file between --start-lib
  // and --end-lib.
  bool lazy = false;

  // True if this is an argument for --just-symbols. Usually false.
  bool justSymbols = false;

  // True for native object files produced by LTO code generation from
  // bitcode inputs. Such files re-encode .neverc.overrides information
  // that has already been recorded via marker symbols during BitcodeFile
  // parsing, so the linker must skip re-parsing the section to avoid
  // duplicate-warning noise.
  bool builtFromBitcode = false;

  std::string getSrcMsg(const Symbol &sym, const InputSectionBase &sec,
                        uint64_t offset);

protected:
  InputFile(Kind k, MemoryBufferRef m);

public:
  // If not empty, this stores the name of the archive containing this file.
  // We use this string for creating error messages.
  SmallString<0> archiveName;

private:
  // Cache for getNameForScript().
  mutable SmallString<0> nameForScriptCache;
};

class ELFFileBase : public InputFile {
public:
  ELFFileBase(Kind k, ELFKind ekind, MemoryBufferRef m);
  static bool classof(const InputFile *f) { return f->isElf(); }

  void init();
  // Run init() unless it already ran. Lazy archive members defer init() so
  // that it can run on the name-interning workers.
  void ensureInitialized() {
    if (!initialized)
      init();
  }
  // Same effect as init() but without diagnostics, for worker threads. Returns
  // false, leaving the file untouched, if init() would report an error.
  bool tryInitQuietly();
  template <typename ELFT> llvm::object::ELFFile<ELFT> getObj() const {
    return check(llvm::object::ELFFile<ELFT>::create(mb.getBuffer()));
  }

  StringRef getStringTable() const { return stringTable; }

  ArrayRef<Symbol *> getLocalSymbols() {
    if (numSymbols == 0)
      return {};
    return llvm::ArrayRef(symbols.get() + 1, firstGlobal - 1);
  }
  ArrayRef<Symbol *> getGlobalSymbols() {
    return llvm::ArrayRef(symbols.get() + firstGlobal,
                          numSymbols - firstGlobal);
  }
  MutableArrayRef<Symbol *> getMutableGlobalSymbols() {
    return llvm::MutableArrayRef(symbols.get() + firstGlobal,
                                 numSymbols - firstGlobal);
  }

  template <typename ELFT> typename ELFT::ShdrRange getELFShdrs() const {
    return typename ELFT::ShdrRange(
        reinterpret_cast<const typename ELFT::Shdr *>(elfShdrs), numELFShdrs);
  }
  template <typename ELFT> typename ELFT::SymRange getELFSyms() const {
    return typename ELFT::SymRange(
        reinterpret_cast<const typename ELFT::Sym *>(elfSyms), numELFSyms);
  }
  size_t getGlobalELFSymCount() const {
    return numELFSyms > firstGlobal ? numELFSyms - firstGlobal : 0;
  }
  template <typename ELFT> typename ELFT::SymRange getGlobalELFSyms() const {
    return getELFSyms<ELFT>().slice(firstGlobal);
  }

protected:
  // Initializes this class's member variables.
  template <typename ELFT> void init(InputFile::Kind k);
  template <typename ELFT> bool initQuietly(InputFile::Kind k);

  bool initialized = false;

  StringRef stringTable;
  const void *elfShdrs = nullptr;
  const void *elfSyms = nullptr;
  uint32_t numELFShdrs = 0;
  uint32_t numELFSyms = 0;
  uint32_t firstGlobal = 0;

public:
  // Progress of internGlobalSymbolNames() for this file, which workers may
  // run while ordered resolution proceeds on the main thread.
  enum class NameInterning : uint8_t { NotScheduled, Pending, Running, Done };
  std::atomic<NameInterning> nameInterning{NameInterning::NotScheduled};

  // Interned name slots for the global symbols (indexed from firstGlobal),
  // filled by internGlobalSymbolNames(). Valid once nameInterning is Done or
  // was never scheduled. A null array or entry means the name must be
  // inserted by hashing it.
  SymbolNameSlot **globalNameSlots = nullptr;
  // Interned signatures of this file's SHT_GROUP sections, in section order,
  // filled with globalNameSlots. A null entry is looked up by name instead.
  SymbolNameSlot **groupSignatureSlots = nullptr;
  uint32_t numGroupSignatureSlots = 0;
  // Member lists of this file's SHT_GROUP sections (in section order),
  // validated by internGlobalSymbolNames(). Null if not validated or if any
  // group is malformed, in which case parse() takes the diagnosing path.
  llvm::ArrayRef<uint32_t> *groupMembers = nullptr;
  // Keep decision per group recorded by parse() when it defers discarding
  // group members to initializeSections(); empty otherwise.
  llvm::SmallVector<uint8_t, 0> deferredGroupKeep;
  // Set by internGlobalSymbolNames() when every section name was readable and
  // none names the NeverC override section, so parse() can skip that scan.
  bool knownNoOverrideSection = false;
  // Indices of the sections parse() must visit (groups, dependent libraries,
  // MTE globals), in section order, recorded by internGlobalSymbolNames().
  // Null means parse() visits every section.
  uint32_t *specialSectionIndices = nullptr;
  uint32_t numSpecialSections = 0;
  uint32_t andFeatures = 0;
  bool hasCommonSyms = false;
  // Set once a thread has claimed initializing this file's sections, which
  // may start as soon as the file is parsed.
  std::atomic<bool> sectionsPrepared{false};
};

// .o file.
template <class ELFT> class ObjFile : public ELFFileBase {
  LLVM_ELF_IMPORT_TYPES_ELFT(ELFT)

public:
  static bool classof(const InputFile *f) { return f->kind() == ObjKind; }

  llvm::object::ELFFile<ELFT> getObj() const {
    return this->ELFFileBase::getObj<ELFT>();
  }

  ObjFile(ELFKind ekind, MemoryBufferRef m, StringRef archiveName)
      : ELFFileBase(ObjKind, ekind, m) {
    this->archiveName = archiveName;
  }

  void parse(bool ignoreComdats = false);
  void parseLazy();
  // Thread-safe; see ELFFileBase::globalNameSlots.
  void internGlobalSymbolNames();
  // Claim and run internGlobalSymbolNames() if it is still pending; returns
  // false if another thread already claimed it.
  bool tryInternGlobalSymbolNames();
  // Make this file's interned names available to the calling thread, doing
  // the work here if no worker has started it yet.
  void awaitInternedNames();
  SymbolNameSlot *getGroupSignatureSlot(uint32_t groupOrdinal,
                                        ArrayRef<Elf_Shdr> sections,
                                        const Elf_Shdr &sec);

  StringRef getShtGroupSignature(ArrayRef<Elf_Shdr> sections,
                                 const Elf_Shdr &sec);

  Symbol &getSymbol(uint32_t symbolIndex) const {
    if (symbolIndex >= numSymbols)
      fatal(toString(this) + ": invalid symbol index");
    return *this->symbols[symbolIndex];
  }

  uint32_t getSectionIndex(const Elf_Sym &sym) const;

  template <typename RelT> Symbol &getRelocTargetSym(const RelT &rel) const {
    uint32_t symIndex = rel.getSymbol();
    return getSymbol(symIndex);
  }

  std::optional<llvm::DILineInfo> getDILineInfo(const InputSectionBase *,
                                                uint64_t);
  std::optional<std::pair<std::string, unsigned>>
  getVariableLoc(StringRef name);

  // Name of source file obtained from STT_FILE symbol value,
  // or empty string if there is no such symbol in object file
  // symbol table.
  StringRef sourceFile;

  // Pointer to this input file's .llvm_addrsig section, if it has one.
  const Elf_Shdr *addrsigSec = nullptr;

  // SHT_LLVM_CALL_GRAPH_PROFILE section index.
  uint32_t cgProfileSectionIndex = 0;

  // True if the file defines functions compiled with
  // -fsplit-stack. Usually false.
  bool splitStack = false;

  // True if the file defines functions compiled with -fsplit-stack,
  // but had one or more functions with the no_split_stack attribute.
  bool someNoSplitStack = false;

  // Get cached DWARF information.
  DWARFCache *getDwarf();

  void prepareSectionsAndLocals(bool ignoreComdats);
  void postParse();

private:
  void initializeSections(bool ignoreComdats,
                          const llvm::object::ELFFile<ELFT> &obj);
  void initializeSymbols(const llvm::object::ELFFile<ELFT> &obj);
  void finishParse(const llvm::object::ELFFile<ELFT> &obj, StringRef shstrtab,
                   uint64_t size);
  void initializeJustSymbols();
  Symbol *insertGlobalSymbol(size_t i);

  InputSectionBase *getRelocTarget(uint32_t idx, const Elf_Shdr &sec,
                                   uint32_t info);
  InputSectionBase *createInputSection(uint32_t idx, const Elf_Shdr &sec,
                                       StringRef name);

  bool shouldMerge(const Elf_Shdr &sec, StringRef name);

  // Each ELF symbol contains a section index which the symbol belongs to.
  // However, because the number of bits dedicated for that is limited, a
  // symbol can directly point to a section only when the section index is
  // equal to or smaller than 65280.
  //
  // If an object file contains more than 65280 sections, the file must
  // contain .symtab_shndx section. The section contains an array of
  // 32-bit integers whose size is the same as the number of symbols.
  // Nth symbol's section index is in the Nth entry of .symtab_shndx.
  //
  // The following variable contains the contents of .symtab_shndx.
  // If the section does not exist (which is common), the array is empty.
  ArrayRef<Elf_Word> shndxTable;

  // Debugging information to retrieve source file and line for error
  // reporting. Linker may find reasonable number of errors in a
  // single object file, so we cache debugging information in order to
  // parse it only once for each object file we link.
  std::unique_ptr<DWARFCache> dwarf;
  llvm::once_flag initDwarf;
};

class BitcodeFile : public InputFile {
public:
  BitcodeFile(MemoryBufferRef m, StringRef archiveName,
              uint64_t offsetInArchive, bool lazy);
  static bool classof(const InputFile *f) { return f->kind() == BitcodeKind; }
  void parse();
  void parseLazy();
  void postParse();
  std::unique_ptr<llvm::lto::InputFile> obj;
  std::vector<bool> keptComdats;
};

// .so file.
class SharedFile : public ELFFileBase {
public:
  SharedFile(MemoryBufferRef m, StringRef defaultSoName);

  // This is actually a vector of Elf_Verdef pointers.
  SmallVector<const void *, 0> verdefs;

  // If the output file needs Elf_Verneed data structures for this file, this is
  // a vector of Elf_Vernaux version identifiers that map onto the entries in
  // Verdefs, otherwise it is empty.
  SmallVector<uint32_t, 0> vernauxs;

  SmallVector<StringRef, 0> dtNeeded;
  StringRef soName;

  static bool classof(const InputFile *f) { return f->kind() == SharedKind; }

  template <typename ELFT> void parse();

  // Used for --as-needed
  bool isNeeded;

  // Non-weak undefined symbols which are not yet resolved when the SO is
  // parsed. Only filled for `--no-allow-shlib-undefined`.
  SmallVector<Symbol *, 0> requiredSymbols;

  // What parse() reads from the file, including the interned names of the
  // symbols it adds, computed without side effects on a worker thread by
  // prepareSymbols() so that parse() only resolves symbols.
  struct PreparedSymbols {
    struct Entry {
      SymbolNameSlot *slot;
      const char *name;
      uint32_t nameSize;
      // Index among the global symbols of the dynamic symbol table.
      uint32_t symIndex;
      // Version id to record when this file provides the symbol.
      uint16_t versionId;
      bool undefined;
      uint32_t alignment;
    };
    SmallVector<StringRef, 0> dtNeeded;
    StringRef soName;
    SmallVector<const void *, 0> verdefs;
    std::vector<Entry> entries;
  };
  std::unique_ptr<PreparedSymbols> prepared;
  // Fills `prepared`; returns false, leaving it empty, if parse() would report
  // a diagnostic for this file. Thread-safe.
  template <typename ELFT> bool prepareSymbols();
  // Claims and runs prepareSymbols() if it is still pending; returns false if
  // another thread already claimed it.
  template <typename ELFT> bool tryPrepareSymbols();

private:
  template <typename ELFT> void parsePrepared();
  template <typename ELFT>
  std::vector<uint32_t> parseVerneed(const llvm::object::ELFFile<ELFT> &obj,
                                     const typename ELFT::Shdr *sec);
};

class BinaryFile : public InputFile {
public:
  explicit BinaryFile(MemoryBufferRef m) : InputFile(BinaryKind, m) {}
  static bool classof(const InputFile *f) { return f->kind() == BinaryKind; }
  void parse();
};

ELFFileBase *createObjFile(MemoryBufferRef mb, StringRef archiveName = "",
                           bool lazy = false);
// Like createObjFile(mb, archiveName, /*lazy=*/true), from any thread, for a
// buffer known to hold a valid 64-bit ELF header. The caller sets groupId.
ELFFileBase *createLazyObjFileFromWorker(MemoryBufferRef mb,
                                         StringRef archiveName);

} // namespace elf
} // namespace linker

#endif
