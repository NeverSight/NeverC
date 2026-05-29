#include "Linker/MachO/Config.h"
#include "Linker/MachO/Driver.h"
#include "Linker/MachO/Emit.h"
#include "Linker/MachO/ICF.h"
#include "Linker/MachO/InputFiles.h"
#include "Linker/MachO/LTO.h"
#include "Linker/MachO/MarkLive.h"
#include "Linker/MachO/OutputSection.h"
#include "Linker/MachO/OutputSegment.h"
#include "Linker/MachO/SectionPriorities.h"
#include "Linker/MachO/SymbolTable.h"
#include "Linker/MachO/Symbols.h"
#include "Linker/MachO/SyntheticSections.h"
#include "Linker/MachO/Target.h"
#include "Linker/MachO/UnwindInfoSection.h"

#include "Linker/Core/Driver/ArgList.h"
#include "Linker/Core/Driver/CommonLTOConfig.h"
#include "Linker/Core/Driver/Dispatcher.h"
#include "Linker/Core/Runtime/Allocator.h"
#include "Linker/Core/Runtime/Diagnostic.h"
#include "Linker/Core/Runtime/Session.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/LTO/LTO.h"
#include "llvm/Object/Archive.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Parallel.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/TextAPI/PackedVersion.h"

#include "neverc/Merge/Merger.h"
#include "neverc/Plugin/PluginLoader.h"
#include <algorithm>
#include <vector>

using namespace llvm;
using namespace llvm::MachO;
using namespace llvm::object;
using namespace llvm::opt;
using namespace llvm::sys;
using namespace linker;
using namespace linker::macho;

std::unique_ptr<Configuration> macho::config;
std::unique_ptr<DependencyTracker> macho::depTracker;

namespace {

HeaderFileType getOutputType(const LinkerDriverConfig &driverCfg) {
  if (driverCfg.relocatable)
    return MH_OBJECT;
  if (driverCfg.shared)
    return MH_DYLIB;
  if (driverCfg.bundle)
    return MH_BUNDLE;
  return MH_EXECUTE;
}

DenseMap<CachedHashStringRef, StringRef> resolvedLibraries;
std::optional<StringRef> findLibrary(StringRef name) {
  CachedHashStringRef key(name);
  auto entry = resolvedLibraries.find(key);
  if (entry != resolvedLibraries.end())
    return entry->second;

  auto doFind = [&] {
    if (config->searchDylibsFirst) {
      if (std::optional<StringRef> path =
              findPathCombination("lib" + name, config->librarySearchPaths,
                                  {".tbd", ".dylib", ".so"}))
        return path;
      return findPathCombination("lib" + name, config->librarySearchPaths,
                                 {".a"});
    }
    return findPathCombination("lib" + name, config->librarySearchPaths,
                               {".tbd", ".dylib", ".so", ".a"});
  };

  std::optional<StringRef> path = doFind();
  if (path)
    resolvedLibraries[key] = *path;

  return path;
}

DenseMap<CachedHashStringRef, StringRef> resolvedFrameworks;
std::optional<StringRef> findFramework(StringRef name) {
  CachedHashStringRef key(name);
  auto entry = resolvedFrameworks.find(key);
  if (entry != resolvedFrameworks.end())
    return entry->second;

  SmallString<260> symlink;
  StringRef suffix;
  std::tie(name, suffix) = name.split(",");
  for (StringRef dir : config->frameworkSearchPaths) {
    symlink = dir;
    path::append(symlink, name + ".framework", name);

    if (!suffix.empty()) {
      // NOTE: we must resolve the symlink before trying the suffixes, because
      // there are no symlinks for the suffixed paths.
      SmallString<260> location;
      if (!fs::real_path(symlink, location)) {
        // only append suffix if realpath() succeeds
        Twine suffixed = location + suffix;
        if (fs::exists(suffixed))
          return resolvedFrameworks[key] = saver().save(suffixed.str());
      }
      // Suffix lookup failed, fall through to the no-suffix case.
    }

    if (std::optional<StringRef> path = resolveDylibPath(symlink.str()))
      return resolvedFrameworks[key] = *path;
  }
  return {};
}

bool warnIfNotDirectory(StringRef option, StringRef path) {
  if (!fs::exists(path)) {
    warn("directory not found for option -" + option + path);
    return false;
  } else if (!fs::is_directory(path)) {
    warn("option -" + option + path + " references a non-directory path");
    return false;
  }
  return true;
}

std::vector<StringRef>
getSearchPaths(unsigned optionCode, InputArgList &args,
               const std::vector<StringRef> &roots,
               const SmallVector<StringRef, 2> &systemPaths) {
  std::vector<StringRef> paths;
  StringRef optionLetter{optionCode == OPT_F ? "F" : "L"};
  for (StringRef path : args::getStrings(args, optionCode)) {
    // NOTE: only absolute paths are re-rooted to syslibroot(s)
    bool found = false;
    if (path::is_absolute(path, path::Style::posix)) {
      for (StringRef root : roots) {
        SmallString<261> buffer(root);
        path::append(buffer, path);
        // Do not warn about paths that are computed via the syslib roots
        if (fs::is_directory(buffer)) {
          paths.push_back(saver().save(buffer.str()));
          found = true;
        }
      }
    }
    if (!found && warnIfNotDirectory(optionLetter, path))
      paths.push_back(path);
  }

  // `-Z` suppresses the standard "system" search paths.
  if (args.hasArg(OPT_Z))
    return paths;

  for (const StringRef &path : systemPaths) {
    for (const StringRef &root : roots) {
      SmallString<261> buffer(root);
      path::append(buffer, path);
      if (fs::is_directory(buffer))
        paths.push_back(saver().save(buffer.str()));
    }
  }
  return paths;
}

std::vector<StringRef>
getSystemLibraryRoots(const LinkerDriverConfig &driverCfg) {
  std::vector<StringRef> roots;
  if (!driverCfg.sysroot.empty())
    roots.push_back(saver().save(driverCfg.sysroot));
  // NOTE: the final `-syslibroot` being `/` will ignore all roots
  if (!roots.empty() && roots.back() == "/")
    roots.clear();
  // NOTE: roots can never be empty - add an empty root to simplify the library
  // and framework search path computation.
  if (roots.empty())
    roots.emplace_back("");
  return roots;
}

std::vector<StringRef>
getLibrarySearchPaths(InputArgList &args, const std::vector<StringRef> &roots) {
  return getSearchPaths(OPT_L, args, roots, {"/usr/lib", "/usr/local/lib"});
}

std::vector<StringRef>
getFrameworkSearchPaths(InputArgList &args,
                        const std::vector<StringRef> &roots) {
  return getSearchPaths(OPT_F, args, roots,
                        {"/Library/Frameworks", "/System/Library/Frameworks"});
}

enum class LoadType {
  CommandLine,      // Library was passed as a regular CLI argument
  CommandLineForce, // Library was passed via `-force_load`
  LCLinkerOption,   // Library was passed via LC_LINKER_OPTIONS
};

struct ArchiveFileInfo {
  ArchiveFile *file;
  bool isCommandLineLoad;
};

DenseMap<StringRef, ArchiveFileInfo> loadedArchives;

InputFile *addFile(StringRef path, LoadType loadType, bool isLazy = false,
                   bool isExplicit = true, bool isBundleLoader = false,
                   bool isForceHidden = false) {
  std::optional<MemoryBufferRef> buffer = readFile(path);
  if (!buffer)
    return nullptr;
  MemoryBufferRef mbref = *buffer;
  InputFile *newFile = nullptr;

  file_magic magic = identify_magic(mbref.getBuffer());
  switch (magic) {
  case file_magic::archive: {
    bool isCommandLineLoad = loadType != LoadType::LCLinkerOption;
    auto entry = loadedArchives.find(path);

    ArchiveFile *file;
    if (entry == loadedArchives.end()) {
      // No cached archive, we need to create a new one
      std::unique_ptr<object::Archive> archive = CHECK(
          object::Archive::create(mbref), path + ": failed to parse archive");

      if (!archive->isEmpty() && !archive->hasSymbolTable())
        error(path + ": archive has no index; run ranlib to add one");
      file = make<ArchiveFile>(std::move(archive), isForceHidden);
    } else {
      file = entry->second.file;
      // Command-line loads take precedence. If file is previously loaded via
      // command line, or is loaded via LC_LINKER_OPTION and being loaded via
      // LC_LINKER_OPTION again, using the cached archive is enough.
      if (entry->second.isCommandLineLoad || !isCommandLineLoad)
        return file;
    }

    if ((isCommandLineLoad && config->allLoad) ||
        loadType == LoadType::CommandLineForce) {
      if (readFile(path)) {
        Error e = Error::success();
        for (const object::Archive::Child &c : file->getArchive().children(e)) {
          StringRef reason;
          switch (loadType) {
          case LoadType::LCLinkerOption:
            reason = "LC_LINKER_OPTION";
            break;
          case LoadType::CommandLineForce:
            reason = "--force-load";
            break;
          case LoadType::CommandLine:
            reason = "--all-load";
            break;
          }
          if (Error e = file->fetch(c, reason))
            error(toString(file) + ": " + reason +
                  " failed to load archive member: " + toString(std::move(e)));
        }
        if (e)
          error(toString(file) +
                ": Archive::children failed: " + toString(std::move(e)));
      }
    }

    file->addLazySymbols();
    loadedArchives[path] = ArchiveFileInfo{file, isCommandLineLoad};
    newFile = file;
    break;
  }
  case file_magic::macho_object:
    newFile = make<ObjFile>(mbref, getModTime(path), "", isLazy);
    break;
  case file_magic::macho_dynamically_linked_shared_lib:
  case file_magic::macho_dynamically_linked_shared_lib_stub:
  case file_magic::tapi_file:
    if (DylibFile *dylibFile =
            loadDylib(mbref, nullptr, /*isBundleLoader=*/false, isExplicit))
      newFile = dylibFile;
    break;
  case file_magic::bitcode:
    newFile = make<BitcodeFile>(mbref, "", 0, isLazy);
    break;
  case file_magic::macho_executable:
  case file_magic::macho_bundle:
    // We only allow executable and bundle type here if it is used
    // as a bundle loader.
    if (!isBundleLoader)
      error(path + ": unhandled file type");
    if (DylibFile *dylibFile = loadDylib(mbref, nullptr, isBundleLoader))
      newFile = dylibFile;
    break;
  default:
    error(path + ": unhandled file type");
  }
  if (newFile && !isa<DylibFile>(newFile)) {
    // printArchiveMemberLoad() prints both .a and .o names, so no need to
    // print the .a name here. Similarly skip lazy files.
    if (config->printEachFile && magic != file_magic::archive && !isLazy)
      message(toString(newFile));
    inputFiles.insert(newFile);
  }
  return newFile;
}

std::vector<StringRef> missingAutolinkWarnings;
void addLibrary(StringRef name, bool isNeeded, bool isWeak, bool isReexport,
                bool isHidden, bool isExplicit, LoadType loadType) {
  if (std::optional<StringRef> path = findLibrary(name)) {
    if (auto *dylibFile = dyn_cast_or_null<DylibFile>(
            addFile(*path, loadType, /*isLazy=*/false, isExplicit,
                    /*isBundleLoader=*/false, isHidden))) {
      if (isNeeded)
        dylibFile->forceNeeded = true;
      if (isWeak)
        dylibFile->forceWeakImport = true;
      if (isReexport) {
        config->hasReexports = true;
        dylibFile->reexport = true;
      }
    }
    return;
  }
  if (loadType == LoadType::LCLinkerOption) {
    missingAutolinkWarnings.push_back(
        saver().save("auto-linked library not found for -l" + name));
    return;
  }
  error("library not found for -l" + name);
}

DenseSet<StringRef> loadedObjectFrameworks;
void addFramework(StringRef name, bool isNeeded, bool isWeak, bool isReexport,
                  bool isExplicit, LoadType loadType) {
  if (std::optional<StringRef> path = findFramework(name)) {
    if (loadedObjectFrameworks.contains(*path))
      return;

    InputFile *file =
        addFile(*path, loadType, /*isLazy=*/false, isExplicit, false);
    if (auto *dylibFile = dyn_cast_or_null<DylibFile>(file)) {
      if (isNeeded)
        dylibFile->forceNeeded = true;
      if (isWeak)
        dylibFile->forceWeakImport = true;
      if (isReexport) {
        config->hasReexports = true;
        dylibFile->reexport = true;
      }
    } else if (isa_and_nonnull<ObjFile>(file) ||
               isa_and_nonnull<BitcodeFile>(file)) {
      // Cache frameworks containing object or bitcode files to avoid duplicate
      // symbols. Frameworks containing static archives are cached separately
      // in addFile() to share caching with libraries, and frameworks
      // containing dylibs should allow overwriting of attributes such as
      // forceNeeded by subsequent loads
      loadedObjectFrameworks.insert(*path);
    }
    return;
  }
  if (loadType == LoadType::LCLinkerOption) {
    missingAutolinkWarnings.push_back(saver().save(
        "auto-linked framework not found for --framework " + name));
    return;
  }
  error("framework not found for --framework " + name);
}

} // namespace

