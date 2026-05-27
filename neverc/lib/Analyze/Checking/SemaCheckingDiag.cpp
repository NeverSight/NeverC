#include "SemaCheckingUtils.h"
#include "neverc/Analyze/Initialization.h"
#include "neverc/Analyze/ScopeInfo.h"
#include "neverc/Foundation/Builtin/TargetBuiltins.h"
#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Tree/Core/APValue.h"
#include "neverc/Tree/Core/CharUnits.h"
#include "neverc/Tree/Expr/EvaluatedExprVisitor.h"
#include "neverc/Tree/Type/StructLayout.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include "neverc/Analyze/SemaInternal.h"
#include "neverc/Foundation/Diagnostic/DiagnosticSema.h"

using namespace neverc;
using namespace sema;

//===--- CHECK: Warn on use of wrong absolute value function. -------------===//

// Returns the related absolute value function that is larger, of 0 if one
// does not exist.
unsigned getLargerAbsoluteValueFunction(unsigned AbsFunction) {
  switch (AbsFunction) {
  default:
    return 0;

  case Builtin::BI__builtin_abs:
    return Builtin::BI__builtin_labs;
  case Builtin::BI__builtin_labs:
    return Builtin::BI__builtin_llabs;
  case Builtin::BI__builtin_llabs:
    return 0;

  case Builtin::BI__builtin_fabsf:
    return Builtin::BI__builtin_fabs;
  case Builtin::BI__builtin_fabs:
    return Builtin::BI__builtin_fabsl;
  case Builtin::BI__builtin_fabsl:
    return 0;

  case Builtin::BI__builtin_cabsf:
    return Builtin::BI__builtin_cabs;
  case Builtin::BI__builtin_cabs:
    return Builtin::BI__builtin_cabsl;
  case Builtin::BI__builtin_cabsl:
    return 0;

  case Builtin::BIabs:
    return Builtin::BIlabs;
  case Builtin::BIlabs:
    return Builtin::BIllabs;
  case Builtin::BIllabs:
    return 0;

  case Builtin::BIfabsf:
    return Builtin::BIfabs;
  case Builtin::BIfabs:
    return Builtin::BIfabsl;
  case Builtin::BIfabsl:
    return 0;

  case Builtin::BIcabsf:
    return Builtin::BIcabs;
  case Builtin::BIcabs:
    return Builtin::BIcabsl;
  case Builtin::BIcabsl:
    return 0;
  }
}

// Returns the argument type of the absolute value function.
QualType getAbsoluteValueArgumentType(TreeContext &Context, unsigned AbsType) {
  if (AbsType == 0)
    return QualType();

  TreeContext::GetBuiltinTypeError Error = TreeContext::GE_None;
  QualType BuiltinType = Context.GetBuiltinType(AbsType, Error);
  if (Error != TreeContext::GE_None)
    return QualType();

  const FunctionProtoType *FT = BuiltinType->getAs<FunctionProtoType>();
  if (!FT)
    return QualType();

  if (FT->getNumParams() != 1)
    return QualType();

  return FT->getParamType(0);
}

// Returns the best absolute value function, or zero, based on type and
// current absolute value function.
unsigned getBestAbsFunction(TreeContext &Context, QualType ArgType,
                            unsigned AbsFunctionKind) {
  unsigned BestKind = 0;
  uint64_t ArgSize = Context.getTypeSize(ArgType);
  for (unsigned Kind = AbsFunctionKind; Kind != 0;
       Kind = getLargerAbsoluteValueFunction(Kind)) {
    QualType ParamType = getAbsoluteValueArgumentType(Context, Kind);
    if (Context.getTypeSize(ParamType) >= ArgSize) {
      if (BestKind == 0)
        BestKind = Kind;
      else if (Context.hasSameType(ParamType, ArgType)) {
        BestKind = Kind;
        break;
      }
    }
  }
  return BestKind;
}

enum AbsoluteValueKind { AVK_Integer, AVK_Floating, AVK_Complex };

AbsoluteValueKind getAbsoluteValueKind(QualType T) {
  if (T->isIntegralOrEnumerationType())
    return AVK_Integer;
  if (T->isRealFloatingType())
    return AVK_Floating;
  if (T->isAnyComplexType())
    return AVK_Complex;

  llvm_unreachable("Type not integer, floating, or complex");
}

// Changes the absolute value function to a different type.  Preserves whether
// the function is a builtin.
unsigned changeAbsFunction(unsigned AbsKind, AbsoluteValueKind ValueKind) {
  switch (ValueKind) {
  case AVK_Integer:
    switch (AbsKind) {
    default:
      return 0;
    case Builtin::BI__builtin_fabsf:
    case Builtin::BI__builtin_fabs:
    case Builtin::BI__builtin_fabsl:
    case Builtin::BI__builtin_cabsf:
    case Builtin::BI__builtin_cabs:
    case Builtin::BI__builtin_cabsl:
      return Builtin::BI__builtin_abs;
    case Builtin::BIfabsf:
    case Builtin::BIfabs:
    case Builtin::BIfabsl:
    case Builtin::BIcabsf:
    case Builtin::BIcabs:
    case Builtin::BIcabsl:
      return Builtin::BIabs;
    }
  case AVK_Floating:
    switch (AbsKind) {
    default:
      return 0;
    case Builtin::BI__builtin_abs:
    case Builtin::BI__builtin_labs:
    case Builtin::BI__builtin_llabs:
    case Builtin::BI__builtin_cabsf:
    case Builtin::BI__builtin_cabs:
    case Builtin::BI__builtin_cabsl:
      return Builtin::BI__builtin_fabsf;
    case Builtin::BIabs:
    case Builtin::BIlabs:
    case Builtin::BIllabs:
    case Builtin::BIcabsf:
    case Builtin::BIcabs:
    case Builtin::BIcabsl:
      return Builtin::BIfabsf;
    }
  case AVK_Complex:
    switch (AbsKind) {
    default:
      return 0;
    case Builtin::BI__builtin_abs:
    case Builtin::BI__builtin_labs:
    case Builtin::BI__builtin_llabs:
    case Builtin::BI__builtin_fabsf:
    case Builtin::BI__builtin_fabs:
    case Builtin::BI__builtin_fabsl:
      return Builtin::BI__builtin_cabsf;
    case Builtin::BIabs:
    case Builtin::BIlabs:
    case Builtin::BIllabs:
    case Builtin::BIfabsf:
    case Builtin::BIfabs:
    case Builtin::BIfabsl:
      return Builtin::BIcabsf;
    }
  }
  llvm_unreachable("Unable to convert function");
}

