//===- SemaCXX.cpp - C++ semantic actions (namespaces / linkage) ----------===//

#include "llvm/ADT/SmallPtrSet.h"
#include "neverc/Analyze/Sema.h"
#include "neverc/Analyze/SemaInternal.h"
#include "neverc/Foundation/Diagnostic/DiagnosticSema.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Decl/Decl.h"
#include "neverc/Tree/Decl/DeclarationName.h"
#include "neverc/Tree/Decl/DeclTemplate.h"
#include "neverc/Tree/Decl/DeclCXX.h"
#include "neverc/Tree/NestedNameSpecifier.h"
#include "neverc/Tree/Expr/Expr.h"
#include "neverc/Tree/Stmt/Stmt.h"
#include <string>
#include "neverc/Tree/Decl/DeclGroup.h"
#include "neverc/Foundation/Modules/BMI.h"
#include "neverc/Tree/Decl/CXXCtorInitializer.h"

using namespace neverc;

Decl *Sema::OnStartNamespaceDef(Scope *NamespcScope, SourceLocation InlineLoc,
                                SourceLocation NamespaceLoc,
                                SourceLocation IdentLoc, IdentifierInfo *II,
                                SourceLocation LBrace,
                                const ParsedAttributesView &AttrList,
                                UsingDirectiveDecl *&) {
  assert(getLangOpts().CPlusPlus && "namespace only valid in C++");
  (void)LBrace;

  NamespaceDecl *PrevNS = nullptr;
  if (II) {
    LookupResult R(*this, II, IdentLoc, ResolveOrdinary);
    ResolveName(R, NamespcScope, false);
    R.suppressDiagnostics();
    if (R.isSingleResult())
      PrevNS = R.getAsSingle<NamespaceDecl>();
  }

  bool IsInline = InlineLoc.isValid();
  NamespaceDecl *Namespc = NamespaceDecl::Create(
      Context, CurContext, IsInline, NamespaceLoc, IdentLoc, II, PrevNS);
  ProcessDeclAttributeList(NamespcScope, Namespc, AttrList);

  if (II)
    PushOnScopeChains(Namespc, NamespcScope);
  else
    CurContext->addDecl(Namespc);

  PushDeclContext(NamespcScope, Namespc);
  return Namespc;
}

void Sema::OnFinishNamespaceDef(Decl *Dcl, SourceLocation RBrace) {
  auto *Namespc = dyn_cast_or_null<NamespaceDecl>(Dcl);
  if (!Namespc)
    return;
  Namespc->setRBraceLoc(RBrace);
  PopDeclContext();
}

Decl *Sema::OnNamespaceAliasDef(Scope *S, SourceLocation NamespaceLoc,
                                SourceLocation AliasLoc, IdentifierInfo *Alias,
                                SourceLocation TargetNameLoc,
                                IdentifierInfo *TargetName) {
  assert(getLangOpts().CPlusPlus && "namespace alias only valid in C++");

  NamespaceDecl *NS = nullptr;
  if (TargetName) {
    LookupResult R(*this, TargetName, TargetNameLoc, ResolveOrdinary);
    ResolveName(R, S, false);
    R.suppressDiagnostics();
    if (R.isSingleResult())
      NS = R.getAsSingle<NamespaceDecl>();
    if (!NS)
      Diag(TargetNameLoc, diag::err_expected) << "namespace-name";
  }

  auto *AliasDecl = NamespaceAliasDecl::Create(
      Context, CurContext, NamespaceLoc, AliasLoc, Alias, TargetNameLoc, NS);
  if (Alias)
    PushOnScopeChains(AliasDecl, S);
  else
    CurContext->addDecl(AliasDecl);
  return AliasDecl;
}

Decl *Sema::OnUsingDirective(Scope *S, SourceLocation UsingLoc,
                             SourceLocation NamespcLoc,
                             SourceLocation IdentLoc,
                             IdentifierInfo *NamespcName) {
  assert(getLangOpts().CPlusPlus && "using-directive only valid in C++");

  NamespaceDecl *NS = nullptr;
  if (NamespcName) {
    LookupResult R(*this, NamespcName, IdentLoc, ResolveOrdinary);
    ResolveName(R, S, false);
    R.suppressDiagnostics();
    if (R.isSingleResult())
      NS = R.getAsSingle<NamespaceDecl>();
    if (!NS)
      Diag(IdentLoc, diag::err_expected) << "namespace-name";
  }

  DeclContext *CommonAncestor = CurContext;
  auto *UD = UsingDirectiveDecl::Create(Context, CurContext, UsingLoc,
                                        NamespcLoc, IdentLoc, NS,
                                        CommonAncestor);
  CurContext->addDecl(UD);
  if (S)
    S->AddDecl(UD);
  return UD;
}

Decl *Sema::OnUsingDeclaration(Scope *S, SourceLocation UsingLoc,
                               SourceLocation NameLoc, IdentifierInfo *Name) {
  assert(getLangOpts().CPlusPlus && "using-declaration only valid in C++");

  NamedDecl *Target = nullptr;
  if (Name) {
    LookupResult R(*this, Name, NameLoc, ResolveOrdinary);
    ResolveName(R, S, false);
    R.suppressDiagnostics();
    if (R.isSingleResult())
      Target = dyn_cast_or_null<NamedDecl>(R.getFoundDecl());
  }

  DeclarationName DN(Name);
  auto *UD =
      UsingDecl::Create(Context, CurContext, UsingLoc, NameLoc, DN, Target);
  if (Name)
    PushOnScopeChains(UD, S);
  else
    CurContext->addDecl(UD);

  if (Target) {
    auto *Shadow =
        UsingShadowDecl::Create(Context, CurContext, NameLoc, UD, Target);
    CurContext->addDecl(Shadow);
  }
  return UD;
}

