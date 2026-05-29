#include "Linker/Core/Driver/ArgList.h"
#include "Linker/Core/Driver/CommonLTOConfig.h"
#include "Linker/Core/Driver/Dispatcher.h"
#include "Linker/Core/Runtime/Allocator.h"
#include "Linker/Core/Runtime/Diagnostic.h"
#include "Linker/Core/Runtime/Session.h"
#include "Linker/Core/Support/FileIO.h"
#include "Linker/Core/Support/Strings.h"
#include "Linker/ELF/Config.h"
#include "Linker/ELF/Driver.h"
#include "Linker/ELF/Emit.h"
#include "Linker/ELF/ICF.h"
#include "Linker/ELF/InputFiles.h"
#include "Linker/ELF/InputSection.h"
#include "Linker/ELF/LTO.h"
#include "Linker/ELF/LinkerScript.h"
#include "Linker/ELF/MarkLive.h"
#include "Linker/ELF/OutputSections.h"
#include "Linker/ELF/ScriptParser.h"
#include "Linker/ELF/SymbolTable.h"
#include "Linker/ELF/Symbols.h"
#include "Linker/ELF/SyntheticSections.h"
#include "Linker/ELF/Target.h"
#include "neverc/Merge/Merger.h"
#include "neverc/Plugin/PluginLoader.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/LTO/LTO.h"
#include "llvm/Object/Archive.h"
#include "llvm/Support/Compression.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/GlobPattern.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/Parallel.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Support/raw_ostream.h"
#include <tuple>
#include <utility>

using namespace llvm;
using namespace llvm::ELF;
using namespace llvm::object;
using namespace llvm::sys;
using namespace llvm::support;
using namespace linker;
using namespace linker::elf;

ConfigWrapper elf::config;
Ctx elf::ctx;

// ===----------------------------------------------------------------------===
// Initialization & link entry point
// ===----------------------------------------------------------------------===

namespace {
void setConfigs(opt::InputArgList &args);
} // namespace
namespace {
void readConfigs(opt::InputArgList &args, const LinkerDriverConfig &driverCfg);
} // namespace

void elf::errorOrWarn(const Twine &msg) {
  if (config->noinhibitExec)
    warn(msg);
  else
    error(msg);
}

void Ctx::reset() {
  driver = LinkerDriver();
  memoryBuffers.clear();
  objectFiles.clear();
  sharedFiles.clear();
  binaryFiles.clear();
  bitcodeFiles.clear();
  lazyBitcodeFiles.clear();
  inputSections.clear();
  ehInputSections.clear();
  duplicates.clear();
  nonPrevailingSyms.clear();
  whyExtractRecords.clear();
  backwardReferences.clear();
  auxiliaryFiles.clear();
  hasSympart.store(false, std::memory_order_relaxed);
  hasTlsIe.store(false, std::memory_order_relaxed);
  needsTlsLd.store(false, std::memory_order_relaxed);
  scriptSymOrderCounter = 1;
  scriptSymOrder.clear();
  overrideSymbols.clear();
}

llvm::raw_fd_ostream Ctx::openAuxiliaryFile(llvm::StringRef filename,
                                            std::error_code &ec) {
  using namespace llvm::sys::fs;
  OpenFlags flags =
      auxiliaryFiles.insert(filename).second ? OF_None : OF_Append;
  return {filename, ec, flags};
}

namespace linker {
namespace elf {
bool link(ArrayRef<const char *> args, llvm::raw_ostream &stdoutOS,
          llvm::raw_ostream &stderrOS, bool exitEarly, bool disableOutput,
          const LinkerDriverConfig &driverCfg) {
  // Freed by CommonLinkerContext::destroy() after link() returns.
  auto *ctx = new CommonLinkerContext;

  ctx->e.initialize(stdoutOS, stderrOS, exitEarly, disableOutput);
  ctx->e.errorLimit = driverCfg.errorLimit;
  ctx->e.cleanupCallback = []() {
    elf::ctx.reset();
    symtab = SymbolTable();

    outputSections.clear();
    symAux.clear();

    in.reset();

    partitions.clear();
    partitions.emplace_back();

    SharedFile::vernauxNum = 0;
  };
  ctx->e.logName = args::getFilenameWithoutExe(args[0]);
  ctx->e.errorLimitExceededMsg = "too many errors emitted, stopping now (use "
                                 "-ferror-limit=0 to see all errors)";

  config = ConfigWrapper();
  script = std::make_unique<LinkerScript>();

  symAux.emplace_back();

  partitions.clear();
  partitions.emplace_back();

  config->progName = args[0];

  // Load neverc out-of-tree C-ABI plugins so their linker passes can fire at
  // the LINK_* hook points during image emission.  Idempotent with the LTO
  // path (loadPlugin dedups by path); doing it here also covers pure
  // object-file links that never build an LTO config.
  for (const std::string &Path : driverCfg.nevercPluginPaths) {
    std::string Err;
    if (!neverc::plugin::getGlobalPluginLoader().loadPlugin(Path, Err))
      error(Err);
  }

  elf::ctx.driver.run(args, driverCfg);

  return errorCount() == 0;
}
} // namespace elf
} // namespace linker

// Parses a linker -m option.
namespace {
std::tuple<ELFKind, uint16_t, uint8_t> parseEmulation(StringRef emul) {
  uint8_t osabi = 0;
  StringRef s = emul;
  if (s.ends_with("_fbsd")) {
    s = s.drop_back(5);
    osabi = ELFOSABI_FREEBSD;
  }

  std::pair<ELFKind, uint16_t> ret =
      StringSwitch<std::pair<ELFKind, uint16_t>>(s)
          .Cases("aarch64elf", "aarch64linux", {ELF64LEKind, EM_AARCH64})
          .Cases("elf_amd64", "elf_x86_64", {ELF64LEKind, EM_X86_64})
          .Default({ELFNoneKind, EM_NONE});

  if (ret.first == ELFNoneKind)
    error("unknown emulation: " + emul);
  return std::make_tuple(ret.first, ret.second, osabi);
}
} // namespace

// Returns slices of MB by parsing MB as an archive file.
// Each slice consists of a member file in the archive.
std::vector<std::pair<MemoryBufferRef, uint64_t>> static getArchiveMembers(
    MemoryBufferRef mb) {
  std::unique_ptr<Archive> file =
      CHECK(Archive::create(mb),
            mb.getBufferIdentifier() + ": failed to parse archive");

  std::vector<std::pair<MemoryBufferRef, uint64_t>> v;
  Error err = Error::success();
  for (const Archive::Child &c : file->children(err)) {
    MemoryBufferRef mbref =
        CHECK(c.getMemoryBufferRef(),
              mb.getBufferIdentifier() +
                  ": could not get the buffer for a child of the archive");
    v.push_back(std::make_pair(mbref, c.getChildOffset()));
  }
  if (err)
    fatal(mb.getBufferIdentifier() +
          ": Archive::children failed: " + toString(std::move(err)));

  // Take ownership of memory buffers created for members of thin archives.
  std::vector<std::unique_ptr<MemoryBuffer>> mbs = file->takeThinBuffers();
  std::move(mbs.begin(), mbs.end(), std::back_inserter(ctx.memoryBuffers));

  return v;
}

namespace {
bool isBitcode(MemoryBufferRef mb) {
  return identify_magic(mb.getBuffer()) == llvm::file_magic::bitcode;
}
} // namespace

// Opens a file and create a file object. Path has to be resolved already.
// ===----------------------------------------------------------------------===
// File & archive input
// ===----------------------------------------------------------------------===

void LinkerDriver::addFile(StringRef path, bool withLOption) {
  using namespace sys::fs;

  std::optional<MemoryBufferRef> buffer = readFile(path);
  if (!buffer)
    return;
  MemoryBufferRef mbref = *buffer;

  if (config->formatBinary) {
    files.push_back(make<BinaryFile>(mbref));
    return;
  }

  switch (identify_magic(mbref.getBuffer())) {
  case file_magic::unknown:
    readLinkerScript(mbref);
    return;
  case file_magic::archive: {
    auto members = getArchiveMembers(mbref);
    if (inWholeArchive) {
      for (const std::pair<MemoryBufferRef, uint64_t> &p : members) {
        if (isBitcode(p.first))
          files.push_back(make<BitcodeFile>(p.first, path, p.second, false));
        else
          files.push_back(createObjFile(p.first, path));
      }
      return;
    }

    archiveFiles.emplace_back(path, members.size());

    // Handle archives and --start-lib/--end-lib using the same code path. This
    // scans all the ELF relocatable object files and bitcode files in the
    // archive rather than just the index file, with the benefit that the
    // symbols are only loaded once. For many projects archives see high
    // utilization rates and it is a net performance win. --start-lib scans
    // symbols in the same order that llvm-ar adds them to the index, so in the
    // common case the semantics are identical. If the archive symbol table was
    // created in a different order, or is incomplete, this strategy has
    // different semantics. Such output differences are considered user error.
    //
    // All files within the archive get the same group ID to allow mutual
    // references for --warn-backrefs.
    bool saved = InputFile::isInGroup;
    InputFile::isInGroup = true;
    for (const std::pair<MemoryBufferRef, uint64_t> &p : members) {
      auto magic = identify_magic(p.first.getBuffer());
      if (magic == file_magic::elf_relocatable) {
        files.push_back(createObjFile(p.first, path, true));
      } else if (magic == file_magic::bitcode)
        files.push_back(make<BitcodeFile>(p.first, path, p.second, true));
      else
        warn(path + ": archive member '" + p.first.getBufferIdentifier() +
             "' is neither ET_REL nor LLVM bitcode");
    }
    InputFile::isInGroup = saved;
    if (!saved)
      ++InputFile::nextGroupId;
    return;
  }
  case file_magic::elf_shared_object: {
    if (config->isStatic || config->relocatable) {
      error("attempted static link of dynamic object " + path);
      return;
    }

    // Shared objects are identified by soname. soname is (if specified)
    // DT_SONAME and falls back to filename. If a file was specified by -lfoo,
    // the directory part is ignored. Note that path may be a temporary and
    // cannot be stored into SharedFile::soName.
    path = mbref.getBufferIdentifier();
    auto *f =
        make<SharedFile>(mbref, withLOption ? path::filename(path) : path);
    f->init();
    files.push_back(f);
    return;
  }
  case file_magic::bitcode:
    files.push_back(make<BitcodeFile>(mbref, "", 0, inLib));
    break;
  case file_magic::elf_relocatable:
    files.push_back(createObjFile(mbref, "", inLib));
    break;
  default:
    error(path + ": unknown file type");
  }
}

// Add a given library by searching it from input search paths.
void LinkerDriver::addLibrary(StringRef name) {
  if (std::optional<std::string> path = searchLibrary(name))
    addFile(saver().save(*path), /*withLOption=*/true);
  else
    error("unable to find library -l" + name, ErrorTag::LibNotFound, {name});
}

namespace {
void initLLVM() {
  InitializeAllTargets();
  InitializeAllTargetMCs();
  InitializeAllAsmPrinters();
  InitializeAllAsmParsers();
}
} // namespace

