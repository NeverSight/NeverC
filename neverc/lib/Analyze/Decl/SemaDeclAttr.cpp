#include "neverc/Analyze/DelayedDiagnostic.h"
#include "neverc/Analyze/ParsedAttr.h"
#include "neverc/Analyze/ScopeInfo.h"
#include "neverc/Analyze/SemaInternal.h"
#include "neverc/Foundation/Builtin/TargetBuiltins.h"
#include "neverc/Foundation/Core/CharInfo.h"
#include "neverc/Foundation/Core/SourceLocation.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Tree/Core/RecursiveTreeVisitor.h"
#include "neverc/Tree/Core/TreeConsumer.h"
#include "neverc/Tree/Core/TreeMutationListener.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/IR/Assumptions.h"
#include "llvm/MC/MCSectionMachO.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <optional>

using namespace neverc;
using namespace sema;

namespace AttributeLangSupport {
enum LANG { C };
} // end namespace AttributeLangSupport

// ===----------------------------------------------------------------------===
// Attribute argument validation helpers
// ===----------------------------------------------------------------------===

namespace {

bool isFunctionOrMethod(const Decl *D) {
  return D->getFunctionType() != nullptr;
}

bool hasDeclarator(const Decl *D) {
  // In some sense, TypedefDecl really *ought* to be a DeclaratorDecl.
  return isa<DeclaratorDecl>(D) || isa<TypedefNameDecl>(D) || false;
}

bool hasFunctionProto(const Decl *D) {
  if (const FunctionType *FnTy = D->getFunctionType())
    return isa<FunctionProtoType>(FnTy);
  return false;
}

unsigned getFunctionNumParams(const Decl *D) {
  if (const FunctionType *FnTy = D->getFunctionType())
    return cast<FunctionProtoType>(FnTy)->getNumParams();
  llvm_unreachable("expected function declaration");
}

const ParmVarDecl *getFunctionParam(const Decl *D, unsigned Idx) {
  if (const auto *FD = dyn_cast<FunctionDecl>(D))
    return FD->getParamDecl(Idx);
  return nullptr;
}

QualType getFunctionParamType(const Decl *D, unsigned Idx) {
  if (const FunctionType *FnTy = D->getFunctionType())
    return cast<FunctionProtoType>(FnTy)->getParamType(Idx);
  llvm_unreachable("expected function declaration");
}

SourceRange getFunctionParamRange(const Decl *D, unsigned Idx) {
  if (auto *PVD = getFunctionParam(D, Idx))
    return PVD->getSourceRange();
  return SourceRange();
}

QualType getFunctionResultType(const Decl *D) {
  if (const FunctionType *FnTy = D->getFunctionType())
    return FnTy->getReturnType();
  llvm_unreachable("expected function declaration");
}

SourceRange getFunctionResultSourceRange(const Decl *D) {
  if (const auto *FD = dyn_cast<FunctionDecl>(D))
    return FD->getReturnTypeSourceRange();
  return SourceRange();
}

bool isVariadicFunctionDecl(const Decl *D) {
  if (const FunctionType *FnTy = D->getFunctionType())
    return cast<FunctionProtoType>(FnTy)->isVariadic();
  llvm_unreachable("expected function declaration");
}

unsigned getNumAttributeArgs(const ParsedAttr &AL) {
  return AL.getNumArgs() + AL.hasParsedType();
}

template <typename AttrInfo>
std::enable_if_t<std::is_base_of_v<Attr, AttrInfo>, SourceLocation>
getAttrLoc(const AttrInfo &AL) {
  return AL.getLocation();
}
SourceLocation getAttrLoc(const ParsedAttr &AL) { return AL.getLoc(); }

template <typename AttrInfo>
bool checkUInt32Argument(Sema &S, const AttrInfo &AI, const Expr *Expr,
                         uint32_t &Val, unsigned Idx = UINT_MAX,
                         bool StrictlyUnsigned = false) {
  std::optional<llvm::APSInt> I = llvm::APSInt(32);
  if (!(I = Expr->getIntegerConstantExpr(S.Context))) {
    if (Idx != UINT_MAX)
      S.Diag(getAttrLoc(AI), diag::err_attribute_argument_n_type)
          << &AI << Idx << AANT_ArgumentIntegerConstant
          << Expr->getSourceRange();
    else
      S.Diag(getAttrLoc(AI), diag::err_attribute_argument_type)
          << &AI << AANT_ArgumentIntegerConstant << Expr->getSourceRange();
    return false;
  }

  if (!I->isIntN(32)) {
    S.Diag(Expr->getExprLoc(), diag::err_ice_too_large)
        << toString(*I, 10, false) << 32 << /* Unsigned */ 1;
    return false;
  }

  if (StrictlyUnsigned && I->isSigned() && I->isNegative()) {
    S.Diag(getAttrLoc(AI), diag::err_attribute_requires_positive_integer)
        << &AI << /*non-negative*/ 1;
    return false;
  }

  Val = (uint32_t)I->getZExtValue();
  return true;
}

template <typename AttrInfo>
bool checkPositiveIntArgument(Sema &S, const AttrInfo &AI, const Expr *Expr,
                              int &Val, unsigned Idx = UINT_MAX) {
  uint32_t UVal;
  if (!checkUInt32Argument(S, AI, Expr, UVal, Idx))
    return false;

  if (UVal > (uint32_t)std::numeric_limits<int>::max()) {
    llvm::APSInt I(32); // for toString
    I = UVal;
    S.Diag(Expr->getExprLoc(), diag::err_ice_too_large)
        << toString(I, 10, false) << 32 << /* Unsigned */ 0;
    return false;
  }

  Val = UVal;
  return true;
}

template <typename AttrTy>
bool checkAttrMutualExclusion(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (const auto *A = D->getAttr<AttrTy>()) {
    S.Diag(AL.getLoc(), diag::err_attributes_are_not_compatible)
        << AL << A
        << (AL.isRegularKeywordAttribute() || A->isRegularKeywordAttribute());
    S.Diag(A->getLocation(), diag::note_conflicting_attribute);
    return true;
  }
  return false;
}

template <typename AttrTy>
bool checkAttrMutualExclusion(Sema &S, Decl *D, const Attr &AL) {
  if (const auto *A = D->getAttr<AttrTy>()) {
    S.Diag(AL.getLocation(), diag::err_attributes_are_not_compatible)
        << &AL << A
        << (AL.isRegularKeywordAttribute() || A->isRegularKeywordAttribute());
    S.Diag(A->getLocation(), diag::note_conflicting_attribute);
    return true;
  }
  return false;
}

template <typename AttrInfo>
bool checkFunctionOrMethodParameterIndex(Sema &S, const Decl *D,
                                         const AttrInfo &AI,
                                         unsigned AttrArgNum,
                                         const Expr *IdxExpr, ParamIdx &Idx) {
  assert(isFunctionOrMethod(D));

  // Parameter indices are 1-based (GNU attribute convention).
  bool HP = hasFunctionProto(D);
  bool IV = HP && isVariadicFunctionDecl(D);
  unsigned NumParams = HP ? getFunctionNumParams(D) : 0;

  std::optional<llvm::APSInt> IdxInt;
  if (!(IdxInt = IdxExpr->getIntegerConstantExpr(S.Context))) {
    S.Diag(getAttrLoc(AI), diag::err_attribute_argument_n_type)
        << &AI << AttrArgNum << AANT_ArgumentIntegerConstant
        << IdxExpr->getSourceRange();
    return false;
  }

  unsigned IdxSource = IdxInt->getLimitedValue(UINT_MAX);
  if (IdxSource < 1 || (!IV && IdxSource > NumParams)) {
    S.Diag(getAttrLoc(AI), diag::err_attribute_argument_out_of_bounds)
        << &AI << AttrArgNum << IdxExpr->getSourceRange();
    return false;
  }

  Idx = ParamIdx(IdxSource, D);
  return true;
}

} // namespace

bool Sema::checkStringLiteralArgumentAttr(const AttributeCommonInfo &CI,
                                          const Expr *E, llvm::StringRef &Str,
                                          SourceLocation *ArgLocation) {
  const auto *Literal = dyn_cast<StringLiteral>(E->IgnoreParenCasts());
  if (ArgLocation)
    *ArgLocation = E->getBeginLoc();

  if (!Literal || (!Literal->isUnevaluated() && !Literal->isOrdinary())) {
    Diag(E->getBeginLoc(), diag::err_attribute_argument_type)
        << CI << AANT_ArgumentString;
    return false;
  }

  Str = Literal->getString();
  return true;
}

bool Sema::checkStringLiteralArgumentAttr(const ParsedAttr &AL, unsigned ArgNum,
                                          llvm::StringRef &Str,
                                          SourceLocation *ArgLocation) {
  // Look for identifiers. If we have one emit a hint to fix it to a literal.
  if (AL.isArgIdent(ArgNum)) {
    IdentifierLoc *Loc = AL.getArgAsIdent(ArgNum);
    Diag(Loc->Loc, diag::err_attribute_argument_type)
        << AL << AANT_ArgumentString
        << FixItHint::CreateInsertion(Loc->Loc, "\"")
        << FixItHint::CreateInsertion(getLocForEndOfToken(Loc->Loc), "\"");
    Str = Loc->Ident->getName();
    if (ArgLocation)
      *ArgLocation = Loc->Loc;
    return true;
  }

  // Now check for an actual string literal.
  Expr *ArgExpr = AL.getArgAsExpr(ArgNum);
  const auto *Literal = dyn_cast<StringLiteral>(ArgExpr->IgnoreParenCasts());
  if (ArgLocation)
    *ArgLocation = ArgExpr->getBeginLoc();

  if (!Literal || (!Literal->isUnevaluated() && !Literal->isOrdinary())) {
    Diag(ArgExpr->getBeginLoc(), diag::err_attribute_argument_type)
        << AL << AANT_ArgumentString;
    return false;
  }
  Str = Literal->getString();
  return checkStringLiteralArgumentAttr(AL, ArgExpr, Str, ArgLocation);
}

// ===----------------------------------------------------------------------===
// Attribute handlers
// ===----------------------------------------------------------------------===

namespace {

template <typename AttrType>
void handleSimpleAttribute(Sema &S, Decl *D, const AttributeCommonInfo &CI) {
  D->addAttr(::new (S.Context) AttrType(S.Context, CI));
}

template <typename AttrInfo>
bool checkParamIsIntegerType(Sema &S, const Decl *D, const AttrInfo &AI,
                             unsigned AttrArgNo) {
  assert(AI.isArgExpr(AttrArgNo) && "Expected expression argument");
  Expr *AttrArg = AI.getArgAsExpr(AttrArgNo);
  ParamIdx Idx;
  if (!checkFunctionOrMethodParameterIndex(S, D, AI, AttrArgNo + 1, AttrArg,
                                           Idx))
    return false;

  QualType ParamTy = getFunctionParamType(D, Idx.getASTIndex());
  if (!ParamTy->isIntegerType() && !ParamTy->isCharType()) {
    SourceLocation SrcLoc = AttrArg->getBeginLoc();
    S.Diag(SrcLoc, diag::err_attribute_integers_only)
        << AI << getFunctionParamRange(D, Idx.getASTIndex());
    return false;
  }
  return true;
}

void handleAllocSizeAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (!AL.checkAtLeastNumArgs(S, 1) || !AL.checkAtMostNumArgs(S, 2))
    return;

  assert(isFunctionOrMethod(D) && hasFunctionProto(D));

  QualType RetTy = getFunctionResultType(D);
  if (!RetTy->isPointerType()) {
    S.Diag(AL.getLoc(), diag::warn_attribute_return_pointers_only) << AL;
    return;
  }

  const Expr *SizeExpr = AL.getArgAsExpr(0);
  int SizeArgNoVal;
  // Parameter indices are 1-indexed, hence Index=1
  if (!checkPositiveIntArgument(S, AL, SizeExpr, SizeArgNoVal, /*Idx=*/1))
    return;
  if (!checkParamIsIntegerType(S, D, AL, /*AttrArgNo=*/0))
    return;
  ParamIdx SizeArgNo(SizeArgNoVal, D);

  ParamIdx NumberArgNo;
  if (AL.getNumArgs() == 2) {
    const Expr *NumberExpr = AL.getArgAsExpr(1);
    int Val;
    // Parameter indices are 1-based, hence Index=2
    if (!checkPositiveIntArgument(S, AL, NumberExpr, Val, /*Idx=*/2))
      return;
    if (!checkParamIsIntegerType(S, D, AL, /*AttrArgNo=*/1))
      return;
    NumberArgNo = ParamIdx(Val, D);
  }

  D->addAttr(::new (S.Context)
                 AllocSizeAttr(S.Context, AL, SizeArgNo, NumberArgNo));
}

bool checkFunctionConditionAttr(Sema &S, Decl *D, const ParsedAttr &AL,
                                Expr *&Cond, llvm::StringRef &Msg) {
  Cond = AL.getArgAsExpr(0);
  if (!Cond->containsErrors()) {
    ExprResult Converted = S.PerformContextuallyConvertToBool(Cond);
    if (Converted.isInvalid())
      return false;
    Cond = Converted.get();
  }

  if (!S.checkStringLiteralArgumentAttr(AL, 1, Msg))
    return false;

  if (Msg.empty())
    Msg = "<no message provided>";

  llvm::SmallVector<PartialDiagnosticAt, 8> Diags;
  if (isa<FunctionDecl>(D) && !Cond->containsErrors() &&
      !Expr::isPotentialConstantExprUnevaluated(Cond, cast<FunctionDecl>(D),
                                                Diags)) {
    S.Diag(AL.getLoc(), diag::err_attr_cond_never_constant_expr) << AL;
    for (const PartialDiagnosticAt &PDiag : Diags)
      S.Diag(PDiag.first, PDiag.second);
    return false;
  }
  return true;
}

void handleEnableIfAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  S.Diag(AL.getLoc(), diag::ext_neverc_enable_if);

  Expr *Cond;
  llvm::StringRef Msg;
  if (checkFunctionConditionAttr(S, D, AL, Cond, Msg))
    D->addAttr(::new (S.Context) EnableIfAttr(S.Context, AL, Cond, Msg));
}

void handleErrorAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  llvm::StringRef NewUserDiagnostic;
  if (!S.checkStringLiteralArgumentAttr(AL, 0, NewUserDiagnostic))
    return;
  if (ErrorAttr *EA = S.mergeErrorAttr(D, AL, NewUserDiagnostic))
    D->addAttr(EA);
}

namespace {
class ArgumentDependenceChecker
    : public RecursiveTreeVisitor<ArgumentDependenceChecker> {
  llvm::SmallPtrSet<const ParmVarDecl *, 16> Parms;
  bool Result;

public:
  ArgumentDependenceChecker(const FunctionDecl *FD) {
    Parms.insert(FD->param_begin(), FD->param_end());
  }

  bool referencesArgs(Expr *E) {
    Result = false;
    TraverseStmt(E);
    return Result;
  }

  bool VisitDeclRefExpr(DeclRefExpr *DRE) {
    if (const auto *PVD = dyn_cast<ParmVarDecl>(DRE->getDecl()))
      if (Parms.contains(PVD)) {
        Result = true;
        return false;
      }
    return true;
  }
};
} // namespace

void handleDiagnoseAsBuiltinAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  const auto *DeclFD = cast<FunctionDecl>(D);

  auto DiagnoseType = [&](unsigned Index, AttributeArgumentNType T) {
    SourceLocation Loc = [&]() {
      auto Union = AL.getArg(Index - 1);
      if (Union.is<Expr *>())
        return Union.get<Expr *>()->getBeginLoc();
      return Union.get<IdentifierLoc *>()->Loc;
    }();

    S.Diag(Loc, diag::err_attribute_argument_n_type) << AL << Index << T;
  };

  FunctionDecl *AttrFD = [&]() -> FunctionDecl * {
    if (!AL.isArgExpr(0))
      return nullptr;
    auto *F = dyn_cast_if_present<DeclRefExpr>(AL.getArgAsExpr(0));
    if (!F)
      return nullptr;
    return dyn_cast_if_present<FunctionDecl>(F->getFoundDecl());
  }();

  if (!AttrFD || !AttrFD->getBuiltinID(true)) {
    DiagnoseType(1, AANT_ArgumentBuiltinFunction);
    return;
  }

  if (AttrFD->getNumParams() != AL.getNumArgs() - 1) {
    S.Diag(AL.getLoc(), diag::err_attribute_wrong_number_arguments_for)
        << AL << AttrFD << AttrFD->getNumParams();
    return;
  }

  llvm::SmallVector<unsigned, 8> Indices;

  for (unsigned I = 1; I < AL.getNumArgs(); ++I) {
    if (!AL.isArgExpr(I)) {
      DiagnoseType(I + 1, AANT_ArgumentIntegerConstant);
      return;
    }

    const Expr *IndexExpr = AL.getArgAsExpr(I);
    uint32_t Index;

    if (!checkUInt32Argument(S, AL, IndexExpr, Index, I + 1, false))
      return;

    if (Index > DeclFD->getNumParams()) {
      S.Diag(AL.getLoc(), diag::err_attribute_bounds_for_function)
          << AL << Index << DeclFD << DeclFD->getNumParams();
      return;
    }

    QualType T1 = AttrFD->getParamDecl(I - 1)->getType();
    QualType T2 = DeclFD->getParamDecl(Index - 1)->getType();

    if (T1.getCanonicalType().getUnqualifiedType() !=
        T2.getCanonicalType().getUnqualifiedType()) {
      S.Diag(IndexExpr->getBeginLoc(), diag::err_attribute_parameter_types)
          << AL << Index << DeclFD << T2 << I << AttrFD << T1;
      return;
    }

    Indices.push_back(Index - 1);
  }

  D->addAttr(::new (S.Context) DiagnoseAsBuiltinAttr(
      S.Context, AL, AttrFD, Indices.data(), Indices.size()));
}

void handleDiagnoseIfAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  S.Diag(AL.getLoc(), diag::ext_neverc_diagnose_if);

  Expr *Cond;
  llvm::StringRef Msg;
  if (!checkFunctionConditionAttr(S, D, AL, Cond, Msg))
    return;

  llvm::StringRef DiagTypeStr;
  if (!S.checkStringLiteralArgumentAttr(AL, 2, DiagTypeStr))
    return;

  DiagnoseIfAttr::DiagnosticType DiagType;
  if (!DiagnoseIfAttr::ConvertStrToDiagnosticType(DiagTypeStr, DiagType)) {
    S.Diag(AL.getArgAsExpr(2)->getBeginLoc(),
           diag::err_diagnose_if_invalid_diagnostic_type);
    return;
  }

  bool ArgDependent = false;
  if (const auto *FD = dyn_cast<FunctionDecl>(D))
    ArgDependent = ArgumentDependenceChecker(FD).referencesArgs(Cond);
  D->addAttr(::new (S.Context) DiagnoseIfAttr(
      S.Context, AL, Cond, Msg, DiagType, ArgDependent, cast<NamedDecl>(D)));
}

void handleNoBuiltinAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  static constexpr const llvm::StringRef kWildcard = "*";

  llvm::SmallVector<llvm::StringRef, 16> Names;
  bool HasWildcard = false;

  const auto AddBuiltinName = [&Names, &HasWildcard](llvm::StringRef Name) {
    if (Name == kWildcard)
      HasWildcard = true;
    Names.push_back(Name);
  };

  // Add previously defined attributes.
  if (const auto *NBA = D->getAttr<NoBuiltinAttr>())
    for (llvm::StringRef BuiltinName : NBA->builtinNames())
      AddBuiltinName(BuiltinName);

  // Add current attributes.
  if (AL.getNumArgs() == 0)
    AddBuiltinName(kWildcard);
  else
    for (unsigned I = 0, E = AL.getNumArgs(); I != E; ++I) {
      llvm::StringRef BuiltinName;
      SourceLocation LiteralLoc;
      if (!S.checkStringLiteralArgumentAttr(AL, I, BuiltinName, &LiteralLoc))
        return;

      if (Builtin::Context::isBuiltinFunc(BuiltinName))
        AddBuiltinName(BuiltinName);
      else
        S.Diag(LiteralLoc, diag::warn_attribute_no_builtin_invalid_builtin_name)
            << BuiltinName << AL;
    }

  // Repeating the same attribute is fine.
  llvm::sort(Names);
  Names.erase(std::unique(Names.begin(), Names.end()), Names.end());

  // Empty no_builtin must be on its own.
  if (HasWildcard && Names.size() > 1)
    S.Diag(D->getLocation(),
           diag::err_attribute_no_builtin_wildcard_or_builtin_name)
        << AL;

  if (D->hasAttr<NoBuiltinAttr>())
    D->dropAttr<NoBuiltinAttr>();
  D->addAttr(::new (S.Context)
                 NoBuiltinAttr(S.Context, AL, Names.data(), Names.size()));
}

void handlePassObjectSizeAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (D->hasAttr<PassObjectSizeAttr>()) {
    S.Diag(D->getBeginLoc(), diag::err_attribute_only_once_per_parameter) << AL;
    return;
  }

  Expr *E = AL.getArgAsExpr(0);
  uint32_t Type;
  if (!checkUInt32Argument(S, AL, E, Type, /*Idx=*/1))
    return;

  // pass_object_size's argument is passed in as the second argument of
  // __builtin_object_size. So, it has the same constraints as that second
  // argument; namely, it must be in the range [0, 3].
  if (Type > 3) {
    S.Diag(E->getBeginLoc(), diag::err_attribute_argument_out_of_range)
        << AL << 0 << 3 << E->getSourceRange();
    return;
  }

  // pass_object_size is only supported on constant pointer parameters; as a
  // kindness to users, we allow the parameter to be non-const for declarations.
  // At this point, we have no clue if `D` belongs to a function declaration or
  // definition, so we defer the constness check until later.
  if (!cast<ParmVarDecl>(D)->getType()->isPointerType()) {
    S.Diag(D->getBeginLoc(), diag::err_attribute_pointers_only) << AL << 1;
    return;
  }

  D->addAttr(::new (S.Context) PassObjectSizeAttr(S.Context, AL, (int)Type));
}

void handleExtVectorTypeAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  // Remember this typedef decl, we will need it later for diagnostics.
  S.ExtVectorDecls.push_back(cast<TypedefNameDecl>(D));
}

void handlePackedAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (auto *TD = dyn_cast<TagDecl>(D))
    TD->addAttr(::new (S.Context) PackedAttr(S.Context, AL));
  else if (auto *FD = dyn_cast<FieldDecl>(D)) {
    bool BitfieldByteAligned =
        (!FD->getType()->isIncompleteType() && FD->isBitField() &&
         S.Context.getTypeAlign(FD->getType()) <= 8);

    if (BitfieldByteAligned)
      S.Diag(AL.getLoc(), diag::warn_attribute_packed_for_bitfield);

    FD->addAttr(::new (S.Context) PackedAttr(S.Context, AL));

  } else
    S.Diag(AL.getLoc(), diag::warn_attribute_ignored) << AL;
}

} // namespace

bool Sema::isValidPointerAttrType(QualType T) {
  // The nonnull attribute, and other similar attributes, can be applied to a
  // transparent union that contains a pointer type.
  if (const RecordType *UT = T->getAsUnionType()) {
    if (UT && UT->getDecl()->hasAttr<TransparentUnionAttr>()) {
      RecordDecl *UD = UT->getDecl();
      for (const auto *I : UD->fields()) {
        QualType QT = I->getType();
        if (QT->isAnyPointerType())
          return true;
      }
    }
  }

  return T->isAnyPointerType();
}

namespace {

bool attrNonNullArgCheck(Sema &S, QualType T, const ParsedAttr &AL,
                         SourceRange AttrParmRange, SourceRange TypeRange,
                         bool isReturnValue = false) {
  if (!S.isValidPointerAttrType(T)) {
    if (isReturnValue)
      S.Diag(AL.getLoc(), diag::warn_attribute_return_pointers_only)
          << AL << AttrParmRange << TypeRange;
    else
      S.Diag(AL.getLoc(), diag::warn_attribute_pointers_only)
          << AL << AttrParmRange << TypeRange << 0;
    return false;
  }
  return true;
}

void handleNonNullAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  llvm::SmallVector<ParamIdx, 8> NonNullArgs;
  for (unsigned I = 0; I < AL.getNumArgs(); ++I) {
    Expr *Ex = AL.getArgAsExpr(I);
    ParamIdx Idx;
    if (!checkFunctionOrMethodParameterIndex(S, D, AL, I + 1, Ex, Idx))
      return;

    // Is the function argument a pointer type?
    if (Idx.getASTIndex() < getFunctionNumParams(D) &&
        !attrNonNullArgCheck(S, getFunctionParamType(D, Idx.getASTIndex()), AL,
                             Ex->getSourceRange(),
                             getFunctionParamRange(D, Idx.getASTIndex())))
      continue;

    NonNullArgs.push_back(Idx);
  }

  // If no arguments were specified to __attribute__((nonnull)) then all pointer
  // arguments have a nonnull attribute; warn if there aren't any. Skip this
  // check if the attribute came from a macro expansion.
  if (NonNullArgs.empty() && AL.getLoc().isFileID()) {
    bool AnyPointers = isVariadicFunctionDecl(D);
    for (unsigned I = 0, E = getFunctionNumParams(D); I != E && !AnyPointers;
         ++I) {
      QualType T = getFunctionParamType(D, I);
      if (S.isValidPointerAttrType(T))
        AnyPointers = true;
    }

    if (!AnyPointers)
      S.Diag(AL.getLoc(), diag::warn_attribute_nonnull_no_pointers);
  }

  ParamIdx *Start = NonNullArgs.data();
  unsigned Size = NonNullArgs.size();
  llvm::array_pod_sort(Start, Start + Size);
  D->addAttr(::new (S.Context) NonNullAttr(S.Context, AL, Start, Size));
}

void handleNonNullAttrParameter(Sema &S, ParmVarDecl *D, const ParsedAttr &AL) {
  if (AL.getNumArgs() > 0) {
    if (D->getFunctionType()) {
      handleNonNullAttr(S, D, AL);
    } else {
      S.Diag(AL.getLoc(), diag::warn_attribute_nonnull_parm_no_args)
          << D->getSourceRange();
    }
    return;
  }

  // Is the argument a pointer type?
  if (!attrNonNullArgCheck(S, D->getType(), AL, SourceRange(),
                           D->getSourceRange()))
    return;

  D->addAttr(::new (S.Context) NonNullAttr(S.Context, AL, nullptr, 0));
}

void handleReturnsNonNullAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  QualType ResultType = getFunctionResultType(D);
  SourceRange SR = getFunctionResultSourceRange(D);
  if (!attrNonNullArgCheck(S, ResultType, AL, SourceRange(), SR,
                           /* isReturnValue */ true))
    return;

  D->addAttr(::new (S.Context) ReturnsNonNullAttr(S.Context, AL));
}

void handleNoEscapeAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (D->isInvalidDecl())
    return;

  // noescape only applies to pointer types.
  QualType T = cast<ParmVarDecl>(D)->getType();
  if (!S.isValidPointerAttrType(T)) {
    S.Diag(AL.getLoc(), diag::warn_attribute_pointers_only)
        << AL << AL.getRange() << 0;
    return;
  }

  D->addAttr(::new (S.Context) NoEscapeAttr(S.Context, AL));
}

void handleAssumeAlignedAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  Expr *E = AL.getArgAsExpr(0),
       *OE = AL.getNumArgs() > 1 ? AL.getArgAsExpr(1) : nullptr;
  S.AddAssumeAlignedAttr(D, AL, E, OE);
}

void handleAllocAlignAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  S.AddAllocAlignAttr(D, AL, AL.getArgAsExpr(0));
}

} // namespace

void Sema::AddAssumeAlignedAttr(Decl *D, const AttributeCommonInfo &CI, Expr *E,
                                Expr *OE) {
  QualType ResultType = getFunctionResultType(D);
  SourceRange SR = getFunctionResultSourceRange(D);

  AssumeAlignedAttr TmpAttr(Context, CI, E, OE);
  SourceLocation AttrLoc = TmpAttr.getLocation();

  if (!isValidPointerAttrType(ResultType)) {
    Diag(AttrLoc, diag::warn_attribute_return_pointers_refs_only)
        << &TmpAttr << TmpAttr.getRange() << SR;
    return;
  }

  {
    std::optional<llvm::APSInt> I = llvm::APSInt(64);
    if (!(I = E->getIntegerConstantExpr(Context))) {
      if (OE)
        Diag(AttrLoc, diag::err_attribute_argument_n_type)
            << &TmpAttr << 1 << AANT_ArgumentIntegerConstant
            << E->getSourceRange();
      else
        Diag(AttrLoc, diag::err_attribute_argument_type)
            << &TmpAttr << AANT_ArgumentIntegerConstant << E->getSourceRange();
      return;
    }

    if (!I->isPowerOf2()) {
      Diag(AttrLoc, diag::err_alignment_not_power_of_two)
          << E->getSourceRange();
      return;
    }

    if (*I > Sema::MaximumAlignment)
      Diag(CI.getLoc(), diag::warn_assume_aligned_too_great)
          << CI.getRange() << Sema::MaximumAlignment;
  }

  if (OE && !OE->isIntegerConstantExpr(Context)) {
    Diag(AttrLoc, diag::err_attribute_argument_n_type)
        << &TmpAttr << 2 << AANT_ArgumentIntegerConstant
        << OE->getSourceRange();
    return;
  }

  D->addAttr(::new (Context) AssumeAlignedAttr(Context, CI, E, OE));
}

void Sema::AddAllocAlignAttr(Decl *D, const AttributeCommonInfo &CI,
                             Expr *ParamExpr) {
  QualType ResultType = getFunctionResultType(D);

  AllocAlignAttr TmpAttr(Context, CI, ParamIdx());
  SourceLocation AttrLoc = CI.getLoc();

  if (!isValidPointerAttrType(ResultType)) {
    Diag(AttrLoc, diag::warn_attribute_return_pointers_refs_only)
        << &TmpAttr << CI.getRange() << getFunctionResultSourceRange(D);
    return;
  }

  ParamIdx Idx;
  const auto *FuncDecl = cast<FunctionDecl>(D);
  if (!checkFunctionOrMethodParameterIndex(*this, FuncDecl, TmpAttr,
                                           /*AttrArgNum=*/1, ParamExpr, Idx))
    return;

  QualType Ty = getFunctionParamType(D, Idx.getASTIndex());
  if (!Ty->isIntegralType(Context)) {
    Diag(ParamExpr->getBeginLoc(), diag::err_attribute_integers_only)
        << &TmpAttr
        << FuncDecl->getParamDecl(Idx.getASTIndex())->getSourceRange();
    return;
  }

  D->addAttr(::new (Context) AllocAlignAttr(Context, CI, Idx));
}

namespace {

void checkAssumptionAttr(Sema &S, SourceLocation Loc,
                         llvm::StringRef AssumptionStr) {
  if (llvm::KnownAssumptionStrings.contains(AssumptionStr))
    return;

  unsigned BestEditDistance = 3;
  llvm::StringRef Suggestion;
  for (const auto &KnownAssumptionIt : llvm::KnownAssumptionStrings) {
    unsigned EditDistance =
        AssumptionStr.edit_distance(KnownAssumptionIt.getKey());
    if (EditDistance < BestEditDistance) {
      Suggestion = KnownAssumptionIt.getKey();
      BestEditDistance = EditDistance;
    }
  }

  if (!Suggestion.empty())
    S.Diag(Loc, diag::warn_assume_attribute_string_unknown_suggested)
        << AssumptionStr << Suggestion;
  else
    S.Diag(Loc, diag::warn_assume_attribute_string_unknown) << AssumptionStr;
}

void handleAssumumptionAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  llvm::StringRef Str;
  SourceLocation AttrStrLoc;
  if (!S.checkStringLiteralArgumentAttr(AL, 0, Str, &AttrStrLoc))
    return;

  checkAssumptionAttr(S, AttrStrLoc, Str);

  D->addAttr(::new (S.Context) AssumptionAttr(S.Context, AL, Str));
}

bool normalizeName(llvm::StringRef &AttrName) {
  if (AttrName.size() > 4 && AttrName.starts_with("__") &&
      AttrName.ends_with("__")) {
    AttrName = AttrName.drop_front(2).drop_back(2);
    return true;
  }
  return false;
}

void handleWeakRefAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (AL.getNumArgs() > 1) {
    S.Diag(AL.getLoc(), diag::err_attribute_wrong_number_arguments) << AL << 1;
    return;
  }

  // gcc rejects
  // struct c {
  //   static int a __attribute__((weakref ("v2")));
  //   static int b() __attribute__((weakref ("f3")));
  // };
  // and ignores the attributes of
  // void f(void) {
  //   static int a __attribute__((weakref ("v2")));
  // }
  // we reject them
  const DeclContext *Ctx = D->getDeclContext()->getRedeclContext();
  if (!Ctx->isFileContext()) {
    S.Diag(AL.getLoc(), diag::err_attribute_weakref_not_global_context)
        << cast<NamedDecl>(D);
    return;
  }

  // The GCC manual says
  //
  // At present, a declaration to which `weakref' is attached can only
  // be `static'.
  //
  // It also says
  //
  // Without a TARGET,
  // given as an argument to `weakref' or to `alias', `weakref' is
  // equivalent to `weak'.
  //
  // gcc 4.4.1 will accept
  // int a7 __attribute__((weakref));
  // as
  // int a7 __attribute__((weak));
  // This looks like a bug in gcc. We reject that for now. We should revisit
  // it if this behaviour is actually used.

  // GCC rejects
  // static ((alias ("y"), weakref)).
  // Should we? How to check that weakref is before or after alias?

  // We keep the WeakRefAttr as an AliasAttr.  The WeakRefAttr never uses the
  // llvm::StringRef parameter it was given anyway.
  llvm::StringRef Str;
  if (AL.getNumArgs() && S.checkStringLiteralArgumentAttr(AL, 0, Str))
    // GCC will accept anything as the argument of weakref. Should we
    // check for an existing decl?
    D->addAttr(::new (S.Context) AliasAttr(S.Context, AL, Str));

  D->addAttr(::new (S.Context) WeakRefAttr(S.Context, AL));
}

void handleIFuncAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  llvm::StringRef Str;
  if (!S.checkStringLiteralArgumentAttr(AL, 0, Str))
    return;

  // Aliases should be on declarations, not definitions.
  const auto *FD = cast<FunctionDecl>(D);
  if (FD->isThisDeclarationADefinition()) {
    S.Diag(AL.getLoc(), diag::err_alias_is_definition) << FD << 1;
    return;
  }

  D->addAttr(::new (S.Context) IFuncAttr(S.Context, AL, Str));
}

void handleAliasAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  llvm::StringRef Str;
  if (!S.checkStringLiteralArgumentAttr(AL, 0, Str))
    return;

  if (S.Context.getTargetInfo().getTriple().isOSDarwin()) {
    S.Diag(AL.getLoc(), diag::err_alias_not_supported_on_darwin);
    return;
  }

  // Aliases should be on declarations, not definitions.
  if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
    if (FD->isThisDeclarationADefinition()) {
      S.Diag(AL.getLoc(), diag::err_alias_is_definition) << FD << 0;
      return;
    }
  } else {
    const auto *VD = cast<VarDecl>(D);
    if (VD->isThisDeclarationADefinition() && VD->isExternallyVisible()) {
      S.Diag(AL.getLoc(), diag::err_alias_is_definition) << VD << 0;
      return;
    }
  }

  {
    const DeclarationNameInfo target(&S.Context.Idents.get(Str), AL.getLoc());
    LookupResult LR(S, target, neverc::ResolveOrdinary);
    if (S.LookupQualifiedName(LR, S.getCurLexicalContext()))
      for (NamedDecl *ND : LR)
        ND->markUsed(S.Context);
  }

  D->addAttr(::new (S.Context) AliasAttr(S.Context, AL, Str));
}

void handleTLSModelAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  llvm::StringRef Model;
  SourceLocation LiteralLoc;
  // Check that it is a string.
  if (!S.checkStringLiteralArgumentAttr(AL, 0, Model, &LiteralLoc))
    return;

  if (Model != "global-dynamic" && Model != "local-dynamic" &&
      Model != "initial-exec" && Model != "local-exec") {
    S.Diag(LiteralLoc, diag::err_attr_tlsmodel_arg);
    return;
  }

  D->addAttr(::new (S.Context) TLSModelAttr(S.Context, AL, Model));
}

void handleRestrictAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  QualType ResultType = getFunctionResultType(D);
  if (ResultType->isAnyPointerType()) {
    D->addAttr(::new (S.Context) RestrictAttr(S.Context, AL));
    return;
  }

  S.Diag(AL.getLoc(), diag::warn_attribute_return_pointers_only)
      << AL << getFunctionResultSourceRange(D);
}

void handleCPUSpecificAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  // Ensure we don't combine these with themselves, since that causes some
  // confusing behavior.
  if (AL.getParsedKind() == ParsedAttr::AT_CPUDispatch) {
    if (checkAttrMutualExclusion<CPUSpecificAttr>(S, D, AL))
      return;

    if (const auto *Other = D->getAttr<CPUDispatchAttr>()) {
      S.Diag(AL.getLoc(), diag::err_disallowed_duplicate_attribute) << AL;
      S.Diag(Other->getLocation(), diag::note_conflicting_attribute);
      return;
    }
  } else if (AL.getParsedKind() == ParsedAttr::AT_CPUSpecific) {
    if (checkAttrMutualExclusion<CPUDispatchAttr>(S, D, AL))
      return;

    if (const auto *Other = D->getAttr<CPUSpecificAttr>()) {
      S.Diag(AL.getLoc(), diag::err_disallowed_duplicate_attribute) << AL;
      S.Diag(Other->getLocation(), diag::note_conflicting_attribute);
      return;
    }
  }

  FunctionDecl *FD = cast<FunctionDecl>(D);

  if (!AL.checkAtLeastNumArgs(S, 1))
    return;

  llvm::SmallVector<IdentifierInfo *, 8> CPUs;
  for (unsigned ArgNo = 0; ArgNo < getNumAttributeArgs(AL); ++ArgNo) {
    if (!AL.isArgIdent(ArgNo)) {
      S.Diag(AL.getLoc(), diag::err_attribute_argument_type)
          << AL << AANT_ArgumentIdentifier;
      return;
    }

    IdentifierLoc *CPUArg = AL.getArgAsIdent(ArgNo);
    llvm::StringRef CPUName = CPUArg->Ident->getName().trim();

    if (!S.Context.getTargetInfo().validateCPUSpecificCPUDispatch(CPUName)) {
      S.Diag(CPUArg->Loc, diag::err_invalid_cpu_specific_dispatch_value)
          << CPUName << (AL.getKind() == ParsedAttr::AT_CPUDispatch);
      return;
    }

    const TargetInfo &Target = S.Context.getTargetInfo();
    if (llvm::any_of(CPUs, [CPUName, &Target](const IdentifierInfo *Cur) {
          return Target.CPUSpecificManglingCharacter(CPUName) ==
                 Target.CPUSpecificManglingCharacter(Cur->getName());
        })) {
      S.Diag(AL.getLoc(), diag::warn_multiversion_duplicate_entries);
      return;
    }
    CPUs.push_back(CPUArg->Ident);
  }

  FD->setIsMultiVersion(true);
  if (AL.getKind() == ParsedAttr::AT_CPUSpecific)
    D->addAttr(::new (S.Context)
                   CPUSpecificAttr(S.Context, AL, CPUs.data(), CPUs.size()));
  else
    D->addAttr(::new (S.Context)
                   CPUDispatchAttr(S.Context, AL, CPUs.data(), CPUs.size()));
}

void handleCommonAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  D->addAttr(::new (S.Context) CommonAttr(S.Context, AL));
}

void handleNakedAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  D->addAttr(::new (S.Context) NakedAttr(S.Context, AL));
}

void handleVolatileAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  D->addAttr(::new (S.Context) VolatileAttr(S.Context, AL));
  D->addAttr(::new (S.Context) UsedAttr(S.Context, AL));
}

void handleDisableTryStmtAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  D->addAttr(::new (S.Context) DisableTryStmtAttr(S.Context, AL));
}

void handleNoReturnAttr(Sema &S, Decl *D, const ParsedAttr &Attrs) {
  if (hasDeclarator(D))
    return;
}

void handleStandardNoReturnAttr(Sema &S, Decl *D, const ParsedAttr &A) {
  // The [[_Noreturn]] spelling is deprecated in C23, so if that was used,
  // issue an appropriate diagnostic. However, don't issue a diagnostic if the
  // attribute name comes from a macro expansion. We don't want to punish users
  // who write [[noreturn]] after including <stdnoreturn.h> (where 'noreturn'
  // is defined as a macro which expands to '_Noreturn').
  if (A.getSemanticSpelling() == StandardNoReturnAttr::C23_Noreturn &&
      !(A.getLoc().isMacroID() &&
        S.getSourceManager().isInSystemMacro(A.getLoc())))
    S.Diag(A.getLoc(), diag::warn_deprecated_noreturn_spelling) << A.getRange();

  D->addAttr(::new (S.Context) StandardNoReturnAttr(S.Context, A));
}

void handleNoCfCheckAttr(Sema &S, Decl *D, const ParsedAttr &Attrs) {
  if (!S.getLangOpts().CFProtectionBranch)
    S.Diag(Attrs.getLoc(), diag::warn_nocf_check_attribute_ignored);
  else
    handleSimpleAttribute<AnyX86NoCfCheckAttr>(S, D, Attrs);
}

} // namespace

// ===----------------------------------------------------------------------===
// Attribute target & section validation
// ===----------------------------------------------------------------------===

bool Sema::CheckAttrNoArgs(const ParsedAttr &Attrs) {
  if (!Attrs.checkExactlyNumArgs(*this, 0)) {
    Attrs.setInvalid();
    return true;
  }

  return false;
}

bool Sema::CheckAttrTarget(const ParsedAttr &AL) {
  if (!AL.existsInTarget(Context.getTargetInfo())) {
    Diag(AL.getLoc(), AL.isRegularKeywordAttribute()
                          ? diag::err_keyword_not_supported_on_target
                          : diag::warn_unknown_attribute_ignored)
        << AL << AL.getRange();
    AL.setInvalid();
    return true;
  }

  return false;
}

