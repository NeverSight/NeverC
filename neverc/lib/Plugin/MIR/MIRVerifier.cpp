#include "MIRBridgeInternal.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace neverc::plugin {
namespace {

bool fail(const TargetInstrInfo &TII, const MachineInstr &Instruction,
          StringRef Reason, std::string &OutError) {
  raw_string_ostream Stream(OutError);
  Stream << "invalid MIR instruction " << TII.getName(Instruction.getOpcode())
         << " in bb." << Instruction.getParent()->getNumber() << ": " << Reason;
  Stream.flush();
  return false;
}

bool verifyPHIShape(const TargetInstrInfo &TII, const MachineInstr &Instruction,
                    std::string &OutError) {
  if (Instruction.getNumOperands() < 3 ||
      (Instruction.getNumOperands() % 2) == 0)
    return fail(TII, Instruction,
                "PHI requires a definition and register/block pairs", OutError);
  if (!Instruction.getOperand(0).isReg())
    return fail(TII, Instruction, "PHI definition is not a register", OutError);
  for (unsigned I = 1; I != Instruction.getNumOperands(); I += 2) {
    if (!Instruction.getOperand(I).isReg() ||
        !Instruction.getOperand(I + 1).isMBB())
      return fail(TII, Instruction,
                  "PHI incoming values must be register/block pairs", OutError);
  }
  return true;
}

bool verifyRegSequenceShape(const TargetInstrInfo &TII,
                            const MachineInstr &Instruction,
                            std::string &OutError) {
  if (Instruction.getNumOperands() < 3 ||
      (Instruction.getNumOperands() % 2) == 0)
    return fail(TII, Instruction,
                "REG_SEQUENCE requires a definition and register/subreg pairs",
                OutError);
  if (!Instruction.getOperand(0).isReg())
    return fail(TII, Instruction, "REG_SEQUENCE definition is not a register",
                OutError);
  for (unsigned I = 1; I != Instruction.getNumOperands(); I += 2) {
    if (!Instruction.getOperand(I).isReg() ||
        !Instruction.getOperand(I + 1).isImm())
      return fail(TII, Instruction,
                  "REG_SEQUENCE inputs must be register/immediate pairs",
                  OutError);
  }
  return true;
}

} // namespace

bool verifyMIRStructure(const MachineFunction &Function,
                        std::string &OutError) {
  OutError.clear();
  const TargetInstrInfo *TII = Function.getSubtarget().getInstrInfo();
  if (!TII) {
    OutError = "MIR function has no TargetInstrInfo";
    return false;
  }

  for (const MachineBasicBlock &Block : Function) {
    for (const MachineInstr &Instruction : Block) {
      unsigned Opcode = Instruction.getOpcode();
      if (Opcode >= TII->getNumOpcodes()) {
        OutError = "MIR instruction opcode is outside TargetInstrInfo";
        return false;
      }
      const MCInstrDesc &Descriptor = TII->get(Opcode);
      if (Instruction.getNumOperands() < Descriptor.getNumOperands())
        return fail(*TII, Instruction, "too few operands for descriptor",
                    OutError);
      for (const MachineOperand &Operand : Instruction.operands()) {
        if (Operand.isMBB() && (Operand.getMBB()->getParent() != &Function ||
                                Operand.getMBB()->getNumber() < 0))
          return fail(*TII, Instruction,
                      "basic block operand is outside the function", OutError);
      }

      for (unsigned I = 0; I != Descriptor.getNumOperands(); ++I) {
        const MachineOperand &Operand = Instruction.getOperand(I);
        const MCOperandInfo &OperandInfo = Descriptor.operands()[I];
        if (I < Descriptor.getNumDefs() && !Operand.isReg())
          return fail(*TII, Instruction,
                      "explicit definition is not a register", OutError);
        if (OperandInfo.OperandType == MCOI::OPERAND_REGISTER &&
            !Operand.isReg() && !Operand.isFI())
          return fail(*TII, Instruction,
                      "descriptor requires a register operand", OutError);
        if (OperandInfo.OperandType == MCOI::OPERAND_IMMEDIATE &&
            Operand.isReg())
          return fail(*TII, Instruction,
                      "descriptor requires a non-register operand", OutError);
        if (OperandInfo.isGenericType() && !Operand.isReg())
          return fail(*TII, Instruction,
                      "generic typed operand is not a register", OutError);
        if (OperandInfo.isGenericImm() && !Operand.isImm())
          return fail(*TII, Instruction,
                      "generic immediate operand is not an immediate",
                      OutError);
        if (Descriptor.getOperandConstraint(I, MCOI::TIED_TO) != -1 &&
            !Operand.isReg())
          return fail(*TII, Instruction, "tied operand is not a register",
                      OutError);
      }

      switch (Opcode) {
      case TargetOpcode::COPY:
        if (!Instruction.getOperand(0).isReg() ||
            !Instruction.getOperand(1).isReg())
          return fail(*TII, Instruction, "COPY requires two register operands",
                      OutError);
        break;
      case TargetOpcode::PHI:
      case TargetOpcode::G_PHI:
        if (!verifyPHIShape(*TII, Instruction, OutError))
          return false;
        break;
      case TargetOpcode::REG_SEQUENCE:
        if (!verifyRegSequenceShape(*TII, Instruction, OutError))
          return false;
        break;
      case TargetOpcode::INLINEASM:
      case TargetOpcode::INLINEASM_BR:
        if (Instruction.getNumOperands() < 2 ||
            !Instruction.getOperand(0).isSymbol() ||
            !Instruction.getOperand(1).isImm())
          return fail(*TII, Instruction,
                      "inline assembly requires symbol and flag operands",
                      OutError);
        break;
      case TargetOpcode::INSERT_SUBREG:
        if (!Instruction.getOperand(2).isReg() ||
            !Instruction.getOperand(3).isImm())
          return fail(*TII, Instruction,
                      "INSERT_SUBREG requires register and subreg operands",
                      OutError);
        break;
      default:
        break;
      }
    }
  }
  return true;
}

} // namespace neverc::plugin
