#include "neverc/Plugin/Host/IRPluginBridge.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/MathExtras.h"
#include <new>

using namespace llvm;

namespace neverc::plugin {

struct IRPluginAttributeRecord {
  explicit IRPluginAttributeRecord(Attribute AttributeValue)
      : Value(AttributeValue) {}
  Attribute Value;
};

namespace {

Error attributeError(const Twine &Message) {
  return createStringError(inconvertibleErrorCode(), Message);
}

NevercStatus attributeStatus(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

Expected<Attribute::AttrKind> decodeAttributeKind(StringRef Name) {
  if (Name.empty() || Name.contains('\0'))
    return attributeError("IR attribute kind name is invalid");
  Attribute::AttrKind Kind = Attribute::getAttrKindFromName(Name);
  if (Kind == Attribute::None)
    return attributeError("IR attribute kind name is unknown");
  return Kind;
}

} // namespace

Expected<NevercIRAttributeHandle> IRPluginBridge::adoptAttributeRecord(
    std::unique_ptr<IRPluginAttributeRecord> Record) {
  if (!Record || !Record->Value.isValid())
    return attributeError("IR attribute record is invalid");
  IRPluginAttributeRecord *Payload = Record.release();
  auto Created = Task.handles().create(
      PluginIRAttributeHandleKind, Payload,
      [](void *Value) {
        delete static_cast<IRPluginAttributeRecord *>(Value);
      });
  if (!Created) {
    delete Payload;
    return Created.takeError();
  }
  NevercIRAttributeHandle Handle = *Created;
  AttributeHandles.emplace(Payload, Handle);
  return Handle;
}

NevercStatus IRPluginBridge::resolveAttributeRecord(
    NevercIRAttributeHandle Handle,
    IRPluginAttributeRecord **OutRecord) const {
  if (!OutRecord)
    return attributeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutRecord = nullptr;
  void *Payload = nullptr;
  NevercStatus Status = Task.handles().resolve(
      Handle, PluginIRAttributeHandleKind, &Payload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *Record = static_cast<IRPluginAttributeRecord *>(Payload);
  auto Existing = AttributeHandles.find(Record);
  if (Existing == AttributeHandles.end() ||
      Existing->second.Owner != Handle.Owner ||
      Existing->second.Value != Handle.Value)
    return attributeStatus(NEVERC_STATUS_STALE_HANDLE);
  *OutRecord = Record;
  return Status;
}

Expected<NevercIRAttributeHandle>
IRPluginBridge::createEnumAttribute(StringRef KindName) {
  auto Kind = decodeAttributeKind(KindName);
  if (!Kind)
    return Kind.takeError();
  if (!Attribute::isEnumAttrKind(*Kind))
    return attributeError("IR attribute kind is not an enum attribute");
  auto Record = std::unique_ptr<IRPluginAttributeRecord>(
      new (std::nothrow)
          IRPluginAttributeRecord(Attribute::get(*Context, *Kind)));
  if (!Record)
    return attributeError("IR attribute allocation failed");
  return adoptAttributeRecord(std::move(Record));
}

Expected<NevercIRAttributeHandle>
IRPluginBridge::createIntegerAttribute(StringRef KindName, uint64_t Value) {
  auto Kind = decodeAttributeKind(KindName);
  if (!Kind)
    return Kind.takeError();
  if (!Attribute::isIntAttrKind(*Kind))
    return attributeError("IR attribute kind is not an integer attribute");
  if ((*Kind == Attribute::Alignment || *Kind == Attribute::StackAlignment) &&
      (!isPowerOf2_64(Value) || Value > llvm::Value::MaximumAlignment))
    return attributeError("IR alignment attribute value is invalid");
  auto Record = std::unique_ptr<IRPluginAttributeRecord>(
      new (std::nothrow)
          IRPluginAttributeRecord(Attribute::get(*Context, *Kind, Value)));
  if (!Record)
    return attributeError("IR attribute allocation failed");
  return adoptAttributeRecord(std::move(Record));
}

Expected<NevercIRAttributeHandle>
IRPluginBridge::createStringAttribute(StringRef Kind, StringRef Value) {
  if (Kind.empty() || Kind.contains('\0') || Value.contains('\0'))
    return attributeError("IR string attribute contains an invalid name");
  if (Attribute::isExistingAttribute(Kind))
    return attributeError(
        "IR built-in attributes require their typed constructor");
  auto Record = std::unique_ptr<IRPluginAttributeRecord>(
      new (std::nothrow)
          IRPluginAttributeRecord(Attribute::get(*Context, Kind, Value)));
  if (!Record)
    return attributeError("IR attribute allocation failed");
  return adoptAttributeRecord(std::move(Record));
}

Expected<NevercIRAttributeHandle>
IRPluginBridge::createTypeAttribute(StringRef KindName,
                                    NevercIRTypeHandle TypeHandle) {
  auto Kind = decodeAttributeKind(KindName);
  if (!Kind)
    return Kind.takeError();
  if (!Attribute::isTypeAttrKind(*Kind))
    return attributeError("IR attribute kind is not a type attribute");
  Type *ResolvedType = nullptr;
  NevercStatus Status = resolveType(TypeHandle, &ResolvedType);
  if (Status.Code != NEVERC_STATUS_OK)
    return attributeError("IR type attribute type handle is invalid");
  if (!ResolvedType->isSized())
    return attributeError("IR type attribute requires a sized type");
  auto Record = std::unique_ptr<IRPluginAttributeRecord>(
      new (std::nothrow) IRPluginAttributeRecord(
          Attribute::get(*Context, *Kind, ResolvedType)));
  if (!Record)
    return attributeError("IR attribute allocation failed");
  return adoptAttributeRecord(std::move(Record));
}

NevercStatus IRPluginBridge::getAttributeValueKind(
    NevercIRAttributeHandle Handle,
    NevercIRAttributeValueKind *OutKind) const {
  if (!OutKind)
    return attributeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutKind = 0;
  IRPluginAttributeRecord *Record = nullptr;
  NevercStatus Status = resolveAttributeRecord(Handle, &Record);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  if (Record->Value.isEnumAttribute())
    *OutKind = NEVERC_IR_ATTRIBUTE_ENUM;
  else if (Record->Value.isIntAttribute())
    *OutKind = NEVERC_IR_ATTRIBUTE_INTEGER;
  else if (Record->Value.isStringAttribute())
    *OutKind = NEVERC_IR_ATTRIBUTE_STRING;
  else if (Record->Value.isTypeAttribute())
    *OutKind = NEVERC_IR_ATTRIBUTE_TYPE;
  else
    return attributeStatus(NEVERC_STATUS_WRONG_TYPE);
  return Status;
}

Expected<StringRef>
IRPluginBridge::getAttributeKindName(NevercIRAttributeHandle Handle) const {
  IRPluginAttributeRecord *Record = nullptr;
  NevercStatus Status = resolveAttributeRecord(Handle, &Record);
  if (Status.Code != NEVERC_STATUS_OK)
    return attributeError("IR attribute handle is invalid");
  if (Record->Value.isStringAttribute())
    return Record->Value.getKindAsString();
  return Attribute::getNameFromAttrKind(Record->Value.getKindAsEnum());
}

Expected<uint64_t> IRPluginBridge::getAttributeIntegerValue(
    NevercIRAttributeHandle Handle) const {
  IRPluginAttributeRecord *Record = nullptr;
  NevercStatus Status = resolveAttributeRecord(Handle, &Record);
  if (Status.Code != NEVERC_STATUS_OK)
    return attributeError("IR attribute handle is invalid");
  if (!Record->Value.isIntAttribute())
    return attributeError("IR attribute does not contain an integer");
  return Record->Value.getValueAsInt();
}

Expected<StringRef> IRPluginBridge::getAttributeStringValue(
    NevercIRAttributeHandle Handle) const {
  IRPluginAttributeRecord *Record = nullptr;
  NevercStatus Status = resolveAttributeRecord(Handle, &Record);
  if (Status.Code != NEVERC_STATUS_OK)
    return attributeError("IR attribute handle is invalid");
  if (!Record->Value.isStringAttribute())
    return attributeError("IR attribute does not contain a string");
  return Record->Value.getValueAsString();
}

Expected<NevercIRTypeHandle>
IRPluginBridge::getAttributeTypeValue(NevercIRAttributeHandle Handle) {
  IRPluginAttributeRecord *Record = nullptr;
  NevercStatus Status = resolveAttributeRecord(Handle, &Record);
  if (Status.Code != NEVERC_STATUS_OK)
    return attributeError("IR attribute handle is invalid");
  if (!Record->Value.isTypeAttribute())
    return attributeError("IR attribute does not contain a type");
  return wrapType(*Record->Value.getValueAsType());
}

NevercStatus IRPluginBridge::addFunctionAttribute(
    NevercIRValueHandle FunctionHandle, NevercIRAttributeLocation Location,
    uint32_t ParameterIndex, NevercIRAttributeHandle AttributeHandle) {
  NevercStatus Status = checkMutationAllowed();
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Value *ResolvedValue = nullptr;
  Status = resolveValue(FunctionHandle, &ResolvedValue);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *Target = dyn_cast<Function>(ResolvedValue);
  if (!Target)
    return attributeStatus(NEVERC_STATUS_WRONG_TYPE);
  if (Target->getParent() != Module)
    return attributeStatus(NEVERC_STATUS_WRONG_SCOPE);
  IRPluginAttributeRecord *Record = nullptr;
  Status = resolveAttributeRecord(AttributeHandle, &Record);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  Attribute AttributeValue = Record->Value;
  if (AttributeValue.isStringAttribute()) {
    if (Location != NEVERC_IR_ATTRIBUTE_LOCATION_FUNCTION)
      return attributeStatus(NEVERC_STATUS_POLICY_VIOLATION);
  } else {
    Attribute::AttrKind Kind = AttributeValue.getKindAsEnum();
    bool Allowed =
        (Location == NEVERC_IR_ATTRIBUTE_LOCATION_FUNCTION &&
         Attribute::canUseAsFnAttr(Kind)) ||
        (Location == NEVERC_IR_ATTRIBUTE_LOCATION_RETURN &&
         Attribute::canUseAsRetAttr(Kind)) ||
        (Location == NEVERC_IR_ATTRIBUTE_LOCATION_PARAMETER &&
         Attribute::canUseAsParamAttr(Kind));
    if (!Allowed)
      return attributeStatus(NEVERC_STATUS_POLICY_VIOLATION);
  }

  switch (Location) {
  case NEVERC_IR_ATTRIBUTE_LOCATION_FUNCTION:
    Target->addFnAttr(AttributeValue);
    break;
  case NEVERC_IR_ATTRIBUTE_LOCATION_RETURN:
    Target->addRetAttr(AttributeValue);
    break;
  case NEVERC_IR_ATTRIBUTE_LOCATION_PARAMETER:
    if (ParameterIndex >= Target->arg_size())
      return attributeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    Target->addParamAttr(ParameterIndex, AttributeValue);
    break;
  default:
    return attributeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  }
  noteMutation();
  return neverc_status_ok();
}

NevercStatus IRPluginBridge::hasFunctionAttribute(
    NevercIRValueHandle FunctionHandle, StringRef Kind,
    NevercBool *OutPresent) const {
  if (!OutPresent || Kind.empty() || Kind.contains('\0'))
    return attributeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutPresent = NEVERC_FALSE;
  Value *ResolvedValue = nullptr;
  NevercStatus Status = resolveValue(FunctionHandle, &ResolvedValue);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto *Target = dyn_cast<Function>(ResolvedValue);
  if (!Target)
    return attributeStatus(NEVERC_STATUS_WRONG_TYPE);
  if (Target->getParent() != Module)
    return attributeStatus(NEVERC_STATUS_WRONG_SCOPE);
  *OutPresent =
      Target->hasFnAttribute(Kind) ? NEVERC_TRUE : NEVERC_FALSE;
  return neverc_status_ok();
}

Expected<StringRef> IRPluginBridge::getFunctionStringAttribute(
    NevercIRValueHandle FunctionHandle, StringRef Kind) const {
  if (Kind.empty() || Kind.contains('\0'))
    return attributeError("IR function attribute name is invalid");
  Value *ResolvedValue = nullptr;
  NevercStatus Status = resolveValue(FunctionHandle, &ResolvedValue);
  if (Status.Code != NEVERC_STATUS_OK)
    return attributeError("IR function handle is invalid");
  auto *Target = dyn_cast<Function>(ResolvedValue);
  if (!Target)
    return attributeError("IR value is not a function");
  if (Target->getParent() != Module)
    return attributeError("IR function belongs to a different module");
  Attribute Value = Target->getFnAttribute(Kind);
  if (!Value.isValid())
    return attributeError("IR function attribute was not found");
  if (!Value.isStringAttribute())
    return attributeError("IR function attribute is not a string attribute");
  return Value.getValueAsString();
}

} // namespace neverc::plugin
