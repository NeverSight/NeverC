//===- SemaDeclDefEnum.cpp - Enum declaration semantics ---===//

#include "SemaDeclUtils.h"
#include "Type/TypeLocBuilder.h"
#include "neverc/Analyze/Initialization.h"
#include "neverc/Analyze/ScopeInfo.h"
#include "neverc/Analyze/SemaInternal.h"
#include "neverc/Foundation/Attr/AttributeCommonInfo.h"
#include "neverc/Foundation/Builtin/BuiltinString.h"
#include "neverc/Foundation/Diagnostic/DiagnosticSema.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Scan/IncludeResolver.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Scan/SourceScanner.h"
#include "neverc/Tree/Core/CharUnits.h"
#include "neverc/Tree/Core/TreeConsumer.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <optional>
#include <unordered_map>

using namespace neverc;
using namespace sema;

// ===----------------------------------------------------------------------===
// Enum constants & enum body
// ===----------------------------------------------------------------------===

EnumConstantDecl *Sema::CheckEnumConstant(EnumDecl *Enum,
                                          EnumConstantDecl *LastEnumConst,
                                          SourceLocation IdLoc,
                                          IdentifierInfo *Id, Expr *Val) {
  unsigned IntWidth = Context.getTargetInfo().getIntWidth();
  llvm::APSInt EnumVal(IntWidth);
  QualType EltTy;

  if (Val)
    Val = DefaultLvalueConversion(Val).get();

  if (Val) {
    if (Val->containsErrors())
      EltTy = Context.DependentTy;
    else {
      if (!(Val = VerifyIntegerConstantExpression(Val, &EnumVal, AllowFold)
                      .get())) {
        // C99 6.7.2.2p2: Make sure we have an integer constant expression.
      } else {
        if (Enum->isComplete()) {
          EltTy = Enum->getIntegerType();

          // MSVC mode: value must fit the fixed underlying type.
          if (!isRepresentableIntegerValue(Context, EnumVal, EltTy)) {
            if (Context.getTargetInfo()
                    .getTriple()
                    .isWindowsMSVCEnvironment()) {
              Diag(IdLoc, diag::ext_enumerator_too_large) << EltTy;
            } else {
              Diag(IdLoc, diag::err_enumerator_too_large) << EltTy;
            }
          }

          // Cast to the underlying type.
          Val = ImpCastExprToType(Val, EltTy,
                                  EltTy->isBooleanType() ? CK_IntegralToBoolean
                                                         : CK_IntegralCast)
                    .get();
        } else {
          // C99 6.7.2.2p2:
          //   The expression that defines the value of an enumeration constant
          //   shall be an integer constant expression that has a value
          //   representable as an int.

          // Complain if the value is not representable in an int.
          if (!isRepresentableIntegerValue(Context, EnumVal, Context.IntTy))
            Diag(IdLoc, diag::ext_enum_value_not_int)
                << toString(EnumVal, 10) << Val->getSourceRange()
                << (EnumVal.isUnsigned() || EnumVal.isNonNegative());
          else if (!Context.hasSameType(Val->getType(), Context.IntTy)) {
            // Force the type of the expression to 'int'.
            Val = ImpCastExprToType(Val, Context.IntTy, CK_IntegralCast).get();
          }
          EltTy = Val->getType();
        }
      }
    }
  }

  if (!Val) {
    if (!LastEnumConst) {
      // First enumerator with no `=`: type is `int` when unfixed (GCC/NeverC;
      // C99 6.7.2.2p3).
      if (Enum->isFixed()) {
        EltTy = Enum->getIntegerType();
      } else {
        EltTy = Context.IntTy;
      }
    } else {
      // Assign the last value + 1.
      EnumVal = LastEnumConst->getInitVal();
      ++EnumVal;
      EltTy = LastEnumConst->getType();

      // Check for overflow on increment.
      if (EnumVal < LastEnumConst->getInitVal()) {
        // Implicit successor: widen integral type if `last+1` does not fit.
        QualType T = getNextLargerIntegralType(Context, EltTy);
        if (T.isNull() || Enum->isFixed()) {
          // There is no integral type larger enough to represent this
          // value. Complain, then allow the value to wrap around.
          EnumVal = LastEnumConst->getInitVal();
          EnumVal = EnumVal.zext(EnumVal.getBitWidth() * 2);
          ++EnumVal;
          if (Enum->isFixed())
            // When the underlying type is fixed, this is ill-formed.
            Diag(IdLoc, diag::err_enumerator_wrapped)
                << toString(EnumVal, 10) << EltTy;
          else
            Diag(IdLoc, diag::ext_enumerator_increment_too_large)
                << toString(EnumVal, 10);
        } else {
          EltTy = T;
        }

        // Retrieve the last enumerator's value, extent that type to the
        // type that is supposed to be large enough to represent the incremented
        // value, then increment.
        EnumVal = LastEnumConst->getInitVal();
        EnumVal.setIsSigned(EltTy->isSignedIntegerOrEnumerationType());
        EnumVal = EnumVal.zextOrTrunc(Context.getIntWidth(EltTy));
        ++EnumVal;

        // C99 6.7.2.2p2 (int representability); GCC allows wider fixed types.
        if (!T.isNull())
          Diag(IdLoc, diag::warn_enum_value_overflow);
      } else if (!isRepresentableIntegerValue(Context, EnumVal, EltTy)) {
        // Enforce C99 6.7.2.2p2 even when we compute the next value.
        Diag(IdLoc, diag::ext_enum_value_not_int) << toString(EnumVal, 10) << 1;
      }
    }
  }

  if (!EltTy.isNull()) {
    // Make the enumerator value match the signedness and size of the
    // enumerator's type.
    EnumVal = EnumVal.extOrTrunc(Context.getIntWidth(EltTy));
    EnumVal.setIsSigned(EltTy->isSignedIntegerOrEnumerationType());
  }

  return EnumConstantDecl::Create(Context, Enum, IdLoc, Id, EltTy, Val,
                                  EnumVal);
}

