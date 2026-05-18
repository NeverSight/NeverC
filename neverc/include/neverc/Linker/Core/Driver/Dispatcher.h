//===----------------------------------------------------------------------===//
//
//  Backend registry shared by `neverc` and every linker backend.
//
//  Each backend publishes its `link()` entry point through the
//  `LINKER_HAS_DRIVER(name)` macro below, and `neverc_main` builds a
//  `linker::DriverDef[]` table keyed by `linker::Flavor` to dispatch to
//  the matching backend.  There is no progname-based dispatch: `neverc`
//  is the single executable entry point and selects a flavor from the
//  driver pipeline (`LinkerCommand::Flavor`).
//
//===----------------------------------------------------------------------===//

#ifndef LINKER_CORE_DRIVER_DISPATCHER_H
#define LINKER_CORE_DRIVER_DISPATCHER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/raw_ostream.h"
#include <string>
#include <vector>

namespace linker {

enum class Flavor : unsigned {
  Invalid = 0,
  Gnu = 1,     // ELF
  WinLink = 2, // COFF
  Darwin = 3,  // Mach-O
};

/// Driver-level settings forwarded to the embedded linker so that the
/// linker does not need to re-parse them from the command line.  The
/// neverc driver populates this struct and passes it through to
/// link(); backends read it during initialization instead of parsing
/// the equivalent standalone linker options.
struct LinkerDriverConfig {
  bool saveTemps = false;
  bool timeTraceEnabled = false;
  unsigned timeTraceGranularity = 500;
  uint64_t errorLimit = 20;
  bool verbose = false;
  unsigned threadCount = 0; // 0 = use hardware default
  bool demangle = true;
  bool nostdlib = false;
  bool suppressWarnings = false;
  bool fatalWarnings = false;
  std::string sysroot;
  std::string compressDebugSections; // "none", "zlib", "zstd", or ""
  std::string hashStyle; // "sysv", "gnu", "both", or "" (backend default)

  // Linker output optimization level (controls ICF aggressiveness, section
  // merge, string dedup etc.).  Derived from the compiler's -O flag.
  // -1 means "not set"; backends fall back to their own default (usually 1).
  int linkerOptLevel = -1;

  // Target CPU for LTO codegen — set directly from the driver's -mcpu /
  // -march resolution so the linker doesn't need to re-parse it.
  std::string cpu;

  // LTO settings computed by the driver from -O / -flto.
  // -1 means "not set"; backends fall back to their own defaults.
  int ltoOptLevel = -1;
  int ltoCGOLevel = -1; // codegen opt level; -1 = derive from ltoOptLevel

  // -fbasic-block-sections= forwarded to LTO codegen.
  std::string ltoBasicBlockSections;
  bool ltoUniqueBasicBlockSectionNames = false;

  // Codegen options forwarded directly to TargetOptions — avoids the
  // string-based cl::opt round-trip through mllvmOpts.
  int globalISel = -1;     // -1=not set, 0=off, 1=on
  int debuggerTuning = -1; // -1=not set, 0=default, 1=gdb, 2=lldb
  bool splitMachineFunctions = false;
  bool jmcInstrument = false;
  bool emulatedTLS = false;
  bool stackSizeSection = false;

  // Remaining LLVM options that have no direct TargetOptions mapping
  // and require cl::opt parsing in the LTO backend.
  std::vector<std::string> mllvmOpts;

  // Optimization-remarks settings for LTO link.
  std::string optRemarksFilename;
  std::string optRemarksPasses;
  std::string optRemarksFormat;
  bool optRemarksWithHotness = false;
  std::string optRemarksHotnessThreshold;

  // LTO pass-plugin libraries.
  std::vector<std::string> passPlugins;

  // Linker-level options now controlled by the neverc driver.
  // Backends use these as defaults; explicit -Wl, overrides still apply.
  bool gcSections = false; // --gc-sections / -dead_strip / /opt:ref
  bool ehFrameHdr = true;  // --eh-frame-hdr (ELF only, default on)
  int icfLevel = 0;        // 0=none, 1=safe, 2=all
  std::string buildId;     // empty=none, "fast", "sha1", etc.
  int stripLevel = 0;      // 0=none, 1=debug-only, 2=all
  bool stripLocals =
      false; // strip local symbols (MachO: -x, ELF: --discard-all)

