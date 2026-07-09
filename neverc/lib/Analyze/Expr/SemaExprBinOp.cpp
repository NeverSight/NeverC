//===- SemaExprBinOp.cpp - Binary operator semantic analysis ------------===//
//
//===----------------------------------------------------------------------===//

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

// ===----------------------------------------------------------------------===
// Helpers shared with SemaExpr.cpp
// ===----------------------------------------------------------------------===

namespace neverc {

struct OriginalOperand {
  explicit OriginalOperand(Expr *Op) : Orig(Op) {
    if (auto *ICE = dyn_cast<ImplicitCastExpr>(Op))
      Orig = ICE->getSubExprAsWritten();
  }

  QualType getType() const { return Orig->getType(); }

  Expr *Orig;
};

void suggestParentheses(Sema &Self, SourceLocation Loc,
                        const PartialDiagnostic &Note, SourceRange ParenRange) {
  SourceLocation EndLoc = Self.getLocForEndOfToken(ParenRange.getEnd());
  if (ParenRange.getBegin().isFileID() && ParenRange.getEnd().isFileID() &&
      EndLoc.isValid()) {
    Self.Diag(Loc, Note) << FixItHint::CreateInsertion(ParenRange.getBegin(),
                                                       "(")
                         << FixItHint::CreateInsertion(EndLoc, ")");
  } else {
    Self.Diag(Loc, Note) << ParenRange;
  }
}

bool couldBeNeverCStringOperand(Expr *E) {
  QualType T = E->getType();
  if (T.isNull())
    return false;
  const Type *TP = T.getTypePtr();
  return TP->isRecordType() || TP->isArrayType() || TP->isPointerType();
}

bool isNeverCStringOperand(Sema &S, Expr *E) {
  if (!couldBeNeverCStringOperand(E))
    return false;
  return S.isNeverCStringType(E->getType()) || getNeverCStringLiteral(E);
}


LLVM_ATTRIBUTE_NOINLINE
void warnNullPtrDeref(Sema &S, Expr *E) {
  const auto *UO = dyn_cast<UnaryOperator>(E->IgnoreParenCasts());
  if (UO && UO->getOpcode() == UO_Deref &&
      UO->getSubExpr()->getType()->isPointerType()) {
    const LangAS AS =
        UO->getSubExpr()->getType()->getPointeeType().getAddressSpace();
    if ((!isTargetAddressSpace(AS) ||
         (isTargetAddressSpace(AS) && toTargetAddressSpace(AS) == 0)) &&
        UO->getSubExpr()->IgnoreParenCasts()->isNullPointerConstant(
            S.Context, Expr::NPC_ValueDependentIsNotNull) &&
        !UO->getType().isVolatileQualified()) {
      S.DiagRuntimeBehavior(UO->getOperatorLoc(), UO,
                            S.PDiag(diag::warn_indirection_through_null)
                                << UO->getSubExpr()->getSourceRange());
      S.DiagRuntimeBehavior(UO->getOperatorLoc(), UO,
                            S.PDiag(diag::note_indirection_through_null));
    }
  }
}

bool isVector(QualType QT, QualType ElementType) {
  if (const VectorType *VT = QT->getAs<VectorType>())
    return VT->getElementType().getCanonicalType() == ElementType;
  return false;
}

ExprResult buildNeverCStringConcat(Sema &S, SourceLocation OpLoc, Expr *LHS,
                                   Expr *RHS) {
  Expr *Args[] = {LHS, RHS};
  return buildNeverCStringRuntimeCall(S, /*Scope=*/nullptr, OpLoc,
                                      BuiltinStringNames::ConcatFunctionName,
                                      Args, OpLoc);
}

ExprResult buildNeverCStringCompare(Sema &S, SourceLocation OpLoc,
                                    BinaryOperatorKind Opc, Expr *LHS,
                                    Expr *RHS) {
  assert(BinaryOperator::isComparisonOp(Opc) &&
         "only comparison operators compare neverc strings");

  Expr *Args[] = {LHS, RHS};

  if (BinaryOperator::isEqualityOp(Opc)) {
    const CallExpr *DecryptCall = getDecryptLiteralCall(RHS);
    Expr *OtherOperand = LHS;
    if (!DecryptCall) {
      DecryptCall = getDecryptLiteralCall(LHS);
      OtherOperand = RHS;
    }

    if (DecryptCall && DecryptCall->getNumArgs() == 3) {
      Expr *DecryptArgs[] = {
          OtherOperand,
          const_cast<Expr *>(DecryptCall->getArg(0)),
          const_cast<Expr *>(DecryptCall->getArg(1)),
          const_cast<Expr *>(DecryptCall->getArg(2))};
      ExprResult Eq = buildNeverCStringRuntimeCall(
          S, /*Scope=*/nullptr, OpLoc,
          BuiltinStringNames::DecryptEqualsFunctionName, DecryptArgs, OpLoc);
      if (Eq.isInvalid() || Opc == BO_EQ)
        return Eq;
      return S.FormUnaryOp(/*Scope=*/nullptr, OpLoc, UO_LNot, Eq.get());
    }

    ExprResult Eq = buildNeverCStringRuntimeCall(
        S, /*Scope=*/nullptr, OpLoc, BuiltinStringNames::EqualFunctionName,
        Args, OpLoc);
    if (Eq.isInvalid() || Opc == BO_EQ)
      return Eq;
    return S.FormUnaryOp(/*Scope=*/nullptr, OpLoc, UO_LNot, Eq.get());
  }

  {
    const CallExpr *DecryptCall = getDecryptLiteralCall(RHS);
    Expr *OtherOperand = LHS;
    bool DecryptOnLHS = false;
    if (!DecryptCall) {
      DecryptCall = getDecryptLiteralCall(LHS);
      OtherOperand = RHS;
      DecryptOnLHS = true;
    }
    if (DecryptCall && DecryptCall->getNumArgs() == 3) {
      Expr *DecryptArgs[] = {
          OtherOperand,
          const_cast<Expr *>(DecryptCall->getArg(0)),
          const_cast<Expr *>(DecryptCall->getArg(1)),
          const_cast<Expr *>(DecryptCall->getArg(2))};
      ExprResult Cmp = buildNeverCStringRuntimeCall(
          S, /*Scope=*/nullptr, OpLoc,
          BuiltinStringNames::DecryptCompareFunctionName, DecryptArgs, OpLoc);
      if (Cmp.isInvalid())
        return Cmp;
      ExprResult Zero = S.OnIntegerConstant(OpLoc, /*Val=*/0);
      if (Zero.isInvalid())
        return ExprError();
      BinaryOperatorKind FinalOpc =
          DecryptOnLHS ? BinaryOperator::reverseComparisonOp(Opc) : Opc;
      return S.FormBinOp(/*Scope=*/nullptr, OpLoc, FinalOpc, Cmp.get(),
                         Zero.get());
    }
  }
  ExprResult Cmp = buildNeverCStringRuntimeCall(
      S, /*Scope=*/nullptr, OpLoc, BuiltinStringNames::CompareFunctionName,
      Args, OpLoc);
  if (Cmp.isInvalid())
    return Cmp;
  ExprResult Zero = S.OnIntegerConstant(OpLoc, /*Val=*/0);
  if (Zero.isInvalid())
    return ExprError();
  return S.FormBinOp(/*Scope=*/nullptr, OpLoc, Opc, Cmp.get(), Zero.get());
}

} // namespace neverc