Decl *Sema::OnEnumConstant(Scope *S, Decl *theEnumDecl, Decl *lastEnumConst,
                           SourceLocation IdLoc, IdentifierInfo *Id,
                           const ParsedAttributesView &Attrs,
                           SourceLocation EqualLoc, Expr *Val) {
  EnumDecl *TheEnumDecl = cast<EnumDecl>(theEnumDecl);
  EnumConstantDecl *LastEnumConst =
      cast_or_null<EnumConstantDecl>(lastEnumConst);

  // The scope passed in may not be a decl scope.  Zip up the scope tree until
  // we find one that is.
  S = getNonFieldDeclScope(S);

  // Verify that there isn't already something declared with this name in this
  // scope.
  LookupResult R(*this, Id, IdLoc, ResolveOrdinary, ForVisibleRedeclaration);
  ResolveName(R, S);
  NamedDecl *PrevDecl = R.getAsSingle<NamedDecl>();

  // Enumerator must not collide with a member name (enum in a struct).
  EnumConstantDecl *New =
      CheckEnumConstant(TheEnumDecl, LastEnumConst, IdLoc, Id, Val);
  if (!New)
    return nullptr;

  if (PrevDecl) {
    if (isa<ValueDecl>(PrevDecl))
      CheckShadow(New, PrevDecl, R);

    assert(!isa<TagDecl>(PrevDecl) &&
           "unexpected TagDecl as previous enumerator decl");
    if (isDeclInScope(PrevDecl, CurContext, S)) {
      // The BuiltinString prelude may have planted an enum constant in
      // <built-in>.  Let the user's real definition silently supersede
      // the synthetic one; drop the old enumerator from scope and from
      // its enclosing enum so later lookups bind to the source-provided
      // constant.
      bool BuiltinPriorEnum = isa<EnumConstantDecl>(PrevDecl) &&
                              Context.getSourceManager().isWrittenInBuiltinFile(
                                  PrevDecl->getLocation());
      if (BuiltinPriorEnum) {
        Scope *EnumScope = getNonFieldDeclScope(S);
        if (EnumScope->isDeclScope(PrevDecl))
          EnumScope->RemoveDecl(PrevDecl);
        IdResolver.RemoveDecl(PrevDecl);
        if (auto *OldEnum =
                dyn_cast<EnumDecl>(PrevDecl->getLexicalDeclContext()))
          OldEnum->removeDecl(PrevDecl);
      } else {
        if (isa<EnumConstantDecl>(PrevDecl))
          Diag(IdLoc, diag::err_redefinition_of_enumerator) << Id;
        else
          Diag(IdLoc, diag::err_redefinition) << Id;
        notePreviousDefinition(PrevDecl, IdLoc);
        return nullptr;
      }
    }
  }

  ProcessDeclAttributeList(S, New, Attrs);
  AddPragmaAttributes(S, New);
  New->setAccess(TheEnumDecl->getAccess());
  PushOnScopeChains(New, S);

  return New;
}

