#include "MIRBridgeInternal.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include <iterator>

using namespace llvm;

namespace neverc::plugin {

NevercStatus mirStatus(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

MIRPluginBridge *getMIRBridge(void *Context, NevercTaskHandle Task,
                              NevercStatus *OutStatus) {
  if (!Context || !OutStatus)
    return nullptr;
  auto *Bridge = static_cast<MIRPluginBridge *>(Context);
  NevercTaskHandle Expected = Bridge->taskHandle();
  if (Expected.Owner != Task.Owner || Expected.Value != Task.Value) {
    *OutStatus = mirStatus(NEVERC_STATUS_WRONG_SCOPE);
    return nullptr;
  }
  *OutStatus = neverc_status_ok();
  return Bridge;
}

NevercMIROperandKind stableMIROperandKind(unsigned LLVMKind) {
  switch (LLVMKind) {
#define NEVERC_MIR_SCHEMA_OPERAND(Internal, Symbol, ID, LLVMValue, Name)       \
  case LLVMValue:                                                              \
    return ID;
#include "neverc/Plugin/Schema/PluginMIRSchema.inc"
#undef NEVERC_MIR_SCHEMA_OPERAND
  default:
    return 0;
  }
}

NevercMIRGenericOpcode stableMIRGenericOpcode(unsigned LLVMOpcode) {
  switch (LLVMOpcode) {
#define NEVERC_MIR_SCHEMA_GENERIC_OPCODE(Internal, Symbol, ID, LLVMValue,      \
                                         Name, RequiresTarget)                 \
  case LLVMValue:                                                              \
    return ID;
#include "neverc/Plugin/Schema/PluginMIRSchema.inc"
#undef NEVERC_MIR_SCHEMA_GENERIC_OPCODE
  default:
    return 0;
  }
}

Expected<unsigned> llvmMIRGenericOpcode(NevercMIRGenericOpcode StableOpcode) {
  switch (StableOpcode) {
#define NEVERC_MIR_SCHEMA_GENERIC_OPCODE(Internal, Symbol, ID, LLVMValue,      \
                                         Name, RequiresTarget)                 \
  case ID:                                                                     \
    return LLVMValue;
#include "neverc/Plugin/Schema/PluginMIRSchema.inc"
#undef NEVERC_MIR_SCHEMA_GENERIC_OPCODE
  default:
    return createStringError(inconvertibleErrorCode(),
                             "unknown stable MIR generic opcode");
  }
}

