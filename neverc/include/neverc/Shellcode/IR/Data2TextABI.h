#ifndef NEVERC_SHELLCODE_DATA2TEXTABI_H
#define NEVERC_SHELLCODE_DATA2TEXTABI_H

#include "llvm/ADT/StringRef.h"

namespace neverc {
namespace shellcode {
namespace Data2TextABI {

inline constexpr llvm::StringLiteral PipelinePhaseSentinel =
    "__neverc_shellcode_data2text_done";

}
}
}

#endif