namespace {
void checkOptions() {
  if (config->emachine != EM_AARCH64 && config->emachine != EM_X86_64)
    error("unsupported target architecture");

  if (config->fixCortexA53Errata843419 && config->emachine != EM_AARCH64)
    error("--fix-cortex-a53-843419 is only supported on AArch64 targets");

  if (config->pie && config->shared)
    error("-shared and -pie may not be used together");

  if (!config->shared && !config->filterList.empty())
    error("-F may not be used without -shared");

  if (!config->shared && !config->auxiliaryList.empty())
    error("-f may not be used without -shared");

  if (config->strip == StripPolicy::All && config->emitRelocs)
    error("--strip-all and --emit-relocs may not be used together");

  if (config->zText && config->zIfuncNoplt)
    error("-z text and -z ifunc-noplt may not be used together");

  if (config->relocatable) {
    if (config->shared)
      error("-r and -shared may not be used together");
    if (config->gdbIndex)
      error("-r and --gdb-index may not be used together");
    if (config->icf != ICFLevel::None)
      error("-r and --icf may not be used together");
    if (config->pie)
      error("-r and -pie may not be used together");
    if (config->exportDynamic)
      error("-r and --export-dynamic may not be used together");
  }

  if (config->executeOnly) {
    if (config->emachine != EM_AARCH64)
      error("--execute-only is only supported on AArch64 targets");

    if (config->singleRoRx && !script->hasSectionsCommand)
      error("--execute-only and --no-rosegment cannot be used together");
  }

  if (config->zRetpolineplt && config->zForceIbt)
    error("-z force-ibt may not be used with -z retpolineplt");

  if (config->emachine != EM_AARCH64) {
    if (config->zPacPlt)
      error("-z pac-plt only supported on AArch64");
    if (config->zForceBti)
      error("-z force-bti only supported on AArch64");
    if (config->zBtiReport != "none")
      error("-z bti-report only supported on AArch64");
  }

  if (config->emachine != EM_X86_64 && config->zCetReport != "none")
    error("-z cet-report only supported on X86_64");
}
} // namespace

namespace {
bool hasZOption(opt::InputArgList &args, StringRef key) {
  for (auto *arg : args.filtered(OPT_z))
    if (key == arg->getValue())
      return true;
  return false;
}
} // namespace

namespace {
bool getZFlag(opt::InputArgList &args, StringRef k1, StringRef k2,
              bool Default) {
  for (auto *arg : args.filtered_reverse(OPT_z)) {
    if (k1 == arg->getValue())
      return true;
    if (k2 == arg->getValue())
      return false;
  }
  return Default;
}
} // namespace

namespace {
SeparateSegmentKind getZSeparate(opt::InputArgList &args) {
  for (auto *arg : args.filtered_reverse(OPT_z)) {
    StringRef v = arg->getValue();
    if (v == "noseparate-code")
      return SeparateSegmentKind::None;
    if (v == "separate-code")
      return SeparateSegmentKind::Code;
    if (v == "separate-loadable-segments")
      return SeparateSegmentKind::Loadable;
  }
  return SeparateSegmentKind::None;
}
} // namespace

namespace {
GnuStackKind getZGnuStack(opt::InputArgList &args) {
  for (auto *arg : args.filtered_reverse(OPT_z)) {
    if (StringRef("execstack") == arg->getValue())
      return GnuStackKind::Exec;
    if (StringRef("noexecstack") == arg->getValue())
      return GnuStackKind::NoExec;
    if (StringRef("nognustack") == arg->getValue())
      return GnuStackKind::None;
  }

  return GnuStackKind::NoExec;
}
} // namespace

namespace {
uint8_t getZStartStopVisibility(opt::InputArgList &args) {
  for (auto *arg : args.filtered_reverse(OPT_z)) {
    std::pair<StringRef, StringRef> kv = StringRef(arg->getValue()).split('=');
    if (kv.first == "start-stop-visibility") {
      if (kv.second == "default")
        return STV_DEFAULT;
      else if (kv.second == "internal")
        return STV_INTERNAL;
      else if (kv.second == "hidden")
        return STV_HIDDEN;
      else if (kv.second == "protected")
        return STV_PROTECTED;
      error("unknown -z start-stop-visibility= value: " + StringRef(kv.second));
    }
  }
  return STV_PROTECTED;
}
} // namespace

constexpr const char *knownZFlags[] = {
    "combreloc",
    "copyreloc",
    "defs",
    "execstack",
    "force-bti",
    "force-ibt",
    "global",
    "ifunc-noplt",
    "initfirst",
    "interpose",
    "keep-text-section-prefix",
    "lazy",
    "muldefs",
    "nocombreloc",
    "nocopyreloc",
    "nodefaultlib",
    "nodelete",
    "nodlopen",
    "noexecstack",
    "nognustack",
    "nokeep-text-section-prefix",
    "nopack-relative-relocs",
    "norelro",
    "noseparate-code",
    "nostart-stop-gc",
    "notext",
    "now",
    "origin",
    "pac-plt",
    "pack-relative-relocs",
    "rel",
    "rela",
    "relro",
    "retpolineplt",
    "rodynamic",
    "separate-code",
    "separate-loadable-segments",
    "shstk",
    "start-stop-gc",
    "text",
    "undefs",
    "wxneeded",
};

namespace {
bool isKnownZFlag(StringRef s) {
  return llvm::is_contained(knownZFlags, s) ||
         s.starts_with("common-page-size=") || s.starts_with("bti-report=") ||
         s.starts_with("cet-report=") ||
         s.starts_with("dead-reloc-in-nonalloc=") ||
         s.starts_with("max-page-size=") || s.starts_with("stack-size=") ||
         s.starts_with("start-stop-visibility=");
}
} // namespace

// Report a warning for an unknown -z option.
namespace {
void checkZOptions(opt::InputArgList &args) {
  for (auto *arg : args.filtered(OPT_z))
    if (!isKnownZFlag(arg->getValue()))
      warn("unknown -z value: " + StringRef(arg->getValue()));
}
} // namespace

// ===----------------------------------------------------------------------===
// ===----------------------------------------------------------------------===
// Top-level entry: option parsing & pipeline dispatch
// ===----------------------------------------------------------------------===

void LinkerDriver::run(ArrayRef<const char *> argsArr,
                       const LinkerDriverConfig &driverCfg) {
  ELFOptTable parser;
  opt::InputArgList args = parser.parse(argsArr.slice(1));

  errorHandler().fatalWarnings = driverCfg.fatalWarnings;
  errorHandler().suppressWarnings = driverCfg.suppressWarnings;
  checkZOptions(args);

  readConfigs(args, driverCfg);

  if (config->driverCfg->timeTraceEnabled)
    timeTraceProfilerInitialize(config->driverCfg->timeTraceGranularity,
                                config->progName);

  {
    llvm::TimeTraceScope timeScope("ExecuteLinker");

    initLLVM();
    createFiles(args);
    if (errorCount())
      return;

    inferMachineType();
    setConfigs(args);
    checkOptions();
    if (errorCount())
      return;

    execute(args);
  }

  if (config->driverCfg->timeTraceEnabled) {
    checkError(timeTraceProfilerWrite(std::string(), config->outputFile));
    timeTraceProfilerCleanup();
  }
}

namespace {
std::string getRpath(opt::InputArgList &args) {
  SmallVector<StringRef, 0> v = args::getStrings(args, OPT_rpath);
  return llvm::join(v.begin(), v.end(), ":");
}
} // namespace

// Determines what we should do if there are remaining unresolved
// symbols after the name resolution.
namespace {
void setUnresolvedSymbolPolicy(opt::InputArgList &args) {
  UnresolvedPolicy errorOrWarn = args.hasFlag(OPT_error_unresolved_symbols,
                                              OPT_warn_unresolved_symbols, true)
                                     ? UnresolvedPolicy::ReportError
                                     : UnresolvedPolicy::Warn;
  // -shared implies --unresolved-symbols=ignore-all because missing
  // symbols are likely to be resolved at runtime.
  bool diagRegular = !config->shared, diagShlib = !config->shared;

  for (const opt::Arg *arg : args) {
    switch (arg->getOption().getID()) {
    case OPT_unresolved_symbols: {
      StringRef s = arg->getValue();
      if (s == "ignore-all") {
        diagRegular = false;
        diagShlib = false;
      } else if (s == "ignore-in-object-files") {
        diagRegular = false;
        diagShlib = true;
      } else if (s == "ignore-in-shared-libs") {
        diagRegular = true;
        diagShlib = false;
      } else if (s == "report-all") {
        diagRegular = true;
        diagShlib = true;
      } else {
        error("unknown --unresolved-symbols value: " + s);
      }
      break;
    }
    case OPT_no_undefined:
      diagRegular = true;
      break;
    case OPT_z:
      if (StringRef(arg->getValue()) == "defs")
        diagRegular = true;
      else if (StringRef(arg->getValue()) == "undefs")
        diagRegular = false;
      break;
    case OPT_allow_shlib_undefined:
      diagShlib = false;
      break;
    case OPT_no_allow_shlib_undefined:
      diagShlib = true;
      break;
    }
  }

  config->unresolvedSymbols =
      diagRegular ? errorOrWarn : UnresolvedPolicy::Ignore;
  config->unresolvedSymbolsInShlib =
      diagShlib ? errorOrWarn : UnresolvedPolicy::Ignore;
}
} // namespace

namespace {
bool isOutputFormatBinary(opt::InputArgList &args) {
  StringRef s = args.getLastArgValue(OPT_oformat, "elf");
  if (s == "binary")
    return true;
  if (!s.starts_with("elf"))
    error("unknown --oformat value: " + s);
  return false;
}
} // namespace

namespace {
DiscardPolicy getDiscard(opt::InputArgList &args) {
  auto *arg =
      args.getLastArg(OPT_discard_all, OPT_discard_locals, OPT_discard_none);
  if (!arg)
    return DiscardPolicy::Default;
  if (arg->getOption().getID() == OPT_discard_all)
    return DiscardPolicy::All;
  if (arg->getOption().getID() == OPT_discard_locals)
    return DiscardPolicy::Locals;
  return DiscardPolicy::None;
}
} // namespace

namespace {
int getMemtagMode(opt::InputArgList &args) {
  StringRef memtagModeArg = args.getLastArgValue(OPT_android_memtag_mode);
  if (memtagModeArg.empty()) {
    if (config->androidMemtagStack)
      warn("--android-memtag-mode is unspecified, leaving "
           "--android-memtag-stack a no-op");
    else if (config->androidMemtagHeap)
      warn("--android-memtag-mode is unspecified, leaving "
           "--android-memtag-heap a no-op");
    return ELF::NT_MEMTAG_LEVEL_NONE;
  }

  if (memtagModeArg == "sync")
    return ELF::NT_MEMTAG_LEVEL_SYNC;
  if (memtagModeArg == "async")
    return ELF::NT_MEMTAG_LEVEL_ASYNC;
  if (memtagModeArg == "none")
    return ELF::NT_MEMTAG_LEVEL_NONE;

  error("unknown --android-memtag-mode value: \"" + memtagModeArg +
        "\", should be one of {async, sync, none}");
  return ELF::NT_MEMTAG_LEVEL_NONE;
}
} // namespace

namespace {
ICFLevel getICFFromDriver(int driverLevel) {
  if (driverLevel >= 2)
    return ICFLevel::All;
  if (driverLevel == 1)
    return ICFLevel::Safe;
  return ICFLevel::None;
}
} // namespace

namespace {
StripPolicy getStripFromDriver(const LinkerDriverConfig &driverCfg) {
  if (driverCfg.relocatable)
    return StripPolicy::None;
  if (driverCfg.stripLevel >= 2)
    return StripPolicy::All;
  if (driverCfg.stripLevel == 1)
    return StripPolicy::Debug;
  return StripPolicy::None;
}
} // namespace

namespace {
uint64_t parseSectionAddress(StringRef s, opt::InputArgList &args,
                             const opt::Arg &arg) {
  uint64_t va = 0;
  if (s.starts_with("0x"))
    s = s.drop_front(2);
  if (!to_integer(s, va, 16))
    error("invalid argument: " + arg.getAsString(args));
  return va;
}
} // namespace