// ===----------------------------------------------------------------------===
// Binary operators (arithmetic, shift, compare)
// ===----------------------------------------------------------------------===

QualType Sema::InvalidOperands(SourceLocation Loc, ExprResult &LHS,
                               ExprResult &RHS) {
  OriginalOperand OrigLHS(LHS.get()), OrigRHS(RHS.get());

  Diag(Loc, diag::err_typecheck_invalid_operands)
      << OrigLHS.getType() << OrigRHS.getType() << LHS.get()->getSourceRange()
      << RHS.get()->getSourceRange();

  return QualType();
}

// Diagnose invalid vector logical operands (including scalar/vector mixes).
QualType Sema::InvalidLogicalVectorOperands(SourceLocation Loc, ExprResult &LHS,
                                            ExprResult &RHS) {
  QualType LHSType = LHS.get()->IgnoreImpCasts()->getType();
  QualType RHSType = RHS.get()->IgnoreImpCasts()->getType();

  bool LHSNatVec = LHSType->isVectorType();
  bool RHSNatVec = RHSType->isVectorType();

  if (!(LHSNatVec && RHSNatVec)) {
    Expr *Vector = LHSNatVec ? LHS.get() : RHS.get();
    Expr *NonVector = !LHSNatVec ? LHS.get() : RHS.get();
    Diag(Loc, diag::err_typecheck_logical_vector_expr_restrict)
        << 0 << Vector->getType() << NonVector->IgnoreImpCasts()->getType()
        << Vector->getSourceRange();
    return QualType();
  }

  Diag(Loc, diag::err_typecheck_logical_vector_expr_restrict)
      << 1 << LHSType << RHSType << LHS.get()->getSourceRange()
      << RHS.get()->getSourceRange();

  return QualType();
}


