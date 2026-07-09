//===- SemaExprType.cpp - Type compatibility, casts & assignment ---===//
//
// Extracted from SemaExpr.cpp (mechanical move, no logic change).
//
//===----------------------------------------------------------------------===//

#include "Expr/SemaExprUtils.h"
#include "Expr/TreeTransform.h"
#include "neverc/Analyze/Designator.h"
#include "neverc/Analyze/EnterExpressionEvaluationContext.h"
#include "neverc/Analyze/Initialization.h"
#include "neverc/Analyze/ScopeInfo.h"
#include "neverc/Analyze/SemaFixItUtils.h"
#include "neverc/Analyze/SemaInternal.h"
#include "neverc/Foundation/Builtin/BuiltinString.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/Diagnostic/DiagnosticSema.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Scan/LiteralParser.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Tree/Core/TreeConsumer.h"
#include "neverc/Tree/Core/TreeMutationListener.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/TypeSize.h"
#include <optional>

using namespace neverc;
using namespace sema;

// ===----------------------------------------------------------------------===
// Scalar cast preparation, type compatibility & assignment
// ===----------------------------------------------------------------------===




CastKind Sema::PrepareScalarCast(ExprResult &Src, QualType DestTy) {
  // Both Src and Dest are scalar types, i.e. arithmetic or pointer.
  // Also, callers should have filtered out the invalid cases with
  // pointers.  Everything else should be possible.

  QualType SrcTy = Src.get()->getType();
  if (Context.hasSameUnqualifiedType(SrcTy, DestTy))
    return CK_NoOp;

  switch (SrcTy->getScalarTypeKind()) {
  case Type::STK_CPointer:
    switch (DestTy->getScalarTypeKind()) {
    case Type::STK_CPointer: {
      LangAS SrcAS = SrcTy->getPointeeType().getAddressSpace();
      LangAS DestAS = DestTy->getPointeeType().getAddressSpace();
      if (SrcAS != DestAS)
        return CK_AddressSpaceConversion;
      if (Context.hasCvrSimilarType(SrcTy, DestTy))
        return CK_NoOp;
      return CK_BitCast;
    }
    case Type::STK_Bool:
      return CK_PointerToBoolean;
    case Type::STK_Integral:
      return CK_PointerToIntegral;
    case Type::STK_Floating:
    case Type::STK_FloatingComplex:
    case Type::STK_IntegralComplex:
    case Type::STK_FixedPoint:
      llvm_unreachable("illegal cast from pointer");
    }
    llvm_unreachable("Should have returned before this");

  case Type::STK_FixedPoint:
    switch (DestTy->getScalarTypeKind()) {
    case Type::STK_FixedPoint:
      return CK_FixedPointCast;
    case Type::STK_Bool:
      return CK_FixedPointToBoolean;
    case Type::STK_Integral:
      return CK_FixedPointToIntegral;
    case Type::STK_Floating:
      return CK_FixedPointToFloating;
    case Type::STK_IntegralComplex:
    case Type::STK_FloatingComplex:
      Diag(Src.get()->getExprLoc(),
           diag::err_unimplemented_conversion_with_fixed_point_type)
          << DestTy;
      return CK_IntegralCast;
    case Type::STK_CPointer:
      llvm_unreachable("illegal cast to pointer type");
    }
    llvm_unreachable("Should have returned before this");

  case Type::STK_Bool: // casting from bool is like casting from an integer
  case Type::STK_Integral:
    switch (DestTy->getScalarTypeKind()) {
    case Type::STK_CPointer:
      if (Src.get()->isNullPointerConstant(Context,
                                           Expr::NPC_ValueDependentIsNull))
        return CK_NullToPointer;
      return CK_IntegralToPointer;
    case Type::STK_Bool:
      return CK_IntegralToBoolean;
    case Type::STK_Integral:
      return CK_IntegralCast;
    case Type::STK_Floating:
      return CK_IntegralToFloating;
    case Type::STK_IntegralComplex:
      Src = ImpCastExprToType(Src.get(),
                              DestTy->castAs<ComplexType>()->getElementType(),
                              CK_IntegralCast);
      return CK_IntegralRealToComplex;
    case Type::STK_FloatingComplex:
      Src = ImpCastExprToType(Src.get(),
                              DestTy->castAs<ComplexType>()->getElementType(),
                              CK_IntegralToFloating);
      return CK_FloatingRealToComplex;
    case Type::STK_FixedPoint:
      return CK_IntegralToFixedPoint;
    }
    llvm_unreachable("Should have returned before this");

  case Type::STK_Floating:
    switch (DestTy->getScalarTypeKind()) {
    case Type::STK_Floating:
      return CK_FloatingCast;
    case Type::STK_Bool:
      return CK_FloatingToBoolean;
    case Type::STK_Integral:
      return CK_FloatingToIntegral;
    case Type::STK_FloatingComplex:
      Src = ImpCastExprToType(Src.get(),
                              DestTy->castAs<ComplexType>()->getElementType(),
                              CK_FloatingCast);
      return CK_FloatingRealToComplex;
    case Type::STK_IntegralComplex:
      Src = ImpCastExprToType(Src.get(),
                              DestTy->castAs<ComplexType>()->getElementType(),
                              CK_FloatingToIntegral);
      return CK_IntegralRealToComplex;
    case Type::STK_CPointer:
      llvm_unreachable("valid float->pointer cast?");
    case Type::STK_FixedPoint:
      return CK_FloatingToFixedPoint;
    }
    llvm_unreachable("Should have returned before this");

  case Type::STK_FloatingComplex:
    switch (DestTy->getScalarTypeKind()) {
    case Type::STK_FloatingComplex:
      return CK_FloatingComplexCast;
    case Type::STK_IntegralComplex:
      return CK_FloatingComplexToIntegralComplex;
    case Type::STK_Floating: {
      QualType ET = SrcTy->castAs<ComplexType>()->getElementType();
      if (Context.hasSameType(ET, DestTy))
        return CK_FloatingComplexToReal;
      Src = ImpCastExprToType(Src.get(), ET, CK_FloatingComplexToReal);
      return CK_FloatingCast;
    }
    case Type::STK_Bool:
      return CK_FloatingComplexToBoolean;
    case Type::STK_Integral:
      Src = ImpCastExprToType(Src.get(),
                              SrcTy->castAs<ComplexType>()->getElementType(),
                              CK_FloatingComplexToReal);
      return CK_FloatingToIntegral;
    case Type::STK_CPointer:
      llvm_unreachable("valid complex float->pointer cast?");
    case Type::STK_FixedPoint:
      Diag(Src.get()->getExprLoc(),
           diag::err_unimplemented_conversion_with_fixed_point_type)
          << SrcTy;
      return CK_IntegralCast;
    }
    llvm_unreachable("Should have returned before this");

  case Type::STK_IntegralComplex:
    switch (DestTy->getScalarTypeKind()) {
    case Type::STK_FloatingComplex:
      return CK_IntegralComplexToFloatingComplex;
    case Type::STK_IntegralComplex:
      return CK_IntegralComplexCast;
    case Type::STK_Integral: {
      QualType ET = SrcTy->castAs<ComplexType>()->getElementType();
      if (Context.hasSameType(ET, DestTy))
        return CK_IntegralComplexToReal;
      Src = ImpCastExprToType(Src.get(), ET, CK_IntegralComplexToReal);
      return CK_IntegralCast;
    }
    case Type::STK_Bool:
      return CK_IntegralComplexToBoolean;
    case Type::STK_Floating:
      Src = ImpCastExprToType(Src.get(),
                              SrcTy->castAs<ComplexType>()->getElementType(),
                              CK_IntegralComplexToReal);
      return CK_IntegralToFloating;
    case Type::STK_CPointer:
      llvm_unreachable("valid complex int->pointer cast?");
    case Type::STK_FixedPoint:
      Diag(Src.get()->getExprLoc(),
           diag::err_unimplemented_conversion_with_fixed_point_type)
          << SrcTy;
      return CK_IntegralCast;
    }
    llvm_unreachable("Should have returned before this");
  }

  llvm_unreachable("Unhandled scalar cast");
}

