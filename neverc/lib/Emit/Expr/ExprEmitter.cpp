#include "ABI/EmitterABI.h"
#include "ABI/TargetInfo.h"
#include "Core/ConstantEmitter.h"
#include "Core/FunctionEmitter.h"
#include "Core/ModuleEmitter.h"
#include "Core/RecordLayoutInfo.h"
#include "Debug/DebugEmitterInfo.h"
#include "Stmt/CallEmitterInfo.h"
#include "Stmt/CleanupEmitterInfo.h"
#include "neverc/Foundation/Builtin/Builtins.h"
#include "neverc/Tree/Core/Attr.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Decl/DeclC.h"
#include "neverc/Tree/Type/StructLayout.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/MatrixBuilder.h"
#include "llvm/Support/ConvertUTF.h"
#include <optional>
#include <string>

using namespace neverc;
using namespace Emit;

// ===----------------------------------------------------------------------===
// Allocation & address helpers
// ===----------------------------------------------------------------------===

namespace {
llvm::cl::opt<bool> ClSanitizeDebugDeoptimization(
    "ubsan-unique-traps", llvm::cl::Optional,
    llvm::cl::desc("Deoptimize traps for UBSAN so there is 1 trap per check"),
    llvm::cl::init(false));
} // namespace

Address FunctionEmitter::createTempAllocaWithoutCast(llvm::Type *Ty,
                                                     CharUnits Align,
                                                     const llvm::Twine &Name,
                                                     llvm::Value *ArraySize) {
  auto Alloca = createTempAlloca(Ty, Name, ArraySize);
  Alloca->setAlignment(Align.getAsAlign());
  return Address(Alloca, Ty, Align, KnownNonNull);
}

Address FunctionEmitter::createTempAlloca(llvm::Type *Ty, CharUnits Align,
                                          const llvm::Twine &Name,
                                          llvm::Value *ArraySize,
                                          Address *AllocaAddr) {
  auto Alloca = createTempAllocaWithoutCast(Ty, Align, Name, ArraySize);
  if (AllocaAddr)
    *AllocaAddr = Alloca;
  llvm::Value *V = Alloca.getPointer();
  // Alloca uses the alloca address space; if that differs from the default,
  // cast to the default address space for ordinary object pointers.
  if (getASTAllocaAddressSpace() != LangAS::Default) {
    auto DestAddrSpace = getContext().getTargetAddressSpace(LangAS::Default);
    llvm::IRBuilderBase::InsertPointGuard IPG(Builder);
    // When ArraySize is nullptr, alloca is inserted at AllocaInsertPt,
    // otherwise alloca is inserted at the current insertion point of the
    // builder.
    if (!ArraySize)
      Builder.SetInsertPoint(getPostAllocaInsertPoint());
    V = getTargetHooks().performAddrSpaceCast(
        *this, V, getASTAllocaAddressSpace(), LangAS::Default,
        Ty->getPointerTo(DestAddrSpace), /*non-null*/ true);
  }

  return Address(V, Ty, Align, KnownNonNull);
}

llvm::AllocaInst *FunctionEmitter::createTempAlloca(llvm::Type *Ty,
                                                    const llvm::Twine &Name,
                                                    llvm::Value *ArraySize) {
  if (ArraySize)
    return Builder.CreateAlloca(Ty, ArraySize, Name);
  return new llvm::AllocaInst(Ty, ME.getDataLayout().getAllocaAddrSpace(),
                              ArraySize, Name, AllocaInsertPt);
}

Address FunctionEmitter::createDefaultAlignTempAlloca(llvm::Type *Ty,
                                                      const llvm::Twine &Name) {
  CharUnits Align =
      CharUnits::fromQuantity(ME.getDataLayout().getPrefTypeAlign(Ty));
  return createTempAlloca(Ty, Align, Name);
}

Address FunctionEmitter::createIRTemp(QualType Ty, const llvm::Twine &Name) {
  CharUnits Align = getContext().getTypeAlignInChars(Ty);
  return createTempAlloca(convertType(Ty), Align, Name);
}

Address FunctionEmitter::createMemTemp(QualType Ty, const llvm::Twine &Name,
                                       Address *Alloca) {
  return createMemTemp(Ty, getContext().getTypeAlignInChars(Ty), Name, Alloca);
}

Address FunctionEmitter::createMemTemp(QualType Ty, CharUnits Align,
                                       const llvm::Twine &Name,
                                       Address *Alloca) {
  Address Result = createTempAlloca(convertTypeForMem(Ty), Align, Name,
                                    /*ArraySize=*/nullptr, Alloca);

  if (Ty->isConstantMatrixType()) {
    auto *ArrayTy = cast<llvm::ArrayType>(Result.getElementType());
    auto *VectorTy = llvm::FixedVectorType::get(ArrayTy->getElementType(),
                                                ArrayTy->getNumElements());

    Result = Address(Result.getPointer(), VectorTy, Result.getAlignment(),
                     KnownNonNull);
  }
  return Result;
}

Address FunctionEmitter::createMemTempWithoutCast(QualType Ty, CharUnits Align,
                                                  const llvm::Twine &Name) {
  return createTempAllocaWithoutCast(convertTypeForMem(Ty), Align, Name);
}

Address FunctionEmitter::createMemTempWithoutCast(QualType Ty,
                                                  const llvm::Twine &Name) {
  return createMemTempWithoutCast(Ty, getContext().getTypeAlignInChars(Ty),
                                  Name);
}

llvm::Value *FunctionEmitter::evaluateExprAsBool(const Expr *E) {
  QualType BoolTy = getContext().BoolTy;
  SourceLocation Loc = E->getExprLoc();
  FPOptionsRAII FPOptsRAII(*this, E);
  if (!E->getType()->isAnyComplexType())
    return genScalarConversion(genScalarExpr(E), E->getType(), BoolTy, Loc);

  return genComplexToScalarConversion(genComplexExpr(E), E->getType(), BoolTy,
                                      Loc);
}

__attribute__((hot)) void FunctionEmitter::genIgnoredExpr(const Expr *E) {
  if (E->isPRValue())
    return (void)genAnyExpr(E, AggValueSlot::ignored(), true);

  // if this is a bitfield-resulting conditional operator, we can special case
  // emit this. The normal 'genLValue' version of this is particularly
  // difficult to codegen for, since creating a single "LValue" for two
  // different sized arguments here is not particularly doable.
  if (const auto *CondOp = dyn_cast<AbstractConditionalOperator>(
          E->IgnoreParenNoopCasts(getContext()))) {
    if (CondOp->getObjectKind() == OK_BitField)
      return genIgnoredConditionalOperator(CondOp);
  }

  // Just emit it as an l-value and drop the result.
  genLValue(E);
}

__attribute__((hot)) RValue FunctionEmitter::genAnyExpr(const Expr *E,
                                                        AggValueSlot aggSlot,
                                                        bool ignoreResult) {
  switch (getEvaluationKind(E->getType())) {
  case TEK_Scalar:
    return RValue::get(genScalarExpr(E, ignoreResult));
  case TEK_Complex:
    return RValue::getComplex(genComplexExpr(E, ignoreResult, ignoreResult));
  case TEK_Aggregate:
    if (!ignoreResult && aggSlot.isIgnored())
      aggSlot = createAggTemp(E->getType(), "agg-temp");
    genAggExpr(E, aggSlot);
    return aggSlot.asRValue();
  }
  llvm_unreachable("bad evaluation kind");
}

RValue FunctionEmitter::genAnyExprToTemp(const Expr *E) {
  AggValueSlot AggSlot = AggValueSlot::ignored();

  if (hasAggregateEvaluationKind(E->getType()))
    AggSlot = createAggTemp(E->getType(), "agg.tmp");
  return genAnyExpr(E, AggSlot);
}

void FunctionEmitter::genAnyExprToMem(const Expr *E, Address Location,
                                      Qualifiers Quals, bool IsInit) {
  switch (getEvaluationKind(E->getType())) {
  case TEK_Complex:
    genComplexExprIntoLValue(E, makeAddrLValue(Location, E->getType()),
                             /*isInit*/ false);
    return;

  case TEK_Aggregate: {
    genAggExpr(E, AggValueSlot::forAddr(Location, Quals,
                                        AggValueSlot::IsDestructed_t(IsInit),
                                        AggValueSlot::IsAliased_t(!IsInit),
                                        AggValueSlot::MayOverlap));
    return;
  }

  case TEK_Scalar: {
    RValue RV = RValue::get(genScalarExpr(E, /*Ignore*/ false));
    LValue LV = makeAddrLValue(Location, E->getType());
    genStoreThroughLValue(RV, LV);
    return;
  }
  }
  llvm_unreachable("bad evaluation kind");
}

namespace {
bool isAAPCS(const TargetInfo &TargetInfo) {
  return TargetInfo.getABI().starts_with("aapcs");
}
} // namespace

unsigned FunctionEmitter::getAccessedFieldNo(unsigned Idx,
                                             const llvm::Constant *Elts) {
  return cast<llvm::ConstantInt>(Elts->getAggregateElement(Idx))
      ->getZExtValue();
}

llvm::Value *FunctionEmitter::loadPassedObjectSize(const Expr *E,
                                                   QualType EltTy) {
  TreeContext &C = getContext();
  uint64_t EltSize = C.getTypeSizeInChars(EltTy).getQuantity();
  if (!EltSize)
    return nullptr;

  auto *ArrayDeclRef = dyn_cast<DeclRefExpr>(E->IgnoreParenImpCasts());
  if (!ArrayDeclRef)
    return nullptr;

  auto *ParamDecl = dyn_cast<ParmVarDecl>(ArrayDeclRef->getDecl());
  if (!ParamDecl)
    return nullptr;

  auto *POSAttr = ParamDecl->getAttr<PassObjectSizeAttr>();
  if (!POSAttr)
    return nullptr;

  // Don't load the size if it's a lower bound.
  int POSType = POSAttr->getType();
  if (POSType != 0 && POSType != 1)
    return nullptr;

  auto PassedSizeIt = SizeArguments.find(ParamDecl);
  if (PassedSizeIt == SizeArguments.end())
    return nullptr;

  const ImplicitParamDecl *PassedSizeDecl = PassedSizeIt->second;
  assert(LocalDeclMap.contains(PassedSizeDecl) && "Passed size not loadable");
  Address AddrOfSize = LocalDeclMap.find(PassedSizeDecl)->second;
  llvm::Value *SizeInBytes = genLoadOfScalar(AddrOfSize, /*Volatile=*/false,
                                             C.getSizeType(), E->getExprLoc());
  llvm::Value *SizeOfElement =
      llvm::ConstantInt::get(SizeInBytes->getType(), EltSize);
  return Builder.CreateUDiv(SizeInBytes, SizeOfElement);
}

namespace {
llvm::Value *getArrayIndexingBound(
    FunctionEmitter &FE, const Expr *Base, QualType &IndexedType,
    LangOptions::StrictFlexArraysLevelKind StrictFlexArraysLevel) {
  // For the vector indexing extension, the bound is the number of elements.
  if (const VectorType *VT = Base->getType()->getAs<VectorType>()) {
    IndexedType = Base->getType();
    return FE.Builder.getInt32(VT->getNumElements());
  }

  Base = Base->IgnoreParens();

  if (const auto *CE = dyn_cast<CastExpr>(Base)) {
    if (CE->getCastKind() == CK_ArrayToPointerDecay &&
        !CE->getSubExpr()->isFlexibleArrayMemberLike(FE.getContext(),
                                                     StrictFlexArraysLevel)) {
      IndexedType = CE->getSubExpr()->getType();
      const ArrayType *AT = IndexedType->castAsArrayTypeUnsafe();
      if (const auto *CAT = dyn_cast<ConstantArrayType>(AT))
        return FE.Builder.getInt(CAT->getSize());
      else if (const auto *VAT = dyn_cast<VariableArrayType>(AT))
        return FE.getVLASize(VAT).NumElts;
      // Ignore pass_object_size here. It's not applicable on decayed pointers.
    }
  }

  QualType EltTy{Base->getType()->getPointeeOrArrayElementType(), 0};
  if (llvm::Value *POS = FE.loadPassedObjectSize(Base, EltTy)) {
    IndexedType = Base->getType();
    return POS;
  }

  return nullptr;
}
} // namespace

FunctionEmitter::ComplexPairTy
FunctionEmitter::genComplexPrePostIncDec(const UnaryOperator *E, LValue LV,
                                         bool isInc, bool isPre) {
  ComplexPairTy InVal = genLoadOfComplex(LV, E->getExprLoc());

  llvm::Value *NextVal;
  if (isa<llvm::IntegerType>(InVal.first->getType())) {
    uint64_t AmountVal = isInc ? 1 : -1;
    NextVal = llvm::ConstantInt::get(InVal.first->getType(), AmountVal, true);

    NextVal = Builder.CreateAdd(InVal.first, NextVal, isInc ? "inc" : "dec");
  } else {
    QualType ElemTy = E->getType()->castAs<ComplexType>()->getElementType();
    llvm::APFloat FVal(getContext().getFloatTypeSemantics(ElemTy), 1);
    if (!isInc)
      FVal.changeSign();
    NextVal = llvm::ConstantFP::get(getLLVMContext(), FVal);

    NextVal = Builder.CreateFAdd(InVal.first, NextVal, isInc ? "inc" : "dec");
  }

  ComplexPairTy IncVal(NextVal, InVal.second);

  genStoreOfComplex(IncVal, LV, /*init*/ false);

  // If this is a postinc, return the value read from memory, otherwise use the
  // updated value.
  return isPre ? IncVal : InVal;
}

void ModuleEmitter::genExplicitCastExprType(const ExplicitCastExpr *E,
                                            FunctionEmitter *FE) {
  // Bind VLAs in the cast type.
  if (FE && E->getType()->isVariablyModifiedType())
    FE->genVariablyModifiedType(E->getType());

  if (DebugEmitter *DI = getModuleDebugInfo())
    DI->genExplicitCastType(E->getType());
}

// ===----------------------------------------------------------------------===
// Pointer alignment & LValue emission
// ===----------------------------------------------------------------------===

