#include "Expr/SemaExprUtils.h"
#include "Expr/TreeTransform.h"
#include "neverc/Analyze/EnterExpressionEvaluationContext.h"
#include "neverc/Analyze/Initialization.h"
#include "neverc/Analyze/ScopeInfo.h"
#include "neverc/Analyze/SemaFixItUtils.h"
#include "neverc/Analyze/SemaInternal.h"
#include "neverc/Foundation/Builtin/BuiltinString.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/Diagnostic/DiagnosticSema.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Tree/Core/TreeConsumer.h"
#include "neverc/Tree/Core/TreeMutationListener.h"
#include "neverc/Tree/Expr/EvaluatedExprVisitor.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/TypeSize.h"
#include <optional>

using namespace neverc;
using namespace sema;

namespace {
inline UnaryOperatorKind ConvertTokenKindToUnaryOpcode(tok::TokenKind Kind) {
  UnaryOperatorKind Opc;
  switch (Kind) {
  default:
    llvm_unreachable("Unknown unary op!");
  case tok::plusplus:
    Opc = UO_PreInc;
    break;
  case tok::minusminus:
    Opc = UO_PreDec;
    break;
  case tok::amp:
    Opc = UO_AddrOf;
    break;
  case tok::star:
    Opc = UO_Deref;
    break;
  case tok::plus:
    Opc = UO_Plus;
    break;
  case tok::minus:
    Opc = UO_Minus;
    break;
  case tok::tilde:
    Opc = UO_Not;
    break;
  case tok::exclaim:
    Opc = UO_LNot;
    break;
  case tok::kw___real:
    Opc = UO_Real;
    break;
  case tok::kw___imag:
    Opc = UO_Imag;
    break;
  case tok::kw___extension__:
    Opc = UO_Extension;
    break;
  }
  return Opc;
}
} // namespace


namespace {
QualType checkRealImagOperand(Sema &S, ExprResult &V, SourceLocation Loc,
                              bool IsReal) {
  if (V.get()->isTypeDependent())
    return S.Context.DependentTy;

  if (V.get()->getObjectKind() != OK_Ordinary) {
    V = S.DefaultLvalueConversion(V.get());
    if (V.isInvalid())
      return QualType();
  }

  if (const ComplexType *CT = V.get()->getType()->getAs<ComplexType>())
    return CT->getElementType();

  if (V.get()->getType()->isArithmeticType())
    return V.get()->getType();

  ExprResult PR = S.CheckPlaceholderExpr(V.get());
  if (PR.isInvalid())
    return QualType();
  if (PR.get() != V.get()) {
    V = PR;
    return checkRealImagOperand(S, V, Loc, IsReal);
  }

  S.Diag(Loc, diag::err_realimag_invalid_type)
      << V.get()->getType() << (IsReal ? "__real" : "__imag");
  return QualType();
}} // namespace

namespace {
QualType checkIncrementDecrementOperand(Sema &S, Expr *Op, ExprValueKind &VK,
                                        ExprObjectKind &OK,
                                        SourceLocation OpLoc, bool IsInc,
                                        bool IsPrefix) {
  if (Op->isTypeDependent())
    return S.Context.DependentTy;

  QualType ResType = Op->getType();
  // Atomic types can be used for increment / decrement where the non-atomic
  // versions can, so ignore the _Atomic() specifier for the purpose of
  // checking.
  if (const AtomicType *ResAtomicType = ResType->getAs<AtomicType>())
    ResType = ResAtomicType->getValueType();

  assert(!ResType.isNull() && "no type for increment/decrement expression");

  if (ResType->isRealType()) {
    // OK!
  } else if (ResType->isPointerType()) {
    // C99 6.5.2.4p2, 6.5.6p2
    if (!verifyArithmeticPointerOp(S, OpLoc, Op))
      return QualType();
  } else if (ResType->isAnyComplexType()) {
    // C99 does not support ++/-- on complex types, we allow as an extension.
    S.Diag(OpLoc, diag::ext_integer_increment_complex)
        << ResType << Op->getSourceRange();
  } else if (ResType->isPlaceholderType()) {
    ExprResult PR = S.CheckPlaceholderExpr(Op);
    if (PR.isInvalid())
      return QualType();
    return checkIncrementDecrementOperand(S, PR.get(), VK, OK, OpLoc, IsInc,
                                          IsPrefix);
  } else {
    S.Diag(OpLoc, diag::err_typecheck_illegal_increment_decrement)
        << ResType << int(IsInc) << Op->getSourceRange();
    return QualType();
  }
  // At this point, we know we have a real, complex or pointer type.
  // Now make sure the operand is a modifiable lvalue.
  if (checkForModifiableLvalue(Op, OpLoc, S))
    return QualType();
  VK = VK_PRValue;
  return ResType.getUnqualifiedType();
}
} // namespace