// ===----------------------------------------------------------------------===
// LC_LINKER_OPTION processing
// ===----------------------------------------------------------------------===

void macho::parseLCLinkerOption(
    llvm::SmallVectorImpl<StringRef> &LCLinkerOptions, InputFile *f,
    unsigned argc, StringRef data) {
  if (config->ignoreAutoLink)
    return;

  SmallVector<StringRef, 4> argv;
  size_t offset = 0;
  for (unsigned i = 0; i < argc && offset < data.size(); ++i) {
    argv.push_back(data.data() + offset);
    offset += strlen(data.data() + offset) + 1;
  }
  if (argv.size() != argc || offset > data.size())
    fatal(toString(f) + ": invalid LC_LINKER_OPTION");

  unsigned i = 0;
  StringRef arg = argv[i];
  if (arg.consume_front("-l")) {
    if (config->ignoreAutoLinkOptions.contains(arg))
      return;
  } else if (arg == "-framework") {
    StringRef name = argv[++i];
    if (config->ignoreAutoLinkOptions.contains(name))
      return;
  } else {
    error(arg + " is not allowed in LC_LINKER_OPTION");
  }

  LCLinkerOptions.append(argv);
}

void macho::resolveLCLinkerOptions() {
  while (!unprocessedLCLinkerOptions.empty()) {
    SmallVector<StringRef> LCLinkerOptions(unprocessedLCLinkerOptions);
    unprocessedLCLinkerOptions.clear();

    for (unsigned i = 0; i < LCLinkerOptions.size(); ++i) {
      StringRef arg = LCLinkerOptions[i];
      if (arg.consume_front("-l")) {
        assert(!config->ignoreAutoLinkOptions.contains(arg));
        addLibrary(arg, /*isNeeded=*/false, /*isWeak=*/false,
                   /*isReexport=*/false, /*isHidden=*/false,
                   /*isExplicit=*/false, LoadType::LCLinkerOption);
      } else if (arg == "-framework") {
        StringRef name = LCLinkerOptions[++i];
        assert(!config->ignoreAutoLinkOptions.contains(name));
        addFramework(name, /*isNeeded=*/false, /*isWeak=*/false,
                     /*isReexport=*/false, /*isExplicit=*/false,
                     LoadType::LCLinkerOption);
      } else {
        error(arg + " is not allowed in LC_LINKER_OPTION");
      }
    }
  }
}

