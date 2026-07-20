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

Expected<NevercMCMutationHandle> MCPluginBridge::beginMutation() {
  if (!MutationAllowed)
    return createStringError(inconvertibleErrorCode(),
                             "MC bridge is read-only");
  if (!neverc_handle_is_null(MutationHandle))
    return createStringError(inconvertibleErrorCode(),
                             "MC mutation is already active");
  UndoActions.clear();
  Detached.clear();
  Removed.clear();
  Created.clear();
  RemovedSections.clear();
  RemovedSymbols.clear();
  RemovedExpressions.clear();
  RemovedFragments.clear();
  RemovedFixups.clear();
  CreatedSections.clear();
  CreatedSymbols.clear();
  CreatedExpressions.clear();
  CreatedFragments.clear();
  CreatedFixups.clear();
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
  if (Valid)
    if (Error Err = verifyPluginMCUnit(Unit, Schema)) {
      consumeError(std::move(Err));
      Valid = false;
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
  RemovedSections.clear();
  RemovedSymbols.clear();
  RemovedExpressions.clear();
  RemovedFragments.clear();
  RemovedFixups.clear();
  Created.clear();
  CreatedSections.clear();
  CreatedSymbols.clear();
  CreatedExpressions.clear();
  CreatedFragments.clear();
  CreatedFixups.clear();
  UndoActions.clear();
  Status =
      Task.handles().release(Mutation, PluginMCMutationHandleKind);
  if (Status.Code == NEVERC_STATUS_OK) {
    MutationHandle = {};
    advanceUnitGeneration();
  }
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
  RemovedSections.clear();
  RemovedSymbols.clear();
  RemovedExpressions.clear();
  RemovedFragments.clear();
  RemovedFixups.clear();
  for (MCInst *Instruction : Created)
    invalidateInstruction(Instruction);
  for (PluginMCSection *Section : CreatedSections)
    invalidateSection(Section);
  for (PluginMCSymbol *Symbol : CreatedSymbols)
    invalidateSymbol(Symbol);
  for (PluginMCExpression *Expression : CreatedExpressions)
    invalidateExpression(Expression);
  for (PluginMCFragment *Fragment : CreatedFragments)
    invalidateFragment(Fragment);
  for (PluginMCFixup *Fixup : CreatedFixups)
    invalidateFixup(Fixup);
  Created.clear();
  CreatedSections.clear();
  CreatedSymbols.clear();
  CreatedExpressions.clear();
  CreatedFragments.clear();
  CreatedFixups.clear();
  Detached.clear();
}

} // namespace neverc::plugin