  // Output file path, set by the driver from neverc -o.
  std::string outputFile;

  // Output type — set by the driver ToolChain, replaces per-backend
  // --shared / -dylib / /dll, --pie / -pie, --relocatable / -r.
  bool shared = false;      // ELF --shared, MachO -dylib, COFF /dll
  bool bundle = false;      // MachO -bundle
  bool pie = false;         // ELF --pie, MachO -pie
  bool relocatable = false; // ELF -r / --relocatable
  bool staticLink = false;  // MachO -static (no dynamic libraries)

  // Dynamic linker path (ELF only), replaces --dynamic-linker.
  std::string dynamicLinker;
  bool noDynamicLinker = false; // --no-dynamic-linker (inhibit .interp)

  // ELF emulation string (e.g. "elf_x86_64"), replaces -m.
  std::string emulation;

  // Endianness override (ELF only), replaces -EL/-EB.
  // 0 = not set (infer from emulation/inputs), 1 = little, 2 = big.
  int endianness = 0;

  // Export all dynamic symbols, replaces --export-dynamic / -rdynamic.
  bool exportDynamic = false;

  // COFF-specific options unified from the linker backend.
  bool debugInfo = false;      // /debug:dwarf (DWARF sections in PE)
  bool repro = false;          // /Brepro (deterministic timestamp)
  bool functionPadMin = false; // /functionpadmin (hotpatching)
  std::string guardSpec;       // /guard:... (e.g. "cf", "cf,ehcont")

  // MachO-specific options unified from the linker backend.
  std::string archName;           // -arch (e.g. "x86_64", "arm64")
  std::string platformName;       // -platform_version <platform>
  std::string platformMinVersion; // -platform_version <min_version>
  std::string platformSdkVersion; // -platform_version <sdk_version>

  // Link map and diagnostic output (replaces --Map/-map//map and
  // --print-gc-sections/--print-icf-sections in per-backend Options.td).
  std::string mapFile;           // empty=none, "-"=stdout, else file path
  bool printGCSections = false;  // --print-gc-sections
  bool printICFSections = false; // --print-icf-sections
  bool traceFiles = false;       // --trace/-t (print loaded input files)

  // Set to 2 by the driver to enable ParallelCodeGenHook /
  // ParallelOptCodeGenHook. The hooks auto-detect the actual partition count
  // from hardware_concurrency().
  unsigned ltoPartitions = 0;

  // Call-graph profile-guided section reordering (unified from per-backend
  // Options.td: --call-graph-profile-sort, /call-graph-profile-sort).
  // Values: "cdsort" (default), "hfsort", "none" (=disabled).
  std::string callGraphProfileSort = "cdsort";

  // Call-graph ordering file for section layout optimization.
  // Replaces per-backend --call-graph-ordering-file /
  // /call-graph-ordering-file.
  std::string callGraphOrderingFile;

  // Diagnostic output: print symbol order produced by call-graph profile sort.
  // Replaces per-backend --print-symbol-order / /print-symbol-order.
  std::string printSymbolOrder;
};

using Driver = bool (*)(llvm::ArrayRef<const char *>, llvm::raw_ostream &,
                        llvm::raw_ostream &, bool, bool,
                        const LinkerDriverConfig &);

struct DriverDef {
  Flavor f;
  Driver d;
};

} // namespace linker

// Every backend publishes its `link()` entry point through this macro so
// `neverc_main` can look it up from a `DriverDef[]` table built in
// `neverc/main.cpp`.
#define LINKER_HAS_DRIVER(name)                                                \
  namespace linker {                                                           \
  namespace name {                                                             \
  bool link(llvm::ArrayRef<const char *> args, llvm::raw_ostream &stdoutOS,    \
            llvm::raw_ostream &stderrOS, bool exitEarly, bool disableOutput,   \
            const LinkerDriverConfig &driverCfg);                              \
  }                                                                            \
  }

#endif // LINKER_CORE_DRIVER_DISPATCHER_H
