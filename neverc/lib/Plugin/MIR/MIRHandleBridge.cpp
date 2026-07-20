#include "MIRBridgeInternal.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineOperand.h"

namespace neverc::plugin {
namespace {

bool sameHandle(NevercHandle Left, NevercHandle Right) {
  return Left.Owner == Right.Owner && Left.Value == Right.Value;
}

} // namespace

MIRPluginBridge::MIRPluginBridge(PluginTaskContext &Task,
                                 llvm::MachineFunction &Function,
                                 uint64_t FunctionGeneration,
                                 llvm::StringRef ActiveTargetSchemaDigest,
                                 llvm::StringRef RequiredTargetSchemaDigest,
                                 llvm::StringRef PluginID)
    : Task(Task), Function(Function),
      FunctionGeneration(FunctionGeneration ? FunctionGeneration : 1),
      MutationGeneration(this->FunctionGeneration),
      ActiveTargetSchemaDigest(ActiveTargetSchemaDigest.str()),
      RequiredTargetSchemaDigest(RequiredTargetSchemaDigest.str()),
      PluginID(PluginID.empty() ? "neverc.host.mir" : PluginID.str()) {
  initializeMIRSchemaAPI(API, this);
  initializeMIRCoreAPI(API);
  initializeMIRBuilderAPI(API);
  initializeMIRRegisterAPI(API);
  initializeMIRFrameAPI(API);
  initializeMIRConstantPoolAPI(API);
  initializeMIRMemoryAPI(API);
  initializeMIRPropertyAPI(API);
}

MIRPluginBridge::~MIRPluginBridge() {
  if (MutationHandle.Owner != 0 || MutationHandle.Value != 0)
    (void)abandonMutation(MutationHandle);
  for (const auto &Entry : ReferenceHandles)
    (void)Task.handles().release(Entry.second.Handle,
                                 PluginMIRReferenceHandleKind);
  for (const auto &Entry : MemoryOperandHandles)
    (void)Task.handles().release(Entry.second,
                                 PluginMIRMemoryOperandHandleKind);
  for (const auto &Entry : OperandHandles)
    (void)Task.handles().release(Entry.second.Handle,
                                 PluginMIROperandHandleKind);
  for (const auto &Entry : InstructionHandles)
    (void)Task.handles().release(Entry.second, PluginMIRInstructionHandleKind);
  for (const auto &Entry : BlockHandles)
    (void)Task.handles().release(Entry.second, PluginMIRBasicBlockHandleKind);
  if (FunctionHandle.Owner != 0 || FunctionHandle.Value != 0)
    (void)Task.handles().release(FunctionHandle, PluginMIRFunctionHandleKind);
}

NevercTaskHandle MIRPluginBridge::taskHandle() const { return Task.handle(); }

llvm::Expected<NevercMIRReferenceHandle>
MIRPluginBridge::wrapReference(const void *Reference,
                               NevercMIROperandKind Kind) {
  if (!Reference)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "cannot wrap a null MIR reference");
  auto Key = std::make_pair(Reference, Kind);
  auto Existing = ReferenceHandles.find(Key);
  if (Existing != ReferenceHandles.end())
    return Existing->second.Handle;
  auto Record = std::make_unique<MIRReferenceRecord>();
  Record->Reference = Reference;
  Record->Kind = Kind;
  auto Created =
      Task.handles().create(PluginMIRReferenceHandleKind, Record.get());
  if (!Created)
    return Created.takeError();
  ReferenceHandleEntry Entry;
  Entry.Handle = *Created;
  Entry.Record = std::move(Record);
  ReferenceHandles.insert({Key, std::move(Entry)});
  return *Created;
}