namespace {
LLVM_ATTRIBUTE_NOINLINE
void warnXorMisusedAsPow(Sema &S, const ExprResult &XorLHS,
                         const ExprResult &XorRHS, const SourceLocation Loc) {
  // Do not diagnose macros.
  if (Loc.isMacroID())
    return;

  // Do not diagnose if both LHS and RHS are macros.
  if (XorLHS.get()->getExprLoc().isMacroID() &&
      XorRHS.get()->getExprLoc().isMacroID())
    return;

  bool Negative = false;
  bool ExplicitPlus = false;
  const auto *LHSInt = dyn_cast<IntegerLiteral>(XorLHS.get());
  const auto *RHSInt = dyn_cast<IntegerLiteral>(XorRHS.get());

  if (!LHSInt)
    return;
  if (!RHSInt) {
    if (const auto *UO = dyn_cast<UnaryOperator>(XorRHS.get())) {
      UnaryOperatorKind Opc = UO->getOpcode();
      if (Opc != UO_Minus && Opc != UO_Plus)
        return;
      RHSInt = dyn_cast<IntegerLiteral>(UO->getSubExpr());
      if (!RHSInt)
        return;
      Negative = (Opc == UO_Minus);
      ExplicitPlus = !Negative;
    } else {
      return;
    }
  }

  const llvm::APInt &LeftSideValue = LHSInt->getValue();
  llvm::APInt RightSideValue = RHSInt->getValue();
  if (LeftSideValue != 2 && LeftSideValue != 10)
    return;

  if (LeftSideValue.getBitWidth() != RightSideValue.getBitWidth())
    return;

  CharSourceRange ExprRange = CharSourceRange::getCharRange(
      LHSInt->getBeginLoc(), S.getLocForEndOfToken(RHSInt->getLocation()));
  llvm::StringRef ExprStr = SourceScanner::getSourceText(
      ExprRange, S.getSourceManager(), S.getLangOpts());

  CharSourceRange XorRange =
      CharSourceRange::getCharRange(Loc, S.getLocForEndOfToken(Loc));
  llvm::StringRef XorStr = SourceScanner::getSourceText(
      XorRange, S.getSourceManager(), S.getLangOpts());
  // Do not diagnose if xor keyword/macro is used.
  if (XorStr == "xor")
    return;

  std::string LHSStr = std::string(SourceScanner::getSourceText(
      CharSourceRange::getTokenRange(LHSInt->getSourceRange()),
      S.getSourceManager(), S.getLangOpts()));
  std::string RHSStr = std::string(SourceScanner::getSourceText(
      CharSourceRange::getTokenRange(RHSInt->getSourceRange()),
      S.getSourceManager(), S.getLangOpts()));

  if (Negative) {
    RightSideValue = -RightSideValue;
    RHSStr = "-" + RHSStr;
  } else if (ExplicitPlus) {
    RHSStr = "+" + RHSStr;
  }

  llvm::StringRef LHSStrRef = LHSStr;
  llvm::StringRef RHSStrRef = RHSStr;
  // Do not diagnose literals with digit separators, binary, hexadecimal, octal
  // literals.
  if (LHSStrRef.starts_with("0b") || LHSStrRef.starts_with("0B") ||
      RHSStrRef.starts_with("0b") || RHSStrRef.starts_with("0B") ||
      LHSStrRef.starts_with("0x") || LHSStrRef.starts_with("0X") ||
      RHSStrRef.starts_with("0x") || RHSStrRef.starts_with("0X") ||
      (LHSStrRef.size() > 1 && LHSStrRef.starts_with("0")) ||
      (RHSStrRef.size() > 1 && RHSStrRef.starts_with("0")) ||
      LHSStrRef.contains('\'') || RHSStrRef.contains('\''))
    return;

  bool SuggestXor = S.getPrepEngine().isMacroDefined("xor");
  const llvm::APInt XorValue = LeftSideValue ^ RightSideValue;
  int64_t RightSideIntValue = RightSideValue.getSExtValue();
  if (LeftSideValue == 2 && RightSideIntValue >= 0) {
    std::string SuggestedExpr = "1 << " + RHSStr;
    bool Overflow = false;
    llvm::APInt One = (LeftSideValue - 1);
    llvm::APInt PowValue = One.sshl_ov(RightSideValue, Overflow);
    if (Overflow) {
      if (RightSideIntValue < 64)
        S.Diag(Loc, diag::warn_xor_used_as_pow_base)
            << ExprStr << toString(XorValue, 10, true) << ("1LL << " + RHSStr)
            << FixItHint::CreateReplacement(ExprRange, "1LL << " + RHSStr);
      else if (RightSideIntValue == 64)
        S.Diag(Loc, diag::warn_xor_used_as_pow)
            << ExprStr << toString(XorValue, 10, true);
      else
        return;
    } else {
      S.Diag(Loc, diag::warn_xor_used_as_pow_base_extra)
          << ExprStr << toString(XorValue, 10, true) << SuggestedExpr
          << toString(PowValue, 10, true)
          << FixItHint::CreateReplacement(
                 ExprRange, (RightSideIntValue == 0) ? "1" : SuggestedExpr);
    }

    S.Diag(Loc, diag::note_xor_used_as_pow_silence)
        << ("0x2 ^ " + RHSStr) << SuggestXor;
  } else if (LeftSideValue == 10) {
    std::string SuggestedValue = "1e" + std::to_string(RightSideIntValue);
    S.Diag(Loc, diag::warn_xor_used_as_pow_base)
        << ExprStr << toString(XorValue, 10, true) << SuggestedValue
        << FixItHint::CreateReplacement(ExprRange, SuggestedValue);
    S.Diag(Loc, diag::note_xor_used_as_pow_silence)
        << ("0xA ^ " + RHSStr) << SuggestXor;
  }
}
} // namespace