// ===----------------------------------------------------------------------===
// Build pipeline
// ===----------------------------------------------------------------------===

namespace {

void addFileList(StringRef path, bool isLazy) {
  std::optional<MemoryBufferRef> buffer = readFile(path);
  if (!buffer)
    return;
  MemoryBufferRef mbref = *buffer;
  for (StringRef path : args::getLines(mbref))
    addFile(rerootPath(path), LoadType::CommandLine, isLazy);
}

void prefetchInputFiles(const InputArgList &args) {
  TimeTraceScope timeScope("Prefetch input files");
  SmallVector<StringRef, 0> paths;
  paths.reserve(args.size());

  auto pushPath = [&](StringRef path) {
    if (path.empty())
      return;
    paths.push_back(saver().save(rerootPath(path)));
  };

  for (const Arg *arg : args) {
    const Option &opt = arg->getOption();
    switch (opt.getID()) {
    case OPT_INPUT:
    case OPT_needed_library:
    case OPT_reexport_library:
    case OPT_weak_library:
    case OPT_force_load:
    case OPT_load_hidden:
      pushPath(arg->getValue());
      break;
    case OPT_filelist: {
      std::optional<MemoryBufferRef> buffer =
          readFile(arg->getValue(), /*reportError=*/false);
      if (!buffer)
        break;
      for (StringRef path : args::getLines(*buffer))
        pushPath(path);
      break;
    }
    default:
      break;
    }
  }

  if (paths.empty())
    return;

  DenseSet<StringRef> seen;
  SmallVector<StringRef, 0> uniquePaths;
  uniquePaths.reserve(paths.size());
  for (StringRef path : paths)
    if (seen.insert(path).second)
      uniquePaths.push_back(path);

  if (parallel::strategy.ThreadsRequested == 1 || uniquePaths.size() < 8) {
    for (StringRef path : uniquePaths)
      (void)readFile(path, /*reportError=*/false);
    return;
  }

  parallelForEach(uniquePaths, [](StringRef path) {
    (void)readFile(path, /*reportError=*/false);
  });
}

// Match sub-library name "libfoo" against any loaded dylib whose filename is
// "libfoo" plus one of the accepted extensions (.dylib / .tbd / none).
bool markReexport(StringRef searchName, ArrayRef<StringRef> extensions) {
  for (InputFile *file : inputFiles) {
    if (auto *dylibFile = dyn_cast<DylibFile>(file)) {
      StringRef filename = path::filename(dylibFile->getName());
      if (filename.consume_front(searchName) &&
          (filename.empty() || llvm::is_contained(extensions, filename))) {
        dylibFile->reexport = true;
        return true;
      }
    }
  }
  return false;
}

// This function is called on startup. We need this for LTO since
// LTO calls LLVM functions to compile bitcode files to native code.
// Technically this can be delayed until we read bitcode files, but
// we don't bother to do lazily because the initialization is fast.
void initLLVM() {
  InitializeAllTargets();
  InitializeAllTargetMCs();
  InitializeAllAsmPrinters();
  InitializeAllAsmParsers();
}

bool compileBitcodeFiles() {
  TimeTraceScope timeScope("LTO");

  auto *lto = make<BitcodeCompiler>();
  SmallVector<BitcodeFile *, 0> bcFiles;
  for (InputFile *file : inputFiles)
    if (auto *bitcodeFile = dyn_cast<BitcodeFile>(file))
      if (!file->lazy)
        bcFiles.push_back(bitcodeFile);
  lto->addBatch(bcFiles);

  std::vector<ObjFile *> compiled = lto->compile();
  for (ObjFile *file : compiled)
    inputFiles.insert(file);

  return !compiled.empty();
}

// Replaces common symbols with defined symbols residing in __common sections.
// This function must be called after all symbol names are resolved (i.e. after
// all InputFiles have been loaded.) As a result, later operations won't see
// any CommonSymbols.
void replaceCommonSymbols() {
  TimeTraceScope timeScope("Replace common symbols");
  ConcatOutputSection *osec = nullptr;
  for (Symbol *sym : symtab->getSymbols()) {
    auto *common = dyn_cast<CommonSymbol>(sym);
    if (common == nullptr)
      continue;

    ArrayRef<uint8_t> data = {nullptr, static_cast<size_t>(common->size)};
    auto *section =
        make<Section>(common->getFile(), segment_names::data,
                      section_names::common, S_ZEROFILL, /*addr=*/0);
    auto *isec = make<ConcatInputSection>(*section, data, common->align);
    if (!osec)
      osec = ConcatOutputSection::getOrCreateForInput(isec);
    isec->parent = osec;
    inputSections.push_back(isec);

    replaceSymbol<Defined>(
        sym, sym->getName(), common->getFile(), isec, /*value=*/0, common->size,
        /*isWeakDef=*/false, /*isExternal=*/true, common->privateExtern,
        /*includeInSymtab=*/true, /*isReferencedDynamically=*/false,
        /*noDeadStrip=*/false);
  }
}

void initializeSectionRenameMap() {
  if (config->dataConst) {
    SmallVector<StringRef> v{section_names::got,
                             section_names::authGot,
                             section_names::authPtr,
                             section_names::nonLazySymbolPtr,
                             section_names::const_,
                             section_names::cfString,
                             section_names::moduleInitFunc,
                             section_names::moduleTermFunc};
    for (StringRef s : v)
      config->sectionRenameMap[{segment_names::data, s}] = {
          segment_names::dataConst, s};
  }
  config->sectionRenameMap[{segment_names::text, section_names::staticInit}] = {
      segment_names::text, section_names::text};
  config->sectionRenameMap[{segment_names::import, section_names::pointers}] = {
      config->dataConst ? segment_names::dataConst : segment_names::data,
      section_names::nonLazySymbolPtr};
}

inline char toLowerDash(char x) {
  if (x >= 'A' && x <= 'Z')
    return x - 'A' + 'a';
  else if (x == ' ')
    return '-';
  return x;
}

std::string lowerDash(StringRef s) {
  return std::string(map_iterator(s.begin(), toLowerDash),
                     map_iterator(s.end(), toLowerDash));
}

// Has the side-effect of setting Config::platformInfo.
void setPlatformVersions(StringRef archName,
                         const LinkerDriverConfig &driverCfg) {
  if (driverCfg.platformName.empty()) {
    if (driverCfg.relocatable)
      return;
    error("must specify platform version (driver bug: platformName empty)");
    return;
  }

  std::string normalized = lowerDash(driverCfg.platformName);
  if (normalized != "macos" && normalized != "1") {
    error(Twine("unsupported platform (only macos is supported): ") +
          driverCfg.platformName);
    return;
  }
  llvm::VersionTuple minimum;
  llvm::VersionTuple sdk;
  if (minimum.tryParse(driverCfg.platformMinVersion))
    error(Twine("malformed minimum version: ") + driverCfg.platformMinVersion);
  if (sdk.tryParse(driverCfg.platformSdkVersion))
    error(Twine("malformed sdk version: ") + driverCfg.platformSdkVersion);

  config->platformInfo = {
      MachO::Target(getArchitectureFromName(archName), PLATFORM_MACOS, minimum),
      sdk};
}

// Has the side-effect of setting Config::target.
TargetInfo *createTargetInfo(const LinkerDriverConfig &driverCfg) {
  StringRef archName = driverCfg.archName;
  if (archName.empty()) {
    error("must specify -arch (driver bug: archName empty)");
    return nullptr;
  }
  if (!is_contained({StringRef("x86_64"), StringRef("arm64")}, archName)) {
    error("missing or unsupported -arch " + archName);
    return nullptr;
  }

  setPlatformVersions(archName, driverCfg);
  auto [cpuType, cpuSubtype] = getCPUTypeFromArchitecture(config->arch());
  switch (cpuType) {
  case CPU_TYPE_X86_64:
    return createX86_64TargetInfo();
  case CPU_TYPE_ARM64:
    return createARM64TargetInfo();
  default:
    error("missing or unsupported -arch " + archName);
    return nullptr;
  }
}

UndefinedSymbolTreatment getUndefinedSymbolTreatment(const ArgList &args) {
  StringRef treatmentStr = args.getLastArgValue(OPT_undefined);
  auto treatment =
      StringSwitch<UndefinedSymbolTreatment>(treatmentStr)
          .Cases("error", "", UndefinedSymbolTreatment::error)
          .Case("warning", UndefinedSymbolTreatment::warning)
          .Case("suppress", UndefinedSymbolTreatment::suppress)
          .Case("dynamic_lookup", UndefinedSymbolTreatment::dynamic_lookup)
          .Default(UndefinedSymbolTreatment::unknown);
  if (treatment == UndefinedSymbolTreatment::unknown) {
    warn(Twine("unknown -undefined TREATMENT '") + treatmentStr +
         "', defaulting to 'error'");
    treatment = UndefinedSymbolTreatment::error;
  } else if (config->namespaceKind == NamespaceKind::twolevel &&
             (treatment == UndefinedSymbolTreatment::warning ||
              treatment == UndefinedSymbolTreatment::suppress)) {
    if (treatment == UndefinedSymbolTreatment::warning)
      fatal("'-undefined warning' only valid with '--flat-namespace'");
    else
      fatal("'-undefined suppress' only valid with '--flat-namespace'");
    treatment = UndefinedSymbolTreatment::error;
  }
  return treatment;
}

ICFLevel getICFFromDriver(int driverLevel) {
  if (driverLevel >= 2)
    return ICFLevel::all;
  if (driverLevel == 1)
    return ICFLevel::safe;
  return ICFLevel::none;
}

void warnIfDeprecatedOption(const Option &) {}

void warnIfUnimplementedOption(const Option &opt) {
  if (!opt.getGroup().isValid() || !opt.hasFlag(DriverFlag::HelpHidden))
    return;
  if (opt.getGroup().getID() == OPT_grp_undocumented)
    warn("Option `" + opt.getPrefixedName() +
         "' is undocumented. Should the linker implement it?");
  else
    warn("Option `" + opt.getPrefixedName() +
         "' is not yet implemented. Stay tuned...");
}

uint32_t parseDylibVersion(const ArgList &args, unsigned id) {
  const Arg *arg = args.getLastArg(id);
  if (!arg)
    return 0;

  if (config->outputType != MH_DYLIB) {
    error(arg->getAsString(args) + ": only valid with -dylib");
    return 0;
  }

  PackedVersion version;
  if (!version.parse32(arg->getValue())) {
    error(arg->getAsString(args) + ": malformed version");
    return 0;
  }

  return version.rawValue();
}

uint32_t parseProtection(StringRef protStr) {
  uint32_t prot = 0;
  for (char c : protStr) {
    switch (c) {
    case 'r':
      prot |= VM_PROT_READ;
      break;
    case 'w':
      prot |= VM_PROT_WRITE;
      break;
    case 'x':
      prot |= VM_PROT_EXECUTE;
      break;
    case '-':
      break;
    default:
      error("unknown -segprot letter '" + Twine(c) + "' in " + protStr);
      return 0;
    }
  }
  return prot;
}

std::vector<SectionAlign> parseSectAlign(const opt::InputArgList &args) {
  std::vector<SectionAlign> sectAligns;
  for (const Arg *arg : args.filtered(OPT_sectalign)) {
    StringRef segName = arg->getValue(0);
    StringRef sectName = arg->getValue(1);
    StringRef alignStr = arg->getValue(2);
    if (alignStr.starts_with("0x") || alignStr.starts_with("0X"))
      alignStr = alignStr.drop_front(2);
    uint32_t align;
    if (alignStr.getAsInteger(16, align)) {
      error("-sectalign: failed to parse '" + StringRef(arg->getValue(2)) +
            "' as number");
      continue;
    }
    if (!isPowerOf2_32(align)) {
      error("-sectalign: '" + StringRef(arg->getValue(2)) +
            "' (in base 16) not a power of two");
      continue;
    }
    sectAligns.push_back({segName, sectName, align});
  }
  return sectAligns;
}

bool supportsNoPie() { return config->arch() != AK_arm64; }

bool shouldAdhocSignByDefault(Architecture arch, PlatformType platform) {
  if (arch != AK_arm64)
    return false;

  return platform == PLATFORM_MACOS;
}

bool dataConstDefault() {
  PlatformType platform = config->platformInfo.target.Platform;
  if (platform == PLATFORM_MACOS &&
      config->platformInfo.target.MinDeployment < VersionTuple(10, 15))
    return false;

  switch (config->outputType) {
  case MH_EXECUTE:
    return config->isPic;
  case MH_BUNDLE:
    return true;
  case MH_DYLIB:
    return true;
  case MH_OBJECT:
    return false;
  default:
    llvm_unreachable(
        "unsupported output type for determining data-const default");
  }
  return false;
}

bool shouldEmitChainedFixups(const InputArgList &args) {
  const Arg *arg = args.getLastArg(OPT_fixup_chains, OPT_no_fixup_chains);
  if (arg && arg->getOption().matches(OPT_no_fixup_chains))
    return false;

  bool isRequested = arg != nullptr;

  // Version numbers taken from the Xcode 13.3 release notes.
  PlatformType platform = config->platformInfo.target.Platform;
  if (platform == PLATFORM_MACOS) {
    VersionTuple required(11, 0);
    if (required > config->platformInfo.target.MinDeployment) {
      if (!isRequested)
        return false;
      warn("--fixup-chains requires " + getPlatformName(platform) + " " +
           required.getAsString() + ", which is newer than target minimum of " +
           config->platformInfo.target.MinDeployment.getAsString());
    }
  }

  if (!is_contained({AK_x86_64, AK_arm64}, config->arch())) {
    if (isRequested)
      error("--fixup-chains is only supported on x86_64 and arm64 targets");
    return false;
  }

  if (!config->isPic) {
    if (isRequested)
      error("--fixup-chains is incompatible with -no_pie");
    return false;
  }

  return isRequested;
}

} // namespace