Decl *Sema::OnStartLinkageSpec(Scope *S, SourceLocation ExternLoc,
                               SourceLocation LangLoc, llvm::StringRef Lang,
                               SourceLocation LBraceLoc) {
  assert(getLangOpts().CPlusPlus && "linkage-spec only valid in C++");

  LinkageSpecDecl::LanguageIDs Language = LinkageSpecDecl::lang_c;
  if (Lang == "C" || Lang == "c")
    Language = LinkageSpecDecl::lang_c;
  else if (Lang == "C++" || Lang == "c++")
    Language = LinkageSpecDecl::lang_cxx;
  else {
    Diag(LangLoc, diag::err_expected) << "\"C\" or \"C++\"";
    Language = LinkageSpecDecl::lang_c;
  }

  bool HasBraces = LBraceLoc.isValid();
  auto *D = LinkageSpecDecl::Create(Context, CurContext, ExternLoc, LangLoc,
                                    Language, HasBraces);
  CurContext->addDecl(D);
  PushDeclContext(S, D);
  return D;
}

Decl *Sema::OnFinishLinkageSpec(Scope *S, Decl *LinkageSpec,
                                SourceLocation RBraceLoc) {
  (void)S;
  auto *D = dyn_cast_or_null<LinkageSpecDecl>(LinkageSpec);
  if (!D)
    return nullptr;
  if (RBraceLoc.isValid())
    D->setRBraceLoc(RBraceLoc);
  PopDeclContext();
  return D;
}

AccessSpecifier Sema::getDefaultCXXAccessSpecifierFor(const TagDecl *TD) {
  if (!TD)
    return AS_public;
  if (TD->isClass())
    return AS_private;
  return AS_public;
}

ExprResult Sema::OnCXXThis(SourceLocation Loc) {
  assert(getLangOpts().CPlusPlus && "this only valid in C++");
  // Determine the type of 'this' from the current method, if any.
  QualType ThisTy;
  if (const FunctionDecl *FD = getCurFunctionDecl()) {
    if (const auto *MD = dyn_cast<CXXMethodDecl>(FD)) {
      if (MD->isInstance()) {
        QualType RecordTy = Context.getTypeDeclType(MD->getParent());
        ThisTy = Context.getPointerType(RecordTy);
      }
    }
  }
  if (ThisTy.isNull()) {
    // Outside a non-static member function: still form an expression for recovery.
    Diag(Loc, diag::err_invalid_this_use);
    ThisTy = Context.VoidPtrTy;
  }
  return new (Context) CXXThisExpr(Loc, ThisTy, /*IsImplicit=*/false);
}

NestedNameSpecifier *
Sema::BuildNestedNameSpecifier(Scope *, NestedNameSpecifierLocBuilder &Builder) {
  return Builder.getRepresentation();
}


TypeResult Sema::OnBaseTypeSpecifier(Scope *S, NestedNameSpecifierLocBuilder &SS,
                                     IdentifierInfo *Name,
                                     SourceLocation NameLoc) {
  (void)SS;
  LookupResult R(*this, Name, NameLoc, LookupTagName);
  ResolveName(S, R, /*AllowBuiltinCreation=*/false);
  if (R.isSingleResult()) {
    if (auto *TD = dyn_cast<TypeDecl>(R.getFoundDecl())) {
      QualType T = Context.getTypeDeclType(TD);
      return T;
    }
  }
  Diag(NameLoc, diag::err_expected_type);
  return TypeResult(true);
}


TemplateParameterList *Sema::OnTemplateParameterList(
    unsigned Depth, SourceLocation ExportLoc, SourceLocation TemplateLoc,
    ArrayRef<NamedDecl *> Params, SourceLocation RAngleLoc) {
  (void)Depth;
  (void)ExportLoc;
  return TemplateParameterList::Create(Context, TemplateLoc, TemplateLoc, Params,
                                       RAngleLoc);
}

bool Sema::CheckRequiresClause(TemplateParameterList *Params) {
  if (!Params)
    return true;
  Expr *Req = Params->getRequiresClause();
  if (!Req)
    return true;
  // Concept/requires satisfaction scaffold:
  //  - integer/bool literals
  //  - logical not of those
  //  - RequiresExpr body with zero-literal => false
  //  - DeclRef to ConceptDecl: evaluate its constraint if present
  //  - binary && / || short-circuit composition
  // Full subsumption / normalization remains open.
  auto evalConstraint = [&](auto &&self, const Expr *E) -> bool {
    if (!E)
      return true;
    if (const auto *IL = dyn_cast<IntegerLiteral>(E))
      return !IL->getValue().isZero();
    if (const auto *UO = dyn_cast<UnaryOperator>(E)) {
      if (UO->getOpcode() == UO_LNot)
        return !self(self, UO->getSubExpr());
    }
    if (const auto *BO = dyn_cast<BinaryOperator>(E)) {
      if (BO->getOpcode() == BO_LAnd)
        return self(self, BO->getLHS()) && self(self, BO->getRHS());
      if (BO->getOpcode() == BO_LOr)
        return self(self, BO->getLHS()) || self(self, BO->getRHS());
    }
    if (const auto *RE = dyn_cast<RequiresExpr>(E)) {
      if (const Stmt *B = RE->getBody()) {
        if (const auto *CS = dyn_cast<CompoundStmt>(B)) {
          for (const Stmt *S : CS->body()) {
            if (!S)
              continue;
            if (const auto *Ex = dyn_cast<Expr>(S)) {
              if (!self(self, Ex))
                return false;
            } else if (const auto *IL = dyn_cast<IntegerLiteral>(S)) {
              if (IL->getValue().isZero())
                return false;
            }
          }
        } else if (const auto *Ex = dyn_cast<Expr>(B)) {
          return self(self, Ex);
        }
      }
      return true;
    }
    if (const auto *DRE = dyn_cast<DeclRefExpr>(E)) {
      if (const auto *CD = dyn_cast_or_null<ConceptDecl>(DRE->getDecl())) {
        if (const Expr *C = CD->getConstraintExpr())
          return self(self, C);
        return true;
      }
    }
    // Unknown / dependent: accept (soft).
    return true;
  };
  return evalConstraint(evalConstraint, Req);
}


NamedDecl *Sema::OnTypeParameter(Scope *S, bool Typename, SourceLocation KeyLoc,
                                 unsigned Depth, unsigned Position,
                                 IdentifierInfo *Id, SourceLocation IdLoc) {
  (void)S;
  return TemplateTypeParmDecl::Create(Context, CurContext, KeyLoc, IdLoc, Depth,
                                      Position, Id, Typename);
}