namespace {
bool isOverflowingIntegerType(TreeContext &Ctx, QualType T) {
  if (T.isNull())
    return false;

  if (!Ctx.isPromotableIntegerType(T))
    return true;

  return Ctx.getIntWidth(T) >= Ctx.getIntWidth(Ctx.IntTy);
}
} // namespace

// ===----------------------------------------------------------------------===
// Unary operators & statement expressions
// ===----------------------------------------------------------------------===

ExprResult Sema::CreateBuiltinUnaryOp(SourceLocation OpLoc,
                                      UnaryOperatorKind Opc, Expr *InputExpr,
                                      bool IsAfterAmp) {
  ExprResult Input = InputExpr;
  ExprValueKind VK = VK_PRValue;
  ExprObjectKind OK = OK_Ordinary;
  QualType resultType;
  bool CanOverflow = false;

  bool ConvertHalfVec = false;

  switch (Opc) {
  case UO_PreInc:
  case UO_PreDec:
  case UO_PostInc:
  case UO_PostDec:
    resultType =
        checkIncrementDecrementOperand(*this, Input.get(), VK, OK, OpLoc,
                                       Opc == UO_PreInc || Opc == UO_PostInc,
                                       Opc == UO_PreInc || Opc == UO_PreDec);
    CanOverflow = isOverflowingIntegerType(Context, resultType);
    break;
  case UO_AddrOf:
    resultType = CheckAddressOfOperand(Input, OpLoc);
    CheckAddressOfNoDeref(InputExpr);
    noteModifiableNonNullParam(*this, InputExpr);
    break;
  case UO_Deref: {
    Input = DefaultFunctionArrayLvalueConversion(Input.get());
    if (Input.isInvalid())
      return ExprError();
    resultType =
        checkIndirectionOperand(*this, Input.get(), VK, OpLoc, IsAfterAmp);
    break;
  }
  case UO_Plus:
  case UO_Minus:
    CanOverflow = Opc == UO_Minus &&
                  isOverflowingIntegerType(Context, Input.get()->getType());
    Input = UsualUnaryConversions(Input.get());
    if (Input.isInvalid())
      return ExprError();
    // Unary plus and minus require promoting an operand of half vector to a
    // float vector and truncating the result back to a half vector. For now, we
    // do this only when HalfArgsAndReturns is set (that is, when the target is
    // arm or arm64).
    ConvertHalfVec = requiresHalfVecConversion(true, Context, Input.get());

    // If the operand is a half vector, promote it to a float vector.
    if (ConvertHalfVec)
      Input = castVectorElement(Input.get(), Context.FloatTy, *this);
    resultType = Input.get()->getType();
    if (resultType->isArithmeticType()) // C99 6.5.3.3p1
      break;
    else if (resultType->isVectorType())
      break;
    else if (resultType->isSveVLSBuiltinType())
      break;

    return ExprError(Diag(OpLoc, diag::err_typecheck_unary_expr)
                     << resultType << Input.get()->getSourceRange());

  case UO_Not: // bitwise complement
    Input = UsualUnaryConversions(Input.get());
    if (Input.isInvalid())
      return ExprError();
    resultType = Input.get()->getType();
    // C99 6.5.3.3p1. We allow complex int and float as a GCC extension.
    if (resultType->isComplexType() || resultType->isComplexIntegerType())
      // C99 does not support '~' for complex conjugation.
      Diag(OpLoc, diag::ext_integer_complement_complex)
          << resultType << Input.get()->getSourceRange();
    else if (resultType->hasIntegerRepresentation())
      break;
    else {
      return ExprError(Diag(OpLoc, diag::err_typecheck_unary_expr)
                       << resultType << Input.get()->getSourceRange());
    }
    break;

  case UO_LNot: // logical negation
    // Unlike +/-/~, integer promotions aren't done here (C99 6.5.3.3p5).
    Input = DefaultFunctionArrayLvalueConversion(Input.get());
    if (Input.isInvalid())
      return ExprError();
    if (Input.get()->containsErrors()) {
      resultType = Context.IntTy;
      break;
    }
    resultType = Input.get()->getType();

    // Though we still have to promote half FP to float...
    if (resultType->isHalfType() && !Context.getLangOpts().NativeHalfType) {
      Input = ImpCastExprToType(Input.get(), Context.FloatTy, CK_FloatingCast)
                  .get();
      resultType = Context.FloatTy;
    }

    if (resultType->isScalarType()) {
    } else if (resultType->isExtVectorType()) {
      // Vector logical not returns the signed variant of the operand type.
      resultType = GetSignedVectorType(resultType);
      break;
    } else {
      return ExprError(Diag(OpLoc, diag::err_typecheck_unary_expr)
                       << resultType << Input.get()->getSourceRange());
    }

    // Logical not has type `int` in C (C99 6.5.3.3p5).
    resultType = Context.getLogicalOperationType();
    break;
  case UO_Real:
  case UO_Imag:
    resultType = checkRealImagOperand(*this, Input, OpLoc, Opc == UO_Real);
    // _Real maps ordinary l-values into ordinary l-values. _Imag maps ordinary
    // complex l-values to ordinary l-values and all other values to r-values.
    if (Input.isInvalid())
      return ExprError();
    if (Opc == UO_Real || Input.get()->getType()->isAnyComplexType()) {
      if (Input.get()->isLValue() &&
          Input.get()->getObjectKind() == OK_Ordinary)
        VK = Input.get()->getValueKind();
    } else {
      Input = DefaultLvalueConversion(Input.get());
    }
    break;
  case UO_Extension:
    resultType = Input.get()->getType();
    VK = Input.get()->getValueKind();
    OK = Input.get()->getObjectKind();
    break;
  }
  if (resultType.isNull() || Input.isInvalid())
    return ExprError();

  // Check for array bounds violations in the operand of the UnaryOperator,
  // except for the '*' and '&' operators that have to be handled specially
  // by CheckArrayAccess (as there are special cases like &array[arraysize]
  // that are explicitly defined as valid by the standard).
  if (Opc != UO_AddrOf && Opc != UO_Deref) {
    auto SC = Input.get()->getStmtClass();
    if (LLVM_UNLIKELY(SC != Stmt::DeclRefExprClass &&
                      SC != Stmt::IntegerLiteralClass &&
                      SC != Stmt::BinaryOperatorClass &&
                      SC != Stmt::CompoundAssignOperatorClass))
      CheckArrayAccess(Input.get());
  }

  auto *UO =
      UnaryOperator::Create(Context, Input.get(), Opc, resultType, VK, OK,
                            OpLoc, CanOverflow, CurFPFeatureOverrides());

  if (Opc == UO_Deref && UO->getType()->hasAttr(attr::NoDeref) &&
      !isa<ArrayType>(UO->getType().getDesugaredType(Context)) &&
      !isUnevaluatedContext())
    ExprEvalContexts.back().PossibleDerefs.insert(UO);

  if (ConvertHalfVec)
    return castVectorElement(UO, Context.HalfTy, *this);
  return UO;
}