NevercStatus
MIRPluginBridge::resolveReference(NevercMIRReferenceHandle Handle,
                                  NevercMIROperandKind ExpectedKind,
                                  const void **OutReference) const {
  if (!OutReference)
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutReference = nullptr;
  void *Payload = nullptr;
  NevercStatus Status =
      Task.handles().resolve(Handle, PluginMIRReferenceHandleKind, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *Record = static_cast<MIRReferenceRecord *>(Payload);
  if (!Record || !Record->Reference || Record->Kind != ExpectedKind)
    return mirStatus(NEVERC_STATUS_WRONG_SCOPE);
  auto Existing =
      ReferenceHandles.find(std::make_pair(Record->Reference, Record->Kind));
  if (Existing == ReferenceHandles.end() ||
      Existing->second.Record.get() != Record ||
      !sameHandle(Existing->second.Handle, Handle))
    return mirStatus(NEVERC_STATUS_WRONG_SCOPE);
  *OutReference = Record->Reference;
  return Status;
}

llvm::ArrayRef<uint64_t>
MIRPluginBridge::setScratchWords(llvm::ArrayRef<uint64_t> Words) const {
  ScratchWords.assign(Words.begin(), Words.end());
  return ScratchWords;
}

const char *MIRPluginBridge::ownString(llvm::StringRef Value) {
  OwnedStrings.emplace_back(Value);
  return OwnedStrings.back().c_str();
}

llvm::ArrayRef<uint32_t>
MIRPluginBridge::ownRegisterMask(llvm::ArrayRef<uint32_t> Words) {
  OwnedRegisterMasks.emplace_back(Words.begin(), Words.end());
  return OwnedRegisterMasks.back();
}

llvm::ArrayRef<int>
MIRPluginBridge::ownShuffleMask(llvm::ArrayRef<int> Values) {
  OwnedShuffleMasks.emplace_back(Values.begin(), Values.end());
  return OwnedShuffleMasks.back();
}

template <typename ValueT>
llvm::Expected<NevercHandle>
MIRPluginBridge::wrap(ValueT &Value, uint16_t Kind,
                      llvm::DenseMap<ValueT *, NevercHandle> &Handles) {
  auto Existing = Handles.find(&Value);
  if (Existing != Handles.end())
    return Existing->second;
  auto Created = Task.handles().create(Kind, &Value);
  if (!Created)
    return Created.takeError();
  Handles.insert({&Value, *Created});
  return *Created;
}

template <typename ValueT>
NevercStatus
MIRPluginBridge::resolve(NevercHandle Handle, uint16_t Kind,
                         const llvm::DenseMap<ValueT *, NevercHandle> &Handles,
                         ValueT **OutValue) const {
  if (!OutValue)
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutValue = nullptr;
  void *Payload = nullptr;
  NevercStatus Status = Task.handles().resolve(Handle, Kind, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *Value = static_cast<ValueT *>(Payload);
  auto Existing = Handles.find(Value);
  if (Existing == Handles.end() || !sameHandle(Existing->second, Handle))
    return mirStatus(NEVERC_STATUS_WRONG_SCOPE);
  *OutValue = Value;
  return Status;
}

llvm::Expected<NevercMachineFunctionHandle> MIRPluginBridge::machineFunction() {
  if (FunctionHandle.Owner != 0 || FunctionHandle.Value != 0)
    return FunctionHandle;
  auto Created = Task.handles().create(PluginMIRFunctionHandleKind, &Function);
  if (!Created)
    return Created.takeError();
  FunctionHandle = *Created;
  return FunctionHandle;
}

llvm::Expected<NevercMachineBasicBlockHandle>
MIRPluginBridge::wrapBasicBlock(llvm::MachineBasicBlock &Block) {
  if (Block.getParent() != &Function)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "machine basic block belongs to another "
                                   "machine function generation");
  return wrap(Block, PluginMIRBasicBlockHandleKind, BlockHandles);
}

llvm::Expected<NevercMachineInstrHandle>
MIRPluginBridge::wrapInstruction(llvm::MachineInstr &Instruction) {
  if (Instruction.getMF() != &Function)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "machine instruction belongs to another "
                                   "machine function generation");
  return wrap(Instruction, PluginMIRInstructionHandleKind, InstructionHandles);
}

llvm::Expected<NevercMachineOperandHandle>
MIRPluginBridge::wrapOperand(llvm::MachineOperand &Operand) {
  llvm::MachineInstr *Instruction = Operand.getParent();
  if (!Instruction || Instruction->getMF() != &Function)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "machine operand belongs to another "
                                   "machine function generation");
  auto Key = std::make_pair(Instruction, Operand.getOperandNo());
  auto Existing = OperandHandles.find(Key);
  if (Existing != OperandHandles.end())
    return Existing->second.Handle;

  auto Reference = std::make_unique<MIROperandReference>();
  Reference->Instruction = Instruction;
  Reference->Index = Operand.getOperandNo();
  auto Created =
      Task.handles().create(PluginMIROperandHandleKind, Reference.get());
  if (!Created)
    return Created.takeError();
  OperandHandleEntry Entry;
  Entry.Handle = *Created;
  Entry.Reference = std::move(Reference);
  OperandHandles.insert({Key, std::move(Entry)});
  return *Created;
}

llvm::Expected<NevercMachineMemOperandHandle>
MIRPluginBridge::wrapMemoryOperand(llvm::MachineMemOperand &Operand) {
  return wrap(Operand, PluginMIRMemoryOperandHandleKind, MemoryOperandHandles);
}

NevercStatus MIRPluginBridge::resolveMachineFunction(
    NevercMachineFunctionHandle Handle,
    llvm::MachineFunction **OutFunction) const {
  if (!OutFunction)
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutFunction = nullptr;
  void *Payload = nullptr;
  NevercStatus Status =
      Task.handles().resolve(Handle, PluginMIRFunctionHandleKind, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Payload != &Function || !sameHandle(Handle, FunctionHandle))
    return mirStatus(NEVERC_STATUS_WRONG_SCOPE);
  *OutFunction = &Function;
  return Status;
}