void handleAnalyzerNoReturnAttr(Sema &S, Decl *D, const ParsedAttr &AL) {

  // The checking path for 'noreturn' and 'analyzer_noreturn' are different
  // because 'analyzer_noreturn' does not impact the type.
  if (!isFunctionOrMethod(D)) {
    ValueDecl *VD = dyn_cast<ValueDecl>(D);
    if (!VD || !VD->getType()->isFunctionPointerType()) {
      S.Diag(AL.getLoc(), AL.isStandardAttributeSyntax()
                              ? diag::err_attribute_wrong_decl_type
                              : diag::warn_attribute_wrong_decl_type)
          << AL << AL.isRegularKeywordAttribute()
          << ExpectedFunctionMethodOrBlock;
      return;
    }
  }

  D->addAttr(::new (S.Context) AnalyzerNoReturnAttr(S.Context, AL));
}

void handleDependencyAttr(Sema &S, Scope *Scope, Decl *D,
                          const ParsedAttr &AL) {
  if (isa<ParmVarDecl>(D)) {
    // [[carries_dependency]] can only be applied to a parameter if it is a
    // parameter of a function declaration.
    if (!(Scope->getFlags() & neverc::Scope::FunctionDeclarationScope)) {
      S.Diag(AL.getLoc(), diag::err_carries_dependency_param_not_function_decl);
      return;
    }
  }

  D->addAttr(::new (S.Context) CarriesDependencyAttr(S.Context, AL));
}

void handleUnusedAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  bool IsC23Attr = AL.isBracketAttribute() && !AL.getScopeName();

  if (IsC23Attr)
    S.Diag(AL.getLoc(), diag::ext_c23_attr) << AL;

  D->addAttr(::new (S.Context) UnusedAttr(S.Context, AL));
}

void handleConstructorAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  uint32_t priority = ConstructorAttr::DefaultPriority;
  if (AL.getNumArgs() &&
      !checkUInt32Argument(S, AL, AL.getArgAsExpr(0), priority))
    return;

  D->addAttr(::new (S.Context) ConstructorAttr(S.Context, AL, priority));
}

void handleDestructorAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  uint32_t priority = DestructorAttr::DefaultPriority;
  if (AL.getNumArgs() &&
      !checkUInt32Argument(S, AL, AL.getArgAsExpr(0), priority))
    return;

  D->addAttr(::new (S.Context) DestructorAttr(S.Context, AL, priority));
}

template <typename AttrTy>
void handleAttrWithMessage(Sema &S, Decl *D, const ParsedAttr &AL) {
  llvm::StringRef Str;
  if (AL.getNumArgs() == 1 && !S.checkStringLiteralArgumentAttr(AL, 0, Str))
    return;

  D->addAttr(::new (S.Context) AttrTy(S.Context, AL, Str));
}

bool checkAvailabilityAttr(Sema &S, SourceRange Range, IdentifierInfo *Platform,
                           llvm::VersionTuple Introduced,
                           llvm::VersionTuple Deprecated,
                           llvm::VersionTuple Obsoleted) {
  llvm::StringRef PlatformName =
      AvailabilityAttr::getPrettyPlatformName(Platform->getName());
  if (PlatformName.empty())
    PlatformName = Platform->getName();

  // Ensure that Introduced <= Deprecated <= Obsoleted (although not all
  // of these steps are needed).
  if (!Introduced.empty() && !Deprecated.empty() &&
      !(Introduced <= Deprecated)) {
    S.Diag(Range.getBegin(), diag::warn_availability_version_ordering)
        << 1 << PlatformName << Deprecated.getAsString() << 0
        << Introduced.getAsString();
    return true;
  }

  if (!Introduced.empty() && !Obsoleted.empty() && !(Introduced <= Obsoleted)) {
    S.Diag(Range.getBegin(), diag::warn_availability_version_ordering)
        << 2 << PlatformName << Obsoleted.getAsString() << 0
        << Introduced.getAsString();
    return true;
  }

  if (!Deprecated.empty() && !Obsoleted.empty() && !(Deprecated <= Obsoleted)) {
    S.Diag(Range.getBegin(), diag::warn_availability_version_ordering)
        << 2 << PlatformName << Obsoleted.getAsString() << 1
        << Deprecated.getAsString();
    return true;
  }

  return false;
}

bool versionsMatch(const llvm::VersionTuple &X, const llvm::VersionTuple &Y) {
  if (X.empty() || Y.empty())
    return true;
  return X == Y;
}

AvailabilityAttr *Sema::mergeAvailabilityAttr(
    NamedDecl *D, const AttributeCommonInfo &CI, IdentifierInfo *Platform,
    bool Implicit, llvm::VersionTuple Introduced, llvm::VersionTuple Deprecated,
    llvm::VersionTuple Obsoleted, bool IsUnavailable, llvm::StringRef Message,
    bool IsStrict, llvm::StringRef Replacement, int Priority) {
  llvm::VersionTuple MergedIntroduced = Introduced;
  llvm::VersionTuple MergedDeprecated = Deprecated;
  llvm::VersionTuple MergedObsoleted = Obsoleted;
  bool FoundAny = false;

  if (D->hasAttrs()) {
    AttrVec &Attrs = D->getAttrs();
    for (unsigned i = 0, e = Attrs.size(); i != e;) {
      const auto *OldAA = dyn_cast<AvailabilityAttr>(Attrs[i]);
      if (!OldAA) {
        ++i;
        continue;
      }

      IdentifierInfo *OldPlatform = OldAA->getPlatform();
      if (OldPlatform != Platform) {
        ++i;
        continue;
      }

      // If there is an existing availability attribute for this platform that
      // has a lower priority use the existing one and discard the new
      // attribute.
      if (OldAA->getPriority() < Priority)
        return nullptr;

      // If there is an existing attribute for this platform that has a higher
      // priority than the new attribute then erase the old one and continue
      // processing the attributes.
      if (OldAA->getPriority() > Priority) {
        Attrs.erase(Attrs.begin() + i);
        --e;
        continue;
      }

      FoundAny = true;
      llvm::VersionTuple OldIntroduced = OldAA->getIntroduced();
      llvm::VersionTuple OldDeprecated = OldAA->getDeprecated();
      llvm::VersionTuple OldObsoleted = OldAA->getObsoleted();
      bool OldIsUnavailable = OldAA->getUnavailable();

      if (!versionsMatch(OldIntroduced, Introduced) ||
          !versionsMatch(Deprecated, OldDeprecated) ||
          !versionsMatch(Obsoleted, OldObsoleted) ||
          !(OldIsUnavailable == IsUnavailable)) {
        Diag(OldAA->getLocation(), diag::warn_mismatched_availability);
        Diag(CI.getLoc(), diag::note_previous_attribute);

        Attrs.erase(Attrs.begin() + i);
        --e;
        continue;
      }

      llvm::VersionTuple MergedIntroduced2 = MergedIntroduced;
      llvm::VersionTuple MergedDeprecated2 = MergedDeprecated;
      llvm::VersionTuple MergedObsoleted2 = MergedObsoleted;

      if (MergedIntroduced2.empty())
        MergedIntroduced2 = OldIntroduced;
      if (MergedDeprecated2.empty())
        MergedDeprecated2 = OldDeprecated;
      if (MergedObsoleted2.empty())
        MergedObsoleted2 = OldObsoleted;

      if (checkAvailabilityAttr(*this, OldAA->getRange(), Platform,
                                MergedIntroduced2, MergedDeprecated2,
                                MergedObsoleted2)) {
        Attrs.erase(Attrs.begin() + i);
        --e;
        continue;
      }

      MergedIntroduced = MergedIntroduced2;
      MergedDeprecated = MergedDeprecated2;
      MergedObsoleted = MergedObsoleted2;
      ++i;
    }
  }

  if (FoundAny && MergedIntroduced == Introduced &&
      MergedDeprecated == Deprecated && MergedObsoleted == Obsoleted)
    return nullptr;

  if (!checkAvailabilityAttr(*this, CI.getRange(), Platform, MergedIntroduced,
                             MergedDeprecated, MergedObsoleted)) {
    auto *Avail = ::new (Context) AvailabilityAttr(
        Context, CI, Platform, Introduced, Deprecated, Obsoleted, IsUnavailable,
        Message, IsStrict, Replacement, Priority);
    Avail->setImplicit(Implicit);
    return Avail;
  }
  return nullptr;
}

void handleAvailabilityAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (!AL.checkExactlyNumArgs(S, 1))
    return;
  IdentifierLoc *Platform = AL.getArgAsIdent(0);

  IdentifierInfo *II = Platform->Ident;
  if (AvailabilityAttr::getPrettyPlatformName(II->getName()).empty())
    S.Diag(Platform->Loc, diag::warn_availability_unknown_platform)
        << Platform->Ident;

  auto *ND = dyn_cast<NamedDecl>(D);
  if (!ND) // We warned about this already, so just return.
    return;

  AvailabilityChange Introduced = AL.getAvailabilityIntroduced();
  AvailabilityChange Deprecated = AL.getAvailabilityDeprecated();
  AvailabilityChange Obsoleted = AL.getAvailabilityObsoleted();
  bool IsUnavailable = AL.getUnavailableLoc().isValid();
  bool IsStrict = AL.getStrictLoc().isValid();
  llvm::StringRef Str;
  if (const auto *SE = dyn_cast_if_present<StringLiteral>(AL.getMessageExpr()))
    Str = SE->getString();
  llvm::StringRef Replacement;
  if (const auto *SE =
          dyn_cast_if_present<StringLiteral>(AL.getReplacementExpr()))
    Replacement = SE->getString();

  int PriorityModifier =
      AL.isPragmaAttribute() ? Sema::AP_PragmaAttribute : Sema::AP_Explicit;
  AvailabilityAttr *NewAttr = S.mergeAvailabilityAttr(
      ND, AL, II, false /*Implicit*/, Introduced.Version, Deprecated.Version,
      Obsoleted.Version, IsUnavailable, Str, IsStrict, Replacement,
      PriorityModifier);
  if (NewAttr)
    D->addAttr(NewAttr);
}

template <class T>
T *mergeVisibilityAttr(Sema &S, Decl *D, const AttributeCommonInfo &CI,
                       typename T::VisibilityType value) {
  T *existingAttr = D->getAttr<T>();
  if (existingAttr) {
    typename T::VisibilityType existingValue = existingAttr->getVisibility();
    if (existingValue == value)
      return nullptr;
    S.Diag(existingAttr->getLocation(), diag::err_mismatched_visibility);
    S.Diag(CI.getLoc(), diag::note_previous_attribute);
    D->dropAttr<T>();
  }
  return ::new (S.Context) T(S.Context, CI, value);
}

VisibilityAttr *Sema::mergeVisibilityAttr(Decl *D,
                                          const AttributeCommonInfo &CI,
                                          VisibilityAttr::VisibilityType Vis) {
  return ::mergeVisibilityAttr<VisibilityAttr>(*this, D, CI, Vis);
}

TypeVisibilityAttr *
Sema::mergeTypeVisibilityAttr(Decl *D, const AttributeCommonInfo &CI,
                              TypeVisibilityAttr::VisibilityType Vis) {
  return ::mergeVisibilityAttr<TypeVisibilityAttr>(*this, D, CI, Vis);
}

void handleVisibilityAttr(Sema &S, Decl *D, const ParsedAttr &AL,
                          bool isTypeVisibility) {
  // Visibility attributes don't mean anything on a typedef.
  if (isa<TypedefNameDecl>(D)) {
    S.Diag(AL.getRange().getBegin(), diag::warn_attribute_ignored) << AL;
    return;
  }

  // 'type_visibility' only applies to tag types (not file-scope typedefs).
  if (isTypeVisibility && !isa<TagDecl>(D)) {
    S.Diag(AL.getRange().getBegin(), diag::err_attribute_wrong_decl_type)
        << AL << AL.isRegularKeywordAttribute() << ExpectedTag;
    return;
  }

  llvm::StringRef TypeStr;
  SourceLocation LiteralLoc;
  if (!S.checkStringLiteralArgumentAttr(AL, 0, TypeStr, &LiteralLoc))
    return;

  VisibilityAttr::VisibilityType type;
  if (!VisibilityAttr::ConvertStrToVisibilityType(TypeStr, type)) {
    S.Diag(LiteralLoc, diag::warn_attribute_type_not_supported)
        << AL << TypeStr;
    return;
  }

  // Complain about attempts to use protected visibility on targets
  // (like Darwin) that don't support it.
  if (type == VisibilityAttr::Protected &&
      !S.Context.getTargetInfo().hasProtectedVisibility()) {
    S.Diag(AL.getLoc(), diag::warn_attribute_protected_visibility);
    type = VisibilityAttr::Default;
  }

  Attr *newAttr;
  if (isTypeVisibility) {
    newAttr = S.mergeTypeVisibilityAttr(
        D, AL, (TypeVisibilityAttr::VisibilityType)type);
  } else {
    newAttr = S.mergeVisibilityAttr(D, AL, type);
  }
  if (newAttr)
    D->addAttr(newAttr);
}

void handleSentinelAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  unsigned sentinel = (unsigned)SentinelAttr::DefaultSentinel;
  if (AL.getNumArgs() > 0) {
    Expr *E = AL.getArgAsExpr(0);
    std::optional<llvm::APSInt> Idx = llvm::APSInt(32);
    if (!(Idx = E->getIntegerConstantExpr(S.Context))) {
      S.Diag(AL.getLoc(), diag::err_attribute_argument_n_type)
          << AL << 1 << AANT_ArgumentIntegerConstant << E->getSourceRange();
      return;
    }

    if (Idx->isSigned() && Idx->isNegative()) {
      S.Diag(AL.getLoc(), diag::err_attribute_sentinel_less_than_zero)
          << E->getSourceRange();
      return;
    }

    sentinel = Idx->getZExtValue();
  }

  unsigned nullPos = (unsigned)SentinelAttr::DefaultNullPos;
  if (AL.getNumArgs() > 1) {
    Expr *E = AL.getArgAsExpr(1);
    std::optional<llvm::APSInt> Idx = llvm::APSInt(32);
    if (!(Idx = E->getIntegerConstantExpr(S.Context))) {
      S.Diag(AL.getLoc(), diag::err_attribute_argument_n_type)
          << AL << 2 << AANT_ArgumentIntegerConstant << E->getSourceRange();
      return;
    }
    nullPos = Idx->getZExtValue();

    if ((Idx->isSigned() && Idx->isNegative()) || nullPos > 1) {
      S.Diag(AL.getLoc(), diag::err_attribute_sentinel_not_zero_or_one)
          << E->getSourceRange();
      return;
    }
  }

  if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
    const FunctionType *FT = FD->getType()->castAs<FunctionType>();
    if (isa<FunctionNoProtoType>(FT)) {
      S.Diag(AL.getLoc(), diag::warn_attribute_sentinel_named_arguments);
      return;
    }

    if (!cast<FunctionProtoType>(FT)->isVariadic()) {
      S.Diag(AL.getLoc(), diag::warn_attribute_sentinel_not_variadic);
      return;
    }
  } else if (const auto *V = dyn_cast<VarDecl>(D)) {
    QualType Ty = V->getType();
    if (Ty->isFunctionPointerType()) {
      const FunctionType *FT = D->getFunctionType();
      if (!cast<FunctionProtoType>(FT)->isVariadic()) {
        S.Diag(AL.getLoc(), diag::warn_attribute_sentinel_not_variadic);
        return;
      }
    } else {
      S.Diag(AL.getLoc(), diag::warn_attribute_wrong_decl_type)
          << AL << AL.isRegularKeywordAttribute()
          << ExpectedFunctionMethodOrBlock;
      return;
    }
  } else {
    S.Diag(AL.getLoc(), diag::warn_attribute_wrong_decl_type)
        << AL << AL.isRegularKeywordAttribute()
        << ExpectedFunctionMethodOrBlock;
    return;
  }
  D->addAttr(::new (S.Context) SentinelAttr(S.Context, AL, sentinel, nullPos));
}

void handleWarnUnusedResult(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (D->getFunctionType() &&
      D->getFunctionType()->getReturnType()->isVoidType()) {
    S.Diag(AL.getLoc(), diag::warn_attribute_void_function) << AL;
    return;
  }

  llvm::StringRef Str;
  if (AL.isStandardAttributeSyntax() && !AL.getScopeName()) {
    // The standard attribute cannot be applied to variable declarations such
    // as a function pointer.
    if (isa<VarDecl>(D))
      S.Diag(AL.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << AL << AL.isRegularKeywordAttribute()
          << "functions, classes, or enumerations";

    // Standard-attribute spelling may be diagnosed as an extension when the
    // feature is not in the active dialect.
    if (AL.getNumArgs() == 1) {
      if (!S.checkStringLiteralArgumentAttr(AL, 0, Str, nullptr))
        return;
    }
  }

  if ((!AL.isGNUAttribute() &&
       !(AL.isStandardAttributeSyntax() && AL.isNeverCScope())) &&
      isa<TypedefNameDecl>(D)) {
    S.Diag(AL.getLoc(), diag::warn_unused_result_typedef_unsupported_spelling)
        << AL.isGNUScope();
    return;
  }

  D->addAttr(::new (S.Context) WarnUnusedResultAttr(S.Context, AL, Str));
}

void handleWeakImportAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  // weak_import only applies to variable & function declarations.
  bool isDef = false;
  if (!D->canBeWeakImported(isDef)) {
    if (isDef)
      S.Diag(AL.getLoc(), diag::warn_attribute_invalid_on_definition)
          << "weak_import";
    else if (S.Context.getTargetInfo().getTriple().isOSDarwin() &&
             isa<EnumDecl>(D)) {
      // Nothing to warn about here.
    } else
      S.Diag(AL.getLoc(), diag::warn_attribute_wrong_decl_type)
          << AL << AL.isRegularKeywordAttribute() << ExpectedVariableOrFunction;

    return;
  }

  D->addAttr(::new (S.Context) WeakImportAttr(S.Context, AL));
}

SectionAttr *Sema::mergeSectionAttr(Decl *D, const AttributeCommonInfo &CI,
                                    llvm::StringRef Name) {
  if (SectionAttr *ExistingAttr = D->getAttr<SectionAttr>()) {
    if (ExistingAttr->getName() == Name)
      return nullptr;
    Diag(ExistingAttr->getLocation(), diag::warn_mismatched_section)
        << 1 /*section*/;
    Diag(CI.getLoc(), diag::note_previous_attribute);
    return nullptr;
  }
  return ::new (Context) SectionAttr(Context, CI, Name);
}

llvm::Error Sema::isValidSectionSpecifier(llvm::StringRef SecName) {
  if (!Context.getTargetInfo().getTriple().isOSDarwin())
    return llvm::Error::success();

  // Let MCSectionMachO validate this.
  llvm::StringRef Segment, Section;
  unsigned TAA, StubSize;
  bool HasTAA;
  return llvm::MCSectionMachO::ParseSectionSpecifier(SecName, Segment, Section,
                                                     TAA, HasTAA, StubSize);
}

bool Sema::checkSectionName(SourceLocation LiteralLoc,
                            llvm::StringRef SecName) {
  if (llvm::Error E = isValidSectionSpecifier(SecName)) {
    Diag(LiteralLoc, diag::err_attribute_section_invalid_for_target)
        << toString(std::move(E)) << 1 /*'section'*/;
    return false;
  }
  return true;
}

void handleSectionAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  // Make sure that there is a string literal as the sections's single
  // argument.
  llvm::StringRef Str;
  SourceLocation LiteralLoc;
  if (!S.checkStringLiteralArgumentAttr(AL, 0, Str, &LiteralLoc))
    return;

  if (!S.checkSectionName(LiteralLoc, Str))
    return;

  SectionAttr *NewAttr = S.mergeSectionAttr(D, AL, Str);
  if (NewAttr) {
    D->addAttr(NewAttr);
    if (isa<FunctionDecl>(D))
      S.UnifySection(NewAttr->getName(),
                     TreeContext::PSF_Execute | TreeContext::PSF_Read,
                     cast<NamedDecl>(D));
  }
}

// This is used for `__declspec(code_seg("segname"))` on a decl.
// `#pragma code_seg("segname")` uses checkSectionName() instead.
bool checkCodeSegName(Sema &S, SourceLocation LiteralLoc,
                      llvm::StringRef CodeSegName) {
  if (llvm::Error E = S.isValidSectionSpecifier(CodeSegName)) {
    S.Diag(LiteralLoc, diag::err_attribute_section_invalid_for_target)
        << toString(std::move(E)) << 0 /*'code-seg'*/;
    return false;
  }

  return true;
}

CodeSegAttr *Sema::mergeCodeSegAttr(Decl *D, const AttributeCommonInfo &CI,
                                    llvm::StringRef Name) {
  if (const auto *ExistingAttr = D->getAttr<CodeSegAttr>()) {
    if (ExistingAttr->getName() == Name)
      return nullptr;
    Diag(ExistingAttr->getLocation(), diag::warn_mismatched_section)
        << 0 /*codeseg*/;
    Diag(CI.getLoc(), diag::note_previous_attribute);
    return nullptr;
  }
  return ::new (Context) CodeSegAttr(Context, CI, Name);
}

void handleCodeSegAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  llvm::StringRef Str;
  SourceLocation LiteralLoc;
  if (!S.checkStringLiteralArgumentAttr(AL, 0, Str, &LiteralLoc))
    return;
  if (!checkCodeSegName(S, LiteralLoc, Str))
    return;
  if (const auto *ExistingAttr = D->getAttr<CodeSegAttr>()) {
    if (!ExistingAttr->isImplicit()) {
      S.Diag(AL.getLoc(), ExistingAttr->getName() == Str
                              ? diag::warn_duplicate_codeseg_attribute
                              : diag::err_conflicting_codeseg_attribute);
      return;
    }
    D->dropAttr<CodeSegAttr>();
  }
  if (CodeSegAttr *CSA = S.mergeCodeSegAttr(D, AL, Str))
    D->addAttr(CSA);
}

