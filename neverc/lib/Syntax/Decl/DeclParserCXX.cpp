#include "neverc/Tree/Decl/DeclTemplate.h"
#include "neverc/Tree/Decl/CXXBaseSpecifier.h"
//===- DeclParserCXX.cpp - C++ declaration parsing ------------------------===//

#include "neverc/Foundation/Core/TokenKinds.h"
#include "neverc/Syntax/ParserGuards.h"
#include "neverc/Syntax/SyntaxParser.h"
#include "neverc/Tree/Decl/Decl.h"
#include "neverc/Tree/NestedNameSpecifier.h"
#include "neverc/Tree/Decl/DeclarationName.h"
#include "neverc/Tree/Expr/Expr.h"
#include <string>

using namespace neverc;

Decl *Parser::ParseNamespace(SourceLocation &DeclEnd,
                             ParsedAttributes &Attrs) {
  assert(Tok.is(tok::kw_namespace) && "Not a namespace!");
  SourceLocation NamespaceLoc = ConsumeToken();

  MaybeParseGNUAttributes(Attrs);

  SourceLocation InlineLoc;
  if (Tok.is(tok::kw_inline))
    InlineLoc = ConsumeToken();

  IdentifierInfo *Ident = nullptr;
  SourceLocation IdentLoc;
  if (Tok.is(tok::identifier)) {
    Ident = Tok.getIdentifierInfo();
    IdentLoc = ConsumeToken();
  }

  if (Tok.is(tok::equal)) {
    ConsumeToken();
    IdentifierInfo *Target = nullptr;
    SourceLocation TargetLoc;
    if (Tok.is(tok::identifier)) {
      Target = Tok.getIdentifierInfo();
      TargetLoc = ConsumeToken();
    } else {
      Diag(Tok, diag::err_expected) << tok::identifier;
    }
    DeclEnd = Tok.getLocation();
    RequireSemicolon(diag::err_expected_semi_declaration);
    return Actions.OnNamespaceAliasDef(getCurScope(), NamespaceLoc, IdentLoc,
                                       Ident, TargetLoc, Target);
  }

  if (Tok.isNot(tok::l_brace)) {
    Diag(Tok, diag::err_expected) << tok::l_brace;
    SkipUntil(tok::semi, StopAtSemi | StopBeforeMatch);
    TryConsumeToken(tok::semi);
    DeclEnd = PrevTokLocation;
    return nullptr;
  }

  BalancedDelimiterTracker T(*this, tok::l_brace);
  if (T.consumeOpen())
    return nullptr;

  UsingDirectiveDecl *ImplicitUsing = nullptr;
  ParseScope NamespaceScope(this, Scope::DeclScope);
  Decl *NamespcDecl = Actions.OnStartNamespaceDef(
      getCurScope(), InlineLoc, NamespaceLoc, IdentLoc, Ident,
      T.getOpenLocation(), Attrs, ImplicitUsing);

  while (Tok.isNot(tok::r_brace) && Tok.isNot(tok::eof)) {
    ParsedAttributes DeclAttrs(AttrFactory);
    ParsedAttributes DeclSpecAttrs(AttrFactory);
    ParseExternalDeclaration(DeclAttrs, DeclSpecAttrs);
  }

  T.consumeClose();
  DeclEnd = T.getCloseLocation();
  Actions.OnFinishNamespaceDef(NamespcDecl, T.getCloseLocation());
  return NamespcDecl;
}