namespace {
StringMap<uint64_t> getSectionStartMap(opt::InputArgList &args) {
  StringMap<uint64_t> ret;
  for (auto *arg : args.filtered(OPT_section_start)) {
    StringRef name;
    StringRef addr;
    std::tie(name, addr) = StringRef(arg->getValue()).split('=');
    ret[name] = parseSectionAddress(addr, args, *arg);
  }

  if (auto *arg = args.getLastArg(OPT_Ttext))
    ret[".text"] = parseSectionAddress(arg->getValue(), args, *arg);
  if (auto *arg = args.getLastArg(OPT_Tdata))
    ret[".data"] = parseSectionAddress(arg->getValue(), args, *arg);
  if (auto *arg = args.getLastArg(OPT_Tbss))
    ret[".bss"] = parseSectionAddress(arg->getValue(), args, *arg);
  return ret;
}
} // namespace

namespace {
SortSectionPolicy getSortSection(opt::InputArgList &args) {
  StringRef s = args.getLastArgValue(OPT_sort_section);
  if (s == "alignment")
    return SortSectionPolicy::Alignment;
  if (s == "name")
    return SortSectionPolicy::Name;
  if (!s.empty())
    error("unknown --sort-section rule: " + s);
  return SortSectionPolicy::Default;
}
} // namespace

namespace {
OrphanHandlingPolicy getOrphanHandling(opt::InputArgList &args) {
  StringRef s = args.getLastArgValue(OPT_orphan_handling, "place");
  if (s == "warn")
    return OrphanHandlingPolicy::Warn;
  if (s == "error")
    return OrphanHandlingPolicy::Error;
  if (s != "place")
    error("unknown --orphan-handling mode: " + s);
  return OrphanHandlingPolicy::Place;
}
} // namespace

// Parse build-id style from LinkerDriverConfig. "tree" is accepted as a
// synonym for "sha1" since all hashes are tree hashes for performance.
namespace {
std::pair<BuildIdStyle, SmallVector<uint8_t, 0>>
getBuildIdFromDriver(const std::string &style) {
  if (style.empty() || style == "none")
    return {BuildIdStyle::None, {}};
  StringRef s = style;
  if (s == "fast")
    return {BuildIdStyle::Fast, {}};
  if (s == "md5")
    return {BuildIdStyle::Md5, {}};
  if (s == "sha1" || s == "tree")
    return {BuildIdStyle::Sha1, {}};
  if (s == "uuid")
    return {BuildIdStyle::Uuid, {}};
  if (s.starts_with("0x"))
    return {BuildIdStyle::Hexstring, parseHex(s.substr(2))};

  error("unknown build-id style: " + s);
  return {BuildIdStyle::None, {}};
}
} // namespace

namespace {
std::pair<bool, bool> getPackDynRelocs(opt::InputArgList &args) {
  StringRef s = args.getLastArgValue(OPT_pack_dyn_relocs, "none");
  if (s == "android")
    return {true, false};
  if (s == "relr")
    return {false, true};
  if (s == "android+relr")
    return {true, true};

  if (s != "none")
    error("unknown --pack-dyn-relocs format: " + s);
  return {false, false};
}
} // namespace

namespace {
void readCallGraph(MemoryBufferRef mb) {
  // Build a map from symbol name to section
  DenseMap<StringRef, Symbol *> map;
  for (ELFFileBase *file : ctx.objectFiles)
    for (Symbol *sym : file->getSymbols())
      map[sym->getName()] = sym;

  auto findSection = [&](StringRef name) -> InputSectionBase * {
    Symbol *sym = map.lookup(name);
    if (!sym) {
      if (config->warnSymbolOrdering)
        warn(mb.getBufferIdentifier() + ": no such symbol: " + name);
      return nullptr;
    }
    maybeWarnUnorderableSymbol(sym);

    if (Defined *dr = dyn_cast_or_null<Defined>(sym))
      return dyn_cast_or_null<InputSectionBase>(dr->section);
    return nullptr;
  };

  for (StringRef line : args::getLines(mb)) {
    SmallVector<StringRef, 3> fields;
    line.split(fields, ' ');
    uint64_t count;

    if (fields.size() != 3 || !to_integer(fields[2], count)) {
      error(mb.getBufferIdentifier() + ": parse error");
      return;
    }

    if (InputSectionBase *from = findSection(fields[0]))
      if (InputSectionBase *to = findSection(fields[1]))
        config->callGraphProfile[std::make_pair(from, to)] += count;
  }
}
} // namespace

// If SHT_LLVM_CALL_GRAPH_PROFILE and its relocation section exist, returns
// true and populates cgProfile and symbolIndices.
namespace {
template <class ELFT>
bool processCallGraphRelocations(SmallVector<uint32_t, 32> &symbolIndices,
                                 ArrayRef<typename ELFT::CGProfile> &cgProfile,
                                 ObjFile<ELFT> *inputObj) {
  if (inputObj->cgProfileSectionIndex == SHN_UNDEF)
    return false;

  ArrayRef<Elf_Shdr_Impl<ELFT>> objSections =
      inputObj->template getELFShdrs<ELFT>();
  symbolIndices.clear();
  const ELFFile<ELFT> &obj = inputObj->getObj();
  cgProfile =
      check(obj.template getSectionContentsAsArray<typename ELFT::CGProfile>(
          objSections[inputObj->cgProfileSectionIndex]));

  for (size_t i = 0, e = objSections.size(); i < e; ++i) {
    const Elf_Shdr_Impl<ELFT> &sec = objSections[i];
    if (sec.sh_info == inputObj->cgProfileSectionIndex) {
      if (sec.sh_type == SHT_RELA) {
        ArrayRef<typename ELFT::Rela> relas =
            CHECK(obj.relas(sec), "could not retrieve cg profile rela section");
        for (const typename ELFT::Rela &rel : relas)
          symbolIndices.push_back(rel.getSymbol());
        break;
      }
      if (sec.sh_type == SHT_REL) {
        ArrayRef<typename ELFT::Rel> rels =
            CHECK(obj.rels(sec), "could not retrieve cg profile rel section");
        for (const typename ELFT::Rel &rel : rels)
          symbolIndices.push_back(rel.getSymbol());
        break;
      }
    }
  }
  if (symbolIndices.empty())
    warn("SHT_LLVM_CALL_GRAPH_PROFILE exists, but relocation section doesn't");
  return !symbolIndices.empty();
}
} // namespace

template <class ELFT> static void readCallGraphsFromObjectFiles() {
  SmallVector<uint32_t, 32> symbolIndices;
  ArrayRef<typename ELFT::CGProfile> cgProfile;
  for (auto file : ctx.objectFiles) {
    auto *obj = cast<ObjFile<ELFT>>(file);
    if (!processCallGraphRelocations(symbolIndices, cgProfile, obj))
      continue;

    if (symbolIndices.size() != cgProfile.size() * 2)
      fatal("number of relocations doesn't match Weights");

    for (uint32_t i = 0, size = cgProfile.size(); i < size; ++i) {
      const Elf_CGProfile_Impl<ELFT> &cgpe = cgProfile[i];
      uint32_t fromIndex = symbolIndices[i * 2];
      uint32_t toIndex = symbolIndices[i * 2 + 1];
      auto *fromSym = dyn_cast<Defined>(&obj->getSymbol(fromIndex));
      auto *toSym = dyn_cast<Defined>(&obj->getSymbol(toIndex));
      if (!fromSym || !toSym)
        continue;

      auto *from = dyn_cast_or_null<InputSectionBase>(fromSym->section);
      auto *to = dyn_cast_or_null<InputSectionBase>(toSym->section);
      if (from && to)
        config->callGraphProfile[{from, to}] += cgpe.cgp_weight;
    }
  }
}

namespace {
CGProfileSortKind getCGProfileSortKind(StringRef s) {
  if (s == "hfsort")
    return CGProfileSortKind::Hfsort;
  if (s == "cdsort")
    return CGProfileSortKind::Cdsort;
  if (s != "none")
    error("unknown call-graph-profile-sort value: " + s);
  return CGProfileSortKind::None;
}
} // namespace

namespace {
DebugCompressionType getCompressionType(StringRef s, StringRef option) {
  DebugCompressionType type = StringSwitch<DebugCompressionType>(s)
                                  .Case("zlib", DebugCompressionType::Zlib)
                                  .Case("zstd", DebugCompressionType::Zstd)
                                  .Default(DebugCompressionType::None);
  if (type == DebugCompressionType::None) {
    if (s != "none")
      error("unknown " + option + " value: " + s);
  } else if (const char *reason = compression::getReasonIfUnsupported(
                 compression::formatFor(type))) {
    error(option + ": " + reason);
  }
  return type;
}
} // namespace

// Parse the symbol ordering file and warn for any duplicate entries.
namespace {
SmallVector<StringRef, 0> getSymbolOrderingFile(MemoryBufferRef mb) {
  SetVector<StringRef, SmallVector<StringRef, 0>> names;
  for (StringRef s : args::getLines(mb))
    if (!names.insert(s) && config->warnSymbolOrdering)
      warn(mb.getBufferIdentifier() + ": duplicate ordered symbol: " + s);

  return names.takeVector();
}
} // namespace

namespace {
bool getIsRela(opt::InputArgList &args) {
  // If -z rel or -z rela is specified, use the last option.
  for (auto *arg : args.filtered_reverse(OPT_z)) {
    StringRef s(arg->getValue());
    if (s == "rel")
      return false;
    if (s == "rela")
      return true;
  }

  // Otherwise use the psABI defined relocation entry format.
  uint16_t m = config->emachine;
  return m == EM_AARCH64 || m == EM_X86_64;
}
} // namespace

// Checks the parameter of the bti-report and cet-report options.
namespace {
bool isValidReportString(StringRef arg) {
  return arg == "none" || arg == "warning" || arg == "error";
}
} // namespace

// Process a remap pattern 'from-glob=to-file'.
namespace {
bool remapInputs(StringRef line, const Twine &location) {
  SmallVector<StringRef, 0> fields;
  line.split(fields, '=');
  if (fields.size() != 2 || fields[1].empty()) {
    error(location + ": parse error, not 'from-glob=to-file'");
    return true;
  }
  if (!hasWildcard(fields[0]))
    config->remapInputs[fields[0]] = fields[1];
  else if (Expected<GlobPattern> pat = GlobPattern::create(fields[0]))
    config->remapInputsWildcards.emplace_back(std::move(*pat), fields[1]);
  else {
    error(location + ": " + toString(pat.takeError()) + ": " + fields[0]);
    return true;
  }
  return false;
}
} // namespace

