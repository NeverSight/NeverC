#include "MIRBridgeInternal.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/MathExtras.h"
#include <limits>
#include <vector>

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

bool validAlignment(uint64_t Alignment) {
  return Alignment != 0 && isPowerOf2_64(Alignment);
}

bool validWordView(const NevercMIRWordView &View) {
  return View.BitWidth != 0 && View.Data &&
         View.Count == APInt::getNumWords(View.BitWidth) &&
         View.Count <= static_cast<uint64_t>(
                           std::numeric_limits<size_t>::max());
}

const fltSemantics *decodeFloatSemantics(uint32_t Semantics) {
  switch (Semantics) {
  case NEVERC_MIR_FLOAT_SEMANTICS_IEEE_HALF:
    return &APFloat::IEEEhalf();
  case NEVERC_MIR_FLOAT_SEMANTICS_BFLOAT:
    return &APFloat::BFloat();
  case NEVERC_MIR_FLOAT_SEMANTICS_IEEE_SINGLE:
    return &APFloat::IEEEsingle();
  case NEVERC_MIR_FLOAT_SEMANTICS_IEEE_DOUBLE:
    return &APFloat::IEEEdouble();
  case NEVERC_MIR_FLOAT_SEMANTICS_X87_DOUBLE_EXTENDED:
    return &APFloat::x87DoubleExtended();
  case NEVERC_MIR_FLOAT_SEMANTICS_IEEE_QUAD:
    return &APFloat::IEEEquad();
  case NEVERC_MIR_FLOAT_SEMANTICS_PPC_DOUBLE_DOUBLE:
    return &APFloat::PPCDoubleDouble();
  default:
    return nullptr;
  }
}

uint32_t encodeFloatSemantics(const fltSemantics &Semantics) {
  if (&Semantics == &APFloat::IEEEhalf())
    return NEVERC_MIR_FLOAT_SEMANTICS_IEEE_HALF;
  if (&Semantics == &APFloat::BFloat())
    return NEVERC_MIR_FLOAT_SEMANTICS_BFLOAT;
  if (&Semantics == &APFloat::IEEEsingle())
    return NEVERC_MIR_FLOAT_SEMANTICS_IEEE_SINGLE;
  if (&Semantics == &APFloat::IEEEdouble())
    return NEVERC_MIR_FLOAT_SEMANTICS_IEEE_DOUBLE;
  if (&Semantics == &APFloat::x87DoubleExtended())
    return NEVERC_MIR_FLOAT_SEMANTICS_X87_DOUBLE_EXTENDED;
  if (&Semantics == &APFloat::IEEEquad())
    return NEVERC_MIR_FLOAT_SEMANTICS_IEEE_QUAD;
  if (&Semantics == &APFloat::PPCDoubleDouble())
    return NEVERC_MIR_FLOAT_SEMANTICS_PPC_DOUBLE_DOUBLE;
  return 0;
}

NevercStatus resolveFunction(MIRPluginBridge &Bridge,
                             NevercMachineFunctionHandle Handle,
                             MachineFunction **OutFunction) {
  return Bridge.resolveMachineFunction(Handle, OutFunction);
}

