#include "Linker/Core/Runtime/Session.h"
#include "Linker/ELF/Config.h"
#include "Linker/ELF/Driver.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/TimeProfiler.h"
#include <optional>

using namespace llvm;
using namespace llvm::sys;
using namespace llvm::opt;
using namespace linker;
using namespace linker::elf;

// Create OptTable

// Create prefix string literals used in Options.td
#define PREFIX(NAME, VALUE)                                                    \
  static constexpr StringLiteral NAME##_init[] = VALUE;                        \
  static constexpr ArrayRef<StringLiteral> NAME(NAME##_init,                   \
                                                std::size(NAME##_init) - 1);
#include "Options.td.h"
#undef PREFIX

// Create table mapping all options defined in Options.td
namespace {
constexpr opt::OptTable::Info optInfo[] = {
#define OPTION(...) LLVM_CONSTRUCT_OPT_INFO(__VA_ARGS__),
#include "Options.td.h"
#undef OPTION
};
} // namespace

ELFOptTable::ELFOptTable() : GenericOptTable(optInfo) {}

// Parses a given list of options.
opt::InputArgList ELFOptTable::parse(ArrayRef<const char *> argv) {
  unsigned missingIndex;
  unsigned missingCount;
  SmallVector<const char *, 256> vec(argv.data(), argv.data() + argv.size());

  opt::InputArgList args = this->ParseArgs(vec, missingIndex, missingCount);

  if (missingCount)
    error(Twine(args.getArgString(missingIndex)) + ": missing argument");

  for (opt::Arg *arg : args.filtered(OPT_UNKNOWN)) {
    std::string nearest;
    if (findNearest(arg->getAsString(args), nearest) > 1)
      error("unknown argument '" + arg->getAsString(args) + "'");
    else
      error("unknown argument '" + arg->getAsString(args) +
            "', did you mean '" + nearest + "'");
  }
  return args;
}

// Find a file by concatenating given paths. If a resulting path
// starts with "=", the character is replaced with a --sysroot value.
namespace {
std::optional<std::string> findFile(StringRef path1, const Twine &path2) {
  SmallString<128> s;
  if (path1.starts_with("="))
    path::append(s, config->sysroot, path1.substr(1), path2);
  else
    path::append(s, path1, path2);

  if (fs::exists(s))
    return std::string(s);
  return std::nullopt;
}
} // namespace

std::optional<std::string> elf::findFromSearchPaths(StringRef path) {
  for (StringRef dir : config->searchPaths)
    if (std::optional<std::string> s = findFile(dir, path))
      return s;
  return std::nullopt;
}

// This is for -l<basename>. We'll look for lib<basename>.so or lib<basename>.a
// from search paths.
std::optional<std::string> elf::searchLibraryBaseName(StringRef name) {
  for (StringRef dir : config->searchPaths) {
    if (!config->isStatic)
      if (std::optional<std::string> s = findFile(dir, "lib" + name + ".so"))
        return s;
    if (std::optional<std::string> s = findFile(dir, "lib" + name + ".a"))
      return s;
  }
  return std::nullopt;
}

// This is for -l<namespec>.
std::optional<std::string> elf::searchLibrary(StringRef name) {
  llvm::TimeTraceScope timeScope("Locate library", name);
  if (name.starts_with(":"))
    return findFromSearchPaths(name.substr(1));
  return searchLibraryBaseName(name);
}

// Look up a script in the '-L' search paths when the file isn't present in
// the current working directory. Mirrors the path-resolution used by `-T`,
// `--version-script=`, and the INPUT() script command.
std::optional<std::string> elf::searchScript(StringRef name) {
  if (fs::exists(name))
    return name.str();
  return findFromSearchPaths(name);
}
