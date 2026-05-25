#include "neverc/Analyze/ScopeInfo.h"
#include "neverc/Analyze/SemaInternal.h"
#include "neverc/Foundation/Builtin/BuiltinString.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Expr/Expr.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/ErrorHandling.h"

using namespace neverc;
using namespace sema;

// ===----------------------------------------------------------------------===
// Member access expression analysis
// ===----------------------------------------------------------------------===

namespace {

Expr *materializeNeverCStringMethodDefaultArg(
    Sema &S, BuiltinString::MethodDefaultArgKind Kind, SourceLocation Loc) {
  using Kind_t = BuiltinString::MethodDefaultArgKind;
  TreeContext &Ctx = S.Context;
  switch (Kind) {
  case Kind_t::None:
    return nullptr;
  case Kind_t::SizeAllOnes: {
    QualType SizeTy = Ctx.getSizeType();
    unsigned Bits = Ctx.getTypeSize(SizeTy);
    return IntegerLiteral::Create(Ctx, llvm::APInt::getAllOnes(Bits), SizeTy,
                                  Loc);
  }
  case Kind_t::CharZero: {
    // `s.resize(n)` -> append `'\0'` so the runtime helper sees the
    // full `(s, n, fill)` signature with std::string-parity default
    // fill.  We materialise the literal as a plain `char` integer
    // value (NUL byte); the runtime helper takes a `char` parameter
    // and a value-initialised `char` is the zero byte by definition.
    // `getIntWidth` matches the bit count IntegerLiteral asserts on,
    // so no width mismatch ever fires.
    QualType CharTy = Ctx.CharTy;
    unsigned Bits = Ctx.getIntWidth(CharTy);
    return IntegerLiteral::Create(Ctx, llvm::APInt(Bits, 0), CharTy, Loc);
  }
  }
  llvm_unreachable("unhandled BuiltinString::MethodDefaultArgKind");
}

bool isRGBA(char c) {
  switch (c) {
  case 'r':
  case 'g':
  case 'b':
  case 'a':
    return true;
  default:
    return false;
  }
}

QualType checkExtVectorComponent(Sema &S, QualType baseType, ExprValueKind &VK,
                                 SourceLocation OpLoc,
                                 const IdentifierInfo *CompName,
                                 SourceLocation CompLoc) {
  const ExtVectorType *vecType = baseType->getAs<ExtVectorType>();

  // The vector accessor can't exceed the number of elements.
  const char *compStr = CompName->getNameStart();

  // This flag determines whether or not the component is one of the four
  // special names that indicate a subset of exactly half the elements are
  // to be selected.
  bool HalvingSwizzle = false;

  // This flag determines whether or not CompName has an 's' char prefix,
  // indicating that it is a string of hex values to be used as vector indices.
  bool HexSwizzle = (*compStr == 's' || *compStr == 'S') && compStr[1];

  bool HasRepeated = false;
  bool HasIndex[16] = {};

  int Idx;

  // Check that we've found one of the special components, or that the component
  // names must come from the same set.
  if (!strcmp(compStr, "hi") || !strcmp(compStr, "lo") ||
      !strcmp(compStr, "even") || !strcmp(compStr, "odd")) {
    HalvingSwizzle = true;
  } else if (!HexSwizzle &&
             (Idx = vecType->getPointAccessorIdx(*compStr)) != -1) {
    bool HasRGBA = isRGBA(*compStr);
    do {
      // Ensure that xyzw and rgba components don't intermingle.
      if (HasRGBA != isRGBA(*compStr))
        break;
      if (HasIndex[Idx])
        HasRepeated = true;
      HasIndex[Idx] = true;
      compStr++;
    } while (*compStr && (Idx = vecType->getPointAccessorIdx(*compStr)) != -1);

  } else {
    if (HexSwizzle)
      compStr++;
    while ((Idx = vecType->getNumericAccessorIdx(*compStr)) != -1) {
      if (HasIndex[Idx])
        HasRepeated = true;
      HasIndex[Idx] = true;
      compStr++;
    }
  }

  if (!HalvingSwizzle && *compStr) {
    // We didn't get to the end of the string. This means the component names
    // didn't come from the same set *or* we encountered an illegal name.
    S.Diag(OpLoc, diag::err_ext_vector_component_name_illegal)
        << llvm::StringRef(compStr, 1) << SourceRange(CompLoc);
    return QualType();
  }

  // Ensure no component accessor exceeds the width of the vector type it
  // operates on.
  if (!HalvingSwizzle) {
    compStr = CompName->getNameStart();

    if (HexSwizzle)
      compStr++;

    while (*compStr) {
      if (!vecType->isAccessorWithinNumElements(*compStr++, HexSwizzle)) {
        S.Diag(OpLoc, diag::err_ext_vector_component_exceeds_length)
            << baseType << SourceRange(CompLoc);
        return QualType();
      }
    }
  }

  // The component accessor looks fine - now we need to compute the actual type.
  // The vector type is implied by the component accessor. For example,
  // vec4.b is a float, vec4.xy is a vec2, vec4.rgb is a vec3, etc.
  // vec4.s0 is a float, vec4.s23 is a vec3, etc.
  // vec4.hi, vec4.lo, vec4.e, and vec4.o all return vec2.
  unsigned CompSize = HalvingSwizzle ? (vecType->getNumElements() + 1) / 2
                                     : CompName->getLength();
  if (HexSwizzle)
    CompSize--;

  if (CompSize == 1)
    return vecType->getElementType();

  if (HasRepeated)
    VK = VK_PRValue;

  QualType VT = S.Context.getExtVectorType(vecType->getElementType(), CompSize);
  // Now look up the TypeDefDecl from the vector type. Without this,
  // diagostics look bad. We want extended vector types to appear built-in.
  for (auto *D : S.ExtVectorDecls) {
    if (D->getUnderlyingType() == VT)
      return S.Context.getTypedefType(D);
  }

  return VT; // should never get here (a typedef type should always be found).
}

// Typo correction for member access: only suggest declarations that are
// members of the current record.
namespace {

class RecordMemberExprValidatorCCC final : public CorrectionCandidateCallback {
public:
  explicit RecordMemberExprValidatorCCC(const RecordType *RTy)
      : Record(RTy->getDecl()) {
    // Don't add bare keywords to the consumer since they will always fail
    // validation by virtue of not being associated with any decls.
    WantTypeSpecifiers = false;
    WantExpressionKeywords = false;
    WantFunctionLikeCasts = false;
    WantRemainingKeywords = false;
  }

