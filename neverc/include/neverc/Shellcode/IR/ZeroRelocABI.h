#ifndef NEVERC_SHELLCODE_ZERORELOCABI_H
#define NEVERC_SHELLCODE_ZERORELOCABI_H

#include "llvm/ADT/StringRef.h"

namespace neverc {
namespace shellcode {
namespace ZeroRelocABI {

inline constexpr llvm::StringLiteral HardErrorSentinel =
    "__neverc_shellcode_hard_error";

inline constexpr llvm::StringLiteral StackifiedSentinel =
    "__neverc_shellcode_stackified";

}
}
}

#endif
