#include "neverc/Plugin/Host/IRPluginBridge.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalIFunc.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"

namespace neverc::plugin {
namespace {

NevercStatus irStatus(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

bool sameHandle(NevercHandle Left, NevercHandle Right) {
  return Left.Owner == Right.Owner && Left.Value == Right.Value;
}

NevercIRValueKind classifyValue(const llvm::Value &Value) {
  if (Value.getValueID() >= llvm::Value::InstructionVal)
    return NEVERC_IR_VALUE_INSTRUCTION;
  switch (static_cast<llvm::Value::ValueTy>(Value.getValueID())) {
#define NEVERC_IR_SCHEMA_INTERNAL_VALUE(Internal, Symbol, ID)                 \
  case llvm::Value::Internal:                                                 \
    return ID;
#include "neverc/Plugin/Schema/PluginIRSchema.inc"
#undef NEVERC_IR_SCHEMA_INTERNAL_VALUE
  }
  return NEVERC_IR_VALUE_UNKNOWN;
}

} // namespace

llvm::Expected<NevercIRContextHandle>
IRPluginBridge::wrapContext(llvm::LLVMContext &Value) {
  if (&Value != Context)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "IR context belongs to another bridge");
  return ContextHandle;
}

NevercStatus
IRPluginBridge::resolveContext(NevercIRContextHandle Handle,
                               llvm::LLVMContext **OutContext) const {
  if (!OutContext)
    return irStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutContext = nullptr;
  void *Payload = nullptr;
  NevercStatus Status = Task.handles().resolve(
      Handle, PluginIRContextHandleKind, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Payload != Context)
    return irStatus(NEVERC_STATUS_WRONG_SCOPE);
  *OutContext = static_cast<llvm::LLVMContext *>(Payload);
  return Status;
}

llvm::Expected<NevercIRModuleHandle>
IRPluginBridge::wrapModule(llvm::Module &Value) {
  if (&Value != Module || &Value.getContext() != Context)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "IR module belongs to another bridge");
  return ModuleHandle;
}

NevercStatus
IRPluginBridge::resolveModule(NevercIRModuleHandle Handle,
                              llvm::Module **OutModule) const {
  if (!OutModule) {
    NevercStatus Status = neverc_status_ok();
    Status.Code = NEVERC_STATUS_INVALID_ARGUMENT;
    return Status;
  }
  *OutModule = nullptr;
  void *Payload = nullptr;
  NevercStatus Status = Task.handles().resolve(
      Handle, PluginIRModuleHandleKind, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Payload != Module) {
    Status.Code = NEVERC_STATUS_WRONG_SCOPE;
    return Status;
  }
  *OutModule = static_cast<llvm::Module *>(Payload);
  return Status;
}

llvm::Expected<NevercIRValueHandle>
IRPluginBridge::wrapValue(llvm::Value &Value) {
  if (&Value.getContext() != Context)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "IR value belongs to another context");
  auto Existing = ValueHandles.find(&Value);
  if (Existing != ValueHandles.end())
    return Existing->second;
  auto Created =
      Task.handles().create(PluginIRValueHandleKind, &Value);
  if (!Created)
    return Created.takeError();
  ValueHandles.emplace(&Value, *Created);
  return *Created;
}

