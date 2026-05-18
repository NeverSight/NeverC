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

} // namespace args
} // namespace linker

#endif // LINKER_CORE_DRIVER_ARGLIST_H