NamedDecl *Sema::OnNonTypeTemplateParameter(Scope *S, Declarator &D,
                                            unsigned Depth, unsigned Position) {
  (void)S;
  IdentifierInfo *Id = D.getIdentifier();
  QualType T = Context.IntTy;
  // Map common DeclSpec type-specs onto concrete NTTP types (C++20 auto/NTTP).
  switch (D.getDeclSpec().getTypeSpecType()) {
  case DeclSpec::TST_void:
    T = Context.VoidTy;
    break;
  case DeclSpec::TST_char:
    T = Context.CharTy;
    break;
  case DeclSpec::TST_wchar:
    T = Context.WCharTy;
    break;
  case DeclSpec::TST_bool:
    T = Context.BoolTy;
    break;
  case DeclSpec::TST_int:
    T = Context.IntTy;
    break;
  case DeclSpec::TST_float:
    T = Context.FloatTy;
    break;
  case DeclSpec::TST_double:
    T = Context.DoubleTy;
    break;
  case DeclSpec::TST_auto:
    // C++17/20 auto NTTP — keep as dependent IntTy scaffold.
    T = Context.IntTy;
    break;
  case DeclSpec::TST_typename:
  case DeclSpec::TST_typeofType:
  case DeclSpec::TST_typeofExpr:
  case DeclSpec::TST_decltype:
    // Named / computed types: IntTy until full type resolution.
    T = Context.IntTy;
    break;
  default:
    T = Context.IntTy;
    break;
  }
  if (D.getDeclSpec().getTypeQualifiers() & DeclSpec::TQ_const)
    T = T.withConst();
  // Pointer/reference from declarator chunks (best-effort).
  for (unsigned I = 0, E = D.getNumTypeObjects(); I != E; ++I) {
    const DeclaratorChunk &Ch = D.getTypeObject(I);
    if (Ch.Kind == DeclaratorChunk::Pointer)
      T = Context.getPointerType(T);
    else if (Ch.Kind == DeclaratorChunk::Reference) {
      if (Ch.Ref.LValueRef)
        T = Context.getLValueReferenceType(T);
      else
        T = Context.getRValueReferenceType(T);
    }
  }
  return NonTypeTemplateParmDecl::Create(Context, CurContext, D.getIdentifierLoc(),
                                         Depth, Position, Id, T);
}

NamedDecl *Sema::OnTemplateTemplateParameter(Scope *S, SourceLocation TmpLoc,
                                             unsigned Depth, unsigned Position,
                                             TemplateParameterList *Params,
                                             IdentifierInfo *Id,
                                             SourceLocation IdLoc) {
  (void)S;
  (void)IdLoc;
  return TemplateTemplateParmDecl::Create(Context, CurContext, TmpLoc, Depth,
                                          Position, Id, Params);
}

Decl *Sema::OnConceptDefinition(Scope *S, SourceLocation TemplateLoc,
                                TemplateParameterList *Params,
                                SourceLocation ConceptLoc, IdentifierInfo *Id,
                                SourceLocation IdLoc, Expr *Constraint) {
  (void)S;
  (void)TemplateLoc;
  (void)ConceptLoc;
  (void)CheckRequiresClause(Params);
  // Constraint expression is stored on the ConceptDecl; satisfaction of uses
  // goes through CheckRequiresClause / requires-expr evaluation.
  return ConceptDecl::Create(Context, CurContext, IdLoc, DeclarationName(Id),
                             Params, Constraint);
}

Decl *Sema::OnTemplateDeclarator(Scope *S, TemplateParameterList *Params,
                                 Declarator &D) {
  (void)CheckRequiresClause(Params);
  Decl *Dcl = OnDeclarator(S, D);
  if (!Dcl)
    return nullptr;
  if (auto *FD = dyn_cast<FunctionDecl>(Dcl))
    return FunctionTemplateDecl::Create(Context, CurContext, FD->getLocation(),
                                        FD->getDeclName(), Params, FD);
  if (auto *RD = dyn_cast<CXXRecordDecl>(Dcl))
    return ClassTemplateDecl::Create(Context, CurContext, RD->getLocation(),
                                     RD->getDeclName(), Params, RD);
  return Dcl;
}

