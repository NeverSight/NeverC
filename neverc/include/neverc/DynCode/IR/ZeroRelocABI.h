#ifndef NEVERC_DYNCODE_ZERORELOCABI_H
#define NEVERC_DYNCODE_ZERORELOCABI_H

#include "llvm/ADT/StringRef.h"

namespace neverc {
namespace dyncode {
namespace ZeroRelocABI {

inline constexpr llvm::StringLiteral HardErrorSentinel =
    "__neverc_dyncode_hard_error";

inline constexpr llvm::StringLiteral StackifiedSentinel =
    "__neverc_dyncode_stackified";

}
}
}

#endif