unsigned getAbsoluteValueFunctionKind(const FunctionDecl *FDecl) {
  const IdentifierInfo *FnInfo = FDecl->getIdentifier();
  if (!FnInfo)
    return 0;

  switch (FDecl->getBuiltinID()) {
  default:
    return 0;
  case Builtin::BI__builtin_abs:
  case Builtin::BI__builtin_fabs:
  case Builtin::BI__builtin_fabsf:
  case Builtin::BI__builtin_fabsl:
  case Builtin::BI__builtin_labs:
  case Builtin::BI__builtin_llabs:
  case Builtin::BI__builtin_cabs:
  case Builtin::BI__builtin_cabsf:
  case Builtin::BI__builtin_cabsl:
  case Builtin::BIabs:
  case Builtin::BIlabs:
  case Builtin::BIllabs:
  case Builtin::BIfabs:
  case Builtin::BIfabsf:
  case Builtin::BIfabsl:
  case Builtin::BIcabs:
  case Builtin::BIcabsf:
  case Builtin::BIcabsl:
    return FDecl->getBuiltinID();
  }
  llvm_unreachable("Unknown Builtin type");
}

// If the replacement is valid, emit a note with replacement function.
// Additionally, suggest including the proper header if not already included.
void emitReplacement(Sema &S, SourceLocation Loc, SourceRange Range,
                     unsigned AbsKind, QualType ArgType) {
  bool GenHeaderHint = true;
  const char *HeaderName = nullptr;
  llvm::StringRef FunctionName = S.Context.BuiltinInfo.getName(AbsKind);
  HeaderName = S.Context.BuiltinInfo.getHeaderName(AbsKind);

  if (HeaderName) {
    DeclarationName DN(&S.Context.Idents.get(FunctionName));
    LookupResult R(S, DN, Loc, neverc::ResolveAny);
    R.suppressDiagnostics();
    S.ResolveName(R, S.getCurScope());

    if (R.isSingleResult()) {
      FunctionDecl *FD = dyn_cast<FunctionDecl>(R.getFoundDecl());
      if (FD && FD->getBuiltinID() == AbsKind) {
        GenHeaderHint = false;
      } else {
        return;
      }
    } else if (!R.empty()) {
      return;
    }
  }

  S.Diag(Loc, diag::note_replace_abs_function)
      << FunctionName << FixItHint::CreateReplacement(Range, FunctionName);

  if (!HeaderName)
    return;

  if (!GenHeaderHint)
    return;

  S.Diag(Loc, diag::note_include_header_or_declare)
      << HeaderName << FunctionName;
}

// ===----------------------------------------------------------------------===
// Diagnostics: abs, alignment, type tags, elementwise math & matrix
// ===----------------------------------------------------------------------===

void Sema::CheckAbsoluteValueFunction(const CallExpr *Call,
                                      const FunctionDecl *FDecl) {
  if (Call->getNumArgs() != 1)
    return;

  unsigned AbsKind = getAbsoluteValueFunctionKind(FDecl);
  if (AbsKind == 0)
    return;

  QualType ArgType = Call->getArg(0)->IgnoreParenImpCasts()->getType();
  QualType ParamType = Call->getArg(0)->getType();

  // Unsigned types cannot be negative.  Suggest removing the absolute value
  // function call.
  if (ArgType->isUnsignedIntegerType()) {
    llvm::StringRef FunctionName = Context.BuiltinInfo.getName(AbsKind);
    Diag(Call->getExprLoc(), diag::warn_unsigned_abs) << ArgType << ParamType;
    Diag(Call->getExprLoc(), diag::note_remove_abs)
        << FunctionName
        << FixItHint::CreateRemoval(Call->getCallee()->getSourceRange());
    return;
  }

  // Taking the absolute value of a pointer is very suspicious, they probably
  // wanted to index into an array, dereference a pointer, call a function, etc.
  if (ArgType->isPointerType() || ArgType->canDecayToPointerType()) {
    unsigned DiagType = 0;
    if (ArgType->isFunctionType())
      DiagType = 1;
    else if (ArgType->isArrayType())
      DiagType = 2;

    Diag(Call->getExprLoc(), diag::warn_pointer_abs) << DiagType << ArgType;
    return;
  }

  AbsoluteValueKind ArgValueKind = getAbsoluteValueKind(ArgType);
  AbsoluteValueKind ParamValueKind = getAbsoluteValueKind(ParamType);

  // The argument and parameter are the same kind.  Check if they are the right
  // size.
  if (ArgValueKind == ParamValueKind) {
    if (Context.getTypeSize(ArgType) <= Context.getTypeSize(ParamType))
      return;

    unsigned NewAbsKind = getBestAbsFunction(Context, ArgType, AbsKind);
    Diag(Call->getExprLoc(), diag::warn_abs_too_small)
        << FDecl << ArgType << ParamType;

    if (NewAbsKind == 0)
      return;

    emitReplacement(*this, Call->getExprLoc(),
                    Call->getCallee()->getSourceRange(), NewAbsKind, ArgType);
    return;
  }

  // ArgValueKind != ParamValueKind
  // The wrong type of absolute value function was used.  Attempt to find the
  // proper one.
  unsigned NewAbsKind = changeAbsFunction(AbsKind, ArgValueKind);
  NewAbsKind = getBestAbsFunction(Context, ArgType, NewAbsKind);
  if (NewAbsKind == 0)
    return;

  Diag(Call->getExprLoc(), diag::warn_wrong_absolute_value_type)
      << FDecl << ParamValueKind << ArgValueKind;

  emitReplacement(*this, Call->getExprLoc(),
                  Call->getCallee()->getSourceRange(), NewAbsKind, ArgType);
}