NevercStatus
MIRPluginBridge::resolveBasicBlock(NevercMachineBasicBlockHandle Handle,
                                   llvm::MachineBasicBlock **OutBlock) const {
  return resolve(Handle, PluginMIRBasicBlockHandleKind, BlockHandles, OutBlock);
}

NevercStatus
MIRPluginBridge::resolveInstruction(NevercMachineInstrHandle Handle,
                                    llvm::MachineInstr **OutInstruction) const {
  return resolve(Handle, PluginMIRInstructionHandleKind, InstructionHandles,
                 OutInstruction);
}

NevercStatus
MIRPluginBridge::resolveOperand(NevercMachineOperandHandle Handle,
                                llvm::MachineOperand **OutOperand) const {
  if (!OutOperand)
    return mirStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutOperand = nullptr;
  void *Payload = nullptr;
  NevercStatus Status =
      Task.handles().resolve(Handle, PluginMIROperandHandleKind, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *Reference = static_cast<MIROperandReference *>(Payload);
  if (!Reference || !Reference->Instruction)
    return mirStatus(NEVERC_STATUS_WRONG_SCOPE);
  auto Key = std::make_pair(Reference->Instruction, Reference->Index);
  auto Existing = OperandHandles.find(Key);
  if (Existing == OperandHandles.end() ||
      Existing->second.Reference.get() != Reference ||
      !sameHandle(Existing->second.Handle, Handle))
    return mirStatus(NEVERC_STATUS_WRONG_SCOPE);
  llvm::MachineInstr *Instruction = Reference->Instruction;
  if (Instruction->getMF() != &Function ||
      Reference->Index >= Instruction->getNumOperands())
    return mirStatus(NEVERC_STATUS_INVALID_STATE);
  *OutOperand = &Instruction->getOperand(Reference->Index);
  return neverc_status_ok();
}

NevercStatus MIRPluginBridge::resolveMemoryOperand(
    NevercMachineMemOperandHandle Handle,
    llvm::MachineMemOperand **OutOperand) const {
  return resolve(Handle, PluginMIRMemoryOperandHandleKind, MemoryOperandHandles,
                 OutOperand);
}

NevercStatus MIRPluginBridge::invalidateOperand(llvm::MachineInstr &Instruction,
                                                unsigned Index) {
  auto Existing = OperandHandles.find(std::make_pair(&Instruction, Index));
  if (Existing == OperandHandles.end())
    return neverc_status_ok();
  NevercStatus Status = Task.handles().release(Existing->second.Handle,
                                               PluginMIROperandHandleKind);
  if (Status.Code == NEVERC_STATUS_OK)
    OperandHandles.erase(Existing);
  return Status;
}

NevercStatus
MIRPluginBridge::invalidateMemoryOperand(llvm::MachineMemOperand &Operand) {
  auto Existing = MemoryOperandHandles.find(&Operand);
  if (Existing == MemoryOperandHandles.end())
    return neverc_status_ok();
  NevercStatus Status = Task.handles().release(
      Existing->second, PluginMIRMemoryOperandHandleKind);
  if (Status.Code == NEVERC_STATUS_OK)
    MemoryOperandHandles.erase(Existing);
  return Status;
}

NevercStatus
MIRPluginBridge::invalidateInstruction(llvm::MachineInstr &Instruction) {
  if (Instruction.getParent() && Instruction.getMF() != &Function)
    return mirStatus(NEVERC_STATUS_WRONG_SCOPE);
  for (auto It = OperandHandles.begin(); It != OperandHandles.end();) {
    if (It->first.first != &Instruction) {
      ++It;
      continue;
    }
    auto Current = It++;
    NevercStatus Status = Task.handles().release(Current->second.Handle,
                                                 PluginMIROperandHandleKind);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    OperandHandles.erase(Current);
  }
  auto Existing = InstructionHandles.find(&Instruction);
  if (Existing == InstructionHandles.end())
    return neverc_status_ok();
  NevercStatus Status =
      Task.handles().release(Existing->second, PluginMIRInstructionHandleKind);
  if (Status.Code == NEVERC_STATUS_OK)
    InstructionHandles.erase(Existing);
  return Status;
}

NevercStatus
MIRPluginBridge::invalidateBasicBlock(llvm::MachineBasicBlock &Block) {
  if (Block.getParent() != &Function)
    return mirStatus(NEVERC_STATUS_WRONG_SCOPE);
  for (llvm::MachineInstr &Instruction : Block) {
    NevercStatus Status = invalidateInstruction(Instruction);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
  }
  auto Existing = BlockHandles.find(&Block);
  if (Existing == BlockHandles.end())
    return neverc_status_ok();
  NevercStatus Status =
      Task.handles().release(Existing->second, PluginMIRBasicBlockHandleKind);
  if (Status.Code == NEVERC_STATUS_OK)
    BlockHandles.erase(Existing);
  return Status;
}

} // namespace neverc::plugin
