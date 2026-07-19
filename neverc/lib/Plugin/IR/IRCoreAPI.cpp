#include "neverc/Plugin/Host/IRPluginBridge.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <cstddef>
#include <limits>

using namespace llvm;

namespace neverc::plugin {
namespace {

NevercStatus coreStatus(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

IRPluginBridge *getBridge(void *Context, NevercTaskHandle Task,
                          NevercStatus *OutStatus) {
  if (!Context) {
    *OutStatus = coreStatus(NEVERC_STATUS_INVALID_ARGUMENT);
    return nullptr;
  }
  auto *Bridge = static_cast<IRPluginBridge *>(Context);
  NevercTaskHandle Expected = Bridge->taskHandle();
  if (Expected.Owner != Task.Owner || Expected.Value != Task.Value) {
    *OutStatus = coreStatus(NEVERC_STATUS_WRONG_SCOPE);
    return nullptr;
  }
  *OutStatus = neverc_status_ok();
  return Bridge;
}

template <typename T>
NevercStatus writeExpected(Expected<T> Result, T *OutValue) {
  if (!OutValue) {
    if (!Result)
      consumeError(Result.takeError());
    return coreStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  }
  *OutValue = {};
  if (!Result) {
    consumeError(Result.takeError());
    return coreStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  }
  *OutValue = *Result;
  return neverc_status_ok();
}

template <typename T>
bool arrayRef(const T *Data, uint64_t Count, ArrayRef<T> *Out) {
  if ((Count != 0 && !Data) ||
      Count > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
    return false;
  *Out = ArrayRef<T>(Data, static_cast<size_t>(Count));
  return true;
}

bool stringRef(NevercStringView View, StringRef *Out) {
  if ((View.Length != 0 && !View.Data) ||
      View.Length >
          static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
    return false;
  *Out = StringRef(View.Data ? View.Data : "",
                   static_cast<size_t>(View.Length));
  return true;
}

NevercStatus writeStringView(StringRef Value, NevercStringView *OutValue) {
  if (!OutValue)
    return coreStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  OutValue->Data = Value.data();
  OutValue->Length = Value.size();
  return neverc_status_ok();
}

NevercStatus writeExpectedString(Expected<StringRef> Result,
                                  NevercStringView *OutValue) {
  if (!OutValue) {
    if (!Result)
      consumeError(Result.takeError());
    return coreStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  }
  *OutValue = {};
  if (!Result) {
    consumeError(Result.takeError());
    return coreStatus(NEVERC_STATUS_WRONG_TYPE);
  }
  return writeStringView(*Result, OutValue);
}

bool booleanValue(uint8_t Value, bool *Out) {
  if (Value != NEVERC_FALSE && Value != NEVERC_TRUE)
    return false;
  *Out = Value == NEVERC_TRUE;
  return true;
}

#define NEVERC_IR_BRIDGE_OR_RETURN()                                           \
  NevercStatus BridgeStatus;                                                   \
  IRPluginBridge *Bridge = getBridge(Context, Task, &BridgeStatus);            \
  if (!Bridge)                                                                 \
    return BridgeStatus

NevercStatus NEVERC_CALL GetContext(void *Context, NevercTaskHandle Task,
                                    NevercIRContextHandle *OutContext) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  if (!OutContext)
    return coreStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutContext = Bridge->contextHandle();
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL GetModule(void *Context, NevercTaskHandle Task,
                                   NevercIRModuleHandle *OutModule) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  if (!OutModule)
    return coreStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutModule = Bridge->moduleHandle();
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL GetValueKind(void *Context, NevercTaskHandle Task,
                                      NevercIRValueHandle Value,
                                      NevercIRValueKind *OutKind) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return Bridge->getValueKind(Value, OutKind);
}

NevercStatus NEVERC_CALL ReplaceAllUsesWith(
    void *Context, NevercTaskHandle Task, NevercIRValueHandle Original,
    NevercIRValueHandle Replacement) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return Bridge->replaceAllUsesWith(Original, Replacement);
}

NevercStatus NEVERC_CALL EraseValue(void *Context, NevercTaskHandle Task,
                                    NevercIRValueHandle Value) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return Bridge->eraseValue(Value);
}

NevercStatus NEVERC_CALL GetTypeKind(void *Context, NevercTaskHandle Task,
                                     NevercIRTypeHandle Type,
                                     NevercIRTypeKind *OutKind) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return Bridge->getTypeKind(Type, OutKind);
}

NevercStatus NEVERC_CALL GetPrimitiveType(void *Context,
                                          NevercTaskHandle Task,
                                          NevercIRTypeKind Kind,
                                          NevercIRTypeHandle *OutType) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return writeExpected(Bridge->getPrimitiveType(Kind), OutType);
}

NevercStatus NEVERC_CALL GetIntegerType(void *Context, NevercTaskHandle Task,
                                        uint32_t BitWidth,
                                        NevercIRTypeHandle *OutType) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return writeExpected(Bridge->getIntegerType(BitWidth), OutType);
}

NevercStatus NEVERC_CALL GetPointerType(void *Context, NevercTaskHandle Task,
                                        uint32_t AddressSpace,
                                        NevercIRTypeHandle *OutType) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return writeExpected(Bridge->getPointerType(AddressSpace), OutType);
}

