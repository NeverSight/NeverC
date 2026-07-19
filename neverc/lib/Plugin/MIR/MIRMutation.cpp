#include "MIRBridgeInternal.h"
#include "neverc/Plugin/Host/PluginDiagnostics.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/Support/raw_ostream.h"
#include <limits>
#include <utility>

namespace neverc::plugin {
namespace {

bool sameHandle(NevercHandle Left, NevercHandle Right) {
  return Left.Owner == Right.Owner && Left.Value == Right.Value;
}

} // namespace

llvm::Expected<NevercMIRMutationHandle> MIRPluginBridge::beginMutation() {
  if (MutationHandle.Owner != 0 || MutationHandle.Value != 0)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "MIR mutation is already active");
  MutationUndoActions.clear();
  MutationCommitActions.clear();
  MutationChanged = false;
  auto Created = Task.handles().create(PluginMIRMutationHandleKind, this);
  if (!Created)
    return Created.takeError();
  MutationHandle = *Created;
  return MutationHandle;
}

NevercStatus
MIRPluginBridge::checkMutation(NevercMIRMutationHandle Mutation) const {
  void *Payload = nullptr;
  NevercStatus Status =
      Task.handles().resolve(Mutation, PluginMIRMutationHandleKind, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Payload != this || !sameHandle(Mutation, MutationHandle))
    return mirStatus(NEVERC_STATUS_WRONG_SCOPE);
  return Status;
}

NevercStatus MIRPluginBridge::commitMutation(NevercMIRMutationHandle Mutation) {
  return finishMutation(Mutation, true);
}

NevercStatus MIRPluginBridge::finishMutation(NevercMIRMutationHandle Mutation,
                                             bool Verify) {
  NevercStatus Status = checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  std::string VerificationError;
  bool Verified =
      !Verify ||
      (verifyMIRStructure(Function, VerificationError) &&
       Function.verify(nullptr, "NeverC plugin MIR mutation", false));
  if (!Verified) {
    std::string Message =
        ("MIR verification failed for function '" + Function.getName() + "'")
            .str();
    llvm::raw_string_ostream Stream(Message);
    if (!VerificationError.empty())
      Stream << ": " << VerificationError;
    Stream << "\n";
    Function.print(Stream);
    Stream.flush();
    Task.session().diagnostics().emitImplicit(Task.session(), &Task, PluginID,
                                              "mir.commit", Message, 0);
    rollbackMutation();
    (void)Task.handles().release(Mutation, PluginMIRMutationHandleKind);
    MutationHandle = {};
    return mirStatus(NEVERC_STATUS_VERIFICATION_FAILED);
  }
  for (std::function<void()> &Action : MutationCommitActions)
    Action();
  MutationCommitActions.clear();
  MutationUndoActions.clear();
  if (MutationChanged)
    advanceGeneration();
  MutationChanged = false;
  Status = Task.handles().release(Mutation, PluginMIRMutationHandleKind);
  if (Status.Code == NEVERC_STATUS_OK)
    MutationHandle = {};
  return Status;
}

NevercStatus
MIRPluginBridge::abandonMutation(NevercMIRMutationHandle Mutation) {
  NevercStatus Status = checkMutation(Mutation);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  rollbackMutation();
  Status = Task.handles().release(Mutation, PluginMIRMutationHandleKind);
  if (Status.Code == NEVERC_STATUS_OK)
    MutationHandle = {};
  return Status;
}

NevercStatus MIRPluginBridge::endMutation(NevercMIRMutationHandle Mutation) {
  return finishMutation(Mutation, true);
}

void MIRPluginBridge::rollbackMutation() {
  for (auto It = MutationUndoActions.rbegin(); It != MutationUndoActions.rend();
       ++It)
    (*It)();
  MutationUndoActions.clear();
  MutationCommitActions.clear();
  MutationChanged = false;
}

void MIRPluginBridge::addMutationUndo(std::function<void()> Action) {
  MutationUndoActions.push_back(std::move(Action));
}

void MIRPluginBridge::addMutationCommit(std::function<void()> Action) {
  MutationCommitActions.push_back(std::move(Action));
}

void MIRPluginBridge::invalidateMutationProperties() {
  llvm::MachineFunctionProperties Previous = Function.getProperties();
  addMutationUndo([this, Previous] { Function.getProperties() = Previous; });
  using Property = llvm::MachineFunctionProperties::Property;
  llvm::MachineFunctionProperties &Properties = Function.getProperties();
  Properties.reset(Property::IsSSA)
      .reset(Property::NoPHIs)
      .reset(Property::TracksLiveness)
      .reset(Property::NoVRegs)
      .reset(Property::Legalized)
      .reset(Property::RegBankSelected)
      .reset(Property::Selected)
      .reset(Property::TiedOpsRewritten)
      .reset(Property::TracksDebugUserValues);
}

void MIRPluginBridge::noteMutation() { MutationChanged = true; }

void MIRPluginBridge::advanceGeneration() {
  if (FunctionGeneration == std::numeric_limits<uint64_t>::max())
    FunctionGeneration = 1;
  else
    ++FunctionGeneration;
  if (MutationGeneration == std::numeric_limits<uint64_t>::max())
    MutationGeneration = 1;
  else
    ++MutationGeneration;
}

} // namespace neverc::plugin
