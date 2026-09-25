#ifndef LINKER_MACHO_CONFIG_H
#define LINKER_MACHO_CONFIG_H

#include "llvm/ADT/CachedHashString.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/Support/GlobPattern.h"
#include "llvm/Support/VersionTuple.h"
#include "llvm/TextAPI/Architecture.h"
#include "llvm/TextAPI/Platform.h"
#include "llvm/TextAPI/Target.h"

#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace llvm {
enum class CodeGenOptLevel;
} // namespace llvm

namespace linker {
struct LinkerDriverConfig;
namespace macho {

class InputFile;
class InputSection;
class Symbol;

using NamePair = std::pair<llvm::StringRef, llvm::StringRef>;
using SectionRenameMap = llvm::DenseMap<NamePair, NamePair>;
using SegmentRenameMap = llvm::DenseMap<llvm::StringRef, llvm::StringRef>;

struct PlatformInfo {
  llvm::MachO::Target target;
  llvm::VersionTuple sdk;
};

inline uint32_t encodeVersion(const llvm::VersionTuple &version) {
  return ((version.getMajor() << 020) | (version.getMinor() << 010) |
          version.getSubminor());
}

enum class NamespaceKind {
  twolevel,
  flat,
};

enum class UndefinedSymbolTreatment {
  unknown,
  error,
  warning,
  suppress,
  dynamic_lookup,
};

enum class ICFLevel {
  unknown,
  none,
  safe,
  all,
};

struct SectionAlign {
  llvm::StringRef segName;
  llvm::StringRef sectName;
  uint32_t align;
};

struct SegmentProtection {
  llvm::StringRef name;
  uint32_t maxProt;
  uint32_t initProt;
};

class SymbolPatterns {
public:
  // GlobPattern can also match literals,
  // but we prefer the O(1) lookup of DenseSet.
  llvm::DenseSet<llvm::CachedHashStringRef> literals;
  std::vector<llvm::GlobPattern> globs;

  bool empty() const { return literals.empty() && globs.empty(); }
  void clear();
  void insert(llvm::StringRef symbolName);
  bool matchLiteral(llvm::StringRef symbolName) const;
  bool matchGlob(llvm::StringRef symbolName) const;
  bool match(llvm::StringRef symbolName) const;
};

// How a tentative definition in an object file and a definition of the same
// name in a dylib resolve (-commons).
enum class CommonsTreatment {
  IgnoreDylibs,
  UseDylibs,
  Error,
};

// How a symbol referenced both weakly and strongly is imported
// (-weak_reference_mismatches).
enum class WeakReferenceMismatches {
  NonWeak,
  Weak,
  Error,
};

// What a relocation that needs dyld to write read-only code does
// (-read_only_relocs).
enum class ReadOnlyRelocs {
  Error,
  Warning,
  Suppress,
};

enum class SymtabPresence {
  All,
  None,
  SelectivelyIncluded,
  SelectivelyExcluded,
};

struct Configuration {
  Symbol *entry = nullptr;
  bool hasReexports = false;
  bool allLoad = false;
  bool applicationExtension = false;
  bool archMultiple = false;
  bool exportDynamic = false;
  bool staticLink = false;
  bool implicitDylibs = false;
  bool isPic = false;
  bool headerPadMaxInstallNames = false;
  bool markDeadStrippableDylib = false;
  bool printDylibSearch = false;
  bool printEachFile = false;
  bool printWhyLoad = false;
  bool searchDylibsFirst = false;
  bool adhocCodesign = false;
  bool emitFunctionStarts = false;
  bool emitDataInCodeInfo = false;
  bool emitEncryptionInfo = false;
  bool emitInitOffsets = false;
  bool emitChainedFixups = false;
  bool dataConst = false;
  bool dedupStrings = true;
  bool deadStripDuplicates = false;
  bool omitDebugInfo = false;
  bool stripAllSymbols = false;
  bool warnDylibInstallName = false;
  bool ignoreOptimizationHints = false;
  bool forceExactCpuSubtypeMatch = false;
  uint32_t headerPad;
  uint32_t dylibCompatibilityVersion = 0;
  uint32_t dylibCurrentVersion = 0;
  unsigned optimize;
  std::string progName;

  // For multi-arch builds, NeverC invokes the linker once per arch and
  // then runs `lipo` to merge the per-arch outputs into a single file.
  // `outputFile` is the per-arch linker output; `finalOutput` is the
  // merged file that lipo produces.
  llvm::StringRef outputFile;
  llvm::StringRef finalOutput;

  llvm::StringRef installName;
  llvm::StringRef mapFile;
  llvm::StringRef umbrella;
  bool deadStripDylibs = false;
  bool demangle = false;
  bool deadStrip = false;
  bool errorForArchMismatch = false;
  bool ignoreAutoLink = false;
  // ld64 allows invalid auto link options as long as the link succeeds. The
  // linker does not, but there are cases in the wild where the invalid linker
  // options exist. This allows users to ignore the specific invalid options in
  // the case they can't easily fix them.
  llvm::StringSet<> ignoreAutoLinkOptions;
  bool strictAutoLink = false;
  PlatformInfo platformInfo;
  NamespaceKind namespaceKind = NamespaceKind::twolevel;
  UndefinedSymbolTreatment undefinedSymbolTreatment =
      UndefinedSymbolTreatment::error;
  ICFLevel icfLevel = ICFLevel::none;
  llvm::MachO::HeaderFileType outputType;
  std::vector<llvm::StringRef> systemLibraryRoots;
  std::vector<llvm::StringRef> librarySearchPaths;
  std::vector<llvm::StringRef> frameworkSearchPaths;
  llvm::SmallVector<llvm::StringRef, 0> runtimePaths;
  std::vector<std::string> astPaths;
  std::vector<Symbol *> explicitUndefineds;
  llvm::StringSet<> explicitDynamicLookups;
  // There are typically few custom sectionAlignments or segmentProtections,
  // so use a vector instead of a map.
  std::vector<SectionAlign> sectionAlignments;
  std::vector<SegmentProtection> segmentProtections;
  const LinkerDriverConfig *driverCfg = nullptr;