  bool ValidateCandidate(const TypoCorrection &candidate) override {
    NamedDecl *ND = candidate.getCorrectionDecl();
    if (!ND || !isa<ValueDecl>(ND))
      return false;

    // Accept candidates that occur in the current record.
    if (Record->containsDecl(ND))
      return true;

    return false;
  }

  std::unique_ptr<CorrectionCandidateCallback> clone() override {
    return std::make_unique<RecordMemberExprValidatorCCC>(*this);
  }

private:
  const RecordDecl *const Record;
};

} // namespace

bool lookupMemberExprInRecord(Sema &SemaRef, LookupResult &R, Expr *BaseExpr,
                              const RecordType *RTy, SourceLocation OpLoc,
                              bool IsArrow, TypoExpr *&TE) {
  assert(BaseExpr && "member access requires base expression");
  SourceRange BaseRange = BaseExpr->getSourceRange();
  RecordDecl *RDecl = RTy->getDecl();
  if (SemaRef.RequireCompleteType(OpLoc, QualType(RTy, 0),
                                  diag::err_typecheck_incomplete_tag,
                                  BaseRange))
    return true;

  DeclContext *DC = RDecl;

  // The record definition is complete, now look up the member.
  SemaRef.LookupQualifiedName(R, DC);

  if (!R.empty())
    return false;

  DeclarationName Typo = R.getLookupName();
  SourceLocation TypoLoc = R.getNameLoc();

  struct QueryState {
    Sema &SemaRef;
    DeclarationNameInfo NameInfo;
    neverc::ResolveNameKind LookupKind;
    neverc::RedeclarationKind Redecl;
  };
  QueryState Q = {R.getSema(), R.getLookupNameInfo(), R.getLookupKind(),
                  R.redeclarationKind()};
  RecordMemberExprValidatorCCC CCC(RTy);
  TE = SemaRef.CorrectTypoDelayed(
      R.getLookupNameInfo(), R.getLookupKind(), nullptr, CCC,
      [=, &SemaRef](const TypoCorrection &TC) {
        if (TC) {
          assert(!TC.isKeyword() &&
                 "Got a keyword as a correction for a member!");
          SemaRef.diagnoseTypo(TC, SemaRef.PDiag(diag::err_no_member_suggest)
                                       << Typo << DC << false << BaseRange);
        } else {
          SemaRef.Diag(TypoLoc, diag::err_no_member) << Typo << DC << BaseRange;
        }
      },
      [=](Sema &SemaRef, TypoExpr *TE, TypoCorrection TC) mutable {
        LookupResult R(Q.SemaRef, Q.NameInfo, Q.LookupKind, Q.Redecl);
        R.clear(); // Ensure there's no decls lingering in the shared state.
        R.suppressDiagnostics();
        R.setLookupName(TC.getCorrection());
        for (NamedDecl *ND : TC)
          R.addDecl(ND);
        R.resolveKind();
        return SemaRef.FormMemberReferenceExpr(BaseExpr, BaseExpr->getType(),
                                               OpLoc, IsArrow, R);
      },
      Sema::CTK_ErrorRecovery, DC);

  return false;
}

ExprResult lookupMemberExpr(Sema &S, LookupResult &R, ExprResult &BaseExpr,
                            bool &IsArrow, SourceLocation OpLoc);
} // namespace