NevercStatus NEVERC_CALL getConstantPoolCount(
    void *Context, NevercTaskHandle Task,
    NevercMachineFunctionHandle FunctionHandle, uint64_t *OutCount) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  if (!OutCount)
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  MachineFunction *Function = nullptr;
  NevercStatus Status =
      resolveFunction(*Bridge, FunctionHandle, &Function);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  *OutCount = Function->getConstantPool()->getNumConstants();
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL getConstantPoolEntry(
    void *Context, NevercTaskHandle Task,
    NevercMachineFunctionHandle FunctionHandle, uint32_t Index,
    NevercMIRConstantPoolEntryInfo *OutInfo) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  if (!OutInfo || !validHeader(OutInfo->Header, sizeof(*OutInfo)))
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  MachineFunction *Function = nullptr;
  NevercStatus Status =
      resolveFunction(*Bridge, FunctionHandle, &Function);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  const MachineConstantPool &Pool = *Function->getConstantPool();
  ArrayRef<MachineConstantPoolEntry> Entries = Pool.getConstants();
  if (Index >= Entries.size())
    return mirStatus(NEVERC_STATUS_NOT_FOUND);

  const MachineConstantPoolEntry &Entry = Entries[Index];
  NevercMIRConstantPoolEntryInfo Result{};
  Result.Header = {sizeof(Result), NEVERC_MIR_API_MAJOR, NEVERC_MIR_API_MINOR,
                   0};
  Result.Index = Index;
  Result.Alignment = Entry.getAlign().value();
  Result.Size = Entry.getSizeInBytes(Function->getDataLayout());
  Result.IsMachineSpecific =
      Entry.isMachineConstantPoolEntry() ? NEVERC_TRUE : NEVERC_FALSE;
  if (!Entry.isMachineConstantPoolEntry()) {
    if (const auto *Integer = dyn_cast<ConstantInt>(Entry.Val.ConstVal)) {
      const APInt &Value = Integer->getValue();
      ArrayRef<uint64_t> Words =
          Bridge->setScratchWords(ArrayRef(Value.getRawData(),
                                           Value.getNumWords()));
      Result.Kind = NEVERC_MIR_CONSTANT_INTEGER;
      Result.Value = {Words.data(), Words.size(), Value.getBitWidth(), 0};
    } else if (const auto *Float =
                   dyn_cast<ConstantFP>(Entry.Val.ConstVal)) {
      const APFloat &Value = Float->getValueAPF();
      APInt Bits = Value.bitcastToAPInt();
      ArrayRef<uint64_t> Words =
          Bridge->setScratchWords(ArrayRef(Bits.getRawData(),
                                           Bits.getNumWords()));
      Result.Kind = NEVERC_MIR_CONSTANT_FLOAT;
      Result.Value = {Words.data(), Words.size(), Bits.getBitWidth(),
                      encodeFloatSemantics(Value.getSemantics())};
    }
  }
  *OutInfo = Result;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL createConstantPoolEntry(
    void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
    const NevercMIRConstantPoolEntryDesc *Desc, uint32_t *OutIndex) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  if (!Desc || !OutIndex || !validHeader(Desc->Header, sizeof(*Desc)) ||
      !validAlignment(Desc->Alignment) || !validWordView(Desc->Value))
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
  Status = resolveFunction(*Bridge, *FunctionHandle, &Function);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  ArrayRef<uint64_t> Words(Desc->Value.Data,
                           static_cast<size_t>(Desc->Value.Count));
  APInt Bits(Desc->Value.BitWidth, Words);
  Constant *Value = nullptr;
  switch (Desc->Kind) {
  case NEVERC_MIR_CONSTANT_INTEGER:
    if (Desc->Value.Semantics != 0)
      return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    Value = ConstantInt::get(Function->getFunction().getContext(), Bits);
    break;
  case NEVERC_MIR_CONSTANT_FLOAT: {
    const fltSemantics *Semantics =
        decodeFloatSemantics(Desc->Value.Semantics);
    if (!Semantics ||
        APFloat::semanticsSizeInBits(*Semantics) != Desc->Value.BitWidth)
      return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    Value = ConstantFP::get(Function->getFunction().getContext(),
                            APFloat(*Semantics, Bits));
    break;
  }
  default:
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  }

  MachineConstantPool &Pool = *Function->getConstantPool();
  unsigned PreviousCount = Pool.getNumConstants();
  Align PreviousPoolAlignment = Pool.getConstantPoolAlign();
  std::vector<Align> PreviousEntryAlignments;
  PreviousEntryAlignments.reserve(PreviousCount);
  for (const MachineConstantPoolEntry &Entry : Pool.getConstants())
    PreviousEntryAlignments.push_back(Entry.getAlign());
  unsigned Index = Pool.getConstantPoolIndex(Value, Align(Desc->Alignment));
  if (Index < PreviousCount) {
    Align PreviousEntryAlignment = PreviousEntryAlignments[Index];
    Bridge->addMutationUndo([&Pool, Index, PreviousEntryAlignment,
                             PreviousPoolAlignment] {
      Pool.restoreEntryAlignment(Index, PreviousEntryAlignment);
      Pool.restoreConstantPoolAlignment(PreviousPoolAlignment);
    });
  } else {
    Bridge->addMutationUndo([&Pool, PreviousCount, PreviousPoolAlignment] {
      Pool.truncateNonMachineConstants(PreviousCount, PreviousPoolAlignment);
    });
  }
  Bridge->invalidateMutationProperties();
  Bridge->noteMutation();
  *OutIndex = Index;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL removeConstantPoolEntry(
    void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
    uint32_t Index) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  NevercStatus Status = Bridge->checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  MachineFunction *Function = nullptr;
  auto FunctionHandle = Bridge->machineFunction();
  if (!FunctionHandle) {
    consumeError(FunctionHandle.takeError());
    return mirStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  Status = resolveFunction(*Bridge, *FunctionHandle, &Function);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  MachineConstantPool &Pool = *Function->getConstantPool();
  if (Index >= Pool.getNumConstants())
    return mirStatus(NEVERC_STATUS_NOT_FOUND);
  if (Pool.getConstants()[Index].isMachineConstantPoolEntry())
    return mirStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);

  SmallVector<MachineOperand *, 16> ShiftedOperands;
  for (MachineBasicBlock &Block : *Function)
    for (MachineInstr &Instruction : Block)
      for (MachineOperand &Operand : Instruction.operands()) {
        if (!Operand.isCPI())
          continue;
        unsigned OperandIndex = Operand.getIndex();
        if (OperandIndex == Index)
          return mirStatus(NEVERC_STATUS_POLICY_VIOLATION);
        if (OperandIndex > Index)
          ShiftedOperands.push_back(&Operand);
      }

  Align PreviousPoolAlignment = Pool.getConstantPoolAlign();
  MachineConstantPoolEntry Removed = Pool.removeNonMachineConstant(Index);
  for (MachineOperand *Operand : ShiftedOperands)
    Operand->setIndex(Operand->getIndex() - 1);
  Bridge->addMutationUndo(
      [&Pool, Index, Removed, PreviousPoolAlignment,
       ShiftedOperands = std::move(ShiftedOperands)] {
        Pool.restoreNonMachineConstant(Index, Removed, PreviousPoolAlignment);
        for (MachineOperand *Operand : ShiftedOperands)
          Operand->setIndex(Operand->getIndex() + 1);
      });
  Bridge->invalidateMutationProperties();
  Bridge->noteMutation();
  return neverc_status_ok();
}

