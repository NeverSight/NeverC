//===----------------------------------------------------------------------===//
//
//  ArgList — thin typed accessors over `llvm::opt::InputArgList`.
//
//  Backends pull option values through these helpers (`getInteger`,
//  `getHex`, `getStrings`, `getZOptionValue`, `getLines`, ...) instead of
//  calling `Arg::getValue()` directly, so the parse-failure diagnostics
//  stay consistent across flavors.
//
//===----------------------------------------------------------------------===//

#ifndef LINKER_CORE_DRIVER_ARGLIST_H
#define LINKER_CORE_DRIVER_ARGLIST_H

#include "Linker/Core/Support/LlvmAliases.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/MemoryBuffer.h"
#include <vector>

namespace llvm {
namespace opt {
class InputArgList;
}
} // namespace llvm

namespace linker {
namespace args {

int getCGOptLevel(int optLevelLTO);

int64_t getInteger(llvm::opt::InputArgList &args, unsigned key,
                   int64_t Default);

int64_t getHex(llvm::opt::InputArgList &args, unsigned key, int64_t Default);

llvm::SmallVector<StringRef, 0> getStrings(llvm::opt::InputArgList &args,
                                           int id);

uint64_t getZOptionValue(llvm::opt::InputArgList &args, int id, StringRef key,
                         uint64_t Default);

std::vector<StringRef> getLines(MemoryBufferRef mb);

StringRef getFilenameWithoutExe(StringRef path);

/// Returns the worker-thread budget named by the last argument that starts
/// with one of \p spellings (for example "--threads="), or 0 when none does or
/// its value is not a positive integer. Response files are expanded with
/// Windows or GNU quoting first. Backends call this before creating their
/// worker pool; the option table still validates and diagnoses the value.
unsigned findThreadCountArg(ArrayRef<const char *> argv,
                            ArrayRef<StringRef> spellings, bool windowsQuoting);

} // namespace args
} // namespace linker

#endif // LINKER_CORE_DRIVER_ARGLIST_H
