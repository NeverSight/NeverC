#include "MIRBridgeInternal.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Support/MathExtras.h"
#include <cstdint>
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

bool validBool(NevercBool Value) {
  return Value == NEVERC_FALSE || Value == NEVERC_TRUE;
}

bool validAlignment(uint64_t Alignment) {
  return Alignment != 0 && isPowerOf2_64(Alignment);
}

NevercStatus resolveFunction(MIRPluginBridge &Bridge,
                             NevercMachineFunctionHandle Handle,
                             MachineFunction **OutFunction) {
  return Bridge.resolveMachineFunction(Handle, OutFunction);
}

bool validFrameIndex(const MachineFrameInfo &Frame, int FrameIndex) {
  return FrameIndex >= Frame.getObjectIndexBegin() &&
         FrameIndex < Frame.getObjectIndexEnd();
}

NevercStatus writeFrameObjectInfo(const MachineFrameInfo &Frame, int FrameIndex,
                                  NevercMIRFrameObjectInfo *OutInfo) {
  if (!OutInfo || !validHeader(OutInfo->Header, sizeof(*OutInfo)))
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  if (!validFrameIndex(Frame, FrameIndex))
    return mirStatus(NEVERC_STATUS_NOT_FOUND);

  NevercMIRFrameObjectInfo Result{};
  Result.Header = {sizeof(Result), NEVERC_MIR_API_MAJOR, NEVERC_MIR_API_MINOR,
                   0};
  Result.Index = FrameIndex;
  if (Frame.isFixedObjectIndex(FrameIndex))
    Result.Flags |= NEVERC_MIR_FRAME_FIXED;
  if (Frame.isSpillSlotObjectIndex(FrameIndex))
    Result.Flags |= NEVERC_MIR_FRAME_SPILL_SLOT;
  if (Frame.isVariableSizedObjectIndex(FrameIndex))
    Result.Flags |= NEVERC_MIR_FRAME_VARIABLE_SIZED;
  if (Frame.isImmutableObjectIndex(FrameIndex))
    Result.Flags |= NEVERC_MIR_FRAME_IMMUTABLE;
  if (Frame.isAliasedObjectIndex(FrameIndex))
    Result.Flags |= NEVERC_MIR_FRAME_ALIASED;
  if (Frame.isDeadObjectIndex(FrameIndex))
    Result.Flags |= NEVERC_MIR_FRAME_DEAD;
  if (Frame.isObjectPreAllocated(FrameIndex))
    Result.Flags |= NEVERC_MIR_FRAME_PREALLOCATED;
  Result.Size = Frame.getObjectSize(FrameIndex);
  if (!Frame.isDeadObjectIndex(FrameIndex) &&
      !Frame.isVariableSizedObjectIndex(FrameIndex))
    Result.Offset = Frame.getObjectOffset(FrameIndex);
  Result.Alignment = Frame.getObjectAlign(FrameIndex).value();
  Result.StackID = Frame.getStackID(FrameIndex);
  *OutInfo = Result;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL getFrameObjectCount(
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
  *OutCount = Function->getFrameInfo().getNumObjects();
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL getFrameObject(
    void *Context, NevercTaskHandle Task,
    NevercMachineFunctionHandle FunctionHandle, uint64_t Ordinal,
    NevercMIRFrameObjectInfo *OutInfo) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  MachineFunction *Function = nullptr;
  NevercStatus Status =
      resolveFunction(*Bridge, FunctionHandle, &Function);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  const MachineFrameInfo &Frame = Function->getFrameInfo();
  if (Ordinal >= Frame.getNumObjects())
    return mirStatus(NEVERC_STATUS_NOT_FOUND);
  return writeFrameObjectInfo(
      Frame, Frame.getObjectIndexBegin() + static_cast<int>(Ordinal), OutInfo);
}

NevercStatus NEVERC_CALL getFrameObjectByIndex(
    void *Context, NevercTaskHandle Task,
    NevercMachineFunctionHandle FunctionHandle, int32_t FrameIndex,
    NevercMIRFrameObjectInfo *OutInfo) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  MachineFunction *Function = nullptr;
  NevercStatus Status =
      resolveFunction(*Bridge, FunctionHandle, &Function);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  return writeFrameObjectInfo(Function->getFrameInfo(), FrameIndex, OutInfo);
}