// ===----------------------------------------------------------------------===
// Symbol patterns
// ===----------------------------------------------------------------------===

void SymbolPatterns::clear() {
  literals.clear();
  globs.clear();
}

void SymbolPatterns::insert(StringRef symbolName) {
  if (symbolName.find_first_of("*?[]") == StringRef::npos)
    literals.insert(CachedHashStringRef(symbolName));
  else if (Expected<GlobPattern> pattern = GlobPattern::create(symbolName))
    globs.emplace_back(*pattern);
  else
    error("invalid symbol-name pattern: " + symbolName);
}

bool SymbolPatterns::matchLiteral(StringRef symbolName) const {
  return literals.contains(CachedHashStringRef(symbolName));
}

bool SymbolPatterns::matchGlob(StringRef symbolName) const {
  for (const GlobPattern &glob : globs)
    if (glob.match(symbolName))
      return true;
  return false;
}

bool SymbolPatterns::match(StringRef symbolName) const {
  return matchLiteral(symbolName) || matchGlob(symbolName);
}

// ===----------------------------------------------------------------------===
// File creation & linking pipeline
// ===----------------------------------------------------------------------===

namespace {

void parseSymbolPatternsFile(const Arg *arg, SymbolPatterns &symbolPatterns) {
  StringRef path = arg->getValue();
  std::optional<MemoryBufferRef> buffer = readFile(path);
  if (!buffer) {
    error("Could not read symbol file: " + path);
    return;
  }
  MemoryBufferRef mbref = *buffer;
  for (StringRef line : args::getLines(mbref)) {
    line = line.take_until([](char c) { return c == '#'; }).trim();
    if (!line.empty())
      symbolPatterns.insert(line);
  }
}

void handleSymbolPatterns(InputArgList &args, SymbolPatterns &symbolPatterns,
                          unsigned singleOptionCode,
                          unsigned listFileOptionCode) {
  for (const Arg *arg : args.filtered(singleOptionCode))
    symbolPatterns.insert(arg->getValue());
  for (const Arg *arg : args.filtered(listFileOptionCode))
    parseSymbolPatternsFile(arg, symbolPatterns);
}

void createFiles(const InputArgList &args) {
  TimeTraceScope timeScope("Load input files");
  prefetchInputFiles(args);
  // This loop should be reserved for options whose exact ordering matters.
  // Other options should be handled via filtered() and/or getLastArg().
  bool isLazy = false;
  for (const Arg *arg : args) {
    const Option &opt = arg->getOption();
    warnIfDeprecatedOption(opt);
    warnIfUnimplementedOption(opt);

    switch (opt.getID()) {
    case OPT_INPUT:
      addFile(rerootPath(arg->getValue()), LoadType::CommandLine, isLazy);
      break;
    case OPT_needed_library:
      if (auto *dylibFile = dyn_cast_or_null<DylibFile>(
              addFile(rerootPath(arg->getValue()), LoadType::CommandLine)))
        dylibFile->forceNeeded = true;
      break;
    case OPT_reexport_library:
      if (auto *dylibFile = dyn_cast_or_null<DylibFile>(
              addFile(rerootPath(arg->getValue()), LoadType::CommandLine))) {
        config->hasReexports = true;
        dylibFile->reexport = true;
      }
      break;
    case OPT_weak_library:
      if (auto *dylibFile = dyn_cast_or_null<DylibFile>(
              addFile(rerootPath(arg->getValue()), LoadType::CommandLine)))
        dylibFile->forceWeakImport = true;
      break;
    case OPT_filelist:
      addFileList(arg->getValue(), isLazy);
      break;
    case OPT_force_load:
      addFile(rerootPath(arg->getValue()), LoadType::CommandLineForce);
      break;
    case OPT_load_hidden:
      addFile(rerootPath(arg->getValue()), LoadType::CommandLine,
              /*isLazy=*/false, /*isExplicit=*/true, /*isBundleLoader=*/false,
              /*isForceHidden=*/true);
      break;
    case OPT_l:
    case OPT_needed_l:
    case OPT_reexport_l:
    case OPT_weak_l:
    case OPT_hidden_l:
      addLibrary(arg->getValue(), opt.getID() == OPT_needed_l,
                 opt.getID() == OPT_weak_l, opt.getID() == OPT_reexport_l,
                 opt.getID() == OPT_hidden_l,
                 /*isExplicit=*/true, LoadType::CommandLine);
      break;
    case OPT_framework:
    case OPT_needed_framework:
    case OPT_reexport_framework:
    case OPT_weak_framework:
      addFramework(arg->getValue(), opt.getID() == OPT_needed_framework,
                   opt.getID() == OPT_weak_framework,
                   opt.getID() == OPT_reexport_framework, /*isExplicit=*/true,
                   LoadType::CommandLine);
      break;
    case OPT_start_lib:
      if (isLazy)
        error("nested --start-lib");
      isLazy = true;
      break;
    case OPT_end_lib:
      if (!isLazy)
        error("stray --end-lib");
      isLazy = false;
      break;
    default:
      break;
    }
  }
}

struct GatheredInputAction {
  enum class Kind {
    Concat,
    InitOffset,
    CString,
    WordLiteral,
  };