NevercStatus NEVERC_CALL GetArrayType(void *Context, NevercTaskHandle Task,
                                      NevercIRTypeHandle ElementType,
                                      uint64_t ElementCount,
                                      NevercIRTypeHandle *OutType) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return writeExpected(Bridge->getArrayType(ElementType, ElementCount),
                       OutType);
}

NevercStatus NEVERC_CALL GetVectorType(
    void *Context, NevercTaskHandle Task, NevercIRTypeHandle ElementType,
    uint32_t MinimumElementCount, uint8_t Scalable,
    NevercIRTypeHandle *OutType) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  bool IsScalable = false;
  if (!booleanValue(Scalable, &IsScalable))
    return coreStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return writeExpected(
      Bridge->getVectorType(ElementType, MinimumElementCount, IsScalable),
      OutType);
}

NevercStatus NEVERC_CALL GetStructType(
    void *Context, NevercTaskHandle Task, NevercStringView Name,
    const NevercIRTypeHandle *ElementTypes, uint64_t ElementCount,
    uint8_t Packed, NevercIRTypeHandle *OutType) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  StringRef NameRef;
  ArrayRef<NevercIRTypeHandle> Elements;
  bool IsPacked = false;
  if (!stringRef(Name, &NameRef) ||
      !arrayRef(ElementTypes, ElementCount, &Elements) ||
      !booleanValue(Packed, &IsPacked))
    return coreStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return writeExpected(Bridge->getStructType(NameRef, Elements, IsPacked),
                       OutType);
}

NevercStatus NEVERC_CALL GetFunctionType(
    void *Context, NevercTaskHandle Task, NevercIRTypeHandle ReturnType,
    const NevercIRTypeHandle *ParameterTypes, uint64_t ParameterCount,
    uint8_t Variadic, NevercIRTypeHandle *OutType) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  ArrayRef<NevercIRTypeHandle> Parameters;
  bool IsVariadic = false;
  if (!arrayRef(ParameterTypes, ParameterCount, &Parameters) ||
      !booleanValue(Variadic, &IsVariadic))
    return coreStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return writeExpected(
      Bridge->getFunctionType(ReturnType, Parameters, IsVariadic), OutType);
}

NevercStatus NEVERC_CALL CreateIntegerConstant(
    void *Context, NevercTaskHandle Task, NevercIRTypeHandle Type,
    const uint64_t *Words, uint64_t WordCount,
    NevercIRValueHandle *OutValue) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  ArrayRef<uint64_t> WordArray;
  if (!arrayRef(Words, WordCount, &WordArray))
    return coreStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return writeExpected(Bridge->getIntegerConstant(Type, WordArray), OutValue);
}

NevercStatus NEVERC_CALL CreateFloatingConstant(
    void *Context, NevercTaskHandle Task, NevercIRTypeHandle Type,
    const uint64_t *Words, uint64_t WordCount,
    NevercIRValueHandle *OutValue) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  ArrayRef<uint64_t> WordArray;
  if (!arrayRef(Words, WordCount, &WordArray))
    return coreStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return writeExpected(Bridge->getFloatingConstant(Type, WordArray),
                       OutValue);
}

NevercStatus NEVERC_CALL GetNullConstant(void *Context, NevercTaskHandle Task,
                                         NevercIRTypeHandle Type,
                                         NevercIRValueHandle *OutValue) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return writeExpected(Bridge->getNullConstant(Type), OutValue);
}

NevercStatus NEVERC_CALL GetPoisonConstant(void *Context,
                                           NevercTaskHandle Task,
                                           NevercIRTypeHandle Type,
                                           NevercIRValueHandle *OutValue) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return writeExpected(Bridge->getPoisonConstant(Type), OutValue);
}

NevercStatus NEVERC_CALL GetUndefConstant(void *Context,
                                          NevercTaskHandle Task,
                                          NevercIRTypeHandle Type,
                                          NevercIRValueHandle *OutValue) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return writeExpected(Bridge->getUndefConstant(Type), OutValue);
}

NevercStatus NEVERC_CALL CreateAggregateConstant(
    void *Context, NevercTaskHandle Task, NevercIRTypeHandle Type,
    const NevercIRValueHandle *Elements, uint64_t ElementCount,
    NevercIRValueHandle *OutValue) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  ArrayRef<NevercIRValueHandle> ElementArray;
  if (!arrayRef(Elements, ElementCount, &ElementArray))
    return coreStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return writeExpected(Bridge->getAggregateConstant(Type, ElementArray),
                       OutValue);
}

NevercStatus NEVERC_CALL CreateConstantBinaryExpression(
    void *Context, NevercTaskHandle Task, NevercIROpcode Opcode,
    NevercIRValueHandle Left, NevercIRValueHandle Right,
    NevercIRValueHandle *OutValue) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return writeExpected(
      Bridge->getConstantBinaryExpression(Opcode, Left, Right), OutValue);
}

NevercStatus NEVERC_CALL CreateConstantCastExpression(
    void *Context, NevercTaskHandle Task, NevercIROpcode Opcode,
    NevercIRValueHandle Operand, NevercIRTypeHandle DestinationType,
    NevercIRValueHandle *OutValue) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return writeExpected(
      Bridge->getConstantCastExpression(Opcode, Operand, DestinationType),
      OutValue);
}

