#include "TreeTransform.h"
#include "neverc/Analyze/Initialization.h"
#include "neverc/Analyze/SemaInternal.h"
#include "neverc/Foundation/Builtin/BuiltinString.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/Diagnostic/DiagnosticSema.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Tree/Core/TreeConsumer.h"
#include "neverc/Tree/Core/TreeMutationListener.h"
#include "neverc/Tree/Expr/EvaluatedExprVisitor.h"
#include "neverc/Analyze/EnterExpressionEvaluationContext.h"
#include "neverc/Analyze/ScopeInfo.h"
#include "neverc/Analyze/SemaFixItUtils.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/TypeSize.h"
#include <optional>

using namespace neverc;
using namespace sema;

namespace neverc {
template <class Derived>
class ReferencedDeclWalker : public EvaluatedExprVisitor<Derived> {
protected:
  Sema &S;

public:
  typedef EvaluatedExprVisitor<Derived> Inherited;

  ReferencedDeclWalker(Sema &S) : Inherited(S.Context), S(S) {}

  Derived &asImpl() { return *static_cast<Derived *>(this); }

  void VisitDeclRefExpr(DeclRefExpr *E) {
    auto *D = E->getDecl();
    if (isa<FunctionDecl>(D) || isa<VarDecl>(D)) {
      asImpl().visitUsedDecl(E->getLocation(), D);
    }
  }

  void VisitMemberExpr(MemberExpr *E) {
    auto *D = E->getMemberDecl();
    if (isa<FunctionDecl>(D) || isa<VarDecl>(D)) {
      asImpl().visitUsedDecl(E->getMemberLoc(), D);
    }
    asImpl().Visit(E->getBase());
  }

  void VisitInitListExpr(InitListExpr *ILE) {
    if (ILE->hasArrayFiller())
      asImpl().Visit(ILE->getArrayFiller());
    Inherited::VisitInitListExpr(ILE);
  }

  void visitUsedDecl(SourceLocation Loc, Decl *D) {}
};
} // namespace neverc

// Defined in SemaExprBinOp.cpp.
namespace neverc {
ExprResult castVectorElement(Expr *E, QualType ElementType, Sema &S);
bool verifyArithmeticPointerOp(Sema &S, SourceLocation Loc, Expr *Operand);
bool checkForModifiableLvalue(Expr *E, SourceLocation Loc, Sema &S);
void noteModifiableNonNullParam(Sema &S, const Expr *Exp);
QualType checkIndirectionOperand(Sema &S, Expr *Op, ExprValueKind &VK,
                                 SourceLocation OpLoc, bool IsAfterAmp = false);
bool requiresHalfVecConversion(bool OpRequiresConversion, TreeContext &Ctx,
                               Expr *E0, Expr *E1 = nullptr);
} // namespace neverc

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

namespace {
bool maybeDiagnoseAssignmentToFunction(Sema &S, QualType DstType,
                                       const Expr *SrcExpr) {
  if (!DstType->isFunctionPointerType() ||
      !SrcExpr->getType()->isFunctionType())
    return false;

  auto *DRE = dyn_cast<DeclRefExpr>(SrcExpr->IgnoreParenImpCasts());
  if (!DRE)
    return false;

  auto *FD = dyn_cast<FunctionDecl>(DRE->getDecl());
  if (!FD)
    return false;

  return !S.checkAddressOfFunctionIsAvailable(FD,
                                              /*Complain=*/true,
                                              SrcExpr->getBeginLoc());
}
} // namespace

bool Sema::DiagnoseAssignmentResult(AssignConvertType ConvTy,
                                    SourceLocation Loc, QualType DstType,
                                    QualType SrcType, Expr *SrcExpr,
                                    AssignmentAction Action, bool *Complained) {
  if (Complained)
    *Complained = false;

  if (SrcExpr && SrcExpr->getType()->isDependentType())
    return ConvTy != Compatible;

  // Decode the result (notice that AST's are still created for extensions).
  bool isInvalid = false;
  unsigned DiagKind = 0;
  ConversionFixItGenerator ConvHints;
  bool MayHaveConvFixit = false;
  bool MayHaveFunctionDiff = false;

  switch (ConvTy) {
  case Compatible:
    DiagnoseAssignmentEnum(DstType, SrcType, SrcExpr);
    return false;

  case PointerToInt:
    DiagKind = diag::ext_typecheck_convert_pointer_int;
    ConvHints.tryToFixConversion(SrcExpr, SrcType, DstType, *this);
    MayHaveConvFixit = true;
    break;
  case IntToPointer:
    DiagKind = diag::ext_typecheck_convert_int_pointer;
    ConvHints.tryToFixConversion(SrcExpr, SrcType, DstType, *this);
    MayHaveConvFixit = true;
    break;
  case IncompatibleFunctionPointerStrict:
    DiagKind =
        diag::warn_typecheck_convert_incompatible_function_pointer_strict;
    ConvHints.tryToFixConversion(SrcExpr, SrcType, DstType, *this);
    MayHaveConvFixit = true;
    break;
  case IncompatibleFunctionPointer:
    DiagKind = diag::ext_typecheck_convert_incompatible_function_pointer;
    ConvHints.tryToFixConversion(SrcExpr, SrcType, DstType, *this);
    MayHaveConvFixit = true;
    break;
  case IncompatiblePointer:
    DiagKind = diag::ext_typecheck_convert_incompatible_pointer;
    ConvHints.tryToFixConversion(SrcExpr, SrcType, DstType, *this);
    MayHaveConvFixit = true;
    break;
  case IncompatiblePointerSign:
    DiagKind = diag::ext_typecheck_convert_incompatible_pointer_sign;
    break;
  case FunctionVoidPointer:
    DiagKind = diag::ext_typecheck_convert_pointer_void_func;
    break;
  case IncompatiblePointerDiscardsQualifiers: {
    // Perform array-to-pointer decay if necessary.
    if (SrcType->isArrayType())
      SrcType = Context.getArrayDecayedType(SrcType);

    isInvalid = true;

    Qualifiers lhq = SrcType->getPointeeType().getQualifiers();
    Qualifiers rhq = DstType->getPointeeType().getQualifiers();
    if (lhq.getAddressSpace() != rhq.getAddressSpace()) {
      DiagKind = diag::err_typecheck_incompatible_address_space;
      break;
    }

    llvm_unreachable("unknown error case for discarding qualifiers!");
    // fallthrough
  }
  case CompatiblePointerDiscardsQualifiers:
    // If the qualifiers lost were because we were applying the
    DiagKind = diag::ext_typecheck_convert_discards_qualifiers;
    break;
  case IncompatibleNestedPointerQualifiers:
    DiagKind = diag::ext_nested_pointer_qualifier_mismatch;
    break;
  case IncompatibleNestedPointerAddressSpaceMismatch:
    DiagKind = diag::err_typecheck_incompatible_nested_address_space;
    isInvalid = true;
    break;
  case IncompatibleVectors:
    DiagKind = diag::warn_incompatible_vectors;
    break;
  case Incompatible:
    if (maybeDiagnoseAssignmentToFunction(*this, DstType, SrcExpr)) {
      if (Complained)
        *Complained = true;
      return true;
    }

    DiagKind = diag::err_typecheck_convert_incompatible;
    ConvHints.tryToFixConversion(SrcExpr, SrcType, DstType, *this);
    MayHaveConvFixit = true;
    isInvalid = true;
    MayHaveFunctionDiff = true;
    break;
  }

  QualType FirstType, SecondType;
  switch (Action) {
  case AA_Assigning:
  case AA_Initializing:
    // The destination type comes first.
    FirstType = DstType;
    SecondType = SrcType;
    break;

  case AA_Returning:
  case AA_Passing:
  case AA_Converting:
  case AA_Sending:
  case AA_Casting:
    FirstType = SrcType;
    SecondType = DstType;
    break;
  }

  PartialDiagnostic FDiag = PDiag(DiagKind);

  FDiag << FirstType << SecondType << Action << SrcExpr->getSourceRange();

  if (DiagKind == diag::ext_typecheck_convert_incompatible_pointer_sign ||
      DiagKind == diag::err_typecheck_convert_incompatible_pointer_sign) {
    auto isPlainChar = [](const neverc::Type *Type) {
      return Type->isSpecificBuiltinType(BuiltinType::Char_S) ||
             Type->isSpecificBuiltinType(BuiltinType::Char_U);
    };
    FDiag << (isPlainChar(FirstType->getPointeeOrArrayElementType()) ||
              isPlainChar(SecondType->getPointeeOrArrayElementType()));
  }

  // If we can fix the conversion, suggest the FixIts.
  if (!ConvHints.isNull()) {
    for (FixItHint &H : ConvHints.Hints)
      FDiag << H;
  }

  if (MayHaveConvFixit) {
    FDiag << (unsigned)(ConvHints.Kind);
  }

  if (MayHaveFunctionDiff)
    DiagnoseFunctionTypeMismatch(FDiag, SecondType, FirstType);

  Diag(Loc, FDiag);

  if (Complained)
    *Complained = true;
  return isInvalid;
}

