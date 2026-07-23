#ifndef NEVERC_LIB_DYNCODE_MIR_MIRPSEUDOCLASSIFY_H
#define NEVERC_LIB_DYNCODE_MIR_MIRPSEUDOCLASSIFY_H

// Volume 6 task 9: shared classifiers for the split dyncode MIR stage.  Both the
// builtin MIR transform provider (which strips these instructions at the PreEmit
// hook) and the sealed final-MIR verifier gate (which rejects any that survive)
// key off the same predicates so the transform/verify pair stays in lockstep.

#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"

namespace neverc {
namespace dyncode {

/// True for a dyncode strip pseudo: an opcode that must never survive into the
/// emitted object.  The set is the frozen MIRStripPseudoOpcodes.def table plus
/// any user extension.
inline bool isDynCodeStripPseudo(unsigned Opc) {
  switch (Opc) {
#define NEVERC_MIR_STRIP_PSEUDO(name, category) case llvm::TargetOpcode::name:
#include "neverc/DynCode/Tables/MIRStripPseudoOpcodes.def"
#include "neverc/DynCode/Tables/UserExtra_MIRStripPseudoOpcodes.def"
#undef NEVERC_MIR_STRIP_PSEUDO
    return true;
  default:
    return false;
  }
}

/// True for a Windows SEH pseudo (mnemonic starts with "SEH_").  These carry
/// unwind metadata that a position-independent dyncode image cannot reference.
inline bool isSEHPseudoByMnemonic(const llvm::MachineInstr &MI) {
  const llvm::MachineFunction *MF = MI.getParent()->getParent();
  if (!MF)
    return false;
  const llvm::TargetInstrInfo *TII = MF->getSubtarget().getInstrInfo();
  if (!TII)
    return false;
  return TII->getName(MI.getOpcode()).starts_with("SEH_");
}

} // namespace dyncode
} // namespace neverc

#endif // NEVERC_LIB_DYNCODE_MIR_MIRPSEUDOCLASSIFY_H