namespace {
bool decomposeVectorType(QualType type, uint64_t &len, QualType &eltType) {
  // Vectors are simple.
  if (const VectorType *vecType = type->getAs<VectorType>()) {
    len = vecType->getNumElements();
    eltType = vecType->getElementType();
    assert(eltType->isScalarType());
    return true;
  }

  // We allow lax conversion to and from non-vector types, but only if
  // they're real types (i.e. non-complex, non-pointer scalar types).
  if (!type->isRealType())
    return false;

  len = 1;
  eltType = type;
  return true;
}
} // namespace


// ===----------------------------------------------------------------------===
// Type compatibility checks & casts
// ===----------------------------------------------------------------------===

bool Sema::isValidSveBitcast(QualType srcTy, QualType destTy) {
  assert(srcTy->isVectorType() || destTy->isVectorType());

  auto ValidScalableConversion = [](QualType FirstType, QualType SecondType) {
    if (!FirstType->isSVESizelessBuiltinType())
      return false;

    const auto *VecTy = SecondType->getAs<VectorType>();
    return VecTy && VecTy->getVectorKind() == VectorKind::SveFixedLengthData;
  };

  return ValidScalableConversion(srcTy, destTy) ||
         ValidScalableConversion(destTy, srcTy);
}

bool Sema::areMatrixTypesOfTheSameDimension(QualType srcTy, QualType destTy) {
  if (!destTy->isMatrixType() || !srcTy->isMatrixType())
    return false;

  const ConstantMatrixType *matSrcType = srcTy->getAs<ConstantMatrixType>();
  const ConstantMatrixType *matDestType = destTy->getAs<ConstantMatrixType>();

  return matSrcType->getNumRows() == matDestType->getNumRows() &&
         matSrcType->getNumColumns() == matDestType->getNumColumns();
}

bool Sema::areVectorTypesSameSize(QualType SrcTy, QualType DestTy) {
  assert(DestTy->isVectorType() || SrcTy->isVectorType());

  uint64_t SrcLen, DestLen;
  QualType SrcEltTy, DestEltTy;
  if (!decomposeVectorType(SrcTy, SrcLen, SrcEltTy))
    return false;
  if (!decomposeVectorType(DestTy, DestLen, DestEltTy))
    return false;

  // TreeContext::getTypeSize will return the size rounded up to a
  // power of 2, so instead of using that, we need to use the raw
  // element size multiplied by the element count.
  uint64_t SrcEltSize = Context.getTypeSize(SrcEltTy);
  uint64_t DestEltSize = Context.getTypeSize(DestEltTy);

  return (SrcLen * SrcEltSize == DestLen * DestEltSize);
}

