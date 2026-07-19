#include "MIRBridgeInternal.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/Support/BranchProbability.h"
#include <algorithm>
#include <utility>
#include <vector>

using namespace llvm;

namespace neverc::plugin {
namespace {

#define NEVERC_MIR_BRIDGE_OR_RETURN()                                          \
  NevercStatus BridgeStatus;                                                   \
  MIRPluginBridge *Bridge = getMIRBridge(Context, Task, &BridgeStatus);        \
  if (!Bridge)                                                                 \
  return BridgeStatus

bool isNullHandle(NevercHandle Handle) {
  return Handle.Owner == 0 && Handle.Value == 0;
}

bool isAttached(const MachineBasicBlock &Block) {
  return Block.getParent() && Block.getNumber() >= 0;
}

NevercStatus checkMutation(MIRPluginBridge &Bridge,
                           NevercMIRMutationHandle Mutation) {
  NevercStatus Status = Bridge.checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return mirStatus(NEVERC_STATUS_POLICY_VIOLATION);
  return Status;
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

NevercStatus resolveOptionalBlock(MIRPluginBridge &Bridge,
                                  NevercMachineBasicBlockHandle Handle,
                                  MachineBasicBlock **OutBlock) {
  if (!OutBlock)
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutBlock = nullptr;
  if (isNullHandle(Handle))
    return neverc_status_ok();
  NevercStatus Status = Bridge.resolveBasicBlock(Handle, OutBlock);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!isAttached(**OutBlock))
    return mirStatus(NEVERC_STATUS_INVALID_STATE);
  return neverc_status_ok();
}

NevercStatus resolveOptionalInstruction(MIRPluginBridge &Bridge,
                                        NevercMachineInstrHandle Handle,
                                        MachineInstr **OutInstruction) {
  if (!OutInstruction)
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutInstruction = nullptr;
  if (isNullHandle(Handle))
    return neverc_status_ok();
  NevercStatus Status = Bridge.resolveInstruction(Handle, OutInstruction);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!(*OutInstruction)->getParent())
    return mirStatus(NEVERC_STATUS_INVALID_STATE);
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL CommitMutation(void *Context, NevercTaskHandle Task,
                                        NevercMIRMutationHandle Mutation) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  return Bridge->commitMutation(Mutation);
}

NevercStatus NEVERC_CALL AbortMutation(void *Context, NevercTaskHandle Task,
                                       NevercMIRMutationHandle Mutation) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  return Bridge->abandonMutation(Mutation);
}

