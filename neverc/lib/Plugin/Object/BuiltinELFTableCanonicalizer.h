#ifndef NEVERC_LIB_PLUGIN_OBJECT_BUILTINELFTABLECANONICALIZER_H
#define NEVERC_LIB_PLUGIN_OBJECT_BUILTINELFTABLECANONICALIZER_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

namespace neverc::plugin {

/// Rebuild only the selected ELF symbol/section name tables and their dependent
/// offsets. DropDebugInfo additionally filters debug sections and remaps the
/// surviving section/symbol indices it makes stale. All retained records stay
/// in their original order, and no caller-owned bytes are changed.
llvm::Expected<llvm::SmallVector<char, 0>>
canonicalizeBuiltinELFTables(llvm::StringRef Input, bool DropDebugInfo = false);

} // namespace neverc::plugin

#endif
