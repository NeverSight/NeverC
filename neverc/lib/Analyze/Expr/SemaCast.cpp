#include "neverc/Analyze/SemaInternal.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Scan/PrepEngine.h"
#include "llvm/ADT/SmallVector.h"
using namespace neverc;

// ===----------------------------------------------------------------------===
// Cast expression analysis
// ===----------------------------------------------------------------------===

namespace {
struct CastOperation {
  CastOperation(Sema &S, QualType destType, ExprResult src)
      : Self(S), SrcExpr(src), DestType(destType),
        ResultType(destType.getNonLValueExprType(S.Context)),
        ValueKind(VK_PRValue), Kind(CK_Dependent) {

    // Cast target type: strip to unqualified, non-atomic scalar (C23
    // parenthesized cast; prvalue adjustment applies for scalars).
    if (!DestType->isRecordType() && !DestType->isArrayType()) {
      DestType = DestType.getAtomicUnqualifiedType();
    }
  }

  Sema &Self;
  ExprResult SrcExpr;
  QualType DestType;
  QualType ResultType;
  ExprValueKind ValueKind;
  CastKind Kind;

  SourceRange OpRange;
  SourceRange DestRange;

  // Top-level semantics-checking routines.
  void CheckCStyleCast();

  void updatePartOfExplicitCastFlags(CastExpr *CE) {
    // Walk down from the CE to the OrigSrcExpr, and mark all immediate
    // ImplicitCastExpr's as being part of ExplicitCastExpr. The original CE
    // (which is a ExplicitCastExpr), and the OrigSrcExpr are not touched.
    for (; auto *ICE = dyn_cast<ImplicitCastExpr>(CE->getSubExpr()); CE = ICE)
      ICE->setIsPartOfExplicitCast(true);
  }

  ExprResult complete(CastExpr *castExpr) {
    updatePartOfExplicitCastFlags(castExpr);
    return castExpr;
  }

  void checkCastAlign() {
    Self.CheckCastAlign(SrcExpr.get(), DestType, OpRange);
  }
};

void checkNoDeref(Sema &S, const QualType FromType, const QualType ToType,
                  SourceLocation OpLoc) {
  if (const auto *PtrType = dyn_cast<PointerType>(FromType)) {
    if (PtrType->getPointeeType()->hasAttr(attr::NoDeref)) {
      if (const auto *DestType = dyn_cast<PointerType>(ToType)) {
        if (!DestType->getPointeeType()->hasAttr(attr::NoDeref)) {
          S.Diag(OpLoc, diag::warn_noderef_to_dereferenceable_pointer);
        }
      }
    }
  }
}

struct checkNoDerefRAII {
  checkNoDerefRAII(CastOperation &Op) : Op(Op) {}
  ~checkNoDerefRAII() {
    if (!Op.SrcExpr.isInvalid())
      checkNoDeref(Op.Self, Op.SrcExpr.get()->getType(), Op.ResultType,
                   Op.OpRange.getBegin());
  }