namespace {
bool isValidBoolVectorBinOp(BinaryOperatorKind Opc) {
  switch (Opc) {
  default:
    return false;
  case BO_And:
  case BO_AndAssign:
  case BO_Or:
  case BO_OrAssign:
  case BO_Xor:
  case BO_XorAssign:
    return true;
  }
}
} // namespace

inline QualType Sema::CheckBitwiseOperands(ExprResult &LHS, ExprResult &RHS,
                                           SourceLocation Loc,
                                           BinaryOperatorKind Opc) {
  bool IsCompAssign =
      Opc == BO_AndAssign || Opc == BO_OrAssign || Opc == BO_XorAssign;

  bool LegalBoolVecOperator = isValidBoolVectorBinOp(Opc);

  if (LHS.get()->getType()->isVectorType() ||
      RHS.get()->getType()->isVectorType()) {
    if (LHS.get()->getType()->hasIntegerRepresentation() &&
        RHS.get()->getType()->hasIntegerRepresentation())
      return CheckVectorOperands(LHS, RHS, Loc, IsCompAssign,
                                 /*AllowBothBool*/ true,
                                 /*AllowBoolConversions*/ false,
                                 /*AllowBooleanOperation*/ LegalBoolVecOperator,
                                 /*ReportInvalid*/ true);
    return InvalidOperands(Loc, LHS, RHS);
  }

  if (LHS.get()->getType()->isSveVLSBuiltinType() ||
      RHS.get()->getType()->isSveVLSBuiltinType()) {
    if (LHS.get()->getType()->hasIntegerRepresentation() &&
        RHS.get()->getType()->hasIntegerRepresentation())
      return CheckSizelessVectorOperands(LHS, RHS, Loc, IsCompAssign,
                                         ACK_BitwiseOp);
    return InvalidOperands(Loc, LHS, RHS);
  }

  if (LHS.get()->getType()->isSveVLSBuiltinType() ||
      RHS.get()->getType()->isSveVLSBuiltinType()) {
    if (LHS.get()->getType()->hasIntegerRepresentation() &&
        RHS.get()->getType()->hasIntegerRepresentation())
      return CheckSizelessVectorOperands(LHS, RHS, Loc, IsCompAssign,
                                         ACK_BitwiseOp);
    return InvalidOperands(Loc, LHS, RHS);
  }

  if (Opc == BO_And)
    warnLogicalNotOnLHS(*this, LHS, RHS, Loc, Opc);

  if (LHS.get()->getType()->hasFloatingRepresentation() ||
      RHS.get()->getType()->hasFloatingRepresentation())
    return InvalidOperands(Loc, LHS, RHS);

  ExprResult LHSResult = LHS, RHSResult = RHS;
  QualType compType = UsualArithmeticConversions(
      LHSResult, RHSResult, Loc, IsCompAssign ? ACK_CompAssign : ACK_BitwiseOp);
  if (LHSResult.isInvalid() || RHSResult.isInvalid())
    return QualType();
  LHS = LHSResult.get();
  RHS = RHSResult.get();

  if (Opc == BO_Xor)
    warnXorMisusedAsPow(*this, LHS, RHS, Loc);

  if (!compType.isNull() && compType->isIntegralOrUnscopedEnumerationType())
    return compType;
  return InvalidOperands(Loc, LHS, RHS);
}