// Check for things we'd like to warn about. Multiversioning issues are
// handled later in the process, once we know how many exist.
bool Sema::checkTargetAttr(SourceLocation LiteralLoc, llvm::StringRef AttrStr) {
  enum FirstParam { Unsupported, Duplicate, Unknown };
  enum SecondParam { None, CPU, Tune };
  enum ThirdParam { Target, TargetClones };
  if (AttrStr.contains("fpmath="))
    return Diag(LiteralLoc, diag::warn_unsupported_target_attribute)
           << Unsupported << None << "fpmath=" << Target;

  // Diagnose use of tune if target doesn't support it.
  if (!Context.getTargetInfo().supportsTargetAttributeTune() &&
      AttrStr.contains("tune="))
    return Diag(LiteralLoc, diag::warn_unsupported_target_attribute)
           << Unsupported << None << "tune=" << Target;

  ParsedTargetAttr ParsedAttrs =
      Context.getTargetInfo().parseTargetAttr(AttrStr);

  if (!ParsedAttrs.CPU.empty() &&
      !Context.getTargetInfo().isValidCPUName(ParsedAttrs.CPU))
    return Diag(LiteralLoc, diag::warn_unsupported_target_attribute)
           << Unknown << CPU << ParsedAttrs.CPU << Target;

  if (!ParsedAttrs.Tune.empty() &&
      !Context.getTargetInfo().isValidCPUName(ParsedAttrs.Tune))
    return Diag(LiteralLoc, diag::warn_unsupported_target_attribute)
           << Unknown << Tune << ParsedAttrs.Tune << Target;

  if (ParsedAttrs.Duplicate != "")
    return Diag(LiteralLoc, diag::warn_unsupported_target_attribute)
           << Duplicate << None << ParsedAttrs.Duplicate << Target;

  for (const auto &Feature : ParsedAttrs.Features) {
    auto CurFeature = llvm::StringRef(Feature).drop_front(); // remove + or -.
    if (!Context.getTargetInfo().isValidFeatureName(CurFeature))
      return Diag(LiteralLoc, diag::warn_unsupported_target_attribute)
             << Unsupported << None << CurFeature << Target;
  }

  TargetInfo::BranchProtectionInfo BPI;
  llvm::StringRef DiagMsg;
  if (ParsedAttrs.BranchProtection.empty())
    return false;
  if (!Context.getTargetInfo().validateBranchProtection(
          ParsedAttrs.BranchProtection, ParsedAttrs.CPU, BPI, DiagMsg)) {
    if (DiagMsg.empty())
      return Diag(LiteralLoc, diag::warn_unsupported_target_attribute)
             << Unsupported << None << "branch-protection" << Target;
    return Diag(LiteralLoc, diag::err_invalid_branch_protection_spec)
           << DiagMsg;
  }
  if (!DiagMsg.empty())
    Diag(LiteralLoc, diag::warn_unsupported_branch_protection_spec) << DiagMsg;

  return false;
}

// Check Target Version attrs
bool Sema::checkTargetVersionAttr(SourceLocation LiteralLoc,
                                  llvm::StringRef &AttrStr, bool &isDefault) {
  enum FirstParam { Unsupported };
  enum SecondParam { None };
  enum ThirdParam { Target, TargetClones, TargetVersion };
  if (AttrStr.trim() == "default")
    isDefault = true;
  llvm::SmallVector<llvm::StringRef, 8> Features;
  AttrStr.split(Features, "+");
  for (auto &CurFeature : Features) {
    CurFeature = CurFeature.trim();
    if (CurFeature == "default")
      continue;
    if (!Context.getTargetInfo().validateCpuSupports(CurFeature))
      return Diag(LiteralLoc, diag::warn_unsupported_target_attribute)
             << Unsupported << None << CurFeature << TargetVersion;
  }
  return false;
}

void handleTargetVersionAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  llvm::StringRef Str;
  SourceLocation LiteralLoc;
  bool isDefault = false;
  if (!S.checkStringLiteralArgumentAttr(AL, 0, Str, &LiteralLoc) ||
      S.checkTargetVersionAttr(LiteralLoc, Str, isDefault))
    return;
  // Do not create default only target_version attribute
  if (!isDefault) {
    TargetVersionAttr *NewAttr =
        ::new (S.Context) TargetVersionAttr(S.Context, AL, Str);
    D->addAttr(NewAttr);
  }
}

void handleTargetAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  llvm::StringRef Str;
  SourceLocation LiteralLoc;
  if (!S.checkStringLiteralArgumentAttr(AL, 0, Str, &LiteralLoc) ||
      S.checkTargetAttr(LiteralLoc, Str))
    return;

  TargetAttr *NewAttr = ::new (S.Context) TargetAttr(S.Context, AL, Str);
  D->addAttr(NewAttr);
}

bool Sema::checkTargetClonesAttrString(
    SourceLocation LiteralLoc, llvm::StringRef Str,
    const StringLiteral *Literal, bool &HasDefault, bool &HasCommas,
    bool &HasNotDefault,
    llvm::SmallVectorImpl<llvm::SmallString<64>> &StringsBuffer) {
  enum FirstParam { Unsupported, Duplicate, Unknown };
  enum SecondParam { None, CPU, Tune };
  enum ThirdParam { Target, TargetClones };
  HasCommas = HasCommas || Str.contains(',');
  const TargetInfo &TInfo = Context.getTargetInfo();
  // Warn on empty at the beginning of a string.
  if (Str.size() == 0)
    return Diag(LiteralLoc, diag::warn_unsupported_target_attribute)
           << Unsupported << None << "" << TargetClones;

  std::pair<llvm::StringRef, llvm::StringRef> Parts = {{}, Str};
  while (!Parts.second.empty()) {
    Parts = Parts.second.split(',');
    llvm::StringRef Cur = Parts.first.trim();
    SourceLocation CurLoc =
        Literal->getLocationOfByte(Cur.data() - Literal->getString().data(),
                                   getSourceManager(), getLangOpts(), TInfo);

    bool DefaultIsDupe = false;
    bool HasCodeGenImpact = false;
    if (Cur.empty())
      return Diag(CurLoc, diag::warn_unsupported_target_attribute)
             << Unsupported << None << "" << TargetClones;

    if (TInfo.getTriple().isAArch64()) {
      // AArch64 target clones specific
      if (Cur == "default") {
        DefaultIsDupe = HasDefault;
        HasDefault = true;
        if (llvm::is_contained(StringsBuffer, Cur) || DefaultIsDupe)
          Diag(CurLoc, diag::warn_target_clone_duplicate_options);
        else
          StringsBuffer.push_back(Cur);
      } else {
        std::pair<llvm::StringRef, llvm::StringRef> CurParts = {{}, Cur};
        llvm::SmallVector<llvm::StringRef, 8> CurFeatures;
        while (!CurParts.second.empty()) {
          CurParts = CurParts.second.split('+');
          llvm::StringRef CurFeature = CurParts.first.trim();
          if (!TInfo.validateCpuSupports(CurFeature)) {
            Diag(CurLoc, diag::warn_unsupported_target_attribute)
                << Unsupported << None << CurFeature << TargetClones;
            continue;
          }
          if (TInfo.doesFeatureAffectCodeGen(CurFeature))
            HasCodeGenImpact = true;
          CurFeatures.push_back(CurFeature);
        }
        // Canonize TargetClones Attributes
        llvm::sort(CurFeatures);
        llvm::SmallString<64> Res;
        for (auto &CurFeat : CurFeatures) {
          if (!Res.equals(""))
            Res.append("+");
          Res.append(CurFeat);
        }
        if (llvm::is_contained(StringsBuffer, Res) || DefaultIsDupe)
          Diag(CurLoc, diag::warn_target_clone_duplicate_options);
        else if (!HasCodeGenImpact)
          // Ignore features in target_clone attribute that don't impact
          // code generation
          Diag(CurLoc, diag::warn_target_clone_no_impact_options);
        else if (!Res.empty()) {
          StringsBuffer.push_back(Res);
          HasNotDefault = true;
        }
      }
    } else {
      // Other targets ( currently X86 )
      if (Cur.starts_with("arch=")) {
        if (!Context.getTargetInfo().isValidCPUName(
                Cur.drop_front(sizeof("arch=") - 1)))
          return Diag(CurLoc, diag::warn_unsupported_target_attribute)
                 << Unsupported << CPU << Cur.drop_front(sizeof("arch=") - 1)
                 << TargetClones;
      } else if (Cur == "default") {
        DefaultIsDupe = HasDefault;
        HasDefault = true;
      } else if (!Context.getTargetInfo().isValidFeatureName(Cur))
        return Diag(CurLoc, diag::warn_unsupported_target_attribute)
               << Unsupported << None << Cur << TargetClones;
      if (llvm::is_contained(StringsBuffer, Cur) || DefaultIsDupe)
        Diag(CurLoc, diag::warn_target_clone_duplicate_options);
      // Note: Add even if there are duplicates, since it changes name mangling.
      StringsBuffer.push_back(Cur);
    }
  }
  if (Str.rtrim().ends_with(","))
    return Diag(LiteralLoc, diag::warn_unsupported_target_attribute)
           << Unsupported << None << "" << TargetClones;
  return false;
}

// ===----------------------------------------------------------------------===
// Extended attribute handlers
// ===----------------------------------------------------------------------===

namespace {

void handleTargetClonesAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (S.Context.getTargetInfo().getTriple().isAArch64() &&
      !S.Context.getTargetInfo().hasFeature("fmv"))
    return;

  // Ensure we don't combine these with themselves, since that causes some
  // confusing behavior.
  if (const auto *Other = D->getAttr<TargetClonesAttr>()) {
    S.Diag(AL.getLoc(), diag::err_disallowed_duplicate_attribute) << AL;
    S.Diag(Other->getLocation(), diag::note_conflicting_attribute);
    return;
  }
  if (checkAttrMutualExclusion<TargetClonesAttr>(S, D, AL))
    return;

  llvm::SmallVector<llvm::StringRef, 2> Strings;
  llvm::SmallVector<llvm::SmallString<64>, 2> StringsBuffer;
  bool HasCommas = false, HasDefault = false, HasNotDefault = false;

  for (unsigned I = 0, E = AL.getNumArgs(); I != E; ++I) {
    llvm::StringRef CurStr;
    SourceLocation LiteralLoc;
    if (!S.checkStringLiteralArgumentAttr(AL, I, CurStr, &LiteralLoc) ||
        S.checkTargetClonesAttrString(
            LiteralLoc, CurStr,
            cast<StringLiteral>(AL.getArgAsExpr(I)->IgnoreParenCasts()),
            HasDefault, HasCommas, HasNotDefault, StringsBuffer))
      return;
  }
  for (auto &SmallStr : StringsBuffer)
    Strings.push_back(SmallStr.str());

  if (HasCommas && AL.getNumArgs() > 1)
    S.Diag(AL.getLoc(), diag::warn_target_clone_mixed_values);

  if (S.Context.getTargetInfo().getTriple().isAArch64() && !HasDefault) {
    // Add default attribute if there is no one
    HasDefault = true;
    Strings.push_back("default");
  }

  if (!HasDefault) {
    S.Diag(AL.getLoc(), diag::err_target_clone_must_have_default);
    return;
  }

  // No multiversion if we have default version only.
  if (S.Context.getTargetInfo().getTriple().isAArch64() && !HasNotDefault)
    return;

  cast<FunctionDecl>(D)->setIsMultiVersion();
  TargetClonesAttr *NewAttr = ::new (S.Context)
      TargetClonesAttr(S.Context, AL, Strings.data(), Strings.size());
  D->addAttr(NewAttr);
}

void handleMinVectorWidthAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  Expr *E = AL.getArgAsExpr(0);
  uint32_t VecWidth;
  if (!checkUInt32Argument(S, AL, E, VecWidth)) {
    AL.setInvalid();
    return;
  }

  MinVectorWidthAttr *Existing = D->getAttr<MinVectorWidthAttr>();
  if (Existing && Existing->getVectorWidth() != VecWidth) {
    S.Diag(AL.getLoc(), diag::warn_duplicate_attribute) << AL;
    return;
  }

  D->addAttr(::new (S.Context) MinVectorWidthAttr(S.Context, AL, VecWidth));
}

void handleCleanupAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  Expr *E = AL.getArgAsExpr(0);
  SourceLocation Loc = E->getExprLoc();
  FunctionDecl *FD = nullptr;
  DeclarationNameInfo NI;

  // gcc only allows for simple identifiers. Since we support more than gcc, we
  // will warn the user.
  if (auto *DRE = dyn_cast<DeclRefExpr>(E)) {
    FD = dyn_cast<FunctionDecl>(DRE->getDecl());
    NI = DRE->getNameInfo();
    if (!FD) {
      S.Diag(Loc, diag::err_attribute_cleanup_arg_not_function)
          << 1 << NI.getName();
      return;
    }
  } else {
    S.Diag(Loc, diag::err_attribute_cleanup_arg_not_function) << 0;
    return;
  }

  if (FD->getNumParams() != 1) {
    S.Diag(Loc, diag::err_attribute_cleanup_func_must_take_one_arg)
        << NI.getName();
    return;
  }

  // We're currently more strict than GCC about what function types we accept.
  // If this ever proves to be a problem it should be easy to fix.
  QualType Ty = S.Context.getPointerType(cast<VarDecl>(D)->getType());
  QualType ParamTy = FD->getParamDecl(0)->getType();
  if (S.CheckAssignmentConstraints(FD->getParamDecl(0)->getLocation(), ParamTy,
                                   Ty) != Sema::Compatible) {
    S.Diag(Loc, diag::err_attribute_cleanup_func_arg_incompatible_type)
        << NI.getName() << ParamTy << Ty;
    return;
  }

  D->addAttr(::new (S.Context) CleanupAttr(S.Context, AL, FD));
}

void handleEnumExtensibilityAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (!AL.isArgIdent(0)) {
    S.Diag(AL.getLoc(), diag::err_attribute_argument_n_type)
        << AL << 0 << AANT_ArgumentIdentifier;
    return;
  }

  EnumExtensibilityAttr::Kind ExtensibilityKind;
  IdentifierInfo *II = AL.getArgAsIdent(0)->Ident;
  if (!EnumExtensibilityAttr::ConvertStrToKind(II->getName(),
                                               ExtensibilityKind)) {
    S.Diag(AL.getLoc(), diag::warn_attribute_type_not_supported) << AL << II;
    return;
  }

  D->addAttr(::new (S.Context)
                 EnumExtensibilityAttr(S.Context, AL, ExtensibilityKind));
}

void handleFormatArgAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  const Expr *IdxExpr = AL.getArgAsExpr(0);
  ParamIdx Idx;
  if (!checkFunctionOrMethodParameterIndex(S, D, AL, 1, IdxExpr, Idx))
    return;

  // Make sure the format string is really a string.
  QualType Ty = getFunctionParamType(D, Idx.getASTIndex());

  if (!Ty->isPointerType() ||
      !Ty->castAs<PointerType>()->getPointeeType()->isCharType()) {
    S.Diag(AL.getLoc(), diag::err_format_attribute_not)
        << IdxExpr->getSourceRange() << getFunctionParamRange(D, 0);
    return;
  }
  Ty = getFunctionResultType(D);
  if (!Ty->isPointerType() ||
      !Ty->castAs<PointerType>()->getPointeeType()->isCharType()) {
    S.Diag(AL.getLoc(), diag::err_format_attribute_result_not)
        << "string type" << IdxExpr->getSourceRange()
        << getFunctionParamRange(D, 0);
    return;
  }

  D->addAttr(::new (S.Context) FormatArgAttr(S.Context, AL, Idx));
}

enum FormatAttrKind {
  StrftimeFormat,
  SupportedFormat,
  IgnoredFormat,
  InvalidFormat
};

FormatAttrKind getFormatAttrKind(llvm::StringRef Format) {
  return llvm::StringSwitch<FormatAttrKind>(Format)
      // Check for formats that get handled specially.
      .Case("strftime", StrftimeFormat)

      // Otherwise, check for supported formats.
      .Cases("scanf", "printf", "printf0", "strfmon", SupportedFormat)
      .Case("os_trace", SupportedFormat)
      .Case("os_log", SupportedFormat)

      .Cases("gcc_diag", "gcc_cdiag", "gcc_tdiag", IgnoredFormat)
      .Default(InvalidFormat);
}

} // namespace

ErrorAttr *Sema::mergeErrorAttr(Decl *D, const AttributeCommonInfo &CI,
                                llvm::StringRef NewUserDiagnostic) {
  if (const auto *EA = D->getAttr<ErrorAttr>()) {
    std::string NewAttr = CI.getNormalizedFullName();
    assert((NewAttr == "error" || NewAttr == "warning") &&
           "unexpected normalized full name");
    bool Match = (EA->isError() && NewAttr == "error") ||
                 (EA->isWarning() && NewAttr == "warning");
    if (!Match) {
      Diag(EA->getLocation(), diag::err_attributes_are_not_compatible)
          << CI << EA
          << (CI.isRegularKeywordAttribute() ||
              EA->isRegularKeywordAttribute());
      Diag(CI.getLoc(), diag::note_conflicting_attribute);
      return nullptr;
    }
    if (EA->getUserDiagnostic() != NewUserDiagnostic) {
      Diag(CI.getLoc(), diag::warn_duplicate_attribute) << EA;
      Diag(EA->getLoc(), diag::note_previous_attribute);
    }
    D->dropAttr<ErrorAttr>();
  }
  return ::new (Context) ErrorAttr(Context, CI, NewUserDiagnostic);
}

FormatAttr *Sema::mergeFormatAttr(Decl *D, const AttributeCommonInfo &CI,
                                  IdentifierInfo *Format, int FormatIdx,
                                  int FirstArg) {
  // Check whether we already have an equivalent format attribute.
  for (auto *F : D->specific_attrs<FormatAttr>()) {
    if (F->getType() == Format && F->getFormatIdx() == FormatIdx &&
        F->getFirstArg() == FirstArg) {
      // If we don't have a valid location for this attribute, adopt the
      // location.
      if (F->getLocation().isInvalid())
        F->setRange(CI.getRange());
      return nullptr;
    }
  }

  return ::new (Context) FormatAttr(Context, CI, Format, FormatIdx, FirstArg);
}

namespace {

void handleFormatAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (!AL.isArgIdent(0)) {
    S.Diag(AL.getLoc(), diag::err_attribute_argument_n_type)
        << AL << 1 << AANT_ArgumentIdentifier;
    return;
  }

  unsigned NumArgs = getFunctionNumParams(D);

  IdentifierInfo *II = AL.getArgAsIdent(0)->Ident;
  llvm::StringRef Format = II->getName();

  if (normalizeName(Format)) {
    // If we've modified the string name, we need a new identifier for it.
    II = &S.Context.Idents.get(Format);
  }

  // Check for supported formats.
  FormatAttrKind Kind = getFormatAttrKind(Format);

  if (Kind == IgnoredFormat)
    return;

  if (Kind == InvalidFormat) {
    S.Diag(AL.getLoc(), diag::warn_attribute_type_not_supported)
        << AL << II->getName();
    return;
  }

  // checks for the 2nd argument
  Expr *IdxExpr = AL.getArgAsExpr(1);
  uint32_t Idx;
  if (!checkUInt32Argument(S, AL, IdxExpr, Idx, 2))
    return;

  if (Idx < 1 || Idx > NumArgs) {
    S.Diag(AL.getLoc(), diag::err_attribute_argument_out_of_bounds)
        << AL << 2 << IdxExpr->getSourceRange();
    return;
  }

  unsigned ArgIdx = Idx - 1;

  // make sure the format string is really a string
  QualType Ty = getFunctionParamType(D, ArgIdx);

  if (!Ty->isPointerType() ||
      !Ty->castAs<PointerType>()->getPointeeType()->isCharType()) {
    S.Diag(AL.getLoc(), diag::err_format_attribute_not)
        << IdxExpr->getSourceRange() << getFunctionParamRange(D, ArgIdx);
    return;
  }

  // check the 3rd argument
  Expr *FirstArgExpr = AL.getArgAsExpr(2);
  uint32_t FirstArg;
  if (!checkUInt32Argument(S, AL, FirstArgExpr, FirstArg, 3))
    return;

  // FirstArg == 0 is is always valid.
  if (FirstArg != 0) {
    if (Kind == StrftimeFormat) {
      // If the kind is strftime, FirstArg must be 0 because strftime does not
      // use any variadic arguments.
      S.Diag(AL.getLoc(), diag::err_format_strftime_third_parameter)
          << FirstArgExpr->getSourceRange()
          << FixItHint::CreateReplacement(FirstArgExpr->getSourceRange(), "0");
      return;
    } else if (isVariadicFunctionDecl(D)) {
      // Else, if the function is variadic, then FirstArg must be 0 or the
      // "position" of the ... parameter. It's unusual to use 0 with variadic
      // functions, so the fixit proposes the latter.
      if (FirstArg != NumArgs + 1) {
        S.Diag(AL.getLoc(), diag::err_attribute_argument_out_of_bounds)
            << AL << 3 << FirstArgExpr->getSourceRange()
            << FixItHint::CreateReplacement(FirstArgExpr->getSourceRange(),
                                            std::to_string(NumArgs + 1));
        return;
      }
    } else {
      // Inescapable GCC compatibility diagnostic.
      S.Diag(D->getLocation(), diag::warn_gcc_requires_variadic_function) << AL;
      if (FirstArg <= Idx) {
        // Else, the function is not variadic, and FirstArg must be 0 or any
        // parameter after the format parameter. We don't offer a fixit because
        // there are too many possible good values.
        S.Diag(AL.getLoc(), diag::err_attribute_argument_out_of_bounds)
            << AL << 3 << FirstArgExpr->getSourceRange();
        return;
      }
    }
  }