//===--- Layout compatibility ----------------------------------------------//

bool isLayoutCompatible(TreeContext &C, QualType T1, QualType T2);

bool isLayoutCompatible(TreeContext &C, EnumDecl *ED1, EnumDecl *ED2) {
  // Same fixed underlying type => layout-compatible enums.
  return ED1->isComplete() && ED2->isComplete() &&
         C.hasSameType(ED1->getIntegerType(), ED2->getIntegerType());
}

bool isLayoutCompatible(TreeContext &C, FieldDecl *Field1, FieldDecl *Field2) {
  if (!isLayoutCompatible(C, Field1->getType(), Field2->getType()))
    return false;

  if (Field1->isBitField() != Field2->isBitField())
    return false;

  if (Field1->isBitField()) {
    // Make sure that the bit-fields are the same length.
    unsigned Bits1 = Field1->getBitWidthValue(C);
    unsigned Bits2 = Field2->getBitWidthValue(C);

    if (Bits1 != Bits2)
      return false;
  }

  return true;
}

bool isLayoutCompatibleStruct(TreeContext &C, RecordDecl *RD1,
                              RecordDecl *RD2) {
  RecordDecl::field_iterator Field2 = RD2->field_begin(),
                             Field2End = RD2->field_end(),
                             Field1 = RD1->field_begin(),
                             Field1End = RD1->field_end();
  for (; Field1 != Field1End && Field2 != Field2End; ++Field1, ++Field2) {
    if (!isLayoutCompatible(C, *Field1, *Field2))
      return false;
  }
  if (Field1 != Field1End || Field2 != Field2End)
    return false;

  return true;
}

bool isLayoutCompatibleUnion(TreeContext &C, RecordDecl *RD1, RecordDecl *RD2) {
  llvm::SmallPtrSet<FieldDecl *, 8> UnmatchedFields;
  for (auto *Field2 : RD2->fields())
    UnmatchedFields.insert(Field2);

  for (auto *Field1 : RD1->fields()) {
    llvm::SmallPtrSet<FieldDecl *, 8>::iterator I = UnmatchedFields.begin(),
                                                E = UnmatchedFields.end();

    for (; I != E; ++I) {
      if (isLayoutCompatible(C, Field1, *I)) {
        bool Result = UnmatchedFields.erase(*I);
        (void)Result;
        assert(Result);
        break;
      }
    }
    if (I == E)
      return false;
  }

  return UnmatchedFields.empty();
}

bool isLayoutCompatible(TreeContext &C, RecordDecl *RD1, RecordDecl *RD2) {
  if (RD1->isUnion() != RD2->isUnion())
    return false;

  if (RD1->isUnion())
    return isLayoutCompatibleUnion(C, RD1, RD2);
  else
    return isLayoutCompatibleStruct(C, RD1, RD2);
}

bool isLayoutCompatible(TreeContext &C, QualType T1, QualType T2) {
  if (T1.isNull() || T2.isNull())
    return false;

  // Identical types are always layout-compatible.
  if (C.hasSameType(T1, T2))
    return true;

  T1 = T1.getCanonicalType().getUnqualifiedType();
  T2 = T2.getCanonicalType().getUnqualifiedType();

  const Type::TypeClass TC1 = T1->getTypeClass();
  const Type::TypeClass TC2 = T2->getTypeClass();

  if (TC1 != TC2)
    return false;

  if (TC1 == Type::Enum) {
    return isLayoutCompatible(C, cast<EnumType>(T1)->getDecl(),
                              cast<EnumType>(T2)->getDecl());
  } else if (TC1 == Type::Record) {
    if (!T1->isStandardLayoutType() || !T2->isStandardLayoutType())
      return false;

    return isLayoutCompatible(C, cast<RecordType>(T1)->getDecl(),
                              cast<RecordType>(T2)->getDecl());
  }

  return false;
}

//===--- CHECK: pointer_with_type_tag attribute: datatypes should match ----//