// C99 6.5.[13,14]
inline QualType Sema::CheckLogicalOperands(ExprResult &LHS, ExprResult &RHS,
                                           SourceLocation Loc,
                                           BinaryOperatorKind Opc) {
  if (LHS.get()->getType()->isVectorType() ||
      RHS.get()->getType()->isVectorType())
    return CheckVectorLogicalOperands(LHS, RHS, Loc);

  bool EnumConstantInBoolContext = false;
  for (const ExprResult &HS : {LHS, RHS}) {
    if (const auto *DREHS = dyn_cast<DeclRefExpr>(HS.get())) {
      const auto *ECDHS = dyn_cast<EnumConstantDecl>(DREHS->getDecl());
      if (ECDHS && ECDHS->getInitVal() != 0 && ECDHS->getInitVal() != 1)
        EnumConstantInBoolContext = true;
    }
  }

  if (EnumConstantInBoolContext)
    Diag(Loc, diag::warn_enum_constant_in_bool_context);

  // Diagnose cases where the user write a logical and/or but probably meant a
  // bitwise one.  We do this when the LHS is a non-bool integer and the RHS
  // is a constant.
  if (!EnumConstantInBoolContext && LHS.get()->getType()->isIntegerType() &&
      !LHS.get()->getType()->isBooleanType() &&
      RHS.get()->getType()->isIntegerType() &&
      // Don't warn in macros.
      !Loc.isMacroID()) {
    // If the RHS can be constant folded, and if it constant folds to something
    // that isn't 0 or 1 (which indicate a potential logical operation that
    // happened to fold to true/false) then warn.
    // Parens on the RHS are ignored.
    Expr::EvalResult EVResult;
    if (RHS.get()->EvaluateAsInt(EVResult, Context)) {
      llvm::APSInt Result = EVResult.Val.getInt();
      if ((getLangOpts().Bool && !RHS.get()->getType()->isBooleanType() &&
           !RHS.get()->getExprLoc().isMacroID()) ||
          (Result != 0 && Result != 1)) {
        Diag(Loc, diag::warn_logical_instead_of_bitwise)
            << RHS.get()->getSourceRange() << (Opc == BO_LAnd ? "&&" : "||");
        // Suggest replacing the logical operator with the bitwise version
        Diag(Loc, diag::note_logical_instead_of_bitwise_change_operator)
            << (Opc == BO_LAnd ? "&" : "|")
            << FixItHint::CreateReplacement(
                   SourceRange(Loc, getLocForEndOfToken(Loc)),
                   Opc == BO_LAnd ? "&" : "|");
        if (Opc == BO_LAnd)
          // Suggest replacing "Foo() && kNonZero" with "Foo()"
          Diag(Loc, diag::note_logical_instead_of_bitwise_remove_constant)
              << FixItHint::CreateRemoval(
                     SourceRange(getLocForEndOfToken(LHS.get()->getEndLoc()),
                                 RHS.get()->getEndLoc()));
      }
    }
  }

  LHS = UsualUnaryConversions(LHS.get());
  if (LHS.isInvalid())
    return QualType();

  RHS = UsualUnaryConversions(RHS.get());
  if (RHS.isInvalid())
    return QualType();

  if (!LHS.get()->getType()->isScalarType() ||
      !RHS.get()->getType()->isScalarType())
    return InvalidOperands(Loc, LHS, RHS);

  return Context.IntTy;
}

namespace {
bool isTypeWritable(QualType Ty, bool IsDereference) {
  if (IsDereference && Ty->isPointerType())
    Ty = Ty->getPointeeType();
  return !Ty.isConstQualified();
}
} // namespace