ExprResult Sema::FormMemberReferenceExpr(Expr *Base, QualType BaseType,
                                         SourceLocation OpLoc, bool IsArrow,
                                         const DeclarationNameInfo &NameInfo) {
  LookupResult R(*this, NameInfo, ResolveMember);

  assert(Base && "member access requires base expression");
  ExprResult BaseResult = Base;
  ExprResult Result = lookupMemberExpr(*this, R, BaseResult, IsArrow, OpLoc);

  if (BaseResult.isInvalid())
    return ExprError();
  Base = BaseResult.get();

  if (Result.isInvalid())
    return ExprError();

  if (Result.get())
    return Result;

  BaseType = Base->getType();

  return FormMemberReferenceExpr(Base, BaseType, OpLoc, IsArrow, R);
}

ExprResult Sema::FormAnonymousStructUnionMemberReference(
    SourceLocation loc, IndirectFieldDecl *indirectField, NamedDecl *foundDecl,
    Expr *baseObjectExpr, SourceLocation opLoc) {
  // First, build the expression that refers to the base object.

  // Case 1:  the base of the indirect field is not a field.
  VarDecl *baseVariable = indirectField->getVarDecl();
  if (baseVariable) {
    assert(baseVariable->getType()->isRecordType());

    assert(!baseObjectExpr && "unexpected base object for anonymous member");

    DeclarationNameInfo baseNameInfo(DeclarationName(), loc);

    ExprResult result = FormDeclarationNameExpr(baseNameInfo, baseVariable);
    if (result.isInvalid())
      return ExprError();

    baseObjectExpr = result.get();
  }

  assert((baseVariable || baseObjectExpr) &&
         "referencing anonymous struct/union without a base variable or "
         "expression");

  // Build the implicit member references to the field of the
  // anonymous struct/union.
  Expr *result = baseObjectExpr;
  IndirectFieldDecl::chain_iterator FI = indirectField->chain_begin(),
                                    FEnd = indirectField->chain_end();

  // Case 2: the base of the indirect field is a field and the user
  // wrote a member expression.
  if (!baseVariable) {
    FieldDecl *field = cast<FieldDecl>(*FI);

    bool baseObjectIsPointer = baseObjectExpr->getType()->isPointerType();

    // Make a nameInfo that properly uses the anonymous name.
    DeclarationNameInfo memberNameInfo(field->getDeclName(), loc);

    // Build the first member access in the chain with full information.
    result =
        FormFieldReferenceExpr(result, baseObjectIsPointer, SourceLocation(),
                               field, foundDecl, memberNameInfo)
            .get();
    if (!result)
      return ExprError();
  }

  // In all cases, we should now skip the first declaration in the chain.
  ++FI;

  while (FI != FEnd) {
    FieldDecl *field = cast<FieldDecl>(*FI++);

    DeclarationNameInfo memberNameInfo(field->getDeclName(), loc);
    NamedDecl *fakeFoundDecl = field;

    result = FormFieldReferenceExpr(result, /*isarrow*/ false, SourceLocation(),
                                    field, fakeFoundDecl, memberNameInfo)
                 .get();
  }

  return result;
}