bool findTypeTagExpr(const Expr *TypeExpr, const TreeContext &Ctx,
                     const ValueDecl **VD, uint64_t *MagicValue,
                     bool isConstantEvaluated) {
  while (true) {
    if (!TypeExpr)
      return false;

    TypeExpr = TypeExpr->IgnoreParenImpCasts()->IgnoreParenCasts();

    switch (TypeExpr->getStmtClass()) {
    case Stmt::UnaryOperatorClass: {
      const UnaryOperator *UO = cast<UnaryOperator>(TypeExpr);
      if (UO->getOpcode() == UO_AddrOf || UO->getOpcode() == UO_Deref) {
        TypeExpr = UO->getSubExpr();
        continue;
      }
      return false;
    }

    case Stmt::DeclRefExprClass: {
      const DeclRefExpr *DRE = cast<DeclRefExpr>(TypeExpr);
      *VD = DRE->getDecl();
      return true;
    }

    case Stmt::IntegerLiteralClass: {
      const IntegerLiteral *IL = cast<IntegerLiteral>(TypeExpr);
      llvm::APInt MagicValueAPInt = IL->getValue();
      if (MagicValueAPInt.getActiveBits() <= 64) {
        *MagicValue = MagicValueAPInt.getZExtValue();
        return true;
      } else
        return false;
    }

    case Stmt::BinaryConditionalOperatorClass:
    case Stmt::ConditionalOperatorClass: {
      const AbstractConditionalOperator *ACO =
          cast<AbstractConditionalOperator>(TypeExpr);
      bool Result;
      if (ACO->getCond()->EvaluateAsBooleanCondition(Result, Ctx,
                                                     isConstantEvaluated)) {
        if (Result)
          TypeExpr = ACO->getTrueExpr();
        else
          TypeExpr = ACO->getFalseExpr();
        continue;
      }
      return false;
    }

    case Stmt::BinaryOperatorClass: {
      const BinaryOperator *BO = cast<BinaryOperator>(TypeExpr);
      if (BO->getOpcode() == BO_Comma) {
        TypeExpr = BO->getRHS();
        continue;
      }
      return false;
    }

    default:
      return false;
    }
  }
}

bool getMatchingCType(const IdentifierInfo *ArgumentKind, const Expr *TypeExpr,
                      const TreeContext &Ctx,
                      const llvm::DenseMap<Sema::TypeTagMagicValue,
                                           Sema::TypeTagData> *MagicValues,
                      bool &FoundWrongKind, Sema::TypeTagData &TypeInfo,
                      bool isConstantEvaluated) {
  FoundWrongKind = false;

  // Variable declaration that has type_tag_for_datatype attribute.
  const ValueDecl *VD = nullptr;

  uint64_t MagicValue;

  if (!findTypeTagExpr(TypeExpr, Ctx, &VD, &MagicValue, isConstantEvaluated))
    return false;

  if (VD) {
    if (TypeTagForDatatypeAttr *I = VD->getAttr<TypeTagForDatatypeAttr>()) {
      if (I->getArgumentKind() != ArgumentKind) {
        FoundWrongKind = true;
        return false;
      }
      TypeInfo.Type = I->getMatchingCType();
      TypeInfo.LayoutCompatible = I->getLayoutCompatible();
      TypeInfo.MustBeNull = I->getMustBeNull();
      return true;
    }
    return false;
  }

  if (!MagicValues)
    return false;

  llvm::DenseMap<Sema::TypeTagMagicValue, Sema::TypeTagData>::const_iterator I =
      MagicValues->find(std::make_pair(ArgumentKind, MagicValue));
  if (I == MagicValues->end())
    return false;

  TypeInfo = I->second;
  return true;
}

void Sema::RegisterTypeTagForDatatype(const IdentifierInfo *ArgumentKind,
                                      uint64_t MagicValue, QualType Type,
                                      bool LayoutCompatible, bool MustBeNull) {
  if (!TypeTagForDatatypeMagicValues)
    TypeTagForDatatypeMagicValues.reset(
        new llvm::DenseMap<TypeTagMagicValue, TypeTagData>);

  TypeTagMagicValue Magic(ArgumentKind, MagicValue);
  (*TypeTagForDatatypeMagicValues)[Magic] =
      TypeTagData(Type, LayoutCompatible, MustBeNull);
}

bool isSameCharType(QualType T1, QualType T2) {
  const BuiltinType *BT1 = T1->getAs<BuiltinType>();
  if (!BT1)
    return false;

  const BuiltinType *BT2 = T2->getAs<BuiltinType>();
  if (!BT2)
    return false;

  BuiltinType::Kind T1Kind = BT1->getKind();
  BuiltinType::Kind T2Kind = BT2->getKind();

  return (T1Kind == BuiltinType::SChar && T2Kind == BuiltinType::Char_S) ||
         (T1Kind == BuiltinType::UChar && T2Kind == BuiltinType::Char_U) ||
         (T1Kind == BuiltinType::Char_U && T2Kind == BuiltinType::UChar) ||
         (T1Kind == BuiltinType::Char_S && T2Kind == BuiltinType::SChar);
}

