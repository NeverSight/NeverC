#ifndef NEVERC_DYNCODE_KERNELIMPORTABI_H
#define NEVERC_DYNCODE_KERNELIMPORTABI_H

#include "neverc/DynCode/Pipeline/Diagnostics.h"
#include "llvm/ADT/StringRef.h"
#include <cstdint>

namespace neverc {
namespace dyncode {
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
// The resolver entry point the loader must export. Unlike the globals above,
// the compiler never emits a reference to it -- rewritten calls reach the
// loader through ResolverGlobalName -- so LoaderResolverFunctionName states the
// contract rather than serving a call site.
//
// Spelled as a macro so LoaderResolverShimLabel can paste it at compile time
// (llvm::StringRef has no constant-expression concatenation); undefined again
// after the last use so it does not leak to includers.
#define NEVERC_KERN_RESOLVE_FUNCTION_NAME "__neverc_kern_resolve"

inline constexpr llvm::StringLiteral LoaderResolverFunctionName =
    NEVERC_KERN_RESOLVE_FUNCTION_NAME;
inline constexpr llvm::StringLiteral LoaderResolverShimLabel =
    "the loader-provided " NEVERC_KERN_RESOLVE_FUNCTION_NAME " shim";

#undef NEVERC_KERN_RESOLVE_FUNCTION_NAME

inline constexpr llvm::StringLiteral DiagnosticPrefix =
    Diagnostics::KernelImportPrefix;

inline constexpr llvm::StringLiteral AddressTakenExternHint =
    "Direct calls are supported and are rewritten through the resolver, "
    "but a function pointer would need resolver/cookie state outside the "
    "entry function. Call the helper directly, or pass a resolved "
    "function pointer from the loader.";

inline constexpr llvm::StringLiteral MissingEntryHint =
    "kernel-mode dyncode with extern dependencies requires a "
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
