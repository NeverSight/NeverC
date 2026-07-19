#include "MIRBridgeInternal.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"

using namespace llvm;

namespace neverc::plugin {
namespace {

#define NEVERC_MIR_BRIDGE_OR_RETURN()                                         \
  NevercStatus BridgeStatus;                                                  \
  MIRPluginBridge *Bridge = getMIRBridge(Context, Task, &BridgeStatus);       \
  if (!Bridge)                                                                \
  return BridgeStatus

bool validHeader(const NevercABITableHeader &Header, size_t Size) {
  return Header.StructSize >= Size && Header.Major == NEVERC_MIR_API_MAJOR &&
         Header.Minor <= NEVERC_MIR_API_MINOR && Header.Flags == 0;
}

Expected<MachineFunctionProperties::Property>
decodeProperty(NevercMIRMachineProperty Property) {
  using PropertyKind = MachineFunctionProperties::Property;
  switch (Property) {
  case NEVERC_MIR_PROPERTY_IS_SSA:
    return PropertyKind::IsSSA;
  case NEVERC_MIR_PROPERTY_NO_PH_IS:
    return PropertyKind::NoPHIs;
  case NEVERC_MIR_PROPERTY_TRACKS_LIVENESS:
    return PropertyKind::TracksLiveness;
  case NEVERC_MIR_PROPERTY_NO_V_REGS:
    return PropertyKind::NoVRegs;
  case NEVERC_MIR_PROPERTY_FAILED_I_SEL:
    return PropertyKind::FailedISel;
  case NEVERC_MIR_PROPERTY_LEGALIZED:
    return PropertyKind::Legalized;
  case NEVERC_MIR_PROPERTY_REG_BANK_SELECTED:
    return PropertyKind::RegBankSelected;
  case NEVERC_MIR_PROPERTY_SELECTED:
    return PropertyKind::Selected;
  case NEVERC_MIR_PROPERTY_TIED_OPS_REWRITTEN:
    return PropertyKind::TiedOpsRewritten;
  case NEVERC_MIR_PROPERTY_FAILS_VERIFICATION:
    return PropertyKind::FailsVerification;
  case NEVERC_MIR_PROPERTY_TRACKS_DEBUG_USER_VALUES:
    return PropertyKind::TracksDebugUserValues;
  default:
    return createStringError(inconvertibleErrorCode(),
                             "unknown MIR machine property");
  }
}

bool structurallyProves(MachineFunction &Function,
                        MachineFunctionProperties::Property Property) {
  using PropertyKind = MachineFunctionProperties::Property;
  switch (Property) {
  case PropertyKind::IsSSA: {
    MachineRegisterInfo &MRI = Function.getRegInfo();
    for (unsigned Index = 0; Index != MRI.getNumVirtRegs(); ++Index) {
      Register Reg = Register::index2VirtReg(Index);
      if (!MRI.def_empty(Reg) && !MRI.hasOneDef(Reg))
        return false;
    }
    return true;
  }
  case PropertyKind::NoPHIs:
    for (const MachineBasicBlock &Block : Function)
      for (const MachineInstr &Instruction : Block)
        if (Instruction.isPHI())
          return false;
    return true;
  case PropertyKind::NoVRegs:
    for (const MachineBasicBlock &Block : Function)
      for (const MachineInstr &Instruction : Block)
        for (const MachineOperand &Operand : Instruction.operands())
          if (Operand.isReg() && Operand.getReg().isVirtual())
            return false;
    return true;
  default:
    return Function.getProperties().hasProperty(Property);
  }
}

NevercStatus NEVERC_CALL
getMachineProperty(void *Context, NevercTaskHandle Task,
                   NevercMachineFunctionHandle FunctionHandle,
                   NevercMIRMachineProperty Property, NevercBool *OutValue) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  if (!OutValue)
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto Decoded = decodeProperty(Property);
  if (!Decoded) {
    consumeError(Decoded.takeError());
    return mirStatus(NEVERC_STATUS_NOT_FOUND);
  }
  MachineFunction *Function = nullptr;
  NevercStatus Status =
      Bridge->resolveMachineFunction(FunctionHandle, &Function);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  *OutValue = Function->getProperties().hasProperty(*Decoded) ? NEVERC_TRUE
                                                               : NEVERC_FALSE;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL setMachinePropertyWithProof(
    void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
    const NevercMIRPropertyProof *Proof) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  if (!Proof || !validHeader(Proof->Header, sizeof(*Proof)) ||
      (Proof->Value != NEVERC_FALSE && Proof->Value != NEVERC_TRUE))
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  NevercStatus Status = Bridge->checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto Decoded = decodeProperty(Proof->Property);
  if (!Decoded) {
    consumeError(Decoded.takeError());
    return mirStatus(NEVERC_STATUS_NOT_FOUND);
  }
  if ((Proof->Kind == NEVERC_MIR_PROPERTY_PROOF_INVALIDATION &&
       Proof->Value != NEVERC_FALSE) ||
      (Proof->Kind == NEVERC_MIR_PROPERTY_PROOF_STRUCTURAL_CHECK &&
       Proof->Value != NEVERC_TRUE) ||
      (Proof->Kind != NEVERC_MIR_PROPERTY_PROOF_INVALIDATION &&
       Proof->Kind != NEVERC_MIR_PROPERTY_PROOF_STRUCTURAL_CHECK))
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);

  MachineFunction *Function = nullptr;
  auto FunctionHandle = Bridge->machineFunction();
  if (!FunctionHandle) {
    consumeError(FunctionHandle.takeError());
    return mirStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  Status = Bridge->resolveMachineFunction(*FunctionHandle, &Function);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Proof->Value == NEVERC_TRUE &&
      !structurallyProves(*Function, *Decoded))
    return mirStatus(NEVERC_STATUS_VERIFICATION_FAILED);

  MachineFunctionProperties &Properties = Function->getProperties();
  bool Previous = Properties.hasProperty(*Decoded);
  bool Next = Proof->Value == NEVERC_TRUE;
  if (Previous == Next)
    return neverc_status_ok();
  if (Next)
    Properties.set(*Decoded);
  else
    Properties.reset(*Decoded);
  Bridge->addMutationUndo([&Properties, Property = *Decoded, Previous] {
    if (Previous)
      Properties.set(Property);
    else
      Properties.reset(Property);
  });
  Bridge->noteMutation();
  return neverc_status_ok();
}

#undef NEVERC_MIR_BRIDGE_OR_RETURN

} // namespace

void initializeMIRPropertyAPI(NevercMIRAPI &API) {
  API.GetMachineProperty = getMachineProperty;
  API.SetMachinePropertyWithProof = setMachinePropertyWithProof;
}

} // namespace neverc::plugin
