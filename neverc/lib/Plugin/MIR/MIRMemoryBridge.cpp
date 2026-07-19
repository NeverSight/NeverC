#include "MIRBridgeInternal.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/PseudoSourceValue.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Metadata.h"
#include "llvm/Support/MathExtras.h"
#include <limits>

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

bool isNullHandle(NevercHandle Handle) {
  return Handle.Owner == 0 && Handle.Value == 0;
}

bool validAlignment(uint64_t Alignment) {
  return Alignment != 0 && isPowerOf2_64(Alignment);
}

Expected<MachineMemOperand::Flags>
decodeMemoryFlags(NevercMIRMemoryFlags Flags, bool TargetSchemaEnabled) {
  constexpr NevercMIRMemoryFlags KnownFlags =
      NEVERC_MIR_MEMORY_LOAD | NEVERC_MIR_MEMORY_STORE |
      NEVERC_MIR_MEMORY_VOLATILE | NEVERC_MIR_MEMORY_NON_TEMPORAL |
      NEVERC_MIR_MEMORY_DEREFERENCEABLE | NEVERC_MIR_MEMORY_INVARIANT |
      NEVERC_MIR_MEMORY_TARGET_FLAG1 | NEVERC_MIR_MEMORY_TARGET_FLAG2 |
      NEVERC_MIR_MEMORY_TARGET_FLAG3;
  if ((Flags & ~KnownFlags) != 0 ||
      (Flags & (NEVERC_MIR_MEMORY_LOAD | NEVERC_MIR_MEMORY_STORE)) == 0)
    return createStringError(inconvertibleErrorCode(),
                             "invalid MIR memory flags");
  constexpr NevercMIRMemoryFlags TargetFlags =
      NEVERC_MIR_MEMORY_TARGET_FLAG1 | NEVERC_MIR_MEMORY_TARGET_FLAG2 |
      NEVERC_MIR_MEMORY_TARGET_FLAG3;
  if ((Flags & TargetFlags) != 0 && !TargetSchemaEnabled)
    return createStringError(inconvertibleErrorCode(),
                             "target memory flags require target schema");

  unsigned LLVMFlags = static_cast<unsigned>(Flags & UINT64_C(0x3f));
  if (Flags & NEVERC_MIR_MEMORY_TARGET_FLAG1)
    LLVMFlags |= MachineMemOperand::MOTargetFlag1;
  if (Flags & NEVERC_MIR_MEMORY_TARGET_FLAG2)
    LLVMFlags |= MachineMemOperand::MOTargetFlag2;
  if (Flags & NEVERC_MIR_MEMORY_TARGET_FLAG3)
    LLVMFlags |= MachineMemOperand::MOTargetFlag3;
  return static_cast<MachineMemOperand::Flags>(LLVMFlags);
}

NevercMIRMemoryFlags encodeMemoryFlags(MachineMemOperand::Flags Flags) {
  NevercMIRMemoryFlags Result = static_cast<unsigned>(Flags) & 0x3f;
  if (Flags & MachineMemOperand::MOTargetFlag1)
    Result |= NEVERC_MIR_MEMORY_TARGET_FLAG1;
  if (Flags & MachineMemOperand::MOTargetFlag2)
    Result |= NEVERC_MIR_MEMORY_TARGET_FLAG2;
  if (Flags & MachineMemOperand::MOTargetFlag3)
    Result |= NEVERC_MIR_MEMORY_TARGET_FLAG3;
  return Result;
}