NevercStatus NEVERC_CALL SetInstructionFlags(
    void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
    NevercMachineInstrHandle Instruction, NevercMIRInstructionFlags Flags) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  NevercStatus Status = checkMutation(*Bridge, Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  constexpr NevercMIRInstructionFlags KnownFlags = (UINT64_C(1) << 18) - 1;
  if ((Flags & ~KnownFlags) != 0)
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  MachineInstr *Resolved = nullptr;
  Status = Bridge->resolveInstruction(Instruction, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!Resolved->getParent())
    return mirStatus(NEVERC_STATUS_INVALID_STATE);
  uint32_t Previous = Resolved->getFlags();
  Resolved->setFlags(static_cast<unsigned>(Flags));
  Bridge->addMutationUndo(
      [Resolved, Previous] { Resolved->setFlags(Previous); });
  Bridge->invalidateMutationProperties();
  Bridge->noteMutation();
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL CreateBasicBlock(
    void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
    NevercMachineBasicBlockHandle InsertBefore,
    NevercMachineBasicBlockHandle *OutBlock) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  if (!OutBlock)
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutBlock = {};
  NevercStatus Status = checkMutation(*Bridge, Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  MachineBasicBlock *Before = nullptr;
  Status = resolveOptionalBlock(*Bridge, InsertBefore, &Before);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  MachineFunction *Function = Before ? Before->getParent() : nullptr;
  if (!Function) {
    auto FunctionHandle = Bridge->machineFunction();
    if (!FunctionHandle) {
      consumeError(FunctionHandle.takeError());
      return mirStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    Status = Bridge->resolveMachineFunction(*FunctionHandle, &Function);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
  }
  MachineBasicBlock *Created = Function->CreateMachineBasicBlock();
  Function->insert(Before ? Before->getIterator() : Function->end(), Created);
  auto Handle = Bridge->wrapBasicBlock(*Created);
  if (!Handle) {
    consumeError(Handle.takeError());
    Created->eraseFromParent();
    return mirStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutBlock = *Handle;
  Bridge->addMutationUndo([Bridge, Created] {
    (void)Bridge->invalidateBasicBlock(*Created);
    Created->eraseFromParent();
  });
  Bridge->invalidateMutationProperties();
  Bridge->noteMutation();
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL MoveBasicBlock(
    void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
    NevercMachineBasicBlockHandle Block,
    NevercMachineBasicBlockHandle InsertBefore) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  NevercStatus Status = checkMutation(*Bridge, Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  MachineBasicBlock *Resolved = nullptr;
  Status = Bridge->resolveBasicBlock(Block, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!isAttached(*Resolved))
    return mirStatus(NEVERC_STATUS_INVALID_STATE);
  MachineBasicBlock *Before = nullptr;
  Status = resolveOptionalBlock(*Bridge, InsertBefore, &Before);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  MachineFunction *Function = Resolved->getParent();
  if (Before && Before->getParent() != Function)
    return mirStatus(NEVERC_STATUS_WRONG_SCOPE);
  if (Before == Resolved || Resolved->getNextNode() == Before ||
      (!Before && Resolved == &Function->back()))
    return neverc_status_ok();

  MachineBasicBlock *OldNext = Resolved->getNextNode();
  Function->splice(Before ? Before->getIterator() : Function->end(), Resolved);
  Bridge->addMutationUndo([Function, Resolved, OldNext] {
    Function->splice(OldNext ? OldNext->getIterator() : Function->end(),
                     Resolved);
  });
  Bridge->invalidateMutationProperties();
  Bridge->noteMutation();
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL EraseBasicBlock(void *Context, NevercTaskHandle Task,
                                         NevercMIRMutationHandle Mutation,
                                         NevercMachineBasicBlockHandle Block) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  NevercStatus Status = checkMutation(*Bridge, Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  MachineBasicBlock *Resolved = nullptr;
  Status = Bridge->resolveBasicBlock(Block, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!isAttached(*Resolved))
    return mirStatus(NEVERC_STATUS_INVALID_STATE);
  if (!Resolved->empty() || !Resolved->pred_empty() || !Resolved->succ_empty())
    return mirStatus(NEVERC_STATUS_POLICY_VIOLATION);

  MachineFunction *Function = Resolved->getParent();
  MachineBasicBlock *OldNext = Resolved->getNextNode();
  Resolved->removeFromParent();
  Bridge->addMutationUndo([Function, Resolved, OldNext] {
    Function->insert(OldNext ? OldNext->getIterator() : Function->end(),
                     Resolved);
  });
  Bridge->addMutationCommit([Bridge, Function, Resolved] {
    (void)Bridge->invalidateBasicBlock(*Resolved);
    Function->deleteMachineBasicBlock(Resolved);
  });
  Bridge->invalidateMutationProperties();
  Bridge->noteMutation();
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL CreateInstruction(
    void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
    NevercMachineBasicBlockHandle Block, NevercMachineInstrHandle InsertBefore,
    NevercMIRInstructionOpcode Opcode,
    NevercMachineInstrHandle *OutInstruction) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  if (!OutInstruction)
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutInstruction = {};
  NevercStatus Status = checkMutation(*Bridge, Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  MachineBasicBlock *ResolvedBlock = nullptr;
  Status = Bridge->resolveBasicBlock(Block, &ResolvedBlock);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!isAttached(*ResolvedBlock))
    return mirStatus(NEVERC_STATUS_INVALID_STATE);
  MachineInstr *Before = nullptr;
  Status = resolveOptionalInstruction(*Bridge, InsertBefore, &Before);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Before && Before->getParent() != ResolvedBlock)
    return mirStatus(NEVERC_STATUS_WRONG_SCOPE);
  if (Opcode.RequiresTargetSchema != NEVERC_FALSE &&
      Opcode.RequiresTargetSchema != NEVERC_TRUE)
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);

  unsigned LLVMOpcode = 0;
  if (Opcode.RequiresTargetSchema == NEVERC_TRUE) {
    if (!Bridge->targetSchemaEnabled())
      return mirStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
    if (Opcode.StableOpcode != 0)
      return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    LLVMOpcode = Opcode.TargetOpcode;
    if (!isTargetSpecificOpcode(LLVMOpcode))
      return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  } else {
    if (Opcode.StableOpcode == 0 || Opcode.TargetOpcode != 0)
      return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    auto Converted = llvmMIRGenericOpcode(Opcode.StableOpcode);
    if (!Converted) {
      consumeError(Converted.takeError());
      return mirStatus(NEVERC_STATUS_NOT_FOUND);
    }
    LLVMOpcode = *Converted;
  }

  MachineFunction *Function = ResolvedBlock->getParent();
  const TargetInstrInfo *TII = Function->getSubtarget().getInstrInfo();
  if (!TII || LLVMOpcode >= TII->getNumOpcodes())
    return mirStatus(NEVERC_STATUS_NOT_FOUND);
  MachineInstr *Created =
      Function->CreateMachineInstr(TII->get(LLVMOpcode), DebugLoc());
  ResolvedBlock->insert(Before ? Before->getIterator() : ResolvedBlock->end(),
                        Created);
  auto Handle = Bridge->wrapInstruction(*Created);
  if (!Handle) {
    consumeError(Handle.takeError());
    Created->eraseFromParent();
    return mirStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutInstruction = *Handle;
  Bridge->addMutationUndo([Bridge, Created] {
    (void)Bridge->invalidateInstruction(*Created);
    Created->eraseFromParent();
  });
  Bridge->invalidateMutationProperties();
  Bridge->noteMutation();
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL MoveInstruction(
    void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
    NevercMachineInstrHandle Instruction, NevercMachineBasicBlockHandle Block,
    NevercMachineInstrHandle InsertBefore) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  NevercStatus Status = checkMutation(*Bridge, Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  MachineInstr *Resolved = nullptr;
  Status = Bridge->resolveInstruction(Instruction, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!Resolved->getParent())
    return mirStatus(NEVERC_STATUS_INVALID_STATE);
  if (Resolved->isBundled())
    return mirStatus(NEVERC_STATUS_POLICY_VIOLATION);
  MachineBasicBlock *Target = nullptr;
  Status = Bridge->resolveBasicBlock(Block, &Target);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!isAttached(*Target))
    return mirStatus(NEVERC_STATUS_INVALID_STATE);
  MachineInstr *Before = nullptr;
  Status = resolveOptionalInstruction(*Bridge, InsertBefore, &Before);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Before && Before->getParent() != Target)
    return mirStatus(NEVERC_STATUS_WRONG_SCOPE);
  if (Before == Resolved || Resolved->getNextNode() == Before ||
      (!Before && Resolved->getParent() == Target &&
       Resolved == &Target->back()))
    return neverc_status_ok();

  MachineBasicBlock *OldBlock = Resolved->getParent();
  MachineInstr *OldNext = Resolved->getNextNode();
  Target->splice(Before ? Before->getIterator() : Target->end(), OldBlock,
                 Resolved->getIterator());
  Bridge->addMutationUndo([Resolved, OldBlock, OldNext] {
    MachineBasicBlock *Current = Resolved->getParent();
    OldBlock->splice(OldNext ? OldNext->getIterator() : OldBlock->end(),
                     Current, Resolved->getIterator());
  });
  Bridge->invalidateMutationProperties();
  Bridge->noteMutation();
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL EraseInstruction(
    void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
    NevercMachineInstrHandle Instruction) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  NevercStatus Status = checkMutation(*Bridge, Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  MachineInstr *Resolved = nullptr;
  Status = Bridge->resolveInstruction(Instruction, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!Resolved->getParent())
    return mirStatus(NEVERC_STATUS_INVALID_STATE);
  if (Resolved->isBundled() || Resolved->isCandidateForCallSiteEntry())
    return mirStatus(NEVERC_STATUS_POLICY_VIOLATION);

  MachineBasicBlock *OldBlock = Resolved->getParent();
  MachineFunction *Function = OldBlock->getParent();
  MachineInstr *OldNext = Resolved->getNextNode();
  Resolved->removeFromParent();
  Bridge->addMutationUndo([Resolved, OldBlock, OldNext] {
    OldBlock->insert(OldNext ? OldNext->getIterator() : OldBlock->end(),
                     Resolved);
  });
  Bridge->addMutationCommit([Bridge, Function, Resolved] {
    (void)Bridge->invalidateInstruction(*Resolved);
    Function->deleteMachineInstr(Resolved);
  });
  Bridge->invalidateMutationProperties();
  Bridge->noteMutation();
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL AppendOperand(void *Context, NevercTaskHandle Task,
                                       NevercMIRMutationHandle Mutation,
                                       NevercMachineInstrHandle Instruction,
                                       const NevercMIROperandValue *Value,
                                       NevercMachineOperandHandle *OutOperand) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  if (!Value || Value->Header.StructSize < sizeof(*Value) ||
      Value->Header.Major != NEVERC_MIR_API_MAJOR || !OutOperand)
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutOperand = {};
  NevercStatus Status = checkMutation(*Bridge, Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Value->Kind == NEVERC_MIR_OPERAND_REGISTER &&
      Value->Payload.Register.IsPhysical == NEVERC_TRUE &&
      !Bridge->targetSchemaEnabled())
    return mirStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
  MachineInstr *Resolved = nullptr;
  Status = Bridge->resolveInstruction(Instruction, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!Resolved->getParent())
    return mirStatus(NEVERC_STATUS_INVALID_STATE);
  MachineFunction *Function = Resolved->getMF();
  auto Operand = createMIROperand(*Bridge, *Function, *Value);
  if (!Operand) {
    consumeError(Operand.takeError());
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  }
  unsigned Index = Resolved->getNumOperands();
  Resolved->addOperand(*Function, *Operand);
  auto Handle = Bridge->wrapOperand(Resolved->getOperand(Index));
  if (!Handle) {
    consumeError(Handle.takeError());
    Resolved->removeOperand(Index);
    return mirStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutOperand = *Handle;
  Bridge->addMutationUndo([Bridge, Resolved, Index] {
    (void)Bridge->invalidateOperand(*Resolved, Index);
    if (Index < Resolved->getNumOperands())
      Resolved->removeOperand(Index);
  });
  Bridge->invalidateMutationProperties();
  Bridge->noteMutation();
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL AddCFGEdge(void *Context, NevercTaskHandle Task,
                                    NevercMIRMutationHandle Mutation,
                                    NevercMachineBasicBlockHandle From,
                                    NevercMachineBasicBlockHandle To,
                                    uint32_t ProbabilityNumerator,
                                    uint32_t ProbabilityDenominator) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  NevercStatus Status = checkMutation(*Bridge, Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (ProbabilityDenominator == 0 ||
      ProbabilityNumerator > ProbabilityDenominator)
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  MachineBasicBlock *ResolvedFrom = nullptr;
  MachineBasicBlock *ResolvedTo = nullptr;
  Status = Bridge->resolveBasicBlock(From, &ResolvedFrom);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = Bridge->resolveBasicBlock(To, &ResolvedTo);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!isAttached(*ResolvedFrom) || !isAttached(*ResolvedTo))
    return mirStatus(NEVERC_STATUS_INVALID_STATE);
  if (ResolvedFrom->isSuccessor(ResolvedTo))
    return mirStatus(NEVERC_STATUS_DUPLICATE_ID);

  ResolvedFrom->addSuccessor(
      ResolvedTo,
      BranchProbability(ProbabilityNumerator, ProbabilityDenominator));
  Bridge->addMutationUndo([ResolvedFrom, ResolvedTo] {
    if (ResolvedFrom->isSuccessor(ResolvedTo))
      ResolvedFrom->removeSuccessor(ResolvedTo);
  });
  Bridge->invalidateMutationProperties();
  Bridge->noteMutation();
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL RemoveCFGEdge(void *Context, NevercTaskHandle Task,
                                       NevercMIRMutationHandle Mutation,
                                       NevercMachineBasicBlockHandle From,
                                       NevercMachineBasicBlockHandle To) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  NevercStatus Status = checkMutation(*Bridge, Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  MachineBasicBlock *ResolvedFrom = nullptr;
  MachineBasicBlock *ResolvedTo = nullptr;
  Status = Bridge->resolveBasicBlock(From, &ResolvedFrom);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = Bridge->resolveBasicBlock(To, &ResolvedTo);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!isAttached(*ResolvedFrom) || !isAttached(*ResolvedTo))
    return mirStatus(NEVERC_STATUS_INVALID_STATE);
  auto Found = std::find(ResolvedFrom->succ_begin(), ResolvedFrom->succ_end(),
                         ResolvedTo);
  if (Found == ResolvedFrom->succ_end())
    return mirStatus(NEVERC_STATUS_NOT_FOUND);

  bool HadProbabilities = ResolvedFrom->hasSuccessorProbabilities();
  std::vector<MachineBasicBlock *> Successors(ResolvedFrom->succ_begin(),
                                              ResolvedFrom->succ_end());
  std::vector<BranchProbability> Probabilities;
  if (HadProbabilities) {
    Probabilities.reserve(Successors.size());
    for (auto It = ResolvedFrom->succ_begin(); It != ResolvedFrom->succ_end();
         ++It)
      Probabilities.push_back(ResolvedFrom->getSuccProbability(It));
  }
  ResolvedFrom->removeSuccessor(Found);
  Bridge->addMutationUndo([ResolvedFrom, Successors = std::move(Successors),
                           Probabilities = std::move(Probabilities),
                           HadProbabilities] {
    while (!ResolvedFrom->succ_empty())
      ResolvedFrom->removeSuccessor(ResolvedFrom->succ_begin());
    for (size_t I = 0; I != Successors.size(); ++I) {
      if (HadProbabilities)
        ResolvedFrom->addSuccessor(Successors[I], Probabilities[I]);
      else
        ResolvedFrom->addSuccessorWithoutProb(Successors[I]);
    }
  });
  Bridge->invalidateMutationProperties();
  Bridge->noteMutation();
  return neverc_status_ok();
}

#undef NEVERC_MIR_BRIDGE_OR_RETURN

} // namespace

void initializeMIRBuilderAPI(NevercMIRAPI &API) {
  API.CommitMutation = CommitMutation;
  API.AbortMutation = AbortMutation;
  API.SetInstructionFlags = SetInstructionFlags;
  API.CreateBasicBlock = CreateBasicBlock;
  API.MoveBasicBlock = MoveBasicBlock;
  API.EraseBasicBlock = EraseBasicBlock;
  API.CreateInstruction = CreateInstruction;
  API.MoveInstruction = MoveInstruction;
  API.EraseInstruction = EraseInstruction;
  API.AppendOperand = AppendOperand;
  API.AddCFGEdge = AddCFGEdge;
  API.RemoveCFGEdge = RemoveCFGEdge;
}

} // namespace neverc::plugin