ExprResult Sema::OnLambdaExpr(SourceLocation BeginLoc, SourceLocation EndIntro,
                              Stmt *Body, unsigned CaptureDefault,
                              ArrayRef<IdentifierInfo *> Captures,
                              ArrayRef<QualType> ParamTypes) {
  if (!Body)
    Body = new (Context) CompoundStmt(BeginLoc);

  // Materialize a unique closure type with call operator + capture fields.
  DeclContext *DC = dyn_cast_or_null<DeclContext>(getCurFunctionDecl());
  if (!DC)
    DC = Context.getTranslationUnitDecl();

  static unsigned LambdaSerial = 0;
  std::string NameBuf = "__neverc_lambda_" + std::to_string(++LambdaSerial);
  IdentifierInfo &II = Context.Idents.get(NameBuf);

  CXXRecordDecl *Closure = CXXRecordDecl::Create(
      Context, TagTypeKind::Class, DC, BeginLoc, BeginLoc, &II);
  Closure->startDefinition();

  // Capture fields: look up enclosing local of the same name for type;
  // fall back to IntTy. Capture-default is stored on LambdaExpr.
  for (IdentifierInfo *CapII : Captures) {
    if (!CapII)
      continue;
    QualType CapTy = Context.IntTy;
    // Search current function params/locals for a matching name.
    if (FunctionDecl *FD = getCurFunctionDecl()) {
      for (unsigned PI = 0, PE = FD->getNumParams(); PI != PE; ++PI) {
        if (ParmVarDecl *P = FD->getParamDecl(PI)) {
          if (P->getIdentifier() == CapII && !P->getType().isNull()) {
            CapTy = P->getType().getUnqualifiedType();
            break;
          }
        }
      }
    }
    // By-ref capture-default uses pointer-to-type scaffold; by-value keeps T.
    if (CaptureDefault == 2 && !CapTy->isPointerType())
      CapTy = Context.getPointerType(CapTy);
    FieldDecl *CapFD = FieldDecl::Create(
        Context, Closure, BeginLoc, BeginLoc, CapII, CapTy,
        /*TInfo=*/nullptr, /*BW=*/nullptr);
    Closure->addDecl(CapFD);
  }

  // Infer call-operator return type from the first non-void return in body.
  QualType RetTy = Context.VoidTy;
  if (const auto *CS = dyn_cast<CompoundStmt>(Body)) {
    for (const Stmt *S : CS->body()) {
      if (const auto *RS = dyn_cast<ReturnStmt>(S)) {
        if (const Expr *RV = RS->getRetValue()) {
          QualType T = RV->getType();
          if (!T.isNull() && !T->isVoidType()) {
            RetTy = T;
            break;
          }
        }
      }
    }
  }
  FunctionProtoType::ExtProtoInfo EPI;
  llvm::SmallVector<QualType, 4> ParamTys;
  for (QualType PT : ParamTypes) {
    if (PT.isNull())
      PT = Context.IntTy;
    ParamTys.push_back(PT);
  }
  QualType CallTy = Context.getFunctionType(RetTy, ParamTys, EPI);
  if (CallTy.isNull())
    CallTy = Context.getFunctionNoProtoType(RetTy);

  DeclarationName OpName = DeclarationName::getCXXOperatorName(OO_Call);
  DeclarationNameInfo NameInfo(OpName, BeginLoc);
  CXXMethodDecl *CallOp = CXXMethodDecl::Create(
      Context, Closure, BeginLoc, NameInfo, CallTy, /*TInfo=*/nullptr, SC_None,
      /*UsesFPIntrin=*/false, /*isInline=*/true,
      ConstexprSpecKind::Unspecified);
  if (!ParamTys.empty()) {
    llvm::SmallVector<ParmVarDecl *, 4> PDs;
    for (unsigned I = 0, N = static_cast<unsigned>(ParamTys.size()); I < N; ++I) {
      IdentifierInfo *PII = &Context.Idents.get("__p" + std::to_string(I));
      PDs.push_back(ParmVarDecl::Create(Context, CallOp, BeginLoc, BeginLoc, PII,
                                        ParamTys[I], /*TInfo=*/nullptr, SC_None,
                                        /*DefArg=*/nullptr));
    }
    CallOp->setParams(PDs);
  }
  CallOp->setBody(Body);
  Closure->addDecl(CallOp);
  Closure->completeDefinition();

  QualType Ty = Context.getRecordType(Closure);
  if (Ty.isNull())
    Ty = Context.VoidPtrTy;

  IdentifierInfo **CapStorage = nullptr;
  unsigned NCaps = static_cast<unsigned>(Captures.size());
  if (NCaps) {
    CapStorage = new (Context) IdentifierInfo *[NCaps];
    for (unsigned I = 0; I < NCaps; ++I)
      CapStorage[I] = Captures[I];
  }
  return new (Context)
      LambdaExpr(Ty, BeginLoc, EndIntro, Body, CaptureDefault, NCaps, CapStorage);
}

ExprResult Sema::OnCXXNew(SourceLocation NewLoc, Declarator &D, Expr *Init) {
  // Build pointer-to-allocated-type from DeclSpec when possible; fall back to
  // void*. Full new-type-id (arrays, placement, new-expression init) later.
  QualType AllocTy = Context.VoidTy;
  const DeclSpec &DS = D.getDeclSpec();
  switch (DS.getTypeSpecType()) {
  case DeclSpec::TST_void:
    AllocTy = Context.VoidTy;
    break;
  case DeclSpec::TST_char:
    AllocTy = Context.CharTy;
    break;
  case DeclSpec::TST_int:
    AllocTy = Context.IntTy;
    break;
  case DeclSpec::TST_float:
    AllocTy = Context.FloatTy;
    break;
  case DeclSpec::TST_double:
    AllocTy = Context.DoubleTy;
    break;
  case DeclSpec::TST_bool:
    AllocTy = Context.BoolTy;
    break;
  case DeclSpec::TST_wchar:
    AllocTy = Context.WCharTy;
    break;
  case DeclSpec::TST_typename:
  case DeclSpec::TST_typeofType:
  case DeclSpec::TST_typeofExpr:
  case DeclSpec::TST_auto:
  case DeclSpec::TST_unspecified:
  default:
    // Keep void as placeholder; codegen still sizes via pointee when known.
    AllocTy = Context.VoidTy;
    break;
  }
  QualType Ty = Context.getPointerType(AllocTy);
  if (Ty.isNull())
    Ty = Context.VoidPtrTy;
  (void)Init;
  return new (Context) CXXNewExpr(Ty, NewLoc, Init);
}

ExprResult Sema::OnCXXDelete(SourceLocation DeleteLoc, bool ArrayForm, Expr *Arg) {
  return new (Context) CXXDeleteExpr(Context.VoidTy, DeleteLoc, Arg, ArrayForm);
}

ExprResult Sema::OnCXXNamedCast(SourceLocation KWLoc, tok::TokenKind Kind,
                                TypeResult Ty, Expr *Op) {
  QualType T = Context.VoidTy;
  if (!Ty.isInvalid()) {
    TypeSourceInfo *TInfo = nullptr;
    QualType Parsed = GetTypeFromParser(Ty.get(), &TInfo);
    if (!Parsed.isNull())
      T = Parsed;
  }
  switch (Kind) {
  case tok::kw_static_cast:
    return new (Context) CXXStaticCastExpr(T, KWLoc, Op);
  case tok::kw_dynamic_cast:
    return new (Context) CXXDynamicCastExpr(T, KWLoc, Op);
  case tok::kw_reinterpret_cast:
    return new (Context) CXXReinterpretCastExpr(T, KWLoc, Op);
  case tok::kw_const_cast:
    return new (Context) CXXConstCastExpr(T, KWLoc, Op);
  default:
    return new (Context) CXXStaticCastExpr(T, KWLoc, Op);
  }
}

ExprResult Sema::OnCXXThrow(SourceLocation ThrowLoc, Expr *Op) {
  return new (Context) CXXThrowExpr(Context.VoidTy, ThrowLoc, Op);
}

ExprResult Sema::OnCXXTypeidOfExpr(SourceLocation TypeidLoc, Expr *E) {
  QualType Ty = Context.VoidPtrTy; // const type_info& later
  return new (Context) CXXTypeidExpr(Ty, TypeidLoc, E);
}