ExprResult Sema::VerifyIntegerConstantExpression(Expr *E, llvm::APSInt *Result,
                                                 AllowFoldKind CanFold) {
  class SimpleICEDiagnoser : public VerifyICEDiagnoser {
  public:
    SemaDiagnosticBuilder diagnoseNotICEType(Sema &S, SourceLocation Loc,
                                             QualType T) override {
      return S.Diag(Loc, diag::err_ice_not_integral) << T << /*C=*/false;
    }
    SemaDiagnosticBuilder diagnoseNotICE(Sema &S, SourceLocation Loc) override {
      return S.Diag(Loc, diag::err_expr_not_ice) << /*C=*/false;
    }
  } Diagnoser;

  return VerifyIntegerConstantExpression(E, Result, Diagnoser, CanFold);
}

ExprResult Sema::VerifyIntegerConstantExpression(Expr *E, llvm::APSInt *Result,
                                                 unsigned DiagID,
                                                 AllowFoldKind CanFold) {
  class IDDiagnoser : public VerifyICEDiagnoser {
    unsigned DiagID;

  public:
    IDDiagnoser(unsigned DiagID)
        : VerifyICEDiagnoser(DiagID == 0), DiagID(DiagID) {}

    SemaDiagnosticBuilder diagnoseNotICE(Sema &S, SourceLocation Loc) override {
      return S.Diag(Loc, DiagID);
    }
  } Diagnoser(DiagID);

  return VerifyIntegerConstantExpression(E, Result, Diagnoser, CanFold);
}

Sema::SemaDiagnosticBuilder
Sema::VerifyICEDiagnoser::diagnoseNotICEType(Sema &S, SourceLocation Loc,
                                             QualType T) {
  return diagnoseNotICE(S, Loc);
}

Sema::SemaDiagnosticBuilder
Sema::VerifyICEDiagnoser::diagnoseFold(Sema &S, SourceLocation Loc) {
  return S.Diag(Loc, diag::ext_expr_not_ice) << /*C=*/false;
}

ExprResult Sema::VerifyIntegerConstantExpression(Expr *E, llvm::APSInt *Result,
                                                 VerifyICEDiagnoser &Diagnoser,
                                                 AllowFoldKind CanFold) {
  SourceLocation DiagLoc = E->getBeginLoc();

  if (!E->getType()->isIntegralOrUnscopedEnumerationType()) {
    if (!Diagnoser.Suppress)
      Diagnoser.diagnoseNotICEType(*this, DiagLoc, E->getType())
          << E->getSourceRange();
    return ExprError();
  }

  ExprResult RValueExpr = DefaultLvalueConversion(E);
  if (RValueExpr.isInvalid())
    return ExprError();

  E = RValueExpr.get();

  if (E->isIntegerConstantExpr(Context)) {
    if (Result)
      *Result = E->EvaluateKnownConstIntCheckOverflow(Context);
    if (!isa<ConstantExpr>(E))
      E = Result ? ConstantExpr::Create(Context, E, APValue(*Result))
                 : ConstantExpr::Create(Context, E);
    return E;
  }

  Expr::EvalResult EvalResult;
  llvm::SmallVector<PartialDiagnosticAt, 8> Notes;
  EvalResult.Diag = &Notes;

  // Try to evaluate the expression, and produce diagnostics explaining why it's
  // not a constant expression as a side-effect.
  bool Folded =
      E->EvaluateAsRValue(EvalResult, Context, /*isConstantContext*/ true) &&
      EvalResult.Val.isInt() && !EvalResult.HasSideEffects;

  if (!isa<ConstantExpr>(E))
    E = ConstantExpr::Create(Context, E, EvalResult.Val);

  // If our only note is the usual "invalid subexpression" note, just point
  // the caret at its location rather than producing an essentially
  // redundant note.
  if (Notes.size() == 1 &&
      Notes[0].second.getDiagID() == diag::note_invalid_subexpr_in_const_expr) {
    DiagLoc = Notes[0].first;
    Notes.clear();
  }

  if (!Folded || !CanFold) {
    if (!Diagnoser.Suppress) {
      Diagnoser.diagnoseNotICE(*this, DiagLoc) << E->getSourceRange();
      for (const PartialDiagnosticAt &Note : Notes)
        Diag(Note.first, Note.second);
    }

    return ExprError();
  }

  Diagnoser.diagnoseFold(*this, DiagLoc) << E->getSourceRange();
  for (const PartialDiagnosticAt &Note : Notes)
    Diag(Note.first, Note.second);

  if (Result)
    *Result = EvalResult.Val.getInt();
  return E;
}

