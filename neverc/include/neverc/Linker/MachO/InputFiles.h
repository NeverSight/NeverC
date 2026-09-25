#ifndef LINKER_MACHO_INPUT_FILES_H
#define LINKER_MACHO_INPUT_FILES_H

#include "Linker/MachO/MachOStructs.h"
#include "Linker/MachO/Target.h"

#include "Linker/Core/Runtime/Allocator.h"
#include "Linker/Core/Runtime/LinkerParallel.h"
#include "Linker/Core/Runtime/NameTable.h"
#include "Linker/Core/Support/Dwarf.h"
#include "Linker/Core/Support/LlvmAliases.h"
#include "llvm/ADT/CachedHashString.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/DWARF/DWARFUnit.h"
#include "llvm/Object/Archive.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Threading.h"
#include "llvm/TextAPI/TextAPIReader.h"

#include <atomic>
#include <mutex>
#include <vector>

namespace llvm {
namespace lto {
class InputFile;
} // namespace lto
namespace MachO {
class InterfaceFile;
} // namespace MachO
} // namespace llvm

namespace linker {
namespace macho {

struct PlatformInfo;
class ConcatInputSection;
class Symbol;
using SymbolNameSlot = NameSlot<Symbol>;
class Defined;
class AliasSymbol;
struct Reloc;
enum class RefState : uint8_t;
int &machoInputFileIdCount();

// If .subsections_via_symbols is set, each InputSection will be split along
// symbol boundaries. The field offset represents the offset of the subsection
// from the start of the original pre-split InputSection.
struct Subsection {
  uint64_t offset = 0;
  InputSection *isec = nullptr;
};

using Subsections = std::vector<Subsection>;
class InputFile;

class Section {
public:
  InputFile *file;
  StringRef segname;
  StringRef name;
  uint32_t flags;
  uint64_t addr;
  Subsections subsections;

  Section(InputFile *file, StringRef segname, StringRef name, uint32_t flags,
          uint64_t addr)
      : file(file), segname(segname), name(name), flags(flags), addr(addr) {}
  // Ensure pointers to Sections are never invalidated.
  Section(const Section &) = delete;
  Section &operator=(const Section &) = delete;
  Section(Section &&) = delete;
  Section &operator=(Section &&) = delete;

private:
  // Whether we have already split this section into individual subsections.
  // For sections that cannot be split (e.g. literal sections), this is always
  // false.
  bool doneSplitting = false;
  friend class ObjFile;
};

// Represents a call graph profile edge.
struct CallGraphEntry {
  // The index of the caller in the symbol table.
  uint32_t fromIndex;
  // The index of the callee in the symbol table.
  uint32_t toIndex;
  // Number of calls from callee to caller in the profile.
  uint64_t count;

  CallGraphEntry(uint32_t fromIndex, uint32_t toIndex, uint64_t count)
      : fromIndex(fromIndex), toIndex(toIndex), count(count) {}
};

class InputFile {
public:
  enum Kind {
    ObjKind,
    OpaqueKind,
    DylibKind,
    ArchiveKind,
    BitcodeKind,
  };

  virtual ~InputFile() = default;
  Kind kind() const { return fileKind; }
  StringRef getName() const { return name; }
  static void resetIdCount() { machoInputFileIdCount() = 0; }

  MemoryBufferRef mb;

  std::vector<Symbol *> symbols;
  std::vector<Section *> sections;

  // If not empty, this stores the name of the archive containing this file.
  // We use this string for creating error messages.
  std::string archiveName;

  // Provides an easy way to sort InputFiles deterministically.
  const int id;

  // True if this is a lazy ObjFile or BitcodeFile.
  bool lazy = false;

protected:
  InputFile(Kind kind, MemoryBufferRef mb, bool lazy = false)
      : mb(mb), id(machoInputFileIdCount()++), lazy(lazy), fileKind(kind),
        name(mb.getBufferIdentifier()) {}

  InputFile(Kind, const llvm::MachO::InterfaceFile &);