// Returns true when the enum initial expression does not trigger the
// duplicate enum warning.  A few common cases are exempted as follows:
// Element2 = Element1
// Element2 = Element1 + 1
// Element2 = Element1 - 1
// Where Element2 and Element1 are from the same enum.
namespace {
bool validDuplicateEnum(EnumConstantDecl *ECD, EnumDecl *Enum) {
  Expr *InitExpr = ECD->getInitExpr();
  if (!InitExpr)
    return true;
  InitExpr = InitExpr->IgnoreImpCasts();

  if (BinaryOperator *BO = dyn_cast<BinaryOperator>(InitExpr)) {
    if (!BO->isAdditiveOp())
      return true;
    IntegerLiteral *IL = dyn_cast<IntegerLiteral>(BO->getRHS());
    if (!IL)
      return true;
    if (IL->getValue() != 1)
      return true;

    InitExpr = BO->getLHS();
  }

  // This checks if the elements are from the same enum.
  DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(InitExpr);
  if (!DRE)
    return true;

  EnumConstantDecl *EnumConstant = dyn_cast<EnumConstantDecl>(DRE->getDecl());
  if (!EnumConstant)
    return true;

  if (cast<EnumDecl>(TagDecl::castFromDeclContext(ECD->getDeclContext())) !=
      Enum)
    return true;

  return false;
}
} // namespace