// Initializes Config members by the command line options.
namespace {
void readConfigs(opt::InputArgList &args, const LinkerDriverConfig &driverCfg) {
  errorHandler().verbose = driverCfg.verbose;

  // Initialize static-linking mode from driver config; subsequent -Bstatic /
  // -Bdynamic on the command line (via -Wl,) can still toggle this per-file.
  config->isStatic = driverCfg.staticLink;

  config->allowMultipleDefinition =
      args.hasFlag(OPT_allow_multiple_definition,
                   OPT_no_allow_multiple_definition, false) ||
      hasZOption(args, "muldefs");
  for (auto *arg : args.filtered(OPT_override))
    ctx.overrideSymbols.try_emplace(arg->getValue(), nullptr);
  config->androidMemtagHeap =
      args.hasFlag(OPT_android_memtag_heap, OPT_no_android_memtag_heap, false);
  config->androidMemtagStack = args.hasFlag(OPT_android_memtag_stack,
                                            OPT_no_android_memtag_stack, false);
  config->androidMemtagMode = getMemtagMode(args);
  config->auxiliaryList = args::getStrings(args, OPT_auxiliary);
  if (opt::Arg *arg = args.getLastArg(
          OPT_Bno_symbolic, OPT_Bsymbolic_non_weak_functions,
          OPT_Bsymbolic_functions, OPT_Bsymbolic_non_weak, OPT_Bsymbolic)) {
    if (arg->getOption().matches(OPT_Bsymbolic_non_weak_functions))
      config->bsymbolic = BsymbolicKind::NonWeakFunctions;
    else if (arg->getOption().matches(OPT_Bsymbolic_functions))
      config->bsymbolic = BsymbolicKind::Functions;
    else if (arg->getOption().matches(OPT_Bsymbolic_non_weak))
      config->bsymbolic = BsymbolicKind::NonWeak;
    else if (arg->getOption().matches(OPT_Bsymbolic))
      config->bsymbolic = BsymbolicKind::All;
  }
  config->callGraphProfileSort =
      getCGProfileSortKind(driverCfg.callGraphProfileSort);
  config->checkSections =
      args.hasFlag(OPT_check_sections, OPT_no_check_sections, true);
  config->compressDebugSections =
      getCompressionType(driverCfg.compressDebugSections.empty()
                             ? "none"
                             : llvm::StringRef(driverCfg.compressDebugSections),
                         "--compress-debug-sections");
  config->cref = args.hasArg(OPT_cref);
  config->optimizeBBJumps =
      args.hasFlag(OPT_optimize_bb_jumps, OPT_no_optimize_bb_jumps, false);
  config->demangle = driverCfg.demangle;
  config->dependencyFile = args.getLastArgValue(OPT_dependency_file);
  config->dependentLibraries =
      args.hasFlag(OPT_dependent_libraries, OPT_no_dependent_libraries, true);
  config->discard = getDiscard(args);
  if (driverCfg.stripLocals && config->discard == DiscardPolicy::Default)
    config->discard = DiscardPolicy::All;
  config->dynamicLinker = saver().save(driverCfg.dynamicLinker);
  config->noDynamicLinker = driverCfg.noDynamicLinker;
  config->ehFrameHdr = driverCfg.ehFrameHdr;
  config->emitRelocs = args.hasArg(OPT_emit_relocs);
  config->enableNewDtags =
      args.hasFlag(OPT_enable_new_dtags, OPT_disable_new_dtags, true);
  config->entry = args.getLastArgValue(OPT_entry);

  errorHandler().errorHandlingScript =
      args.getLastArgValue(OPT_error_handling_script);

  config->executeOnly =
      args.hasFlag(OPT_execute_only, OPT_no_execute_only, false);
  config->exportDynamic = driverCfg.exportDynamic || driverCfg.shared;
  config->filterList = args::getStrings(args, OPT_filter);
  config->fini = args.getLastArgValue(OPT_fini, "_fini");
  config->fixCortexA53Errata843419 =
      args.hasArg(OPT_fix_cortex_a53_843419) && !driverCfg.relocatable;
  config->gcSections = driverCfg.gcSections;
  config->gnuUnique = args.hasFlag(OPT_gnu_unique, OPT_no_gnu_unique, true);
  config->gdbIndex = args.hasFlag(OPT_gdb_index, OPT_no_gdb_index, false);
  config->icf = getICFFromDriver(driverCfg.icfLevel);
  config->ignoreDataAddressEquality =
      args.hasArg(OPT_ignore_data_address_equality);
  config->ignoreFunctionAddressEquality =
      args.hasArg(OPT_ignore_function_address_equality);
  config->init = args.getLastArgValue(OPT_init, "_init");
  config->driverCfg = &driverCfg;
  config->mapFile = driverCfg.mapFile;
  config->mmapOutputFile =
      args.hasFlag(OPT_mmap_output_file, OPT_no_mmap_output_file, true);
  config->nmagic = args.hasFlag(OPT_nmagic, OPT_no_nmagic, false);
  config->noinhibitExec = args.hasArg(OPT_noinhibit_exec);
  config->nostdlib = driverCfg.nostdlib;
  config->oFormatBinary = isOutputFormatBinary(args);
  config->omagic = args.hasFlag(OPT_omagic, OPT_no_omagic, false);
  config->optimize =
      driverCfg.linkerOptLevel >= 0 ? driverCfg.linkerOptLevel : 1;
  config->orphanHandling = getOrphanHandling(args);
  config->outputFile = driverCfg.outputFile;
  config->packageMetadata = args.getLastArgValue(OPT_package_metadata);
  config->pie = driverCfg.pie;
  config->printIcfSections = driverCfg.printICFSections;
  config->printGcSections = driverCfg.printGCSections;
  config->printMemoryUsage = args.hasArg(OPT_print_memory_usage);
  config->printArchiveStats = args.getLastArgValue(OPT_print_archive_stats);
  config->printSymbolOrder = saver().save(driverCfg.printSymbolOrder);
  if (!driverCfg.callGraphOrderingFile.empty())
    config->callGraphOrderingFile =
        saver().save(driverCfg.callGraphOrderingFile);
  config->relax = args.hasFlag(OPT_relax, OPT_no_relax, true);
  config->rpath = getRpath(args);
  config->relocatable = driverCfg.relocatable;

  config->searchPaths = args::getStrings(args, OPT_library_path);
  config->sectionStartMap = getSectionStartMap(args);
  config->shared = driverCfg.shared;
  config->singleRoRx = !args.hasFlag(OPT_rosegment, OPT_no_rosegment, true);
  config->soName = args.getLastArgValue(OPT_soname);
  config->sortSection = getSortSection(args);
  config->splitStackAdjustSize =
      args::getInteger(args, OPT_split_stack_adjust_size, 16384);
  config->strip = getStripFromDriver(driverCfg);
  config->sysroot = driverCfg.sysroot;
  config->trace = driverCfg.traceFiles;
  config->undefined = args::getStrings(args, OPT_undefined);
  config->undefinedVersion =
      args.hasFlag(OPT_undefined_version, OPT_no_undefined_version, false);
  config->unique = args.hasArg(OPT_unique);
  config->useAndroidRelrTags = args.hasFlag(
      OPT_use_android_relr_tags, OPT_no_use_android_relr_tags, false);
  config->warnBackrefs =
      args.hasFlag(OPT_warn_backrefs, OPT_no_warn_backrefs, false);
  config->warnCommon = args.hasFlag(OPT_warn_common, OPT_no_warn_common, false);
  config->warnSymbolOrdering =
      args.hasFlag(OPT_warn_symbol_ordering, OPT_no_warn_symbol_ordering, true);
  config->whyExtract = args.getLastArgValue(OPT_why_extract);
  config->zCombreloc = getZFlag(args, "combreloc", "nocombreloc", true);
  config->zCopyreloc = getZFlag(args, "copyreloc", "nocopyreloc", true);
  config->zForceBti = hasZOption(args, "force-bti");
  config->zForceIbt = hasZOption(args, "force-ibt");
  config->zGlobal = hasZOption(args, "global");
  config->zGnustack = getZGnuStack(args);
  config->zIfuncNoplt = hasZOption(args, "ifunc-noplt");
  config->zInitfirst = hasZOption(args, "initfirst");
  config->zInterpose = hasZOption(args, "interpose");
  config->zKeepTextSectionPrefix = getZFlag(
      args, "keep-text-section-prefix", "nokeep-text-section-prefix", false);
  config->zNodefaultlib = hasZOption(args, "nodefaultlib");
  config->zNodelete = hasZOption(args, "nodelete");
  config->zNodlopen = hasZOption(args, "nodlopen");
  config->zNow = getZFlag(args, "now", "lazy", false);
  config->zOrigin = hasZOption(args, "origin");
  config->zPacPlt = hasZOption(args, "pac-plt");
  config->zRelro = getZFlag(args, "relro", "norelro", true);
  config->zRetpolineplt = hasZOption(args, "retpolineplt");
  config->zRodynamic = hasZOption(args, "rodynamic");
  config->zSeparate = getZSeparate(args);
  config->zShstk = hasZOption(args, "shstk");
  config->zStackSize = args::getZOptionValue(args, OPT_z, "stack-size", 0);
  config->zStartStopGC =
      getZFlag(args, "start-stop-gc", "nostart-stop-gc", true);
  config->zStartStopVisibility = getZStartStopVisibility(args);
  config->zText = getZFlag(args, "text", "notext", true);
  config->zWxneeded = hasZOption(args, "wxneeded");
  setUnresolvedSymbolPolicy(args);

  if (driverCfg.endianness == 1)
    config->optEL = true;
  else if (driverCfg.endianness == 2)
    config->optEB = true;

  for (opt::Arg *arg : args.filtered(OPT_remap_inputs)) {
    StringRef value(arg->getValue());
    remapInputs(value, arg->getSpelling());
  }
  for (opt::Arg *arg : args.filtered(OPT_remap_inputs_file)) {
    StringRef filename(arg->getValue());
    std::optional<MemoryBufferRef> buffer = readFile(filename);
    if (!buffer)
      continue;
    // Parse 'from-glob=to-file' lines, ignoring #-led comments.
    for (auto [lineno, line] : llvm::enumerate(args::getLines(*buffer)))
      if (remapInputs(line, filename + ":" + Twine(lineno + 1)))
        break;
  }

  for (opt::Arg *arg : args.filtered(OPT_shuffle_sections)) {
    constexpr StringRef errPrefix = "--shuffle-sections=: ";
    std::pair<StringRef, StringRef> kv = StringRef(arg->getValue()).split('=');
    if (kv.first.empty() || kv.second.empty()) {
      error(errPrefix + "expected <section_glob>=<seed>, but got '" +
            arg->getValue() + "'");
      continue;
    }
    // Signed so that <section_glob>=-1 is allowed.
    int64_t v;
    if (!to_integer(kv.second, v))
      error(errPrefix + "expected an integer, but got '" + kv.second + "'");
    else if (Expected<GlobPattern> pat = GlobPattern::create(kv.first))
      config->shuffleSections.emplace_back(std::move(*pat), uint32_t(v));
    else
      error(errPrefix + toString(pat.takeError()) + ": " + kv.first);
  }

  auto reports = {std::make_pair("bti-report", &config->zBtiReport),
                  std::make_pair("cet-report", &config->zCetReport)};
  for (opt::Arg *arg : args.filtered(OPT_z)) {
    std::pair<StringRef, StringRef> option =
        StringRef(arg->getValue()).split('=');
    for (auto reportArg : reports) {
      if (option.first != reportArg.first)
        continue;
      if (!isValidReportString(option.second)) {
        error(Twine("-z ") + reportArg.first + "= parameter " + option.second +
              " is not recognized");
        continue;
      }
      *reportArg.second = option.second;
    }
  }

  for (opt::Arg *arg : args.filtered(OPT_z)) {
    std::pair<StringRef, StringRef> option =
        StringRef(arg->getValue()).split('=');
    if (option.first != "dead-reloc-in-nonalloc")
      continue;
    constexpr StringRef errPrefix = "-z dead-reloc-in-nonalloc=: ";
    std::pair<StringRef, StringRef> kv = option.second.split('=');
    if (kv.first.empty() || kv.second.empty()) {
      error(errPrefix + "expected <section_glob>=<value>");
      continue;
    }
    uint64_t v;
    if (!to_integer(kv.second, v))
      error(errPrefix + "expected a non-negative integer, but got '" +
            kv.second + "'");
    else if (Expected<GlobPattern> pat = GlobPattern::create(kv.first))
      config->deadRelocInNonAlloc.emplace_back(std::move(*pat), v);
    else
      error(errPrefix + toString(pat.takeError()) + ": " + kv.first);
  }

  parseMllvmOptions(driverCfg);

  if (driverCfg.threadCount) {
    parallel::strategy = hardware_concurrency(driverCfg.threadCount);
  } else if (parallel::strategy.compute_thread_count() > 16) {
    log("set maximum concurrency to 16");
    parallel::strategy = hardware_concurrency(16);
  }
  config->threadCount = parallel::strategy.compute_thread_count();

  // ltoPartitions is set to 2 by the driver — just enough to enable
  // the parallel codegen hooks.  The hooks auto-detect internally.

  if (config->splitStackAdjustSize < 0)
    error("--split-stack-adjust-size: size must be >= 0");

  if (args.hasArg(OPT_Ttext_segment))
    error("-Ttext-segment is not supported. Use --image-base if you "
          "intend to set the base address");

  // Parse ELF{32,64}{LE,BE} and CPU type from driver-supplied emulation.
  if (!driverCfg.emulation.empty()) {
    StringRef s = saver().save(driverCfg.emulation);
    std::tie(config->ekind, config->emachine, config->osabi) =
        parseEmulation(s);
    config->emulation = s;
  }

  // Hash style is determined by the neverc driver via driverCfg.hashStyle.
  if (!driverCfg.hashStyle.empty()) {
    StringRef s = driverCfg.hashStyle;
    if (s == "sysv")
      config->sysvHash = true;
    else if (s == "gnu")
      config->gnuHash = true;
    else
      config->sysvHash = config->gnuHash = true;
  }

  // Page alignment can be disabled by the -n (--nmagic) and -N (--omagic).
  // As PT_GNU_RELRO relies on Paging, do not create it when we have disabled
  // it. Also disable RELRO for -r.
  if (config->nmagic || config->omagic || config->relocatable)
    config->zRelro = false;

  std::tie(config->buildId, config->buildIdVector) =
      getBuildIdFromDriver(driverCfg.buildId);

  if (getZFlag(args, "pack-relative-relocs", "nopack-relative-relocs", false)) {
    config->relrGlibc = true;
    config->relrPackDynRelocs = true;
  } else {
    std::tie(config->androidPackDynRelocs, config->relrPackDynRelocs) =
        getPackDynRelocs(args);
  }

  if (auto *arg = args.getLastArg(OPT_symbol_ordering_file)) {
    if (!driverCfg.callGraphOrderingFile.empty())
      error("--symbol-ordering-file and --call-graph-ordering-file "
            "may not be used together");
    if (std::optional<MemoryBufferRef> buffer = readFile(arg->getValue())) {
      config->symbolOrderingFile = getSymbolOrderingFile(*buffer);
      config->callGraphProfileSort = CGProfileSortKind::None;
    }
  }

  assert(config->versionDefinitions.empty());
  config->versionDefinitions.push_back(
      {"local", (uint16_t)VER_NDX_LOCAL, {}, {}});
  config->versionDefinitions.push_back(
      {"global", (uint16_t)VER_NDX_GLOBAL, {}, {}});

  // If --retain-symbol-file is used, we'll keep only the symbols listed in
  // the file and discard all others.
  if (auto *arg = args.getLastArg(OPT_retain_symbols_file)) {
    config->versionDefinitions[VER_NDX_LOCAL].nonLocalPatterns.push_back(
        {"*", /*hasWildcard=*/true});
    if (std::optional<MemoryBufferRef> buffer = readFile(arg->getValue()))
      for (StringRef s : args::getLines(*buffer))
        config->versionDefinitions[VER_NDX_GLOBAL].nonLocalPatterns.push_back(
            {s, /*hasWildcard=*/false});
  }

  for (opt::Arg *arg : args.filtered(OPT_warn_backrefs_exclude)) {
    StringRef pattern(arg->getValue());
    if (Expected<GlobPattern> pat = GlobPattern::create(pattern))
      config->warnBackrefsExclude.push_back(std::move(*pat));
    else
      error(arg->getSpelling() + ": " + toString(pat.takeError()) + ": " +
            pattern);
  }

  // For -no-pie and -pie, --export-dynamic-symbol specifies defined symbols
  // which should be exported. For -shared, references to matched non-local
  // STV_DEFAULT symbols are not bound to definitions within the shared object,
  // even if other options express a symbolic intention: -Bsymbolic,
  // -Bsymbolic-functions (if STT_FUNC), --dynamic-list.
  for (auto *arg : args.filtered(OPT_export_dynamic_symbol))
    config->dynamicList.push_back(
        {arg->getValue(), /*hasWildcard=*/hasWildcard(arg->getValue())});

  // --export-dynamic-symbol-list specifies a list of --export-dynamic-symbol
  // patterns. --dynamic-list is --export-dynamic-symbol-list plus -Bsymbolic
  // like semantics.
  config->symbolic =
      config->bsymbolic == BsymbolicKind::All || args.hasArg(OPT_dynamic_list);
  for (auto *arg :
       args.filtered(OPT_dynamic_list, OPT_export_dynamic_symbol_list))
    if (std::optional<MemoryBufferRef> buffer = readFile(arg->getValue()))
      readDynamicList(*buffer);

  for (auto *arg : args.filtered(OPT_version_script))
    if (std::optional<std::string> path = searchScript(arg->getValue())) {
      if (std::optional<MemoryBufferRef> buffer = readFile(*path))
        readVersionScript(*buffer);
    } else {
      error(Twine("cannot find version script ") + arg->getValue());
    }
}
} // namespace

