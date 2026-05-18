#ifndef LINKER_ELF_CONFIG_H
#define LINKER_ELF_CONFIG_H

#include "Linker/Core/Driver/Dispatcher.h"
#include "Linker/Core/Runtime/Diagnostic.h"
#include "llvm/ADT/CachedHashString.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Compression.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/GlobPattern.h"
#include "llvm/Support/PrettyStackTrace.h"
#include <atomic>
#include <memory>
#include <optional>
#include <vector>

namespace linker::elf {

class InputFile;
class BinaryFile;
class BitcodeFile;
class ELFFileBase;
class SharedFile;
class InputSectionBase;
class EhInputSection;
class Symbol;
class BitcodeCompiler;

enum ELFKind : uint8_t { ELFNoneKind, ELF64LEKind, ELF64BEKind };

// For -Bno-symbolic, -Bsymbolic-non-weak-functions, -Bsymbolic-functions,
// -Bsymbolic-non-weak, -Bsymbolic.
enum class BsymbolicKind { None, NonWeakFunctions, Functions, NonWeak, All };

// For --build-id.
enum class BuildIdStyle { None, Fast, Md5, Sha1, Hexstring, Uuid };

// For --call-graph-profile-sort={none,hfsort,cdsort}.
enum class CGProfileSortKind { None, Hfsort, Cdsort };

// For --discard-{all,locals,none}.
enum class DiscardPolicy { Default, All, Locals, None };

// For --icf={none,safe,all}.
enum class ICFLevel { None, Safe, All };

// For --strip-{all,debug}.
enum class StripPolicy { None, All, Debug };

// For --unresolved-symbols.
enum class UnresolvedPolicy { ReportError, Warn, Ignore };

// For --orphan-handling.
enum class OrphanHandlingPolicy { Place, Warn, Error };

// For --sort-section and linkerscript sorting rules.
enum class SortSectionPolicy {
  Default,
  None,
  Alignment,
  Name,
  Priority,
  Reverse,
};

// For -z noseparate-code, -z separate-code and -z separate-loadable-segments.
enum class SeparateSegmentKind { None, Code, Loadable };

// For -z *stack
enum class GnuStackKind { None, Exec, NoExec };

struct SymbolVersion {
  llvm::StringRef name;
  bool hasWildcard;
};

// This struct contains symbols version definition that
// can be found in version script if it is used for link.
struct VersionDefinition {
  llvm::StringRef name;
  uint16_t id;
  SmallVector<SymbolVersion, 0> nonLocalPatterns;
  SmallVector<SymbolVersion, 0> localPatterns;
};

class LinkerDriver {
public:
  void run(ArrayRef<const char *> args, const LinkerDriverConfig &driverCfg);
  void addFile(StringRef path, bool withLOption);
  void addLibrary(StringRef name);

private:
  void createFiles(llvm::opt::InputArgList &args);
  void inferMachineType();
  void execute(llvm::opt::InputArgList &args);
  template <class ELFT> void compileBitcodeFiles();
  // True if we are in --whole-archive and --no-whole-archive.
  bool inWholeArchive = false;

  // True if we are in --start-lib and --end-lib.
  bool inLib = false;