// Update err_typecheck_assign_const and note_typecheck_assign_const
// when this enum is changed.
enum {
  ConstFunction,
  ConstVariable,
  ConstMember,
  NestedConstMember,
  ConstUnknown, // Keep as last element
};

namespace {
LLVM_ATTRIBUTE_NOINLINE
void reportConstAssignment(Sema &S, const Expr *E, SourceLocation Loc) {
  SourceRange ExprRange = E->getSourceRange();

  // Only emit one error on the first const found.  All other consts will emit
  // a note to the error.
  bool DiagnosticEmitted = false;

  // Track if the current expression is the result of a dereference, and if the
  // next checked expression is the result of a dereference.
  bool IsDereference = false;
  bool NextIsDereference = false;
  while (true) {
    IsDereference = NextIsDereference;

    E = E->IgnoreImplicit()->IgnoreParenImpCasts();
    if (const MemberExpr *ME = dyn_cast<MemberExpr>(E)) {
      NextIsDereference = ME->isArrow();
      const ValueDecl *VD = ME->getMemberDecl();
      if (const FieldDecl *Field = dyn_cast<FieldDecl>(VD)) {
        if (!isTypeWritable(Field->getType(), IsDereference)) {
          if (!DiagnosticEmitted) {
            S.Diag(Loc, diag::err_typecheck_assign_const)
                << ExprRange << ConstMember << Field << Field->getType();
            DiagnosticEmitted = true;
          }
          S.Diag(VD->getLocation(), diag::note_typecheck_assign_const)
              << ConstMember << Field << Field->getType()
              << Field->getSourceRange();
        }
        break;
      }
      break; // End MemberExpr
    } else if (const ArraySubscriptExpr *ASE =
                   dyn_cast<ArraySubscriptExpr>(E)) {
      E = ASE->getBase()->IgnoreParenImpCasts();
      continue;
    } else if (const ExtVectorElementExpr *EVE =
                   dyn_cast<ExtVectorElementExpr>(E)) {
      E = EVE->getBase()->IgnoreParenImpCasts();
      continue;
    }
    break;
  }

  if (const CallExpr *CE = dyn_cast<CallExpr>(E)) {
    // Function calls
    const FunctionDecl *FD = CE->getDirectCallee();
    if (FD && !isTypeWritable(FD->getReturnType(), IsDereference)) {
      if (!DiagnosticEmitted) {
        S.Diag(Loc, diag::err_typecheck_assign_const)
            << ExprRange << ConstFunction << FD;
        DiagnosticEmitted = true;
      }
      S.Diag(FD->getReturnTypeSourceRange().getBegin(),
             diag::note_typecheck_assign_const)
          << ConstFunction << FD << FD->getReturnType()
          << FD->getReturnTypeSourceRange();
    }
  } else if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E)) {
    // Point to variable declaration.
    if (const ValueDecl *VD = DRE->getDecl()) {
      if (!isTypeWritable(VD->getType(), IsDereference)) {
        if (!DiagnosticEmitted) {
          S.Diag(Loc, diag::err_typecheck_assign_const)
              << ExprRange << ConstVariable << VD << VD->getType();
          DiagnosticEmitted = true;
        }
        S.Diag(VD->getLocation(), diag::note_typecheck_assign_const)
            << ConstVariable << VD << VD->getType() << VD->getSourceRange();
      }
    }
  }

  if (DiagnosticEmitted)
    return;

  // Can't determine a more specific message, so display the generic error.
  S.Diag(Loc, diag::err_typecheck_assign_const) << ExprRange << ConstUnknown;
}
} // namespace

enum OriginalExprKind { OEK_Variable, OEK_Member, OEK_LValue };

