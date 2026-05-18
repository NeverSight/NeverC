#ifndef NEVERC_SHELLCODE_IRHELPERNAMES_H
#define NEVERC_SHELLCODE_IRHELPERNAMES_H

#include "llvm/ADT/StringRef.h"

namespace neverc {
namespace shellcode {
namespace ir {

#define NEVERC_SC_IR_NAME(Ident, Spelling)                                     \
  inline constexpr llvm::StringLiteral k##Ident = Spelling;

#include "neverc/Shellcode/Tables/ShellcodeIRHelperNames.def"

#undef NEVERC_SC_IR_NAME

}
}
}

#endif