namespace {
class TransformToPE : public TreeTransform<TransformToPE> {
public:
  TransformToPE(Sema &SemaRef) : TreeTransform<TransformToPE>(SemaRef) {}

  bool AlwaysRebuild() { return true; }
};
} // namespace

ExprResult Sema::TransformToPotentiallyEvaluated(Expr *E) {
  assert(isUnevaluatedContext() &&
         "Should only transform unevaluated expressions");
  ExprEvalContexts.back().Context =
      ExprEvalContexts[ExprEvalContexts.size() - 2].Context;
  if (isUnevaluatedContext())
    return E;
  return TransformToPE(*this).TransformExpr(E);
}

TypeSourceInfo *Sema::TransformToPotentiallyEvaluated(TypeSourceInfo *TInfo) {
  assert(isUnevaluatedContext() &&
         "Should only transform unevaluated expressions");
  ExprEvalContexts.back().Context =
      ExprEvalContexts[ExprEvalContexts.size() - 2].Context;
  if (isUnevaluatedContext())
    return TInfo;
  return TransformToPE(*this).TransformType(TInfo);
}

void Sema::PushExpressionEvaluationContext(
    ExpressionEvaluationContext NewContext) {
  ExprEvalContexts.emplace_back(NewContext, ExprCleanupObjects.size(), Cleanup);
  Cleanup.reset();
}

namespace {

const DeclRefExpr *CheckPossibleDeref(Sema &S, const Expr *PossibleDeref) {
  PossibleDeref = PossibleDeref->IgnoreParenImpCasts();
  if (const auto *E = dyn_cast<UnaryOperator>(PossibleDeref)) {
    if (E->getOpcode() == UO_Deref)
      return CheckPossibleDeref(S, E->getSubExpr());
  } else if (const auto *E = dyn_cast<ArraySubscriptExpr>(PossibleDeref)) {
    return CheckPossibleDeref(S, E->getBase());
  } else if (const auto *E = dyn_cast<MemberExpr>(PossibleDeref)) {
    return CheckPossibleDeref(S, E->getBase());
  } else if (const auto E = dyn_cast<DeclRefExpr>(PossibleDeref)) {
    QualType Inner;
    QualType Ty = E->getType();
    if (const auto *Ptr = Ty->getAs<PointerType>())
      Inner = Ptr->getPointeeType();
    else if (const auto *Arr = S.Context.getAsArrayType(Ty))
      Inner = Arr->getElementType();
    else
      return nullptr;

    if (Inner->hasAttr(attr::NoDeref))
      return E;
  }
  return nullptr;
}

} // namespace

void Sema::WarnOnPendingNoDerefs(ExpressionEvaluationContextRecord &Rec) {
  for (const Expr *E : Rec.PossibleDerefs) {
    const DeclRefExpr *DeclRef = CheckPossibleDeref(*this, E);
    if (DeclRef) {
      const ValueDecl *Decl = DeclRef->getDecl();
      Diag(E->getExprLoc(), diag::warn_dereference_of_noderef_type)
          << Decl->getName() << E->getSourceRange();
      Diag(Decl->getLocation(), diag::note_previous_decl) << Decl->getName();
    } else {
      Diag(E->getExprLoc(), diag::warn_dereference_of_noderef_type_no_decl)
          << E->getSourceRange();
    }
  }
  Rec.PossibleDerefs.clear();
}

void Sema::PopExpressionEvaluationContext() {
  ExpressionEvaluationContextRecord &Rec = ExprEvalContexts.back();
  unsigned NumTypos = Rec.NumTypos;

  WarnOnPendingNoDerefs(Rec);

  // When are coming out of an unevaluated context, clear out any
  // temporaries that we may have created as part of the evaluation of
  // the expression in that context: they aren't relevant because they
  // will never be constructed.
  if (Rec.isUnevaluated() || Rec.isConstantEvaluated()) {
    ExprCleanupObjects.erase(ExprCleanupObjects.begin() + Rec.NumCleanupObjects,
                             ExprCleanupObjects.end());
    Cleanup = Rec.ParentCleanup;
  } else {
    Cleanup.mergeFrom(Rec.ParentCleanup);
  }

  // Pop the current expression evaluation context off the stack.
  ExprEvalContexts.pop_back();

  // The global expression evaluation context record is never popped.
  ExprEvalContexts.back().NumTypos += NumTypos;
}

void Sema::DiscardCleanupsInEvaluationContext() {
  ExprCleanupObjects.erase(ExprCleanupObjects.begin() +
                               ExprEvalContexts.back().NumCleanupObjects,
                           ExprCleanupObjects.end());
  Cleanup.reset();
}

ExprResult Sema::ResolveExprEvaluationContextForTypeof(Expr *E) {
  ExprResult Result = CheckPlaceholderExpr(E);
  if (Result.isInvalid())
    return ExprError();
  E = Result.get();
  if (!E->getType()->isVariablyModifiedType())
    return E;
  return TransformToPotentiallyEvaluated(E);
}

namespace {
bool funcHasParameterSizeMangling(Sema &S, FunctionDecl *FD) {
  // These manglings don't do anything on non-Windows or non-x86 platforms, so
  // we don't need parameter type sizes.
  const llvm::Triple &TT = S.Context.getTargetInfo().getTriple();
  if (!TT.isOSWindows() || !TT.isX86())
    return false;

  // Stdcall, fastcall, and vectorcall need this special treatment.
  CallingConv CC = FD->getType()->castAs<FunctionType>()->getCallConv();
  switch (CC) {
  case CC_X86StdCall:
  case CC_X86FastCall:
  case CC_X86VectorCall:
    return true;
  default:
    break;
  }
  return false;
}
} // namespace