namespace {
Address genPointerWithAlignment(const Expr *E, LValueBaseInfo *BaseInfo,
                                TBAAAccessInfo *TBAAInfo,
                                KnownNonNull_t IsKnownNonNull,
                                FunctionEmitter &FE) {
  assert(E->getType()->isPointerType());
  E = E->IgnoreParens();

  // Casts:
  if (const CastExpr *CE = dyn_cast<CastExpr>(E)) {
    if (const auto *ECE = dyn_cast<ExplicitCastExpr>(CE))
      FE.ME.genExplicitCastExprType(ECE, &FE);

    switch (CE->getCastKind()) {
    // Non-converting casts (but not C's implicit conversion from void*).
    case CK_BitCast:
    case CK_NoOp:
    case CK_AddressSpaceConversion:
      if (auto PtrTy = CE->getSubExpr()->getType()->getAs<PointerType>()) {
        if (PtrTy->getPointeeType()->isVoidType())
          break;

        LValueBaseInfo InnerBaseInfo;
        TBAAAccessInfo InnerTBAAInfo;
        Address Addr = FE.genPointerWithAlignment(
            CE->getSubExpr(), &InnerBaseInfo, &InnerTBAAInfo, IsKnownNonNull);
        if (BaseInfo)
          *BaseInfo = InnerBaseInfo;
        if (TBAAInfo)
          *TBAAInfo = InnerTBAAInfo;

        if (isa<ExplicitCastExpr>(CE)) {
          LValueBaseInfo TargetTypeBaseInfo;
          TBAAAccessInfo TargetTypeTBAAInfo;
          CharUnits Align = FE.ME.getNaturalPointeeTypeAlignment(
              E->getType(), &TargetTypeBaseInfo, &TargetTypeTBAAInfo);
          if (TBAAInfo)
            *TBAAInfo =
                FE.ME.mergeTBAAInfoForCast(*TBAAInfo, TargetTypeTBAAInfo);
          // If the source l-value is opaque, honor the alignment of the
          // casted-to type.
          if (InnerBaseInfo.getAlignmentSource() != AlignmentSource::Decl) {
            if (BaseInfo)
              BaseInfo->mergeForCast(TargetTypeBaseInfo);
            Addr = Address(Addr.getPointer(), Addr.getElementType(), Align,
                           IsKnownNonNull);
          }
        }

        llvm::Type *ElemTy =
            FE.convertTypeForMem(E->getType()->getPointeeType());
        Addr = Addr.withElementType(ElemTy);
        if (CE->getCastKind() == CK_AddressSpaceConversion)
          Addr = FE.Builder.CreateAddrSpaceCast(Addr,
                                                FE.convertType(E->getType()));
        return Addr;
      }
      break;

    // Array-to-pointer decay.
    case CK_ArrayToPointerDecay:
      return FE.genArrayToPointerDecay(CE->getSubExpr(), BaseInfo, TBAAInfo);

    default:
      break;
    }
  }

  // Unary &.
  if (const UnaryOperator *UO = dyn_cast<UnaryOperator>(E)) {
    if (UO->getOpcode() == UO_AddrOf) {
      LValue LV = FE.genLValue(UO->getSubExpr(), IsKnownNonNull);
      if (BaseInfo)
        *BaseInfo = LV.getBaseInfo();
      if (TBAAInfo)
        *TBAAInfo = LV.getTBAAInfo();
      return LV.getAddress(FE);
    }
  }

  // Otherwise, use the alignment of the type.
  CharUnits Align =
      FE.ME.getNaturalPointeeTypeAlignment(E->getType(), BaseInfo, TBAAInfo);
  llvm::Type *ElemTy = FE.convertTypeForMem(E->getType()->getPointeeType());
  return Address(FE.genScalarExpr(E), ElemTy, Align, IsKnownNonNull);
}
} // namespace

Address FunctionEmitter::genPointerWithAlignment(
    const Expr *E, LValueBaseInfo *BaseInfo, TBAAAccessInfo *TBAAInfo,
    KnownNonNull_t IsKnownNonNull) {
  Address Addr =
      ::genPointerWithAlignment(E, BaseInfo, TBAAInfo, IsKnownNonNull, *this);
  if (IsKnownNonNull && !Addr.isKnownNonNull())
    Addr.setKnownNonNull();
  return Addr;
}

RValue FunctionEmitter::getUndefRValue(QualType Ty) {
  if (Ty->isVoidType())
    return RValue::get(nullptr);

  switch (getEvaluationKind(Ty)) {
  case TEK_Complex: {
    llvm::Type *EltTy =
        convertType(Ty->castAs<ComplexType>()->getElementType());
    llvm::Value *U = llvm::UndefValue::get(EltTy);
    return RValue::getComplex(std::make_pair(U, U));
  }

  // If this is a use of an undefined aggregate type, the aggregate must have an
  // identifiable address.  Just because the contents of the value are undefined
  // doesn't mean that the address can't be taken and compared.
  case TEK_Aggregate: {
    Address DestPtr = createMemTemp(Ty, "undef.agg.tmp");
    return RValue::getAggregate(DestPtr);
  }

  case TEK_Scalar:
    return RValue::get(llvm::UndefValue::get(convertType(Ty)));
  }
  llvm_unreachable("bad evaluation kind");
}

RValue FunctionEmitter::genUnsupportedRValue(const Expr *E, const char *Name) {
  errorUnsupported(E, Name);
  return getUndefRValue(E->getType());
}

LValue FunctionEmitter::genUnsupportedLValue(const Expr *E, const char *Name) {
  errorUnsupported(E, Name);
  llvm::Type *ElTy = convertType(E->getType());
  llvm::Type *Ty = UnqualPtrTy;
  return makeAddrLValue(
      Address(llvm::UndefValue::get(Ty), ElTy, CharUnits::One()), E->getType());
}

LValue FunctionEmitter::genCheckedLValue(const Expr *E, TypeCheckKind) {
  return genLValue(E);
}

__attribute__((hot)) LValue
FunctionEmitter::genLValue(const Expr *E, KnownNonNull_t IsKnownNonNull) {
  LValue LV = genLValueHelper(E, IsKnownNonNull);
  if (IsKnownNonNull && !LV.isKnownNonNull())
    LV.setKnownNonNull();
  return LV;
}

__attribute__((hot)) LValue
FunctionEmitter::genLValueHelper(const Expr *E, KnownNonNull_t IsKnownNonNull) {
  ApplyDebugLocation DL(*this, E);
  switch (E->getStmtClass()) {
  default:
    return genUnsupportedLValue(E, "l-value expression");

  case Expr::BinaryOperatorClass:
    return genBinaryOperatorLValue(cast<BinaryOperator>(E));
  case Expr::CompoundAssignOperatorClass: {
    QualType Ty = E->getType();
    if (const AtomicType *AT = Ty->getAs<AtomicType>())
      Ty = AT->getValueType();
    if (!Ty->isAnyComplexType())
      return genCompoundAssignmentLValue(cast<CompoundAssignOperator>(E));
    return genComplexCompoundAssignmentLValue(cast<CompoundAssignOperator>(E));
  }
  case Expr::CallExprClass:
    return genCallExprLValue(cast<CallExpr>(E));
  case Expr::VAArgExprClass:
    return genVAArgExprLValue(cast<VAArgExpr>(E));
  case Expr::DeclRefExprClass:
    return genDeclRefLValue(cast<DeclRefExpr>(E));
  case Expr::ConstantExprClass: {
    const ConstantExpr *CE = cast<ConstantExpr>(E);
    if (llvm::Value *Result = ConstantEmitter(*this).tryEmitConstantExpr(CE)) {
      QualType RetType = cast<CallExpr>(CE->getSubExpr()->IgnoreImplicit())
                             ->getCallReturnType(getContext())
                             ->getPointeeType();
      return makeNaturalAlignAddrLValue(Result, RetType);
    }
    return genLValue(cast<ConstantExpr>(E)->getSubExpr(), IsKnownNonNull);
  }
  case Expr::ParenExprClass:
    return genLValue(cast<ParenExpr>(E)->getSubExpr(), IsKnownNonNull);
  case Expr::GenericSelectionExprClass:
    return genLValue(cast<GenericSelectionExpr>(E)->getResultExpr(),
                     IsKnownNonNull);
  case Expr::PredefinedExprClass:
    return genPredefinedLValue(cast<PredefinedExpr>(E));
  case Expr::StringLiteralClass:
    return genStringLiteralLValue(cast<StringLiteral>(E));
  case Expr::PseudoObjectExprClass:
    return genPseudoObjectLValue(cast<PseudoObjectExpr>(E));
  case Expr::InitListExprClass:
    return genInitListLValue(cast<InitListExpr>(E));

  case Expr::ExprWithCleanupsClass: {
    const auto *cleanups = cast<ExprWithCleanups>(E);
    RunCleanupsScope Scope(*this);
    LValue LV = genLValue(cleanups->getSubExpr(), IsKnownNonNull);
    if (LV.isSimple()) {
      // Defend against branches out of gnu statement expressions surrounded by
      // cleanups.
      Address Addr = LV.getAddress(*this);
      llvm::Value *V = Addr.getPointer();
      Scope.ForceCleanup({&V});
      return LValue::MakeAddr(Addr.withPointer(V, Addr.isKnownNonNull()),
                              LV.getType(), getContext(), LV.getBaseInfo(),
                              LV.getTBAAInfo());
    }
    return LV;
  }

  case Expr::StmtExprClass:
    return genStmtExprLValue(cast<StmtExpr>(E));
  case Expr::UnaryOperatorClass:
    return genUnaryOpLValue(cast<UnaryOperator>(E));
  case Expr::ArraySubscriptExprClass:
    return genArraySubscriptExpr(cast<ArraySubscriptExpr>(E));
  case Expr::MatrixSubscriptExprClass:
    return genMatrixSubscriptExpr(cast<MatrixSubscriptExpr>(E));
  case Expr::ExtVectorElementExprClass:
    return genExtVectorElementExpr(cast<ExtVectorElementExpr>(E));
  case Expr::MemberExprClass:
    return genMemberExpr(cast<MemberExpr>(E));
  case Expr::CompoundLiteralExprClass:
    return genCompoundLiteralLValue(cast<CompoundLiteralExpr>(E));
  case Expr::ConditionalOperatorClass:
    return genConditionalOperatorLValue(cast<ConditionalOperator>(E));
  case Expr::BinaryConditionalOperatorClass:
    return genConditionalOperatorLValue(cast<BinaryConditionalOperator>(E));
  case Expr::ChooseExprClass:
    return genLValue(cast<ChooseExpr>(E)->getChosenSubExpr(), IsKnownNonNull);
  case Expr::OpaqueValueExprClass:
    return genOpaqueValueLValue(cast<OpaqueValueExpr>(E));
  case Expr::ImplicitCastExprClass:
  case Expr::CStyleCastExprClass:
    return genCastLValue(cast<CastExpr>(E));
  }
}

// ===----------------------------------------------------------------------===
// LValue & store/load helpers
// ===----------------------------------------------------------------------===

namespace {

bool isConstantEmittableObjectType(QualType type) {
  assert(type.isCanonical());

  // Must be const-qualified but non-volatile.
  Qualifiers qs = type.getLocalQualifiers();
  if (!qs.hasConst() || qs.hasVolatile())
    return false;

  return true;
}
} // namespace

FunctionEmitter::ConstantEmission
FunctionEmitter::tryEmitAsConstant(DeclRefExpr *refExpr) {
  ValueDecl *value = refExpr->getDecl();

  if (isa<ParmVarDecl>(value))
    return ConstantEmission();
  if (auto *var = dyn_cast<VarDecl>(value)) {
    if (!isConstantEmittableObjectType(var->getType().getCanonicalType()))
      return ConstantEmission();
  } else if (!isa<EnumConstantDecl>(value)) {
    return ConstantEmission();
  }

  Expr::EvalResult result;
  if (!refExpr->EvaluateAsRValue(result, getContext()))
    return ConstantEmission();

  if (result.HasSideEffects)
    return ConstantEmission();

  QualType resultType = refExpr->getType();
  auto C = ConstantEmitter(*this).emitAbstract(refExpr->getLocation(),
                                               result.Val, resultType);

  if (isa<VarDecl>(value)) {
    if (!getContext().DeclMustBeEmitted(cast<VarDecl>(value)))
      genDeclRefExprDbgValue(refExpr, result.Val);
  } else {
    assert(isa<EnumConstantDecl>(value));
    genDeclRefExprDbgValue(refExpr, result.Val);
  }

  return ConstantEmission::forValue(C);
}

// ===----------------------------------------------------------------------===
// Constant emission (member expressions → DeclRef)
// ===----------------------------------------------------------------------===

namespace {
DeclRefExpr *tryToConvertMemberExprToDeclRefExpr(FunctionEmitter &FE,
                                                 const MemberExpr *ME) {
  if (auto *VD = dyn_cast<VarDecl>(ME->getMemberDecl())) {
    // Try to emit static variable member expressions as DREs.
    return DeclRefExpr::Create(FE.getContext(), VD, ME->getExprLoc(),
                               ME->getType(), ME->getValueKind(), nullptr,
                               ME->isNonOdrUse());
  }
  return nullptr;
}
} // namespace

FunctionEmitter::ConstantEmission
FunctionEmitter::tryEmitAsConstant(const MemberExpr *ME) {
  if (DeclRefExpr *DRE = tryToConvertMemberExprToDeclRefExpr(*this, ME))
    return tryEmitAsConstant(DRE);
  return ConstantEmission();
}

llvm::Value *FunctionEmitter::emitScalarConstant(
    const FunctionEmitter::ConstantEmission &Constant) {
  assert(Constant && "not a constant");
  return Constant.getValue();
}

// ===----------------------------------------------------------------------===
// Scalar loads, TBAA & memory/register round-trip
// ===----------------------------------------------------------------------===

__attribute__((hot)) llvm::Value *
FunctionEmitter::genLoadOfScalar(LValue lvalue, SourceLocation Loc) {
  return genLoadOfScalar(lvalue.getAddress(*this), lvalue.isVolatile(),
                         lvalue.getType(), Loc, lvalue.getBaseInfo(),
                         lvalue.getTBAAInfo(), lvalue.isNontemporal());
}

namespace {
bool hasBooleanRepresentation(QualType Ty) {
  if (Ty->isBooleanType())
    return true;

  if (const EnumType *ET = Ty->getAs<EnumType>())
    return ET->getDecl()->getIntegerType()->isBooleanType();

  if (const AtomicType *AT = Ty->getAs<AtomicType>())
    return hasBooleanRepresentation(AT->getValueType());

  return false;
}

bool getRangeForType(FunctionEmitter &FE, QualType Ty, llvm::APInt &Min,
                     llvm::APInt &End, bool IsBool) {
  if (!IsBool)
    return false;
  Min = llvm::APInt(FE.getContext().getTypeSize(Ty), 0);
  End = llvm::APInt(FE.getContext().getTypeSize(Ty), 2);
  return true;
}
} // namespace

llvm::MDNode *FunctionEmitter::getRangeForLoadFromType(QualType Ty) {
  llvm::APInt Min, End;
  if (!getRangeForType(*this, Ty, Min, End, hasBooleanRepresentation(Ty)))
    return nullptr;

  llvm::MDBuilder MDHelper(getLLVMContext());
  return MDHelper.createRange(Min, End);
}

