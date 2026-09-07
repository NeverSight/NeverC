#ifndef NEVERC_TRANSLATE_ARTIFACTPLATFORM_H
#define NEVERC_TRANSLATE_ARTIFACTPLATFORM_H
#include "llvm/ADT/StringRef.h"
#include <system_error>
namespace neverc::translate {
/// Atomic directory rename that refuses an existing destination.
std::error_code renameDirectoryExclusive(llvm::StringRef From,
                                         llvm::StringRef To);
} // namespace neverc::translate
#endif