  FormatAttr *NewAttr = S.mergeFormatAttr(D, AL, II, Idx, FirstArg);
  if (NewAttr)
    D->addAttr(NewAttr);
}

void handleCallbackAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  // The index that identifies the callback callee is mandatory.
  if (AL.getNumArgs() == 0) {
    S.Diag(AL.getLoc(), diag::err_callback_attribute_no_callee)
        << AL.getRange();
    return;
  }

  bool HasImplicitThisParam = false;
  int32_t NumArgs = getFunctionNumParams(D);

  FunctionDecl *FD = D->getAsFunction();
  assert(FD && "Expected a function declaration!");

  llvm::StringMap<int> NameIdxMapping;
  NameIdxMapping["__"] = -1;

  NameIdxMapping["this"] = 0;

  int Idx = 1;
  for (const ParmVarDecl *PVD : FD->parameters())
    NameIdxMapping[PVD->getName()] = Idx++;

  auto UnknownName = NameIdxMapping.end();

  llvm::SmallVector<int, 8> EncodingIndices;
  for (unsigned I = 0, E = AL.getNumArgs(); I < E; ++I) {
    SourceRange SR;
    int32_t ArgIdx;

    if (AL.isArgIdent(I)) {
      IdentifierLoc *IdLoc = AL.getArgAsIdent(I);
      auto It = NameIdxMapping.find(IdLoc->Ident->getName());
      if (It == UnknownName) {
        S.Diag(AL.getLoc(), diag::err_callback_attribute_argument_unknown)
            << IdLoc->Ident << IdLoc->Loc;
        return;
      }

      SR = SourceRange(IdLoc->Loc);
      ArgIdx = It->second;
    } else if (AL.isArgExpr(I)) {
      Expr *IdxExpr = AL.getArgAsExpr(I);

      // If the expression is not parseable as an int32_t we have a problem.
      if (!checkUInt32Argument(S, AL, IdxExpr, (uint32_t &)ArgIdx, I + 1,
                               false)) {
        S.Diag(AL.getLoc(), diag::err_attribute_argument_out_of_bounds)
            << AL << (I + 1) << IdxExpr->getSourceRange();
        return;
      }

      // Check oob, excluding the special values, 0 and -1.
      if (ArgIdx < -1 || ArgIdx > NumArgs) {
        S.Diag(AL.getLoc(), diag::err_attribute_argument_out_of_bounds)
            << AL << (I + 1) << IdxExpr->getSourceRange();
        return;
      }

      SR = IdxExpr->getSourceRange();
    } else {
      llvm_unreachable("Unexpected ParsedAttr argument type!");
    }

    if (ArgIdx == 0 && !HasImplicitThisParam) {
      S.Diag(AL.getLoc(), diag::err_callback_implicit_this_not_available)
          << (I + 1) << SR;
      return;
    }

    // Adjust for the case we do not have an implicit "this" parameter. In this
    // case we decrease all positive values by 1 to get LLVM argument indices.
    if (!HasImplicitThisParam && ArgIdx > 0)
      ArgIdx -= 1;

    EncodingIndices.push_back(ArgIdx);
  }

  int CalleeIdx = EncodingIndices.front();
  // Check if the callee index is proper, thus not "this" and not "unknown".
  // This means the "CalleeIdx" has to be non-negative if "HasImplicitThisParam"
  // is false and positive if "HasImplicitThisParam" is true.
  if (CalleeIdx < (int)HasImplicitThisParam) {
    S.Diag(AL.getLoc(), diag::err_callback_attribute_invalid_callee)
        << AL.getRange();
    return;
  }

  // Get the callee type, note the index adjustment as the AST doesn't contain
  // the this type (which the callee cannot reference anyway!).
  const Type *CalleeType =
      getFunctionParamType(D, CalleeIdx - HasImplicitThisParam).getTypePtr();
  if (!CalleeType || !CalleeType->isFunctionPointerType()) {
    S.Diag(AL.getLoc(), diag::err_callback_callee_no_function_type)
        << AL.getRange();
    return;
  }

  const Type *CalleeFnType =
      CalleeType->getPointeeType()->getUnqualifiedDesugaredType();

  const auto *CalleeFnProtoType = dyn_cast<FunctionProtoType>(CalleeFnType);
  if (!CalleeFnProtoType) {
    S.Diag(AL.getLoc(), diag::err_callback_callee_no_function_type)
        << AL.getRange();
    return;
  }

  if (CalleeFnProtoType->getNumParams() > EncodingIndices.size() - 1) {
    S.Diag(AL.getLoc(), diag::err_attribute_wrong_number_arguments)
        << AL << (unsigned)(EncodingIndices.size() - 1);
    return;
  }

  if (CalleeFnProtoType->getNumParams() < EncodingIndices.size() - 1) {
    S.Diag(AL.getLoc(), diag::err_attribute_wrong_number_arguments)
        << AL << (unsigned)(EncodingIndices.size() - 1);
    return;
  }

  if (CalleeFnProtoType->isVariadic()) {
    S.Diag(AL.getLoc(), diag::err_callback_callee_is_variadic) << AL.getRange();
    return;
  }

  // Do not allow multiple callback attributes.
  if (D->hasAttr<CallbackAttr>()) {
    S.Diag(AL.getLoc(), diag::err_callback_attribute_multiple) << AL.getRange();
    return;
  }

  D->addAttr(::new (S.Context) CallbackAttr(
      S.Context, AL, EncodingIndices.data(), EncodingIndices.size()));
}

void handleTransparentUnionAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  // Try to find the underlying union declaration.
  RecordDecl *RD = nullptr;
  const auto *TD = dyn_cast<TypedefNameDecl>(D);
  if (TD && TD->getUnderlyingType()->isUnionType())
    RD = TD->getUnderlyingType()->getAsUnionType()->getDecl();
  else
    RD = dyn_cast<RecordDecl>(D);

  if (!RD || !RD->isUnion()) {
    S.Diag(AL.getLoc(), diag::warn_attribute_wrong_decl_type)
        << AL << AL.isRegularKeywordAttribute() << ExpectedUnion;
    return;
  }

  if (!RD->isCompleteDefinition()) {
    if (!RD->isBeingDefined())
      S.Diag(AL.getLoc(),
             diag::warn_transparent_union_attribute_not_definition);
    return;
  }

  RecordDecl::field_iterator Field = RD->field_begin(),
                             FieldEnd = RD->field_end();
  if (Field == FieldEnd) {
    S.Diag(AL.getLoc(), diag::warn_transparent_union_attribute_zero_fields);
    return;
  }

  FieldDecl *FirstField = *Field;
  QualType FirstType = FirstField->getType();
  if (FirstType->hasFloatingRepresentation() || FirstType->isVectorType()) {
    S.Diag(FirstField->getLocation(),
           diag::warn_transparent_union_attribute_floating)
        << FirstType->isVectorType() << FirstType;
    return;
  }

  if (FirstType->isIncompleteType())
    return;
  uint64_t FirstSize = S.Context.getTypeSize(FirstType);
  uint64_t FirstAlign = S.Context.getTypeAlign(FirstType);
  for (; Field != FieldEnd; ++Field) {
    QualType FieldType = Field->getType();
    if (FieldType->isIncompleteType())
      return;
    // Not fully correct; we also need to test whether the
    // members of the union would all have the same calling convention as the
    // first member of the union. Checking just the size and alignment isn't
    // sufficient (consider structs passed on the stack instead of in registers
    // as an example).
    if (S.Context.getTypeSize(FieldType) != FirstSize ||
        S.Context.getTypeAlign(FieldType) > FirstAlign) {
      // Warn if we drop the attribute.
      bool isSize = S.Context.getTypeSize(FieldType) != FirstSize;
      unsigned FieldBits = isSize ? S.Context.getTypeSize(FieldType)
                                  : S.Context.getTypeAlign(FieldType);
      S.Diag(Field->getLocation(),
             diag::warn_transparent_union_attribute_field_size_align)
          << isSize << *Field << FieldBits;
      unsigned FirstBits = isSize ? FirstSize : FirstAlign;
      S.Diag(FirstField->getLocation(),
             diag::note_transparent_union_first_field_size_align)
          << isSize << FirstBits;
      return;
    }
  }

  RD->addAttr(::new (S.Context) TransparentUnionAttr(S.Context, AL));
}

} // namespace

// ===----------------------------------------------------------------------===
// Alignment & annotation attributes
// ===----------------------------------------------------------------------===

void Sema::AddAnnotationAttr(Decl *D, const AttributeCommonInfo &CI,
                             llvm::StringRef Str,
                             llvm::MutableArrayRef<Expr *> Args) {
  auto *Attr = AnnotateAttr::Create(Context, Str, Args.data(), Args.size(), CI);
  if (ConstantFoldAttrArgs(CI, llvm::MutableArrayRef<Expr *>(
                                   Attr->args_begin(), Attr->args_end()))) {
    D->addAttr(Attr);
  }
}

void handleAnnotateAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  // Make sure that there is a string literal as the annotation's first
  // argument.
  llvm::StringRef Str;
  if (!S.checkStringLiteralArgumentAttr(AL, 0, Str))
    return;

  llvm::SmallVector<Expr *, 4> Args;
  Args.reserve(AL.getNumArgs() - 1);
  for (unsigned Idx = 1; Idx < AL.getNumArgs(); Idx++) {
    assert(!AL.isArgIdent(Idx));
    Args.push_back(AL.getArgAsExpr(Idx));
  }

  S.AddAnnotationAttr(D, AL, Str, Args);
}

void handleAlignValueAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  S.AddAlignValueAttr(D, AL, AL.getArgAsExpr(0));
}

void Sema::AddAlignValueAttr(Decl *D, const AttributeCommonInfo &CI, Expr *E) {
  AlignValueAttr TmpAttr(Context, CI, E);
  SourceLocation AttrLoc = CI.getLoc();

  QualType T;
  if (const auto *TD = dyn_cast<TypedefNameDecl>(D))
    T = TD->getUnderlyingType();
  else if (const auto *VD = dyn_cast<ValueDecl>(D))
    T = VD->getType();
  else
    llvm_unreachable("Unknown decl type for align_value");

  if (!T->isAnyPointerType()) {
    Diag(AttrLoc, diag::warn_attribute_pointer_or_reference_only)
        << &TmpAttr << T << D->getSourceRange();
    return;
  }

  llvm::APSInt Alignment;
  ExprResult ICE = VerifyIntegerConstantExpression(
      E, &Alignment, diag::err_align_value_attribute_argument_not_int);
  if (ICE.isInvalid())
    return;

  if (!Alignment.isPowerOf2()) {
    Diag(AttrLoc, diag::err_alignment_not_power_of_two) << E->getSourceRange();
    return;
  }

  D->addAttr(::new (Context) AlignValueAttr(Context, CI, ICE.get()));
}

void handleAlignedAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (AL.hasParsedType()) {
    const ParsedType &TypeArg = AL.getTypeArg();
    TypeSourceInfo *TInfo;
    (void)S.GetTypeFromParser(
        ParsedType::getFromOpaquePtr(TypeArg.getAsOpaquePtr()), &TInfo);

    S.AddAlignedAttr(D, AL, TInfo);
    return;
  }

  // check the attribute arguments.
  if (AL.getNumArgs() > 1) {
    S.Diag(AL.getLoc(), diag::err_attribute_wrong_number_arguments) << AL << 1;
    return;
  }

  if (AL.getNumArgs() == 0) {
    D->addAttr(::new (S.Context) AlignedAttr(S.Context, AL, true, nullptr));
    return;
  }

  Expr *E = AL.getArgAsExpr(0);
  S.AddAlignedAttr(D, AL, E);
}

bool validateAlignasAppliedType(Sema &S, Decl *D, const AlignedAttr &Attr,
                                SourceLocation AttrLoc) {
  int DiagKind = -1;
  if (isa<ParmVarDecl>(D)) {
    DiagKind = 0;
  } else if (const auto *VD = dyn_cast<VarDecl>(D)) {
    if (VD->getStorageClass() == SC_Register)
      DiagKind = 1;
  } else if (const auto *FD = dyn_cast<FieldDecl>(D)) {
    if (FD->isBitField())
      DiagKind = 2;
  } else if (!isa<EnumDecl>(D) && !isa<TagDecl>(D)) {
    return S.Diag(AttrLoc, diag::err_attribute_wrong_decl_type)
           << &Attr << Attr.isRegularKeywordAttribute()
           << (Attr.isC11() ? ExpectedVariableOrField
                            : ExpectedVariableFieldOrTag);
  }
  if (DiagKind != -1) {
    return S.Diag(AttrLoc, diag::err_alignas_attribute_wrong_decl_type)
           << &Attr << DiagKind;
  }
  return false;
}

void Sema::AddAlignedAttr(Decl *D, const AttributeCommonInfo &CI, Expr *E) {
  AlignedAttr TmpAttr(Context, CI, true, E);
  SourceLocation AttrLoc = CI.getLoc();

  // alignas / _Alignas placement rules.
  if (TmpAttr.isAlignas() &&
      validateAlignasAppliedType(*this, D, TmpAttr, AttrLoc))
    return;

  llvm::APSInt Alignment;
  ExprResult ICE = VerifyIntegerConstantExpression(
      E, &Alignment, diag::err_aligned_attribute_argument_not_int);
  if (ICE.isInvalid())
    return;

  uint64_t MaximumAlignment = Sema::MaximumAlignment;
  if (Context.getTargetInfo().getTriple().isOSBinFormatCOFF())
    MaximumAlignment = std::min(MaximumAlignment, uint64_t(8192));
  if (Alignment > MaximumAlignment) {
    Diag(AttrLoc, diag::err_attribute_aligned_too_great)
        << MaximumAlignment << E->getSourceRange();
    return;
  }

  uint64_t AlignVal = Alignment.getZExtValue();
  // Alignment of zero is a no-op for _Alignas.
  if (!(TmpAttr.isAlignas() && !Alignment)) {
    if (!llvm::isPowerOf2_64(AlignVal)) {
      Diag(AttrLoc, diag::err_alignment_not_power_of_two)
          << E->getSourceRange();
      return;
    }
  }

  const auto *VD = dyn_cast<VarDecl>(D);
  if (VD) {
    unsigned MaxTLSAlign =
        Context.toCharUnitsFromBits(Context.getTargetInfo().getMaxTLSAlign())
            .getQuantity();
    if (MaxTLSAlign && AlignVal > MaxTLSAlign &&
        VD->getTLSKind() != VarDecl::TLS_None) {
      Diag(VD->getLocation(), diag::err_tls_var_aligned_over_maximum)
          << (unsigned)AlignVal << VD << MaxTLSAlign;
      return;
    }
  }

  AlignedAttr *AA = ::new (Context) AlignedAttr(Context, CI, true, ICE.get());
  AA->setCachedAlignmentValue(
      static_cast<unsigned>(AlignVal * Context.getCharWidth()));
  D->addAttr(AA);
}

void Sema::AddAlignedAttr(Decl *D, const AttributeCommonInfo &CI,
                          TypeSourceInfo *TS) {
  AlignedAttr TmpAttr(Context, CI, false, TS);
  SourceLocation AttrLoc = CI.getLoc();

  // alignas / _Alignas placement rules.
  if (TmpAttr.isAlignas() &&
      validateAlignasAppliedType(*this, D, TmpAttr, AttrLoc))
    return;

  unsigned AlignVal = TmpAttr.getAlignment(Context);
  AlignedAttr *AA = ::new (Context) AlignedAttr(Context, CI, false, TS);
  AA->setCachedAlignmentValue(AlignVal);
  D->addAttr(AA);
}

void Sema::CheckAlignasUnderalignment(Decl *D) {
  assert(D->hasAttrs() && "no attributes on decl");

  QualType UnderlyingTy, DiagTy;
  if (const auto *VD = dyn_cast<ValueDecl>(D)) {
    UnderlyingTy = DiagTy = VD->getType();
  } else {
    UnderlyingTy = DiagTy = Context.getTagDeclType(cast<TagDecl>(D));
    if (const auto *ED = dyn_cast<EnumDecl>(D))
      UnderlyingTy = ED->getIntegerType();
  }
  if (DiagTy->isIncompleteType())
    return;

  // Combined alignment attributes must not under-align the entity.
  AlignedAttr *AlignasAttr = nullptr;
  AlignedAttr *LastAlignedAttr = nullptr;
  unsigned Align = 0;
  for (auto *I : D->specific_attrs<AlignedAttr>()) {
    if (I->isAlignmentDependent())
      return;
    if (I->isAlignas())
      AlignasAttr = I;
    Align = std::max(Align, I->getAlignment(Context));
    LastAlignedAttr = I;
  }

  if (Align && DiagTy->isSizelessType()) {
    Diag(LastAlignedAttr->getLocation(), diag::err_attribute_sizeless_type)
        << LastAlignedAttr << DiagTy;
  } else if (AlignasAttr && Align) {
    CharUnits RequestedAlign = Context.toCharUnitsFromBits(Align);
    CharUnits NaturalAlign = Context.getTypeAlignInChars(UnderlyingTy);
    if (NaturalAlign > RequestedAlign)
      Diag(AlignasAttr->getLocation(), diag::err_alignas_underaligned)
          << DiagTy << (unsigned)NaturalAlign.getQuantity();
  }
}

void parseModeAttrArg(Sema &S, llvm::StringRef Str, unsigned &DestWidth,
                      bool &IntegerMode, bool &ComplexMode,
                      FloatModeKind &ExplicitType) {
  IntegerMode = true;
  ComplexMode = false;
  ExplicitType = FloatModeKind::NoFloat;
  switch (Str.size()) {
  case 2:
    switch (Str[0]) {
    case 'Q':
      DestWidth = 8;
      break;
    case 'H':
      DestWidth = 16;
      break;
    case 'S':
      DestWidth = 32;
      break;
    case 'D':
      DestWidth = 64;
      break;
    case 'X':
      DestWidth = 96;
      break;
    case 'K': // KFmode - IEEE quad precision (__float128)
      ExplicitType = FloatModeKind::Float128;
      DestWidth = Str[1] == 'I' ? 0 : 128;
      break;
    case 'T':
      ExplicitType = FloatModeKind::LongDouble;
      DestWidth = 128;
      break;
    case 'I':
      DestWidth = 0;
      break;
    }
    if (Str[1] == 'F') {
      IntegerMode = false;
    } else if (Str[1] == 'C') {
      IntegerMode = false;
      ComplexMode = true;
    } else if (Str[1] != 'I') {
      DestWidth = 0;
    }
    break;
  case 4:
    // glibc uses 'word' to define register_t; this is narrower than a
    // pointer on some embedded platforms.
    if (Str == "word")
      DestWidth = S.Context.getTargetInfo().getRegisterWidth();
    else if (Str == "byte")
      DestWidth = S.Context.getTargetInfo().getCharWidth();
    break;
  case 7:
    if (Str == "pointer")
      DestWidth = S.Context.getTargetInfo().getPointerWidth(LangAS::Default);
    break;
  case 11:
    if (Str == "unwind_word")
      DestWidth = S.Context.getTargetInfo().getUnwindWordWidth();
    break;
  }
}

void handleModeAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  // This attribute isn't documented, but glibc uses it.  It changes
  // the width of an int or unsigned int to the specified size.
  if (!AL.isArgIdent(0)) {
    S.Diag(AL.getLoc(), diag::err_attribute_argument_type)
        << AL << AANT_ArgumentIdentifier;
    return;
  }

  IdentifierInfo *Name = AL.getArgAsIdent(0)->Ident;

  S.AddModeAttr(D, AL, Name);
}

