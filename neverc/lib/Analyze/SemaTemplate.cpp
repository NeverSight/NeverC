//===--- SemaTemplate.cpp - template instantiation scaffolding ------------===//
#include "neverc/Analyze/Template.h"
#include "neverc/Analyze/Sema.h"
#include "neverc/Tree/Decl/Decl.h"
#include "neverc/Tree/Decl/CXXBaseSpecifier.h"
#include "neverc/Tree/Decl/DeclTemplate.h"
#include "neverc/Tree/Type/Type.h"
#include "neverc/Tree/Stmt/Stmt.h"
#include "neverc/Tree/Expr/Expr.h"
#include "llvm/ADT/SmallVector.h"

using namespace neverc;

namespace {

QualType substTypeRecursive(Sema &S, QualType T,
                            const MultiLevelTemplateArgumentList &Args) {
  if (T.isNull())
    return T;

  const Type *Ty = T.getTypePtr();

  if (const auto *TTP = dyn_cast<TemplateTypeParmType>(Ty)) {
    unsigned Depth = TTP->getDepth();
    unsigned Idx = TTP->getIndex();
    if (Depth < Args.getNumLevels()) {
      auto Level = Args.get(Depth);
      if (Idx < Level.size() && Level[Idx].isType()) {
        QualType Repl = Level[Idx].getAsType();
        return S.Context.getQualifiedType(Repl.getUnqualifiedType(),
                                          T.getQualifiers());
      }
    }
    return T;
  }

  if (const auto *PT = dyn_cast<PointerType>(Ty)) {
    QualType Pointee = substTypeRecursive(S, PT->getPointeeType(), Args);
    if (Pointee.isNull())
      return QualType();
    QualType Result = S.Context.getPointerType(Pointee);
    return S.Context.getQualifiedType(Result.getUnqualifiedType(),
                                      T.getQualifiers());
  }

  if (const auto *LRT = dyn_cast<LValueReferenceType>(Ty)) {
    QualType Pointee = substTypeRecursive(S, LRT->getPointeeType(), Args);
    if (Pointee.isNull())
      return QualType();
    return S.Context.getLValueReferenceType(Pointee);
  }

  if (const auto *RRT = dyn_cast<RValueReferenceType>(Ty)) {
    QualType Pointee = substTypeRecursive(S, RRT->getPointeeType(), Args);
    if (Pointee.isNull())
      return QualType();
    return S.Context.getRValueReferenceType(Pointee);
  }

  if (const auto *AT = dyn_cast<ArrayType>(Ty)) {
    // Array substitution: rebuild as pointer-to-element for Phase-3 safety
    // (full array size expr subst needs ArraySizeModifier wiring).
    QualType Elem = substTypeRecursive(S, AT->getElementType(), Args);
    if (Elem.isNull())
      return QualType();
    QualType Result = S.Context.getPointerType(Elem);
    return S.Context.getQualifiedType(Result.getUnqualifiedType(),
                                      T.getQualifiers());
  }

  if (const auto *FPT = dyn_cast<FunctionProtoType>(Ty)) {
    QualType Ret = substTypeRecursive(S, FPT->getReturnType(), Args);
    if (Ret.isNull())
      return QualType();
    llvm::SmallVector<QualType, 8> Params;
    for (QualType P : FPT->getParamTypes()) {
      QualType SP = substTypeRecursive(S, P, Args);
      if (SP.isNull())
        return QualType();
      Params.push_back(SP);
    }
    return S.Context.getFunctionType(Ret, Params, FPT->getExtProtoInfo());
  }

  if (const auto *FNP = dyn_cast<FunctionNoProtoType>(Ty)) {
    QualType Ret = substTypeRecursive(S, FNP->getReturnType(), Args);
    if (Ret.isNull())
      return QualType();
    return S.Context.getFunctionNoProtoType(Ret, FNP->getExtInfo());
  }

  if (const auto *ATY = dyn_cast<AtomicType>(Ty)) {
    QualType Val = substTypeRecursive(S, ATY->getValueType(), Args);
    if (Val.isNull())
      return QualType();
    return S.Context.getAtomicType(Val);
  }

  if (const auto *PaT = dyn_cast<ParenType>(Ty)) {
    QualType Inner = substTypeRecursive(S, PaT->getInnerType(), Args);
    if (Inner.isNull())
      return QualType();
    return S.Context.getParenType(Inner);
  }

  if (const auto *DecT = dyn_cast<DecayedType>(Ty)) {
    return substTypeRecursive(S, DecT->getPointeeType(), Args);
  }

  (void)S;
  return T;
}

} // namespace


