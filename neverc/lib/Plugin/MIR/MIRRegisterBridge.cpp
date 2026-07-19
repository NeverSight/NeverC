#include "MIRBridgeInternal.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/LowLevelType.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterBank.h"
#include "llvm/CodeGen/RegisterBankInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/MC/LaneBitmask.h"
#include <utility>

using namespace llvm;

namespace neverc::plugin {
namespace {

#define NEVERC_MIR_BRIDGE_OR_RETURN()                                          \
  NevercStatus BridgeStatus;                                                   \
  MIRPluginBridge *Bridge = getMIRBridge(Context, Task, &BridgeStatus);        \
  if (!Bridge)                                                                 \
  return BridgeStatus

bool validHeader(const NevercABITableHeader &Header, size_t Size) {
  return Header.StructSize >= Size && Header.Major == NEVERC_MIR_API_MAJOR;
}

Expected<LLT> decodeType(const NevercMIRLowLevelType &Type) {
  if (Type.ScalarSizeInBits == 0)
    return createStringError(inconvertibleErrorCode(),
                             "MIR type has zero scalar width");
  switch (Type.Kind) {
  case NEVERC_MIR_LLT_SCALAR:
    if (Type.ElementCount != 0 || Type.IsScalable)
      break;
    return LLT::scalar(Type.ScalarSizeInBits);
  case NEVERC_MIR_LLT_POINTER:
    if (Type.ElementCount != 0 || Type.IsScalable)
      break;
    return LLT::pointer(Type.AddressSpace, Type.ScalarSizeInBits);
  case NEVERC_MIR_LLT_VECTOR:
  case NEVERC_MIR_LLT_POINTER_VECTOR: {
    if (Type.ElementCount == 0 ||
        (!Type.IsScalable && Type.ElementCount == 1))
      break;
    LLT Element =
        Type.Kind == NEVERC_MIR_LLT_POINTER_VECTOR
            ? LLT::pointer(Type.AddressSpace, Type.ScalarSizeInBits)
            : LLT::scalar(Type.ScalarSizeInBits);
    return Type.IsScalable
               ? LLT::scalable_vector(Type.ElementCount, Element)
               : LLT::fixed_vector(Type.ElementCount, Element);
  }
  default:
    break;
  }
  return createStringError(inconvertibleErrorCode(),
                           "invalid MIR low-level type");
}

NevercMIRLowLevelType encodeType(LLT Type) {
  NevercMIRLowLevelType Result{};
  if (!Type.isValid())
    return Result;
  Result.ScalarSizeInBits = Type.getScalarSizeInBits();
  if (Type.isVector()) {
    LLT Element = Type.getElementType();
    Result.Kind = Element.isPointer() ? NEVERC_MIR_LLT_POINTER_VECTOR
                                      : NEVERC_MIR_LLT_VECTOR;
    Result.ElementCount = Type.getElementCount().getKnownMinValue();
    Result.IsScalable =
        Type.getElementCount().isScalable() ? NEVERC_TRUE : NEVERC_FALSE;
    if (Element.isPointer())
      Result.AddressSpace = Element.getAddressSpace();
  } else if (Type.isPointer()) {
    Result.Kind = NEVERC_MIR_LLT_POINTER;
    Result.AddressSpace = Type.getAddressSpace();
  } else {
    Result.Kind = NEVERC_MIR_LLT_SCALAR;
  }
  return Result;
}

NevercStatus resolveFunction(MIRPluginBridge &Bridge,
                             NevercMachineFunctionHandle Handle,
                             MachineFunction **OutFunction) {
  return Bridge.resolveMachineFunction(Handle, OutFunction);
}

NevercStatus resolveRegister(MIRPluginBridge &Bridge, uint32_t Number,
                             Register *OutRegister) {
  if (!OutRegister || Number == 0)
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  Register Reg(Number);
  MachineFunction *Function = nullptr;
  auto FunctionHandle = Bridge.machineFunction();
  if (!FunctionHandle) {
    consumeError(FunctionHandle.takeError());
    return mirStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  NevercStatus Status =
      Bridge.resolveMachineFunction(*FunctionHandle, &Function);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  const TargetRegisterInfo *TRI =
      Function->getSubtarget().getRegisterInfo();
  if (Reg.isPhysical()) {
    if (!Bridge.targetSchemaEnabled())
      return mirStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
    if (!TRI || Reg.id() >= TRI->getNumRegs())
      return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  } else if (!Reg.isVirtual() ||
             Register::virtReg2Index(Reg) >=
                 Function->getRegInfo().getNumVirtRegs()) {
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  }
  *OutRegister = Reg;
  return neverc_status_ok();
}

NevercStatus writeOperandHandle(MIRPluginBridge &Bridge, MachineOperand &Operand,
                                NevercMachineOperandHandle *OutOperand) {
  auto Handle = Bridge.wrapOperand(Operand);
  if (!Handle) {
    consumeError(Handle.takeError());
    return mirStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutOperand = *Handle;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL createVirtualRegister(
    void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
    const NevercMIRVirtualRegisterDesc *Desc, uint32_t *OutRegister) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  if (!Desc || !OutRegister ||
      !validHeader(Desc->Header, sizeof(*Desc)))
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  NevercStatus Status = Bridge->checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  MachineFunction *Function = nullptr;
  auto FunctionHandle = Bridge->machineFunction();
  if (!FunctionHandle) {
    consumeError(FunctionHandle.takeError());
    return mirStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  Status = Bridge->resolveMachineFunction(*FunctionHandle, &Function);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  MachineRegisterInfo &MRI = Function->getRegInfo();
  unsigned PreviousCount = MRI.getNumVirtRegs();
  Register Reg;

  switch (Desc->AssignmentKind) {
  case NEVERC_MIR_REG_ASSIGNMENT_GENERIC: {
    auto Type = decodeType(Desc->Type);
    if (!Type) {
      consumeError(Type.takeError());
      return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    }
    Reg = MRI.createGenericVirtualRegister(*Type);
    break;
  }
  case NEVERC_MIR_REG_ASSIGNMENT_CLASS: {
    if (!Bridge->targetSchemaEnabled())
      return mirStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
    const TargetRegisterInfo *TRI =
        Function->getSubtarget().getRegisterInfo();
    if (!TRI || Desc->TargetID >= TRI->getNumRegClasses())
      return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    const TargetRegisterClass *RC = TRI->getRegClass(Desc->TargetID);
    if (!RC || !RC->isAllocatable())
      return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    Reg = MRI.createVirtualRegister(RC);
    break;
  }
  case NEVERC_MIR_REG_ASSIGNMENT_BANK: {
    if (!Bridge->targetSchemaEnabled())
      return mirStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
    const RegisterBankInfo *RBI =
        Function->getSubtarget().getRegBankInfo();
    auto Type = decodeType(Desc->Type);
    if (!RBI || Desc->TargetID >= RBI->getNumRegBanks() || !Type) {
      if (!Type)
        consumeError(Type.takeError());
      return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    }
    Reg = MRI.createIncompleteVirtualRegister();
    MRI.setRegBank(Reg, RBI->getRegBank(Desc->TargetID));
    MRI.setType(Reg, *Type);
    break;
  }
  default:
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  }

  Bridge->addMutationUndo(
      [&MRI, PreviousCount] { MRI.discardVirtualRegisters(PreviousCount); });
  Bridge->invalidateMutationProperties();
  Bridge->noteMutation();
  *OutRegister = Reg.id();
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL getRegisterInfo(
    void *Context, NevercTaskHandle Task,
    NevercMachineFunctionHandle FunctionHandle, uint32_t RegisterNumber,
    NevercMIRRegisterInfo *OutInfo) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  if (!OutInfo || !validHeader(OutInfo->Header, sizeof(*OutInfo)))
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  MachineFunction *Function = nullptr;
  NevercStatus Status =
      resolveFunction(*Bridge, FunctionHandle, &Function);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Register Reg;
  Status = resolveRegister(*Bridge, RegisterNumber, &Reg);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  NevercMIRRegisterInfo Result{};
  Result.Header = {sizeof(Result), NEVERC_MIR_API_MAJOR, NEVERC_MIR_API_MINOR,
                   0};
  Result.Number = Reg.id();
  Result.IsPhysical = Reg.isPhysical() ? NEVERC_TRUE : NEVERC_FALSE;
  Result.RequiresTargetSchema =
      Reg.isPhysical() ? NEVERC_TRUE : NEVERC_FALSE;
  if (Reg.isVirtual()) {
    MachineRegisterInfo &MRI = Function->getRegInfo();
    Result.Type = encodeType(MRI.getType(Reg));
    if (const TargetRegisterClass *RC = MRI.getRegClassOrNull(Reg)) {
      Result.AssignmentKind = NEVERC_MIR_REG_ASSIGNMENT_CLASS;
      Result.RequiresTargetSchema = NEVERC_TRUE;
      if (Bridge->targetSchemaEnabled())
        Result.TargetID = RC->getID();
    } else if (const RegisterBank *RB = MRI.getRegBankOrNull(Reg)) {
      Result.AssignmentKind = NEVERC_MIR_REG_ASSIGNMENT_BANK;
      Result.RequiresTargetSchema = NEVERC_TRUE;
      if (Bridge->targetSchemaEnabled())
        Result.TargetID = RB->getID();
    } else if (MRI.getType(Reg).isValid()) {
      Result.AssignmentKind = NEVERC_MIR_REG_ASSIGNMENT_GENERIC;
    }
  }
  *OutInfo = Result;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL setVirtualRegisterAssignment(
    void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
    uint32_t RegisterNumber, const NevercMIRVirtualRegisterDesc *Desc) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  if (!Desc || !validHeader(Desc->Header, sizeof(*Desc)))
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  NevercStatus Status = Bridge->checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Register Reg;
  Status = resolveRegister(*Bridge, RegisterNumber, &Reg);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!Reg.isVirtual())
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
  MachineRegisterInfo &MRI = Function->getRegInfo();
  RegClassOrRegBank PreviousAssignment = MRI.getRegClassOrRegBank(Reg);
  LLT PreviousType = MRI.getType(Reg);

  switch (Desc->AssignmentKind) {
  case NEVERC_MIR_REG_ASSIGNMENT_GENERIC: {
    auto Type = decodeType(Desc->Type);
    if (!Type) {
      consumeError(Type.takeError());
      return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    }
    MRI.setRegClassOrRegBank(Reg, RegClassOrRegBank());
    MRI.setType(Reg, *Type);
    break;
  }
  case NEVERC_MIR_REG_ASSIGNMENT_CLASS: {
    if (!Bridge->targetSchemaEnabled())
      return mirStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
    const TargetRegisterInfo *TRI =
        Function->getSubtarget().getRegisterInfo();
    if (!TRI || Desc->TargetID >= TRI->getNumRegClasses())
      return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    MRI.setRegClass(Reg, TRI->getRegClass(Desc->TargetID));
    break;
  }
  case NEVERC_MIR_REG_ASSIGNMENT_BANK: {
    if (!Bridge->targetSchemaEnabled())
      return mirStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
    const RegisterBankInfo *RBI =
        Function->getSubtarget().getRegBankInfo();
    auto Type = decodeType(Desc->Type);
    if (!RBI || Desc->TargetID >= RBI->getNumRegBanks() || !Type) {
      if (!Type)
        consumeError(Type.takeError());
      return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    }
    MRI.setRegBank(Reg, RBI->getRegBank(Desc->TargetID));
    MRI.setType(Reg, *Type);
    break;
  }
  default:
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  }

  Bridge->addMutationUndo([&MRI, Reg, PreviousAssignment, PreviousType] {
    MRI.setRegClassOrRegBank(Reg, PreviousAssignment);
    MRI.setType(Reg, PreviousType);
  });
  Bridge->invalidateMutationProperties();
  Bridge->noteMutation();
  return neverc_status_ok();
}

template <bool Definitions>
NevercStatus getRegisterOperandCount(MIRPluginBridge &Bridge,
                                     NevercMachineFunctionHandle FunctionHandle,
                                     uint32_t RegisterNumber,
                                     uint64_t *OutCount) {
  if (!OutCount)
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  MachineFunction *Function = nullptr;
  NevercStatus Status =
      resolveFunction(Bridge, FunctionHandle, &Function);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Register Reg;
  Status = resolveRegister(Bridge, RegisterNumber, &Reg);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  uint64_t Count = 0;
  if constexpr (Definitions)
    for (MachineOperand &Ignored : Function->getRegInfo().def_operands(Reg)) {
      (void)Ignored;
      ++Count;
    }
  else
    for (MachineOperand &Ignored : Function->getRegInfo().use_operands(Reg)) {
      (void)Ignored;
      ++Count;
    }
  *OutCount = Count;
  return neverc_status_ok();
}

template <bool Definitions>
NevercStatus getRegisterOperand(MIRPluginBridge &Bridge,
                                NevercMachineFunctionHandle FunctionHandle,
                                uint32_t RegisterNumber, uint64_t Index,
                                NevercMachineOperandHandle *OutOperand) {
  if (!OutOperand)
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  MachineFunction *Function = nullptr;
  NevercStatus Status =
      resolveFunction(Bridge, FunctionHandle, &Function);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Register Reg;
  Status = resolveRegister(Bridge, RegisterNumber, &Reg);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  uint64_t Current = 0;
  if constexpr (Definitions) {
    for (MachineOperand &Operand : Function->getRegInfo().def_operands(Reg)) {
      if (Current++ == Index)
        return writeOperandHandle(Bridge, Operand, OutOperand);
    }
  } else {
    for (MachineOperand &Operand : Function->getRegInfo().use_operands(Reg)) {
      if (Current++ == Index)
        return writeOperandHandle(Bridge, Operand, OutOperand);
    }
  }
  return mirStatus(NEVERC_STATUS_NOT_FOUND);
}

NevercStatus NEVERC_CALL getRegisterDefCount(
    void *Context, NevercTaskHandle Task,
    NevercMachineFunctionHandle Function, uint32_t Register,
    uint64_t *OutCount) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  return getRegisterOperandCount<true>(*Bridge, Function, Register, OutCount);
}

NevercStatus NEVERC_CALL getRegisterDef(
    void *Context, NevercTaskHandle Task,
    NevercMachineFunctionHandle Function, uint32_t Register, uint64_t Index,
    NevercMachineOperandHandle *OutOperand) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  return getRegisterOperand<true>(*Bridge, Function, Register, Index,
                                  OutOperand);
}

NevercStatus NEVERC_CALL getRegisterUseCount(
    void *Context, NevercTaskHandle Task,
    NevercMachineFunctionHandle Function, uint32_t Register,
    uint64_t *OutCount) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  return getRegisterOperandCount<false>(*Bridge, Function, Register, OutCount);
}

NevercStatus NEVERC_CALL getRegisterUse(
    void *Context, NevercTaskHandle Task,
    NevercMachineFunctionHandle Function, uint32_t Register, uint64_t Index,
    NevercMachineOperandHandle *OutOperand) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  return getRegisterOperand<false>(*Bridge, Function, Register, Index,
                                   OutOperand);
}