namespace {
LLVM_ATTRIBUTE_NOINLINE
void reportRecursiveConstFields(Sema &S, const ValueDecl *VD,
                                const RecordType *Ty, SourceLocation Loc,
                                SourceRange Range, OriginalExprKind OEK,
                                bool &DiagnosticEmitted) {
  std::vector<const RecordType *> RecordTypeList;
  RecordTypeList.push_back(Ty);
  unsigned NextToCheckIndex = 0;
  // We walk the record hierarchy breadth-first to ensure that we print
  // diagnostics in field nesting order.
  while (RecordTypeList.size() > NextToCheckIndex) {
    bool IsNested = NextToCheckIndex > 0;
    for (const FieldDecl *Field :
         RecordTypeList[NextToCheckIndex]->getDecl()->fields()) {
      // First, check every field for constness.
      QualType FieldTy = Field->getType();
      if (FieldTy.isConstQualified()) {
        if (!DiagnosticEmitted) {
          S.Diag(Loc, diag::err_typecheck_assign_const)
              << Range << NestedConstMember << OEK << VD << IsNested << Field;
          DiagnosticEmitted = true;
        }
        S.Diag(Field->getLocation(), diag::note_typecheck_assign_const)
            << NestedConstMember << IsNested << Field << FieldTy
            << Field->getSourceRange();
      }

      // Then we append it to the list to check next in order.
      FieldTy = FieldTy.getCanonicalType();
      if (const auto *FieldRecTy = FieldTy->getAs<RecordType>()) {
        if (!llvm::is_contained(RecordTypeList, FieldRecTy))
          RecordTypeList.push_back(FieldRecTy);
      }
    }
    ++NextToCheckIndex;
  }
}
} // namespace

namespace {
LLVM_ATTRIBUTE_NOINLINE
void reportRecursiveConstFields(Sema &S, const Expr *E, SourceLocation Loc) {
  QualType Ty = E->getType();
  assert(Ty->isRecordType() && "lvalue was not record?");
  SourceRange Range = E->getSourceRange();
  const RecordType *RTy = Ty.getCanonicalType()->getAs<RecordType>();
  bool DiagEmitted = false;

  if (const MemberExpr *ME = dyn_cast<MemberExpr>(E))
    reportRecursiveConstFields(S, ME->getMemberDecl(), RTy, Loc, Range,
                               OEK_Member, DiagEmitted);
  else if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E))
    reportRecursiveConstFields(S, DRE->getDecl(), RTy, Loc, Range, OEK_Variable,
                               DiagEmitted);
  else
    reportRecursiveConstFields(S, nullptr, RTy, Loc, Range, OEK_LValue,
                               DiagEmitted);
  if (!DiagEmitted)
    reportConstAssignment(S, E, Loc);
}
} // namespace

namespace neverc {
bool checkForModifiableLvalue(Expr *E, SourceLocation Loc, Sema &S) {
  assert(!E->hasPlaceholderType(BuiltinType::PseudoObject));

  SourceLocation OrigLoc = Loc;
  Expr::isModifiableLvalueResult IsLV = E->isModifiableLvalue(S.Context, &Loc);
  if (IsLV == Expr::MLV_Valid)
    return false;

  unsigned DiagID = 0;
  bool NeedType = false;
  switch (IsLV) { // C99 6.5.16p2
  case Expr::MLV_ConstQualified:
    reportConstAssignment(S, E, Loc);
    return true;
  case Expr::MLV_ConstQualifiedField:
    reportRecursiveConstFields(S, E, Loc);
    return true;
  case Expr::MLV_ArrayType:
  case Expr::MLV_ArrayTemporary:
    DiagID = diag::err_typecheck_array_not_modifiable_lvalue;
    NeedType = true;
    break;
  case Expr::MLV_NotObjectType:
    DiagID = diag::err_typecheck_non_object_not_modifiable_lvalue;
    NeedType = true;
    break;
  case Expr::MLV_LValueCast:
    DiagID = diag::err_typecheck_lvalue_casts_not_supported;
    break;
  case Expr::MLV_Valid:
    llvm_unreachable("did not take early return for MLV_Valid");
  case Expr::MLV_InvalidExpression:
  case Expr::MLV_RecordTemporary:
    DiagID = diag::err_typecheck_expression_not_modifiable_lvalue;
    break;
  case Expr::MLV_IncompleteType:
  case Expr::MLV_IncompleteVoidType:
    return S.RequireCompleteType(
        Loc, E->getType(),
        diag::err_typecheck_incomplete_type_not_modifiable_lvalue, E);
  case Expr::MLV_DuplicateVectorComponents:
    DiagID = diag::err_typecheck_duplicate_vector_components_not_mlvalue;
    break;
  }

  SourceRange Assign;
  if (Loc != OrigLoc)
    Assign = SourceRange(OrigLoc, OrigLoc);
  if (NeedType)
    S.Diag(Loc, DiagID) << E->getType() << E->getSourceRange() << Assign;
  else
    S.Diag(Loc, DiagID) << E->getSourceRange() << Assign;
  return true;
}
} // namespace