  CastOperation &Op;
};

enum CastAwayConstnessKind {
  CACK_None = 0,
  CACK_Similar = 1,
  CACK_SimilarKind = 2,
  CACK_Incoherent = 3,
};

void diagnoseCastQual(Sema &Self, const ExprResult &SrcExpr, QualType DestType);

CastAwayConstnessKind unwrapCastAwayConstnessLevel(TreeContext &Context,
                                                   QualType &T1, QualType &T2) {
  enum { None, Ptr, Array };
  auto Classify = [](QualType T) {
    if (T->isAnyPointerType())
      return Ptr;
    // We somewhat-arbitrarily don't look through VLA types here. This is at
    // least consistent with the behavior of UnwrapSimilarTypes.
    if (T->isConstantArrayType() || T->isIncompleteArrayType())
      return Array;
    return None;
  };

  auto Unwrap = [&](QualType T) {
    if (auto *AT = Context.getAsArrayType(T))
      return AT->getElementType();
    return T->getPointeeType();
  };

  CastAwayConstnessKind Kind;

  if (Context.UnwrapSimilarTypes(T1, T2)) {
    Kind = CastAwayConstnessKind::CACK_Similar;
  } else {
    // Try unwrapping mismatching levels.
    int T1Class = Classify(T1);
    if (T1Class == None)
      return CastAwayConstnessKind::CACK_None;

    int T2Class = Classify(T2);
    if (T2Class == None)
      return CastAwayConstnessKind::CACK_None;

    T1 = Unwrap(T1);
    T2 = Unwrap(T2);
    Kind = T1Class == T2Class ? CastAwayConstnessKind::CACK_SimilarKind
                              : CastAwayConstnessKind::CACK_Incoherent;
  }

  // We've unwrapped at least one level. If the resulting T1 is a (possibly
  // multidimensional) array type, any qualifier on any matching layer of
  // T2 is considered to correspond to T1. Decompose down to the element
  // type of T1 so that we can compare properly.
  while (true) {
    Context.UnwrapSimilarArrayTypes(T1, T2);

    if (Classify(T1) != Array)
      break;

    auto T2Class = Classify(T2);
    if (T2Class == None)
      break;

    if (T2Class != Array)
      Kind = CastAwayConstnessKind::CACK_Incoherent;
    else if (Kind != CastAwayConstnessKind::CACK_Incoherent)
      Kind = CastAwayConstnessKind::CACK_SimilarKind;

    T1 = Unwrap(T1);
    T2 = Unwrap(T2).withCVRQualifiers(T2.getCVRQualifiers());
  }

  return Kind;
}

CastAwayConstnessKind
castsAwayConstness(Sema &Self, QualType SrcType, QualType DestType,
                   bool CheckCVR, bool = false,
                   QualType *TheOffendingSrcType = nullptr,
                   QualType *TheOffendingDestType = nullptr,
                   Qualifiers *CastAwayQualifiers = nullptr) {
  if (!CheckCVR)
    return CastAwayConstnessKind::CACK_None;

  assert(SrcType->isAnyPointerType() && "Source type is not a pointer.");
  assert(DestType->isAnyPointerType() && "Destination type is not a pointer.");

  QualType UnwrappedSrcType = Self.Context.getCanonicalType(SrcType),
           UnwrappedDestType = Self.Context.getCanonicalType(DestType);

  // Find the qualifiers. We only care about cvr-qualifiers for the
  // purpose of this check, because other qualifiers (address spaces,
  // etc.) are part of the type's identity.
  QualType PrevUnwrappedSrcType = UnwrappedSrcType;
  QualType PrevUnwrappedDestType = UnwrappedDestType;
  auto WorstKind = CastAwayConstnessKind::CACK_Similar;
  bool AllConstSoFar = true;
  while (auto Kind = unwrapCastAwayConstnessLevel(
             Self.Context, UnwrappedSrcType, UnwrappedDestType)) {
    // Track the worst kind of unwrap we needed to do before we found a
    // problem.
    if (Kind > WorstKind)
      WorstKind = Kind;

    // Determine the relevant qualifiers at this level.
    Qualifiers SrcQuals, DestQuals;
    Self.Context.getUnqualifiedArrayType(UnwrappedSrcType, SrcQuals);
    Self.Context.getUnqualifiedArrayType(UnwrappedDestType, DestQuals);

    if (CheckCVR) {
      Qualifiers SrcCvrQuals =
          Qualifiers::fromCVRMask(SrcQuals.getCVRQualifiers());
      Qualifiers DestCvrQuals =
          Qualifiers::fromCVRMask(DestQuals.getCVRQualifiers());

      if (SrcCvrQuals != DestCvrQuals) {
        if (CastAwayQualifiers)
          *CastAwayQualifiers = SrcCvrQuals - DestCvrQuals;

        // If we removed a cvr-qualifier, this is casting away 'constness'.
        if (!DestCvrQuals.compatiblyIncludes(SrcCvrQuals)) {
          if (TheOffendingSrcType)
            *TheOffendingSrcType = PrevUnwrappedSrcType;
          if (TheOffendingDestType)
            *TheOffendingDestType = PrevUnwrappedDestType;
          return WorstKind;
        }

        // If any prior level was not 'const', this is also casting away
        // 'constness'. We noted the outermost type missing a 'const' already.
        if (!AllConstSoFar)
          return WorstKind;
      }
    }

    // If we found our first non-const-qualified type, this may be the place
    // where things start to go wrong.
    if (AllConstSoFar && !DestQuals.hasConst()) {
      AllConstSoFar = false;
      if (TheOffendingSrcType)
        *TheOffendingSrcType = PrevUnwrappedSrcType;
      if (TheOffendingDestType)
        *TheOffendingDestType = PrevUnwrappedDestType;
    }

    PrevUnwrappedSrcType = UnwrappedSrcType;
    PrevUnwrappedDestType = UnwrappedDestType;
  }

  return CastAwayConstnessKind::CACK_None;
}

bool argTypeIsABIEquivalent(QualType SrcType, QualType DestType,
                            TreeContext &Context) {
  if (SrcType->isPointerType() && DestType->isPointerType())
    return true;

  // Allow integral type mismatch if their size are equal.
  if (SrcType->isIntegralType(Context) && DestType->isIntegralType(Context))
    if (Context.getTypeInfoInChars(SrcType).Width ==
        Context.getTypeInfoInChars(DestType).Width)
      return true;

  return Context.hasSameUnqualifiedType(SrcType, DestType);
}

unsigned int checkCastFunctionType(Sema &Self, const ExprResult &SrcExpr,
                                   QualType DestType) {
  unsigned int DiagID = 0;
  const unsigned int DiagList[] = {diag::warn_cast_function_type_strict,
                                   diag::warn_cast_function_type};
  for (auto ID : DiagList) {
    if (!Self.Diags.isIgnored(ID, SrcExpr.get()->getExprLoc())) {
      DiagID = ID;
      break;
    }
  }
  if (!DiagID)
    return 0;

  QualType SrcType = SrcExpr.get()->getType();
  const FunctionType *SrcFTy = nullptr;
  const FunctionType *DstFTy = nullptr;
  if (SrcType->isFunctionPointerType() && DestType->isFunctionPointerType()) {
    SrcFTy = SrcType->getPointeeType()->castAs<FunctionType>();
    DstFTy = DestType->getPointeeType()->castAs<FunctionType>();
  } else {
    return 0;
  }
  assert(SrcFTy && DstFTy);

  if (Self.Context.hasSameType(SrcFTy, DstFTy))
    return 0;

  // For strict checks, ensure we have an exact match.
  if (DiagID == diag::warn_cast_function_type_strict)
    return DiagID;

  auto IsVoidVoid = [](const FunctionType *T) {
    if (!T->getReturnType()->isVoidType())
      return false;
    if (const auto *PT = T->getAs<FunctionProtoType>())
      return !PT->isVariadic() && PT->getNumParams() == 0;
    return false;
  };

  // Skip if either function type is void(*)(void)
  if (IsVoidVoid(SrcFTy) || IsVoidVoid(DstFTy))
    return 0;

  // Check return type.
  if (!argTypeIsABIEquivalent(SrcFTy->getReturnType(), DstFTy->getReturnType(),
                              Self.Context))
    return DiagID;

  // Check if either has unspecified number of parameters
  if (SrcFTy->isFunctionNoProtoType() || DstFTy->isFunctionNoProtoType())
    return 0;

  // Check parameter types.

  const auto *SrcFPTy = cast<FunctionProtoType>(SrcFTy);
  const auto *DstFPTy = cast<FunctionProtoType>(DstFTy);

  // In a cast involving function types with a variable argument list only the
  // types of initial arguments that are provided are considered.
  unsigned NumParams = SrcFPTy->getNumParams();
  unsigned DstNumParams = DstFPTy->getNumParams();
  if (NumParams > DstNumParams) {
    if (!DstFPTy->isVariadic())
      return DiagID;
    NumParams = DstNumParams;
  } else if (NumParams < DstNumParams) {
    if (!SrcFPTy->isVariadic())
      return DiagID;
  }

  for (unsigned i = 0; i < NumParams; ++i)
    if (!argTypeIsABIEquivalent(SrcFPTy->getParamType(i),
                                DstFPTy->getParamType(i), Self.Context))
      return DiagID;

  return 0;
}

void diagnoseCallingConvCast(Sema &Self, const ExprResult &SrcExpr,
                             QualType DstType, SourceRange OpRange) {
  // Check if this cast would change the calling convention of a function
  // pointer type.
  QualType SrcType = SrcExpr.get()->getType();
  if (Self.Context.hasSameType(SrcType, DstType) ||
      !SrcType->isFunctionPointerType() || !DstType->isFunctionPointerType())
    return;
  const auto *SrcFTy =
      SrcType->castAs<PointerType>()->getPointeeType()->castAs<FunctionType>();
  const auto *DstFTy =
      DstType->castAs<PointerType>()->getPointeeType()->castAs<FunctionType>();
  CallingConv SrcCC = SrcFTy->getCallConv();
  CallingConv DstCC = DstFTy->getCallConv();
  if (SrcCC == DstCC)
    return;

  // We have a calling convention cast. Check if the source is a pointer to a
  // known, specific function that has already been defined.
  Expr *Src = SrcExpr.get()->IgnoreParenImpCasts();
  if (auto *UO = dyn_cast<UnaryOperator>(Src))
    if (UO->getOpcode() == UO_AddrOf)
      Src = UO->getSubExpr()->IgnoreParenImpCasts();
  auto *DRE = dyn_cast<DeclRefExpr>(Src);
  if (!DRE)
    return;
  auto *FD = dyn_cast<FunctionDecl>(DRE->getDecl());
  if (!FD)
    return;

  // Only warn if we are casting from the default convention to a non-default
  // convention. This can happen when the programmer forgot to apply the calling
  // convention to the function declaration and then inserted this cast to
  // satisfy the type system.
  CallingConv DefaultCC =
      Self.getTreeContext().getDefaultCallingConvention(FD->isVariadic());
  if (DstCC == DefaultCC || SrcCC != DefaultCC)
    return;

  // Diagnose this cast, as it is probably bad.
  llvm::StringRef SrcCCName = FunctionType::getNameForCallConv(SrcCC);
  llvm::StringRef DstCCName = FunctionType::getNameForCallConv(DstCC);
  Self.Diag(OpRange.getBegin(), diag::warn_cast_calling_conv)
      << SrcCCName << DstCCName << OpRange;

  // The checks above are cheaper than checking if the diagnostic is enabled.
  // However, it's worth checking if the warning is enabled before we construct
  // a fixit.
  if (Self.Diags.isIgnored(diag::warn_cast_calling_conv, OpRange.getBegin()))
    return;

  // Try to suggest a fixit to change the calling convention of the function
  // whose address was taken. Try to use the latest macro for the convention.
  // For example, users probably want to write "WINAPI" instead of "__stdcall"
  // to match the Windows header declarations.
  SourceLocation NameLoc = FD->getFirstDecl()->getNameInfo().getLoc();
  PrepEngine &PP = Self.getPrepEngine();
  llvm::SmallVector<TokenValue, 6> AttrTokens;
  llvm::SmallString<64> CCAttrText;
  llvm::raw_svector_ostream OS(CCAttrText);
  if (Self.getLangOpts().MicrosoftExt) {
    // __stdcall or __vectorcall
    OS << "__" << DstCCName;
    IdentifierInfo *II = PP.getIdentifierInfo(OS.str());
    AttrTokens.push_back(II->isKeyword(Self.getLangOpts())
                             ? TokenValue(II->getTokenID())
                             : TokenValue(II));
  } else {
    // __attribute__((stdcall)) or __attribute__((vectorcall))
    OS << "__attribute__((" << DstCCName << "))";
    AttrTokens.push_back(tok::kw___attribute);
    AttrTokens.push_back(tok::l_paren);
    AttrTokens.push_back(tok::l_paren);
    IdentifierInfo *II = PP.getIdentifierInfo(DstCCName);
    AttrTokens.push_back(II->isKeyword(Self.getLangOpts())
                             ? TokenValue(II->getTokenID())
                             : TokenValue(II));
    AttrTokens.push_back(tok::r_paren);
    AttrTokens.push_back(tok::r_paren);
  }
  llvm::StringRef AttrSpelling =
      PP.getLastMacroWithSpelling(NameLoc, AttrTokens);
  if (!AttrSpelling.empty())
    CCAttrText = AttrSpelling;
  OS << ' ';
  Self.Diag(NameLoc, diag::note_change_calling_conv_fixit)
      << FD << DstCCName << FixItHint::CreateInsertion(NameLoc, CCAttrText);
}

void checkIntToPointerCast(bool CStyle, const SourceRange &OpRange,
                           const Expr *SrcExpr, QualType DestType, Sema &Self) {
  QualType SrcType = SrcExpr->getType();

  // Not warning on reinterpret_cast, boolean, constant expressions, etc
  // are not explicit design choices, but consistent with GCC's behavior.
  // Feel free to modify them if you've reason/evidence for an alternative.
#ifndef _WIN32
  if (CStyle && SrcType->isIntegralType(Self.Context) &&
      !SrcType->isBooleanType() && !SrcType->isEnumeralType() &&
      !SrcExpr->isIntegerConstantExpr(Self.Context) &&
      Self.Context.getTypeSize(DestType) > Self.Context.getTypeSize(SrcType)) {
    // Separate between casts to void* and non-void* pointers.
    // Some APIs use (abuse) void* for something like a user context,
    // and often that value is an integer even if it isn't a pointer itself.
    // Having a separate warning flag allows users to control the warning
    // for their workflow.
    unsigned Diag = DestType->isVoidPointerType()
                        ? diag::warn_int_to_void_pointer_cast
                        : diag::warn_int_to_pointer_cast;
    Self.Diag(OpRange.getBegin(), Diag) << SrcType << DestType << OpRange;
  }
#endif
}

void diagnoseBadFunctionCast(Sema &Self, const ExprResult &SrcExpr,
                             QualType DestType) {
  if (Self.Diags.isIgnored(diag::warn_bad_function_cast,
                           SrcExpr.get()->getExprLoc()))
    return;

  if (!isa<CallExpr>(SrcExpr.get()))
    return;

  QualType SrcType = SrcExpr.get()->getType();
  if (DestType.getUnqualifiedType()->isVoidType())
    return;
  if (SrcType->isAnyPointerType() && DestType->isAnyPointerType())
    return;
  if (SrcType->isIntegerType() && DestType->isIntegerType() &&
      (SrcType->isBooleanType() == DestType->isBooleanType()) &&
      (SrcType->isEnumeralType() == DestType->isEnumeralType()))
    return;
  if (SrcType->isRealFloatingType() && DestType->isRealFloatingType())
    return;
  if (SrcType->isEnumeralType() && DestType->isEnumeralType())
    return;
  if (SrcType->isComplexType() && DestType->isComplexType())
    return;
  if (SrcType->isComplexIntegerType() && DestType->isComplexIntegerType())
    return;
  if (SrcType->isFixedPointType() && DestType->isFixedPointType())
    return;

  Self.Diag(SrcExpr.get()->getExprLoc(), diag::warn_bad_function_cast)
      << SrcType << DestType << SrcExpr.get()->getSourceRange();
}

} // namespace