MemberExpr *Sema::FormMemberExpr(Expr *Base, bool IsArrow, SourceLocation OpLoc,
                                 ValueDecl *Member, NamedDecl *FoundDecl,
                                 const DeclarationNameInfo &MemberNameInfo,
                                 QualType Ty, ExprValueKind VK,
                                 ExprObjectKind OK) {
  assert((!IsArrow || Base->isPRValue()) &&
         "-> base must be a pointer prvalue");
  MemberExpr *E = MemberExpr::Create(
      Context, Base, IsArrow, OpLoc, Member, FoundDecl, MemberNameInfo, Ty, VK,
      OK, getNonOdrUseReasonInCurrentContext(Member));
  MarkMemberReferenced(E);
  return E;
}

ExprResult Sema::FormMemberReferenceExpr(Expr *BaseExpr, QualType BaseExprType,
                                         SourceLocation OpLoc, bool IsArrow,
                                         LookupResult &R) {
  QualType BaseType = BaseExprType;
  if (IsArrow) {
    assert(BaseType->isPointerType());
    BaseType = BaseType->castAs<PointerType>()->getPointeeType();
  }

  const DeclarationNameInfo &MemberNameInfo = R.getLookupNameInfo();
  DeclarationName MemberName = MemberNameInfo.getName();
  SourceLocation MemberLoc = MemberNameInfo.getLoc();

#ifndef _WIN32
  if (R.isAmbiguous())
    return ExprError();
#endif

  if (R.empty()) {
    DeclContext *DC = BaseType->castAs<RecordType>()->getDecl();

    Diag(R.getNameLoc(), diag::err_no_member)
        << MemberName << DC
        << (BaseExpr ? BaseExpr->getSourceRange() : SourceRange());
    return ExprError();
  }

  // Member lookup should not yield an overload set; reject if it does.
  if (R.isOverloadedResult()) {
    R.suppressDiagnostics();
    Diag(MemberLoc, diag::err_typecheck_member_reference_unknown)
        << MemberName << BaseType << int(IsArrow);
    return ExprError();
  }

  NamedDecl *FoundDecl = *R.begin();
  NamedDecl *MemberDecl = R.getFoundDecl();

  // If the decl being referenced had an error, return an error for this
  // sub-expr without emitting another error, in order to avoid cascading
  // error cases.
  if (MemberDecl->isInvalidDecl())
    return ExprError();

  assert(BaseExpr && "member access requires base expression");

  // Check the use of this member.
  if (CheckDeclUsage(MemberDecl, MemberLoc))
    return ExprError();

  if (FieldDecl *FD = dyn_cast<FieldDecl>(MemberDecl))
    return FormFieldReferenceExpr(BaseExpr, IsArrow, OpLoc, FD, FoundDecl,
                                  MemberNameInfo);

  if (IndirectFieldDecl *FD = dyn_cast<IndirectFieldDecl>(MemberDecl))
    // Field within an anonymous struct or union (C11 / GNU).
    return FormAnonymousStructUnionMemberReference(MemberLoc, FD, FoundDecl,
                                                   BaseExpr, OpLoc);

  if (isa<VarDecl>(MemberDecl) || isa<EnumConstantDecl>(MemberDecl)) {
    if (VarDecl *Var = dyn_cast<VarDecl>(MemberDecl))
      return FormMemberExpr(BaseExpr, IsArrow, OpLoc, Var, FoundDecl,
                            MemberNameInfo, Var->getType(), VK_LValue,
                            OK_Ordinary);
    auto *Enum = cast<EnumConstantDecl>(MemberDecl);
    return FormMemberExpr(BaseExpr, IsArrow, OpLoc, Enum, FoundDecl,
                          MemberNameInfo, Enum->getType(), VK_PRValue,
                          OK_Ordinary);
  }

  assert(!isa<FunctionDecl>(MemberDecl) &&
         "unexpected member FunctionDecl; handled above");

  // We found something that we didn't expect. Complain.
  if (isa<TypeDecl>(MemberDecl))
    Diag(MemberLoc, diag::err_typecheck_member_reference_type)
        << MemberName << BaseType << int(IsArrow);
  else
    Diag(MemberLoc, diag::err_typecheck_member_reference_unknown)
        << MemberName << BaseType << int(IsArrow);

  Diag(MemberDecl->getLocation(), diag::note_member_declared_here)
      << MemberName;
  R.suppressDiagnostics();
  return ExprError();
}

