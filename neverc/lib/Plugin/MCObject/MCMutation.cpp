#include "neverc/Plugin/Host/MCPluginBridge.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "llvm/Support/Error.h"

using namespace llvm;

namespace neverc::plugin {
namespace {

NevercStatus mcStatus(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

bool sameHandle(NevercHandle Left, NevercHandle Right) {
  return Left.Owner == Right.Owner && Left.Value == Right.Value;
}

} // namespace

MCInst &PluginMCUnit::append(std::unique_ptr<MCInst> Instruction) {
  Instructions.push_back(std::move(Instruction));
  return *Instructions.back();
}

MCInst *PluginMCUnit::at(size_t Index) {
  auto It = Instructions.begin();
  while (It != Instructions.end() && Index != 0) {
    ++It;
    --Index;
  }
  return It == Instructions.end() ? nullptr : It->get();
}

const MCInst *PluginMCUnit::at(size_t Index) const {
  auto It = Instructions.begin();
  while (It != Instructions.end() && Index != 0) {
    ++It;
    --Index;
  }
  return It == Instructions.end() ? nullptr : It->get();
}

Expected<NevercMCMutationHandle> MCPluginBridge::beginMutation() {
  if (!neverc_handle_is_null(MutationHandle))
    return createStringError(inconvertibleErrorCode(),
                             "MC mutation is already active");
  UndoActions.clear();
  Detached.clear();
  Removed.clear();
  Created.clear();
  auto Handle =
      Task.handles().create(PluginMCMutationHandleKind, this);
  if (!Handle)
    return Handle.takeError();
  MutationHandle = *Handle;
  return MutationHandle;
}

NevercStatus
MCPluginBridge::checkMutation(NevercMCMutationHandle Mutation) const {
  void *Payload = nullptr;
  NevercStatus Status = Task.handles().resolve(
      Mutation, PluginMCMutationHandleKind, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Payload != this || !sameHandle(Mutation, MutationHandle))
    return mcStatus(NEVERC_STATUS_WRONG_SCOPE);
  return neverc_status_ok();
}

NevercStatus
MCPluginBridge::commitMutation(NevercMCMutationHandle Mutation) {
  NevercStatus Status = checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  bool Valid = Detached.empty();
  if (Valid) {
    for (const auto &Instruction : Unit.Instructions) {
      auto Opcode = stableOpcode(Instruction->getOpcode());
      if (!Opcode) {
        consumeError(Opcode.takeError());
        Valid = false;
        break;
      }
      for (const MCOperand &Operand : *Instruction) {
        if (Operand.isReg()) {
          auto Register = stableRegister(Operand.getReg());
          if (!Register) {
            consumeError(Register.takeError());
            Valid = false;
            break;
          }
        }
        if (Operand.isInst() &&
            !containsInstruction(Operand.getInst())) {
          Valid = false;
          break;
        }
      }
      if (!Valid)
        break;
    }
  }
  if (!Valid) {
    rollbackMutation();
    (void)Task.handles().release(Mutation,
                                 PluginMCMutationHandleKind);
    MutationHandle = {};
    return mcStatus(NEVERC_STATUS_VERIFICATION_FAILED);
  }

  for (const auto &Instruction : Removed)
    invalidateInstruction(Instruction.get());
  Removed.clear();
  Created.clear();
  UndoActions.clear();
  Status =
      Task.handles().release(Mutation, PluginMCMutationHandleKind);
  if (Status.Code == NEVERC_STATUS_OK)
    MutationHandle = {};
  return Status;
}

NevercStatus
MCPluginBridge::abandonMutation(NevercMCMutationHandle Mutation) {
  NevercStatus Status = checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  rollbackMutation();
  Status =
      Task.handles().release(Mutation, PluginMCMutationHandleKind);
  if (Status.Code == NEVERC_STATUS_OK)
    MutationHandle = {};
  return Status;
}

void MCPluginBridge::rollbackMutation() {
  for (auto It = UndoActions.rbegin(); It != UndoActions.rend(); ++It)
    (*It)();
  UndoActions.clear();
  Removed.clear();
  for (MCInst *Instruction : Created)
    invalidateInstruction(Instruction);
  Created.clear();
  Detached.clear();
}

} // namespace neverc::plugin