NevercStatus NEVERC_CALL CreateConstantCompareExpression(
    void *Context, NevercTaskHandle Task, NevercIRPredicate Predicate,
    NevercIRValueHandle Left, NevercIRValueHandle Right,
    NevercIRValueHandle *OutValue) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return writeExpected(
      Bridge->getConstantCompareExpression(Predicate, Left, Right), OutValue);
}

NevercStatus NEVERC_CALL CreateConstantGEPExpression(
    void *Context, NevercTaskHandle Task,
    NevercIRTypeHandle SourceElementType, NevercIRValueHandle Pointer,
    const NevercIRValueHandle *Indices, uint64_t IndexCount, uint8_t InBounds,
    NevercIRValueHandle *OutValue) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  ArrayRef<NevercIRValueHandle> IndexArray;
  bool IsInBounds = false;
  if (!arrayRef(Indices, IndexCount, &IndexArray) ||
      !booleanValue(InBounds, &IsInBounds))
    return coreStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return writeExpected(Bridge->getConstantGEPExpression(
                           SourceElementType, Pointer, IndexArray, IsInBounds),
                       OutValue);
}

NevercStatus NEVERC_CALL GetGlobalAddressConstant(
    void *Context, NevercTaskHandle Task, NevercIRValueHandle Global,
    NevercIRValueHandle *OutValue) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return writeExpected(Bridge->getGlobalAddressConstant(Global), OutValue);
}

NevercStatus NEVERC_CALL GetMetadataKind(
    void *Context, NevercTaskHandle Task, NevercIRMetadataHandle Metadata,
    NevercIRMetadataKind *OutKind) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return Bridge->getMetadataKind(Metadata, OutKind);
}

NevercStatus NEVERC_CALL CreateMetadataString(
    void *Context, NevercTaskHandle Task, NevercStringView Bytes,
    NevercIRMetadataHandle *OutMetadata) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  StringRef ByteRef;
  if (!stringRef(Bytes, &ByteRef))
    return coreStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return writeExpected(Bridge->getMetadataString(ByteRef), OutMetadata);
}

NevercStatus NEVERC_CALL GetMetadataStringBytes(
    void *Context, NevercTaskHandle Task, NevercIRMetadataHandle Metadata,
    NevercStringView *OutBytes) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  if (!OutBytes)
    return coreStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutBytes = {};
  auto Bytes = Bridge->getMetadataStringBytes(Metadata);
  if (!Bytes) {
    consumeError(Bytes.takeError());
    return coreStatus(NEVERC_STATUS_WRONG_TYPE);
  }
  OutBytes->Data = Bytes->data();
  OutBytes->Length = Bytes->size();
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL CreateMetadataNode(
    void *Context, NevercTaskHandle Task,
    const NevercIRMetadataHandle *Operands, uint64_t OperandCount,
    uint8_t Distinct, NevercIRMetadataHandle *OutMetadata) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  ArrayRef<NevercIRMetadataHandle> OperandArray;
  bool IsDistinct = false;
  if (!arrayRef(Operands, OperandCount, &OperandArray) ||
      !booleanValue(Distinct, &IsDistinct))
    return coreStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return writeExpected(Bridge->getMetadataNode(OperandArray, IsDistinct),
                       OutMetadata);
}

NevercStatus NEVERC_CALL GetValueAsMetadata(
    void *Context, NevercTaskHandle Task, NevercIRValueHandle Value,
    NevercIRMetadataHandle *OutMetadata) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return writeExpected(Bridge->getValueAsMetadata(Value), OutMetadata);
}

NevercStatus NEVERC_CALL GetMetadataAsValue(
    void *Context, NevercTaskHandle Task, NevercIRMetadataHandle Metadata,
    NevercIRValueHandle *OutValue) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return writeExpected(Bridge->getMetadataAsValue(Metadata), OutValue);
}

NevercStatus NEVERC_CALL GetMetadataOperandCount(
    void *Context, NevercTaskHandle Task, NevercIRMetadataHandle Metadata,
    uint64_t *OutCount) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return Bridge->getMetadataOperandCount(Metadata, OutCount);
}

NevercStatus NEVERC_CALL GetMetadataOperand(
    void *Context, NevercTaskHandle Task, NevercIRMetadataHandle Metadata,
    uint64_t Index, NevercIRMetadataHandle *OutOperand) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return writeExpected(Bridge->getMetadataOperand(Metadata, Index),
                       OutOperand);
}

NevercStatus NEVERC_CALL GetOrInsertNamedMetadata(
    void *Context, NevercTaskHandle Task, NevercStringView Name,
    NevercIRNamedMetadataHandle *OutMetadata) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  NevercStatus MutationStatus = Bridge->checkMutationAllowed();
  if (MutationStatus.Code != NEVERC_STATUS_OK)
    return MutationStatus;
  StringRef NameRef;
  if (!stringRef(Name, &NameRef))
    return coreStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return writeExpected(Bridge->getOrInsertNamedMetadata(NameRef), OutMetadata);
}

NevercStatus NEVERC_CALL AppendNamedMetadata(
    void *Context, NevercTaskHandle Task,
    NevercIRNamedMetadataHandle NamedMetadata,
    NevercIRMetadataHandle Metadata) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return Bridge->appendNamedMetadata(NamedMetadata, Metadata);
}