namespace {
void checkCompleteParameterTypesForMangler(Sema &S, FunctionDecl *FD,
                                           SourceLocation Loc) {
  class ParamIncompleteTypeDiagnoser : public Sema::TypeDiagnoser {
    FunctionDecl *FD;
    ParmVarDecl *Param;

  public:
    ParamIncompleteTypeDiagnoser(FunctionDecl *FD, ParmVarDecl *Param)
        : FD(FD), Param(Param) {}

    void diagnose(Sema &S, SourceLocation Loc, QualType T) override {
      CallingConv CC = FD->getType()->castAs<FunctionType>()->getCallConv();
      llvm::StringRef CCName;
      switch (CC) {
      case CC_X86StdCall:
        CCName = "stdcall";
        break;
      case CC_X86FastCall:
        CCName = "fastcall";
        break;
      case CC_X86VectorCall:
        CCName = "vectorcall";
        break;
      default:
        llvm_unreachable("CC does not need mangling");
      }

      S.Diag(Loc, diag::err_cconv_incomplete_param_type)
          << Param->getDeclName() << FD->getDeclName() << CCName;
    }
  };

  for (ParmVarDecl *Param : FD->parameters()) {
    ParamIncompleteTypeDiagnoser Diagnoser(FD, Param);
    S.RequireCompleteType(Loc, Param->getType(), Diagnoser);
  }
}
} // namespace

namespace {
enum class OdrUseContext { None, FormallyOdrUsed, Used };
}

namespace {
OdrUseContext isOdrUseContext(Sema &SemaRef) {
  OdrUseContext Result;

  switch (SemaRef.ExprEvalContexts.back().Context) {
  case Sema::ExpressionEvaluationContext::Unevaluated:
  case Sema::ExpressionEvaluationContext::UnevaluatedAbstract:
    return OdrUseContext::None;

  case Sema::ExpressionEvaluationContext::ConstantEvaluated:
  case Sema::ExpressionEvaluationContext::PotentiallyEvaluated:
    Result = OdrUseContext::Used;
    break;
  }

  return Result;
}
} // namespace

void Sema::MarkFunctionReferenced(SourceLocation Loc, FunctionDecl *Func,
                                  bool MightBeOdrUse) {
  assert(Func && "No function?");

  Func->setReferenced();

  // Recursive functions aren't really used until they're used from some other
  // context.
  bool IsRecursiveCall = CurContext == Func;

  // Potentially-evaluated function name => odr-use (C99 6.9p3); see
  // isOdrUseContext.
  OdrUseContext OdrUse =
      MightBeOdrUse ? isOdrUseContext(*this) : OdrUseContext::None;
  if (IsRecursiveCall && OdrUse == OdrUseContext::Used)
    OdrUse = OdrUseContext::FormallyOdrUsed;

  // If this is the first "real" use, act on that.
  if (OdrUse == OdrUseContext::Used && !Func->isUsed(/*CheckUsedAttr=*/false)) {
    // Keep track of used but undefined functions.
    if (!Func->isDefined()) {
      if (mightHaveNonExternalLinkage(Func))
        UndefinedButUsed.insert(std::make_pair(Func->getCanonicalDecl(), Loc));
      else if (Func->getMostRecentDecl()->isInlined() && !LangOpts.GNUInline &&
               !Func->getMostRecentDecl()->hasAttr<GNUInlineAttr>()) {
        const llvm::Triple &Triple = Context.getTargetInfo().getTriple();
        if (Triple.isOSWindows() || Triple.isOSBinFormatCOFF())
          Func->getMostRecentDecl()->setImplicitlyInline(false);
        UndefinedButUsed.insert(std::make_pair(Func->getCanonicalDecl(), Loc));
      }
    }

    // Some x86 Windows calling conventions mangle the size of the parameter
    // pack into the name. Computing the size of the parameters requires the
    // parameter types to be complete. Check that now.
    if (funcHasParameterSizeMangling(*this, Func))
      checkCompleteParameterTypesForMangler(*this, Func, Loc);

    Func->markUsed(Context);
  }
  // If the function is inlined in the most recent declaration but not in the
  // first declaration, then the function is implicitly inlined. This can happen
  // when a function is not declared inline in a header file and then defined in
  // a source file with the inline keyword. In this case, we need to set the
  // `ImplicitlyInline` flag to false so that the function is not treated as
  // explicitly inlined.
  const llvm::Triple &Triple = Context.getTargetInfo().getTriple();
  if ((Triple.isOSWindows() || Triple.isOSBinFormatCOFF()) &&
      Func->getMostRecentDecl() && Func->getFirstDecl() &&
      Func->getMostRecentDecl()->isInlined() &&
      !Func->getFirstDecl()->isInlined()) {
    Func->getMostRecentDecl()->setImplicitlyInline(false);
  }
}

namespace {
void markVarDeclODRUsed(ValueDecl *V, SourceLocation Loc, Sema &SemaRef) {
  auto *Var = cast<VarDecl>(V);

  if (LLVM_UNLIKELY(isa<ParmVarDecl>(Var)
                        ? false
                        : Var->hasExternalStorage() || Var->isInline()) &&
      Var->hasDefinition(SemaRef.Context) == VarDecl::DeclarationOnly &&
      (!Var->isExternallyVisible() || Var->isInline())) {
    SourceLocation &old = SemaRef.UndefinedButUsed[Var->getCanonicalDecl()];
    if (old.isInvalid())
      old = Loc;
  }
  SemaRef.tryCaptureVariable(V, Loc);

  V->markUsed(SemaRef.Context);
}
} // namespace

void diagnoseUncapturableValueReferenceOrBinding(Sema &S, SourceLocation loc,
                                                 ValueDecl *var) {
  DeclContext *VarDC = var->getDeclContext();

  //  If the parameter still belongs to the translation unit, then
  //  we're actually just using one parameter in the declaration of
  //  the next.
  if (isa<ParmVarDecl>(var) && isa<TranslationUnitDecl>(VarDC))
    return;

  // Outside a function, other diagnostics will cover invalid captures.
  if (!S.CurContext->isFunctionOrMethod())
    return;

  unsigned ContextKind = isa<FunctionDecl>(VarDC) ? 0 : 1;

  S.Diag(loc, diag::err_reference_to_local_in_enclosing_context)
      << var << ContextKind << VarDC;
  S.Diag(var->getLocation(), diag::note_entity_declared_at) << var;
}

bool Sema::tryCaptureVariable(ValueDecl *Var, SourceLocation Loc) {
  DeclContext *VarDC = Var->getDeclContext();
  if (VarDC == CurContext)
    return true;

  const auto *VD = dyn_cast<VarDecl>(Var);
  if (!VD || !VD->hasLocalStorage())
    return true;

  diagnoseUncapturableValueReferenceOrBinding(*this, Loc, Var);
  return true;
}