void Sema::CheckArgumentWithTypeTag(const ArgumentWithTypeTagAttr *Attr,
                                    const llvm::ArrayRef<const Expr *> ExprArgs,
                                    SourceLocation CallSiteLoc) {
  const IdentifierInfo *ArgumentKind = Attr->getArgumentKind();
  bool IsPointerAttr = Attr->getIsPointer();

  // Retrieve the argument representing the 'type_tag'.
  unsigned TypeTagIdxAST = Attr->getTypeTagIdx().getASTIndex();
  if (TypeTagIdxAST >= ExprArgs.size()) {
    Diag(CallSiteLoc, diag::err_tag_index_out_of_range)
        << 0 << Attr->getTypeTagIdx().getSourceIndex();
    return;
  }
  const Expr *TypeTagExpr = ExprArgs[TypeTagIdxAST];
  bool FoundWrongKind;
  TypeTagData TypeInfo;
  if (!getMatchingCType(ArgumentKind, TypeTagExpr, Context,
                        TypeTagForDatatypeMagicValues.get(), FoundWrongKind,
                        TypeInfo, isConstantEvaluatedContext())) {
    if (FoundWrongKind)
      Diag(TypeTagExpr->getExprLoc(),
           diag::warn_type_tag_for_datatype_wrong_kind)
          << TypeTagExpr->getSourceRange();
    return;
  }

  // Retrieve the argument representing the 'arg_idx'.
  unsigned ArgumentIdxAST = Attr->getArgumentIdx().getASTIndex();
  if (ArgumentIdxAST >= ExprArgs.size()) {
    Diag(CallSiteLoc, diag::err_tag_index_out_of_range)
        << 1 << Attr->getArgumentIdx().getSourceIndex();
    return;
  }
  const Expr *ArgumentExpr = ExprArgs[ArgumentIdxAST];
  if (IsPointerAttr) {
    // Skip implicit cast of pointer to `void *' (as a function argument).
    if (const ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(ArgumentExpr))
      if (ICE->getType()->isVoidPointerType() &&
          ICE->getCastKind() == CK_BitCast)
        ArgumentExpr = ICE->getSubExpr();
  }
  QualType ArgumentType = ArgumentExpr->getType();

  // Passing a `void*' pointer shouldn't trigger a warning.
  if (IsPointerAttr && ArgumentType->isVoidPointerType())
    return;

  if (TypeInfo.MustBeNull) {
    // Type tag with matching void type requires a null pointer.
    if (!ArgumentExpr->isNullPointerConstant(
            Context, Expr::NPC_ValueDependentIsNotNull)) {
      Diag(ArgumentExpr->getExprLoc(),
           diag::warn_type_safety_null_pointer_required)
          << ArgumentKind->getName() << ArgumentExpr->getSourceRange()
          << TypeTagExpr->getSourceRange();
    }
    return;
  }

  QualType RequiredType = TypeInfo.Type;
  if (IsPointerAttr)
    RequiredType = Context.getPointerType(RequiredType);

  bool mismatch = false;
  if (!TypeInfo.LayoutCompatible) {
    mismatch = !Context.hasSameType(ArgumentType, RequiredType);

    // `char` vs `signed char` / `unsigned char`: treat as equivalent when the
    // target's plain `char` matches the required signedness.
    if (mismatch)
      if ((IsPointerAttr && isSameCharType(ArgumentType->getPointeeType(),
                                           RequiredType->getPointeeType())) ||
          (!IsPointerAttr && isSameCharType(ArgumentType, RequiredType)))
        mismatch = false;
  } else if (IsPointerAttr)
    mismatch = !isLayoutCompatible(Context, ArgumentType->getPointeeType(),
                                   RequiredType->getPointeeType());
  else
    mismatch = !isLayoutCompatible(Context, ArgumentType, RequiredType);

  if (mismatch)
    Diag(ArgumentExpr->getExprLoc(), diag::warn_type_safety_type_mismatch)
        << ArgumentType << ArgumentKind << TypeInfo.LayoutCompatible
        << RequiredType << ArgumentExpr->getSourceRange()
        << TypeTagExpr->getSourceRange();
}

void Sema::AddPotentialMisalignedMembers(Expr *E, RecordDecl *RD, ValueDecl *MD,
                                         CharUnits Alignment) {
  MisalignedMembers.emplace_back(E, RD, MD, Alignment);
}

void Sema::DiagnoseMisalignedMembers() {
  for (MisalignedMember &m : MisalignedMembers) {
    const NamedDecl *ND = m.RD;
    if (ND->getName().empty()) {
      if (const TypedefNameDecl *TD = m.RD->getTypedefNameForAnonDecl())
        ND = TD;
    }
    Diag(m.E->getBeginLoc(), diag::warn_taking_address_of_packed_member)
        << m.MD << ND << m.E->getSourceRange();
  }
  MisalignedMembers.clear();
}

void Sema::DiscardMisalignedMemberAddress(const Type *T, Expr *E) {
  E = E->IgnoreParens();
  if (!T->isPointerType() && !T->isIntegerType())
    return;
  if (isa<UnaryOperator>(E) &&
      cast<UnaryOperator>(E)->getOpcode() == UO_AddrOf) {
    auto *Op = cast<UnaryOperator>(E)->getSubExpr()->IgnoreParens();
    if (isa<MemberExpr>(Op)) {
      auto *MA = llvm::find(MisalignedMembers, MisalignedMember(Op));
      if (MA != MisalignedMembers.end() &&
          (T->isIntegerType() ||
           (T->isPointerType() && (T->getPointeeType()->isIncompleteType() ||
                                   Context.getTypeAlignInChars(
                                       T->getPointeeType()) <= MA->Alignment))))
        MisalignedMembers.erase(MA);
    }
  }
}

