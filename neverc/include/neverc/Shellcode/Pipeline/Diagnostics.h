#ifndef NEVERC_SHELLCODE_DIAGNOSTICS_H
#define NEVERC_SHELLCODE_DIAGNOSTICS_H

#include "llvm/ADT/StringRef.h"

namespace neverc {
namespace shellcode {
namespace Diagnostics {

inline constexpr llvm::StringLiteral ExtractorPrefix = "shellcode-extractor: ";
inline constexpr llvm::StringLiteral MIRPrefix = "shellcode-mir: ";
inline constexpr llvm::StringLiteral KernelImportPrefix = "shellcode-kernel: ";

}
}
}

#endif
