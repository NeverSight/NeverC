#include "neverc/Plugin/Host/IRPluginBridge.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Type.h"

using namespace llvm;

namespace neverc::plugin {
namespace {

Error typeError(const Twine &Message) {
  return createStringError(inconvertibleErrorCode(), Message);
}

NevercStatus typeStatus(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

} // namespace

NevercStatus IRPluginBridge::getTypeKind(NevercIRTypeHandle Handle,
                                         NevercIRTypeKind *OutKind) const {
  if (!OutKind)
    return typeStatus(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutKind = NEVERC_IR_TYPE_UNKNOWN;
  Type *Value = nullptr;
  NevercStatus Status = resolveType(Handle, &Value);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  switch (Value->getTypeID()) {
#define NEVERC_IR_SCHEMA_INTERNAL_TYPE(Internal, Symbol, ID)                  \
  case Type::Internal:                                                        \
    *OutKind = ID;                                                            \
    return Status;
#include "neverc/Plugin/Schema/PluginIRSchema.inc"
#undef NEVERC_IR_SCHEMA_INTERNAL_TYPE
  }
  return typeStatus(NEVERC_STATUS_VERIFICATION_FAILED);
}

Expected<NevercIRTypeHandle>
IRPluginBridge::getPrimitiveType(NevercIRTypeKind Kind) {
  Type *Value = nullptr;
  switch (Kind) {
  case NEVERC_IR_TYPE_HALF:
    Value = Type::getHalfTy(*Context);
    break;
  case NEVERC_IR_TYPE_B_FLOAT:
    Value = Type::getBFloatTy(*Context);
    break;
  case NEVERC_IR_TYPE_FLOAT:
    Value = Type::getFloatTy(*Context);
    break;
  case NEVERC_IR_TYPE_DOUBLE:
    Value = Type::getDoubleTy(*Context);
    break;
  case NEVERC_IR_TYPE_X86_FP80:
    Value = Type::getX86_FP80Ty(*Context);
    break;
  case NEVERC_IR_TYPE_FP128:
    Value = Type::getFP128Ty(*Context);
    break;
  case NEVERC_IR_TYPE_PPC_FP128:
    Value = Type::getPPC_FP128Ty(*Context);
    break;
  case NEVERC_IR_TYPE_VOID:
    Value = Type::getVoidTy(*Context);
    break;
  case NEVERC_IR_TYPE_LABEL:
    Value = Type::getLabelTy(*Context);
    break;
  case NEVERC_IR_TYPE_METADATA:
    Value = Type::getMetadataTy(*Context);
    break;
  case NEVERC_IR_TYPE_X86_MMX:
    Value = Type::getX86_MMXTy(*Context);
    break;
  case NEVERC_IR_TYPE_X86_AMX:
    Value = Type::getX86_AMXTy(*Context);
    break;
  case NEVERC_IR_TYPE_TOKEN:
    Value = Type::getTokenTy(*Context);
    break;
  default:
    return typeError("IR type kind is not a primitive type");
  }
  return wrapType(*Value);
}

Expected<NevercIRTypeHandle>
IRPluginBridge::getIntegerType(uint32_t BitWidth) {
  if (BitWidth < IntegerType::MIN_INT_BITS ||
      BitWidth > IntegerType::MAX_INT_BITS)
    return typeError("IR integer bit width is out of range");
  return wrapType(*IntegerType::get(*Context, BitWidth));
}

Expected<NevercIRTypeHandle>
IRPluginBridge::getPointerType(uint32_t AddressSpace) {
  return wrapType(*PointerType::get(*Context, AddressSpace));
}

Expected<NevercIRTypeHandle>
IRPluginBridge::getArrayType(NevercIRTypeHandle ElementType,
                             uint64_t ElementCount) {
  Type *Element = nullptr;
  NevercStatus Status = resolveType(ElementType, &Element);
  if (Status.Code != NEVERC_STATUS_OK)
    return typeError("IR array element type handle is invalid");
  if (!ArrayType::isValidElementType(Element))
    return typeError("IR array element type is invalid");
  return wrapType(*ArrayType::get(Element, ElementCount));
}

Expected<NevercIRTypeHandle>
IRPluginBridge::getVectorType(NevercIRTypeHandle ElementType,
                              uint32_t MinimumElementCount,
                              bool Scalable) {
  Type *Element = nullptr;
  NevercStatus Status = resolveType(ElementType, &Element);
  if (Status.Code != NEVERC_STATUS_OK)
    return typeError("IR vector element type handle is invalid");
  if (MinimumElementCount == 0 || !VectorType::isValidElementType(Element))
    return typeError("IR vector element contract is invalid");
  return wrapType(*VectorType::get(
      Element, ElementCount::get(MinimumElementCount, Scalable)));
}

Expected<NevercIRTypeHandle>
IRPluginBridge::getStructType(
    StringRef Name, ArrayRef<NevercIRTypeHandle> ElementTypes,
    bool Packed) {
  if (Name.contains('\0'))
    return typeError("IR structure name contains a null byte");
  SmallVector<Type *, 8> Elements;
  Elements.reserve(ElementTypes.size());
  for (NevercIRTypeHandle Handle : ElementTypes) {
    Type *Element = nullptr;
    NevercStatus Status = resolveType(Handle, &Element);
    if (Status.Code != NEVERC_STATUS_OK ||
        !StructType::isValidElementType(Element))
      return typeError("IR structure element type is invalid");
    Elements.push_back(Element);
  }

  StructType *Structure = nullptr;
  if (Name.empty()) {
    Structure = StructType::get(*Context, Elements, Packed);
  } else {
    if (StructType::getTypeByName(*Context, Name))
      return typeError("IR named structure already exists");
    Structure = StructType::create(*Context, Name);
    Structure->setBody(Elements, Packed);
  }
  return wrapType(*Structure);
}

Expected<NevercIRTypeHandle>
IRPluginBridge::getFunctionType(
    NevercIRTypeHandle ReturnType,
    ArrayRef<NevercIRTypeHandle> ParameterTypes, bool Variadic) {
  Type *Result = nullptr;
  NevercStatus Status = resolveType(ReturnType, &Result);
  if (Status.Code != NEVERC_STATUS_OK ||
      !FunctionType::isValidReturnType(Result))
    return typeError("IR function return type is invalid");
  SmallVector<Type *, 8> Parameters;
  Parameters.reserve(ParameterTypes.size());
  for (NevercIRTypeHandle Handle : ParameterTypes) {
    Type *Parameter = nullptr;
    Status = resolveType(Handle, &Parameter);
    if (Status.Code != NEVERC_STATUS_OK ||
        !FunctionType::isValidArgumentType(Parameter))
      return typeError("IR function parameter type is invalid");
    Parameters.push_back(Parameter);
  }
  return wrapType(*FunctionType::get(Result, Parameters, Variadic));
}

} // namespace neverc::plugin