bool Sema::areLaxCompatibleVectorTypes(QualType srcTy, QualType destTy) {
  assert(destTy->isVectorType() || srcTy->isVectorType());

  // Disallow lax conversions between scalars and ExtVectors (these
  // conversions are allowed for other vector types because common headers
  // depend on them).  Most scalar OP ExtVector cases are handled by the
  // splat path anyway, which does what we want (convert, not bitcast).
  // What this rules out for ExtVectors is crazy things like char4*float.
  if (srcTy->isScalarType() && destTy->isExtVectorType())
    return false;
  if (destTy->isScalarType() && srcTy->isExtVectorType())
    return false;

  return areVectorTypesSameSize(srcTy, destTy);
}

bool Sema::isLaxVectorConversion(QualType srcTy, QualType destTy) {
  assert(destTy->isVectorType() || srcTy->isVectorType());

  switch (Context.getLangOpts().getLaxVectorConversions()) {
  case LangOptions::LaxVectorConversionKind::None:
    return false;

  case LangOptions::LaxVectorConversionKind::Integer:
    if (!srcTy->isIntegralOrEnumerationType()) {
      auto *Vec = srcTy->getAs<VectorType>();
      if (!Vec || !Vec->getElementType()->isIntegralOrEnumerationType())
        return false;
    }
    if (!destTy->isIntegralOrEnumerationType()) {
      auto *Vec = destTy->getAs<VectorType>();
      if (!Vec || !Vec->getElementType()->isIntegralOrEnumerationType())
        return false;
    }
    // OK, integer (vector) -> integer (vector) bitcast.
    break;

  case LangOptions::LaxVectorConversionKind::All:
    break;
  }

  return areLaxCompatibleVectorTypes(srcTy, destTy);
}

bool Sema::CheckMatrixCast(SourceRange R, QualType DestTy, QualType SrcTy,
                           CastKind &Kind) {
  if (SrcTy->isMatrixType() && DestTy->isMatrixType()) {
    if (!areMatrixTypesOfTheSameDimension(SrcTy, DestTy)) {
      return Diag(R.getBegin(), diag::err_invalid_conversion_between_matrixes)
             << DestTy << SrcTy << R;
    }
  } else if (SrcTy->isMatrixType()) {
    return Diag(R.getBegin(),
                diag::err_invalid_conversion_between_matrix_and_type)
           << SrcTy << DestTy << R;
  } else if (DestTy->isMatrixType()) {
    return Diag(R.getBegin(),
                diag::err_invalid_conversion_between_matrix_and_type)
           << DestTy << SrcTy << R;
  }

  Kind = CK_MatrixCast;
  return false;
}

bool Sema::CheckVectorCast(SourceRange R, QualType VectorTy, QualType Ty,
                           CastKind &Kind) {
  assert(VectorTy->isVectorType() && "Not a vector type!");

  if (Ty->isVectorType() || Ty->isIntegralType(Context)) {
    if (!areLaxCompatibleVectorTypes(Ty, VectorTy))
      return Diag(R.getBegin(),
                  Ty->isVectorType()
                      ? diag::err_invalid_conversion_between_vectors
                      : diag::err_invalid_conversion_between_vector_and_integer)
             << VectorTy << Ty << R;
  } else
    return Diag(R.getBegin(),
                diag::err_invalid_conversion_between_vector_and_scalar)
           << VectorTy << Ty << R;

  Kind = CK_BitCast;
  return false;
}

ExprResult Sema::prepareVectorSplat(QualType VectorTy, Expr *SplattedExpr) {
  QualType DestElemTy = VectorTy->castAs<VectorType>()->getElementType();

  if (DestElemTy == SplattedExpr->getType())
    return SplattedExpr;

  assert(DestElemTy->isFloatingType() ||
         DestElemTy->isIntegralOrEnumerationType());

  CastKind CK;
  if (VectorTy->isExtVectorType() && SplattedExpr->getType()->isBooleanType()) {
    // Convert `true` boolean expressions to -1 when splatting vectors.
    if (DestElemTy->isFloatingType()) {
      // To avoid having to have a CK_BooleanToSignedFloating cast kind, we cast
      // in two steps: boolean to signed integral, then to floating.
      ExprResult CastExprRes = ImpCastExprToType(SplattedExpr, Context.IntTy,
                                                 CK_BooleanToSignedIntegral);
      SplattedExpr = CastExprRes.get();
      CK = CK_IntegralToFloating;
    } else {
      CK = CK_BooleanToSignedIntegral;
    }
  } else {
    ExprResult CastExprRes = SplattedExpr;
    CK = PrepareScalarCast(CastExprRes, DestElemTy);
    if (CastExprRes.isInvalid())
      return ExprError();
    SplattedExpr = CastExprRes.get();
  }
  return ImpCastExprToType(SplattedExpr, DestElemTy, CK);
}

ExprResult Sema::CheckExtVectorCast(SourceRange R, QualType DestTy,
                                    Expr *CastExpr, CastKind &Kind) {
  assert(DestTy->isExtVectorType() && "Not an extended vector type!");

  QualType SrcTy = CastExpr->getType();

  // If SrcTy is a VectorType, the total size must match to explicitly cast to
  // an ExtVectorType.
  if (SrcTy->isVectorType()) {
    if (!areLaxCompatibleVectorTypes(SrcTy, DestTy)) {
      Diag(R.getBegin(), diag::err_invalid_conversion_between_ext_vectors)
          << DestTy << SrcTy << R;
      return ExprError();
    }
    Kind = CK_BitCast;
    return CastExpr;
  }

  // All non-pointer scalars can be cast to ExtVector type.  The appropriate
  // conversion will take place first from scalar to elt type, and then
  // splat from elt type to vector.
  if (SrcTy->isPointerType())
    return Diag(R.getBegin(),
                diag::err_invalid_conversion_between_vector_and_scalar)
           << DestTy << SrcTy << R;

  Kind = CK_VectorSplat;
  return prepareVectorSplat(DestTy, CastExpr);
}