// Emits a warning when an element is implicitly set a value that
// a previous element has already been set to.
namespace {
void checkForDuplicateEnumValues(Sema &S, llvm::ArrayRef<Decl *> Elements,
                                 EnumDecl *Enum, QualType EnumType) {
  // Avoid anonymous enums
  if (!Enum->getIdentifier())
    return;

  // Only check for small enums.
  if (Enum->getNumPositiveBits() > 63 || Enum->getNumNegativeBits() > 64)
    return;

  if (S.Diags.isIgnored(diag::warn_duplicate_enum_values, Enum->getLocation()))
    return;

  using ECDVector = llvm::SmallVector<EnumConstantDecl *, 3>;
  using DuplicatesVector = llvm::SmallVector<std::unique_ptr<ECDVector>, 3>;

  using DeclOrVector = llvm::PointerUnion<EnumConstantDecl *, ECDVector *>;

  // DenseMaps cannot contain the all ones int64_t value, so use unordered_map.
  using ValueToVectorMap = std::unordered_map<int64_t, DeclOrVector>;

  // Use int64_t as a key to avoid needing special handling for map keys.
  auto EnumConstantToKey = [](const EnumConstantDecl *D) {
    llvm::APSInt Val = D->getInitVal();
    return Val.isSigned() ? Val.getSExtValue() : Val.getZExtValue();
  };

  DuplicatesVector DupVector;
  ValueToVectorMap EnumMap;

  // Populate the EnumMap with all values represented by enum constants without
  // an initializer.
  for (auto *Element : Elements) {
    EnumConstantDecl *ECD = cast_or_null<EnumConstantDecl>(Element);

    // Null EnumConstantDecl means a previous diagnostic has been emitted for
    // this constant.  Skip this enum since it may be ill-formed.
    if (!ECD) {
      return;
    }

    // Constants with initializers are handled in the next loop.
    if (ECD->getInitExpr())
      continue;

    // Duplicate values are handled in the next loop.
    EnumMap.insert({EnumConstantToKey(ECD), ECD});
  }

  if (EnumMap.size() == 0)
    return;

  for (auto *Element : Elements) {
    // The last loop returned if any constant was null.
    EnumConstantDecl *ECD = cast<EnumConstantDecl>(Element);
    if (!validDuplicateEnum(ECD, Enum))
      continue;

    auto Iter = EnumMap.find(EnumConstantToKey(ECD));
    if (Iter == EnumMap.end())
      continue;

    DeclOrVector &Entry = Iter->second;
    if (EnumConstantDecl *D = Entry.dyn_cast<EnumConstantDecl *>()) {
      // Ensure constants are different.
      if (D == ECD)
        continue;

      auto Vec = std::make_unique<ECDVector>();
      Vec->push_back(D);
      Vec->push_back(ECD);

      // Update entry to point to the duplicates vector.
      Entry = Vec.get();

      // Store the vector somewhere we can consult later for quick emission of
      // diagnostics.
      DupVector.emplace_back(std::move(Vec));
      continue;
    }

    ECDVector *Vec = Entry.get<ECDVector *>();
    // Make sure constants are not added more than once.
    if (*Vec->begin() == ECD)
      continue;

    Vec->push_back(ECD);
  }

  for (const auto &Vec : DupVector) {
    assert(Vec->size() > 1 && "ECDVector should have at least 2 elements.");

    auto *FirstECD = Vec->front();
    S.Diag(FirstECD->getLocation(), diag::warn_duplicate_enum_values)
        << FirstECD << toString(FirstECD->getInitVal(), 10)
        << FirstECD->getSourceRange();

    for (auto *ECD : llvm::drop_begin(*Vec))
      S.Diag(ECD->getLocation(), diag::note_duplicate_element)
          << ECD << toString(ECD->getInitVal(), 10) << ECD->getSourceRange();
  }
}
} // namespace

bool Sema::IsValueInFlagEnum(const EnumDecl *ED, const llvm::APInt &Val,
                             bool AllowMask) const {
  assert(ED->isClosedFlag() && "looking for value in non-flag or open enum");
  assert(ED->isCompleteDefinition() && "expected enum definition");

  auto R = FlagBitsCache.insert(std::make_pair(ED, llvm::APInt()));
  llvm::APInt &FlagBits = R.first->second;

  if (R.second) {
    for (auto *E : ED->enumerators()) {
      const auto &EVal = E->getInitVal();
      // Only single-bit enumerators introduce new flag values.
      if (EVal.isPowerOf2())
        FlagBits = FlagBits.zext(EVal.getBitWidth()) | EVal;
    }
  }

  // A value is in a flag enum if either its bits are a subset of the enum's
  // flag bits (the first condition) or we are allowing masks and the same is
  // true of its complement (the second condition). When masks are allowed, we
  // allow the common idiom of ~(enum1 | enum2) to be a valid enum value.
  //
  // While it's true that any value could be used as a mask, the assumption is
  // that a mask will have all of the insignificant bits set. Anything else is
  // likely a logic error.
  llvm::APInt FlagMask = ~FlagBits.zextOrTrunc(Val.getBitWidth());
  return !(FlagMask & Val) || (AllowMask && !(FlagMask & ~Val));
}

