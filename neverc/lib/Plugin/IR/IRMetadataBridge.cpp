#include "neverc/Plugin/Host/IRPluginBridge.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Value.h"
#include <cstring>

using namespace llvm;

namespace neverc::plugin {
namespace {

Error metadataError(const Twine &Message) {
  return createStringError(inconvertibleErrorCode(), Message);
}

NevercStatus metadataStatus(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

bool metadataBelongsTo(Metadata &Value, LLVMContext &ExpectedContext) {
  if (auto *String = dyn_cast<MDString>(&Value))
    return MDString::get(ExpectedContext, String->getString()) == String;
  if (auto *Node = dyn_cast<MDNode>(&Value))
    return &Node->getContext() == &ExpectedContext;
  if (auto *WrappedValue = dyn_cast<ValueAsMetadata>(&Value))
    return &WrappedValue->getContext() == &ExpectedContext;
  return false;
}

} // namespace

Expected<NevercIRMetadataHandle>
IRPluginBridge::wrapMetadata(Metadata &Value) {
  if (!metadataBelongsTo(Value, *Context))
    return metadataError("IR metadata belongs to another context");
  auto Existing = MetadataHandles.find(&Value);
  if (Existing != MetadataHandles.end())
    return Existing->second;
  auto Created =
      Task.handles().create(PluginIRMetadataHandleKind, &Value);
  if (!Created)
    return Created.takeError();
  NevercIRMetadataHandle Handle = *Created;
  MetadataHandles.emplace(&Value, Handle);
  return Handle;
}

NevercStatus
IRPluginBridge::resolveMetadata(NevercIRMetadataHandle Handle,
                                Metadata **OutMetadata) const {
  if (!OutMetadata)
    return metadataStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutMetadata = nullptr;
  void *Payload = nullptr;
  NevercStatus Status = Task.handles().resolve(
      Handle, PluginIRMetadataHandleKind, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *Resolved = static_cast<Metadata *>(Payload);
  auto Existing = MetadataHandles.find(Resolved);
  if (Existing == MetadataHandles.end() ||
      Existing->second.Owner != Handle.Owner ||
      Existing->second.Value != Handle.Value)
    return metadataStatus(NEVERC_STATUS_STALE_HANDLE);
  *OutMetadata = Resolved;
  return Status;
}

Expected<NevercIRNamedMetadataHandle>
IRPluginBridge::wrapNamedMetadata(NamedMDNode &Value) {
  if (Value.getParent() != Module)
    return metadataError("IR named metadata belongs to another module");
  auto Existing = NamedMetadataHandles.find(&Value);
  if (Existing != NamedMetadataHandles.end())
    return Existing->second;
  auto Created =
      Task.handles().create(PluginIRNamedMetadataHandleKind, &Value);
  if (!Created)
    return Created.takeError();
  NevercIRNamedMetadataHandle Handle = *Created;
  NamedMetadataHandles.emplace(&Value, Handle);
  return Handle;
}

NevercStatus IRPluginBridge::resolveNamedMetadata(
    NevercIRNamedMetadataHandle Handle, NamedMDNode **OutMetadata) const {
  if (!OutMetadata)
    return metadataStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutMetadata = nullptr;
  void *Payload = nullptr;
  NevercStatus Status = Task.handles().resolve(
      Handle, PluginIRNamedMetadataHandleKind, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *Resolved = static_cast<NamedMDNode *>(Payload);
  auto Existing = NamedMetadataHandles.find(Resolved);
  if (Existing == NamedMetadataHandles.end() ||
      Existing->second.Owner != Handle.Owner ||
      Existing->second.Value != Handle.Value ||
      Resolved->getParent() != Module)
    return metadataStatus(NEVERC_STATUS_STALE_HANDLE);
  *OutMetadata = Resolved;
  return Status;
}

NevercStatus
IRPluginBridge::getMetadataKind(NevercIRMetadataHandle Handle,
                                NevercIRMetadataKind *OutKind) const {
  if (!OutKind)
    return metadataStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutKind = NEVERC_IR_METADATA_UNKNOWN;
  Metadata *Resolved = nullptr;
  NevercStatus Status = resolveMetadata(Handle, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (isa<DILocation>(Resolved))
    *OutKind = NEVERC_IR_METADATA_DEBUG_LOCATION;
  else if (isa<MDString>(Resolved))
    *OutKind = NEVERC_IR_METADATA_STRING;
  else if (isa<ValueAsMetadata>(Resolved))
    *OutKind = NEVERC_IR_METADATA_VALUE;
  else if (isa<MDNode>(Resolved))
    *OutKind = NEVERC_IR_METADATA_NODE;
  else
    return metadataStatus(NEVERC_STATUS_CAPABILITY_UNAVAILABLE);
  return Status;
}

Expected<NevercIRMetadataHandle>
IRPluginBridge::getMetadataString(StringRef Bytes) {
  return wrapMetadata(*MDString::get(*Context, Bytes));
}

Expected<StringRef>
IRPluginBridge::getMetadataStringBytes(NevercIRMetadataHandle Handle) const {
  Metadata *Resolved = nullptr;
  NevercStatus Status = resolveMetadata(Handle, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return metadataError("IR metadata string handle is invalid");
  auto *String = dyn_cast<MDString>(Resolved);
  if (!String)
    return metadataError("IR metadata is not a string");
  return String->getString();
}

Expected<NevercIRMetadataHandle> IRPluginBridge::getMetadataNode(
    ArrayRef<NevercIRMetadataHandle> OperandHandles, bool Distinct) {
  SmallVector<Metadata *, 8> Operands;
  Operands.reserve(OperandHandles.size());
  for (NevercIRMetadataHandle Handle : OperandHandles) {
    if (Handle.Owner == 0 && Handle.Value == 0) {
      Operands.push_back(nullptr);
      continue;
    }
    Metadata *Operand = nullptr;
    NevercStatus Status = resolveMetadata(Handle, &Operand);
    if (Status.Code != NEVERC_STATUS_OK)
      return metadataError("IR metadata node operand handle is invalid");
    Operands.push_back(Operand);
  }
  MDNode *Node = Distinct ? MDNode::getDistinct(*Context, Operands)
                          : MDNode::get(*Context, Operands);
  return wrapMetadata(*Node);
}

Expected<NevercIRMetadataHandle>
IRPluginBridge::getValueAsMetadata(NevercIRValueHandle Handle) {
  Value *Resolved = nullptr;
  NevercStatus Status = resolveValue(Handle, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return metadataError("IR value-as-metadata handle is invalid");
  if (!isa<Constant>(Resolved) && !isa<Argument>(Resolved) &&
      !isa<Instruction>(Resolved))
    return metadataError(
        "only constants and function-local values can become metadata");
  return wrapMetadata(*ValueAsMetadata::get(Resolved));
}

Expected<NevercIRValueHandle>
IRPluginBridge::getMetadataAsValue(NevercIRMetadataHandle Handle) {
  Metadata *Resolved = nullptr;
  NevercStatus Status = resolveMetadata(Handle, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return metadataError("IR metadata-as-value handle is invalid");
  return wrapValue(*MetadataAsValue::get(*Context, Resolved));
}

NevercStatus
IRPluginBridge::getMetadataOperandCount(NevercIRMetadataHandle Handle,
                                        uint64_t *OutCount) const {
  if (!OutCount)
    return metadataStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutCount = 0;
  Metadata *Resolved = nullptr;
  NevercStatus Status = resolveMetadata(Handle, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *Node = dyn_cast<MDNode>(Resolved);
  if (!Node)
    return metadataStatus(NEVERC_STATUS_WRONG_TYPE);
  *OutCount = Node->getNumOperands();
  return Status;
}

Expected<NevercIRMetadataHandle>
IRPluginBridge::getMetadataOperand(NevercIRMetadataHandle Handle,
                                   uint64_t Index) {
  Metadata *Resolved = nullptr;
  NevercStatus Status = resolveMetadata(Handle, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return metadataError("IR metadata node handle is invalid");
  auto *Node = dyn_cast<MDNode>(Resolved);
  if (!Node)
    return metadataError("IR metadata is not a node");
  if (Index >= Node->getNumOperands())
    return metadataError("IR metadata operand index is out of range");
  Metadata *Operand = Node->getOperand(static_cast<unsigned>(Index)).get();
  if (!Operand)
    return NevercIRMetadataHandle{};
  return wrapMetadata(*Operand);
}

Expected<NevercIRNamedMetadataHandle>
IRPluginBridge::getOrInsertNamedMetadata(StringRef Name) {
  if (checkMutationAllowed().Code != NEVERC_STATUS_OK)
    return metadataError("IR mutation is unavailable in this context");
  if (Name.empty() || Name.contains('\0'))
    return metadataError("IR named metadata name is invalid");
  auto Handle = wrapNamedMetadata(*Module->getOrInsertNamedMetadata(Name));
  if (Handle)
    noteMutation();
  return Handle;
}

NevercStatus IRPluginBridge::appendNamedMetadata(
    NevercIRNamedMetadataHandle NamedHandle,
    NevercIRMetadataHandle NodeHandle) {
  NevercStatus Status = checkMutationAllowed();
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  NamedMDNode *Named = nullptr;
  Status = resolveNamedMetadata(NamedHandle, &Named);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Metadata *Resolved = nullptr;
  Status = resolveMetadata(NodeHandle, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *Node = dyn_cast<MDNode>(Resolved);
  if (!Node)
    return metadataStatus(NEVERC_STATUS_WRONG_TYPE);
  Named->addOperand(Node);
  noteMutation();
  return neverc_status_ok();
}

NevercStatus IRPluginBridge::getNamedMetadataOperandCount(
    NevercIRNamedMetadataHandle Handle, uint64_t *OutCount) const {
  if (!OutCount)
    return metadataStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutCount = 0;
  NamedMDNode *Named = nullptr;
  NevercStatus Status = resolveNamedMetadata(Handle, &Named);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  *OutCount = Named->getNumOperands();
  return Status;
}

Expected<NevercIRMetadataHandle> IRPluginBridge::getNamedMetadataOperand(
    NevercIRNamedMetadataHandle Handle, uint64_t Index) {
  NamedMDNode *Named = nullptr;
  NevercStatus Status = resolveNamedMetadata(Handle, &Named);
  if (Status.Code != NEVERC_STATUS_OK)
    return metadataError("IR named metadata handle is invalid");
  if (Index >= Named->getNumOperands())
    return metadataError("IR named metadata operand index is out of range");
  return wrapMetadata(*Named->getOperand(static_cast<unsigned>(Index)));
}

NevercStatus IRPluginBridge::getDebugLocationInfo(
    NevercIRMetadataHandle Handle, NevercIRDebugLocationInfo *OutInfo) {
  if (!OutInfo || OutInfo->Size < sizeof(NevercIRDebugLocationInfo) ||
      OutInfo->Version != 1)
    return metadataStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  uint32_t Size = OutInfo->Size;
  uint32_t Version = OutInfo->Version;
  std::memset(OutInfo, 0, sizeof(*OutInfo));
  OutInfo->Size = Size;
  OutInfo->Version = Version;

  Metadata *Resolved = nullptr;
  NevercStatus Status = resolveMetadata(Handle, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *Location = dyn_cast<DILocation>(Resolved);
  if (!Location)
    return metadataStatus(NEVERC_STATUS_WRONG_TYPE);
  OutInfo->Line = Location->getLine();
  OutInfo->Column = Location->getColumn();
  OutInfo->IsImplicitCode =
      Location->isImplicitCode() ? NEVERC_TRUE : NEVERC_FALSE;
  auto ScopeHandle = wrapMetadata(*Location->getScope());
  if (!ScopeHandle)
    return metadataStatus(NEVERC_STATUS_PLUGIN_FAILURE);
  OutInfo->Scope = *ScopeHandle;
  if (DILocation *InlinedAt = Location->getInlinedAt()) {
    auto InlinedHandle = wrapMetadata(*InlinedAt);
    if (!InlinedHandle)
      return metadataStatus(NEVERC_STATUS_PLUGIN_FAILURE);
    OutInfo->InlinedAt = *InlinedHandle;
  }
  return Status;
}

} // namespace neverc::plugin
