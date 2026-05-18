#include "ABI/ABIInfoImpl.h"
#include "ABI/TargetInfo.h"

using namespace neverc;
using namespace neverc::CodeGen;

// ===----------------------------------------------------------------------===
// AArch64ABIInfo
// ===----------------------------------------------------------------------===

namespace {

class AArch64ABIInfo : public ABIInfo {
  AArch64ABIKind Kind;

public:
  AArch64ABIInfo(TypeEmitter &CGT, AArch64ABIKind Kind)
      : ABIInfo(CGT), Kind(Kind) {}

private:
  AArch64ABIKind getABIKind() const { return Kind; }
  bool isDarwinPCS() const { return Kind == AArch64ABIKind::DarwinPCS; }

  ABIArgInfo classifyReturnType(QualType RetTy, bool IsVariadic) const;
  ABIArgInfo classifyArgumentType(QualType RetTy, bool IsVariadic,
                                  unsigned CallingConvention) const;
  ABIArgInfo coerceIllegalVector(QualType Ty) const;
  bool isHomogeneousAggregateBaseType(QualType Ty) const override;
  bool isHomogeneousAggregateSmallEnough(const Type *Ty,
                                         uint64_t Members) const override;
  bool isZeroLengthBitfieldPermittedInHomogeneousAggregate() const override;

  bool isIllegalVectorType(QualType Ty) const;

  void computeInfo(ABIFunctionInfo &FI) const override {
    if (!::classifyReturnType(FI, *this))
      FI.getReturnInfo() =
          classifyReturnType(FI.getReturnType(), FI.isVariadic());

    for (auto &it : FI.arguments())
      it.info = classifyArgumentType(it.type, FI.isVariadic(),
                                     FI.getCallingConvention());
  }

  Address genDarwinVAArg(Address VAListAddr, QualType Ty,
                         FunctionEmitter &FE) const;

  Address genAAPCSVAArg(Address VAListAddr, QualType Ty,
                        FunctionEmitter &FE) const;

  Address genMSVAArg(FunctionEmitter &FE, Address VAListAddr,
                     QualType Ty) const override;

  Address genVAArg(FunctionEmitter &FE, Address VAListAddr,
                   QualType Ty) const override {
    llvm::Type *BaseTy = FE.convertType(Ty);
    if (isa<llvm::ScalableVectorType>(BaseTy))
      llvm::report_fatal_error("Passing SVE types to variadic functions is "
                               "currently not supported");

    return isDarwinPCS() ? genDarwinVAArg(VAListAddr, Ty, FE)
                         : genAAPCSVAArg(VAListAddr, Ty, FE);
  }

  bool allowBFloatArgsAndRet() const override {
    return getTarget().hasBFloat16Type();
  }
};

// ===----------------------------------------------------------------------===
// AArch64TargetCodeGenInfo
// ===----------------------------------------------------------------------===

class AArch64TargetCodeGenInfo : public TargetCodeGenInfo {
public:
  AArch64TargetCodeGenInfo(TypeEmitter &CGT, AArch64ABIKind Kind)
      : TargetCodeGenInfo(std::make_unique<AArch64ABIInfo>(CGT, Kind)) {}

  int getDwarfEHStackPointer(Emit::ModuleEmitter &M) const override {
    return 31;
  }

  bool doesReturnSlotInterfereWithArgs() const override { return false; }