// Derived config values not directly mapped from command line options.
namespace {
void setConfigs(opt::InputArgList &args) {
  ELFKind k = config->ekind;

  config->copyRelocs = (config->relocatable || config->emitRelocs);
  config->isLE = (k == ELF64LEKind);
  config->endianness = config->isLE ? endianness::little : endianness::big;
  config->isPic = config->pie || config->shared;
  config->picThunk = args.hasArg(OPT_pic_veneer, config->isPic);

  // ELF defines two different ways to store relocation addends as shown below:
  //
  config->isRela = getIsRela(args);
  config->writeAddends = args.hasFlag(OPT_apply_dynamic_relocs,
                                      OPT_no_apply_dynamic_relocs, false) ||
                         !config->isRela;
#ifndef NDEBUG
  bool checkDynamicRelocsDefault = true;
#else
  bool checkDynamicRelocsDefault = false;
#endif
  config->checkDynamicRelocs =
      args.hasFlag(OPT_check_dynamic_relocations,
                   OPT_no_check_dynamic_relocations, checkDynamicRelocsDefault);
}
} // namespace

namespace {
bool isFormatBinary(StringRef s) {
  if (s == "binary")
    return true;
  if (s == "elf" || s == "default")
    return false;
  error("unknown --format value: " + s +
        " (supported formats: elf, default, binary)");
  return false;
}
} // namespace

void LinkerDriver::createFiles(opt::InputArgList &args) {
  llvm::TimeTraceScope timeScope("Load input files");
  // For --{push,pop}-state.
  std::vector<std::tuple<bool, bool, bool>> stack;

  // Iterate over argv to process input files and positional arguments.
  InputFile::isInGroup = false;
  bool hasInput = false;
  for (auto *arg : args) {
    switch (arg->getOption().getID()) {
    case OPT_library:
      addLibrary(arg->getValue());
      hasInput = true;
      break;
    case OPT_INPUT:
      addFile(arg->getValue(), /*withLOption=*/false);
      hasInput = true;
      break;
    case OPT_defsym: {
      StringRef from;
      StringRef to;
      std::tie(from, to) = StringRef(arg->getValue()).split('=');
      if (from.empty() || to.empty())
        error("--defsym: syntax error: " + StringRef(arg->getValue()));
      else
        readDefsym(from, MemoryBufferRef(to, "--defsym"));
      break;
    }
    case OPT_script:
      if (std::optional<std::string> path = searchScript(arg->getValue())) {
        if (std::optional<MemoryBufferRef> mb = readFile(*path))
          readLinkerScript(*mb);
        break;
      }
      error(Twine("cannot find linker script ") + arg->getValue());
      break;
    case OPT_as_needed:
      config->asNeeded = true;
      break;
    case OPT_format:
      config->formatBinary = isFormatBinary(arg->getValue());
      break;
    case OPT_no_as_needed:
      config->asNeeded = false;
      break;
    case OPT_Bstatic:
    case OPT_omagic:
    case OPT_nmagic:
      config->isStatic = true;
      break;
    case OPT_Bdynamic:
      config->isStatic = false;
      break;
    case OPT_whole_archive:
      inWholeArchive = true;
      break;
    case OPT_no_whole_archive:
      inWholeArchive = false;
      break;
    case OPT_just_symbols:
      if (std::optional<MemoryBufferRef> mb = readFile(arg->getValue())) {
        files.push_back(createObjFile(*mb));
        files.back()->justSymbols = true;
      }
      break;
    case OPT_start_group:
      if (InputFile::isInGroup)
        error("nested --start-group");
      InputFile::isInGroup = true;
      break;
    case OPT_end_group:
      if (!InputFile::isInGroup)
        error("stray --end-group");
      InputFile::isInGroup = false;
      ++InputFile::nextGroupId;
      break;
    case OPT_start_lib:
      if (inLib)
        error("nested --start-lib");
      if (InputFile::isInGroup)
        error("may not nest --start-lib in --start-group");
      inLib = true;
      InputFile::isInGroup = true;
      break;
    case OPT_end_lib:
      if (!inLib)
        error("stray --end-lib");
      inLib = false;
      InputFile::isInGroup = false;
      ++InputFile::nextGroupId;
      break;
    case OPT_push_state:
      stack.emplace_back(config->asNeeded, config->isStatic, inWholeArchive);
      break;
    case OPT_pop_state:
      if (stack.empty()) {
        error("unbalanced --push-state/--pop-state");
        break;
      }
      std::tie(config->asNeeded, config->isStatic, inWholeArchive) =
          stack.back();
      stack.pop_back();
      break;
    }
  }

  if (files.empty() && !hasInput && errorCount() == 0)
    error("no input files");
}

// If -m <machine_type> was not given, infer it from object files.
void LinkerDriver::inferMachineType() {
  if (config->ekind != ELFNoneKind)
    return;

  for (InputFile *f : files) {
    if (f->ekind == ELFNoneKind)
      continue;
    config->ekind = f->ekind;
    config->emachine = f->emachine;
    config->osabi = f->osabi;
    if (config->emachine != EM_AARCH64 && config->emachine != EM_X86_64)
      error(toString(f) + ": unsupported target architecture");
    return;
  }
  error("target emulation unknown: -m or at least one .o file required");
}

// Parse -z max-page-size=<value>. The default value is defined by
// each target.
namespace {
uint64_t getMaxPageSize(opt::InputArgList &args) {
  uint64_t val = args::getZOptionValue(args, OPT_z, "max-page-size",
                                       target->defaultMaxPageSize);
  if (!isPowerOf2_64(val)) {
    error("max-page-size: value isn't a power of 2");
    return target->defaultMaxPageSize;
  }
  if (config->nmagic || config->omagic) {
    if (val != target->defaultMaxPageSize)
      warn("-z max-page-size set, but paging disabled by omagic or nmagic");
    return 1;
  }
  return val;
}
} // namespace