  Kind kind;
  InputSection *isec;
  const Section *sourceSection;
};

struct GatheredFileSections {
  SmallVector<GatheredInputAction, 0> actions;
};

void gatherInputSections() {
  TimeTraceScope timeScope("Gathering input sections");

  SmallVector<const InputFile *> files(inputFiles.begin(), inputFiles.end());
  std::vector<GatheredFileSections> gatheredFiles(files.size());

  auto gatherFile = [&](size_t idx) {
    const InputFile *file = files[idx];
    GatheredFileSections &gathered = gatheredFiles[idx];
    for (const Section *section : file->sections) {
      if (section->name == section_names::compactUnwind)
        continue;
      for (const Subsection &subsection : section->subsections) {
        if (auto *isec = dyn_cast<ConcatInputSection>(subsection.isec)) {
          if (isec->isCoalescedWeak())
            continue;
          if (config->emitInitOffsets &&
              sectionType(isec->getFlags()) == S_MOD_INIT_FUNC_POINTERS) {
            gathered.actions.push_back(
                {GatheredInputAction::Kind::InitOffset, isec, nullptr});
            continue;
          }
          gathered.actions.push_back(
              {GatheredInputAction::Kind::Concat, isec, section});
        } else if (auto *isec =
                       dyn_cast<CStringInputSection>(subsection.isec)) {
          gathered.actions.push_back(
              {GatheredInputAction::Kind::CString, isec, nullptr});
        } else if (auto *isec =
                       dyn_cast<WordLiteralInputSection>(subsection.isec)) {
          gathered.actions.push_back(
              {GatheredInputAction::Kind::WordLiteral, isec, nullptr});
        } else {
          llvm_unreachable("unexpected input section kind");
        }
      }
    }
  };

  parallelFor(0, files.size(), gatherFile);

  int inputOrder = 0;
  for (size_t fileIdx = 0; fileIdx < files.size(); ++fileIdx) {
    const GatheredFileSections &gathered = gatheredFiles[fileIdx];
    ConcatOutputSection *osec = nullptr;
    const Section *currentConcatSection = nullptr;

    for (const GatheredInputAction &action : gathered.actions) {
      switch (action.kind) {
      case GatheredInputAction::Kind::Concat: {
        auto *isec = cast<ConcatInputSection>(action.isec);
        isec->outSecOff = inputOrder++;
        if (action.sourceSection != currentConcatSection) {
          currentConcatSection = action.sourceSection;
          osec = ConcatOutputSection::getOrCreateForInput(isec);
        }
        isec->parent = osec;
        inputSections.push_back(isec);
        break;
      }
      case GatheredInputAction::Kind::InitOffset:
        in.initOffsets->addInput(cast<ConcatInputSection>(action.isec));
        break;
      case GatheredInputAction::Kind::CString: {
        auto *isec = cast<CStringInputSection>(action.isec);
        if (in.cStringSection->inputOrder == UnspecifiedInputOrder)
          in.cStringSection->inputOrder = inputOrder++;
        in.cStringSection->addInput(isec);
        break;
      }
      case GatheredInputAction::Kind::WordLiteral: {
        auto *isec = cast<WordLiteralInputSection>(action.isec);
        if (in.wordLiteralSection->inputOrder == UnspecifiedInputOrder)
          in.wordLiteralSection->inputOrder = inputOrder++;
        in.wordLiteralSection->addInput(isec);
        break;
      }
      }
    }
  }
  assert(inputOrder <= UnspecifiedInputOrder);
}

void foldIdenticalLiterals() {
  TimeTraceScope timeScope("Fold identical literals");
  // We always create a cStringSection, regardless of whether dedupLiterals is
  // true. If it isn't, we simply create a non-deduplicating CStringSection.
  // Either way, we must unconditionally finalize it here.
  // The two sections are independent, so finalize them in parallel.
  parallelFor(0, 2, [](size_t i) {
    switch (i) {
    case 0:
      in.cStringSection->finalizeContents();
      break;
    case 1:
      in.wordLiteralSection->finalizeContents();
      break;
    default:
      llvm_unreachable("unexpected literal section index");
    }
  });
}

void referenceStubBinder() {
  bool needsStubHelper = config->outputType == MH_DYLIB ||
                         config->outputType == MH_EXECUTE ||
                         config->outputType == MH_BUNDLE;
  if (!needsStubHelper || !symtab->find("dyld_stub_binder"))
    return;

  // dyld_stub_binder is used by dyld to resolve lazy bindings. This code here
  // adds a opportunistic reference to dyld_stub_binder if it happens to exist.
  // dyld_stub_binder is in libSystem.dylib, which is usually linked in. This
  // isn't needed for correctness, but the presence of that symbol suppresses
  // "no symbols" diagnostics from `nm`.
  // StubHelperSection::setUp() adds a reference and errors out if
  // dyld_stub_binder doesn't exist in case it is actually needed.
  symtab->addUndefined("dyld_stub_binder", /*file=*/nullptr, /*isWeak=*/false);
}

void createAliases() {
  for (const auto &pair : config->aliasedSymbols) {
    if (const auto &sym = symtab->find(pair.first)) {
      if (const auto &defined = dyn_cast<Defined>(sym)) {
        symtab->aliasDefined(defined, pair.second, defined->getFile())
            ->noDeadStrip = true;
      } else {
        error("unsupported aliasing to symbols of kind " + Twine(sym->kind()));
      }
    } else {
      warn("undefined base symbol '" + pair.first + "' for alias '" +
           pair.second + "'\n");
    }
  }

  for (const InputFile *file : inputFiles) {
    if (auto *objFile = dyn_cast<ObjFile>(file)) {
      for (const AliasSymbol *alias : objFile->aliases) {
        if (const auto &aliased = symtab->find(alias->getAliasedName())) {
          if (const auto &defined = dyn_cast<Defined>(aliased)) {
            symtab->aliasDefined(defined, alias->getName(), alias->getFile(),
                                 alias->privateExtern);
          } else {
            // Common, dylib, and undefined symbols are all valid alias
            // referents (undefineds can become valid Defined symbols later on
            // in the link.)
            error("unsupported aliasing to symbols of kind " +
                  Twine(aliased->kind()));
          }
        } else {
          // This shouldn't happen since MC generates undefined symbols to
          // represent the alias referents. Thus we fatal() instead of just
          // warning here.
          fatal("unable to find alias referent " + alias->getAliasedName() +
                " for " + alias->getName());
        }
      }
    }
  }
}

void handleExplicitExports() {
  static constexpr int kMaxWarnings = 3;
  if (config->hasExplicitExports) {
    std::atomic<uint64_t> warningsCount{0};
    parallelForEach(symtab->getSymbols(), [&warningsCount](Symbol *sym) {
      if (auto *defined = dyn_cast<Defined>(sym)) {
        if (config->exportedSymbols.match(sym->getName())) {
          if (defined->privateExtern) {
            if (defined->weakDefCanBeHidden) {
              // weak_def_can_be_hidden symbols behave similarly to
              // private_extern symbols in most cases, except for when
              // it is explicitly exported.
              // The former can be exported but the latter cannot.
              defined->privateExtern = false;
            } else {
              // Only print the first 3 warnings verbosely, and
              // shorten the rest to avoid crowding logs.
              if (warningsCount.fetch_add(1, std::memory_order_relaxed) <
                  kMaxWarnings)
                warn("cannot export hidden symbol " + toString(*defined) +
                     "\n>>> defined in " + toString(defined->getFile()));
            }
          }
        } else {
          defined->privateExtern = true;
        }
      } else if (auto *dysym = dyn_cast<DylibSymbol>(sym)) {
        dysym->shouldReexport = config->exportedSymbols.match(sym->getName());
      }
    });
    if (warningsCount > kMaxWarnings)
      warn("<... " + Twine(warningsCount - kMaxWarnings) +
           " more similar warnings...>");
  } else if (!config->unexportedSymbols.empty()) {
    parallelForEach(symtab->getSymbols(), [](Symbol *sym) {
      if (auto *defined = dyn_cast<Defined>(sym))
        if (config->unexportedSymbols.match(defined->getName()))
          defined->privateExtern = true;
    });
  }
}

} // namespace