  void setTargetAttributes(const Decl *D, llvm::GlobalValue *GV,
                           Emit::ModuleEmitter &ME) const override {
    const FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(D);
    if (!FD)
      return;

    // Propagate `-mstack-probe-size=N` / `-mno-stack-arg-probe` to
    // every emitted function.  The LLVM AArch64 backend consults the
    // `stack-probe-size` / `no-stack-arg-probe` function attributes
    // before deciding whether to emit a `__chkstk` call on Windows;
    // without this hand-off the frontend flags would be ignored and
    // large stack frames would regress to a dangling extern call.
    addStackProbeTargetAttributes(D, GV, ME);

    const auto *TA = FD->getAttr<TargetAttr>();
    if (TA == nullptr)
      return;

    ParsedTargetAttr Attr =
        ME.getTarget().parseTargetAttr(TA->getFeaturesStr());
    if (Attr.BranchProtection.empty())
      return;

    TargetInfo::BranchProtectionInfo BPI;
    llvm::StringRef Error;
    (void)ME.getTarget().validateBranchProtection(Attr.BranchProtection,
                                                  Attr.CPU, BPI, Error);
    assert(Error.empty());

    auto *Fn = cast<llvm::Function>(GV);
    static const char *SignReturnAddrStr[] = {"none", "non-leaf", "all"};
    Fn->addFnAttr("sign-return-address",
                  SignReturnAddrStr[static_cast<int>(BPI.SignReturnAddr)]);

    if (BPI.SignReturnAddr != LangOptions::SignReturnAddressScopeKind::None) {
      Fn->addFnAttr("sign-return-address-key",
                    BPI.SignKey == LangOptions::SignReturnAddressKeyKind::AKey
                        ? "a_key"
                        : "b_key");
    }

    Fn->addFnAttr("branch-target-enforcement",
                  BPI.BranchTargetEnforcement ? "true" : "false");
    Fn->addFnAttr("branch-protection-pauth-lr",
                  BPI.BranchProtectionPAuthLR ? "true" : "false");
  }

  bool isScalarizableAsmOperand(Emit::FunctionEmitter &FE,
                                llvm::Type *Ty) const override {
    if (FE.getTarget().hasFeature("ls64")) {
      auto *ST = dyn_cast<llvm::StructType>(Ty);
      if (ST && ST->getNumElements() == 1) {
        auto *AT = dyn_cast<llvm::ArrayType>(ST->getElementType(0));
        if (AT && AT->getNumElements() == 8 &&
            AT->getElementType()->isIntegerTy(64))
          return true;
      }
    }
    return TargetCodeGenInfo::isScalarizableAsmOperand(FE, Ty);
  }
};
} // namespace

// ===----------------------------------------------------------------------===
// AArch64ABIInfo implementation
// ===----------------------------------------------------------------------===