namespace {

Expr *substExprRecursive(Sema &S, Expr *E,
                         const MultiLevelTemplateArgumentList &Args,
                         SourceLocation POI) {
  if (!E)
    return nullptr;
  (void)POI;
  // Type-carrying leaves: rebuild DeclRef with substituted type when possible.
  if (auto *DRE = dyn_cast<DeclRefExpr>(E)) {
    QualType Ty = S.SubstType(DRE->getType(), Args, E->getBeginLoc(),
                              DeclarationName());
    if (Ty.isNull() || Ty == DRE->getType())
      return E;
    if (ValueDecl *VD = DRE->getDecl()) {
      return DeclRefExpr::Create(S.Context, VD, DRE->getLocation(), Ty,
                                 DRE->getValueKind());
    }
    return E;
  }
  if (auto *ILE = dyn_cast<IntegerLiteral>(E)) {
    (void)ILE;
    return E;
  }
  if (isa<CharacterLiteral>(E) || isa<FloatingLiteral>(E) ||
      isa<NullPtrLiteralExpr>(E) || isa<StringLiteral>(E) ||
      isa<ImaginaryLiteral>(E) || isa<FixedPointLiteral>(E)) {
    return E;
  }
  if (auto *BO = dyn_cast<BinaryOperator>(E)) {
    Expr *LHS = substExprRecursive(S, BO->getLHS(), Args, POI);
    Expr *RHS = substExprRecursive(S, BO->getRHS(), Args, POI);
    if (LHS == BO->getLHS() && RHS == BO->getRHS())
      return E;
    QualType Ty = S.SubstType(BO->getType(), Args, BO->getOperatorLoc(),
                              DeclarationName());
    if (Ty.isNull())
      Ty = BO->getType();
    return BinaryOperator::Create(S.Context, LHS, RHS, BO->getOpcode(), Ty,
                                  BO->getValueKind(), BO->getObjectKind(),
                                  BO->getOperatorLoc(), FPOptionsOverride());
  }
  if (auto *UO = dyn_cast<UnaryOperator>(E)) {
    Expr *Sub = substExprRecursive(S, UO->getSubExpr(), Args, POI);
    if (Sub == UO->getSubExpr())
      return E;
    QualType Ty = S.SubstType(UO->getType(), Args, UO->getOperatorLoc(),
                              DeclarationName());
    if (Ty.isNull())
      Ty = UO->getType();
    return UnaryOperator::Create(S.Context, Sub, UO->getOpcode(), Ty,
                                 UO->getValueKind(), UO->getObjectKind(),
                                 UO->getOperatorLoc(), UO->canOverflow(),
                                 FPOptionsOverride());
  }
  if (auto *PE = dyn_cast<ParenExpr>(E)) {
    Expr *Inner = substExprRecursive(S, PE->getSubExpr(), Args, POI);
    if (Inner == PE->getSubExpr())
      return E;
    return new (S.Context) ParenExpr(PE->getLParen(), PE->getRParen(), Inner);
  }
  if (auto *CE = dyn_cast<CStyleCastExpr>(E)) {
    Expr *Sub = substExprRecursive(S, CE->getSubExpr(), Args, POI);
    QualType Ty = S.SubstType(CE->getType(), Args, CE->getBeginLoc(),
                              DeclarationName());
    if (Ty.isNull())
      Ty = CE->getType();
    if (Sub == CE->getSubExpr() && Ty == CE->getType())
      return E;
    return CStyleCastExpr::Create(S.Context, Ty, CE->getValueKind(),
                                  CE->getCastKind(), Sub, FPOptionsOverride(),
                                  /*WrittenTy=*/nullptr, CE->getLParenLoc(),
                                  CE->getRParenLoc());
  }
  if (auto *ICE = dyn_cast<ImplicitCastExpr>(E)) {
    Expr *Sub = substExprRecursive(S, ICE->getSubExpr(), Args, POI);
    QualType Ty = S.SubstType(ICE->getType(), Args, ICE->getBeginLoc(),
                              DeclarationName());
    if (Ty.isNull())
      Ty = ICE->getType();
    if (Sub == ICE->getSubExpr() && Ty == ICE->getType())
      return E;
    return ImplicitCastExpr::Create(S.Context, Ty, ICE->getCastKind(), Sub,
                                    ICE->getValueKind(), FPOptionsOverride());
  }
  if (auto *CO = dyn_cast<ConditionalOperator>(E)) {
    Expr *C = substExprRecursive(S, CO->getCond(), Args, POI);
    Expr *L = substExprRecursive(S, CO->getLHS(), Args, POI);
    Expr *R = substExprRecursive(S, CO->getRHS(), Args, POI);
    if (C == CO->getCond() && L == CO->getLHS() && R == CO->getRHS())
      return E;
    QualType Ty = S.SubstType(CO->getType(), Args, CO->getQuestionLoc(),
                              DeclarationName());
    if (Ty.isNull())
      Ty = CO->getType();
    return new (S.Context) ConditionalOperator(
        C, CO->getQuestionLoc(), L, CO->getColonLoc(), R, Ty,
        CO->getValueKind(), CO->getObjectKind());
  }
  if (auto *ASE = dyn_cast<ArraySubscriptExpr>(E)) {
    Expr *L = substExprRecursive(S, ASE->getLHS(), Args, POI);
    Expr *R = substExprRecursive(S, ASE->getRHS(), Args, POI);
    if (L == ASE->getLHS() && R == ASE->getRHS())
      return E;
    QualType Ty = S.SubstType(ASE->getType(), Args, ASE->getRBracketLoc(),
                              DeclarationName());
    if (Ty.isNull())
      Ty = ASE->getType();
    return new (S.Context) ArraySubscriptExpr(
        L, R, Ty, ASE->getValueKind(), ASE->getObjectKind(),
        ASE->getRBracketLoc());
  }
  if (auto *TE = dyn_cast<CXXThisExpr>(E)) {
    QualType Ty = S.SubstType(TE->getType(), Args, TE->getLocation(),
                              DeclarationName());
    if (Ty.isNull() || Ty == TE->getType())
      return E;
    return new (S.Context) CXXThisExpr(TE->getLocation(), Ty, TE->isImplicit());
  }
  if (auto *OCE = dyn_cast<CXXOperatorCallExpr>(E)) {
    Expr *Callee = substExprRecursive(S, OCE->getCallee(), Args, POI);
    Expr *A0 = OCE->getNumArgs() > 0
                   ? substExprRecursive(S, OCE->getArg(0), Args, POI)
                   : nullptr;
    Expr *A1 = OCE->getNumArgs() > 1
                   ? substExprRecursive(S, OCE->getArg(1), Args, POI)
                   : nullptr;
    QualType Ty = S.SubstType(OCE->getType(), Args, OCE->getOperatorLoc(),
                              DeclarationName());
    if (Ty.isNull())
      Ty = OCE->getType();
    if (Callee == OCE->getCallee() &&
        (OCE->getNumArgs() == 0 || A0 == OCE->getArg(0)) &&
        (OCE->getNumArgs() < 2 || A1 == OCE->getArg(1)) && Ty == OCE->getType())
      return E;
    return new (S.Context)
        CXXOperatorCallExpr(Ty, OCE->getOperatorLoc(), Callee, A0, A1);
  }
  if (auto *ME = dyn_cast<MemberExpr>(E)) {
    Expr *Base = substExprRecursive(S, ME->getBase(), Args, POI);
    QualType Ty = S.SubstType(ME->getType(), Args, ME->getMemberLoc(),
                              DeclarationName());
    if (Ty.isNull())
      Ty = ME->getType();
    if (Base == ME->getBase() && Ty == ME->getType())
      return E;
    DeclarationNameInfo NameInfo(ME->getMemberDecl()->getDeclName(),
                                 ME->getMemberLoc());
    return MemberExpr::Create(S.Context, Base, ME->isArrow(),
                              ME->getOperatorLoc(), ME->getMemberDecl(),
                              /*FoundDecl=*/nullptr, NameInfo, Ty,
                              ME->getValueKind(), ME->getObjectKind(),
                              NOUR_None);
  }
  // Unhandled shapes share the pattern expression.
  return E;
}

Stmt *substStmtRecursive(Sema &S, Stmt *St,
                         const MultiLevelTemplateArgumentList &Args,
                         SourceLocation POI) {
  if (!St)
    return nullptr;
  (void)Args;
  (void)POI;

  switch (St->getStmtClass()) {
  case Stmt::NullStmtClass:
    return new (S.Context) NullStmt(St->getBeginLoc());

  case Stmt::CompoundStmtClass: {
    auto *CS = cast<CompoundStmt>(St);
    llvm::SmallVector<Stmt *, 8> NewBody;
    for (Stmt *C : CS->body())
      NewBody.push_back(substStmtRecursive(S, C, Args, POI));
    FPOptionsOverride FP;
    return CompoundStmt::Create(S.Context, NewBody, FP, CS->getLBracLoc(),
                                CS->getRBracLoc());
  }

  case Stmt::ReturnStmtClass: {
    auto *RS = cast<ReturnStmt>(St);
    Expr *Ret = const_cast<Expr *>(RS->getRetValue());
    Ret = substExprRecursive(S, Ret, Args, POI);
    return ReturnStmt::Create(S.Context, RS->getReturnLoc(), Ret,
                              /*NRVOCandidate=*/nullptr);
  }

  case Stmt::DeclStmtClass: {
    // Declaration statements share the pattern DeclStmt; deep VarDecl
    // substitution is handled at InstantiateFunction level for params.
    return St;
  }

  case Stmt::BreakStmtClass:
    return new (S.Context) BreakStmt(St->getBeginLoc());

  case Stmt::ContinueStmtClass:
    return new (S.Context) ContinueStmt(St->getBeginLoc());

  case Stmt::CaseStmtClass: {
    auto *CS = cast<CaseStmt>(St);
    Expr *LHS = substExprRecursive(
        S, const_cast<Expr *>(CS->getLHS()), Args, POI);
    Expr *RHS = CS->getRHS()
                    ? substExprRecursive(S, const_cast<Expr *>(CS->getRHS()),
                                         Args, POI)
                    : nullptr;
    Stmt *Sub = substStmtRecursive(S, CS->getSubStmt(), Args, POI);
    CaseStmt *NewCS = CaseStmt::Create(S.Context, LHS, RHS, CS->getCaseLoc(),
                                       CS->getEllipsisLoc(), CS->getColonLoc());
    NewCS->setSubStmt(Sub);
    return NewCS;
  }

  case Stmt::DefaultStmtClass: {
    auto *DS = cast<DefaultStmt>(St);
    Stmt *Sub = substStmtRecursive(S, DS->getSubStmt(), Args, POI);
    return new (S.Context)
        DefaultStmt(DS->getDefaultLoc(), DS->getColonLoc(), Sub);
  }

  case Stmt::IfStmtClass: {
    auto *IS = cast<IfStmt>(St);
    Expr *Cond = substExprRecursive(
        S, const_cast<Expr *>(IS->getCond()), Args, POI);
    Stmt *Then = substStmtRecursive(S, IS->getThen(), Args, POI);
    Stmt *Else = IS->getElse()
                     ? substStmtRecursive(S, IS->getElse(), Args, POI)
                     : nullptr;
    if (Cond == IS->getCond() && Then == IS->getThen() &&
        Else == IS->getElse())
      return St;
    return IfStmt::Create(S.Context, IS->getIfLoc(),
                          /*Init=*/IS->getInit(),
                          /*Var=*/IS->getConditionVariable(), Cond,
                          IS->getLParenLoc(), IS->getRParenLoc(), Then,
                          IS->getElseLoc(), Else);
  }

  case Stmt::WhileStmtClass: {
    auto *WS = cast<WhileStmt>(St);
    Expr *Cond = substExprRecursive(
        S, const_cast<Expr *>(WS->getCond()), Args, POI);
    Stmt *Body = substStmtRecursive(S, WS->getBody(), Args, POI);
    if (Cond == WS->getCond() && Body == WS->getBody())
      return St;
    // WhileStmt::Create may be heavy; rebuild with available ctor path via
    // Create if present, else keep original when only trivial.
    return WhileStmt::Create(S.Context, WS->getConditionVariable(), Cond, Body,
                             WS->getWhileLoc(), WS->getLParenLoc(),
                             WS->getRParenLoc());
  }

  case Stmt::ForStmtClass: {
    auto *FS = cast<ForStmt>(St);
    Stmt *Init = FS->getInit()
                     ? substStmtRecursive(S, FS->getInit(), Args, POI)
                     : nullptr;
    Expr *Cond = substExprRecursive(
        S, const_cast<Expr *>(FS->getCond()), Args, POI);
    Expr *Inc = substExprRecursive(
        S, const_cast<Expr *>(FS->getInc()), Args, POI);
    Stmt *Body = substStmtRecursive(S, FS->getBody(), Args, POI);
    if (Init == FS->getInit() && Cond == FS->getCond() && Inc == FS->getInc() &&
        Body == FS->getBody())
      return St;
    return new (S.Context) ForStmt(S.Context, Init, Cond,
                                   FS->getConditionVariable(), Inc, Body,
                                   FS->getForLoc(), FS->getLParenLoc(),
                                   FS->getRParenLoc());
  }

  case Stmt::DoStmtClass: {
    auto *DS = cast<DoStmt>(St);
    Stmt *Body = substStmtRecursive(S, DS->getBody(), Args, POI);
    Expr *Cond = substExprRecursive(
        S, const_cast<Expr *>(DS->getCond()), Args, POI);
    if (Body == DS->getBody() && Cond == DS->getCond())
      return St;
    return new (S.Context) DoStmt(Body, Cond, DS->getDoLoc(),
                                  DS->getWhileLoc(), DS->getRParenLoc());
  }

  case Stmt::SwitchStmtClass: {
    auto *SS = cast<SwitchStmt>(St);
    Expr *Cond = substExprRecursive(
        S, const_cast<Expr *>(SS->getCond()), Args, POI);
    Stmt *Body = substStmtRecursive(S, SS->getBody(), Args, POI);
    if (Cond == SS->getCond() && Body == SS->getBody())
      return St;
    SwitchStmt *NewSS = SwitchStmt::Create(
        S.Context, SS->getInit(), SS->getConditionVariable(), Cond,
        SS->getLParenLoc(), SS->getRParenLoc());
    NewSS->setBody(Body);
    return NewSS;
  }

  case Stmt::CXXForRangeStmtClass:
  case Stmt::CXXTryStmtClass:
    // Heavier shapes still share the pattern node.
    return St;

  default:
    // Expression-statements: substitute the expression when possible.
    if (auto *ES = dyn_cast<Expr>(St)) {
      Expr *NE = substExprRecursive(S, ES, Args, POI);
      return NE ? static_cast<Stmt *>(NE) : St;
    }
    return St;
  }
}

} // namespace