namespace {
bool isRecordType(QualType T) { return T->isRecordType(); }
bool isPointerToRecordType(QualType T) {
  if (const PointerType *PT = T->getAs<PointerType>())
    return PT->getPointeeType()->isRecordType();
  return false;
}
} // namespace

ExprResult Sema::PerformMemberExprBaseConversion(Expr *Base, bool IsArrow) {
  if (IsArrow && !Base->getType()->isFunctionType())
    return DefaultFunctionArrayLvalueConversion(Base);

  return CheckPlaceholderExpr(Base);
}

namespace {
ExprResult lookupMemberExpr(Sema &S, LookupResult &R, ExprResult &BaseExpr,
                            bool &IsArrow, SourceLocation OpLoc) {
  assert(BaseExpr.get() && "no base expression");

  // Perform default conversions.
  BaseExpr = S.PerformMemberExprBaseConversion(BaseExpr.get(), IsArrow);
  if (BaseExpr.isInvalid())
    return ExprError();

  QualType BaseType = BaseExpr.get()->getType();
  assert(!BaseType->isDependentType());

  DeclarationName MemberName = R.getLookupName();
  SourceLocation MemberLoc = R.getNameLoc();

  // For later type-checking purposes, turn arrow accesses into dot
  // accesses, following the C equivalence "a->b === (*a).b".
  if (IsArrow) {
    if (const PointerType *Ptr = BaseType->getAs<PointerType>())
      BaseType = Ptr->getPointeeType();
    else if (BaseType->isRecordType()) {
      // Recover from arrow accesses to records, e.g.:
      //   struct MyRecord foo;
      //   foo->bar
      {
        S.Diag(OpLoc, diag::err_typecheck_member_reference_suggestion)
            << BaseType << int(IsArrow) << BaseExpr.get()->getSourceRange()
            << FixItHint::CreateReplacement(OpLoc, ".");
      }
      IsArrow = false;
    } else if (BaseType->isFunctionType()) {
      goto fail;
    } else {
      S.Diag(MemberLoc, diag::err_typecheck_member_reference_arrow)
          << BaseType << BaseExpr.get()->getSourceRange();
      return ExprError();
    }
  }

  // Member access on atomic types is UB. Warn (default=error) and recover
  // by stripping the atomic qualifier.
  if (const auto *ATy = BaseType->getAs<AtomicType>()) {
    S.DiagRuntimeBehavior(OpLoc, nullptr,
                          S.PDiag(diag::warn_atomic_member_access));
    BaseType = ATy->getValueType().getUnqualifiedType();
    BaseExpr = ImplicitCastExpr::Create(
        S.Context, IsArrow ? S.Context.getPointerType(BaseType) : BaseType,
        CK_AtomicToNonAtomic, BaseExpr.get(), BaseExpr.get()->getValueKind(),
        FPOptionsOverride());
  }

  // Handle field access to simple records.
  if (const RecordType *RTy = BaseType->getAs<RecordType>()) {
    TypoExpr *TE = nullptr;
    if (lookupMemberExprInRecord(S, R, BaseExpr.get(), RTy, OpLoc, IsArrow, TE))
      return ExprError();

    // Returning valid-but-null is how we indicate to the caller that
    // the lookup result was filled in. If typo correction was attempted and
    // failed, the lookup result will have been cleared--that combined with the
    // valid-but-null ExprResult will trigger the appropriate diagnostics.
    return ExprResult(TE);
  }

  if (BaseType->isExtVectorBoolType()) {
    // We disallow element access for ext_vector_type bool.  There is no way to
    // materialize a reference to a vector element as a pointer (each element is
    // one bit in the vector).
    S.Diag(R.getNameLoc(), diag::err_ext_vector_component_name_illegal)
        << MemberName
        << (BaseExpr.get() ? BaseExpr.get()->getSourceRange() : SourceRange());
    return ExprError();
  }

  // Handle 'field access' to vectors, such as 'V.xx'.
  if (BaseType->isExtVectorType()) {
    IdentifierInfo *Member = MemberName.getAsIdentifierInfo();
    ExprValueKind VK = (IsArrow ? VK_LValue : BaseExpr.get()->getValueKind());
    QualType ret =
        checkExtVectorComponent(S, BaseType, VK, OpLoc, Member, MemberLoc);
    if (ret.isNull())
      return ExprError();
    Qualifiers BaseQ =
        S.Context.getCanonicalType(BaseExpr.get()->getType()).getQualifiers();
    ret = S.Context.getQualifiedType(ret, BaseQ);

    return new (S.Context)
        ExtVectorElementExpr(ret, VK, BaseExpr.get(), *Member, MemberLoc);
  }

  // Failure cases.
fail:

  // Recover from dot accesses to pointers, e.g.:
  //   type *foo;
  //   foo.bar
  if (const PointerType *Ptr = BaseType->getAs<PointerType>()) {
    if (!IsArrow && Ptr->getPointeeType()->isRecordType()) {
      S.Diag(OpLoc, diag::err_typecheck_member_reference_suggestion)
          << BaseType << int(IsArrow) << BaseExpr.get()->getSourceRange()
          << FixItHint::CreateReplacement(OpLoc, "->");

      // Recurse as an -> access.
      IsArrow = true;
      return lookupMemberExpr(S, R, BaseExpr, IsArrow, OpLoc);
    }
  }

  // If the user is trying to apply -> or . to a function name, it's probably
  // because they forgot parentheses to call that function.
  if (S.tryToRecoverWithCall(
          BaseExpr, S.PDiag(diag::err_member_reference_needs_call),
          /*complain*/ false,
          IsArrow ? &isPointerToRecordType : &isRecordType)) {
    if (BaseExpr.isInvalid())
      return ExprError();
    BaseExpr = S.DefaultFunctionArrayConversion(BaseExpr.get());
    return lookupMemberExpr(S, R, BaseExpr, IsArrow, OpLoc);
  }

  S.Diag(OpLoc, diag::err_typecheck_member_reference_struct_union)
      << BaseType << BaseExpr.get()->getSourceRange() << MemberLoc;

  return ExprError();
}
} // namespace