ABIArgInfo AArch64ABIInfo::coerceIllegalVector(QualType Ty) const {
  assert(Ty->isVectorType() && "expected vector type!");

  const auto *VT = Ty->castAs<VectorType>();
  if (VT->getVectorKind() == VectorKind::SveFixedLengthPredicate) {
    assert(VT->getElementType()->isBuiltinType() && "expected builtin type!");
    assert(VT->getElementType()->castAs<BuiltinType>()->getKind() ==
               BuiltinType::UChar &&
           "unexpected builtin type for SVE predicate!");
    return ABIArgInfo::getDirect(llvm::ScalableVectorType::get(
        llvm::Type::getInt1Ty(getVMContext()), 16));
  }

  if (VT->getVectorKind() == VectorKind::SveFixedLengthData) {
    assert(VT->getElementType()->isBuiltinType() && "expected builtin type!");

    const auto *BT = VT->getElementType()->castAs<BuiltinType>();
    llvm::ScalableVectorType *ResType = nullptr;
    switch (BT->getKind()) {
    default:
      llvm_unreachable("unexpected builtin type for SVE vector!");
    case BuiltinType::SChar:
    case BuiltinType::UChar:
      ResType = llvm::ScalableVectorType::get(
          llvm::Type::getInt8Ty(getVMContext()), 16);
      break;
    case BuiltinType::Short:
    case BuiltinType::UShort:
      ResType = llvm::ScalableVectorType::get(
          llvm::Type::getInt16Ty(getVMContext()), 8);
      break;
    case BuiltinType::Int:
    case BuiltinType::UInt:
      ResType = llvm::ScalableVectorType::get(
          llvm::Type::getInt32Ty(getVMContext()), 4);
      break;
    case BuiltinType::Long:
    case BuiltinType::ULong:
      ResType = llvm::ScalableVectorType::get(
          llvm::Type::getInt64Ty(getVMContext()), 2);
      break;
    case BuiltinType::Half:
      ResType = llvm::ScalableVectorType::get(
          llvm::Type::getHalfTy(getVMContext()), 8);
      break;
    case BuiltinType::Float:
      ResType = llvm::ScalableVectorType::get(
          llvm::Type::getFloatTy(getVMContext()), 4);
      break;
    case BuiltinType::Double:
      ResType = llvm::ScalableVectorType::get(
          llvm::Type::getDoubleTy(getVMContext()), 2);
      break;
    case BuiltinType::BFloat16:
      ResType = llvm::ScalableVectorType::get(
          llvm::Type::getBFloatTy(getVMContext()), 8);
      break;
    }
    return ABIArgInfo::getDirect(ResType);
  }

  uint64_t Size = getContext().getTypeSize(Ty);
  // Android promotes <2 x i8> to i16, not i32
  if (isAndroid() && (Size <= 16)) {
    llvm::Type *ResType = llvm::Type::getInt16Ty(getVMContext());
    return ABIArgInfo::getDirect(ResType);
  }
  if (Size <= 32) {
    llvm::Type *ResType = llvm::Type::getInt32Ty(getVMContext());
    return ABIArgInfo::getDirect(ResType);
  }
  if (Size == 64) {
    auto *ResType =
        llvm::FixedVectorType::get(llvm::Type::getInt32Ty(getVMContext()), 2);
    return ABIArgInfo::getDirect(ResType);
  }
  if (Size == 128) {
    auto *ResType =
        llvm::FixedVectorType::get(llvm::Type::getInt32Ty(getVMContext()), 4);
    return ABIArgInfo::getDirect(ResType);
  }
  return getNaturalAlignIndirect(Ty, /*ByVal=*/false);
}

ABIArgInfo
AArch64ABIInfo::classifyArgumentType(QualType Ty, bool IsVariadic,
                                     unsigned CallingConvention) const {
  Ty = useFirstFieldIfTransparentUnion(Ty);

  if (isIllegalVectorType(Ty))
    return coerceIllegalVector(Ty);

  if (!isAggregateTypeForABI(Ty)) {
    // Treat an enum type as its underlying type.
    if (const EnumType *EnumTy = Ty->getAs<EnumType>())
      Ty = EnumTy->getDecl()->getIntegerType();

    if (const auto *EIT = Ty->getAs<BitIntType>())
      if (EIT->getNumBits() > 128)
        return getNaturalAlignIndirect(Ty);

    return (isPromotableIntegerTypeForABI(Ty) && isDarwinPCS()
                ? ABIArgInfo::getExtend(Ty)
                : ABIArgInfo::getDirect());
  }

  // Structures that require indirect passing (e.g. too large for registers).
  if (CGABI::RecordArgABI RAA = getRecordArgABI(Ty)) {
    return getNaturalAlignIndirect(Ty,
                                   /*ByVal=*/RAA == CGABI::RAA_DirectInMemory);
  }

  // Empty records are ignored on Darwin; other targets may pass them for GNU
  // compatibility.
  uint64_t Size = getContext().getTypeSize(Ty);
  bool IsEmpty = isEmptyRecord(getContext(), Ty, true);
  if (IsEmpty || Size == 0)
    return ABIArgInfo::getIgnore();

  // Homogeneous Floating-point Aggregates (HFAs) need to be expanded.
  const Type *Base = nullptr;
  uint64_t Members = 0;
  bool IsWin64 = CallingConvention == llvm::CallingConv::Win64;
  bool IsWinVariadic = IsWin64 && IsVariadic;
  // In variadic functions on Windows, all composite types are treated alike,
  // no special handling of HFAs/HVAs.
  if (!IsWinVariadic && isHomogeneousAggregate(Ty, Base, Members)) {
    if (Kind != AArch64ABIKind::AAPCS)
      return ABIArgInfo::getDirect(
          llvm::ArrayType::get(CGT.convertType(QualType(Base, 0)), Members));

    // For HFAs/HVAs, cap the argument alignment to 16, otherwise
    // set it to 8 according to the AAPCS64 document.
    unsigned Align =
        getContext().getTypeUnadjustedAlignInChars(Ty).getQuantity();
    Align = (Align >= 16) ? 16 : 8;
    return ABIArgInfo::getDirect(
        llvm::ArrayType::get(CGT.convertType(QualType(Base, 0)), Members), 0,
        nullptr, true, Align);
  }

  // Aggregates <= 16 bytes are passed directly in registers or on the stack.
  if (Size <= 128) {
    unsigned Alignment;
    if (Kind == AArch64ABIKind::AAPCS) {
      Alignment = getContext().getTypeUnadjustedAlign(Ty);
      Alignment = Alignment < 128 ? 64 : 128;
    } else {
      Alignment =
          std::max(getContext().getTypeAlign(Ty),
                   (unsigned)getTarget().getPointerWidth(LangAS::Default));
    }
    Size = llvm::alignTo(Size, Alignment);

    // We use a pair of i64 for 16-byte aggregate with 8-byte alignment.
    // For aggregates with 16-byte alignment, we use i128.
    llvm::Type *BaseTy = llvm::Type::getIntNTy(getVMContext(), Alignment);
    return ABIArgInfo::getDirect(
        Size == Alignment ? BaseTy
                          : llvm::ArrayType::get(BaseTy, Size / Alignment));
  }

  return getNaturalAlignIndirect(Ty, /*ByVal=*/false);
}