Decl *Parser::ParseUsingDirectiveOrDeclaration(SourceLocation &DeclEnd,
                                               ParsedAttributes &Attrs) {
  assert(Tok.is(tok::kw_using) && "Not a using declaration!");
  SourceLocation UsingLoc = ConsumeToken();
  ProhibitAttributes(Attrs);

  if (Tok.is(tok::kw_namespace)) {
    SourceLocation NamespaceLoc = ConsumeToken();
    IdentifierInfo *Name = nullptr;
    SourceLocation NameLoc;
    if (Tok.is(tok::identifier)) {
      Name = Tok.getIdentifierInfo();
      NameLoc = ConsumeToken();
    } else {
      Diag(Tok, diag::err_expected) << tok::identifier;
    }
    DeclEnd = Tok.getLocation();
    RequireSemicolon(diag::err_expected_semi_declaration);
    return Actions.OnUsingDirective(getCurScope(), UsingLoc, NamespaceLoc,
                                    NameLoc, Name);
  }

  if (Tok.is(tok::kw_enum)) {
    ConsumeToken();
    IdentifierInfo *Name = nullptr;
    SourceLocation NameLoc;
    if (Tok.is(tok::identifier)) {
      Name = Tok.getIdentifierInfo();
      NameLoc = ConsumeToken();
    } else {
      Diag(Tok, diag::err_expected) << tok::identifier;
    }
    DeclEnd = Tok.getLocation();
    RequireSemicolon(diag::err_expected_semi_declaration);
    return Actions.OnUsingDeclaration(getCurScope(), UsingLoc, NameLoc, Name);
  }

  IdentifierInfo *Name = nullptr;
  SourceLocation NameLoc;
  if (Tok.is(tok::identifier)) {
    Name = Tok.getIdentifierInfo();
    NameLoc = ConsumeToken();
  } else {
    Diag(Tok, diag::err_expected) << tok::identifier;
  }
  DeclEnd = Tok.getLocation();
  RequireSemicolon(diag::err_expected_semi_declaration);
  return Actions.OnUsingDeclaration(getCurScope(), UsingLoc, NameLoc, Name);
}

Parser::DeclGroupPtrTy
Parser::ParseLinkage(ParsingDeclSpec &DS, AccessSpecifier AS) {
  assert(getLangOpts().CPlusPlus && "linkage-spec requires C++");
  assert(Tok.is(tok::string_literal) && "Not a linkage string");

  ExprResult LangStr = ParseStringLiteralExpression(/*Unevaluated=*/true);
  if (LangStr.isInvalid()) {
    SkipUntil(tok::semi, StopAtSemi);
    return nullptr;
  }

  StringLiteral *Lit = cast_or_null<StringLiteral>(LangStr.get());
  llvm::StringRef Lang = Lit ? Lit->getString() : llvm::StringRef();
  SourceLocation LangLoc = Lit ? Lit->getBeginLoc() : SourceLocation();
  SourceLocation ExternLoc = DS.getStorageClassSpecLoc();

  if (Tok.is(tok::l_brace)) {
    BalancedDelimiterTracker T(*this, tok::l_brace);
    if (T.consumeOpen())
      return nullptr;

    ParseScope LinkageScope(this, Scope::DeclScope);
    Decl *LS = Actions.OnStartLinkageSpec(getCurScope(), ExternLoc, LangLoc,
                                          Lang, T.getOpenLocation());

    while (Tok.isNot(tok::r_brace) && Tok.isNot(tok::eof)) {
      ParsedAttributes DeclAttrs(AttrFactory);
      ParsedAttributes DeclSpecAttrs(AttrFactory);
      ParseExternalDeclaration(DeclAttrs, DeclSpecAttrs);
    }

    T.consumeClose();
    Actions.OnFinishLinkageSpec(getCurScope(), LS, T.getCloseLocation());
    return Actions.WrapDeclAsGroup(LS);
  }

  // Single-declaration form: fresh decl-spec (DS already holds extern).
  (void)DS;
  ParseScope LinkageScope(this, Scope::DeclScope);
  Decl *LS = Actions.OnStartLinkageSpec(getCurScope(), ExternLoc, LangLoc, Lang,
                                        SourceLocation());
  ParsedAttributes EmptyAttrs(AttrFactory);
  ParsedAttributes EmptyDSAttrs(AttrFactory);
  DeclGroupPtrTy Result = ParseDeclarationOrFunctionDefinition(
      EmptyAttrs, EmptyDSAttrs, nullptr, AS);
  Actions.OnFinishLinkageSpec(getCurScope(), LS, SourceLocation());
  if (Result)
    return Result;
  return Actions.WrapDeclAsGroup(LS);
}