void Sema::RefersToMemberWithReducedAlignment(
    Expr *E,
    llvm::function_ref<void(Expr *, RecordDecl *, FieldDecl *, CharUnits)>
        Action) {
  const auto *ME = dyn_cast<MemberExpr>(E);
  if (!ME)
    return;

  // No need to check expressions with an __unaligned-qualified type.
  if (E->getType().getQualifiers().hasUnaligned())
    return;

  // For a chain of MemberExpr like "a.b.c.d" this list
  // will keep FieldDecl's like [d, c, b].
  llvm::SmallVector<FieldDecl *, 4> ReverseMemberChain;
  const MemberExpr *TopME = nullptr;
  bool AnyIsPacked = false;
  do {
    QualType BaseType = ME->getBase()->getType();
    if (ME->isArrow())
      BaseType = BaseType->getPointeeType();
    RecordDecl *RD = BaseType->castAs<RecordType>()->getDecl();
    if (RD->isInvalidDecl())
      return;

    ValueDecl *MD = ME->getMemberDecl();
    auto *FD = dyn_cast<FieldDecl>(MD);
    // We do not care about non-data members.
    if (!FD || FD->isInvalidDecl())
      return;

    AnyIsPacked =
        AnyIsPacked || (RD->hasAttr<PackedAttr>() || MD->hasAttr<PackedAttr>());
    ReverseMemberChain.push_back(FD);

    TopME = ME;
    ME = dyn_cast<MemberExpr>(ME->getBase()->IgnoreParens());
  } while (ME);
  assert(TopME && "We did not compute a topmost MemberExpr!");

  // Not the scope of this diagnostic.
  if (!AnyIsPacked)
    return;

  const Expr *TopBase = TopME->getBase()->IgnoreParenImpCasts();
  const auto *DRE = dyn_cast<DeclRefExpr>(TopBase);
  // The innermost base of the member expression may be too complicated.
  // For now, just disregard these cases. This is left for future
  // improvement.
  if (!DRE)
    return;

  // Alignment expected by the whole expression.
  CharUnits ExpectedAlignment = Context.getTypeAlignInChars(E->getType());

  // No need to do anything else with this case.
  if (ExpectedAlignment.isOne())
    return;

  // Synthesize offset of the whole access.
  CharUnits Offset;
  for (const FieldDecl *FD : llvm::reverse(ReverseMemberChain))
    Offset += Context.toCharUnitsFromBits(Context.getFieldOffset(FD));

  // Compute the CompleteObjectAlignment as the alignment of the whole chain.
  CharUnits CompleteObjectAlignment = Context.getTypeAlignInChars(
      ReverseMemberChain.back()->getParent()->getTypeForDecl());

  // The base expression of the innermost MemberExpr may give
  // stronger guarantees than the struct/union containing the member.
  if (DRE && !TopME->isArrow()) {
    const ValueDecl *VD = DRE->getDecl();
    CompleteObjectAlignment =
        std::max(CompleteObjectAlignment, Context.getDeclAlign(VD));
  }
  if (Offset % ExpectedAlignment != 0 ||
      // It may fulfill the offset it but the effective alignment may still be
      // lower than the expected expression alignment.
      CompleteObjectAlignment < ExpectedAlignment) {
    // If this happens, we want to determine a sensible culprit of this.
    // Intuitively, watching the chain of member expressions from right to
    // left, we start with the required alignment (as required by the field
    // type) but some packed attribute in that chain has reduced the alignment.
    // It may happen that another packed structure increases it again. But if
    // we are here such increase has not been enough. So pointing the first
    // FieldDecl that either is packed or else its RecordDecl is,
    // seems reasonable.
    FieldDecl *FD = nullptr;
    CharUnits Alignment;
    for (FieldDecl *FDI : ReverseMemberChain) {
      if (FDI->hasAttr<PackedAttr>() ||
          FDI->getParent()->hasAttr<PackedAttr>()) {
        FD = FDI;
        Alignment = std::min(
            Context.getTypeAlignInChars(FD->getType()),
            Context.getTypeAlignInChars(FD->getParent()->getTypeForDecl()));
        break;
      }
    }
    assert(FD && "We did not find a packed FieldDecl!");
    Action(E, FD->getParent(), FD, Alignment);
  }
}

void Sema::CheckAddressOfPackedMember(Expr *rhs) {
  using namespace std::placeholders;

  RefersToMemberWithReducedAlignment(
      rhs, std::bind(&Sema::AddPotentialMisalignedMembers, std::ref(*this), _1,
                     _2, _3, _4));
}

bool Sema::PrepareBuiltinElementwiseMathOneArgCall(CallExpr *TheCall) {
  if (checkArgCount(*this, TheCall, 1))
    return true;

  ExprResult A = UsualUnaryConversions(TheCall->getArg(0));
  if (A.isInvalid())
    return true;

  TheCall->setArg(0, A.get());
  QualType TyA = A.get()->getType();

  if (checkMathBuiltinElementType(*this, A.get()->getBeginLoc(), TyA))
    return true;

  TheCall->setType(TyA);
  return false;
}

bool Sema::SemaBuiltinElementwiseMath(CallExpr *TheCall) {
  if (checkArgCount(*this, TheCall, 2))
    return true;

  ExprResult A = TheCall->getArg(0);
  ExprResult B = TheCall->getArg(1);
  // Do standard promotions between the two arguments, returning their common
  // type.
  QualType Res =
      UsualArithmeticConversions(A, B, TheCall->getExprLoc(), ACK_Comparison);
  if (A.isInvalid() || B.isInvalid())
    return true;

  QualType TyA = A.get()->getType();
  QualType TyB = B.get()->getType();

  if (Res.isNull() || TyA.getCanonicalType() != TyB.getCanonicalType())
    return Diag(A.get()->getBeginLoc(),
                diag::err_typecheck_call_different_arg_types)
           << TyA << TyB;

  if (checkMathBuiltinElementType(*this, A.get()->getBeginLoc(), TyA))
    return true;

  TheCall->setArg(0, A.get());
  TheCall->setArg(1, B.get());
  TheCall->setType(Res);
  return false;
}

bool Sema::SemaBuiltinElementwiseTernaryMath(CallExpr *TheCall) {
  if (checkArgCount(*this, TheCall, 3))
    return true;

  Expr *Args[3];
  for (int I = 0; I < 3; ++I) {
    ExprResult Converted = UsualUnaryConversions(TheCall->getArg(I));
    if (Converted.isInvalid())
      return true;
    Args[I] = Converted.get();
  }

  int ArgOrdinal = 1;
  for (Expr *Arg : Args) {
    if (checkFPMathBuiltinElementType(*this, Arg->getBeginLoc(), Arg->getType(),
                                      ArgOrdinal++))
      return true;
  }

  for (int I = 1; I < 3; ++I) {
    if (Args[0]->getType().getCanonicalType() !=
        Args[I]->getType().getCanonicalType()) {
      return Diag(Args[0]->getBeginLoc(),
                  diag::err_typecheck_call_different_arg_types)
             << Args[0]->getType() << Args[I]->getType();
    }

    TheCall->setArg(I, Args[I]);
  }

  TheCall->setType(Args[0]->getType());
  return false;
}