namespace {
ExprResult rebuildPotentialResultsAsNonOdrUsed(Sema &S, Expr *E,
                                               NonOdrUseReason NOUR) {
  // Strip odr-use when a variable only appears in the "constant / immediately
  // lvalue-to-rvalue" cases.
  //
  // If we encounter a node that claims to be an odr-use but shouldn't be, we
  // transform it into the relevant kind of non-odr-use node and rebuild the
  // tree of nodes leading to it.
  //
  // This is a mini-TreeTransform that only transforms a restricted subset of
  // nodes (and only certain operands of them).

  // Rebuild a subexpression.
  auto Rebuild = [&](Expr *Sub) {
    return rebuildPotentialResultsAsNonOdrUsed(S, Sub, NOUR);
  };

  // Check whether a potential result satisfies the requirements of NOUR.
  auto IsPotentialResultOdrUsed = [&](NamedDecl *D) {
    // Any entity other than a VarDecl is always odr-used whenever it's named
    // in a potentially-evaluated expression.
    auto *VD = dyn_cast<VarDecl>(D);
    if (!VD)
      return true;

    // Variable odr-use exceptions: constexpr-usable refs; non-class potential
    // results with lvalue-to-rvalue; discarded-value cases (see also
    // CheckLValueToRValueConversionOperand).
    switch (NOUR) {
    case NOUR_None:
    case NOUR_Unevaluated:
      llvm_unreachable("unexpected non-odr-use-reason");

    case NOUR_Constant:
      return true;
    }
    return false;
  };

  auto MarkNotOdrUsed = [&] {};

  // Walk the "potential results" of an expression for non-odr-use rebuilds.
  switch (E->getStmtClass()) {
  //   -- If e is an id-expression, ...
  case Expr::DeclRefExprClass: {
    auto *DRE = cast<DeclRefExpr>(E);
    if (DRE->isNonOdrUse() || IsPotentialResultOdrUsed(DRE->getDecl()))
      break;

    // Rebuild as a non-odr-use DeclRefExpr.
    MarkNotOdrUsed();
    return DeclRefExpr::Create(S.Context, DRE->getDecl(), DRE->getNameInfo(),
                               DRE->getType(), DRE->getValueKind(),
                               DRE->getFoundDecl(), NOUR);
  }

  //   -- If e is a subscripting operation with an array operand...
  case Expr::ArraySubscriptExprClass: {
    auto *ASE = cast<ArraySubscriptExpr>(E);
    Expr *OldBase = ASE->getBase()->IgnoreImplicit();
    if (!OldBase->getType()->isArrayType())
      break;
    ExprResult Base = Rebuild(OldBase);
    if (!Base.isUsable())
      return Base;
    Expr *LHS = ASE->getBase() == ASE->getLHS() ? Base.get() : ASE->getLHS();
    Expr *RHS = ASE->getBase() == ASE->getRHS() ? Base.get() : ASE->getRHS();
    SourceLocation LBracketLoc = ASE->getBeginLoc();
    return S.OnArraySubscriptExpr(nullptr, LHS, LBracketLoc, RHS,
                                  ASE->getRBracketLoc());
  }

  case Expr::MemberExprClass: {
    auto *ME = cast<MemberExpr>(E);
    // -- If e is a member access expression naming a field...
    if (isa<FieldDecl>(ME->getMemberDecl())) {
      ExprResult Base = Rebuild(ME->getBase());
      if (!Base.isUsable())
        return Base;
      return MemberExpr::Create(S.Context, Base.get(), ME->isArrow(),
                                ME->getOperatorLoc(), ME->getMemberDecl(),
                                ME->getFoundDecl(), ME->getMemberNameInfo(),
                                ME->getType(), ME->getValueKind(),
                                ME->getObjectKind(), ME->isNonOdrUse());
    }

    // -- Otherwise, fall through.
    if (ME->isNonOdrUse() || IsPotentialResultOdrUsed(ME->getMemberDecl()))
      break;

    // Rebuild as a non-odr-use MemberExpr.
    MarkNotOdrUsed();
    return MemberExpr::Create(
        S.Context, ME->getBase(), ME->isArrow(), ME->getOperatorLoc(),
        ME->getMemberDecl(), ME->getFoundDecl(), ME->getMemberNameInfo(),
        ME->getType(), ME->getValueKind(), ME->getObjectKind(), NOUR);
  }

  case Expr::BinaryOperatorClass: {
    auto *BO = cast<BinaryOperator>(E);
    Expr *LHS = BO->getLHS();
    Expr *RHS = BO->getRHS();
    //   -- If e is a comma expression, ...
    if (BO->getOpcode() == BO_Comma) {
      ExprResult Sub = Rebuild(RHS);
      if (!Sub.isUsable())
        return Sub;
      RHS = Sub.get();
    } else {
      break;
    }
    return S.FormBinOp(nullptr, BO->getOperatorLoc(), BO->getOpcode(), LHS,
                       RHS);
  }

  //   -- If e has the form (e1)...
  case Expr::ParenExprClass: {
    auto *PE = cast<ParenExpr>(E);
    ExprResult Sub = Rebuild(PE->getSubExpr());
    if (!Sub.isUsable())
      return Sub;
    return S.OnParenExpr(PE->getLParen(), PE->getRParen(), Sub.get());
  }

  //   -- If e is an lvalue conditional expression, ...
  case Expr::ConditionalOperatorClass: {
    auto *CO = cast<ConditionalOperator>(E);
    ExprResult LHS = Rebuild(CO->getLHS());
    if (LHS.isInvalid())
      return ExprError();
    ExprResult RHS = Rebuild(CO->getRHS());
    if (RHS.isInvalid())
      return ExprError();
    if (!LHS.isUsable() && !RHS.isUsable())
      return ExprEmpty();
    if (!LHS.isUsable())
      LHS = CO->getLHS();
    if (!RHS.isUsable())
      RHS = CO->getRHS();
    return S.OnConditionalOp(CO->getQuestionLoc(), CO->getColonLoc(),
                             CO->getCond(), LHS.get(), RHS.get());
  }

  // [NeverC extension]
  //   -- If e has the form __extension__ e1...
  case Expr::UnaryOperatorClass: {
    auto *UO = cast<UnaryOperator>(E);
    if (UO->getOpcode() != UO_Extension)
      break;
    ExprResult Sub = Rebuild(UO->getSubExpr());
    if (!Sub.isUsable())
      return Sub;
    return S.FormUnaryOp(nullptr, UO->getOperatorLoc(), UO_Extension,
                         Sub.get());
  }

  // [NeverC extension]
  //   -- If e has the form _Generic(...), the set of potential results is the
  //      union of the sets of potential results of the associated expressions.
  case Expr::GenericSelectionExprClass: {
    auto *GSE = cast<GenericSelectionExpr>(E);

    llvm::SmallVector<Expr *, 4> AssocExprs;
    bool AnyChanged = false;
    for (Expr *OrigAssocExpr : GSE->getAssocExprs()) {
      ExprResult AssocExpr = Rebuild(OrigAssocExpr);
      if (AssocExpr.isInvalid())
        return ExprError();
      if (AssocExpr.isUsable()) {
        AssocExprs.push_back(AssocExpr.get());
        AnyChanged = true;
      } else {
        AssocExprs.push_back(OrigAssocExpr);
      }
    }

    void *ExOrTy = nullptr;
    bool IsExpr = GSE->isExprPredicate();
    if (IsExpr)
      ExOrTy = GSE->getControllingExpr();
    else
      ExOrTy = GSE->getControllingType();
    return AnyChanged ? S.CreateGenericSelectionExpr(
                            GSE->getGenericLoc(), GSE->getDefaultLoc(),
                            GSE->getRParenLoc(), IsExpr, ExOrTy,
                            GSE->getAssocTypeSourceInfos(), AssocExprs)
                      : ExprEmpty();
  }

  // [NeverC extension]
  //   -- If e has the form __builtin_choose_expr(...), the set of potential
  //      results is the union of the sets of potential results of the
  //      second and third subexpressions.
  case Expr::ChooseExprClass: {
    auto *CE = cast<ChooseExpr>(E);

    ExprResult LHS = Rebuild(CE->getLHS());
    if (LHS.isInvalid())
      return ExprError();

    ExprResult RHS = Rebuild(CE->getRHS());
    if (RHS.isInvalid())
      return ExprError();

    if (!LHS.get() && !RHS.get())
      return ExprEmpty();
    if (!LHS.isUsable())
      LHS = CE->getLHS();
    if (!RHS.isUsable())
      RHS = CE->getRHS();

    return S.OnChooseExpr(CE->getBuiltinLoc(), CE->getCond(), LHS.get(),
                          RHS.get(), CE->getRParenLoc());
  }

  // Step through non-syntactic nodes.
  case Expr::ConstantExprClass: {
    auto *CE = cast<ConstantExpr>(E);
    ExprResult Sub = Rebuild(CE->getSubExpr());
    if (!Sub.isUsable())
      return Sub;
    return ConstantExpr::Create(S.Context, Sub.get());
  }

  // We could mostly rely on the recursive rebuilding to rebuild implicit
  // casts, but not at the top level, so rebuild them here.
  case Expr::ImplicitCastExprClass: {
    auto *ICE = cast<ImplicitCastExpr>(E);
    // Only step through no-op implicit casts; other kinds are not used for
    // potential-result / odr-use analysis in C.
    switch (ICE->getCastKind()) {
    case CK_NoOp: {
      ExprResult Sub = Rebuild(ICE->getSubExpr());
      if (!Sub.isUsable())
        return Sub;
      return S.ImpCastExprToType(Sub.get(), ICE->getType(), ICE->getCastKind(),
                                 ICE->getValueKind());
    }

    default:
      break;
    }
    break;
  }

  default:
    break;
  }

  // Can't traverse through this node. Nothing to do.
  return ExprEmpty();
}
} // namespace