NevercStatus NEVERC_CALL createStackObject(
    void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
    uint64_t Size, uint64_t Alignment, NevercBool IsSpillSlot,
    uint32_t StackID, int32_t *OutFrameIndex) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  if (!OutFrameIndex || Size == 0 ||
      Size > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) ||
      !validAlignment(Alignment) || !validBool(IsSpillSlot) ||
      StackID > std::numeric_limits<uint8_t>::max())
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

  MachineFrameInfo &Frame = Function->getFrameInfo();
  Align PreviousMaxAlignment = Frame.getMaxAlign();
  bool PreviouslyHadVariableObjects = Frame.hasVarSizedObjects();
  int FrameIndex =
      Frame.CreateStackObject(Size, Align(Alignment),
                              IsSpillSlot == NEVERC_TRUE, nullptr, StackID);
  Bridge->addMutationUndo([&Frame, FrameIndex, PreviousMaxAlignment,
                           PreviouslyHadVariableObjects] {
    Frame.discardLastObject(FrameIndex, PreviousMaxAlignment,
                            PreviouslyHadVariableObjects);
  });
  Bridge->invalidateMutationProperties();
  Bridge->noteMutation();
  *OutFrameIndex = FrameIndex;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL createFixedStackObject(
    void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
    uint64_t Size, int64_t Offset, NevercBool IsImmutable,
    NevercBool IsAliased, int32_t *OutFrameIndex) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  if (!OutFrameIndex || Size == 0 ||
      Size > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) ||
      !validBool(IsImmutable) || !validBool(IsAliased))
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

  MachineFrameInfo &Frame = Function->getFrameInfo();
  Align PreviousMaxAlignment = Frame.getMaxAlign();
  bool PreviouslyHadVariableObjects = Frame.hasVarSizedObjects();
  int FrameIndex =
      Frame.CreateFixedObject(Size, Offset, IsImmutable == NEVERC_TRUE,
                              IsAliased == NEVERC_TRUE);
  Bridge->addMutationUndo([&Frame, FrameIndex, PreviousMaxAlignment,
                           PreviouslyHadVariableObjects] {
    Frame.discardLastObject(FrameIndex, PreviousMaxAlignment,
                            PreviouslyHadVariableObjects);
  });
  Bridge->invalidateMutationProperties();
  Bridge->noteMutation();
  *OutFrameIndex = FrameIndex;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL createVariableSizedStackObject(
    void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
    uint64_t Alignment, int32_t *OutFrameIndex) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  if (!OutFrameIndex || !validAlignment(Alignment))
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

  MachineFrameInfo &Frame = Function->getFrameInfo();
  Align PreviousMaxAlignment = Frame.getMaxAlign();
  bool PreviouslyHadVariableObjects = Frame.hasVarSizedObjects();
  int FrameIndex = Frame.CreateVariableSizedObject(Align(Alignment), nullptr);
  Bridge->addMutationUndo([&Frame, FrameIndex, PreviousMaxAlignment,
                           PreviouslyHadVariableObjects] {
    Frame.discardLastObject(FrameIndex, PreviousMaxAlignment,
                            PreviouslyHadVariableObjects);
  });
  Bridge->invalidateMutationProperties();
  Bridge->noteMutation();
  *OutFrameIndex = FrameIndex;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL setFrameObjectSize(
    void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
    int32_t FrameIndex, uint64_t Size) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  if (Size == 0 ||
      Size > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
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
  MachineFrameInfo &Frame = Function->getFrameInfo();
  if (!validFrameIndex(Frame, FrameIndex) ||
      Frame.isDeadObjectIndex(FrameIndex) ||
      Frame.isVariableSizedObjectIndex(FrameIndex))
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  int64_t PreviousSize = Frame.getObjectSize(FrameIndex);
  Frame.setObjectSize(FrameIndex, Size);
  Bridge->addMutationUndo(
      [&Frame, FrameIndex, PreviousSize] {
        Frame.setObjectSize(FrameIndex, PreviousSize);
      });
  Bridge->invalidateMutationProperties();
  Bridge->noteMutation();
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL setFrameObjectAlignment(
    void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
    int32_t FrameIndex, uint64_t Alignment) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  if (!validAlignment(Alignment))
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
  MachineFrameInfo &Frame = Function->getFrameInfo();
  if (!validFrameIndex(Frame, FrameIndex) ||
      Frame.isDeadObjectIndex(FrameIndex))
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  Align PreviousAlignment = Frame.getObjectAlign(FrameIndex);
  Align PreviousMaxAlignment = Frame.getMaxAlign();
  Frame.setObjectAlignment(FrameIndex, Align(Alignment));
  Bridge->addMutationUndo([&Frame, FrameIndex, PreviousAlignment,
                           PreviousMaxAlignment] {
    Frame.setObjectAlignment(FrameIndex, PreviousAlignment);
    Frame.restoreMaxAlignment(PreviousMaxAlignment);
  });
  Bridge->invalidateMutationProperties();
  Bridge->noteMutation();
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL setFrameObjectOffset(
    void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
    int32_t FrameIndex, int64_t Offset) {
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
  MachineFrameInfo &Frame = Function->getFrameInfo();
  if (!validFrameIndex(Frame, FrameIndex) ||
      Frame.isDeadObjectIndex(FrameIndex) ||
      Frame.isVariableSizedObjectIndex(FrameIndex))
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  int64_t PreviousOffset = Frame.getObjectOffset(FrameIndex);
  Frame.setObjectOffset(FrameIndex, Offset);
  Bridge->addMutationUndo([&Frame, FrameIndex, PreviousOffset] {
    Frame.setObjectOffset(FrameIndex, PreviousOffset);
  });
  Bridge->invalidateMutationProperties();
  Bridge->noteMutation();
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL getCalleeSavedCount(
    void *Context, NevercTaskHandle Task,
    NevercMachineFunctionHandle FunctionHandle, uint64_t *OutCount) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  if (!OutCount)
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  if (!Bridge->targetSchemaEnabled())
    return mirStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
  MachineFunction *Function = nullptr;
  NevercStatus Status =
      resolveFunction(*Bridge, FunctionHandle, &Function);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  *OutCount = Function->getFrameInfo().getCalleeSavedInfo().size();
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL getCalleeSaved(
    void *Context, NevercTaskHandle Task,
    NevercMachineFunctionHandle FunctionHandle, uint64_t Index,
    NevercMIRCalleeSavedInfo *OutInfo) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  if (!OutInfo)
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  if (!Bridge->targetSchemaEnabled())
    return mirStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
  MachineFunction *Function = nullptr;
  NevercStatus Status =
      resolveFunction(*Bridge, FunctionHandle, &Function);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  const std::vector<CalleeSavedInfo> &Entries =
      Function->getFrameInfo().getCalleeSavedInfo();
  if (Index >= Entries.size())
    return mirStatus(NEVERC_STATUS_NOT_FOUND);
  const CalleeSavedInfo &Entry = Entries[Index];
  NevercMIRCalleeSavedInfo Result{};
  Result.Register = Entry.getReg().id();
  Result.IsSpilledToRegister =
      Entry.isSpilledToReg() ? NEVERC_TRUE : NEVERC_FALSE;
  Result.IsRestored = Entry.isRestored() ? NEVERC_TRUE : NEVERC_FALSE;
  if (Entry.isSpilledToReg())
    Result.DestinationRegister = Entry.getDstReg();
  else
    Result.FrameIndex = Entry.getFrameIdx();
  *OutInfo = Result;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL setCalleeSaved(
    void *Context, NevercTaskHandle Task, NevercMIRMutationHandle Mutation,
    const NevercMIRCalleeSavedInfo *Entries, uint64_t Count) {
  NEVERC_MIR_BRIDGE_OR_RETURN();
  if ((Count != 0 && !Entries) ||
      Count > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  if (!Bridge->targetSchemaEnabled())
    return mirStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
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
  const TargetRegisterInfo *RegisterInfo =
      Function->getSubtarget().getRegisterInfo();
  if (!RegisterInfo)
    return mirStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
  MachineFrameInfo &Frame = Function->getFrameInfo();

  SmallDenseSet<unsigned, 16> SeenRegisters;
  std::vector<CalleeSavedInfo> NewEntries;
  NewEntries.reserve(static_cast<size_t>(Count));
  for (uint64_t Index = 0; Index != Count; ++Index) {
    const NevercMIRCalleeSavedInfo &Input = Entries[Index];
    Register Saved(Input.Register);
    if (!validBool(Input.IsSpilledToRegister) ||
        !validBool(Input.IsRestored) || !Saved.isPhysical() ||
        Saved.id() >= RegisterInfo->getNumRegs() ||
        !SeenRegisters.insert(Saved.id()).second)
      return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);

    CalleeSavedInfo Output(Saved.id());
    if (Input.IsSpilledToRegister == NEVERC_TRUE) {
      Register Destination(Input.DestinationRegister);
      if (!Destination.isPhysical() ||
          Destination.id() >= RegisterInfo->getNumRegs())
        return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
      Output.setDstReg(Destination);
    } else {
      if (!validFrameIndex(Frame, Input.FrameIndex) ||
          Frame.isDeadObjectIndex(Input.FrameIndex))
        return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
      Output.setFrameIdx(Input.FrameIndex);
    }
    Output.setRestored(Input.IsRestored == NEVERC_TRUE);
    NewEntries.push_back(Output);
  }

  std::vector<CalleeSavedInfo> PreviousEntries = Frame.getCalleeSavedInfo();
  bool PreviousValidity = Frame.isCalleeSavedInfoValid();
  Frame.setCalleeSavedInfo(std::move(NewEntries));
  Frame.setCalleeSavedInfoValid(true);
  Bridge->addMutationUndo(
      [&Frame, PreviousEntries = std::move(PreviousEntries),
       PreviousValidity]() mutable {
        Frame.setCalleeSavedInfo(std::move(PreviousEntries));
        Frame.setCalleeSavedInfoValid(PreviousValidity);
      });
  Bridge->invalidateMutationProperties();
  Bridge->noteMutation();
  return neverc_status_ok();
}

#undef NEVERC_MIR_BRIDGE_OR_RETURN

} // namespace

void initializeMIRFrameAPI(NevercMIRAPI &API) {
  API.GetFrameObjectCount = getFrameObjectCount;
  API.GetFrameObject = getFrameObject;
  API.GetFrameObjectByIndex = getFrameObjectByIndex;
  API.CreateStackObject = createStackObject;
  API.CreateFixedStackObject = createFixedStackObject;
  API.CreateVariableSizedStackObject = createVariableSizedStackObject;
  API.SetFrameObjectSize = setFrameObjectSize;
  API.SetFrameObjectAlignment = setFrameObjectAlignment;
  API.SetFrameObjectOffset = setFrameObjectOffset;
  API.GetCalleeSavedCount = getCalleeSavedCount;
  API.GetCalleeSaved = getCalleeSaved;
  API.SetCalleeSaved = setCalleeSaved;
}

} // namespace neverc::plugin