Expected<AtomicOrdering>
decodeAtomicOrdering(NevercMIRAtomicOrdering Ordering) {
  switch (Ordering) {
  case NEVERC_MIR_ATOMIC_NOT_ATOMIC:
    return AtomicOrdering::NotAtomic;
  case NEVERC_MIR_ATOMIC_UNORDERED:
    return AtomicOrdering::Unordered;
  case NEVERC_MIR_ATOMIC_MONOTONIC:
    return AtomicOrdering::Monotonic;
  case NEVERC_MIR_ATOMIC_ACQUIRE:
    return AtomicOrdering::Acquire;
  case NEVERC_MIR_ATOMIC_RELEASE:
    return AtomicOrdering::Release;
  case NEVERC_MIR_ATOMIC_ACQUIRE_RELEASE:
    return AtomicOrdering::AcquireRelease;
  case NEVERC_MIR_ATOMIC_SEQUENTIALLY_CONSISTENT:
    return AtomicOrdering::SequentiallyConsistent;
  default:
    return createStringError(inconvertibleErrorCode(),
                             "invalid MIR atomic ordering");
  }
}

NevercMIRAtomicOrdering encodeAtomicOrdering(AtomicOrdering Ordering) {
  switch (Ordering) {
  case AtomicOrdering::NotAtomic:
    return NEVERC_MIR_ATOMIC_NOT_ATOMIC;
  case AtomicOrdering::Unordered:
    return NEVERC_MIR_ATOMIC_UNORDERED;
  case AtomicOrdering::Monotonic:
    return NEVERC_MIR_ATOMIC_MONOTONIC;
  case AtomicOrdering::Acquire:
    return NEVERC_MIR_ATOMIC_ACQUIRE;
  case AtomicOrdering::Release:
    return NEVERC_MIR_ATOMIC_RELEASE;
  case AtomicOrdering::AcquireRelease:
    return NEVERC_MIR_ATOMIC_ACQUIRE_RELEASE;
  case AtomicOrdering::SequentiallyConsistent:
    return NEVERC_MIR_ATOMIC_SEQUENTIALLY_CONSISTENT;
  }
  llvm_unreachable("unknown LLVM atomic ordering");
}