ExprResult Sema::FormUnaryOp(Scope *S, SourceLocation OpLoc,
                             UnaryOperatorKind Opc, Expr *Input,
                             bool IsAfterAmp) {
  if (Input->getType()->getAsPlaceholderType()) {
    // extension is always a builtin operator.
    if (Opc == UO_Extension)
      return CreateBuiltinUnaryOp(OpLoc, Opc, Input);

    // Anything else needs to be handled now.
    ExprResult Result = CheckPlaceholderExpr(Input);
    if (Result.isInvalid())
      return ExprError();
    Input = Result.get();
  }

  return CreateBuiltinUnaryOp(OpLoc, Opc, Input, IsAfterAmp);
}

// Unary Operators.  'Tok' is the token for the operator.
ExprResult Sema::OnUnaryOp(Scope *S, SourceLocation OpLoc, tok::TokenKind Op,
                           Expr *Input, bool IsAfterAmp) {
  return FormUnaryOp(S, OpLoc, ConvertTokenKindToUnaryOpcode(Op), Input,
                     IsAfterAmp);
}

ExprResult Sema::OnPostfixUnaryOp(Scope *S, SourceLocation OpLoc,
                                  tok::TokenKind Kind, Expr *Input) {
  UnaryOperatorKind Opc;
  switch (Kind) {
  default:
    llvm_unreachable("Unknown unary op!");
  case tok::plusplus:
    Opc = UO_PostInc;
    break;
  case tok::minusminus:
    Opc = UO_PostDec;
    break;
  }

  // Since this might is a postfix expression, get rid of ParenListExprs.
  ExprResult Result = MaybeConvertParenListExprToParenExpr(S, Input);
  if (Result.isInvalid())
    return ExprError();
  Input = Result.get();

  return FormUnaryOp(S, OpLoc, Opc, Input);
}