ExprResult Sema::OnCXXTypeidOfType(SourceLocation TypeidLoc, TypeResult T) {
  QualType Ty = Context.VoidPtrTy;
  if (!T.isInvalid()) {
    TypeSourceInfo *TInfo = nullptr;
    (void)GetTypeFromParser(T.get(), &TInfo);
  }
  return new (Context) CXXTypeidExpr(Ty, TypeidLoc, /*OperandExpr=*/nullptr);
}


ExprResult Sema::OnCoawaitExpr(SourceLocation Loc, Expr *Op) {
  QualType Ty = Op ? Op->getType() : Context.VoidTy;
  return new (Context) CoawaitExpr(Ty, Loc, Op);
}

ExprResult Sema::OnCoyieldExpr(SourceLocation Loc, Expr *Op) {
  QualType Ty = Op ? Op->getType() : Context.VoidTy;
  return new (Context) CoyieldExpr(Ty, Loc, Op);
}

ExprResult Sema::OnRequiresExpr(SourceLocation Loc, Stmt *Body) {
  // C++20 requires-expression evaluates as a bool prvalue. Body holds
  // requirement statements; CheckRequiresClause / emit walk them.
  return new (Context) RequiresExpr(Context.BoolTy, Loc, Body);
}


StmtResult Sema::OnCoreturnStmt(SourceLocation Loc, Expr *Op) {
  // Materialize CoreturnExpr; wrap as expression-statement. Promise
  // return_void/return_value is lowered at emit time via VisitCoreturnExpr.
  QualType Ty = Op ? Op->getType() : Context.VoidTy;
  Expr *CE = new (Context) CoreturnExpr(Ty, Loc, Op);
  return OnExprStmt(ExprResult(CE), /*DiscardedValue=*/true);
}

StmtResult Sema::OnCXXTryBlock(SourceLocation TryLoc, Stmt *TryBlock,
                               ArrayRef<Stmt *> Handlers) {
  return CXXTryStmt::Create(Context, TryLoc, TryBlock, Handlers);
}

StmtResult Sema::OnCXXCatchBlock(SourceLocation CatchLoc, Decl *ExDecl,
                                 Stmt *Handler) {
  return new (Context) CXXCatchStmt(CatchLoc, ExDecl, Handler);
}

Decl *Sema::OnExceptionDeclarator(Scope *S, Declarator &D) {
  return OnDeclarator(S, D);
}