ABIArgInfo AArch64ABIInfo::classifyReturnType(QualType RetTy,
                                              bool IsVariadic) const {
  if (RetTy->isVoidType())
    return ABIArgInfo::getIgnore();

  if (const auto *VT = RetTy->getAs<VectorType>()) {
    if (VT->getVectorKind() == VectorKind::SveFixedLengthData ||
        VT->getVectorKind() == VectorKind::SveFixedLengthPredicate)
      return coerceIllegalVector(RetTy);
  }

  // Large vector types should be returned via memory.
  if (RetTy->isVectorType() && getContext().getTypeSize(RetTy) > 128)
    return getNaturalAlignIndirect(RetTy);

  if (!isAggregateTypeForABI(RetTy)) {
    // Treat an enum type as its underlying type.
    if (const EnumType *EnumTy = RetTy->getAs<EnumType>())
      RetTy = EnumTy->getDecl()->getIntegerType();

    if (const auto *EIT = RetTy->getAs<BitIntType>())
      if (EIT->getNumBits() > 128)
        return getNaturalAlignIndirect(RetTy);

    return (isPromotableIntegerTypeForABI(RetTy) && isDarwinPCS()
                ? ABIArgInfo::getExtend(RetTy)
                : ABIArgInfo::getDirect());
  }

  uint64_t Size = getContext().getTypeSize(RetTy);
  if (isEmptyRecord(getContext(), RetTy, true) || Size == 0)
    return ABIArgInfo::getIgnore();

  const Type *Base = nullptr;
  uint64_t Members = 0;
  if (isHomogeneousAggregate(RetTy, Base, Members))
    // Homogeneous Floating-point Aggregates (HFAs) are returned directly.
    return ABIArgInfo::getDirect();

  // Aggregates <= 16 bytes are returned directly in registers or on the stack.
  // Only little-endian AArch64 is supported; composite types are returned in
  // lower bits of a 64-bit register, matching integer return convention.
  if (Size <= 128) {
    if (Size <= 64) {
      return ABIArgInfo::getDirect(
          llvm::IntegerType::get(getVMContext(), Size));
    }

    unsigned Alignment = getContext().getTypeAlign(RetTy);
    Size = llvm::alignTo(Size, 64);

    if (Alignment < 128 && Size == 128) {
      llvm::Type *BaseTy = llvm::Type::getInt64Ty(getVMContext());
      return ABIArgInfo::getDirect(llvm::ArrayType::get(BaseTy, Size / 64));
    }
    return ABIArgInfo::getDirect(llvm::IntegerType::get(getVMContext(), Size));
  }

  return getNaturalAlignIndirect(RetTy);
}