// Parse -z common-page-size=<value>. The default value is defined by
// each target.
namespace {
uint64_t getCommonPageSize(opt::InputArgList &args) {
  uint64_t val = args::getZOptionValue(args, OPT_z, "common-page-size",
                                       target->defaultCommonPageSize);
  if (!isPowerOf2_64(val)) {
    error("common-page-size: value isn't a power of 2");
    return target->defaultCommonPageSize;
  }
  if (config->nmagic || config->omagic) {
    if (val != target->defaultCommonPageSize)
      warn("-z common-page-size set, but paging disabled by omagic or nmagic");
    return 1;
  }
  // commonPageSize can't be larger than maxPageSize.
  if (val > config->maxPageSize)
    val = config->maxPageSize;
  return val;
}
} // namespace

// Parses --image-base option.
namespace {
std::optional<uint64_t> getImageBase(opt::InputArgList &args) {
  // Because we are using "Config->maxPageSize" here, this function has to be
  // called after the variable is initialized.
  auto *arg = args.getLastArg(OPT_image_base);
  if (!arg)
    return std::nullopt;

  StringRef s = arg->getValue();
  uint64_t v;
  if (!to_integer(s, v)) {
    error("--image-base: number expected, but got " + s);
    return 0;
  }
  if ((v % config->maxPageSize) != 0)
    warn("--image-base: address isn't multiple of page size: " + s);
  return v;
}
} // namespace

// Parses `--exclude-libs=lib,lib,...`.
// The library names may be delimited by commas or colons.
namespace {
DenseSet<StringRef> getExcludeLibs(opt::InputArgList &args) {
  DenseSet<StringRef> ret;
  for (auto *arg : args.filtered(OPT_exclude_libs)) {
    StringRef s = arg->getValue();
    for (;;) {
      size_t pos = s.find_first_of(",:");
      if (pos == StringRef::npos)
        break;
      ret.insert(s.substr(0, pos));
      s = s.substr(pos + 1);
    }
    ret.insert(s);
  }
  return ret;
}
} // namespace

namespace {
void excludeLibs(opt::InputArgList &args) {
  DenseSet<StringRef> libs = getExcludeLibs(args);
  bool all = libs.count("ALL");

  auto visit = [&](InputFile *file) {
    if (file->archiveName.empty() ||
        !(all || libs.count(path::filename(file->archiveName))))
      return;
    ArrayRef<Symbol *> symbols = file->getSymbols();
    if (isa<ELFFileBase>(file))
      symbols = cast<ELFFileBase>(file)->getGlobalSymbols();
    for (Symbol *sym : symbols)
      if (sym && !sym->isUndefined() && sym->file == file)
        sym->versionId = VER_NDX_LOCAL;
  };

  for (ELFFileBase *file : ctx.objectFiles)
    visit(file);

  for (BitcodeFile *file : ctx.bitcodeFiles)
    visit(file);
}
} // namespace

// Force Sym to be entered in the output.
namespace {
void handleUndefined(Symbol *sym, const char *option) {
  // Since a symbol may not be used inside the program, LTO may
  // eliminate it. Mark the symbol as "used" to prevent it.
  sym->isUsedInRegularObj = true;

  if (!sym->isLazy())
    return;
  sym->extract();
  if (!config->whyExtract.empty())
    ctx.whyExtractRecords.emplace_back(option, sym->file, *sym);
}
} // namespace

// As an extension to GNU linkers, the NeverC linker supports a variant of `-u`
// which accepts wildcard patterns. All symbols that match a given
// pattern are handled as if they were given by `-u`.
namespace {
void handleUndefinedGlob(StringRef arg) {
  Expected<GlobPattern> pat = GlobPattern::create(arg);
  if (!pat) {
    error("--undefined-glob: " + toString(pat.takeError()) + ": " + arg);
    return;
  }

  // Calling sym->extract() in the loop is not safe because it may add new
  // symbols to the symbol table, invalidating the current iterator.
  SmallVector<Symbol *, 0> syms;
  for (Symbol *sym : symtab.getSymbols())
    if (!sym->isPlaceholder() && pat->match(sym->getName()))
      syms.push_back(sym);

  for (Symbol *sym : syms)
    handleUndefined(sym, "--undefined-glob");
}
} // namespace

namespace {
void handleLibcall(StringRef name) {
  Symbol *sym = symtab.find(name);
  if (!sym || !sym->isLazy())
    return;

  MemoryBufferRef mb;
  mb = cast<LazyObject>(sym)->file->mb;

  if (isBitcode(mb))
    sym->extract();
}
} // namespace

namespace {
void writeArchiveStats() {
  if (config->printArchiveStats.empty())
    return;

  std::error_code ec;
  raw_fd_ostream os = ctx.openAuxiliaryFile(config->printArchiveStats, ec);
  if (ec) {
    error("--print-archive-stats=: cannot open " + config->printArchiveStats +
          ": " + ec.message());
    return;
  }

  os << "members\textracted\tarchive\n";

  SmallVector<StringRef, 0> archives;
  DenseMap<CachedHashStringRef, unsigned> all, extracted;
  for (ELFFileBase *file : ctx.objectFiles)
    if (file->archiveName.size())
      ++extracted[CachedHashStringRef(file->archiveName)];
  for (BitcodeFile *file : ctx.bitcodeFiles)
    if (file->archiveName.size())
      ++extracted[CachedHashStringRef(file->archiveName)];
  for (std::pair<StringRef, unsigned> f : ctx.driver.archiveFiles) {
    unsigned &v = extracted[CachedHashString(f.first)];
    os << f.second << '\t' << v << '\t' << f.first << '\n';
    // If the archive occurs multiple times, other instances have a count of 0.
    v = 0;
  }
}
} // namespace

namespace {
void writeWhyExtract() {
  if (config->whyExtract.empty())
    return;

  std::error_code ec;
  raw_fd_ostream os = ctx.openAuxiliaryFile(config->whyExtract, ec);
  if (ec) {
    error("cannot open --why-extract= file " + config->whyExtract + ": " +
          ec.message());
    return;
  }

  os << "reference\textracted\tsymbol\n";
  for (auto &entry : ctx.whyExtractRecords) {
    os << std::get<0>(entry) << '\t' << toString(std::get<1>(entry)) << '\t'
       << toString(std::get<2>(entry)) << '\n';
  }
}
} // namespace

namespace {
void reportBackrefs() {
  for (auto &ref : ctx.backwardReferences) {
    const Symbol &sym = *ref.first;
    std::string to = toString(ref.second.second);
    // Some libraries have known problems and can cause noise. Filter them out
    // with --warn-backrefs-exclude=. The value may look like (for --start-lib)
    // *.o or (archive member) *.a(*.o).
    bool exclude = false;
    for (const llvm::GlobPattern &pat : config->warnBackrefsExclude)
      if (pat.match(to)) {
        exclude = true;
        break;
      }
    if (!exclude)
      warn("backward reference detected: " + sym.getName() + " in " +
           toString(ref.second.first) + " refers to " + to);
  }
}
} // namespace

// Handle --dependency-file=<path>. If that option is given, the linker creates
// a file at a given path with the following contents:
//
//   <output-file>: <input-file> ...
//
//   <input-file>:
//
// where <output-file> is a pathname of an output file and <input-file>
// ... is a list of pathnames of all input files. `make` command can read a
// file in the above format and interpret it as a dependency info. We write
// phony targets for every <input-file> to avoid an error when that file is
// removed.
//
// This option is useful if you want to make your final executable to depend
// on all input files including system libraries. Here is why.
//
// When you write a Makefile, you usually write it so that the final
// executable depends on all user-generated object files. Normally, you
// don't make your executable to depend on system libraries (such as libc)
// because you don't know the exact paths of libraries, even though system
// libraries that are linked to your executable statically are technically a
// part of your program. By using --dependency-file option, you can make
// the linker to dump dependency info so that you can maintain exact
// dependencies easily.
namespace {
void writeDependencyFile() {
  std::error_code ec;
  raw_fd_ostream os = ctx.openAuxiliaryFile(config->dependencyFile, ec);
  if (ec) {
    error("cannot open " + config->dependencyFile + ": " + ec.message());
    return;
  }

  // Escape for Make/Ninja: space→\\, #→\#, $→$$.
  auto printFilename = [](raw_fd_ostream &os, StringRef filename) {
    llvm::SmallString<256> nativePath;
    llvm::sys::path::native(filename.str(), nativePath);
    llvm::sys::path::remove_dots(nativePath, /*remove_dot_dot=*/true);
    for (unsigned i = 0, e = nativePath.size(); i != e; ++i) {
      if (nativePath[i] == '#') {
        os << '\\';
      } else if (nativePath[i] == ' ') {
        os << '\\';
        unsigned j = i;
        while (j > 0 && nativePath[--j] == '\\')
          os << '\\';
      } else if (nativePath[i] == '$') {
        os << '$';
      }
      os << nativePath[i];
    }
  };

  os << config->outputFile << ":";
  for (StringRef path : config->dependencyFiles) {
    os << " \\\n ";
    printFilename(os, path);
  }
  os << "\n";

  for (StringRef path : config->dependencyFiles) {
    os << "\n";
    printFilename(os, path);
    os << ":\n";
  }
}
} // namespace

namespace {
void replaceCommonSymbols() {
  llvm::TimeTraceScope timeScope("Replace common symbols");
  for (ELFFileBase *file : ctx.objectFiles) {
    if (!file->hasCommonSyms)
      continue;
    for (Symbol *sym : file->getGlobalSymbols()) {
      auto *s = dyn_cast<CommonSymbol>(sym);
      if (!s)
        continue;

      auto *bss = make<BssSection>("COMMON", s->size, s->alignment);
      bss->file = s->file;
      ctx.inputSections.push_back(bss);
      Defined(s->file, StringRef(), s->binding, s->stOther, s->type,
              /*value=*/0, s->size, bss)
          .overwrite(*s);
    }
  }
}
} // namespace

// The section referred to by `s` is considered address-significant. Set the
// keepUnique flag on the section if appropriate.
namespace {
void markAddrsig(Symbol *s) {
  if (auto *d = dyn_cast_or_null<Defined>(s))
    if (d->section)
      // We don't need to keep text sections unique under --icf=all even if they
      // are address-significant.
      if (config->icf == ICFLevel::Safe || !(d->section->flags & SHF_EXECINSTR))
        d->section->keepUnique = true;
}
} // namespace

namespace {
template <class ELFT> void findKeepUniqueSections(opt::InputArgList &args) {
  for (auto *arg : args.filtered(OPT_keep_unique)) {
    StringRef name = arg->getValue();
    auto *d = dyn_cast_or_null<Defined>(symtab.find(name));
    if (!d || !d->section) {
      warn("could not find symbol " + name + " to keep unique");
      continue;
    }
    d->section->keepUnique = true;
  }

  if (config->icf == ICFLevel::All && config->ignoreDataAddressEquality)
    return;

  for (Symbol *sym : symtab.getSymbols())
    if (sym->includeInDynsym())
      markAddrsig(sym);

  for (InputFile *f : ctx.objectFiles) {
    auto *obj = cast<ObjFile<ELFT>>(f);
    ArrayRef<Symbol *> syms = obj->getSymbols();
    if (obj->addrsigSec) {
      ArrayRef<uint8_t> contents =
          check(obj->getObj().getSectionContents(*obj->addrsigSec));
      const uint8_t *cur = contents.begin();
      while (cur != contents.end()) {
        unsigned size;
        const char *err = nullptr;
        uint64_t symIndex = decodeULEB128(cur, &size, contents.end(), &err);
        if (err)
          fatal(toString(f) + ": could not decode addrsig section: " + err);
        markAddrsig(syms[symIndex]);
        cur += size;
      }
    } else {
      // If an object file does not have an address-significance table,
      // conservatively mark all of its symbols as address-significant.
      for (Symbol *s : syms)
        markAddrsig(s);
    }
  }
}
} // namespace

