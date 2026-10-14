#include "Frontend.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/ADT/DenseSet.h"

using namespace clang;
namespace nct {
namespace {

// The lowering implements the pinned libc++ loops. Follow the declarations
// actually selected by those loops, including instantiated traits and helper
// bodies. Header identity alone cannot authenticate a source specialization.
class RandomSelection : public RecursiveASTVisitor<RandomSelection> {
  const State &S;
  const SourceManager &SM;
  const ASTContext &Context;
  const CXXRecordDecl *Generator;
  QualType ResultType, DistanceType;
  llvm::DenseSet<const Decl *> Declarations;
  llvm::DenseSet<const Type *> Types;
  unsigned Nodes = 0;

  bool origin(const Decl *D) const {
    if (approvedStandardSDKDeclaration(S, SM, D))
      return true;
    // libc++'s cstdint/cstddef aliases refer to the pinned Clang resource
    // typedefs in the global namespace.
    const auto *Alias = dyn_cast_or_null<TypedefNameDecl>(D);
    if (Alias && Alias->isImplicit() && Alias->getLocation().isInvalid() &&
        ((Alias->getName() == "__uint128_t" &&
          Alias->getUnderlyingType()->isSpecificBuiltinType(BuiltinType::UInt128)) ||
         (Alias->getName() == "__int128_t" &&
          Alias->getUnderlyingType()->isSpecificBuiltinType(BuiltinType::Int128))))
      return true;
    const auto File = D ? S.sdkFile(SM, D->getLocation()) : std::nullopt;
    return Alias && File && File->Root == "resource" &&
           Alias->getUnderlyingType()->isBuiltinType() &&
           Alias->getUnderlyingType()->isIntegerType() &&
           Context.getTypeSize(Alias->getUnderlyingType()) <= 64;
  }
  bool qualifier(const NestedNameSpecifier *Q) {
    for (; Q; Q = Q->getPrefix())
      if (const auto *T = Q->getAsType())
        if (!retainType(QualType(T, 0)))
          return false;
    return true;
  }
  bool countLeadingZeros(const FunctionDecl *F) const {
    if (!F || !F->isImplicit())
      return false;
    switch (F->getBuiltinID()) {
    case Builtin::BI__builtin_clz:
    case Builtin::BI__builtin_clzl:
    case Builtin::BI__builtin_clzll:
    case Builtin::BI__builtin_clzg:
      return true;
    default:
      return false;
    }
  }
  bool sourceMethod(const CXXMethodDecl *M, const CallExpr *Call) {
    const auto *Definition = M->getDefinition();
    if (!Definition ||
        M->getParent()->getCanonicalDecl() != Generator->getCanonicalDecl() ||
        M->getNumParams() || M->isVariadic() || M->isVolatile() ||
        !Context.hasSameType(M->getReturnType(), ResultType))
      return false;
    for (const auto *D : M->redecls())
      if (!S.owns(SM, D->getLocation()))
        return false;
    if (!S.owns(SM, Definition->getLocation()))
      return false;
    if (M->getOverloadedOperator() == OO_Call) {
      const auto *Invocation = dyn_cast<CXXOperatorCallExpr>(Call);
      if (!Invocation || Invocation->getNumArgs() != 1 || M->isStatic() ||
          !ordinaryOperator(M) || !callableMethod(M) ||
          M->getTemplatedKind() != FunctionDecl::TK_NonTemplate ||
          !Invocation->getArg(0)->isLValue() || M->getRefQualifier() == RQ_RValue ||
          (Method && Method->getCanonicalDecl() != M->getCanonicalDecl()))
        return false;
      Method = M;
      return true;
    }
    if (!M->isStatic() || !M->isConstexpr() || !M->getIdentifier() ||
        (M->getName() != "min" && M->getName() != "max"))
      return false;
    const auto *Body = dyn_cast_or_null<CompoundStmt>(Definition->getBody());
    const auto *Return = Body && Body->size() == 1
                             ? dyn_cast<ReturnStmt>(*Body->body_begin())
                             : nullptr;
    const auto Value = Return && Return->getRetValue()
                           ? Return->getRetValue()->getIntegerConstantExpr(Context)
                           : std::nullopt;
    if (!Value || Value->getBitWidth() > 64)
      return false;
    auto &Selected = M->getName() == "min" ? Minimum : Maximum;
    auto &Bound = M->getName() == "min" ? MinimumValue : MaximumValue;
    if (Selected && Selected->getCanonicalDecl() != M->getCanonicalDecl())
      return false;
    Selected = M;
    Bound = Value->getZExtValue();
    return true;
  }

public:
  const CXXMethodDecl *Method = nullptr, *Minimum = nullptr, *Maximum = nullptr;
  uint64_t MinimumValue = 0, MaximumValue = 0;