NevercStatus NEVERC_CALL GetNamedMetadataOperandCount(
    void *Context, NevercTaskHandle Task,
    NevercIRNamedMetadataHandle NamedMetadata, uint64_t *OutCount) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return Bridge->getNamedMetadataOperandCount(NamedMetadata, OutCount);
}

NevercStatus NEVERC_CALL GetNamedMetadataOperand(
    void *Context, NevercTaskHandle Task,
    NevercIRNamedMetadataHandle NamedMetadata, uint64_t Index,
    NevercIRMetadataHandle *OutOperand) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return writeExpected(
      Bridge->getNamedMetadataOperand(NamedMetadata, Index), OutOperand);
}

NevercStatus NEVERC_CALL GetDebugLocationInfo(
    void *Context, NevercTaskHandle Task, NevercIRMetadataHandle Location,
    NevercIRDebugLocationInfo *OutInfo) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return Bridge->getDebugLocationInfo(Location, OutInfo);
}

NevercStatus NEVERC_CALL CreateEnumAttribute(
    void *Context, NevercTaskHandle Task, NevercStringView Kind,
    NevercIRAttributeHandle *OutAttribute) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  StringRef KindRef;
  if (!stringRef(Kind, &KindRef))
    return coreStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return writeExpected(Bridge->createEnumAttribute(KindRef), OutAttribute);
}

NevercStatus NEVERC_CALL CreateIntegerAttribute(
    void *Context, NevercTaskHandle Task, NevercStringView Kind,
    uint64_t Value, NevercIRAttributeHandle *OutAttribute) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  StringRef KindRef;
  if (!stringRef(Kind, &KindRef))
    return coreStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return writeExpected(Bridge->createIntegerAttribute(KindRef, Value),
                       OutAttribute);
}

NevercStatus NEVERC_CALL CreateStringAttribute(
    void *Context, NevercTaskHandle Task, NevercStringView Kind,
    NevercStringView Value, NevercIRAttributeHandle *OutAttribute) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  StringRef KindRef;
  StringRef ValueRef;
  if (!stringRef(Kind, &KindRef) || !stringRef(Value, &ValueRef))
    return coreStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return writeExpected(Bridge->createStringAttribute(KindRef, ValueRef),
                       OutAttribute);
}

NevercStatus NEVERC_CALL CreateTypeAttribute(
    void *Context, NevercTaskHandle Task, NevercStringView Kind,
    NevercIRTypeHandle Type, NevercIRAttributeHandle *OutAttribute) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  StringRef KindRef;
  if (!stringRef(Kind, &KindRef))
    return coreStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return writeExpected(Bridge->createTypeAttribute(KindRef, Type),
                       OutAttribute);
}

NevercStatus NEVERC_CALL GetAttributeValueKind(
    void *Context, NevercTaskHandle Task, NevercIRAttributeHandle Attribute,
    NevercIRAttributeValueKind *OutKind) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return Bridge->getAttributeValueKind(Attribute, OutKind);
}

NevercStatus NEVERC_CALL GetAttributeKindName(
    void *Context, NevercTaskHandle Task, NevercIRAttributeHandle Attribute,
    NevercStringView *OutName) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  if (!OutName)
    return coreStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutName = {};
  auto Name = Bridge->getAttributeKindName(Attribute);
  if (!Name) {
    consumeError(Name.takeError());
    return coreStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  }
  OutName->Data = Name->data();
  OutName->Length = Name->size();
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL GetAttributeIntegerValue(
    void *Context, NevercTaskHandle Task, NevercIRAttributeHandle Attribute,
    uint64_t *OutValue) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return writeExpected(Bridge->getAttributeIntegerValue(Attribute), OutValue);
}

NevercStatus NEVERC_CALL GetAttributeStringValue(
    void *Context, NevercTaskHandle Task, NevercIRAttributeHandle Attribute,
    NevercStringView *OutValue) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  if (!OutValue)
    return coreStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutValue = {};
  auto Value = Bridge->getAttributeStringValue(Attribute);
  if (!Value) {
    consumeError(Value.takeError());
    return coreStatus(NEVERC_STATUS_WRONG_TYPE);
  }
  OutValue->Data = Value->data();
  OutValue->Length = Value->size();
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL GetAttributeTypeValue(
    void *Context, NevercTaskHandle Task, NevercIRAttributeHandle Attribute,
    NevercIRTypeHandle *OutType) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return writeExpected(Bridge->getAttributeTypeValue(Attribute), OutType);
}

NevercStatus NEVERC_CALL AddFunctionAttribute(
    void *Context, NevercTaskHandle Task, NevercIRValueHandle Function,
    NevercIRAttributeLocation Location, uint32_t ParameterIndex,
    NevercIRAttributeHandle Attribute) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return Bridge->addFunctionAttribute(Function, Location, ParameterIndex,
                                      Attribute);
}

NevercStatus NEVERC_CALL HasFunctionAttribute(
    void *Context, NevercTaskHandle Task, NevercIRValueHandle Function,
    NevercStringView Kind, NevercBool *OutPresent) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  StringRef KindName;
  if (!stringRef(Kind, &KindName))
    return coreStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return Bridge->hasFunctionAttribute(Function, KindName, OutPresent);
}