ExprResult
Sema::OnBuiltinStringMethodCall(Scope *S, Expr *Base, SourceLocation OpLoc,
                                tok::TokenKind OpKind, UnqualifiedId &Member,
                                SourceLocation LParenLoc, MultiExprArg ArgExprs,
                                SourceLocation RParenLoc) {
  auto FormRegularMemberCall = [&]() -> ExprResult {
    ExprResult MemberRef = OnMemberAccessExpr(S, Base, OpLoc, OpKind, Member);
    if (MemberRef.isInvalid())
      return ExprError();
    return OnCallExpr(S, MemberRef.get(), LParenLoc, ArgExprs, RParenLoc);
  };

  IdentifierInfo *II = Member.getIdentifierInfo();
  if (OpKind != tok::period || !II)
    return FormRegularMemberCall();

  ExprResult BaseResult =
      PerformMemberExprBaseConversion(Base, /*IsArrow=*/false);
  if (BaseResult.isInvalid())
    return ExprError();
  Base = BaseResult.get();

  StringLiteral *BaseLit = getNeverCStringLiteral(Base);
  if (!BaseLit && !this->isNeverCStringType(Base->getType()))
    return FormRegularMemberCall();

  llvm::StringRef MethodName = II->getName();
  unsigned DottedArgCount = static_cast<unsigned>(ArgExprs.size());

  if (MethodName == "encrypt") {
    if (DottedArgCount != 0) {
      Diag(LParenLoc, diag::err_neverc_string_encrypt_args);
      return ExprError();
    }
    if (!BaseLit) {
      Diag(LParenLoc, diag::err_neverc_string_encrypt_non_literal);
      return ExprError();
    }
    return buildNeverCStringEncryptedLiteral(*this, S, Base, BaseLit,
                                            LParenLoc, RParenLoc);
  }

  bool FirstArgIsCharLike = false;
  if (DottedArgCount >= 1) {
    QualType ArgTy = ArgExprs[0]->getType();
    FirstArgIsCharLike =
        ArgTy->isIntegerType() && !this->isNeverCStringType(ArgTy);
  }

  llvm::StringRef FunctionName = BuiltinString::resolveMethodFunctionName(
      MethodName, DottedArgCount, FirstArgIsCharLike);
  if (FunctionName.empty())
    return FormRegularMemberCall();

  if (BaseLit) {
    if (StringLiteral *Folded =
            foldNeverCStringWideLiteralToUtf8(*this, BaseLit)) {
      if (Folded != BaseLit)
        Base = Folded;
    }
  }

  llvm::SmallVector<Expr *, 4> CallArgs;
  CallArgs.push_back(Base);
  CallArgs.append(ArgExprs.begin(), ArgExprs.end());

  // std::string-parity default arguments.  When the user omits a
  // trailing parameter that the runtime helper still expects, the
  // dispatcher synthesises the documented default value (e.g.
  // `s.substr(pos)` -> append `npos`).  The "(method, arity) -> kind"
  // rows live in `BuiltinStringMethodDefaults.def`; the per-kind
  // literal construction lives in `materializeNeverCStringMethodDefaultArg`
  // so adding a new shape is a one-row edit per def file plus one
  // matching `case` arm in the helper.
  if (Expr *Default = materializeNeverCStringMethodDefaultArg(
          *this,
          BuiltinString::getMethodDefaultArgKind(MethodName, DottedArgCount),
          RParenLoc))
    CallArgs.push_back(Default);

  // Receiver / string-argument passing convention.  `s.assign(t)` and
  // `s.swap(t)` need their receiver (and, for swap, every string
  // argument) wrapped in a `&` UnaryOp so the runtime helper sees the
  // canonical `string *` shape.  The `(method -> kind)` rows live in
  // `BuiltinStringMethodReceiverKinds.def`; the kind enum roster lives
  // in `BuiltinStringMethodReceiverKindsRoster.def` and is expanded
  // inside the enum body in `BuiltinString.h` so adding a new
  // pointer-shaped helper is a one-row edit per def file plus one
  // matching arm in the switch below.
  using RecvKind = BuiltinString::MethodReceiverKind;
  RecvKind Recv = BuiltinString::getMethodReceiverKind(MethodName);
  auto WrapAddrOf = [&](Expr *&Slot) -> bool {
    ExprResult Addr = FormUnaryOp(/*Scope=*/S, OpLoc, UO_AddrOf, Slot);
    if (Addr.isInvalid())
      return false;
    Slot = Addr.get();
    return true;
  };
  switch (Recv) {
  case RecvKind::ByValue:
    break;
  case RecvKind::Receiver:
    if (!WrapAddrOf(CallArgs[0]))
      return ExprError();
    break;
  case RecvKind::All:
    if (!WrapAddrOf(CallArgs[0]))
      return ExprError();
    for (unsigned I = 1, NumArgs = CallArgs.size(); I < NumArgs; ++I) {
      if (this->isNeverCStringType(CallArgs[I]->getType()) &&
          !WrapAddrOf(CallArgs[I]))
        return ExprError();
    }
    break;
  }

  return buildNeverCStringRuntimeCall(*this, S, LParenLoc, FunctionName,
                                      CallArgs, RParenLoc);
}

