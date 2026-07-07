#ifndef NEVERC_DYNCODE_DATA2TEXTABI_H
#define NEVERC_DYNCODE_DATA2TEXTABI_H

#include "llvm/ADT/StringRef.h"

namespace neverc {
namespace dyncode {
namespace Data2TextABI {

inline constexpr llvm::StringLiteral PipelinePhaseSentinel =
    "__neverc_dyncode_data2text_done";

}
}
}

#endif