NevercStatus NEVERC_CALL GetFunctionStringAttribute(
    void *Context, NevercTaskHandle Task, NevercIRValueHandle Function,
    NevercStringView Kind, NevercStringView *OutValue) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  StringRef KindName;
  if (!stringRef(Kind, &KindName))
    return coreStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return writeExpectedString(
      Bridge->getFunctionStringAttribute(Function, KindName), OutValue);
}

NevercStatus NEVERC_CALL GetModuleIdentifier(
    void *Context, NevercTaskHandle Task, NevercStringView *OutIdentifier) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return writeStringView(Bridge->getModuleIdentifier(), OutIdentifier);
}

NevercStatus NEVERC_CALL SetModuleIdentifier(
    void *Context, NevercTaskHandle Task, NevercStringView Identifier) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  StringRef Value;
  if (!stringRef(Identifier, &Value))
    return coreStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return Bridge->setModuleIdentifier(Value);
}

NevercStatus NEVERC_CALL GetModuleTargetTriple(
    void *Context, NevercTaskHandle Task, NevercStringView *OutTriple) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return writeStringView(Bridge->getModuleTargetTriple(), OutTriple);
}

NevercStatus NEVERC_CALL SetModuleTargetTriple(
    void *Context, NevercTaskHandle Task, NevercStringView Triple) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  StringRef Value;
  if (!stringRef(Triple, &Value))
    return coreStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return Bridge->setModuleTargetTriple(Value);
}

NevercStatus NEVERC_CALL GetModuleDataLayout(
    void *Context, NevercTaskHandle Task, NevercStringView *OutDataLayout) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return writeStringView(Bridge->getModuleDataLayout(), OutDataLayout);
}

NevercStatus NEVERC_CALL SetModuleDataLayout(
    void *Context, NevercTaskHandle Task, NevercStringView DataLayout) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  StringRef Value;
  if (!stringRef(DataLayout, &Value))
    return coreStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return Bridge->setModuleDataLayout(Value);
}

NevercStatus NEVERC_CALL GetModuleInlineAssembly(
    void *Context, NevercTaskHandle Task, NevercStringView *OutAssembly) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return writeStringView(Bridge->getModuleInlineAssembly(), OutAssembly);
}

NevercStatus NEVERC_CALL SetModuleInlineAssembly(
    void *Context, NevercTaskHandle Task, NevercStringView Assembly) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  StringRef Value;
  if (!stringRef(Assembly, &Value))
    return coreStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return Bridge->setModuleInlineAssembly(Value);
}

NevercStatus NEVERC_CALL BeginValueCursor(
    void *Context, NevercTaskHandle Task, NevercHandle Container,
    NevercIRValueCollection Collection, NevercIRValueCursor *OutCursor) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return Bridge->beginValueCursor(Container, Collection, OutCursor);
}

NevercStatus NEVERC_CALL CollectValueCursor(
    void *Context, NevercTaskHandle Task, NevercIRValueCursor *Cursor,
    NevercIRValueHandle *OutValues, uint64_t Capacity, uint64_t *OutCount) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  if ((Capacity != 0 && !OutValues) ||
      Capacity > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
    return coreStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return Bridge->collectValueCursor(
      Cursor, MutableArrayRef<NevercIRValueHandle>(
                  OutValues, static_cast<size_t>(Capacity)),
      OutCount);
}

NevercStatus NEVERC_CALL GetValueName(
    void *Context, NevercTaskHandle Task, NevercIRValueHandle Value,
    NevercStringView *OutName) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return writeExpectedString(Bridge->getValueName(Value), OutName);
}

NevercStatus NEVERC_CALL SetValueName(
    void *Context, NevercTaskHandle Task, NevercIRValueHandle Value,
    NevercStringView Name) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  StringRef NameRef;
  if (!stringRef(Name, &NameRef))
    return coreStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return Bridge->setValueName(Value, NameRef);
}

NevercStatus NEVERC_CALL GetValueType(
    void *Context, NevercTaskHandle Task, NevercIRValueHandle Value,
    NevercIRTypeHandle *OutType) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return writeExpected(Bridge->getValueType(Value), OutType);
}

NevercStatus NEVERC_CALL GetValueUseCount(
    void *Context, NevercTaskHandle Task, NevercIRValueHandle Value,
    uint64_t *OutCount) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return Bridge->getValueUseCount(Value, OutCount);
}

NevercStatus NEVERC_CALL GetValueUse(
    void *Context, NevercTaskHandle Task, NevercIRValueHandle Value,
    uint64_t Index, NevercIRUseInfo *OutUse) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return Bridge->getValueUse(Value, Index, OutUse);
}

NevercStatus NEVERC_CALL GetOperandCount(
    void *Context, NevercTaskHandle Task, NevercIRValueHandle Value,
    uint64_t *OutCount) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return Bridge->getOperandCount(Value, OutCount);
}

NevercStatus NEVERC_CALL GetOperand(
    void *Context, NevercTaskHandle Task, NevercIRValueHandle Value,
    uint64_t Index, NevercIRValueHandle *OutOperand) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return writeExpected(Bridge->getOperand(Value, Index), OutOperand);
}