__attribute__((hot)) llvm::Value *
FunctionEmitter::genLoadOfScalar(Address Addr, bool Volatile, QualType Ty,
                                 SourceLocation Loc, LValueBaseInfo BaseInfo,
                                 TBAAAccessInfo TBAAInfo, bool isNontemporal) {
  if (LLVM_UNLIKELY(isa<llvm::GlobalValue>(Addr.getPointer()))) {
    auto *GV = cast<llvm::GlobalValue>(Addr.getPointer());
    if (GV->isThreadLocal())
      Addr = Addr.withPointer(Builder.CreateThreadLocalAddress(GV),
                              NotKnownNonNull);
  }

  if (LLVM_UNLIKELY(Ty->isVectorType())) {
    const auto *ExtVecTy = Ty->getAs<VectorType>();
    // Boolean vectors use `iN` as storage type.
    if (ExtVecTy->isExtVectorBoolType()) {
      llvm::Type *ValTy = convertType(Ty);
      unsigned ValNumElems =
          cast<llvm::FixedVectorType>(ValTy)->getNumElements();
      auto *RawIntV = Builder.CreateLoad(Addr, Volatile, "load_bits");
      const auto *RawIntTy = RawIntV->getType();
      assert(RawIntTy->isIntegerTy() && "compressed iN storage for bitvectors");
      // Bitcast iP --> <P x i1>.
      auto *PaddedVecTy = llvm::FixedVectorType::get(
          Builder.getInt1Ty(), RawIntTy->getPrimitiveSizeInBits());
      llvm::Value *V = Builder.CreateBitCast(RawIntV, PaddedVecTy);
      // Shuffle <P x i1> --> <N x i1> (N is the actual bit size).
      V = emitBoolVecConversion(V, ValNumElems, "extractvec");

      return genFromMemory(V, Ty);
    }

    // Handle vectors of size 3 like size 4 for better performance.
    const llvm::Type *EltTy = Addr.getElementType();
    const auto *VTy = cast<llvm::FixedVectorType>(EltTy);

    if (VTy->getNumElements() == 3) {

      llvm::VectorType *vec4Ty =
          llvm::FixedVectorType::get(VTy->getElementType(), 4);
      Address Cast = Addr.withElementType(vec4Ty);
      // Now load value.
      llvm::Value *V = Builder.CreateLoad(Cast, Volatile, "loadVec4");

      // Shuffle vector to get vec3.
      V = Builder.CreateShuffleVector(V, llvm::ArrayRef<int>{0, 1, 2},
                                      "extractVec");
      return genFromMemory(V, Ty);
    }
  }

  // Atomic operations have to be done on integral types.
  LValue AtomicLValue =
      LValue::MakeAddr(Addr, Ty, getContext(), BaseInfo, TBAAInfo);
  if (Ty->isAtomicType() || lValueIsSuitableForInlineAtomic(AtomicLValue)) {
    return genAtomicLoad(AtomicLValue, Loc).getScalarVal();
  }

  llvm::LoadInst *Load = Builder.CreateLoad(Addr, Volatile);
  if (isNontemporal) {
    llvm::MDNode *Node = llvm::MDNode::get(
        Load->getContext(), llvm::ConstantAsMetadata::get(Builder.getInt32(1)));
    Load->setMetadata(llvm::LLVMContext::MD_nontemporal, Node);
  }

  ME.decorateInstructionWithTBAA(Load, TBAAInfo);

  if (ME.getCodeGenOpts().OptimizationLevel > 0)
    if (llvm::MDNode *RangeInfo = getRangeForLoadFromType(Ty)) {
      Load->setMetadata(llvm::LLVMContext::MD_range, RangeInfo);
      Load->setMetadata(llvm::LLVMContext::MD_noundef,
                        llvm::MDNode::get(getLLVMContext(), std::nullopt));
    }

  return genFromMemory(Load, Ty);
}

llvm::Value *FunctionEmitter::genToMemory(llvm::Value *Value, QualType Ty) {
  // Bool has a different representation in memory than in registers.
  if (hasBooleanRepresentation(Ty)) {
    // This should really always be an i1, but sometimes it's already
    // an i8, and it's awkward to track those cases down.
    if (Value->getType()->isIntegerTy(1))
      return Builder.CreateZExt(Value, convertTypeForMem(Ty), "frombool");
    assert(Value->getType()->isIntegerTy(getContext().getTypeSize(Ty)) &&
           "wrong value rep of bool");
  }

  return Value;
}

llvm::Value *FunctionEmitter::genFromMemory(llvm::Value *Value, QualType Ty) {
  // Bool has a different representation in memory than in registers.
  if (hasBooleanRepresentation(Ty)) {
    assert(Value->getType()->isIntegerTy(getContext().getTypeSize(Ty)) &&
           "wrong value rep of bool");
    return Builder.CreateTrunc(Value, Builder.getInt1Ty(), "tobool");
  }
  if (Ty->isExtVectorBoolType()) {
    const auto *RawIntTy = Value->getType();
    // Bitcast iP --> <P x i1>.
    auto *PaddedVecTy = llvm::FixedVectorType::get(
        Builder.getInt1Ty(), RawIntTy->getPrimitiveSizeInBits());
    auto *V = Builder.CreateBitCast(Value, PaddedVecTy);
    // Shuffle <P x i1> --> <N x i1> (N is the actual bit size).
    llvm::Type *ValTy = convertType(Ty);
    unsigned ValNumElems = cast<llvm::FixedVectorType>(ValTy)->getNumElements();
    return emitBoolVecConversion(V, ValNumElems, "extractvec");
  }

  return Value;
}

namespace {

Address maybeConvertMatrixAddress(Address Addr, FunctionEmitter &FE,
                                  bool IsVector = true) {
  auto *ArrayTy = dyn_cast<llvm::ArrayType>(Addr.getElementType());
  if (ArrayTy && IsVector) {
    auto *VectorTy = llvm::FixedVectorType::get(ArrayTy->getElementType(),
                                                ArrayTy->getNumElements());

    return Addr.withElementType(VectorTy);
  }
  auto *VectorTy = dyn_cast<llvm::VectorType>(Addr.getElementType());
  if (VectorTy && !IsVector) {
    auto *ArrayTy = llvm::ArrayType::get(
        VectorTy->getElementType(),
        cast<llvm::FixedVectorType>(VectorTy)->getNumElements());

    return Addr.withElementType(ArrayTy);
  }

  return Addr;
}

// May need to cast the pointer from ArrayType (memory) to VectorType (value).
void genStoreOfMatrixScalar(llvm::Value *value, LValue lvalue, bool isInit,
                            FunctionEmitter &FE) {
  Address Addr = maybeConvertMatrixAddress(lvalue.getAddress(FE), FE,
                                           value->getType()->isVectorTy());
  FE.genStoreOfScalar(value, Addr, lvalue.isVolatile(), lvalue.getType(),
                      lvalue.getBaseInfo(), lvalue.getTBAAInfo(), isInit,
                      lvalue.isNontemporal());
}
} // namespace

__attribute__((hot)) void FunctionEmitter::genStoreOfScalar(
    llvm::Value *Value, Address Addr, bool Volatile, QualType Ty,
    LValueBaseInfo BaseInfo, TBAAAccessInfo TBAAInfo, bool isInit,
    bool isNontemporal) {
  if (LLVM_UNLIKELY(isa<llvm::GlobalValue>(Addr.getPointer()))) {
    auto *GV = cast<llvm::GlobalValue>(Addr.getPointer());
    if (GV->isThreadLocal())
      Addr = Addr.withPointer(Builder.CreateThreadLocalAddress(GV),
                              NotKnownNonNull);
  }

  llvm::Type *SrcTy = Value->getType();
  if (LLVM_UNLIKELY(Ty->isVectorType())) {
    const auto *ExtVecTy = Ty->getAs<VectorType>();
    auto *VecTy = dyn_cast<llvm::FixedVectorType>(SrcTy);
    if (VecTy && ExtVecTy->isExtVectorBoolType()) {
      auto *MemIntTy = cast<llvm::IntegerType>(Addr.getElementType());
      // Expand to the memory bit width.
      unsigned MemNumElems = MemIntTy->getPrimitiveSizeInBits();
      // <N x i1> --> <P x i1>.
      Value = emitBoolVecConversion(Value, MemNumElems, "insertvec");
      // <P x i1> --> iP.
      Value = Builder.CreateBitCast(Value, MemIntTy);
    } else {
      if (VecTy && cast<llvm::FixedVectorType>(VecTy)->getNumElements() == 3) {
        // Our source is a vec3, do a shuffle vector to make it a vec4.
        Value = Builder.CreateShuffleVector(
            Value, llvm::ArrayRef<int>{0, 1, 2, -1}, "extractVec");
        SrcTy = llvm::FixedVectorType::get(VecTy->getElementType(), 4);
      }
      if (Addr.getElementType() != SrcTy) {
        Addr = Addr.withElementType(SrcTy);
      }
    }
  }

  Value = genToMemory(Value, Ty);

  LValue AtomicLValue =
      LValue::MakeAddr(Addr, Ty, getContext(), BaseInfo, TBAAInfo);
  if (Ty->isAtomicType() ||
      (!isInit && lValueIsSuitableForInlineAtomic(AtomicLValue))) {
    genAtomicStore(RValue::get(Value), AtomicLValue, isInit);
    return;
  }

  llvm::StoreInst *Store = Builder.CreateStore(Value, Addr, Volatile);
  if (isNontemporal) {
    llvm::MDNode *Node =
        llvm::MDNode::get(Store->getContext(),
                          llvm::ConstantAsMetadata::get(Builder.getInt32(1)));
    Store->setMetadata(llvm::LLVMContext::MD_nontemporal, Node);
  }

  ME.decorateInstructionWithTBAA(Store, TBAAInfo);
}

void FunctionEmitter::genStoreOfScalar(llvm::Value *value, LValue lvalue,
                                       bool isInit) {
  if (lvalue.getType()->isConstantMatrixType()) {
    genStoreOfMatrixScalar(value, lvalue, isInit, *this);
    return;
  }

  genStoreOfScalar(value, lvalue.getAddress(*this), lvalue.isVolatile(),
                   lvalue.getType(), lvalue.getBaseInfo(), lvalue.getTBAAInfo(),
                   isInit, lvalue.isNontemporal());
}

namespace {
RValue genLoadOfMatrixLValue(LValue LV, SourceLocation Loc,
                             FunctionEmitter &FE) {
  assert(LV.getType()->isConstantMatrixType());
  Address Addr = maybeConvertMatrixAddress(LV.getAddress(FE), FE);
  LV.setAddress(Addr);
  return RValue::get(FE.genLoadOfScalar(LV, Loc));
}
} // namespace

__attribute__((hot)) RValue
FunctionEmitter::genLoadOfLValue(LValue LV, SourceLocation Loc) {

  if (LV.isSimple()) {
    assert(!LV.getType()->isFunctionType());

    if (LV.getType()->isConstantMatrixType())
      return genLoadOfMatrixLValue(LV, Loc, *this);

    // Everything needs a load.
    return RValue::get(genLoadOfScalar(LV, Loc));
  }

  if (LV.isVectorElt()) {
    llvm::LoadInst *Load =
        Builder.CreateLoad(LV.getVectorAddress(), LV.isVolatileQualified());
    return RValue::get(
        Builder.CreateExtractElement(Load, LV.getVectorIdx(), "vecext"));
  }

  // If this is a reference to a subset of the elements of a vector, either
  // shuffle the input or extract/insert them as appropriate.
  if (LV.isExtVectorElt()) {
    return genLoadOfExtVectorElementLValue(LV);
  }

  // Global Register variables always invoke intrinsics
  if (LV.isGlobalReg())
    return genLoadOfGlobalRegLValue(LV);

  if (LV.isMatrixElt()) {
    llvm::Value *Idx = LV.getMatrixIdx();
    if (ME.getCodeGenOpts().OptimizationLevel > 0) {
      const auto *const MatTy = LV.getType()->castAs<ConstantMatrixType>();
      llvm::MatrixBuilder MB(Builder);
      MB.CreateIndexAssumption(Idx, MatTy->getNumElementsFlattened());
    }
    llvm::LoadInst *Load =
        Builder.CreateLoad(LV.getMatrixAddress(), LV.isVolatileQualified());
    return RValue::get(Builder.CreateExtractElement(Load, Idx, "matrixext"));
  }

  assert(LV.isBitField() && "Unknown LValue type!");
  return genLoadOfBitfieldLValue(LV, Loc);
}

RValue FunctionEmitter::genLoadOfBitfieldLValue(LValue LV, SourceLocation Loc) {
  const BitFieldInfo &Info = LV.getBitFieldInfo();

  llvm::Type *ResLTy = convertType(LV.getType());

  Address Ptr = LV.getBitFieldAddress();
  llvm::Value *Val =
      Builder.CreateLoad(Ptr, LV.isVolatileQualified(), "bf.load");

  bool UseVolatile = LV.isVolatileQualified() &&
                     Info.VolatileStorageSize != 0 && isAAPCS(ME.getTarget());
  const unsigned Offset = UseVolatile ? Info.VolatileOffset : Info.Offset;
  const unsigned StorageSize =
      UseVolatile ? Info.VolatileStorageSize : Info.StorageSize;
  if (Info.IsSigned) {
    assert(static_cast<unsigned>(Offset + Info.Size) <= StorageSize);
    unsigned HighBits = StorageSize - Offset - Info.Size;
    if (HighBits)
      Val = Builder.CreateShl(Val, HighBits, "bf.shl");
    if (Offset + HighBits)
      Val = Builder.CreateAShr(Val, Offset + HighBits, "bf.ashr");
  } else {
    if (Offset)
      Val = Builder.CreateLShr(Val, Offset, "bf.lshr");
    if (static_cast<unsigned>(Offset) + Info.Size < StorageSize)
      Val = Builder.CreateAnd(
          Val, llvm::APInt::getLowBitsSet(StorageSize, Info.Size), "bf.clear");
  }
  Val = Builder.CreateIntCast(Val, ResLTy, Info.IsSigned, "bf.cast");
  return RValue::get(Val);
}

RValue FunctionEmitter::genLoadOfExtVectorElementLValue(LValue LV) {
  llvm::Value *Vec =
      Builder.CreateLoad(LV.getExtVectorAddress(), LV.isVolatileQualified());

  const llvm::Constant *Elts = LV.getExtVectorElts();

  const VectorType *ExprVT = LV.getType()->getAs<VectorType>();
  if (!ExprVT) {
    unsigned InIdx = getAccessedFieldNo(0, Elts);
    llvm::Value *Elt = llvm::ConstantInt::get(SizeTy, InIdx);
    return RValue::get(Builder.CreateExtractElement(Vec, Elt));
  }

  unsigned NumResultElts = ExprVT->getNumElements();

  llvm::SmallVector<int, 4> Mask;
  for (unsigned i = 0; i != NumResultElts; ++i)
    Mask.push_back(getAccessedFieldNo(i, Elts));

  Vec = Builder.CreateShuffleVector(Vec, Mask);
  return RValue::get(Vec);
}

Address FunctionEmitter::genExtVectorElementLValue(LValue LV) {
  Address VectorAddress = LV.getExtVectorAddress();
  QualType EQT = LV.getType()->castAs<VectorType>()->getElementType();
  llvm::Type *VectorElementTy = ME.getTypes().convertType(EQT);

  Address CastToPointerElement = VectorAddress.withElementType(VectorElementTy);

  const llvm::Constant *Elts = LV.getExtVectorElts();
  unsigned ix = getAccessedFieldNo(0, Elts);

  Address VectorBasePtrPlusIx =
      Builder.CreateConstInBoundsGEP(CastToPointerElement, ix, "vector.elt");

  return VectorBasePtrPlusIx;
}

