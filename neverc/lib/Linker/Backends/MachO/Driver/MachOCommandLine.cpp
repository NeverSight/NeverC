#include "Linker/MachO/Config.h"
#include "Linker/MachO/Driver.h"
#include "Linker/MachO/InputFiles.h"
#include "Linker/MachO/Target.h"

#include "Linker/Core/Driver/ArgList.h"
#include "Linker/Core/Runtime/Session.h"
#include "llvm/ADT/CachedHashString.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/LTO/LTO.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/TextAPI/InterfaceFile.h"
#include "llvm/TextAPI/TextAPIReader.h"

using namespace llvm;
using namespace llvm::MachO;
using namespace llvm::opt;
using namespace llvm::sys;
using namespace linker;
using namespace linker::macho;

// Create prefix string literals used in Options.td
#define PREFIX(NAME, VALUE)                                                    \
  static constexpr StringLiteral NAME##_init[] = VALUE;                        \
  static constexpr ArrayRef<StringLiteral> NAME(NAME##_init,                   \
                                                std::size(NAME##_init) - 1);
#include "Options.td.h"
#undef PREFIX

// ===----------------------------------------------------------------------===
// MachOOptTable: command-line argument parsing
// ===----------------------------------------------------------------------===

// Create table mapping all options defined in Options.td
namespace {
constexpr OptTable::Info optInfo[] = {
#define OPTION(PREFIX, NAME, ID, KIND, GROUP, ALIAS, ALIASARGS, FLAGS,         \
               VISIBILITY, PARAM, HELPTEXT, METAVAR, VALUES)                   \
  {PREFIX,      NAME,        HELPTEXT,                                         \
   METAVAR,     OPT_##ID,    opt::Option::KIND##Class,                         \
   PARAM,       FLAGS,       VISIBILITY,                                       \
   OPT_##GROUP, OPT_##ALIAS, ALIASARGS,                                        \
   VALUES},
#include "Options.td.h"
#undef OPTION
};
} // namespace

MachOOptTable::MachOOptTable() : GenericOptTable(optInfo) {}

InputArgList MachOOptTable::parse(ArrayRef<const char *> argv) {
  unsigned missingIndex;
  unsigned missingCount;
  SmallVector<const char *, 256> vec(argv.data(), argv.data() + argv.size());

  InputArgList args = ParseArgs(vec, missingIndex, missingCount);

  if (missingCount)
    error(Twine(args.getArgString(missingIndex)) + ": missing argument");

  for (const Arg *arg : args.filtered(OPT_UNKNOWN)) {
    std::string nearest;
    if (findNearest(arg->getAsString(args), nearest) > 1)
      error("unknown argument '" + arg->getAsString(args) + "'");
    else
      error("unknown argument '" + arg->getAsString(args) +
            "', did you mean '" + nearest + "'");
  }
  return args;
}

// ===----------------------------------------------------------------------===
// Dylib path resolution
// ===----------------------------------------------------------------------===

namespace {
void searchedDylib(const Twine &path, bool found) {
  if (config->printDylibSearch)
    message("searched " + path + (found ? ", found " : ", not found"));
  if (!found)
    depTracker->logFileNotFound(path);
}
} // namespace

std::optional<StringRef> macho::resolveDylibPath(StringRef dylibPath) {
  SmallString<261> tbdPath = dylibPath;
  path::replace_extension(tbdPath, ".tbd");
  bool tbdExists = fs::exists(tbdPath);
  searchedDylib(tbdPath, tbdExists);
  if (tbdExists)
    return saver().save(tbdPath.str());

  bool dylibExists = fs::exists(dylibPath);
  searchedDylib(dylibPath, dylibExists);
  if (dylibExists)
    return saver().save(dylibPath);
  return {};
}

// It's not uncommon to have multiple attempts to load a single dylib,
// especially if it's a commonly re-exported core library.
namespace {
DenseMap<CachedHashStringRef, DylibFile *> loadedDylibs;
} // namespace

// ===----------------------------------------------------------------------===
// Dylib loading & caching
// ===----------------------------------------------------------------------===

DylibFile *macho::loadDylib(MemoryBufferRef mbref, DylibFile *umbrella,
                            bool isBundleLoader, bool explicitlyLinked) {
  CachedHashStringRef path(mbref.getBufferIdentifier());
  DylibFile *&file = loadedDylibs[path];
  if (file) {
    if (explicitlyLinked)
      file->setExplicitlyLinked();
    return file;
  }

  DylibFile *newFile;
  file_magic magic = identify_magic(mbref.getBuffer());
  if (magic == file_magic::tapi_file) {
    Expected<std::unique_ptr<InterfaceFile>> result = TextAPIReader::get(mbref);
    if (!result) {
      error("could not load TAPI file at " + mbref.getBufferIdentifier() +
            ": " + llvm::toString(result.takeError()));
      return nullptr;
    }
    file =
        make<DylibFile>(**result, umbrella, isBundleLoader, explicitlyLinked);

    // parseReexports() can recursively call loadDylib(). That's fine since
    // we wrote the DylibFile we just loaded to the loadDylib cache via the
    // `file` reference. But the recursive load can grow loadDylibs, so the
    // `file` reference might become invalid after parseReexports() -- so copy
    // the pointer it refers to before continuing.
    newFile = file;
    if (newFile->exportingFile)
      newFile->parseReexports(**result);
  } else {
    assert(magic == file_magic::macho_dynamically_linked_shared_lib ||
           magic == file_magic::macho_dynamically_linked_shared_lib_stub ||
           magic == file_magic::macho_executable ||
           magic == file_magic::macho_bundle);
    file = make<DylibFile>(mbref, umbrella, isBundleLoader, explicitlyLinked);

    // parseLoadCommands() can also recursively call loadDylib(). See comment
    // in previous block for why this means we must copy `file` here.
    newFile = file;
    if (newFile->exportingFile)
      newFile->parseLoadCommands(mbref);
  }
  return newFile;
}

void macho::resetLoadedDylibs() { loadedDylibs.clear(); }

std::optional<StringRef>
macho::findPathCombination(const Twine &name,
                           const std::vector<StringRef> &roots,
                           ArrayRef<StringRef> extensions) {
  SmallString<261> base;
  for (StringRef dir : roots) {
    base = dir;
    path::append(base, name);
    for (StringRef ext : extensions) {
      Twine location = base + ext;
      bool exists = fs::exists(location);
      searchedDylib(location, exists);
      if (exists)
        return saver().save(location.str());
    }
  }
  return {};
}

// ===----------------------------------------------------------------------===
// Path & metadata utilities
// ===----------------------------------------------------------------------===

StringRef macho::rerootPath(StringRef path) {
  if (!path::is_absolute(path, path::Style::posix) || path.ends_with(".o"))
    return path;

  if (std::optional<StringRef> rerootedPath =
          findPathCombination(path, config->systemLibraryRoots))
    return *rerootedPath;

  return path;
}

uint32_t macho::getModTime(StringRef path) {
  if (config->zeroModTime)
    return 0;

  fs::file_status stat;
  if (!fs::status(path, stat))
    if (fs::exists(stat))
      return toTimeT(stat.getLastModificationTime());

  warn("failed to get modification time of " + path);
  return 0;
}

void macho::printArchiveMemberLoad(StringRef reason, const InputFile *f) {
  if (config->printEachFile)
    message(toString(f));
  if (config->printWhyLoad)
    message(reason + " forced load of " + toString(f));
}

// ===----------------------------------------------------------------------===
// DependencyTracker: -dependency_info output
// ===----------------------------------------------------------------------===

macho::DependencyTracker::DependencyTracker(StringRef path)
    : path(path), active(!path.empty()) {
  if (active && fs::exists(path) && !fs::can_write(path)) {
    warn("Ignoring dependency_info option since specified path is not "
         "writeable.");
    active = false;
  }
}

void macho::DependencyTracker::write(const SetVector<InputFile *> &inputs,
                                     StringRef output) {
  if (!active)
    return;

  std::error_code ec;
  raw_fd_ostream os(path, ec, fs::OF_None);
  if (ec) {
    warn("Error writing dependency info to file");
    return;
  }

  auto addDep = [&os](DepOpCode opcode, const StringRef &path) {
    os << static_cast<uint8_t>(opcode);
    os << path;
    os << '\0';
  };

  // Sort the input by its names.
  std::vector<StringRef> inputNames;
  inputNames.reserve(inputs.size());
  for (InputFile *f : inputs)
    inputNames.push_back(f->getName());
  llvm::sort(inputNames);

  for (const StringRef &in : inputNames)
    addDep(DepOpCode::Input, in);

  for (const std::string &f : notFounds)
    addDep(DepOpCode::NotFound, f);

  addDep(DepOpCode::Output, output);
}