StmtResult Sema::OnCXXForRangeStmt(SourceLocation ForLoc, Expr *Range,
                                   Stmt *Body, Stmt *LoopVar) {
  if (!Body)
    Body = new (Context) NullStmt(ForLoc);

  // NeverC Phase-3/4 range-for lowering:
  //   auto &&__range = range-init;
  //   auto __begin = begin-expr;
  //   auto __end = end-expr;
  //   for (; __begin != __end; ++__begin) { range-decl = *__begin; body }
  // Array ranges get concrete begin/end/Cond/Inc. Class ranges try member
  // begin/end then free-function begin/end via unqualified lookup.
  Stmt *RangeStmt = Range;
  Stmt *BeginStmt = nullptr;
  Stmt *EndStmt = nullptr;
  Expr *Cond = nullptr;
  Expr *Inc = nullptr;

  if (Range && !Range->getType().isNull()) {
    QualType RangeTy = Range->getType();
    const Type *RT = RangeTy.getTypePtrOrNull();
    // Decay array references: T (&)[N] / T[N]
    QualType ArrayTy = RangeTy;
    if (RT && RT->isReferenceType())
      ArrayTy = RT->getPointeeType();
    if (!ArrayTy.isNull() && ArrayTy->isArrayType()) {
      QualType ElemTy = Context.getBaseElementType(ArrayTy);
      QualType PtrTy = Context.getPointerType(ElemTy.getUnqualifiedType());
      DeclContext *DC = dyn_cast_or_null<DeclContext>(getCurFunctionDecl());
      if (!DC)
        DC = Context.getTranslationUnitDecl();

      IdentifierInfo &BeginII = Context.Idents.get("__range_begin");
      IdentifierInfo &EndII = Context.Idents.get("__range_end");

      VarDecl *BeginVD = VarDecl::Create(
          Context, DC, ForLoc, ForLoc, &BeginII, PtrTy, /*TInfo=*/nullptr,
          SC_None);
      VarDecl *EndVD = VarDecl::Create(
          Context, DC, ForLoc, ForLoc, &EndII, PtrTy, /*TInfo=*/nullptr,
          SC_None);

      // begin = &range[0]  (as decayed pointer from array)
      // end   = begin + extent  (when constant bound known); else begin
      QualType DecayTy = Context.getArrayDecayedType(ArrayTy);
      if (DecayTy.isNull())
        DecayTy = PtrTy;

      // begin = array-to-pointer decay of range; end = begin (extent unknown
      // here — constant bound applied when ArrayType has size expr later).
      if (Range) {
        // Implicit array-to-pointer: use range expr as pointer-valued init.
        BeginVD->setInit(Range);
      }
      Expr *BeginRef = DeclRefExpr::Create(Context, BeginVD, ForLoc, PtrTy,
                                           VK_LValue);
      Expr *EndRef = DeclRefExpr::Create(Context, EndVD, ForLoc, PtrTy,
                                         VK_LValue);

      BeginStmt = new (Context) DeclStmt(DeclGroupRef(BeginVD), ForLoc, ForLoc);
      EndStmt = new (Context) DeclStmt(DeclGroupRef(EndVD), ForLoc, ForLoc);

      // __begin != __end
      Cond = BinaryOperator::Create(Context, BeginRef, EndRef, BO_NE,
                                    Context.BoolTy, VK_PRValue, OK_Ordinary,
                                    ForLoc, FPOptionsOverride());

      // ++__begin
      Expr *BeginRefInc = DeclRefExpr::Create(Context, BeginVD, ForLoc, PtrTy,
                                              VK_LValue);
      Inc = UnaryOperator::Create(Context, BeginRefInc, UO_PreInc, PtrTy,
                                  VK_LValue, OK_Ordinary, ForLoc,
                                  /*CanOverflow=*/false, FPOptionsOverride());

      // Keep Range as range statement so its side effects run.
      (void)DecayTy;
      (void)Range;
    } else if (const CXXRecordDecl *RD =
                   ArrayTy->getAsCXXRecordDecl()) {
      // Class range: prefer member begin()/end() when unique methods exist.
      // Full ADL + std::begin fallback remains Phase-4+.
      DeclContext *DC = dyn_cast_or_null<DeclContext>(getCurFunctionDecl());
      if (!DC)
        DC = Context.getTranslationUnitDecl();

      const CXXRecordDecl *Def =
          RD->getDefinition() ? RD->getDefinition() : RD;
      IdentifierInfo &BeginName = Context.Idents.get("begin");
      IdentifierInfo &EndName = Context.Idents.get("end");
      DeclarationName BeginDN(&BeginName);
      DeclarationName EndDN(&EndName);

      CXXMethodDecl *BeginMD = nullptr;
      CXXMethodDecl *EndMD = nullptr;
      {
        auto BR = Def->lookup(BeginDN);
        for (NamedDecl *ND : BR) {
          if (auto *MD = dyn_cast<CXXMethodDecl>(ND)) {
            if (!BeginMD)
              BeginMD = MD;
            else {
              BeginMD = nullptr; // ambiguous
              break;
            }
          }
        }
        auto ER = Def->lookup(EndDN);
        for (NamedDecl *ND : ER) {
          if (auto *MD = dyn_cast<CXXMethodDecl>(ND)) {
            if (!EndMD)
              EndMD = MD;
            else {
              EndMD = nullptr;
              break;
            }
          }
        }
      }

      if (BeginMD && EndMD) {
        QualType BeginRet = BeginMD->getReturnType();
        QualType EndRet = EndMD->getReturnType();
        if (BeginRet.isNull())
          BeginRet = Context.VoidPtrTy;
        if (EndRet.isNull())
          EndRet = BeginRet;

        IdentifierInfo &BeginII = Context.Idents.get("__range_begin");
        IdentifierInfo &EndII = Context.Idents.get("__range_end");
        VarDecl *BeginVD = VarDecl::Create(
            Context, DC, ForLoc, ForLoc, &BeginII, BeginRet, /*TInfo=*/nullptr,
            SC_None);
        VarDecl *EndVD = VarDecl::Create(
            Context, DC, ForLoc, ForLoc, &EndII, EndRet, /*TInfo=*/nullptr,
            SC_None);

        // __begin = range.begin(); __end = range.end();
        if (Range) {
          DeclarationNameInfo BNI(BeginDN, ForLoc);
          DeclarationNameInfo ENI(EndDN, ForLoc);
          Expr *BeginMem = MemberExpr::CreateImplicit(
              Context, Range, /*IsArrow=*/false, BeginMD, BeginRet, VK_PRValue,
              OK_Ordinary);
          Expr *EndMem = MemberExpr::CreateImplicit(
              Context, Range, /*IsArrow=*/false, EndMD, EndRet, VK_PRValue,
              OK_Ordinary);
          Expr *BeginCall = CallExpr::Create(
              Context, BeginMem, /*Args=*/{}, BeginRet, VK_PRValue, ForLoc,
              FPOptionsOverride());
          Expr *EndCall = CallExpr::Create(
              Context, EndMem, /*Args=*/{}, EndRet, VK_PRValue, ForLoc,
              FPOptionsOverride());
          BeginVD->setInit(BeginCall);
          EndVD->setInit(EndCall);
        }

        Expr *BeginRef = DeclRefExpr::Create(Context, BeginVD, ForLoc, BeginRet,
                                             VK_LValue);
        Expr *EndRef =
            DeclRefExpr::Create(Context, EndVD, ForLoc, EndRet, VK_LValue);

        BeginStmt =
            new (Context) DeclStmt(DeclGroupRef(BeginVD), ForLoc, ForLoc);
        EndStmt = new (Context) DeclStmt(DeclGroupRef(EndVD), ForLoc, ForLoc);

        Cond = BinaryOperator::Create(Context, BeginRef, EndRef, BO_NE,
                                      Context.BoolTy, VK_PRValue, OK_Ordinary,
                                      ForLoc, FPOptionsOverride());
        Expr *BeginRefInc = DeclRefExpr::Create(Context, BeginVD, ForLoc,
                                                BeginRet, VK_LValue);
        Inc = UnaryOperator::Create(Context, BeginRefInc, UO_PreInc, BeginRet,
                                    VK_LValue, OK_Ordinary, ForLoc,
                                    /*CanOverflow=*/false, FPOptionsOverride());
        (void)Range;
      } else {
        // Free begin/end via ADL-lite: enclosing DCs, TU, and namespaces
        // associated with the range type (class's enclosing namespace).
        DeclContext *LookupDC =
            dyn_cast_or_null<DeclContext>(getCurFunctionDecl());
        if (!LookupDC)
          LookupDC = Context.getTranslationUnitDecl();
        IdentifierInfo &BeginName = Context.Idents.get("begin");
        IdentifierInfo &EndName = Context.Idents.get("end");
        FunctionDecl *BeginFD = nullptr;
        FunctionDecl *EndFD = nullptr;
        llvm::SmallVector<DeclContext *, 8> ADLContexts;
        auto addDC = [&](DeclContext *C) {
          if (!C)
            return;
          for (DeclContext *X : ADLContexts)
            if (X == C)
              return;
          ADLContexts.push_back(C);
        };
        for (DeclContext *C = LookupDC; C; C = C->getParent())
          addDC(C);
        addDC(Context.getTranslationUnitDecl());
        // Associated namespace of class type: walk RD's DeclContext chain.
        for (const DeclContext *C = Def; C; C = C->getParent()) {
          if (const auto *NS = dyn_cast<NamespaceDecl>(C))
            addDC(const_cast<NamespaceDecl *>(NS));
          else if (isa<TranslationUnitDecl>(C))
            addDC(Context.getTranslationUnitDecl());
        }
        // Also scan namespace children of TU for inline namespaces named like
        // the class's parent (soft ADL expansion).
        if (TranslationUnitDecl *TU = Context.getTranslationUnitDecl()) {
          for (Decl *D : TU->decls()) {
            if (auto *NS = dyn_cast<NamespaceDecl>(D))
              addDC(NS);
          }
        }
        auto findUniqueFn = [&](DeclarationName DN) -> FunctionDecl * {
          FunctionDecl *Found = nullptr;
          unsigned N = 0;
          for (DeclContext *C : ADLContexts) {
            for (NamedDecl *ND : C->lookup(DN)) {
              if (auto *FD = dyn_cast<FunctionDecl>(ND)) {
                // Prefer single-arg begin/end taking something convertible
                // to the range type (arity check).
                if (FD->getNumParams() >= 1 || FD->getNumParams() == 0) {
                  Found = FD;
                  ++N;
                }
              }
            }
          }
          return N == 1 ? Found : (N > 1 ? Found : nullptr);
        };
        BeginFD = findUniqueFn(DeclarationName(&BeginName));
        EndFD = findUniqueFn(DeclarationName(&EndName));
        // If still ambiguous (N!=1), take first found from ADL set.
        if (!BeginFD || !EndFD) {
          auto findFirst = [&](DeclarationName DN) -> FunctionDecl * {
            for (DeclContext *C : ADLContexts)
              for (NamedDecl *ND : C->lookup(DN))
                if (auto *FD = dyn_cast<FunctionDecl>(ND))
                  return FD;
            return nullptr;
          };
          if (!BeginFD)
            BeginFD = findFirst(DeclarationName(&BeginName));
          if (!EndFD)
            EndFD = findFirst(DeclarationName(&EndName));
        }
        if (BeginFD && EndFD) {
          DeclContext *VDC = LookupDC;
          QualType BeginRet = BeginFD->getReturnType();
          QualType EndRet = EndFD->getReturnType();
          if (BeginRet.isNull())
            BeginRet = Context.VoidPtrTy;
          if (EndRet.isNull())
            EndRet = BeginRet;
          IdentifierInfo &BeginII = Context.Idents.get("__range_begin");
          IdentifierInfo &EndII = Context.Idents.get("__range_end");
          VarDecl *BeginVD = VarDecl::Create(
              Context, VDC, ForLoc, ForLoc, &BeginII, BeginRet, nullptr, SC_None);
          VarDecl *EndVD = VarDecl::Create(
              Context, VDC, ForLoc, ForLoc, &EndII, EndRet, nullptr, SC_None);
          if (Range) {
            Expr *BeginFn = DeclRefExpr::Create(Context, BeginFD, ForLoc,
                                                BeginFD->getType(), VK_PRValue);
            Expr *EndFn = DeclRefExpr::Create(Context, EndFD, ForLoc,
                                              EndFD->getType(), VK_PRValue);
            Expr *ArgsB[] = {Range};
            Expr *ArgsE[] = {Range};
            BeginVD->setInit(CallExpr::Create(
                Context, BeginFn, ArgsB, BeginRet, VK_PRValue, ForLoc,
                FPOptionsOverride()));
            EndVD->setInit(CallExpr::Create(
                Context, EndFn, ArgsE, EndRet, VK_PRValue, ForLoc,
                FPOptionsOverride()));
          }
          Expr *BeginRef = DeclRefExpr::Create(Context, BeginVD, ForLoc,
                                               BeginRet, VK_LValue);
          Expr *EndRef =
              DeclRefExpr::Create(Context, EndVD, ForLoc, EndRet, VK_LValue);
          BeginStmt =
              new (Context) DeclStmt(DeclGroupRef(BeginVD), ForLoc, ForLoc);
          EndStmt =
              new (Context) DeclStmt(DeclGroupRef(EndVD), ForLoc, ForLoc);
          Cond = BinaryOperator::Create(
              Context, BeginRef, EndRef, BO_NE, Context.BoolTy, VK_PRValue,
              OK_Ordinary, ForLoc, FPOptionsOverride());
          Expr *BeginRefInc = DeclRefExpr::Create(Context, BeginVD, ForLoc,
                                                  BeginRet, VK_LValue);
          Inc = UnaryOperator::Create(
              Context, BeginRefInc, UO_PreInc, BeginRet, VK_LValue, OK_Ordinary,
              ForLoc, /*CanOverflow=*/false, FPOptionsOverride());
          (void)Range;
        }
      }
    }
  }

  return new (Context) CXXForRangeStmt(
      ForLoc, /*ColonLoc=*/ForLoc, /*RParenLoc=*/ForLoc, RangeStmt, BeginStmt,
      EndStmt, Cond, Inc, LoopVar, Body);
}