RValue FunctionEmitter::genLoadOfGlobalRegLValue(LValue LV) {
  assert((LV.getType()->isIntegerType() || LV.getType()->isPointerType()) &&
         "Bad type for register variable");
  llvm::MDNode *RegName = cast<llvm::MDNode>(
      cast<llvm::MetadataAsValue>(LV.getGlobalReg())->getMetadata());

  // We accept integer and pointer types only
  llvm::Type *OrigTy = ME.getTypes().convertType(LV.getType());
  llvm::Type *Ty = OrigTy;
  if (OrigTy->isPointerTy())
    Ty = ME.getTypes().getDataLayout().getIntPtrType(OrigTy);
  llvm::Type *Types[] = {Ty};

  llvm::Function *F = ME.getIntrinsic(llvm::Intrinsic::read_register, Types);
  llvm::Value *Call = Builder.CreateCall(
      F, llvm::MetadataAsValue::get(Ty->getContext(), RegName));
  if (OrigTy->isPointerTy())
    Call = Builder.CreateIntToPtr(Call, OrigTy);
  return RValue::get(Call);
}

__attribute__((hot)) void
FunctionEmitter::genStoreThroughLValue(RValue Src, LValue Dst, bool isInit) {
  if (!Dst.isSimple()) {
    if (Dst.isVectorElt()) {
      // Read/modify/write the vector, inserting the new element.
      llvm::Value *Vec =
          Builder.CreateLoad(Dst.getVectorAddress(), Dst.isVolatileQualified());
      auto *IRStoreTy = dyn_cast<llvm::IntegerType>(Vec->getType());
      if (IRStoreTy) {
        auto *IRVecTy = llvm::FixedVectorType::get(
            Builder.getInt1Ty(), IRStoreTy->getPrimitiveSizeInBits());
        Vec = Builder.CreateBitCast(Vec, IRVecTy);
        // iN --> <N x i1>.
      }
      Vec = Builder.CreateInsertElement(Vec, Src.getScalarVal(),
                                        Dst.getVectorIdx(), "vecins");
      if (IRStoreTy) {
        // <N x i1> --> <iN>.
        Vec = Builder.CreateBitCast(Vec, IRStoreTy);
      }
      Builder.CreateStore(Vec, Dst.getVectorAddress(),
                          Dst.isVolatileQualified());
      return;
    }

    // If this is an update of extended vector elements, insert them as
    // appropriate.
    if (Dst.isExtVectorElt())
      return genStoreThroughExtVectorComponentLValue(Src, Dst);

    if (Dst.isGlobalReg())
      return genStoreThroughGlobalRegLValue(Src, Dst);

    if (Dst.isMatrixElt()) {
      llvm::Value *Idx = Dst.getMatrixIdx();
      if (ME.getCodeGenOpts().OptimizationLevel > 0) {
        const auto *const MatTy = Dst.getType()->castAs<ConstantMatrixType>();
        llvm::MatrixBuilder MB(Builder);
        MB.CreateIndexAssumption(Idx, MatTy->getNumElementsFlattened());
      }
      llvm::Instruction *Load = Builder.CreateLoad(Dst.getMatrixAddress());
      llvm::Value *Vec =
          Builder.CreateInsertElement(Load, Src.getScalarVal(), Idx, "matins");
      Builder.CreateStore(Vec, Dst.getMatrixAddress(),
                          Dst.isVolatileQualified());
      return;
    }

    assert(Dst.isBitField() && "Unknown LValue type");
    return genStoreThroughBitfieldLValue(Src, Dst);
  }

  assert(Src.isScalar() &&
         "expected scalar RValue for store through simple lvalue");
  genStoreOfScalar(Src.getScalarVal(), Dst, isInit);
}

void FunctionEmitter::genStoreThroughBitfieldLValue(RValue Src, LValue Dst,
                                                    llvm::Value **Result) {
  const BitFieldInfo &Info = Dst.getBitFieldInfo();
  llvm::Type *ResLTy = convertTypeForMem(Dst.getType());
  Address Ptr = Dst.getBitFieldAddress();

  llvm::Value *SrcVal = Src.getScalarVal();
  SrcVal = Builder.CreateIntCast(SrcVal, Ptr.getElementType(),
                                 /*isSigned=*/false);
  llvm::Value *MaskedVal = SrcVal;

  const bool UseVolatile =
      ME.getCodeGenOpts().AAPCSBitfieldWidth && Dst.isVolatileQualified() &&
      Info.VolatileStorageSize != 0 && isAAPCS(ME.getTarget());
  const unsigned StorageSize =
      UseVolatile ? Info.VolatileStorageSize : Info.StorageSize;
  const unsigned Offset = UseVolatile ? Info.VolatileOffset : Info.Offset;
  if (StorageSize != Info.Size) {
    assert(StorageSize > Info.Size && "Invalid bitfield size.");
    llvm::Value *Val =
        Builder.CreateLoad(Ptr, Dst.isVolatileQualified(), "bf.load");

    if (!hasBooleanRepresentation(Dst.getType()))
      SrcVal = Builder.CreateAnd(
          SrcVal, llvm::APInt::getLowBitsSet(StorageSize, Info.Size),
          "bf.value");
    MaskedVal = SrcVal;
    if (Offset)
      SrcVal = Builder.CreateShl(SrcVal, Offset, "bf.shl");

    Val = Builder.CreateAnd(
        Val, ~llvm::APInt::getBitsSet(StorageSize, Offset, Offset + Info.Size),
        "bf.clear");

    SrcVal = Builder.CreateOr(Val, SrcVal, "bf.set");
  } else {
    assert(Offset == 0);
    // According to the AACPS:
    // When a volatile bit-field is written, and its container does not overlap
    // with any non-bit-field member, its container must be read exactly once
    // and written exactly once using the access width appropriate to the type
    // of the container. The two accesses are not atomic.
  }

  Builder.CreateStore(SrcVal, Ptr, Dst.isVolatileQualified());

  if (Result) {
    llvm::Value *ResultVal = MaskedVal;

    if (Info.IsSigned) {
      assert(Info.Size <= StorageSize);
      unsigned HighBits = StorageSize - Info.Size;
      if (HighBits) {
        ResultVal = Builder.CreateShl(ResultVal, HighBits, "bf.result.shl");
        ResultVal = Builder.CreateAShr(ResultVal, HighBits, "bf.result.ashr");
      }
    }

    ResultVal = Builder.CreateIntCast(ResultVal, ResLTy, Info.IsSigned,
                                      "bf.result.cast");
    *Result = genFromMemory(ResultVal, Dst.getType());
  }
}

void FunctionEmitter::genStoreThroughExtVectorComponentLValue(RValue Src,
                                                              LValue Dst) {
  Address DstAddr = Dst.getExtVectorAddress();
  if (!DstAddr.getElementType()->isVectorTy()) {
    assert(!Dst.getType()->isVectorType() &&
           "this should only occur for non-vector l-values");
    Builder.CreateStore(Src.getScalarVal(), DstAddr, Dst.isVolatileQualified());
    return;
  }

  // This access turns into a read/modify/write of the vector.  Load the input
  // value now.
  llvm::Value *Vec = Builder.CreateLoad(DstAddr, Dst.isVolatileQualified());
  const llvm::Constant *Elts = Dst.getExtVectorElts();

  llvm::Value *SrcVal = Src.getScalarVal();

  if (const VectorType *VTy = Dst.getType()->getAs<VectorType>()) {
    unsigned NumSrcElts = VTy->getNumElements();
    unsigned NumDstElts =
        cast<llvm::FixedVectorType>(Vec->getType())->getNumElements();
    if (NumDstElts == NumSrcElts) {
      // Src and dst have the same element count: shuffle once using the
      // accessed-element mask.
      llvm::SmallVector<int, 4> Mask(NumDstElts);
      for (unsigned i = 0; i != NumSrcElts; ++i)
        Mask[getAccessedFieldNo(i, Elts)] = i;

      Vec = Builder.CreateShuffleVector(SrcVal, Mask);
    } else if (NumDstElts > NumSrcElts) {
      // Extend the source vector to the same length, then shuffle into place.
      llvm::SmallVector<int, 4> ExtMask;
      for (unsigned i = 0; i != NumSrcElts; ++i)
        ExtMask.push_back(i);
      ExtMask.resize(NumDstElts, -1);
      llvm::Value *ExtSrcVal = Builder.CreateShuffleVector(SrcVal, ExtMask);
      // build identity
      llvm::SmallVector<int, 4> Mask;
      for (unsigned i = 0; i != NumDstElts; ++i)
        Mask.push_back(i);

      // When the vector size is odd and .odd or .hi is used, the last element
      // of the Elts constant array will be one past the size of the vector.
      // Ignore the last element here, if it is greater than the mask size.
      if (getAccessedFieldNo(NumSrcElts - 1, Elts) == Mask.size())
        NumSrcElts--;

      // Overwrite the selected lanes from ExtSrcVal.
      for (unsigned i = 0; i != NumSrcElts; ++i)
        Mask[getAccessedFieldNo(i, Elts)] = i + NumDstElts;
      Vec = Builder.CreateShuffleVector(Vec, ExtSrcVal, Mask);
    } else {
      // We should never shorten the vector
      llvm_unreachable("unexpected shorten vector length");
    }
  } else {
    // If the Src is a scalar (not a vector), and the target is a vector it must
    // be updating one element.
    unsigned InIdx = getAccessedFieldNo(0, Elts);
    llvm::Value *Elt = llvm::ConstantInt::get(SizeTy, InIdx);
    Vec = Builder.CreateInsertElement(Vec, SrcVal, Elt);
  }

  Builder.CreateStore(Vec, Dst.getExtVectorAddress(),
                      Dst.isVolatileQualified());
}

void FunctionEmitter::genStoreThroughGlobalRegLValue(RValue Src, LValue Dst) {
  assert((Dst.getType()->isIntegerType() || Dst.getType()->isPointerType()) &&
         "Bad type for register variable");
  llvm::MDNode *RegName = cast<llvm::MDNode>(
      cast<llvm::MetadataAsValue>(Dst.getGlobalReg())->getMetadata());
  assert(RegName && "Register LValue is not metadata");

  // We accept integer and pointer types only
  llvm::Type *OrigTy = ME.getTypes().convertType(Dst.getType());
  llvm::Type *Ty = OrigTy;
  if (OrigTy->isPointerTy())
    Ty = ME.getTypes().getDataLayout().getIntPtrType(OrigTy);
  llvm::Type *Types[] = {Ty};

  llvm::Function *F = ME.getIntrinsic(llvm::Intrinsic::write_register, Types);
  llvm::Value *Value = Src.getScalarVal();
  if (OrigTy->isPointerTy())
    Value = Builder.CreatePtrToInt(Value, Ty);
  Builder.CreateCall(
      F, {llvm::MetadataAsValue::get(Ty->getContext(), RegName), Value});
}

Address FunctionEmitter::genLoadOfPointer(Address Ptr, const PointerType *PtrTy,
                                          LValueBaseInfo *BaseInfo,
                                          TBAAAccessInfo *TBAAInfo) {
  llvm::Value *Addr = Builder.CreateLoad(Ptr);
  return Address(Addr, convertTypeForMem(PtrTy->getPointeeType()),
                 ME.getNaturalTypeAlignment(PtrTy->getPointeeType(), BaseInfo,
                                            TBAAInfo,
                                            /*forPointeeType=*/true));
}

// ===----------------------------------------------------------------------===
// DeclRef, globals & function pointers
// ===----------------------------------------------------------------------===

namespace {
LValue genGlobalVarDeclLValue(FunctionEmitter &FE, const Expr *E,
                              const VarDecl *VD) {
  QualType T = E->getType();

  llvm::Value *V = FE.ME.getGlobalVarAddr(VD);

  if (VD->getTLSKind() != VarDecl::TLS_None)
    V = FE.Builder.CreateThreadLocalAddress(V);

  llvm::Type *RealVarTy = FE.getTypes().convertTypeForMem(VD->getType());
  CharUnits Alignment = FE.getContext().getDeclAlign(VD);
  Address Addr(V, RealVarTy, Alignment);
  return FE.makeAddrLValue(Addr, T, AlignmentSource::Decl);
}

llvm::Constant *genFunctionDeclPointer(ModuleEmitter &ME, GlobalDecl GD) {
  const FunctionDecl *FD = cast<FunctionDecl>(GD.getDecl());
  if (FD->hasAttr<WeakRefAttr>()) {
    ConstantAddress aliasee = ME.getWeakRefReference(FD);
    return aliasee.getPointer();
  }

  llvm::Constant *V = ME.addrOfFunction(GD);
  return V;
}

LValue genFunctionDeclLValue(FunctionEmitter &FE, const Expr *E,
                             GlobalDecl GD) {
  const FunctionDecl *FD = cast<FunctionDecl>(GD.getDecl());
  llvm::Value *V = genFunctionDeclPointer(FE.ME, GD);
  CharUnits Alignment = FE.getContext().getDeclAlign(FD);
  return FE.makeAddrLValue(V, E->getType(), Alignment, AlignmentSource::Decl);
}

LValue genGlobalNamedRegister(const VarDecl *VD, ModuleEmitter &ME) {
  llvm::SmallString<64> Name("llvm.named.register.");
  AsmLabelAttr *Asm = VD->getAttr<AsmLabelAttr>();
  assert(Asm->getLabel().size() < 64 - Name.size() && "Register name too big");
  Name.append(Asm->getLabel());
  llvm::NamedMDNode *M = ME.getModule().getOrInsertNamedMetadata(Name);
  if (M->getNumOperands() == 0) {
    llvm::MDString *Str =
        llvm::MDString::get(ME.getLLVMContext(), Asm->getLabel());
    llvm::Metadata *Ops[] = {Str};
    M->addOperand(llvm::MDNode::get(ME.getLLVMContext(), Ops));
  }

  CharUnits Alignment = ME.getContext().getDeclAlign(VD);

  llvm::Value *Ptr =
      llvm::MetadataAsValue::get(ME.getLLVMContext(), M->getOperand(0));
  return LValue::MakeGlobalReg(Ptr, Alignment, VD->getType());
}

bool canEmitSpuriousReferenceToVariable(FunctionEmitter &FE,
                                        const VarDecl *VD) {
  // For a local declaration declared in this function, we can always reference
  // it even if we don't have an odr-use.
  if (VD->hasLocalStorage()) {
    return VD->getDeclContext() ==
           dyn_cast_or_null<DeclContext>(FE.CurCodeDecl);
  }

  // For a global declaration, we can emit a reference to it if we know
  // for sure that we are able to emit a definition of it.
  VD = VD->getDefinition(FE.getContext());
  if (!VD)
    return false;

  // We can emit a spurious reference only if the linkage implies that we'll
  // be emitting a non-interposable symbol that will be retained until link
  // time.
  switch (FE.ME.getLLVMLinkageVarDefinition(VD)) {
  case llvm::GlobalValue::ExternalLinkage:
  case llvm::GlobalValue::LinkOnceODRLinkage:
  case llvm::GlobalValue::WeakODRLinkage:
  case llvm::GlobalValue::InternalLinkage:
  case llvm::GlobalValue::PrivateLinkage:
    return true;
  default:
    return false;
  }
}
} // namespace