ExprResult Sema::CheckLValueToRValueConversionOperand(Expr *E) {
  // Only scalar (non-class, non-volatile) lvalue-to-rvalue can drop odr-use.
  if (E->getType().isVolatileQualified() || E->getType()->getAs<RecordType>())
    return E;

  // NOUR_Constant never transforms a plain DeclRefExpr —
  // IsPotentialResultOdrUsed unconditionally returns true for NOUR_Constant, so
  // the rebuild is a no-op.
  if (LLVM_LIKELY(isa<DeclRefExpr>(E)))
    return E;

  ExprResult Result =
      rebuildPotentialResultsAsNonOdrUsed(*this, E, NOUR_Constant);
  if (Result.isInvalid())
    return ExprError();
  return Result.get() ? Result : E;
}

ExprResult Sema::OnConstantExpression(ExprResult Res) {
  if (!Res.isUsable())
    return Res;

  return CheckLValueToRValueConversionOperand(Res.get());
}

namespace {
void doMarkVarDeclReferenced(
    Sema &SemaRef, SourceLocation Loc, VarDecl *Var, Expr *E,
    llvm::DenseMap<const VarDecl *, int> &RefsMinusAssignments) {
  assert((!E || isa<DeclRefExpr>(E) || isa<MemberExpr>(E)) &&
         "Invalid Expr argument to doMarkVarDeclReferenced");
  Var->setReferenced();

  if (LLVM_UNLIKELY(Var->isInvalidDecl()))
    return;

  if (LLVM_UNLIKELY(SemaRef.TrackUnusedButSet) && Var->isLocalVarDeclOrParm() &&
      !Var->hasExternalStorage()) {
    ++RefsMinusAssignments[Var];
  }

  if (DeclRefExpr *DRE = dyn_cast_or_null<DeclRefExpr>(E))
    if (DRE->isNonOdrUse())
      return;
  if (MemberExpr *ME = dyn_cast_or_null<MemberExpr>(E))
    if (ME->isNonOdrUse())
      return;

  OdrUseContext OdrUse = isOdrUseContext(SemaRef);

  switch (OdrUse) {
  case OdrUseContext::None:
    assert((!E || SemaRef.isUnevaluatedContext()) &&
           "missing non-odr-use marking for unevaluated decl ref");
    break;

  case OdrUseContext::FormallyOdrUsed:
    break;

  case OdrUseContext::Used:
    markVarDeclODRUsed(Var, Loc, SemaRef);
    break;
  }
}
} // namespace

void Sema::MarkVariableReferenced(SourceLocation Loc, VarDecl *Var) {
  doMarkVarDeclReferenced(*this, Loc, Var, nullptr, RefsMinusAssignments);
}

namespace {
void markExprReferenced(
    Sema &SemaRef, SourceLocation Loc, Decl *D, Expr *E, bool MightBeOdrUse,
    llvm::DenseMap<const VarDecl *, int> &RefsMinusAssignments) {
  if (VarDecl *Var = dyn_cast<VarDecl>(D)) {
    doMarkVarDeclReferenced(SemaRef, Loc, Var, E, RefsMinusAssignments);
    return;
  }

  SemaRef.MarkAnyDeclReferenced(Loc, D, MightBeOdrUse);
}
} // namespace

