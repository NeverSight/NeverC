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

bool randomWeightScalar(QualType T, const ASTContext &Context) {
  return (T->isIntegerType() && Context.getTypeSize(T) <= 64) ||
         T->isSpecificBuiltinType(BuiltinType::Float) ||
         T->isSpecificBuiltinType(BuiltinType::Double);
}
bool randomWeightFunction(QualType T, const ASTContext &Context) {
  const auto *P = T->isFunctionPointerType()
      ? T->getPointeeType()->getAs<FunctionProtoType>() : nullptr;
  return P && !P->isVariadic() && P->getCallConv() == CC_C && P->getNumParams() == 1 &&
         randomWeightScalar(P->getReturnType().getNonReferenceType(), Context) &&
         randomWeightScalar(P->getParamType(0).getNonReferenceType(), Context);
}

enum class SelectedRandomMath { None, LinearSquareRoot, ExponentialLog };

// The lowering implements the pinned libc++ loops. Follow the declarations
// actually selected by those loops, including instantiated traits and helper
// bodies. Header identity alone cannot authenticate a source specialization.
class RandomSelection : public RecursiveASTVisitor<RandomSelection> {
  const State &S;
  const SourceManager &SM;
  const ASTContext &Context;
  const CXXRecordDecl *Generator;
  bool SDKGenerator;
  bool WeightCallback;
  SelectedRandomMath Math;
  const CXXRecordDecl *WeightRecord;
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
  bool engineIntrinsic(const FunctionDecl *F, SourceLocation Location) const {
    if (!F || !F->isImplicit())
      return false;
    if (!SDKGenerator) {
      // A source generator can still be called by a pinned vector algorithm.
      // Its iterator alignment/constant-evaluation helpers are SDK proof
      // dependencies; the source generator body closes separately.
      const auto File = S.sdkFile(SM, Location);
      if (!File || File->Root != "libcxx" ||
          (F->getBuiltinID() != Builtin::BI__builtin_is_constant_evaluated &&
           F->getBuiltinID() != Builtin::BI__builtin_assume_aligned))
        return false;
    }
    // Equality and seed-sequence storage use these selected SDK helpers.
    // These compiler intrinsics are proof dependencies, never emitted calls.
    return F->getBuiltinID() == Builtin::BI__builtin_is_constant_evaluated ||
           F->getBuiltinID() == Builtin::BI__builtin_constant_p ||
           F->getBuiltinID() == Builtin::BI__builtin_memcmp ||
           F->getBuiltinID() == Builtin::BI__builtin_memcpy ||
           F->getBuiltinID() == Builtin::BI__builtin_memmove ||
           F->getBuiltinID() == Builtin::BI__builtin_addressof ||
           F->getBuiltinID() == Builtin::BI__builtin_assume_aligned ||
           F->getBuiltinID() == Builtin::BI__builtin_operator_new ||
           F->getBuiltinID() == Builtin::BI__builtin_operator_delete;
  }
  bool selectedUnaryMath(const FunctionDecl *F, SourceLocation Location) const {
    if (Math == SelectedRandomMath::None || !F || F->isVariadic() || F->getNumParams() != 1 ||
        (!DistanceType->isSpecificBuiltinType(BuiltinType::Float) &&
         !DistanceType->isSpecificBuiltinType(BuiltinType::Double)) ||
        !Context.hasSameType(F->getReturnType(), DistanceType) ||
        !Context.hasSameType(F->getParamDecl(0)->getType(), DistanceType))
      return false;
    const bool Float = DistanceType->isSpecificBuiltinType(BuiltinType::Float);
    const bool Root = Math == SelectedRandomMath::LinearSquareRoot;
    const auto ID = F->getBuiltinID();
    if (Root ? (Float ? ID != Builtin::BIsqrtf && ID != Builtin::BI__builtin_sqrtf
                      : ID != Builtin::BIsqrt && ID != Builtin::BI__builtin_sqrt)
             : (Float ? ID != Builtin::BIlogf && ID != Builtin::BI__builtin_logf
                      : ID != Builtin::BIlog && ID != Builtin::BI__builtin_log))
      return false;
    const llvm::StringRef Helper = Root ? "__math/roots.h" : "__math/logarithms.h";
    const llvm::StringRef Distribution = Root ? "__random/piecewise_linear_distribution.h"
                                              : "__random/exponential_distribution.h";
    const auto CallFile = S.sdkFile(SM, Location);
    if (!CallFile || CallFile->Root != "libcxx" ||
        (CallFile->Path != Helper && CallFile->Path != Distribution))
      return false;
    for (const auto *D : F->redecls()) {
      if (D->isImplicit() && D->getLocation().isInvalid())
        continue;
      const auto File = S.sdkFile(SM, D->getLocation());
      if (!File || S.owns(SM, D->getLocation()) ||
          !((File->Root == "resource" && File->Path == "include/math.h") ||
            (File->Root == "libcxx" && File->Path == Helper)))
        return false;
    }
    return true;
  }
  bool selectedLimitInfinity(const FunctionDecl *F, SourceLocation Location) const {
    if (!SDKGenerator || !F || !F->isImplicit() || F->isVariadic() || F->getNumParams() ||
        !Context.hasSameType(F->getReturnType(), DistanceType))
      return false;
    const bool Float = DistanceType->isSpecificBuiltinType(BuiltinType::Float);
    if ((!Float && !DistanceType->isSpecificBuiltinType(BuiltinType::Double)) ||
        F->getBuiltinID() != (Float ? Builtin::BI__builtin_huge_valf : Builtin::BI__builtin_huge_val))
      return false;
    const auto CallFile = S.sdkFile(SM, Location);
    if (!CallFile || CallFile->Root != "libcxx" || CallFile->Path != "limits")
      return false;
    for (const auto *D : F->redecls()) {
      if (D->isImplicit() && D->getLocation().isInvalid()) continue;
      const auto File = S.sdkFile(SM, D->getLocation());
      if (!File || S.owns(SM, D->getLocation()) || File->Root != "libcxx" || File->Path != "limits")
        return false;
    }
    return true;
  }
  bool alignmentType(QualType T) const {
    const auto *E = T->getAs<EnumType>();
    const auto *D = E ? E->getDecl() : nullptr;
    const auto File = D ? S.sdkFile(SM, D->getLocation()) : std::nullopt;
    return D && D->getName() == "align_val_t" && File &&
           File->Root == "libcxx" && File->Path == "__new/align_val_t.h" &&
           approvedStandardSDKDeclaration(S, SM, D);
  }
  bool allocatorHook(const FunctionDecl *F) const {
    if (!SDKGenerator || !F || isa<CXXMethodDecl>(F) || F->isVariadic() ||
        F->getPrimaryTemplate() ||
        !F->getDeclContext()->getRedeclContext()->isTranslationUnit())
      return false;
    const bool Allocate = F->getOverloadedOperator() == OO_New;
    if ((!Allocate && F->getOverloadedOperator() != OO_Delete) ||
        F->getNumParams() < 1 || F->getNumParams() > (Allocate ? 2u : 3u) ||
        !Context.hasSameType(F->getReturnType(), Allocate ? Context.VoidPtrTy : Context.VoidTy) ||
        !Context.hasSameType(F->getParamDecl(0)->getType(),
                             Allocate ? Context.getSizeType() : Context.VoidPtrTy) ||
        (F->getNumParams() >= 2 &&
         !alignmentType(F->getParamDecl(1)->getType()) &&
         (Allocate || !Context.hasSameType(F->getParamDecl(1)->getType(), Context.getSizeType()))) ||
        (F->getNumParams() == 3 && !alignmentType(F->getParamDecl(2)->getType())))
      return false;
    for (const auto *D : F->redecls()) {
      if (S.owns(SM, D->getLocation()) ||
          (D->isImplicit() && D->getLocation().isInvalid()))
        continue;
      const auto File = S.sdkFile(SM, D->getLocation());
      if (!File || File->Root != "libcxx" || File->Path != "__new/global_new_delete.h")
        return false;
    }
    // The allocation boundary separately selects and checks the source-owned
    // global new/delete implementation. Do not traverse that user's body as SDK.
    return true;
  }
  bool weightParameter(const ParmVarDecl *Parameter) const {
    const auto *C = Parameter ? dyn_cast<CXXConstructorDecl>(Parameter->getDeclContext()) : nullptr;
    const auto *Owner = C ? C->getParent() : nullptr;
    if (Owner && Owner->getName() == "param_type")
      Owner = dyn_cast<CXXRecordDecl>(Owner->getDeclContext());
    if ((!WeightCallback && !WeightRecord) || !C || !Owner ||
        Owner->getCanonicalDecl() != Generator->getCanonicalDecl())
      return false;
    const bool Linear = Generator->getName() == "piecewise_linear_distribution";
    const bool Piecewise = Linear || Generator->getName() == "piecewise_constant_distribution";
    if (!Piecewise && Generator->getName() != "discrete_distribution")
      return false;
    const auto File = S.sdkFile(SM, C->getLocation());
    if (!File || File->Root != "libcxx" ||
        File->Path != (Linear ? "__random/piecewise_linear_distribution.h"
                     : Piecewise ? "__random/piecewise_constant_distribution.h"
                                 : "__random/discrete_distribution.h") ||
        !approvedStandardSDKDeclaration(S, SM, C))
      return false;
    if (C->getNumParams() == 4)
      return Parameter == C->getParamDecl(3) &&
          Context.hasSameType(C->getParamDecl(0)->getType(), Context.getSizeType()) &&
          Context.hasSameType(C->getParamDecl(1)->getType(), DistanceType) &&
          Context.hasSameType(C->getParamDecl(2)->getType(), DistanceType);
    if (!Piecewise || C->getNumParams() != 2 || Parameter != C->getParamDecl(1))
      return false;
    const auto List = approvedUtilityInitializerListRecord(S, SM,
        C->getParamDecl(0)->getType()->getAsCXXRecordDecl(), Context);
    return List && Context.hasSameType(List->ElementType, DistanceType);
  }
  bool sourceWeightCopy(const CXXConstructorDecl *C) const {
    if (!WeightRecord || !C || C->getParent()->getCanonicalDecl() != WeightRecord->getCanonicalDecl() ||
        C->isDeleted() || !C->isCopyConstructor() ||
        (!(C->isImplicit() && C->isTrivial()) && (!supportedConstructor(C) || !C->hasBody())) ||
        !C->getNumParams() || !C->getParamDecl(0)->getType()->isReferenceType() ||
        !Context.hasSameUnqualifiedType(C->getParamDecl(0)->getType().getNonReferenceType(),
                                        Context.getRecordType(WeightRecord)))
      return false;
    for (const auto *D : C->redecls())
      if (!S.owns(SM, D->getLocation())) return false;
    return true;
  }
  bool sourceWeightMethod(const CXXMethodDecl *M, const CallExpr *Call) {
    const auto *Invocation = dyn_cast<CXXOperatorCallExpr>(Call);
    const auto *Definition = M ? M->getDefinition() : nullptr;
    if (!WeightRecord || !M || !Definition || !Invocation || Invocation->getNumArgs() != 2 ||
        M->getParent()->getCanonicalDecl() != WeightRecord->getCanonicalDecl() ||
        M->isStatic() || M->isVolatile() || M->isVariadic() || M->getNumParams() != 1 ||
        M->getOverloadedOperator() != OO_Call || M->getRefQualifier() == RQ_RValue ||
        (M->getTemplatedKind() != FunctionDecl::TK_NonTemplate &&
         M->getTemplatedKind() != FunctionDecl::TK_MemberSpecialization) ||
        M->getPrimaryTemplate() || M->getDescribedFunctionTemplate() ||
        !ordinaryOperator(M) || !callableMethod(M) ||
        !randomWeightScalar(M->getReturnType().getNonReferenceType(), Context) ||
        !randomWeightScalar(M->getParamDecl(0)->getType().getNonReferenceType(), Context) ||
        !S.owns(SM, Definition->getLocation()))
      return false;
    const auto *Object = dyn_cast<DeclRefExpr>(Invocation->getArg(0)->IgnoreParenImpCasts());
    const auto *Parameter = Object ? dyn_cast<ParmVarDecl>(Object->getDecl()) : nullptr;
    if (!weightParameter(Parameter) ||
        !Context.hasSameUnqualifiedType(Parameter->getType(), Context.getRecordType(WeightRecord)) ||
        !Invocation->getArg(0)->isLValue() ||
        !Context.hasSameUnqualifiedType(Invocation->getArg(1)->getType(), M->getParamDecl(0)->getType().getNonReferenceType()))
      return false;
    for (const auto *D : M->redecls())
      if (!S.owns(SM, D->getLocation())) return false;
    if (WeightMethod && WeightMethod->getCanonicalDecl() != M->getCanonicalDecl())
      return false;
    WeightMethod = M;
    return true;
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
  const CXXMethodDecl *WeightMethod = nullptr;
  const CXXConstructExpr *WeightCopy = nullptr;
  uint64_t MinimumValue = 0, MaximumValue = 0;

  RandomSelection(const State &S, const SourceManager &SM,
                  const ASTContext &Context, const CXXRecordDecl *Generator,
                  QualType ResultType, QualType DistanceType, bool WeightCallback = false,
                  const CXXRecordDecl *WeightRecord = nullptr, SelectedRandomMath Math = SelectedRandomMath::None)
      : S(S), SM(SM), Context(Context), Generator(Generator),
        SDKGenerator(approvedStandardSDKDeclaration(S, SM, Generator)),
        WeightCallback(WeightCallback), Math(Math), WeightRecord(WeightRecord),
        ResultType(ResultType), DistanceType(DistanceType) {}

  bool retainType(QualType T) {
    if (T.isNull() || T->isDependentType() ||
        !Types.insert(T.getTypePtr()).second)
      return true;
    if (++Nodes > 4096)
      return false;
    if (const auto *Alias = dyn_cast<TypedefType>(T.getTypePtr())) {
      const auto *D = Alias->getDecl();
      if (WeightRecord && D->getDeclContext() == WeightRecord) {
        if (!S.owns(SM, D->getLocation())) return false;
      } else if (D->getDeclContext() == Generator) {
        if (SDKGenerator ? !origin(D) : !S.owns(SM, D->getLocation()))
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
          (!WeightRecord || R->getCanonicalDecl() != WeightRecord->getCanonicalDecl()) &&
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
    if (WeightRecord) {
      if (const auto *R = dyn_cast<CXXRecordDecl>(D);
          R && R->getCanonicalDecl() == WeightRecord->getCanonicalDecl())
        return S.owns(SM, R->getLocation());
      if (const auto *C = dyn_cast<CXXConstructorDecl>(D); sourceWeightCopy(C))
        return true;
    }
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
    if (countLeadingZeros(F) || engineIntrinsic(F, E->getExprLoc()) ||
        selectedUnaryMath(F, E->getExprLoc()) || selectedLimitInfinity(F, E->getExprLoc()) || allocatorHook(F))
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
             (M->getParent()->getCanonicalDecl() == Generator->getCanonicalDecl() ||
              (WeightRecord && M->getParent()->getCanonicalDecl() == WeightRecord->getCanonicalDecl()));
    return qualifier(E->getQualifier()) && retain(E->getMemberDecl());
  }
  bool VisitCallExpr(CallExpr *E) {
    const auto *F = E->getDirectCallee();
    if (!F) {
      const auto *Reference = dyn_cast<DeclRefExpr>(E->getCallee()->IgnoreParenImpCasts());
      const auto *Parameter = Reference ? dyn_cast<ParmVarDecl>(Reference->getDecl()) : nullptr;
      if (WeightCallback && weightParameter(Parameter) &&
          E->getNumArgs() == 1 && randomWeightFunction(Parameter->getType(), Context)) {
        const auto *Prototype = Parameter->getType()->getPointeeType()->getAs<FunctionProtoType>();
        return Context.hasSameUnqualifiedType(E->getType(), Prototype->getReturnType().getNonReferenceType()) &&
               Context.hasSameUnqualifiedType(E->getArg(0)->getType(), Prototype->getParamType(0).getNonReferenceType());
      }
      const auto *Destructor = dyn_cast<CXXPseudoDestructorExpr>(
          E->getCallee()->IgnoreParenImpCasts());
      const auto File = S.sdkFile(SM, E->getExprLoc());
      if (SDKGenerator && Destructor && File && File->Root == "libcxx" &&
          File->Path == "__memory/allocator.h" && !E->getNumArgs() &&
          E->getType()->isVoidType() && Destructor->isArrow() &&
          Destructor->getBase()->getType()->isPointerType() &&
          ((Destructor->getDestroyedType()->isUnsignedIntegerType() &&
            Context.getTypeSize(Destructor->getDestroyedType()) == 32) ||
           Context.hasSameType(Destructor->getDestroyedType(), Context.DoubleTy) ||
           Context.hasSameType(Destructor->getDestroyedType(), Context.FloatTy)) &&
          Context.hasSameUnqualifiedType(Destructor->getDestroyedType(),
              Destructor->getBase()->getType()->getPointeeType()))
        return true;
      return false;
    }
    if (selectedUnaryMath(F, E->getExprLoc()))
      return E->getNumArgs() == 1 && Context.hasSameType(E->getType(), DistanceType) &&
             Context.hasSameType(E->getArg(0)->getType(), DistanceType);
    if (selectedLimitInfinity(F, E->getExprLoc()))
      return !E->getNumArgs() && Context.hasSameType(E->getType(), DistanceType);
    if (allocatorHook(F))
      return E->getNumArgs() == F->getNumParams();
    if (countLeadingZeros(F))
      return E->getType()->isSpecificBuiltinType(BuiltinType::Int) &&
             E->getNumArgs() >= 1 && E->getNumArgs() <= 2;
    if (engineIntrinsic(F, E->getExprLoc())) {
      switch (F->getBuiltinID()) {
      case Builtin::BI__builtin_is_constant_evaluated:
        return E->getNumArgs() == 0 && E->getType()->isBooleanType();
      case Builtin::BI__builtin_constant_p:
        return E->getNumArgs() == 1 &&
               E->getType()->isSpecificBuiltinType(BuiltinType::Int);
      case Builtin::BI__builtin_memcmp:
        return E->getNumArgs() == 3 &&
               E->getType()->isSpecificBuiltinType(BuiltinType::Int);
      case Builtin::BI__builtin_memcpy:
      case Builtin::BI__builtin_memmove:
        return E->getNumArgs() == 3 &&
               Context.hasSameType(E->getType(), Context.VoidPtrTy) &&
               Context.hasSameType(E->getArg(0)->getType(), Context.VoidPtrTy) &&
               E->getArg(1)->getType()->isObjectPointerType() &&
               Context.hasSameType(E->getArg(2)->getType(), Context.getSizeType());
      case Builtin::BI__builtin_addressof:
        return E->getNumArgs() == 1 && E->getType()->isPointerType() &&
               Context.hasSameType(E->getType()->getPointeeType(), E->getArg(0)->getType());
      case Builtin::BI__builtin_assume_aligned:
        return E->getNumArgs() >= 2 && E->getNumArgs() <= 3 &&
               E->getType()->isPointerType() &&
               E->getArg(0)->getType()->isPointerType() &&
               E->getArg(1)->getType()->isIntegerType() &&
               (E->getNumArgs() == 2 || E->getArg(2)->getType()->isIntegerType());
      case Builtin::BI__builtin_operator_new:
        return E->getNumArgs() >= 1 && E->getNumArgs() <= 2 &&
               Context.hasSameType(E->getType(), Context.VoidPtrTy) &&
               Context.hasSameType(E->getArg(0)->getType(), Context.getSizeType()) &&
               (E->getNumArgs() == 1 || alignmentType(E->getArg(1)->getType()));
      case Builtin::BI__builtin_operator_delete:
        return E->getNumArgs() >= 1 && E->getNumArgs() <= 3 &&
               E->getType()->isVoidType() &&
               E->getArg(0)->getType()->isObjectPointerType() &&
               (E->getNumArgs() == 1 ||
                Context.hasSameType(E->getArg(1)->getType(), Context.getSizeType()) ||
                alignmentType(E->getArg(1)->getType())) &&
               (E->getNumArgs() != 3 || alignmentType(E->getArg(2)->getType()));
      default:
        return false;
      }
    }
    if (S.owns(SM, F->getLocation())) {
      const auto *M = dyn_cast<CXXMethodDecl>(F);
      return M && (sourceWeightMethod(M, E) || sourceMethod(M, E));
    }
    if (SDKGenerator)
      if (const auto *M = dyn_cast<CXXMethodDecl>(F);
          M && M->getParent()->getCanonicalDecl() ==
                   Generator->getCanonicalDecl() &&
          !M->getNumParams() &&
          Context.hasSameType(M->getReturnType(), ResultType)) {
        if (!retain(M))
          return false;
        if (M->getOverloadedOperator() == OO_Call) {
          if (Method && Method->getCanonicalDecl() != M->getCanonicalDecl())
            return false;
          Method = M;
        } else if (M->isStatic() && M->isConstexpr() && M->getIdentifier() &&
                   (M->getName() == "min" || M->getName() == "max")) {
          const auto *Body = dyn_cast_or_null<CompoundStmt>(M->getBody());
          const auto *Return = Body && Body->size() == 1
                                   ? dyn_cast<ReturnStmt>(*Body->body_begin())
                                   : nullptr;
          const auto Value = Return && Return->getRetValue()
                                 ? Return->getRetValue()->getIntegerConstantExpr(Context)
                                 : std::nullopt;
          if (!Value || Value->getBitWidth() > 64)
            return false;
          (M->getName() == "min" ? Minimum : Maximum) = M;
          (M->getName() == "min" ? MinimumValue : MaximumValue) =
              Value->getZExtValue();
        }
        return true;
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
    if (WeightRecord && E->getConstructor()->getParent()->getCanonicalDecl() == WeightRecord->getCanonicalDecl()) {
      const auto *Reference = E->getNumArgs() >= 1
          ? dyn_cast<DeclRefExpr>(E->getArg(0)->IgnoreParenImpCasts()) : nullptr;
      const auto *Parameter = Reference ? dyn_cast<ParmVarDecl>(Reference->getDecl()) : nullptr;
      if (!sourceWeightCopy(E->getConstructor()) || E->getNumArgs() != E->getConstructor()->getNumParams() ||
          !weightParameter(Parameter) ||
          !Context.hasSameUnqualifiedType(Parameter->getType(), Context.getRecordType(WeightRecord)) ||
          !E->getArg(0)->isLValue() || (WeightCopy && WeightCopy != E))
        return false;
      for (unsigned I = 1; I < E->getNumArgs(); ++I)
        if (!isa<CXXDefaultArgExpr>(E->getArg(I))) return false;
      WeightCopy = E;
    }
    return retainType(E->getType()) && retain(E->getConstructor());
  }
  bool VisitCXXBindTemporaryExpr(CXXBindTemporaryExpr *E) {
    const auto *D = E->getTemporary()->getDestructor();
    if (!WeightRecord || !D || D->getParent()->getCanonicalDecl() != WeightRecord->getCanonicalDecl())
      return true;
    if (D->isDeleted() || (!D->isTrivial() && !ordinaryDestructor(D) && !defaultedLifecycle(D)) ||
        (!D->isTrivial() && !D->hasBody()))
      return false;
    for (const auto *Declaration : D->redecls())
      if (!S.owns(SM, Declaration->getLocation())) return false;
    return true;
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

std::optional<RandomEngineRecord>
approvedRandomEngineRecord(const State &S, const SourceManager &SM,
                           const CXXRecordDecl *Record,
                           const ASTContext &Context) {
  const auto *R = Record ? dyn_cast_or_null<ClassTemplateSpecializationDecl>(
                              Record->getDefinition()) : nullptr;
  const auto *Template = R ? R->getSpecializedTemplate() : nullptr;
  const auto File = R ? S.sdkFile(SM, R->getLocation()) : std::nullopt;
  if (!S.coreV2() || !R || !Template || !File || File->Root != "libcxx" ||
      !R->isStandardLayout() || !R->isTriviallyCopyable() ||
      !R->hasTrivialDestructor() || R->getNumBases() || R->isUnion() ||
      R->getSpecializationKind() != TSK_ImplicitInstantiation ||
      !approvedStandardSDKDeclaration(S, SM, Template))
    return std::nullopt;
  RandomEngineKind Kind;
  unsigned ArgumentCount, TypeIndex = 0;
  if (Template->getName() == "linear_congruential_engine" &&
      File->Path == "__random/linear_congruential_engine.h") {
    Kind = RandomEngineKind::LinearCongruential;
    ArgumentCount = 4;
  } else if (Template->getName() == "mersenne_twister_engine" &&
             File->Path == "__random/mersenne_twister_engine.h") {
    Kind = RandomEngineKind::MersenneTwister;
    ArgumentCount = 14;
  } else if (Template->getName() == "subtract_with_carry_engine" &&
             File->Path == "__random/subtract_with_carry_engine.h") {
    Kind = RandomEngineKind::SubtractWithCarry;
    ArgumentCount = 4;
  } else if (Template->getName() == "discard_block_engine" &&
             File->Path == "__random/discard_block_engine.h") {
    Kind = RandomEngineKind::DiscardBlock;
    ArgumentCount = 3;
  } else if (Template->getName() == "independent_bits_engine" &&
             File->Path == "__random/independent_bits_engine.h") {
    Kind = RandomEngineKind::IndependentBits;
    ArgumentCount = 3;
    TypeIndex = 2;
  } else if (Template->getName() == "shuffle_order_engine" &&
             File->Path == "__random/shuffle_order_engine.h") {
    Kind = RandomEngineKind::ShuffleOrder;
    ArgumentCount = 2;
  } else {
    return std::nullopt;
  }
  const bool Adaptor = Kind == RandomEngineKind::DiscardBlock ||
                       Kind == RandomEngineKind::IndependentBits ||
                       Kind == RandomEngineKind::ShuffleOrder;
  const auto &Arguments = R->getTemplateArgs();
  if (Arguments.size() != ArgumentCount ||
      Arguments.get(0).getKind() != TemplateArgument::Type ||
      Arguments.get(TypeIndex).getKind() != TemplateArgument::Type)
    return std::nullopt;
  const auto Base = Adaptor ? approvedRandomEngineRecord(
      S, SM, Arguments.get(0).getAsType()->getAsCXXRecordDecl(), Context)
                           : std::nullopt;
  if (Adaptor && !Base)
    return std::nullopt;
  const auto Result = Adaptor && Kind != RandomEngineKind::IndependentBits
                          ? Base->ResultType : Arguments.get(TypeIndex).getAsType();
  if (Result.hasLocalQualifiers() || !Result->isUnsignedIntegerType() ||
      Result->isBooleanType() || Context.getTypeSize(Result) > 64)
    return std::nullopt;
  std::vector<uint64_t> Parameters;
  for (unsigned I = 1; I < Arguments.size(); ++I) {
    if (I == TypeIndex)
      continue;
    const auto &Argument = Arguments.get(I);
    if (Argument.getKind() != TemplateArgument::Integral ||
        Argument.getAsIntegral().isNegative() ||
        Argument.getAsIntegral().getBitWidth() > 64)
      return std::nullopt;
    Parameters.push_back(Argument.getAsIntegral().getZExtValue());
  }
  const unsigned Bits = Context.getTypeSize(Result);
  uint64_t Minimum = 0, Maximum = 0, DefaultSeed = 0, ArraySize = 0;
  unsigned ExpectedFields = 0;
  switch (Kind) {
  case RandomEngineKind::LinearCongruential: {
    const auto Modulus = Parameters[2];
    if (Modulus && (Parameters[0] >= Modulus || Parameters[1] >= Modulus))
      return std::nullopt;
    Minimum = Parameters[1] == 0 ? 1 : 0;
    Maximum = (llvm::APInt(Bits, Modulus) - 1).getZExtValue();
    DefaultSeed = 1;
    ExpectedFields = 1;
    break;
  }
  case RandomEngineKind::MersenneTwister: {
    const auto Width = Parameters[0], Size = Parameters[1];
    if (Width < 2 || Width > Bits || !Size || Size > 65536 ||
        Parameters[2] > Size || Parameters[3] > Width ||
        Parameters[5] > Width || Parameters[7] > Width ||
        Parameters[9] > Width || Parameters[11] > Width)
      return std::nullopt;
    Maximum = llvm::APInt::getLowBitsSet(Bits, Width).getZExtValue();
    DefaultSeed = 5489;
    ArraySize = Size;
    ExpectedFields = 2;
    break;
  }
  case RandomEngineKind::SubtractWithCarry:
    if (!Parameters[0] || Parameters[0] > Bits || !Parameters[1] ||
        Parameters[1] >= Parameters[2] || Parameters[2] > 65536)
      return std::nullopt;
    Maximum = llvm::APInt::getLowBitsSet(Bits, Parameters[0]).getZExtValue();
    DefaultSeed = llvm::APInt(Bits, 19780503).getZExtValue();
    ArraySize = Parameters[2];
    ExpectedFields = 3;
    break;
  case RandomEngineKind::DiscardBlock:
    if (!Parameters[1] || Parameters[1] > Parameters[0] ||
        Parameters[1] > uint64_t(llvm::APInt::getSignedMaxValue(
                                  Context.getTypeSize(Context.IntTy)).getZExtValue()))
      return std::nullopt;
    Minimum = Base->Minimum;
    Maximum = Base->Maximum;
    DefaultSeed = Base->DefaultSeed;
    ExpectedFields = 2;
    break;
  case RandomEngineKind::IndependentBits:
    if (!Parameters[0] || Parameters[0] > Bits)
      return std::nullopt;
    Maximum = llvm::APInt::getLowBitsSet(Bits, Parameters[0]).getZExtValue();
    DefaultSeed = Base->DefaultSeed;
    ExpectedFields = 1;
    break;
  case RandomEngineKind::ShuffleOrder:
    if (!Parameters[0] || Parameters[0] > 65536)
      return std::nullopt;
    Minimum = Base->Minimum;
    Maximum = Base->Maximum;
    DefaultSeed = Base->DefaultSeed;
    ArraySize = Parameters[0];
    ExpectedFields = 3;
    break;
  }
  if (Minimum >= Maximum)
    return std::nullopt;
  RandomEngineRecord Engine{Kind, R, Result, nullptr, nullptr,
                            std::move(Parameters), Minimum, Maximum, DefaultSeed};
  unsigned Fields = 0;
  for (const auto *F : R->fields()) {
    ++Fields;
    if (!approvedStandardSDKDeclaration(S, SM, F) || F->isBitField() ||
        F->isMutable())
      return std::nullopt;
    if (F->getName() == "__x_" ||
        (Kind == RandomEngineKind::ShuffleOrder && F->getName() == "__v_")) {
      const auto *Array = Context.getAsConstantArrayType(F->getType());
      if (ArraySize ? !Array || Array->getSize() != ArraySize ||
                         !Context.hasSameType(Array->getElementType(), Result)
                    : !Context.hasSameType(F->getType(), Result))
        return std::nullopt;
      Engine.State = F;
    } else if ((F->getName() == "__i_" &&
                (Kind == RandomEngineKind::MersenneTwister ||
                 Kind == RandomEngineKind::SubtractWithCarry) &&
                Context.hasSameType(F->getType(), Context.getSizeType())) ||
               (F->getName() == "__n_" && Kind == RandomEngineKind::DiscardBlock &&
                Context.hasSameType(F->getType(), Context.IntTy))) {
      Engine.Index = F;
    } else if (F->getName() == "__c_" && Kind == RandomEngineKind::SubtractWithCarry &&
               Context.hasSameType(F->getType(), Result)) {
      Engine.Carry = F;
    } else if (F->getName() == "__e_" && Adaptor &&
               Context.hasSameType(F->getType(), Context.getRecordType(Base->Record))) {
      Engine.Base = F;
    } else if (F->getName() == "__y_" && Kind == RandomEngineKind::ShuffleOrder &&
               Context.hasSameType(F->getType(), Result)) {
      Engine.Value = F;
    } else {
      return std::nullopt;
    }
  }
  RandomSelection Selection(S, SM, Context, R, Result, Result);
  if (Fields != ExpectedFields || (!Adaptor && !Engine.State) ||
      (Adaptor && !Engine.Base) || (ArraySize && !Engine.State) ||
      !Selection.retain(R))
    return std::nullopt;
  return Engine;
}

std::optional<SeedSequenceRecord>
approvedSeedSequenceRecord(const State &S, const SourceManager &SM,
                           const CXXRecordDecl *Record, const ASTContext &Context) {
  const auto *R = Record ? Record->getDefinition() : nullptr;
  const auto File = R ? S.sdkFile(SM, R->getLocation()) : std::nullopt;
  if (!S.coreV2() || !R || !File || File->Root != "libcxx" ||
      File->Path != "__random/seed_seq.h" || R->getName() != "seed_seq" ||
      !approvedStandardSDKDeclaration(S, SM, R) || R->getNumBases() ||
      R->isUnion() || !R->isStandardLayout())
    return std::nullopt;
  const FieldDecl *Storage = nullptr;
  QualType Result;
  for (const auto *F : R->fields()) {
    const auto Vector = approvedUtilityVectorRecord(
        S, SM, F->getType()->getAsCXXRecordDecl(), Context);
    if (Storage || !approvedStandardSDKDeclaration(S, SM, F) ||
        F->getName() != "__v_" || F->isBitField() || F->isMutable() || !Vector ||
        !Vector->ElementType->isUnsignedIntegerType() ||
        Context.getTypeSize(Vector->ElementType) != 32)
      return std::nullopt;
    Storage = F;
    Result = Vector->ElementType;
  }
  RandomSelection Selection(S, SM, Context, R, Result, Result);
  if (!Storage || !Selection.retain(R))
    return std::nullopt;
  return SeedSequenceRecord{R, Storage, Result};
}

namespace {
QualType seedSequencePointer(const State &S, const SourceManager &SM,
                             QualType Iterator, const ASTContext &Context) {
  if (const auto Wrapped = approvedUtilityWrapIteratorRecord(
          S, SM, Iterator->getAsCXXRecordDecl(), Context))
    return Wrapped->IteratorType;
  return Iterator->isPointerType() ? Iterator : QualType();
}
bool supportedSeedSequence(const State &S, const SourceManager &SM,
                           const RandomEngineRecord &Engine, QualType Parameter,
                           const ASTContext &Context) {
  if (!Parameter->isLValueReferenceType() ||
      Parameter.getNonReferenceType().hasLocalQualifiers() ||
      !approvedSeedSequenceRecord(S, SM,
          Parameter.getNonReferenceType()->getAsCXXRecordDecl(), Context))
    return false;
  if (Engine.Base) {
    const auto Base = approvedRandomEngineRecord(
        S, SM, Engine.Base->getType()->getAsCXXRecordDecl(), Context);
    return Base && supportedSeedSequence(S, SM, *Base, Parameter, Context);
  }
  // This pinned libc++ release takes a remainder by __m in its seed-sequence
  // LCG body. With a zero modulus that selected implementation is undefined.
  return Engine.Kind != RandomEngineKind::LinearCongruential || Engine.Parameters[2];
}
}

std::optional<SeedSequenceConstruction>
approvedSeedSequenceConstruction(const State &S, const SourceManager &SM,
                                 const CXXConstructExpr *Construction,
                                 const ASTContext &Context) {
  const auto *C = Construction ? Construction->getConstructor() : nullptr;
  const auto Seq = C ? approvedSeedSequenceRecord(S, SM, C->getParent(), Context)
                     : std::nullopt;
  if (!Seq || C->isVariadic() || !C->hasBody() || C->isCopyOrMoveConstructor() ||
      !approvedStandardSDKDeclaration(S, SM, C) ||
      Construction->getNumArgs() != C->getNumParams())
    return std::nullopt;
  auto Kind = SeedSequenceConstruction::Default;
  if (C->getNumParams() == 1) {
    const auto List = approvedUtilityInitializerListRecord(S, SM,
        C->getParamDecl(0)->getType()->getAsCXXRecordDecl(), Context);
    const auto *Argument = dyn_cast<CXXStdInitializerListExpr>(Construction->getArg(0));
    if (!List || !List->ElementType->isIntegerType() ||
        Context.getTypeSize(List->ElementType) > 64 ||
        (Argument && !approvedUtilityInitializerListExpression(S, SM, Argument, Context)))
      return std::nullopt;
    Kind = SeedSequenceConstruction::InitializerList;
  } else if (C->getNumParams() == 2) {
    const auto Pointer = seedSequencePointer(S, SM, C->getParamDecl(0)->getType(), Context);
    if (Pointer.isNull() || !Pointer->getPointeeType()->isIntegerType() ||
        Pointer->getPointeeType().isVolatileQualified() ||
        Context.getTypeSize(Pointer->getPointeeType()) > 64 ||
        !Context.hasSameType(C->getParamDecl(0)->getType(), C->getParamDecl(1)->getType()))
      return std::nullopt;
    Kind = SeedSequenceConstruction::Range;
  } else if (C->getNumParams()) {
    return std::nullopt;
  }
  RandomSelection Selection(S, SM, Context, Seq->Record, Seq->ResultType, Seq->ResultType);
  return Selection.retain(C) ? std::optional<SeedSequenceConstruction>(Kind)
                            : std::nullopt;
}

std::optional<UtilityOperation>
approvedSeedSequenceOperation(const State &S, const SourceManager &SM,
                              const CallExpr *Call, const ASTContext &Context) {
  const auto *M = Call ? dyn_cast_or_null<CXXMethodDecl>(Call->getDirectCallee()) : nullptr;
  const auto Seq = M ? approvedSeedSequenceRecord(S, SM, M->getParent(), Context)
                     : std::nullopt;
  if (!Seq || !isa<CXXMemberCallExpr>(Call) || M->isStatic() || M->isVolatile() ||
      M->isVariadic() || !M->getIdentifier() ||
      !approvedStandardSDKDeclaration(S, SM, M) ||
      Call->getNumArgs() != M->getNumParams())
    return std::nullopt;
  std::optional<UtilityOperation> Operation;
  if (M->getName() == "size" && M->isConst() && !M->getNumParams() &&
      Context.hasSameType(M->getReturnType(), Context.getSizeType())) {
    Operation = UtilityOperation::SeedSequenceSize;
  } else if (M->getName() == "param" && M->isConst() && M->getNumParams() == 1 &&
             M->getReturnType()->isVoidType()) {
    const auto Pointer = seedSequencePointer(S, SM, M->getParamDecl(0)->getType(), Context);
    if (!Pointer.isNull() && !Pointer->getPointeeType().hasLocalQualifiers() &&
        scalarElement(Pointer->getPointeeType(), Context))
      Operation = UtilityOperation::SeedSequenceParam;
  } else if (M->getName() == "generate" && !M->isConst() && M->getNumParams() == 2 &&
             M->getReturnType()->isVoidType() &&
             Context.hasSameType(M->getParamDecl(0)->getType(), M->getParamDecl(1)->getType())) {
    const auto Pointer = seedSequencePointer(S, SM, M->getParamDecl(0)->getType(), Context);
    if (!Pointer.isNull() && !Pointer->getPointeeType().hasLocalQualifiers() &&
        Pointer->getPointeeType()->isUnsignedIntegerType() &&
        Context.getTypeSize(Pointer->getPointeeType()) >= 32 &&
        Context.getTypeSize(Pointer->getPointeeType()) <= 64)
      Operation = UtilityOperation::SeedSequenceGenerate;
  }
  RandomSelection Selection(S, SM, Context, Seq->Record, Seq->ResultType, Seq->ResultType);
  return Operation && Selection.retain(M) ? Operation : std::nullopt;
}

std::optional<RandomEngineConstruction>
approvedRandomEngineConstruction(const State &S, const SourceManager &SM,
                                 const CXXConstructExpr *Construction,
                                 const ASTContext &Context) {
  const auto *C = Construction ? Construction->getConstructor() : nullptr;
  const auto Engine = C ? approvedRandomEngineRecord(S, SM, C->getParent(), Context)
                        : std::nullopt;
  if (!Engine || C->isVariadic() ||
      !approvedStandardSDKDeclaration(S, SM, C) ||
      Construction->getNumArgs() != C->getNumParams())
    return std::nullopt;
  if (C->isCopyOrMoveConstructor()) {
    const auto Parameter = C->getNumParams() == 1
                               ? C->getParamDecl(0)->getType() : QualType();
    if (C->isImplicit() && C->isTrivial() && !Parameter.isNull() &&
        Parameter->isReferenceType() &&
        Context.hasSameUnqualifiedType(Parameter.getNonReferenceType(),
                                       Context.getRecordType(Engine->Record)))
      return RandomEngineConstruction::CopyOrMove;
    return std::nullopt;
  }
  if (!C->hasBody() || C->getNumParams() > 1)
    return std::nullopt;
  auto Kind = C->getNumParams() ? RandomEngineConstruction::Seed
                                : RandomEngineConstruction::Default;
  if (C->getNumParams()) {
    const auto Parameter = C->getParamDecl(0)->getType();
    if (Engine->Base && Parameter->isReferenceType() &&
        Context.hasSameUnqualifiedType(Parameter.getNonReferenceType(),
                                       Engine->Base->getType()))
      Kind = RandomEngineConstruction::FromBase;
    else if (supportedSeedSequence(S, SM, *Engine, Parameter, Context))
      Kind = RandomEngineConstruction::SeedSequence;
    else if (!Context.hasSameType(Parameter, Engine->ResultType))
      return std::nullopt;
  }
  RandomSelection Selection(S, SM, Context, Engine->Record, Engine->ResultType,
                            Engine->ResultType);
  if (!Selection.retain(C))
    return std::nullopt;
  return Kind;
}

bool approvedRandomEngineDefaultArgument(const State &S, const SourceManager &SM,
                                        const CXXDefaultArgExpr *Default,
                                        const FunctionDecl *Function,
                                        unsigned Index,
                                        const ASTContext &Context) {
  const auto *M = dyn_cast_or_null<CXXMethodDecl>(Function);
  const auto Engine = M ? approvedRandomEngineRecord(S, SM, M->getParent(), Context)
                        : std::nullopt;
  if (!Engine || !Default || Index != 0 || M->getNumParams() != 1 ||
      !M->getIdentifier() || M->getName() != "seed" ||
      Default->getParam() != M->getParamDecl(0) ||
      !Context.hasSameType(M->getParamDecl(0)->getType(), Engine->ResultType))
    return false;
  const auto Value = Default->getExpr()->getIntegerConstantExpr(Context);
  RandomSelection Selection(S, SM, Context, Engine->Record, Engine->ResultType,
                            Engine->ResultType);
  return Value && Value->getZExtValue() == Engine->DefaultSeed &&
         Selection.retain(M) &&
         Selection.TraverseStmt(const_cast<Expr *>(Default->getExpr()));
}

std::optional<UtilityOperation>
approvedRandomEngineOperation(const State &S, const SourceManager &SM,
                              const CallExpr *Call,
                              const ASTContext &Context) {
  const auto *F = Call ? Call->getDirectCallee() : nullptr;
  const auto *M = dyn_cast_or_null<CXXMethodDecl>(F);
  const auto *Operator = dyn_cast_or_null<CXXOperatorCallExpr>(Call);
  if (!S.coreV2() || !F || F->isVariadic() ||
      !approvedStandardSDKDeclaration(S, SM, F) ||
      Call->isTypeDependent() || Call->isValueDependent() ||
      Call->isInstantiationDependent())
    return std::nullopt;
  const auto *R = M ? M->getParent()
                    : F->getNumParams() == 2
                          ? F->getParamDecl(0)->getType().getNonReferenceType()
                                ->getAsCXXRecordDecl() : nullptr;
  const auto Engine = approvedRandomEngineRecord(S, SM, R, Context);
  if (!Engine)
    return std::nullopt;
  auto Operation = std::optional<UtilityOperation>();
  if (M) {
    const bool ScalarResult = Context.hasSameType(M->getReturnType(),
                                                Engine->ResultType);
    if (!M->getNumParams() && ScalarResult && M->isStatic() && M->isConstexpr() &&
        M->getIdentifier() && (M->getName() == "min" || M->getName() == "max"))
      Operation = M->getName() == "min" ? UtilityOperation::RandomEngineMinimum
                                       : UtilityOperation::RandomEngineMaximum;
    else if (M->getOverloadedOperator() == OO_Call && !M->getNumParams() &&
             ScalarResult && !M->isStatic() && !M->isConst() && !M->isVolatile() &&
             (Operator ? Operator->getNumArgs() == 1
                       : isa<CXXMemberCallExpr>(Call)))
      Operation = UtilityOperation::RandomEngineGenerate;
    else if (M->getIdentifier() && !M->isStatic() && !M->isConst() &&
             !M->isVolatile() && M->getNumParams() <= 1 &&
             M->getReturnType()->isVoidType()) {
      if (M->getName() == "seed" &&
          ((!M->getNumParams() && Engine->Base) || (M->getNumParams() == 1 &&
          Context.hasSameType(M->getParamDecl(0)->getType(), Engine->ResultType))))
        Operation = UtilityOperation::RandomEngineSeed;
      else if (M->getName() == "seed" && M->getNumParams() == 1 &&
               supportedSeedSequence(S, SM, *Engine, M->getParamDecl(0)->getType(), Context))
        Operation = UtilityOperation::RandomEngineSeedSequence;
      else if (M->getName() == "discard" && M->getNumParams() == 1 &&
               Context.hasSameType(M->getParamDecl(0)->getType(),
                                   Context.UnsignedLongLongTy))
        Operation = UtilityOperation::RandomEngineDiscard;
    } else if (Engine->Base && M->getIdentifier() && M->getName() == "base" &&
               !M->isStatic() && M->isConst() && !M->isVolatile() &&
               !M->getNumParams() && M->getReturnType()->isLValueReferenceType() &&
               Context.hasSameType(M->getReturnType().getNonReferenceType(),
                                   Engine->Base->getType().withConst())) {
      Operation = UtilityOperation::RandomEngineBase;
    } else if (M->getOverloadedOperator() == OO_Equal && M->isImplicit() &&
               M->isTrivial() && M->getNumParams() == 1 &&
               (Operator ? Operator->getNumArgs() == 2
                         : isa<CXXMemberCallExpr>(Call)) &&
               M->getParamDecl(0)->getType()->isReferenceType() &&
               M->getReturnType()->isLValueReferenceType() &&
               Context.hasSameType(M->getReturnType().getNonReferenceType(),
                                   Context.getRecordType(Engine->Record)) &&
               Context.hasSameUnqualifiedType(
                   M->getParamDecl(0)->getType().getNonReferenceType(),
                   Context.getRecordType(Engine->Record))) {
      Operation = UtilityOperation::RandomEngineAssign;
    }
  } else if (Operator && Operator->getNumArgs() == 2 && F->getNumParams() == 2 &&
             F->getReturnType()->isBooleanType() &&
             (F->getOverloadedOperator() == OO_EqualEqual ||
              F->getOverloadedOperator() == OO_ExclaimEqual)) {
    for (const auto *P : F->parameters())
      if (!P->getType()->isLValueReferenceType() ||
          !Context.hasSameType(P->getType().getNonReferenceType(),
                               Context.getRecordType(Engine->Record).withConst()))
        return std::nullopt;
    Operation = F->getOverloadedOperator() == OO_EqualEqual
                    ? UtilityOperation::RandomEngineEqual
                    : UtilityOperation::RandomEngineNotEqual;
  }
  if (!Operation ||
      Call->getNumArgs() != F->getNumParams() + (Operator && M ? 1u : 0u) ||
      !Context.hasSameType(Call->getType(), F->getReturnType().getNonReferenceType()))
    return std::nullopt;
  // Trivial implicit assignment has compiler-defined memberwise semantics.
  // Its synthesized array-copy body is not a substituted libc++ function.
  if (*Operation == UtilityOperation::RandomEngineAssign) {
    for (const auto *Declaration : M->redecls())
      if (!approvedStandardSDKDeclaration(S, SM, Declaration))
        return std::nullopt;
    return Operation;
  }
  RandomSelection Selection(S, SM, Context, Engine->Record, Engine->ResultType,
                            Engine->ResultType);
  if (!Selection.retain(F))
    return std::nullopt;
  return Operation;
}

std::optional<UniformDistributionRecord>
approvedUniformDistributionRecord(const State &S, const SourceManager &SM,
                         const CXXRecordDecl *Record, const ASTContext &Context) {
  const auto *R = Record ? Record->getDefinition() : nullptr;
  const auto *Outer = R && R->getName() == "param_type"
      ? dyn_cast<ClassTemplateSpecializationDecl>(R->getDeclContext())
      : dyn_cast_or_null<ClassTemplateSpecializationDecl>(R);
  const auto *Template = Outer ? Outer->getSpecializedTemplate() : nullptr;
  const auto File = R ? S.sdkFile(SM, R->getLocation()) : std::nullopt;
  const bool Real = Template && Template->getName() == "uniform_real_distribution";
  if (!S.coreV2() || !R || !Outer || !Template || !File || File->Root != "libcxx" ||
      File->Path != (Real ? "__random/uniform_real_distribution.h" : "__random/uniform_int_distribution.h") ||
      (!Real && Template->getName() != "uniform_int_distribution") ||
      Outer->getSpecializationKind() != TSK_ImplicitInstantiation ||
      !approvedStandardSDKDeclaration(S, SM, Template) ||
      !R->isStandardLayout() || !R->isTriviallyCopyable() ||
      !R->hasTrivialDestructor() || R->getNumBases() || R->isUnion() ||
      Outer->getTemplateArgs().size() != 1 ||
      Outer->getTemplateArgs().get(0).getKind() != TemplateArgument::Type)
    return std::nullopt;
  const auto Result = Outer->getTemplateArgs().get(0).getAsType();
  if (Result.hasLocalQualifiers() ||
      (Real ? !Result->isSpecificBuiltinType(BuiltinType::Float) &&
                  !Result->isSpecificBuiltinType(BuiltinType::Double)
            : !Result->isIntegerType() || Result->isBooleanType() || Context.getTypeSize(Result) > 64))
    return std::nullopt;
  const CXXRecordDecl *Parameter = nullptr;
  const FieldDecl *Storage = nullptr, *Lower = nullptr, *Upper = nullptr;
  for (const auto *D : Outer->decls())
    if (const auto *Nested = dyn_cast<CXXRecordDecl>(D);
        Nested && Nested->getName() == "param_type" && Nested->getDefinition())
      Parameter = Nested->getDefinition();
  if (!Parameter || (R != Outer && R != Parameter) ||
      !Parameter->isStandardLayout() || !Parameter->isTriviallyCopyable() ||
      !Parameter->hasTrivialDestructor() || Parameter->getNumBases())
    return std::nullopt;
  for (const auto *F : Parameter->fields()) {
    if (!approvedStandardSDKDeclaration(S, SM, F) || F->isBitField() || F->isMutable() ||
        !Context.hasSameType(F->getType(), Result))
      return std::nullopt;
    if (F->getName() == "__a_" && !Lower)
      Lower = F;
    else if (F->getName() == "__b_" && !Upper)
      Upper = F;
    else
      return std::nullopt;
  }
  for (const auto *F : Outer->fields()) {
    if (Storage || F->getName() != "__p_" || F->isBitField() || F->isMutable() ||
        !approvedStandardSDKDeclaration(S, SM, F) ||
        !Context.hasSameType(F->getType(), Context.getRecordType(Parameter)))
      return std::nullopt;
    Storage = F;
  }
  RandomSelection Selection(S, SM, Context, Outer, Result, Result);
  if (!Storage || !Lower || !Upper || !Selection.retain(Outer) || !Selection.retain(Parameter))
    return std::nullopt;
  return UniformDistributionRecord{R, Outer, Parameter, Result, Storage, Lower, Upper, Real};
}

std::optional<UniformDistributionConstruction>
approvedUniformDistributionConstruction(const State &S, const SourceManager &SM,
                               const CXXConstructExpr *Construction,
                               const ASTContext &Context) {
  const auto *C = Construction ? Construction->getConstructor() : nullptr;
  const auto Info = C ? approvedUniformDistributionRecord(S, SM, C->getParent(), Context)
                      : std::nullopt;
  if (!Info || C->isVariadic() || C->getPrimaryTemplate() ||
      !approvedStandardSDKDeclaration(S, SM, C) ||
      Construction->getNumArgs() != C->getNumParams())
    return std::nullopt;
  if (C->isCopyOrMoveConstructor()) {
    if (C->isImplicit() && C->isTrivial())
      return UniformDistributionConstruction::CopyOrMove;
    return std::nullopt;
  }
  std::optional<UniformDistributionConstruction> Kind;
  if (!C->getNumParams() && Info->Record == Info->Distribution)
    Kind = UniformDistributionConstruction::Default;
  else if (C->getNumParams() == 2 &&
           Context.hasSameType(C->getParamDecl(0)->getType(), Info->ResultType) &&
           Context.hasSameType(C->getParamDecl(1)->getType(), Info->ResultType))
    Kind = UniformDistributionConstruction::Bounds;
  else if (C->getNumParams() == 1 && Info->Record == Info->Distribution &&
           Context.hasSameType(C->getParamDecl(0)->getType(),
               Context.getLValueReferenceType(Context.getRecordType(Info->Parameter).withConst())))
    Kind = UniformDistributionConstruction::Parameter;
  RandomSelection Selection(S, SM, Context, Info->Distribution, Info->ResultType, Info->ResultType);
  return Kind && C->hasBody() && Selection.retain(C) ? Kind : std::nullopt;
}

bool approvedUniformDistributionDefaultArgument(const State &S, const SourceManager &SM,
                                      const CXXDefaultArgExpr *Default,
                                      const FunctionDecl *Function, unsigned Index,
                                      const ASTContext &Context) {
  const auto *C = dyn_cast_or_null<CXXConstructorDecl>(Function);
  const auto Info = C ? approvedUniformDistributionRecord(S, SM, C->getParent(), Context)
                      : std::nullopt;
  if (!Info || !Default || Index >= C->getNumParams() || C->getNumParams() != 2 ||
      Default->getParam() != C->getParamDecl(Index) ||
      !Context.hasSameType(C->getParamDecl(Index)->getType(), Info->ResultType))
    return false;
  RandomSelection Selection(S, SM, Context, Info->Distribution, Info->ResultType, Info->ResultType);
  if (!Selection.retain(C) || !Selection.TraverseStmt(const_cast<Expr *>(Default->getExpr())))
    return false;
  if (Info->Real) {
    Expr::EvalResult Value;
    return Default->getExpr()->EvaluateAsRValue(Value, Context) && Value.Val.isFloat() &&
           Value.Val.getFloat().isExactlyValue(Index ? 1.0 : 0.0);
  }
  const auto Value = Default->getExpr()->getIntegerConstantExpr(Context);
  const unsigned Bits = Context.getTypeSize(Info->ResultType);
  const auto Expected = Index ? (Info->ResultType->isUnsignedIntegerType()
      ? llvm::APInt::getAllOnes(Bits) : llvm::APInt::getSignedMaxValue(Bits))
                             : llvm::APInt(Bits, 0);
  return Value && Value->zextOrTrunc(Bits) == Expected;
}

namespace {
std::optional<UtilityAlgorithmRandomCall> selectedRandomGenerator(
    const State &S, const SourceManager &SM, const FunctionDecl *Function,
    QualType Object, QualType Distance, UtilityOperation Operation, unsigned Index,
    const ASTContext &Context, SelectedRandomMath Math = SelectedRandomMath::None) {
  const auto *Generator = Object.getNonReferenceType()->getAsCXXRecordDecl();
  Generator = Generator ? Generator->getDefinition() : nullptr;
  const auto Engine = approvedRandomEngineRecord(S, SM, Generator, Context);
  if (!Object->isLValueReferenceType() || Object.getNonReferenceType().hasLocalQualifiers() ||
      !Generator || Generator->isLambda() || Generator->isUnion() || Generator->getNumBases() ||
      !Generator->isStandardLayout() || !Generator->isTriviallyCopyable() ||
      !Generator->hasTrivialDestructor() || (!Engine && !S.owns(SM, Generator->getLocation())))
    return std::nullopt;
  QualType Result;
  for (const auto *D : Generator->decls())
    if (const auto *Alias = dyn_cast<TypedefNameDecl>(D);
        Alias && Alias->getName() == "result_type") {
      if (Engine ? !approvedStandardSDKDeclaration(S, SM, Alias)
                 : !S.owns(SM, Alias->getLocation()))
        return std::nullopt;
      Result = Alias->getUnderlyingType();
      break;
    }
  if (Result.isNull() || Result.hasLocalQualifiers() || !Result->isUnsignedIntegerType() ||
      Result->isBooleanType() || Context.getTypeSize(Result) > 64)
    return std::nullopt;
  RandomSelection Selection(S, SM, Context, Generator, Result, Distance, false, nullptr, Math);
  if (!Selection.retain(Function) || !Selection.Method || !Selection.Minimum || !Selection.Maximum ||
      Selection.MinimumValue >= Selection.MaximumValue ||
      (Engine && (Selection.MinimumValue != Engine->Minimum || Selection.MaximumValue != Engine->Maximum)))
    return std::nullopt;
  return UtilityAlgorithmRandomCall{Operation,
      Selection.Method, Selection.Minimum, Selection.Maximum, Object, Result,
      Distance, Selection.MinimumValue, Selection.MaximumValue, Index};
}
}

std::optional<UtilityAlgorithmRandomCall>
approvedUniformDistributionRandomCall(const State &S, const SourceManager &SM,
                            const CallExpr *Call, const ASTContext &Context) {
  const auto *M = Call ? dyn_cast_or_null<CXXMethodDecl>(Call->getDirectCallee()) : nullptr;
  const auto *Operator = dyn_cast_or_null<CXXOperatorCallExpr>(Call);
  const auto Info = M ? approvedUniformDistributionRecord(S, SM, M->getParent(), Context) : std::nullopt;
  if (!Info || Info->Record != Info->Distribution || M->isStatic() || M->isConst() ||
      M->isVolatile() || M->isVariadic() || M->getOverloadedOperator() != OO_Call ||
      M->getNumParams() < 1 || M->getNumParams() > 2 ||
      !approvedStandardSDKDeclaration(S, SM, M) ||
      !Context.hasSameType(M->getReturnType(), Info->ResultType) ||
      Call->getNumArgs() != M->getNumParams() + (Operator ? 1u : 0u) ||
      !(Operator || isa<CXXMemberCallExpr>(Call)) ||
      Call->isTypeDependent() || Call->isValueDependent() || Call->isInstantiationDependent())
    return std::nullopt;
  if (M->getNumParams() == 2 && !Context.hasSameType(M->getParamDecl(1)->getType(),
      Context.getLValueReferenceType(Context.getRecordType(Info->Parameter).withConst())))
    return std::nullopt;
  return selectedRandomGenerator(S, SM, M, M->getParamDecl(0)->getType(), Info->ResultType,
      UtilityOperation::UniformGenerate, Operator ? 1u : 0u, Context);
}

std::optional<CanonicalRandomCall>
approvedCanonicalRandomCall(const State &S, const SourceManager &SM,
                            const CallExpr *Call, const ASTContext &Context) {
  const auto *F = Call ? Call->getDirectCallee() : nullptr;
  const auto *Args = F ? F->getTemplateSpecializationArgs() : nullptr;
  const auto File = F ? S.sdkFile(SM, F->getLocation()) : std::nullopt;
  if (!S.coreV2() || !F || !F->getIdentifier() || F->getName() != "generate_canonical" ||
      isa<CXXMethodDecl>(F) || F->isVariadic() || !F->getPrimaryTemplate() ||
      F->getTemplateSpecializationKind() != TSK_ImplicitInstantiation ||
      F->getNumParams() != 1 || Call->getNumArgs() != 1 || !Args || Args->size() != 3 ||
      !File || File->Root != "libcxx" || File->Path != "__random/generate_canonical.h" ||
      !approvedStandardSDKDeclaration(S, SM, F) ||
      Args->get(0).getKind() != TemplateArgument::Type ||
      Args->get(1).getKind() != TemplateArgument::Integral ||
      Args->get(2).getKind() != TemplateArgument::Type ||
      Args->get(1).getAsIntegral().isNegative() || Args->get(1).getAsIntegral().getBitWidth() > 64)
    return std::nullopt;
  const auto Real = Args->get(0).getAsType();
  if (Real.hasLocalQualifiers() ||
      (!Real->isSpecificBuiltinType(BuiltinType::Float) && !Real->isSpecificBuiltinType(BuiltinType::Double)) ||
      !Context.hasSameType(F->getReturnType(), Real) || !Context.hasSameType(Call->getType(), Real) ||
      !Context.hasSameType(F->getParamDecl(0)->getType(), Context.getLValueReferenceType(Args->get(2).getAsType())))
    return std::nullopt;
  const auto Random = selectedRandomGenerator(S, SM, F, F->getParamDecl(0)->getType(), Real,
      UtilityOperation::GenerateCanonical, 0, Context);
  if (!Random)
    return std::nullopt;
  return CanonicalRandomCall{*Random, Args->get(1).getAsIntegral().getZExtValue()};
}

std::optional<UtilityOperation>
approvedUniformDistributionOperation(const State &S, const SourceManager &SM,
                            const CallExpr *Call, const ASTContext &Context) {
  const auto *F = Call ? Call->getDirectCallee() : nullptr;
  const auto *M = dyn_cast_or_null<CXXMethodDecl>(F);
  const auto *Operator = dyn_cast_or_null<CXXOperatorCallExpr>(Call);
  const auto *R = M ? M->getParent() : F && F->getNumParams() == 2
      ? F->getParamDecl(0)->getType().getNonReferenceType()->getAsCXXRecordDecl() : nullptr;
  const auto Info = approvedUniformDistributionRecord(S, SM, R, Context);
  if (!Info || !F || F->isVariadic() || !approvedStandardSDKDeclaration(S, SM, F) ||
      Call->getNumArgs() != F->getNumParams() + (Operator && M ? 1u : 0u))
    return std::nullopt;
  std::optional<UtilityOperation> Operation;
  if (M && !M->isStatic() && !M->isVolatile()) {
    if (approvedUniformDistributionRandomCall(S, SM, Call, Context))
      return UtilityOperation::UniformGenerate;
    if (M->isConst() && !M->getNumParams() &&
        Context.hasSameType(M->getReturnType(), Info->ResultType) && M->getIdentifier()) {
      if (M->getName() == "a" || M->getName() == "min")
        Operation = UtilityOperation::UniformLower;
      else if (M->getName() == "b" || M->getName() == "max")
        Operation = UtilityOperation::UniformUpper;
    } else if (Info->Record == Info->Distribution && M->getIdentifier()) {
      if (M->getName() == "reset" && !M->isConst() && !M->getNumParams() && M->getReturnType()->isVoidType())
        Operation = UtilityOperation::UniformReset;
      else if (M->getName() == "param" && M->isConst() && !M->getNumParams() &&
               Context.hasSameType(M->getReturnType(), Context.getRecordType(Info->Parameter)))
        Operation = UtilityOperation::UniformParamGet;
      else if (M->getName() == "param" && !M->isConst() && M->getNumParams() == 1 &&
               M->getReturnType()->isVoidType() &&
               Context.hasSameType(M->getParamDecl(0)->getType(),
                   Context.getLValueReferenceType(Context.getRecordType(Info->Parameter).withConst())))
        Operation = UtilityOperation::UniformParamSet;
    }
    if (M->getOverloadedOperator() == OO_Equal && M->isImplicit() && M->isTrivial() &&
        M->getNumParams() == 1 && M->getReturnType()->isLValueReferenceType() &&
        Context.hasSameType(M->getReturnType().getNonReferenceType(), Context.getRecordType(Info->Record)) &&
        M->getParamDecl(0)->getType()->isReferenceType() &&
        Context.hasSameUnqualifiedType(M->getParamDecl(0)->getType().getNonReferenceType(),
                                       Context.getRecordType(Info->Record)))
      return UtilityOperation::UniformAssign;
  } else if (!M && F->getReturnType()->isBooleanType() && F->getNumParams() == 2 &&
             (F->getOverloadedOperator() == OO_EqualEqual || F->getOverloadedOperator() == OO_ExclaimEqual)) {
    for (const auto *P : F->parameters())
      if (!Context.hasSameType(P->getType(),
          Context.getLValueReferenceType(Context.getRecordType(Info->Record).withConst())))
        return std::nullopt;
    Operation = F->getOverloadedOperator() == OO_EqualEqual
        ? UtilityOperation::UniformEqual : UtilityOperation::UniformNotEqual;
  }
  RandomSelection Selection(S, SM, Context, Info->Distribution, Info->ResultType, Info->ResultType);
  return Operation && Selection.retain(F) ? Operation : std::nullopt;
}

std::optional<BernoulliRecord>
approvedBernoulliRecord(const State &S, const SourceManager &SM,
                       const CXXRecordDecl *Record, const ASTContext &Context) {
  const auto *R = Record ? Record->getDefinition() : nullptr;
  const auto *Outer = R && R->getName() == "param_type"
      ? dyn_cast<CXXRecordDecl>(R->getDeclContext()) : R;
  const auto File = R ? S.sdkFile(SM, R->getLocation()) : std::nullopt;
  if (!S.coreV2() || !R || !Outer || !File || File->Root != "libcxx" ||
      File->Path != "__random/bernoulli_distribution.h" ||
      Outer->getName() != "bernoulli_distribution" || isa<ClassTemplateSpecializationDecl>(Outer) ||
      !approvedStandardSDKDeclaration(S, SM, Outer) || !R->isStandardLayout() ||
      !R->isTriviallyCopyable() || !R->hasTrivialDestructor() || R->getNumBases() || R->isUnion())
    return std::nullopt;
  const CXXRecordDecl *Parameter = nullptr;
  for (const auto *D : Outer->decls())
    if (const auto *Nested = dyn_cast<CXXRecordDecl>(D);
        Nested && Nested->getName() == "param_type" && Nested->getDefinition())
      Parameter = Nested->getDefinition();
  if (!Parameter || (R != Outer && R != Parameter) || !Parameter->isStandardLayout() ||
      !Parameter->isTriviallyCopyable() || !Parameter->hasTrivialDestructor() || Parameter->getNumBases())
    return std::nullopt;
  const FieldDecl *Storage = nullptr, *Probability = nullptr;
  for (const auto *F : Parameter->fields()) {
    if (Probability || F->getName() != "__p_" || F->isBitField() || F->isMutable() ||
        !approvedStandardSDKDeclaration(S, SM, F) || !Context.hasSameType(F->getType(), Context.DoubleTy))
      return std::nullopt;
    Probability = F;
  }
  for (const auto *F : Outer->fields()) {
    if (Storage || F->getName() != "__p_" || F->isBitField() || F->isMutable() ||
        !approvedStandardSDKDeclaration(S, SM, F) ||
        !Context.hasSameType(F->getType(), Context.getRecordType(Parameter)))
      return std::nullopt;
    Storage = F;
  }
  RandomSelection Selection(S, SM, Context, Outer, Context.BoolTy, Context.DoubleTy);
  if (!Storage || !Probability || !Selection.retain(Outer) || !Selection.retain(Parameter))
    return std::nullopt;
  return BernoulliRecord{R, Outer, Parameter, Storage, Probability};
}

std::optional<BernoulliConstruction>
approvedBernoulliConstruction(const State &S, const SourceManager &SM,
                             const CXXConstructExpr *Construction,
                             const ASTContext &Context) {
  const auto *C = Construction ? Construction->getConstructor() : nullptr;
  const auto Info = C ? approvedBernoulliRecord(S, SM, C->getParent(), Context) : std::nullopt;
  if (!Info || C->isVariadic() || C->getPrimaryTemplate() ||
      !approvedStandardSDKDeclaration(S, SM, C) || Construction->getNumArgs() != C->getNumParams())
    return std::nullopt;
  if (C->isCopyOrMoveConstructor())
    return C->isImplicit() && C->isTrivial() ? std::optional<BernoulliConstruction>(BernoulliConstruction::CopyOrMove) : std::nullopt;
  std::optional<BernoulliConstruction> Kind;
  if (!C->getNumParams() && Info->Record == Info->Distribution)
    Kind = BernoulliConstruction::Default;
  else if (C->getNumParams() == 1 && Context.hasSameType(C->getParamDecl(0)->getType(), Context.DoubleTy))
    Kind = BernoulliConstruction::Probability;
  else if (C->getNumParams() == 1 && Info->Record == Info->Distribution &&
           Context.hasSameType(C->getParamDecl(0)->getType(),
               Context.getLValueReferenceType(Context.getRecordType(Info->Parameter).withConst())))
    Kind = BernoulliConstruction::Parameter;
  RandomSelection Selection(S, SM, Context, Info->Distribution, Context.BoolTy, Context.DoubleTy);
  return Kind && C->hasBody() && Selection.retain(C) ? Kind : std::nullopt;
}

bool approvedBernoulliDefaultArgument(const State &S, const SourceManager &SM,
                                    const CXXDefaultArgExpr *Default,
                                    const FunctionDecl *Function, unsigned Index,
                                    const ASTContext &Context) {
  const auto *C = dyn_cast_or_null<CXXConstructorDecl>(Function);
  const auto Info = C ? approvedBernoulliRecord(S, SM, C->getParent(), Context) : std::nullopt;
  if (!Info || !Default || Index || C->getNumParams() != 1 ||
      Default->getParam() != C->getParamDecl(0) ||
      !Context.hasSameType(C->getParamDecl(0)->getType(), Context.DoubleTy))
    return false;
  Expr::EvalResult Value;
  RandomSelection Selection(S, SM, Context, Info->Distribution, Context.BoolTy, Context.DoubleTy);
  return Default->getExpr()->EvaluateAsRValue(Value, Context) && Value.Val.isFloat() &&
      Value.Val.getFloat().isExactlyValue(0.5) && Selection.retain(C) &&
      Selection.TraverseStmt(const_cast<Expr *>(Default->getExpr()));
}

std::optional<UtilityAlgorithmRandomCall>
approvedBernoulliRandomCall(const State &S, const SourceManager &SM,
                          const CallExpr *Call, const ASTContext &Context) {
  const auto *M = Call ? dyn_cast_or_null<CXXMethodDecl>(Call->getDirectCallee()) : nullptr;
  const auto *Operator = dyn_cast_or_null<CXXOperatorCallExpr>(Call);
  const auto Info = M ? approvedBernoulliRecord(S, SM, M->getParent(), Context) : std::nullopt;
  if (!Info || Info->Record != Info->Distribution || M->isStatic() || M->isConst() ||
      M->isVolatile() || M->isVariadic() || M->getOverloadedOperator() != OO_Call ||
      M->getNumParams() < 1 || M->getNumParams() > 2 ||
      !approvedStandardSDKDeclaration(S, SM, M) || !M->getReturnType()->isBooleanType() ||
      Call->getNumArgs() != M->getNumParams() + (Operator ? 1u : 0u) ||
      !(Operator || isa<CXXMemberCallExpr>(Call)) ||
      (M->getNumParams() == 2 && !Context.hasSameType(M->getParamDecl(1)->getType(),
          Context.getLValueReferenceType(Context.getRecordType(Info->Parameter).withConst()))))
    return std::nullopt;
  return selectedRandomGenerator(S, SM, M, M->getParamDecl(0)->getType(), Context.DoubleTy,
      UtilityOperation::BernoulliGenerate, Operator ? 1u : 0u, Context);
}

std::optional<UtilityOperation>
approvedBernoulliOperation(const State &S, const SourceManager &SM,
                         const CallExpr *Call, const ASTContext &Context) {
  const auto *F = Call ? Call->getDirectCallee() : nullptr;
  const auto *M = dyn_cast_or_null<CXXMethodDecl>(F);
  const auto *Operator = dyn_cast_or_null<CXXOperatorCallExpr>(Call);
  const auto *R = M ? M->getParent() : F && F->getNumParams() == 2
      ? F->getParamDecl(0)->getType().getNonReferenceType()->getAsCXXRecordDecl() : nullptr;
  const auto Info = approvedBernoulliRecord(S, SM, R, Context);
  if (!Info || !F || F->isVariadic() || !approvedStandardSDKDeclaration(S, SM, F) ||
      Call->getNumArgs() != F->getNumParams() + (Operator && M ? 1u : 0u))
    return std::nullopt;
  if (approvedBernoulliRandomCall(S, SM, Call, Context))
    return UtilityOperation::BernoulliGenerate;
  std::optional<UtilityOperation> Operation;
  if (M && !M->isStatic() && !M->isVolatile()) {
    if (M->isConst() && M->getIdentifier() && !M->getNumParams()) {
      if (M->getName() == "p" && Context.hasSameType(M->getReturnType(), Context.DoubleTy))
        Operation = UtilityOperation::BernoulliProbability;
      else if (Info->Record == Info->Distribution && M->getReturnType()->isBooleanType()) {
        if (M->getName() == "min") Operation = UtilityOperation::BernoulliMinimum;
        else if (M->getName() == "max") Operation = UtilityOperation::BernoulliMaximum;
      } else if (M->getName() == "param" && Info->Record == Info->Distribution &&
                 Context.hasSameType(M->getReturnType(), Context.getRecordType(Info->Parameter)))
        Operation = UtilityOperation::BernoulliParamGet;
    } else if (!M->isConst() && M->getIdentifier() && M->getReturnType()->isVoidType() &&
               Info->Record == Info->Distribution) {
      if (M->getName() == "reset" && !M->getNumParams())
        Operation = UtilityOperation::BernoulliReset;
      else if (M->getName() == "param" && M->getNumParams() == 1 &&
          Context.hasSameType(M->getParamDecl(0)->getType(),
              Context.getLValueReferenceType(Context.getRecordType(Info->Parameter).withConst())))
        Operation = UtilityOperation::BernoulliParamSet;
    }
    if (M->getOverloadedOperator() == OO_Equal && M->isImplicit() && M->isTrivial() &&
        M->getNumParams() == 1 && M->getReturnType()->isLValueReferenceType() &&
        Context.hasSameType(M->getReturnType().getNonReferenceType(), Context.getRecordType(Info->Record)) &&
        M->getParamDecl(0)->getType()->isReferenceType() &&
        Context.hasSameUnqualifiedType(M->getParamDecl(0)->getType().getNonReferenceType(), Context.getRecordType(Info->Record)))
      return UtilityOperation::BernoulliAssign;
  } else if (!M && F->getReturnType()->isBooleanType() && F->getNumParams() == 2 &&
             (F->getOverloadedOperator() == OO_EqualEqual || F->getOverloadedOperator() == OO_ExclaimEqual)) {
    for (const auto *P : F->parameters())
      if (!Context.hasSameType(P->getType(),
          Context.getLValueReferenceType(Context.getRecordType(Info->Record).withConst())))
        return std::nullopt;
    Operation = F->getOverloadedOperator() == OO_EqualEqual ? UtilityOperation::BernoulliEqual : UtilityOperation::BernoulliNotEqual;
  }
  RandomSelection Selection(S, SM, Context, Info->Distribution, Context.BoolTy, Context.DoubleTy);
  return Operation && Selection.retain(F) ? Operation : std::nullopt;
}

std::optional<ExponentialRecord>
approvedExponentialRecord(const State &S, const SourceManager &SM,
                         const CXXRecordDecl *Record, const ASTContext &Context) {
  const auto *R = Record ? Record->getDefinition() : nullptr;
  const auto *Outer = R && R->getName() == "param_type"
      ? dyn_cast<ClassTemplateSpecializationDecl>(R->getDeclContext())
      : dyn_cast_or_null<ClassTemplateSpecializationDecl>(R);
  const auto *Template = Outer ? Outer->getSpecializedTemplate() : nullptr;
  const auto File = R ? S.sdkFile(SM, R->getLocation()) : std::nullopt;
  if (!S.coreV2() || !R || !Outer || !Template || !File || File->Root != "libcxx" ||
      File->Path != "__random/exponential_distribution.h" ||
      Template->getName() != "exponential_distribution" ||
      Outer->getSpecializationKind() != TSK_ImplicitInstantiation ||
      !approvedStandardSDKDeclaration(S, SM, Template) ||
      !R->isStandardLayout() || !R->isTriviallyCopyable() ||
      !R->hasTrivialDestructor() || R->getNumBases() || R->isUnion() ||
      Outer->getTemplateArgs().size() != 1 ||
      Outer->getTemplateArgs().get(0).getKind() != TemplateArgument::Type)
    return std::nullopt;
  const auto Result = Outer->getTemplateArgs().get(0).getAsType();
  if (Result.hasLocalQualifiers() ||
      (!Result->isSpecificBuiltinType(BuiltinType::Float) &&
       !Result->isSpecificBuiltinType(BuiltinType::Double)))
    return std::nullopt;
  const CXXRecordDecl *Parameter = nullptr;
  const FieldDecl *Storage = nullptr, *Rate = nullptr;
  for (const auto *D : Outer->decls())
    if (const auto *Nested = dyn_cast<CXXRecordDecl>(D);
        Nested && Nested->getName() == "param_type" && Nested->getDefinition())
      Parameter = Nested->getDefinition();
  if (!Parameter || (R != Outer && R != Parameter) ||
      !Parameter->isStandardLayout() || !Parameter->isTriviallyCopyable() ||
      !Parameter->hasTrivialDestructor() || Parameter->getNumBases())
    return std::nullopt;
  for (const auto *F : Parameter->fields()) {
    if (!approvedStandardSDKDeclaration(S, SM, F) || F->isBitField() || F->isMutable() ||
        !Context.hasSameType(F->getType(), Result))
      return std::nullopt;
    if (F->getName() == "__lambda_" && !Rate)
      Rate = F;
    else
      return std::nullopt;
  }
  for (const auto *F : Outer->fields()) {
    if (Storage || F->getName() != "__p_" || F->isBitField() || F->isMutable() ||
        !approvedStandardSDKDeclaration(S, SM, F) ||
        !Context.hasSameType(F->getType(), Context.getRecordType(Parameter)))
      return std::nullopt;
    Storage = F;
  }
  RandomSelection Selection(S, SM, Context, Outer, Result, Result);
  if (!Storage || !Rate || !Selection.retain(Outer) || !Selection.retain(Parameter))
    return std::nullopt;
  return ExponentialRecord{R, Outer, Parameter, Result, Storage, Rate};
}

std::optional<ExponentialConstruction>
approvedExponentialConstruction(const State &S, const SourceManager &SM,
                             const CXXConstructExpr *Construction,
                             const ASTContext &Context) {
  const auto *C = Construction ? Construction->getConstructor() : nullptr;
  const auto Info = C ? approvedExponentialRecord(S, SM, C->getParent(), Context) : std::nullopt;
  if (!Info || C->isVariadic() || C->getPrimaryTemplate() ||
      !approvedStandardSDKDeclaration(S, SM, C) || Construction->getNumArgs() != C->getNumParams())
    return std::nullopt;
  if (C->isCopyOrMoveConstructor())
    return C->isImplicit() && C->isTrivial() ? std::optional<ExponentialConstruction>(ExponentialConstruction::CopyOrMove) : std::nullopt;
  std::optional<ExponentialConstruction> Kind;
  if (!C->getNumParams() && Info->Record == Info->Distribution)
    Kind = ExponentialConstruction::Default;
  else if (C->getNumParams() == 1 && Context.hasSameType(C->getParamDecl(0)->getType(), Info->ResultType))
    Kind = ExponentialConstruction::Rate;
  else if (C->getNumParams() == 1 && Info->Record == Info->Distribution &&
           Context.hasSameType(C->getParamDecl(0)->getType(),
               Context.getLValueReferenceType(Context.getRecordType(Info->Parameter).withConst())))
    Kind = ExponentialConstruction::Parameter;
  RandomSelection Selection(S, SM, Context, Info->Distribution, Info->ResultType, Info->ResultType);
  return Kind && C->hasBody() && Selection.retain(C) ? Kind : std::nullopt;
}

bool approvedExponentialDefaultArgument(const State &S, const SourceManager &SM,
                                    const CXXDefaultArgExpr *Default,
                                    const FunctionDecl *Function, unsigned Index,
                                    const ASTContext &Context) {
  const auto *C = dyn_cast_or_null<CXXConstructorDecl>(Function);
  const auto Info = C ? approvedExponentialRecord(S, SM, C->getParent(), Context) : std::nullopt;
  if (!Info || !Default || Index || C->getNumParams() != 1 ||
      Default->getParam() != C->getParamDecl(0) ||
      !Context.hasSameType(C->getParamDecl(0)->getType(), Info->ResultType))
    return false;
  Expr::EvalResult Value;
  RandomSelection Selection(S, SM, Context, Info->Distribution, Info->ResultType, Info->ResultType);
  return Default->getExpr()->EvaluateAsRValue(Value, Context) && Value.Val.isFloat() &&
      Value.Val.getFloat().isExactlyValue(1.0) && Selection.retain(C) &&
      Selection.TraverseStmt(const_cast<Expr *>(Default->getExpr()));
}

std::optional<UtilityAlgorithmRandomCall>
approvedExponentialRandomCall(const State &S, const SourceManager &SM,
                          const CallExpr *Call, const ASTContext &Context) {
  const auto *M = Call ? dyn_cast_or_null<CXXMethodDecl>(Call->getDirectCallee()) : nullptr;
  const auto *Operator = dyn_cast_or_null<CXXOperatorCallExpr>(Call);
  const auto Info = M ? approvedExponentialRecord(S, SM, M->getParent(), Context) : std::nullopt;
  if (!Info || Info->Record != Info->Distribution || M->isStatic() || M->isConst() ||
      M->isVolatile() || M->isVariadic() || M->getOverloadedOperator() != OO_Call ||
      M->getNumParams() < 1 || M->getNumParams() > 2 ||
      !approvedStandardSDKDeclaration(S, SM, M) || !Context.hasSameType(M->getReturnType(), Info->ResultType) ||
      Call->getNumArgs() != M->getNumParams() + (Operator ? 1u : 0u) ||
      !(Operator || isa<CXXMemberCallExpr>(Call)) ||
      (M->getNumParams() == 2 && !Context.hasSameType(M->getParamDecl(1)->getType(),
          Context.getLValueReferenceType(Context.getRecordType(Info->Parameter).withConst()))))
    return std::nullopt;
  return selectedRandomGenerator(S, SM, M, M->getParamDecl(0)->getType(), Info->ResultType,
      UtilityOperation::ExponentialGenerate, Operator ? 1u : 0u, Context, SelectedRandomMath::ExponentialLog);
}

std::optional<UtilityOperation>
approvedExponentialOperation(const State &S, const SourceManager &SM,
                         const CallExpr *Call, const ASTContext &Context) {
  const auto *F = Call ? Call->getDirectCallee() : nullptr;
  const auto *M = dyn_cast_or_null<CXXMethodDecl>(F);
  const auto *Operator = dyn_cast_or_null<CXXOperatorCallExpr>(Call);
  const auto *R = M ? M->getParent() : F && F->getNumParams() == 2
      ? F->getParamDecl(0)->getType().getNonReferenceType()->getAsCXXRecordDecl() : nullptr;
  const auto Info = approvedExponentialRecord(S, SM, R, Context);
  if (!Info || !F || F->isVariadic() || !approvedStandardSDKDeclaration(S, SM, F) ||
      Call->getNumArgs() != F->getNumParams() + (Operator && M ? 1u : 0u))
    return std::nullopt;
  if (approvedExponentialRandomCall(S, SM, Call, Context))
    return UtilityOperation::ExponentialGenerate;
  std::optional<UtilityOperation> Operation;
  if (M && !M->isStatic() && !M->isVolatile()) {
    if (M->isConst() && M->getIdentifier() && !M->getNumParams()) {
      if (M->getName() == "lambda" && Context.hasSameType(M->getReturnType(), Info->ResultType))
        Operation = UtilityOperation::ExponentialRate;
      else if (Info->Record == Info->Distribution && Context.hasSameType(M->getReturnType(), Info->ResultType)) {
        if (M->getName() == "min") Operation = UtilityOperation::ExponentialMinimum;
        else if (M->getName() == "max") Operation = UtilityOperation::ExponentialMaximum;
      } else if (M->getName() == "param" && Info->Record == Info->Distribution &&
                 Context.hasSameType(M->getReturnType(), Context.getRecordType(Info->Parameter)))
        Operation = UtilityOperation::ExponentialParamGet;
    } else if (!M->isConst() && M->getIdentifier() && M->getReturnType()->isVoidType() &&
               Info->Record == Info->Distribution) {
      if (M->getName() == "reset" && !M->getNumParams())
        Operation = UtilityOperation::ExponentialReset;
      else if (M->getName() == "param" && M->getNumParams() == 1 &&
          Context.hasSameType(M->getParamDecl(0)->getType(),
              Context.getLValueReferenceType(Context.getRecordType(Info->Parameter).withConst())))
        Operation = UtilityOperation::ExponentialParamSet;
    }
    if (M->getOverloadedOperator() == OO_Equal && M->isImplicit() && M->isTrivial() &&
        M->getNumParams() == 1 && M->getReturnType()->isLValueReferenceType() &&
        Context.hasSameType(M->getReturnType().getNonReferenceType(), Context.getRecordType(Info->Record)) &&
        M->getParamDecl(0)->getType()->isReferenceType() &&
        Context.hasSameUnqualifiedType(M->getParamDecl(0)->getType().getNonReferenceType(), Context.getRecordType(Info->Record)))
      return UtilityOperation::ExponentialAssign;
  } else if (!M && F->getReturnType()->isBooleanType() && F->getNumParams() == 2 &&
             (F->getOverloadedOperator() == OO_EqualEqual || F->getOverloadedOperator() == OO_ExclaimEqual)) {
    for (const auto *P : F->parameters())
      if (!Context.hasSameType(P->getType(),
          Context.getLValueReferenceType(Context.getRecordType(Info->Record).withConst())))
        return std::nullopt;
    Operation = F->getOverloadedOperator() == OO_EqualEqual ? UtilityOperation::ExponentialEqual : UtilityOperation::ExponentialNotEqual;
  }
  RandomSelection Selection(S, SM, Context, Info->Distribution, Info->ResultType, Info->ResultType);
  return Operation && Selection.retain(F) ? Operation : std::nullopt;
}

std::optional<PiecewiseRecord>
approvedPiecewiseRecord(const State &S, const SourceManager &SM,
                                const CXXRecordDecl *Record,
                                const ASTContext &Context) {
  const auto *R = Record ? Record->getDefinition() : nullptr;
  const auto *Outer = R && R->getName() == "param_type"
      ? dyn_cast<CXXRecordDecl>(R->getDeclContext()) : R;
  const auto *Specialization = dyn_cast_or_null<ClassTemplateSpecializationDecl>(Outer);
  const auto File = R ? S.sdkFile(SM, R->getLocation()) : std::nullopt;
  const bool Linear = Outer && Outer->getName() == "piecewise_linear_distribution";
  if (!S.coreV2() || !R || !Outer || !Specialization || !File ||
      File->Root != "libcxx" ||
      File->Path != (Linear ? "__random/piecewise_linear_distribution.h"
                           : "__random/piecewise_constant_distribution.h") ||
      (!Linear && Outer->getName() != "piecewise_constant_distribution") ||
      !approvedStandardSDKDeclaration(S, SM, Outer) || R->getNumBases() ||
      R->isUnion() || !R->isStandardLayout() ||
      Specialization->getTemplateArgs().size() != 1 ||
      Specialization->getTemplateArgs().get(0).getKind() != TemplateArgument::Type)
    return std::nullopt;
  const auto Real = Specialization->getTemplateArgs().get(0).getAsType();
  if (Real.hasLocalQualifiers() ||
      (!Real->isSpecificBuiltinType(BuiltinType::Float) &&
       !Real->isSpecificBuiltinType(BuiltinType::Double)))
    return std::nullopt;
  const CXXRecordDecl *Parameter = nullptr;
  for (const auto *D : Outer->decls())
    if (const auto *Nested = dyn_cast<CXXRecordDecl>(D);
        Nested && Nested->getName() == "param_type" && Nested->getDefinition())
      Parameter = Nested->getDefinition();
  if (!Parameter || (R != Outer && R != Parameter) ||
      !Parameter->isStandardLayout() || Parameter->getNumBases() ||
      !approvedStandardSDKDeclaration(S, SM, Parameter))
    return std::nullopt;
  const FieldDecl *Fields[3] = {};
  const char *Names[] = {"__b_", "__densities_", "__areas_"};
  unsigned Index = 0;
  for (const auto *F : Parameter->fields()) {
    const auto Vector = approvedUtilityVectorRecord(S, SM,
        F->getType()->getAsCXXRecordDecl(), Context);
    if (Index == 3 || F->getName() != Names[Index] || F->isBitField() || F->isMutable() ||
        !approvedStandardSDKDeclaration(S, SM, F) || !Vector ||
        !Context.hasSameType(Vector->ElementType, Real))
      return std::nullopt;
    Fields[Index++] = F;
  }
  const FieldDecl *Storage = nullptr;
  for (const auto *F : Outer->fields()) {
    if (Storage || F->getName() != "__p_" || F->isBitField() || F->isMutable() ||
        !approvedStandardSDKDeclaration(S, SM, F) ||
        !Context.hasSameType(F->getType(), Context.getRecordType(Parameter)))
      return std::nullopt;
    Storage = F;
  }
  RandomSelection Selection(S, SM, Context, Outer, Real, Real);
  if (Index != 3 || !Storage || !Selection.retain(R))
    return std::nullopt;
  return PiecewiseRecord{R, Outer, Parameter, Real, Storage, Fields[0], Fields[1], Fields[2], Linear};
}

std::optional<PiecewiseConstruction>
approvedPiecewiseConstruction(const State &S, const SourceManager &SM,
                                      const CXXConstructExpr *Construction,
                                      const ASTContext &Context) {
  const auto *C = Construction ? Construction->getConstructor() : nullptr;
  const auto Info = C ? approvedPiecewiseRecord(S, SM, C->getParent(), Context) : std::nullopt;
  if (!Info || C->isVariadic() || !approvedStandardSDKDeclaration(S, SM, C) ||
      Construction->getNumArgs() != C->getNumParams())
    return std::nullopt;
  std::optional<PiecewiseConstruction> Kind;
  if (C->isCopyOrMoveConstructor()) {
    if ((!C->isImplicit() && !C->isExplicitlyDefaulted()) || C->getNumParams() != 1 ||
        !C->getParamDecl(0)->getType()->isReferenceType() ||
        !Context.hasSameUnqualifiedType(C->getParamDecl(0)->getType().getNonReferenceType(),
                                       Context.getRecordType(Info->Record)))
      return std::nullopt;
    // param_type explicitly declares copy construction/assignment and has no
    // move. The outer generated move therefore copies its parameter member.
    Kind = PiecewiseConstruction::Copy;
  } else if (!C->getNumParams()) {
    Kind = PiecewiseConstruction::Default;
  } else if (C->getNumParams() == 1 && Info->Record == Info->Distribution &&
      Context.hasSameType(C->getParamDecl(0)->getType(),
          Context.getLValueReferenceType(Context.getRecordType(Info->Parameter).withConst()))) {
    Kind = PiecewiseConstruction::Parameter;
  } else if (C->getNumParams() == 3 &&
      Context.hasSameType(C->getParamDecl(0)->getType(), C->getParamDecl(1)->getType())) {
    const auto Bound = seedSequencePointer(S, SM, C->getParamDecl(0)->getType(), Context);
    const auto Weight = seedSequencePointer(S, SM, C->getParamDecl(2)->getType(), Context);
    if (!Bound.isNull() && !Weight.isNull() &&
        !Bound->getPointeeType().isVolatileQualified() &&
        !Weight->getPointeeType().isVolatileQualified() &&
        scalarElement(Bound->getPointeeType(), Context) &&
        scalarElement(Weight->getPointeeType(), Context))
      Kind = PiecewiseConstruction::Range;
  }
  if (C->getNumParams() == 4 &&
      Context.hasSameType(C->getParamDecl(0)->getType(), Context.getSizeType()) &&
      Context.hasSameType(C->getParamDecl(1)->getType(), Info->ResultType) &&
      Context.hasSameType(C->getParamDecl(2)->getType(), Info->ResultType))
    Kind = PiecewiseConstruction::CountWeights;
  if (C->getNumParams() == 2) {
    const auto List = approvedUtilityInitializerListRecord(S, SM,
        C->getParamDecl(0)->getType()->getAsCXXRecordDecl(), Context);
    if (List && Context.hasSameType(List->ElementType, Info->ResultType)) {
      const auto *Argument = dyn_cast<CXXStdInitializerListExpr>(Construction->getArg(0));
      if (Argument && !approvedUtilityInitializerListExpression(S, SM, Argument, Context))
        return std::nullopt;
      Kind = PiecewiseConstruction::ListWeights;
    }
  }
  const bool Callback = Kind == PiecewiseConstruction::CountWeights ||
                        Kind == PiecewiseConstruction::ListWeights;
  if (Callback) {
    if (approvedRandomWeightObjectConstruction(S, SM, Construction, Context))
      return Kind;
    if (!randomWeightFunction(C->getParamDecl(C->getNumParams() - 1)->getType(), Context))
      return std::nullopt;
  }
  RandomSelection Selection(S, SM, Context, Info->Distribution, Info->ResultType, Info->ResultType, Callback);
  return Kind && C->hasBody() && Selection.retain(C) ? Kind : std::nullopt;
}

std::optional<UtilityAlgorithmRandomCall>
approvedPiecewiseRandomCall(const State &S, const SourceManager &SM,
                                   const CallExpr *Call, const ASTContext &Context) {
  const auto *M = Call ? dyn_cast_or_null<CXXMethodDecl>(Call->getDirectCallee()) : nullptr;
  const auto *Operator = dyn_cast_or_null<CXXOperatorCallExpr>(Call);
  const auto Info = M ? approvedPiecewiseRecord(S, SM, M->getParent(), Context) : std::nullopt;
  if (!Info || Info->Record != Info->Distribution || M->isStatic() || M->isConst() ||
      M->isVolatile() || M->isVariadic() || M->getOverloadedOperator() != OO_Call ||
      M->getNumParams() < 1 || M->getNumParams() > 2 ||
      !approvedStandardSDKDeclaration(S, SM, M) ||
      !Context.hasSameType(M->getReturnType(), Info->ResultType) ||
      Call->getNumArgs() != M->getNumParams() + (Operator ? 1u : 0u) ||
      !(Operator || isa<CXXMemberCallExpr>(Call)) ||
      (M->getNumParams() == 2 && !Context.hasSameType(M->getParamDecl(1)->getType(),
          Context.getLValueReferenceType(Context.getRecordType(Info->Parameter).withConst()))))
    return std::nullopt;
  return selectedRandomGenerator(S, SM, M, M->getParamDecl(0)->getType(), Info->ResultType,
      UtilityOperation::PiecewiseGenerate, Operator ? 1u : 0u, Context, Info->Linear ? SelectedRandomMath::LinearSquareRoot : SelectedRandomMath::None);
}

std::optional<UtilityOperation>
approvedPiecewiseOperation(const State &S, const SourceManager &SM,
                                  const CallExpr *Call, const ASTContext &Context) {
  const auto *F = Call ? Call->getDirectCallee() : nullptr;
  const auto *M = dyn_cast_or_null<CXXMethodDecl>(F);
  const auto *Operator = dyn_cast_or_null<CXXOperatorCallExpr>(Call);
  const auto *R = M ? M->getParent() : F && F->getNumParams() == 2
      ? F->getParamDecl(0)->getType().getNonReferenceType()->getAsCXXRecordDecl() : nullptr;
  const auto Info = approvedPiecewiseRecord(S, SM, R, Context);
  if (!Info || !F || F->isVariadic() || !approvedStandardSDKDeclaration(S, SM, F) ||
      Call->getNumArgs() != F->getNumParams() + (Operator && M ? 1u : 0u))
    return std::nullopt;
  if (approvedPiecewiseRandomCall(S, SM, Call, Context))
    return UtilityOperation::PiecewiseGenerate;
  std::optional<UtilityOperation> Operation;
  if (M && !M->isStatic() && !M->isVolatile()) {
    if (M->isConst() && M->getIdentifier() && !M->getNumParams()) {
      if (M->getName() == "intervals" || M->getName() == "densities") {
        const auto Vector = approvedUtilityVectorRecord(S, SM,
            M->getReturnType()->getAsCXXRecordDecl(), Context);
        if (Vector && Context.hasSameType(Vector->ElementType, Info->ResultType))
          Operation = M->getName() == "intervals" ? UtilityOperation::PiecewiseIntervals
                                                : UtilityOperation::PiecewiseDensities;
      } else if (Info->Record == Info->Distribution &&
                 Context.hasSameType(M->getReturnType(), Info->ResultType)) {
        if (M->getName() == "min") Operation = UtilityOperation::PiecewiseMinimum;
        else if (M->getName() == "max") Operation = UtilityOperation::PiecewiseMaximum;
      } else if (M->getName() == "param" && Info->Record == Info->Distribution &&
                 Context.hasSameType(M->getReturnType(), Context.getRecordType(Info->Parameter))) {
        Operation = UtilityOperation::PiecewiseParamGet;
      }
    } else if (!M->isConst() && M->getIdentifier() && M->getReturnType()->isVoidType() &&
               Info->Record == Info->Distribution) {
      if (M->getName() == "reset" && !M->getNumParams())
        Operation = UtilityOperation::PiecewiseReset;
      else if (M->getName() == "param" && M->getNumParams() == 1 &&
          Context.hasSameType(M->getParamDecl(0)->getType(),
              Context.getLValueReferenceType(Context.getRecordType(Info->Parameter).withConst())))
        Operation = UtilityOperation::PiecewiseParamSet;
    }
    if (M->getOverloadedOperator() == OO_Equal && M->getNumParams() == 1 &&
        M->getReturnType()->isLValueReferenceType() &&
        Context.hasSameType(M->getReturnType().getNonReferenceType(), Context.getRecordType(Info->Record)) &&
        M->getParamDecl(0)->getType()->isReferenceType() &&
        Context.hasSameUnqualifiedType(M->getParamDecl(0)->getType().getNonReferenceType(), Context.getRecordType(Info->Record)))
      Operation = UtilityOperation::PiecewiseAssign;
  } else if (!M && F->getReturnType()->isBooleanType() && F->getNumParams() == 2 &&
             (F->getOverloadedOperator() == OO_EqualEqual || F->getOverloadedOperator() == OO_ExclaimEqual)) {
    for (const auto *P : F->parameters())
      if (!Context.hasSameType(P->getType(),
          Context.getLValueReferenceType(Context.getRecordType(Info->Record).withConst())))
        return std::nullopt;
    Operation = F->getOverloadedOperator() == OO_EqualEqual
        ? UtilityOperation::PiecewiseEqual : UtilityOperation::PiecewiseNotEqual;
  }
  RandomSelection Selection(S, SM, Context, Info->Distribution, Info->ResultType, Info->ResultType);
  return Operation && Selection.retain(F) ? Operation : std::nullopt;
}

std::optional<DiscreteDistributionRecord>
approvedDiscreteDistributionRecord(const State &S, const SourceManager &SM,
                                   const CXXRecordDecl *Record,
                                   const ASTContext &Context) {
  const auto *R = Record ? Record->getDefinition() : nullptr;
  const auto *Outer = R && R->getName() == "param_type"
      ? dyn_cast<CXXRecordDecl>(R->getDeclContext()) : R;
  const auto *Specialization = dyn_cast_or_null<ClassTemplateSpecializationDecl>(Outer);
  const auto File = R ? S.sdkFile(SM, R->getLocation()) : std::nullopt;
  if (!S.coreV2() || !R || !Outer || !Specialization || !File ||
      File->Root != "libcxx" || File->Path != "__random/discrete_distribution.h" ||
      Outer->getName() != "discrete_distribution" ||
      !approvedStandardSDKDeclaration(S, SM, Outer) || R->getNumBases() ||
      R->isUnion() || !R->isStandardLayout() ||
      Specialization->getTemplateArgs().size() != 1 ||
      Specialization->getTemplateArgs().get(0).getKind() != TemplateArgument::Type)
    return std::nullopt;
  auto Result = Specialization->getTemplateArgs().get(0).getAsType();
  if (!Result->isIntegerType() || Result->isBooleanType() ||
      Result.hasLocalQualifiers() || Context.getTypeSize(Result) > 64)
    return std::nullopt;
  const CXXRecordDecl *Parameter = nullptr;
  for (const auto *D : Outer->decls())
    if (const auto *Nested = dyn_cast<CXXRecordDecl>(D);
        Nested && Nested->getName() == "param_type" && Nested->getDefinition())
      Parameter = Nested->getDefinition();
  if (!Parameter || (R != Outer && R != Parameter) ||
      !Parameter->isStandardLayout() || Parameter->getNumBases() ||
      !approvedStandardSDKDeclaration(S, SM, Parameter))
    return std::nullopt;
  const FieldDecl *Storage = nullptr, *Thresholds = nullptr;
  for (const auto *F : Parameter->fields()) {
    const auto Vector = approvedUtilityVectorRecord(
        S, SM, F->getType()->getAsCXXRecordDecl(), Context);
    if (Thresholds || F->getName() != "__p_" || F->isBitField() || F->isMutable() ||
        !approvedStandardSDKDeclaration(S, SM, F) || !Vector ||
        !Context.hasSameType(Vector->ElementType, Context.DoubleTy))
      return std::nullopt;
    Thresholds = F;
  }
  for (const auto *F : Outer->fields()) {
    if (Storage || F->getName() != "__p_" || F->isBitField() || F->isMutable() ||
        !approvedStandardSDKDeclaration(S, SM, F) ||
        !Context.hasSameType(F->getType(), Context.getRecordType(Parameter)))
      return std::nullopt;
    Storage = F;
  }
  RandomSelection Selection(S, SM, Context, Outer, Result, Context.DoubleTy);
  if (!Storage || !Thresholds || !Selection.retain(R))
    return std::nullopt;
  return DiscreteDistributionRecord{R, Outer, Parameter, Result, Storage, Thresholds};
}

std::optional<RandomWeightObject>
approvedRandomWeightObjectConstruction(const State &S, const SourceManager &SM,
                                        const CXXConstructExpr *Construction,
                                        const ASTContext &Context) {
  const auto *C = Construction ? Construction->getConstructor() : nullptr;
  const auto Discrete = C ? approvedDiscreteDistributionRecord(S, SM, C->getParent(), Context) : std::nullopt;
  const auto Piecewise = !Discrete && C ? approvedPiecewiseRecord(S, SM, C->getParent(), Context) : std::nullopt;
  if ((!Discrete && !Piecewise) || !C || !C->hasBody() || C->isVariadic() ||
      Construction->getNumArgs() != C->getNumParams() ||
      !approvedStandardSDKDeclaration(S, SM, C))
    return std::nullopt;
  const auto *Distribution = Discrete ? Discrete->Distribution : Piecewise->Distribution;
  const auto Result = Discrete ? Discrete->ResultType : Piecewise->ResultType;
  const auto Real = Discrete ? Context.DoubleTy : Piecewise->ResultType;
  if (C->getNumParams() == 4) {
    if (!Context.hasSameType(C->getParamDecl(0)->getType(), Context.getSizeType()) ||
        !Context.hasSameType(C->getParamDecl(1)->getType(), Real) ||
        !Context.hasSameType(C->getParamDecl(2)->getType(), Real))
      return std::nullopt;
  } else if (Piecewise && C->getNumParams() == 2) {
    const auto List = approvedUtilityInitializerListRecord(S, SM,
        C->getParamDecl(0)->getType()->getAsCXXRecordDecl(), Context);
    if (!List || !Context.hasSameType(List->ElementType, Real))
      return std::nullopt;
    const auto *Argument = dyn_cast<CXXStdInitializerListExpr>(Construction->getArg(0));
    if (Argument && !approvedUtilityInitializerListExpression(S, SM, Argument, Context))
      return std::nullopt;
  } else {
    return std::nullopt;
  }
  const auto Object = C->getParamDecl(C->getNumParams() - 1)->getType();
  const auto *R = Object->getAsCXXRecordDecl(); R = R ? R->getDefinition() : nullptr;
  if (!R || Object.hasLocalQualifiers() || !S.owns(SM, R->getLocation()) ||
      R->isLambda() || R->isUnion() || R->getNumBases() ||
      !R->isStandardLayout())
    return std::nullopt;
  RandomSelection Selection(S, SM, Context, Distribution, Result, Real, true, R);
  const bool Outer = C->getParent()->getCanonicalDecl() == Distribution->getCanonicalDecl();
  if (!Selection.retain(C) || !Selection.WeightMethod || (Outer && !Selection.WeightCopy))
    return std::nullopt;
  if (isa<ClassTemplateSpecializationDecl>(R)) {
    const auto *Pattern = Selection.WeightMethod->getInstantiatedFromMemberFunction();
    const auto *Written = Pattern ? Pattern->getDefinition() : Selection.WeightMethod->getDefinition();
    // Start with source definitions written in a class body. Out-of-line
    // template signatures need their own retained declaration-context proof.
    if (!Written || !Written->getLexicalDeclContext()->isRecord())
      return std::nullopt;
  }
  const auto *D = R->getDestructor();
  if (D && !D->isTrivial()) {
    if (D->isDeleted() || (!ordinaryDestructor(D) && !defaultedLifecycle(D)) || !D->hasBody())
      return std::nullopt;
    for (const auto *Declaration : D->redecls())
      if (!S.owns(SM, Declaration->getLocation())) return std::nullopt;
  }
  return RandomWeightObject{R, Selection.WeightMethod, Selection.WeightCopy, Object};
}

std::optional<DiscreteDistributionConstruction>
approvedDiscreteDistributionConstruction(const State &S, const SourceManager &SM,
                                         const CXXConstructExpr *Construction,
                                         const ASTContext &Context) {
  const auto *C = Construction ? Construction->getConstructor() : nullptr;
  const auto Info = C ? approvedDiscreteDistributionRecord(S, SM, C->getParent(), Context)
                      : std::nullopt;
  if (!Info || C->isVariadic() || !approvedStandardSDKDeclaration(S, SM, C) ||
      Construction->getNumArgs() != C->getNumParams())
    return std::nullopt;
  std::optional<DiscreteDistributionConstruction> Kind;
  if (C->isCopyOrMoveConstructor()) {
    if (!C->isImplicit())
      return std::nullopt;
    Kind = C->isMoveConstructor() ? DiscreteDistributionConstruction::Move
                                  : DiscreteDistributionConstruction::Copy;
  } else if (!C->getNumParams()) {
    Kind = DiscreteDistributionConstruction::Default;
  } else if (C->getNumParams() == 1) {
    if (Info->Record == Info->Distribution && Context.hasSameType(
        C->getParamDecl(0)->getType(), Context.getLValueReferenceType(
            Context.getRecordType(Info->Parameter).withConst()))) {
      Kind = DiscreteDistributionConstruction::Parameter;
    } else if (const auto List = approvedUtilityInitializerListRecord(S, SM,
                   C->getParamDecl(0)->getType()->getAsCXXRecordDecl(), Context);
               List && Context.hasSameType(List->ElementType, Context.DoubleTy)) {
      const auto *Argument = dyn_cast<CXXStdInitializerListExpr>(Construction->getArg(0));
      if (Argument && !approvedUtilityInitializerListExpression(S, SM, Argument, Context))
        return std::nullopt;
      Kind = DiscreteDistributionConstruction::InitializerList;
    }
  } else if (C->getNumParams() == 2 && Context.hasSameType(
                 C->getParamDecl(0)->getType(), C->getParamDecl(1)->getType())) {
    const auto Pointer = seedSequencePointer(S, SM, C->getParamDecl(0)->getType(), Context);
    if (!Pointer.isNull() && !Pointer->getPointeeType().isVolatileQualified() &&
        scalarElement(Pointer->getPointeeType(), Context))
      Kind = DiscreteDistributionConstruction::Range;
  }
  if (C->getNumParams() == 4 &&
      Context.hasSameType(C->getParamDecl(0)->getType(), Context.getSizeType()) &&
      Context.hasSameType(C->getParamDecl(1)->getType(), Context.DoubleTy) &&
      Context.hasSameType(C->getParamDecl(2)->getType(), Context.DoubleTy) &&
      randomWeightFunction(C->getParamDecl(3)->getType(), Context))
    Kind = DiscreteDistributionConstruction::FunctionWeights;
  if (approvedRandomWeightObjectConstruction(S, SM, Construction, Context))
    return DiscreteDistributionConstruction::ObjectWeights;
  RandomSelection Selection(S, SM, Context, Info->Distribution, Info->ResultType, Context.DoubleTy,
      Kind == DiscreteDistributionConstruction::FunctionWeights);
  return Kind && C->hasBody() && Selection.retain(C) ? Kind : std::nullopt;
}

std::optional<UtilityAlgorithmRandomCall>
approvedDiscreteDistributionRandomCall(const State &S, const SourceManager &SM,
                                      const CallExpr *Call,
                                      const ASTContext &Context) {
  const auto *M = Call ? dyn_cast_or_null<CXXMethodDecl>(Call->getDirectCallee()) : nullptr;
  const auto *Operator = dyn_cast_or_null<CXXOperatorCallExpr>(Call);
  const auto Info = M ? approvedDiscreteDistributionRecord(S, SM, M->getParent(), Context)
                      : std::nullopt;
  if (!Info || Info->Record != Info->Distribution || M->isStatic() || M->isConst() ||
      M->isVolatile() || M->isVariadic() || M->getOverloadedOperator() != OO_Call ||
      M->getNumParams() < 1 || M->getNumParams() > 2 ||
      !approvedStandardSDKDeclaration(S, SM, M) ||
      !Context.hasSameType(M->getReturnType(), Info->ResultType) ||
      Call->getNumArgs() != M->getNumParams() + (Operator ? 1u : 0u) ||
      !(Operator || isa<CXXMemberCallExpr>(Call)) ||
      (M->getNumParams() == 2 && !Context.hasSameType(M->getParamDecl(1)->getType(),
          Context.getLValueReferenceType(Context.getRecordType(Info->Parameter).withConst()))))
    return std::nullopt;
  return selectedRandomGenerator(S, SM, M, M->getParamDecl(0)->getType(), Context.DoubleTy,
      UtilityOperation::DiscreteGenerate, Operator ? 1u : 0u, Context);
}

std::optional<UtilityOperation>
approvedDiscreteDistributionOperation(const State &S, const SourceManager &SM,
                                     const CallExpr *Call, const ASTContext &Context) {
  const auto *F = Call ? Call->getDirectCallee() : nullptr;
  const auto *M = dyn_cast_or_null<CXXMethodDecl>(F);
  const auto *Operator = dyn_cast_or_null<CXXOperatorCallExpr>(Call);
  const auto *R = M ? M->getParent() : F && F->getNumParams() == 2
      ? F->getParamDecl(0)->getType().getNonReferenceType()->getAsCXXRecordDecl() : nullptr;
  const auto Info = approvedDiscreteDistributionRecord(S, SM, R, Context);
  if (!Info || !F || F->isVariadic() || !approvedStandardSDKDeclaration(S, SM, F) ||
      Call->getNumArgs() != F->getNumParams() + (Operator && M ? 1u : 0u))
    return std::nullopt;
  if (approvedDiscreteDistributionRandomCall(S, SM, Call, Context))
    return UtilityOperation::DiscreteGenerate;
  std::optional<UtilityOperation> Operation;
  if (M && !M->isStatic() && !M->isVolatile()) {
    if (M->isConst() && M->getIdentifier() && !M->getNumParams()) {
      if (M->getName() == "probabilities") {
        const auto Vector = approvedUtilityVectorRecord(S, SM,
            M->getReturnType()->getAsCXXRecordDecl(), Context);
        if (Vector && Context.hasSameType(Vector->ElementType, Context.DoubleTy))
          Operation = UtilityOperation::DiscreteProbabilities;
      } else if (Info->Record == Info->Distribution &&
                 Context.hasSameType(M->getReturnType(), Info->ResultType)) {
        if (M->getName() == "min") Operation = UtilityOperation::DiscreteMinimum;
        else if (M->getName() == "max") Operation = UtilityOperation::DiscreteMaximum;
      } else if (M->getName() == "param" && Info->Record == Info->Distribution &&
                 Context.hasSameType(M->getReturnType(), Context.getRecordType(Info->Parameter))) {
        Operation = UtilityOperation::DiscreteParamGet;
      }
    } else if (!M->isConst() && M->getIdentifier() && M->getReturnType()->isVoidType() &&
               Info->Record == Info->Distribution) {
      if (M->getName() == "reset" && !M->getNumParams())
        Operation = UtilityOperation::DiscreteReset;
      else if (M->getName() == "param" && M->getNumParams() == 1 &&
          Context.hasSameType(M->getParamDecl(0)->getType(),
              Context.getLValueReferenceType(Context.getRecordType(Info->Parameter).withConst())))
        Operation = UtilityOperation::DiscreteParamSet;
    }
    if (M->getOverloadedOperator() == OO_Equal && M->isImplicit() &&
        M->getNumParams() == 1 && M->getReturnType()->isLValueReferenceType() &&
        Context.hasSameType(M->getReturnType().getNonReferenceType(), Context.getRecordType(Info->Record)) &&
        M->getParamDecl(0)->getType()->isReferenceType() &&
        Context.hasSameUnqualifiedType(M->getParamDecl(0)->getType().getNonReferenceType(), Context.getRecordType(Info->Record)))
      Operation = UtilityOperation::DiscreteAssign;
  } else if (!M && F->getReturnType()->isBooleanType() && F->getNumParams() == 2 &&
             (F->getOverloadedOperator() == OO_EqualEqual || F->getOverloadedOperator() == OO_ExclaimEqual)) {
    for (const auto *P : F->parameters())
      if (!Context.hasSameType(P->getType(),
          Context.getLValueReferenceType(Context.getRecordType(Info->Record).withConst())))
        return std::nullopt;
    Operation = F->getOverloadedOperator() == OO_EqualEqual
        ? UtilityOperation::DiscreteEqual : UtilityOperation::DiscreteNotEqual;
  }
  RandomSelection Selection(S, SM, Context, Info->Distribution, Info->ResultType, Context.DoubleTy);
  return Operation && Selection.retain(F) ? Operation : std::nullopt;
}

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
  const auto InputIterator = F->getParamDecl(0)->getType();
  const auto OutputIterator = F->getParamDecl(Sample ? 2 : 0)->getType();
  auto RangePointer = [&](QualType Iterator) {
    if (const auto Wrapped = approvedUtilityWrapIteratorRecord(
            S, SM, Iterator->getAsCXXRecordDecl(), Context))
      return Wrapped->IteratorType;
    return Iterator;
  };
  const auto Input = RangePointer(InputIterator);
  const auto Output = RangePointer(OutputIterator);
  const auto Object = F->getParamDecl(Index)->getType();
  const auto *Generator =
      Object.getNonReferenceType()->getAsCXXRecordDecl();
  Generator = Generator ? Generator->getDefinition() : nullptr;
  const auto Engine = approvedRandomEngineRecord(S, SM, Generator, Context);
  if (!Input->isPointerType() || !Output->isPointerType() ||
      !scalarElement(Input->getPointeeType(), Context) ||
      !scalarElement(Output->getPointeeType(), Context) ||
      Output->getPointeeType().isConstQualified() ||
      !Context.hasSameType(F->getParamDecl(1)->getType(), InputIterator) ||
      !Object->isReferenceType() ||
      Object.getNonReferenceType().isVolatileQualified() || !Generator ||
      Generator->isLambda() || Generator->isUnion() ||
      !Generator->isStandardLayout() || !Generator->isTriviallyCopyable() ||
      !Generator->hasTrivialDestructor() ||
      (!Engine && !S.owns(SM, Generator->getLocation())) ||
      !Context.hasSameType(Call->getType(), F->getReturnType()) ||
      (Sample ? !Context.hasSameType(F->getReturnType(), OutputIterator)
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
      (Engine ? !approvedStandardSDKDeclaration(S, SM, ResultAlias)
              : !S.owns(SM, ResultAlias->getLocation())))
    return std::nullopt;
  RandomSelection Selection(S, SM, Context, Generator, Result, Distance);
  if (!Selection.retain(F) || !Selection.Method || !Selection.Minimum ||
      !Selection.Maximum || Selection.MinimumValue >= Selection.MaximumValue ||
      (Engine && (Selection.MinimumValue != Engine->Minimum ||
                  Selection.MaximumValue != Engine->Maximum)))
    return std::nullopt;
  return UtilityAlgorithmRandomCall{
      Sample ? UtilityOperation::AlgorithmSample
             : UtilityOperation::AlgorithmShuffle,
      Selection.Method, Selection.Minimum, Selection.Maximum,
      Object, Result, Distance, Selection.MinimumValue, Selection.MaximumValue,
      Index};
}
} // namespace nct