NevercStatus NEVERC_CALL replaceRegister(
    void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
    uint32_t FromRegister, uint32_t ToRegister) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  NevercStatus Status = Bridge->checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Register From;
  Register To;
  Status = resolveRegister(*Bridge, FromRegister, &From);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = resolveRegister(*Bridge, ToRegister, &To);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (From == To)
    return neverc_status_ok();

  MachineFunction *Function = nullptr;
  auto FunctionHandle = Bridge->machineFunction();
  if (!FunctionHandle) {
    consumeError(FunctionHandle.takeError());
    return mirStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  Status = Bridge->resolveMachineFunction(*FunctionHandle, &Function);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  SmallVector<std::pair<MachineInstr *, unsigned>, 16> Changed;
  for (MachineOperand &Operand : Function->getRegInfo().reg_operands(From))
    Changed.emplace_back(Operand.getParent(), Operand.getOperandNo());
  for (const auto &[Instruction, Index] : Changed)
    Instruction->getOperand(Index).setReg(To);
  Bridge->addMutationUndo([Changed = std::move(Changed), From, To] {
    for (const auto &[Instruction, Index] : Changed)
      if (Instruction && Index < Instruction->getNumOperands() &&
          Instruction->getOperand(Index).isReg() &&
          Instruction->getOperand(Index).getReg() == To)
        Instruction->getOperand(Index).setReg(From);
  });
  Bridge->invalidateMutationProperties();
  Bridge->noteMutation();
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL getFunctionLiveInCount(
    void *Context, NevercTaskHandle Task,
    NevercMachineFunctionHandle FunctionHandle, uint64_t *OutCount) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  if (!OutCount || !Bridge->targetSchemaEnabled())
    return mirStatus(!OutCount ? NEVERC_STATUS_INVALID_ARGUMENT
                               : NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
  MachineFunction *Function = nullptr;
  NevercStatus Status =
      resolveFunction(*Bridge, FunctionHandle, &Function);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  *OutCount = Function->getRegInfo().liveins().size();
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL getFunctionLiveIn(
    void *Context, NevercTaskHandle Task,
    NevercMachineFunctionHandle FunctionHandle, uint64_t Index,
    NevercMIRFunctionLiveIn *OutLiveIn) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  if (!OutLiveIn)
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  if (!Bridge->targetSchemaEnabled())
    return mirStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
  MachineFunction *Function = nullptr;
  NevercStatus Status =
      resolveFunction(*Bridge, FunctionHandle, &Function);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  ArrayRef<std::pair<MCRegister, Register>> LiveIns =
      Function->getRegInfo().liveins();
  if (Index >= LiveIns.size())
    return mirStatus(NEVERC_STATUS_NOT_FOUND);
  OutLiveIn->PhysicalRegister = LiveIns[Index].first.id();
  OutLiveIn->VirtualRegister = LiveIns[Index].second.id();
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL addFunctionLiveIn(
    void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
    uint32_t PhysicalRegister, uint32_t VirtualRegister) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  NevercStatus Status = Bridge->checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Register Physical;
  Status = resolveRegister(*Bridge, PhysicalRegister, &Physical);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!Physical.isPhysical())
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  Register Virtual;
  if (VirtualRegister != 0) {
    Status = resolveRegister(*Bridge, VirtualRegister, &Virtual);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    if (!Virtual.isVirtual())
      return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  }

  MachineFunction *Function = nullptr;
  auto FunctionHandle = Bridge->machineFunction();
  if (!FunctionHandle) {
    consumeError(FunctionHandle.takeError());
    return mirStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  Status = Bridge->resolveMachineFunction(*FunctionHandle, &Function);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  MachineRegisterInfo &MRI = Function->getRegInfo();
  for (const auto &Entry : MRI.liveins())
    if (Entry.first.id() == Physical.id())
      return mirStatus(NEVERC_STATUS_DUPLICATE_ID);
  MRI.addLiveIn(Physical, Virtual);
  Bridge->addMutationUndo(
      [&MRI, Physical] { (void)MRI.removeLiveIn(Physical); });
  Bridge->invalidateMutationProperties();
  Bridge->noteMutation();
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL removeFunctionLiveIn(
    void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
    uint32_t PhysicalRegister) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  NevercStatus Status = Bridge->checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Register Physical;
  Status = resolveRegister(*Bridge, PhysicalRegister, &Physical);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!Physical.isPhysical())
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
  MachineRegisterInfo &MRI = Function->getRegInfo();
  Register Virtual = MRI.getLiveInVirtReg(Physical);
  bool Found = false;
  for (const auto &Entry : MRI.liveins())
    Found |= Entry.first.id() == Physical.id();
  if (!Found)
    return mirStatus(NEVERC_STATUS_NOT_FOUND);
  (void)MRI.removeLiveIn(Physical);
  Bridge->addMutationUndo(
      [&MRI, Physical, Virtual] { MRI.addLiveIn(Physical, Virtual); });
  Bridge->invalidateMutationProperties();
  Bridge->noteMutation();
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL addBasicBlockLiveIn(
    void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
    NevercMachineBasicBlockHandle BlockHandle, uint32_t PhysicalRegister,
    uint64_t LaneMaskValue) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  NevercStatus Status = Bridge->checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Register Physical;
  Status = resolveRegister(*Bridge, PhysicalRegister, &Physical);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!Physical.isPhysical() || LaneMaskValue == 0)
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  MachineBasicBlock *Block = nullptr;
  Status = Bridge->resolveBasicBlock(BlockHandle, &Block);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  LaneBitmask Mask(LaneMaskValue);
  for (const MachineBasicBlock::RegisterMaskPair &Entry :
       Block->liveins_dbg())
    if (Entry.PhysReg == Physical && Entry.LaneMask == Mask)
      return mirStatus(NEVERC_STATUS_DUPLICATE_ID);
  Block->addLiveIn(Physical, Mask);
  Bridge->addMutationUndo(
      [Block, Physical, Mask] { Block->removeLiveIn(Physical, Mask); });
  Bridge->invalidateMutationProperties();
  Bridge->noteMutation();
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL removeBasicBlockLiveIn(
    void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
    NevercMachineBasicBlockHandle BlockHandle, uint32_t PhysicalRegister,
    uint64_t LaneMaskValue) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  NevercStatus Status = Bridge->checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Register Physical;
  Status = resolveRegister(*Bridge, PhysicalRegister, &Physical);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!Physical.isPhysical() || LaneMaskValue == 0)
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  MachineBasicBlock *Block = nullptr;
  Status = Bridge->resolveBasicBlock(BlockHandle, &Block);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  LaneBitmask Mask(LaneMaskValue);
  bool Found = false;
  for (const MachineBasicBlock::RegisterMaskPair &Entry :
       Block->liveins_dbg())
    Found |= Entry.PhysReg == Physical && Entry.LaneMask == Mask;
  if (!Found)
    return mirStatus(NEVERC_STATUS_NOT_FOUND);
  Block->removeLiveIn(Physical, Mask);
  Bridge->addMutationUndo(
      [Block, Physical, Mask] { Block->addLiveIn(Physical, Mask); });
  Bridge->invalidateMutationProperties();
  Bridge->noteMutation();
  return neverc_status_ok();
}

} // namespace

void initializeMIRRegisterAPI(NevercMIRAPI &API) {
  API.CreateVirtualRegister = createVirtualRegister;
  API.GetRegisterInfo = getRegisterInfo;
  API.SetVirtualRegisterAssignment = setVirtualRegisterAssignment;
  API.GetRegisterDefCount = getRegisterDefCount;
  API.GetRegisterDef = getRegisterDef;
  API.GetRegisterUseCount = getRegisterUseCount;
  API.GetRegisterUse = getRegisterUse;
  API.ReplaceRegister = replaceRegister;
  API.GetFunctionLiveInCount = getFunctionLiveInCount;
  API.GetFunctionLiveIn = getFunctionLiveIn;
  API.AddFunctionLiveIn = addFunctionLiveIn;
  API.RemoveFunctionLiveIn = removeFunctionLiveIn;
  API.AddBasicBlockLiveIn = addBasicBlockLiveIn;
  API.RemoveBasicBlockLiveIn = removeBasicBlockLiveIn;
}

} // namespace neverc::plugin