ExprResult Sema::OnCastExpr(Scope *S, SourceLocation LParenLoc, Declarator &D,
                            ParsedType &Ty, SourceLocation RParenLoc,
                            Expr *CastExpr) {
  assert(!D.isInvalidType() && (CastExpr != nullptr) &&
         "OnCastExpr(): missing type or expr");

  TypeSourceInfo *castTInfo = ResolveDeclaratorTypeCast(D, CastExpr->getType());
  if (D.isInvalidType())
    return ExprError();

  checkUnusedDeclAttributes(D);

  QualType castType = castTInfo->getType();
  Ty = CreateParsedType(castType, castTInfo);

  // If the Expr being casted is a ParenListExpr, handle it specially.
  // Turn the ParenListExpr into a sequence of BinOp comma operators.
  if (isa<ParenListExpr>(CastExpr)) {
    ExprResult Result = MaybeConvertParenListExprToParenExpr(S, CastExpr);
    if (Result.isInvalid())
      return ExprError();
    CastExpr = Result.get();
  }
  DiscardMisalignedMemberAddress(castType.getTypePtr(), CastExpr);

  return FormCStyleCastExpr(LParenLoc, castTInfo, RParenLoc, CastExpr);
}

ExprResult Sema::MaybeConvertParenListExprToParenExpr(Scope *S,
                                                      Expr *OrigExpr) {
  ParenListExpr *E = dyn_cast<ParenListExpr>(OrigExpr);
  if (!E)
    return OrigExpr;

  ExprResult Result(E->getExpr(0));

  for (unsigned i = 1, e = E->getNumExprs(); i != e && !Result.isInvalid(); ++i)
    Result =
        OnBinOp(S, E->getExprLoc(), tok::comma, Result.get(), E->getExpr(i));

  if (Result.isInvalid())
    return ExprError();

  return OnParenExpr(E->getLParenLoc(), E->getRParenLoc(), Result.get());
}

ExprResult Sema::OnParenListExpr(SourceLocation L, SourceLocation R,
                                 MultiExprArg Val) {
  return ParenListExpr::Create(Context, L, Val, R);
}



// Check that the SME attributes for PSTATE.ZA and PSTATE.SM are compatible.
bool Sema::IsInvalidSMECallConversion(QualType FromType, QualType ToType,
                                      AArch64SMECallConversionKind C) {
  unsigned FromAttributes = 0, ToAttributes = 0;
  if (const auto *FromFn =
          dyn_cast<FunctionProtoType>(Context.getCanonicalType(FromType)))
    FromAttributes =
        FromFn->getAArch64SMEAttributes() & FunctionType::SME_AttributeMask;
  if (const auto *ToFn =
          dyn_cast<FunctionProtoType>(Context.getCanonicalType(ToType)))
    ToAttributes =
        ToFn->getAArch64SMEAttributes() & FunctionType::SME_AttributeMask;

  if (FromAttributes == ToAttributes)
    return false;

  // If the '__arm_preserves_za' is the only difference between the types,
  // check whether we're allowed to add or remove it.
  if ((FromAttributes ^ ToAttributes) ==
      FunctionType::SME_PStateZAPreservedMask) {
    switch (C) {
    case AArch64SMECallConversionKind::MatchExactly:
      return true;
    case AArch64SMECallConversionKind::MayAddPreservesZA:
      return !(ToAttributes & FunctionType::SME_PStateZAPreservedMask);
    case AArch64SMECallConversionKind::MayDropPreservesZA:
      return !(FromAttributes & FunctionType::SME_PStateZAPreservedMask);
    }
  }

  // There has been a mismatch of attributes
  return true;
}