void Sema::AddModeAttr(Decl *D, const AttributeCommonInfo &CI,
                       IdentifierInfo *Name) {
  llvm::StringRef Str = Name->getName();
  normalizeName(Str);
  SourceLocation AttrLoc = CI.getLoc();

  unsigned DestWidth = 0;
  bool IntegerMode = true;
  bool ComplexMode = false;
  FloatModeKind ExplicitType = FloatModeKind::NoFloat;
  llvm::APInt VectorSize(64, 0);
  if (Str.size() >= 4 && Str[0] == 'V') {
    // Minimal length of vector mode is 4: 'V' + NUMBER(>=1) + TYPE(>=2).
    size_t StrSize = Str.size();
    size_t VectorStringLength = 0;
    while ((VectorStringLength + 1) < StrSize &&
           isdigit(Str[VectorStringLength + 1]))
      ++VectorStringLength;
    if (VectorStringLength &&
        !Str.substr(1, VectorStringLength).getAsInteger(10, VectorSize) &&
        VectorSize.isPowerOf2()) {
      parseModeAttrArg(*this, Str.substr(VectorStringLength + 1), DestWidth,
                       IntegerMode, ComplexMode, ExplicitType);
      Diag(AttrLoc, diag::warn_vector_mode_deprecated);
    } else {
      VectorSize = 0;
    }
  }

  if (!VectorSize)
    parseModeAttrArg(*this, Str, DestWidth, IntegerMode, ComplexMode,
                     ExplicitType);

  if (!DestWidth) {
    Diag(AttrLoc, diag::err_machine_mode) << 0 /*Unknown*/ << Name;
    return;
  }

  QualType OldTy;
  if (const auto *TD = dyn_cast<TypedefNameDecl>(D))
    OldTy = TD->getUnderlyingType();
  else if (const auto *ED = dyn_cast<EnumDecl>(D)) {
    // Something like 'typedef enum { X } __attribute__((mode(XX))) T;'.
    // Try to get type from enum declaration, default to int.
    OldTy = ED->getIntegerType();
    if (OldTy.isNull())
      OldTy = Context.IntTy;
  } else
    OldTy = cast<ValueDecl>(D)->getType();

  // Base type can also be a vector type (see PR17453).
  // Distinguish between base type and base element type.
  QualType OldElemTy = OldTy;
  if (const auto *VT = OldTy->getAs<VectorType>())
    OldElemTy = VT->getElementType();

  // GCC allows 'mode' attribute on enumeration types (even incomplete), except
  // for vector modes. So, 'enum X __attribute__((mode(QI)));' forms a complete
  // type, 'enum { A } __attribute__((mode(V4SI)))' is rejected.
  if ((isa<EnumDecl>(D) || OldElemTy->getAs<EnumType>()) &&
      VectorSize.getBoolValue()) {
    Diag(AttrLoc, diag::err_enum_mode_vector_type) << Name << CI.getRange();
    return;
  }
  bool IntegralOrAnyEnumType = (OldElemTy->isIntegralOrEnumerationType() &&
                                !OldElemTy->isBitIntType()) ||
                               OldElemTy->getAs<EnumType>();

  if (!OldElemTy->getAs<BuiltinType>() && !OldElemTy->isComplexType() &&
      !IntegralOrAnyEnumType)
    Diag(AttrLoc, diag::err_mode_not_primitive);
  else if (IntegerMode) {
    if (!IntegralOrAnyEnumType)
      Diag(AttrLoc, diag::err_mode_wrong_type);
  } else if (ComplexMode) {
    if (!OldElemTy->isComplexType())
      Diag(AttrLoc, diag::err_mode_wrong_type);
  } else {
    if (!OldElemTy->isFloatingType())
      Diag(AttrLoc, diag::err_mode_wrong_type);
  }

  QualType NewElemTy;

  if (IntegerMode)
    NewElemTy = Context.getIntTypeForBitwidth(DestWidth,
                                              OldElemTy->isSignedIntegerType());
  else
    NewElemTy = Context.getRealTypeForBitwidth(DestWidth, ExplicitType);

  if (NewElemTy.isNull()) {
    Diag(AttrLoc, diag::err_machine_mode) << 1 /*Unsupported*/ << Name;
    return;
  }

  if (ComplexMode) {
    NewElemTy = Context.getComplexType(NewElemTy);
  }

  QualType NewTy = NewElemTy;
  if (VectorSize.getBoolValue()) {
    NewTy = Context.getVectorType(NewTy, VectorSize.getZExtValue(),
                                  VectorKind::Generic);
  } else if (const auto *OldVT = OldTy->getAs<VectorType>()) {
    // Complex machine mode does not support base vector types.
    if (ComplexMode) {
      Diag(AttrLoc, diag::err_complex_mode_vector_type);
      return;
    }
    unsigned NumElements = Context.getTypeSize(OldElemTy) *
                           OldVT->getNumElements() /
                           Context.getTypeSize(NewElemTy);
    NewTy =
        Context.getVectorType(NewElemTy, NumElements, OldVT->getVectorKind());
  }

  if (NewTy.isNull()) {
    Diag(AttrLoc, diag::err_mode_wrong_type);
    return;
  }

  // Install the new type.
  if (auto *TD = dyn_cast<TypedefNameDecl>(D))
    TD->setModedTypeSourceInfo(TD->getTypeSourceInfo(), NewTy);
  else if (auto *ED = dyn_cast<EnumDecl>(D))
    ED->setIntegerType(NewTy);
  else
    cast<ValueDecl>(D)->setType(NewTy);

  D->addAttr(::new (Context) ModeAttr(Context, CI, Name));
}

namespace {
void handleNoDebugAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  D->addAttr(::new (S.Context) NoDebugAttr(S.Context, AL));
}
} // namespace

AlwaysInlineAttr *Sema::mergeAlwaysInlineAttr(Decl *D,
                                              const AttributeCommonInfo &CI,
                                              const IdentifierInfo *Ident) {
  if (OptimizeNoneAttr *Optnone = D->getAttr<OptimizeNoneAttr>()) {
    Diag(CI.getLoc(), diag::warn_attribute_ignored) << Ident;
    Diag(Optnone->getLocation(), diag::note_conflicting_attribute);
    return nullptr;
  }

  if (D->hasAttr<AlwaysInlineAttr>())
    return nullptr;

  return ::new (Context) AlwaysInlineAttr(Context, CI);
}

InternalLinkageAttr *Sema::mergeInternalLinkageAttr(Decl *D,
                                                    const ParsedAttr &AL) {
  if (const auto *VD = dyn_cast<VarDecl>(D)) {
    // Attribute applies to Var but not ParmVar / ImplicitParm.
    if (VD->getKind() != Decl::Var) {
      Diag(AL.getLoc(), diag::warn_attribute_wrong_decl_type)
          << AL << AL.isRegularKeywordAttribute() << ExpectedVariableOrFunction;
      return nullptr;
    }
    // Attribute does not apply to non-static local variables.
    if (VD->hasLocalStorage()) {
      Diag(VD->getLocation(), diag::warn_internal_linkage_local_storage);
      return nullptr;
    }
  }

  return ::new (Context) InternalLinkageAttr(Context, AL);
}
InternalLinkageAttr *
Sema::mergeInternalLinkageAttr(Decl *D, const InternalLinkageAttr &AL) {
  if (const auto *VD = dyn_cast<VarDecl>(D)) {
    // Attribute applies to Var but not ParmVar / ImplicitParm.
    if (VD->getKind() != Decl::Var) {
      Diag(AL.getLocation(), diag::warn_attribute_wrong_decl_type)
          << &AL << AL.isRegularKeywordAttribute()
          << ExpectedVariableOrFunction;
      return nullptr;
    }
    // Attribute does not apply to non-static local variables.
    if (VD->hasLocalStorage()) {
      Diag(VD->getLocation(), diag::warn_internal_linkage_local_storage);
      return nullptr;
    }
  }

  return ::new (Context) InternalLinkageAttr(Context, AL);
}

MinSizeAttr *Sema::mergeMinSizeAttr(Decl *D, const AttributeCommonInfo &CI) {
  if (OptimizeNoneAttr *Optnone = D->getAttr<OptimizeNoneAttr>()) {
    Diag(CI.getLoc(), diag::warn_attribute_ignored) << "'minsize'";
    Diag(Optnone->getLocation(), diag::note_conflicting_attribute);
    return nullptr;
  }

  if (D->hasAttr<MinSizeAttr>())
    return nullptr;

  return ::new (Context) MinSizeAttr(Context, CI);
}

OptimizeNoneAttr *Sema::mergeOptimizeNoneAttr(Decl *D,
                                              const AttributeCommonInfo &CI) {
  if (AlwaysInlineAttr *Inline = D->getAttr<AlwaysInlineAttr>()) {
    Diag(Inline->getLocation(), diag::warn_attribute_ignored) << Inline;
    Diag(CI.getLoc(), diag::note_conflicting_attribute);
    D->dropAttr<AlwaysInlineAttr>();
  }
  if (MinSizeAttr *MinSize = D->getAttr<MinSizeAttr>()) {
    Diag(MinSize->getLocation(), diag::warn_attribute_ignored) << MinSize;
    Diag(CI.getLoc(), diag::note_conflicting_attribute);
    D->dropAttr<MinSizeAttr>();
  }

  if (D->hasAttr<OptimizeNoneAttr>())
    return nullptr;

  return ::new (Context) OptimizeNoneAttr(Context, CI);
}

void handleAlwaysInlineAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (AlwaysInlineAttr *Inline =
          S.mergeAlwaysInlineAttr(D, AL, AL.getAttrName()))
    D->addAttr(Inline);
}

void handleMinSizeAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (MinSizeAttr *MinSize = S.mergeMinSizeAttr(D, AL))
    D->addAttr(MinSize);
}

void handleOptimizeNoneAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (OptimizeNoneAttr *Optnone = S.mergeOptimizeNoneAttr(D, AL))
    D->addAttr(Optnone);
}

void handleGNUInlineAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  const auto *Fn = cast<FunctionDecl>(D);
  if (!Fn->isInlineSpecified()) {
    S.Diag(AL.getLoc(), diag::warn_gnu_inline_attribute_requires_inline);
    return;
  }

  D->addAttr(::new (S.Context) GNUInlineAttr(S.Context, AL));
}

void handleCallConvAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (hasDeclarator(D))
    return;
}

void handleSuppressAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (AL.getAttributeSpellingListIndex() ==
      SuppressAttr::Bracket_gsl_suppress) {
    // Suppression attribute with GSL spelling requires at least 1 argument.
    if (!AL.checkAtLeastNumArgs(S, 1))
      return;
  } else if (!isa<VarDecl>(D)) {
    // Analyzer suppression applies only to variables and statements.
    S.Diag(AL.getLoc(), diag::err_attribute_wrong_decl_type_str)
        << AL << 0 << "variables and statements";
    return;
  }

  std::vector<llvm::StringRef> DiagnosticIdentifiers;
  for (unsigned I = 0, E = AL.getNumArgs(); I != E; ++I) {
    llvm::StringRef RuleName;

    if (!S.checkStringLiteralArgumentAttr(AL, I, RuleName, nullptr))
      return;

    DiagnosticIdentifiers.push_back(RuleName);
  }
  D->addAttr(::new (S.Context)
                 SuppressAttr(S.Context, AL, DiagnosticIdentifiers.data(),
                              DiagnosticIdentifiers.size()));
}

void handleRandomizeLayoutAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  S.Diag(AL.getLoc(), diag::warn_attribute_ignored) << AL;
}

void handleNoRandomizeLayoutAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  S.Diag(AL.getLoc(), diag::warn_attribute_ignored) << AL;
}

bool Sema::ValidateCallingConvAttr(const ParsedAttr &Attrs, CallingConv &CC,
                                   const FunctionDecl *FD) {
  if (Attrs.isInvalid())
    return true;

  if (Attrs.hasProcessingCache()) {
    CC = (CallingConv)Attrs.getProcessingCache();
    return false;
  }

  unsigned ReqArgs = 0;
  if (!Attrs.checkExactlyNumArgs(*this, ReqArgs)) {
    Attrs.setInvalid();
    return true;
  }

  switch (Attrs.getKind()) {
  case ParsedAttr::AT_CDecl:
    CC = CC_C;
    break;
  case ParsedAttr::AT_FastCall:
    CC = CC_X86FastCall;
    break;
  case ParsedAttr::AT_StdCall:
    CC = CC_X86StdCall;
    break;
  case ParsedAttr::AT_VectorCall:
    CC = CC_X86VectorCall;
    break;
  case ParsedAttr::AT_AArch64VectorPcs:
    CC = CC_AArch64VectorCall;
    break;
  case ParsedAttr::AT_AArch64SVEPcs:
    CC = CC_AArch64SVEPCS;
    break;
  case ParsedAttr::AT_RegCall:
    CC = CC_X86RegCall;
    break;
  case ParsedAttr::AT_MSABI:
    CC = Context.getTargetInfo().getTriple().isOSWindows() ? CC_C : CC_Win64;
    break;
  case ParsedAttr::AT_SysVABI:
    CC = Context.getTargetInfo().getTriple().isOSWindows() ? CC_X86_64SysV
                                                           : CC_C;
    break;
  case ParsedAttr::AT_PreserveMost:
    CC = CC_PreserveMost;
    break;
  case ParsedAttr::AT_PreserveAll:
    CC = CC_PreserveAll;
    break;
  default:
    llvm_unreachable("unexpected attribute kind");
  }

  TargetInfo::CallingConvCheckResult A = TargetInfo::CCCR_OK;
  const TargetInfo &TI = Context.getTargetInfo();
  A = TI.checkCallingConvention(CC);

  switch (A) {
  case TargetInfo::CCCR_OK:
    break;

  case TargetInfo::CCCR_Ignore:
    // Treat an ignored convention as if it was an explicit C calling convention
    // attribute. For example, __stdcall on Win x64 functions as __cdecl, so
    // that command line flags that change the default convention to
    // __vectorcall don't affect declarations marked __stdcall.
    CC = CC_C;
    break;

  case TargetInfo::CCCR_Error:
    Diag(Attrs.getLoc(), diag::error_cconv_unsupported)
        << Attrs << (int)CallingConventionIgnoredReason::ForThisTarget;
    break;

  case TargetInfo::CCCR_Warning: {
    Diag(Attrs.getLoc(), diag::warn_cconv_unsupported)
        << Attrs << (int)CallingConventionIgnoredReason::ForThisTarget;

    bool IsVariadic = false;
    if (FD)
      IsVariadic = FD->isVariadic();
    CC = Context.getDefaultCallingConvention(IsVariadic);
    break;
  }
  }

  Attrs.setProcessingCache((unsigned)CC);
  return false;
}

bool Sema::CheckRegparmAttr(const ParsedAttr &AL, unsigned &numParams) {
  if (AL.isInvalid())
    return true;

  if (!AL.checkExactlyNumArgs(*this, 1)) {
    AL.setInvalid();
    return true;
  }

  uint32_t NP;
  Expr *NumParamsExpr = AL.getArgAsExpr(0);
  if (!checkUInt32Argument(*this, AL, NumParamsExpr, NP)) {
    AL.setInvalid();
    return true;
  }

  if (Context.getTargetInfo().getRegParmMax() == 0) {
    Diag(AL.getLoc(), diag::err_attribute_regparm_wrong_platform)
        << NumParamsExpr->getSourceRange();
    AL.setInvalid();
    return true;
  }

  numParams = NP;
  if (numParams > Context.getTargetInfo().getRegParmMax()) {
    Diag(AL.getLoc(), diag::err_attribute_regparm_invalid_number)
        << Context.getTargetInfo().getRegParmMax()
        << NumParamsExpr->getSourceRange();
    AL.setInvalid();
    return true;
  }

  return false;
}

// ===----------------------------------------------------------------------===
// Calling convention & dispatch table
// ===----------------------------------------------------------------------===

namespace {

void handleArgumentWithTypeTagAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (!AL.isArgIdent(0)) {
    S.Diag(AL.getLoc(), diag::err_attribute_argument_n_type)
        << AL << /* arg num = */ 1 << AANT_ArgumentIdentifier;
    return;
  }

  ParamIdx ArgumentIdx;
  if (!checkFunctionOrMethodParameterIndex(S, D, AL, 2, AL.getArgAsExpr(1),
                                           ArgumentIdx))
    return;

  ParamIdx TypeTagIdx;
  if (!checkFunctionOrMethodParameterIndex(S, D, AL, 3, AL.getArgAsExpr(2),
                                           TypeTagIdx))
    return;

  bool IsPointer = AL.getAttrName()->getName() == "pointer_with_type_tag";
  if (IsPointer) {
    // Ensure that buffer has a pointer type.
    unsigned ArgumentIdxAST = ArgumentIdx.getASTIndex();
    if (ArgumentIdxAST >= getFunctionNumParams(D) ||
        !getFunctionParamType(D, ArgumentIdxAST)->isPointerType())
      S.Diag(AL.getLoc(), diag::err_attribute_pointers_only) << AL << 0;
  }

  D->addAttr(::new (S.Context) ArgumentWithTypeTagAttr(
      S.Context, AL, AL.getArgAsIdent(0)->Ident, ArgumentIdx, TypeTagIdx,
      IsPointer));
}

void handleTypeTagForDatatypeAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (!AL.isArgIdent(0)) {
    S.Diag(AL.getLoc(), diag::err_attribute_argument_n_type)
        << AL << 1 << AANT_ArgumentIdentifier;
    return;
  }

  if (!AL.checkExactlyNumArgs(S, 1))
    return;

  if (!isa<VarDecl>(D)) {
    S.Diag(AL.getLoc(), diag::err_attribute_wrong_decl_type)
        << AL << AL.isRegularKeywordAttribute() << ExpectedVariable;
    return;
  }

  IdentifierInfo *PointerKind = AL.getArgAsIdent(0)->Ident;
  TypeSourceInfo *MatchingCTypeLoc = nullptr;
  S.GetTypeFromParser(AL.getMatchingCType(), &MatchingCTypeLoc);
  assert(MatchingCTypeLoc && "no type source info for attribute argument");

  D->addAttr(::new (S.Context) TypeTagForDatatypeAttr(
      S.Context, AL, PointerKind, MatchingCTypeLoc, AL.getLayoutCompatible(),
      AL.getMustBeNull()));
}

void handlePatchableFunctionEntryAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  uint32_t Count = 0, Offset = 0;
  if (!checkUInt32Argument(S, AL, AL.getArgAsExpr(0), Count, 0, true))
    return;
  if (AL.getNumArgs() == 2) {
    Expr *Arg = AL.getArgAsExpr(1);
    if (!checkUInt32Argument(S, AL, Arg, Offset, 1, true))
      return;
    if (Count < Offset) {
      S.Diag(getAttrLoc(AL), diag::err_attribute_argument_out_of_range)
          << &AL << 0 << Count << Arg->getBeginLoc();
      return;
    }
  }
  D->addAttr(::new (S.Context)
                 PatchableFunctionEntryAttr(S.Context, AL, Count, Offset));
}

bool armSveAliasValid(TreeContext &Context, unsigned BuiltinID,
                      llvm::StringRef AliasName) {
  return BuiltinID >= AArch64::FirstSVEBuiltin &&
         BuiltinID <= AArch64::LastSVEBuiltin;
}

bool armSmeAliasValid(TreeContext &Context, unsigned BuiltinID,
                      llvm::StringRef AliasName) {
  return BuiltinID >= AArch64::FirstSMEBuiltin &&
         BuiltinID <= AArch64::LastSMEBuiltin;
}

void handleArmBuiltinAliasAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (!AL.isArgIdent(0)) {
    S.Diag(AL.getLoc(), diag::err_attribute_argument_n_type)
        << AL << 1 << AANT_ArgumentIdentifier;
    return;
  }

  IdentifierInfo *Ident = AL.getArgAsIdent(0)->Ident;
  unsigned BuiltinID = Ident->getBuiltinID();
  llvm::StringRef AliasName = cast<FunctionDecl>(D)->getIdentifier()->getName();

  if (!armSveAliasValid(S.Context, BuiltinID, AliasName) &&
      !armSmeAliasValid(S.Context, BuiltinID, AliasName)) {
    S.Diag(AL.getLoc(), diag::err_attribute_arm_builtin_alias);
    return;
  }

  D->addAttr(::new (S.Context) ArmBuiltinAliasAttr(S.Context, AL, Ident));
}

void handleBuiltinAliasAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (!AL.isArgIdent(0)) {
    S.Diag(AL.getLoc(), diag::err_attribute_argument_n_type)
        << AL << 1 << AANT_ArgumentIdentifier;
    return;
  }

  IdentifierInfo *Ident = AL.getArgAsIdent(0)->Ident;
  unsigned BuiltinID = Ident->getBuiltinID();
  llvm::StringRef AliasName = cast<FunctionDecl>(D)->getIdentifier()->getName();

  bool IsAArch64 = S.Context.getTargetInfo().getTriple().isAArch64();
  if ((IsAArch64 && !armSveAliasValid(S.Context, BuiltinID, AliasName) &&
       !armSmeAliasValid(S.Context, BuiltinID, AliasName)) ||
      !IsAArch64) {
    S.Diag(AL.getLoc(), diag::err_attribute_builtin_alias) << AL;
    return;
  }

  D->addAttr(::new (S.Context) BuiltinAliasAttr(S.Context, AL, Ident));
}

void handlePreferredTypeAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (!AL.hasParsedType()) {
    S.Diag(AL.getLoc(), diag::err_attribute_wrong_number_arguments) << AL << 1;
    return;
  }

  TypeSourceInfo *ParmTSI = nullptr;
  QualType QT = S.GetTypeFromParser(AL.getTypeArg(), &ParmTSI);
  assert(ParmTSI && "no type source info for attribute argument");
  S.RequireCompleteType(ParmTSI->getTypeLoc().getBeginLoc(), QT,
                        diag::err_incomplete_type);

  D->addAttr(::new (S.Context) PreferredTypeAttr(S.Context, AL, ParmTSI));
}

void handleDeclspecThreadAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  const auto *VD = cast<VarDecl>(D);
  if (!S.Context.getTargetInfo().isTLSSupported()) {
    S.Diag(AL.getLoc(), diag::err_thread_unsupported);
    return;
  }
  if (VD->getTSCSpec() != TSCS_unspecified) {
    S.Diag(AL.getLoc(), diag::err_declspec_thread_on_thread_variable);
    return;
  }
  if (VD->hasLocalStorage()) {
    S.Diag(AL.getLoc(), diag::err_thread_non_global) << "__declspec(thread)";
    return;
  }
  D->addAttr(::new (S.Context) ThreadAttr(S.Context, AL));
}