void CastOperation::CheckCStyleCast() {
  // Cast type must be void or scalar; expression type must be scalar.
  if (DestType->isVoidType()) {
    // We don't necessarily do lvalue-to-rvalue conversions on this.
    SrcExpr = Self.IgnoredValueConversions(SrcExpr.get());
    if (SrcExpr.isInvalid())
      return;

    // Cast to void allows any expr type.
    Kind = CK_ToVoid;
    return;
  }

  if (Self.getTreeContext().isDependenceAllowed() &&
      (DestType->isDependentType() || SrcExpr.get()->isTypeDependent() ||
       SrcExpr.get()->isValueDependent())) {
    assert(Kind == CK_Dependent);
    return;
  }

  if (SrcExpr.get()->getType() == Self.Context.OverloadTy)
    return;
  SrcExpr = Self.DefaultFunctionArrayLvalueConversion(SrcExpr.get());
  if (SrcExpr.isInvalid())
    return;
  QualType SrcType = SrcExpr.get()->getType();

  assert(!SrcType->isPlaceholderType());

  if (Self.RequireCompleteType(OpRange.getBegin(), DestType,
                               diag::err_typecheck_cast_to_incomplete)) {
    SrcExpr = ExprError();
    return;
  }

  // Allow casting a sizeless built-in type to itself.
  if (DestType->isSizelessBuiltinType() &&
      Self.Context.hasSameUnqualifiedType(DestType, SrcType)) {
    Kind = CK_NoOp;
    return;
  }

  // Allow bitcasting between compatible SVE vector types.
  if ((SrcType->isVectorType() || DestType->isVectorType()) &&
      Self.isValidSveBitcast(SrcType, DestType)) {
    Kind = CK_BitCast;
    return;
  }

  if (!DestType->isScalarType() && !DestType->isVectorType() &&
      !DestType->isMatrixType()) {
    const RecordType *DestRecordTy = DestType->getAs<RecordType>();

    if (DestRecordTy &&
        Self.Context.hasSameUnqualifiedType(DestType, SrcType)) {
      // GCC struct/union extension: allow cast to self.
      Self.Diag(OpRange.getBegin(), diag::ext_typecheck_cast_nonscalar)
          << DestType << SrcExpr.get()->getSourceRange();
      Kind = CK_NoOp;
      return;
    }

    // GCC's cast to union extension.
    if (DestRecordTy && DestRecordTy->getDecl()->isUnion()) {
      RecordDecl *RD = DestRecordTy->getDecl();
      if (CastExpr::getTargetFieldForToUnionCast(RD, SrcType)) {
        Self.Diag(OpRange.getBegin(), diag::ext_typecheck_cast_to_union)
            << SrcExpr.get()->getSourceRange();
        Kind = CK_ToUnion;
        return;
      } else {
        Self.Diag(OpRange.getBegin(), diag::err_typecheck_cast_to_union_no_type)
            << SrcType << SrcExpr.get()->getSourceRange();
        SrcExpr = ExprError();
        return;
      }
    }

    // Reject any other conversions to non-scalar types.
    Self.Diag(OpRange.getBegin(), diag::err_typecheck_cond_expect_scalar)
        << DestType << SrcExpr.get()->getSourceRange();
    SrcExpr = ExprError();
    return;
  }

  // The type we're casting to is known to be a scalar, a vector, or a matrix.

  // Require the operand to be a scalar, a vector, or a matrix.
  if (!SrcType->isScalarType() && !SrcType->isVectorType() &&
      !SrcType->isMatrixType()) {
    Self.Diag(SrcExpr.get()->getExprLoc(),
              diag::err_typecheck_expect_scalar_operand)
        << SrcType << SrcExpr.get()->getSourceRange();
    SrcExpr = ExprError();
    return;
  }

  // nullptr_t can only be cast to void, bool, pointer, or itself.
  if (SrcType->isNullPtrType()) {
    if (!DestType->isVoidType() && !DestType->isBooleanType() &&
        !DestType->isPointerType() && !DestType->isNullPtrType()) {
      Self.Diag(SrcExpr.get()->getExprLoc(), diag::err_nullptr_cast)
          << /*nullptr to type*/ 0 << DestType;
      SrcExpr = ExprError();
      return;
    }
    if (!DestType->isNullPtrType()) {
      // Implicitly cast from the null pointer type to the type of the
      // destination.
      CastKind CK = DestType->isPointerType() ? CK_NullToPointer : CK_BitCast;
      SrcExpr =
          ImplicitCastExpr::Create(Self.Context, DestType, CK, SrcExpr.get(),
                                   VK_PRValue, Self.CurFPFeatureOverrides());
    }
  }
  if (DestType->isNullPtrType() && !SrcType->isNullPtrType()) {
    Self.Diag(SrcExpr.get()->getExprLoc(), diag::err_nullptr_cast)
        << /*type to nullptr*/ 1 << SrcType;
    SrcExpr = ExprError();
    return;
  }

  if (DestType->isExtVectorType()) {
    SrcExpr = Self.CheckExtVectorCast(OpRange, DestType, SrcExpr.get(), Kind);
    return;
  }

  if (DestType->getAs<MatrixType>() || SrcType->getAs<MatrixType>()) {
    if (Self.CheckMatrixCast(OpRange, DestType, SrcType, Kind))
      SrcExpr = ExprError();
    return;
  }

  if (DestType->getAs<VectorType>()) {
    if (Self.CheckVectorCast(OpRange, DestType, SrcType, Kind))
      SrcExpr = ExprError();
    return;
  }

  if (SrcType->isVectorType()) {
    if (Self.CheckVectorCast(OpRange, SrcType, DestType, Kind))
      SrcExpr = ExprError();
    return;
  }

  // The source and target types are both scalars, i.e.
  //   - arithmetic types (fundamental, enum, and complex)
  //   - all kinds of pointers

  // If either type is a pointer, the other type has to be either an
  // integer or a pointer.
  if (!DestType->isArithmeticType()) {
    if (!SrcType->isIntegralType(Self.Context) && SrcType->isArithmeticType()) {
      Self.Diag(SrcExpr.get()->getExprLoc(),
                diag::err_cast_pointer_from_non_pointer_int)
          << SrcType << SrcExpr.get()->getSourceRange();
      SrcExpr = ExprError();
      return;
    }
    checkIntToPointerCast(/* CStyle */ true, OpRange, SrcExpr.get(), DestType,
                          Self);
  } else if (!SrcType->isArithmeticType()) {
    if (!DestType->isIntegralType(Self.Context) &&
        DestType->isArithmeticType()) {
      Self.Diag(SrcExpr.get()->getBeginLoc(),
                diag::err_cast_pointer_to_non_pointer_int)
          << DestType << SrcExpr.get()->getSourceRange();
      SrcExpr = ExprError();
      return;
    }

    if ((Self.Context.getTypeSize(SrcType) >
         Self.Context.getTypeSize(DestType)) &&
        !DestType->isBooleanType()) {
      // C 6.3.2.3p6: Any pointer type may be converted to an integer type.
      // Except as previously specified, the result is implementation-defined.
      // If the result cannot be represented in the integer type, the behavior
      // is undefined. The result need not be in the range of values of any
      // integer type.
      unsigned Diag;
      if (SrcType->isVoidPointerType())
        Diag = DestType->isEnumeralType() ? diag::warn_void_pointer_to_enum_cast
                                          : diag::warn_void_pointer_to_int_cast;
      else if (DestType->isEnumeralType())
        Diag = diag::warn_pointer_to_enum_cast;
      else
        Diag = diag::warn_pointer_to_int_cast;
      Self.Diag(OpRange.getBegin(), Diag) << SrcType << DestType << OpRange;
    }
  }

  if (LLVM_LIKELY(DestType->isArithmeticType() &&
                  SrcType->isArithmeticType())) {
    Kind = Self.PrepareScalarCast(SrcExpr, DestType);
    return;
  }

  if (unsigned DiagID = checkCastFunctionType(Self, SrcExpr, DestType))
    Self.Diag(OpRange.getBegin(), DiagID) << SrcType << DestType << OpRange;

  if (isa<PointerType>(SrcType) && isa<PointerType>(DestType)) {
    QualType SrcTy = cast<PointerType>(SrcType)->getPointeeType();
    QualType DestTy = cast<PointerType>(DestType)->getPointeeType();

    const RecordDecl *SrcRD = SrcTy->getAsRecordDecl();
    const RecordDecl *DestRD = DestTy->getAsRecordDecl();
#ifndef _WIN32
    if (SrcRD && DestRD && SrcRD->hasAttr<RandomizeLayoutAttr>() &&
        SrcRD != DestRD) {
      // The struct we are casting the pointer from was randomized.
      Self.Diag(OpRange.getBegin(), diag::err_cast_from_randomized_struct)
          << SrcType << DestType;
      SrcExpr = ExprError();
      return;
    }
#endif
  }

  diagnoseCallingConvCast(Self, SrcExpr, DestType, OpRange);
  diagnoseBadFunctionCast(Self, SrcExpr, DestType);
  Kind = Self.PrepareScalarCast(SrcExpr, DestType);
  if (SrcExpr.isInvalid())
    return;

  if (Kind == CK_BitCast)
    checkCastAlign();
}