  RandomSelection(const State &S, const SourceManager &SM,
                  const ASTContext &Context, const CXXRecordDecl *Generator,
                  QualType ResultType, QualType DistanceType)
      : S(S), SM(SM), Context(Context), Generator(Generator),
        ResultType(ResultType), DistanceType(DistanceType) {}

  bool retainType(QualType T) {
    if (T.isNull() || T->isDependentType() ||
        !Types.insert(T.getTypePtr()).second)
      return true;
    if (++Nodes > 4096)
      return false;
    if (const auto *Alias = dyn_cast<TypedefType>(T.getTypePtr())) {
      const auto *D = Alias->getDecl();
      if (D->getDeclContext() == Generator) {
        if (!S.owns(SM, D->getLocation()))
          return false;
      } else if (!retain(D)) {
        return false;
      }
      return retainType(Alias->desugar());
    }
    if (const auto *Specialization =
            dyn_cast<TemplateSpecializationType>(T.getTypePtr())) {
      if (const auto *Template =
              Specialization->getTemplateName().getAsTemplateDecl())
        if (!retain(Template))
          return false;
      for (const auto &Argument : Specialization->template_arguments())
        if (Argument.getKind() == TemplateArgument::Type) {
          if (!retainType(Argument.getAsType()))
            return false;
        } else if (Argument.getKind() == TemplateArgument::Template) {
          if (!retain(Argument.getAsTemplate().getAsTemplateDecl()))
            return false;
        }
    }
    if (const auto *RT = T->getAs<RecordType>()) {
      const auto *R = dyn_cast<CXXRecordDecl>(RT->getDecl());
      if (R && R->getCanonicalDecl() != Generator->getCanonicalDecl() &&
          !retain(R))
        return false;
    }
    if (T->isPointerType() || T->isReferenceType())
      if (!retainType(T->getPointeeType()))
        return false;
    const auto Desugared = T.getSingleStepDesugaredType(Context);
    return Desugared == T || retainType(Desugared);
  }
  bool retain(const Decl *D) {
    if (!D || !Declarations.insert(D).second)
      return true;
    if (const auto *Builtin = dyn_cast<BuiltinTemplateDecl>(D))
      return Builtin->isImplicit() && Builtin->getLocation().isInvalid() &&
             Builtin->getBuiltinTemplateKind() == BTK__builtin_common_type;
    if (++Nodes > 4096 || !origin(D))
      return false;
    if (const auto *T = dyn_cast<TemplateDecl>(D)) {
      for (const auto *R : T->redecls())
        if (!origin(R))
          return false;
      // Concrete uses below supply the instantiated body. The template's
      // written declaration must still come from the approved SDK.
      return origin(T->getTemplatedDecl());
    }
    if (const auto *R = dyn_cast<CXXRecordDecl>(D)) {
      for (const auto *Redeclaration : R->redecls())
        if (!origin(Redeclaration))
          return false;
      const auto *Definition = R->getDefinition();
      if (!Definition || !origin(Definition))
        return false;
      if (const auto *Specialization =
              dyn_cast<ClassTemplateSpecializationDecl>(Definition)) {
        if (Definition->getName() == "uniform_int_distribution" &&
            (Specialization->getTemplateArgs().size() != 1 ||
             Specialization->getTemplateArgs().get(0).getKind() !=
                 TemplateArgument::Type ||
             !Context.hasSameType(
                 Specialization->getTemplateArgs().get(0).getAsType(),
                 DistanceType)))
          return false;
        if (!retain(Specialization->getSpecializedTemplate()))
          return false;
        const auto Selected = Specialization->getSpecializedTemplateOrPartial();
        if (const auto *Partial =
                Selected.dyn_cast<ClassTemplatePartialSpecializationDecl *>())
          if (!retain(Partial))
            return false;
      }
      for (const auto &Base : Definition->bases())
        if (!retainType(Base.getType()))
          return false;
      for (const auto *Field : Definition->fields())
        if (!retainType(Field->getType()))
          return false;
      return true;
    }
    if (const auto *F = dyn_cast<FunctionDecl>(D)) {
      for (const auto *Redeclaration : F->redecls())
        if (!origin(Redeclaration))
          return false;
      if (const auto *Primary = F->getPrimaryTemplate())
        if (!retain(Primary))
          return false;
      if (const auto *M = dyn_cast<CXXMethodDecl>(F))
        if (!retain(M->getParent()))
          return false;
      if (!retainType(F->getReturnType()))
        return false;
      for (const auto *Parameter : F->parameters()) {
        if (!retainType(Parameter->getType()))
          return false;
        if (Parameter->hasDefaultArg() &&
            !Parameter->hasUninstantiatedDefaultArg() &&
            !TraverseStmt(const_cast<Expr *>(Parameter->getDefaultArg())))
          return false;
      }
      if (const auto *Definition = F->getDefinition()) {
        if (!origin(Definition))
          return false;
        if (const auto *Constructor = dyn_cast<CXXConstructorDecl>(Definition))
          for (const auto *Initializer : Constructor->inits())
            if (!TraverseStmt(Initializer->getInit()))
              return false;
        return TraverseStmt(Definition->getBody());
      }
      return true;
    }
    if (const auto *Alias = dyn_cast<TypedefNameDecl>(D))
      return retainType(Alias->getUnderlyingType());
    if (const auto *V = dyn_cast<ValueDecl>(D)) {
      if (const auto *Parent = dyn_cast<CXXRecordDecl>(V->getDeclContext()))
        if (!retain(Parent))
          return false;
      if (!retainType(V->getType()))
        return false;
      if (const auto *Variable = dyn_cast<VarDecl>(V)) {
        for (const auto *Redeclaration : Variable->redecls())
          if (!origin(Redeclaration))
            return false;
        if (const auto *Initializer = Variable->getAnyInitializer())
          if (!Initializer->isValueDependent() &&
              !TraverseStmt(const_cast<Expr *>(Initializer)))
            return false;
      }
    }
    return true;
  }
  bool VisitDeclRefExpr(DeclRefExpr *E) {
    const auto *F = dyn_cast<FunctionDecl>(E->getDecl());
    if (countLeadingZeros(F))
      return true;
    // Calls retain source methods with their actual selected receiver below.
    if (F && S.owns(SM, F->getLocation()))
      return qualifier(E->getQualifier());
    return qualifier(E->getQualifier()) && retain(E->getDecl());
  }
  bool VisitMemberExpr(MemberExpr *E) {
    if (const auto *M = dyn_cast<CXXMethodDecl>(E->getMemberDecl());
        M && S.owns(SM, M->getLocation()))
      return qualifier(E->getQualifier()) &&
             M->getParent()->getCanonicalDecl() == Generator->getCanonicalDecl();
    return qualifier(E->getQualifier()) && retain(E->getMemberDecl());
  }
  bool VisitCallExpr(CallExpr *E) {
    const auto *F = E->getDirectCallee();
    if (!F)
      return false;
    if (countLeadingZeros(F))
      return E->getType()->isSpecificBuiltinType(BuiltinType::Int) &&
             E->getNumArgs() >= 1 && E->getNumArgs() <= 2;
    if (S.owns(SM, F->getLocation())) {
      const auto *M = dyn_cast<CXXMethodDecl>(F);
      return M && sourceMethod(M, E);
    }
    if (F->getIdentifier() && F->getName() == "__sample" &&
        F->getNumParams() == 6 &&
        (!Context.hasSameType(F->getParamDecl(3)->getType(), DistanceType) ||
         !F->getParamDecl(5)->getType()->getAsCXXRecordDecl() ||
         F->getParamDecl(5)->getType()->getAsCXXRecordDecl()->getName() !=
             "forward_iterator_tag"))
      return false;
    return retain(F);
  }
  bool VisitCXXConstructExpr(CXXConstructExpr *E) {
    return retainType(E->getType()) && retain(E->getConstructor());
  }
  bool VisitType(Type *T) { return retainType(QualType(T, 0)); }
  bool VisitExpr(Expr *E) {
    const auto L = E->getExprLoc();
    return !L.isMacroID() ||
           !SM.isWrittenInMainFile(SM.getSpellingLoc(L));
  }
};

bool scalarElement(QualType T, const ASTContext &Context) {
  if (T.isVolatileQualified())
    return false;
  T = T.getUnqualifiedType();
  return ((T->isIntegerType() || T->isEnumeralType()) &&
          Context.getTypeSize(T) <= 64) ||
         T->isSpecificBuiltinType(BuiltinType::Float) ||
         T->isSpecificBuiltinType(BuiltinType::Double) ||
         T->isNullPtrType() ||
         (T->isPointerType() && !T->getPointeeType()->isFunctionType());
}
} // namespace

std::optional<UtilityAlgorithmRandomCall>
approvedUtilityAlgorithmRandomCall(const State &S, const SourceManager &SM,
                                   const CallExpr *Call,
                                   const ASTContext &Context) {
  const auto *F = Call ? Call->getDirectCallee() : nullptr;
  if (!S.coreV2() || !F || !F->getIdentifier() ||
      (F->getName() != "shuffle" && F->getName() != "sample") ||
      Call->isTypeDependent() || Call->isValueDependent() ||
      Call->isInstantiationDependent() || !Call->isPRValue())
    return std::nullopt;
  const bool Sample = F->getName() == "sample";
  const unsigned Parameters = Sample ? 5 : 3, Index = Parameters - 1;
  const auto *Primary = F->getPrimaryTemplate();
  const auto *Definition = F->getDefinition();
  const auto *Arguments = F->getTemplateSpecializationArgs();
  const auto File = S.sdkFile(SM, F->getLocation());
  if (F->getNumParams() != Parameters || Call->getNumArgs() != Parameters ||
      F->isVariadic() || !Primary || !Definition || !Arguments ||
      Arguments->size() != (Sample ? 4u : 2u) || !File ||
      File->Root != "libcxx" ||
      File->Path != (Sample ? "__algorithm/sample.h" : "__algorithm/shuffle.h") ||
      F->getTemplateSpecializationKind() != TSK_ImplicitInstantiation ||
      !approvedStandardSDKDeclaration(S, SM, Definition))
    return std::nullopt;
  const auto *Reference =
      dyn_cast<DeclRefExpr>(Call->getCallee()->IgnoreParenImpCasts());
  if (!Reference || Reference->getDecl() != F || !Reference->getQualifier())
    return std::nullopt;
  const auto Input = F->getParamDecl(0)->getType();
  const auto Output = F->getParamDecl(Sample ? 2 : 0)->getType();
  const auto Object = F->getParamDecl(Index)->getType();
  const auto *Generator =
      Object.getNonReferenceType()->getAsCXXRecordDecl();
  Generator = Generator ? Generator->getDefinition() : nullptr;
  if (!Input->isPointerType() || !Output->isPointerType() ||
      !scalarElement(Input->getPointeeType(), Context) ||
      !scalarElement(Output->getPointeeType(), Context) ||
      Output->getPointeeType().isConstQualified() ||
      !Context.hasSameType(F->getParamDecl(1)->getType(), Input) ||
      !Object->isReferenceType() ||
      Object.getNonReferenceType().isVolatileQualified() || !Generator ||
      Generator->isLambda() || Generator->isUnion() ||
      !Generator->isStandardLayout() || !Generator->isTriviallyCopyable() ||
      !Generator->hasTrivialDestructor() ||
      !S.owns(SM, Generator->getLocation()) ||
      !Context.hasSameType(Call->getType(), F->getReturnType()) ||
      (Sample ? !Context.hasSameType(F->getReturnType(), Output)
              : !F->getReturnType()->isVoidType()))
    return std::nullopt;
  for (unsigned I = 0; I != Parameters; ++I)
    if (!Context.hasSameType(Call->getArg(I)->getType(),
                             F->getParamDecl(I)->getType().getNonReferenceType()))
      return std::nullopt;
  auto Distance = Context.getPointerDiffType();
  if (Sample) {
    const auto Count = F->getParamDecl(3)->getType();
    if (Count.hasLocalQualifiers() || !Count->isIntegerType() ||
        Count->isEnumeralType() || Context.getTypeSize(Count) > 64 ||
        !utilityScalarComparisonType(Context, Input->getPointeeType(),
                                     Output->getPointeeType()))
      return std::nullopt;
    const auto Common = utilityScalarComparisonType(Context, Count, Distance);
    if (!Common)
      return std::nullopt;
    Distance = *Common;
  }
  const TypedefNameDecl *ResultAlias = nullptr;
  for (const auto *D : Generator->decls())
    if (const auto *Alias = dyn_cast<TypedefNameDecl>(D);
        Alias && Alias->getName() == "result_type")
      ResultAlias = Alias;
  const auto Result = ResultAlias ? ResultAlias->getUnderlyingType() : QualType{};
  if (Result.isNull() || Result.hasLocalQualifiers() ||
      !Result->isUnsignedIntegerType() || Result->isBooleanType() ||
      Context.getTypeSize(Result) > 64 ||
      !S.owns(SM, ResultAlias->getLocation()))
    return std::nullopt;
  RandomSelection Selection(S, SM, Context, Generator, Result, Distance);
  if (!Selection.retain(F) || !Selection.Method || !Selection.Minimum ||
      !Selection.Maximum || Selection.MinimumValue >= Selection.MaximumValue)
    return std::nullopt;
  return UtilityAlgorithmRandomCall{
      Sample ? UtilityOperation::AlgorithmSample
             : UtilityOperation::AlgorithmShuffle,
      Selection.Method, Selection.Minimum, Selection.Maximum,
      Object, Result, Distance, Selection.MinimumValue, Selection.MaximumValue,
      Index};
}
} // namespace nct