// checkPointerTypesForAssignment - This is a very tricky routine (despite
// being closely modeled after the C99 spec:-). The odd characteristic of this
// routine is it effectively iqnores the qualifiers on the top level pointee.
// This circumvents the usual type rules specified in 6.2.7p1 & 6.7.5.[1-3].
namespace {
Sema::AssignConvertType checkPointerTypesForAssignment(Sema &S,
                                                       QualType LHSType,
                                                       QualType RHSType,
                                                       SourceLocation Loc) {
  assert(LHSType.isCanonical() && "LHS not canonicalized!");
  assert(RHSType.isCanonical() && "RHS not canonicalized!");

  // get the "pointed to" type (ignoring qualifiers at the top level)
  const Type *lhptee, *rhptee;
  Qualifiers lhq, rhq;
  std::tie(lhptee, lhq) =
      cast<PointerType>(LHSType)->getPointeeType().split().asPair();
  std::tie(rhptee, rhq) =
      cast<PointerType>(RHSType)->getPointeeType().split().asPair();

  Sema::AssignConvertType ConvTy = Sema::Compatible;

  // C99 6.5.16.1p1: This following citation is common to constraints
  // 3 & 4 (below). ...and the type *pointed to* by the left has all the
  // qualifiers of the type *pointed to* by the right;

  if (!lhq.compatiblyIncludes(rhq)) {
    // Treat address-space mismatches as fatal.
    if (!lhq.isAddressSpaceSupersetOf(rhq))
      return Sema::IncompatiblePointerDiscardsQualifiers;
    // For GCC/MS compatibility, other qualifier mismatches are treated
    // as still compatible in C.
    else
      ConvTy = Sema::CompatiblePointerDiscardsQualifiers;
  }

  // C99 6.5.16.1p1 (constraint 4): If one operand is a pointer to an object or
  // incomplete type and the other is a pointer to a qualified or unqualified
  // version of void...
  if (lhptee->isVoidType()) {
    if (rhptee->isIncompleteOrObjectType())
      return ConvTy;

    // As an extension, we allow cast to/from void* to function pointer.
    assert(rhptee->isFunctionType());
    return Sema::FunctionVoidPointer;
  }

  if (rhptee->isVoidType()) {
    if (lhptee->isIncompleteOrObjectType())
      return ConvTy;

    // As an extension, we allow cast to/from void* to function pointer.
    assert(lhptee->isFunctionType());
    return Sema::FunctionVoidPointer;
  }

  if (!S.Diags.isIgnored(
          diag::warn_typecheck_convert_incompatible_function_pointer_strict,
          Loc) &&
      RHSType->isFunctionPointerType() && LHSType->isFunctionPointerType() &&
      !S.IsFunctionConversion(RHSType, LHSType, RHSType))
    return Sema::IncompatibleFunctionPointerStrict;

  // C99 6.5.16.1p1 (constraint 3): both operands are pointers to qualified or
  // unqualified versions of compatible types, ...
  QualType ltrans = QualType(lhptee, 0), rtrans = QualType(rhptee, 0);
  if (!S.Context.typesAreCompatible(ltrans, rtrans)) {
    // Check if the pointee types are compatible ignoring the sign.
    // We explicitly check for char so that we catch "char" vs
    // "unsigned char" on systems where "char" is unsigned.
    if (lhptee->isCharType())
      ltrans = S.Context.UnsignedCharTy;
    else if (lhptee->hasSignedIntegerRepresentation())
      ltrans = S.Context.getCorrespondingUnsignedType(ltrans);

    if (rhptee->isCharType())
      rtrans = S.Context.UnsignedCharTy;
    else if (rhptee->hasSignedIntegerRepresentation())
      rtrans = S.Context.getCorrespondingUnsignedType(rtrans);

    if (ltrans == rtrans) {
      // Types are compatible ignoring the sign. Qualifier incompatibility
      // takes priority over sign incompatibility because the sign
      // warning can be disabled.
      if (ConvTy != Sema::Compatible)
        return ConvTy;

      return Sema::IncompatiblePointerSign;
    }

    // If we are a multi-level pointer, it's possible that our issue is simply
    // one of qualification - e.g. char ** -> const char ** is not allowed. If
    // the eventual target type is the same and the pointers have the same
    // level of indirection, this must be the issue.
    if (isa<PointerType>(lhptee) && isa<PointerType>(rhptee)) {
      do {
        std::tie(lhptee, lhq) =
            cast<PointerType>(lhptee)->getPointeeType().split().asPair();
        std::tie(rhptee, rhq) =
            cast<PointerType>(rhptee)->getPointeeType().split().asPair();

        // Inconsistent address spaces at this point is invalid, even if the
        // address spaces would be compatible.
        // This doesn't catch address space mismatches for pointers of
        // different nesting levels, like:
        //   __local int *** a;
        //   int ** b = a;
        // It's not clear how to actually determine when such pointers are
        // invalidly incompatible.
        if (lhq.getAddressSpace() != rhq.getAddressSpace())
          return Sema::IncompatibleNestedPointerAddressSpaceMismatch;

      } while (isa<PointerType>(lhptee) && isa<PointerType>(rhptee));

      if (lhptee == rhptee)
        return Sema::IncompatibleNestedPointerQualifiers;
    }

    // General pointer incompatibility takes priority over qualifiers.
    if (RHSType->isFunctionPointerType() && LHSType->isFunctionPointerType())
      return Sema::IncompatibleFunctionPointer;
    return Sema::IncompatiblePointer;
  }
  if (S.IsFunctionConversion(ltrans, rtrans, ltrans))
    return Sema::IncompatibleFunctionPointer;
  if (S.IsInvalidSMECallConversion(
          rtrans, ltrans,
          Sema::AArch64SMECallConversionKind::MayDropPreservesZA))
    return Sema::IncompatibleFunctionPointer;
  return ConvTy;
}
} // namespace

Sema::AssignConvertType Sema::CheckAssignmentConstraints(SourceLocation Loc,
                                                         QualType LHSType,
                                                         QualType RHSType) {
  // Fake up an opaque expression.  We don't actually care about what
  // cast operations are required, so if CheckAssignmentConstraints
  // adds casts to this they'll be wasted, but fortunately that doesn't
  // usually happen on valid code.
  OpaqueValueExpr RHSExpr(Loc, RHSType, VK_PRValue);
  ExprResult RHSPtr = &RHSExpr;
  CastKind K;

  return CheckAssignmentConstraints(LHSType, RHSPtr, K, /*ConvertRHS=*/false);
}