__attribute__((hot)) LValue
FunctionEmitter::genDeclRefLValue(const DeclRefExpr *E) {
  const NamedDecl *ND = E->getDecl();
  QualType T = E->getType();

  assert(E->isNonOdrUse() != NOUR_Unevaluated &&
         "should not emit an unevaluated operand");

  if (const auto *VD = dyn_cast<VarDecl>(ND)) {
    // Global Named registers access via intrinsics only
    if (VD->getStorageClass() == SC_Register && VD->hasAttr<AsmLabelAttr>() &&
        !VD->isLocalVarDecl())
      return genGlobalNamedRegister(VD, ME);

    // If this DeclRefExpr does not constitute an odr-use of the variable,
    // we're not permitted to emit a reference to it in general, and it might
    // not be captured if capture would be necessary for a use. Emit the
    // constant value directly instead.
    if (E->isNonOdrUse() == NOUR_Constant &&
        !canEmitSpuriousReferenceToVariable(*this, VD)) {
      VD->getAnyInitializer(VD);
      llvm::Constant *Val = ConstantEmitter(*this).emitAbstract(
          E->getLocation(), *VD->evaluateValue(), VD->getType());
      assert(Val && "failed to emit constant expression");

      Address Addr =
          ME.createUnnamedGlobalFrom(*VD, Val, getContext().getDeclAlign(VD));
      llvm::Type *VarTy = getTypes().convertTypeForMem(VD->getType());
      auto *PTy = llvm::PointerType::get(
          VarTy, getTypes().getTargetAddressSpace(VD->getType()));
      Addr = Builder.CreatePointerBitCastOrAddrSpaceCast(Addr, PTy, VarTy);
      return makeAddrLValue(Addr, T, AlignmentSource::Decl);
    }
  }

  assert((ND->isUsed(false) || !isa<VarDecl>(ND) || E->isNonOdrUse() ||
          !E->getLocation().isValid()) &&
         "Should not use decl without marking it used!");

  if (ND->hasAttr<WeakRefAttr>()) {
    const auto *VD = cast<ValueDecl>(ND);
    ConstantAddress Aliasee = ME.getWeakRefReference(VD);
    return makeAddrLValue(Aliasee, T, AlignmentSource::Decl);
  }

  if (const auto *VD = dyn_cast<VarDecl>(ND)) {
    if (LLVM_UNLIKELY(VD->hasGlobalStorage()) && VD->hasLinkage())
      return genGlobalVarDeclLValue(*this, E, VD);

    Address addr = Address::invalid();

    // The variable should generally be present in the local decl map.
    auto iter = LocalDeclMap.find(VD);
    if (iter != LocalDeclMap.end()) {
      addr = iter->second;

      // Otherwise, it might be static local we haven't emitted yet for
      // some reason; most likely, because it's in an outer function.
    } else if (VD->isStaticLocal()) {
      llvm::Constant *var =
          ME.getOrCreateStaticVarDecl(*VD, ME.getLLVMLinkageVarDefinition(VD));
      addr = Address(var, convertTypeForMem(VD->getType()),
                     getContext().getDeclAlign(VD));

      // No other cases for now.
    } else {
      llvm_unreachable("DeclRefExpr for Decl not entered in LocalDeclMap?");
    }

    if (VD->getTLSKind() != VarDecl::TLS_None)
      addr = addr.withPointer(
          Builder.CreateThreadLocalAddress(addr.getPointer()), NotKnownNonNull);

    return makeAddrLValue(addr, T, AlignmentSource::Decl);
  }

  if (const auto *FD = dyn_cast<FunctionDecl>(ND)) {
    LValue LV = genFunctionDeclLValue(*this, E, FD);

    if (getContext().getTargetInfo().allowDebugInfoForExternalRef()) {
      if (DebugEmitter *DI = ME.getModuleDebugInfo()) {
        auto *Fn =
            cast<llvm::Function>(LV.getPointer(*this)->stripPointerCasts());
        if (!Fn->getSubprogram())
          DI->genFunctionDecl(FD, FD->getLocation(), T, Fn);
      }
    }

    return LV;
  }

  llvm_unreachable("Unhandled DeclRefExpr");
}

LValue FunctionEmitter::genUnaryOpLValue(const UnaryOperator *E) {
  // __extension__ doesn't affect lvalue-ness.
  if (E->getOpcode() == UO_Extension)
    return genLValue(E->getSubExpr());

  QualType ExprTy = getContext().getCanonicalType(E->getSubExpr()->getType());
  switch (E->getOpcode()) {
  default:
    llvm_unreachable("Unknown unary operator lvalue!");
  case UO_Deref: {
    QualType T = E->getSubExpr()->getType()->getPointeeType();
    assert(!T.isNull() && "FunctionEmitter::genUnaryOpLValue: Illegal type");

    LValueBaseInfo BaseInfo;
    TBAAAccessInfo TBAAInfo;
    Address Addr =
        genPointerWithAlignment(E->getSubExpr(), &BaseInfo, &TBAAInfo);
    LValue LV = makeAddrLValue(Addr, T, BaseInfo, TBAAInfo);
    LV.getQuals().setAddressSpace(ExprTy.getAddressSpace());

    return LV;
  }
  case UO_Real:
  case UO_Imag: {
    LValue LV = genLValue(E->getSubExpr());
    assert(LV.isSimple() && "real/imag on non-ordinary l-value");

    // __real is valid on scalars.  This is a faster way of testing that.
    // __imag can only produce an rvalue on scalars.
    if (E->getOpcode() == UO_Real &&
        !LV.getAddress(*this).getElementType()->isStructTy()) {
      assert(E->getSubExpr()->getType()->isArithmeticType());
      return LV;
    }

    QualType T = ExprTy->castAs<ComplexType>()->getElementType();

    Address Component =
        (E->getOpcode() == UO_Real
             ? emitAddrOfRealComponent(LV.getAddress(*this), LV.getType())
             : emitAddrOfImagComponent(LV.getAddress(*this), LV.getType()));
    LValue ElemLV = makeAddrLValue(Component, T, LV.getBaseInfo(),
                                   ME.getTBAAInfoForSubobject(LV, T));
    ElemLV.getQuals().addQualifiers(LV.getQuals());
    return ElemLV;
  }
  case UO_PreInc:
  case UO_PreDec: {
    LValue LV = genLValue(E->getSubExpr());
    bool isInc = E->getOpcode() == UO_PreInc;

    if (E->getType()->isAnyComplexType())
      genComplexPrePostIncDec(E, LV, isInc, true /*isPre*/);
    else
      genScalarPrePostIncDec(E, LV, isInc, true /*isPre*/);
    return LV;
  }
  }
}

LValue FunctionEmitter::genStringLiteralLValue(const StringLiteral *E) {
  return makeAddrLValue(ME.addrOfConstantStringFromLiteral(E), E->getType(),
                        AlignmentSource::Decl);
}

LValue FunctionEmitter::genPredefinedLValue(const PredefinedExpr *E) {
  auto SL = E->getFunctionName();
  assert(SL != nullptr && "No StringLiteral name in PredefinedExpr");
  llvm::StringRef FnName = CurFn->getName();
  if (FnName.starts_with("\01"))
    FnName = FnName.substr(1);
  llvm::StringRef NameItems[] = {
      PredefinedExpr::getIdentKindName(E->getIdentKind()), FnName};
  std::string GVName = llvm::join(NameItems, NameItems + 2, ".");
  auto C = ME.addrOfConstantStringFromLiteral(SL, GVName);
  return makeAddrLValue(C, E->getType(), AlignmentSource::Decl);
}

void FunctionEmitter::genUnreachable(SourceLocation) {
  Builder.CreateUnreachable();
}

void FunctionEmitter::genTrapCheck(llvm::Value *Checked,
                                   SanitizerHandler CheckHandlerID) {
  llvm::BasicBlock *Cont = createBasicBlock("cont");

  // If we're optimizing, collapse all calls to trap down to just one per
  // check-type per function to save on code size.
  if (TrapBBs.size() <= CheckHandlerID)
    TrapBBs.resize(CheckHandlerID + 1);

  llvm::BasicBlock *&TrapBB = TrapBBs[CheckHandlerID];

  if (!ClSanitizeDebugDeoptimization && ME.getCodeGenOpts().OptimizationLevel &&
      TrapBB && (!CurCodeDecl || !CurCodeDecl->hasAttr<OptimizeNoneAttr>())) {
    auto Call = TrapBB->begin();
    assert(isa<llvm::CallInst>(Call) && "Expected call in trap BB");

    Call->applyMergedLocation(Call->getDebugLoc(),
                              Builder.getCurrentDebugLocation());
    Builder.CreateCondBr(Checked, Cont, TrapBB);
  } else {
    TrapBB = createBasicBlock("trap");
    Builder.CreateCondBr(Checked, Cont, TrapBB);
    genBlock(TrapBB);

    llvm::CallInst *TrapCall = Builder.CreateCall(
        ME.getIntrinsic(llvm::Intrinsic::ubsantrap),
        llvm::ConstantInt::get(ME.Int8Ty, ClSanitizeDebugDeoptimization
                                              ? TrapBB->getParent()->size()
                                              : CheckHandlerID));

    if (!ME.getCodeGenOpts().TrapFuncName.empty()) {
      auto A = llvm::Attribute::get(getLLVMContext(), "trap-func-name",
                                    ME.getCodeGenOpts().TrapFuncName);
      TrapCall->addFnAttr(A);
    }
    TrapCall->setDoesNotReturn();
    TrapCall->setDoesNotThrow();
    Builder.CreateUnreachable();
  }

  genBlock(Cont);
}

llvm::CallInst *FunctionEmitter::genTrapCall(llvm::Intrinsic::ID IntrID) {
  llvm::CallInst *TrapCall = Builder.CreateCall(ME.getIntrinsic(IntrID));

  if (!ME.getCodeGenOpts().TrapFuncName.empty()) {
    auto A = llvm::Attribute::get(getLLVMContext(), "trap-func-name",
                                  ME.getCodeGenOpts().TrapFuncName);
    TrapCall->addFnAttr(A);
  }

  return TrapCall;
}

Address FunctionEmitter::genArrayToPointerDecay(const Expr *E,
                                                LValueBaseInfo *BaseInfo,
                                                TBAAAccessInfo *TBAAInfo) {
  assert(E->getType()->isArrayType() &&
         "Array to pointer decay must have array source type!");

  // Expressions of array type can't be bitfields or vector elements.
  LValue LV = genLValue(E);
  Address Addr = LV.getAddress(*this);

  // If the array type was an incomplete type, we need to make sure
  // the decay ends up being the right type.
  llvm::Type *NewTy = convertType(E->getType());
  Addr = Addr.withElementType(NewTy);

  // Note that VLA pointers are always decayed, so we don't need to do
  // anything here.
  if (!E->getType()->isVariableArrayType()) {
    assert(isa<llvm::ArrayType>(Addr.getElementType()) &&
           "Expected pointer to array");
    Addr = Builder.CreateConstArrayGEP(Addr, 0, "arraydecay");
  }

  // The result of this decay conversion points to an array element within the
  // base lvalue. However, since TBAA currently does not support representing
  // accesses to elements of member arrays, we conservatively represent accesses
  // to the pointee object as if it had no any base lvalue specified.
  QualType EltType = E->getType()->castAsArrayTypeUnsafe()->getElementType();
  if (BaseInfo)
    *BaseInfo = LV.getBaseInfo();
  if (TBAAInfo)
    *TBAAInfo = ME.getTBAAAccessInfo(EltType);

  return Addr.withElementType(convertTypeForMem(EltType));
}

// ===----------------------------------------------------------------------===
// Array subscript helpers
// ===----------------------------------------------------------------------===

namespace {
const Expr *isSimpleArrayDecayOperand(const Expr *E) {
  // If this isn't just an array->pointer decay, bail out.
  const auto *CE = dyn_cast<CastExpr>(E);
  if (!CE || CE->getCastKind() != CK_ArrayToPointerDecay)
    return nullptr;

  // If this is a decay from variable width array, bail out.
  const Expr *SubExpr = CE->getSubExpr();
  if (SubExpr->getType()->isVariableArrayType())
    return nullptr;

  return SubExpr;
}

llvm::Value *emitArraySubscriptGEP(FunctionEmitter &FE, llvm::Type *elemType,
                                   llvm::Value *ptr,
                                   llvm::ArrayRef<llvm::Value *> indices,
                                   bool inbounds, bool signedIndices,
                                   SourceLocation loc,
                                   const llvm::Twine &name = "arrayidx") {
  if (inbounds) {
    return FE.genCheckedInBoundsGEP(elemType, ptr, indices, signedIndices,
                                    FunctionEmitter::NotSubtraction, loc, name);
  } else {
    return FE.Builder.CreateGEP(elemType, ptr, indices, name);
  }
}

CharUnits getArrayElementAlign(CharUnits arrayAlign, llvm::Value *idx,
                               CharUnits eltSize) {
  // Constant index: use alignment at the exact element offset.
  if (auto constantIdx = dyn_cast<llvm::ConstantInt>(idx)) {
    CharUnits offset = constantIdx->getZExtValue() * eltSize;
    return arrayAlign.alignmentAtOffset(offset);
  }
  // Variable index: worst-case alignment for any element.
  return arrayAlign.alignmentOfArrayElement(eltSize);
}

QualType getFixedSizeElementType(const TreeContext &ctx,
                                 const VariableArrayType *vla) {
  QualType eltType;
  do {
    eltType = vla->getElementType();
  } while ((vla = ctx.getAsVariableArrayType(eltType)));
  return eltType;
}

bool isPreserveAIArrayBase(FunctionEmitter &FE, const Expr *ArrayBase) {
  if (!ArrayBase || !FE.getDebugInfo())
    return false;

  // Only support base as either a MemberExpr or DeclRefExpr.
  // DeclRefExpr to cover cases like:
  //    struct s { int a; int b[10]; };
  //    struct s *p;
  //    p[1].a
  // p[1] will generate a DeclRefExpr and p[1].a is a MemberExpr.
  // p->b[5] is a MemberExpr example.
  return false;
}

Address emitArraySubscriptGEP(FunctionEmitter &FE, Address addr,
                              llvm::ArrayRef<llvm::Value *> indices,
                              QualType eltType, bool inbounds,
                              bool signedIndices, SourceLocation loc,
                              QualType *arrayType = nullptr,
                              const Expr *Base = nullptr,
                              const llvm::Twine &name = "arrayidx") {
  // All the indices except that last must be zero.
#ifndef NDEBUG
  for (auto *idx : indices.drop_back())
    assert(isa<llvm::ConstantInt>(idx) &&
           cast<llvm::ConstantInt>(idx)->isZero());
#endif

  // Determine the element size of the statically-sized base.  This is
  // the thing that the indices are expressed in terms of.
  if (auto vla = FE.getContext().getAsVariableArrayType(eltType)) {
    eltType = getFixedSizeElementType(FE.getContext(), vla);
  }

  // We can use that to compute the best alignment of the element.
  CharUnits eltSize = FE.getContext().getTypeSizeInChars(eltType);
  CharUnits eltAlign =
      getArrayElementAlign(addr.getAlignment(), indices.back(), eltSize);

  llvm::Value *eltPtr;
  auto LastIndex = dyn_cast<llvm::ConstantInt>(indices.back());
  if (!LastIndex ||
      (!FE.IsInPreservedAIRegion && !isPreserveAIArrayBase(FE, Base))) {
    eltPtr = emitArraySubscriptGEP(FE, addr.getElementType(), addr.getPointer(),
                                   indices, inbounds, signedIndices, loc, name);
  } else {
    unsigned idx = LastIndex->getZExtValue();
    llvm::DIType *DbgInfo = nullptr;
    if (arrayType)
      DbgInfo = FE.getDebugInfo()->getOrCreateStandaloneType(*arrayType, loc);
    eltPtr = FE.Builder.CreatePreserveArrayAccessIndex(
        addr.getElementType(), addr.getPointer(), indices.size() - 1, idx,
        DbgInfo);
  }

  return Address(eltPtr, FE.convertTypeForMem(eltType), eltAlign);
}
} // namespace