bool Sema::PrepareBuiltinReduceMathOneArgCall(CallExpr *TheCall) {
  if (checkArgCount(*this, TheCall, 1))
    return true;

  ExprResult A = UsualUnaryConversions(TheCall->getArg(0));
  if (A.isInvalid())
    return true;

  TheCall->setArg(0, A.get());
  return false;
}

bool Sema::SemaBuiltinNonDeterministicValue(CallExpr *TheCall) {
  if (checkArgCount(*this, TheCall, 1))
    return true;

  ExprResult Arg = TheCall->getArg(0);
  QualType TyArg = Arg.get()->getType();

  if (!TyArg->isBuiltinType() && !TyArg->isVectorType())
    return Diag(TheCall->getArg(0)->getBeginLoc(),
                diag::err_builtin_invalid_arg_type)
           << 1 << /*vector, integer or floating point ty*/ 0 << TyArg;

  TheCall->setType(TyArg);
  return false;
}

ExprResult Sema::SemaBuiltinMatrixTranspose(CallExpr *TheCall,
                                            ExprResult CallResult) {
  if (checkArgCount(*this, TheCall, 1))
    return ExprError();

  ExprResult MatrixArg = DefaultLvalueConversion(TheCall->getArg(0));
  if (MatrixArg.isInvalid())
    return MatrixArg;
  Expr *Matrix = MatrixArg.get();

  auto *MType = Matrix->getType()->getAs<ConstantMatrixType>();
  if (!MType) {
    Diag(Matrix->getBeginLoc(), diag::err_builtin_invalid_arg_type)
        << 1 << /* matrix ty*/ 1 << Matrix->getType();
    return ExprError();
  }
  QualType ResultType = Context.getConstantMatrixType(
      MType->getElementType(), MType->getNumColumns(), MType->getNumRows());

  // Change the return type to the type of the returned matrix.
  TheCall->setType(ResultType);

  // Update call argument to use the possibly converted matrix argument.
  TheCall->setArg(0, Matrix);
  return CallResult;
}

// Get and verify the matrix dimensions.
std::optional<unsigned>
getAndVerifyMatrixDimension(Expr *Expr, llvm::StringRef Name, Sema &S) {
  SourceLocation ErrorPos;
  std::optional<llvm::APSInt> Value =
      Expr->getIntegerConstantExpr(S.Context, &ErrorPos);
  if (!Value) {
    S.Diag(Expr->getBeginLoc(), diag::err_builtin_matrix_scalar_unsigned_arg)
        << Name;
    return {};
  }
  uint64_t Dim = Value->getZExtValue();
  if (!ConstantMatrixType::isDimensionValid(Dim)) {
    S.Diag(Expr->getBeginLoc(), diag::err_builtin_matrix_invalid_dimension)
        << Name << ConstantMatrixType::getMaxElementsPerDimension();
    return {};
  }
  return Dim;
}

ExprResult Sema::SemaBuiltinMatrixColumnMajorLoad(CallExpr *TheCall,
                                                  ExprResult CallResult) {
  if (checkArgCount(*this, TheCall, 4))
    return ExprError();

  unsigned PtrArgIdx = 0;
  Expr *PtrExpr = TheCall->getArg(PtrArgIdx);
  Expr *RowsExpr = TheCall->getArg(1);
  Expr *ColumnsExpr = TheCall->getArg(2);
  Expr *StrideExpr = TheCall->getArg(3);

  bool ArgError = false;
  {
    ExprResult PtrConv = DefaultFunctionArrayLvalueConversion(PtrExpr);
    if (PtrConv.isInvalid())
      return PtrConv;
    PtrExpr = PtrConv.get();
    TheCall->setArg(0, PtrExpr);
  }

  auto *PtrTy = PtrExpr->getType()->getAs<PointerType>();
  QualType ElementTy;
  if (!PtrTy) {
    Diag(PtrExpr->getBeginLoc(), diag::err_builtin_invalid_arg_type)
        << PtrArgIdx + 1 << /*pointer to element ty*/ 2 << PtrExpr->getType();
    ArgError = true;
  } else {
    ElementTy = PtrTy->getPointeeType().getUnqualifiedType();

    if (!ConstantMatrixType::isValidElementType(ElementTy)) {
      Diag(PtrExpr->getBeginLoc(), diag::err_builtin_invalid_arg_type)
          << PtrArgIdx + 1 << /* pointer to element ty*/ 2
          << PtrExpr->getType();
      ArgError = true;
    }
  }

  // Apply default Lvalue conversions and convert the expression to size_t.
  auto ApplyArgumentConversions = [this](Expr *E) {
    ExprResult Conv = DefaultLvalueConversion(E);
    if (Conv.isInvalid())
      return Conv;

    return tryConvertExprToType(Conv.get(), Context.getSizeType());
  };

  // Apply conversion to row and column expressions.
  ExprResult RowsConv = ApplyArgumentConversions(RowsExpr);
  if (!RowsConv.isInvalid()) {
    RowsExpr = RowsConv.get();
    TheCall->setArg(1, RowsExpr);
  } else
    RowsExpr = nullptr;

  ExprResult ColumnsConv = ApplyArgumentConversions(ColumnsExpr);
  if (!ColumnsConv.isInvalid()) {
    ColumnsExpr = ColumnsConv.get();
    TheCall->setArg(2, ColumnsExpr);
  } else
    ColumnsExpr = nullptr;
  std::optional<unsigned> MaybeRows;
  if (RowsExpr)
    MaybeRows = getAndVerifyMatrixDimension(RowsExpr, "row", *this);

  std::optional<unsigned> MaybeColumns;
  if (ColumnsExpr)
    MaybeColumns = getAndVerifyMatrixDimension(ColumnsExpr, "column", *this);
  ExprResult StrideConv = ApplyArgumentConversions(StrideExpr);
  if (StrideConv.isInvalid())
    return ExprError();
  StrideExpr = StrideConv.get();
  TheCall->setArg(3, StrideExpr);

  if (MaybeRows) {
    if (std::optional<llvm::APSInt> Value =
            StrideExpr->getIntegerConstantExpr(Context)) {
      uint64_t Stride = Value->getZExtValue();
      if (Stride < *MaybeRows) {
        Diag(StrideExpr->getBeginLoc(),
             diag::err_builtin_matrix_stride_too_small);
        ArgError = true;
      }
    }
  }

  if (ArgError || !MaybeRows || !MaybeColumns)
    return ExprError();

  TheCall->setType(
      Context.getConstantMatrixType(ElementTy, *MaybeRows, *MaybeColumns));
  return CallResult;
}