QualType Sema::SubstType(QualType T, const MultiLevelTemplateArgumentList &Args,
                         SourceLocation Loc, DeclarationName Entity) {
  (void)Loc;
  (void)Entity;
  if (T.isNull() || Args.empty())
    return T;
  return substTypeRecursive(*this, T, Args);
}

ExprResult Sema::SubstExpr(Expr *E, const MultiLevelTemplateArgumentList &Args) {
  if (!E || Args.empty())
    return E;
  Expr *R = substExprRecursive(*this, E, Args, E->getBeginLoc());
  return R;
}

FunctionDecl *Sema::InstantiateFunctionTemplate(
    FunctionTemplateDecl *FTD, const MultiLevelTemplateArgumentList &Args,
    SourceLocation PointOfInstantiation) {
  if (!FTD)
    return nullptr;
  FunctionDecl *Pattern = FTD->getTemplatedDecl();
  if (!Pattern)
    return nullptr;

  QualType NewTy =
      SubstType(Pattern->getType(), Args, PointOfInstantiation,
                Pattern->getDeclName());
  if (NewTy.isNull())
    NewTy = Pattern->getType();

  // Create a specialization FunctionDecl in the same context.
  DeclarationNameInfo NameInfo(Pattern->getDeclName(), PointOfInstantiation);
  FunctionDecl *Spec = FunctionDecl::Create(
      Context, Pattern->getDeclContext(), PointOfInstantiation, NameInfo, NewTy,
      /*TInfo=*/nullptr, Pattern->getStorageClass(),
      /*UsesFPIntrin=*/false, Pattern->isInlineSpecified(),
      /*hasWrittenPrototype=*/true, Pattern->getConstexprKind());

  // Parameter substitution (types only; names copied).
  if (const auto *Proto = NewTy->getAs<FunctionProtoType>()) {
    llvm::SmallVector<ParmVarDecl *, 8> NewParams;
    unsigned N = Proto->getNumParams();
    unsigned PatternN = Pattern->getNumParams();
    for (unsigned I = 0; I < N; ++I) {
      QualType PT = Proto->getParamType(I);
      IdentifierInfo *II = nullptr;
      SourceLocation PL;
      if (I < PatternN) {
        if (ParmVarDecl *OldP = Pattern->getParamDecl(I)) {
          II = OldP->getIdentifier();
          PL = OldP->getLocation();
        }
      }
      if (PL.isInvalid())
        PL = PointOfInstantiation;
      ParmVarDecl *PVD = ParmVarDecl::Create(
          Context, Spec, PL, PL, II, PT, /*TInfo=*/nullptr, SC_None,
          /*DefArg=*/nullptr);
      NewParams.push_back(PVD);
    }
    Spec->setParams(NewParams);
  }

  // Body instantiation: shallow attach of the pattern body so the specialization
  // is a definition for codegen. Full SubstStmt/dependent Expr rebuild remains
  // incremental (dependent names inside the body are not rewritten yet).
  if (Stmt *Body = Pattern->getBody()) {
    // Rebuild simple statement shapes; complex/dependent stmts share pattern.
    Stmt *NewBody = substStmtRecursive(*this, Body, Args, PointOfInstantiation);
    Spec->setBody(NewBody ? NewBody : Body);
  }

  Pattern->getDeclContext()->addDecl(Spec);
  return Spec;
}