NevercStatus NEVERC_CALL SetOperand(
    void *Context, NevercTaskHandle Task, NevercIRValueHandle Value,
    uint64_t Index, NevercIRValueHandle Operand) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return Bridge->setOperand(Value, Index, Operand);
}

NevercStatus NEVERC_CALL GetGlobalLinkage(
    void *Context, NevercTaskHandle Task, NevercIRValueHandle Global,
    NevercIRLinkage *OutLinkage) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return Bridge->getGlobalLinkage(Global, OutLinkage);
}

NevercStatus NEVERC_CALL SetGlobalLinkage(
    void *Context, NevercTaskHandle Task, NevercIRValueHandle Global,
    NevercIRLinkage Linkage) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return Bridge->setGlobalLinkage(Global, Linkage);
}

NevercStatus NEVERC_CALL GetGlobalVisibility(
    void *Context, NevercTaskHandle Task, NevercIRValueHandle Global,
    NevercIRVisibility *OutVisibility) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return Bridge->getGlobalVisibility(Global, OutVisibility);
}

NevercStatus NEVERC_CALL SetGlobalVisibility(
    void *Context, NevercTaskHandle Task, NevercIRValueHandle Global,
    NevercIRVisibility Visibility) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return Bridge->setGlobalVisibility(Global, Visibility);
}

NevercStatus NEVERC_CALL GetGlobalSection(
    void *Context, NevercTaskHandle Task, NevercIRValueHandle Global,
    NevercStringView *OutSection) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return writeExpectedString(Bridge->getGlobalSection(Global), OutSection);
}

NevercStatus NEVERC_CALL SetGlobalSection(
    void *Context, NevercTaskHandle Task, NevercIRValueHandle Global,
    NevercStringView Section) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  StringRef SectionRef;
  if (!stringRef(Section, &SectionRef))
    return coreStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return Bridge->setGlobalSection(Global, SectionRef);
}

NevercStatus NEVERC_CALL GetOrInsertComdat(
    void *Context, NevercTaskHandle Task, NevercStringView Name,
    NevercIRComdatHandle *OutComdat) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  NevercStatus MutationStatus = Bridge->checkMutationAllowed();
  if (MutationStatus.Code != NEVERC_STATUS_OK)
    return MutationStatus;
  StringRef NameRef;
  if (!stringRef(Name, &NameRef))
    return coreStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return writeExpected(Bridge->getOrInsertComdat(NameRef), OutComdat);
}

NevercStatus NEVERC_CALL GetGlobalComdat(
    void *Context, NevercTaskHandle Task, NevercIRValueHandle Global,
    NevercIRComdatHandle *OutComdat) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return writeExpected(Bridge->getGlobalComdat(Global), OutComdat);
}

NevercStatus NEVERC_CALL SetGlobalComdat(
    void *Context, NevercTaskHandle Task, NevercIRValueHandle Global,
    NevercIRComdatHandle Comdat) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return Bridge->setGlobalComdat(Global, Comdat);
}

NevercStatus NEVERC_CALL GetFunctionCallingConvention(
    void *Context, NevercTaskHandle Task, NevercIRValueHandle Function,
    NevercIRCallingConvention *OutCallingConvention) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return Bridge->getFunctionCallingConvention(Function,
                                               OutCallingConvention);
}

NevercStatus NEVERC_CALL SetFunctionCallingConvention(
    void *Context, NevercTaskHandle Task, NevercIRValueHandle Function,
    NevercIRCallingConvention CallingConvention) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return Bridge->setFunctionCallingConvention(Function, CallingConvention);
}

NevercStatus NEVERC_CALL GetFunctionPersonality(
    void *Context, NevercTaskHandle Task, NevercIRValueHandle Function,
    NevercIRValueHandle *OutPersonality) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return writeExpected(Bridge->getFunctionPersonality(Function),
                       OutPersonality);
}

NevercStatus NEVERC_CALL SetFunctionPersonality(
    void *Context, NevercTaskHandle Task, NevercIRValueHandle Function,
    NevercIRValueHandle Personality) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return Bridge->setFunctionPersonality(Function, Personality);
}

NevercStatus NEVERC_CALL GetFunctionGC(
    void *Context, NevercTaskHandle Task, NevercIRValueHandle Function,
    NevercStringView *OutGC) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return writeExpectedString(Bridge->getFunctionGC(Function), OutGC);
}

NevercStatus NEVERC_CALL SetFunctionGC(
    void *Context, NevercTaskHandle Task, NevercIRValueHandle Function,
    NevercStringView GC) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  StringRef GCRef;
  if (!stringRef(GC, &GCRef))
    return coreStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return Bridge->setFunctionGC(Function, GCRef);
}

NevercStatus NEVERC_CALL GetFunctionSection(
    void *Context, NevercTaskHandle Task, NevercIRValueHandle Function,
    NevercStringView *OutSection) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return writeExpectedString(Bridge->getFunctionSection(Function),
                             OutSection);
}

NevercStatus NEVERC_CALL SetFunctionSection(
    void *Context, NevercTaskHandle Task, NevercIRValueHandle Function,
    NevercStringView Section) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  StringRef SectionRef;
  if (!stringRef(Section, &SectionRef))
    return coreStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  return Bridge->setFunctionSection(Function, SectionRef);
}