void handleAnyX86InterruptAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  // Semantic checks for a function with the 'interrupt' attribute.
  // a) Must be a function.
  // b) Must have the 'void' return type.
  // c) Must take 1 or 2 arguments.
  // d) The 1st argument must be a pointer.
  // e) The 2nd argument (if any) must be an unsigned integer.
  if (!isFunctionOrMethod(D) || !hasFunctionProto(D)) {
    S.Diag(AL.getLoc(), diag::warn_attribute_wrong_decl_type)
        << AL << AL.isRegularKeywordAttribute()
        << ExpectedFunctionWithProtoType;
    return;
  }
  // Interrupt handler must have void return type.
  if (!getFunctionResultType(D)->isVoidType()) {
    S.Diag(getFunctionResultSourceRange(D).getBegin(),
           diag::err_anyx86_interrupt_attribute)
        << 1 << 0;
    return;
  }
  // Interrupt handler must have 1 or 2 parameters.
  unsigned NumParams = getFunctionNumParams(D);
  if (NumParams < 1 || NumParams > 2) {
    S.Diag(D->getBeginLoc(), diag::err_anyx86_interrupt_attribute) << 1 << 1;
    return;
  }
  // The first argument must be a pointer.
  if (!getFunctionParamType(D, 0)->isPointerType()) {
    S.Diag(getFunctionParamRange(D, 0).getBegin(),
           diag::err_anyx86_interrupt_attribute)
        << 1 << 2;
    return;
  }
  // The second argument, if present, must be a 64-bit unsigned integer.
  if (NumParams == 2 &&
      (!getFunctionParamType(D, 1)->isUnsignedIntegerType() ||
       S.Context.getTypeSize(getFunctionParamType(D, 1)) != 64)) {
    S.Diag(getFunctionParamRange(D, 1).getBegin(),
           diag::err_anyx86_interrupt_attribute)
        << 1 << 3 << S.Context.getIntTypeForBitwidth(64, /*Signed=*/false);
    return;
  }
  D->addAttr(::new (S.Context) AnyX86InterruptAttr(S.Context, AL));
  D->addAttr(UsedAttr::CreateImplicit(S.Context));
}

bool hasBTFDeclTagAttr(Decl *D, llvm::StringRef Tag) {
  for (const auto *I : D->specific_attrs<BTFDeclTagAttr>()) {
    if (I->getBTFDeclTag() == Tag)
      return true;
  }
  return false;
}

void handleBTFDeclTagAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  llvm::StringRef Str;
  if (!S.checkStringLiteralArgumentAttr(AL, 0, Str))
    return;
  if (hasBTFDeclTagAttr(D, Str))
    return;

  D->addAttr(::new (S.Context) BTFDeclTagAttr(S.Context, AL, Str));
}

} // namespace

BTFDeclTagAttr *Sema::mergeBTFDeclTagAttr(Decl *D, const BTFDeclTagAttr &AL) {
  if (hasBTFDeclTagAttr(D, AL.getBTFDeclTag()))
    return nullptr;
  return ::new (Context) BTFDeclTagAttr(Context, AL, AL.getBTFDeclTag());
}

namespace {

void handleInterruptAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  switch (S.Context.getTargetInfo().getTriple().getArch()) {
  case llvm::Triple::x86_64:
    handleAnyX86InterruptAttr(S, D, AL);
    break;
  default:
    S.Diag(AL.getLoc(), diag::err_attribute_not_supported_on_arch)
        << AL << S.Context.getTargetInfo().getTriple().getArchName();
    break;
  }
}

void handleX86ForceAlignArgPointerAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  // If we try to apply it to a function pointer, don't warn, but don't
  // do anything, either. It doesn't matter anyway, because there's nothing
  // special about calling a force_align_arg_pointer function.
  const auto *VD = dyn_cast<ValueDecl>(D);
  if (VD && VD->getType()->isFunctionPointerType())
    return;
  // Also don't warn on function pointer typedefs.
  const auto *TD = dyn_cast<TypedefNameDecl>(D);
  if (TD && (TD->getUnderlyingType()->isFunctionPointerType() ||
             TD->getUnderlyingType()->isFunctionType()))
    return;
  // Attribute can only be applied to function types.
  if (!isa<FunctionDecl>(D)) {
    S.Diag(AL.getLoc(), diag::warn_attribute_wrong_decl_type)
        << AL << AL.isRegularKeywordAttribute() << ExpectedFunction;
    return;
  }

  D->addAttr(::new (S.Context) X86ForceAlignArgPointerAttr(S.Context, AL));
}

} // namespace

DLLImportAttr *Sema::mergeDLLImportAttr(Decl *D,
                                        const AttributeCommonInfo &CI) {
  if (D->hasAttr<DLLExportAttr>()) {
    Diag(CI.getLoc(), diag::warn_attribute_ignored) << "'dllimport'";
    return nullptr;
  }

  if (D->hasAttr<DLLImportAttr>())
    return nullptr;

  return ::new (Context) DLLImportAttr(Context, CI);
}

DLLExportAttr *Sema::mergeDLLExportAttr(Decl *D,
                                        const AttributeCommonInfo &CI) {
  if (DLLImportAttr *Import = D->getAttr<DLLImportAttr>()) {
    Diag(Import->getLocation(), diag::warn_attribute_ignored) << Import;
    D->dropAttr<DLLImportAttr>();
  }

  if (D->hasAttr<DLLExportAttr>())
    return nullptr;

  return ::new (Context) DLLExportAttr(Context, CI);
}

namespace {

void handleDLLAttr(Sema &S, Decl *D, const ParsedAttr &A) {
  if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
    if (FD->isInlined() && A.getKind() == ParsedAttr::AT_DLLImport &&
        !(S.Context.getTargetInfo().shouldDLLImportComdatSymbols())) {
      S.Diag(A.getRange().getBegin(), diag::warn_attribute_ignored_on_inline)
          << A;
      return;
    }
  }

  Attr *NewAttr = A.getKind() == ParsedAttr::AT_DLLExport
                      ? (Attr *)S.mergeDLLExportAttr(D, A)
                      : (Attr *)S.mergeDLLImportAttr(D, A);
  if (NewAttr)
    D->addAttr(NewAttr);
}

void handleDeprecatedAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  llvm::StringRef Str, Replacement;
  if (AL.isArgExpr(0) && AL.getArgAsExpr(0) &&
      !S.checkStringLiteralArgumentAttr(AL, 0, Str))
    return;

  // Support a single optional message only for Declspec and [[]] spellings.
  if (AL.isDeclspecAttribute() || AL.isStandardAttributeSyntax())
    AL.checkAtMostNumArgs(S, 1);
  else if (AL.isArgExpr(1) && AL.getArgAsExpr(1) &&
           !S.checkStringLiteralArgumentAttr(AL, 1, Replacement))
    return;

  if (AL.isBracketAttribute() && !AL.isGNUScope())
    S.Diag(AL.getLoc(), diag::ext_c23_attr) << AL;

  D->addAttr(::new (S.Context) DeprecatedAttr(S.Context, AL, Str, Replacement));
}

bool isGlobalVar(const Decl *D) {
  if (const auto *S = dyn_cast<VarDecl>(D))
    return S->hasGlobalStorage();
  return false;
}

void handleInternalLinkageAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (InternalLinkageAttr *Internal = S.mergeInternalLinkageAttr(D, AL))
    D->addAttr(Internal);
}

void handleZeroCallUsedRegsAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  llvm::StringRef KindStr;
  SourceLocation LiteralLoc;
  if (!S.checkStringLiteralArgumentAttr(AL, 0, KindStr, &LiteralLoc))
    return;

  ZeroCallUsedRegsAttr::ZeroCallUsedRegsKind Kind;
  if (!ZeroCallUsedRegsAttr::ConvertStrToZeroCallUsedRegsKind(KindStr, Kind)) {
    S.Diag(LiteralLoc, diag::warn_attribute_type_not_supported)
        << AL << KindStr;
    return;
  }

  D->dropAttr<ZeroCallUsedRegsAttr>();
  D->addAttr(ZeroCallUsedRegsAttr::Create(S.Context, Kind, AL));
}

void handleFunctionReturnThunksAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  llvm::StringRef KindStr;
  SourceLocation LiteralLoc;
  if (!S.checkStringLiteralArgumentAttr(AL, 0, KindStr, &LiteralLoc))
    return;

  FunctionReturnThunksAttr::Kind Kind;
  if (!FunctionReturnThunksAttr::ConvertStrToKind(KindStr, Kind)) {
    S.Diag(LiteralLoc, diag::warn_attribute_type_not_supported)
        << AL << KindStr;
    return;
  }
  // It would be good to better handle attribute merging rather than
  // silently replacing the existing attribute, so long as it does not break
  // the expected codegen tests.
  D->dropAttr<FunctionReturnThunksAttr>();
  D->addAttr(FunctionReturnThunksAttr::Create(S.Context, Kind, AL));
}

void handleAvailableOnlyInDefaultEvalMethod(Sema &S, Decl *D,
                                            const ParsedAttr &AL) {
  assert(isa<TypedefNameDecl>(D) && "This attribute only applies to a typedef");
  handleSimpleAttribute<AvailableOnlyInDefaultEvalMethodAttr>(S, D, AL);
}

void handleNoMergeAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  auto *VDecl = dyn_cast<VarDecl>(D);
  if (VDecl && !VDecl->isFunctionPointerType()) {
    S.Diag(AL.getLoc(), diag::warn_attribute_ignored_non_function_pointer)
        << AL << VDecl;
    return;
  }
  D->addAttr(NoMergeAttr::Create(S.Context, AL));
}

void handleDestroyAttr(Sema &S, Decl *D, const ParsedAttr &A) {
  if (!cast<VarDecl>(D)->hasGlobalStorage()) {
    S.Diag(D->getLocation(), diag::err_destroy_attr_on_non_static_var)
        << (A.getKind() == ParsedAttr::AT_AlwaysDestroy);
    return;
  }

  if (A.getKind() == ParsedAttr::AT_AlwaysDestroy)
    handleSimpleAttribute<AlwaysDestroyAttr>(S, D, A);
  else
    handleSimpleAttribute<NoDestroyAttr>(S, D, A);
}

void handleUninitializedAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  assert(cast<VarDecl>(D)->getStorageDuration() == SD_Automatic &&
         "uninitialized is only valid on automatic duration variables");
  D->addAttr(::new (S.Context) UninitializedAttr(S.Context, AL));
}

void handleMSAllocatorAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  // Warn if the return type is not a pointer type.
  if (auto *FD = dyn_cast<FunctionDecl>(D)) {
    QualType RetTy = FD->getReturnType();
    if (!RetTy->isPointerType()) {
      S.Diag(AL.getLoc(), diag::warn_declspec_allocator_nonpointer)
          << AL.getRange() << RetTy;
      return;
    }
  }

  handleSimpleAttribute<MSAllocatorAttr>(S, D, AL);
}

void handleAcquireHandleAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (AL.isUsedAsTypeAttr())
    return;
  // Warn if the parameter is definitely not an output parameter.
  if (const auto *PVD = dyn_cast<ParmVarDecl>(D)) {
    if (PVD->getType()->isIntegerType()) {
      S.Diag(AL.getLoc(), diag::err_attribute_output_parameter)
          << AL.getRange();
      return;
    }
  }
  llvm::StringRef Argument;
  if (!S.checkStringLiteralArgumentAttr(AL, 0, Argument))
    return;
  D->addAttr(AcquireHandleAttr::Create(S.Context, Argument, AL));
}

template <typename Attr>
void handleHandleAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  llvm::StringRef Argument;
  if (!S.checkStringLiteralArgumentAttr(AL, 0, Argument))
    return;
  D->addAttr(Attr::Create(S.Context, Argument, AL));
}

template <typename Attr>
void handleUnsafeBufferUsage(Sema &S, Decl *D, const ParsedAttr &AL) {
  D->addAttr(Attr::Create(S.Context, AL));
}

void handleCFGuardAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  // The guard attribute takes a single identifier argument.

  if (!AL.isArgIdent(0)) {
    S.Diag(AL.getLoc(), diag::err_attribute_argument_type)
        << AL << AANT_ArgumentIdentifier;
    return;
  }

  CFGuardAttr::GuardArg Arg;
  IdentifierInfo *II = AL.getArgAsIdent(0)->Ident;
  if (!CFGuardAttr::ConvertStrToGuardArg(II->getName(), Arg)) {
    S.Diag(AL.getLoc(), diag::warn_attribute_type_not_supported) << AL << II;
    return;
  }

  D->addAttr(::new (S.Context) CFGuardAttr(S.Context, AL, Arg));
}

template <typename AttrTy>
const AttrTy *findEnforceTCBAttrByName(Decl *D, llvm::StringRef Name) {
  auto Attrs = D->specific_attrs<AttrTy>();
  auto I = llvm::find_if(
      Attrs, [Name](const AttrTy *A) { return A->getTCBName() == Name; });
  return I == Attrs.end() ? nullptr : *I;
}

template <typename AttrTy, typename ConflictingAttrTy>
void handleEnforceTCBAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  llvm::StringRef Argument;
  if (!S.checkStringLiteralArgumentAttr(AL, 0, Argument))
    return;

  // A function cannot be have both regular and leaf membership in the same TCB.
  if (const ConflictingAttrTy *ConflictingAttr =
          findEnforceTCBAttrByName<ConflictingAttrTy>(D, Argument)) {
    // We could attach a note to the other attribute but in this case
    // there's no need given how the two are very close to each other.
    S.Diag(AL.getLoc(), diag::err_tcb_conflicting_attributes)
        << AL.getAttrName()->getName()
        << ConflictingAttr->getAttrName()->getName() << Argument;

    // Error recovery: drop the non-leaf attribute so that to suppress
    // all future warnings caused by erroneous attributes. The leaf attribute
    // needs to be kept because it can only suppresses warnings, not cause them.
    D->dropAttr<EnforceTCBAttr>();
    return;
  }

  D->addAttr(AttrTy::Create(S.Context, Argument, AL));
}

template <typename AttrTy, typename ConflictingAttrTy>
AttrTy *mergeEnforceTCBAttrImpl(Sema &S, Decl *D, const AttrTy &AL) {
  // Check if the new redeclaration has different leaf-ness in the same TCB.
  llvm::StringRef TCBName = AL.getTCBName();
  if (const ConflictingAttrTy *ConflictingAttr =
          findEnforceTCBAttrByName<ConflictingAttrTy>(D, TCBName)) {
    S.Diag(ConflictingAttr->getLoc(), diag::err_tcb_conflicting_attributes)
        << ConflictingAttr->getAttrName()->getName()
        << AL.getAttrName()->getName() << TCBName;

    // Add a note so that the user could easily find the conflicting attribute.
    S.Diag(AL.getLoc(), diag::note_conflicting_attribute);

    // More error recovery.
    D->dropAttr<EnforceTCBAttr>();
    return nullptr;
  }

  TreeContext &Context = S.getTreeContext();
  return ::new (Context) AttrTy(Context, AL, AL.getTCBName());
}

} // namespace

EnforceTCBAttr *Sema::mergeEnforceTCBAttr(Decl *D, const EnforceTCBAttr &AL) {
  return mergeEnforceTCBAttrImpl<EnforceTCBAttr, EnforceTCBLeafAttr>(*this, D,
                                                                     AL);
}

EnforceTCBLeafAttr *
Sema::mergeEnforceTCBLeafAttr(Decl *D, const EnforceTCBLeafAttr &AL) {
  return mergeEnforceTCBAttrImpl<EnforceTCBLeafAttr, EnforceTCBAttr>(*this, D,
                                                                     AL);
}

namespace {

void handleArmNewZaAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (auto *FPT = dyn_cast<FunctionProtoType>(D->getFunctionType())) {
    if (FPT->getAArch64SMEAttributes() & FunctionType::SME_PStateZASharedMask) {
      S.Diag(AL.getLoc(), diag::err_attributes_are_not_compatible)
          << AL << "'__arm_shared_za'" << true;
      AL.setInvalid();
    }
    if (FPT->getAArch64SMEAttributes() &
        FunctionType::SME_PStateZAPreservedMask) {
      S.Diag(AL.getLoc(), diag::err_attributes_are_not_compatible)
          << AL << "'__arm_preserves_za'" << true;
      AL.setInvalid();
    }
    if (AL.isInvalid())
      return;
  }

  handleSimpleAttribute<ArmNewZAAttr>(S, D, AL);
}