bool AArch64ABIInfo::isIllegalVectorType(QualType Ty) const {
  if (const VectorType *VT = Ty->getAs<VectorType>()) {
    // Check whether VT is a fixed-length SVE vector. These types are
    // represented as scalable vectors in function args/return and must be
    // coerced from fixed vectors.
    if (VT->getVectorKind() == VectorKind::SveFixedLengthData ||
        VT->getVectorKind() == VectorKind::SveFixedLengthPredicate)
      return true;

    unsigned NumElements = VT->getNumElements();
    uint64_t Size = getContext().getTypeSize(VT);
    // NumElements should be power of 2.
    if (!llvm::isPowerOf2_32(NumElements))
      return true;

    return Size != 64 && (Size != 128 || NumElements == 1);
  }
  return false;
}

bool AArch64ABIInfo::isHomogeneousAggregateBaseType(QualType Ty) const {
  // Homogeneous aggregates for AAPCS64 must have base types of a floating
  // point type or a short-vector type. This is the same as the 32-bit ABI,
  // but with the difference that any floating-point type is allowed,
  // including __fp16.
  if (const BuiltinType *BT = Ty->getAs<BuiltinType>()) {
    if (BT->isFloatingPoint())
      return true;
  } else if (const VectorType *VT = Ty->getAs<VectorType>()) {
    unsigned VecSize = getContext().getTypeSize(VT);
    if (VecSize == 64 || VecSize == 128)
      return true;
  }
  return false;
}

bool AArch64ABIInfo::isHomogeneousAggregateSmallEnough(const Type *Base,
                                                       uint64_t Members) const {
  return Members <= 4;
}

bool AArch64ABIInfo::isZeroLengthBitfieldPermittedInHomogeneousAggregate()
    const {
  // AAPCS64 says that the rule for whether something is a homogeneous
  // aggregate is applied to the output of the data layout decision. So
  // anything that doesn't affect the data layout also does not affect
  // homogeneity. In particular, zero-length bitfields don't stop a struct
  // being homogeneous.
  return true;
}

