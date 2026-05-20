#ifndef NEVERC_FOUNDATION_TYPETRAITS_H
#define NEVERC_FOUNDATION_TYPETRAITS_H

#include "llvm/Support/Compiler.h"

namespace neverc {

enum UnaryExprOrTypeTrait {
#define UNARY_EXPR_OR_TYPE_TRAIT(Spelling, Name, Key) UETT_##Name,
#include "neverc/Foundation/Core/TokenKinds.def"
  UETT_Last = -1 // UETT_Last == last UETT_XX in the enum.
#define UNARY_EXPR_OR_TYPE_TRAIT(Spelling, Name, Key) +1
#include "neverc/Foundation/Core/TokenKinds.def"
};

const char *getTraitSpelling(UnaryExprOrTypeTrait T) LLVM_READONLY;

} // namespace neverc

#endif
