#ifndef NEVERC_DYNCODE_DIAGNOSTICS_H
#define NEVERC_DYNCODE_DIAGNOSTICS_H

#include "llvm/ADT/StringRef.h"

namespace neverc {
namespace dyncode {
namespace Diagnostics {

inline constexpr llvm::StringLiteral ExtractorPrefix = "dyncode-extractor: ";
inline constexpr llvm::StringLiteral MIRPrefix = "dyncode-mir: ";
inline constexpr llvm::StringLiteral KernelImportPrefix = "dyncode-kernel: ";

}
}
}

#endif