Address AArch64ABIInfo::genAAPCSVAArg(Address VAListAddr, QualType Ty,
                                      FunctionEmitter &FE) const {
  ABIArgInfo AI = classifyArgumentType(Ty, /*IsVariadic=*/true,
                                       FE.CurFnInfo->getCallingConvention());
  // Empty records are ignored for parameter passing purposes.
  if (AI.isIgnore()) {
    uint64_t PointerSize = getTarget().getPointerWidth(LangAS::Default) / 8;
    CharUnits SlotSize = CharUnits::fromQuantity(PointerSize);
    VAListAddr = VAListAddr.withElementType(FE.Int8PtrTy);
    auto *Load = FE.Builder.CreateLoad(VAListAddr);
    return Address(Load, FE.convertTypeForMem(Ty), SlotSize);
  }

  bool IsIndirect = AI.isIndirect();

  llvm::Type *BaseTy = FE.convertType(Ty);
  if (IsIndirect)
    BaseTy = llvm::PointerType::getUnqual(BaseTy);
  else if (AI.getCoerceToType())
    BaseTy = AI.getCoerceToType();

  unsigned NumRegs = 1;
  if (llvm::ArrayType *ArrTy = dyn_cast<llvm::ArrayType>(BaseTy)) {
    BaseTy = ArrTy->getElementType();
    NumRegs = ArrTy->getNumElements();
  }
  bool IsFPR = BaseTy->isFloatingPointTy() || BaseTy->isVectorTy();

  // The AArch64 va_list type and handling is specified in the Procedure Call
  // Standard, section B.4:
  //
  // struct {
  //   void *__stack;
  //   void *__gr_top;
  //   void *__vr_top;
  //   int __gr_offs;
  //   int __vr_offs;
  // };

  llvm::BasicBlock *MaybeRegBlock = FE.createBasicBlock("vaarg.maybe_reg");
  llvm::BasicBlock *InRegBlock = FE.createBasicBlock("vaarg.in_reg");
  llvm::BasicBlock *OnStackBlock = FE.createBasicBlock("vaarg.on_stack");
  llvm::BasicBlock *ContBlock = FE.createBasicBlock("vaarg.end");

  CharUnits TySize = getContext().getTypeSizeInChars(Ty);
  CharUnits TyAlign = getContext().getTypeUnadjustedAlignInChars(Ty);

  Address reg_offs_p = Address::invalid();
  llvm::Value *reg_offs = nullptr;
  int reg_top_index;
  int RegSize = IsIndirect ? 8 : TySize.getQuantity();
  if (!IsFPR) {
    // 3 is the field number of __gr_offs
    reg_offs_p = FE.Builder.CreateStructGEP(VAListAddr, 3, "gr_offs_p");
    reg_offs = FE.Builder.CreateLoad(reg_offs_p, "gr_offs");
    reg_top_index = 1; // field number for __gr_top
    RegSize = llvm::alignTo(RegSize, 8);
  } else {
    // 4 is the field number of __vr_offs.
    reg_offs_p = FE.Builder.CreateStructGEP(VAListAddr, 4, "vr_offs_p");
    reg_offs = FE.Builder.CreateLoad(reg_offs_p, "vr_offs");
    reg_top_index = 2; // field number for __vr_top
    RegSize = 16 * NumRegs;
  }

  //=======================================
  // Find out where argument was passed
  //=======================================

  // If reg_offs >= 0 we're already using the stack for this type of
  // argument. We don't want to keep updating reg_offs (in case it overflows,
  // though anyone passing 2GB of arguments, each at most 16 bytes, deserves
  // whatever they get).
  llvm::Value *UsingStack = nullptr;
  UsingStack =
      FE.Builder.CreateICmpSGE(reg_offs, llvm::ConstantInt::get(FE.Int32Ty, 0));

  FE.Builder.CreateCondBr(UsingStack, OnStackBlock, MaybeRegBlock);

  // Otherwise, at least some kind of argument could go in these registers, the
  // question is whether this particular type is too big.
  FE.genBlock(MaybeRegBlock);

  // Integer arguments may need to correct register alignment (for example a
  // "struct { __int128 a; };" gets passed in x_2N, x_{2N+1}). In this case we
  // align __gr_offs to calculate the potential address.
  if (!IsFPR && !IsIndirect && TyAlign.getQuantity() > 8) {
    int Align = TyAlign.getQuantity();

    reg_offs = FE.Builder.CreateAdd(
        reg_offs, llvm::ConstantInt::get(FE.Int32Ty, Align - 1),
        "align_regoffs");
    reg_offs = FE.Builder.CreateAnd(reg_offs,
                                    llvm::ConstantInt::get(FE.Int32Ty, -Align),
                                    "aligned_regoffs");
  }

  // Update the gr_offs/vr_offs pointer for next call to va_arg on this va_list.
  // The fact that this is done unconditionally reflects the fact that
  // allocating an argument to the stack also uses up all the remaining
  // registers of the appropriate kind.
  llvm::Value *NewOffset = nullptr;
  NewOffset = FE.Builder.CreateAdd(
      reg_offs, llvm::ConstantInt::get(FE.Int32Ty, RegSize), "new_reg_offs");
  FE.Builder.CreateStore(NewOffset, reg_offs_p);

  // Now we're in a position to decide whether this argument really was in
  // registers or not.
  llvm::Value *InRegs = nullptr;
  InRegs = FE.Builder.CreateICmpSLE(
      NewOffset, llvm::ConstantInt::get(FE.Int32Ty, 0), "inreg");

  FE.Builder.CreateCondBr(InRegs, InRegBlock, OnStackBlock);

  //=======================================
  // Argument was in registers
  //=======================================

  // Now we emit the code for if the argument was originally passed in
  // registers. First start the appropriate block:
  FE.genBlock(InRegBlock);

  llvm::Value *reg_top = nullptr;
  Address reg_top_p =
      FE.Builder.CreateStructGEP(VAListAddr, reg_top_index, "reg_top_p");
  reg_top = FE.Builder.CreateLoad(reg_top_p, "reg_top");
  Address BaseAddr(FE.Builder.CreateInBoundsGEP(FE.Int8Ty, reg_top, reg_offs),
                   FE.Int8Ty, CharUnits::fromQuantity(IsFPR ? 16 : 8));
  Address RegAddr = Address::invalid();
  llvm::Type *MemTy = FE.convertTypeForMem(Ty), *ElementTy = MemTy;

  if (IsIndirect) {
    // If it's been passed indirectly (actually a struct), whatever we find from
    // stored registers or on the stack will actually be a struct **.
    MemTy = llvm::PointerType::getUnqual(MemTy);
  }

  const Type *Base = nullptr;
  uint64_t NumMembers = 0;
  bool IsHFA = isHomogeneousAggregate(Ty, Base, NumMembers);
  if (IsHFA && NumMembers > 1) {
    // Homogeneous aggregates passed in registers will have their elements split
    // and stored 16-bytes apart regardless of size (they're notionally in qN,
    // qN+1, ...). We reload and store into a temporary local variable
    // contiguously.
    assert(!IsIndirect && "Homogeneous aggregates should be passed directly");
    auto BaseTyInfo = getContext().getTypeInfoInChars(QualType(Base, 0));
    llvm::Type *BaseTy = FE.convertType(QualType(Base, 0));
    llvm::Type *HFATy = llvm::ArrayType::get(BaseTy, NumMembers);
    Address Tmp =
        FE.createTempAlloca(HFATy, std::max(TyAlign, BaseTyInfo.Align));

    for (unsigned i = 0; i < NumMembers; ++i) {
      CharUnits BaseOffset = CharUnits::fromQuantity(16 * i);
      Address LoadAddr =
          FE.Builder.CreateConstInBoundsByteGEP(BaseAddr, BaseOffset);
      LoadAddr = LoadAddr.withElementType(BaseTy);

      Address StoreAddr = FE.Builder.CreateConstArrayGEP(Tmp, i);

      llvm::Value *Elem = FE.Builder.CreateLoad(LoadAddr);
      FE.Builder.CreateStore(Elem, StoreAddr);
    }

    RegAddr = Tmp.withElementType(MemTy);
  } else {
    // Otherwise the object is contiguous in memory.

    RegAddr = BaseAddr.withElementType(MemTy);
  }

  FE.genBranch(ContBlock);

  //=======================================
  // Argument was on the stack
  //=======================================
  FE.genBlock(OnStackBlock);

  Address stack_p = FE.Builder.CreateStructGEP(VAListAddr, 0, "stack_p");
  llvm::Value *OnStackPtr = FE.Builder.CreateLoad(stack_p, "stack");

  // Again, stack arguments may need realignment. In this case both integer and
  // floating-point ones might be affected.
  if (!IsIndirect && TyAlign.getQuantity() > 8) {
    int Align = TyAlign.getQuantity();

    OnStackPtr = FE.Builder.CreatePtrToInt(OnStackPtr, FE.Int64Ty);

    OnStackPtr = FE.Builder.CreateAdd(
        OnStackPtr, llvm::ConstantInt::get(FE.Int64Ty, Align - 1),
        "align_stack");
    OnStackPtr = FE.Builder.CreateAnd(
        OnStackPtr, llvm::ConstantInt::get(FE.Int64Ty, -Align), "align_stack");

    OnStackPtr = FE.Builder.CreateIntToPtr(OnStackPtr, FE.Int8PtrTy);
  }
  Address OnStackAddr = Address(OnStackPtr, FE.Int8Ty,
                                std::max(CharUnits::fromQuantity(8), TyAlign));

  // All stack slots are multiples of 8 bytes.
  CharUnits StackSlotSize = CharUnits::fromQuantity(8);
  CharUnits StackSize;
  if (IsIndirect)
    StackSize = StackSlotSize;
  else
    StackSize = TySize.alignTo(StackSlotSize);

  llvm::Value *StackSizeC = FE.Builder.getSize(StackSize);
  llvm::Value *NewStack = FE.Builder.CreateInBoundsGEP(FE.Int8Ty, OnStackPtr,
                                                       StackSizeC, "new_stack");

  // Write the new value of __stack for the next call to va_arg
  FE.Builder.CreateStore(NewStack, stack_p);

  OnStackAddr = OnStackAddr.withElementType(MemTy);

  FE.genBranch(ContBlock);

  //=======================================
  // Tidy up
  //=======================================
  FE.genBlock(ContBlock);

  Address ResAddr = emitMergePHI(FE, RegAddr, InRegBlock, OnStackAddr,
                                 OnStackBlock, "vaargs.addr");

  if (IsIndirect)
    return Address(FE.Builder.CreateLoad(ResAddr, "vaarg.addr"), ElementTy,
                   TyAlign);

  return ResAddr;
}