AccessSpecifier Parser::getAccessSpecifierIfPresent() {
  switch (Tok.getKind()) {
  case tok::kw_public:
    return AS_public;
  case tok::kw_protected:
    return AS_protected;
  case tok::kw_private:
    return AS_private;
  default:
    return AS_none;
  }
}


bool Parser::ParseOptionalCXXScopeSpecifier(NestedNameSpecifierLocBuilder &SS,
                                            bool EnteringContext) {
  (void)EnteringContext;
  if (!getLangOpts().CPlusPlus)
    return false;

  if (Tok.is(tok::coloncolon)) {
    SourceLocation CCLoc = ConsumeToken();
    SS.MakeGlobal(Actions.getTreeContext(), CCLoc);
  }

  bool HasScope = SS.getRepresentation() != nullptr;
  while (true) {
    if (Tok.is(tok::identifier) && NextToken().is(tok::coloncolon)) {
      IdentifierInfo *II = Tok.getIdentifierInfo();
      SourceLocation IdLoc = ConsumeToken();
      SourceLocation CCLoc = ConsumeToken(); // ::
      // Prefer namespace if lookup finds one.
      NamespaceDecl *NS = nullptr;
      LookupResult R(Actions, II, IdLoc, ResolveOrdinary);
      Actions.ResolveName(R, getCurScope(), false);
      R.suppressDiagnostics();
      if (R.isSingleResult())
        NS = R.getAsSingle<NamespaceDecl>();
      if (NS)
        SS.Extend(Actions.getTreeContext(), NS, IdLoc, CCLoc);
      else
        SS.Extend(Actions.getTreeContext(), II, IdLoc, CCLoc);
      HasScope = true;
      continue;
    }
    break;
  }
  return HasScope;
}

bool Parser::ParseOperatorFunctionId(SourceLocation &OpLoc,
                                     OverloadedOperatorKind &Op) {
  assert(Tok.is(tok::kw_operator) && "expected 'operator'");
  OpLoc = ConsumeToken();
  Op = OO_None;

  // Map common operator tokens.
  switch (Tok.getKind()) {
  case tok::plus: Op = OO_Plus; break;
  case tok::minus: Op = OO_Minus; break;
  case tok::star: Op = OO_Star; break;
  case tok::slash: Op = OO_Slash; break;
  case tok::percent: Op = OO_Percent; break;
  case tok::caret: Op = OO_Caret; break;
  case tok::amp: Op = OO_Amp; break;
  case tok::pipe: Op = OO_Pipe; break;
  case tok::tilde: Op = OO_Tilde; break;
  case tok::exclaim: Op = OO_Exclaim; break;
  case tok::equal: Op = OO_Equal; break;
  case tok::less: Op = OO_Less; break;
  case tok::greater: Op = OO_Greater; break;
  case tok::plusplus: Op = OO_PlusPlus; break;
  case tok::minusminus: Op = OO_MinusMinus; break;
  case tok::equalequal: Op = OO_EqualEqual; break;
  case tok::exclaimequal: Op = OO_Exclaimequal; break;
  case tok::lessequal: Op = OO_Lessequal; break;
  case tok::greaterequal: Op = OO_Greaterequal; break;
  case tok::ampamp: Op = OO_AmpAmp; break;
  case tok::pipepipe: Op = OO_PipePipe; break;
  case tok::lessless: Op = OO_LessLess; break;
  case tok::greatergreater: Op = OO_GreaterGreater; break;
  case tok::plusequal: Op = OO_PlusEqual; break;
  case tok::minusequal: Op = OO_MinusEqual; break;
  case tok::starequal: Op = OO_StarEqual; break;
  case tok::slashequal: Op = OO_SlashEqual; break;
  case tok::percentequal: Op = OO_PercentEqual; break;
  case tok::ampequal: Op = OO_Ampequal; break;
  case tok::pipeequal: Op = OO_Pipeequal; break;
  case tok::caretequal: Op = OO_Caretequal; break;
  case tok::lesslessequal: Op = OO_LessLessequal; break;
  case tok::greatergreaterequal: Op = OO_GreaterGreaterequal; break;
  case tok::comma: Op = OO_Comma; break;
  case tok::arrow: Op = OO_Arrow; break;
  case tok::l_paren:
    ConsumeToken();
    if (Tok.is(tok::r_paren)) {
      ConsumeToken();
      Op = OO_Call;
      return true;
    }
    Diag(Tok, diag::err_expected) << tok::r_paren;
    return false;
  case tok::l_square:
    ConsumeToken();
    if (Tok.is(tok::r_square)) {
      ConsumeToken();
      Op = OO_Subscript;
      return true;
    }
    Diag(Tok, diag::err_expected) << tok::r_square;
    return false;
  case tok::kw_new:
    Op = OO_New;
    break;
  case tok::kw_delete:
    Op = OO_Delete;
    break;
  default:
    Diag(Tok, diag::err_expected) << "operator";
    return false;
  }
  ConsumeToken();
  return Op != OO_None;
}