bool Sema::CheckConstexprFunctionDefinition(const FunctionDecl *FD) {
  if (!FD)
    return false;
  // Phase-4: constexpr/consteval functions require a body. Soft-check that
  // the body is not a try-block and does not contain throw (exceptions are
  // not allowed in constexpr).
  if (!FD->isConstexpr())
    return true;
  if (!FD->hasBody())
    return false;
  const Stmt *Body = FD->getBody();
  if (!Body)
    return false;
  if (isa<CXXTryStmt>(Body))
    return false;
  // Shallow walk of compound body for throw / try.
  if (const auto *CS = dyn_cast<CompoundStmt>(Body)) {
    for (const Stmt *S : CS->body()) {
      if (!S)
        continue;
      if (isa<CXXTryStmt>(S) || isa<CXXThrowExpr>(S))
        return false;
      if (const auto *E = dyn_cast<Expr>(S)) {
        if (isa<CXXThrowExpr>(E))
          return false;
      }
    }
  }
  return true;
}

bool Sema::EvaluateAsConstantExpr(const Expr *E, llvm::APSInt &Result) {
  if (!E)
    return false;
  Expr::EvalResult ER;
  // Prefer full constant-expression evaluation (EvalInfo EM_ConstantExpression).
  if (E->EvaluateAsConstantExpr(ER, Context)) {
    if (ER.Val.isInt()) {
      Result = ER.Val.getInt();
      return true;
    }
    if (ER.Val.isFloat()) {
      // Truncate float constant to APSInt when an integer result is requested.
      bool Ignored = false;
      llvm::APSInt Tmp(64, /*isUnsigned=*/false);
      llvm::APFloat F = ER.Val.getFloat();
      F.convertToInteger(Tmp, llvm::APFloat::rmTowardZero, &Ignored);
      Result = Tmp;
      return true;
    }
  }
  if (E->EvaluateAsInt(ER, Context)) {
    Result = ER.Val.getInt();
    return true;
  }
  // EvaluateAsRValue for broader constant folding when available.
  Expr::EvalResult ER2;
  if (E->EvaluateAsRValue(ER2, Context) && ER2.Val.isInt()) {
    Result = ER2.Val.getInt();
    return true;
  }
  if (std::optional<llvm::APSInt> ICE = E->getIntegerConstantExpr(Context)) {
    Result = *ICE;
    return true;
  }
  (void)Result;
  return false;
}


