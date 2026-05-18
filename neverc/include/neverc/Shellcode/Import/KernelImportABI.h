#ifndef NEVERC_SHELLCODE_KERNELIMPORTABI_H
#define NEVERC_SHELLCODE_KERNELIMPORTABI_H

#include "neverc/Shellcode/Pipeline/Diagnostics.h"
#include "llvm/ADT/StringRef.h"
#include <cstdint>

namespace neverc {
namespace shellcode {
namespace KernelResolverABI {

inline constexpr uint64_t NameHashOffset = 0xcbf29ce484222325ULL;
inline constexpr uint64_t NameHashPrime = 0x100000001b3ULL;

inline uint64_t hashName(llvm::StringRef Name) {
  uint64_t H = NameHashOffset;
  for (char C : Name) {
    H ^= static_cast<uint64_t>(static_cast<uint8_t>(C));
    H *= NameHashPrime;
  }
  return H;
}

inline constexpr llvm::StringLiteral ResolverGlobalName =
    "__neverc_kern_resolver";
inline constexpr llvm::StringLiteral CookieGlobalName = "__neverc_kern_cookie";
inline constexpr llvm::StringLiteral OrigEntryRenameSuffix = "__kern_orig";
inline constexpr llvm::StringLiteral LoaderResolverFunctionName =
    "__neverc_kern_resolve";
inline constexpr llvm::StringLiteral DiagnosticPrefix =
    Diagnostics::KernelImportPrefix;

inline constexpr llvm::StringLiteral AddressTakenExternHint =
    "Direct calls are supported and are rewritten through the resolver, "
    "but a function pointer would need resolver/cookie state outside the "
    "entry function. Call the helper directly, or pass a resolved "
    "function pointer from the loader.";

inline constexpr llvm::StringLiteral MissingEntryHint =
    "kernel-mode shellcode with extern dependencies requires a "
    "recognisable entry";

namespace IRNames {

inline constexpr llvm::StringLiteral ResolverLoad = "kern.resolver";
inline constexpr llvm::StringLiteral CookieLoad = "kern.cookie";
inline constexpr llvm::StringLiteral ResolvedCallee = "kern.fn";

}

}
}
}

#endif