LValue FunctionEmitter::genArraySubscriptExpr(const ArraySubscriptExpr *E,
                                              bool Accessed) {
  // The index must always be an integer, which is not an aggregate. Emit it
  // in the order required for subscripting (base vs index side effects).
  llvm::Value *IdxPre =
      (E->getLHS() == E->getIdx()) ? genScalarExpr(E->getIdx()) : nullptr;
  bool SignedIndices = false;
  auto genIdxAfterBase = [&, IdxPre](bool Promote) -> llvm::Value * {
    auto *Idx = IdxPre;
    if (E->getLHS() != E->getIdx()) {
      assert(E->getRHS() == E->getIdx() && "index was neither LHS nor RHS");
      Idx = genScalarExpr(E->getIdx());
    }

    QualType IdxTy = E->getIdx()->getType();
    bool IdxSigned = IdxTy->isSignedIntegerOrEnumerationType();
    SignedIndices |= IdxSigned;

    // Extend or truncate the index type to 32 or 64-bits.
    if (Promote && Idx->getType() != IntPtrTy)
      Idx = Builder.CreateIntCast(Idx, IntPtrTy, IdxSigned, "idxprom");

    return Idx;
  };
  IdxPre = nullptr;

  // If the base is a vector type, then we are forming a vector element lvalue
  // with this subscript.
  if (E->getBase()->getType()->isVectorType() &&
      !isa<ExtVectorElementExpr>(E->getBase())) {
    LValue LHS = genLValue(E->getBase());
    auto *Idx = genIdxAfterBase(/*Promote*/ false);
    assert(LHS.isSimple() && "Can only subscript lvalue vectors here!");
    return LValue::MakeVectorElt(LHS.getAddress(*this), Idx,
                                 E->getBase()->getType(), LHS.getBaseInfo(),
                                 TBAAAccessInfo());
  }

  // All the other cases basically behave like simple offsetting.

  if (isa<ExtVectorElementExpr>(E->getBase())) {
    LValue LV = genLValue(E->getBase());
    auto *Idx = genIdxAfterBase(/*Promote*/ true);
    Address Addr = genExtVectorElementLValue(LV);

    QualType EltType = LV.getType()->castAs<VectorType>()->getElementType();
    Addr = emitArraySubscriptGEP(*this, Addr, Idx, EltType, /*inbounds*/ true,
                                 SignedIndices, E->getExprLoc());
    return makeAddrLValue(Addr, EltType, LV.getBaseInfo(),
                          ME.getTBAAInfoForSubobject(LV, EltType));
  }

  LValueBaseInfo EltBaseInfo;
  TBAAAccessInfo EltTBAAInfo;
  Address Addr = Address::invalid();
  if (const VariableArrayType *vla =
          getContext().getAsVariableArrayType(E->getType())) {
    // The base must be a pointer, which is not an aggregate.  Emit
    // it.  It needs to be emitted first in case it's what captures
    // the VLA bounds.
    Addr = genPointerWithAlignment(E->getBase(), &EltBaseInfo, &EltTBAAInfo);
    auto *Idx = genIdxAfterBase(/*Promote*/ true);

    // The element count here is the total number of non-VLA elements.
    llvm::Value *numElements = getVLASize(vla).NumElts;

    // Effectively, the multiply by the VLA size is part of the GEP.
    // GEP indexes are signed, and scaling an index isn't permitted to
    // signed-overflow, so we use the same semantics for our explicit
    // multiply.  We suppress this if overflow is not undefined behavior.
    if (getLangOpts().isSignedOverflowDefined()) {
      Idx = Builder.CreateMul(Idx, numElements);
    } else {
      Idx = Builder.CreateNSWMul(Idx, numElements);
    }

    Addr = emitArraySubscriptGEP(*this, Addr, Idx, vla->getElementType(),
                                 !getLangOpts().isSignedOverflowDefined(),
                                 SignedIndices, E->getExprLoc());

  } else if (const Expr *Array = isSimpleArrayDecayOperand(E->getBase())) {
    // If this is A[i] where A is an array, the frontend will have decayed the
    // base to be a ArrayToPointerDecay implicit cast.  While correct, it is
    // inefficient at -O0 to emit a "gep A, 0, 0" when codegen'ing it, then a
    // "gep x, i" here.  Emit one "gep A, 0, i".
    assert(Array->getType()->isArrayType() &&
           "Array to pointer decay must have array source type!");
    LValue ArrayLV;
    // For simple multidimensional array indexing, set the 'accessed' flag for
    // better bounds-checking of the base expression.
    if (const auto *ASE = dyn_cast<ArraySubscriptExpr>(Array))
      ArrayLV = genArraySubscriptExpr(ASE, /*Accessed*/ true);
    else
      ArrayLV = genLValue(Array);
    auto *Idx = genIdxAfterBase(/*Promote*/ true);

    // Propagate the alignment from the array itself to the result.
    QualType arrayType = Array->getType();
    Addr = emitArraySubscriptGEP(
        *this, ArrayLV.getAddress(*this), {ME.getSize(CharUnits::Zero()), Idx},
        E->getType(), !getLangOpts().isSignedOverflowDefined(), SignedIndices,
        E->getExprLoc(), &arrayType, E->getBase());
    EltBaseInfo = ArrayLV.getBaseInfo();
    EltTBAAInfo = ME.getTBAAInfoForSubobject(ArrayLV, E->getType());
  } else {
    // The base must be a pointer; emit it with an estimate of its alignment.
    Addr = genPointerWithAlignment(E->getBase(), &EltBaseInfo, &EltTBAAInfo);
    auto *Idx = genIdxAfterBase(/*Promote*/ true);
    QualType ptrType = E->getBase()->getType();
    Addr = emitArraySubscriptGEP(*this, Addr, Idx, E->getType(),
                                 !getLangOpts().isSignedOverflowDefined(),
                                 SignedIndices, E->getExprLoc(), &ptrType,
                                 E->getBase());
  }

  LValue LV = makeAddrLValue(Addr, E->getType(), EltBaseInfo, EltTBAAInfo);

  return LV;
}

LValue FunctionEmitter::genMatrixSubscriptExpr(const MatrixSubscriptExpr *E) {
  assert(
      !E->isIncomplete() &&
      "incomplete matrix subscript expressions should be rejected during Sema");
  LValue Base = genLValue(E->getBase());
  llvm::Value *RowIdx = genScalarExpr(E->getRowIdx());
  llvm::Value *ColIdx = genScalarExpr(E->getColumnIdx());
  llvm::Value *NumRows = Builder.getIntN(
      RowIdx->getType()->getScalarSizeInBits(),
      E->getBase()->getType()->castAs<ConstantMatrixType>()->getNumRows());
  llvm::Value *FinalIdx =
      Builder.CreateAdd(Builder.CreateMul(ColIdx, NumRows), RowIdx);
  return LValue::MakeMatrixElt(
      maybeConvertMatrixAddress(Base.getAddress(*this), *this), FinalIdx,
      E->getBase()->getType(), Base.getBaseInfo(), TBAAAccessInfo());
}

LValue FunctionEmitter::genExtVectorElementExpr(const ExtVectorElementExpr *E) {
  LValue Base;

  // ExtVectorElementExpr's base can either be a vector or pointer to vector.
  if (E->isArrow()) {
    // If it is a pointer to a vector, emit the address and form an lvalue with
    // it.
    LValueBaseInfo BaseInfo;
    TBAAAccessInfo TBAAInfo;
    Address Ptr = genPointerWithAlignment(E->getBase(), &BaseInfo, &TBAAInfo);
    const auto *PT = E->getBase()->getType()->castAs<PointerType>();
    Base = makeAddrLValue(Ptr, PT->getPointeeType(), BaseInfo, TBAAInfo);
  } else if (E->getBase()->isLValue()) {
    // Otherwise, if the base is an lvalue ( as in the case of foo.x.x),
    // emit the base as an lvalue.
    assert(E->getBase()->getType()->isVectorType());
    Base = genLValue(E->getBase());
  } else {
    // Otherwise, the base is a normal rvalue (as in (V+V).x), emit it as such.
    assert(E->getBase()->getType()->isVectorType() &&
           "Result must be a vector");
    llvm::Value *Vec = genScalarExpr(E->getBase());

    Address VecMem = createMemTemp(E->getBase()->getType());
    Builder.CreateStore(Vec, VecMem);
    Base =
        makeAddrLValue(VecMem, E->getBase()->getType(), AlignmentSource::Decl);
  }

  QualType type =
      E->getType().withCVRQualifiers(Base.getQuals().getCVRQualifiers());

  // Encode the element access list into a vector of unsigned indices.
  llvm::SmallVector<uint32_t, 4> Indices;
  E->getEncodedElementAccess(Indices);

  if (Base.isSimple()) {
    llvm::Constant *CV =
        llvm::ConstantDataVector::get(getLLVMContext(), Indices);
    return LValue::MakeExtVectorElt(Base.getAddress(*this), CV, type,
                                    Base.getBaseInfo(), TBAAAccessInfo());
  }
  assert(Base.isExtVectorElt() && "Can only subscript lvalue vec elts here!");

  llvm::Constant *BaseElts = Base.getExtVectorElts();
  llvm::SmallVector<llvm::Constant *, 4> CElts;

  for (unsigned i = 0, e = Indices.size(); i != e; ++i)
    CElts.push_back(BaseElts->getAggregateElement(Indices[i]));
  llvm::Constant *CV = llvm::ConstantVector::get(CElts);
  return LValue::MakeExtVectorElt(Base.getExtVectorAddress(), CV, type,
                                  Base.getBaseInfo(), TBAAAccessInfo());
}

LValue FunctionEmitter::genMemberExpr(const MemberExpr *E) {
  if (DeclRefExpr *DRE = tryToConvertMemberExprToDeclRefExpr(*this, E)) {
    genIgnoredExpr(E->getBase());
    return genDeclRefLValue(DRE);
  }

  Expr *BaseExpr = E->getBase();
  // If this is s.x, emit s as an lvalue.  If it is s->x, emit s as a scalar.
  LValue BaseLV;
  if (E->isArrow()) {
    LValueBaseInfo BaseInfo;
    TBAAAccessInfo TBAAInfo;
    Address Addr = genPointerWithAlignment(BaseExpr, &BaseInfo, &TBAAInfo);
    QualType PtrTy = BaseExpr->getType()->getPointeeType();
    BaseLV = makeAddrLValue(Addr, PtrTy, BaseInfo, TBAAInfo);
  } else
    BaseLV = genCheckedLValue(BaseExpr, TCK_MemberAccess);

  NamedDecl *ND = E->getMemberDecl();
  if (auto *Field = dyn_cast<FieldDecl>(ND)) {
    LValue LV = genLValueForField(BaseLV, Field);
    return LV;
  }

  if (const auto *FD = dyn_cast<FunctionDecl>(ND))
    return genFunctionDeclLValue(*this, E, FD);

  llvm_unreachable("Unhandled member declaration!");
}

unsigned FunctionEmitter::getDebugInfoFIndex(const RecordDecl *Rec,
                                             unsigned FieldIndex) {
  unsigned I = 0, Skipped = 0;

  for (auto *F : Rec->getDefinition()->fields()) {
    if (I == FieldIndex)
      break;
    if (F->isUnnamedBitfield())
      Skipped++;
    I++;
  }

  return FieldIndex - Skipped;
}

// ===----------------------------------------------------------------------===
// Field storage helpers
// ===----------------------------------------------------------------------===

namespace {
Address emitAddrOfZeroSizeField(FunctionEmitter &FE, Address Base,
                                const FieldDecl *Field) {
  CharUnits Offset = FE.getContext().toCharUnitsFromBits(
      FE.getContext().getFieldOffset(Field));
  if (Offset.isZero())
    return Base;
  Base = Base.withElementType(FE.Int8Ty);
  return FE.Builder.CreateConstInBoundsByteGEP(Base, Offset);
}

Address emitAddrOfFieldStorage(FunctionEmitter &FE, Address base,
                               const FieldDecl *field) {
  if (field->isZeroSize(FE.getContext()))
    return emitAddrOfZeroSizeField(FE, base, field);

  const RecordDecl *rec = field->getParent();

  unsigned idx =
      FE.ME.getTypes().getRecordLayoutInfo(rec).getLLVMFieldNo(field);

  return FE.Builder.CreateStructGEP(base, idx, field->getName());
}

Address emitPreserveStructAccess(FunctionEmitter &FE, LValue base, Address addr,
                                 const FieldDecl *field) {
  const RecordDecl *rec = field->getParent();
  llvm::DIType *DbgInfo = FE.getDebugInfo()->getOrCreateStandaloneType(
      base.getType(), rec->getLocation());

  unsigned idx =
      FE.ME.getTypes().getRecordLayoutInfo(rec).getLLVMFieldNo(field);

  return FE.Builder.CreatePreserveStructAccessIndex(
      addr, idx, FE.getDebugInfoFIndex(rec, field->getFieldIndex()), DbgInfo);
}
} // namespace