NevercStatus NEVERC_CALL GetTerminator(
    void *Context, NevercTaskHandle Task, NevercIRValueHandle Block,
    NevercIRValueHandle *OutTerminator) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return writeExpected(Bridge->getTerminator(Block), OutTerminator);
}

NevercStatus NEVERC_CALL GetPredecessorCount(
    void *Context, NevercTaskHandle Task, NevercIRValueHandle Block,
    uint64_t *OutCount) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return Bridge->getPredecessorCount(Block, OutCount);
}

NevercStatus NEVERC_CALL GetPredecessor(
    void *Context, NevercTaskHandle Task, NevercIRValueHandle Block,
    uint64_t Index, NevercIRValueHandle *OutPredecessor) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return writeExpected(Bridge->getPredecessor(Block, Index),
                       OutPredecessor);
}

NevercStatus NEVERC_CALL GetSuccessorCount(
    void *Context, NevercTaskHandle Task, NevercIRValueHandle Block,
    uint64_t *OutCount) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return Bridge->getSuccessorCount(Block, OutCount);
}

NevercStatus NEVERC_CALL GetSuccessor(
    void *Context, NevercTaskHandle Task, NevercIRValueHandle Block,
    uint64_t Index, NevercIRValueHandle *OutSuccessor) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return writeExpected(Bridge->getSuccessor(Block, Index), OutSuccessor);
}

NevercStatus NEVERC_CALL GetInstructionOpcode(
    void *Context, NevercTaskHandle Task,
    NevercIRValueHandle Instruction, NevercIROpcode *OutOpcode) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return Bridge->getInstructionOpcode(Instruction, OutOpcode);
}

NevercStatus NEVERC_CALL GetInstructionProperty(
    void *Context, NevercTaskHandle Task,
    NevercIRValueHandle Instruction, NevercIRPropertyID Property,
    NevercIRPropertyValue *OutValue) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return Bridge->getInstructionProperty(Instruction, Property, OutValue);
}

NevercStatus NEVERC_CALL SetInstructionProperty(
    void *Context, NevercTaskHandle Task,
    NevercIRValueHandle Instruction, NevercIRPropertyID Property,
    NevercIRPropertyValue Value) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return Bridge->setInstructionProperty(Instruction, Property, Value);
}

NevercStatus NEVERC_CALL GetPHIIncomingCount(
    void *Context, NevercTaskHandle Task, NevercIRValueHandle Phi,
    uint64_t *OutCount) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return Bridge->getPHIIncomingCount(Phi, OutCount);
}

NevercStatus NEVERC_CALL GetPHIIncoming(
    void *Context, NevercTaskHandle Task, NevercIRValueHandle Phi,
    uint64_t Index, NevercIRPhiIncoming *OutIncoming) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return Bridge->getPHIIncoming(Phi, Index, OutIncoming);
}

NevercStatus NEVERC_CALL SetPHIIncoming(
    void *Context, NevercTaskHandle Task, NevercIRValueHandle Phi,
    uint64_t Index, NevercIRPhiIncoming Incoming) {
  NEVERC_IR_BRIDGE_OR_RETURN();
  return Bridge->setPHIIncoming(Phi, Index, Incoming);
}

#undef NEVERC_IR_BRIDGE_OR_RETURN

} // namespace

