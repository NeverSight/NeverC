#include "neverc/Analyze/SemaFixItUtils.h"
#include "neverc/Analyze/Sema.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Tree/Core/TreeContext.h"

using namespace neverc;

// ===----------------------------------------------------------------------===
// ConversionFixItGenerator: type-conversion suggestion logic
// ===----------------------------------------------------------------------===

bool ConversionFixItGenerator::compareTypesSimple(CanQualType From,
                                                  CanQualType To, Sema &S,
                                                  SourceLocation Loc,
                                                  ExprValueKind FromVK) {
  if (!To.isAtLeastAsQualifiedAs(From))
    return false;

  // If both are pointer types, work with the pointee types.
  if (isa<PointerType>(From) && isa<PointerType>(To)) {
    From =
        S.Context.getCanonicalType((cast<PointerType>(From))->getPointeeType());
    To = S.Context.getCanonicalType((cast<PointerType>(To))->getPointeeType());
  }

  const CanQualType FromUnq = From.getUnqualifiedType();
  const CanQualType ToUnq = To.getUnqualifiedType();

  return FromUnq == ToUnq && To.isAtLeastAsQualifiedAs(From);
}

bool ConversionFixItGenerator::tryToFixConversion(const Expr *FullExpr,
                                                  const QualType FromTy,
                                                  const QualType ToTy,
                                                  Sema &S) {
  if (!FullExpr)
    return false;

  const CanQualType FromQTy = S.Context.getCanonicalType(FromTy);
  const CanQualType ToQTy = S.Context.getCanonicalType(ToTy);
  const SourceLocation Begin = FullExpr->getSourceRange().getBegin();
  const SourceLocation End =
      S.getLocForEndOfToken(FullExpr->getSourceRange().getEnd());

  // Strip the implicit casts - those are implied by the compiler, not the
  // original source code.
  const Expr *Expr = FullExpr->IgnoreImpCasts();

  bool NeedParen = true;
  if (isa<ArraySubscriptExpr>(Expr) || isa<CallExpr>(Expr) ||
      isa<DeclRefExpr>(Expr) || isa<CastExpr>(Expr) || isa<MemberExpr>(Expr) ||
      isa<ParenExpr>(FullExpr) || isa<ParenListExpr>(Expr) ||
      isa<UnaryOperator>(Expr))
    NeedParen = false;

  // Check if the argument needs to be dereferenced:
  //   (type * -> type) or (type * -> type &).
  if (const PointerType *FromPtrTy = dyn_cast<PointerType>(FromQTy)) {
    OverloadFixItKind FixKind = OFIK_Dereference;

    bool CanConvert =
        CompareTypes(S.Context.getCanonicalType(FromPtrTy->getPointeeType()),
                     ToQTy, S, Begin, VK_LValue);
    if (CanConvert) {
      // Do not suggest dereferencing a Null pointer.
      if (Expr->IgnoreParenCasts()->isNullPointerConstant(
              S.Context, Expr::NPC_ValueDependentIsNotNull))
        return false;

      if (const UnaryOperator *UO = dyn_cast<UnaryOperator>(Expr)) {
        if (UO->getOpcode() == UO_AddrOf) {
          FixKind = OFIK_RemoveTakeAddress;
          Hints.push_back(FixItHint::CreateRemoval(
              CharSourceRange::getTokenRange(Begin, Begin)));
        }
      } else if (NeedParen) {
        Hints.push_back(FixItHint::CreateInsertion(Begin, "*("));
        Hints.push_back(FixItHint::CreateInsertion(End, ")"));
      } else {
        Hints.push_back(FixItHint::CreateInsertion(Begin, "*"));
      }

      NumConversionsFixed++;
      if (NumConversionsFixed == 1)
        Kind = FixKind;
      return true;
    }
  }

  // Check if the pointer to the argument needs to be passed:
  //   (type -> type *) or (type & -> type *).
  if (const auto *ToPtrTy = dyn_cast<PointerType>(ToQTy)) {
    bool CanConvert = false;
    OverloadFixItKind FixKind = OFIK_TakeAddress;

    // Only suggest taking address of L-values.
    if (!Expr->isLValue() || Expr->getObjectKind() != OK_Ordinary)
      return false;

    // Do no take address of const pointer to get void*
    if (isa<PointerType>(FromQTy) && ToPtrTy->isVoidPointerType())
      return false;

    CanConvert = CompareTypes(S.Context.getPointerType(FromQTy), ToQTy, S,
                              Begin, VK_PRValue);
    if (CanConvert) {

      if (const UnaryOperator *UO = dyn_cast<UnaryOperator>(Expr)) {
        if (UO->getOpcode() == UO_Deref) {
          FixKind = OFIK_RemoveDereference;
          Hints.push_back(FixItHint::CreateRemoval(
              CharSourceRange::getTokenRange(Begin, Begin)));
        }
      } else if (NeedParen) {
        Hints.push_back(FixItHint::CreateInsertion(Begin, "&("));
        Hints.push_back(FixItHint::CreateInsertion(End, ")"));
      } else {
        Hints.push_back(FixItHint::CreateInsertion(Begin, "&"));
      }

      NumConversionsFixed++;
      if (NumConversionsFixed == 1)
        Kind = FixKind;
      return true;
    }
  }

  return false;
}

// ===----------------------------------------------------------------------===
// Zero-initializer literal helpers
// ===----------------------------------------------------------------------===

namespace {
bool isMacroDefined(const Sema &S, SourceLocation Loc, llvm::StringRef Name) {
  return (bool)S.PP.getMacroDefinitionAtLoc(
      &S.getTreeContext().Idents.get(Name), Loc);
}

std::string getScalarZeroExpressionForType(const Type &T, SourceLocation Loc,
                                           const Sema &S) {
  assert(T.isScalarType() && "use scalar types only");
  // Suggest "0" for non-enumeration scalar types, unless we can find a
  // better initializer.
  if (T.isEnumeralType())
    return std::string();
  if (T.isRealFloatingType())
    return "0.0";
  if (T.isBooleanType() && isMacroDefined(S, Loc, "false"))
    return "false";
  if (T.isPointerType()) {
    if (isMacroDefined(S, Loc, "NULL"))
      return "NULL";
  }
  if (T.isCharType())
    return "'\\0'";
  if (T.isWideCharType())
    return "L'\\0'";
  if (T.isChar16Type())
    return "u'\\0'";
  if (T.isChar32Type())
    return "U'\\0'";
  return "0";
}
} // namespace

// ===----------------------------------------------------------------------===
// Sema entry points
// ===----------------------------------------------------------------------===

std::string Sema::getFixItZeroInitializerForType(QualType T,
                                                 SourceLocation Loc) const {
  if (T->isScalarType()) {
    std::string s = getScalarZeroExpressionForType(*T, Loc, *this);
    if (!s.empty())
      s = " = " + s;
    return s;
  }

  return std::string();
}

std::string Sema::getFixItZeroLiteralForType(QualType T,
                                             SourceLocation Loc) const {
  return getScalarZeroExpressionForType(*T, Loc, *this);
}