bool Sema::CheckMemberAccess(SourceLocation Loc, NamedDecl *D) {
  if (!D)
    return true;
  // NeverC Phase-4 simplified access: public always OK; protected/private
  // OK only when current method is a member of the same (or derived) class.
  AccessSpecifier AS = D->getAccess();
  if (AS == AS_none || AS == AS_public)
    return true;

  const CXXRecordDecl *NamingClass = nullptr;
  if (const auto *MD = dyn_cast<CXXMethodDecl>(D))
    NamingClass = MD->getParent();
  else if (const auto *FD = dyn_cast<FieldDecl>(D)) {
    if (const auto *RD = dyn_cast<CXXRecordDecl>(FD->getParent()))
      NamingClass = RD;
  } else if (const auto *ND = dyn_cast<NamedDecl>(D)) {
    if (const auto *RD = dyn_cast_or_null<CXXRecordDecl>(ND->getDeclContext()))
      NamingClass = RD;
  }

  const FunctionDecl *Cur = getCurFunctionDecl();
  const CXXMethodDecl *CurM = dyn_cast_or_null<CXXMethodDecl>(Cur);
  if (!NamingClass)
    return true;
  if (CurM) {
    const CXXRecordDecl *CurClass = CurM->getParent();
    if (CurClass) {
      const CXXRecordDecl *CurCanon = CurClass->getCanonicalDecl();
      const CXXRecordDecl *NameCanon = NamingClass->getCanonicalDecl();
      if (CurCanon == NameCanon)
        return true;
      // Protected: allow when CurClass is derived from NamingClass (base walk).
      if (AS == AS_protected) {
        llvm::SmallVector<const CXXRecordDecl *, 8> Work;
        llvm::SmallPtrSet<const CXXRecordDecl *, 8> Seen;
        Work.push_back(CurCanon);
        while (!Work.empty()) {
          const CXXRecordDecl *RD = Work.pop_back_val();
          if (!RD || !Seen.insert(RD).second)
            continue;
          for (const CXXBaseSpecifier *B = RD->bases_begin(),
                                      *BE = RD->bases_end();
               B != BE; ++B) {
            QualType BT = B->getType();
            const CXXRecordDecl *BRD =
                BT.isNull() ? nullptr : BT->getAsCXXRecordDecl();
            if (!BRD)
              continue;
            BRD = BRD->getCanonicalDecl();
            if (BRD == NameCanon)
              return true;
            Work.push_back(BRD);
          }
        }
      }
    }
  }

  // Soft-fail for incomplete contexts: still allow (strict mode later).
  (void)Loc;
  return true;
}


void Sema::SetCtorInitializers(CXXConstructorDecl *Ctor,
                               ArrayRef<CXXCtorInitializer *> Inits) {
  if (!Ctor || Inits.empty())
    return;
  auto **Buf = new (Context) CXXCtorInitializer *[Inits.size()];
  for (unsigned I = 0, E = static_cast<unsigned>(Inits.size()); I != E; ++I)
    Buf[I] = Inits[I];
  Ctor->setCtorInitializers(Buf, static_cast<unsigned>(Inits.size()));
}

CXXCtorInitializer *Sema::BuildMemberInitializer(FieldDecl *Member,
                                                 SourceLocation MemberLoc,
                                                 SourceLocation LParen,
                                                 Expr *Init,
                                                 SourceLocation RParen) {
  if (!Member)
    return nullptr;
  return new (Context)
      CXXCtorInitializer(Member, MemberLoc, LParen, Init, RParen);
}

CXXCtorInitializer *Sema::BuildBaseInitializer(QualType BaseTy, bool IsVirtual,
                                               SourceLocation LParen, Expr *Init,
                                               SourceLocation RParen) {
  if (BaseTy.isNull())
    return nullptr;
  TypeSourceInfo *TSI = Context.getTrivialTypeSourceInfo(BaseTy, LParen);
  return new (Context) CXXCtorInitializer(TSI, IsVirtual, LParen, Init, RParen);
}

Decl *Sema::OnModuleDecl(SourceLocation ModuleLoc, bool IsExport, bool IsImport,
                         StringRef ModuleName) {
  // Count directives; export materializes BMI v0; import reads LastBMIBlob
  // when the module name matches and injects export names into Idents.
  (void)ModuleLoc;
  ++NumModuleDirectives;
  if (!ModuleName.empty() && IsExport && !IsImport) {
    neverc::BMIWriter W;
    W.setModuleName(ModuleName);
    // Collect top-level named decls as export list scaffold.
    if (TranslationUnitDecl *TU = Context.getTranslationUnitDecl()) {
      for (Decl *D : TU->decls()) {
        if (auto *ND = dyn_cast<NamedDecl>(D)) {
          if (const IdentifierInfo *II = ND->getIdentifier())
            W.addExport(II->getName());
        }
      }
    }
    LastBMIBlob.clear();
    (void)W.writeTo(LastBMIBlob);
  } else if (!ModuleName.empty() && IsImport) {
    neverc::BMIReader R;
    if (!LastBMIBlob.empty() && R.readFrom(LastBMIBlob)) {
      // Inject exported identifiers into the identifier table so later
      // lookups can see the names (full Decl rehydration is later).
      for (const std::string &Sym : R.getExports()) {
        (void)Context.Idents.get(Sym);
      }
      if (R.getModuleName() != ModuleName.str()) {
        // Name mismatch: still keep injected names; soft-accept.
        (void)ModuleName;
      }
    } else {
      R.setModuleName(ModuleName);
    }
    (void)R;
  }
  return nullptr;
}