void IRPluginBridge::initializeCoreAPI() {
  CoreAPI.Header = {sizeof(CoreAPI), NEVERC_IR_CORE_API_MAJOR,
                    NEVERC_IR_CORE_API_MINOR, 0};
  CoreAPI.Context = this;
  CoreAPI.GetContext = GetContext;
  CoreAPI.GetModule = GetModule;
  CoreAPI.GetValueKind = GetValueKind;
  CoreAPI.ReplaceAllUsesWith = ReplaceAllUsesWith;
  CoreAPI.EraseValue = EraseValue;
  CoreAPI.GetTypeKind = GetTypeKind;
  CoreAPI.GetPrimitiveType = GetPrimitiveType;
  CoreAPI.GetIntegerType = GetIntegerType;
  CoreAPI.GetPointerType = GetPointerType;
  CoreAPI.GetArrayType = GetArrayType;
  CoreAPI.GetVectorType = GetVectorType;
  CoreAPI.GetStructType = GetStructType;
  CoreAPI.GetFunctionType = GetFunctionType;
  CoreAPI.CreateIntegerConstant = CreateIntegerConstant;
  CoreAPI.CreateFloatingConstant = CreateFloatingConstant;
  CoreAPI.GetNullConstant = GetNullConstant;
  CoreAPI.GetPoisonConstant = GetPoisonConstant;
  CoreAPI.GetUndefConstant = GetUndefConstant;
  CoreAPI.CreateAggregateConstant = CreateAggregateConstant;
  CoreAPI.CreateConstantBinaryExpression = CreateConstantBinaryExpression;
  CoreAPI.CreateConstantCastExpression = CreateConstantCastExpression;
  CoreAPI.CreateConstantCompareExpression = CreateConstantCompareExpression;
  CoreAPI.CreateConstantGEPExpression = CreateConstantGEPExpression;
  CoreAPI.GetGlobalAddressConstant = GetGlobalAddressConstant;
  CoreAPI.GetMetadataKind = GetMetadataKind;
  CoreAPI.CreateMetadataString = CreateMetadataString;
  CoreAPI.GetMetadataStringBytes = GetMetadataStringBytes;
  CoreAPI.CreateMetadataNode = CreateMetadataNode;
  CoreAPI.GetValueAsMetadata = GetValueAsMetadata;
  CoreAPI.GetMetadataAsValue = GetMetadataAsValue;
  CoreAPI.GetMetadataOperandCount = GetMetadataOperandCount;
  CoreAPI.GetMetadataOperand = GetMetadataOperand;
  CoreAPI.GetOrInsertNamedMetadata = GetOrInsertNamedMetadata;
  CoreAPI.AppendNamedMetadata = AppendNamedMetadata;
  CoreAPI.GetNamedMetadataOperandCount = GetNamedMetadataOperandCount;
  CoreAPI.GetNamedMetadataOperand = GetNamedMetadataOperand;
  CoreAPI.GetDebugLocationInfo = GetDebugLocationInfo;
  CoreAPI.CreateEnumAttribute = CreateEnumAttribute;
  CoreAPI.CreateIntegerAttribute = CreateIntegerAttribute;
  CoreAPI.CreateStringAttribute = CreateStringAttribute;
  CoreAPI.CreateTypeAttribute = CreateTypeAttribute;
  CoreAPI.GetAttributeValueKind = GetAttributeValueKind;
  CoreAPI.GetAttributeKindName = GetAttributeKindName;
  CoreAPI.GetAttributeIntegerValue = GetAttributeIntegerValue;
  CoreAPI.GetAttributeStringValue = GetAttributeStringValue;
  CoreAPI.GetAttributeTypeValue = GetAttributeTypeValue;
  CoreAPI.AddFunctionAttribute = AddFunctionAttribute;
  CoreAPI.HasFunctionAttribute = HasFunctionAttribute;
  CoreAPI.GetFunctionStringAttribute = GetFunctionStringAttribute;
  CoreAPI.GetModuleIdentifier = GetModuleIdentifier;
  CoreAPI.SetModuleIdentifier = SetModuleIdentifier;
  CoreAPI.GetModuleTargetTriple = GetModuleTargetTriple;
  CoreAPI.SetModuleTargetTriple = SetModuleTargetTriple;
  CoreAPI.GetModuleDataLayout = GetModuleDataLayout;
  CoreAPI.SetModuleDataLayout = SetModuleDataLayout;
  CoreAPI.GetModuleInlineAssembly = GetModuleInlineAssembly;
  CoreAPI.SetModuleInlineAssembly = SetModuleInlineAssembly;
  CoreAPI.BeginValueCursor = BeginValueCursor;
  CoreAPI.CollectValueCursor = CollectValueCursor;
  CoreAPI.GetValueName = GetValueName;
  CoreAPI.SetValueName = SetValueName;
  CoreAPI.GetValueType = GetValueType;
  CoreAPI.GetValueUseCount = GetValueUseCount;
  CoreAPI.GetValueUse = GetValueUse;
  CoreAPI.GetOperandCount = GetOperandCount;
  CoreAPI.GetOperand = GetOperand;
  CoreAPI.SetOperand = SetOperand;
  CoreAPI.GetGlobalLinkage = GetGlobalLinkage;
  CoreAPI.SetGlobalLinkage = SetGlobalLinkage;
  CoreAPI.GetGlobalVisibility = GetGlobalVisibility;
  CoreAPI.SetGlobalVisibility = SetGlobalVisibility;
  CoreAPI.GetGlobalSection = GetGlobalSection;
  CoreAPI.SetGlobalSection = SetGlobalSection;
  CoreAPI.GetOrInsertComdat = GetOrInsertComdat;
  CoreAPI.GetGlobalComdat = GetGlobalComdat;
  CoreAPI.SetGlobalComdat = SetGlobalComdat;
  CoreAPI.GetFunctionCallingConvention = GetFunctionCallingConvention;
  CoreAPI.SetFunctionCallingConvention = SetFunctionCallingConvention;
  CoreAPI.GetFunctionPersonality = GetFunctionPersonality;
  CoreAPI.SetFunctionPersonality = SetFunctionPersonality;
  CoreAPI.GetFunctionGC = GetFunctionGC;
  CoreAPI.SetFunctionGC = SetFunctionGC;
  CoreAPI.GetFunctionSection = GetFunctionSection;
  CoreAPI.SetFunctionSection = SetFunctionSection;
  CoreAPI.GetTerminator = GetTerminator;
  CoreAPI.GetPredecessorCount = GetPredecessorCount;
  CoreAPI.GetPredecessor = GetPredecessor;
  CoreAPI.GetSuccessorCount = GetSuccessorCount;
  CoreAPI.GetSuccessor = GetSuccessor;
  CoreAPI.GetInstructionOpcode = GetInstructionOpcode;
  CoreAPI.GetInstructionProperty = GetInstructionProperty;
  CoreAPI.SetInstructionProperty = SetInstructionProperty;
  CoreAPI.GetPHIIncomingCount = GetPHIIncomingCount;
  CoreAPI.GetPHIIncoming = GetPHIIncoming;
  CoreAPI.SetPHIIncoming = SetPHIIncoming;
}

} // namespace neverc::plugin