  // If true, this input's arch is compatible with target.
  bool compatArch = true;

private:
  const Kind fileKind;
  const StringRef name;
};

struct FDE {
  uint32_t funcLength;
  Symbol *personality;
  InputSection *lsda;
};

struct DebugSection {
  StringRef name;
  ArrayRef<uint8_t> data;
};

// .o file
class ObjFile final : public InputFile {
public:
  ObjFile(MemoryBufferRef mb, uint32_t modTime, StringRef archiveName,
          bool lazy = false, bool forceHidden = false, bool compatArch = true,
          bool builtFromBitcode = false);
  ArrayRef<llvm::MachO::data_in_code_entry> getDataInCode() const;
  ArrayRef<uint8_t> getOptimizationHints() const;
  template <class LP> void parse();
  // Parses the relocations that parse() left for finishDeferredRelocations().
  template <class LP> void parseDeferredRelocations();
  template <class LP>
  void parseLinkerOptions(llvm::SmallVectorImpl<StringRef> &LinkerOptions);

  static bool classof(const InputFile *f) { return f->kind() == ObjKind; }

  bool hasCompileUnit();
  std::string sourceFile();
  // Parses line table information for diagnostics. compileUnit should be used
  // for other purposes.
  linker::DWARFCache *getDwarf();

  llvm::DWARFUnit *compileUnit = nullptr;
  std::unique_ptr<linker::DWARFCache> dwarfCache;
  Section *addrSigSection = nullptr;
  const uint32_t modTime;
  bool forceHidden;
  bool builtFromBitcode;
  std::vector<DebugSection> debugSections;
  std::vector<CallGraphEntry> callGraph;
  llvm::DenseMap<ConcatInputSection *, FDE> fdes;
  std::vector<AliasSymbol *> aliases;
  llvm::SmallVector<Defined *, 0> localNoDeadStripSymbols;

private:
  // Slots of this file's external symbol names by symbol index, interned
  // ahead of parsing by background workers. Empty when not available.
  ArrayRef<SymbolNameSlot *> internedNames;
  SymbolNameSlot *internedNameSlot(uint32_t index) const {
    return index < internedNames.size() ? internedNames[index] : nullptr;
  }

  llvm::once_flag initDwarf;
  llvm::once_flag initCompileUnit;
  std::unique_ptr<llvm::DWARFContext> compileUnitContext;
  template <class LP> void parseLazy();
  template <class SectionHeader> void parseSections(ArrayRef<SectionHeader>);
  template <class LP>
  void parseSymbols(ArrayRef<typename LP::section> sectionHeaders,
                    ArrayRef<typename LP::nlist> nList, const char *strtab,
                    bool subsectionsViaSymbols);
  template <class NList>
  Symbol *parseNonSectionSymbol(const NList &sym, const char *strtab);
  template <class SectionHeader>
  void parseRelocations(ArrayRef<SectionHeader> sectionHeaders,
                        const SectionHeader &, Section &);
  void parseDebugInfoIfNeeded();
  void parseDebugInfo();
  void splitEhFrames(ArrayRef<uint8_t> dataArr, Section &ehFrameSection);
  void registerCompactUnwind(Section &compactUnwindSection);
  void registerEhFrames(Section &ehFrameSection);
};

// While the command-line inputs are loaded, ObjFile::parse() leaves the
// relocations of ordinary sections unparsed; nothing reads them until loading
// ends, so finishDeferredRelocations() parses them for all files in parallel.
// Unwind-info sections are still parsed immediately because their
// registration depends on which definitions prevail at load time.
void beginDeferredRelocations();
void finishDeferredRelocations();
void resetDeferredRelocations();

// State of loading one link's inputs, owned by its MachOLinkerContext.
struct MachOLoadState {
  // Files whose ordinary relocations wait for finishDeferredRelocations().
  bool deferRelocations = false;
  std::mutex deferredMutex;
  std::vector<ObjFile *> deferredFiles;
  // Background interning of archive members' names.
  bool interningOpen = false;
  std::atomic<bool> interningCancelled{false};
  std::unique_ptr<LinkerTaskGroup> interningWorkers;
  // The interned names of the archive member being extracted, handed from
  // ArchiveFile::fetch() to the ObjFile::parse() it triggers.
  ArrayRef<SymbolNameSlot *> pendingInternedNames;
};
MachOLoadState &machoLoadState();

// Archive members' external symbol names are interned by background workers
// while inputs load (see ArchiveFile::startNameInterning()); the ordered
// resolution then finds each name's slot without hashing it. finish waits for
// the workers and must run before the worker pool is reconfigured.
void beginMemberNameInterning();
void finishMemberNameInterning();

// command-line -sectcreate file
class OpaqueFile final : public InputFile {
public:
  OpaqueFile(MemoryBufferRef mb, StringRef segName, StringRef sectName);
  static bool classof(const InputFile *f) { return f->kind() == OpaqueKind; }
};

// .dylib or .tbd file
class DylibFile final : public InputFile {
public:
  // Mach-O dylibs can re-export other dylibs as sub-libraries, meaning that the
  // symbols in those sub-libraries will be available under the umbrella
  // library's namespace. Those sub-libraries can also have their own
  // re-exports. When loading a re-exported dylib, `umbrella` should be set to
  // the root dylib to ensure symbols in the child library are correctly bound
  // to the root. On the other hand, if a dylib is being directly loaded
  // (through an -lfoo flag), then `umbrella` should be a nullptr.
  explicit DylibFile(MemoryBufferRef mb, DylibFile *umbrella,
                     bool isBundleLoader, bool explicitlyLinked);
  explicit DylibFile(const llvm::MachO::InterfaceFile &interface,
                     DylibFile *umbrella, bool isBundleLoader,
                     bool explicitlyLinked);
  explicit DylibFile(DylibFile *umbrella);

