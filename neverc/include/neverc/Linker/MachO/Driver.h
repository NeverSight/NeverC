#ifndef LINKER_MACHO_DRIVER_H
#define LINKER_MACHO_DRIVER_H

#include "Linker/Core/Support/LlvmAliases.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/Option/OptTable.h"
#include "llvm/Support/MemoryBuffer.h"
#include <optional>

#include <set>
#include <type_traits>

namespace linker::macho {

class DylibFile;
class InputFile;

class MachOOptTable : public llvm::opt::GenericOptTable {
public:
  MachOOptTable();
  llvm::opt::InputArgList parse(ArrayRef<const char *> argv);
};

// Create enum with OPT_xxx values for each option in Options.td
enum {
  OPT_INVALID = 0,
#define OPTION(...) LLVM_MAKE_OPT_ID(__VA_ARGS__),
#include "Options.td.h"
#undef OPTION
};

void parseLCLinkerOption(llvm::SmallVectorImpl<StringRef> &LCLinkerOptions,
                         InputFile *f, unsigned argc, StringRef data);
void resolveLCLinkerOptions();

// Check for both libfoo.dylib and libfoo.tbd (in that order).
std::optional<StringRef> resolveDylibPath(llvm::StringRef path);

DylibFile *loadDylib(llvm::MemoryBufferRef mbref, DylibFile *umbrella = nullptr,
                     bool isBundleLoader = false,
                     bool explicitlyLinked = false);
void resetLoadedDylibs();

// Search for all possible combinations of `{root}/{name}.{extension}`.
// If \p extensions are not specified, then just search for `{root}/{name}`.
std::optional<llvm::StringRef>
findPathCombination(const llvm::Twine &name,
                    const std::vector<llvm::StringRef> &roots,
                    ArrayRef<llvm::StringRef> extensions = {""});

// If -syslibroot is specified, absolute paths to non-object files may be
// rerooted.
llvm::StringRef rerootPath(llvm::StringRef path);

uint32_t getModTime(llvm::StringRef path);

void printArchiveMemberLoad(StringRef reason, const InputFile *);

// Helper class to export dependency info.
class DependencyTracker {
public:
  explicit DependencyTracker(llvm::StringRef path);

  // Adds the given path to the set of not-found files.
  inline void logFileNotFound(const Twine &path) {
    if (active)
      notFounds.insert(path.str());
  }

  // Writes the dependencies to specified path. The content is first sorted by
  // OpCode and then by the filename (in alphabetical order).
  void write(const llvm::SetVector<InputFile *> &inputs,
             llvm::StringRef output);

private:
  enum DepOpCode : uint8_t {
    // Denotes the linker version.
    Version = 0x00,
    // Denotes the input files.
    Input = 0x10,
    // Denotes the files that do not exist(?)
    NotFound = 0x11,
    // Denotes the output files.
    Output = 0x40,
  };

  const llvm::StringRef path;
  bool active;

  // The paths need to be alphabetically ordered.
  // We need to own the paths because some of them are temporarily
  // constructed.
  std::set<std::string> notFounds;
};

extern std::unique_ptr<DependencyTracker> depTracker;

} // namespace linker::macho

#endif
