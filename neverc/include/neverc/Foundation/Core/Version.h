#ifndef NEVERC_BASIC_VERSION_H
#define NEVERC_BASIC_VERSION_H

#include "neverc/Foundation/Core/Version.inc"
#include "llvm/ADT/StringRef.h"

namespace neverc {
std::string getNeverCFullRepositoryVersion();

std::string getNeverCFullVersion();

std::string getNeverCToolFullVersion(llvm::StringRef ToolName);

std::string getNeverCFullVersionForMacro();
} // namespace neverc

#endif // NEVERC_BASIC_VERSION_H
