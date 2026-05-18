#ifndef LINKER_COFF_CONFIG_H
#define LINKER_COFF_CONFIG_H

#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Object/COFF.h"
#include "llvm/Support/VirtualFileSystem.h"
#include <cstdint>
#include <map>
#include <set>
#include <string>

namespace linker {
struct LinkerDriverConfig;
} // namespace linker

namespace linker::coff {

using llvm::StringRef;
using llvm::COFF::IMAGE_FILE_MACHINE_UNKNOWN;
using llvm::COFF::WindowsSubsystem;
class DefinedAbsolute;
class StringChunk;
class Symbol;
class InputFile;
class SectionChunk;

static const auto AMD64 = llvm::COFF::IMAGE_FILE_MACHINE_AMD64;
static const auto ARM64 = llvm::COFF::IMAGE_FILE_MACHINE_ARM64;

enum class ExportSource {
  Unset,
  Directives,
  Export,
  ModuleDefinition,
};

// Represents an --export option.
struct Export {
  StringRef name;        // N in --export=N or --export=E=N
  StringRef extName;     // E in --export=E=N
  StringRef aliasTarget; // GNU specific: N in "alias == N"
  Symbol *sym = nullptr;
  uint16_t ordinal = 0;
  bool noname = false;
  bool data = false;
  bool isPrivate = false;
  bool constant = false;

  // If an export is a form of --export=foo=dllname.bar, that means
  // that foo should be exported as an alias to bar in the DLL.
  // forwardTo is set to "dllname.bar" part. Usually empty.
  StringRef forwardTo;
  StringChunk *forwardChunk = nullptr;

  ExportSource source = ExportSource::Unset;
  StringRef symbolName;
  StringRef exportName; // Name in DLL

  bool operator==(const Export &e) const {
    return (name == e.name && extName == e.extName &&
            aliasTarget == e.aliasTarget && ordinal == e.ordinal &&
            noname == e.noname && data == e.data && isPrivate == e.isPrivate);
  }
};

enum GuardCFLevel {
  Off = 0x0,
  CF = 0x1,      /// Emit gfids tables
  LongJmp = 0x2, /// Emit longjmp tables
  EHCont = 0x4,  /// Emit ehcont tables
  All = 0x7      /// Enable all protections
};

enum class ICFLevel {
  None,
  Safe, // Safe ICF for all sections.
  All,  // Aggressive ICF for code, but safe ICF for data, similar to MSVC's
        // behavior.
};

enum class FormIDHash {
  None,
  Binary,
};

// Global configuration.
struct Configuration {
  enum ManifestKind { Default, SideBySide, Embed, No };
  llvm::COFF::MachineTypes machine = IMAGE_FILE_MACHINE_UNKNOWN;
  static constexpr size_t wordsize = 8;
  bool verbose = false;
  WindowsSubsystem subsystem = llvm::COFF::IMAGE_SUBSYSTEM_UNKNOWN;
  Symbol *entry = nullptr;
  bool noEntry = false;
  std::string outputFile;
  std::string importName;
  bool demangle = true;
  bool doGC = true;
  ICFLevel doICF = ICFLevel::None;
  bool tailMerge;
  bool relocatable = true;
  bool forceMultiple = false;
  bool forceMultipleRes = false;
  bool forceUnresolved = false;
  bool debug = false;
  bool includeDwarfChunks = false;
  bool writeSymtab = false;
  bool driver = false;
  bool driverUponly = false;
  bool driverWdm = false;
  bool showSummary = false;
  bool dontMergeSections = false;
  std::vector<llvm::StringRef> argv;

  // Symbols in this set are considered as live by the garbage collector.
  std::vector<Symbol *> gcroot;

  std::set<std::string> noDefaultLibs;
  bool noDefaultLibAll = false;

  // True if we are creating a DLL.
  bool dll = false;
  StringRef implib;
  bool noimplib = false;
  std::vector<Export> exports;
  bool hadExplicitExports;
  std::set<std::string> delayLoads;
  std::map<std::string, int> dllOrder;
  Symbol *delayLoadHelper = nullptr;

  // /guard:cf
  int guardCF = GuardCFLevel::Off;

  const LinkerDriverConfig *driverCfg = nullptr;

  // Used for /merge:from=to (e.g. /merge:.rdata=.text)
  std::map<StringRef, StringRef> merge;

  // Used for /section=.name,{DEKPRSW} to set section attributes.
  std::map<StringRef, uint32_t> section;

  // Options for manifest files.
  ManifestKind manifest = Default;
  int manifestID = 1;
  llvm::SetVector<StringRef> manifestDependencies;
  bool manifestUAC = true;
  std::vector<std::string> manifestInput;
  StringRef manifestLevel = "'asInvoker'";
  StringRef manifestUIAccess = "'false'";
  StringRef manifestFile;

  // Used for /aligncomm.
  std::map<std::string, int> alignComm;

  // Used for --failifmismatch.
  std::map<StringRef, std::pair<StringRef, InputFile *>> mustMatch;

  // Used for --alternatename.
  std::map<StringRef, StringRef> alternateNames;

  // Used for /order.
  llvm::StringMap<int> order;

  // Used for /map.
  std::string mapFile;

  // Used for /mapinfo.
  bool mapInfo = false;

  // Used for /call-graph-ordering-file:
  llvm::MapVector<std::pair<const SectionChunk *, const SectionChunk *>,
                  uint64_t>
      callGraphProfile;
  bool callGraphProfileSort = false;

  // Used for /print-symbol-order:
  StringRef printSymbolOrder;

  // Used for /vfsoverlay:
  std::unique_ptr<llvm::vfs::FileSystem> vfs;

  uint64_t align = 4096;
  uint64_t imageBase = -1;
  uint64_t fileAlign = 512;
  uint64_t stackReserve = 1024 * 1024;
  uint64_t stackCommit = 4096;
  uint64_t heapReserve = 1024 * 1024;
  uint64_t heapCommit = 4096;
  uint32_t majorImageVersion = 0;
  uint32_t minorImageVersion = 0;
  // Change the value to 4 to support win-xp
  uint32_t majorOSVersion = 4;
  uint32_t minorOSVersion = 0;
  // Change the value to 4 to support win-xp
  uint32_t majorSubsystemVersion = 4;
  uint32_t minorSubsystemVersion = 0;
  uint32_t timestamp = 0;
  uint32_t functionPadMin = 0;
  uint16_t dependentLoadFlags = 0;
  bool dynamicBase = true;
  bool allowBind = true;
  bool cetCompat = false;
  bool nxCompat = true;
  bool allowIsolation = true;
  bool terminalServerAware = true;
  bool largeAddressAware = false;
  bool highEntropyVA = false;
  bool appContainer = false;
  bool warnMissingOrderSymbol = true;
  bool warnLocallyDefinedImported = true;
  bool warnLongSectionNames = true;
  bool incremental = true;
  bool integrityCheck = false;
  bool killAt = false;
  bool repro = false;
  bool swaprunCD = false;
  bool swaprunNet = false;
  bool writeCheckSum = false;
  bool hasCustomBSSSection = false;
  FormIDHash buildIDHash = FormIDHash::None;
};

} // namespace linker::coff

#endif