void Sema::OnEnumBody(SourceLocation EnumLoc, SourceRange BraceRange,
                      Decl *EnumDeclX, llvm::ArrayRef<Decl *> Elements,
                      Scope *S, const ParsedAttributesView &Attrs) {
  EnumDecl *Enum = cast<EnumDecl>(EnumDeclX);
  QualType EnumType = Context.getTypeDeclType(Enum);

  ProcessDeclAttributeList(S, Enum, Attrs);

  // If the result value doesn't fit in an int, it must be a long or long
  // long value.  ISO C does not support this, but GCC does as an extension,
  // emit a warning.
  unsigned IntWidth = Context.getTargetInfo().getIntWidth();
  unsigned CharWidth = Context.getTargetInfo().getCharWidth();
  unsigned ShortWidth = Context.getTargetInfo().getShortWidth();

  // Verify that all the values are okay, compute the size of the values, and
  // reverse the list.
  unsigned NumNegativeBits = 0;
  unsigned NumPositiveBits = 0;

  for (unsigned i = 0, e = Elements.size(); i != e; ++i) {
    EnumConstantDecl *ECD = cast_or_null<EnumConstantDecl>(Elements[i]);
    if (!ECD)
      continue; // Already issued a diagnostic.

    const llvm::APSInt &InitVal = ECD->getInitVal();

    // Keep track of the size of positive and negative values.
    if (InitVal.isUnsigned() || InitVal.isNonNegative()) {
      // If the enumerator is zero that should still be counted as a positive
      // bit since we need a bit to store the value zero.
      unsigned ActiveBits = InitVal.getActiveBits();
      NumPositiveBits = std::max({NumPositiveBits, ActiveBits, 1u});
    } else {
      NumNegativeBits =
          std::max(NumNegativeBits, (unsigned)InitVal.getSignificantBits());
    }
  }

  // Empty enumerator-list is treated as if it had a single 0-valued constant.
  if (!NumPositiveBits && !NumNegativeBits)
    NumPositiveBits = 1;

  // Figure out the type that should be used for this enum.
  QualType BestType;
  unsigned BestWidth;

  // Pick the smallest promoted integral type that fits all values (GCC
  // -fshort-enums etc.); C99 constants otherwise behave like int.
  QualType BestPromotionType;

  bool Packed = Enum->hasAttr<PackedAttr>();
  // -fshort-enums is the equivalent to specifying the packed attribute on all
  // enum definitions.
  if (LangOpts.ShortEnums)
    Packed = true;

  // If the enum already has a type because it is fixed or dictated by the
  // target, promote that type instead of analyzing the enumerators.
  if (Enum->isComplete()) {
    BestType = Enum->getIntegerType();
    if (Context.isPromotableIntegerType(BestType))
      BestPromotionType = Context.getPromotedIntegerType(BestType);
    else
      BestPromotionType = BestType;

    BestWidth = Context.getIntWidth(BestType);
  } else if (NumNegativeBits) {
    // If there is a negative value, figure out the smallest integer type (of
    // int/long/longlong) that fits.
    // If it's packed, check also if it fits a char or a short.
    if (Packed && NumNegativeBits <= CharWidth && NumPositiveBits < CharWidth) {
      BestType = Context.SignedCharTy;
      BestWidth = CharWidth;
    } else if (Packed && NumNegativeBits <= ShortWidth &&
               NumPositiveBits < ShortWidth) {
      BestType = Context.ShortTy;
      BestWidth = ShortWidth;
    } else if (NumNegativeBits <= IntWidth && NumPositiveBits < IntWidth) {
      BestType = Context.IntTy;
      BestWidth = IntWidth;
    } else {
      BestWidth = Context.getTargetInfo().getLongWidth();

      if (NumNegativeBits <= BestWidth && NumPositiveBits < BestWidth) {
        BestType = Context.LongTy;
      } else {
        BestWidth = Context.getTargetInfo().getLongLongWidth();

        if (NumNegativeBits > BestWidth || NumPositiveBits >= BestWidth)
          Diag(Enum->getLocation(), diag::ext_enum_too_large);
        BestType = Context.LongLongTy;
      }
    }
    BestPromotionType = (BestWidth <= IntWidth ? Context.IntTy : BestType);
  } else {
    // If there is no negative value, figure out the smallest type that fits
    // all of the enumerator values.
    // If it's packed, check also if it fits a char or a short.
    if (Packed && NumPositiveBits <= CharWidth) {
      BestType = Context.UnsignedCharTy;
      BestPromotionType = Context.IntTy;
      BestWidth = CharWidth;
    } else if (Packed && NumPositiveBits <= ShortWidth) {
      BestType = Context.UnsignedShortTy;
      BestPromotionType = Context.IntTy;
      BestWidth = ShortWidth;
    } else if (NumPositiveBits <= IntWidth) {
      BestType = Context.UnsignedIntTy;
      BestWidth = IntWidth;
      BestPromotionType = Context.UnsignedIntTy;
    } else if (NumPositiveBits <=
               (BestWidth = Context.getTargetInfo().getLongWidth())) {
      BestType = Context.UnsignedLongTy;
      BestPromotionType = Context.UnsignedLongTy;
    } else {
      BestWidth = Context.getTargetInfo().getLongLongWidth();
      assert(NumPositiveBits <= BestWidth &&
             "How could an initializer get larger than ULL?");
      BestType = Context.UnsignedLongLongTy;
      BestPromotionType = Context.UnsignedLongLongTy;
    }
  }

  for (auto *D : Elements) {
    auto *ECD = cast_or_null<EnumConstantDecl>(D);
    if (!ECD)
      continue; // Already issued a diagnostic.

    // Standard C says the enumerators have int type, but we allow, as an
    // extension, the enumerators to be larger than int size.  If each
    // enumerator value fits in an int, type it as an int, otherwise type it the
    // same as the enumerator decl itself.  This means that in "enum { X = 1U }"
    // that X has type 'int', not 'unsigned'.

    llvm::APSInt InitVal = ECD->getInitVal();

    // If it fits into an integer type, force it.  Otherwise force it to match
    // the enum decl type.
    QualType NewTy;
    unsigned NewWidth;
    bool NewSign;
    if (!Enum->isFixed() &&
        isRepresentableIntegerValue(Context, InitVal, Context.IntTy)) {
      NewTy = Context.IntTy;
      NewWidth = IntWidth;
      NewSign = true;
    } else if (ECD->getType() == BestType) {
      continue;
    } else {
      NewTy = BestType;
      NewWidth = BestWidth;
      NewSign = BestType->isSignedIntegerOrEnumerationType();
    }

    // Adjust the APSInt value.
    InitVal = InitVal.extOrTrunc(NewWidth);
    InitVal.setIsSigned(NewSign);
    ECD->setInitVal(InitVal);

    // Adjust the Expr initializer and type.
    if (ECD->getInitExpr() &&
        !Context.hasSameType(NewTy, ECD->getInitExpr()->getType()))
      ECD->setInitExpr(ImplicitCastExpr::Create(Context, NewTy, CK_IntegralCast,
                                                ECD->getInitExpr(), VK_PRValue,
                                                FPOptionsOverride()));
    ECD->setType(NewTy);
  }

  Enum->completeDefinition(BestType, BestPromotionType, NumPositiveBits,
                           NumNegativeBits);

  checkForDuplicateEnumValues(*this, Elements, Enum, EnumType);

  if (Enum->isClosedFlag()) {
    for (Decl *D : Elements) {
      EnumConstantDecl *ECD = cast_or_null<EnumConstantDecl>(D);
      if (!ECD)
        continue; // Already issued a diagnostic.

      llvm::APSInt InitVal = ECD->getInitVal();
      if (InitVal != 0 && !InitVal.isPowerOf2() &&
          !IsValueInFlagEnum(Enum, InitVal, true))
        Diag(ECD->getLocation(), diag::warn_flag_enum_constant_out_of_range)
            << ECD << Enum;
    }
  }

  // Now that the enum type is defined, ensure it's not been underaligned.
  if (Enum->hasAttrs())
    CheckAlignasUnderalignment(Enum);
}