void Sema::MarkDeclRefReferenced(DeclRefExpr *E) {
  bool OdrUse = true;

  markExprReferenced(*this, E->getLocation(), E->getDecl(), E, OdrUse,
                     RefsMinusAssignments);
}

void Sema::MarkMemberReferenced(MemberExpr *E) {
  bool MightBeOdrUse = true;
  SourceLocation Loc =
      E->getMemberLoc().isValid() ? E->getMemberLoc() : E->getBeginLoc();
  markExprReferenced(*this, Loc, E->getMemberDecl(), E, MightBeOdrUse,
                     RefsMinusAssignments);
}

void Sema::MarkAnyDeclReferenced(SourceLocation Loc, Decl *D,
                                 bool MightBeOdrUse) {
  if (MightBeOdrUse) {
    if (auto *VD = dyn_cast<VarDecl>(D)) {
      MarkVariableReferenced(Loc, VD);
      return;
    }
  }
  if (auto *FD = dyn_cast<FunctionDecl>(D)) {
    MarkFunctionReferenced(Loc, FD, MightBeOdrUse);
    return;
  }
  D->setReferenced();
}

namespace {
class EvaluatedExprMarker : public ReferencedDeclWalker<EvaluatedExprMarker> {
public:
  typedef ReferencedDeclWalker<EvaluatedExprMarker> Inherited;
  bool SkipLocalVariables;
  llvm::ArrayRef<const Expr *> StopAt;

  EvaluatedExprMarker(Sema &S, bool SkipLocalVariables,
                      llvm::ArrayRef<const Expr *> StopAt)
      : Inherited(S), SkipLocalVariables(SkipLocalVariables), StopAt(StopAt) {}

  void visitUsedDecl(SourceLocation Loc, Decl *D) {
    S.MarkFunctionReferenced(Loc, cast<FunctionDecl>(D));
  }

  void Visit(Expr *E) {
    if (llvm::is_contained(StopAt, E))
      return;
    Inherited::Visit(E);
  }

  void VisitConstantExpr(ConstantExpr *E) {
    // Don't mark declarations within a ConstantExpression, as this expression
    // will be evaluated and folded to a value.
  }

  void VisitDeclRefExpr(DeclRefExpr *E) {
    // If we were asked not to visit local variables, don't.
    if (SkipLocalVariables) {
      if (VarDecl *VD = dyn_cast<VarDecl>(E->getDecl()))
        if (VD->hasLocalStorage())
          return;
    }

    S.MarkDeclRefReferenced(E);
  }

  void VisitMemberExpr(MemberExpr *E) {
    S.MarkMemberReferenced(E);
    Visit(E->getBase());
  }
};
} // namespace

void Sema::MarkDeclarationsReferencedInExpr(
    Expr *E, bool SkipLocalVariables, llvm::ArrayRef<const Expr *> StopAt) {
  EvaluatedExprMarker(*this, SkipLocalVariables, StopAt).Visit(E);
}

bool Sema::DiagIfReachable(SourceLocation Loc,
                           llvm::ArrayRef<const Stmt *> Stmts,
                           const PartialDiagnostic &PD) {
  if (!Stmts.empty() && getCurFunctionDecl()) {
    if (!FunctionScopes.empty())
      FunctionScopes.back()->PossiblyUnreachableDiags.push_back(
          sema::PossiblyUnreachableDiag(PD, Loc, Stmts));
    return true;
  }

  Diag(Loc, PD);
  return true;
}

bool Sema::DiagRuntimeBehavior(SourceLocation Loc,
                               llvm::ArrayRef<const Stmt *> Stmts,
                               const PartialDiagnostic &PD) {

  switch (ExprEvalContexts.back().Context) {
  case ExpressionEvaluationContext::Unevaluated:
  case ExpressionEvaluationContext::UnevaluatedAbstract:
    break;

  case ExpressionEvaluationContext::ConstantEvaluated:
    break;

  case ExpressionEvaluationContext::PotentiallyEvaluated:
    return DiagIfReachable(Loc, Stmts, PD);
  }

  return false;
}

bool Sema::DiagRuntimeBehavior(SourceLocation Loc, const Stmt *Statement,
                               const PartialDiagnostic &PD) {
  return DiagRuntimeBehavior(
      Loc, Statement ? llvm::ArrayRef(Statement) : std::nullopt, PD);
}

bool Sema::CheckCallReturnType(QualType ReturnType, SourceLocation Loc,
                               CallExpr *CE, FunctionDecl *FD) {
  if (ReturnType->isVoidType() || !ReturnType->isIncompleteType())
    return false;

  class CallReturnIncompleteDiagnoser : public TypeDiagnoser {
    FunctionDecl *FD;
    CallExpr *CE;

  public:
    CallReturnIncompleteDiagnoser(FunctionDecl *FD, CallExpr *CE)
        : FD(FD), CE(CE) {}

    void diagnose(Sema &S, SourceLocation Loc, QualType T) override {
      if (!FD) {
        S.Diag(Loc, diag::err_call_incomplete_return)
            << T << CE->getSourceRange();
        return;
      }

      S.Diag(Loc, diag::err_call_function_incomplete_return)
          << CE->getSourceRange() << FD << T;
      S.Diag(FD->getLocation(), diag::note_entity_declared_at)
          << FD->getDeclName();
    }
  } Diagnoser(FD, CE);

  if (RequireCompleteType(Loc, ReturnType, Diagnoser))
    return true;

  return false;
}

// Diagnose the s/=/==/ and s/\|=/!=/ typos. Note that adding parentheses
// will prevent this condition from triggering, which is what we want.
void Sema::DiagnoseAssignmentAsCondition(Expr *E) {
  SourceLocation Loc;

  unsigned diagnostic = diag::warn_condition_is_assignment;
  bool IsOrAssign = false;

  if (BinaryOperator *Op = dyn_cast<BinaryOperator>(E)) {
    if (Op->getOpcode() != BO_Assign && Op->getOpcode() != BO_OrAssign)
      return;

    IsOrAssign = Op->getOpcode() == BO_OrAssign;

    // Greylist some idioms by putting them into a warning subcategory.

    Loc = Op->getOperatorLoc();
  } else {
    return;
  }

  Diag(Loc, diagnostic) << E->getSourceRange();

  SourceLocation Open = E->getBeginLoc();
  SourceLocation Close = getLocForEndOfToken(E->getSourceRange().getEnd());
  Diag(Loc, diag::note_condition_assign_silence)
      << FixItHint::CreateInsertion(Open, "(")
      << FixItHint::CreateInsertion(Close, ")");

  if (IsOrAssign)
    Diag(Loc, diag::note_condition_or_assign_to_comparison)
        << FixItHint::CreateReplacement(Loc, "!=");
  else
    Diag(Loc, diag::note_condition_assign_to_comparison)
        << FixItHint::CreateReplacement(Loc, "==");
}