ExprResult Sema::OnAddrLabel(SourceLocation OpLoc, SourceLocation LabLoc,
                             LabelDecl *TheDecl) {
  TheDecl->markUsed(Context);
  auto *Res = new (Context) AddrLabelExpr(
      OpLoc, LabLoc, TheDecl, Context.getPointerType(Context.VoidTy));

  if (getCurFunction())
    getCurFunction()->AddrLabels.push_back(Res);

  return Res;
}

void Sema::OnStartStmtExpr() {
  PushExpressionEvaluationContext(ExprEvalContexts.back().Context);
  // Make sure we diagnose jumping into a statement expression.
  setFunctionHasBranchProtectedScope();
}

void Sema::OnStmtExprError() {
  // Note that function is also called by TreeTransform when leaving a
  // StmtExpr scope without rebuilding anything.

  DiscardCleanupsInEvaluationContext();
  PopExpressionEvaluationContext();
}

ExprResult Sema::OnStmtExpr(Scope *S, SourceLocation LPLoc, Stmt *SubStmt,
                            SourceLocation RPLoc) {
  return FormStmtExpr(LPLoc, SubStmt, RPLoc);
}

ExprResult Sema::FormStmtExpr(SourceLocation LPLoc, Stmt *SubStmt,
                              SourceLocation RPLoc) {
  assert(SubStmt && isa<CompoundStmt>(SubStmt) && "Invalid action invocation!");
  CompoundStmt *Compound = cast<CompoundStmt>(SubStmt);

  if (hasAnyUnrecoverableErrorsInThisFunction())
    DiscardCleanupsInEvaluationContext();
  assert(!Cleanup.exprNeedsCleanups() &&
         "cleanups within StmtExpr not correctly bound!");
  PopExpressionEvaluationContext();

  // More semantic analysis is needed.

  // If there are sub-stmts in the compound stmt, take the type of the last one
  // as the type of the stmtexpr.
  QualType Ty = Context.VoidTy;
  bool StmtExprMayBindToTemp = false;
  if (!Compound->body_empty()) {
    // For GCC compatibility we get the last Stmt excluding trailing NullStmts.
    if (const auto *LastStmt =
            dyn_cast<ValueStmt>(Compound->getStmtExprResult())) {
      if (const Expr *Value = LastStmt->getExprStmt()) {
        StmtExprMayBindToTemp = true;
        Ty = Value->getType();
      }
    }
  }

  Expr *ResStmtExpr = new (Context) StmtExpr(Compound, Ty, LPLoc, RPLoc);
  if (StmtExprMayBindToTemp)
    return MaybeBindToTemporary(ResStmtExpr);
  return ResStmtExpr;
}

ExprResult Sema::OnStmtExprResult(ExprResult ER) {
  if (ER.isInvalid())
    return ExprError();

  // Do function/array conversion on the last expression, but not
  // lvalue-to-rvalue.  However, initialize an unqualified type.
  ER = DefaultFunctionArrayConversion(ER.get());
  if (ER.isInvalid())
    return ExprError();
  Expr *E = ER.get();

  return PerformCopyInitialization(
      InitializedEntity::InitializeStmtExprResult(
          E->getBeginLoc(), E->getType().getUnqualifiedType()),
      SourceLocation(), E);
}

