#ifndef LINKER_ELF_DRIVER_H
#define LINKER_ELF_DRIVER_H

#include "Linker/Core/Support/LlvmAliases.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Option/ArgList.h"
#include <optional>

namespace linker::elf {
// Parses command line options.
class ELFOptTable : public llvm::opt::GenericOptTable {
public:
  ELFOptTable();
  llvm::opt::InputArgList parse(ArrayRef<const char *> argv);
};

// Create enum with OPT_xxx values for each option in Options.td
enum {
  OPT_INVALID = 0,
#define OPTION(...) LLVM_MAKE_OPT_ID(__VA_ARGS__),
#include "Options.td.h"
#undef OPTION
};

std::optional<std::string> findFromSearchPaths(StringRef path);
std::optional<std::string> searchScript(StringRef path);
std::optional<std::string> searchLibraryBaseName(StringRef path);
std::optional<std::string> searchLibrary(StringRef path);

} // namespace linker::elf

#endif