  void parseLoadCommands(MemoryBufferRef mb);
  void parseReexports(const llvm::MachO::InterfaceFile &interface);
  bool isReferenced() const { return numReferencedSymbols > 0; }
  bool isExplicitlyLinked() const;
  void setExplicitlyLinked() { explicitlyLinked = true; }

  static bool classof(const InputFile *f) { return f->kind() == DylibKind; }

  StringRef installName;
  DylibFile *exportingFile = nullptr;
  DylibFile *umbrella;
  SmallVector<StringRef, 2> rpaths;
  uint32_t compatibilityVersion = 0;
  uint32_t currentVersion = 0;
  int64_t ordinal = 0; // Ordinal numbering starts from 1, so 0 is a sentinel
  unsigned numReferencedSymbols = 0;
  RefState refState;
  bool reexport = false;
  bool forceNeeded = false;
  bool forceWeakImport = false;
  bool upward = false; // loaded with LC_LOAD_UPWARD_DYLIB
  bool deadStrippable = false;

private:
  bool explicitlyLinked = false; // Access via isExplicitlyLinked().

public:
  // An executable can be used as a bundle loader that will load the output
  // file being linked, and that contains symbols referenced, but not
  // implemented in the bundle. When used like this, it is very similar
  // to a dylib, so we've used the same class to represent it.
  bool isBundleLoader;

  // Synthetic Dylib objects created by $ld$previous symbols in this dylib.
  // Usually empty. These synthetic dylibs won't have synthetic dylibs
  // themselves.
  SmallVector<DylibFile *, 2> extraDylibs;

private:
  DylibFile *getSyntheticDylib(StringRef installName, uint32_t currentVersion,
                               uint32_t compatVersion);

  bool handleLDSymbol(StringRef originalName);
  void handleLDPreviousSymbol(StringRef name, StringRef originalName);
  void handleLDInstallNameSymbol(StringRef name, StringRef originalName);
  void handleLDHideSymbol(StringRef name, StringRef originalName);
  void checkAppExtensionSafety(bool dylibIsAppExtensionSafe) const;
  void parseExportedSymbols(uint32_t offset, uint32_t size);
  void loadReexport(StringRef path, DylibFile *umbrella,
                    const llvm::MachO::InterfaceFile *currentTopLevelTapi);