  bool callGraphProfileSort = false;
  llvm::StringRef printSymbolOrder;

  SectionRenameMap sectionRenameMap;
  SegmentRenameMap segmentRenameMap;

  bool hasExplicitExports = false;
  SymbolPatterns exportedSymbols;
  SymbolPatterns unexportedSymbols;
  SymbolPatterns whyLive;

  std::vector<std::pair<llvm::StringRef, llvm::StringRef>> aliasedSymbols;

  SymtabPresence localSymbolsPresence = SymtabPresence::All;
  SymbolPatterns localSymbolPatterns;
  bool zeroModTime = true;
  bool generateUuid = true;
  bool randomUuid = false;

  // Native options that set load commands and header flags.
  uint64_t stackSize = 0;
  std::optional<uint64_t> imageBase;
  std::optional<uint64_t> sourceVersion;
  llvm::StringRef initFunction;
  Symbol *initSymbol = nullptr;
  llvm::StringRef clientName;
  llvm::StringRef executablePath;
  std::vector<llvm::StringRef> allowableClients;
  llvm::DenseMap<llvm::StringRef, llvm::StringRef> dylibFiles;
  bool allowStackExecute = false;
  bool bindAtLoad = false;
  bool forceFlatNamespace = false;
  bool noWeakImports = false;
  bool noWeakExports = false;
  bool warnWeakExports = false;
  bool whatsLoaded = false;
  bool dedupSymbolStrings = true;
  bool noBranchIslands = false;
  bool warnCommons = false;
  bool printStatistics = false;
  CommonsTreatment commons = CommonsTreatment::IgnoreDylibs;
  // Alignment bound of tentative definitions without an explicit alignment.
  uint64_t maxDefaultCommonAlign = 0x8000;
  // Minimum alignment of segment addresses, and fixed segment addresses.
  uint64_t segmentAlign = 0;
  llvm::DenseMap<llvm::StringRef, uint64_t> segmentAddresses;
  SymbolPatterns reexportedSymbols;
  // -force_symbols_weak_list and -force_symbols_not_weak_list override the
  // weakness exported definitions carry.
  SymbolPatterns forceWeakSymbols;
  SymbolPatterns forceNotWeakSymbols;
  llvm::DenseMap<llvm::StringRef, uint64_t> segmentPageSizes;
  bool rootSafe = false;
  bool setuidSafe = false;
  bool forceCpuSubtypeAll = false;
  bool noInits = false;
  bool noArchWarnings = false;
  bool verboseDeduplicate = false;
  bool pageAlignDataAtoms = false;
  bool noZeroFillSections = false;
  bool mergeZeroFillSections = false;
  bool orderFileStatistics = false;
  bool noOrderData = false;
  bool warnStabs = false;
  bool forceLoadObjC = false;
  bool interposable = false;
  ReadOnlyRelocs readOnlyRelocs = ReadOnlyRelocs::Warning;
  // What a pointer dyld fixes up at an unaligned address does.
  ReadOnlyRelocs unalignedPointers = ReadOnlyRelocs::Warning;
  bool traceSymbolLayout = false;
  bool forceLoadSwiftLibs = false;
  bool warnCompactUnwind = false;
  bool verboseOptimizationHints = false;
  bool textExec = false;
  bool keepDwarfUnwind = false;
  bool noNewMain = false;
  // -exported_symbols_order: each listed name's position.
  llvm::DenseMap<llvm::CachedHashStringRef, uint32_t> exportedSymbolsOrder;
  WeakReferenceMismatches weakReferenceMismatches =
      WeakReferenceMismatches::NonWeak;
  // Sections -move_to_ro_segment and -move_to_rw_segment send to another
  // segment.
  llvm::DenseMap<const InputSection *, llvm::StringRef> movedSections;
  SymbolPatterns interposableSymbols;

  llvm::StringRef osoPrefix;

  std::vector<llvm::StringRef> dyldEnvs;

  llvm::MachO::Architecture arch() const { return platformInfo.target.Arch; }

  llvm::MachO::PlatformType platform() const {
    return platformInfo.target.Platform;
  }

  // Symbols marked with __attribute__((override)) or --override=<sym>.
  // Maps symbol name → originating InputFile (nullptr for --override flag).
  llvm::DenseMap<llvm::StringRef, const InputFile *> overrideSymbols;
};

std::unique_ptr<Configuration> &machoConfig();

struct ConfigurationAccessor {
  Configuration *operator->() const { return machoConfig().get(); }
  Configuration &operator*() const { return *machoConfig(); }
  explicit operator bool() const {
    return static_cast<bool>(machoConfig());
  }
  void operator=(std::unique_ptr<Configuration> Value) const {
    machoConfig() = std::move(Value);
  }
};

inline constexpr ConfigurationAccessor config;

} // namespace macho
} // namespace linker

#endif