LValue FunctionEmitter::genLValueForField(LValue base, const FieldDecl *field) {
  LValueBaseInfo BaseInfo = base.getBaseInfo();

  if (field->isBitField()) {
    const RecordLayoutInfo &RL =
        ME.getTypes().getRecordLayoutInfo(field->getParent());
    const BitFieldInfo &Info = RL.getBitFieldInfo(field);
    const bool UseVolatile = isAAPCS(ME.getTarget()) &&
                             ME.getCodeGenOpts().AAPCSBitfieldWidth &&
                             Info.VolatileStorageSize != 0 &&
                             field->getType()
                                 .withCVRQualifiers(base.getVRQualifiers())
                                 .isVolatileQualified();
    Address Addr = base.getAddress(*this);
    unsigned Idx = RL.getLLVMFieldNo(field);
    const RecordDecl *rec = field->getParent();
    if (!UseVolatile) {
      // Only the explicit `__builtin_preserve_access_index({...})`
      // region routes through `llvm.preserve.struct.access.index`.
      // Plain `-g` debug info MUST NOT change codegen shape: NeverC
      // targets (AArch64 / x86_64) have no ISel pattern for that
      // intrinsic, so emitting it without the user opting in would
      // turn a `-g` build of a struct-field access into a hard ISel
      // failure.  Debug info still flows through DWARF on the
      // standard GEP path below.
      if (!IsInPreservedAIRegion) {
        if (Idx != 0)
          // For structs, we GEP to the field that the record layout suggests.
          Addr = Builder.CreateStructGEP(Addr, Idx, field->getName());
      } else {
        llvm::DIType *DbgInfo = getDebugInfo()->getOrCreateRecordType(
            getContext().getRecordType(rec), rec->getLocation());
        Addr = Builder.CreatePreserveStructAccessIndex(
            Addr, Idx, getDebugInfoFIndex(rec, field->getFieldIndex()),
            DbgInfo);
      }
    }
    const unsigned SS =
        UseVolatile ? Info.VolatileStorageSize : Info.StorageSize;
    llvm::Type *FieldIntTy = llvm::Type::getIntNTy(getLLVMContext(), SS);
    Addr = Addr.withElementType(FieldIntTy);
    if (UseVolatile) {
      const unsigned VolatileOffset = Info.VolatileStorageOffset.getQuantity();
      if (VolatileOffset)
        Addr = Builder.CreateConstInBoundsGEP(Addr, VolatileOffset);
    }

    QualType fieldType =
        field->getType().withCVRQualifiers(base.getVRQualifiers());
    LValueBaseInfo FieldBaseInfo(BaseInfo.getAlignmentSource());
    return LValue::MakeBitfield(Addr, Info, fieldType, FieldBaseInfo,
                                TBAAAccessInfo());
  }

  // Fields of may-alias structures are may-alias themselves.
  QualType FieldType = field->getType();
  const RecordDecl *rec = field->getParent();
  AlignmentSource BaseAlignSource = BaseInfo.getAlignmentSource();
  LValueBaseInfo FieldBaseInfo(getFieldAlignmentSource(BaseAlignSource));
  TBAAAccessInfo FieldTBAAInfo;
  if (base.getTBAAInfo().isMayAlias() || rec->hasAttr<MayAliasAttr>() ||
      FieldType->isVectorType()) {
    FieldTBAAInfo = TBAAAccessInfo::getMayAliasInfo();
  } else if (rec->isUnion()) {
    FieldTBAAInfo = TBAAAccessInfo::getMayAliasInfo();
  } else {
    // If no base type been assigned for the base access, then try to generate
    // one for this base lvalue.
    FieldTBAAInfo = base.getTBAAInfo();
    if (!FieldTBAAInfo.BaseType) {
      FieldTBAAInfo.BaseType = ME.getTBAABaseTypeInfo(base.getType());
      assert(!FieldTBAAInfo.Offset &&
             "Nonzero offset for an access with no base type!");
    }

    // Adjust offset to be relative to the base type.
    const StructRecordLayout &Layout =
        getContext().getStructRecordLayout(field->getParent());
    unsigned CharWidth = getContext().getCharWidth();
    if (FieldTBAAInfo.BaseType)
      FieldTBAAInfo.Offset +=
          Layout.getFieldOffset(field->getFieldIndex()) / CharWidth;

    FieldTBAAInfo.AccessType = ME.getTBAATypeInfo(FieldType);
    FieldTBAAInfo.Size =
        getContext().getTypeSizeInChars(FieldType).getQuantity();
  }

  Address addr = base.getAddress(*this);
  unsigned RecordCVR = base.getVRQualifiers();
  if (rec->isUnion()) {
    if (IsInPreservedAIRegion) {
      // Remember the original union field index
      llvm::DIType *DbgInfo = getDebugInfo()->getOrCreateStandaloneType(
          base.getType(), rec->getLocation());
      addr =
          Address(Builder.CreatePreserveUnionAccessIndex(
                      addr.getPointer(),
                      getDebugInfoFIndex(rec, field->getFieldIndex()), DbgInfo),
                  addr.getElementType(), addr.getAlignment());
    }
  } else {
    // Sibling of the bitfield path above: only the explicit
    // `__builtin_preserve_access_index({...})` region routes through
    // `llvm.preserve.struct.access.index`.  Plain `-g` keeps the
    // standard GEP shape so the AArch64 / x86_64 backends (NeverC's
    // only supported targets) do not see an intrinsic they cannot
    // select.
    if (!IsInPreservedAIRegion)
      // For structs, we GEP to the field that the record layout suggests.
      addr = emitAddrOfFieldStorage(*this, addr, field);
    else
      // Remember the original struct field index
      addr = emitPreserveStructAccess(*this, base, addr, field);
  }

  // Make sure that the address is pointing to the right type.  This is critical
  // for both unions and structs.
  addr = addr.withElementType(ME.getTypes().convertTypeForMem(FieldType));

  if (field->hasAttr<AnnotateAttr>())
    addr = genFieldAnnotations(field, addr);

  LValue LV = makeAddrLValue(addr, FieldType, FieldBaseInfo, FieldTBAAInfo);
  LV.getQuals().addCVRQualifiers(RecordCVR);

  return LV;
}

LValue
FunctionEmitter::genLValueForFieldInitialization(LValue Base,
                                                 const FieldDecl *Field) {
  return genLValueForField(Base, Field);
}

LValue FunctionEmitter::genCompoundLiteralLValue(const CompoundLiteralExpr *E) {
  if (E->isFileScope()) {
    ConstantAddress GlobalPtr = ME.addrOfConstantCompoundLiteral(E);
    return makeAddrLValue(GlobalPtr, E->getType(), AlignmentSource::Decl);
  }
  if (E->getType()->isVariablyModifiedType())
    // make sure to emit the VLA size.
    genVariablyModifiedType(E->getType());

  Address DeclPtr = createMemTemp(E->getType(), ".compoundliteral");
  const Expr *InitExpr = E->getInitializer();
  LValue Result = makeAddrLValue(DeclPtr, E->getType(), AlignmentSource::Decl);

  genAnyExprToMem(InitExpr, DeclPtr, E->getType().getQualifiers(),
                  /*Init*/ true);

  return Result;
}

LValue FunctionEmitter::genInitListLValue(const InitListExpr *E) {
  if (!E->isLValue())
    return genAggExprToLValue(E);

  assert(E->isTransparent() && "non-transparent lvalue init list");
  return genLValue(E->getInit(0));
}

namespace {
// Handle the case where the condition is a constant evaluatable simple integer,
// which means we don't have to separately handle the true/false blocks.
std::optional<LValue> HandleConditionalOperatorLValueSimpleCase(
    FunctionEmitter &FE, const AbstractConditionalOperator *E) {
  const Expr *condExpr = E->getCond();
  bool CondExprBool;
  if (FE.constantFoldsToSimpleInteger(condExpr, CondExprBool)) {
    const Expr *Live = E->getTrueExpr(), *Dead = E->getFalseExpr();
    if (!CondExprBool)
      std::swap(Live, Dead);

    if (!FE.containsLabel(Dead)) {
      // If the true case is live, we need to track its region.
      if (CondExprBool)
        return FE.genLValue(Live);
    }
  }
  return std::nullopt;
}
struct ConditionalInfo {
  llvm::BasicBlock *lhsBlock, *rhsBlock;
  std::optional<LValue> LHS, RHS;
};

// Create and generate the 3 blocks for a conditional operator.
// Leaves the 'current block' in the continuation basic block.
template <typename FuncTy>
ConditionalInfo genConditionalBlocks(FunctionEmitter &FE,
                                     const AbstractConditionalOperator *E,
                                     const FuncTy &BranchGenFunc) {
  ConditionalInfo Info{FE.createBasicBlock("cond.true"),
                       FE.createBasicBlock("cond.false"), std::nullopt,
                       std::nullopt};
  llvm::BasicBlock *endBlock = FE.createBasicBlock("cond.end");

  FunctionEmitter::ConditionalEvaluation eval(FE);
  FE.genBranchOnBoolExpr(E->getCond(), Info.lhsBlock, Info.rhsBlock);

  // Any temporaries created here are conditional.
  FE.genBlock(Info.lhsBlock);
  eval.begin(FE);
  Info.LHS = BranchGenFunc(FE, E->getTrueExpr());
  eval.end(FE);
  Info.lhsBlock = FE.Builder.GetInsertBlock();

  if (Info.LHS)
    FE.Builder.CreateBr(endBlock);

  // Any temporaries created here are conditional.
  FE.genBlock(Info.rhsBlock);
  eval.begin(FE);
  Info.RHS = BranchGenFunc(FE, E->getFalseExpr());
  eval.end(FE);
  Info.rhsBlock = FE.Builder.GetInsertBlock();
  FE.genBlock(endBlock);

  return Info;
}
} // namespace

void FunctionEmitter::genIgnoredConditionalOperator(
    const AbstractConditionalOperator *E) {
  if (!E->isLValue()) {
    // ?: here should be an aggregate.
    assert(hasAggregateEvaluationKind(E->getType()) &&
           "Unexpected conditional operator!");
    return (void)genAggExprToLValue(E);
  }

  OpaqueValueMapping binding(*this, E);
  if (HandleConditionalOperatorLValueSimpleCase(*this, E))
    return;

  genConditionalBlocks(*this, E, [](FunctionEmitter &FE, const Expr *E) {
    FE.genIgnoredExpr(E);
    return LValue{};
  });
}
LValue FunctionEmitter::genConditionalOperatorLValue(
    const AbstractConditionalOperator *expr) {
  if (!expr->isLValue()) {
    // ?: here should be an aggregate.
    assert(hasAggregateEvaluationKind(expr->getType()) &&
           "Unexpected conditional operator!");
    return genAggExprToLValue(expr);
  }

  OpaqueValueMapping binding(*this, expr);
  if (std::optional<LValue> Res =
          HandleConditionalOperatorLValueSimpleCase(*this, expr))
    return *Res;

  ConditionalInfo Info =
      genConditionalBlocks(*this, expr, [](FunctionEmitter &FE, const Expr *E) {
        return std::optional<LValue>(FE.genLValue(E));
      });

  if ((Info.LHS && !Info.LHS->isSimple()) ||
      (Info.RHS && !Info.RHS->isSimple()))
    return genUnsupportedLValue(expr, "conditional operator");

  if (Info.LHS && Info.RHS) {
    Address lhsAddr = Info.LHS->getAddress(*this);
    Address rhsAddr = Info.RHS->getAddress(*this);
    llvm::PHINode *phi = Builder.CreatePHI(lhsAddr.getType(), 2, "cond-lvalue");
    phi->addIncoming(lhsAddr.getPointer(), Info.lhsBlock);
    phi->addIncoming(rhsAddr.getPointer(), Info.rhsBlock);
    Address result(phi, lhsAddr.getElementType(),
                   std::min(lhsAddr.getAlignment(), rhsAddr.getAlignment()));
    AlignmentSource alignSource =
        std::max(Info.LHS->getBaseInfo().getAlignmentSource(),
                 Info.RHS->getBaseInfo().getAlignmentSource());
    TBAAAccessInfo TBAAInfo = ME.mergeTBAAInfoForConditionalOperator(
        Info.LHS->getTBAAInfo(), Info.RHS->getTBAAInfo());
    return makeAddrLValue(result, expr->getType(), LValueBaseInfo(alignSource),
                          TBAAInfo);
  } else {
    assert((Info.LHS || Info.RHS) &&
           "both operands of lvalue conditional are missing?");
    return Info.LHS ? *Info.LHS : *Info.RHS;
  }
}

LValue FunctionEmitter::genCastLValue(const CastExpr *E) {
  switch (E->getCastKind()) {
  case CK_ToVoid:
  case CK_BitCast:
  case CK_LValueToRValueBitCast:
  case CK_ArrayToPointerDecay:
  case CK_FunctionToPointerDecay:
  case CK_NullToPointer:
  case CK_IntegralToPointer:
  case CK_PointerToIntegral:
  case CK_PointerToBoolean:
  case CK_IntegralCast:
  case CK_BooleanToSignedIntegral:
  case CK_IntegralToBoolean:
  case CK_IntegralToFloating:
  case CK_FloatingToIntegral:
  case CK_FloatingToBoolean:
  case CK_FloatingCast:
  case CK_FloatingRealToComplex:
  case CK_FloatingComplexToReal:
  case CK_FloatingComplexToBoolean:
  case CK_FloatingComplexCast:
  case CK_FloatingComplexToIntegralComplex:
  case CK_IntegralRealToComplex:
  case CK_IntegralComplexToReal:
  case CK_IntegralComplexToBoolean:
  case CK_IntegralComplexCast:
  case CK_IntegralComplexToFloatingComplex:
  case CK_FloatingToFixedPoint:
  case CK_FixedPointToFloating:
  case CK_FixedPointCast:
  case CK_FixedPointToBoolean:
  case CK_FixedPointToIntegral:
  case CK_IntegralToFixedPoint:
  case CK_MatrixCast:
    return genUnsupportedLValue(E, "unexpected cast lvalue");

  case CK_Dependent:
    llvm_unreachable("dependent cast kind in IR gen!");

  case CK_BuiltinFnToFnPtr:
    llvm_unreachable("builtin functions are handled elsewhere");

  // These are never l-values; just use the aggregate emission code.
  case CK_NonAtomicToAtomic:
  case CK_AtomicToNonAtomic:
    return genAggExprToLValue(E);

  case CK_LValueToRValue:
    return genLValue(E->getSubExpr());

  case CK_NoOp: {
    // CK_NoOp can model a qualification conversion, which can remove an array
    // bound and change the IR type.
    LValue LV = genLValue(E->getSubExpr());
    // Propagate the volatile qualifer to LValue, if exist in E.
    if (E->changesVolatileQualification())
      LV.getQuals() = E->getType().getQualifiers();
    if (LV.isSimple()) {
      Address V = LV.getAddress(*this);
      if (V.isValid()) {
        llvm::Type *T = convertTypeForMem(E->getType());
        if (V.getElementType() != T)
          LV.setAddress(V.withElementType(T));
      }
    }
    return LV;
  }

  case CK_ToUnion:
    return genAggExprToLValue(E);
  case CK_LValueBitCast: {
    // This must be a reinterpret_cast (or c-style equivalent).
    const auto *CE = cast<ExplicitCastExpr>(E);

    ME.genExplicitCastExprType(CE, this);
    LValue LV = genLValue(E->getSubExpr());
    Address V = LV.getAddress(*this).withElementType(
        convertTypeForMem(CE->getTypeAsWritten()->getPointeeType()));

    return makeAddrLValue(V, E->getType(), LV.getBaseInfo(),
                          ME.getTBAAInfoForSubobject(LV, E->getType()));
  }
  case CK_AddressSpaceConversion: {
    LValue LV = genLValue(E->getSubExpr());
    QualType DestTy = getContext().getPointerType(E->getType());
    llvm::Value *V = getTargetHooks().performAddrSpaceCast(
        *this, LV.getPointer(*this),
        E->getSubExpr()->getType().getAddressSpace(),
        E->getType().getAddressSpace(), convertType(DestTy));
    return makeAddrLValue(Address(V, convertTypeForMem(E->getType()),
                                  LV.getAddress(*this).getAlignment()),
                          E->getType(), LV.getBaseInfo(), LV.getTBAAInfo());
  }

  case CK_VectorSplat:
    return genUnsupportedLValue(E, "unexpected cast lvalue");
  }

  llvm_unreachable("Unhandled lvalue cast kind?");
}

LValue FunctionEmitter::genOpaqueValueLValue(const OpaqueValueExpr *e) {
  assert(OpaqueValueMappingData::shouldBindAsLValue(e));
  return getOrCreateOpaqueLValueMapping(e);
}

LValue
FunctionEmitter::getOrCreateOpaqueLValueMapping(const OpaqueValueExpr *e) {
  assert(OpaqueValueMapping::shouldBindAsLValue(e));

  llvm::DenseMap<const OpaqueValueExpr *, LValue>::iterator it =
      OpaqueLValues.find(e);

  if (it != OpaqueLValues.end())
    return it->second;

  assert(e->isUnique() && "LValue for a nonunique OVE hasn't been emitted");
  return genLValue(e->getSourceExpr());
}