ExprResult Sema::OnMemberAccessExpr(Scope *S, Expr *Base, SourceLocation OpLoc,
                                    tok::TokenKind OpKind, UnqualifiedId &Id) {
  DeclarationNameInfo NameInfo = GetNameFromUnqualifiedId(Id);

  bool IsArrow = (OpKind == tok::arrow);

  // This is a postfix expression, so get rid of ParenListExprs.
  ExprResult Result = MaybeConvertParenListExprToParenExpr(S, Base);
  if (Result.isInvalid())
    return ExprError();
  Base = Result.get();

  ExprResult Res =
      FormMemberReferenceExpr(Base, Base->getType(), OpLoc, IsArrow, NameInfo);

  if (!Res.isInvalid() && isa<MemberExpr>(Res.get()))
    CheckMemberAccessOfNoDeref(cast<MemberExpr>(Res.get()));

  return Res;
}

void Sema::CheckMemberAccessOfNoDeref(const MemberExpr *E) {
  if (isUnevaluatedContext())
    return;

  QualType ResultTy = E->getType();

  // Member accesses have four cases:
  // 1: non-array member via "->": dereferences
  // 2: non-array member via ".": nothing interesting happens
  // 3: array member access via "->": nothing interesting happens
  //    (this returns an array lvalue and does not actually dereference memory)
  // 4: array member access via ".": *adds* a layer of indirection
  if (ResultTy->isArrayType()) {
    if (!E->isArrow()) {
      // This might be something like:
      //     (*structPtr).arrayMember
      // which behaves roughly like:
      //     &(*structPtr).pointerMember
      // in that the apparent dereference in the base expression does not
      // actually happen.
      CheckAddressOfNoDeref(E->getBase());
    }
  } else if (E->isArrow()) {
    if (const auto *Ptr = dyn_cast<PointerType>(
            E->getBase()->getType().getDesugaredType(Context))) {
      if (Ptr->getPointeeType()->hasAttr(attr::NoDeref))
        ExprEvalContexts.back().PossibleDerefs.insert(E);
    }
  }
}