void processDeclAttribute(Sema &S, Scope *scope, Decl *D, const ParsedAttr &AL,
                          const Sema::ProcessDeclAttributeOptions &Options) {
  if (AL.isInvalid() || AL.getKind() == ParsedAttr::IgnoredAttribute)
    return;

  // Ignore [[...]] attributes on declarator chunks: they appertain to the type.
  if (AL.isBracketAttribute() && !Options.IncludeBracketAttributes)
    return;

  // Unknown attributes are automatically warned on. Target-specific attributes
  // which do not apply to the current target architecture are treated as
  // though they were unknown attributes.
  if (AL.getKind() == ParsedAttr::UnknownAttribute ||
      !AL.existsInTarget(S.Context.getTargetInfo())) {
    S.Diag(AL.getLoc(),
           AL.isRegularKeywordAttribute()
               ? (unsigned)diag::err_keyword_not_supported_on_target
           : AL.isDeclspecAttribute()
               ? (unsigned)diag::warn_unhandled_ms_attribute_ignored
               : (unsigned)diag::warn_unknown_attribute_ignored)
        << AL << AL.getRange();
    return;
  }

  if (S.checkCommonAttributeFeatures(D, AL))
    return;

  switch (AL.getKind()) {
  default:
    if (AL.getInfo().handleDeclAttribute(S, D, AL) !=
        ParsedAttrInfo::NotHandled)
      break;
    if (!AL.isStmtAttr()) {
      assert(AL.isTypeAttr() && "Non-type attribute not handled");
    }
    if (AL.isTypeAttr()) {
      if (Options.IgnoreTypeAttributes)
        break;
      if (!AL.isStandardAttributeSyntax() && !AL.isRegularKeywordAttribute()) {
        // Non-[[]] type attributes are handled in processTypeAttrs(); silently
        // move on.
        break;
      }

      // A `[[]]` type attribute on a declaration is non-standard. However, we
      // have in the past allowed some type attributes to "slide" to the
      // `DeclSpec`, so we need to continue to support this legacy behavior. We
      // only do this, however, if
      // - we actually have a `DeclSpec`, i.e. if we're looking at a
      //   `DeclaratorDecl`, or
      // - we are looking at an alias-declaration, where historically we have
      //   allowed type attributes after the identifier to slide to the type.
      if (AL.slidesFromDeclToDeclSpecLegacyBehavior() &&
          isa<DeclaratorDecl>(D)) {
        // Suggest moving the attribute to the type instead, but only for our
        // own vendor attributes; moving other vendors' attributes might hurt
        // portability.
        if (AL.isNeverCScope()) {
          S.Diag(AL.getLoc(), diag::warn_type_attribute_deprecated_on_decl)
              << AL << D->getLocation();
        }

        // Allow this type attribute to be handled in processTypeAttrs();
        // silently move on.
        break;
      }

      if (AL.getKind() == ParsedAttr::AT_Regparm) {
        // `regparm` is a special case: It's a type attribute but we still want
        // to treat it as if it had been written on the declaration because that
        // way we'll be able to handle it directly in `processTypeAttr()`.
        // If we treated `regparm` it as if it had been written on the
        // `DeclSpec`, the logic in `distributeFunctionTypeAttrFromDeclSepc()`
        // would try to move it to the declarator, but that doesn't work: We
        // can't remove the attribute from the list of declaration attributes
        // because it might be needed by other declarators in the same
        // declaration.
        break;
      }

      if (AL.getKind() == ParsedAttr::AT_VectorSize) {
        // `vector_size` is a special case: It's a type attribute semantically,
        // but GCC expects the [[]] syntax to be written on the declaration (and
        // warns that the attribute has no effect if it is placed on the
        // decl-specifier-seq).
        // Silently move on and allow the attribute to be handled in
        // processTypeAttr().
        break;
      }

      if (AL.getKind() == ParsedAttr::AT_NoDeref) {
        // `noderef` doesn't work correctly in [[]] syntax yet.
        // processTypeAttrs() emits a warning; silently move on here.
        break;
      }
    }
    // N.B., NeverCAttrEmitter.cpp emits a diagnostic helper that ensures a
    // statement attribute is not written on a declaration, but this code is
    // needed for type attributes as well as statement attributes in Attr.td
    // that do not list any subjects.
    S.Diag(AL.getLoc(), diag::err_attribute_invalid_on_decl)
        << AL << AL.isRegularKeywordAttribute() << D->getLocation();
    break;
  case ParsedAttr::AT_Interrupt:
    handleInterruptAttr(S, D, AL);
    break;
  case ParsedAttr::AT_X86ForceAlignArgPointer:
    handleX86ForceAlignArgPointerAttr(S, D, AL);
    break;
  case ParsedAttr::AT_ReadOnlyPlacement:
    handleSimpleAttribute<ReadOnlyPlacementAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_DLLExport:
  case ParsedAttr::AT_DLLImport:
    handleDLLAttr(S, D, AL);
    break;
  case ParsedAttr::AT_BTFDeclTag:
    handleBTFDeclTagAttr(S, D, AL);
    break;
  case ParsedAttr::AT_IFunc:
    handleIFuncAttr(S, D, AL);
    break;
  case ParsedAttr::AT_Alias:
    handleAliasAttr(S, D, AL);
    break;
  case ParsedAttr::AT_Aligned:
    handleAlignedAttr(S, D, AL);
    break;
  case ParsedAttr::AT_AlignValue:
    handleAlignValueAttr(S, D, AL);
    break;
  case ParsedAttr::AT_AllocSize:
    handleAllocSizeAttr(S, D, AL);
    break;
  case ParsedAttr::AT_AlwaysInline:
    handleAlwaysInlineAttr(S, D, AL);
    break;
  case ParsedAttr::AT_AnalyzerNoReturn:
    handleAnalyzerNoReturnAttr(S, D, AL);
    break;
  case ParsedAttr::AT_TLSModel:
    handleTLSModelAttr(S, D, AL);
    break;
  case ParsedAttr::AT_Annotate:
    handleAnnotateAttr(S, D, AL);
    break;
  case ParsedAttr::AT_Availability:
    handleAvailabilityAttr(S, D, AL);
    break;
  case ParsedAttr::AT_CarriesDependency:
    handleDependencyAttr(S, scope, D, AL);
    break;
  case ParsedAttr::AT_CPUDispatch:
  case ParsedAttr::AT_CPUSpecific:
    handleCPUSpecificAttr(S, D, AL);
    break;
  case ParsedAttr::AT_Common:
    handleCommonAttr(S, D, AL);
    break;
  case ParsedAttr::AT_PassObjectSize:
    handlePassObjectSizeAttr(S, D, AL);
    break;
  case ParsedAttr::AT_Constructor:
    handleConstructorAttr(S, D, AL);
    break;
  case ParsedAttr::AT_Deprecated:
    handleDeprecatedAttr(S, D, AL);
    break;
  case ParsedAttr::AT_Destructor:
    handleDestructorAttr(S, D, AL);
    break;
  case ParsedAttr::AT_EnableIf:
    handleEnableIfAttr(S, D, AL);
    break;
  case ParsedAttr::AT_Error:
    handleErrorAttr(S, D, AL);
    break;
  case ParsedAttr::AT_DiagnoseIf:
    handleDiagnoseIfAttr(S, D, AL);
    break;
  case ParsedAttr::AT_DiagnoseAsBuiltin:
    handleDiagnoseAsBuiltinAttr(S, D, AL);
    break;
  case ParsedAttr::AT_NoBuiltin:
    handleNoBuiltinAttr(S, D, AL);
    break;
  case ParsedAttr::AT_ExtVectorType:
    handleExtVectorTypeAttr(S, D, AL);
    break;
  case ParsedAttr::AT_MinSize:
    handleMinSizeAttr(S, D, AL);
    break;
  case ParsedAttr::AT_OptimizeNone:
    handleOptimizeNoneAttr(S, D, AL);
    break;
  case ParsedAttr::AT_EnumExtensibility:
    handleEnumExtensibilityAttr(S, D, AL);
    break;
  case ParsedAttr::AT_Format:
    handleFormatAttr(S, D, AL);
    break;
  case ParsedAttr::AT_FormatArg:
    handleFormatArgAttr(S, D, AL);
    break;
  case ParsedAttr::AT_Callback:
    handleCallbackAttr(S, D, AL);
    break;
  case ParsedAttr::AT_GNUInline:
    handleGNUInlineAttr(S, D, AL);
    break;
  case ParsedAttr::AT_Restrict:
    handleRestrictAttr(S, D, AL);
    break;
  case ParsedAttr::AT_Mode:
    handleModeAttr(S, D, AL);
    break;
  case ParsedAttr::AT_NonNull:
    if (auto *PVD = dyn_cast<ParmVarDecl>(D))
      handleNonNullAttrParameter(S, PVD, AL);
    else
      handleNonNullAttr(S, D, AL);
    break;
  case ParsedAttr::AT_ReturnsNonNull:
    handleReturnsNonNullAttr(S, D, AL);
    break;
  case ParsedAttr::AT_NoEscape:
    handleNoEscapeAttr(S, D, AL);
    break;
  case ParsedAttr::AT_MaybeUndef:
    handleSimpleAttribute<MaybeUndefAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_AssumeAligned:
    handleAssumeAlignedAttr(S, D, AL);
    break;
  case ParsedAttr::AT_AllocAlign:
    handleAllocAlignAttr(S, D, AL);
    break;
  case ParsedAttr::AT_Naked:
    handleNakedAttr(S, D, AL);
    break;
  case ParsedAttr::AT_Volatile:
    handleVolatileAttr(S, D, AL);
    break;
  case ParsedAttr::AT_DisableTryStmt:
    handleDisableTryStmtAttr(S, D, AL);
    break;
  case ParsedAttr::AT_NoReturn:
    handleNoReturnAttr(S, D, AL);
    break;
  case ParsedAttr::AT_StandardNoReturn:
    handleStandardNoReturnAttr(S, D, AL);
    break;
  case ParsedAttr::AT_AnyX86NoCfCheck:
    handleNoCfCheckAttr(S, D, AL);
    break;
  case ParsedAttr::AT_NoThrow:
    if (!AL.isUsedAsTypeAttr())
      handleSimpleAttribute<NoThrowAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_Packed:
    handlePackedAttr(S, D, AL);
    break;
  case ParsedAttr::AT_Section:
    handleSectionAttr(S, D, AL);
    break;
  case ParsedAttr::AT_RandomizeLayout:
    handleRandomizeLayoutAttr(S, D, AL);
    break;
  case ParsedAttr::AT_NoRandomizeLayout:
    handleNoRandomizeLayoutAttr(S, D, AL);
    break;
  case ParsedAttr::AT_CodeSeg:
    handleCodeSegAttr(S, D, AL);
    break;
  case ParsedAttr::AT_Target:
    handleTargetAttr(S, D, AL);
    break;
  case ParsedAttr::AT_TargetVersion:
    handleTargetVersionAttr(S, D, AL);
    break;
  case ParsedAttr::AT_TargetClones:
    handleTargetClonesAttr(S, D, AL);
    break;
  case ParsedAttr::AT_MinVectorWidth:
    handleMinVectorWidthAttr(S, D, AL);
    break;
  case ParsedAttr::AT_Unavailable:
    handleAttrWithMessage<UnavailableAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_Assumption:
    handleAssumumptionAttr(S, D, AL);
    break;
  case ParsedAttr::AT_Unused:
    handleUnusedAttr(S, D, AL);
    break;
  case ParsedAttr::AT_Visibility:
    handleVisibilityAttr(S, D, AL, false);
    break;
  case ParsedAttr::AT_TypeVisibility:
    handleVisibilityAttr(S, D, AL, true);
    break;
  case ParsedAttr::AT_WarnUnusedResult:
    handleWarnUnusedResult(S, D, AL);
    break;
  case ParsedAttr::AT_WeakRef:
    handleWeakRefAttr(S, D, AL);
    break;
  case ParsedAttr::AT_WeakImport:
    handleWeakImportAttr(S, D, AL);
    break;
  case ParsedAttr::AT_TransparentUnion:
    handleTransparentUnionAttr(S, D, AL);
    break;
  case ParsedAttr::AT_Sentinel:
    handleSentinelAttr(S, D, AL);
    break;
  case ParsedAttr::AT_Cleanup:
    handleCleanupAttr(S, D, AL);
    break;
  case ParsedAttr::AT_NoDebug:
    handleNoDebugAttr(S, D, AL);
    break;
  case ParsedAttr::AT_StdCall:
  case ParsedAttr::AT_CDecl:
  case ParsedAttr::AT_FastCall:
  case ParsedAttr::AT_RegCall:
  case ParsedAttr::AT_VectorCall:
  case ParsedAttr::AT_MSABI:
  case ParsedAttr::AT_SysVABI:
  case ParsedAttr::AT_PreserveMost:
  case ParsedAttr::AT_PreserveAll:
  case ParsedAttr::AT_AArch64VectorPcs:
  case ParsedAttr::AT_AArch64SVEPcs:
    handleCallConvAttr(S, D, AL);
    break;
  case ParsedAttr::AT_Suppress:
    handleSuppressAttr(S, D, AL);
    break;
  case ParsedAttr::AT_InternalLinkage:
    handleInternalLinkageAttr(S, D, AL);
    break;
  case ParsedAttr::AT_ZeroCallUsedRegs:
    handleZeroCallUsedRegsAttr(S, D, AL);
    break;
  case ParsedAttr::AT_FunctionReturnThunks:
    handleFunctionReturnThunksAttr(S, D, AL);
    break;
  case ParsedAttr::AT_NoMerge:
    handleNoMergeAttr(S, D, AL);
    break;
  case ParsedAttr::AT_AvailableOnlyInDefaultEvalMethod:
    handleAvailableOnlyInDefaultEvalMethod(S, D, AL);
    break;

  // Microsoft attributes:
  case ParsedAttr::AT_Thread:
    handleDeclspecThreadAttr(S, D, AL);
    break;
  case ParsedAttr::AT_CFGuard:
    handleCFGuardAttr(S, D, AL);
    break;

  // Type safety attributes.
  case ParsedAttr::AT_ArgumentWithTypeTag:
    handleArgumentWithTypeTagAttr(S, D, AL);
    break;
  case ParsedAttr::AT_TypeTagForDatatype:
    handleTypeTagForDatatypeAttr(S, D, AL);
    break;

  case ParsedAttr::AT_PatchableFunctionEntry:
    handlePatchableFunctionEntryAttr(S, D, AL);
    break;

  case ParsedAttr::AT_AlwaysDestroy:
  case ParsedAttr::AT_NoDestroy:
    handleDestroyAttr(S, D, AL);
    break;

  case ParsedAttr::AT_Uninitialized:
    handleUninitializedAttr(S, D, AL);
    break;

  case ParsedAttr::AT_MSAllocator:
    handleMSAllocatorAttr(S, D, AL);
    break;

  case ParsedAttr::AT_ArmBuiltinAlias:
    handleArmBuiltinAliasAttr(S, D, AL);
    break;

  case ParsedAttr::AT_ArmLocallyStreaming:
    handleSimpleAttribute<ArmLocallyStreamingAttr>(S, D, AL);
    break;

  case ParsedAttr::AT_ArmNewZA:
    handleArmNewZaAttr(S, D, AL);
    break;

  case ParsedAttr::AT_AcquireHandle:
    handleAcquireHandleAttr(S, D, AL);
    break;

  case ParsedAttr::AT_ReleaseHandle:
    handleHandleAttr<ReleaseHandleAttr>(S, D, AL);
    break;

  case ParsedAttr::AT_UnsafeBufferUsage:
    handleUnsafeBufferUsage<UnsafeBufferUsageAttr>(S, D, AL);
    break;

  case ParsedAttr::AT_UseHandle:
    handleHandleAttr<UseHandleAttr>(S, D, AL);
    break;

  case ParsedAttr::AT_EnforceTCB:
    handleEnforceTCBAttr<EnforceTCBAttr, EnforceTCBLeafAttr>(S, D, AL);
    break;

  case ParsedAttr::AT_EnforceTCBLeaf:
    handleEnforceTCBAttr<EnforceTCBLeafAttr, EnforceTCBAttr>(S, D, AL);
    break;

  case ParsedAttr::AT_BuiltinAlias:
    handleBuiltinAliasAttr(S, D, AL);
    break;

  case ParsedAttr::AT_PreferredType:
    handlePreferredTypeAttr(S, D, AL);
    break;
  }
}

} // namespace

// ===----------------------------------------------------------------------===
// Sema attribute processing entry points
// ===----------------------------------------------------------------------===

void Sema::ProcessDeclAttributeList(
    Scope *S, Decl *D, const ParsedAttributesView &AttrList,
    const ProcessDeclAttributeOptions &Options) {
  if (AttrList.empty())
    return;

  for (const ParsedAttr &AL : AttrList)
    processDeclAttribute(*this, S, D, AL, Options);

  // GCC accepts
  // static int a9 __attribute__((weakref));
  // but that looks really pointless. We reject it.
  if (D->hasAttr<WeakRefAttr>() && !D->hasAttr<AliasAttr>()) {
    Diag(AttrList.begin()->getLoc(), diag::err_attribute_weakref_without_alias)
        << cast<NamedDecl>(D);
    D->dropAttr<WeakRefAttr>();
    return;
  }

  // It would be good to have a way to specify "these attributes must appear as
  // a group", for these. Additionally, it would be good to have a way to
  // specify "these attribute must never appear as a group" for attributes like
  // cold and hot.
}

// Helper for delayed processing TransparentUnion attribute.
void Sema::ProcessDeclAttributeDelayed(Decl *D,
                                       const ParsedAttributesView &AttrList) {
  for (const ParsedAttr &AL : AttrList)
    if (AL.getKind() == ParsedAttr::AT_TransparentUnion) {
      handleTransparentUnionAttr(*this, D, AL);
      break;
    }
}

// Annotation attributes are the only attributes allowed after an access
// specifier.
void checkUnusedDeclAttributes(Sema &S, const ParsedAttributesView &A) {
  for (const ParsedAttr &AL : A) {
    // Only warn if the attribute is an unignored, non-type attribute.
    if (AL.isUsedAsTypeAttr() || AL.isInvalid())
      continue;
    if (AL.getKind() == ParsedAttr::IgnoredAttribute)
      continue;

    if (AL.getKind() == ParsedAttr::UnknownAttribute) {
      S.Diag(AL.getLoc(), diag::warn_unknown_attribute_ignored)
          << AL << AL.getRange();
    } else {
      S.Diag(AL.getLoc(), diag::warn_attribute_not_on_decl)
          << AL << AL.getRange();
    }
  }
}

void Sema::checkUnusedDeclAttributes(Declarator &D) {
  ::checkUnusedDeclAttributes(*this, D.getDeclarationAttributes());
  ::checkUnusedDeclAttributes(*this, D.getDeclSpec().getAttributes());
  ::checkUnusedDeclAttributes(*this, D.getAttributes());
  for (unsigned i = 0, e = D.getNumTypeObjects(); i != e; ++i)
    ::checkUnusedDeclAttributes(*this, D.getTypeObject(i).getAttrs());
}

NamedDecl *Sema::DeclClonePragmaWeak(NamedDecl *ND, const IdentifierInfo *II,
                                     SourceLocation Loc) {
  assert(isa<FunctionDecl>(ND) || isa<VarDecl>(ND));
  NamedDecl *NewD = nullptr;
  if (auto *FD = dyn_cast<FunctionDecl>(ND)) {
    FunctionDecl *NewFD;
    NewFD = FunctionDecl::Create(
        FD->getTreeContext(), FD->getDeclContext(), Loc, Loc,
        DeclarationName(II), FD->getType(), FD->getTypeSourceInfo(), SC_None,
        getCurFPFeatures().isFPConstrained(), false /*isInlineSpecified*/,
        FD->hasPrototype(), ConstexprSpecKind::Unspecified);
    NewD = NewFD;

    // Fake up parameter variables; they are declared as if this were
    // a typedef.
    QualType FDTy = FD->getType();
    if (const auto *FT = FDTy->getAs<FunctionProtoType>()) {
      llvm::SmallVector<ParmVarDecl *, 16> Params;
      for (const auto &AI : FT->param_types()) {
        ParmVarDecl *Param = FormParmVarDeclForTypedef(NewFD, Loc, AI);
        Param->setScopeInfo(0, Params.size());
        Params.push_back(Param);
      }
      NewFD->setParams(Params);
    }
  } else if (auto *VD = dyn_cast<VarDecl>(ND)) {
    NewD = VarDecl::Create(VD->getTreeContext(), VD->getDeclContext(),
                           VD->getInnerLocStart(), VD->getLocation(), II,
                           VD->getType(), VD->getTypeSourceInfo(),
                           VD->getStorageClass());
  }
  return NewD;
}

void Sema::DeclApplyPragmaWeak(Scope *S, NamedDecl *ND, const WeakInfo &W) {
  if (W.getAlias()) { // clone decl, impersonate __attribute(weak,alias(...))
    IdentifierInfo *NDId = ND->getIdentifier();
    NamedDecl *NewD = DeclClonePragmaWeak(ND, W.getAlias(), W.getLocation());
    NewD->addAttr(
        AliasAttr::CreateImplicit(Context, NDId->getName(), W.getLocation()));
    NewD->addAttr(WeakAttr::CreateImplicit(Context, W.getLocation()));
    WeakTopLevelDecl.push_back(NewD);
    // Insert Decl at TU scope.
    DeclContext *SavedContext = CurContext;
    CurContext = Context.getTranslationUnitDecl();
    NewD->setDeclContext(CurContext);
    NewD->setLexicalDeclContext(CurContext);
    PushOnScopeChains(NewD, S);
    CurContext = SavedContext;
  } else { // just add weak to existing
    ND->addAttr(WeakAttr::CreateImplicit(Context, W.getLocation()));
  }
}

void Sema::ProcessPragmaWeak(Scope *S, Decl *D) {
  // It's valid to "forward-declare" #pragma weak, in which case we
  // have to do this.
  if (WeakUndeclaredIdentifiers.empty())
    return;
  NamedDecl *ND = nullptr;
  if (auto *VD = dyn_cast<VarDecl>(D))
    if (VD->isExternC())
      ND = VD;
  if (auto *FD = dyn_cast<FunctionDecl>(D))
    if (FD->isExternC())
      ND = FD;
  if (!ND)
    return;
  if (IdentifierInfo *Id = ND->getIdentifier()) {
    auto I = WeakUndeclaredIdentifiers.find(Id);
    if (I != WeakUndeclaredIdentifiers.end()) {
      auto &WeakInfos = I->second;
      for (const auto &W : WeakInfos)
        DeclApplyPragmaWeak(S, ND, W);
      std::remove_reference_t<decltype(WeakInfos)> EmptyWeakInfos;
      WeakInfos.swap(EmptyWeakInfos);
    }
  }
}

void Sema::ApplyDeclAttributes(Scope *S, Decl *D, const Declarator &PD) {
  // Ordering of attributes can be important, so we take care to process
  // attributes in the order in which they appeared in the source code.

  // First, process attributes that appeared on the declaration itself (but
  // only if they don't have the legacy behavior of "sliding" to the DeclSepc).
  ParsedAttributesView NonSlidingAttrs;
  for (ParsedAttr &AL : PD.getDeclarationAttributes()) {
    if (AL.slidesFromDeclToDeclSpecLegacyBehavior()) {
      // Skip processing the attribute, but do check if it appertains to the
      // declaration. This is needed for the `MatrixType` attribute, which,
      // despite being a type attribute, defines a `SubjectList` that only
      // allows it to be used on typedef declarations.
      AL.diagnoseAppertainsTo(*this, D);
    } else {
      NonSlidingAttrs.addAtEnd(&AL);
    }
  }
  ProcessDeclAttributeList(S, D, NonSlidingAttrs);

  // Apply decl attributes from the DeclSpec if present.
  if (!PD.getDeclSpec().getAttributes().empty()) {
    ProcessDeclAttributeList(S, D, PD.getDeclSpec().getAttributes(),
                             ProcessDeclAttributeOptions()
                                 .WithIncludeBracketAttributes(false)
                                 .WithIgnoreTypeAttributes(true));
  }

  // Walk the declarator structure, applying decl attributes that were in a type
  // position to the decl itself.  This handles cases like:
  //   int *__attr__(x)** D;
  // when X is a decl attribute.
  for (unsigned i = 0, e = PD.getNumTypeObjects(); i != e; ++i) {
    ProcessDeclAttributeList(S, D, PD.getTypeObject(i).getAttrs(),
                             ProcessDeclAttributeOptions()
                                 .WithIncludeBracketAttributes(false)
                                 .WithIgnoreTypeAttributes(true));
  }

  // Finally, apply any attributes on the decl itself.
  ProcessDeclAttributeList(S, D, PD.getAttributes());

  // Apply additional attributes specified by '#pragma neverc attribute'.
  AddPragmaAttributes(S, D);
}

void Sema::PopParsingDeclaration(ParsingDeclState state, Decl *decl) {
  assert(DelayedDiagnostics.getCurrentPool());
  DelayedDiagnosticPool &poppedPool = *DelayedDiagnostics.getCurrentPool();
  DelayedDiagnostics.popWithoutEmitting(state);

  // When delaying diagnostics to run in the context of a parsed
  // declaration, we only want to actually emit anything if parsing
  // succeeds.
  if (!decl)
    return;

  // We emit all the active diagnostics in this pool or any of its
  // parents.  In general, we'll get one pool for the decl spec
  // and a child pool for each declarator; in a decl group like:
  //   deprecated_typedef foo, *bar, baz();
  // only the declarator pops will be passed decls.  This is correct;
  // we really do need to consider delayed diagnostics from the decl spec
  // for each of the different declarations.
  const DelayedDiagnosticPool *pool = &poppedPool;
  do {
    for (DelayedDiagnosticPool::pool_iterator i = pool->pool_begin(),
                                              e = pool->pool_end();
         i != e; ++i) {
      // This const_cast is a bit lame.  Really, Triggered should be mutable.
      DelayedDiagnostic &diag = const_cast<DelayedDiagnostic &>(*i);
      if (diag.Triggered)
        continue;

      // Don't bother giving deprecation/unavailable diagnostics if
      // the decl is invalid.
      if (!decl->isInvalidDecl())
        handleDelayedAvailabilityCheck(diag, decl);
    }
  } while ((pool = pool->getParent()));
}

void Sema::redelayDiagnostics(DelayedDiagnosticPool &pool) {
  DelayedDiagnosticPool *curPool = DelayedDiagnostics.getCurrentPool();
  assert(curPool && "re-emitting in undelayed context not supported");
  curPool->steal(pool);
}
