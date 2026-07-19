#include "neverc/Plugin/Host/IRPluginBridge.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include <cstddef>

using namespace llvm;

namespace neverc::plugin {
namespace {

NevercStatus valueStatus(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

bool canNameValue(const Value &Value) {
  if (isa<GlobalValue, Argument, BasicBlock>(Value))
    return true;
  auto *InstructionValue = dyn_cast<Instruction>(&Value);
  return InstructionValue && !InstructionValue->getType()->isVoidTy();
}

} // namespace

Expected<StringRef>
IRPluginBridge::getValueName(NevercIRValueHandle Handle) const {
  Value *Resolved = nullptr;
  NevercStatus Status = resolveValue(Handle, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return createStringError(inconvertibleErrorCode(),
                             "invalid IR value handle");
  if (!canNameValue(*Resolved))
    return createStringError(inconvertibleErrorCode(),
                             "IR value cannot have a name");
  return Resolved->getName();
}

NevercStatus IRPluginBridge::setValueName(NevercIRValueHandle Handle,
                                           StringRef Name) {
  NevercStatus Status = checkMutationAllowed();
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Name.contains('\0'))
    return valueStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  Value *Resolved = nullptr;
  Status = resolveValue(Handle, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (!canNameValue(*Resolved))
    return valueStatus(NEVERC_STATUS_WRONG_TYPE);
  Resolved->setName(Name);
  noteMutation();
  return neverc_status_ok();
}

Expected<NevercIRTypeHandle>
IRPluginBridge::getValueType(NevercIRValueHandle Handle) {
  Value *Resolved = nullptr;
  NevercStatus Status = resolveValue(Handle, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return createStringError(inconvertibleErrorCode(),
                             "invalid IR value handle");
  return wrapType(*Resolved->getType());
}

NevercStatus IRPluginBridge::getValueUseCount(
    NevercIRValueHandle Handle, uint64_t *OutCount) const {
  if (!OutCount)
    return valueStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutCount = 0;
  Value *Resolved = nullptr;
  NevercStatus Status = resolveValue(Handle, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  *OutCount = static_cast<uint64_t>(Resolved->getNumUses());
  return neverc_status_ok();
}

NevercStatus IRPluginBridge::getValueUse(NevercIRValueHandle Handle,
                                          uint64_t Index,
                                          NevercIRUseInfo *OutUse) {
  if (!OutUse || OutUse->Header.StructSize < sizeof(NevercIRUseInfo))
    return valueStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  Value *Resolved = nullptr;
  NevercStatus Status = resolveValue(Handle, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Index >= Resolved->getNumUses())
    return valueStatus(NEVERC_STATUS_NOT_FOUND);
  auto Iterator = Resolved->use_begin();
  std::advance(Iterator, static_cast<ptrdiff_t>(Index));
  Use &ValueUse = *Iterator;
  auto UserHandle = wrapValue(*ValueUse.getUser());
  if (!UserHandle) {
    consumeError(UserHandle.takeError());
    return valueStatus(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutUse = {};
  OutUse->Header = {sizeof(*OutUse), NEVERC_IR_CORE_API_MAJOR,
                    NEVERC_IR_CORE_API_MINOR, 0};
  OutUse->User = *UserHandle;
  OutUse->OperandIndex = ValueUse.getOperandNo();
  return neverc_status_ok();
}

NevercStatus IRPluginBridge::getOperandCount(
    NevercIRValueHandle Handle, uint64_t *OutCount) const {
  if (!OutCount)
    return valueStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutCount = 0;
  Value *Resolved = nullptr;
  NevercStatus Status = resolveValue(Handle, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *ValueUser = dyn_cast<User>(Resolved);
  if (!ValueUser)
    return valueStatus(NEVERC_STATUS_WRONG_TYPE);
  *OutCount = ValueUser->getNumOperands();
  return neverc_status_ok();
}

Expected<NevercIRValueHandle>
IRPluginBridge::getOperand(NevercIRValueHandle Handle, uint64_t Index) {
  Value *Resolved = nullptr;
  NevercStatus Status = resolveValue(Handle, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return createStringError(inconvertibleErrorCode(),
                             "invalid IR value handle");
  auto *ValueUser = dyn_cast<User>(Resolved);
  if (!ValueUser)
    return createStringError(inconvertibleErrorCode(),
                             "IR value has no operands");
  if (Index >= ValueUser->getNumOperands())
    return createStringError(inconvertibleErrorCode(),
                             "IR operand index is out of range");
  return wrapValue(*ValueUser->getOperand(static_cast<unsigned>(Index)));
}

NevercStatus IRPluginBridge::setOperand(
    NevercIRValueHandle Handle, uint64_t Index,
    NevercIRValueHandle OperandHandle) {
  NevercStatus Status = checkMutationAllowed();
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Value *Resolved = nullptr;
  Status = resolveValue(Handle, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *ValueUser = dyn_cast<User>(Resolved);
  if (!ValueUser)
    return valueStatus(NEVERC_STATUS_WRONG_TYPE);
  if (Index >= ValueUser->getNumOperands())
    return valueStatus(NEVERC_STATUS_NOT_FOUND);
  Value *Operand = nullptr;
  Status = resolveValue(OperandHandle, &Operand);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (ValueUser->getOperand(static_cast<unsigned>(Index))->getType() !=
      Operand->getType())
    return valueStatus(NEVERC_STATUS_WRONG_TYPE);
  ValueUser->setOperand(static_cast<unsigned>(Index), Operand);
  noteMutation();
  return neverc_status_ok();
}

} // namespace neverc::plugin