Expected<MachineJumpTableInfo::JTEntryKind>
decodeJumpTableKind(NevercMIRJumpTableEntryKind Kind) {
  switch (Kind) {
  case NEVERC_MIR_JT_BLOCK_ADDRESS:
    return MachineJumpTableInfo::EK_BlockAddress;
  case NEVERC_MIR_JT_GP_REL64_BLOCK_ADDRESS:
    return MachineJumpTableInfo::EK_GPRel64BlockAddress;
  case NEVERC_MIR_JT_GP_REL32_BLOCK_ADDRESS:
    return MachineJumpTableInfo::EK_GPRel32BlockAddress;
  case NEVERC_MIR_JT_LABEL_DIFFERENCE32:
    return MachineJumpTableInfo::EK_LabelDifference32;
  case NEVERC_MIR_JT_LABEL_DIFFERENCE64:
    return MachineJumpTableInfo::EK_LabelDifference64;
  case NEVERC_MIR_JT_INLINE:
    return MachineJumpTableInfo::EK_Inline;
  case NEVERC_MIR_JT_CUSTOM32:
    return MachineJumpTableInfo::EK_Custom32;
  default:
    return createStringError(inconvertibleErrorCode(),
                             "invalid MIR jump table entry kind");
  }
}

NevercMIRJumpTableEntryKind
encodeJumpTableKind(MachineJumpTableInfo::JTEntryKind Kind) {
  switch (Kind) {
  case MachineJumpTableInfo::EK_BlockAddress:
    return NEVERC_MIR_JT_BLOCK_ADDRESS;
  case MachineJumpTableInfo::EK_GPRel64BlockAddress:
    return NEVERC_MIR_JT_GP_REL64_BLOCK_ADDRESS;
  case MachineJumpTableInfo::EK_GPRel32BlockAddress:
    return NEVERC_MIR_JT_GP_REL32_BLOCK_ADDRESS;
  case MachineJumpTableInfo::EK_LabelDifference32:
    return NEVERC_MIR_JT_LABEL_DIFFERENCE32;
  case MachineJumpTableInfo::EK_LabelDifference64:
    return NEVERC_MIR_JT_LABEL_DIFFERENCE64;
  case MachineJumpTableInfo::EK_Inline:
    return NEVERC_MIR_JT_INLINE;
  case MachineJumpTableInfo::EK_Custom32:
    return NEVERC_MIR_JT_CUSTOM32;
  }
  llvm_unreachable("unknown LLVM jump table entry kind");
}

