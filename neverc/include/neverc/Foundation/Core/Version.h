#ifndef NEVERC_FOUNDATION_VERSION_H
#define NEVERC_FOUNDATION_VERSION_H

#include "neverc/Foundation/Core/Version.inc"
#include "llvm/ADT/StringRef.h"

namespace neverc {
std::string getNeverCFullRepositoryVersion();

std::string getNeverCFullVersion();

std::string getNeverCToolFullVersion(llvm::StringRef ToolName);

std::string getNeverCFullVersionForMacro();
} // namespace neverc

#endif // NEVERC_FOUNDATION_VERSION_H
