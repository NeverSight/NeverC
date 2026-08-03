//===--- DeclTemplate.h - C++ template declarations -------------*- C++ -*-===//
#ifndef NEVERC_TREE_DECLTEMPLATE_H
#define NEVERC_TREE_DECLTEMPLATE_H

#include "neverc/Tree/Decl/Decl.h"
#include "neverc/Tree/Decl/DeclarationName.h"
#include "llvm/ADT/ArrayRef.h"

namespace neverc {

class Expr;
class TreeContext;

class TemplateParameterList {
  SourceLocation TemplateLoc;
  SourceLocation LAngleLoc;
  SourceLocation RAngleLoc;
  NamedDecl **Params = nullptr;
  unsigned NumParams = 0;
  Expr *RequiresClause = nullptr;

  TemplateParameterList() = default;

public:
  static TemplateParameterList *Create(const TreeContext &Ctx,
                                       SourceLocation TemplateLoc,
                                       SourceLocation LAngleLoc,
                                       ArrayRef<NamedDecl *> Params,
                                       SourceLocation RAngleLoc,
                                       Expr *RequiresClause = nullptr);

  unsigned size() const { return NumParams; }
  ArrayRef<NamedDecl *> asArray() const {
    return ArrayRef<NamedDecl *>(Params, NumParams);
  }
  NamedDecl *getParam(unsigned I) const {
    assert(I < NumParams);
    return Params[I];
  }
  SourceLocation getTemplateLoc() const { return TemplateLoc; }
  SourceLocation getLAngleLoc() const { return LAngleLoc; }
  SourceLocation getRAngleLoc() const { return RAngleLoc; }
  Expr *getRequiresClause() const { return RequiresClause; }
  void setRequiresClause(Expr *E) { RequiresClause = E; }
};

class TemplateDecl : public NamedDecl {
  TemplateParameterList *TemplateParams = nullptr;
  NamedDecl *TemplatedDecl = nullptr;

protected:
  TemplateDecl(Kind DK, DeclContext *DC, SourceLocation L, DeclarationName Name,
               TemplateParameterList *Params, NamedDecl *Templated)
      : NamedDecl(DK, DC, L, Name), TemplateParams(Params),
        TemplatedDecl(Templated) {}

public:
  TemplateParameterList *getTemplateParameters() const { return TemplateParams; }
  NamedDecl *getTemplatedDecl() const { return TemplatedDecl; }

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) {
    return K == TemplateTemplateParm || K == ClassTemplate ||
           K == FunctionTemplate || K == TypeAliasTemplate || K == VarTemplate ||
           K == Concept;
  }
};

class TemplateTypeParmDecl : public NamedDecl {
  bool Typename : 1;
  unsigned Depth;
  unsigned Position;

  TemplateTypeParmDecl(DeclContext *DC, SourceLocation KeyLoc,
                       SourceLocation IdLoc, IdentifierInfo *Id, bool TypenameK,
                       unsigned D, unsigned P)
      : NamedDecl(TemplateTypeParm, DC, IdLoc, DeclarationName(Id)),
        Typename(TypenameK), Depth(D), Position(P) {
    (void)KeyLoc;
  }

public:
  static TemplateTypeParmDecl *Create(const TreeContext &C, DeclContext *DC,
                                      SourceLocation KeyLoc, SourceLocation IdLoc,
                                      unsigned D, unsigned P, IdentifierInfo *Id,
                                      bool TypenameK);

  bool wasDeclaredWithTypename() const { return Typename; }
  unsigned getDepth() const { return Depth; }
  unsigned getIndex() const { return Position; }

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == TemplateTypeParm; }
};

class NonTypeTemplateParmDecl : public NamedDecl {
  QualType T;
  unsigned Depth;
  unsigned Position;

  NonTypeTemplateParmDecl(DeclContext *DC, SourceLocation IdLoc,
                          IdentifierInfo *Id, QualType T, unsigned D, unsigned P)
      : NamedDecl(NonTypeTemplateParm, DC, IdLoc, DeclarationName(Id)), T(T),
        Depth(D), Position(P) {}

public:
  static NonTypeTemplateParmDecl *Create(const TreeContext &C, DeclContext *DC,
                                         SourceLocation IdLoc, unsigned D,
                                         unsigned P, IdentifierInfo *Id,
                                         QualType T);

  QualType getType() const { return T; }
  unsigned getDepth() const { return Depth; }
  unsigned getIndex() const { return Position; }

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == NonTypeTemplateParm; }
};

class TemplateTemplateParmDecl : public TemplateDecl {
  unsigned Depth;
  unsigned Position;

