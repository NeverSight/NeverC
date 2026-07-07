#ifndef NEVERC_DYNCODE_IRHELPERNAMES_H
#define NEVERC_DYNCODE_IRHELPERNAMES_H

#include "llvm/ADT/StringRef.h"

namespace neverc {
namespace dyncode {
namespace ir {

#define NEVERC_SC_IR_NAME(Ident, Spelling)                                     \
  inline constexpr llvm::StringLiteral k##Ident = Spelling;

#include "neverc/DynCode/Tables/DynCodeIRHelperNames.def"

#undef NEVERC_SC_IR_NAME

}
}
}

#endif
