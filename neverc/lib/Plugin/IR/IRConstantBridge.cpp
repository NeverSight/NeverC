#include "neverc/Plugin/Host/IRPluginBridge.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Instructions.h"
#include <optional>

using namespace llvm;

namespace neverc::plugin {
namespace {

Error constantError(const Twine &Message) {
  return createStringError(inconvertibleErrorCode(), Message);
}

std::optional<unsigned> decodeOpcode(NevercIROpcode Opcode) {
  switch (Opcode) {
#define NEVERC_IR_SCHEMA_INTERNAL_OPCODE(Internal, Symbol, ID)                \
  case ID:                                                                    \
    return Instruction::Internal;
#include "neverc/Plugin/Schema/PluginIRSchema.inc"
#undef NEVERC_IR_SCHEMA_INTERNAL_OPCODE
  default:
    return std::nullopt;
  }
}

std::optional<CmpInst::Predicate>
decodePredicate(NevercIRPredicate Predicate) {
  switch (Predicate) {
#define NEVERC_IR_SCHEMA_INTERNAL_PREDICATE(Internal, Symbol, ID)             \
  case ID:                                                                    \
    return CmpInst::Internal;
#include "neverc/Plugin/Schema/PluginIRSchema.inc"
#undef NEVERC_IR_SCHEMA_INTERNAL_PREDICATE
  default:
    return std::nullopt;
  }
}

Expected<Constant *> resolveConstant(IRPluginBridge &Bridge,
                                     NevercIRValueHandle Handle) {
  Value *Resolved = nullptr;
  NevercStatus Status = Bridge.resolveValue(Handle, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return constantError("IR constant handle is invalid");
  auto *Result = dyn_cast<Constant>(Resolved);
  if (!Result)
    return constantError("IR value is not a constant");
  return Result;
}

bool supportsNullConstant(Type *Value) {
  switch (Value->getTypeID()) {
  case Type::IntegerTyID:
  case Type::HalfTyID:
  case Type::BFloatTyID:
  case Type::FloatTyID:
  case Type::DoubleTyID:
  case Type::X86_FP80TyID:
  case Type::FP128TyID:
  case Type::PPC_FP128TyID:
  case Type::PointerTyID:
  case Type::StructTyID:
  case Type::ArrayTyID:
  case Type::FixedVectorTyID:
  case Type::ScalableVectorTyID:
  case Type::TokenTyID:
  case Type::TargetExtTyID:
    return true;
  default:
    return false;
  }
}

} // namespace

Expected<NevercIRValueHandle> IRPluginBridge::getIntegerConstant(
    NevercIRTypeHandle TypeHandle, ArrayRef<uint64_t> LittleEndianWords) {
  Type *Resolved = nullptr;
  NevercStatus Status = resolveType(TypeHandle, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return constantError("IR integer constant type handle is invalid");
  auto *Integer = dyn_cast<IntegerType>(Resolved);
  if (!Integer)
    return constantError("IR integer constant requires an integer type");
  APInt Bits(Integer->getBitWidth(), LittleEndianWords);
  return wrapValue(*ConstantInt::get(*Context, Bits));
}

Expected<NevercIRValueHandle> IRPluginBridge::getFloatingConstant(
    NevercIRTypeHandle TypeHandle, ArrayRef<uint64_t> LittleEndianWords) {
  Type *Resolved = nullptr;
  NevercStatus Status = resolveType(TypeHandle, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return constantError("IR floating constant type handle is invalid");
  if (!Resolved->isFloatingPointTy())
    return constantError("IR floating constant requires a floating type");
  const fltSemantics &Semantics = Resolved->getFltSemantics();
  APInt Bits(APFloat::getSizeInBits(Semantics), LittleEndianWords);
  APFloat Number(Semantics, Bits);
  return wrapValue(*ConstantFP::get(*Context, Number));
}

Expected<NevercIRValueHandle>
IRPluginBridge::getNullConstant(NevercIRTypeHandle TypeHandle) {
  Type *Resolved = nullptr;
  NevercStatus Status = resolveType(TypeHandle, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return constantError("IR null constant type handle is invalid");
  if (!supportsNullConstant(Resolved))
    return constantError("IR type does not support a null constant");
  return wrapValue(*Constant::getNullValue(Resolved));
}

Expected<NevercIRValueHandle>
IRPluginBridge::getPoisonConstant(NevercIRTypeHandle TypeHandle) {
  Type *Resolved = nullptr;
  NevercStatus Status = resolveType(TypeHandle, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return constantError("IR poison constant type handle is invalid");
  if (!Resolved->isFirstClassType() || Resolved->isMetadataTy() ||
      Resolved->isLabelTy())
    return constantError("IR type does not support a poison constant");
  return wrapValue(*PoisonValue::get(Resolved));
}

Expected<NevercIRValueHandle>
IRPluginBridge::getUndefConstant(NevercIRTypeHandle TypeHandle) {
  Type *Resolved = nullptr;
  NevercStatus Status = resolveType(TypeHandle, &Resolved);
  if (Status.Code != NEVERC_STATUS_OK)
    return constantError("IR undef constant type handle is invalid");
  if (!Resolved->isFirstClassType() || Resolved->isMetadataTy() ||
      Resolved->isLabelTy())
    return constantError("IR type does not support an undef constant");
  return wrapValue(*UndefValue::get(Resolved));
}

Expected<NevercIRValueHandle> IRPluginBridge::getAggregateConstant(
    NevercIRTypeHandle TypeHandle,
    ArrayRef<NevercIRValueHandle> ElementHandles) {
  Type *ResolvedType = nullptr;
  NevercStatus Status = resolveType(TypeHandle, &ResolvedType);
  if (Status.Code != NEVERC_STATUS_OK)
    return constantError("IR aggregate constant type handle is invalid");
  SmallVector<Constant *, 8> Elements;
  Elements.reserve(ElementHandles.size());
  for (NevercIRValueHandle Handle : ElementHandles) {
    auto Element = resolveConstant(*this, Handle);
    if (!Element)
      return Element.takeError();
    Elements.push_back(*Element);
  }

  Constant *Result = nullptr;
  if (auto *Array = dyn_cast<ArrayType>(ResolvedType)) {
    if (Elements.size() != Array->getNumElements())
      return constantError("IR array constant element count does not match");
    for (Constant *Element : Elements)
      if (Element->getType() != Array->getElementType())
        return constantError("IR array constant element type does not match");
    Result = ConstantArray::get(Array, Elements);
  } else if (auto *Structure = dyn_cast<StructType>(ResolvedType)) {
    if (Elements.size() != Structure->getNumElements())
      return constantError("IR structure constant element count does not match");
    for (size_t Index = 0; Index != Elements.size(); ++Index)
      if (Elements[Index]->getType() !=
          Structure->getElementType(static_cast<unsigned>(Index)))
        return constantError(
            "IR structure constant element type does not match");
    Result = ConstantStruct::get(Structure, Elements);
  } else if (auto *Vector = dyn_cast<VectorType>(ResolvedType)) {
    if (Elements.size() != Vector->getElementCount().getKnownMinValue())
      return constantError("IR vector constant element count does not match");
    for (Constant *Element : Elements)
      if (Element->getType() != Vector->getElementType())
        return constantError("IR vector constant element type does not match");
    if (Vector->getElementCount().isScalable()) {
      if (Elements.empty())
        return constantError("IR scalable vector constant cannot be empty");
      for (Constant *Element : drop_begin(Elements))
        if (Element != Elements.front())
          return constantError(
              "IR scalable vector constants must be splats");
      Result =
          ConstantVector::getSplat(Vector->getElementCount(), Elements.front());
    } else {
      Result = ConstantVector::get(Elements);
    }
  } else {
    return constantError("IR constant type is not an aggregate type");
  }
  if (Result->getType() != ResolvedType)
    return constantError("IR aggregate constant produced an unexpected type");
  return wrapValue(*Result);
}

Expected<NevercIRValueHandle>
IRPluginBridge::getConstantBinaryExpression(NevercIROpcode Opcode,
                                            NevercIRValueHandle Left,
                                            NevercIRValueHandle Right) {
  auto InternalOpcode = decodeOpcode(Opcode);
  if (!InternalOpcode || !Instruction::isBinaryOp(*InternalOpcode))
    return constantError("IR opcode is not a binary constant expression");
  auto LeftConstant = resolveConstant(*this, Left);
  if (!LeftConstant)
    return LeftConstant.takeError();
  auto RightConstant = resolveConstant(*this, Right);
  if (!RightConstant)
    return RightConstant.takeError();
  Type *OperandType = (*LeftConstant)->getType();
  if (OperandType != (*RightConstant)->getType())
    return constantError("IR binary constant operand types do not match");
  bool IsFloating = *InternalOpcode == Instruction::FAdd ||
                    *InternalOpcode == Instruction::FSub ||
                    *InternalOpcode == Instruction::FMul ||
                    *InternalOpcode == Instruction::FDiv ||
                    *InternalOpcode == Instruction::FRem;
  if ((IsFloating && !OperandType->isFPOrFPVectorTy()) ||
      (!IsFloating && !OperandType->isIntOrIntVectorTy()))
    return constantError("IR binary constant operand type is invalid");
  return wrapValue(
      *ConstantExpr::get(*InternalOpcode, *LeftConstant, *RightConstant));
}

Expected<NevercIRValueHandle>
IRPluginBridge::getConstantCastExpression(
    NevercIROpcode Opcode, NevercIRValueHandle Operand,
    NevercIRTypeHandle DestinationType) {
  auto InternalOpcode = decodeOpcode(Opcode);
  if (!InternalOpcode || !Instruction::isCast(*InternalOpcode))
    return constantError("IR opcode is not a cast constant expression");
  auto ConstantOperand = resolveConstant(*this, Operand);
  if (!ConstantOperand)
    return ConstantOperand.takeError();
  Type *Destination = nullptr;
  NevercStatus Status = resolveType(DestinationType, &Destination);
  if (Status.Code != NEVERC_STATUS_OK)
    return constantError("IR cast destination type handle is invalid");
  auto CastOpcode = static_cast<Instruction::CastOps>(*InternalOpcode);
  if (!CastInst::castIsValid(CastOpcode, *ConstantOperand, Destination))
    return constantError("IR constant cast is not valid for these types");
  return wrapValue(
      *ConstantExpr::getCast(CastOpcode, *ConstantOperand, Destination));
}

Expected<NevercIRValueHandle>
IRPluginBridge::getConstantCompareExpression(NevercIRPredicate Predicate,
                                             NevercIRValueHandle Left,
                                             NevercIRValueHandle Right) {
  auto InternalPredicate = decodePredicate(Predicate);
  if (!InternalPredicate)
    return constantError("IR comparison predicate is unknown");
  auto LeftConstant = resolveConstant(*this, Left);
  if (!LeftConstant)
    return LeftConstant.takeError();
  auto RightConstant = resolveConstant(*this, Right);
  if (!RightConstant)
    return RightConstant.takeError();
  Type *OperandType = (*LeftConstant)->getType();
  if (OperandType != (*RightConstant)->getType())
    return constantError("IR comparison operand types do not match");
  if ((CmpInst::isIntPredicate(*InternalPredicate) &&
       !OperandType->isIntOrIntVectorTy() &&
       !OperandType->isPtrOrPtrVectorTy()) ||
      (CmpInst::isFPPredicate(*InternalPredicate) &&
       !OperandType->isFPOrFPVectorTy()))
    return constantError("IR comparison predicate does not match its operands");
  return wrapValue(*ConstantExpr::getCompare(
      *InternalPredicate, *LeftConstant, *RightConstant));
}

Expected<NevercIRValueHandle> IRPluginBridge::getConstantGEPExpression(
    NevercIRTypeHandle SourceElementType, NevercIRValueHandle Pointer,
    ArrayRef<NevercIRValueHandle> IndexHandles, bool InBounds) {
  Type *Source = nullptr;
  NevercStatus Status = resolveType(SourceElementType, &Source);
  if (Status.Code != NEVERC_STATUS_OK || !Source->isSized())
    return constantError("IR GEP source element type is invalid");
  auto PointerConstant = resolveConstant(*this, Pointer);
  if (!PointerConstant)
    return PointerConstant.takeError();
  if (!(*PointerConstant)->getType()->isPointerTy())
    return constantError("IR GEP base is not a pointer constant");
  SmallVector<Constant *, 8> Indices;
  for (NevercIRValueHandle Handle : IndexHandles) {
    auto Index = resolveConstant(*this, Handle);
    if (!Index)
      return Index.takeError();
    if (!(*Index)->getType()->isIntOrIntVectorTy())
      return constantError("IR GEP index is not an integer constant");
    Indices.push_back(*Index);
  }
  SmallVector<Value *, 8> IndexValues(Indices.begin(), Indices.end());
  if (!GetElementPtrInst::getIndexedType(Source, IndexValues))
    return constantError("IR GEP indices are invalid for the source type");
  return wrapValue(*ConstantExpr::getGetElementPtr(
      Source, *PointerConstant, Indices, InBounds));
}

Expected<NevercIRValueHandle>
IRPluginBridge::getGlobalAddressConstant(NevercIRValueHandle Global) {
  auto Resolved = resolveConstant(*this, Global);
  if (!Resolved)
    return Resolved.takeError();
  if (!isa<GlobalValue>(*Resolved))
    return constantError("IR constant is not a global address");
  return Global;
}

} // namespace neverc::plugin
