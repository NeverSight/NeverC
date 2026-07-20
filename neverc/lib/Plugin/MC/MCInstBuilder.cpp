#include "neverc/Plugin/Host/MCPluginBridge.h"
#include "llvm/ADT/STLExtras.h"

using namespace llvm;

namespace neverc::plugin {
namespace {

NevercStatus mcStatus(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

} // namespace

NevercStatus MCPluginBridge::appendInstructionToFragment(
    NevercMCMutationHandle Mutation, NevercMCFragmentHandle Fragment,
    NevercMCInstHandle Instruction) {
  NevercStatus Status = checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  PluginMCFragment *FragmentValue = nullptr;
  MCInst *InstructionValue = nullptr;
  Status = resolveFragment(Fragment, &FragmentValue);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = resolveInstruction(Instruction, &InstructionValue);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto It = llvm::find_if(
      Detached, [InstructionValue](const auto &Value) {
        return Value.get() == InstructionValue;
      });
  if (It == Detached.end())
    return mcStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  FragmentValue->Instructions.splice(FragmentValue->Instructions.end(),
                                     Detached, It);
  UndoActions.push_back([this, FragmentValue, InstructionValue] {
    auto Current = llvm::find_if(
        FragmentValue->Instructions,
        [InstructionValue](const auto &Value) {
          return Value.get() == InstructionValue;
        });
    if (Current != FragmentValue->Instructions.end())
      Detached.splice(Detached.end(), FragmentValue->Instructions,
                      Current);
  });
  return neverc_status_ok();
}

NevercStatus MCPluginBridge::insertOperand(
    NevercMCMutationHandle Mutation, NevercMCInstHandle Instruction,
    uint64_t Index, const NevercMCOperandValue &Value,
    NevercMCOperandHandle *OutOperand) {
  NevercStatus Status = checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  MCInst *Resolved = nullptr;
  Status = resolveInstruction(Instruction, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Index > Resolved->getNumOperands())
    return mcStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  unsigned PreviousCount = Resolved->getNumOperands();
  Status = appendOperand(Mutation, Instruction, Value);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  MCOperand Inserted = Resolved->getOperand(PreviousCount);
  Resolved->erase(Resolved->begin() + PreviousCount);
  Resolved->insert(Resolved->begin() + Index, Inserted);
  UndoActions.pop_back();
  UndoActions.push_back([Resolved, Index] {
    if (Index < Resolved->getNumOperands())
      Resolved->erase(Resolved->begin() + Index);
  });
  finishBorrowedHandles();
  if (OutOperand) {
    auto Handle = wrapOperand(*Resolved, Index);
    if (!Handle) {
      consumeError(Handle.takeError());
      return mcStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
    }
    *OutOperand = *Handle;
  }
  return neverc_status_ok();
}

NevercStatus MCPluginBridge::eraseOperand(
    NevercMCMutationHandle Mutation, NevercMCInstHandle Instruction,
    uint64_t Index) {
  NevercStatus Status = checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  MCInst *Resolved = nullptr;
  Status = resolveInstruction(Instruction, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Index >= Resolved->getNumOperands())
    return mcStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  MCOperand RemovedOperand = Resolved->getOperand(Index);
  finishBorrowedHandles();
  Resolved->erase(Resolved->begin() + Index);
  UndoActions.push_back([Resolved, Index, RemovedOperand] {
    Resolved->insert(Resolved->begin() + Index, RemovedOperand);
  });
  return neverc_status_ok();
}

} // namespace neverc::plugin