  std::unique_ptr<BitcodeCompiler> lto;
  std::vector<InputFile *> files;

public:
  SmallVector<std::pair<StringRef, unsigned>, 0> archiveFiles;
};

// This struct contains the global configuration for the linker.
// Most fields are direct mapping from the command line options
// and such fields have the same name as the corresponding options.
// Most fields are initialized by the ctx.driver.
struct Config {
  const LinkerDriverConfig *driverCfg = nullptr;
  uint8_t osabi = 0;
  uint32_t andFeatures = 0;
  llvm::SetVector<llvm::CachedHashString>
      dependencyFiles; // for --dependency-file
  llvm::StringMap<uint64_t> sectionStartMap;
  llvm::StringRef bfdname;
  llvm::StringRef dependencyFile;
  llvm::StringRef dynamicLinker;
  llvm::StringRef entry;
  llvm::StringRef emulation;
  llvm::StringRef fini;
  llvm::StringRef init;
  llvm::StringRef mapFile;
  llvm::StringRef outputFile;
  llvm::StringRef progName;
  llvm::StringRef printArchiveStats;
  llvm::StringRef printSymbolOrder;
  llvm::StringRef callGraphOrderingFile;
  llvm::StringRef soName;
  llvm::StringRef sysroot;
  llvm::StringRef whyExtract;
  StringRef zBtiReport = "none";
  StringRef zCetReport = "none";
  std::string rpath;
  llvm::SmallVector<VersionDefinition, 0> versionDefinitions;
  llvm::SmallVector<llvm::StringRef, 0> auxiliaryList;
  llvm::SmallVector<llvm::StringRef, 0> filterList;
  llvm::SmallVector<llvm::StringRef, 0> searchPaths;
  llvm::SmallVector<llvm::StringRef, 0> symbolOrderingFile;
  llvm::SmallVector<llvm::StringRef, 0> undefined;
  llvm::SmallVector<SymbolVersion, 0> dynamicList;
  llvm::SmallVector<uint8_t, 0> buildIdVector;
  llvm::MapVector<std::pair<const InputSectionBase *, const InputSectionBase *>,
                  uint64_t>
      callGraphProfile;
  bool allowMultipleDefinition;
  bool androidPackDynRelocs = false;
  bool asNeeded = false;
  BsymbolicKind bsymbolic = BsymbolicKind::None;
  CGProfileSortKind callGraphProfileSort;
  bool checkSections;
  bool checkDynamicRelocs;
  llvm::DebugCompressionType compressDebugSections;
  bool cref;
  llvm::SmallVector<std::pair<llvm::GlobPattern, uint64_t>, 0>
      deadRelocInNonAlloc;
  bool demangle = true;
  bool dependentLibraries;
  bool ehFrameHdr;
  bool emitRelocs;
  bool enableNewDtags;
  bool executeOnly;
  bool exportDynamic;
  bool fixCortexA53Errata843419;
  bool formatBinary = false;
  bool gcSections;
  bool gdbIndex;
  bool gnuHash = false;
  bool gnuUnique;
  bool hasDynSymTab;
  bool ignoreDataAddressEquality;
  bool ignoreFunctionAddressEquality;
  bool mmapOutputFile;
  bool nmagic;
  bool noDynamicLinker = false;
  bool noinhibitExec;
  bool nostdlib;
  bool oFormatBinary;
  bool omagic;
  bool optEB = false;
  bool optEL = false;
  bool optimizeBBJumps;
  bool picThunk;
  bool pie;
  bool printGcSections;
  bool printIcfSections;
  bool printMemoryUsage;
  bool relax;
  bool relocatable;
  bool relrGlibc = false;
  bool relrPackDynRelocs = false;
  llvm::SmallVector<std::pair<llvm::GlobPattern, uint32_t>, 0> shuffleSections;
  bool singleRoRx;
  bool shared;
  bool symbolic;
  bool isStatic = false;
  bool sysvHash = false;
  bool trace;
  bool undefinedVersion;
  bool unique;
  bool useAndroidRelrTags = false;
  bool warnBackrefs;
  llvm::SmallVector<llvm::GlobPattern, 0> warnBackrefsExclude;
  bool warnCommon;
  bool warnMissingEntry;
  bool warnSymbolOrdering;
  bool writeAddends;
  bool zCombreloc;
  bool zCopyreloc;
  bool zForceBti;
  bool zForceIbt;
  bool zGlobal;
  bool zIfuncNoplt;
  bool zInitfirst;
  bool zInterpose;
  bool zKeepTextSectionPrefix;
  bool zNodefaultlib;
  bool zNodelete;
  bool zNodlopen;
  bool zNow;
  bool zOrigin;
  bool zPacPlt;
  bool zRelro;
  bool zRodynamic;
  bool zShstk;
  bool zStartStopGC;
  uint8_t zStartStopVisibility;
  bool zText;
  bool zRetpolineplt;
  bool zWxneeded;
  DiscardPolicy discard;
  GnuStackKind zGnustack;
  ICFLevel icf;
  OrphanHandlingPolicy orphanHandling;
  SortSectionPolicy sortSection;
  StripPolicy strip;
  UnresolvedPolicy unresolvedSymbols;
  UnresolvedPolicy unresolvedSymbolsInShlib;
  BuildIdStyle buildId = BuildIdStyle::None;
  SeparateSegmentKind zSeparate;
  ELFKind ekind = ELFNoneKind;
  uint16_t emachine = llvm::ELF::EM_NONE;
  std::optional<uint64_t> imageBase;
  uint64_t commonPageSize;
  uint64_t maxPageSize;
  uint64_t zStackSize;
  unsigned optimize;
  int32_t splitStackAdjustSize;
  StringRef packageMetadata;

  // The following config options do not directly correspond to any
  // particular command line options.

  // True if we need to pass through relocations in input files to the
  // output file. Usually false because we consume relocations.
  bool copyRelocs;

  // NeverC only supports 64-bit targets (x86_64 / AArch64).
  static constexpr bool is64 = true;

  // True if the target is little-endian. False if big-endian.
  bool isLE;

  // endianness::little if isLE is true. endianness::big otherwise.
  llvm::endianness endianness;

  // True if we need to set the DF_STATIC_TLS flag to an output file, which
  // works as a hint to the dynamic loader that the shared object contains code
  // compiled with the initial-exec TLS model.
  bool hasTlsIe = false;

  // Holds set of ELF header flags for the target.
  uint32_t eflags = 0;