CXXRecordDecl *Sema::InstantiateClassTemplate(
    ClassTemplateDecl *CTD, const MultiLevelTemplateArgumentList &Args,
    SourceLocation PointOfInstantiation) {
  if (!CTD)
    return nullptr;
  CXXRecordDecl *Pattern = CTD->getTemplatedDecl();
  if (!Pattern)
    return nullptr;

  // Create a specialization record and subst member method types.
  CXXRecordDecl *Spec = CXXRecordDecl::Create(
      Context, Pattern->getTagKind(), Pattern->getDeclContext(),
      PointOfInstantiation, PointOfInstantiation, Pattern->getIdentifier());
  Spec->startDefinition();
  if (Pattern->isDynamicClass())
    Spec->setPolymorphic(true);

  // Clone methods / ctors / dtors with substituted types and body rewrite.
  const CXXRecordDecl *PatternDef =
      Pattern->getDefinition() ? Pattern->getDefinition() : Pattern;
  for (Decl *D : PatternDef->decls()) {
    auto *MD = dyn_cast<CXXMethodDecl>(D);
    if (!MD)
      continue;

    QualType NewTy =
        SubstType(MD->getType(), Args, PointOfInstantiation, MD->getDeclName());
    if (NewTy.isNull())
      NewTy = MD->getType();

    DeclarationNameInfo NameInfo(MD->getDeclName(), PointOfInstantiation);
    CXXMethodDecl *NewMD = nullptr;
    if (isa<CXXConstructorDecl>(MD)) {
      NewMD = CXXConstructorDecl::Create(
          Context, Spec, PointOfInstantiation, NameInfo, NewTy,
          /*TInfo=*/nullptr, /*isExplicit=*/false, MD->isInlineSpecified(),
          /*isImplicitlyDeclared=*/false, MD->getConstexprKind());
    } else if (isa<CXXDestructorDecl>(MD)) {
      NewMD = CXXDestructorDecl::Create(
          Context, Spec, PointOfInstantiation, NameInfo, NewTy,
          /*TInfo=*/nullptr, MD->isInlineSpecified(),
          /*isImplicitlyDeclared=*/false);
    } else {
      NewMD = CXXMethodDecl::Create(
          Context, Spec, PointOfInstantiation, NameInfo, NewTy, /*TInfo=*/nullptr,
          MD->getStorageClass(), /*UsesFPIntrin=*/false, MD->isInlineSpecified(),
          MD->getConstexprKind());
    }
    if (MD->isVirtualAsWritten())
      NewMD->setVirtualAsWritten(true);
    if (MD->isPureVirtual())
      NewMD->setPureVirtual(true);
    // Rebuild simple statement shapes for method specializations.
    if (Stmt *Body = MD->getBody()) {
      Stmt *NB = substStmtRecursive(*this, Body, Args, PointOfInstantiation);
      NewMD->setBody(NB ? NB : Body);
    }

    if (const auto *Proto = NewTy->getAs<FunctionProtoType>()) {
      llvm::SmallVector<ParmVarDecl *, 8> NewParams;
      unsigned N = Proto->getNumParams();
      unsigned PatternN = MD->getNumParams();
      for (unsigned I = 0; I < N; ++I) {
        QualType PT = Proto->getParamType(I);
        IdentifierInfo *II = nullptr;
        SourceLocation PL = PointOfInstantiation;
        if (I < PatternN) {
          if (ParmVarDecl *OldP = MD->getParamDecl(I)) {
            II = OldP->getIdentifier();
            PL = OldP->getLocation();
          }
        }
        NewParams.push_back(ParmVarDecl::Create(
            Context, NewMD, PL, PL, II, PT, /*TInfo=*/nullptr, SC_None,
            /*DefArg=*/nullptr));
      }
      NewMD->setParams(NewParams);
    }
    Spec->addDecl(NewMD);
  }

  // Clone fields with substituted types.
  for (Decl *D : PatternDef->decls()) {
    auto *FD = dyn_cast<FieldDecl>(D);
    if (!FD)
      continue;
    QualType NewTy =
        SubstType(FD->getType(), Args, PointOfInstantiation, FD->getDeclName());
    if (NewTy.isNull())
      NewTy = FD->getType();
    FieldDecl *NewFD = FieldDecl::Create(
        Context, Spec, FD->getLocation(), FD->getLocation(), FD->getIdentifier(),
        NewTy, /*TInfo=*/nullptr, /*BW=*/nullptr);
    Spec->addDecl(NewFD);
  }

  // Clone direct base specifiers with substituted base types when possible.
  if (unsigned NB = PatternDef->getNumBases()) {
    auto *NewBases = new (Context) CXXBaseSpecifier[NB];
    unsigned Out = 0;
    for (const CXXBaseSpecifier *B = PatternDef->bases_begin(),
                                *BE = PatternDef->bases_end();
         B != BE; ++B) {
      QualType BT = B->getType();
      QualType NBT = SubstType(BT, Args, PointOfInstantiation, DeclarationName());
      if (NBT.isNull())
        NBT = BT;
      TypeSourceInfo *TSI = Context.getTrivialTypeSourceInfo(NBT, PointOfInstantiation);
      NewBases[Out++] =
          CXXBaseSpecifier(B->getSourceRange(), B->isVirtual(), B->isBaseOfClass(),
                           B->getAccessSpecifier(), TSI, B->getEllipsisLoc());
    }
    Spec->setBases(NewBases, Out);
  }

  Spec->completeDefinition();
  Pattern->getDeclContext()->addDecl(Spec);
  return Spec;
}