void Parser::ParseCXXBaseClause(CXXRecordDecl *ClassDecl) {
  assert(Tok.is(tok::colon) && "expected base-clause colon");
  ConsumeToken(); // ':'

  llvm::SmallVector<CXXBaseSpecifier, 4> Bases;
  while (true) {
    bool IsVirtual = false;
    AccessSpecifier AS = AS_none;
    SourceLocation StartLoc = Tok.getLocation();

    // optional access / virtual in either order
    for (int I = 0; I < 2; ++I) {
      if (!IsVirtual && Tok.is(tok::kw_virtual)) {
        IsVirtual = true;
        ConsumeToken();
        continue;
      }
      AccessSpecifier A = getAccessSpecifierIfPresent();
      if (A != AS_none && AS == AS_none) {
        AS = A;
        ConsumeToken();
        continue;
      }
      break;
    }
    if (AS == AS_none)
      AS = ClassDecl && ClassDecl->isClass() ? AS_private : AS_public;

    // base-type-specifier (simplified: nested-name + identifier type)
    NestedNameSpecifierLocBuilder SS;
    if (Tok.is(tok::coloncolon) ||
        (Tok.is(tok::identifier) && NextToken().is(tok::coloncolon)))
      (void)ParseOptionalCXXScopeSpecifier(SS, /*EnteringContext=*/false);

    TypeResult BaseType;
    if (Tok.is(tok::identifier)) {
      // Build a type via OnTypeName-style path if available; otherwise skip id.
      IdentifierInfo *II = Tok.getIdentifierInfo();
      SourceLocation IdLoc = ConsumeToken();
      BaseType = Actions.OnBaseTypeSpecifier(getCurScope(), SS, II, IdLoc);
    } else {
      Diag(Tok, diag::err_expected_type);
      SkipUntil(tok::comma, tok::l_brace, StopAtSemi | StopBeforeMatch);
    }

    TypeSourceInfo *TInfo = nullptr;
    if (!BaseType.isInvalid()) {
      QualType T = BaseType.get();
      TInfo = Actions.Context.getTrivialTypeSourceInfo(T, StartLoc);
    }

    SourceLocation EndLoc = PrevTokLocation;
    Bases.push_back(CXXBaseSpecifier(SourceRange(StartLoc, EndLoc), IsVirtual,
                                     /*BaseOfClass=*/ClassDecl && ClassDecl->isClass(),
                                     AS, TInfo));

    if (Tok.is(tok::ellipsis))
      ConsumeToken();

    if (Tok.isNot(tok::comma))
      break;
    ConsumeToken();
  }

  if (!Bases.empty() && ClassDecl) {
    // Ensure definition data exists.
    ClassDecl->startDefinition();
    auto *Mem = new (Actions.Context)
        CXXBaseSpecifier[Bases.size()];
    std::uninitialized_copy(Bases.begin(), Bases.end(), Mem);
    ClassDecl->setBases(Mem, static_cast<unsigned>(Bases.size()));
  }
}