NevercStatus
IRPluginBridge::resolveValue(NevercIRValueHandle Handle,
                             llvm::Value **OutValue) const {
  if (!OutValue)
    return irStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutValue = nullptr;
  void *Payload = nullptr;
  NevercStatus Status =
      Task.handles().resolve(Handle, PluginIRValueHandleKind, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *Value = static_cast<llvm::Value *>(Payload);
  auto Existing = ValueHandles.find(Value);
  if (Existing == ValueHandles.end() ||
      !sameHandle(Existing->second, Handle))
    return irStatus(NEVERC_STATUS_WRONG_SCOPE);
  *OutValue = Value;
  return Status;
}

NevercStatus
IRPluginBridge::getValueKind(NevercIRValueHandle Handle,
                             NevercIRValueKind *OutKind) const {
  if (!OutKind)
    return irStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutKind = NEVERC_IR_VALUE_UNKNOWN;
  llvm::Value *Value = nullptr;
  NevercStatus Status = resolveValue(Handle, &Value);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  *OutKind = classifyValue(*Value);
  return Status;
}

NevercStatus
IRPluginBridge::replaceAllUsesWith(NevercIRValueHandle Original,
                                   NevercIRValueHandle Replacement) {
  NevercStatus Status = checkMutationAllowed();
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  llvm::Value *OriginalValue = nullptr;
  Status = resolveValue(Original, &OriginalValue);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  llvm::Value *ReplacementValue = nullptr;
  Status = resolveValue(Replacement, &ReplacementValue);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (OriginalValue->getType() != ReplacementValue->getType())
    return irStatus(NEVERC_STATUS_WRONG_TYPE);
  if (OriginalValue != ReplacementValue) {
    Status = invalidateValueMetadataHandle(*OriginalValue);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
    OriginalValue->replaceAllUsesWith(ReplacementValue);
    noteMutation();
  }
  return Status;
}

NevercStatus IRPluginBridge::eraseValue(NevercIRValueHandle Handle) {
  NevercStatus Status = checkMutationAllowed();
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  llvm::Value *Value = nullptr;
  Status = resolveValue(Handle, &Value);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *Instruction = llvm::dyn_cast<llvm::Instruction>(Value);
  if (!Instruction)
    return irStatus(NEVERC_STATUS_WRONG_TYPE);
  if (!Instruction->use_empty() || !Instruction->getParent())
    return irStatus(NEVERC_STATUS_INVALID_STATE);

  Status = invalidateValueMetadataHandle(*Instruction);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  ValueHandles.erase(Instruction);
  Status = Task.handles().release(Handle, PluginIRValueHandleKind);
  if (Status.Code != NEVERC_STATUS_OK) {
    ValueHandles.emplace(Instruction, Handle);
    return Status;
  }
  Instruction->eraseFromParent();
  noteMutation();
  return Status;
}

NevercStatus
IRPluginBridge::invalidateValueMetadataHandle(llvm::Value &Value) {
  llvm::ValueAsMetadata *Metadata =
      llvm::ValueAsMetadata::getIfExists(&Value);
  if (!Metadata)
    return neverc_status_ok();
  auto Existing = MetadataHandles.find(Metadata);
  if (Existing == MetadataHandles.end())
    return neverc_status_ok();
  NevercStatus Status = Task.handles().release(
      Existing->second, PluginIRMetadataHandleKind);
  if (Status.Code == NEVERC_STATUS_OK)
    MetadataHandles.erase(Existing);
  return Status;
}

llvm::Expected<NevercIRTypeHandle>
IRPluginBridge::wrapType(llvm::Type &Value) {
  if (&Value.getContext() != Context)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "IR type belongs to another context");
  auto Existing = TypeHandles.find(&Value);
  if (Existing != TypeHandles.end())
    return Existing->second;
  auto Created =
      Task.handles().create(PluginIRTypeHandleKind, &Value);
  if (!Created)
    return Created.takeError();
  TypeHandles.emplace(&Value, *Created);
  return *Created;
}

NevercStatus
IRPluginBridge::resolveType(NevercIRTypeHandle Handle,
                            llvm::Type **OutType) const {
  if (!OutType)
    return irStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutType = nullptr;
  void *Payload = nullptr;
  NevercStatus Status =
      Task.handles().resolve(Handle, PluginIRTypeHandleKind, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *Type = static_cast<llvm::Type *>(Payload);
  auto Existing = TypeHandles.find(Type);
  if (Existing == TypeHandles.end() ||
      !sameHandle(Existing->second, Handle))
    return irStatus(NEVERC_STATUS_WRONG_SCOPE);
  *OutType = Type;
  return Status;
}

} // namespace neverc::plugin