ExprResult
Sema::FormBuiltinOffsetOf(SourceLocation BuiltinLoc, TypeSourceInfo *TInfo,
                          llvm::ArrayRef<OffsetOfComponent> Components,
                          SourceLocation RParenLoc) {
  QualType ArgTy = TInfo->getType();
  SourceRange TypeRange = TInfo->getTypeLoc().getLocalSourceRange();

  // We must have at least one component that refers to the type, and the first
  // one is known to be a field designator.  Verify that the ArgTy represents
  // a struct/union/class.
  if (!ArgTy->isRecordType())
    return ExprError(Diag(BuiltinLoc, diag::err_offsetof_record_type)
                     << ArgTy << TypeRange);

  // Type must be complete per C99 7.17p3 because a declaring a variable
  // with an incomplete type would be ill-formed.
  if (RequireCompleteType(BuiltinLoc, ArgTy, diag::err_offsetof_incomplete_type,
                          TypeRange))
    return ExprError();

  QualType CurrentType = ArgTy;
  llvm::SmallVector<OffsetOfNode, 4> Comps;
  llvm::SmallVector<Expr *, 4> Exprs;
  for (const OffsetOfComponent &OC : Components) {
    if (OC.isBrackets) {
      // Offset of an array sub-field.
      {
        const ArrayType *AT = Context.getAsArrayType(CurrentType);
        if (!AT)
          return ExprError(Diag(OC.LocEnd, diag::err_offsetof_array_type)
                           << CurrentType);
        CurrentType = AT->getElementType();
      }

      ExprResult IdxRval = DefaultLvalueConversion(static_cast<Expr *>(OC.U.E));
      if (IdxRval.isInvalid())
        return ExprError();
      Expr *Idx = IdxRval.get();

      // The expression must be an integral expression.
      if (!Idx->getType()->isIntegerType())
        return ExprError(
            Diag(Idx->getBeginLoc(), diag::err_typecheck_subscript_not_integer)
            << Idx->getSourceRange());

      // Record this array index.
      Comps.push_back(OffsetOfNode(OC.LocStart, Exprs.size(), OC.LocEnd));
      Exprs.push_back(Idx);
      continue;
    }

    // Offset of a field.
    // We need to have a complete type to look into.
    if (RequireCompleteType(OC.LocStart, CurrentType,
                            diag::err_offsetof_incomplete_type))
      return ExprError();

    // Look for the designated field.
    const RecordType *RC = CurrentType->getAs<RecordType>();
    if (!RC)
      return ExprError(Diag(OC.LocEnd, diag::err_offsetof_record_type)
                       << CurrentType);
    RecordDecl *RD = RC->getDecl();

    // Look for the field.
    LookupResult R(*this, OC.U.IdentInfo, OC.LocStart, ResolveMember);
    LookupQualifiedName(R, RD);
    FieldDecl *MemberDecl = R.getAsSingle<FieldDecl>();
    IndirectFieldDecl *IndirectMemberDecl = nullptr;
    if (!MemberDecl) {
      if ((IndirectMemberDecl = R.getAsSingle<IndirectFieldDecl>()))
        MemberDecl = IndirectMemberDecl->getAnonField();
    }

    if (!MemberDecl) {
      // Lookup could be ambiguous when looking up a placeholder variable
      // __builtin_offsetof(S, _).
      // In that case we would already have emitted a diagnostic
      if (!R.isAmbiguous())
        Diag(BuiltinLoc, diag::err_no_member)
            << OC.U.IdentInfo << RD << SourceRange(OC.LocStart, OC.LocEnd);
      return ExprError();
    }

    // C99 7.17p3:
    //   (If the specified member is a bit-field, the behavior is undefined.)
    //
    // We diagnose this as an error.
    if (MemberDecl->isBitField()) {
      Diag(OC.LocEnd, diag::err_offsetof_bitfield)
          << MemberDecl->getDeclName() << SourceRange(BuiltinLoc, RParenLoc);
      Diag(MemberDecl->getLocation(), diag::note_bitfield_decl);
      return ExprError();
    }

    if (IndirectMemberDecl) {
      for (auto *FI : IndirectMemberDecl->chain()) {
        assert(isa<FieldDecl>(FI));
        Comps.push_back(
            OffsetOfNode(OC.LocStart, cast<FieldDecl>(FI), OC.LocEnd));
      }
    } else
      Comps.push_back(OffsetOfNode(OC.LocStart, MemberDecl, OC.LocEnd));

    CurrentType = MemberDecl->getType();
  }

  return OffsetOfExpr::Create(Context, Context.getSizeType(), BuiltinLoc, TInfo,
                              Comps, Exprs, RParenLoc);
}