  TemplateTemplateParmDecl(DeclContext *DC, SourceLocation L, unsigned D,
                           unsigned P, IdentifierInfo *Id,
                           TemplateParameterList *Params)
      : TemplateDecl(TemplateTemplateParm, DC, L, DeclarationName(Id), Params,
                     nullptr),
        Depth(D), Position(P) {}

public:
  static TemplateTemplateParmDecl *
  Create(const TreeContext &C, DeclContext *DC, SourceLocation L, unsigned D,
         unsigned P, IdentifierInfo *Id, TemplateParameterList *Params);

  unsigned getDepth() const { return Depth; }
  unsigned getIndex() const { return Position; }

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == TemplateTemplateParm; }
};

class ClassTemplateDecl : public TemplateDecl {
  ClassTemplateDecl(DeclContext *DC, SourceLocation L, DeclarationName Name,
                    TemplateParameterList *Params, NamedDecl *Templated)
      : TemplateDecl(ClassTemplate, DC, L, Name, Params, Templated) {}

public:
  static ClassTemplateDecl *Create(const TreeContext &C, DeclContext *DC,
                                   SourceLocation L, DeclarationName Name,
                                   TemplateParameterList *Params,
                                   NamedDecl *Templated);

  CXXRecordDecl *getTemplatedDecl() const {
    return cast_or_null<CXXRecordDecl>(TemplateDecl::getTemplatedDecl());
  }

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == ClassTemplate; }
};

class FunctionTemplateDecl : public TemplateDecl {
  FunctionTemplateDecl(DeclContext *DC, SourceLocation L, DeclarationName Name,
                       TemplateParameterList *Params, NamedDecl *Templated)
      : TemplateDecl(FunctionTemplate, DC, L, Name, Params, Templated) {}

public:
  static FunctionTemplateDecl *Create(const TreeContext &C, DeclContext *DC,
                                      SourceLocation L, DeclarationName Name,
                                      TemplateParameterList *Params,
                                      NamedDecl *Templated);

  FunctionDecl *getTemplatedDecl() const {
    return cast_or_null<FunctionDecl>(TemplateDecl::getTemplatedDecl());
  }

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == FunctionTemplate; }
};

class TypeAliasTemplateDecl : public TemplateDecl {
  TypeAliasTemplateDecl(DeclContext *DC, SourceLocation L, DeclarationName Name,
                        TemplateParameterList *Params, NamedDecl *Templated)
      : TemplateDecl(TypeAliasTemplate, DC, L, Name, Params, Templated) {}

public:
  static TypeAliasTemplateDecl *Create(const TreeContext &C, DeclContext *DC,
                                       SourceLocation L, DeclarationName Name,
                                       TemplateParameterList *Params,
                                       NamedDecl *Templated);

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == TypeAliasTemplate; }
};

class VarTemplateDecl : public TemplateDecl {
  VarTemplateDecl(DeclContext *DC, SourceLocation L, DeclarationName Name,
                  TemplateParameterList *Params, NamedDecl *Templated)
      : TemplateDecl(VarTemplate, DC, L, Name, Params, Templated) {}

public:
  static VarTemplateDecl *Create(const TreeContext &C, DeclContext *DC,
                                 SourceLocation L, DeclarationName Name,
                                 TemplateParameterList *Params,
                                 NamedDecl *Templated);

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == VarTemplate; }
};

class ConceptDecl : public TemplateDecl {
  Expr *ConstraintExpr = nullptr;

  ConceptDecl(DeclContext *DC, SourceLocation L, DeclarationName Name,
              TemplateParameterList *Params, Expr *Constraint)
      : TemplateDecl(Concept, DC, L, Name, Params, nullptr),
        ConstraintExpr(Constraint) {}

public:
  static ConceptDecl *Create(const TreeContext &C, DeclContext *DC,
                             SourceLocation L, DeclarationName Name,
                             TemplateParameterList *Params, Expr *Constraint);

  Expr *getConstraintExpr() const { return ConstraintExpr; }

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == Concept; }
};

class TypeAliasDecl : public NamedDecl {
  TypeSourceInfo *TInfo = nullptr;

  TypeAliasDecl(DeclContext *DC, SourceLocation StartLoc, SourceLocation IdLoc,
                IdentifierInfo *Id, TypeSourceInfo *TInfo)
      : NamedDecl(TypeAlias, DC, IdLoc, DeclarationName(Id)), TInfo(TInfo) {
    (void)StartLoc;
  }

public:
  static TypeAliasDecl *Create(const TreeContext &C, DeclContext *DC,
                               SourceLocation StartLoc, SourceLocation IdLoc,
                               IdentifierInfo *Id, TypeSourceInfo *TInfo);

  TypeSourceInfo *getTypeSourceInfo() const { return TInfo; }
  QualType getUnderlyingType() const {
    return TInfo ? TInfo->getType() : QualType();
  }

  static bool classof(const Decl *D) { return classofKind(D->getKind()); }
  static bool classofKind(Kind K) { return K == TypeAlias; }
};

} // namespace neverc

#endif