namespace {

template <typename T> bool validOutput(const T *Output) {
  return Output && Output->Header.StructSize >= sizeof(T) &&
         Output->Header.Major == NEVERC_MIR_API_MAJOR;
}

template <typename HandleT>
NevercStatus writeHandle(Expected<HandleT> Handle, HandleT *OutHandle) {
  if (!OutHandle) {
    if (!Handle)
      consumeError(Handle.takeError());
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  }
  *OutHandle = {};
  if (!Handle) {
    consumeError(Handle.takeError());
    return mirStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutHandle = *Handle;
  return neverc_status_ok();
}

#define NEVERC_MIR_BRIDGE_OR_RETURN()                                          \
  NevercStatus BridgeStatus;                                                   \
  MIRPluginBridge *Bridge = getMIRBridge(Context, Task, &BridgeStatus);        \
  if (!Bridge)                                                                 \
  return BridgeStatus

NevercStatus NEVERC_CALL BeginMutation(void *Context, NevercTaskHandle Task,
                                       NevercMachineFunctionHandle Function,
                                       NevercMIRMutationHandle *OutMutation) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  MachineFunction *Resolved = nullptr;
  NevercStatus Status = Bridge->resolveMachineFunction(Function, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  return writeHandle(Bridge->beginMutation(), OutMutation);
}

NevercStatus NEVERC_CALL EndMutation(void *Context, NevercTaskHandle Task,
                                     NevercMIRMutationHandle Mutation) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  return Bridge->endMutation(Mutation);
}

NevercStatus NEVERC_CALL GetMachineFunctionGeneration(
    void *Context, NevercTaskHandle Task, NevercMachineFunctionHandle Function,
    uint64_t *OutGeneration) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  if (!OutGeneration)
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  MachineFunction *Resolved = nullptr;
  NevercStatus Status = Bridge->resolveMachineFunction(Function, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  *OutGeneration = Bridge->functionGeneration();
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL
GetBasicBlockCount(void *Context, NevercTaskHandle Task,
                   NevercMachineFunctionHandle Function, uint64_t *OutCount) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  if (!OutCount)
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  MachineFunction *Resolved = nullptr;
  NevercStatus Status = Bridge->resolveMachineFunction(Function, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  *OutCount = Resolved->size();
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL GetFirstBasicBlock(
    void *Context, NevercTaskHandle Task, NevercMachineFunctionHandle Function,
    NevercMachineBasicBlockHandle *OutBlock) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  if (!OutBlock)
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutBlock = {};
  MachineFunction *Resolved = nullptr;
  NevercStatus Status = Bridge->resolveMachineFunction(Function, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Resolved->empty())
    return mirStatus(NEVERC_STATUS_NOT_FOUND);
  return writeHandle(Bridge->wrapBasicBlock(Resolved->front()), OutBlock);
}

NevercStatus NEVERC_CALL GetLastBasicBlock(
    void *Context, NevercTaskHandle Task, NevercMachineFunctionHandle Function,
    NevercMachineBasicBlockHandle *OutBlock) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  if (!OutBlock)
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutBlock = {};
  MachineFunction *Resolved = nullptr;
  NevercStatus Status = Bridge->resolveMachineFunction(Function, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Resolved->empty())
    return mirStatus(NEVERC_STATUS_NOT_FOUND);
  return writeHandle(Bridge->wrapBasicBlock(Resolved->back()), OutBlock);
}

NevercStatus findAdjacentBlock(MIRPluginBridge &Bridge,
                               MachineBasicBlock &Block, bool Next,
                               NevercMachineBasicBlockHandle *OutBlock) {
  if (!OutBlock)
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutBlock = {};
  MachineBasicBlock *Previous = nullptr;
  bool ReturnNext = false;
  for (MachineBasicBlock &Candidate : *Block.getParent()) {
    if (ReturnNext)
      return writeHandle(Bridge.wrapBasicBlock(Candidate), OutBlock);
    if (&Candidate == &Block) {
      if (!Next) {
        if (!Previous)
          return mirStatus(NEVERC_STATUS_NOT_FOUND);
        return writeHandle(Bridge.wrapBasicBlock(*Previous), OutBlock);
      }
      ReturnNext = true;
    }
    Previous = &Candidate;
  }
  return mirStatus(NEVERC_STATUS_NOT_FOUND);
}

NevercStatus NEVERC_CALL GetNextBasicBlock(
    void *Context, NevercTaskHandle Task, NevercMachineBasicBlockHandle Block,
    NevercMachineBasicBlockHandle *OutBlock) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  MachineBasicBlock *Resolved = nullptr;
  NevercStatus Status = Bridge->resolveBasicBlock(Block, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  return findAdjacentBlock(*Bridge, *Resolved, true, OutBlock);
}

NevercStatus NEVERC_CALL GetPreviousBasicBlock(
    void *Context, NevercTaskHandle Task, NevercMachineBasicBlockHandle Block,
    NevercMachineBasicBlockHandle *OutBlock) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  MachineBasicBlock *Resolved = nullptr;
  NevercStatus Status = Bridge->resolveBasicBlock(Block, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  return findAdjacentBlock(*Bridge, *Resolved, false, OutBlock);
}

NevercStatus NEVERC_CALL CollectBasicBlocks(
    void *Context, NevercTaskHandle Task, NevercMachineFunctionHandle Function,
    NevercMachineBasicBlockHandle *OutBlocks, uint64_t Capacity,
    uint64_t *OutCount) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  if (!OutCount || (!OutBlocks && Capacity != 0))
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  MachineFunction *Resolved = nullptr;
  NevercStatus Status = Bridge->resolveMachineFunction(Function, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  *OutCount = Resolved->size();
  if (!OutBlocks)
    return neverc_status_ok();
  if (Capacity < Resolved->size())
    return mirStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  size_t Index = 0;
  for (MachineBasicBlock &Block : *Resolved) {
    auto Handle = Bridge->wrapBasicBlock(Block);
    if (!Handle) {
      consumeError(Handle.takeError());
      return mirStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    OutBlocks[Index++] = *Handle;
  }
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL
GetBasicBlockNumber(void *Context, NevercTaskHandle Task,
                    NevercMachineBasicBlockHandle Block, int64_t *OutNumber) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  if (!OutNumber)
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  MachineBasicBlock *Resolved = nullptr;
  NevercStatus Status = Bridge->resolveBasicBlock(Block, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  *OutNumber = Resolved->getNumber();
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL GetBasicBlockFunction(
    void *Context, NevercTaskHandle Task, NevercMachineBasicBlockHandle Block,
    NevercMachineFunctionHandle *OutFunction) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  MachineBasicBlock *Resolved = nullptr;
  NevercStatus Status = Bridge->resolveBasicBlock(Block, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  return writeHandle(Bridge->machineFunction(), OutFunction);
}

NevercStatus NEVERC_CALL
GetInstructionCount(void *Context, NevercTaskHandle Task,
                    NevercMachineBasicBlockHandle Block, uint64_t *OutCount) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  if (!OutCount)
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  MachineBasicBlock *Resolved = nullptr;
  NevercStatus Status = Bridge->resolveBasicBlock(Block, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  *OutCount = Resolved->size();
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL GetFirstInstruction(
    void *Context, NevercTaskHandle Task, NevercMachineBasicBlockHandle Block,
    NevercMachineInstrHandle *OutInstruction) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  if (!OutInstruction)
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutInstruction = {};
  MachineBasicBlock *Resolved = nullptr;
  NevercStatus Status = Bridge->resolveBasicBlock(Block, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Resolved->empty())
    return mirStatus(NEVERC_STATUS_NOT_FOUND);
  return writeHandle(Bridge->wrapInstruction(Resolved->front()),
                     OutInstruction);
}

NevercStatus NEVERC_CALL GetLastInstruction(
    void *Context, NevercTaskHandle Task, NevercMachineBasicBlockHandle Block,
    NevercMachineInstrHandle *OutInstruction) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  if (!OutInstruction)
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutInstruction = {};
  MachineBasicBlock *Resolved = nullptr;
  NevercStatus Status = Bridge->resolveBasicBlock(Block, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Resolved->empty())
    return mirStatus(NEVERC_STATUS_NOT_FOUND);
  return writeHandle(Bridge->wrapInstruction(Resolved->back()), OutInstruction);
}

NevercStatus findAdjacentInstruction(MIRPluginBridge &Bridge,
                                     MachineInstr &Instruction, bool Next,
                                     NevercMachineInstrHandle *OutInstruction) {
  if (!OutInstruction)
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutInstruction = {};
  MachineInstr *Previous = nullptr;
  bool ReturnNext = false;
  for (MachineInstr &Candidate : *Instruction.getParent()) {
    if (ReturnNext)
      return writeHandle(Bridge.wrapInstruction(Candidate), OutInstruction);
    if (&Candidate == &Instruction) {
      if (!Next) {
        if (!Previous)
          return mirStatus(NEVERC_STATUS_NOT_FOUND);
        return writeHandle(Bridge.wrapInstruction(*Previous), OutInstruction);
      }
      ReturnNext = true;
    }
    Previous = &Candidate;
  }
  return mirStatus(NEVERC_STATUS_NOT_FOUND);
}

NevercStatus NEVERC_CALL GetNextInstruction(
    void *Context, NevercTaskHandle Task, NevercMachineInstrHandle Instruction,
    NevercMachineInstrHandle *OutInstruction) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  MachineInstr *Resolved = nullptr;
  NevercStatus Status = Bridge->resolveInstruction(Instruction, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  return findAdjacentInstruction(*Bridge, *Resolved, true, OutInstruction);
}

NevercStatus NEVERC_CALL GetPreviousInstruction(
    void *Context, NevercTaskHandle Task, NevercMachineInstrHandle Instruction,
    NevercMachineInstrHandle *OutInstruction) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  MachineInstr *Resolved = nullptr;
  NevercStatus Status = Bridge->resolveInstruction(Instruction, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  return findAdjacentInstruction(*Bridge, *Resolved, false, OutInstruction);
}

NevercStatus NEVERC_CALL CollectInstructions(
    void *Context, NevercTaskHandle Task, NevercMachineBasicBlockHandle Block,
    NevercMachineInstrHandle *OutInstructions, uint64_t Capacity,
    uint64_t *OutCount) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  if (!OutCount || (!OutInstructions && Capacity != 0))
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  MachineBasicBlock *Resolved = nullptr;
  NevercStatus Status = Bridge->resolveBasicBlock(Block, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  *OutCount = Resolved->size();
  if (!OutInstructions)
    return neverc_status_ok();
  if (Capacity < Resolved->size())
    return mirStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  size_t Index = 0;
  for (MachineInstr &Instruction : *Resolved) {
    auto Handle = Bridge->wrapInstruction(Instruction);
    if (!Handle) {
      consumeError(Handle.takeError());
      return mirStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    OutInstructions[Index++] = *Handle;
  }
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL GetInstructionInfo(
    void *Context, NevercTaskHandle Task, NevercMachineInstrHandle Instruction,
    NevercMIRInstructionInfo *OutInfo) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  if (!validOutput(OutInfo))
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  MachineInstr *Resolved = nullptr;
  NevercStatus Status = Bridge->resolveInstruction(Instruction, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  NevercMIRInstructionInfo Result{};
  Result.Header = {sizeof(Result), NEVERC_MIR_API_MAJOR, NEVERC_MIR_API_MINOR,
                   0};
  Result.StableOpcode = stableMIRGenericOpcode(Resolved->getOpcode());
  Result.RequiresTargetSchema = isTargetSpecificOpcode(Resolved->getOpcode())
                                    ? NEVERC_TRUE
                                    : NEVERC_FALSE;
  Result.TargetOpcode = Result.RequiresTargetSchema ? Resolved->getOpcode() : 0;
  Result.IsBranch = Resolved->isBranch();
  Result.IsCall = Resolved->isCall();
  Result.IsReturn = Resolved->isReturn();
  Result.IsTerminator = Resolved->isTerminator();
  Result.IsBarrier = Resolved->isBarrier();
  Result.IsInlineAssembly = Resolved->isInlineAsm();
  Result.IsDebugInstruction = Resolved->isDebugInstr();
  Result.IsPseudo = Resolved->isPseudo();
  Result.IsBundle = Resolved->isBundle();
  Result.Flags = static_cast<NevercMIRInstructionFlags>(Resolved->getFlags());
  Result.OperandCount = Resolved->getNumOperands();
  Result.MemoryOperandCount = Resolved->getNumMemOperands();
  *OutInfo = Result;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL GetInstructionBasicBlock(
    void *Context, NevercTaskHandle Task, NevercMachineInstrHandle Instruction,
    NevercMachineBasicBlockHandle *OutBlock) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  MachineInstr *Resolved = nullptr;
  NevercStatus Status = Bridge->resolveInstruction(Instruction, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  return writeHandle(Bridge->wrapBasicBlock(*Resolved->getParent()), OutBlock);
}

NevercStatus NEVERC_CALL GetInstructionDebugLocation(
    void *Context, NevercTaskHandle Task, NevercMachineInstrHandle Instruction,
    NevercMIRDebugLocation *OutLocation) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  if (!validOutput(OutLocation))
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  MachineInstr *Resolved = nullptr;
  NevercStatus Status = Bridge->resolveInstruction(Instruction, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  const DILocation *Location = Resolved->getDebugLoc().get();
  if (!Location)
    return mirStatus(NEVERC_STATUS_NOT_FOUND);
  NevercMIRDebugLocation Result{};
  Result.Header = {sizeof(Result), NEVERC_MIR_API_MAJOR, NEVERC_MIR_API_MINOR,
                   0};
  Result.Line = Location->getLine();
  Result.Column = Location->getColumn();
  Result.Discriminator = Location->getDiscriminator();
  Result.IsImplicitCode = Location->isImplicitCode();
  StringRef Directory = Location->getDirectory();
  StringRef Filename = Location->getFilename();
  Result.Directory = {Directory.data(), Directory.size()};
  Result.Filename = {Filename.data(), Filename.size()};
  *OutLocation = Result;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL GetInstructionOperand(
    void *Context, NevercTaskHandle Task, NevercMachineInstrHandle Instruction,
    uint64_t Index, NevercMachineOperandHandle *OutOperand) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  MachineInstr *Resolved = nullptr;
  NevercStatus Status = Bridge->resolveInstruction(Instruction, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Index >= Resolved->getNumOperands())
    return mirStatus(NEVERC_STATUS_NOT_FOUND);
  return writeHandle(
      Bridge->wrapOperand(Resolved->getOperand(static_cast<unsigned>(Index))),
      OutOperand);
}

NevercStatus NEVERC_CALL GetSuccessorCount(void *Context, NevercTaskHandle Task,
                                           NevercMachineBasicBlockHandle Block,
                                           uint64_t *OutCount) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  if (!OutCount)
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  MachineBasicBlock *Resolved = nullptr;
  NevercStatus Status = Bridge->resolveBasicBlock(Block, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  *OutCount = Resolved->succ_size();
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL GetSuccessor(void *Context, NevercTaskHandle Task,
                                      NevercMachineBasicBlockHandle Block,
                                      uint64_t Index,
                                      NevercMIRCFGEdge *OutEdge) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  if (!OutEdge)
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutEdge = {};
  MachineBasicBlock *Resolved = nullptr;
  NevercStatus Status = Bridge->resolveBasicBlock(Block, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Index >= Resolved->succ_size())
    return mirStatus(NEVERC_STATUS_NOT_FOUND);
  auto It = Resolved->succ_begin();
  std::advance(It, static_cast<ptrdiff_t>(Index));
  auto Handle = Bridge->wrapBasicBlock(**It);
  if (!Handle) {
    consumeError(Handle.takeError());
    return mirStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  BranchProbability Probability = Resolved->getSuccProbability(It);
  OutEdge->Block = *Handle;
  OutEdge->ProbabilityNumerator = Probability.getNumerator();
  OutEdge->ProbabilityDenominator = Probability.getDenominator();
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL
GetPredecessorCount(void *Context, NevercTaskHandle Task,
                    NevercMachineBasicBlockHandle Block, uint64_t *OutCount) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  if (!OutCount)
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  MachineBasicBlock *Resolved = nullptr;
  NevercStatus Status = Bridge->resolveBasicBlock(Block, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  *OutCount = Resolved->pred_size();
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL GetPredecessor(
    void *Context, NevercTaskHandle Task, NevercMachineBasicBlockHandle Block,
    uint64_t Index, NevercMachineBasicBlockHandle *OutPredecessor) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  MachineBasicBlock *Resolved = nullptr;
  NevercStatus Status = Bridge->resolveBasicBlock(Block, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Index >= Resolved->pred_size())
    return mirStatus(NEVERC_STATUS_NOT_FOUND);
  auto It = Resolved->pred_begin();
  std::advance(It, static_cast<ptrdiff_t>(Index));
  return writeHandle(Bridge->wrapBasicBlock(**It), OutPredecessor);
}

NevercStatus NEVERC_CALL GetLiveInCount(void *Context, NevercTaskHandle Task,
                                        NevercMachineBasicBlockHandle Block,
                                        uint64_t *OutCount) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  if (!OutCount)
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  MachineBasicBlock *Resolved = nullptr;
  NevercStatus Status = Bridge->resolveBasicBlock(Block, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  *OutCount =
      std::distance(Resolved->livein_begin_dbg(), Resolved->livein_end());
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL GetLiveIn(void *Context, NevercTaskHandle Task,
                                   NevercMachineBasicBlockHandle Block,
                                   uint64_t Index, NevercMIRLiveIn *OutLiveIn) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  if (!OutLiveIn)
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  MachineBasicBlock *Resolved = nullptr;
  NevercStatus Status = Bridge->resolveBasicBlock(Block, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  uint64_t Count =
      std::distance(Resolved->livein_begin_dbg(), Resolved->livein_end());
  if (Index >= Count)
    return mirStatus(NEVERC_STATUS_NOT_FOUND);
  auto It = Resolved->livein_begin_dbg();
  std::advance(It, static_cast<ptrdiff_t>(Index));
  *OutLiveIn = {static_cast<uint32_t>(It->PhysReg), 0,
                It->LaneMask.getAsInteger()};
  return neverc_status_ok();
}

#undef NEVERC_MIR_BRIDGE_OR_RETURN

static_assert(MachineInstr::FrameSetup == NEVERC_MIR_INSTR_FLAG_FRAME_SETUP);
static_assert(MachineInstr::FrameDestroy ==
              NEVERC_MIR_INSTR_FLAG_FRAME_DESTROY);
static_assert(MachineInstr::NoConvergent ==
              NEVERC_MIR_INSTR_FLAG_NO_CONVERGENT);

} // namespace

void initializeMIRCoreAPI(NevercMIRAPI &API) {
  API.BeginMutation = BeginMutation;
  API.EndMutation = EndMutation;
  API.GetMachineFunctionGeneration = GetMachineFunctionGeneration;
  API.GetBasicBlockCount = GetBasicBlockCount;
  API.GetFirstBasicBlock = GetFirstBasicBlock;
  API.GetLastBasicBlock = GetLastBasicBlock;
  API.GetNextBasicBlock = GetNextBasicBlock;
  API.GetPreviousBasicBlock = GetPreviousBasicBlock;
  API.CollectBasicBlocks = CollectBasicBlocks;
  API.GetBasicBlockNumber = GetBasicBlockNumber;
  API.GetBasicBlockFunction = GetBasicBlockFunction;
  API.GetInstructionCount = GetInstructionCount;
  API.GetFirstInstruction = GetFirstInstruction;
  API.GetLastInstruction = GetLastInstruction;
  API.GetNextInstruction = GetNextInstruction;
  API.GetPreviousInstruction = GetPreviousInstruction;
  API.CollectInstructions = CollectInstructions;
  API.GetInstructionInfo = GetInstructionInfo;
  API.GetInstructionBasicBlock = GetInstructionBasicBlock;
  API.GetInstructionDebugLocation = GetInstructionDebugLocation;
  API.GetInstructionOperand = GetInstructionOperand;
  API.GetOperandValue = getMIROperandValue;
  API.GetOperandInstruction = getMIROperandInstruction;
  API.SetOperandValue = setMIROperandValue;
  API.GetSuccessorCount = GetSuccessorCount;
  API.GetSuccessor = GetSuccessor;
  API.GetPredecessorCount = GetPredecessorCount;
  API.GetPredecessor = GetPredecessor;
  API.GetLiveInCount = GetLiveInCount;
  API.GetLiveIn = GetLiveIn;
}

} // namespace neverc::plugin