ExprResult Sema::OnBuiltinOffsetOf(Scope *S, SourceLocation BuiltinLoc,
                                   SourceLocation TypeLoc,
                                   ParsedType ParsedArgTy,
                                   llvm::ArrayRef<OffsetOfComponent> Components,
                                   SourceLocation RParenLoc) {

  TypeSourceInfo *ArgTInfo;
  QualType ArgTy = GetTypeFromParser(ParsedArgTy, &ArgTInfo);
  if (ArgTy.isNull())
    return ExprError();

  if (!ArgTInfo)
    ArgTInfo = Context.getTrivialTypeSourceInfo(ArgTy, TypeLoc);

  return FormBuiltinOffsetOf(BuiltinLoc, ArgTInfo, Components, RParenLoc);
}

ExprResult Sema::OnChooseExpr(SourceLocation BuiltinLoc, Expr *CondExpr,
                              Expr *LHSExpr, Expr *RHSExpr,
                              SourceLocation RPLoc) {
  assert((CondExpr && LHSExpr && RHSExpr) && "Missing type argument(s)");

  ExprValueKind VK = VK_PRValue;
  ExprObjectKind OK = OK_Ordinary;
  QualType resType;
  bool CondIsTrue = false;
  // The conditional expression is required to be a constant expression.
  llvm::APSInt condEval(32);
  ExprResult CondICE = VerifyIntegerConstantExpression(
      CondExpr, &condEval, diag::err_typecheck_choose_expr_requires_constant);
  if (CondICE.isInvalid())
    return ExprError();
  CondExpr = CondICE.get();
  CondIsTrue = condEval.getZExtValue();

  // If the condition is > zero, then the AST type is the same as the LHSExpr.
  Expr *ActiveExpr = CondIsTrue ? LHSExpr : RHSExpr;

  resType = ActiveExpr->getType();
  VK = ActiveExpr->getValueKind();
  OK = ActiveExpr->getObjectKind();

  return new (Context) ChooseExpr(BuiltinLoc, CondExpr, LHSExpr, RHSExpr,
                                  resType, VK, OK, RPLoc, CondIsTrue);
}

ExprResult Sema::OnVAArg(SourceLocation BuiltinLoc, Expr *E, ParsedType Ty,
                         SourceLocation RPLoc) {
  TypeSourceInfo *TInfo;
  GetTypeFromParser(Ty, &TInfo);
  return FormVAArgExpr(BuiltinLoc, E, TInfo, RPLoc);
}