Parser::DeclGroupPtrTy
Parser::ParseTemplateDeclaration(SourceLocation TemplateLoc) {
  // template <...> declaration
  if (Tok.isNot(tok::less)) {
    Diag(Tok, diag::err_expected) << "<";
    SkipUntil(tok::greater, StopAtSemi);
    return nullptr;
  }
  ConsumeToken();
  TemplateParameterList *Params = ParseTemplateParameterList(/*Depth=*/0);
  if (Tok.is(tok::greater) || Tok.is(tok::greatergreater)) {
    // consume one '>' (angle)
    if (Tok.is(tok::greatergreater)) {
      // split >> later; consume as greater for now
      Tok.setKind(tok::greater);
    } else {
      ConsumeToken();
    }
  } else {
    Diag(Tok, diag::err_expected) << ">";
  }

  // optional requires-clause after template-head
  if (Tok.is(tok::kw_requires) && Params) {
    ConsumeToken();
    // requires-clause: constraint-expression (simplified: constant-expr)
    ExprResult Req = ParseConstantExpression();
    if (Req.isUsable())
      Params->setRequiresClause(Req.get());
  }

  // concept C = constraint;
  if (Tok.is(tok::kw_concept)) {
    SourceLocation ConceptLoc = ConsumeToken();
    if (Tok.isNot(tok::identifier)) {
      Diag(Tok, diag::err_expected) << "identifier";
      return nullptr;
    }
    IdentifierInfo *II = Tok.getIdentifierInfo();
    SourceLocation IdLoc = ConsumeToken();
    if (Tok.is(tok::equal))
      ConsumeToken();
    ExprResult Constr = ParseConstantExpression();
    (void)Constr;
    Decl *Concept =
        Actions.OnConceptDefinition(getCurScope(), TemplateLoc, Params, ConceptLoc,
                                    II, IdLoc, Constr.get());
    RequireSemicolon();
    return Actions.WrapDeclAsGroup(Concept);
  }

  // using-alias template or ordinary declaration following template-head
  ParsedAttributes attrs(AttrFactory);
  DeclSpec DS(AttrFactory);
  ParseDeclarationSpecifiers(DS, attrs);
  ParsingDeclarator D(*this, DS, attrs, DeclaratorContext::File);
  ParseDeclarator(D);
  Decl *TheDecl = Actions.OnTemplateDeclarator(getCurScope(), Params, D);
  if (Tok.is(tok::semi))
    ConsumeToken();
  else if (Tok.is(tok::l_brace)) {
    // function template definition
    ParseFunctionStatementBody(TheDecl, D);
  }
  return Actions.WrapDeclAsGroup(TheDecl);
}