void Sema::DiagnoseEqualityWithExtraParens(ParenExpr *ParenE) {
  // Don't warn if the parens came from a macro.
  SourceLocation parenLoc = ParenE->getBeginLoc();
  if (parenLoc.isInvalid() || parenLoc.isMacroID())
    return;
  Expr *E = ParenE->IgnoreParens();

  if (BinaryOperator *opE = dyn_cast<BinaryOperator>(E))
    if (opE->getOpcode() == BO_EQ &&
        opE->getLHS()->IgnoreParenImpCasts()->isModifiableLvalue(Context) ==
            Expr::MLV_Valid) {
      SourceLocation Loc = opE->getOperatorLoc();

      Diag(Loc, diag::warn_equality_with_extra_parens) << E->getSourceRange();
      SourceRange ParenERange = ParenE->getSourceRange();
      Diag(Loc, diag::note_equality_comparison_silence)
          << FixItHint::CreateRemoval(ParenERange.getBegin())
          << FixItHint::CreateRemoval(ParenERange.getEnd());
      Diag(Loc, diag::note_equality_comparison_to_assign)
          << FixItHint::CreateReplacement(Loc, "=");
    }
}

ExprResult Sema::CheckBooleanCondition(SourceLocation Loc, Expr *E,
                                       bool IsConstexpr) {
  DiagnoseAssignmentAsCondition(E);
  if (ParenExpr *parenE = dyn_cast<ParenExpr>(E))
    DiagnoseEqualityWithExtraParens(parenE);

  ExprResult result = CheckPlaceholderExpr(E);
  if (result.isInvalid())
    return ExprError();
  E = result.get();

  {
    ExprResult ERes = DefaultFunctionArrayLvalueConversion(E);
    if (ERes.isInvalid())
      return ExprError();
    E = ERes.get();

    QualType T = E->getType();
    if (!T->isScalarType()) {
      Diag(Loc, diag::err_typecheck_statement_requires_scalar)
          << T << E->getSourceRange();
      return ExprError();
    }
    CheckBoolLikeConversion(E, Loc);
  }

  return E;
}

Sema::ConditionResult Sema::OnCondition(Scope *S, SourceLocation Loc,
                                        Expr *SubExpr, ConditionKind CK,
                                        bool MissingOK) {
  // MissingOK indicates whether having no condition expression is valid
  // (for loop) or invalid (e.g. while loop).
  if (!SubExpr)
    return MissingOK ? ConditionResult() : ConditionError();

  ExprResult Cond;
  switch (CK) {
  case ConditionKind::Boolean:
    Cond = CheckBooleanCondition(Loc, SubExpr);
    break;

  case ConditionKind::Switch:
    Cond = CheckSwitchCondition(Loc, SubExpr);
    break;
  }
  if (Cond.isInvalid()) {
    Cond = CreateRecoveryExpr(SubExpr->getBeginLoc(), SubExpr->getEndLoc(),
                              {SubExpr}, PreferredConditionType(CK));
    if (!Cond.get())
      return ConditionError();
  }
  FullExprArg FullExpr = MakeFullExpr(Cond.get(), Loc);
  if (!FullExpr.get())
    return ConditionError();

  return ConditionResult(*this, nullptr, FullExpr, false);
}

ExprResult Sema::CheckPlaceholderExpr(Expr *E) {
  const BuiltinType *placeholderType = E->getType()->getAsPlaceholderType();
  if (!placeholderType)
    return E;

  switch (placeholderType->getKind()) {

  case BuiltinType::Overload: {
    ExprResult Result = E;
    tryToRecoverWithCall(Result, PDiag(diag::err_ovl_unresolvable),
                         /*complain*/ true);
    return Result;
  }

  case BuiltinType::PseudoObject:
    return ExprError();

  case BuiltinType::BuiltinFn: {
    // Accept __noop without parens by implicitly converting it to a call expr.
    auto *DRE = dyn_cast<DeclRefExpr>(E->IgnoreParenImpCasts());
    if (DRE) {
      auto *FD = cast<FunctionDecl>(DRE->getDecl());
      unsigned BuiltinID = FD->getBuiltinID();
      if (BuiltinID == Builtin::BI__noop) {
        E = ImpCastExprToType(E, Context.getPointerType(FD->getType()),
                              CK_BuiltinFnToFnPtr)
                .get();
        return CallExpr::Create(Context, E, /*Args=*/{}, Context.IntTy,
                                VK_PRValue, SourceLocation(),
                                FPOptionsOverride());
      }
    }

    Diag(E->getBeginLoc(), diag::err_builtin_fn_use);
    return ExprError();
  }

  case BuiltinType::IncompleteMatrixIdx:
    Diag(cast<MatrixSubscriptExpr>(E->IgnoreParens())
             ->getRowIdx()
             ->getBeginLoc(),
         diag::err_matrix_incomplete_index);
    return ExprError();

    // Everything else should be impossible.
#define SVE_TYPE(Name, Id, SingletonId) case BuiltinType::Id:
#include "neverc/Foundation/Builtin/AArch64SVEACLETypes.def"
#define BUILTIN_TYPE(Id, SingletonId) case BuiltinType::Id:
#define PLACEHOLDER_TYPE(Id, SingletonId)
#include "neverc/Tree/Type/BuiltinTypes.def"
    break;
  }

  llvm_unreachable("invalid placeholder type!");
}

bool Sema::CheckCaseExpression(Expr *E) {
  if (E->isIntegerConstantExpr(Context))
    return E->getType()->isIntegralOrEnumerationType();
  return false;
}

ExprResult Sema::CreateRecoveryExpr(SourceLocation Begin, SourceLocation End,
                                    llvm::ArrayRef<Expr *> SubExprs,
                                    QualType T) {
  if (!Context.getLangOpts().RecoveryAST)
    return ExprError();

  if (T.isNull() || T->isUndeducedType() ||
      !Context.getLangOpts().RecoveryASTType)
    // We don't know the concrete type, fallback to dependent type.
    T = Context.DependentTy;

  return RecoveryExpr::Create(Context, T, Begin, End, SubExprs);
}