RValue
FunctionEmitter::getOrCreateOpaqueRValueMapping(const OpaqueValueExpr *e) {
  assert(!OpaqueValueMapping::shouldBindAsLValue(e));

  llvm::DenseMap<const OpaqueValueExpr *, RValue>::iterator it =
      OpaqueRValues.find(e);

  if (it != OpaqueRValues.end())
    return it->second;

  assert(e->isUnique() && "RValue for a nonunique OVE hasn't been emitted");
  return genAnyExpr(e->getSourceExpr());
}

RValue FunctionEmitter::genRValueForField(LValue LV, const FieldDecl *FD,
                                          SourceLocation Loc) {
  QualType FT = FD->getType();
  LValue FieldLV = genLValueForField(LV, FD);
  switch (getEvaluationKind(FT)) {
  case TEK_Complex:
    return RValue::getComplex(genLoadOfComplex(FieldLV, Loc));
  case TEK_Aggregate:
    return FieldLV.asAggregateRValue(*this);
  case TEK_Scalar:
    // Call genLoadOfScalar except when the lvalue is a bitfield to emit a
    // primitive load.
    if (FieldLV.isBitField())
      return genLoadOfLValue(FieldLV, Loc);
    return RValue::get(genLoadOfScalar(FieldLV, Loc));
  }
  llvm_unreachable("bad evaluation kind");
}

RValue FunctionEmitter::genCallExpr(const CallExpr *E,
                                    ReturnValueSlot ReturnValue) {
  FnCallee callee = genCallee(E->getCallee());

  if (callee.isBuiltin()) {
    return genBuiltinExpr(callee.getBuiltinDecl(), callee.getBuiltinID(), E,
                          ReturnValue);
  }

  return genCall(E->getCallee()->getType(), callee, E, ReturnValue);
}

// Detect the unusual situation where an inline version is shadowed by a
// non-inline version. In that case we should pick the external one
// everywhere. That's GCC behavior too.
namespace {
bool onlyHasInlineBuiltinDeclaration(const FunctionDecl *FD) {
  for (const FunctionDecl *PD = FD; PD; PD = PD->getPreviousDecl())
    if (!PD->isInlineBuiltinDeclaration())
      return false;
  return true;
}

FnCallee genDirectCallee(FunctionEmitter &FE, GlobalDecl GD) {
  const FunctionDecl *FD = cast<FunctionDecl>(GD.getDecl());

  if (auto builtinID = FD->getBuiltinID()) {
    std::string NoBuiltinFD = ("no-builtin-" + FD->getName()).str();
    std::string NoBuiltins = "no-builtins";

    llvm::StringRef Ident = FE.ME.getMangledName(GD);
    std::string FDInlineName = (Ident + ".inline").str();

    bool IsPredefinedLibFunction =
        FE.getContext().BuiltinInfo.isPredefinedLibFunction(builtinID);
    bool HasAttributeNoBuiltin =
        FE.CurFn->getAttributes().hasFnAttr(NoBuiltinFD) ||
        FE.CurFn->getAttributes().hasFnAttr(NoBuiltins);

    // When directly calling an inline builtin, refer to it via its mangled
    // name to make it clear it's not the actual builtin.
    if (FE.CurFn->getName() != FDInlineName &&
        onlyHasInlineBuiltinDeclaration(FD)) {
      llvm::Constant *CalleePtr = genFunctionDeclPointer(FE.ME, GD);
      llvm::Function *Fn = llvm::cast<llvm::Function>(CalleePtr);
      llvm::Module *M = Fn->getParent();
      llvm::Function *Clone = M->getFunction(FDInlineName);
      if (!Clone) {
        Clone = llvm::Function::Create(Fn->getFunctionType(),
                                       llvm::GlobalValue::InternalLinkage,
                                       Fn->getAddressSpace(), FDInlineName, M);
        Clone->addFnAttr(llvm::Attribute::AlwaysInline);
      }
      return FnCallee::forDirect(Clone, GD);
    }

    // Replaceable builtins provide their own implementation of a builtin. If we
    // are in an inline builtin implementation, avoid trivial infinite
    // recursion. Honor __attribute__((no_builtin("foo"))) or
    // __attribute__((no_builtin)) on the current function unless foo is
    // not a predefined library function which means we must generate the
    // builtin no matter what.
    else if (!IsPredefinedLibFunction || !HasAttributeNoBuiltin)
      return FnCallee::forBuiltin(builtinID, FD);
  }

  llvm::Constant *CalleePtr = genFunctionDeclPointer(FE.ME, GD);
  return FnCallee::forDirect(CalleePtr, GD);
}
} // namespace

// ===----------------------------------------------------------------------===
// Call expressions & callee resolution
// ===----------------------------------------------------------------------===

FnCallee FunctionEmitter::genCallee(const Expr *E) {
  E = E->IgnoreParens();

  // Look through function-to-pointer decay.
  if (auto ICE = dyn_cast<ImplicitCastExpr>(E)) {
    if (ICE->getCastKind() == CK_FunctionToPointerDecay ||
        ICE->getCastKind() == CK_BuiltinFnToFnPtr) {
      return genCallee(ICE->getSubExpr());
    }

    // Resolve direct calls.
  } else if (auto DRE = dyn_cast<DeclRefExpr>(E)) {
    if (auto FD = dyn_cast<FunctionDecl>(DRE->getDecl())) {
      return genDirectCallee(*this, FD);
    }
  } else if (auto ME = dyn_cast<MemberExpr>(E)) {
    if (auto FD = dyn_cast<FunctionDecl>(ME->getMemberDecl())) {
      genIgnoredExpr(ME->getBase());
      return genDirectCallee(*this, FD);
    }
  }

  // Otherwise, we have an indirect reference.
  llvm::Value *calleePtr;
  QualType functionType;
  if (auto ptrType = E->getType()->getAs<PointerType>()) {
    calleePtr = genScalarExpr(E);
    functionType = ptrType->getPointeeType();
  } else {
    functionType = E->getType();
    calleePtr = genLValue(E, KnownNonNull).getPointer(*this);
  }
  assert(functionType->isFunctionType());

  GlobalDecl GD;
  if (const auto *VD =
          dyn_cast_or_null<VarDecl>(E->getReferencedDeclOfCallee()))
    GD = GlobalDecl(VD);

  FnCalleeInfo calleeInfo(functionType->getAs<FunctionProtoType>(), GD);
  FnCallee callee(calleeInfo, calleePtr);
  return callee;
}

LValue FunctionEmitter::genBinaryOperatorLValue(const BinaryOperator *E) {
  // Comma expressions just emit their LHS then their RHS as an l-value.
  if (E->getOpcode() == BO_Comma) {
    genIgnoredExpr(E->getLHS());
    ensureInsertPoint();
    return genLValue(E->getRHS());
  }

  assert(E->getOpcode() == BO_Assign && "unexpected binary l-value");

  switch (getEvaluationKind(E->getType())) {
  case TEK_Scalar: {
    RValue RV = genAnyExpr(E->getRHS());
    LValue LV = genCheckedLValue(E->getLHS(), TCK_Store);
    genStoreThroughLValue(RV, LV);
    return LV;
  }

  case TEK_Complex:
    return genComplexAssignmentLValue(E);

  case TEK_Aggregate:
    return genAggExprToLValue(E);
  }
  llvm_unreachable("bad evaluation kind");
}

LValue FunctionEmitter::genCallExprLValue(const CallExpr *E) {
  RValue RV = genCallExpr(E);

  if (!RV.isScalar())
    return makeAddrLValue(RV.getAggregateAddress(), E->getType(),
                          AlignmentSource::Decl);

  // Scalar call results that are still pointer-like: expose as pointee LValue.
  return makeNaturalAlignPointeeAddrLValue(RV.getScalarVal(), E->getType());
}

LValue FunctionEmitter::genVAArgExprLValue(const VAArgExpr *E) {
  return genAggExprToLValue(E);
}

LValue FunctionEmitter::genStmtExprLValue(const StmtExpr *E) {
  // Can only get l-value for message expression returning aggregate type
  RValue RV = genAnyExprToTemp(E);
  return makeAddrLValue(RV.getAggregateAddress(), E->getType(),
                        AlignmentSource::Decl);
}

RValue FunctionEmitter::genCall(QualType CalleeType, const FnCallee &OrigCallee,
                                const CallExpr *E, ReturnValueSlot ReturnValue,
                                llvm::Value *Chain) {
  assert(CalleeType->isFunctionPointerType() &&
         "Call must have function pointer type!");

  const Decl *TargetDecl =
      OrigCallee.getAbstractInfo().getCalleeDecl().getDecl();

  CalleeType = getContext().getCanonicalType(CalleeType);

  auto PointeeType = cast<PointerType>(CalleeType)->getPointeeType();

  FnCallee Callee = OrigCallee;

  const auto *FnType = cast<FunctionType>(PointeeType);

  CallArgList Args;
  if (Chain)
    Args.add(RValue::get(Chain), ME.getContext().VoidPtrTy);

  EvaluationOrder Order = EvaluationOrder::Default;

  genCallArgs(Args, dyn_cast<FunctionProtoType>(FnType), E->arguments(),
              E->getDirectCallee(), /*ParamsToSkip*/ 0, Order);

  const ABIFunctionInfo &FnInfo =
      ME.getTypes().arrangeFreeFunctionCall(Args, FnType, /*ChainCall=*/Chain);

  // K&R / no-prototype calls: lower as non-variadic with promoted args,
  // bitcasting the function pointer to match.
  //
  // Chain calls use this same code path to add the invisible chain parameter
  // to the function type.
  if (isa<FunctionNoProtoType>(FnType) || Chain) {
    llvm::Type *CalleeTy = getTypes().GetFunctionType(FnInfo);
    int AS = Callee.getFunctionPointer()->getType()->getPointerAddressSpace();
    CalleeTy = CalleeTy->getPointerTo(AS);

    llvm::Value *CalleePtr = Callee.getFunctionPointer();
    CalleePtr = Builder.CreateBitCast(CalleePtr, CalleeTy, "callee.knr.cast");
    Callee.setFunctionPointer(CalleePtr);
  }

  llvm::CallBase *CallOrInvoke = nullptr;
  RValue Call = genCall(FnInfo, Callee, ReturnValue, Args, &CallOrInvoke,
                        E == MustTailCall, E->getExprLoc());

  // Generate function declaration DISuprogram in order to be used
  // in debug info about call sites.
  if (DebugEmitter *DI = getDebugInfo()) {
    if (auto *CalleeDecl = dyn_cast_or_null<FunctionDecl>(TargetDecl)) {
      FunctionArgList Args;
      QualType ResTy = formFunctionArgList(CalleeDecl, Args);
      DI->genFuncDeclForCallSite(CallOrInvoke,
                                 DI->getFunctionType(CalleeDecl, ResTy, Args),
                                 CalleeDecl);
    }
  }

  return Call;
}

RValue FunctionEmitter::convertTempToRValue(Address addr, QualType type,
                                            SourceLocation loc) {
  LValue lvalue = makeAddrLValue(addr, type, AlignmentSource::Decl);
  switch (getEvaluationKind(type)) {
  case TEK_Complex:
    return RValue::getComplex(genLoadOfComplex(lvalue, loc));
  case TEK_Aggregate:
    return lvalue.asAggregateRValue(*this);
  case TEK_Scalar:
    return RValue::get(genLoadOfScalar(lvalue, loc));
  }
  llvm_unreachable("bad evaluation kind");
}

void FunctionEmitter::setFPAccuracy(llvm::Value *Val, float Accuracy) {
  assert(Val->getType()->isFPOrFPVectorTy());
  if (Accuracy == 0.0 || !isa<llvm::Instruction>(Val))
    return;

  llvm::MDBuilder MDHelper(getLLVMContext());
  llvm::MDNode *Node = MDHelper.createFPMath(Accuracy);

  cast<llvm::Instruction>(Val)->setMetadata(llvm::LLVMContext::MD_fpmath, Node);
}

// ===----------------------------------------------------------------------===
// Pseudo-object expressions
// ===----------------------------------------------------------------------===

namespace {
struct LValueOrRValue {
  LValue LV;
  RValue RV;
};

LValueOrRValue emitPseudoObjectExpr(FunctionEmitter &FE,
                                    const PseudoObjectExpr *E, bool forLValue,
                                    AggValueSlot slot) {
  llvm::SmallVector<FunctionEmitter::OpaqueValueMappingData, 4> opaques;

  const Expr *resultExpr = E->getResultExpr();
  LValueOrRValue result;

  for (PseudoObjectExpr::const_semantics_iterator i = E->semantics_begin(),
                                                  e = E->semantics_end();
       i != e; ++i) {
    const Expr *semantic = *i;

    // If this semantic expression is an opaque value, bind it
    // to the result of its source expression.
    if (const auto *ov = dyn_cast<OpaqueValueExpr>(semantic)) {
      // Skip unique OVEs.
      if (ov->isUnique()) {
        assert(ov != resultExpr &&
               "A unique OVE cannot be used as the result expression");
        continue;
      }

      // If this is the result expression, we may need to evaluate
      // directly into the slot.
      typedef FunctionEmitter::OpaqueValueMappingData OVMA;
      OVMA opaqueData;
      if (ov == resultExpr && ov->isPRValue() && !forLValue &&
          FunctionEmitter::hasAggregateEvaluationKind(ov->getType())) {
        FE.genAggExpr(ov->getSourceExpr(), slot);
        LValue LV = FE.makeAddrLValue(slot.getAddress(), ov->getType(),
                                      AlignmentSource::Decl);
        opaqueData = OVMA::bind(FE, ov, LV);
        result.RV = slot.asRValue();

        // Otherwise, emit as normal.
      } else {
        opaqueData = OVMA::bind(FE, ov, ov->getSourceExpr());

        // If this is the result, also evaluate the result now.
        if (ov == resultExpr) {
          if (forLValue)
            result.LV = FE.genLValue(ov);
          else
            result.RV = FE.genAnyExpr(ov, slot);
        }
      }

      opaques.push_back(opaqueData);

      // Otherwise, if the expression is the result, evaluate it
      // and remember the result.
    } else if (semantic == resultExpr) {
      if (forLValue)
        result.LV = FE.genLValue(semantic);
      else
        result.RV = FE.genAnyExpr(semantic, slot);

      // Otherwise, evaluate the expression in an ignored context.
    } else {
      FE.genIgnoredExpr(semantic);
    }
  }

  // Unbind all the opaques now.
  for (unsigned i = 0, e = opaques.size(); i != e; ++i)
    opaques[i].unbind(FE);

  return result;
}
} // namespace

RValue FunctionEmitter::genPseudoObjectRValue(const PseudoObjectExpr *E,
                                              AggValueSlot slot) {
  return emitPseudoObjectExpr(*this, E, false, slot).RV;
}

LValue FunctionEmitter::genPseudoObjectLValue(const PseudoObjectExpr *E) {
  return emitPseudoObjectExpr(*this, E, true, AggValueSlot::ignored()).LV;
}