  // The ELF spec defines two types of relocation table entries, RELA and
  // REL. RELA is a triplet of (offset, info, addend) while REL is a
  // tuple of (offset, info). Addends for REL are implicit and read from
  // the location where the relocations are applied. So, REL is more
  // compact than RELA but requires a bit of more work to process.
  //
  // (From the linker writer's view, this distinction is not necessary.
  // If the ELF had chosen whichever and sticked with it, it would have
  // been easier to write code to process relocations, but it's too late
  // to change the spec.)
  //
  // Each ABI defines its relocation type. IsRela is true if target
  // uses RELA. As far as we know, all 64-bit ABIs are using RELA. A
  // few 32-bit ABIs are using RELA too.
  bool isRela;

  // True if we are creating position-independent code.
  bool isPic;

  // NeverC only supports 64-bit targets.
  static constexpr int wordsize = 8;

  // Mode of MTE to write to the ELF note. Should be one of NT_MEMTAG_ASYNC (for
  // async), NT_MEMTAG_SYNC (for sync), or NT_MEMTAG_LEVEL_NONE (for none). If
  // async or sync is enabled, write the ELF note specifying the default MTE
  // mode.
  int androidMemtagMode;
  // Signal to the dynamic loader to enable heap MTE.
  bool androidMemtagHeap;
  // Signal to the dynamic loader that this binary expects stack MTE. Generally,
  // this means to map the primary and thread stacks as PROT_MTE. Note: This is
  // not supported on Android 11 & 12.
  bool androidMemtagStack;

  unsigned threadCount;

  // If an input file equals a key, remap it to the value.
  llvm::DenseMap<llvm::StringRef, llvm::StringRef> remapInputs;
  // If an input file matches a wildcard pattern, remap it to the value.
  llvm::SmallVector<std::pair<llvm::GlobPattern, llvm::StringRef>, 0>
      remapInputsWildcards;
};
struct ConfigWrapper {
  Config c;
  Config *operator->() { return &c; }
};

LLVM_LIBRARY_VISIBILITY extern ConfigWrapper config;

struct DuplicateSymbol {
  const Symbol *sym;
  const InputFile *file;
  InputSectionBase *section;
  uint64_t value;
};

struct Ctx {
  LinkerDriver driver;
  SmallVector<std::unique_ptr<MemoryBuffer>> memoryBuffers;
  SmallVector<ELFFileBase *, 0> objectFiles;
  SmallVector<SharedFile *, 0> sharedFiles;
  SmallVector<BinaryFile *, 0> binaryFiles;
  SmallVector<BitcodeFile *, 0> bitcodeFiles;
  SmallVector<BitcodeFile *, 0> lazyBitcodeFiles;
  SmallVector<InputSectionBase *, 0> inputSections;
  SmallVector<EhInputSection *, 0> ehInputSections;
  // Duplicate symbol candidates.
  SmallVector<DuplicateSymbol, 0> duplicates;
  // Symbols in a non-prevailing COMDAT group which should be changed to an
  // Undefined.
  SmallVector<std::pair<Symbol *, unsigned>, 0> nonPrevailingSyms;
  // A tuple of (reference, extractedFile, sym). Used by --why-extract=.
  SmallVector<std::tuple<std::string, const InputFile *, const Symbol &>, 0>
      whyExtractRecords;
  // A mapping from a symbol to an InputFile referencing it backward. Used by
  // --warn-backrefs.
  llvm::DenseMap<const Symbol *,
                 std::pair<const InputFile *, const InputFile *>>
      backwardReferences;
  llvm::SmallSet<llvm::StringRef, 0> auxiliaryFiles;
  // True if SHT_LLVM_SYMPART is used.
  std::atomic<bool> hasSympart{false};
  // True if there are TLS IE relocations. Set DF_STATIC_TLS if -shared.
  std::atomic<bool> hasTlsIe{false};
  // True if we need to reserve two .got entries for local-dynamic TLS model.
  std::atomic<bool> needsTlsLd{false};

  // Symbols marked with __attribute__((override)) or --override=<sym>.
  // Maps symbol name → originating InputFile (nullptr for --override flag).
  llvm::DenseMap<llvm::StringRef, const InputFile *> overrideSymbols;

  // Each symbol assignment and DEFINED(sym) reference is assigned an increasing
  // order. Each DEFINED(sym) evaluation checks whether the reference happens
  // before a possible `sym = expr;`.
  unsigned scriptSymOrderCounter = 1;
  llvm::DenseMap<const Symbol *, unsigned> scriptSymOrder;

  void reset();

  llvm::raw_fd_ostream openAuxiliaryFile(llvm::StringRef, std::error_code &);
};

LLVM_LIBRARY_VISIBILITY extern Ctx ctx;

// The first two elements of versionDefinitions represent VER_NDX_LOCAL and
// VER_NDX_GLOBAL. This helper returns other elements.
static inline ArrayRef<VersionDefinition> namedVersionDefs() {
  return llvm::ArrayRef(config->versionDefinitions).slice(2);
}

void errorOrWarn(const Twine &msg);

static inline void internalLinkerError(StringRef loc, const Twine &msg) {
  errorOrWarn(loc + "internal linker error: " + msg + "\n" +
              llvm::getBugReportMsg());
}

} // namespace linker::elf

#endif