NevercStatus NEVERC_CALL getJumpTableCount(
    void *Context, NevercTaskHandle Task,
    NevercMachineFunctionHandle FunctionHandle, uint64_t *OutCount) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  if (!OutCount)
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  MachineFunction *Function = nullptr;
  NevercStatus Status =
      resolveFunction(*Bridge, FunctionHandle, &Function);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  const MachineJumpTableInfo *Info = Function->getJumpTableInfo();
  *OutCount = Info ? Info->getJumpTables().size() : 0;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL getJumpTable(
    void *Context, NevercTaskHandle Task,
    NevercMachineFunctionHandle FunctionHandle, uint32_t Index,
    NevercMIRJumpTableInfo *OutInfo) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  if (!OutInfo || !validHeader(OutInfo->Header, sizeof(*OutInfo)))
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  MachineFunction *Function = nullptr;
  NevercStatus Status =
      resolveFunction(*Bridge, FunctionHandle, &Function);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  const MachineJumpTableInfo *Info = Function->getJumpTableInfo();
  if (!Info || Index >= Info->getJumpTables().size())
    return mirStatus(NEVERC_STATUS_NOT_FOUND);
  const MachineJumpTableEntry &Entry = Info->getJumpTables()[Index];
  NevercMIRJumpTableInfo Result{};
  Result.Header = {sizeof(Result), NEVERC_MIR_API_MAJOR, NEVERC_MIR_API_MINOR,
                   0};
  Result.Index = Index;
  Result.EntryKind = encodeJumpTableKind(Info->getEntryKind());
  Result.DestinationCount = Entry.MBBs.size();
  Result.IsDeleted = Entry.MBBs.empty() ? NEVERC_TRUE : NEVERC_FALSE;
  *OutInfo = Result;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL getJumpTableDestination(
    void *Context, NevercTaskHandle Task,
    NevercMachineFunctionHandle FunctionHandle, uint32_t Index,
    uint64_t DestinationIndex, NevercMachineBasicBlockHandle *OutBlock) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  if (!OutBlock)
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  MachineFunction *Function = nullptr;
  NevercStatus Status =
      resolveFunction(*Bridge, FunctionHandle, &Function);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  const MachineJumpTableInfo *Info = Function->getJumpTableInfo();
  if (!Info || Index >= Info->getJumpTables().size())
    return mirStatus(NEVERC_STATUS_NOT_FOUND);
  const MachineJumpTableEntry &Entry = Info->getJumpTables()[Index];
  if (DestinationIndex >= Entry.MBBs.size())
    return mirStatus(NEVERC_STATUS_NOT_FOUND);
  auto Handle = Bridge->wrapBasicBlock(*Entry.MBBs[DestinationIndex]);
  if (!Handle) {
    consumeError(Handle.takeError());
    return mirStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutBlock = *Handle;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL createJumpTable(
    void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
    NevercMIRJumpTableEntryKind EntryKind,
    const NevercMachineBasicBlockHandle *Destinations,
    uint64_t DestinationCount, uint32_t *OutIndex) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  if (!Destinations || DestinationCount == 0 || !OutIndex ||
      DestinationCount >
          static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  auto DecodedKind = decodeJumpTableKind(EntryKind);
  if (!DecodedKind) {
    consumeError(DecodedKind.takeError());
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  }
  NevercStatus Status = Bridge->checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  MachineFunction *Function = nullptr;
  auto FunctionHandle = Bridge->machineFunction();
  if (!FunctionHandle) {
    consumeError(FunctionHandle.takeError());
    return mirStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  Status = resolveFunction(*Bridge, *FunctionHandle, &Function);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  std::vector<MachineBasicBlock *> Blocks;
  Blocks.reserve(static_cast<size_t>(DestinationCount));
  for (uint64_t I = 0; I != DestinationCount; ++I) {
    MachineBasicBlock *Block = nullptr;
    Status = Bridge->resolveBasicBlock(Destinations[I], &Block);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    if (!Block || Block->getParent() != Function)
      return mirStatus(NEVERC_STATUS_WRONG_SCOPE);
    Blocks.push_back(Block);
  }

  bool HadInfo = Function->getJumpTableInfo() != nullptr;
  MachineJumpTableInfo *Info = Function->getJumpTableInfo();
  if (Info && Info->getEntryKind() != *DecodedKind)
    return mirStatus(NEVERC_STATUS_INVALID_STATE);
  if (!Info)
    Info = Function->getOrCreateJumpTableInfo(*DecodedKind);
  unsigned PreviousCount = Info->getJumpTables().size();
  unsigned Index = Info->createJumpTableIndex(Blocks);
  Bridge->addMutationUndo([Function, Info, PreviousCount, HadInfo] {
    Info->truncateJumpTables(PreviousCount);
    if (!HadInfo)
      Function->discardEmptyJumpTableInfo();
  });
  Bridge->invalidateMutationProperties();
  Bridge->noteMutation();
  *OutIndex = Index;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL removeJumpTable(
    void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
    uint32_t Index) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  NevercStatus Status = Bridge->checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  MachineFunction *Function = nullptr;
  auto FunctionHandle = Bridge->machineFunction();
  if (!FunctionHandle) {
    consumeError(FunctionHandle.takeError());
    return mirStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  Status = resolveFunction(*Bridge, *FunctionHandle, &Function);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  MachineJumpTableInfo *Info = Function->getJumpTableInfo();
  if (!Info || Index >= Info->getJumpTables().size() ||
      Info->getJumpTables()[Index].MBBs.empty())
    return mirStatus(NEVERC_STATUS_NOT_FOUND);
  std::vector<MachineBasicBlock *> Previous =
      Info->getJumpTables()[Index].MBBs;
  Info->RemoveJumpTable(Index);
  Bridge->addMutationUndo(
      [Info, Index, Previous = std::move(Previous)] {
        Info->restoreJumpTable(Index, Previous);
      });
  Bridge->invalidateMutationProperties();
  Bridge->noteMutation();
  return neverc_status_ok();
}

#undef NEVERC_MIR_BRIDGE_OR_RETURN

} // namespace

void initializeMIRConstantPoolAPI(NevercMIRAPI &API) {
  API.GetConstantPoolCount = getConstantPoolCount;
  API.GetConstantPoolEntry = getConstantPoolEntry;
  API.CreateConstantPoolEntry = createConstantPoolEntry;
  API.RemoveConstantPoolEntry = removeConstantPoolEntry;
  API.GetJumpTableCount = getJumpTableCount;
  API.GetJumpTable = getJumpTable;
  API.GetJumpTableDestination = getJumpTableDestination;
  API.CreateJumpTable = createJumpTable;
  API.RemoveJumpTable = removeJumpTable;
}

} // namespace neverc::plugin