Sema::AssignConvertType Sema::CheckAssignmentConstraints(QualType LHSType,
                                                         ExprResult &RHS,
                                                         CastKind &Kind,
                                                         bool ConvertRHS) {
  QualType RHSType = RHS.get()->getType();

  // Get canonical types.  We're not formatting these types, just comparing
  // them.
  LHSType = Context.getCanonicalType(LHSType).getUnqualifiedType();
  RHSType = Context.getCanonicalType(RHSType).getUnqualifiedType();

  // Common case: no conversion required.
  if (LHSType == RHSType) {
    Kind = CK_NoOp;
    return Compatible;
  }

  // If the LHS has an __auto_type, there are no additional type constraints
  // to be worried about.
  if (const auto *AT = dyn_cast<AutoType>(LHSType)) {
    if (AT->isGNUAutoType()) {
      Kind = CK_NoOp;
      return Compatible;
    }
  }

  // If we have an atomic type, try a non-atomic assignment, then just add an
  // atomic qualification step.
  if (const AtomicType *AtomicTy = dyn_cast<AtomicType>(LHSType)) {
    Sema::AssignConvertType result =
        CheckAssignmentConstraints(AtomicTy->getValueType(), RHS, Kind);
    if (result != Compatible)
      return result;
    if (Kind != CK_NoOp && ConvertRHS)
      RHS = ImpCastExprToType(RHS.get(), AtomicTy->getValueType(), Kind);
    Kind = CK_NonAtomicToAtomic;
    return Compatible;
  }

  // Allow scalar to ExtVector assignments, and assignments of an ExtVector type
  // to the same ExtVector type.
  if (LHSType->isExtVectorType()) {
    if (RHSType->isExtVectorType())
      return Incompatible;
    if (RHSType->isArithmeticType()) {
      // CK_VectorSplat does T -> vector T, so first cast to the element type.
      if (ConvertRHS)
        RHS = prepareVectorSplat(LHSType, RHS.get());
      Kind = CK_VectorSplat;
      return Compatible;
    }
  }

  // Conversions to or from vector type.
  if (LHSType->isVectorType() || RHSType->isVectorType()) {
    if (LHSType->isVectorType() && RHSType->isVectorType()) {
      // Allow assignments between compatible vector types
      if (Context.areCompatibleVectorTypes(LHSType, RHSType)) {
        Kind = CK_BitCast;
        return Compatible;
      }

      // If we are allowing lax vector conversions, and LHS and RHS are both
      // vectors, the total size only needs to be the same. This is a bitcast;
      // no bits are changed but the result type is different.
      if (isLaxVectorConversion(RHSType, LHSType)) {
        Kind = CK_BitCast;
        return IncompatibleVectors;
      }
    }

    // When the RHS comes from another lax conversion (e.g. binops between
    // scalars and vectors) the result is canonicalized as a vector. When the
    // LHS is also a vector, the lax is allowed by the condition above. Handle
    // the case where LHS is a scalar.
    if (LHSType->isScalarType()) {
      const VectorType *VecType = RHSType->getAs<VectorType>();
      if (VecType && VecType->getNumElements() == 1 &&
          isLaxVectorConversion(RHSType, LHSType)) {
        ExprResult *VecExpr = &RHS;
        *VecExpr = ImpCastExprToType(VecExpr->get(), LHSType, CK_BitCast);
        Kind = CK_BitCast;
        return Compatible;
      }
    }

    // Allow assignments between fixed-length and sizeless SVE vectors.
    if ((LHSType->isSVESizelessBuiltinType() && RHSType->isVectorType()) ||
        (LHSType->isVectorType() && RHSType->isSVESizelessBuiltinType()))
      if (Context.areCompatibleSveTypes(LHSType, RHSType) ||
          Context.areLaxCompatibleSveTypes(LHSType, RHSType)) {
        Kind = CK_BitCast;
        return Compatible;
      }

    return Incompatible;
  }

  // Arithmetic conversions.
  if (LHSType->isArithmeticType() && RHSType->isArithmeticType()) {
    if (ConvertRHS)
      Kind = PrepareScalarCast(RHS, LHSType);
    return Compatible;
  }

  // Conversions to normal pointers.
  if (const PointerType *LHSPointer = dyn_cast<PointerType>(LHSType)) {
    // U* -> T*
    if (isa<PointerType>(RHSType)) {
      LangAS AddrSpaceL = LHSPointer->getPointeeType().getAddressSpace();
      LangAS AddrSpaceR = RHSType->getPointeeType().getAddressSpace();
      if (AddrSpaceL != AddrSpaceR)
        Kind = CK_AddressSpaceConversion;
      else if (Context.hasCvrSimilarType(RHSType, LHSType))
        Kind = CK_NoOp;
      else
        Kind = CK_BitCast;
      return checkPointerTypesForAssignment(*this, LHSType, RHSType,
                                            RHS.get()->getBeginLoc());
    }

    // int -> T*
    if (RHSType->isIntegerType()) {
      Kind = CK_IntegralToPointer;
      return IntToPointer;
    }

    return Incompatible;
  }

  // Conversion to nullptr_t (C23 only)
  if (getLangOpts().C23 && LHSType->isNullPtrType() &&
      RHS.get()->isNullPointerConstant(Context,
                                       Expr::NPC_ValueDependentIsNull)) {
    // null -> nullptr_t
    Kind = CK_NullToPointer;
    return Compatible;
  }

  // Conversions from pointers that are not covered by the above.
  if (isa<PointerType>(RHSType)) {
    // T* -> _Bool
    if (LHSType == Context.BoolTy) {
      Kind = CK_PointerToBoolean;
      return Compatible;
    }

    // T* -> int
    if (LHSType->isIntegerType()) {
      Kind = CK_PointerToIntegral;
      return PointerToInt;
    }

    return Incompatible;
  }

  // struct A -> struct B
  if (isa<TagType>(LHSType) && isa<TagType>(RHSType)) {
    if (Context.typesAreCompatible(LHSType, RHSType)) {
      Kind = CK_NoOp;
      return Compatible;
    }
  }

  return Incompatible;
}