ExprResult
Sema::FormFieldReferenceExpr(Expr *BaseExpr, bool IsArrow, SourceLocation OpLoc,
                             FieldDecl *Field, NamedDecl *FoundDecl,
                             const DeclarationNameInfo &MemberNameInfo) {
  // x.a is an l-value/pr-value if the base is (and note that *x is
  // always an l-value), except that if the base isn't an ordinary object then
  // we must have an rvalue.
  ExprValueKind VK = VK_LValue;
  ExprObjectKind OK = OK_Ordinary;
  if (!IsArrow) {
    if (BaseExpr->getObjectKind() == OK_Ordinary)
      VK = BaseExpr->getValueKind();
    else
      VK = VK_PRValue;
  }
  if (VK != VK_PRValue && Field->isBitField())
    OK = OK_BitField;

  // Determine the type of the accessed member.
  QualType MemberType = Field->getType();

  QualType BaseType = BaseExpr->getType();
  if (IsArrow)
    BaseType = BaseType->castAs<PointerType>()->getPointeeType();

  Qualifiers BaseQuals = BaseType.getQualifiers();

  Qualifiers MemberQuals = Context.getCanonicalType(MemberType).getQualifiers();

  assert(!MemberQuals.hasAddressSpace());

  Qualifiers Combined = BaseQuals + MemberQuals;
  if (Combined != MemberQuals)
    MemberType = Context.getQualifiedType(MemberType, Combined);

  // Pick up NoDeref from the base in case we end up using AddrOf on the
  // result. E.g. the expression
  //     &someNoDerefPtr->pointerMember
  // should be a noderef pointer again.
  if (BaseType->hasAttr(attr::NoDeref))
    MemberType =
        Context.getAttributedType(attr::NoDeref, MemberType, MemberType);

  UnusedPrivateFields.remove(Field);

  return FormMemberExpr(BaseExpr, IsArrow, OpLoc, Field, FoundDecl,
                        MemberNameInfo, MemberType, VK, OK);
}