  llvm::DenseSet<llvm::CachedHashStringRef> hiddenSymbols;
};

// .a file
class ArchiveFile final : public InputFile {
public:
  explicit ArchiveFile(std::unique_ptr<llvm::object::Archive> &&file,
                       bool forceHidden);
  void addLazySymbols();
  void fetch(const llvm::object::Archive::Symbol &);
  // The linker normally doesn't use Error for error-handling, but the
  // underlying Archive library does, so this is the cleanest way to wrap it.
  Error fetch(const llvm::object::Archive::Child &, StringRef reason);
  const llvm::object::Archive &getArchive() const { return *file; };
  static bool classof(const InputFile *f) { return f->kind() == ArchiveKind; }

  // Queues this archive's object members for background name interning once
  // a worker pool is available. Does nothing otherwise, or if already queued.
  void startNameInterning();

  // Background interning state of one object member.
  struct MemberNames {
    enum State : uint8_t { Pending, Running, Done, Skipped };
    MemoryBufferRef mb;
    std::atomic<uint8_t> state{Pending};
    std::vector<SymbolNameSlot *> slots;
  };

private:
  friend class MemberNameInterning;
  std::unique_ptr<llvm::object::Archive> file;
  // Object members queued for interning, and their lookup by child offset.
  llvm::DenseMap<uint64_t, MemberNames *> memberNames;
  std::vector<MemberNames *> memberQueue;
  bool nameInterningStarted = false;
  // Keep track of children fetched from the archive by tracking
  // which address offsets have been fetched already.
  llvm::DenseSet<uint64_t> seen;
  // Load all symbols with hidden visibility (-load_hidden).
  bool forceHidden;
};

class BitcodeFile final : public InputFile {
public:
  explicit BitcodeFile(MemoryBufferRef mb, StringRef archiveName,
                       uint64_t offsetInArchive, bool lazy = false,
                       bool forceHidden = false, bool compatArch = true);
  static bool classof(const InputFile *f) { return f->kind() == BitcodeKind; }
  void parse();

  std::unique_ptr<llvm::lto::InputFile> obj;
  bool forceHidden;

private:
  void parseLazy();
};

llvm::SetVector<InputFile *> &machoInputFiles();
llvm::DenseMap<llvm::CachedHashStringRef, MemoryBufferRef> &
machoCachedReads();
llvm::SmallVector<StringRef> &machoUnprocessedLCLinkerOptions();

std::optional<MemoryBufferRef> readFile(StringRef path,
                                        bool reportError = true);

void extract(InputFile &file, StringRef reason);

namespace detail {

template <class CommandType, class... Types>
std::vector<const CommandType *>
findCommands(const void *anyHdr, size_t maxCommands, Types... types) {
  std::vector<const CommandType *> cmds;
  std::initializer_list<uint32_t> typesList{types...};
  const auto *hdr = reinterpret_cast<const llvm::MachO::mach_header *>(anyHdr);
  const uint8_t *p =
      reinterpret_cast<const uint8_t *>(hdr) + target->headerSize;
  for (uint32_t i = 0, n = hdr->ncmds; i < n; ++i) {
    auto *cmd = reinterpret_cast<const CommandType *>(p);
    if (llvm::is_contained(typesList, cmd->cmd)) {
      cmds.push_back(cmd);
      if (cmds.size() == maxCommands)
        return cmds;
    }
    p += cmd->cmdsize;
  }
  return cmds;
}

} // namespace detail

// anyHdr should be a pointer to either mach_header or mach_header_64
template <class CommandType = llvm::MachO::load_command, class... Types>
const CommandType *findCommand(const void *anyHdr, Types... types) {
  std::vector<const CommandType *> cmds =
      detail::findCommands<CommandType>(anyHdr, 1, types...);
  return cmds.size() ? cmds[0] : nullptr;
}

template <class CommandType = llvm::MachO::load_command, class... Types>
std::vector<const CommandType *> findCommands(const void *anyHdr,
                                              Types... types) {
  return detail::findCommands<CommandType>(anyHdr, 0, types...);
}

} // namespace macho

std::string toString(const macho::InputFile *file);
std::string toString(const macho::Section &);
} // namespace linker

#endif