namespace {
void constructTransparentUnion(Sema &S, TreeContext &C, ExprResult &EResult,
                               QualType UnionType, FieldDecl *Field) {
  Expr *E = EResult.get();
  InitListExpr *Initializer =
      new (C) InitListExpr(C, SourceLocation(), E, SourceLocation());
  Initializer->setType(UnionType);
  Initializer->setInitializedFieldInUnion(Field);

  TypeSourceInfo *unionTInfo = C.getTrivialTypeSourceInfo(UnionType);
  EResult = new (C) CompoundLiteralExpr(SourceLocation(), unionTInfo, UnionType,
                                        VK_PRValue, Initializer, false);
}
} // namespace

Sema::AssignConvertType
Sema::CheckTransparentUnionArgumentConstraints(QualType ArgType,
                                               ExprResult &RHS) {
  QualType RHSType = RHS.get()->getType();

  // If the ArgType is a Union type, we want to handle a potential
  // transparent_union GCC extension.
  const RecordType *UT = ArgType->getAsUnionType();
  if (!UT || !UT->getDecl()->hasAttr<TransparentUnionAttr>())
    return Incompatible;

  // The field to initialize within the transparent union.
  RecordDecl *UD = UT->getDecl();
  FieldDecl *InitField = nullptr;
  // It's compatible if the expression matches any of the fields.
  for (auto *it : UD->fields()) {
    if (it->getType()->isPointerType()) {
      // If the transparent union contains a pointer type, we allow:
      // 1) void pointer
      // 2) null pointer constant
      if (RHSType->isPointerType())
        if (RHSType->castAs<PointerType>()->getPointeeType()->isVoidType()) {
          RHS = ImpCastExprToType(RHS.get(), it->getType(), CK_BitCast);
          InitField = it;
          break;
        }

      if (RHS.get()->isNullPointerConstant(Context,
                                           Expr::NPC_ValueDependentIsNull)) {
        RHS = ImpCastExprToType(RHS.get(), it->getType(), CK_NullToPointer);
        InitField = it;
        break;
      }
    }

    CastKind Kind;
    if (CheckAssignmentConstraints(it->getType(), RHS, Kind) == Compatible) {
      RHS = ImpCastExprToType(RHS.get(), it->getType(), Kind);
      InitField = it;
      break;
    }
  }

  if (!InitField)
    return Incompatible;

  constructTransparentUnion(*this, Context, RHS, ArgType, InitField);
  return Compatible;
}

namespace neverc {
ExprResult buildNeverCStringLiteral(Sema &S, QualType StringTy,
                                    Expr *Initializer, StringLiteral *SL) {
  TreeContext &Context = S.Context;

  // Fold wide / UTF-16 / UTF-32 source bytes into a compile-time UTF-8
  // byte stream so the byte-shaped `string` value type splices the
  // bytes directly.  When folding occurs we also adopt the synthesised
  // ordinary literal as the `Initializer`, so the array decay below
  // sees `char[N]` (decaying to `const char *`) rather than the source
  // wide-character array.  Ordinary / UTF-8 literals pass through
  // unchanged.
  StringLiteral *FoldedSL = foldNeverCStringWideLiteralToUtf8(S, SL);
  if (!FoldedSL)
    return ExprError();
  if (FoldedSL != SL) {
    SL = FoldedSL;
    Initializer = FoldedSL;
  }

  ExprResult Data = S.DefaultFunctionArrayLvalueConversion(Initializer,
                                                           /*Diagnose=*/false);
  if (Data.isInvalid())
    return ExprError();

  QualType DataTy = Context.getPointerType(Context.CharTy.withConst());
  if (Data.get()->getType() != DataTy)
    Data = S.ImpCastExprToType(Data.get(), DataTy, CK_NoOp);
  if (Data.isInvalid())
    return ExprError();

  QualType SizeTy = Context.getSizeType();
  unsigned SizeBits = Context.getTypeSize(SizeTy);

  Expr *Len =
      IntegerLiteral::Create(Context, llvm::APInt(SizeBits, SL->getLength()),
                             SizeTy, Initializer->getExprLoc());
  Expr *Cap = IntegerLiteral::Create(Context, llvm::APInt(SizeBits, 0), SizeTy,
                                     Initializer->getExprLoc());

  Expr *Inits[] = {Data.get(), Len, Cap};
  auto *ILE = new (Context) InitListExpr(Context, Initializer->getBeginLoc(),
                                         Inits, Initializer->getEndLoc());
  ILE->setType(StringTy);

  bool IsFileScope = !S.CurContext->isFunctionOrMethod();
  if (IsFileScope) {
    for (unsigned I = 0, E = ILE->getNumInits(); I != E; ++I)
      ILE->setInit(I, ConstantExpr::Create(Context, ILE->getInit(I)));
  }

  TypeSourceInfo *TInfo =
      Context.getTrivialTypeSourceInfo(StringTy, Initializer->getBeginLoc());
  return new (Context) CompoundLiteralExpr(SourceLocation(), TInfo, StringTy,
                                           VK_LValue, ILE, IsFileScope);
}
} // namespace neverc