bool neverc::isAtLeastAsSpecialized(Sema &S, FunctionTemplateDecl *A,
                                    FunctionTemplateDecl *B) {
  (void)S;
  if (!A || !B)
    return false;
  if (A == B)
    return true;
  FunctionDecl *FA = A->getTemplatedDecl();
  FunctionDecl *FB = B->getTemplatedDecl();
  if (!FA || !FB)
    return false;
  // Scaffold: fewer template parameters counts as "more specialized".
  // Full partial ordering (deduce A from B and B from A) is Phase-4+.
  TemplateParameterList *PA = A->getTemplateParameters();
  TemplateParameterList *PB = B->getTemplateParameters();
  unsigned NA = PA ? PA->size() : 0;
  unsigned NB = PB ? PB->size() : 0;
  if (NA != NB)
    return NA < NB;
  // Equal template-arity: fewer function parameters is not decisive; prefer
  // fewer remaining template-type-parm mentions in the function type (more
  // specialized / concrete wins). Also prefer non-variadic over variadic.
  if (FA->isVariadic() != FB->isVariadic())
    return !FA->isVariadic() && FB->isVariadic();
  // Equal template-arity: prefer fewer remaining template-type-parm mentions
  // in the function type (more specialized / concrete wins).
  unsigned DepA = 0, DepB = 0;
  auto countTTP = [](QualType T, unsigned &N) {
    if (T.isNull())
      return;
    const Type *Ty = T.getTypePtr();
    if (isa<TemplateTypeParmType>(Ty)) {
      ++N;
      return;
    }
    if (const auto *PT = dyn_cast<PointerType>(Ty)) {
      QualType P = PT->getPointeeType();
      if (!P.isNull() && isa<TemplateTypeParmType>(P.getTypePtr()))
        ++N;
      return;
    }
    if (const auto *RT = dyn_cast<ReferenceType>(Ty)) {
      QualType P = RT->getPointeeType();
      if (!P.isNull() && isa<TemplateTypeParmType>(P.getTypePtr()))
        ++N;
      return;
    }
    if (const auto *FT = Ty->getAs<FunctionProtoType>()) {
      QualType R = FT->getReturnType();
      if (!R.isNull() && isa<TemplateTypeParmType>(R.getTypePtr()))
        ++N;
      for (QualType P : FT->getParamTypes()) {
        if (P.isNull())
          continue;
        const Type *PT = P.getTypePtr();
        if (isa<TemplateTypeParmType>(PT))
          ++N;
        else if (const auto *Ptr = dyn_cast<PointerType>(PT)) {
          QualType Po = Ptr->getPointeeType();
          if (!Po.isNull() && isa<TemplateTypeParmType>(Po.getTypePtr()))
            ++N;
        }
      }
    }
  };
  countTTP(FA->getType(), DepA);
  countTTP(FB->getType(), DepB);
  if (DepA != DepB)
    return DepA < DepB;
  return false;
}