TemplateParameterList *Parser::ParseTemplateParameterList(unsigned Depth) {
  llvm::SmallVector<NamedDecl *, 4> Params;
  SourceLocation LAngleLoc = PrevTokLocation;
  if (Tok.is(tok::greater) || Tok.is(tok::greatergreater)) {
    return Actions.OnTemplateParameterList(Depth, SourceLocation(), LAngleLoc,
                                           Params, Tok.getLocation());
  }
  while (true) {
    if (Tok.is(tok::kw_typename) || Tok.is(tok::kw_class)) {
      bool IsTypename = Tok.is(tok::kw_typename);
      SourceLocation KeyLoc = ConsumeToken();
      // optional ellipsis
      if (Tok.is(tok::ellipsis))
        ConsumeToken();
      IdentifierInfo *II = nullptr;
      SourceLocation IdLoc;
      if (Tok.is(tok::identifier)) {
        II = Tok.getIdentifierInfo();
        IdLoc = ConsumeToken();
      }
      // optional default
      if (Tok.is(tok::equal)) {
        ConsumeToken();
        // skip type-id simply
        if (Tok.is(tok::identifier) || false) {
          // best-effort consume a simple type name
          while (Tok.is(tok::identifier) || Tok.is(tok::coloncolon) ||
                 Tok.is(tok::kw_const) || Tok.is(tok::kw_volatile) ||
                 Tok.is(tok::star) || Tok.is(tok::amp) || Tok.is(tok::ampamp))
            ConsumeToken();
        }
      }
      NamedDecl *P = Actions.OnTypeParameter(getCurScope(), IsTypename, KeyLoc,
                                             Depth, Params.size(), II, IdLoc);
      if (P)
        Params.push_back(P);
    } else if (Tok.is(tok::kw_template)) {
      // template-template-parameter: template<...> class Name
      SourceLocation TmpLoc = ConsumeToken();
      TemplateParameterList *Nested = nullptr;
      if (Tok.is(tok::less)) {
        ConsumeToken();
        Nested = ParseTemplateParameterList(Depth + 1);
        if (Tok.is(tok::greater))
          ConsumeToken();
      }
      if (Tok.is(tok::kw_class) || Tok.is(tok::kw_typename))
        ConsumeToken();
      IdentifierInfo *II = nullptr;
      SourceLocation IdLoc;
      if (Tok.is(tok::identifier)) {
        II = Tok.getIdentifierInfo();
        IdLoc = ConsumeToken();
      }
      NamedDecl *P = Actions.OnTemplateTemplateParameter(
          getCurScope(), TmpLoc, Depth, Params.size(), Nested, II, IdLoc);
      if (P)
        Params.push_back(P);
    } else {
      // non-type template parameter: decl-spec + declarator (simplified)
      ParsedAttributes attrs(AttrFactory);
      DeclSpec DS(AttrFactory);
      ParseDeclarationSpecifiers(DS, attrs);
      ParsingDeclarator D(*this, DS, attrs, DeclaratorContext::TypeName);
      ParseDeclarator(D);
      NamedDecl *P = Actions.OnNonTypeTemplateParameter(getCurScope(), D, Depth,
                                                        Params.size());
      if (P)
        Params.push_back(P);
    }

    if (Tok.is(tok::comma)) {
      ConsumeToken();
      continue;
    }
    break;
  }
  SourceLocation RAngleLoc = Tok.getLocation();
  return Actions.OnTemplateParameterList(Depth, SourceLocation(), LAngleLoc,
                                         Params, RAngleLoc);
}


Parser::DeclGroupPtrTy Parser::ParseModuleDecl(SourceLocation ModuleLoc) {
  // module-declaration scaffolding: module M; / export module M; / import M;
  bool IsExport = false;
  bool IsImport = false;
  // Caller may have already consumed 'export' or 'module'/'import'.
  if (Tok.is(tok::kw_export)) {
    IsExport = true;
    ConsumeToken();
  }
  if (Tok.is(tok::kw_import)) {
    IsImport = true;
    ConsumeToken();
  } else if (Tok.is(tok::kw_module)) {
    ConsumeToken();
  }
  std::string ModuleName;
  while (Tok.is(tok::identifier) || Tok.is(tok::period) || Tok.is(tok::colon) ||
         Tok.is(tok::kw_export)) {
    if (Tok.is(tok::identifier)) {
      if (!ModuleName.empty() && ModuleName.back() != '.')
        ModuleName.push_back('.');
      ModuleName += Tok.getIdentifierInfo()->getName().str();
    } else if (Tok.is(tok::period)) {
      ModuleName.push_back('.');
    } else if (Tok.is(tok::colon)) {
      ModuleName.push_back(':');
    }
    ConsumeToken();
  }
  if (Tok.is(tok::semi))
    ConsumeToken();
  return Actions.OnModuleDecl(ModuleLoc, IsExport, IsImport, ModuleName);
}