namespace {
template <typename ELFT> void readSymbolPartitionSection(InputSectionBase *s) {
  // Read the relocation that refers to the partition's entry point symbol.
  Symbol *sym;
  const RelsOrRelas<ELFT> rels = s->template relsOrRelas<ELFT>();
  if (rels.areRelocsRel())
    sym = &s->getFile<ELFT>()->getRelocTargetSym(rels.rels[0]);
  else
    sym = &s->getFile<ELFT>()->getRelocTargetSym(rels.relas[0]);
  if (!isa<Defined>(sym) || !sym->includeInDynsym())
    return;

  StringRef partName = reinterpret_cast<const char *>(s->content().data());
  for (Partition &part : partitions) {
    if (part.name == partName) {
      sym->partition = part.getNumber();
      return;
    }
  }

  // Forbid partitions from being used on incompatible targets, and forbid them
  // from being used together with various linker features that assume a single
  // set of output sections.
  if (script->hasSectionsCommand)
    error(toString(s->file) +
          ": partitions cannot be used with the SECTIONS command");
  if (script->hasPhdrsCommands())
    error(toString(s->file) +
          ": partitions cannot be used with the PHDRS command");
  if (!config->sectionStartMap.empty())
    error(toString(s->file) + ": partitions cannot be used with "
                              "--section-start, -Ttext, -Tdata or -Tbss");
  // Impose a limit of no more than 254 partitions. This limit comes from the
  // sizes of the Partition fields in InputSectionBase and Symbol, as well as
  // the amount of space devoted to the partition number in RankFlags.
  if (partitions.size() == 254)
    fatal("may not have more than 254 partitions");

  partitions.emplace_back();
  Partition &newPart = partitions.back();
  newPart.name = partName;
  sym->partition = newPart.getNumber();
}
} // namespace

namespace {
Symbol *addUnusedUndefined(StringRef name, uint8_t binding = STB_GLOBAL) {
  return symtab.addSymbol(Undefined{nullptr, name, binding, STV_DEFAULT, 0});
}
} // namespace

namespace {
void markBuffersAsDontNeed() {
  if (ctx.memoryBuffers.empty())
    return;

  SmallPtrSet<const char *, 16> bufs;
  for (BitcodeFile *file : ctx.bitcodeFiles)
    bufs.insert(file->mb.getBufferStart());
  for (BitcodeFile *file : ctx.lazyBitcodeFiles)
    bufs.insert(file->mb.getBufferStart());
  for (MemoryBuffer &mb : llvm::make_pointee_range(ctx.memoryBuffers))
    if (bufs.count(mb.getBufferStart()))
      mb.dontNeedIfMmap();
}
} // namespace

// ===----------------------------------------------------------------------===
// LTO compilation & symbol resolution
// ===----------------------------------------------------------------------===

template <class ELFT> void LinkerDriver::compileBitcodeFiles() {
  llvm::TimeTraceScope timeScope("LTO");
  lto.reset(new BitcodeCompiler);
  lto->addBatch(ctx.bitcodeFiles);

  if (!ctx.bitcodeFiles.empty())
    markBuffersAsDontNeed();

  auto Compiled = lto->compile();

  for (InputFile *file : Compiled) {
    auto *obj = cast<ObjFile<ELFT>>(file);
    obj->parse(/*ignoreComdats=*/true);

    if (!config->relocatable)
      for (Symbol *sym : obj->getGlobalSymbols())
        if (sym->hasVersionSuffix)
          sym->parseSymbolVersion();
    ctx.objectFiles.push_back(obj);
  }
}

struct WrappedSymbol {
  Symbol *sym;
  Symbol *real;
  Symbol *wrap;
};

// Wrapper symbols appear unused at this point; flags prevent LTO elimination.
namespace {
std::vector<WrappedSymbol> addWrappedSymbols(opt::InputArgList &args) {
  std::vector<WrappedSymbol> v;
  DenseSet<StringRef> seen;

  for (auto *arg : args.filtered(OPT_wrap)) {
    StringRef name = arg->getValue();
    if (!seen.insert(name).second)
      continue;

    Symbol *sym = symtab.find(name);
    if (!sym)
      continue;

    Symbol *wrap =
        addUnusedUndefined(saver().save("__wrap_" + name), sym->binding);

    // If __real_ is referenced, pull in the symbol if it is lazy. Do this after
    // processing __wrap_ as that may have referenced __real_.
    StringRef realName = saver().save("__real_" + name);
    if (symtab.find(realName))
      addUnusedUndefined(name, sym->binding);

    Symbol *real = addUnusedUndefined(realName);
    v.push_back({sym, real, wrap});

    real->scriptDefined = true;
    sym->scriptDefined = true;

    if (real->referenced || real->isDefined())
      sym->referencedAfterWrap = true;
    if (sym->referenced || sym->isDefined())
      wrap->referencedAfterWrap = true;
  }
  return v;
}
} // namespace

namespace {
void combineVersionedSymbol(Symbol &sym, DenseMap<Symbol *, Symbol *> &map) {
  const char *suffix1 = sym.getVersionSuffix();
  if (suffix1[0] != '@' || suffix1[1] == '@')
    return;

  Defined *sym2 = dyn_cast_or_null<Defined>(symtab.find(sym.getName()));
  if (!sym2)
    return;
  const char *suffix2 = sym2->getVersionSuffix();
  if (suffix2[0] == '@' && suffix2[1] == '@' &&
      strcmp(suffix1 + 1, suffix2 + 2) == 0) {
    map.try_emplace(&sym, sym2);
    if (sym.isDefined()) {
      sym2->checkDuplicate(cast<Defined>(sym));
      sym2->resolve(cast<Defined>(sym));
    } else if (sym.isUndefined()) {
      sym2->resolve(cast<Undefined>(sym));
    } else {
      sym2->resolve(cast<SharedSymbol>(sym));
    }
    // Eliminate foo@v1 from the symbol table.
    sym.symbolKind = Symbol::PlaceholderKind;
    sym.isUsedInRegularObj = false;
  } else if (auto *sym1 = dyn_cast<Defined>(&sym)) {
    if (sym2->versionId > VER_NDX_GLOBAL
            ? config->versionDefinitions[sym2->versionId].name == suffix1 + 1
            : sym1->section == sym2->section && sym1->value == sym2->value) {
      // Due to an assembler design flaw, if foo is defined, .symver foo,
      // foo@v1 defines both foo and foo@v1. Unless foo is bound to a
      // different version, GNU ld makes foo@v1 canonical and eliminates
      // foo. Emulate its behavior, otherwise we would have foo or foo@@v1
      // beside foo@v1. foo@v1 and foo combining does not apply if they are
      // not defined in the same place.
      map.try_emplace(sym2, &sym);
      sym2->symbolKind = Symbol::PlaceholderKind;
      sym2->isUsedInRegularObj = false;
    }
  }
}
} // namespace

// Do renaming for --wrap and foo@v1 by updating pointers to symbols.
//
// When this function is executed, only InputFiles and symbol table
// contain pointers to symbol objects. We visit them to replace pointers,
// so that wrapped symbols are swapped as instructed by the command line.
namespace {
void redirectSymbols(ArrayRef<WrappedSymbol> wrapped) {
  llvm::TimeTraceScope timeScope("Redirect symbols");
  DenseMap<Symbol *, Symbol *> map;
  for (const WrappedSymbol &w : wrapped) {
    map[w.sym] = w.wrap;
    map[w.real] = w.sym;
  }

  // If there are version definitions (versionDefinitions.size() > 2), enumerate
  // symbols with a non-default version (foo@v1) and check whether it should be
  // combined with foo or foo@@v1.
  if (config->versionDefinitions.size() > 2)
    for (Symbol *sym : symtab.getSymbols())
      if (sym->hasVersionSuffix)
        combineVersionedSymbol(*sym, map);

  if (map.empty())
    return;

  // Update pointers in input files.
  parallelForEach(ctx.objectFiles, [&](ELFFileBase *file) {
    for (Symbol *&sym : file->getMutableGlobalSymbols())
      if (Symbol *s = map.lookup(sym))
        sym = s;
  });

  // Update pointers in the symbol table.
  for (const WrappedSymbol &w : wrapped)
    symtab.wrap(w.sym, w.real, w.wrap);
}
} // namespace

namespace {
void checkAndReportMissingFeature(StringRef config, uint32_t features,
                                  uint32_t mask, const Twine &report) {
  if (!(features & mask)) {
    if (config == "error")
      error(report);
    else if (config == "warning")
      warn(report);
  }
}
} // namespace

// To enable CET (x86's hardware-assisted control flow enforcement), each
// source file must be compiled with -fcf-protection. Object files compiled
// with the flag contain feature flags indicating that they are compatible
// with CET. We enable the feature only when all object files are compatible
// with CET.
//
// This is also the case with AARCH64's BTI and PAC which use the similar
// GNU_PROPERTY_AARCH64_FEATURE_1_AND mechanism.
namespace {
uint32_t getAndFeatures() {
  if (config->emachine != EM_X86_64 && config->emachine != EM_AARCH64)
    return 0;

  uint32_t ret = -1;
  for (ELFFileBase *f : ctx.objectFiles) {
    uint32_t features = f->andFeatures;

    checkAndReportMissingFeature(
        config->zBtiReport, features, GNU_PROPERTY_AARCH64_FEATURE_1_BTI,
        toString(f) + ": -z bti-report: file does not have "
                      "GNU_PROPERTY_AARCH64_FEATURE_1_BTI property");

    checkAndReportMissingFeature(
        config->zCetReport, features, GNU_PROPERTY_X86_FEATURE_1_IBT,
        toString(f) + ": -z cet-report: file does not have "
                      "GNU_PROPERTY_X86_FEATURE_1_IBT property");

    checkAndReportMissingFeature(
        config->zCetReport, features, GNU_PROPERTY_X86_FEATURE_1_SHSTK,
        toString(f) + ": -z cet-report: file does not have "
                      "GNU_PROPERTY_X86_FEATURE_1_SHSTK property");

    if (config->zForceBti && !(features & GNU_PROPERTY_AARCH64_FEATURE_1_BTI)) {
      features |= GNU_PROPERTY_AARCH64_FEATURE_1_BTI;
      if (config->zBtiReport == "none")
        warn(toString(f) + ": -z force-bti: file does not have "
                           "GNU_PROPERTY_AARCH64_FEATURE_1_BTI property");
    } else if (config->zForceIbt &&
               !(features & GNU_PROPERTY_X86_FEATURE_1_IBT)) {
      if (config->zCetReport == "none")
        warn(toString(f) + ": -z force-ibt: file does not have "
                           "GNU_PROPERTY_X86_FEATURE_1_IBT property");
      features |= GNU_PROPERTY_X86_FEATURE_1_IBT;
    }
    if (config->zPacPlt && !(features & GNU_PROPERTY_AARCH64_FEATURE_1_PAC)) {
      warn(toString(f) + ": -z pac-plt: file does not have "
                         "GNU_PROPERTY_AARCH64_FEATURE_1_PAC property");
      features |= GNU_PROPERTY_AARCH64_FEATURE_1_PAC;
    }
    ret &= features;
  }

  // Force enable Shadow Stack.
  if (config->zShstk)
    ret |= GNU_PROPERTY_X86_FEATURE_1_SHSTK;

  return ret;
}
} // namespace