namespace {
ExprResult buildNeverCStringRetain(Sema &S, SourceLocation Loc, Expr *Value) {
  Expr *Args[] = {Value};
  return buildNeverCStringRuntimeCall(S, /*Scope=*/nullptr, Loc,
                                      BuiltinStringNames::RetainFunctionName,
                                      Args, Loc);
}
} // namespace


Sema::AssignConvertType
Sema::CheckSingleAssignmentConstraints(QualType LHSType, ExprResult &CallerRHS,
                                       bool Diagnose, bool ConvertRHS) {
  // We need to be able to tell the caller whether we diagnosed a problem, if
  // they ask us to issue diagnostics.
  assert((ConvertRHS || !Diagnose) && "can't indicate whether we diagnosed");

  // If ConvertRHS is false, we want to leave the caller's RHS untouched. Sadly,
  // we can't avoid *all* modifications at the moment, so we need some somewhere
  // to put the updated value.
  ExprResult LocalRHS = CallerRHS;
  ExprResult &RHS = ConvertRHS ? CallerRHS : LocalRHS;

  if (const auto *LHSPtrType = LHSType->getAs<PointerType>()) {
    if (const auto *RHSPtrType = RHS.get()->getType()->getAs<PointerType>()) {
      if (RHSPtrType->getPointeeType()->hasAttr(attr::NoDeref) &&
          !LHSPtrType->getPointeeType()->hasAttr(attr::NoDeref)) {
        Diag(RHS.get()->getExprLoc(),
             diag::warn_noderef_to_dereferenceable_pointer)
            << RHS.get()->getSourceRange();
      }
    }
  }

  if (RHS.get()->getType() == Context.OverloadTy)
    return Incompatible;

  if (this->isNeverCStringType(LHSType) &&
      !isInsideNeverCStringRuntime()) {
    if (StringLiteral *SL = getNeverCStringLiteral(RHS.get())) {
      if (ConvertRHS) {
        ExprResult Converted =
            buildNeverCStringLiteral(*this, LHSType, RHS.get(), SL);
        if (Converted.isInvalid())
          return Incompatible;
        if (CurContext->isFunctionOrMethod()) {
          Converted = buildNeverCStringRetain(*this, RHS.get()->getExprLoc(),
                                              Converted.get());
          if (Converted.isInvalid())
            return Incompatible;
        }
        RHS = Converted;
      }
      return Compatible;
    }

    if (this->isNeverCStringType(RHS.get()->getType()) &&
        RHS.get()->getValueKind() == VK_LValue &&
        !isa<CompoundLiteralExpr>(RHS.get()->IgnoreParens())) {
      if (ConvertRHS) {
        ExprResult Converted =
            buildNeverCStringRetain(*this, RHS.get()->getExprLoc(), RHS.get());
        if (Converted.isInvalid())
          return Incompatible;
        RHS = Converted;
      }
      return Compatible;
    }
  }

  RHS = DefaultFunctionArrayLvalueConversion(RHS.get(), Diagnose);
  if (RHS.isInvalid())
    return Incompatible;

  // The constraints are expressed in terms of the atomic, qualified, or
  // unqualified type of the LHS.
  QualType LHSTypeAfterConversion = LHSType.getAtomicUnqualifiedType();

  // C99 6.5.16.1p1: the left operand is a pointer and the right is
  // a null pointer constant <C23>or its type is nullptr_t;</C23>.
  if (LHSTypeAfterConversion->isPointerType() &&
      ((getLangOpts().C23 && RHS.get()->getType()->isNullPtrType()) ||
       RHS.get()->isNullPointerConstant(Context,
                                        Expr::NPC_ValueDependentIsNull))) {
    if (Diagnose || ConvertRHS) {
      CastKind Kind;
      CheckPointerConversion(RHS.get(), LHSType, Kind,
                             /*IgnoreBaseAccess=*/false, Diagnose);
      if (ConvertRHS)
        RHS = ImpCastExprToType(RHS.get(), LHSType, Kind, VK_PRValue);
    }
    return Compatible;
  }
  // C23 6.5.16.1p1: the left operand has type atomic, qualified, or
  // unqualified bool, and the right operand is a pointer or its type is
  // nullptr_t.
  if (getLangOpts().C23 && LHSType->isBooleanType() &&
      RHS.get()->getType()->isNullPtrType()) {
    // NB: T* -> _Bool is handled in CheckAssignmentConstraints, this only
    // only handles nullptr -> _Bool due to needing an extra conversion
    // step.
    // We model this by converting from nullptr -> void * and then let the
    // conversion from void * -> _Bool happen naturally.
    if (Diagnose || ConvertRHS) {
      CastKind Kind;
      CheckPointerConversion(RHS.get(), Context.VoidPtrTy, Kind,
                             /*IgnoreBaseAccess=*/false, Diagnose);
      if (ConvertRHS)
        RHS = ImpCastExprToType(RHS.get(), Context.VoidPtrTy, Kind, VK_PRValue);
    }
  }

  CastKind Kind;
  Sema::AssignConvertType result =
      CheckAssignmentConstraints(LHSType, RHS, Kind, ConvertRHS);

  // C99 6.5.16.1p2: The value of the right operand is converted to the
  if (result != Incompatible && RHS.get()->getType() != LHSType) {
    QualType Ty = LHSType.getNonLValueExprType(Context);
    Expr *E = RHS.get();

    if (ConvertRHS)
      RHS = ImpCastExprToType(E, Ty, Kind);
  }

  return result;
}