ExprResult Sema::SemaBuiltinMatrixColumnMajorStore(CallExpr *TheCall,
                                                   ExprResult CallResult) {
  if (checkArgCount(*this, TheCall, 3))
    return ExprError();

  unsigned PtrArgIdx = 1;
  Expr *MatrixExpr = TheCall->getArg(0);
  Expr *PtrExpr = TheCall->getArg(PtrArgIdx);
  Expr *StrideExpr = TheCall->getArg(2);

  bool ArgError = false;

  {
    ExprResult MatrixConv = DefaultLvalueConversion(MatrixExpr);
    if (MatrixConv.isInvalid())
      return MatrixConv;
    MatrixExpr = MatrixConv.get();
    TheCall->setArg(0, MatrixExpr);
  }
  auto *MatrixTy = MatrixExpr->getType()->getAs<ConstantMatrixType>();
  if (!MatrixTy) {
    Diag(MatrixExpr->getBeginLoc(), diag::err_builtin_invalid_arg_type)
        << 1 << /*matrix ty */ 1 << MatrixExpr->getType();
    ArgError = true;
  }

  {
    ExprResult PtrConv = DefaultFunctionArrayLvalueConversion(PtrExpr);
    if (PtrConv.isInvalid())
      return PtrConv;
    PtrExpr = PtrConv.get();
    TheCall->setArg(1, PtrExpr);
  }
  auto *PtrTy = PtrExpr->getType()->getAs<PointerType>();
  if (!PtrTy) {
    Diag(PtrExpr->getBeginLoc(), diag::err_builtin_invalid_arg_type)
        << PtrArgIdx + 1 << /*pointer to element ty*/ 2 << PtrExpr->getType();
    ArgError = true;
  } else {
    QualType ElementTy = PtrTy->getPointeeType();
    if (ElementTy.isConstQualified()) {
      Diag(PtrExpr->getBeginLoc(), diag::err_builtin_matrix_store_to_const);
      ArgError = true;
    }
    ElementTy = ElementTy.getUnqualifiedType().getCanonicalType();
    if (MatrixTy &&
        !Context.hasSameType(ElementTy, MatrixTy->getElementType())) {
      Diag(PtrExpr->getBeginLoc(),
           diag::err_builtin_matrix_pointer_arg_mismatch)
          << ElementTy << MatrixTy->getElementType();
      ArgError = true;
    }
  }

  // Apply default Lvalue conversions and convert the stride expression to
  // size_t.
  {
    ExprResult StrideConv = DefaultLvalueConversion(StrideExpr);
    if (StrideConv.isInvalid())
      return StrideConv;

    StrideConv = tryConvertExprToType(StrideConv.get(), Context.getSizeType());
    if (StrideConv.isInvalid())
      return StrideConv;
    StrideExpr = StrideConv.get();
    TheCall->setArg(2, StrideExpr);
  }
  if (MatrixTy) {
    if (std::optional<llvm::APSInt> Value =
            StrideExpr->getIntegerConstantExpr(Context)) {
      uint64_t Stride = Value->getZExtValue();
      if (Stride < MatrixTy->getNumRows()) {
        Diag(StrideExpr->getBeginLoc(),
             diag::err_builtin_matrix_stride_too_small);
        ArgError = true;
      }
    }
  }

  if (ArgError)
    return ExprError();

  return CallResult;
}

void Sema::CheckTCBEnforcement(const SourceLocation CallExprLoc,
                               const NamedDecl *Callee) {
  // This warning does not make sense in code that has no runtime behavior.
  if (isUnevaluatedContext())
    return;

  const FunctionDecl *Caller = getCurFunctionDecl();

  if (!Caller || !Caller->hasAttr<EnforceTCBAttr>())
    return;

  // Search through the enforce_tcb and enforce_tcb_leaf attributes to find
  // all TCBs the callee is a part of.
  llvm::StringSet<> CalleeTCBs;
  for (const auto *A : Callee->specific_attrs<EnforceTCBAttr>())
    CalleeTCBs.insert(A->getTCBName());
  for (const auto *A : Callee->specific_attrs<EnforceTCBLeafAttr>())
    CalleeTCBs.insert(A->getTCBName());

  // Go through the TCBs the caller is a part of and emit warnings if Caller
  // is in a TCB that the Callee is not.
  for (const auto *A : Caller->specific_attrs<EnforceTCBAttr>()) {
    llvm::StringRef CallerTCB = A->getTCBName();
    if (!CalleeTCBs.contains(CallerTCB)) {
      this->Diag(CallExprLoc, diag::warn_tcb_enforcement_violation)
          << Callee << CallerTCB;
    }
  }
}