namespace {
void prepareSectionsAndLocals(ELFFileBase *file, bool ignoreComdats) {
  switch (file->ekind) {
  case ELF64LEKind:
    cast<ObjFile<ELF64LE>>(file)->prepareSectionsAndLocals(ignoreComdats);
    break;
  case ELF64BEKind:
    cast<ObjFile<ELF64BE>>(file)->prepareSectionsAndLocals(ignoreComdats);
    break;
  default:
    llvm_unreachable("");
  }
}
} // namespace

namespace {
void finalizeObjectFile(ELFFileBase *file) {
  switch (file->ekind) {
  case ELF64LEKind:
    cast<ObjFile<ELF64LE>>(file)->postParse();
    break;
  case ELF64BEKind:
    cast<ObjFile<ELF64BE>>(file)->postParse();
    break;
  default:
    llvm_unreachable("");
  }
}
} // namespace

// Do actual linking. Note that when this function is called,
// all linker scripts have already been parsed.
void LinkerDriver::execute(opt::InputArgList &args) {
  llvm::TimeTraceScope timeScope("Link", StringRef("LinkerDriver::execute"));
  // Fallback: if driverCfg.hashStyle was empty, default to "both".
  if (!config->sysvHash && !config->gnuHash)
    config->sysvHash = config->gnuHash = true;

  // Default output filename is "a.out" by the Unix tradition.
  if (config->outputFile.empty())
    config->outputFile = "a.out";

  // Fail early if the output file or map file is not writable. If a user has a
  // long link, e.g. due to a large LTO link, they do not wish to run it and
  // find that it failed because there was a mistake in their command-line.
  {
    llvm::TimeTraceScope timeScope("Create output files");
    if (auto e = tryCreateFile(config->outputFile))
      error("cannot open output file " + config->outputFile + ": " +
            e.message());
    if (auto e = tryCreateFile(config->mapFile))
      error("cannot open map file " + config->mapFile + ": " + e.message());
    if (auto e = tryCreateFile(config->whyExtract))
      error("cannot open --why-extract= file " + config->whyExtract + ": " +
            e.message());
  }
  if (errorCount())
    return;

  // Use default entry point name if no name was given via the command
  // line nor linker scripts.
  config->warnMissingEntry =
      (!config->entry.empty() || (!config->shared && !config->relocatable));
  if (config->entry.empty() && !config->relocatable)
    config->entry = "_start";

  for (auto *arg : args.filtered(OPT_trace_symbol))
    symtab.insert(arg->getValue())->traced = true;

  // Handle -u/--undefined before input files. If both a.a and b.so define foo,
  // -u foo a.a b.so will extract a.a.
  for (StringRef name : config->undefined)
    addUnusedUndefined(name)->referenced = true;

  // Add all files to the symbol table. This will add almost all
  // symbols that we need to the symbol table. This process might
  // add files to the link, via autolinking, these files are always
  // appended to the Files vector.
  {
    llvm::TimeTraceScope timeScope("Parse input files");
    for (size_t i = 0; i < files.size(); ++i) {
      llvm::TimeTraceScope timeScope("Parse input files", files[i]->getName());
      parseFile(files[i]);
    }
  }

  config->hasDynSymTab =
      !ctx.sharedFiles.empty() || config->isPic || config->exportDynamic;

  for (StringRef name : script->referencedSymbols) {
    Symbol *sym = addUnusedUndefined(name);
    sym->isUsedInRegularObj = true;
    sym->referenced = true;
  }

  for (StringRef name : config->undefined)
    if (Defined *sym = dyn_cast_or_null<Defined>(symtab.find(name)))
      sym->isUsedInRegularObj = true;

  if (Symbol *sym = symtab.find(config->entry))
    handleUndefined(sym, "--entry");

  for (StringRef pat : args::getStrings(args, OPT_undefined_glob))
    handleUndefinedGlob(pat);

  if (Symbol *sym = dyn_cast_or_null<Defined>(symtab.find(config->init)))
    sym->isUsedInRegularObj = true;
  if (Symbol *sym = dyn_cast_or_null<Defined>(symtab.find(config->fini)))
    sym->isUsedInRegularObj = true;

  // Pre-LTO: pull in bitcode-defined libcall symbols only.
  if (!ctx.bitcodeFiles.empty())
    for (auto *s : lto::LTO::getRuntimeLibcallSymbols())
      handleLibcall(s);

  // Archive members defining __wrap symbols may be extracted.
  std::vector<WrappedSymbol> wrapped = addWrappedSymbols(args);

  parallelForEach(ctx.objectFiles, [](ELFFileBase *file) {
    prepareSectionsAndLocals(file, /*ignoreComdats=*/false);
  });
  parallelForEach(ctx.objectFiles, finalizeObjectFile);
  parallelForEach(ctx.bitcodeFiles,
                  [](BitcodeFile *file) { file->postParse(); });
  for (auto &it : ctx.nonPrevailingSyms) {
    Symbol &sym = *it.first;
    Undefined(sym.file, sym.getName(), sym.binding, sym.stOther, sym.type,
              it.second)
        .overwrite(sym);
    cast<Undefined>(sym).nonPrevailing = true;
  }
  ctx.nonPrevailingSyms.clear();
  for (const DuplicateSymbol &d : ctx.duplicates)
    reportDuplicate(*d.sym, d.file, d.section, d.value);
  ctx.duplicates.clear();

  if (errorCount())
    return;

  script->declareSymbols();

  if (args.hasArg(OPT_exclude_libs))
    excludeLibs(args);

  Out::elfHeader = make<OutputSection>("", 0, SHF_ALLOC);

  if (!config->relocatable)
    addReservedSymbols();

  if (!config->relocatable) {
    llvm::TimeTraceScope timeScope("Process symbol versions");
    symtab.scanVersionScript();
  }

  const size_t numObjsBeforeLTO = ctx.objectFiles.size();
  dispatchByFormat(compileBitcodeFiles);

  reportBackrefs();
  writeArchiveStats();
  writeWhyExtract();
  if (errorCount())
    return;

  auto newObjectFiles = ArrayRef(ctx.objectFiles).slice(numObjsBeforeLTO);
  parallelForEach(newObjectFiles, [](ELFFileBase *file) {
    prepareSectionsAndLocals(file, /*ignoreComdats=*/true);
  });
  parallelForEach(newObjectFiles, finalizeObjectFile);
  for (const DuplicateSymbol &d : ctx.duplicates)
    reportDuplicate(*d.sym, d.file, d.section, d.value);

  // Relocatable merge (-r): use the shared neverc::merge module for
  // section merge, symbol resolution and reloc remap in a single
  // in-memory pass.
  if (config->relocatable) {
    llvm::TimeTraceScope timeScope("Relocatable merge");
    SmallVector<StringRef, 32> buffers;
    for (ELFFileBase *f : ctx.objectFiles)
      buffers.push_back(f->mb.getBuffer());

    neverc::merge::Options mergeOpts;
    mergeOpts.pureC = true;
    mergeOpts.dropDebugInfo = (config->strip != StripPolicy::None);

    std::error_code ec;
    raw_fd_ostream out(config->outputFile, ec, sys::fs::OF_None);
    if (ec) {
      error("cannot open " + config->outputFile + ": " + ec.message());
      return;
    }

    if (!neverc::merge::mergeObjects(buffers, out,
                                     neverc::merge::Format::ELF64LE, mergeOpts))
      error("relocatable merge failed");
    return;
  }

  if (args.hasArg(OPT_exclude_libs))
    excludeLibs(args);

  redirectSymbols(wrapped);
  replaceCommonSymbols();

  {
    llvm::TimeTraceScope timeScope("Aggregate sections");
    // Per-file buckets fill in parallel; single sequential merge.
    const size_t numFiles = ctx.objectFiles.size();
    std::vector<SmallVector<InputSectionBase *, 0>> perFileSections(numFiles);
    std::vector<SmallVector<EhInputSection *, 0>> perFileEh(numFiles);

    parallelFor(0, numFiles, [&](size_t i) {
      for (InputSectionBase *s : ctx.objectFiles[i]->getSections()) {
        if (!s || s == &InputSection::discarded)
          continue;
        if (LLVM_UNLIKELY(isa<EhInputSection>(s)))
          perFileEh[i].push_back(cast<EhInputSection>(s));
        else
          perFileSections[i].push_back(s);
      }
    });

    size_t totalSections = 0, totalEh = 0;
    for (size_t i = 0; i < numFiles; ++i) {
      totalSections += perFileSections[i].size();
      totalEh += perFileEh[i].size();
    }
    ctx.inputSections.reserve(ctx.inputSections.size() + totalSections);
    ctx.ehInputSections.reserve(ctx.ehInputSections.size() + totalEh);
    for (size_t i = 0; i < numFiles; ++i) {
      ctx.inputSections.append(perFileSections[i].begin(),
                               perFileSections[i].end());
      ctx.ehInputSections.append(perFileEh[i].begin(), perFileEh[i].end());
    }

    for (BinaryFile *f : ctx.binaryFiles)
      for (InputSectionBase *s : f->getSections())
        ctx.inputSections.push_back(cast<InputSection>(s));
  }

  {
    llvm::TimeTraceScope timeScope("Strip sections");
    const bool hasSympart = ctx.hasSympart.load(std::memory_order_relaxed);
    const bool stripDebug = config->strip != StripPolicy::None;

    if (hasSympart || stripDebug) {
      llvm::erase_if(ctx.inputSections, [&](InputSectionBase *s) {
        if (hasSympart && s->type == SHT_LLVM_SYMPART) {
          dispatchByFormat(readSymbolPartitionSection, s);
          return true;
        }
        if (stripDebug) {
          if (isDebugSection(*s))
            return true;
          if (auto *isec = dyn_cast<InputSection>(s))
            if (InputSectionBase *rel = isec->getRelocatedSection())
              if (isDebugSection(*rel))
                return true;
        }
        return false;
      });
    }
  }

  if (!config->dependencyFile.empty())
    writeDependencyFile();

  mainPart = &partitions[0];
  config->andFeatures = getAndFeatures();
  target = getTarget();

  config->eflags = target->calcEFlags();
  config->maxPageSize = getMaxPageSize(args);
  config->commonPageSize = getCommonPageSize(args);

  config->imageBase = getImageBase(args);

  dispatchByFormat(splitSections, );
  dispatchByFormat(markLive, );
  copySectionsIntoPartitions();

  if (canHaveMemtagGlobals()) {
    llvm::TimeTraceScope timeScope("Process memory tagged symbols");
    createTaggedSymbols(ctx.objectFiles);
  }

  dispatchByFormat(createSyntheticSections, );
  combineEhSections();

  {
    llvm::TimeTraceScope timeScope("Assign sections");
    script->processSectionCommands();
    script->addOrphanSections();
  }

  {
    llvm::TimeTraceScope timeScope("Merge/finalize input sections");
    SmallVector<OutputSection *, 0> osecs;
    for (SectionCommand *cmd : script->sectionCommands)
      if (auto *osd = dyn_cast<OutputDesc>(cmd))
        osecs.push_back(&osd->osec);
    parallelForEach(osecs,
                    [](OutputSection *os) { os->finalizeInputSections(); });
  }

  if (config->icf != ICFLevel::None) {
    dispatchByFormat(findKeepUniqueSections, args);
    dispatchByFormat(doIcf, );
  }

  if (config->callGraphProfileSort != CGProfileSortKind::None) {
    if (!config->callGraphOrderingFile.empty())
      if (std::optional<MemoryBufferRef> buffer =
              readFile(config->callGraphOrderingFile))
        readCallGraph(*buffer);
    dispatchByFormat(readCallGraphsFromObjectFiles, );
  }

  dispatchByFormat(writeOutput, );
}