namespace {
void diagnoseCastQual(Sema &Self, const ExprResult &SrcExpr,
                      QualType DestType) {
  if (SrcExpr.isInvalid())
    return;

  QualType SrcType = SrcExpr.get()->getType();
  if (!(SrcType->isAnyPointerType() && DestType->isAnyPointerType()))
    return;

  QualType TheOffendingSrcType, TheOffendingDestType;
  Qualifiers CastAwayQualifiers;
  if (castsAwayConstness(Self, SrcType, DestType, true, false,
                         &TheOffendingSrcType, &TheOffendingDestType,
                         &CastAwayQualifiers) !=
      CastAwayConstnessKind::CACK_Similar)
    return;

  int qualifiers = -1;
  if (CastAwayQualifiers.hasConst() && CastAwayQualifiers.hasVolatile()) {
    qualifiers = 0;
  } else if (CastAwayQualifiers.hasConst()) {
    qualifiers = 1;
  } else if (CastAwayQualifiers.hasVolatile()) {
    qualifiers = 2;
  }
  if (qualifiers == -1)
    Self.Diag(SrcExpr.get()->getBeginLoc(), diag::warn_cast_qual2)
        << SrcType << DestType;
  else
    Self.Diag(SrcExpr.get()->getBeginLoc(), diag::warn_cast_qual)
        << TheOffendingSrcType << TheOffendingDestType << qualifiers;
}
} // namespace

ExprResult Sema::FormCStyleCastExpr(SourceLocation LPLoc,
                                    TypeSourceInfo *CastTypeInfo,
                                    SourceLocation RPLoc, Expr *CastExpr) {
  CastOperation Op(*this, CastTypeInfo->getType(), CastExpr);
  Op.DestRange = CastTypeInfo->getTypeLoc().getSourceRange();
  Op.OpRange = SourceRange(LPLoc, CastExpr->getEndLoc());

  {
    Op.CheckCStyleCast();
  }

  if (Op.SrcExpr.isInvalid())
    return ExprError();

  // -Wcast-qual
  diagnoseCastQual(Op.Self, Op.SrcExpr, Op.DestType);

  return Op.complete(CStyleCastExpr::Create(
      Context, Op.ResultType, Op.ValueKind, Op.Kind, Op.SrcExpr.get(),
      CurFPFeatureOverrides(), CastTypeInfo, LPLoc, RPLoc));
}
