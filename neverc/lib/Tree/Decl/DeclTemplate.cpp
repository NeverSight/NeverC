//===--- DeclTemplate.cpp - C++ template declarations ---------------------===//
#include "neverc/Tree/Decl/DeclTemplate.h"
#include "neverc/Tree/TreeContext.h"

using namespace neverc;

TemplateParameterList *
TemplateParameterList::Create(const TreeContext &Ctx, SourceLocation TemplateLoc,
                              SourceLocation LAngleLoc,
                              ArrayRef<NamedDecl *> Params,
                              SourceLocation RAngleLoc, Expr *RequiresClause) {
  auto *L = new (Ctx) TemplateParameterList();
  L->TemplateLoc = TemplateLoc;
  L->LAngleLoc = LAngleLoc;
  L->RAngleLoc = RAngleLoc;
  L->RequiresClause = RequiresClause;
  L->NumParams = static_cast<unsigned>(Params.size());
  if (!Params.empty()) {
    L->Params = new (Ctx) NamedDecl *[Params.size()];
    for (unsigned I = 0, E = L->NumParams; I != E; ++I)
      L->Params[I] = Params[I];
  }
  return L;
}

TemplateTypeParmDecl *
TemplateTypeParmDecl::Create(const TreeContext &C, DeclContext *DC,
                             SourceLocation KeyLoc, SourceLocation IdLoc,
                             unsigned D, unsigned P, IdentifierInfo *Id,
                             bool TypenameK) {
  return new (C, DC) TemplateTypeParmDecl(DC, KeyLoc, IdLoc, Id, TypenameK, D, P);
}

NonTypeTemplateParmDecl *
NonTypeTemplateParmDecl::Create(const TreeContext &C, DeclContext *DC,
                                SourceLocation IdLoc, unsigned D, unsigned P,
                                IdentifierInfo *Id, QualType T) {
  return new (C, DC) NonTypeTemplateParmDecl(DC, IdLoc, Id, T, D, P);
}

TemplateTemplateParmDecl *TemplateTemplateParmDecl::Create(
    const TreeContext &C, DeclContext *DC, SourceLocation L, unsigned D,
    unsigned P, IdentifierInfo *Id, TemplateParameterList *Params) {
  return new (C, DC) TemplateTemplateParmDecl(DC, L, D, P, Id, Params);
}

ClassTemplateDecl *ClassTemplateDecl::Create(const TreeContext &C, DeclContext *DC,
                                             SourceLocation L,
                                             DeclarationName Name,
                                             TemplateParameterList *Params,
                                             NamedDecl *Templated) {
  return new (C, DC) ClassTemplateDecl(DC, L, Name, Params, Templated);
}

FunctionTemplateDecl *FunctionTemplateDecl::Create(
    const TreeContext &C, DeclContext *DC, SourceLocation L, DeclarationName Name,
    TemplateParameterList *Params, NamedDecl *Templated) {
  return new (C, DC) FunctionTemplateDecl(DC, L, Name, Params, Templated);
}

TypeAliasTemplateDecl *TypeAliasTemplateDecl::Create(
    const TreeContext &C, DeclContext *DC, SourceLocation L, DeclarationName Name,
    TemplateParameterList *Params, NamedDecl *Templated) {
  return new (C, DC) TypeAliasTemplateDecl(DC, L, Name, Params, Templated);
}

VarTemplateDecl *VarTemplateDecl::Create(const TreeContext &C, DeclContext *DC,
                                         SourceLocation L, DeclarationName Name,
                                         TemplateParameterList *Params,
                                         NamedDecl *Templated) {
  return new (C, DC) VarTemplateDecl(DC, L, Name, Params, Templated);
}

ConceptDecl *ConceptDecl::Create(const TreeContext &C, DeclContext *DC,
                                 SourceLocation L, DeclarationName Name,
                                 TemplateParameterList *Params,
                                 Expr *Constraint) {
  return new (C, DC) ConceptDecl(DC, L, Name, Params, Constraint);
}

TypeAliasDecl *TypeAliasDecl::Create(const TreeContext &C, DeclContext *DC,
                                     SourceLocation StartLoc, SourceLocation IdLoc,
                                     IdentifierInfo *Id, TypeSourceInfo *TInfo) {
  return new (C, DC) TypeAliasDecl(DC, StartLoc, IdLoc, Id, TInfo);
}
