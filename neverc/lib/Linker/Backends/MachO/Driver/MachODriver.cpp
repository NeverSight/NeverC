#include "Driver/Parallelism.h"
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
#include "Linker/Core/Driver/Dispatcher.h"
#include "Linker/Core/Runtime/Allocator.h"
#include "Linker/Core/Runtime/CrashRecovery.h"
#include "Linker/Core/Runtime/Diagnostic.h"
#include "Linker/Core/Runtime/LinkerExecutionContext.h"
#include "Linker/Core/Runtime/LinkerParallel.h"
#include "Linker/Core/Runtime/Session.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/LTO/LTO.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/MachO.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/TextAPI/PackedVersion.h"
#if !defined(_WIN32)
#include <sys/resource.h>
#endif

#include "neverc/Foundation/Core/Version.h"
#include "neverc/Merge/Merger.h"
#include <algorithm>
#include <csignal>
#include <optional>
#include <set>
#include <vector>

// MachOContextAccess defines short accessor macros such as `in`. Keep every
// standard/product header above it so those macros cannot rewrite headers.
// clang-format off
#include "Linker/MachO/MachOContextAccess.h"
#include "Linker/MachO/MachOLinkerContext.h"
// clang-format on

using namespace llvm;
using namespace llvm::MachO;
using namespace llvm::object;
using namespace llvm::opt;
using namespace llvm::sys;
using namespace linker;
using namespace linker::macho;
using linker::macho::detail::LinkInputWorkload;

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
getSystemLibraryRoots(const InputArgList &args,
                      const LinkerDriverConfig &driverCfg) {
  std::vector<StringRef> roots;
  if (!driverCfg.sysroot.empty())
    roots.push_back(saver().save(driverCfg.sysroot));
  for (const Arg *arg : args.filtered(OPT_syslibroot))
    roots.push_back(arg->getValue());
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

LinkThreadPolicy machODenseOutputThreadPolicy() {
  LinkThreadPolicy Policy;
  Policy.MinParallelBytes = 8ULL * 1024ULL * 1024ULL;
  Policy.BytesPerAdditionalThread = 0;
  // UUID/code-signature hashing follows total output size even when most
  // inputs are tiny, so the relocation-oriented average-file gate is wrong.
  Policy.MinAverageFileBytes = 0;
  return Policy;
}

void configureParallelismForWorkload(unsigned RequestedThreads,
                                     LinkInputWorkload Workload,
                                     bool FinalizeSerial) {
  if (commonContext().parallelConfigured())
    return;

  const LinkThreadPolicy Policy = machODenseOutputThreadPolicy();
  if (RequestedThreads == 0 && !FinalizeSerial &&
      (Workload.Bytes < Policy.MinParallelBytes || Workload.Files == 0 ||
       Workload.Bytes / Workload.Files < Policy.MinAverageFileBytes))
    return;

  commonContext().configureParallelForInputWorkload(
      RequestedThreads, Workload.Bytes, Workload.Files, Policy, FinalizeSerial);
}

std::optional<LinkInputWorkload>
recordIncrementalInputWorkload(file_magic Magic, uint64_t Size) {
  linker::macho::detail::IncrementalInputWorkload &Workload =
      linker::macho::detail::incrementalInputWorkload();
  if (Magic == file_magic::macho_object)
    return Workload.recordNative(Size);
  if (Magic == file_magic::bitcode)
    return Workload.recordBitcode(Size);
  return std::nullopt;
}

enum class LoadType {
  CommandLine,      // Library was passed as a regular CLI argument
  CommandLineForce, // Library was passed via `-force_load`
  LCLinkerOption,   // Library was passed via LC_LINKER_OPTIONS
};

// Whether a member object has Objective-C class or category lists.
bool hasObjCSection(MemoryBufferRef mb) {
  if (identify_magic(mb.getBuffer()) != file_magic::macho_object)
    return false;
  Expected<std::unique_ptr<object::MachOObjectFile>> obj =
      object::MachOObjectFile::create(mb, /*IsLittleEndian=*/true,
                                      /*Is64Bits=*/true);
  if (!obj) {
    consumeError(obj.takeError());
    return false;
  }
  for (const object::SectionRef &sec : (*obj)->sections()) {
    Expected<StringRef> name = sec.getName();
    if (!name) {
      consumeError(name.takeError());
      continue;
    }
    if (*name == "__objc_classlist" || *name == "__objc_catlist" ||
        *name == "__objc_nlclslist" || *name == "__objc_nlcatlist")
      return true;
  }
  return false;
}

InputFile *addFile(StringRef path, LoadType loadType, bool isLazy = false,
                   bool isExplicit = true, bool isBundleLoader = false,
                   bool isForceHidden = false) {
  std::optional<MemoryBufferRef> buffer = readFile(path);
  if (!buffer)
    return nullptr;
  MemoryBufferRef mbref = *buffer;
  InputFile *newFile = nullptr;

  file_magic magic = identify_magic(mbref.getBuffer());
  if (!isLazy) {
    if (std::optional<LinkInputWorkload> Workload =
            recordIncrementalInputWorkload(magic, mbref.getBufferSize()))
      configureParallelismForWorkload(/*RequestedThreads=*/0, *Workload,
                                      /*FinalizeSerial=*/false);
  }
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
    } else if (config->forceLoadObjC) {
      // -ObjC loads the members that define Objective-C classes or
      // categories, which nothing may reference by name.
      for (const object::Archive::Symbol &sym : file->getArchive().symbols())
        if (sym.getName().starts_with("_OBJC_CLASS_$_"))
          file->fetch(sym);
      Error e = Error::success();
      for (const object::Archive::Child &c : file->getArchive().children(e)) {
        Expected<MemoryBufferRef> mb = c.getMemoryBufferRef();
        if (!mb) {
          consumeError(mb.takeError());
          continue;
        }
        if (hasObjCSection(*mb))
          if (Error err = file->fetch(c, "-ObjC"))
            error(toString(file) + ": -ObjC failed to load archive member: " +
                  toString(std::move(err)));
      }
      if (e)
        error(toString(file) +
              ": Archive::children failed: " + toString(std::move(e)));
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

void addLibrary(StringRef name, bool isNeeded, bool isWeak, bool isReexport,
                bool isHidden, bool isExplicit, LoadType loadType,
                bool isUpward = false) {
  if (std::optional<StringRef> path = findLibrary(name)) {
    if (auto *dylibFile = dyn_cast_or_null<DylibFile>(
            addFile(*path, loadType, /*isLazy=*/false, isExplicit,
                    /*isBundleLoader=*/false, isHidden))) {
      if (isUpward)
        dylibFile->upward = true;
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

void addFramework(StringRef name, bool isNeeded, bool isWeak, bool isReexport,
                  bool isExplicit, LoadType loadType, bool isUpward = false) {
  if (std::optional<StringRef> path = findFramework(name)) {
    if (loadedObjectFrameworks.contains(*path))
      return;

    InputFile *file =
        addFile(*path, loadType, /*isLazy=*/false, isExplicit, false);
    if (auto *dylibFile = dyn_cast_or_null<DylibFile>(file)) {
      if (isUpward)
        dylibFile->upward = true;
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

void configureParallelismForMaterializedInputs(
    const LinkerDriverConfig &driverCfg, bool FinalizeSerial,
    LinkInputWorkload AdditionalWorkload = {}) {
  LinkInputWorkload Workload =
      linker::macho::detail::incrementalInputWorkload().current();
  Workload.merge(AdditionalWorkload);
  // UUID and code-signature hashing benefit from the full budget once the
  // output is large enough; tiny links still stay entirely serial.
  configureParallelismForWorkload(driverCfg.threadCount, Workload,
                                  FinalizeSerial);
}

} // namespace

void macho::configureParallelismForLTONativeObjects(
    ArrayRef<uint64_t> ObjectSizes) {
  LinkInputWorkload Workload = linker::macho::detail::incrementalInputWorkload()
                                   .replaceBitcodeWithNative(ObjectSizes);
  configureParallelismForWorkload(/*RequestedThreads=*/0, Workload,
                                  /*FinalizeSerial=*/false);
}

void macho::configureParallelismForMaterializedInput(MemoryBufferRef Buffer) {
  const file_magic Magic = identify_magic(Buffer.getBuffer());
  std::optional<LinkInputWorkload> Workload =
      recordIncrementalInputWorkload(Magic, Buffer.getBufferSize());
  if (!Workload || commonContext().parallelConfigured())
    return;
  configureParallelismForWorkload(/*RequestedThreads=*/0, *Workload,
                                  /*FinalizeSerial=*/false);
}

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

    if (!commonContext().parallelConfigured()) {
      LinkInputWorkload Workload =
          linker::macho::detail::incrementalInputWorkload().current();
      DenseSet<StringRef> SeenPaths;
      auto Account = [&](MemoryBufferRef Buffer) {
        file_magic Magic = identify_magic(Buffer.getBuffer());
        if (Magic == file_magic::macho_object || Magic == file_magic::bitcode)
          Workload.account(Buffer.getBufferSize());
      };
      for (const InputFile *File : inputFiles)
        SeenPaths.insert(File->getName());
      for (unsigned I = 0; I < LCLinkerOptions.size(); ++I) {
        StringRef Arg = LCLinkerOptions[I];
        std::optional<StringRef> Path;
        if (Arg.consume_front("-l")) {
          Path = findLibrary(Arg);
        } else if (Arg == "-framework" && I + 1 < LCLinkerOptions.size()) {
          Path = findFramework(LCLinkerOptions[++I]);
        }
        if (!Path || !SeenPaths.insert(*Path).second)
          continue;
        if (std::optional<MemoryBufferRef> Buffer =
                readFile(*Path, /*reportError=*/false))
          Account(*Buffer);
      }
      // Object-containing frameworks can be sized before parsing. Archives
      // are intentionally deferred until a member is actually fetched.
      configureParallelismForWorkload(/*RequestedThreads=*/0, Workload,
                                      /*FinalizeSerial=*/false);
    }

    for (unsigned i = 0; i < LCLinkerOptions.size(); ++i) {
      StringRef arg = LCLinkerOptions[i];
      if (arg.consume_front("-l")) {
        assert(!config->ignoreAutoLinkOptions.contains(arg));
        // -force_load_swift_libs loads every member of the Swift libraries
        // objects ask for.
        addLibrary(arg, /*isNeeded=*/false, /*isWeak=*/false,
                   /*isReexport=*/false, /*isHidden=*/false,
                   /*isExplicit=*/false,
                   config->forceLoadSwiftLibs && arg.starts_with("swift")
                       ? LoadType::CommandLineForce
                       : LoadType::LCLinkerOption);
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

    // addLazySymbols() may have synchronously fetched archive members for
    // already-known undefineds. The per-context accumulator records each
    // pending member before parsing, never its archive container.
    configureParallelismForWorkload(
        /*RequestedThreads=*/0,
        linker::macho::detail::incrementalInputWorkload().current(),
        /*FinalizeSerial=*/false);
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

void prefetchInputFiles(const InputArgList &args, unsigned RequestedThreads) {
  TimeTraceScope timeScope("Prefetch input files");
  if (RequestedThreads != 0 && !commonContext().parallelConfigured())
    commonContext().configureParallel(RequestedThreads);

  SmallVector<StringRef, 0> paths;
  paths.reserve(args.size());

  auto pushPath = [&](StringRef path) {
    if (path.empty())
      return;
    paths.push_back(saver().save(rerootPath(path)));
  };
  auto pushResolvedPath = [&](StringRef path) {
    if (!path.empty())
      paths.push_back(saver().save(path));
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
    case OPT_l:
    case OPT_needed_l:
    case OPT_reexport_l:
    case OPT_weak_l:
    case OPT_hidden_l:
      if (std::optional<StringRef> Path = findLibrary(arg->getValue()))
        pushResolvedPath(*Path);
      break;
    case OPT_framework:
    case OPT_needed_framework:
    case OPT_reexport_framework:
    case OPT_weak_framework:
      if (std::optional<StringRef> Path = findFramework(arg->getValue()))
        pushResolvedPath(*Path);
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

  if (!parallelEnabled() || uniquePaths.size() < 8) {
    LinkInputWorkload PrefetchedWorkload;
    for (StringRef path : uniquePaths) {
      std::optional<MemoryBufferRef> Buffer =
          readFile(path, /*reportError=*/false);
      if (!Buffer)
        continue;
      file_magic Magic = identify_magic(Buffer->getBuffer());
      if (Magic == file_magic::macho_object || Magic == file_magic::bitcode)
        PrefetchedWorkload.account(Buffer->getBufferSize());
    }
    // A single complex object can use the pool while it splits C strings and
    // parses relocation sections. Configure after the serial prefetch has made
    // its size known. Archives, dylibs and TBDs do not contribute native
    // output until an archive member is actually materialized.
    configureParallelismForWorkload(/*RequestedThreads=*/0, PrefetchedWorkload,
                                    /*FinalizeSerial=*/false);
    return;
  }

  parallelForEach(uniquePaths, [](StringRef path) {
    (void)readFile(path, /*reportError=*/false);
  });
}

LinkInputWorkload prefetchSectCreatePayloads(const InputArgList &args) {
  LinkInputWorkload Workload;
  for (const Arg *Arg : args.filtered(OPT_sectcreate)) {
    if (std::optional<MemoryBufferRef> Buffer =
            readFile(Arg->getValue(2), /*reportError=*/false))
      Workload.account(Buffer->getBufferSize());
  }
  return Workload;
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

  SmallVector<BitcodeFile *, 0> bcFiles;
  for (InputFile *file : inputFiles)
    if (auto *bitcodeFile = dyn_cast<BitcodeFile>(file))
      if (!file->lazy)
        bcFiles.push_back(bitcodeFile);
  // Constructing BitcodeCompiler also creates the plugin LTO child task.  With
  // no bitcode inputs there is no backend run (and therefore no
  // BackendDoneHook) to finish that child, which would prevent the parent
  // LinkTask from ending.  Skip LTO setup entirely when there is nothing to do.
  if (bcFiles.empty())
    return false;

  auto *lto = make<BitcodeCompiler>();
  lto->addBatch(bcFiles);

  std::vector<ObjFile *> compiled = lto->compile();
  for (ObjFile *file : compiled)
    inputFiles.insert(file);

  return !compiled.empty();
}

// -debug_snapshot: copies the link's inputs and records its command in
// <snapshot dir>/<output>.snapshot, so that the link can be rerun elsewhere.
void writeDebugSnapshot(ArrayRef<const char *> argv) {
  SmallString<256> dir(config->snapshotDir.empty()
                           ? sys::path::parent_path(config->outputFile)
                           : config->snapshotDir);
  if (dir.empty())
    dir = ".";
  sys::path::append(dir, sys::path::filename(config->outputFile) + ".snapshot");
  sys::fs::remove_directories(dir, /*IgnoreErrors=*/true);
  SmallString<256> inputsDir(dir);
  sys::path::append(inputsDir, "inputs");
  if (std::error_code ec = sys::fs::create_directories(inputsDir)) {
    error("-debug_snapshot: cannot create " + inputsDir + ": " + ec.message());
    return;
  }

  std::string map;
  SetVector<StringRef> copied;
  for (const InputFile *file : inputFiles) {
    StringRef path = file->getName();
    if (const auto *obj = dyn_cast<ObjFile>(file);
        obj && !obj->archiveName.empty())
      path = obj->archiveName;
    else if (!sys::fs::exists(path))
      path = path.take_until([](char c) { return c == '('; });
    if (path.empty() || !sys::fs::is_regular_file(path) || !copied.insert(path))
      continue;
    SmallString<256> copy(inputsDir);
    sys::path::append(copy,
                      Twine(copied.size()) + "-" + sys::path::filename(path));
    if (std::error_code ec = sys::fs::copy_file(path, copy))
      error("-debug_snapshot: cannot copy " + path + ": " + ec.message());
    map += (sys::path::filename(copy) + " " + path + "\n").str();
  }

  std::string command;
  for (const char *arg : argv) {
    StringRef s(arg);
    command += s.find_first_of(" \t\"'") == StringRef::npos
                   ? s.str()
                   : ("'" + s + "'").str();
    command += ' ';
  }
  command.back() = '\n';
  auto write = [&](StringRef name, StringRef contents) {
    SmallString<256> path(dir);
    sys::path::append(path, name);
    std::error_code ec;
    raw_fd_ostream os(path, ec, sys::fs::OF_Text);
    if (ec)
      error("-debug_snapshot: cannot write " + path + ": " + ec.message());
    else
      os << contents;
  };
  write("link_command", command);
  write("inputs.txt", map);
}

// -kext_objects_dir: a kext link also writes its objects, merged into one
// relocatable object, for linking the kext again later.
void writeKextObjects() {
  SmallVector<StringRef, 32> buffers;
  for (const InputFile *file : inputFiles)
    if (isa<ObjFile>(file) && !file->lazy)
      buffers.push_back(file->mb.getBuffer());
  SmallString<256> path(config->kextObjectsDir);
  sys::path::append(path, sys::path::filename(config->outputFile) + ".o");
  if (std::error_code ec =
          sys::fs::create_directories(config->kextObjectsDir)) {
    error("-kext_objects_dir: cannot create " + config->kextObjectsDir + ": " +
          ec.message());
    return;
  }
  std::error_code ec;
  raw_fd_ostream out(path, ec, sys::fs::OF_None);
  if (ec) {
    error("-kext_objects_dir: cannot open " + path + ": " + ec.message());
    return;
  }
  neverc::merge::Options mergeOpts;
  mergeOpts.pureC = true;
  if (!neverc::merge::mergeObjects(buffers, out, neverc::merge::Format::MachO64,
                                   mergeOpts))
    error("-kext_objects_dir: cannot merge the kext's objects into " + path);
}

// -objc_category_merging: the categories of one class in __objc_catlist
// become one category, whose method, protocol and property lists join
// theirs, later categories first, as the runtime would attach them. The
// merged category takes the first one's place in __objc_catlist. Categories
// with +load (__objc_nlcatlist) keep their own.
void mergeObjCCategories() {
  constexpr uint64_t categorySize = 64;
  // Where a pointer leads: an input section and an offset in it.
  auto targetOf =
      [](const Reloc &r) -> std::pair<ConcatInputSection *, uint64_t> {
    if (const auto *sym = r.referent.dyn_cast<Symbol *>()) {
      const auto *d = dyn_cast<Defined>(sym);
      if (!d || !d->isec)
        return {nullptr, 0};
      return {dyn_cast<ConcatInputSection>(d->isec), d->value + r.addend};
    }
    return {dyn_cast<ConcatInputSection>(r.referent.get<InputSection *>()),
            uint64_t(r.addend)};
  };
  auto stringOf = [](const Reloc &r) -> StringRef {
    if (const auto *d =
            dyn_cast_or_null<Defined>(r.referent.dyn_cast<Symbol *>());
        d && d->isec)
      if (const auto *cstrings = dyn_cast<CStringInputSection>(d->isec))
        return cstrings->getStringRefAtOffset(d->value + r.addend);
    if (const auto *isec = r.referent.dyn_cast<InputSection *>())
      if (const auto *cstrings = dyn_cast<CStringInputSection>(isec))
        return cstrings->getStringRefAtOffset(r.addend);
    return "";
  };
  auto isPointer = [](const Reloc &r) {
    return r.length == 3 && !r.pcrel && r.type == target->unsignedRelocType;
  };
  struct Category {
    ConcatInputSection *isec;
    ConcatInputSection *catlist;
    uint64_t entry;
    std::array<const Reloc *, 7> fields{};
  };
  SmallVector<Category, 0> categories;
  DenseSet<const InputSection *> withLoad;
  for (const InputFile *file : inputFiles) {
    const auto *obj = dyn_cast<ObjFile>(file);
    if (!obj)
      continue;
    for (const Section *section : obj->sections) {
      const bool nonLazy = section->name == "__objc_nlcatlist";
      if (section->name != "__objc_catlist" && !nonLazy)
        continue;
      for (const Subsection &subsection : section->subsections) {
        auto *catlist = dyn_cast<ConcatInputSection>(subsection.isec);
        if (!catlist)
          continue;
        for (const Reloc &r : catlist->relocs) {
          auto [isec, offset] = targetOf(r);
          if (!isec)
            continue;
          if (nonLazy) {
            withLoad.insert(isec);
            continue;
          }
          if (!isPointer(r) || offset != 0 ||
              isec->data.size() != categorySize ||
              support::endian::read32le(isec->data.data() + 56) != categorySize)
            continue;
          Category category{isec, catlist, r.offset};
          bool valid = true;
          for (const Reloc &field : isec->relocs) {
            if (!isPointer(field) || field.offset % 8 || field.offset >= 56) {
              valid = false;
              break;
            }
            category.fields[field.offset / 8] = &field;
          }
          if (valid && category.fields[0] && category.fields[1])
            categories.push_back(category);
        }
      }
    }
  }

  // Categories by class, in __objc_catlist order.
  MapVector<std::pair<const void *, int64_t>, SmallVector<size_t, 2>> byClass;
  for (size_t i = 0; i < categories.size(); ++i) {
    if (withLoad.contains(categories[i].isec))
      continue;
    const Reloc *cls = categories[i].fields[1];
    byClass[{cls->referent.getOpaqueValue(), cls->addend}].push_back(i);
  }

  // A list's header size and entry size, and whether its header is a 64-bit
  // count (protocol lists) or an entry size and a count.
  struct ListKind {
    uint32_t headerSize, entrySize;
    bool countOnly;
  };
  auto listKind = [](int field) {
    return field == 2 || field == 3 ? ListKind{8, 24, false}
           : field == 4             ? ListKind{8, 8, true}
                                    : ListKind{8, 16, false};
  };
  // A list of the category, if it is one the merge can read.
  auto listOf = [&](const Category &category,
                    int field) -> std::optional<ConcatInputSection *> {
    const Reloc *r = category.fields[field];
    if (!r)
      return nullptr;
    const ListKind kind = listKind(field);
    auto [isec, offset] = targetOf(*r);
    if (!isec || offset != 0 || isec->data.size() < kind.headerSize)
      return std::nullopt;
    const uint8_t *data = isec->data.data();
    const uint64_t count = kind.countOnly ? support::endian::read64le(data)
                                          : support::endian::read32le(data + 4);
    if ((!kind.countOnly &&
         (support::endian::read32le(data) & 0xffff) != kind.entrySize) ||
        isec->data.size() != kind.headerSize + count * kind.entrySize)
      return std::nullopt;
    for (const Reloc &entry : isec->relocs)
      if (!isPointer(entry) || entry.offset < kind.headerSize)
        return std::nullopt;
    return isec;
  };

  DenseMap<ConcatInputSection *, DenseSet<uint64_t>> removedEntries;
  for (auto &[cls, members] : byClass) {
    if (members.size() < 2)
      continue;
    // Every list of every category must be readable.
    SmallVector<std::array<ConcatInputSection *, 7>, 2> lists;
    bool mergeable = true;
    for (size_t index : members) {
      std::array<ConcatInputSection *, 7> fieldLists{};
      for (int field = 2; field < 7 && mergeable; ++field) {
        std::optional<ConcatInputSection *> list =
            listOf(categories[index], field);
        if (!list)
          mergeable = false;
        else
          fieldLists[field] = *list;
      }
      lists.push_back(fieldLists);
    }
    if (!mergeable)
      continue;

    const Category &first = categories[members.front()];
    // The merged structures join the first category's section.
    Section *homeSection = nullptr;
    for (Section *section : cast<ObjFile>(first.isec->getFile())->sections)
      for (const Subsection &subsection : section->subsections)
        if (subsection.isec == first.isec)
          homeSection = section;
    if (!homeSection)
      continue;
    Section &home = *homeSection;
    uint64_t nextOffset = 0;
    for (const Subsection &subsection : home.subsections)
      nextOffset = std::max<uint64_t>(
          nextOffset, subsection.offset + subsection.isec->getSize());
    auto addSection = [&](ArrayRef<uint8_t> data, std::vector<Reloc> relocs) {
      auto *isec = make<ConcatInputSection>(home, data, /*align=*/8);
      isec->relocs = std::move(relocs);
      nextOffset = alignTo(nextOffset, 8);
      home.subsections.push_back({nextOffset, isec});
      nextOffset += data.size();
      return isec;
    };

    // The merged lists, the later categories' entries first.
    std::array<ConcatInputSection *, 7> merged{};
    for (int field = 2; field < 7; ++field) {
      const ListKind kind = listKind(field);
      uint64_t count = 0;
      for (const auto &fieldLists : lists)
        if (ConcatInputSection *list = fieldLists[field])
          count += (list->data.size() - kind.headerSize) / kind.entrySize;
      if (count == 0)
        continue;
      const uint64_t size = kind.headerSize + count * kind.entrySize;
      uint8_t *data = bAlloc().Allocate<uint8_t>(size);
      memset(data, 0, size);
      if (kind.countOnly) {
        support::endian::write64le(data, count);
      } else {
        support::endian::write32le(data, kind.entrySize);
        support::endian::write32le(data + 4, count);
      }
      std::vector<Reloc> relocs;
      uint64_t next = kind.headerSize;
      for (auto it = lists.rbegin(); it != lists.rend(); ++it) {
        ConcatInputSection *list = (*it)[field];
        if (!list)
          continue;
        for (const Reloc &entry : list->relocs) {
          Reloc copy = entry;
          copy.offset = next + entry.offset - kind.headerSize;
          relocs.push_back(copy);
        }
        next += list->data.size() - kind.headerSize;
      }
      merged[field] = addSection({data, size}, std::move(relocs));
    }

    // The merged category, named after its members.
    std::string name;
    for (size_t index : members)
      name += (name.empty() ? "" : "|") +
              stringOf(*categories[index].fields[0]).str();
    if (!in.objcMethNames) {
      in.objcMethNames = make<ObjCMethNameSection>();
      in.objcSelRefs = make<ObjCSelRefsSection>();
    }
    const uint64_t nameOffset = in.objcMethNames->addName(saver().save(name));
    uint8_t *data = bAlloc().Allocate<uint8_t>(categorySize);
    memset(data, 0, categorySize);
    support::endian::write32le(data + 56, categorySize);
    std::vector<Reloc> relocs;
    Reloc pointer = *first.fields[0];
    pointer.offset = 0;
    pointer.referent = in.objcMethNames->isec;
    pointer.addend = nameOffset;
    relocs.push_back(pointer);
    Reloc clsRef = *first.fields[1];
    clsRef.offset = 8;
    relocs.push_back(clsRef);
    for (int field = 2; field < 7; ++field)
      if (merged[field]) {
        pointer.offset = 8 * field;
        pointer.referent = merged[field];
        pointer.addend = 0;
        relocs.push_back(pointer);
      }
    ConcatInputSection *category = addSection({data, categorySize}, relocs);

    // __objc_catlist lists the merged category once.
    for (Reloc &r : first.catlist->relocs)
      if (r.offset == first.entry) {
        r.referent = category;
        r.addend = 0;
      }
    for (size_t k = 1; k < members.size(); ++k)
      removedEntries[categories[members[k]].catlist].insert(
          categories[members[k]].entry);
  }

  // Drop the merged-away entries from their lists.
  for (auto &[catlist, entries] : removedEntries) {
    const uint64_t size = catlist->data.size() - 8 * entries.size();
    uint8_t *data = bAlloc().Allocate<uint8_t>(size);
    memset(data, 0, size);
    std::vector<Reloc> relocs;
    for (const Reloc &r : catlist->relocs) {
      if (entries.contains(r.offset))
        continue;
      Reloc copy = r;
      copy.offset -= 8 * llvm::count_if(entries, [&](uint64_t removed) {
                       return removed < r.offset;
                     });
      relocs.push_back(copy);
    }
    catlist->data = {data, size};
    catlist->relocs = std::move(relocs);
  }
}

// -objc_relative_method_lists: each method list's 24-byte entries (name,
// types and implementation pointers) become 12-byte offsets from the entry
// fields, and the list moves to __TEXT,__objc_methlist. A name offset leads
// to a selector reference rather than to the name.
void createRelativeMethodLists() {
  constexpr uint32_t pointerListEntSize = 24, relativeEntSize = 12,
                     relativeFlag = 0x80000000;
  const bool macOS11 =
      config->platform() != PLATFORM_MACOS ||
      config->platformInfo.target.MinDeployment >= VersionTuple(11, 0);
  if (!macOS11) {
    warn("-objc_relative_method_lists: the Objective-C runtime reads relative "
         "method lists from macOS 11; the lists stay as they are");
    return;
  }
  static constexpr StringLiteral listPrefixes[] = {
      "__OBJC_$_INSTANCE_METHODS_", "__OBJC_$_CLASS_METHODS_",
      "__OBJC_$_CATEGORY_INSTANCE_METHODS_",
      "__OBJC_$_CATEGORY_CLASS_METHODS_"};
  DenseMap<CachedHashStringRef, uint64_t> selRefs;
  auto stringAt = [](const Reloc &r) -> std::optional<StringRef> {
    uint64_t offset = r.addend;
    const InputSection *isec = nullptr;
    if (const auto *sym = r.referent.dyn_cast<Symbol *>()) {
      const auto *d = dyn_cast<Defined>(sym);
      if (!d || !d->isec)
        return std::nullopt;
      isec = d->isec;
      offset += d->value;
    } else {
      isec = r.referent.get<InputSection *>();
    }
    if (const auto *cstrings = dyn_cast<CStringInputSection>(isec))
      return cstrings->getStringRefAtOffset(offset);
    return std::nullopt;
  };
  for (const InputFile *file : inputFiles) {
    const auto *obj = dyn_cast<ObjFile>(file);
    if (!obj)
      continue;
    for (const Section *section : obj->sections) {
      if (section->name != "__objc_const")
        continue;
      for (const Subsection &subsection : section->subsections) {
        auto *isec = dyn_cast<ConcatInputSection>(subsection.isec);
        if (!isec || isec->data.size() < 8)
          continue;
        // The list's symbol starts the section, beside section symbols.
        Defined *list = nullptr;
        for (Defined *sym : isec->symbols)
          if (sym->value == 0 &&
              llvm::any_of(listPrefixes, [&](StringRef prefix) {
                return sym->getName().starts_with(prefix);
              }))
            list = sym;
        if (!list)
          continue;
        const uint32_t entSize =
            support::endian::read32le(isec->data.data()) & 0xffff;
        const uint32_t count = support::endian::read32le(isec->data.data() + 4);
        if (entSize != pointerListEntSize ||
            isec->data.size() != 8 + uint64_t(count) * pointerListEntSize)
          continue;

        // The references of each entry's three fields, by field.
        SmallVector<std::array<const Reloc *, 3>, 8> fields(count);
        bool representable = true;
        for (const Reloc &r : isec->relocs) {
          if (r.offset < 8 || r.length != 3 ||
              r.type != target->unsignedRelocType) {
            representable = false;
            break;
          }
          fields[(r.offset - 8) / pointerListEntSize]
                [(r.offset - 8) % pointerListEntSize / 8] = &r;
        }
        if (!representable)
          continue;

        // Selector references for the method names.
        if (!in.objcSelRefs) {
          in.objcMethNames = make<ObjCMethNameSection>();
          in.objcSelRefs = make<ObjCSelRefsSection>();
        }
        std::vector<Reloc> relocs;
        uint8_t *data = bAlloc().Allocate<uint8_t>(8 + count * relativeEntSize);
        memset(data, 0, 8 + count * relativeEntSize);
        support::endian::write32le(data, relativeEntSize | relativeFlag);
        support::endian::write32le(data + 4, count);
        // field - list = (target + addend - fieldOffset) - list.
        auto addDelta = [&](uint64_t fieldOffset,
                            PointerUnion<Symbol *, InputSection *> referent,
                            int64_t addend) {
          relocs.push_back({target->subtractorRelocType, false, 2,
                            uint32_t(fieldOffset), 0, list});
          relocs.push_back({target->unsignedRelocType, false, 2,
                            uint32_t(fieldOffset),
                            addend - int64_t(fieldOffset), referent});
        };
        for (uint32_t m = 0; m < count; ++m) {
          const uint64_t entry = 8 + uint64_t(m) * relativeEntSize;
          if (const Reloc *name = fields[m][0]) {
            std::optional<StringRef> selector = stringAt(*name);
            if (!selector) {
              error(toString(isec) + ": method name is not a string");
              continue;
            }
            auto [it, inserted] =
                selRefs.try_emplace(CachedHashStringRef(*selector), 0);
            if (inserted)
              it->second =
                  in.objcSelRefs->addRef(in.objcMethNames->addName(*selector));
            addDelta(entry, in.objcSelRefs->isec, it->second);
          }
          for (int f = 1; f < 3; ++f)
            if (const Reloc *field = fields[m][f])
              addDelta(entry + 4 * f, field->referent, field->addend);
        }
        isec->data = {data, 8 + count * relativeEntSize};
        isec->relocs = std::move(relocs);
        isec->align = std::max<uint32_t>(isec->align, 4);
        isec->keepUnique = true;
        config->movedSections[isec] = {segment_names::text, "__objc_methlist"};
      }
    }
  }
}

// DTrace static probes: the probe and is-enabled symbols name calls that
// become nops or return 0, and the stability and typedef symbols describe
// the providers. None of them is defined anywhere.
void createDtraceProbes() {
  constexpr StringLiteral probePrefix = "___dtrace_probe$",
                          enabledPrefix = "___dtrace_isenabled$",
                          stabilityPrefix = "___dtrace_stability$",
                          typedefsPrefix = "___dtrace_typedefs$";
  struct ProbeRef {
    StringRef provider, name;
    bool isEnabled;
    SmallVector<std::string, 2> argTypes;
  };
  DenseMap<Symbol *, ProbeRef> refs;
  MapVector<StringRef, std::array<uint32_t, 5>> providers;
  auto defaultAttrs = [] {
    // Internal stability, unknown dependency class.
    constexpr uint32_t attr = (1u << 24) | (1u << 16) | (1u << 8);
    return std::array<uint32_t, 5>{attr, attr, attr, attr, attr};
  };
  SmallVector<Symbol *> symbols;
  for (Symbol *sym : symtab->getSymbols())
    if (isa<Undefined>(sym) && sym->getName().starts_with("___dtrace_"))
      symbols.push_back(sym);
  for (Symbol *sym : symbols) {
    StringRef name = sym->getName();
    SmallVector<StringRef, 8> parts;
    if (name.starts_with(probePrefix) || name.starts_with(enabledPrefix)) {
      const bool isEnabled = name.starts_with(enabledPrefix);
      name.drop_front(isEnabled ? enabledPrefix.size() : probePrefix.size())
          .split(parts, '$');
      if (parts.size() < 3) {
        error("malformed DTrace probe symbol: " + name);
        continue;
      }
      ProbeRef ref{parts[0], "", isEnabled, {}};
      // Probe names spell '-' as "__".
      std::string probe = parts[1].str();
      for (size_t pos; (pos = probe.find("__")) != std::string::npos;)
        probe.replace(pos, 2, "-");
      ref.name = saver().save(probe);
      // Argument types follow the version, hex encoded.
      for (StringRef hex : ArrayRef<StringRef>(parts).drop_front(3)) {
        std::string type;
        for (size_t k = 0; k + 1 < hex.size(); k += 2) {
          unsigned c = 0;
          hex.substr(k, 2).getAsInteger(16, c);
          type.push_back(char(c));
        }
        ref.argTypes.push_back(type);
      }
      providers.try_emplace(ref.provider, defaultAttrs());
      refs[sym] = std::move(ref);
    } else if (name.starts_with(stabilityPrefix)) {
      // <provider>$v1$ then provider, module, function, name and argument
      // attributes, three numbers each.
      name.drop_front(stabilityPrefix.size()).split(parts, '$');
      std::array<uint32_t, 5> attrs = defaultAttrs();
      if (parts.size() >= 3) {
        SmallVector<StringRef, 15> numbers;
        parts[2].split(numbers, '_');
        if (numbers.size() == 15)
          for (int a = 0; a < 5; ++a) {
            unsigned n = 0, d = 0, c = 0;
            numbers[3 * a].getAsInteger(10, n);
            numbers[3 * a + 1].getAsInteger(10, d);
            numbers[3 * a + 2].getAsInteger(10, c);
            attrs[a] = (n << 24) | (d << 16) | (c << 8);
          }
      }
      providers[parts[0]] = attrs;
    } else if (!name.starts_with(typedefsPrefix)) {
      continue;
    }
    symtab
        ->addDefined(sym->getName(), /*file=*/nullptr, /*isec=*/nullptr,
                     /*value=*/0, /*size=*/0, /*isWeakDef=*/false,
                     /*isPrivateExtern=*/true,
                     /*isReferencedDynamically=*/false,
                     /*noDeadStrip=*/false, /*isWeakDefCanBeHidden=*/false)
        ->includeInSymtab = false;
  }
  if (refs.empty())
    return;

  in.dtrace = make<DtraceSupport>();
  DenseMap<StringRef, DofSection *> sections;
  for (auto &[provider, attrs] : providers)
    sections[provider] = in.dtrace->sections.emplace_back(
        make<DofSection>(provider, attrs.data()));
  for (const InputFile *file : inputFiles) {
    const auto *obj = dyn_cast<ObjFile>(file);
    if (!obj)
      continue;
    for (const Section *section : obj->sections)
      for (const Subsection &subsection : section->subsections) {
        auto *isec = dyn_cast<ConcatInputSection>(subsection.isec);
        if (!isec)
          continue;
        llvm::erase_if(isec->relocs, [&](const Reloc &r) {
          auto *sym = r.referent.dyn_cast<Symbol *>();
          auto it = sym ? refs.find(sym) : refs.end();
          if (it == refs.end())
            return false;
          if (!target->hasAttr(r.type, RelocAttrBits::BRANCH)) {
            error(toString(isec) + ": " + sym->getName() +
                  " must be called, not referenced");
            return false;
          }
          const ProbeRef &ref = it->second;
          sections[ref.provider]->sites.push_back(
              {isec, r.offset, ref.isEnabled, ref.name, ref.argTypes});
          // Folding functions would merge different probes.
          isec->keepUnique = true;
          return true;
        });
      }
  }
}

// Defines each `_objc_msgSend$<selector>` that nothing else defines as a
// stub that sends the selector through _objc_msgSend.
void createObjCStubs() {
  SmallVector<Symbol *> refs;
  for (Symbol *sym : symtab->getSymbols())
    if (isa<Undefined>(sym) &&
        sym->getName().starts_with(ObjCStubsSection::symbolPrefix) &&
        sym->getName().size() > ObjCStubsSection::symbolPrefix.size())
      refs.push_back(sym);
  if (refs.empty())
    return;
  in.objcMethNames = make<ObjCMethNameSection>();
  in.objcSelRefs = make<ObjCSelRefsSection>();
  in.objcStubs = make<ObjCStubsSection>();
  in.objcStubs->msgSend =
      symtab->addUndefined("_objc_msgSend", /*file=*/nullptr,
                           /*isWeakRef=*/false);
  for (Symbol *sym : refs)
    in.objcStubs->addEntry(sym);
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
    uint32_t flags = S_ZEROFILL;
    if (config->noZeroFillSections) {
      uint8_t *zeros = bAlloc().Allocate<uint8_t>(common->size);
      memset(zeros, 0, common->size);
      data = {zeros, static_cast<size_t>(common->size)};
      flags = S_REGULAR;
    }
    auto *section = make<Section>(common->getFile(), segment_names::data,
                                  section_names::common, flags, /*addr=*/0);
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
  // -text_exec moves code to its own __TEXT_EXEC segment.
  const StringRef codeSegment =
      config->textExec ? StringRef("__TEXT_EXEC") : segment_names::text;
  config->sectionRenameMap[{segment_names::text, section_names::staticInit}] = {
      codeSegment, section_names::text};
  if (config->textExec) {
    config->sectionRenameMap[{segment_names::text, section_names::text}] = {
        codeSegment, section_names::text};
    config->segmentProtections.push_back({codeSegment,
                                          VM_PROT_READ | VM_PROT_EXECUTE,
                                          VM_PROT_READ | VM_PROT_EXECUTE});
  }
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

// Applies the native linker options for settings the neverc driver derives
// from its own flags; they come later on the command line and override them.
// The output kind, architecture and output path must agree with the driver,
// which chose the startup files, target and post-link steps for them.
LinkerDriverConfig applyLinkerOptions(InputArgList &args,
                                      const LinkerDriverConfig &driverCfg) {
  LinkerDriverConfig cfg = driverCfg;
  auto conflict = [&](const Arg *arg, StringRef use) {
    error(arg->getAsString(args) +
          " conflicts with the compiler's link settings; pass " + use +
          " to the compiler instead");
  };
  if (const Arg *arg = args.getLastArg(OPT_o))
    if (arg->getValue() != cfg.outputFile)
      conflict(arg, "-o");
  if (const Arg *arg = args.getLastArg(OPT_arch)) {
    if (cfg.archName.empty())
      cfg.archName = arg->getValue();
    else if (arg->getValue() != cfg.archName)
      conflict(arg, "-arch");
  }
  if (const Arg *arg =
          args.getLastArg(OPT_dylib, OPT_bundle, OPT_execute, OPT_r)) {
    const unsigned id = arg->getOption().getID();
    const bool agrees =
        id == OPT_dylib    ? cfg.shared
        : id == OPT_bundle ? cfg.bundle
        : id == OPT_r      ? cfg.relocatable
                           : !cfg.shared && !cfg.bundle && !cfg.relocatable;
    if (!agrees)
      conflict(arg, id == OPT_dylib    ? "-dynamiclib"
                    : id == OPT_bundle ? "-bundle"
                    : id == OPT_r      ? "-r"
                                       : "no output kind option");
  }
  if (const Arg *arg = args.getLastArg(OPT_static, OPT_dynamic))
    if (arg->getOption().matches(OPT_static) != cfg.staticLink)
      conflict(arg, arg->getOption().matches(OPT_static) ? "-static"
                                                         : "no -static");
  // arm64 executables are always position independent; link() warns.
  if (const Arg *arg = args.getLastArg(OPT_pie, OPT_no_pie))
    if (arg->getOption().matches(OPT_pie) || cfg.archName != "arm64")
      cfg.pie = arg->getOption().matches(OPT_pie);
  for (unsigned id :
       {OPT_ios_version_min, OPT_ios_simulator_version_min,
        OPT_maccatalyst_version_min, OPT_tvos_version_min,
        OPT_watchos_version_min, OPT_bridgeos_version_min,
        OPT_driverkit_version_min})
    if (const Arg *arg = args.getLastArg(id))
      error(arg->getSpelling() + ": only macOS targets are supported");
  if (const Arg *arg = args.getLastArg(OPT_platform_version)) {
    cfg.platformName = arg->getValue(0);
    cfg.platformMinVersion = arg->getValue(1);
    cfg.platformSdkVersion = arg->getValue(2);
  }
  if (const Arg *arg = args.getLastArg(OPT_macos_version_min)) {
    cfg.platformName = "macos";
    cfg.platformMinVersion = arg->getValue();
  }
  if (const Arg *arg = args.getLastArg(OPT_sdk_version))
    cfg.platformSdkVersion = arg->getValue();

  if (args.hasArg(OPT_dead_strip))
    cfg.gcSections = true;
  if (const Arg *arg = args.getLastArg(OPT_map))
    cfg.mapFile = arg->getValue();
  if (args.hasArg(OPT_S) && cfg.stripMode == StripMode::None)
    cfg.stripMode = StripMode::DebugInfo;
  cfg.traceFiles = cfg.traceFiles || args.hasArg(OPT_t);
  cfg.verbose = cfg.verbose || args.hasArg(OPT_verbose);
  cfg.suppressWarnings = cfg.suppressWarnings || args.hasArg(OPT_w);
  cfg.fatalWarnings = cfg.fatalWarnings || args.hasArg(OPT_fatal_warnings);
  if (args.hasArg(OPT_error_limit_eq))
    cfg.errorLimit = args::getInteger(args, OPT_error_limit_eq, 20);
  if (args.hasArg(OPT_O))
    cfg.linkerOptLevel = args::getInteger(args, OPT_O, 1);
  if (const Arg *arg = args.getLastArg(OPT_icf_eq)) {
    StringRef level = arg->getValue();
    if (level == "none")
      cfg.icfLevel = 0;
    else if (level == "safe")
      cfg.icfLevel = 1;
    else if (level == "all")
      cfg.icfLevel = 2;
    else
      error("unsupported --icf=" + level + "; use none, safe or all");
  }
  if (const Arg *arg = args.getLastArg(OPT_call_graph_profile_sort,
                                       OPT_no_call_graph_profile_sort))
    cfg.callGraphProfileSort =
        arg->getOption().matches(OPT_call_graph_profile_sort) ? "cdsort"
                                                              : "none";
  if (const Arg *arg = args.getLastArg(OPT_print_symbol_order_eq))
    cfg.printSymbolOrder = arg->getValue();
  cfg.repro = cfg.repro || args.hasArg(OPT_reproducible);
  cfg.saveTemps = cfg.saveTemps || args.hasArg(OPT_save_temps);
  if (args.hasArg(OPT_time_trace_eq)) {
    cfg.timeTraceEnabled = true;
    cfg.timeTraceGranularity =
        args::getInteger(args, OPT_time_trace_granularity_eq, 500);
  }

  // LTO code generation.
  auto level = [&](const Arg *arg, int &out) {
    unsigned v;
    if (!to_integer(arg->getValue(), v) || v > 3)
      error(arg->getSpelling() + ": invalid optimization level: " +
            arg->getValue());
    else
      out = int(v);
  };
  if (const Arg *arg = args.getLastArg(OPT_lto_O))
    level(arg, cfg.ltoOptLevel);
  if (const Arg *arg = args.getLastArg(OPT_lto_CGO))
    level(arg, cfg.ltoCGOLevel);
  if (const Arg *arg = args.getLastArg(OPT_mcpu))
    cfg.cpu = arg->getValue();
  for (const Arg *arg : args.filtered(OPT_mllvm))
    cfg.mllvmOpts.push_back(arg->getValue());
  if (const Arg *arg = args.getLastArg(OPT_lto_newpm_passes))
    cfg.ltoOptPipeline = arg->getValue();
  cfg.ltoDebugPassManager =
      cfg.ltoDebugPassManager || args.hasArg(OPT_lto_debug_pass_manager);
  for (const Arg *arg : args.filtered(OPT_load_pass_plugins))
    cfg.ltoPassPlugins.push_back(arg->getValue());
  cfg.ltoCodeGenOnly = cfg.ltoCodeGenOnly || args.hasArg(OPT_flto_codegen_only);
  return cfg;
}

void applyColorDiagnostics(const InputArgList &args) {
  const Arg *arg = args.getLastArg(OPT_color_diagnostics,
                                   OPT_color_diagnostics_eq,
                                   OPT_no_color_diagnostics);
  if (!arg)
    return;
  StringRef mode = arg->getOption().matches(OPT_color_diagnostics) ? "always"
                   : arg->getOption().matches(OPT_no_color_diagnostics)
                       ? "never"
                       : arg->getValue();
  raw_ostream &os = linker::errs();
  if (mode == "always")
    os.enable_colors(true);
  else if (mode == "never")
    os.enable_colors(false);
  else if (mode == "auto")
    os.enable_colors(os.has_colors());
  else
    error("unknown option: --color-diagnostics=" + mode);
}

ICFLevel getICFFromDriver(int driverLevel) {
  if (driverLevel >= 2)
    return ICFLevel::all;
  if (driverLevel == 1)
    return ICFLevel::safe;
  return ICFLevel::none;
}

void warnIfDeprecatedOption(const Option &opt) {
  if (!opt.getGroup().isValid() ||
      opt.getGroup().getID() != OPT_grp_deprecated)
    return;
  warn("Option `" + opt.getPrefixedName() + "' is deprecated: " +
       opt.getHelpText());
}

// Native linker options NeverC accepts for compatibility without
// implementing them.
void warnIfUnimplementedOption(const Option &opt) {
  if (!opt.getGroup().isValid() || !opt.hasFlag(DriverFlag::HelpHidden))
    return;
  switch (opt.getGroup().getID()) {
  case OPT_grp_ignored_silently:
    break;
  case OPT_grp_ignored:
    warn("Option `" + opt.getPrefixedName() + "' is ignored");
    break;
  case OPT_grp_obsolete:
    warn("Option `" + opt.getPrefixedName() +
         "' is obsolete and has no effect");
    break;
  default:
    warn("Option `" + opt.getPrefixedName() +
         "' is not supported and has no effect: " + opt.getHelpText());
    break;
  }
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
  // The chain starts sections describe chained fixups.
  const bool startsSection =
      config->fixupChainsSection || config->threadedStartsSection;
  if (arg && arg->getOption().matches(OPT_no_fixup_chains)) {
    if (startsSection)
      error("-fixup_chains_section and -threaded_starts_section need "
            "chained fixups, which -no_fixup_chains turns off");
    return false;
  }

  bool isRequested = arg != nullptr || startsSection;

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

  // Apple's ld enables chained fixups by default on supported platforms.
  // Match that behavior: enable unless the user passed --no-fixup-chains.
  return true;
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

// -alias_list: each line names a symbol and the alias to create for it.
void parseAliasList(const Arg *arg) {
  StringRef path = arg->getValue();
  std::optional<MemoryBufferRef> buffer = readFile(path);
  if (!buffer)
    return;
  for (StringRef line : args::getLines(*buffer)) {
    line = line.take_until([](char c) { return c == '#'; }).trim();
    if (line.empty())
      continue;
    auto [name, alias] = getToken(line);
    alias = alias.trim();
    if (alias.empty() || alias.find_first_of(" \t") != StringRef::npos) {
      error(path + ": malformed alias line '" + line +
            "', expected '<symbol> <alias>'");
      continue;
    }
    config->aliasedSymbols.push_back({name, alias});
  }
}

// Parses A[.B[.C[.D[.E]]]] with A < 2^24 and the rest < 2^10, the packing
// LC_SOURCE_VERSION uses.
std::optional<uint64_t> parseSourceVersion(StringRef str) {
  SmallVector<StringRef, 5> parts;
  str.split(parts, '.');
  if (parts.size() > 5)
    return std::nullopt;
  uint64_t version = 0;
  for (size_t i = 0; i < 5; ++i) {
    uint64_t part = 0;
    if (i < parts.size() && (parts[i].getAsInteger(10, part) ||
                             part >= (i == 0 ? 1u << 24 : 1u << 10)))
      return std::nullopt;
    version = i == 0 ? part : (version << 10) | part;
  }
  return version;
}

// The name a dylib's LC_SUB_CLIENT list must hold for this output to link to
// it directly: -client_name, the output's name, or for a dylib its library
// name without the "lib" prefix and any variant suffix.
StringRef clientNameForOutput() {
  if (!config->clientName.empty())
    return config->clientName;
  if (config->outputType != MH_DYLIB)
    return sys::path::filename(config->finalOutput);
  StringRef name =
      sys::path::filename(config->installName).take_until([](char c) {
        return c == '.' || c == '_';
      });
  name.consume_front("lib");
  return name;
}

// A dylib that is part of an umbrella may only be linked directly by the
// umbrella itself, its other sub-frameworks, and the clients it lists.
void checkDirectDylibClients() {
  StringRef ourName =
      config->outputType == MH_DYLIB
          ? sys::path::stem(config->installName).take_until([](char c) {
              return c == '.' || c == '_';
            })
          : StringRef();
  StringRef client = clientNameForOutput();
  for (InputFile *file : inputFiles) {
    auto *dylib = dyn_cast<DylibFile>(file);
    if (!dylib || !dylib->isExplicitlyLinked() || dylib->parentUmbrella.empty())
      continue;
    StringRef parent = dylib->parentUmbrella;
    if (parent == ourName || parent == config->umbrella ||
        is_contained(dylib->allowableClients, client))
      continue;
    error("cannot link directly with '" + toString(dylib) +
          "': it is part of the umbrella '" + parent + "' and '" + client +
          "' is not one of its allowed clients; link against the umbrella "
          "or pass -client_name");
  }
}

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

// -dot writes which symbols each live symbol's section refers to, as a
// Graphviz graph.
void writeDependencyGraph(StringRef path) {
  std::error_code ec;
  raw_fd_ostream os(path, ec, sys::fs::OF_Text);
  if (ec) {
    error("-dot: cannot open " + path + ": " + ec.message());
    return;
  }
  auto quote = [](StringRef s) {
    std::string out = "\"";
    for (char c : s) {
      if (c == '"' || c == '\\')
        out += '\\';
      out += c;
    }
    return out + "\"";
  };
  os << "digraph dependencies {\n";
  for (const InputFile *file : inputFiles) {
    if (!isa<ObjFile>(file))
      continue;
    for (const Symbol *sym : file->symbols) {
      const auto *defined = dyn_cast_or_null<Defined>(sym);
      if (!defined || !defined->isLive() || !defined->isec ||
          defined->value != 0)
        continue;
      const auto *isec = dyn_cast<ConcatInputSection>(defined->isec);
      if (!isec || !isec->live)
        continue;
      SetVector<std::string, SmallVector<std::string, 8>, std::set<std::string>>
          targets;
      for (const Reloc &r : isec->relocs) {
        if (const auto *target = r.referent.dyn_cast<Symbol *>())
          targets.insert(toString(*target));
        else if (const auto *target = r.referent.dyn_cast<InputSection *>())
          targets.insert(toString(target));
      }
      for (const std::string &target : targets)
        os << "  " << quote(toString(*defined)) << " -> " << quote(target)
           << ";\n";
    }
  }
  os << "}\n";
}

// -move_to_ro_segment and -move_to_rw_segment send the listed code or
// read-only data, or writable data, to another segment.
void moveSymbolsToSegments(const InputArgList &args) {
  for (const Arg *arg :
       args.filtered(OPT_move_to_ro_segment, OPT_move_to_rw_segment)) {
    const bool readOnly = arg->getOption().matches(OPT_move_to_ro_segment);
    StringRef segment = arg->getValue(0);
    SymbolPatterns patterns;
    std::optional<MemoryBufferRef> buffer = readFile(arg->getValue(1));
    if (!buffer)
      continue;
    for (StringRef line : args::getLines(*buffer)) {
      line = line.take_until([](char c) { return c == '#'; }).trim();
      if (!line.empty())
        patterns.insert(line);
    }
    if (readOnly && llvm::none_of(config->segmentProtections,
                                  [&](const SegmentProtection &p) {
                                    return p.name == segment;
                                  }))
      config->segmentProtections.push_back({segment,
                                            VM_PROT_READ | VM_PROT_EXECUTE,
                                            VM_PROT_READ | VM_PROT_EXECUTE});
    for (const InputFile *file : inputFiles) {
      if (!isa<ObjFile>(file))
        continue;
      for (Symbol *sym : file->symbols) {
        auto *defined = dyn_cast_or_null<Defined>(sym);
        if (!defined || !defined->isec ||
            !isa<ConcatInputSection>(defined->isec) ||
            !patterns.match(defined->getName()))
          continue;
        const bool fromReadOnly =
            defined->isec->getSegName() == segment_names::text;
        if (fromReadOnly != readOnly)
          continue;
        config->movedSections[defined->isec] = {segment,
                                                defined->isec->getName()};
        if (config->traceSymbolLayout)
          message(Twine(arg->getSpelling()) + ": " + toString(*defined) +
                  " moves from " + defined->isec->getSegName() + "," +
                  defined->isec->getName() + " to " + segment + "," +
                  defined->isec->getName());
      }
    }
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

void createFiles(const InputArgList &args, unsigned RequestedThreads) {
  TimeTraceScope timeScope("Load input files");
  prefetchInputFiles(args, RequestedThreads);
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
    // Upward dependencies are loaded with LC_LOAD_UPWARD_DYLIB, which breaks
    // initializer-order cycles between libraries.
    case OPT_upward_l:
      addLibrary(arg->getValue(), false, false, false, false,
                 /*isExplicit=*/true, LoadType::CommandLine, /*isUpward=*/true);
      break;
    case OPT_upward_framework:
      addFramework(arg->getValue(), false, false, false, /*isExplicit=*/true,
                   LoadType::CommandLine, /*isUpward=*/true);
      break;
    case OPT_upward_library:
      if (auto *dylibFile = dyn_cast_or_null<DylibFile>(
              addFile(rerootPath(arg->getValue()), LoadType::CommandLine)))
        dylibFile->upward = true;
      break;
    // Lazy loading is deprecated; these link as ordinary dependencies.
    case OPT_lazy_l:
      addLibrary(arg->getValue(), false, false, false, false,
                 /*isExplicit=*/true, LoadType::CommandLine);
      break;
    case OPT_lazy_framework:
      addFramework(arg->getValue(), false, false, false, /*isExplicit=*/true,
                   LoadType::CommandLine);
      break;
    case OPT_lazy_library:
      addFile(rerootPath(arg->getValue()), LoadType::CommandLine);
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
        if (!config->movedSections.empty() &&
            config->movedSections.count(isec)) {
          isec->parent = ConcatOutputSection::getOrCreateForInput(isec);
          inputSections.push_back(isec);
          currentConcatSection = nullptr;
          break;
        }
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
          const LinkerDriverConfig &callerCfg) {
  // An explicit --threads= replaces the caller's budget for the whole link,
  // including the early pool setup that runs before option parsing.
  std::optional<LinkerDriverConfig> threadOverride;
  if (unsigned threads = args::findThreadCountArg(
          argsArr.drop_front(), {"--threads="}, /*windowsQuoting=*/false)) {
    threadOverride.emplace(callerCfg);
    threadOverride->threadCount = threads;
  }
  const LinkerDriverConfig &baseCfg =
      threadOverride ? *threadOverride : callerCfg;
  // Declared first: crash recovery must not unwind it before the backend.
  std::optional<linker::crash_recovery_detail::CrashRecoveryTimeTraceOwner>
      TraceProfiler;
  // A trace the caller asked for is acquired before any backend state exists,
  // so a link refused for a busy profiler leaves nothing behind; --time-trace
  // on the command line starts it after parsing.
  const bool GuardAmbientTimeTrace = !baseCfg.timeTraceEnabled &&
                                     llvm::CrashRecoveryContext::GetCurrent() &&
                                     llvm::timeTraceProfilerEnabled();
  if (baseCfg.timeTraceEnabled || GuardAmbientTimeTrace)
    TraceProfiler.emplace(baseCfg.timeTraceGranularity,
                          argsArr.empty() ? "neverc" : argsArr.front());
  if (llvm::StringRef Error =
          TraceProfiler ? TraceProfiler->acquisitionError() : llvm::StringRef();
      !Error.empty()) {
    stderrOS << Error << '\n';
    return false;
  }
  linker::crash_recovery_detail::CrashRecoveryLocalOwner<LinkerExecutionContext>
      ExecutionOwner(baseCfg.executionContext);
  LinkerExecutionContext &Execution = ExecutionOwner.get();
  MachOLinkerContext &Backend = Execution.createBackend<MachOLinkerContext>();
  llvm::CrashRecoveryContextCleanupRegistrar<
      LinkerExecutionContext,
      linker::crash_recovery_detail::CrashRecoveryDestroyBackendCleanup<
          LinkerExecutionContext>>
      CrashBackend(baseCfg.executionContext ? &Execution : nullptr);
  CommonLinkerContext &Common = Backend;

  Common.e.initialize(stdoutOS, stderrOS, exitEarly, disableOutput);
  Common.e.cleanupCallback = []() {
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
    finishMemberNameInterning();
    resetDeferredRelocations();
  };

  Common.e.logName = args::getFilenameWithoutExe(argsArr[0]);

  MachOOptTable parser;
  InputArgList args = parser.parse(argsArr.slice(1));
  applyColorDiagnostics(args);
  const LinkerDriverConfig driverCfg = applyLinkerOptions(args, baseCfg);
  if (errorCount())
    return false;

  if (args.hasArg(OPT_help) || args.hasArg(OPT_help_hidden)) {
    parser.printHelp(linker::outs(),
                     (Twine(argsArr[0]) + " [options] file...").str().c_str(),
                     "NeverC Mach-O linker", args.hasArg(OPT_help_hidden),
                     /*ShowAllAliases=*/true);
    return true;
  }
  if (args.hasArg(OPT_version)) {
    message(neverc::getNeverCFullVersion());
    return true;
  }
  if (args.hasArg(OPT_version_details)) {
    linker::outs() << "{\"version\":\""
                   << StringRef(neverc::getNeverCFullVersion())
                   << "\",\"architectures\":[\"arm64\",\"x86_64\"]}\n";
    return true;
  }

  if (driverCfg.timeTraceEnabled && !TraceProfiler) {
    TraceProfiler.emplace(driverCfg.timeTraceGranularity,
                          argsArr.empty() ? "neverc" : argsArr.front());
    if (llvm::StringRef Error = TraceProfiler->acquisitionError();
        !Error.empty()) {
      stderrOS << Error << '\n';
      return false;
    }
  }
  auto WriteTrace = [&](llvm::StringRef OutputFile) {
    if (driverCfg.timeTraceEnabled && TraceProfiler)
      checkError(TraceProfiler->neverc::LLVMTimeTraceProfilerOwner::write(
          args.getLastArgValue(OPT_time_trace_eq), OutputFile));
  };

  Common.e.errorLimitExceededMsg = "too many errors emitted, stopping now "
                                   "(use -ferror-limit=0 to see all errors)";
  Common.e.errorLimit = driverCfg.errorLimit;
  Common.e.verbose = driverCfg.verbose;
  Common.e.fatalWarnings = driverCfg.fatalWarnings;
  Common.e.suppressWarnings = driverCfg.suppressWarnings;
  if (const Arg *arg = args.getLastArg(OPT_pie, OPT_no_pie))
    if (arg->getOption().matches(OPT_no_pie) && driverCfg.archName == "arm64")
      warn("-no_pie ignored for arm64");

  config = std::make_unique<Configuration>();
  symtab = std::make_unique<SymbolTable>();
  config->outputType = getOutputType(driverCfg);
  // -dylinker, -preload and -kext choose image kinds the compiler driver
  // does not: they start from its executable or bundle output.
  if (const Arg *arg = args.getLastArg(OPT_dylinker, OPT_preload, OPT_kext)) {
    const HeaderFileType kind =
        arg->getOption().matches(OPT_dylinker)  ? MH_DYLINKER
        : arg->getOption().matches(OPT_preload) ? MH_PRELOAD
                                                : MH_KEXT_BUNDLE;
    const bool fromBundle = config->outputType == MH_BUNDLE;
    if (config->outputType != MH_EXECUTE &&
        !(kind == MH_KEXT_BUNDLE && fromBundle))
      error(arg->getAsString(args) + ": link an executable" +
            (kind == MH_KEXT_BUNDLE ? " or a bundle" : "") +
            " with the compiler driver to produce this kind of image");
    config->outputType = kind;
  }
  target = createTargetInfo(driverCfg);
  depTracker = std::make_unique<DependencyTracker>(
      args.getLastArgValue(OPT_dependency_info));

  if (errorCount()) {
    WriteTrace(driverCfg.outputFile);
    return false;
  }

  // Only a main executable is loaded above a __PAGEZERO.
  if (config->outputType != MH_EXECUTE && !args.hasArg(OPT_pagezero_size))
    target->pageZeroSize = 0;
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
                  config->outputType == MH_DYLINKER ||
                  config->outputType == MH_KEXT_BUNDLE ||
                  (config->outputType == MH_EXECUTE && pie);

  config->deadStrip = driverCfg.gcSections;

  config->systemLibraryRoots = getSystemLibraryRoots(args, driverCfg);

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
  config->printDylibSearch =
      driverCfg.verbose || args.hasArg(OPT_print_dylib_search);
  config->printEachFile = driverCfg.traceFiles;
  config->printWhyLoad = args.hasArg(OPT_why_load);
  config->omitDebugInfo = driverCfg.stripsDebugInfo();
  config->stripAllSymbols = driverCfg.stripsSymbols();
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
  config->fixupChainsSection = args.hasArg(OPT_fixup_chains_section);
  config->threadedStartsSection = args.hasArg(OPT_threaded_starts_section);
  if (config->fixupChainsSection && config->threadedStartsSection)
    error("-fixup_chains_section and -threaded_starts_section describe the "
          "same chains in different forms; pass one of them");
  config->simulatorSupport = args.hasArg(OPT_simulator_support);
  config->allowSimulatorLinkingToMacOSDylibs =
      args.hasArg(OPT_allow_simulator_linking_to_macosx_dylibs);
  config->emitChainedFixups = shouldEmitChainedFixups(args);
  config->emitInitOffsets =
      config->emitChainedFixups || args.hasArg(OPT_init_offsets);
  config->icfLevel = getICFFromDriver(driverCfg.icfLevel);
  config->dedupStrings =
      args.hasFlag(OPT_deduplicate_strings, OPT_no_deduplicate_strings, true);
  config->deadStripDuplicates =
      args.hasArg(OPT_dead_strip_duplicates, OPT_allow_dead_duplicates);
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
  config->randomUuid = args.hasArg(OPT_random_uuid);
  config->dedupSymbolStrings = !args.hasArg(OPT_no_deduplicate_symbol_strings);
  config->whatsLoaded = args.hasArg(OPT_whatsloaded);
  config->noWeakImports = args.hasArg(OPT_no_weak_imports);
  config->noWeakExports = args.hasArg(OPT_no_weak_exports);
  config->warnWeakExports = args.hasArg(OPT_warn_weak_exports);
  config->bindAtLoad = args.hasArg(OPT_bind_at_load);
  config->clientName = args.getLastArgValue(OPT_client_name);
  config->executablePath = args.getLastArgValue(OPT_executable_path);
  config->initFunction = args.getLastArgValue(OPT_init);
  config->noBranchIslands = args.hasArg(OPT_no_branch_islands);
  config->rootSafe = args.hasArg(OPT_root_safe);
  config->setuidSafe = args.hasArg(OPT_setuid_safe);
  config->forceCpuSubtypeAll = args.hasArg(OPT_force_cpusubtype_ALL);
  config->noInits = args.hasArg(OPT_no_inits);
  config->noArchWarnings = args.hasArg(OPT_no_arch_warnings);
  config->verboseDeduplicate = args.hasArg(OPT_verbose_deduplicate);
  config->pageAlignDataAtoms = args.hasArg(OPT_page_align_data_atoms);
  config->noZeroFillSections = args.hasArg(OPT_no_zero_fill_sections);
  config->mergeZeroFillSections = args.hasArg(OPT_merge_zero_fill_sections);
  config->orderFileStatistics = args.hasArg(OPT_order_file_statistics);
  config->noOrderData = args.hasArg(OPT_no_order_data);
  config->warnStabs = args.hasArg(OPT_warn_stabs);
  config->forceLoadObjC = args.hasArg(OPT_ObjC);
  config->interposable = args.hasArg(OPT_interposable);
  config->traceSymbolLayout = args.hasArg(OPT_trace_symbol_layout);
  config->forceLoadSwiftLibs = args.hasArg(OPT_force_load_swift_libs);
  config->warnCompactUnwind = args.hasArg(OPT_warn_compact_unwind);
  config->verboseOptimizationHints =
      args.hasArg(OPT_verbose_optimization_hints);
  config->textExec = args.hasArg(OPT_text_exec);
  config->noNewMain = args.hasArg(OPT_no_new_main);
  config->noCompactUnwind = args.hasArg(OPT_no_compact_unwind);
  config->addSplitSegInfo = args.hasArg(OPT_add_split_seg_info);
  config->keepRelocs = args.hasArg(OPT_keep_relocs);
  config->objcCategoryMerging = args.hasFlag(
      OPT_objc_category_merging, OPT_no_objc_category_merging, false);
  config->objcRelativeMethodLists = args.hasFlag(
      OPT_objc_relative_method_lists, OPT_no_objc_relative_method_lists, false);
  for (const Arg *arg : args.filtered(OPT_force_symbol_weak))
    config->forceWeakSymbols.insert(arg->getValue());
  for (const Arg *arg : args.filtered(OPT_force_symbol_not_weak))
    config->forceNotWeakSymbols.insert(arg->getValue());
  for (const Arg *arg : args.filtered(OPT_force_symbols_coalesce_list))
    parseSymbolPatternsFile(arg, config->forceCoalesceSymbols);
  // -i<definition>:<indirect> is the older spelling of -alias.
  for (const Arg *arg : args.filtered(OPT_i)) {
    auto [definition, indirect] = StringRef(arg->getValue()).split(':');
    if (definition.empty() || indirect.empty())
      error(arg->getAsString(args) + ": expected -i<definition>:<indirect>");
    else
      config->aliasedSymbols.push_back({indirect, definition});
  }
  if (const Arg *arg = args.getLastArg(OPT_dtrace))
    if (!sys::fs::exists(arg->getValue()))
      error(arg->getAsString(args) + ": no such file");
  config->debugSnapshot = args.hasArg(OPT_debug_snapshot);
  config->snapshotDir = args.getLastArgValue(OPT_snapshot_dir);
  if (!args.hasArg(OPT_no_kext_objects))
    config->kextObjectsDir = args.getLastArgValue(OPT_kext_objects_dir);
  if (!config->kextObjectsDir.empty() && config->outputType != MH_KEXT_BUNDLE)
    warn("-kext_objects_dir: has no effect without -kext");
  if (args.hasArg(OPT_add_linker_option) && config->outputType != MH_OBJECT)
    warn("-add_linker_option: has no effect without -r");
  if (const Arg *arg =
          args.getLastArg(OPT_objc_stubs_fast, OPT_objc_stubs_small)) {
    config->objcStubsSmall = arg->getOption().matches(OPT_objc_stubs_small);
    if (config->objcStubsSmall && config->arch() != AK_arm64) {
      warn(arg->getAsString(args) + " is only available on arm64; using "
                                    "-objc_stubs_fast");
      config->objcStubsSmall = false;
    }
  }
  if (const Arg *arg = args.getLastArg(OPT_segment_order)) {
    if (config->outputType != MH_PRELOAD)
      error(arg->getAsString(args) + ": only valid with -preload");
    SmallVector<StringRef> segments;
    StringRef(arg->getValue()).split(segments, ':', -1, false);
    config->segmentOrder.assign(segments.begin(), segments.end());
  }
  for (const Arg *arg : args.filtered(OPT_section_order)) {
    if (config->outputType != MH_PRELOAD)
      error(arg->getAsString(args) + ": only valid with -preload");
    SmallVector<StringRef> sections;
    StringRef(arg->getValue(1)).split(sections, ':', -1, false);
    auto &order = config->sectionOrder[arg->getValue(0)];
    order.assign(sections.begin(), sections.end());
  }
  if (config->noNewMain && config->outputType != MH_EXECUTE)
    error("-no_new_main: only valid when linking a main executable");
  config->keepDwarfUnwind =
      args.hasFlag(OPT_keep_dwarf_unwind, OPT_no_keep_dwarf_unwind, false);
#if !defined(_WIN32)
  // -pause stops the linker until a debugger or SIGCONT resumes it.
  if (args.hasArg(OPT_pause)) {
    message("-pause: process " + Twine(sys::Process::getProcessId()) +
            " stopped; send SIGCONT to continue");
    raise(SIGSTOP);
  }
#endif
  if (const Arg *arg = args.getLastArg(OPT_exported_symbols_order))
    if (std::optional<MemoryBufferRef> buffer = readFile(arg->getValue()))
      for (StringRef line : args::getLines(*buffer)) {
        line = line.take_until([](char c) { return c == '#'; }).trim();
        if (!line.empty())
          config->exportedSymbolsOrder.try_emplace(
              CachedHashStringRef(line), config->exportedSymbolsOrder.size());
      }
  if (const Arg *arg = args.getLastArg(OPT_unaligned_pointers)) {
    std::optional<ReadOnlyRelocs> mode =
        StringSwitch<std::optional<ReadOnlyRelocs>>(arg->getValue())
            .Case("error", ReadOnlyRelocs::Error)
            .Case("warning", ReadOnlyRelocs::Warning)
            .Case("suppress", ReadOnlyRelocs::Suppress)
            .Default(std::nullopt);
    if (mode)
      config->unalignedPointers = *mode;
    else
      error(arg->getAsString(args) + ": expected error, warning or suppress");
  }
  if (const Arg *arg = args.getLastArg(OPT_weak_reference_mismatches)) {
    std::optional<WeakReferenceMismatches> mode =
        StringSwitch<std::optional<WeakReferenceMismatches>>(arg->getValue())
            .Case("non-weak", WeakReferenceMismatches::NonWeak)
            .Case("weak", WeakReferenceMismatches::Weak)
            .Case("error", WeakReferenceMismatches::Error)
            .Default(std::nullopt);
    if (mode)
      config->weakReferenceMismatches = *mode;
    else
      error(arg->getAsString(args) + ": expected error, weak or non-weak");
  }
  if (const Arg *arg = args.getLastArg(OPT_read_only_relocs)) {
    std::optional<ReadOnlyRelocs> mode =
        StringSwitch<std::optional<ReadOnlyRelocs>>(arg->getValue())
            .Case("error", ReadOnlyRelocs::Error)
            .Case("warning", ReadOnlyRelocs::Warning)
            .Case("suppress", ReadOnlyRelocs::Suppress)
            .Default(std::nullopt);
    if (mode)
      config->readOnlyRelocs = *mode;
    else
      error(arg->getAsString(args) + ": expected error, warning or suppress");
  }
  for (const Arg *arg : args.filtered(OPT_interposable_list))
    parseSymbolPatternsFile(arg, config->interposableSymbols);
  for (const Arg *arg : args.filtered(OPT_force_symbols_weak_list))
    parseSymbolPatternsFile(arg, config->forceWeakSymbols);
  for (const Arg *arg : args.filtered(OPT_force_symbols_not_weak_list))
    parseSymbolPatternsFile(arg, config->forceNotWeakSymbols);
  for (const Arg *arg : args.filtered(OPT_seg_page_size)) {
    uint64_t size = 0;
    StringRef value = arg->getValue(1);
    value.consume_front_insensitive("0x");
    if (value.getAsInteger(16, size) || !isPowerOf2_64(size) ||
        size < target->getPageSize())
      error(arg->getAsString(args) + ": expected a hex power-of-2 page " +
            "size of at least 0x" + Twine::utohexstr(target->getPageSize()));
    else
      config->segmentPageSizes[arg->getValue(0)] = size;
  }
  // Linked images always define their tentative definitions; a relocatable
  // output keeps them tentative.
  if (const Arg *arg = args.getLastArg(OPT_d);
      arg && config->outputType == MH_OBJECT)
    warn(arg->getAsString(args) +
         ": relocatable output keeps tentative "
         "definitions; the option has no effect with -r");
  config->warnCommons = args.hasArg(OPT_warn_commons);
  config->printStatistics = args.hasArg(OPT_print_statistics);
  if (const Arg *arg = args.getLastArg(OPT_commons)) {
    std::optional<CommonsTreatment> treatment =
        StringSwitch<std::optional<CommonsTreatment>>(arg->getValue())
            .Case("ignore_dylibs", CommonsTreatment::IgnoreDylibs)
            .Case("use_dylibs", CommonsTreatment::UseDylibs)
            .Case("error", CommonsTreatment::Error)
            .Default(std::nullopt);
    if (treatment)
      config->commons = *treatment;
    else
      error(arg->getAsString(args) +
            ": expected ignore_dylibs, use_dylibs or error");
  }
  if (const Arg *arg = args.getLastArg(OPT_max_default_common_align)) {
    config->maxDefaultCommonAlign =
        args::getHex(args, OPT_max_default_common_align, 0);
    if (!isPowerOf2_64(config->maxDefaultCommonAlign))
      error(arg->getAsString(args) + ": alignment must be a power of 2");
  }
  if (const Arg *arg = args.getLastArg(OPT_segalign)) {
    config->segmentAlign = args::getHex(args, OPT_segalign, 0);
    if (!isPowerOf2_64(config->segmentAlign) ||
        config->segmentAlign < target->getPageSize())
      error(arg->getAsString(args) + ": alignment must be a power of 2 and " +
            "at least the page size (0x" +
            Twine::utohexstr(target->getPageSize()) + ")");
  }
  for (const Arg *arg : args.filtered(OPT_segaddr)) {
    uint64_t address = 0;
    StringRef value = arg->getValue(1);
    value.consume_front_insensitive("0x");
    if (value.getAsInteger(16, address) ||
        !isAligned(Align(target->getPageSize()), address))
      error(arg->getAsString(args) + ": expected a hex address that is a " +
            "multiple of the page size (0x" +
            Twine::utohexstr(target->getPageSize()) + ")");
    else
      config->segmentAddresses[arg->getValue(0)] = address;
  }
  for (const Arg *arg : args.filtered(OPT_reexported_symbols_list)) {
    if (config->outputType != MH_DYLIB)
      error(arg->getAsString(args) + ": only valid with -dylib");
    parseSymbolPatternsFile(arg, config->reexportedSymbols);
  }
  if (!config->initFunction.empty() && config->outputType != MH_DYLIB)
    error("-init: only valid with -dylib");

  if (const Arg *arg = args.getLastArg(OPT_stack_size)) {
    if (config->outputType != MH_EXECUTE)
      error("-stack_size: only valid when linking a main executable");
    config->stackSize = args::getHex(args, OPT_stack_size, 0);
    if (!isAligned(Align(target->getPageSize()), config->stackSize))
      error(arg->getAsString(args) + ": stack size must be a multiple of " +
            "the page size (0x" + Twine::utohexstr(target->getPageSize()) +
            ")");
  }
  if (const Arg *arg = args.getLastArg(OPT_allow_stack_execute)) {
    if (config->outputType != MH_EXECUTE)
      error(arg->getAsString(args) +
            ": only valid when linking a main executable");
    config->allowStackExecute = true;
  }
  if (const Arg *arg = args.getLastArg(OPT_force_flat_namespace)) {
    if (config->outputType != MH_EXECUTE)
      error(arg->getAsString(args) +
            ": only valid when linking a main executable");
    config->forceFlatNamespace = true;
  }
  if (const Arg *arg = args.getLastArg(OPT_image_base)) {
    config->imageBase = args::getHex(args, OPT_image_base, 0);
    if (!isAligned(Align(target->getPageSize()), *config->imageBase))
      error(arg->getAsString(args) + ": address must be a multiple of the " +
            "page size (0x" + Twine::utohexstr(target->getPageSize()) + ")");
    // An executable's first segment follows __PAGEZERO.
    if (config->outputType == MH_EXECUTE) {
      if (!args.hasArg(OPT_pagezero_size))
        target->pageZeroSize = *config->imageBase;
      else if (target->pageZeroSize != *config->imageBase)
        error(arg->getAsString(args) + ": an executable's image base is the " +
              "end of __PAGEZERO, which -pagezero_size sets differently");
    }
  }
  for (const Arg *arg : args.filtered(OPT_allowable_client)) {
    if (config->outputType != MH_DYLIB)
      error(arg->getAsString(args) + ": only valid with -dylib");
    config->allowableClients.push_back(arg->getValue());
  }
  for (const Arg *arg : args.filtered(OPT_dylib_file)) {
    auto [installPath, currentPath] = StringRef(arg->getValue()).split(':');
    if (installPath.empty() || currentPath.empty())
      error(arg->getAsString(args) +
            ": expected <install_path>:<current_path>");
    else
      config->dylibFiles[installPath] = currentPath;
  }
  if (const Arg *arg = args.getLastArg(
          OPT_add_source_version, OPT_no_source_version, OPT_source_version))
    if (!arg->getOption().matches(OPT_no_source_version)) {
      config->sourceVersion = 0;
      if (const Arg *v = args.getLastArg(OPT_source_version)) {
        config->sourceVersion = parseSourceVersion(v->getValue());
        if (!config->sourceVersion)
          error(v->getAsString(args) + ": malformed version, expected " +
                "A[.B[.C[.D[.E]]]]");
      }
    }
  {
    const bool warnDuplicates = args.hasFlag(OPT_warn_duplicate_rpath,
                                             OPT_no_warn_duplicate_rpath, true);
    llvm::StringSet<> seen;
    llvm::erase_if(config->runtimePaths, [&](StringRef path) {
      if (seen.insert(path).second)
        return false;
      if (warnDuplicates)
        warn("duplicate -rpath '" + path +
             "' ignored [--warn-duplicate-rpath]");
      return true;
    });
  }

  for (const Arg *arg : args.filtered(OPT_alias)) {
    config->aliasedSymbols.push_back(
        std::make_pair(arg->getValue(0), arg->getValue(1)));
  }
  for (const Arg *arg : args.filtered(OPT_alias_list))
    parseAliasList(arg);

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

  // -force_flat_namespace makes this image flat as well as its dependents.
  if (config->forceFlatNamespace)
    config->namespaceKind = NamespaceKind::flat;

  // A kext's references to the kernel are bound when the kernel loads it.
  if (config->outputType == MH_KEXT_BUNDLE) {
    if (!args.hasArg(OPT_flat_namespace, OPT_twolevel_namespace))
      config->namespaceKind = NamespaceKind::flat;
  }
  config->undefinedSymbolTreatment = getUndefinedSymbolTreatment(args);
  if (config->outputType == MH_KEXT_BUNDLE && !args.hasArg(OPT_undefined))
    config->undefinedSymbolTreatment = UndefinedSymbolTreatment::dynamic_lookup;

  if (config->outputType == MH_EXECUTE)
    config->entry = symtab->addUndefined(args.getLastArgValue(OPT_e, "_main"),
                                         /*file=*/nullptr,
                                         /*isWeakRef=*/false);
  // dyld and preloaded images start at their entry point directly.
  else if (config->outputType == MH_DYLINKER ||
           config->outputType == MH_PRELOAD)
    config->entry = symtab->addUndefined(args.getLastArgValue(OPT_e, "start"),
                                         /*file=*/nullptr,
                                         /*isWeakRef=*/false);

  config->librarySearchPaths =
      getLibrarySearchPaths(args, config->systemLibraryRoots);
  config->frameworkSearchPaths =
      getFrameworkSearchPaths(args, config->systemLibraryRoots);
  if (args.hasArg(OPT_v)) {
    message(neverc::getNeverCFullVersion(), linker::errs());
    message(StringRef("Library search paths:") +
                (config->librarySearchPaths.empty()
                     ? ""
                     : "\n\t" + join(config->librarySearchPaths, "\n\t")),
            linker::errs());
    message(StringRef("Framework search paths:") +
                (config->frameworkSearchPaths.empty()
                     ? ""
                     : "\n\t" + join(config->frameworkSearchPaths, "\n\t")),
            linker::errs());
  }
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
  if (config->stripAllSymbols)
    config->localSymbolsPresence = SymtabPresence::None;
  else if (driverCfg.stripLocals &&
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

  const bool finishedRelocatable = [&] {
    TimeTraceScope timeScope("ExecuteLinker");

    initLLVM(); // must be run before any call to addFile()
    beginDeferredRelocations();
    beginMemberNameInterning();
    createFiles(args, driverCfg.threadCount);
    finishMemberNameInterning();
    if (config->debugSnapshot)
      writeDebugSnapshot(argsArr);
    // A tiny direct set may still discover a large auto-linked archive below.
    // Select a parallel budget now when justified, but do not permanently
    // lock an automatic one-thread result until those late inputs are known.
    configureParallelismForMaterializedInputs(driverCfg,
                                              /*FinalizeSerial=*/false);
    finishDeferredRelocations();

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
    checkDirectDylibClients();

    if (!config->initFunction.empty())
      config->initSymbol =
          symtab->addUndefined(config->initFunction, /*file=*/nullptr,
                               /*isWeakRef=*/false);

    createSyntheticSections();
    if (config->addSplitSegInfo)
      in.splitSegInfo = make<SplitSegInfoSection>();
    if (config->keepRelocs)
      in.keptRelocs = make<KeptRelocsSection>();
    if (config->emitChainedFixups &&
        (config->fixupChainsSection || config->threadedStartsSection))
      in.chainStarts = make<ChainStartsSection>(config->threadedStartsSection);
    if (config->textExec)
      in.stubs->segname = "__TEXT_EXEC";
    createSyntheticSymbols();

    createAliases();
    handleExplicitExports();

    bool didCompileBitcodeFiles = compileBitcodeFiles();

    resolveLCLinkerOptions();

    // -sectcreate payloads become output sections below. Read and account for
    // them before the automatic decision without changing input-file order.
    const LinkInputWorkload SectCreateWorkload =
        prefetchSectCreatePayloads(args);

    // The resolver preflights each LC_LINKER_OPTION batch before publishing
    // lazy symbols. Finalize the automatic serial decision here only after all
    // recursively discovered options and materialized inputs are known.
    configureParallelismForMaterializedInputs(driverCfg,
                                              /*FinalizeSerial=*/true,
                                              SectCreateWorkload);

    if (didCompileBitcodeFiles)
      handleExplicitExports();
    replaceCommonSymbols();
    createObjCStubs();
    if (config->objcCategoryMerging)
      mergeObjCCategories();
    if (config->objcRelativeMethodLists)
      createRelativeMethodLists();
    createDtraceProbes();
    // -reexported_symbols_list re-exports these dependent dylib symbols.
    if (!config->reexportedSymbols.empty())
      for (Symbol *sym : symtab->getSymbols())
        if (auto *dysym = dyn_cast<DylibSymbol>(sym))
          if (config->reexportedSymbols.match(dysym->getName()))
            dysym->shouldReexport = true;

    if (config->outputType == MH_OBJECT) {
      {
        TimeTraceScope mergeScope("Relocatable merge");
        SmallVector<StringRef, 32> buffers;
        for (InputFile *f : inputFiles)
          if (isa<ObjFile>(f))
            buffers.push_back(f->mb.getBuffer());

        neverc::merge::Options mergeOpts;
        mergeOpts.pureC = true;
        // -add_linker_option records auto-link options in the object.
        for (const Arg *arg : args.filtered(OPT_add_linker_option)) {
          SmallVector<StringRef> parts;
          StringRef(arg->getValue()).split(parts, ' ', -1, false);
          mergeOpts.machoLinkerOptions.emplace_back(parts.begin(), parts.end());
        }

        std::error_code ec;
        raw_fd_ostream out(config->outputFile, ec, sys::fs::OF_None);
        if (ec)
          error("cannot open " + config->outputFile + ": " + ec.message());
        else if (!neverc::merge::mergeObjects(
                     buffers, out, neverc::merge::Format::MachO64, mergeOpts))
          error("relocatable merge failed");
      }
      return true;
    }

    StringRef orderFile = args.getLastArgValue(OPT_order_file);
    if (!orderFile.empty())
      priorityBuilder.parseOrderFile(orderFile);
    // -dirty_data_list groups the data it names after the ordered symbols.
    for (const Arg *arg : args.filtered(OPT_dirty_data_list))
      priorityBuilder.parseOrderFile(arg->getValue());

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

    if (config->whatsLoaded)
      for (const InputFile *file : inputFiles)
        if (isa<ObjFile>(file) && !file->lazy)
          message(toString(file));

    moveSymbolsToSegments(args);
    gatherInputSections();
    if (config->callGraphProfileSort)
      priorityBuilder.extractCallGraphProfile();

    if (config->deadStrip)
      markLive();
    if (const Arg *arg = args.getLastArg(OPT_dot))
      writeDependencyGraph(arg->getValue());

    foldIdenticalLiterals();
    if (config->icfLevel != ICFLevel::none) {
      if (config->icfLevel == ICFLevel::safe)
        markAddrSigSymbols();
      foldIdenticalSections(/*onlyCfStrings=*/false);
    } else if (config->dedupStrings) {
      foldIdenticalSections(/*onlyCfStrings=*/true);
    }

    writeOutput<LP64>();
    if (config->outputType == MH_KEXT_BUNDLE &&
        !config->kextObjectsDir.empty() && errorCount() == 0)
      writeKextObjects();

    depTracker->write(inputFiles, config->outputFile);
    return false;
  }();

  if (finishedRelocatable) {
    // LLVM requires every time-trace scope to be balanced before writing.
    WriteTrace(config->outputFile);
    return errorCount() == 0;
  }

  if (errorCount() != 0 || config->strictAutoLink)
    for (const auto &warning : missingAutolinkWarnings)
      warn(warning);

  if (config->printStatistics) {
    size_t objects = 0, dylibs = 0;
    for (const InputFile *file : inputFiles) {
      objects += isa<ObjFile>(file) && !file->lazy;
      dylibs += isa<DylibFile>(file);
    }
    sys::TimePoint<> elapsed;
    std::chrono::nanoseconds user, system;
    sys::Process::GetTimeUsage(elapsed, user, system);
    using std::chrono::duration_cast;
    using std::chrono::milliseconds;
    std::string peak;
#if !defined(_WIN32)
    struct rusage usage = {};
    getrusage(RUSAGE_SELF, &usage);
#ifdef __APPLE__
    const uint64_t peakKiB = uint64_t(usage.ru_maxrss) >> 10;
#else
    const uint64_t peakKiB = uint64_t(usage.ru_maxrss);
#endif
    peak = (", " + Twine(peakKiB >> 10) + " MiB peak memory").str();
#endif
    message("statistics: " + Twine(objects) + " object files, " +
            Twine(dylibs) + " dylibs, " + Twine(symtab->getSymbols().size()) +
            " symbols; " + Twine(duration_cast<milliseconds>(user).count()) +
            " ms user, " + Twine(duration_cast<milliseconds>(system).count()) +
            " ms system" + peak);
  }

  WriteTrace(config->outputFile);
  return errorCount() == 0;
}
} // namespace macho
} // namespace linker