NevercStatus resolveFunction(MIRPluginBridge &Bridge,
                             MachineFunction **OutFunction) {
  auto FunctionHandle = Bridge.machineFunction();
  if (!FunctionHandle) {
    consumeError(FunctionHandle.takeError());
    return mirStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  return Bridge.resolveMachineFunction(*FunctionHandle, OutFunction);
}

NevercStatus resolveMetadata(MIRPluginBridge &Bridge, NevercMIRReferenceHandle H,
                             MDNode **OutMetadata) {
  if (!OutMetadata)
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutMetadata = nullptr;
  if (isNullHandle(H))
    return neverc_status_ok();
  const void *Reference = nullptr;
  NevercStatus Status =
      Bridge.resolveReference(H, NEVERC_MIR_OPERAND_METADATA, &Reference);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  *OutMetadata = const_cast<MDNode *>(static_cast<const MDNode *>(Reference));
  return neverc_status_ok();
}

NevercStatus writeMetadataHandle(MIRPluginBridge &Bridge, const MDNode *Metadata,
                                 NevercMIRReferenceHandle *OutHandle) {
  if (!OutHandle)
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutHandle = {};
  if (!Metadata)
    return neverc_status_ok();
  auto Handle =
      Bridge.wrapReference(Metadata, NEVERC_MIR_OPERAND_METADATA);
  if (!Handle) {
    consumeError(Handle.takeError());
    return mirStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutHandle = *Handle;
  return neverc_status_ok();
}

NevercStatus decodePointerInfo(MIRPluginBridge &Bridge,
                               MachineFunction &Function,
                               const NevercMIRMemoryPointerInfo &Input,
                               MachinePointerInfo *OutPointer) {
  if (!OutPointer)
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  if (Input.StackID > std::numeric_limits<uint8_t>::max())
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  switch (Input.Kind) {
  case NEVERC_MIR_MEMORY_POINTER_UNKNOWN:
    if (!isNullHandle(Input.Reference))
      return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *OutPointer = MachinePointerInfo(Input.AddressSpace, Input.Offset);
    return neverc_status_ok();
  case NEVERC_MIR_MEMORY_POINTER_IR_VALUE: {
    const void *Reference = nullptr;
    NevercStatus Status = Bridge.resolveReference(
        Input.Reference, NEVERC_MIR_OPERAND_GLOBAL_ADDRESS, &Reference);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    const auto *Value = static_cast<const llvm::Value *>(Reference);
    if (!Value || !Value->getType()->isPointerTy())
      return mirStatus(NEVERC_STATUS_WRONG_TYPE);
    *OutPointer = MachinePointerInfo(Value, Input.Offset, Input.StackID);
    return neverc_status_ok();
  }
  case NEVERC_MIR_MEMORY_POINTER_FIXED_STACK:
    *OutPointer = MachinePointerInfo::getFixedStack(
        Function, Input.FrameIndex, Input.Offset);
    return neverc_status_ok();
  case NEVERC_MIR_MEMORY_POINTER_STACK:
    *OutPointer =
        MachinePointerInfo::getStack(Function, Input.Offset, Input.StackID);
    return neverc_status_ok();
  case NEVERC_MIR_MEMORY_POINTER_CONSTANT_POOL:
    *OutPointer =
        MachinePointerInfo::getConstantPool(Function).getWithOffset(Input.Offset);
    return neverc_status_ok();
  case NEVERC_MIR_MEMORY_POINTER_JUMP_TABLE:
    *OutPointer =
        MachinePointerInfo::getJumpTable(Function).getWithOffset(Input.Offset);
    return neverc_status_ok();
  case NEVERC_MIR_MEMORY_POINTER_GOT:
    *OutPointer =
        MachinePointerInfo::getGOT(Function).getWithOffset(Input.Offset);
    return neverc_status_ok();
  case NEVERC_MIR_MEMORY_POINTER_UNKNOWN_STACK:
    if (!isNullHandle(Input.Reference))
      return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    *OutPointer = MachinePointerInfo::getUnknownStack(Function);
    return neverc_status_ok();
  case NEVERC_MIR_MEMORY_POINTER_TARGET_CUSTOM: {
    if (!Bridge.targetSchemaEnabled())
      return mirStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
    const void *Reference = nullptr;
    NevercStatus Status = Bridge.resolveReference(
        Input.Reference, NEVERC_MIR_OPERAND_TARGET_INDEX, &Reference);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    const auto *Pseudo = static_cast<const PseudoSourceValue *>(Reference);
    if (!Pseudo || Pseudo->getTargetCustom() == 0)
      return mirStatus(NEVERC_STATUS_WRONG_TYPE);
    *OutPointer =
        MachinePointerInfo(Pseudo, Input.Offset, Input.StackID);
    return neverc_status_ok();
  }
  default:
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  }
}

NevercStatus encodePointerInfo(MIRPluginBridge &Bridge,
                               const MachinePointerInfo &Input,
                               NevercMIRMemoryPointerInfo *OutPointer) {
  if (!OutPointer)
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  NevercMIRMemoryPointerInfo Result{};
  Result.AddressSpace = Input.getAddrSpace();
  Result.Offset = Input.Offset;
  Result.StackID = Input.StackID;
  if (const Value *Value = dyn_cast_if_present<const llvm::Value *>(Input.V)) {
    Result.Kind = NEVERC_MIR_MEMORY_POINTER_IR_VALUE;
    auto Handle =
        Bridge.wrapReference(Value, NEVERC_MIR_OPERAND_GLOBAL_ADDRESS);
    if (!Handle) {
      consumeError(Handle.takeError());
      return mirStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    Result.Reference = *Handle;
  } else if (const auto *Pseudo =
                 dyn_cast_if_present<const PseudoSourceValue *>(Input.V)) {
    switch (Pseudo->kind()) {
    case PseudoSourceValue::FixedStack:
      Result.Kind = NEVERC_MIR_MEMORY_POINTER_FIXED_STACK;
      Result.FrameIndex =
          cast<FixedStackPseudoSourceValue>(Pseudo)->getFrameIndex();
      break;
    case PseudoSourceValue::Stack:
      Result.Kind = NEVERC_MIR_MEMORY_POINTER_STACK;
      break;
    case PseudoSourceValue::ConstantPool:
      Result.Kind = NEVERC_MIR_MEMORY_POINTER_CONSTANT_POOL;
      break;
    case PseudoSourceValue::JumpTable:
      Result.Kind = NEVERC_MIR_MEMORY_POINTER_JUMP_TABLE;
      break;
    case PseudoSourceValue::GOT:
      Result.Kind = NEVERC_MIR_MEMORY_POINTER_GOT;
      break;
    default:
      Result.Kind = NEVERC_MIR_MEMORY_POINTER_TARGET_CUSTOM;
      if (!Bridge.targetSchemaEnabled())
        return mirStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
      auto Handle =
          Bridge.wrapReference(Pseudo, NEVERC_MIR_OPERAND_TARGET_INDEX);
      if (!Handle) {
        consumeError(Handle.takeError());
        return mirStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
      }
      Result.Reference = *Handle;
      break;
    }
  } else {
    Result.Kind = NEVERC_MIR_MEMORY_POINTER_UNKNOWN;
  }
  *OutPointer = Result;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL getInstructionMemoryOperand(
    void *Context, NevercTaskHandle Task,
    NevercMachineInstrHandle InstructionHandle, uint64_t Index,
    NevercMachineMemOperandHandle *OutMemoryOperand) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  if (!OutMemoryOperand)
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  MachineInstr *Instruction = nullptr;
  NevercStatus Status =
      Bridge->resolveInstruction(InstructionHandle, &Instruction);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  ArrayRef<MachineMemOperand *> Operands = Instruction->memoperands();
  if (Index >= Operands.size())
    return mirStatus(NEVERC_STATUS_NOT_FOUND);
  auto Handle = Bridge->wrapMemoryOperand(*Operands[Index]);
  if (!Handle) {
    consumeError(Handle.takeError());
    return mirStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutMemoryOperand = *Handle;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL getMemoryOperandInfo(
    void *Context, NevercTaskHandle Task,
    NevercMachineMemOperandHandle MemoryOperand,
    NevercMIRMemoryOperandInfo *OutInfo) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  if (!OutInfo || !validHeader(OutInfo->Header, sizeof(*OutInfo)))
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  MachineMemOperand *Operand = nullptr;
  NevercStatus Status =
      Bridge->resolveMemoryOperand(MemoryOperand, &Operand);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  MachineFunction *Function = nullptr;
  Status = resolveFunction(*Bridge, &Function);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  NevercMIRMemoryOperandInfo Result{};
  Result.Header = {sizeof(Result), NEVERC_MIR_API_MAJOR, NEVERC_MIR_API_MINOR,
                   0};
  Result.Flags = encodeMemoryFlags(Operand->getFlags());
  Result.Size = Operand->getSize();
  Result.BaseAlignment = Operand->getBaseAlign().value();
  Result.Alignment = Operand->getAlign().value();
  Status =
      encodePointerInfo(*Bridge, Operand->getPointerInfo(), &Result.Pointer);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Result.SuccessOrdering =
      encodeAtomicOrdering(Operand->getSuccessOrdering());
  Result.FailureOrdering =
      encodeAtomicOrdering(Operand->getFailureOrdering());

  SmallVector<StringRef, 8> ScopeNames;
  Function->getFunction().getContext().getSyncScopeNames(ScopeNames);
  unsigned ScopeID = Operand->getSyncScopeID();
  if (ScopeID < ScopeNames.size()) {
    StringRef Name = ScopeNames[ScopeID];
    Result.SynchronizationScope = {Bridge->ownString(Name), Name.size()};
  }

  AAMDNodes AA = Operand->getAAInfo();
  if ((Status = writeMetadataHandle(*Bridge, AA.TBAA, &Result.TBAA)).Code !=
      NEVERC_STATUS_OK)
    return Status;
  if ((Status = writeMetadataHandle(*Bridge, AA.TBAAStruct,
                                    &Result.TBAAStruct))
          .Code != NEVERC_STATUS_OK)
    return Status;
  if ((Status = writeMetadataHandle(*Bridge, AA.Scope, &Result.AliasScope))
          .Code != NEVERC_STATUS_OK)
    return Status;
  if ((Status = writeMetadataHandle(*Bridge, AA.NoAlias, &Result.NoAlias))
          .Code != NEVERC_STATUS_OK)
    return Status;
  if ((Status = writeMetadataHandle(*Bridge, Operand->getRanges(),
                                    &Result.Ranges))
          .Code != NEVERC_STATUS_OK)
    return Status;
  *OutInfo = Result;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL createMemoryOperand(
    void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
    const NevercMIRMemoryOperandDesc *Desc,
    NevercMachineMemOperandHandle *OutMemoryOperand) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  if (!Desc || !OutMemoryOperand ||
      !validHeader(Desc->Header, sizeof(*Desc)) ||
      !validAlignment(Desc->BaseAlignment) ||
      (Desc->Size == 0) ||
      (Desc->Size != std::numeric_limits<uint64_t>::max() &&
       Desc->Size > std::numeric_limits<uint64_t>::max() / 8) ||
      Desc->SynchronizationScope.Length >
          static_cast<uint64_t>(std::numeric_limits<size_t>::max()) ||
      (Desc->SynchronizationScope.Length != 0 &&
       !Desc->SynchronizationScope.Data))
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  NevercStatus Status = Bridge->checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto Flags = decodeMemoryFlags(Desc->Flags, Bridge->targetSchemaEnabled());
  auto SuccessOrdering = decodeAtomicOrdering(Desc->SuccessOrdering);
  auto FailureOrdering = decodeAtomicOrdering(Desc->FailureOrdering);
  if (!Flags || !SuccessOrdering || !FailureOrdering) {
    if (!Flags)
      consumeError(Flags.takeError());
    if (!SuccessOrdering)
      consumeError(SuccessOrdering.takeError());
    if (!FailureOrdering)
      consumeError(FailureOrdering.takeError());
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  }
  if ((*SuccessOrdering == AtomicOrdering::NotAtomic &&
       *FailureOrdering != AtomicOrdering::NotAtomic) ||
      *FailureOrdering == AtomicOrdering::Release ||
      *FailureOrdering == AtomicOrdering::AcquireRelease ||
      (*FailureOrdering != AtomicOrdering::NotAtomic &&
       isStrongerThan(*FailureOrdering, *SuccessOrdering)))
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);

  MachineFunction *Function = nullptr;
  Status = resolveFunction(*Bridge, &Function);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  MachinePointerInfo Pointer;
  Status = decodePointerInfo(*Bridge, *Function, Desc->Pointer, &Pointer);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  MDNode *TBAA = nullptr;
  MDNode *TBAAStruct = nullptr;
  MDNode *AliasScope = nullptr;
  MDNode *NoAlias = nullptr;
  MDNode *Ranges = nullptr;
  if ((Status = resolveMetadata(*Bridge, Desc->TBAA, &TBAA)).Code !=
          NEVERC_STATUS_OK ||
      (Status = resolveMetadata(*Bridge, Desc->TBAAStruct, &TBAAStruct)).Code !=
          NEVERC_STATUS_OK ||
      (Status = resolveMetadata(*Bridge, Desc->AliasScope, &AliasScope)).Code !=
          NEVERC_STATUS_OK ||
      (Status = resolveMetadata(*Bridge, Desc->NoAlias, &NoAlias)).Code !=
          NEVERC_STATUS_OK ||
      (Status = resolveMetadata(*Bridge, Desc->Ranges, &Ranges)).Code !=
          NEVERC_STATUS_OK)
    return Status;

  LLVMContext &LLVMContext = Function->getFunction().getContext();
  SyncScope::ID Scope = SyncScope::System;
  if (Desc->SynchronizationScope.Length != 0)
    Scope = LLVMContext.getOrInsertSyncScopeID(StringRef(
        Desc->SynchronizationScope.Data,
        static_cast<size_t>(Desc->SynchronizationScope.Length)));
  MachineMemOperand *Created = Function->getMachineMemOperand(
      Pointer, *Flags, Desc->Size, Align(Desc->BaseAlignment),
      AAMDNodes(TBAA, TBAAStruct, AliasScope, NoAlias), Ranges, Scope,
      *SuccessOrdering, *FailureOrdering);
  auto Handle = Bridge->wrapMemoryOperand(*Created);
  if (!Handle) {
    consumeError(Handle.takeError());
    return mirStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  Bridge->addMutationUndo(
      [Bridge, Created] { (void)Bridge->invalidateMemoryOperand(*Created); });
  Bridge->invalidateMutationProperties();
  Bridge->noteMutation();
  *OutMemoryOperand = *Handle;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL addInstructionMemoryOperand(
    void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
    NevercMachineInstrHandle InstructionHandle,
    NevercMachineMemOperandHandle MemoryOperandHandle) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  NevercStatus Status = Bridge->checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  MachineInstr *Instruction = nullptr;
  Status = Bridge->resolveInstruction(InstructionHandle, &Instruction);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  MachineMemOperand *MemoryOperand = nullptr;
  Status =
      Bridge->resolveMemoryOperand(MemoryOperandHandle, &MemoryOperand);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (llvm::is_contained(Instruction->memoperands(), MemoryOperand))
    return mirStatus(NEVERC_STATUS_DUPLICATE_ID);
  MachineFunction *Function = Instruction->getMF();
  if (!Function)
    return mirStatus(NEVERC_STATUS_INVALID_STATE);
  SmallVector<MachineMemOperand *, 4> Previous(Instruction->memoperands());
  Instruction->addMemOperand(*Function, MemoryOperand);
  Bridge->addMutationUndo(
      [Instruction, Function, Previous = std::move(Previous)] {
        Instruction->setMemRefs(*Function, Previous);
      });
  Bridge->invalidateMutationProperties();
  Bridge->noteMutation();
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL removeInstructionMemoryOperand(
    void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
    NevercMachineInstrHandle InstructionHandle, uint64_t Index) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  NevercStatus Status = Bridge->checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  MachineInstr *Instruction = nullptr;
  Status = Bridge->resolveInstruction(InstructionHandle, &Instruction);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  ArrayRef<MachineMemOperand *> Current = Instruction->memoperands();
  if (Index >= Current.size())
    return mirStatus(NEVERC_STATUS_NOT_FOUND);
  MachineFunction *Function = Instruction->getMF();
  if (!Function)
    return mirStatus(NEVERC_STATUS_INVALID_STATE);
  SmallVector<MachineMemOperand *, 4> Previous(Current);
  SmallVector<MachineMemOperand *, 4> Updated;
  Updated.reserve(Current.size() - 1);
  for (uint64_t I = 0; I != Current.size(); ++I)
    if (I != Index)
      Updated.push_back(Current[I]);
  Instruction->setMemRefs(*Function, Updated);
  Bridge->addMutationUndo(
      [Instruction, Function, Previous = std::move(Previous)] {
        Instruction->setMemRefs(*Function, Previous);
      });
  Bridge->invalidateMutationProperties();
  Bridge->noteMutation();
  return neverc_status_ok();
}

#undef NEVERC_MIR_BRIDGE_OR_RETURN

} // namespace

void initializeMIRMemoryAPI(NevercMIRAPI &API) {
  API.GetInstructionMemoryOperand = getInstructionMemoryOperand;
  API.GetMemoryOperandInfo = getMemoryOperandInfo;
  API.CreateMemoryOperand = createMemoryOperand;
  API.AddInstructionMemoryOperand = addInstructionMemoryOperand;
  API.RemoveInstructionMemoryOperand = removeInstructionMemoryOperand;
}

} // namespace neverc::plugin
