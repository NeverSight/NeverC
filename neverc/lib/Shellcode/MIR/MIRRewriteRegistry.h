#ifndef NEVERC_LIB_SHELLCODE_MIRREWRITEREGISTRY_H
#define NEVERC_LIB_SHELLCODE_MIRREWRITEREGISTRY_H

#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/TargetInstrInfo.h"

namespace neverc {
namespace shellcode {

inline unsigned findMIRRewriteOpcodeByName(const llvm::TargetInstrInfo &TII,
                                           llvm::StringRef Want) {
  unsigned N = TII.getNumOpcodes();
  for (unsigned I = 1; I < N; ++I)
    if (TII.getName(I) == Want)
      return I;
  return 0;
}

inline unsigned lookupMIRRewriteOpcode(const llvm::TargetInstrInfo &TII,
                                       llvm::StringRef Pattern,
                                       llvm::StringRef Role) {
  struct Row {
    const char *Pattern;
    const char *Role;
    const char *Opcode;
  };
  static constexpr Row kRows[] = {
#define NEVERC_MIR_REWRITE_OPCODE(pattern, role, opcode)                       \
  {#pattern, #role, #opcode},
#include "neverc/Shellcode/Tables/MIRRewriteOpcodes.def"
#include "neverc/Shellcode/Tables/UserExtra_MIRRewriteOpcodes.def"
#undef NEVERC_MIR_REWRITE_OPCODE
  };
  llvm::StringRef Selected;
  for (const Row &R : kRows) {
    if (Pattern == llvm::StringRef(R.Pattern) &&
        Role == llvm::StringRef(R.Role))
      Selected = R.Opcode;
  }
  if (Selected.empty())
    return 0;
  return findMIRRewriteOpcodeByName(TII, Selected);
}

} // namespace shellcode
} // namespace neverc

#endif
