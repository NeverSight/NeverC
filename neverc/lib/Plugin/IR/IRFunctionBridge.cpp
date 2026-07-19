#include "neverc/Plugin/Host/IRPluginBridge.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include <iterator>

using namespace llvm;

namespace neverc::plugin {
namespace {

NevercStatus functionStatus(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

Expected<CallingConv::ID>
decodeCallingConvention(NevercIRCallingConvention CallingConvention) {
  switch (CallingConvention) {
#define NEVERC_IR_SCHEMA_INTERNAL_CALLING_CONVENTION(Internal, Symbol, ID)    \
  case ID:                                                                    \
    return CallingConv::Internal;
#include "neverc/Plugin/Schema/PluginIRSchema.inc"
#undef NEVERC_IR_SCHEMA_INTERNAL_CALLING_CONVENTION
  default:
    return createStringError(inconvertibleErrorCode(),
                             "unknown IR calling convention");
  }
}

NevercIRCallingConvention
encodeCallingConvention(CallingConv::ID CallingConvention) {
  switch (CallingConvention) {
#define NEVERC_IR_SCHEMA_INTERNAL_CALLING_CONVENTION(Internal, Symbol, ID)    \
  case CallingConv::Internal:                                                 \
    return ID;
#include "neverc/Plugin/Schema/PluginIRSchema.inc"
#undef NEVERC_IR_SCHEMA_INTERNAL_CALLING_CONVENTION
  default:
    return NEVERC_IR_CALLING_CONVENTION_UNKNOWN;
  }
}

} // namespace

NevercStatus IRPluginBridge::getFunctionCallingConvention(
    NevercIRValueHandle Handle,
    NevercIRCallingConvention *OutCallingConvention) const {
  if (!OutCallingConvention)
    return functionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutCallingConvention = NEVERC_IR_CALLING_CONVENTION_UNKNOWN;
  Value *Resolved = nullptr;
  NevercStatus Status = resolveValue(Handle, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *FunctionValue = dyn_cast<Function>(Resolved);
  if (!FunctionValue)
    return functionStatus(NEVERC_STATUS_WRONG_TYPE);
  *OutCallingConvention =
      encodeCallingConvention(FunctionValue->getCallingConv());
  return *OutCallingConvention == NEVERC_IR_CALLING_CONVENTION_UNKNOWN
             ? functionStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE)
             : neverc_status_ok();
}

NevercStatus IRPluginBridge::setFunctionCallingConvention(
    NevercIRValueHandle Handle,
    NevercIRCallingConvention CallingConvention) {
  NevercStatus Status = checkMutationAllowed();
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Value *Resolved = nullptr;
  Status = resolveValue(Handle, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *FunctionValue = dyn_cast<Function>(Resolved);
  if (!FunctionValue)
    return functionStatus(NEVERC_STATUS_WRONG_TYPE);
  auto Internal = decodeCallingConvention(CallingConvention);
  if (!Internal) {
    consumeError(Internal.takeError());
    return functionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  }
  FunctionValue->setCallingConv(*Internal);
  noteMutation();
  return neverc_status_ok();
}

Expected<NevercIRValueHandle>
IRPluginBridge::getFunctionPersonality(NevercIRValueHandle Handle) {
  Value *Resolved = nullptr;
  NevercStatus Status = resolveValue(Handle, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return createStringError(inconvertibleErrorCode(),
                             "invalid IR function handle");
  auto *FunctionValue = dyn_cast<Function>(Resolved);
  if (!FunctionValue)
    return createStringError(inconvertibleErrorCode(),
                             "IR value is not a function");
  if (!FunctionValue->hasPersonalityFn())
    return NevercIRValueHandle{};
  return wrapValue(*FunctionValue->getPersonalityFn());
}

NevercStatus IRPluginBridge::setFunctionPersonality(
    NevercIRValueHandle Handle, NevercIRValueHandle PersonalityHandle) {
  NevercStatus Status = checkMutationAllowed();
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Value *Resolved = nullptr;
  Status = resolveValue(Handle, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *FunctionValue = dyn_cast<Function>(Resolved);
  if (!FunctionValue)
    return functionStatus(NEVERC_STATUS_WRONG_TYPE);
  if (neverc_handle_is_null(PersonalityHandle) == NEVERC_TRUE) {
    FunctionValue->setPersonalityFn(nullptr);
  } else {
    Value *Personality = nullptr;
    Status = resolveValue(PersonalityHandle, &Personality);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    auto *PersonalityConstant = dyn_cast<Constant>(Personality);
    if (!PersonalityConstant)
      return functionStatus(NEVERC_STATUS_WRONG_TYPE);
    FunctionValue->setPersonalityFn(PersonalityConstant);
  }
  noteMutation();
  return neverc_status_ok();
}

Expected<StringRef>
IRPluginBridge::getFunctionGC(NevercIRValueHandle Handle) const {
  Value *Resolved = nullptr;
  NevercStatus Status = resolveValue(Handle, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return createStringError(inconvertibleErrorCode(),
                             "invalid IR function handle");
  auto *FunctionValue = dyn_cast<Function>(Resolved);
  if (!FunctionValue)
    return createStringError(inconvertibleErrorCode(),
                             "IR value is not a function");
  return FunctionValue->hasGC() ? StringRef(FunctionValue->getGC())
                                : StringRef();
}

NevercStatus IRPluginBridge::setFunctionGC(NevercIRValueHandle Handle,
                                            StringRef GC) {
  NevercStatus Status = checkMutationAllowed();
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Value *Resolved = nullptr;
  Status = resolveValue(Handle, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *FunctionValue = dyn_cast<Function>(Resolved);
  if (!FunctionValue)
    return functionStatus(NEVERC_STATUS_WRONG_TYPE);
  if (GC.empty())
    FunctionValue->clearGC();
  else
    FunctionValue->setGC(GC.str());
  noteMutation();
  return neverc_status_ok();
}

Expected<StringRef>
IRPluginBridge::getFunctionSection(NevercIRValueHandle Handle) const {
  Value *Resolved = nullptr;
  NevercStatus Status = resolveValue(Handle, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return createStringError(inconvertibleErrorCode(),
                             "invalid IR function handle");
  auto *FunctionValue = dyn_cast<Function>(Resolved);
  if (!FunctionValue)
    return createStringError(inconvertibleErrorCode(),
                             "IR value is not a function");
  return FunctionValue->getSection();
}

NevercStatus IRPluginBridge::setFunctionSection(
    NevercIRValueHandle Handle, StringRef Section) {
  NevercStatus Status = checkMutationAllowed();
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Value *Resolved = nullptr;
  Status = resolveValue(Handle, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *FunctionValue = dyn_cast<Function>(Resolved);
  if (!FunctionValue)
    return functionStatus(NEVERC_STATUS_WRONG_TYPE);
  FunctionValue->setSection(Section);
  noteMutation();
  return neverc_status_ok();
}

Expected<NevercIRValueHandle>
IRPluginBridge::getTerminator(NevercIRValueHandle Handle) {
  Value *Resolved = nullptr;
  NevercStatus Status = resolveValue(Handle, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return createStringError(inconvertibleErrorCode(),
                             "invalid IR block handle");
  auto *Block = dyn_cast<BasicBlock>(Resolved);
  if (!Block)
    return createStringError(inconvertibleErrorCode(),
                             "IR value is not a basic block");
  Instruction *Terminator = Block->getTerminator();
  if (!Terminator)
    return NevercIRValueHandle{};
  return wrapValue(*Terminator);
}

NevercStatus IRPluginBridge::getPredecessorCount(
    NevercIRValueHandle Handle, uint64_t *OutCount) const {
  if (!OutCount)
    return functionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutCount = 0;
  Value *Resolved = nullptr;
  NevercStatus Status = resolveValue(Handle, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *Block = dyn_cast<BasicBlock>(Resolved);
  if (!Block)
    return functionStatus(NEVERC_STATUS_WRONG_TYPE);
  *OutCount = static_cast<uint64_t>(pred_size(Block));
  return neverc_status_ok();
}

Expected<NevercIRValueHandle>
IRPluginBridge::getPredecessor(NevercIRValueHandle Handle, uint64_t Index) {
  Value *Resolved = nullptr;
  NevercStatus Status = resolveValue(Handle, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return createStringError(inconvertibleErrorCode(),
                             "invalid IR block handle");
  auto *Block = dyn_cast<BasicBlock>(Resolved);
  if (!Block)
    return createStringError(inconvertibleErrorCode(),
                             "IR value is not a basic block");
  if (Index >= pred_size(Block))
    return createStringError(inconvertibleErrorCode(),
                             "predecessor index is out of range");
  auto Iterator = pred_begin(Block);
  std::advance(Iterator, static_cast<ptrdiff_t>(Index));
  return wrapValue(**Iterator);
}

NevercStatus IRPluginBridge::getSuccessorCount(
    NevercIRValueHandle Handle, uint64_t *OutCount) const {
  if (!OutCount)
    return functionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutCount = 0;
  Value *Resolved = nullptr;
  NevercStatus Status = resolveValue(Handle, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *Block = dyn_cast<BasicBlock>(Resolved);
  if (!Block)
    return functionStatus(NEVERC_STATUS_WRONG_TYPE);
  if (Instruction *Terminator = Block->getTerminator())
    *OutCount = Terminator->getNumSuccessors();
  return neverc_status_ok();
}

Expected<NevercIRValueHandle>
IRPluginBridge::getSuccessor(NevercIRValueHandle Handle, uint64_t Index) {
  Value *Resolved = nullptr;
  NevercStatus Status = resolveValue(Handle, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return createStringError(inconvertibleErrorCode(),
                             "invalid IR block handle");
  auto *Block = dyn_cast<BasicBlock>(Resolved);
  if (!Block)
    return createStringError(inconvertibleErrorCode(),
                             "IR value is not a basic block");
  Instruction *Terminator = Block->getTerminator();
  if (!Terminator || Index >= Terminator->getNumSuccessors())
    return createStringError(inconvertibleErrorCode(),
                             "successor index is out of range");
  return wrapValue(*Terminator->getSuccessor(static_cast<unsigned>(Index)));
}

NevercStatus IRPluginBridge::getPHIIncomingCount(
    NevercIRValueHandle Handle, uint64_t *OutCount) const {
  if (!OutCount)
    return functionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutCount = 0;
  Value *Resolved = nullptr;
  NevercStatus Status = resolveValue(Handle, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *Phi = dyn_cast<PHINode>(Resolved);
  if (!Phi)
    return functionStatus(NEVERC_STATUS_WRONG_TYPE);
  *OutCount = Phi->getNumIncomingValues();
  return neverc_status_ok();
}

NevercStatus IRPluginBridge::getPHIIncoming(
    NevercIRValueHandle Handle, uint64_t Index,
    NevercIRPhiIncoming *OutIncoming) {
  if (!OutIncoming ||
      OutIncoming->Header.StructSize < sizeof(NevercIRPhiIncoming))
    return functionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  Value *Resolved = nullptr;
  NevercStatus Status = resolveValue(Handle, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *Phi = dyn_cast<PHINode>(Resolved);
  if (!Phi)
    return functionStatus(NEVERC_STATUS_WRONG_TYPE);
  if (Index >= Phi->getNumIncomingValues())
    return functionStatus(NEVERC_STATUS_NOT_FOUND);
  auto ValueHandle =
      wrapValue(*Phi->getIncomingValue(static_cast<unsigned>(Index)));
  auto BlockHandle =
      wrapValue(*Phi->getIncomingBlock(static_cast<unsigned>(Index)));
  if (!ValueHandle || !BlockHandle) {
    if (!ValueHandle)
      consumeError(ValueHandle.takeError());
    if (!BlockHandle)
      consumeError(BlockHandle.takeError());
    return functionStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutIncoming = {};
  OutIncoming->Header = {sizeof(*OutIncoming), NEVERC_IR_CORE_API_MAJOR,
                         NEVERC_IR_CORE_API_MINOR, 0};
  OutIncoming->Value = *ValueHandle;
  OutIncoming->Block = *BlockHandle;
  return neverc_status_ok();
}

NevercStatus IRPluginBridge::setPHIIncoming(
    NevercIRValueHandle Handle, uint64_t Index,
    const NevercIRPhiIncoming &Incoming) {
  NevercStatus Status = checkMutationAllowed();
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Incoming.Header.StructSize < sizeof(NevercIRPhiIncoming))
    return functionStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  Value *Resolved = nullptr;
  Status = resolveValue(Handle, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *Phi = dyn_cast<PHINode>(Resolved);
  if (!Phi)
    return functionStatus(NEVERC_STATUS_WRONG_TYPE);
  if (Index >= Phi->getNumIncomingValues())
    return functionStatus(NEVERC_STATUS_NOT_FOUND);
  Value *IncomingValue = nullptr;
  Status = resolveValue(Incoming.Value, &IncomingValue);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Value *IncomingBlockValue = nullptr;
  Status = resolveValue(Incoming.Block, &IncomingBlockValue);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *IncomingBlock = dyn_cast<BasicBlock>(IncomingBlockValue);
  if (!IncomingBlock || IncomingValue->getType() != Phi->getType())
    return functionStatus(NEVERC_STATUS_WRONG_TYPE);
  if (!Phi->getParent() || !Phi->getFunction() ||
      IncomingBlock->getParent() != Phi->getFunction())
    return functionStatus(NEVERC_STATUS_WRONG_SCOPE);
  Phi->setIncomingValue(static_cast<unsigned>(Index), IncomingValue);
  Phi->setIncomingBlock(static_cast<unsigned>(Index), IncomingBlock);
  noteMutation();
  return neverc_status_ok();
}

} // namespace neverc::plugin