ExprResult Sema::FormVAArgExpr(SourceLocation BuiltinLoc, Expr *E,
                               TypeSourceInfo *TInfo, SourceLocation RPLoc) {
  Expr *OrigExpr = E;
  bool IsMS = false;

  // It might be a __builtin_ms_va_list. (But don't ever mark a va_arg()
  // as Microsoft ABI on an actual Microsoft platform, where
  // __builtin_ms_va_list and __builtin_va_list are the same.)
  if (Context.getTargetInfo().hasBuiltinMSVaList() &&
      Context.getTargetInfo().getBuiltinVaListKind() !=
          TargetInfo::CharPtrBuiltinVaList) {
    QualType MSVaListType = Context.getBuiltinMSVaListType();
    if (Context.hasSameType(MSVaListType, E->getType())) {
      if (checkForModifiableLvalue(E, BuiltinLoc, *this))
        return ExprError();
      IsMS = true;
    }
  }
  QualType VaListType = Context.getBuiltinVaListType();
  if (!IsMS) {
    if (VaListType->isArrayType()) {
      // Deal with implicit array decay; for example, on x86-64,
      // va_list is an array, but it's supposed to decay to
      // a pointer for va_arg.
      VaListType = Context.getArrayDecayedType(VaListType);
      // Make sure the input expression also decays appropriately.
      ExprResult Result = UsualUnaryConversions(E);
      if (Result.isInvalid())
        return ExprError();
      E = Result.get();
    } else {
      // Otherwise, the va_list argument must be an l-value because
      // it is modified by va_arg.
      if (checkForModifiableLvalue(E, BuiltinLoc, *this))
        return ExprError();
    }
  }

  if (!IsMS && !Context.hasSameType(VaListType, E->getType()))
    return ExprError(
        Diag(E->getBeginLoc(),
             diag::err_first_argument_to_va_arg_not_of_type_va_list)
        << OrigExpr->getType() << E->getSourceRange());

  if (RequireCompleteType(TInfo->getTypeLoc().getBeginLoc(), TInfo->getType(),
                          diag::err_second_parameter_to_va_arg_incomplete,
                          TInfo->getTypeLoc()))
    return ExprError();

  if (!TInfo->getType().isPODType(Context)) {
    Diag(TInfo->getTypeLoc().getBeginLoc(),
         diag::warn_second_parameter_to_va_arg_not_pod)
        << TInfo->getType() << TInfo->getTypeLoc().getSourceRange();
  }

  // Check for va_arg where arguments of the given type will be promoted
  // (i.e. this va_arg is guaranteed to have undefined behavior).
  {
    QualType PromoteType;
    if (Context.isPromotableIntegerType(TInfo->getType())) {
      PromoteType = Context.getPromotedIntegerType(TInfo->getType());
      // C23 7.16.1.1p2 says, in part:
      //   If type is not compatible with the type of the actual next argument
      //   (as promoted according to the default argument promotions), the
      //   behavior is undefined, except for the following cases:
      //     - both types are pointers to qualified or unqualified versions of
      //       compatible types;
      //     - one type is compatible with a signed integer type, the other
      //       type is compatible with the corresponding unsigned integer type,
      //       and the value is representable in both types;
      //     - one type is pointer to qualified or unqualified void and the
      //       other is a pointer to a qualified or unqualified character type;
      //     - or, the type of the next argument is nullptr_t and type is a
      //       pointer type that has the same representation and alignment
      //       requirements as a pointer to a character type.
      // typesAreCompatible() must compare the promoted type to what was passed
      // on the variadic call; use the enumeration's underlying integer type so
      // the check matches the promoted value, not the enumerated type alone.
      QualType UnderlyingType = TInfo->getType();
      if (const auto *ET = UnderlyingType->getAs<EnumType>())
        UnderlyingType = ET->getDecl()->getIntegerType();
      if (Context.typesAreCompatible(PromoteType, UnderlyingType,
                                     /*CompareUnqualified*/ true))
        PromoteType = QualType();

      // If the types are still not compatible, we need to test whether the
      // promoted type and the underlying type are the same except for
      // signedness. Ask the AST for the correctly corresponding type and see
      // if that's compatible.
      if (!PromoteType.isNull() && !UnderlyingType->isBooleanType() &&
          PromoteType->isUnsignedIntegerType() !=
              UnderlyingType->isUnsignedIntegerType()) {
        UnderlyingType =
            UnderlyingType->isUnsignedIntegerType()
                ? Context.getCorrespondingSignedType(UnderlyingType)
                : Context.getCorrespondingUnsignedType(UnderlyingType);
        if (Context.typesAreCompatible(PromoteType, UnderlyingType,
                                       /*CompareUnqualified*/ true))
          PromoteType = QualType();
      }
    }
    if (TInfo->getType()->isSpecificBuiltinType(BuiltinType::Float))
      PromoteType = Context.DoubleTy;
    if (!PromoteType.isNull())
      DiagRuntimeBehavior(
          TInfo->getTypeLoc().getBeginLoc(), E,
          PDiag(diag::warn_second_parameter_to_va_arg_never_compatible)
              << TInfo->getType() << PromoteType
              << TInfo->getTypeLoc().getSourceRange());
  }

  QualType T = TInfo->getType().getNonLValueExprType(Context);
  return new (Context) VAArgExpr(BuiltinLoc, E, TInfo, RPLoc, T, IsMS);
}

ExprResult Sema::OnSourceLocExpr(SourceLocIdentKind Kind,
                                 SourceLocation BuiltinLoc,
                                 SourceLocation RPLoc) {
  QualType ResultTy;
  switch (Kind) {
  case SourceLocIdentKind::File:
  case SourceLocIdentKind::FileName:
  case SourceLocIdentKind::Function:
  case SourceLocIdentKind::FuncSig: {
    QualType ArrTy = Context.getStringLiteralArrayType(Context.CharTy, 0);
    ResultTy =
        Context.getPointerType(ArrTy->getAsArrayTypeUnsafe()->getElementType());
    break;
  }
  case SourceLocIdentKind::Line:
  case SourceLocIdentKind::Column:
    ResultTy = Context.UnsignedIntTy;
    break;
  }

  return FormSourceLocExpr(Kind, ResultTy, BuiltinLoc, RPLoc, CurContext);
}

ExprResult Sema::FormSourceLocExpr(SourceLocIdentKind Kind, QualType ResultTy,
                                   SourceLocation BuiltinLoc,
                                   SourceLocation RPLoc,
                                   DeclContext *ParentContext) {
  return new (Context)
      SourceLocExpr(Context, Kind, ResultTy, BuiltinLoc, RPLoc, ParentContext);
}

