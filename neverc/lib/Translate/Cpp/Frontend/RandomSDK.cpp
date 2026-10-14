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
  bool SDKGenerator;
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
  bool engineIntrinsic(const FunctionDecl *F) const {
    if (!SDKGenerator || !F || !F->isImplicit())
      return false;
    // The pinned MT equality body uses libc++'s scalar-array equality path.
    // These compiler intrinsics are proof dependencies, never emitted calls.
    return F->getBuiltinID() == Builtin::BI__builtin_is_constant_evaluated ||
           F->getBuiltinID() == Builtin::BI__builtin_memcmp;
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
        SDKGenerator(approvedStandardSDKDeclaration(S, SM, Generator)),
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
    if (countLeadingZeros(F) || engineIntrinsic(F))
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
    if (engineIntrinsic(F))
      return F->getBuiltinID() == Builtin::BI__builtin_is_constant_evaluated
                 ? E->getNumArgs() == 0 && E->getType()->isBooleanType()
                 : E->getNumArgs() == 3 &&
                       E->getType()->isSpecificBuiltinType(BuiltinType::Int);
    if (S.owns(SM, F->getLocation())) {
      const auto *M = dyn_cast<CXXMethodDecl>(F);
      return M && sourceMethod(M, E);
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
  const bool Linear = Template->getName() == "linear_congruential_engine" &&
                      File->Path == "__random/linear_congruential_engine.h";
  const bool Twister = Template->getName() == "mersenne_twister_engine" &&
                       File->Path == "__random/mersenne_twister_engine.h";
  const auto &Arguments = R->getTemplateArgs();
  if ((!Linear && !Twister) || Arguments.size() != (Linear ? 4u : 14u) ||
      Arguments.get(0).getKind() != TemplateArgument::Type)
    return std::nullopt;
  const auto Result = Arguments.get(0).getAsType();
  if (Result.hasLocalQualifiers() || !Result->isUnsignedIntegerType() ||
      Result->isBooleanType() || Context.getTypeSize(Result) > 64)
    return std::nullopt;
  std::vector<uint64_t> Parameters;
  for (unsigned I = 1; I < Arguments.size(); ++I) {
    const auto &Argument = Arguments.get(I);
    if (Argument.getKind() != TemplateArgument::Integral ||
        Argument.getAsIntegral().isNegative() ||
        Argument.getAsIntegral().getBitWidth() > 64)
      return std::nullopt;
    Parameters.push_back(Argument.getAsIntegral().getZExtValue());
  }
  const unsigned Bits = Context.getTypeSize(Result);
  uint64_t Minimum, Maximum;
  if (Linear) {
    const auto Modulus = Parameters[2];
    if (Modulus && (Parameters[0] >= Modulus || Parameters[1] >= Modulus))
      return std::nullopt;
    Minimum = Parameters[1] == 0 ? 1 : 0;
    Maximum = (llvm::APInt(Bits, Modulus) - 1).getZExtValue();
  } else {
    const auto Width = Parameters[0], Size = Parameters[1];
    if (Width < 2 || Width > Bits || !Size || Size > 65536 ||
        Parameters[2] > Size || Parameters[3] > Width ||
        Parameters[5] > Width || Parameters[7] > Width ||
        Parameters[9] > Width || Parameters[11] > Width)
      return std::nullopt;
    Minimum = 0;
    Maximum = llvm::APInt::getLowBitsSet(Bits, Width).getZExtValue();
  }
  if (Minimum >= Maximum)
    return std::nullopt;
  const FieldDecl *State = nullptr, *Index = nullptr;
  unsigned Fields = 0;
  for (const auto *F : R->fields()) {
    ++Fields;
    if (!approvedStandardSDKDeclaration(S, SM, F) || F->isBitField() ||
        F->isMutable())
      return std::nullopt;
    if (F->getName() == "__x_") {
      if (Linear ? !Context.hasSameType(F->getType(), Result)
                 : !Context.getAsConstantArrayType(F->getType()) ||
                       Context.getAsConstantArrayType(F->getType())->getSize() !=
                           Parameters[1] ||
                       !Context.hasSameType(
                           Context.getAsConstantArrayType(F->getType())->getElementType(),
                           Result))
        return std::nullopt;
      State = F;
    } else if (Twister && F->getName() == "__i_" &&
               Context.hasSameType(F->getType(), Context.getSizeType())) {
      Index = F;
    } else {
      return std::nullopt;
    }
  }
  RandomSelection Selection(S, SM, Context, R, Result, Result);
  if (!State || Fields != (Linear ? 1u : 2u) || (Twister && !Index) ||
      !Selection.retain(R))
    return std::nullopt;
  return RandomEngineRecord{
      Linear ? RandomEngineKind::LinearCongruential
             : RandomEngineKind::MersenneTwister,
      R, Result, State, Index, std::move(Parameters), Minimum, Maximum,
      Linear ? 1u : 5489u};
}

std::optional<RandomEngineConstruction>
approvedRandomEngineConstruction(const State &S, const SourceManager &SM,
                                 const CXXConstructExpr *Construction,
                                 const ASTContext &Context) {
  const auto *C = Construction ? Construction->getConstructor() : nullptr;
  const auto Engine = C ? approvedRandomEngineRecord(S, SM, C->getParent(), Context)
                        : std::nullopt;
  if (!Engine || C->isVariadic() || C->getPrimaryTemplate() ||
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
  if (!C->hasBody() || C->getNumParams() > 1 ||
      (C->getNumParams() &&
       !Context.hasSameType(C->getParamDecl(0)->getType(), Engine->ResultType)))
    return std::nullopt;
  RandomSelection Selection(S, SM, Context, Engine->Record, Engine->ResultType,
                            Engine->ResultType);
  if (!Selection.retain(C))
    return std::nullopt;
  return C->getNumParams() ? RandomEngineConstruction::Seed
                           : RandomEngineConstruction::Default;
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
             !M->isVolatile() && M->getNumParams() == 1 &&
             M->getReturnType()->isVoidType()) {
      if (M->getName() == "seed" &&
          Context.hasSameType(M->getParamDecl(0)->getType(), Engine->ResultType))
        Operation = UtilityOperation::RandomEngineSeed;
      else if (M->getName() == "discard" &&
               Context.hasSameType(M->getParamDecl(0)->getType(),
                                   Context.UnsignedLongLongTy))
        Operation = UtilityOperation::RandomEngineDiscard;
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