Address AArch64ABIInfo::genDarwinVAArg(Address VAListAddr, QualType Ty,
                                       FunctionEmitter &FE) const {
  // The backend's lowering doesn't support va_arg for aggregates or
  // illegal vector types.  Lower VAArg here for these cases and use
  // the LLVM va_arg instruction for everything else.
  if (!isAggregateTypeForABI(Ty) && !isIllegalVectorType(Ty))
    return genVAArgInstr(FE, VAListAddr, Ty, ABIArgInfo::getDirect());

  uint64_t PointerSize = getTarget().getPointerWidth(LangAS::Default) / 8;
  CharUnits SlotSize = CharUnits::fromQuantity(PointerSize);

  // Empty records are ignored for parameter passing purposes.
  if (isEmptyRecord(getContext(), Ty, true))
    return Address(FE.Builder.CreateLoad(VAListAddr, "ap.cur"),
                   FE.convertTypeForMem(Ty), SlotSize);

  // The size of the actual thing passed, which might end up just
  // being a pointer for indirect types.
  auto TyInfo = getContext().getTypeInfoInChars(Ty);

  // Arguments bigger than 16 bytes which aren't homogeneous
  // aggregates should be passed indirectly.
  bool IsIndirect = false;
  if (TyInfo.Width.getQuantity() > 16) {
    const Type *Base = nullptr;
    uint64_t Members = 0;
    IsIndirect = !isHomogeneousAggregate(Ty, Base, Members);
  }

  return emitVoidPtrVAArg(FE, VAListAddr, Ty, IsIndirect, TyInfo, SlotSize,
                          /*AllowHigherAlign*/ true);
}

Address AArch64ABIInfo::genMSVAArg(FunctionEmitter &FE, Address VAListAddr,
                                   QualType Ty) const {
  bool IsIndirect = false;

  // Composites larger than 16 bytes are passed by reference.
  if (isAggregateTypeForABI(Ty) && getContext().getTypeSize(Ty) > 128)
    IsIndirect = true;

  return emitVoidPtrVAArg(FE, VAListAddr, Ty, IsIndirect,
                          FE.getContext().getTypeInfoInChars(Ty),
                          CharUnits::fromQuantity(8),
                          /*allowHigherAlign*/ false);
}

std::unique_ptr<TargetCodeGenInfo>
Emit::createAArch64TargetCodeGenInfo(ModuleEmitter &ME, AArch64ABIKind Kind) {
  return std::make_unique<AArch64TargetCodeGenInfo>(ME.getTypes(), Kind);
}
