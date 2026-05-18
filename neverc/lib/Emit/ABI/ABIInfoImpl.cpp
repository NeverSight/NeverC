#include "ABI/ABIInfoImpl.h"

using namespace neverc;
using namespace neverc::Emit;

namespace neverc::CodeGen {

// ===----------------------------------------------------------------------===
// DefaultABIInfo
// ===----------------------------------------------------------------------===

DefaultABIInfo::~DefaultABIInfo() = default;

ABIArgInfo DefaultABIInfo::classifyArgumentType(QualType Ty) const {
  Ty = useFirstFieldIfTransparentUnion(Ty);

  if (isAggregateTypeForABI(Ty)) {
    // Records requiring indirect passing.
    if (CGABI::RecordArgABI RAA = getRecordArgABI(Ty))
      return getNaturalAlignIndirect(Ty, RAA == CGABI::RAA_DirectInMemory);

    return getNaturalAlignIndirect(Ty);
  }

  // Treat an enum type as its underlying type.
  if (const EnumType *EnumTy = Ty->getAs<EnumType>())
    Ty = EnumTy->getDecl()->getIntegerType();

  TreeContext &Context = getContext();
  if (const auto *EIT = Ty->getAs<BitIntType>())
    if (EIT->getNumBits() >
        Context.getTypeSize(Context.getTargetInfo().hasInt128Type()
                                ? Context.Int128Ty
                                : Context.LongLongTy))
      return getNaturalAlignIndirect(Ty);

  return (isPromotableIntegerTypeForABI(Ty) ? ABIArgInfo::getExtend(Ty)
                                            : ABIArgInfo::getDirect());
}

ABIArgInfo DefaultABIInfo::classifyReturnType(QualType RetTy) const {
  if (RetTy->isVoidType())
    return ABIArgInfo::getIgnore();

  if (isAggregateTypeForABI(RetTy))
    return getNaturalAlignIndirect(RetTy);

  // Treat an enum type as its underlying type.
  if (const EnumType *EnumTy = RetTy->getAs<EnumType>())
    RetTy = EnumTy->getDecl()->getIntegerType();

  if (const auto *EIT = RetTy->getAs<BitIntType>())
    if (EIT->getNumBits() >
        getContext().getTypeSize(getContext().getTargetInfo().hasInt128Type()
                                     ? getContext().Int128Ty
                                     : getContext().LongLongTy))
      return getNaturalAlignIndirect(RetTy);

  return (isPromotableIntegerTypeForABI(RetTy) ? ABIArgInfo::getExtend(RetTy)
                                               : ABIArgInfo::getDirect());
}

void DefaultABIInfo::computeInfo(ABIFunctionInfo &FI) const {
  FI.getReturnInfo() = classifyReturnType(FI.getReturnType());
  for (auto &I : FI.arguments())
    I.info = classifyArgumentType(I.type);
}

Address DefaultABIInfo::genVAArg(FunctionEmitter &FE, Address VAListAddr,
                                 QualType Ty) const {
  return genVAArgInstr(FE, VAListAddr, Ty, classifyArgumentType(Ty));
}

// ===----------------------------------------------------------------------===
// ABI utility functions
// ===----------------------------------------------------------------------===

void assignToArrayRange(CGBuilderTy &Builder, llvm::Value *Array,
                        llvm::Value *Value, unsigned FirstIndex,
                        unsigned LastIndex) {
  // Alternatively, we could emit this as a loop in the source.
  for (unsigned I = FirstIndex; I <= LastIndex; ++I) {
    llvm::Value *Cell =
        Builder.CreateConstInBoundsGEP1_32(Builder.getInt8Ty(), Array, I);
    Builder.CreateAlignedStore(Value, Cell, CharUnits::One());
  }
}

bool isAggregateTypeForABI(QualType T) {
  return !FunctionEmitter::hasScalarEvaluationKind(T);
}

CGABI::RecordArgABI getRecordArgABI(const RecordType *RT) {
  return CGABI::RAA_Default;
}

CGABI::RecordArgABI getRecordArgABI(QualType T) {
  const RecordType *RT = T->getAs<RecordType>();
  if (!RT)
    return CGABI::RAA_Default;
  return getRecordArgABI(RT);
}

bool classifyReturnType(ABIFunctionInfo &FI, const ABIInfo &Info) {
  return false;
}

QualType useFirstFieldIfTransparentUnion(QualType Ty) {
  if (const RecordType *UT = Ty->getAsUnionType()) {
    const RecordDecl *UD = UT->getDecl();
    if (UD->hasAttr<TransparentUnionAttr>()) {
      assert(!UD->field_empty() && "sema created an empty transparent union");
      return UD->field_begin()->getType();
    }
  }
  return Ty;
}

// ===----------------------------------------------------------------------===
// VA arg helpers
// ===----------------------------------------------------------------------===

llvm::Value *emitRoundPointerUpToAlignment(FunctionEmitter &FE,
                                           llvm::Value *Ptr, CharUnits Align) {
  // OverflowArgArea = (OverflowArgArea + Align - 1) & -Align;
  llvm::Value *RoundUp = FE.Builder.CreateConstInBoundsGEP1_32(
      FE.Builder.getInt8Ty(), Ptr, Align.getQuantity() - 1);
  return FE.Builder.CreateIntrinsic(
      llvm::Intrinsic::ptrmask, {FE.AllocaInt8PtrTy, FE.IntPtrTy},
      {RoundUp, llvm::ConstantInt::get(FE.IntPtrTy, -Align.getQuantity())},
      nullptr, Ptr->getName() + ".aligned");
}

Address emitVoidPtrDirectVAArg(FunctionEmitter &FE, Address VAListAddr,
                               llvm::Type *DirectTy, CharUnits DirectSize,
                               CharUnits DirectAlign, CharUnits SlotSize,
                               bool AllowHigherAlign) {
  // Cast the element type to i8* if necessary.  Some platforms define
  // va_list as a struct containing an i8* instead of just an i8*.
  if (VAListAddr.getElementType() != FE.Int8PtrTy)
    VAListAddr = VAListAddr.withElementType(FE.Int8PtrTy);

  llvm::Value *Ptr = FE.Builder.CreateLoad(VAListAddr, "argp.cur");

  // If the CC aligns values higher than the slot size, do so if needed.
  Address Addr = Address::invalid();
  if (AllowHigherAlign && DirectAlign > SlotSize) {
    Addr = Address(emitRoundPointerUpToAlignment(FE, Ptr, DirectAlign),
                   FE.Int8Ty, DirectAlign);
  } else {
    Addr = Address(Ptr, FE.Int8Ty, SlotSize);
  }

  CharUnits FullDirectSize = DirectSize.alignTo(SlotSize);
  Address NextPtr =
      FE.Builder.CreateConstInBoundsByteGEP(Addr, FullDirectSize, "argp.next");
  FE.Builder.CreateStore(NextPtr.getPointer(), VAListAddr);

  return Addr.withElementType(DirectTy);
}

Address emitVoidPtrVAArg(FunctionEmitter &FE, Address VAListAddr,
                         QualType ValueTy, bool IsIndirect,
                         TypeInfoChars ValueInfo, CharUnits SlotSizeAndAlign,
                         bool AllowHigherAlign) {
  CharUnits DirectSize, DirectAlign;
  if (IsIndirect) {
    DirectSize = FE.getPointerSize();
    DirectAlign = FE.getPointerAlign();
  } else {
    DirectSize = ValueInfo.Width;
    DirectAlign = ValueInfo.Align;
  }

  llvm::Type *DirectTy = FE.convertTypeForMem(ValueTy), *ElementTy = DirectTy;
  if (IsIndirect) {
    unsigned AllocaAS = FE.ME.getDataLayout().getAllocaAddrSpace();
    DirectTy = llvm::PointerType::get(FE.getLLVMContext(), AllocaAS);
  }

  Address Addr =
      emitVoidPtrDirectVAArg(FE, VAListAddr, DirectTy, DirectSize, DirectAlign,
                             SlotSizeAndAlign, AllowHigherAlign);

  if (IsIndirect) {
    Addr = Address(FE.Builder.CreateLoad(Addr), ElementTy, ValueInfo.Align);
  }

  return Addr;
}

Address emitMergePHI(FunctionEmitter &FE, Address Addr1,
                     llvm::BasicBlock *Block1, Address Addr2,
                     llvm::BasicBlock *Block2, const llvm::Twine &Name) {
  assert(Addr1.getType() == Addr2.getType());
  llvm::PHINode *PHI = FE.Builder.CreatePHI(Addr1.getType(), 2, Name);
  PHI->addIncoming(Addr1.getPointer(), Block1);
  PHI->addIncoming(Addr2.getPointer(), Block2);
  CharUnits Align = std::min(Addr1.getAlignment(), Addr2.getAlignment());
  return Address(PHI, Addr1.getElementType(), Align);
}

// ===----------------------------------------------------------------------===
// Record type queries
// ===----------------------------------------------------------------------===

bool isEmptyField(TreeContext &Context, const FieldDecl *FD, bool AllowArrays) {
  if (FD->isUnnamedBitfield())
    return true;

  QualType FT = FD->getType();

  // Constant arrays of empty records count as empty, strip them off.
  // Constant arrays of zero length always count as empty.
  if (AllowArrays)
    while (const ConstantArrayType *AT = Context.getAsConstantArrayType(FT)) {
      if (AT->getSize() == 0)
        return true;
      FT = AT->getElementType();
    }

  const RecordType *RT = FT->getAs<RecordType>();
  if (!RT)
    return false;

  return isEmptyRecord(Context, FT, AllowArrays);
}

bool isEmptyRecord(TreeContext &Context, QualType T, bool AllowArrays) {
  const RecordType *RT = T->getAs<RecordType>();
  if (!RT)
    return false;
  const RecordDecl *RD = RT->getDecl();
  if (RD->hasFlexibleArrayMember())
    return false;

  for (const auto *I : RD->fields())
    if (!isEmptyField(Context, I, AllowArrays))
      return false;
  return true;
}

const Type *isSingleElementStruct(QualType T, TreeContext &Context) {
  const RecordType *RT = T->getAs<RecordType>();
  if (!RT)
    return nullptr;

  const RecordDecl *RD = RT->getDecl();
  if (RD->hasFlexibleArrayMember())
    return nullptr;

  const Type *Found = nullptr;

  for (const auto *FD : RD->fields()) {
    QualType FT = FD->getType();

    if (isEmptyField(Context, FD, true))
      continue;

    if (Found)
      return nullptr;

    // Treat single element arrays as the element.
    while (const ConstantArrayType *AT = Context.getAsConstantArrayType(FT)) {
      if (AT->getSize().getZExtValue() != 1)
        break;
      FT = AT->getElementType();
    }

    if (!isAggregateTypeForABI(FT)) {
      Found = FT.getTypePtr();
    } else {
      Found = isSingleElementStruct(FT, Context);
      if (!Found)
        return nullptr;
    }
  }

  // We don't consider a struct a single-element struct if it has
  // padding beyond the element type.
  if (Found && Context.getTypeSize(Found) != Context.getTypeSize(T))
    return nullptr;

  return Found;
}

Address genVAArgInstr(FunctionEmitter &FE, Address VAListAddr, QualType Ty,
                      const ABIArgInfo &AI) {
  // This default implementation defers to the llvm backend's va_arg
  // instruction. It can handle only passing arguments directly
  // (typically only handled in the backend for primitive types), or
  // aggregates passed indirectly by pointer (NOTE: if the "byval"
  // flag has ABI impact in the callee, this implementation cannot
  // work.)

  // Only a few cases are covered here at the moment -- those needed
  // by the default abi.
  llvm::Value *Val;

  if (AI.isIndirect()) {
    assert(!AI.getPaddingType() &&
           "Unexpected PaddingType seen in arginfo in generic VAArg emitter!");
    assert(
        !AI.getIndirectRealign() &&
        "Unexpected IndirectRealign seen in arginfo in generic VAArg emitter!");

    auto TyInfo = FE.getContext().getTypeInfoInChars(Ty);
    CharUnits TyAlignForABI = TyInfo.Align;

    llvm::Type *ElementTy = FE.convertTypeForMem(Ty);
    llvm::Type *BaseTy = llvm::PointerType::getUnqual(ElementTy);
    llvm::Value *Addr = FE.Builder.CreateVAArg(VAListAddr.getPointer(), BaseTy);
    return Address(Addr, ElementTy, TyAlignForABI);
  } else {
    assert((AI.isDirect() || AI.isExtend()) &&
           "Unexpected ArgInfo Kind in generic VAArg emitter!");

    assert(!AI.getInReg() &&
           "Unexpected InReg seen in arginfo in generic VAArg emitter!");
    assert(!AI.getPaddingType() &&
           "Unexpected PaddingType seen in arginfo in generic VAArg emitter!");
    assert(!AI.getDirectOffset() &&
           "Unexpected DirectOffset seen in arginfo in generic VAArg emitter!");
    assert(!AI.getCoerceToType() &&
           "Unexpected CoerceToType seen in arginfo in generic VAArg emitter!");

    Address Temp = FE.createMemTemp(Ty, "varet");
    Val = FE.Builder.CreateVAArg(VAListAddr.getPointer(),
                                 FE.convertTypeForMem(Ty));
    FE.Builder.CreateStore(Val, Temp);
    return Temp;
  }
}

} // namespace neverc::CodeGen