// ===----------------------------------------------------------------------===
// Link entry point
// ===----------------------------------------------------------------------===

namespace linker {
namespace macho {
bool link(ArrayRef<const char *> argsArr, llvm::raw_ostream &stdoutOS,
          llvm::raw_ostream &stderrOS, bool exitEarly, bool disableOutput,
          const LinkerDriverConfig &driverCfg) {
  auto *ctx = new CommonLinkerContext;

  ctx->e.initialize(stdoutOS, stderrOS, exitEarly, disableOutput);
  ctx->e.cleanupCallback = []() {
    resolvedFrameworks.clear();
    resolvedLibraries.clear();
    cachedReads.clear();
    concatOutputSections.clear();
    inputFiles.clear();
    inputSections.clear();
    loadedArchives.clear();
    loadedObjectFrameworks.clear();
    missingAutolinkWarnings.clear();
    syntheticSections.clear();
    thunkMap.clear();
    unprocessedLCLinkerOptions.clear();

    firstTLVDataSection = nullptr;
    memset(&in, 0, sizeof(in));

    resetLoadedDylibs();
    resetOutputSegments();
    resetEmitState();
    InputFile::resetIdCount();
  };

  ctx->e.logName = args::getFilenameWithoutExe(argsArr[0]);

  MachOOptTable parser;
  InputArgList args = parser.parse(argsArr.slice(1));

  ctx->e.errorLimitExceededMsg = "too many errors emitted, stopping now "
                                 "(use -ferror-limit=0 to see all errors)";
  ctx->e.errorLimit = driverCfg.errorLimit;
  ctx->e.verbose = driverCfg.verbose;
  ctx->e.fatalWarnings = driverCfg.fatalWarnings;
  ctx->e.suppressWarnings = driverCfg.suppressWarnings;

  for (const std::string &Path : driverCfg.nevercPluginPaths) {
    std::string Err;
    if (!neverc::plugin::getGlobalPluginLoader().loadPlugin(Path, Err))
      error(Err);
  }

  config = std::make_unique<Configuration>();
  symtab = std::make_unique<SymbolTable>();
  config->outputType = getOutputType(driverCfg);
  target = createTargetInfo(driverCfg);
  depTracker = std::make_unique<DependencyTracker>(
      args.getLastArgValue(OPT_dependency_info));

  if (errorCount())
    return false;

  if (args.hasArg(OPT_pagezero_size)) {
    uint64_t pagezeroSize = args::getHex(args, OPT_pagezero_size, 0);

    // Always round __PAGEZERO down to the target page size (16K on arm64).
    if (!isAligned(Align(target->getPageSize()), pagezeroSize)) {
      pagezeroSize -= pagezeroSize % target->getPageSize();
      warn("__PAGEZERO size is not page aligned, rounding down to 0x" +
           Twine::utohexstr(pagezeroSize));
    }

    target->pageZeroSize = pagezeroSize;
  }

  config->osoPrefix = args.getLastArgValue(OPT_oso_prefix);
  if (!config->osoPrefix.empty()) {
    // Resolve "." / ".." / "~" via real_path so debuggers see a stable
    // prefix; preserve a trailing separator because the strip is textual.
    SmallString<1024> expanded;
    if (!fs::real_path(config->osoPrefix, expanded,
                       /*expand_tilde=*/true)) {
      StringRef sep = sys::path::get_separator();
      if (config->osoPrefix.equals(".") || config->osoPrefix.ends_with(sep))
        expanded += sep;
      config->osoPrefix = saver().save(expanded.str());
    }
  }

  bool pie = driverCfg.pie;

  config->isPic = config->outputType == MH_DYLIB ||
                  config->outputType == MH_BUNDLE ||
                  (config->outputType == MH_EXECUTE && pie);

  config->deadStrip = driverCfg.gcSections;

  config->systemLibraryRoots = getSystemLibraryRoots(driverCfg);

  if (driverCfg.threadCount) {
    parallel::strategy = hardware_concurrency(driverCfg.threadCount);
  }

  for (const Arg *arg : args.filtered(OPT_u)) {
    config->explicitUndefineds.push_back(symtab->addUndefined(
        arg->getValue(), /*file=*/nullptr, /*isWeakRef=*/false));
  }

  for (const Arg *arg : args.filtered(OPT_U))
    config->explicitDynamicLookups.insert(arg->getValue());

  config->mapFile = driverCfg.mapFile;
  config->optimize =
      driverCfg.linkerOptLevel >= 0 ? driverCfg.linkerOptLevel : 1;
  config->outputFile = driverCfg.outputFile.empty()
                           ? StringRef("a.out")
                           : StringRef(driverCfg.outputFile);
  config->finalOutput =
      args.getLastArgValue(OPT_final_output, config->outputFile);
  config->astPaths = args.getAllArgValues(OPT_add_ast_path);
  config->headerPad = args::getHex(args, OPT_headerpad, /*Default=*/32);
  config->headerPadMaxInstallNames =
      args.hasArg(OPT_headerpad_max_install_names);
  config->printDylibSearch = driverCfg.verbose;
  config->printEachFile = driverCfg.traceFiles;
  config->printWhyLoad = args.hasArg(OPT_why_load);
  config->omitDebugInfo = driverCfg.stripLevel >= 1;
  config->errorForArchMismatch = args.hasArg(OPT_arch_errors_fatal);
  if (const Arg *arg = args.getLastArg(OPT_bundle_loader)) {
    if (config->outputType != MH_BUNDLE)
      error("--bundle-loader can only be used with MachO bundle output");
    addFile(arg->getValue(), LoadType::CommandLine, /*isLazy=*/false,
            /*isExplicit=*/false, /*isBundleLoader=*/true);
  }
  for (auto *arg : args.filtered(OPT_dyld_env)) {
    StringRef envPair(arg->getValue());
    if (!envPair.contains('='))
      error("--dyld-env's argument is  malformed. Expected "
            "--dyld-env <ENV_VAR>=<VALUE>, got `" +
            envPair + "`");
    config->dyldEnvs.push_back(envPair);
  }
  if (!config->dyldEnvs.empty() && config->outputType != MH_EXECUTE)
    error("--dyld-env can only be used when creating executable output");

  if (const Arg *arg = args.getLastArg(OPT_umbrella)) {
    if (config->outputType != MH_DYLIB)
      warn("--umbrella used, but not creating dylib");
    config->umbrella = arg->getValue();
  }
  config->runtimePaths = args::getStrings(args, OPT_rpath);
  config->allLoad = args.hasFlag(OPT_all_load, OPT_noall_load, false);
  config->archMultiple = args.hasArg(OPT_arch_multiple);
  config->applicationExtension = args.hasFlag(
      OPT_application_extension, OPT_no_application_extension, false);
  config->exportDynamic =
      driverCfg.exportDynamic || args.hasArg(OPT_export_dynamic);
  config->deadStripDylibs = args.hasArg(OPT_dead_strip_dylibs);
  config->demangle = driverCfg.demangle;
  config->implicitDylibs = !args.hasArg(OPT_no_implicit_dylibs);
  config->emitFunctionStarts =
      args.hasFlag(OPT_function_starts, OPT_no_function_starts, true);
  config->emitDataInCodeInfo =
      args.hasFlag(OPT_data_in_code_info, OPT_no_data_in_code_info, true);
  config->emitChainedFixups = shouldEmitChainedFixups(args);
  config->emitInitOffsets =
      config->emitChainedFixups || args.hasArg(OPT_init_offsets);
  config->icfLevel = getICFFromDriver(driverCfg.icfLevel);
  config->dedupStrings =
      args.hasFlag(OPT_deduplicate_strings, OPT_no_deduplicate_strings, true);
  config->deadStripDuplicates = args.hasArg(OPT_dead_strip_duplicates);
  for (auto *arg : args.filtered(OPT_override_eq))
    config->overrideSymbols.try_emplace(arg->getValue(), nullptr);
  config->warnDylibInstallName = args.hasFlag(
      OPT_warn_dylib_install_name, OPT_no_warn_dylib_install_name, false);
  config->ignoreOptimizationHints = args.hasArg(OPT_ignore_optimization_hints);
  config->callGraphProfileSort = (driverCfg.callGraphProfileSort != "none");
  if (!driverCfg.printSymbolOrder.empty())
    config->printSymbolOrder = saver().save(driverCfg.printSymbolOrder);
  config->forceExactCpuSubtypeMatch = false;
  config->ignoreAutoLink = args.hasArg(OPT_ignore_auto_link);
  for (const Arg *arg : args.filtered(OPT_ignore_auto_link_option))
    config->ignoreAutoLinkOptions.insert(arg->getValue());
  config->strictAutoLink = args.hasArg(OPT_strict_auto_link);
  config->driverCfg = &driverCfg;
  config->generateUuid = !args.hasArg(OPT_no_uuid);

  for (const Arg *arg : args.filtered(OPT_alias)) {
    config->aliasedSymbols.push_back(
        std::make_pair(arg->getValue(0), arg->getValue(1)));
  }

  config->zeroModTime = driverCfg.repro;

  config->emitEncryptionInfo =
      args.hasFlag(OPT_encryptable, OPT_no_encryption, /*Default=*/false);

  if (const Arg *arg = args.getLastArg(OPT_install_name)) {
    if (config->warnDylibInstallName && config->outputType != MH_DYLIB)
      warn(
          arg->getAsString(args) +
          ": ignored, only has effect with -dylib [--warn-dylib-install-name]");
    else
      config->installName = arg->getValue();
  } else if (config->outputType == MH_DYLIB) {
    config->installName = config->finalOutput;
  }

  if (args.hasArg(OPT_mark_dead_strippable_dylib)) {
    if (config->outputType != MH_DYLIB)
      warn(
          "--mark-dead-strippable-dylib: ignored, only has effect with -dylib");
    else
      config->markDeadStrippableDylib = true;
  }

  config->staticLink = driverCfg.staticLink;

  if (const Arg *arg =
          args.getLastArg(OPT_flat_namespace, OPT_twolevel_namespace))
    config->namespaceKind = arg->getOption().getID() == OPT_twolevel_namespace
                                ? NamespaceKind::twolevel
                                : NamespaceKind::flat;

  config->undefinedSymbolTreatment = getUndefinedSymbolTreatment(args);

  if (config->outputType == MH_EXECUTE)
    config->entry = symtab->addUndefined(args.getLastArgValue(OPT_e, "_main"),
                                         /*file=*/nullptr,
                                         /*isWeakRef=*/false);

  config->librarySearchPaths =
      getLibrarySearchPaths(args, config->systemLibraryRoots);
  config->frameworkSearchPaths =
      getFrameworkSearchPaths(args, config->systemLibraryRoots);
  if (const Arg *arg =
          args.getLastArg(OPT_search_paths_first, OPT_search_dylibs_first))
    config->searchDylibsFirst =
        arg->getOption().getID() == OPT_search_dylibs_first;

  config->dylibCompatibilityVersion =
      parseDylibVersion(args, OPT_compatibility_version);
  config->dylibCurrentVersion = parseDylibVersion(args, OPT_current_version);

  config->dataConst =
      args.hasFlag(OPT_data_const, OPT_no_data_const, dataConstDefault());
  initializeSectionRenameMap();
  StringRef invalidNameChars("!\"#%&'()*+,-/:;<=>?@[\\]^`{|}~");
  auto validName = [invalidNameChars](StringRef s) {
    if (s.find_first_of(invalidNameChars) != StringRef::npos)
      error("invalid name for segment or section: " + s);
    return s;
  };
  for (const Arg *arg : args.filtered(OPT_rename_section)) {
    config->sectionRenameMap[{validName(arg->getValue(0)),
                              validName(arg->getValue(1))}] = {
        validName(arg->getValue(2)), validName(arg->getValue(3))};
  }
  for (const Arg *arg : args.filtered(OPT_rename_segment)) {
    config->segmentRenameMap[validName(arg->getValue(0))] =
        validName(arg->getValue(1));
  }

  config->sectionAlignments = parseSectAlign(args);

  for (const Arg *arg : args.filtered(OPT_segprot)) {
    StringRef segName = arg->getValue(0);
    uint32_t maxProt = parseProtection(arg->getValue(1));
    uint32_t initProt = parseProtection(arg->getValue(2));
    if (maxProt != initProt)
      error("invalid argument '" + arg->getAsString(args) +
            "': max and init protections must be the same");
    if (segName == segment_names::linkEdit)
      error("-segprot cannot be used to change __LINKEDIT's protections");
    config->segmentProtections.push_back({segName, maxProt, initProt});
  }

  config->hasExplicitExports =
      args.hasArg(OPT_no_exported_symbols) ||
      args.hasArgNoClaim(OPT_exported_symbol, OPT_exported_symbols_list);
  handleSymbolPatterns(args, config->exportedSymbols, OPT_exported_symbol,
                       OPT_exported_symbols_list);
  handleSymbolPatterns(args, config->unexportedSymbols, OPT_unexported_symbol,
                       OPT_unexported_symbols_list);
  if (config->hasExplicitExports && !config->unexportedSymbols.empty())
    error("cannot use both -exported_symbol* and -unexported_symbol* options");

  if (args.hasArg(OPT_no_exported_symbols) && !config->exportedSymbols.empty())
    error("cannot use both -exported_symbol* and -no_exported_symbols options");

  {
    bool includeLocal = false;
    bool excludeLocal = false;
    for (const Arg *arg :
         args.filtered(OPT_x, OPT_non_global_symbols_no_strip_list,
                       OPT_non_global_symbols_strip_list)) {
      switch (arg->getOption().getID()) {
      case OPT_x:
        config->localSymbolsPresence = SymtabPresence::None;
        break;
      case OPT_non_global_symbols_no_strip_list:
        if (excludeLocal) {
          error("cannot use both -non_global_symbols_no_strip_list and "
                "-non_global_symbols_strip_list");
        } else {
          includeLocal = true;
          config->localSymbolsPresence = SymtabPresence::SelectivelyIncluded;
          parseSymbolPatternsFile(arg, config->localSymbolPatterns);
        }
        break;
      case OPT_non_global_symbols_strip_list:
        if (includeLocal) {
          error("cannot use both -non_global_symbols_no_strip_list and "
                "-non_global_symbols_strip_list");
        } else {
          excludeLocal = true;
          config->localSymbolsPresence = SymtabPresence::SelectivelyExcluded;
          parseSymbolPatternsFile(arg, config->localSymbolPatterns);
        }
        break;
      default:
        llvm_unreachable("unexpected option");
      }
    }
  }
  if (driverCfg.stripLocals &&
      config->localSymbolsPresence == SymtabPresence::All)
    config->localSymbolsPresence = SymtabPresence::None;
  for (const CachedHashStringRef &cachedName : config->exportedSymbols.literals)
    symtab->addUndefined(cachedName.val(), /*file=*/nullptr,
                         /*isWeakRef=*/false);

  for (const Arg *arg : args.filtered(OPT_why_live))
    config->whyLive.insert(arg->getValue());
  if (!config->whyLive.empty() && !config->deadStrip)
    warn("--why-live has no effect without dead stripping (use -O1+ to "
         "enable), ignoring");

  config->adhocCodesign = args.hasFlag(
      OPT_adhoc_codesign, OPT_no_adhoc_codesign,
      shouldAdhocSignByDefault(config->arch(), config->platform()));

  config->progName = argsArr[0];

  if (driverCfg.timeTraceEnabled)
    timeTraceProfilerInitialize(driverCfg.timeTraceGranularity,
                                config->progName);

  {
    TimeTraceScope timeScope("ExecuteLinker");

    initLLVM(); // must be run before any call to addFile()
    createFiles(args);

    {
      auto reexportHandler = [](const Arg *arg,
                                const std::vector<StringRef> &extensions) {
        config->hasReexports = true;
        StringRef searchName = arg->getValue();
        if (!markReexport(searchName, extensions))
          error(arg->getSpelling() + " " + searchName +
                " does not match a supplied dylib");
      };
      std::vector<StringRef> extensions = {".tbd"};
      for (const Arg *arg : args.filtered(OPT_sub_umbrella))
        reexportHandler(arg, extensions);

      extensions.push_back(".dylib");
      for (const Arg *arg : args.filtered(OPT_sub_library))
        reexportHandler(arg, extensions);
    }

    parseMllvmOptions(driverCfg);

    createSyntheticSections();
    createSyntheticSymbols();

    createAliases();
    handleExplicitExports();

    bool didCompileBitcodeFiles = compileBitcodeFiles();

    resolveLCLinkerOptions();

    if (didCompileBitcodeFiles)
      handleExplicitExports();
    replaceCommonSymbols();

    if (config->outputType == MH_OBJECT) {
      TimeTraceScope mergeScope("Relocatable merge");
      SmallVector<StringRef, 32> buffers;
      for (InputFile *f : inputFiles)
        if (isa<ObjFile>(f))
          buffers.push_back(f->mb.getBuffer());

      neverc::merge::Options mergeOpts;
      mergeOpts.pureC = true;

      std::error_code ec;
      raw_fd_ostream out(config->outputFile, ec, sys::fs::OF_None);
      if (ec) {
        error("cannot open " + config->outputFile + ": " + ec.message());
        return errorCount() == 0;
      }
      if (!neverc::merge::mergeObjects(
              buffers, out, neverc::merge::Format::MachO64, mergeOpts))
        error("relocatable merge failed");
      return errorCount() == 0;
    }

    StringRef orderFile = args.getLastArgValue(OPT_order_file);
    if (!orderFile.empty())
      priorityBuilder.parseOrderFile(orderFile);

    referenceStubBinder();

    for (const Arg *arg : args.filtered(OPT_sectcreate)) {
      StringRef segName = arg->getValue(0);
      StringRef sectName = arg->getValue(1);
      StringRef fileName = arg->getValue(2);
      std::optional<MemoryBufferRef> buffer = readFile(fileName);
      if (buffer)
        inputFiles.insert(make<OpaqueFile>(*buffer, segName, sectName));
    }

    for (const Arg *arg : args.filtered(OPT_add_empty_section)) {
      StringRef segName = arg->getValue(0);
      StringRef sectName = arg->getValue(1);
      inputFiles.insert(make<OpaqueFile>(MemoryBufferRef(), segName, sectName));
    }

    gatherInputSections();
    if (config->callGraphProfileSort)
      priorityBuilder.extractCallGraphProfile();

    if (config->deadStrip)
      markLive();

    foldIdenticalLiterals();
    if (config->icfLevel != ICFLevel::none) {
      if (config->icfLevel == ICFLevel::safe)
        markAddrSigSymbols();
      foldIdenticalSections(/*onlyCfStrings=*/false);
    } else if (config->dedupStrings) {
      foldIdenticalSections(/*onlyCfStrings=*/true);
    }

    writeOutput<LP64>();

    depTracker->write(inputFiles, config->outputFile);
  }

  if (config->driverCfg->timeTraceEnabled) {
    checkError(timeTraceProfilerWrite(std::string(), config->outputFile));

    timeTraceProfilerCleanup();
  }

  if (errorCount() != 0 || config->strictAutoLink)
    for (const auto &warning : missingAutolinkWarnings)
      warn(warning);

  return errorCount() == 0;
}
} // namespace macho
} // namespace linker
